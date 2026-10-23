import json
from pathlib import Path
import tempfile
import unittest

import cv2
import numpy as np
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QSettings, QPointF

from prepare import write_png
from review_model import compose, discover, mutual_information, local_field
from viewer import ReviewWindow


class ViewerTests(unittest.TestCase):
    def test_adaptive_marking_coordinates_and_photo_scope(self):
        window = ReviewWindow(str(self.root))
        window.processing.settings = QSettings(str(self.root / "regions.ini"), QSettings.Format.IniFormat)
        window.processing.regions = []
        window.processing.refresh_regions()
        window.processing.region_group.setChecked(True)
        window.region_drawing("refine")
        canvas = window.panels[0].canvas
        canvas.resize(400, 400)
        canvas.state(2., .1, -.1)
        scale = canvas.fit_scale() * canvas.zoom
        x = (canvas.width() - canvas.image.width() * scale) / 2 + canvas.pan_x * canvas.width() + 25 * scale
        y = (canvas.height() - canvas.image.height() * scale) / 2 + canvas.pan_y * canvas.height() + 35 * scale
        self.assertEqual(canvas.image_position(QPointF(x, y)), (25, 35))
        canvas.region_selected.emit([20, 20, 30, 40])
        self.assertEqual(window.processing.regions[0]["rect"], [20, 20, 30, 40])
        self.assertEqual(len(window.panels[2].canvas.region_overlays), 1)
        self.assertEqual(window.processing.regions[0]["photo_path"], str((self.root / "fixed.png").resolve()))
        window.processing.regions[0]["photo_path"] = (self.root / "fixed.png").resolve().as_posix()
        window.update_region_overlays()
        self.assertEqual(len(window.panels[0].canvas.region_overlays), 1)
        window.processing.region_group.setChecked(False)
        self.assertFalse(window.region_mode)
        self.assertEqual(window.panels[0].canvas.region_overlays, [])
        window.close()

    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        a = np.zeros((90, 80, 3), np.uint8)
        a[20:65, 20:50] = (130, 200, 250)
        write_png(self.root / "fixed.png", a)
        write_png(self.root / "moving.png", a)
        transform = np.eye(3).ravel().tolist()
        result = {"inputs": {key: {"path": str(self.root / f"{key}.png")} for key in ["fixed", "moving"]},
                  "forward_matrix_3x3": transform,
                  "candidates": [{"objective": .01, "forward_matrix_3x3": transform}], "body_dice": 1.0}
        (self.root / "result.json").write_text(json.dumps(result), encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def test_controls_modes_contours_hypotheses_and_navigation(self):
        window = ReviewWindow(str(self.root))
        window.show()
        self.app.processEvents()
        self.assertEqual(len(window.entries), 1)
        self.assertIsNotNone(window.sources)
        self.assertFalse(window.stage.isEnabled())
        for mode in range(4):
            window.mode.setCurrentIndex(mode)
            window.alpha.setValue(70)
            window.checker.setValue(12)
            window.panels[2].contours.setChecked(True)
            window.hypothesis.setCurrentIndex(1)
            window.render()
            self.assertFalse(window.panels[2].canvas.image.isNull())
            self.assertNotIn("Erro", window.details.toPlainText())
        window.alignment.setChecked(False)
        window.render()
        self.assertIn("desligado", window.mi.text())
        canvas = window.panels[0].canvas
        canvas.state(2, .1, -.2)
        self.assertEqual(window.panels[2].canvas.zoom, 2)
        window.sync.setChecked(False)
        canvas.state(3, .2, .2)
        self.assertEqual(window.panels[2].canvas.zoom, 2)
        canvas.one_to_one()
        self.assertAlmostEqual(canvas.zoom * canvas.fit_scale(), 1.)
        canvas.fit()
        self.assertEqual(canvas.zoom, 1)
        window.navigate(1)
        self.assertEqual(window.index, 0)
        window.close()

    def test_loader_and_composition_direction(self):
        entries, errors = discover(str(self.root))
        self.assertEqual(len(entries), 1)
        self.assertEqual(errors, [])
        fixed = np.zeros((20, 20, 3), np.uint8)
        moving = fixed.copy()
        moving[4, 5] = 255
        transform = np.eye(3)
        transform[:2, 2] = [3, 2]
        result = compose(fixed, moving, transform, alpha=1)
        self.assertEqual(int(result[6, 8, 0]), 255)
        self.assertEqual(int(result[4, 5, 0]), 0)
        random = np.random.default_rng(10).integers(0, 256, (80, 80, 3), np.uint8)
        self.assertGreater(mutual_information(random, random, np.eye(3)),
                           mutual_information(random, np.flipud(random).copy(), np.eye(3)))

    def test_local_field_applies_in_all_modes_and_can_be_disabled(self):
        from prepare import digest
        field = np.zeros((90, 80, 2), np.float32)
        field[:, :, 0] = 5
        path = self.root / "local_field.npz"
        from deformation import FIELD_CONVENTION
        np.savez_compressed(path, displacement_xy=field, roi=[0, 0, 80, 90], output_shape=[90, 80], convention=FIELD_CONVENTION)
        result_path = self.root / "result.json"
        result = json.loads(result_path.read_text())
        result["local_deformation"] = {"status": "candidate", "field_path": str(path), "sha256": digest(path)}
        stage_path = self.root / "stage_01_field.npz"
        np.savez_compressed(stage_path, displacement_xy=field * -1, roi=[0, 0, 80, 90],
                            output_shape=[90, 80], convention=FIELD_CONVENTION)
        result["local_deformation"]["stages"] = [{"level": 0, "count": 7,
            "status": "intermediate_unvalidated", "field_path": str(stage_path),
            "sha256": digest(stage_path), "settings": {"nodes": [7]}}]
        result_path.write_text(json.dumps(result))
        window = ReviewWindow(str(self.root))
        window.show()
        self.app.processEvents()
        self.assertTrue(window.local_enabled.isEnabled())
        window.alpha.setValue(100)
        window.render()
        before = window.panels[2].canvas.image.toImage()
        window.panels[0].canvas.state(2., .1, -.1)
        window.stage.setCurrentIndex(1)
        window.render()
        self.assertNotEqual(before, window.panels[2].canvas.image.toImage())
        self.assertIn("INTERMEDIARIA", window.details.toPlainText())
        self.assertEqual(window.panels[2].canvas.zoom, 2.)
        window.select(0)
        self.assertEqual(window.stage.currentIndex(), 1)
        window.stage.setCurrentIndex(0)
        window.local_enabled.setChecked(False)
        window.render()
        after = window.panels[2].canvas.image.toImage()
        self.assertNotEqual(before, after)
        window.local_enabled.setChecked(True)
        window.show_mesh.setChecked(True)
        for mode in range(4):
            window.mode.setCurrentIndex(mode)
            window.render()
            self.assertNotIn("Erro", window.details.toPlainText())
        with self.assertRaises(ValueError):
            local_field(str(path), "wrong hash")
        window.close()


if __name__ == "__main__":
    unittest.main()
