import unittest

import cv2
import numpy as np

from global_registration import Settings, apply_points, register


class GlobalRegistrationTests(unittest.TestCase):
    def test_recovers_large_rotation_scale_and_translation_without_initial_matrix(self):
        moving = np.zeros((160, 160), np.uint8)
        cv2.fillPoly(moving, [np.array([[35, 35], [105, 25], [130, 60], [92, 86], [85, 130], [40, 110]])], 1)
        mb = np.zeros_like(moving)
        cv2.circle(mb, (60, 55), 10, 1)
        cv2.rectangle(mb, (80, 85), (91, 111), 1, -1)
        truth = np.eye(3)
        truth[:2] = cv2.getRotationMatrix2D((80, 80), 115, 1.12)
        truth[:2, 2] += [19, 11]
        fixed = cv2.warpAffine(moving, truth[:2], (210, 210), flags=cv2.INTER_NEAREST)
        fb = cv2.warpAffine(mb, truth[:2], (210, 210), flags=cv2.INTER_NEAREST)
        result = register(fixed, moving, fb, mb, Settings(working_size=128, max_evaluations=2400))
        fitted = np.asarray(result["forward_matrix_3x3"]).reshape(3, 3)
        landmarks = np.array([[60, 55], [85, 100], [40, 110], [105, 25]], float)
        error = np.linalg.norm(apply_points(landmarks, fitted) - apply_points(landmarks, truth), axis=1)
        self.assertLess(error.mean(), 2.0, str(error))
        self.assertGreater(result["body_dice"], .94)
        self.assertFalse(result["used_session_transform"])

    def test_rejects_empty_or_unpaired_bone_masks(self):
        empty = np.zeros((80, 80), np.uint8)
        body = empty.copy()
        body[10:60, 10:60] = 1
        with self.assertRaises(ValueError):
            register(empty, body)
        with self.assertRaises(ValueError):
            register(body, body, body, None)

    def test_reflection_is_explicit_and_detectable(self):
        moving = np.zeros((120, 120), np.uint8)
        cv2.fillPoly(moving, [np.array([[20, 15], [90, 30], [95, 90], [60, 100], [25, 60]])], 1)
        bone = np.zeros_like(moving)
        cv2.circle(bone, (40, 35), 6, 1)
        cv2.rectangle(bone, (70, 60), (79, 80), 1, -1)
        result = register(np.flipud(moving).copy(), moving, np.flipud(bone).copy(), bone,
                          Settings(allow_vertical_reflection=True, max_evaluations=2400))
        self.assertGreater(result["body_dice"], .94)
        transform = np.asarray(result["forward_matrix_3x3"]).reshape(3, 3)
        self.assertLess(np.linalg.det(transform[:2, :2]), 0)


if __name__ == "__main__":
    unittest.main()
