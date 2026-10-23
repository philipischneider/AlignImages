import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from PySide6.QtCore import QProcess, QCoreApplication, QEvent, QSettings
from PySide6.QtWidgets import QApplication
from processing_panel import ProcessingPanel, lattice_sizes


class ProcessingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def tearDown(self):
        for widget in self.app.topLevelWidgets():
            if isinstance(widget, ProcessingPanel):
                widget.deleteLater()
        QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
        self.settings_temp.cleanup()

    def setUp(self):
        self.settings_temp = tempfile.TemporaryDirectory()
        self.settings = QSettings(str(Path(self.settings_temp.name) / "inputs.ini"), QSettings.Format.IniFormat)

    def test_lattice_validation(self):
        self.assertEqual(lattice_sizes("7,13,25"), [7, 13, 25])
        for text in ["", "13,7", "7,7", "2,13", "7,66", "abc"]:
            with self.assertRaises(ValueError):
                lattice_sizes(text)

    def test_context_command_cancel_and_preservation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "Head100").mkdir()
            baseline, annotations = root / "baseline.json", root / "annotations.json"
            baseline.write_text("{}")
            annotations.write_text("{}")
            (root / "audit.json").write_text(json.dumps({"parameters": {"baseline": str(baseline),
                "annotations": str(annotations)}, "settings": {"nodes": [7, 13], "device": "cpu"}}))
            panel = ProcessingPanel(settings=self.settings)
            panel.load_context(root / "Head100/result.json")
            self.assertEqual(panel.baseline.text(), str(baseline))
            panel.nodes.setText("7,13,25")
            with patch.object(panel.process, "start") as start:
                panel.start()
            args = start.call_args.args[1]
            self.assertEqual(args[args.index("--nodes") + 1:args.index("--working-size")], ["7", "13", "25"])
            self.assertFalse(panel.output.exists())
            panel.cancel_path = root / "cancel"
            with patch.object(panel, "running", return_value=True):
                panel.cancel()
            self.assertTrue(panel.cancel_path.exists())
            opened = []
            panel.completed.connect(opened.append)
            panel.finished(1, QProcess.ExitStatus.NormalExit)
            self.assertEqual(opened, [])
            self.assertFalse(panel.cancel_path.exists())
            self.assertEqual(baseline.read_text(), "{}")
            panel.cancelled = False
            panel.finished(0, QProcess.ExitStatus.NormalExit)
            self.assertEqual(opened, [str(panel.output)])
            panel.close()

    def test_invalid_inputs_do_not_launch_process(self):
        panel = ProcessingPanel(settings=self.settings)
        panel.baseline.clear()
        panel.annotations.clear()
        with patch.object(panel.process, "start") as start:
            panel.start()
            start.assert_not_called()
        self.assertIn("Selecione", panel.status.text())
        panel.close()

    def test_last_valid_inputs_restore_without_result_audit(self):
        root = Path(self.settings_temp.name)
        baseline, annotations = root / "previous baseline.json", root / "previous annotations.json"
        baseline.write_text("{}")
        annotations.write_text("{}")
        panel = ProcessingPanel(settings=self.settings)
        panel.baseline.setText(str(baseline))
        panel.annotations.setText(str(annotations))
        panel.save_inputs()
        reopened = ProcessingPanel(settings=self.settings)
        reopened.load_context(root / "missing/result.json")
        self.assertEqual(reopened.baseline.text(), str(baseline))
        self.assertEqual(reopened.annotations.text(), str(annotations))
        panel.close()
        reopened.close()

    def test_known_pilot_defaults_without_saved_inputs(self):
        panel = ProcessingPanel(settings=self.settings)
        root = Path(__file__).resolve().parents[2] / "out/deformable"
        for control, relative in [(panel.baseline, "local_baseline_001/dicom_pilot.json"),
                                  (panel.annotations, "original_photo_space_002/photo_stack.json")]:
            path = root / relative
            self.assertEqual(control.text(), str(path) if path.is_file() else "")
        panel.close()

    def test_regions_are_per_photo_persisted_and_removable(self):
        panel = ProcessingPanel(settings=self.settings)
        photo = Path(self.settings_temp.name) / "photo.png"
        panel.add_region(photo, [20, 30, 50, 60], "refine")
        panel.add_region(photo, [40, 40, 20, 20], "protect")
        self.assertEqual(panel.region_list.count(), 2)
        self.assertEqual(panel.regions[0]["nodes"], 9)
        reopened = ProcessingPanel(settings=self.settings)
        self.assertEqual(reopened.regions, panel.regions)
        reopened.region_list.setCurrentRow(0)
        reopened.remove_region()
        self.assertEqual(reopened.regions[0]["mode"], "protect")
        self.assertEqual(reopened.region_list.count(), 1)
        panel.close()
        reopened.close()

    def test_adaptive_run_passes_immutable_region_input_to_executor(self):
        root = Path(self.settings_temp.name)
        baseline, annotations = root / "baseline.json", root / "annotations.json"
        baseline.write_text("{}")
        annotations.write_text("{}")
        panel = ProcessingPanel(settings=self.settings)
        panel.baseline.setText(str(baseline))
        panel.annotations.setText(str(annotations))
        panel.add_region(root / "photo.png", [20, 30, 50, 60], "refine")
        panel.region_group.setChecked(True)
        with patch.object(panel.process, "start") as start:
            panel.start()
        args = start.call_args.args[1]
        region_path = Path(args[args.index("--regions") + 1])
        try:
            data = json.loads(region_path.read_text())
            self.assertEqual(data["coordinate_system"], "original_photo_pixels")
            self.assertEqual(data["regions"], panel.regions)
            self.assertFalse(panel.region_group.isEnabled())
        finally:
            region_path.unlink()
        panel.finished(1, QProcess.ExitStatus.NormalExit)
        panel.close()
