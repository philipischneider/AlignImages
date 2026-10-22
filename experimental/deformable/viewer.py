"""Interactive desktop review of processed slices, independent of AlignImages."""
import argparse
from functools import lru_cache
import json
from pathlib import Path
import sys

import cv2
import numpy as np
from PySide6.QtCore import Qt, Signal, QTimer, QSize, QRectF, QPointF
from PySide6.QtGui import QColor, QImage, QPainter, QPixmap, QIcon, QAction, QKeySequence, QFont, QFontDatabase
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QSplitter, QPushButton, QLabel, QCheckBox, QComboBox, QSlider, QSpinBox, QDoubleSpinBox,
    QDockWidget, QGroupBox, QFormLayout, QFileDialog, QMessageBox, QTableWidget,
    QTableWidgetItem, QAbstractItemView, QPlainTextEdit)

from review_model import Source, compose, discover, edges, mutual_information, validated_matrix, warp
from prepare import orient


def pixmap(image):
    rgb = np.ascontiguousarray(cv2.cvtColor(image, cv2.COLOR_BGR2RGB))
    return QPixmap.fromImage(QImage(rgb.data, rgb.shape[1], rgb.shape[0], rgb.strides[0], QImage.Format.Format_RGB888).copy())


class Canvas(QWidget):
    changed = Signal(float, float, float)

    def __init__(self):
        super().__init__()
        self.image = QPixmap()
        self.zoom, self.pan_x, self.pan_y = 1., 0., 0.
        self.drag = None
        self.setMinimumSize(180, 150)
        self.setMouseTracking(True)
        self.setToolTip("Roda: zoom · botão central: arrastar · duplo clique central: ajustar")

    def fit_scale(self):
        return min(self.width() / max(1, self.image.width()), self.height() / max(1, self.image.height()))

    def set_image(self, image):
        self.image = pixmap(image)
        self.update()

    def state(self, zoom, x, y, emit=True):
        self.zoom, self.pan_x, self.pan_y = max(.05, min(20., zoom)), x, y
        self.update()
        if emit:
            self.changed.emit(self.zoom, x, y)

    def fit(self):
        self.state(1., 0., 0.)

    def one_to_one(self):
        self.state(1 / max(.001, self.fit_scale()), 0., 0.)

    def paintEvent(self, event):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor("#1c1f26"))
        if self.image.isNull():
            p.setPen(QColor("#b0bac8"))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "Abra uma pasta de resultados")
            return
        factor = self.fit_scale() * self.zoom
        width, height = self.image.width() * factor, self.image.height() * factor
        rect = QRectF((self.width() - width) / 2 + self.pan_x * self.width(),
                      (self.height() - height) / 2 + self.pan_y * self.height(), width, height)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        p.drawPixmap(rect, self.image, QRectF(self.image.rect()))
        p.setPen(QColor("#b0bac8"))
        p.drawText(10, 20, f"Zoom {self.zoom:.2f}× · {self.image.width()} × {self.image.height()}")

    def wheelEvent(self, event):
        self.state(self.zoom * (1.1 if event.angleDelta().y() > 0 else 1 / 1.1), self.pan_x, self.pan_y)
        event.accept()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self.drag = event.position()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)

    def mouseMoveEvent(self, event):
        if self.drag is not None:
            delta = event.position() - self.drag
            self.drag = event.position()
            self.state(self.zoom, self.pan_x + delta.x() / self.width(), self.pan_y + delta.y() / self.height())

    def mouseReleaseEvent(self, event):
        self.drag = None
        self.unsetCursor()

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self.fit()


class Panel(QWidget):
    def __init__(self, title):
        super().__init__()
        layout = QVBoxLayout(self)
        layout.addWidget(QLabel(title))
        controls = QHBoxLayout()
        self.canvas = Canvas()
        for text, callback in [("Fit", self.canvas.fit), ("1:1", self.canvas.one_to_one), ("Reset", self.canvas.fit)]:
            button = QPushButton(text)
            button.clicked.connect(callback)
            controls.addWidget(button)
        self.contours = QCheckBox("Contornos")
        controls.addWidget(self.contours)
        layout.addLayout(controls)
        layout.addWidget(self.canvas, 1)


