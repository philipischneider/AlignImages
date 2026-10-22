import unittest
import numpy as np
import cv2

from restore_photo_space import restore_mask
from global_registration import extract_body


class PhotoSpaceTests(unittest.TestCase):
    def test_integer_translation_preserves_labels_and_unknown_exterior(self):
        mask = np.array([[0, 1, 2], [2, 0, 1]], np.uint8)
        restored, support = restore_mask(mask, (10, 12, 3), (4, 3, 3, 2))
        np.testing.assert_array_equal(restored[3:5, 4:7], mask)
        self.assertEqual(int(np.count_nonzero(support)), 6)
        self.assertEqual(int(restored[3, 6]), 2)
        self.assertEqual(int(support[0, 0]), 0)
        with self.assertRaises(ValueError):
            restore_mask(mask, (3, 3), (4, 3, 3, 2))

    def test_photo_body_excludes_dark_equipment_outside_blue(self):
        image = np.zeros((120, 180, 3), np.uint8)
        image[10:110, 20:160] = (160, 70, 30)
        cv2.ellipse(image, (90, 60), (28, 38), 0, 0, 360, (70, 130, 180), -1)
        mask = extract_body(image, "photo")
        self.assertEqual(int(mask[60, 90]), 1)
        self.assertEqual(int(mask[1, 1]), 0)
        self.assertLess(np.count_nonzero(mask), 5000)


if __name__ == "__main__":
    unittest.main()
