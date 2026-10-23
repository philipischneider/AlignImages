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

from review_model import Source, compose, discover, edges, mutual_information, validated_matrix, warp, local_field, aligned_display
from prepare import orient
from processing_panel import ProcessingPanel


def pixmap(image):
    rgb = np.ascontiguousarray(cv2.cvtColor(image, cv2.COLOR_BGR2RGB))
    return QPixmap.fromImage(QImage(rgb.data, rgb.shape[1], rgb.shape[0], rgb.strides[0], QImage.Format.Format_RGB888).copy())


class Canvas(QWidget):
    changed = Signal(float, float, float)
    region_selected = Signal(object)

    def __init__(self):
        super().__init__()
        self.image = QPixmap()
        self.zoom, self.pan_x, self.pan_y = 1., 0., 0.
        self.drag = None
        self.selection_mode = ""
        self.selection_start = None
        self.selection_end = None
        self.region_overlays = []
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
        overlays = list(self.region_overlays)
        if self.selection_start is not None and self.selection_end is not None:
            x, y = self.selection_start
            xx, yy = self.selection_end
            overlays.append({"rect": [min(x, xx), min(y, yy), abs(xx - x), abs(yy - y)], "mode": self.selection_mode})
        for region in overlays:
            x, y, w, h = region["rect"]
            p.setPen(QColor("#ffbd45" if region["mode"] == "refine" else "#68e394"))
            p.drawRect(QRectF(rect.left() + x * factor, rect.top() + y * factor, w * factor, h * factor))
        p.setPen(QColor("#b0bac8"))
        p.drawText(10, 20, f"Zoom {self.zoom:.2f}× · {self.image.width()} × {self.image.height()}")

    def wheelEvent(self, event):
        self.state(self.zoom * (1.1 if event.angleDelta().y() > 0 else 1 / 1.1), self.pan_x, self.pan_y)
        event.accept()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and self.selection_mode and not self.image.isNull():
            self.selection_start = self.image_position(event.position())
            self.selection_end = self.selection_start
            event.accept()
            return
        if event.button() == Qt.MouseButton.MiddleButton:
            self.drag = event.position()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)

    def mouseMoveEvent(self, event):
        if self.selection_start is not None:
            self.selection_end = self.image_position(event.position())
            self.update()
            return
        if self.drag is not None:
            delta = event.position() - self.drag
            self.drag = event.position()
            self.state(self.zoom, self.pan_x + delta.x() / self.width(), self.pan_y + delta.y() / self.height())

    def mouseReleaseEvent(self, event):
        if self.selection_start is not None and event.button() == Qt.MouseButton.LeftButton:
            end = self.image_position(event.position())
            start = self.selection_start
            self.selection_start = None
            self.selection_end = None
            left, top = min(start[0], end[0]), min(start[1], end[1])
            width, height = abs(start[0] - end[0]), abs(start[1] - end[1])
            if width >= 8 and height >= 8:
                self.region_selected.emit([left, top, width, height])
            self.update()
            return
        self.drag = None
        self.unsetCursor()

    def image_position(self, position):
        factor = self.fit_scale() * self.zoom
        left = (self.width() - self.image.width() * factor) / 2 + self.pan_x * self.width()
        top = (self.height() - self.image.height() * factor) / 2 + self.pan_y * self.height()
        return (int(np.clip(round((position.x() - left) / factor), 0, self.image.width() - 1)),
                int(np.clip(round((position.y() - top) / factor), 0, self.image.height() - 1)))

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
        self.local_enabled = QCheckBox("Aplicar refino regional (candidato)")
        self.local_enabled.setChecked(True)
        self.local_enabled.toggled.connect(self.schedule)
        self.show_mesh = QCheckBox("Mostrar malha de deformação")
        self.show_mesh.toggled.connect(self.schedule)
        side_layout.addWidget(self.local_enabled)
        side_layout.addWidget(self.show_mesh)
        side_layout.addWidget(QLabel("Etapa da deformacao"))
        self.stage = QComboBox()
        self.stage.currentIndexChanged.connect(self.schedule)
        side_layout.addWidget(self.stage)
        self.stage_notice = QLabel("Etapa intermediaria: antes dos criterios finais; nao validada.")
        self.stage_notice.setWordWrap(True)
        self.stage_notice.hide()
        side_layout.addWidget(self.stage_notice)
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
        self.processing_dock = QDockWidget("Reprocessamento / malha", self)
        self.processing = ProcessingPanel(self)
        self.processing.completed.connect(self.open_pattern)
        self.processing.draw_requested.connect(self.region_drawing)
        self.processing.regions_changed.connect(self.update_region_overlays)
        self.region_mode = ""
        for i in [0, 2]:
            self.panels[i].canvas.region_selected.connect(self.add_adaptive_region)
        self.processing_dock.setWidget(self.processing)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.processing_dock)
        self.tabifyDockWidget(self.dock, self.processing_dock)
        self.dock.raise_()
        view_menu = self.menuBar().addMenu("Visualização")
        view_menu.addAction(self.dock.toggleViewAction())
        view_menu.addAction(self.processing_dock.toggleViewAction())
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

    def region_drawing(self, mode):
        self.region_mode = mode
        for i in [0, 2]:
            canvas = self.panels[i].canvas
            canvas.selection_mode = mode
            canvas.selection_start = None
            canvas.selection_end = None
            canvas.setCursor(Qt.CursorShape.CrossCursor if mode else Qt.CursorShape.ArrowCursor)
        self.statusBar().showMessage("Arraste na foto ou preview para marcar " + mode if mode else "Marcacao encerrada")

    def add_adaptive_region(self, rect):
        if self.entries and self.region_mode and not self.processing.running():
            self.processing.add_region(self.entries[self.index].fixed, rect, self.region_mode)

    def update_region_overlays(self):
        regions = []
        if self.entries and self.processing.region_group.isChecked():
            key = str(self.entries[self.index].fixed.resolve())
            regions = [r for r in self.processing.regions if str(Path(r["photo_path"]).resolve()) == key]
        for i in [0, 2]:
            self.panels[i].canvas.region_overlays = regions
            self.panels[i].canvas.update()

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
        self.processing.load_context(entries[0].origin)
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

    def closeEvent(self, event):
        if self.processing.running():
            self.processing.cancel()
            self.statusBar().showMessage("Aguarde o cancelamento terminar antes de fechar.")
            event.ignore()
            return
        super().closeEvent(event)

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
        self.update_region_overlays()
        entry = self.entries[index]
        try:
            previous_stage = self.stage.currentText()
            self.stage.blockSignals(True)
            self.stage.clear()
            self.stage.addItem("Final (apos criterios)", entry.local)
            for stage in entry.local.get("stages", []):
                count = stage["count"]
                label = f"Etapa {stage['level'] + 1}: adaptativa" if stage.get("adaptive") else f"Etapa {stage['level'] + 1}: {count} x {count}"
                self.stage.addItem(label, stage)
            stage_index = self.stage.findText(previous_stage)
            self.stage.setCurrentIndex(max(0, stage_index))
            self.stage.setEnabled(bool(entry.local.get("stages")))
            self.stage.setToolTip("Etapas anteriores nao foram salvas neste resultado; reprocesse para gera-las."
                                  if not entry.local.get("stages") else "Snapshots reais ao terminar cada etapa, antes dos criterios finais.")
            self.stage.blockSignals(False)
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
            selected_local = self.stage.currentData() or entry.local
            self.stage_notice.setVisible(selected_local.get("status") == "intermediate_unvalidated")
            if entry.local:
                self.setWindowTitle("Deformable Lab | DICOM original | refino regional")
            fixed, moving = [source.display(controls[0].value(), controls[1].value()) for source, controls in zip(self.sources, self.window_controls)]
            transform = self.hypothesis.currentData()
            if transform is None:
                transform = entry.transform
            alignment = self.alignment.isChecked()
            available = selected_local.get("status") in {"candidate", "intermediate_unvalidated"} and self.hypothesis.currentIndex() == 0
            self.local_enabled.setEnabled(available and alignment)
            regional = local_field(selected_local["field_path"], selected_local.get("sha256")) if available and alignment and self.local_enabled.isChecked() else None
            self.show_mesh.setEnabled(regional is not None)
            displayed_aligned = aligned_display(self.sources[1], transform, fixed.shape,
                                                self.window_controls[1][0].value(), self.window_controls[1][1].value(), regional) if alignment else None
            output = compose(fixed, moving, transform, self.mode.currentData(), self.alpha.value() / 100,
                             self.checker.value(), alignment, regional, displayed_aligned)
            images = [fixed.copy(), moving.copy(), output]
            edge_a, edge_b = edges(fixed), edges(moving)
            if self.panels[0].contours.isChecked():
                images[0][edge_a != 0] = (80, 230, 80)
            if self.panels[1].contours.isChecked():
                images[1][edge_b != 0] = (220, 80, 220)
            if self.panels[2].contours.isChecked():
                eb = warp(edge_b, transform, fixed.shape, True, regional) if alignment else cv2.resize(edge_b, (fixed.shape[1], fixed.shape[0]), interpolation=cv2.INTER_NEAREST)
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
                        outline = warp(outline, transform, fixed.shape, True, regional) if alignment else cv2.resize(outline, (fixed.shape[1], fixed.shape[0]), interpolation=cv2.INTER_NEAREST)
                    output[outline != 0] = color
            if regional is not None and self.show_mesh.isChecked():
                field, roi = regional
                left, top, width, height = roi
                grid = np.zeros(fixed.shape[:2], np.uint8)
                count = selected_local.get("settings", {}).get("nodes", [13])[-1]
                for x in np.linspace(left, left + width - 1, count).round().astype(int):
                    cv2.line(grid, (x, top), (x, top + height - 1), 255, 1)
                for y in np.linspace(top, top + height - 1, count).round().astype(int):
                    cv2.line(grid, (left, y), (left + width - 1, y), 255, 1)
                active_regions = selected_local.get("adaptive_regions", []) if selected_local.get("adaptive") or selected_local.get("adaptive_step_scale", 0) > 0 else []
                for region in active_regions:
                    if region["mode"] != "refine":
                        continue
                    x, y, w, h = map(int, region["rect"])
                    right, bottom = min(x + w - 1, left + width - 1), min(y + h - 1, top + height - 1)
                    x, y = max(x, left), max(y, top)
                    w, h = right - x + 1, bottom - y + 1
                    n = int(region.get("nodes", 9))
                    for xx in np.linspace(x, x + w - 1, n).round().astype(int):
                        cv2.line(grid, (xx, y), (xx, y + h - 1), 255, 1)
                    for yy in np.linspace(y, y + h - 1, n).round().astype(int):
                        cv2.line(grid, (x, yy), (x + w - 1, yy), 255, 1)
                output[warp(grid, np.eye(3), fixed.shape, True, regional) != 0] = (255, 190, 60)
            for panel, image in zip(self.panels, images):
                panel.canvas.set_image(image)
            self.alpha_label.setText(f"{self.alpha.value()}%")
            self.alpha.setEnabled(self.mode.currentData() == "blend")
            self.checker.setEnabled(self.mode.currentData() == "checkerboard")
            score = mutual_information(self.sources[0].display(), self.sources[1].display(), transform, regional) if alignment else None
            self.mi.setText(f"Informação mútua: {score:.3f}" if score is not None else "Informação mútua: — (alinhamento desligado)")
            shown_body = selected_local.get("metrics", {}).get("body_after" if regional is not None else "body_before", entry.score)
            self.details.setPlainText(f"{entry.label}\n\nReferência: {entry.fixed}\nMóvel: {entry.moving}\nResultado: {entry.origin}\n\nDice corporal: {shown_body}\nA métrica não valida anatomia interna.\n\n" + "\n".join(entry.warnings) + "\n\nMatriz global visualizada:\n" + np.array2string(transform, precision=5))
            if entry.local:
                metrics = selected_local.get("metrics", {})
                self.details.appendPlainText("\n" + self.stage.currentText() + "\nRefino regional: " + selected_local.get("status", "") +
                    (" (ativado)" if regional is not None else " (desativado)") + "\n" + json.dumps(metrics, indent=2, ensure_ascii=False) +
                    "\nA matriz acima é apenas a componente global; o campo residual compõe o mapeamento completo.")
                if selected_local.get("status") == "intermediate_unvalidated":
                    self.details.appendPlainText("ETAPA INTERMEDIARIA: antes dos criterios finais; pode conter piora local ou distorcao. Nao e alinhamento aprovado.")
                if "adaptive_step_scale" in selected_local:
                    self.details.appendPlainText(f"Passo geral: {selected_local['base_step_scale']}; incremento adaptativo: {selected_local['adaptive_step_scale']}")
        except Exception as exc:
            self.player.stop()
            self.details.setPlainText(f"Erro de visualização: {exc}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", default=str(Path(__file__).resolve().parents[2] / "out/deformable/regional_stages_001"))
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
