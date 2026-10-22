"""Contract tests for pairing, transform direction and safe preparation."""
import argparse
import json
from pathlib import Path
import tempfile
import unittest

import cv2
import numpy as np

from prepare import build_preview, load_session, matrix, orient, prepare, read_image, write_png


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.inputs = self.root / "inputs"
        self.inputs.mkdir()
        self.mask_dir = self.inputs / "masks"
        self.mask_dir.mkdir()
        image = np.zeros((16, 20, 3), dtype=np.uint8)
        image[6, 4] = 255
        write_png(self.inputs / "photo.png", image)
        write_png(self.inputs / "1006.png", image)
        write_png(self.mask_dir / "slice_0005.png", image[:, :, 0])
        self.forward = [1, 0, 3, 0, 1, 2, 0, 0, 1]
        self.session = {"stacks": [
            {"id": "stack_a", "directory": str(self.inputs), "slices": [
                {"index": 5, "file_path": str(self.inputs / "photo.png")}]},
            {"id": "stack_b", "directory": str(self.inputs), "slices": [
                {"index": 0, "file_path": str(self.inputs / "1006.png")}]}],
            "pairing": {"global_offset": -5, "pairs": [
                {"fixed_index": 5, "moving_index": 0, "valid": True}]},
            "registrations": [{"fixed_index": 5, "moving_index": 0,
                               "forward_matrix_3x3": self.forward}]}
        self.path = self.inputs / "sessão teste.json"

    def tearDown(self):
        self.temp.cleanup()

    def save(self):
        self.path.write_text(json.dumps(self.session), encoding="utf-8")

    def args(self, output=None):
        return argparse.Namespace(session=str(self.path), masks=str(self.mask_dir),
                                  output=str(output or self.root / "run"), indices="5",
                                  mask_pattern="slice_{index:04d}.png", pairing_id=None,
                                  dicom_dir=None, source_map=None)

    def test_pairing_forward_direction_and_input_unchanged(self):
        self.save()
        original = self.path.read_bytes()
        self.assertEqual(prepare(self.args()), 0)
        rendered = read_image(self.root / "run/images/fixed_0005_moving_0000_aligned.png")
        self.assertEqual(int(rendered[8, 7, 0]), 255)
        self.assertEqual(int(rendered[6, 4, 0]), 0)
        self.assertEqual(original, self.path.read_bytes())
        self.assertTrue((self.root / "run/review.html").is_file())
        manifest = json.loads((self.root / "run/manifest.json").read_text())
        self.assertEqual(manifest["pairs"][0]["moving_index"], 0)
        self.assertEqual(manifest["status"], "needs_review")
        with self.assertRaises(FileExistsError):
            prepare(self.args())

    def test_output_inside_inputs_rejected(self):
        self.save()
        with self.assertRaises(ValueError):
            prepare(self.args(self.inputs / "new_output"))

    def test_current_schema_and_duplicate_registration(self):
        pairing = self.session.pop("pairing")
        pairing.update(id="pair", fixed_stack_id="stack_a", moving_stack_id="stack_b")
        self.session.update(pairings=[pairing], active_pairing_id="pair")
        self.save()
        self.assertEqual(load_session(self.path)[1]["id"], "pair")
        self.session["registrations"] *= 2
        self.save()
        with self.assertRaises(ValueError):
            load_session(self.path)

    def test_bad_matrix_and_orientation_order(self):
        with self.assertRaises(ValueError):
            matrix({"forward_matrix_3x3": [0] * 9})
        image = np.arange(6).reshape(2, 3)
        np.testing.assert_array_equal(orient(image, {"rotation_degrees": 90, "flip_horizontal": True}),
                                      np.flip(np.rot90(image, -1), 1))

    def test_missing_input_is_reported(self):
        self.save()
        (self.inputs / "1006.png").unlink()
        self.assertEqual(prepare(self.args()), 2)
        manifest = json.loads((self.root / "run/manifest.json").read_text())
        self.assertEqual(manifest["pairs"][0]["status"], "error")
        self.assertEqual(manifest["requested_indices_without_render"], [5])

    def test_saved_compositing_mode(self):
        fixed = np.full((8, 8, 3), 100, np.uint8)
        moving = np.full_like(fixed, 200)
        self.assertEqual(int(build_preview(fixed, moving, {"preview_mode": "difference"})[0, 0, 0]), 100)
        self.assertEqual(int(build_preview(fixed, moving, {"preview_mode": "blend", "blend_alpha": 1})[0, 0, 0]), 200)
        self.assertEqual(int(build_preview(fixed, moving, {"preview_mode": "multiply"})[0, 0, 0]), 78)
        checker = build_preview(fixed, moving, {"preview_mode": "checkerboard", "checker_size": 4})
        self.assertEqual(int(checker[0, 0, 0]), 100)
        self.assertEqual(int(checker[0, 4, 0]), 200)


if __name__ == "__main__":
    unittest.main()
