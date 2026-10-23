"""Nonblocking regional experiment controls; every run gets a new output directory."""
from datetime import datetime
import json
from pathlib import Path
import sys
import uuid

from PySide6.QtCore import QProcess, Signal, QSettings
from PySide6.QtWidgets import (QWidget, QFormLayout, QLineEdit, QPushButton, QHBoxLayout,
                              QSpinBox, QComboBox, QLabel, QPlainTextEdit, QFileDialog, QGroupBox, QListWidget)


def lattice_sizes(text):
    values = [int(value.strip()) for value in text.split(",")]
    if not values or min(values) < 3 or max(values) > 65 or any(a >= b for a, b in zip(values, values[1:])):
        raise ValueError("Use tamanhos crescentes entre 3 e 65, por exemplo 7,13,25")
    return values


class ProcessingPanel(QWidget):
    completed = Signal(str)
    draw_requested = Signal(str)
    regions_changed = Signal()

    def __init__(self, parent=None, settings=None):
        super().__init__(parent)
        root = Path(__file__).resolve().parents[2]
        self.settings = settings if settings is not None else QSettings(
            str(root / "out/deformable/reprocessing_inputs.ini"), QSettings.Format.IniFormat)
        self.process = QProcess(self)
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self.process.readyReadStandardOutput.connect(self.read_output)
        self.process.finished.connect(self.finished)
        self.process.errorOccurred.connect(self.process_error)
        self.output = None
        self.cancel_path = None
        self.cancelled = False
        form = QFormLayout(self)
        self.baseline, self.annotations = QLineEdit(), QLineEdit()
        for key, control, default in [
                ("baseline", self.baseline, "local_baseline_001/dicom_pilot.json"),
                ("annotations", self.annotations, "original_photo_space_002/photo_stack.json")]:
            saved = str(self.settings.value(key, ""))
            fallback = root / "out/deformable" / default
            control.setText(str(Path(saved).resolve()) if saved and Path(saved).is_file()
                            else str(fallback) if fallback.is_file() else "")
            control.setCursorPosition(0)
            control.editingFinished.connect(self.save_inputs)
        for label, control in [("Baseline global", self.baseline), ("Mascaras / foto_stack", self.annotations)]:
            row = QHBoxLayout()
            row.addWidget(control)
            button = QPushButton("...")
            button.clicked.connect(lambda checked=False, target=control: self.browse(target))
            row.addWidget(button)
            form.addRow(label, row)
        self.nodes = QLineEdit("7,13")
        self.nodes.setToolTip("Nos por eixo em cada etapa. Mais nos permitem correcoes menores, mas aumentam o risco de distorcao.")
        form.addRow("Malhas (nos/eixo)", self.nodes)
        self.resolution = QSpinBox()
        self.resolution.setRange(64, 2048)
        self.resolution.setValue(256)
        form.addRow("Processamento (px)", self.resolution)
        self.iterations = QSpinBox()
        self.iterations.setRange(1, 2000)
        self.iterations.setValue(120)
        form.addRow("Iteracoes por etapa", self.iterations)
        self.device = QComboBox()
        self.device.addItems(["cpu", "cuda"])
        form.addRow("Dispositivo", self.device)
        self.region_group = QGroupBox("Regioes adaptativas (manual)")
        self.region_group.setCheckable(True)
        self.region_group.setChecked(False)
        region_form = QFormLayout(self.region_group)
        self.region_nodes = QSpinBox()
        self.region_nodes.setRange(3, 33)
        self.region_nodes.setValue(9)
        region_form.addRow("Nos por regiao", self.region_nodes)
        row = QHBoxLayout()
        for label, mode in [("Marcar refino", "refine"), ("Marcar protecao", "protect"), ("Parar", "")]:
            button = QPushButton(label)
            button.clicked.connect(lambda checked=False, mode=mode: self.draw_requested.emit(mode))
            row.addWidget(button)
        region_form.addRow(row)
        self.region_list = QListWidget()
        self.region_list.setMaximumHeight(80)
        region_form.addRow(self.region_list)
        row = QHBoxLayout()
        remove = QPushButton("Remover selecionada")
        remove.clicked.connect(self.remove_region)
        row.addWidget(remove)
        region_form.addRow(row)
        hint = QLabel("Arraste com o botao esquerdo na foto ou preview. Vale so para esse corte. Protecao congela apenas a correcao adaptativa adicional; nao a malha geral.")
        hint.setWordWrap(True)
        region_form.addRow(hint)
        form.addRow(self.region_group)
        self.regions = []
        try:
            self.regions = json.loads(str(self.settings.value("adaptive_regions", "[]")))
        except (ValueError, TypeError):
            pass
        self.refresh_regions()
        self.region_group.toggled.connect(self.region_toggle)
        note = QLabel("Reprocessa toda a sequencia do baseline, a partir do global. Pesos corpo/osso 1:1.\nNova pasta a cada teste; resultados anteriores preservados.")
        note.setWordWrap(True)
        form.addRow(note)
        row = QHBoxLayout()
        self.start_button, self.cancel_button = QPushButton("Reprocessar"), QPushButton("Cancelar")
        self.cancel_button.setEnabled(False)
        self.start_button.clicked.connect(self.start)
        self.cancel_button.clicked.connect(self.cancel)
        row.addWidget(self.start_button)
        row.addWidget(self.cancel_button)
        form.addRow(row)
        self.status = QLabel("Pronto")
        self.status.setWordWrap(True)
        form.addRow(self.status)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(1000)
        form.addRow(self.log)

    def browse(self, target):
        path, _ = QFileDialog.getOpenFileName(self, "Entrada do experimento", filter="JSON (*.json)")
        if path:
            target.setText(path)
            self.save_inputs()

    def region_toggle(self, enabled):
        if not enabled:
            self.draw_requested.emit("")
        self.regions_changed.emit()

    def add_region(self, photo_path, rect, mode):
        from adaptive_regions import validate_regions
        region = {"photo_path": str(Path(photo_path).resolve()), "rect": rect,
                  "mode": mode, "nodes": self.region_nodes.value()}
        validate_regions([region])
        if len(self.regions) >= 64:
            self.status.setText("Limite de 64 regioes; remova uma antes de marcar outra")
            return
        self.regions.append(region)
        self.refresh_regions()

    def remove_region(self):
        index = self.region_list.currentRow()
        if 0 <= index < len(self.regions):
            self.regions.pop(index)
            self.refresh_regions()

    def refresh_regions(self):
        self.region_list.clear()
        for region in self.regions:
            label = "refino" if region["mode"] == "refine" else "protecao"
            nodes = f" ({region.get('nodes', 9)} nos)" if region["mode"] == "refine" else ""
            self.region_list.addItem(f"{Path(region['photo_path']).stem}: {label} {region['rect']}{nodes}")
        self.settings.setValue("adaptive_regions", json.dumps(self.regions))
        self.settings.sync()
        self.regions_changed.emit()

    def save_inputs(self):
        for key, control in [("baseline", self.baseline), ("annotations", self.annotations)]:
            path = Path(control.text())
            if control.text().strip() and path.is_file():
                self.settings.setValue(key, str(path.resolve()))
        self.settings.sync()

    def load_context(self, result):
        if self.running():
            return
        try:
            audit = json.loads((Path(result).parent.parent / "audit.json").read_text(encoding="utf-8"))
            parameters = audit["parameters"]
            for key, control in [("baseline", self.baseline), ("annotations", self.annotations)]:
                saved = str(self.settings.value(key, ""))
                if not saved or not Path(saved).is_file():
                    path = Path(parameters[key])
                    if not path.is_absolute():
                        path = Path(__file__).resolve().parents[2] / path
                    if path.is_file():
                        control.setText(str(path.resolve()))
                        control.setCursorPosition(0)
            settings = audit.get("settings", {})
            self.nodes.setText(",".join(map(str, settings.get("nodes", [7, 13]))))
            self.resolution.setValue(settings.get("working_size", 256))
            self.iterations.setValue(settings.get("iterations", 120))
            self.device.setCurrentText(settings.get("device", "cpu"))
            if not self.regions and audit.get("adaptive_regions"):
                self.regions = [{k: v for k, v in r.items() if k != "sequence_index"} for r in audit["adaptive_regions"]]
                self.refresh_regions()
                self.region_group.setChecked(True)
        except (OSError, ValueError, KeyError):
            pass  # Older review results can still supply inputs explicitly.

    def running(self):
        return self.process.state() != QProcess.ProcessState.NotRunning

    def start(self):
        if self.running():
            return
        try:
            nodes = lattice_sizes(self.nodes.text())
            baseline, annotations = Path(self.baseline.text()).resolve(), Path(self.annotations.text()).resolve()
            if not baseline.is_file() or not annotations.is_file():
                raise ValueError("Selecione dicom_pilot.json e photo_stack.json existentes")
            self.save_inputs()
            root = Path(__file__).resolve().parents[2]
            self.output = root / "out/deformable" / ("interactive_" + datetime.now().strftime("%Y%m%d_%H%M%S_") + uuid.uuid4().hex[:8])
            self.cancel_path = self.output.parent / (self.output.name + ".cancel")
            self.cancelled = False
            args = ["-u", str(Path(__file__).with_name("run_local.py")), "--baseline", str(baseline),
                    "--annotations", str(annotations), "--output", str(self.output), "--nodes", *map(str, nodes),
                    "--working-size", str(self.resolution.value()), "--iterations", str(self.iterations.value()),
                    "--device", self.device.currentText(), "--cancel-file", str(self.cancel_path)]
            if self.region_group.isChecked() and self.regions:
                region_path = self.output.parent / (self.output.name + ".regions.json")
                region_path.parent.mkdir(parents=True, exist_ok=True)
                region_path.write_text(json.dumps({"schema_version": 1, "coordinate_system": "original_photo_pixels",
                                                   "regions": self.regions}, indent=2), encoding="utf-8")
                args += ["--regions", str(region_path)]
            self.log.clear()
            self.log.appendPlainText("Saida: " + str(self.output))
            self.status.setText("Processando; a visualizacao continua disponivel")
            self.start_button.setEnabled(False)
            self.region_group.setEnabled(False)
            self.draw_requested.emit("")
            self.cancel_button.setEnabled(True)
            self.process.setWorkingDirectory(str(root))
            executable = Path(sys.executable)
            console = executable.with_name(executable.name.replace("pythonw", "python"))
            self.process.start(str(console if console.exists() else executable), args)
        except (ValueError, OSError) as exc:
            self.status.setText(str(exc))

    def read_output(self):
        self.log.appendPlainText(bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace").rstrip())

    def cancel(self):
        if self.running() and self.cancel_path:
            self.cancel_path.parent.mkdir(parents=True, exist_ok=True)
            self.cancel_path.touch()
            self.cancelled = True
            self.cancel_button.setEnabled(False)
            self.status.setText("Cancelamento solicitado; aguardando ponto seguro e auditoria")

    def process_error(self, error):
        self.status.setText("Falha no processo: " + self.process.errorString())
        if error == QProcess.ProcessError.FailedToStart:
            self.start_button.setEnabled(True)
            self.region_group.setEnabled(True)
            self.cancel_button.setEnabled(False)

    def finished(self, code, exit_status):
        self.read_output()
        self.start_button.setEnabled(True)
        self.region_group.setEnabled(True)
        self.cancel_button.setEnabled(False)
        if self.cancel_path:
            self.cancel_path.unlink(missing_ok=True)
        if code == 0 and exit_status == QProcess.ExitStatus.NormalExit and not self.cancelled:
            self.status.setText("Concluido: " + str(self.output))
            self.completed.emit(str(self.output))
        else:
            self.status.setText("Cancelado; resultado anterior mantido" if self.cancelled else "Falha; consulte o log e audit.json")