class ReviewWindow(QMainWindow):
    def __init__(self, pattern=None):
        super().__init__()
        self.setWindowTitle("Deformable Lab — revisão de cortes")
        self.resize(1500, 960)
        self.entries, self.sources = [], None
        self.index = -1
        self.pending = QTimer(self)
        self.pending.setSingleShot(True)
        self.pending.timeout.connect(self.render)
        self.player = QTimer(self)
        self.player.timeout.connect(lambda: self.navigate(1))
        self.thumb_timer = QTimer(self)
        self.thumb_timer.setInterval(35)
        self.thumb_timer.timeout.connect(self.populate_thumbnail)
        self.thumb_queue = []
        central = QWidget()
        self.setCentralWidget(central)
        outer = QVBoxLayout(central)
        toolbar = QHBoxLayout()
        for text, callback in [("Abrir pasta", self.open_folder), ("Abrir JSON", self.open_file),
                               ("Anterior", lambda: self.navigate(-1)), ("Próximo", lambda: self.navigate(1))]:
            button = QPushButton(text)
            button.clicked.connect(callback)
            toolbar.addWidget(button)
        self.play = QPushButton("Reproduzir")
        self.play.setCheckable(True)
        self.play.toggled.connect(self.toggle_play)
        toolbar.addWidget(self.play)
        self.fps = QSpinBox()
        self.fps.setRange(1, 30)
        self.fps.setValue(5)
        self.fps.setSuffix(" fps")
        self.fps.valueChanged.connect(lambda value: self.player.setInterval(round(1000 / value)))
        toolbar.addWidget(self.fps)
        self.slice = QComboBox()
        self.slice.setMinimumContentsLength(18)
        self.slice.setSizeAdjustPolicy(QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon)
        self.slice.currentIndexChanged.connect(self.select)
        toolbar.addWidget(self.slice, 1)
        self.alignment = QCheckBox("Aplicar alinhamento")
        self.alignment.setChecked(True)
        self.sync = QCheckBox("Sincronizar zoom/pan")
        self.sync.setChecked(True)
        toolbar.addWidget(self.alignment)
        toolbar.addWidget(self.sync)
        outer.addLayout(toolbar)
        compositing = QHBoxLayout()
        self.mode = QComboBox()
        for label, value in [("Blend", "blend"), ("Checkerboard", "checkerboard"), ("Difference", "difference"), ("Multiply", "multiply")]:
            self.mode.addItem(label, value)
        compositing.addWidget(self.mode)
        compositing.addWidget(QLabel("Opacidade móvel"))
        self.alpha = QSlider(Qt.Orientation.Horizontal)
        self.alpha.setRange(0, 100)
        self.alpha.setValue(50)
        compositing.addWidget(self.alpha)
        self.alpha_label = QLabel("50%")
        compositing.addWidget(self.alpha_label)
        compositing.addWidget(QLabel("Checker (px)"))
        self.checker = QSpinBox()
        self.checker.setRange(4, 128)
        self.checker.setValue(32)
        compositing.addWidget(self.checker)
        self.hypothesis = QComboBox()
        self.hypothesis.setMinimumContentsLength(18)
        self.hypothesis.setSizeAdjustPolicy(QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon)
        compositing.addWidget(QLabel("Resultado / hipótese"))
        compositing.addWidget(self.hypothesis)
        outer.addLayout(compositing)
        split = QSplitter(Qt.Orientation.Vertical)
        viewers = QSplitter(Qt.Orientation.Horizontal)
        self.panels = [Panel(title) for title in ["Referência · fotografia", "Móvel · imagem original", "Preview · móvel → referência"]]
        for panel in self.panels:
            viewers.addWidget(panel)
            panel.contours.toggled.connect(self.schedule)
            panel.canvas.changed.connect(lambda z, x, y, source=panel.canvas: self.sync_views(source, z, x, y))
        split.addWidget(viewers)
        self.timeline = QTableWidget(2, 0)
        self.timeline.setVerticalHeaderLabels(["Referência", "Móvel"])
        self.timeline.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.timeline.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectColumns)
        self.timeline.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.timeline.setIconSize(QSize(66, 66))
        self.timeline.cellClicked.connect(lambda row, column: self.slice.setCurrentIndex(column))
        self.timeline.horizontalScrollBar().valueChanged.connect(self.queue_thumbnails)
        self.timeline.verticalHeader().setDefaultSectionSize(82)
        split.addWidget(self.timeline)
        split.setSizes([650, 190])
        outer.addWidget(split, 1)
        self.dock = QDockWidget("Exibição e diagnóstico", self)
        self.dock.setMinimumWidth(270)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.dock)
        side = QWidget()
        side_layout = QVBoxLayout(side)
        self.mi = QLabel("Informação mútua: —")
        side_layout.addWidget(self.mi)
        self.window_controls = []
        for name in ["Referência", "Móvel"]:
            group = QGroupBox(f"{name} · janela / nível")
            form = QFormLayout(group)
            center, width = QDoubleSpinBox(), QDoubleSpinBox()
            center.setRange(-100000, 100000)
            width.setRange(1, 100000)
            form.addRow("Centro", center)
            form.addRow("Largura", width)
            note = QLabel("PNG: ajuste de exibição em 0–255; sem HU")
            note.setWordWrap(True)
            form.addRow(note)
            row = QHBoxLayout()
            for label, c, w in [("Tecidos", 40, 400), ("Pulmão", -600, 1500), ("Osso", 400, 1800), ("Reset", None, None)]:
                button = QPushButton(label)
                button.clicked.connect(lambda checked=False, i=len(self.window_controls), c=c, w=w: self.preset(i, c, w))
                row.addWidget(button)
                button.setEnabled(label == "Reset")
            form.addRow(row)
            side_layout.addWidget(group)
            self.window_controls.append((center, width, note, row))
            center.valueChanged.connect(self.schedule)
            width.valueChanged.connect(self.schedule)
        self.show_masks = QCheckBox("Mostrar máscaras ósseas disponíveis")
        self.show_masks.setChecked(True)
        self.show_masks.toggled.connect(self.schedule)
        side_layout.addWidget(self.show_masks)
        self.details = QPlainTextEdit()
        self.details.setReadOnly(True)
        side_layout.addWidget(self.details, 1)
        self.dock.setWidget(side)
        view_menu = self.menuBar().addMenu("Visualização")
        view_menu.addAction(self.dock.toggleViewAction())
        scale_menu = view_menu.addMenu("Escala da interface")
        for percent in [100, 125, 150, 175, 200]:
            action = scale_menu.addAction(f"{percent}%")
            action.triggered.connect(lambda checked=False, value=percent: self.ui_scale(value))
        for shortcut, callback in [("Left", lambda: self.navigate(-1)), ("Right", lambda: self.navigate(1)),
                                   ("Space", lambda: self.play.setChecked(not self.play.isChecked())),
                                   ("F", lambda: self.panels[0].canvas.fit())]:
            action = QAction(self)
            action.setShortcut(QKeySequence(shortcut))
            action.triggered.connect(callback)
            self.addAction(action)
        for widget in [self.mode, self.hypothesis]:
            widget.currentIndexChanged.connect(self.schedule)
        for widget in [self.alpha, self.checker]:
            widget.valueChanged.connect(self.schedule)
        self.alignment.toggled.connect(self.schedule)
        self.sync.toggled.connect(self.enable_sync)
        if pattern:
            self.open_pattern(pattern)

    def ui_scale(self, percent):
        font = QApplication.font()
        font.setPointSizeF(9 * percent / 100)
        QApplication.setFont(font)

    def schedule(self, *args):
        self.pending.start(35)

    def sync_views(self, source, z, x, y):
        if self.sync.isChecked():
            for panel in self.panels:
                if panel.canvas is not source:
                    panel.canvas.state(z, x, y, emit=False)

    def enable_sync(self, enabled):
        if enabled:
            canvas = self.panels[0].canvas
            self.sync_views(canvas, canvas.zoom, canvas.pan_x, canvas.pan_y)

    def open_folder(self):
        path = QFileDialog.getExistingDirectory(self, "Pasta de resultados processados")
        if path:
            self.open_pattern(path)

    def open_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "Resultado ou manifesto", filter="JSON (*.json)")
        if path:
            self.open_pattern(path)

    def open_pattern(self, pattern):
        entries, errors = discover(pattern)
        if not entries:
            QMessageBox.warning(self, "Resultados", "Nenhum corte processado encontrado.\n" + "\n".join(errors[:5]))
            return
        self.play.setChecked(False)
        self.entries = entries
        source_label = "DICOM original" if all(e.moving.suffix.lower() == ".dcm" for e in entries) else "fontes PNG / resultados anteriores"
        self.setWindowTitle(f"Deformable Lab — {source_label}")
        self.slice.blockSignals(True)
        self.slice.clear()
        self.slice.addItems([e.label for e in entries])
        self.slice.blockSignals(False)
        self.timeline.setColumnCount(len(entries))
        self.timeline.setHorizontalHeaderLabels([e.fixed.stem for e in entries])
        for column, entry in enumerate(entries):
            self.timeline.setColumnWidth(column, 105)
            for row, path in enumerate([entry.fixed, entry.moving]):
                item = QTableWidgetItem(path.stem)
                item.setToolTip(str(path))
                self.timeline.setItem(row, column, item)
        self.select(0)
        QTimer.singleShot(100, self.queue_thumbnails)
        self.statusBar().showMessage(f"{len(entries)} cortes · roda: zoom · botão central: pan · setas: navegar" + (f" · {len(errors)} arquivos ignorados" if errors else ""))

    def navigate(self, delta):
        if self.entries:
            self.slice.setCurrentIndex((self.slice.currentIndex() + delta) % len(self.entries))

    def queue_thumbnails(self, *args):
        if not self.entries:
            return
        first = max(0, self.timeline.columnAt(0))
        last = self.timeline.columnAt(self.timeline.viewport().width() - 1)
        if last < 0:
            last = len(self.entries) - 1
        self.thumb_queue = [(row, column) for column in range(first, min(len(self.entries), last + 1))
                            for row in range(2) if self.timeline.item(row, column) is not None
                            and self.timeline.item(row, column).icon().isNull()]
        if self.thumb_queue:
            self.thumb_timer.start()

    def populate_thumbnail(self):
        if not self.thumb_queue:
            self.thumb_timer.stop()
            return
        row, column = self.thumb_queue.pop(0)
        if column >= len(self.entries):
            return
        entry = self.entries[column]
        path = entry.fixed if row == 0 else entry.moving
        orientation = entry.fixed_orientation if row == 0 else entry.moving_orientation
        try:
            image = self.source(str(path), json.dumps(orientation)).display()
            image = cv2.resize(image, (max(1, round(66 * image.shape[1] / image.shape[0])), 66))
            self.timeline.item(row, column).setIcon(QIcon(pixmap(image)))
        except Exception:
            self.timeline.item(row, column).setToolTip(f"Miniatura indisponível: {path}")

    def toggle_play(self, checked):
        self.play.setText("Pausar" if checked else "Reproduzir")
        if checked and self.entries:
            self.player.start(round(1000 / self.fps.value()))
        else:
            self.player.stop()

    @lru_cache(maxsize=8)
    def source(self, path, orientation_json):
        return Source(path, json.loads(orientation_json))

    def select(self, index):
        if not 0 <= index < len(self.entries):
            return
        self.index = index
        entry = self.entries[index]
        try:
            self.sources = [self.source(str(entry.fixed), json.dumps(entry.fixed_orientation)),
                            self.source(str(entry.moving), json.dumps(entry.moving_orientation))]
            for i, source in enumerate(self.sources):
                center, width, note, row = self.window_controls[i]
                for control, value in [(center, source.default_center), (width, source.default_width)]:
                    control.blockSignals(True)
                    control.setValue(value)
                    control.blockSignals(False)
                note.setText("DICOM: valores reescalonados da fonte" if source.dicom else "PNG: ajuste em 0–255; não representa HU")
                for j in range(row.count() - 1):
                    row.itemAt(j).widget().setEnabled(source.dicom)
                thumb = self.sources[i].display()
                thumb = cv2.resize(thumb, (max(1, round(66 * thumb.shape[1] / thumb.shape[0])), 66))
                self.timeline.item(i, index).setIcon(QIcon(pixmap(thumb)))
            self.hypothesis.blockSignals(True)
            self.hypothesis.clear()
            self.hypothesis.addItem("Resultado selecionado", entry.transform)
            for i, candidate in enumerate(entry.candidates):
                self.hypothesis.addItem(f"Hipótese {i + 1} · custo {candidate['objective']:.4f}", validated_matrix(candidate["forward_matrix_3x3"]))
            self.hypothesis.blockSignals(False)
            self.timeline.selectColumn(index)
            self.timeline.scrollToItem(self.timeline.item(0, index))
            self.render()
        except Exception as exc:
            self.sources = None
            self.play.setChecked(False)
            self.details.setPlainText(f"Falha no corte {entry.label}: {exc}")
            for panel in self.panels:
                panel.canvas.image = QPixmap()
                panel.canvas.update()

    def preset(self, index, center, width):
        if self.sources:
            source = self.sources[index]
            c, w, _, _ = self.window_controls[index]
            c.setValue(source.default_center if center is None else center)
            w.setValue(source.default_width if width is None else width)

    def render(self):
        if not self.sources or self.index < 0:
            return
        try:
            entry = self.entries[self.index]
            fixed, moving = [source.display(controls[0].value(), controls[1].value()) for source, controls in zip(self.sources, self.window_controls)]
            transform = self.hypothesis.currentData()
            if transform is None:
                transform = entry.transform
            alignment = self.alignment.isChecked()
            output = compose(fixed, moving, transform, self.mode.currentData(), self.alpha.value() / 100, self.checker.value(), alignment)
            images = [fixed.copy(), moving.copy(), output]
            edge_a, edge_b = edges(fixed), edges(moving)
            if self.panels[0].contours.isChecked():
                images[0][edge_a != 0] = (80, 230, 80)
            if self.panels[1].contours.isChecked():
                images[1][edge_b != 0] = (220, 80, 220)
            if self.panels[2].contours.isChecked():
                eb = warp(edge_b, transform, fixed.shape, True) if alignment else cv2.resize(edge_b, (fixed.shape[1], fixed.shape[0]), interpolation=cv2.INTER_NEAREST)
                output[edge_a != 0] = (80, 230, 80)
                output[eb != 0] = (220, 80, 220)
            if self.show_masks.isChecked():
                for key, mask_path in entry.masks.items():
                    raw = cv2.imdecode(np.fromfile(str(mask_path), np.uint8), cv2.IMREAD_GRAYSCALE)
                    if raw is None:
                        raise ValueError(f"Cannot decode mask {mask_path}")
                    is_fixed = key == "fixed_bone"
                    raw = orient(raw, entry.fixed_orientation if is_fixed else entry.moving_orientation)
                    reference = fixed if is_fixed else moving
                    if raw.shape != reference.shape[:2]:
                        raise ValueError("Mask does not share the image grid")
                    contours, _ = cv2.findContours((raw != 0).astype(np.uint8), cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
                    outline = np.zeros_like(raw)
                    cv2.drawContours(outline, contours, -1, 255, 1)
                    color = (0, 230, 255) if is_fixed else (255, 180, 40)
                    images[0 if is_fixed else 1][outline != 0] = color
                    if not is_fixed:
                        outline = warp(outline, transform, fixed.shape, True) if alignment else cv2.resize(outline, (fixed.shape[1], fixed.shape[0]), interpolation=cv2.INTER_NEAREST)
                    output[outline != 0] = color
            for panel, image in zip(self.panels, images):
                panel.canvas.set_image(image)
            self.alpha_label.setText(f"{self.alpha.value()}%")
            self.alpha.setEnabled(self.mode.currentData() == "blend")
            self.checker.setEnabled(self.mode.currentData() == "checkerboard")
            score = mutual_information(self.sources[0].display(), self.sources[1].display(), transform) if alignment else None
            self.mi.setText(f"Informação mútua: {score:.3f}" if score is not None else "Informação mútua: — (alinhamento desligado)")
            self.details.setPlainText(f"{entry.label}\n\nReferência: {entry.fixed}\nMóvel: {entry.moving}\nResultado: {entry.origin}\n\nDice corporal original: {entry.score}\nA métrica não valida anatomia interna.\n\n" + "\n".join(entry.warnings) + "\n\nMatriz visualizada:\n" + np.array2string(transform, precision=5))
        except Exception as exc:
            self.player.stop()
            self.details.setPlainText(f"Erro de visualização: {exc}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", default=str(Path(__file__).resolve().parents[2] / "out/deformable/dicom_full_photo_pilot_002"))
    parser.add_argument("--smoke-screenshot", help="Save a screenshot and quit, for UI verification")
    args = parser.parse_args()
    app = QApplication(sys.argv[:1])
    font_path = Path("C:/Windows/Fonts/segoeui.ttf")
    if font_path.is_file():
        QFontDatabase.addApplicationFont(str(font_path))
    app.setFont(QFont("Segoe UI", 9))
    app.setStyle("Fusion")
    app.setStyleSheet("QMainWindow,QWidget{background:#252b34;color:#e7ecf3} QPushButton,QComboBox,QSpinBox,QDoubleSpinBox{background:#384252;padding:5px;border:1px solid #546176;border-radius:3px} QPushButton:checked{background:#396db0} QTableWidget{background:#1b2028} QHeaderView::section{background:#384252;color:#e7ecf3} QToolTip{background:#384252;color:white}")
    window = ReviewWindow(args.results)
    window.show()
    if args.smoke_screenshot:
        def save():
            window.grab().save(args.smoke_screenshot)
            app.quit()
        QTimer.singleShot(800, save)
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
