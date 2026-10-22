import json
from pathlib import Path
import tempfile
import unittest

import cv2
import numpy as np
from PySide6.QtWidgets import QApplication

from prepare import write_png
from review_model import compose, discover, mutual_information
from viewer import ReviewWindow


class ViewerTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
