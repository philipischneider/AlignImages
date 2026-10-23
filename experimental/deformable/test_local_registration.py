import unittest
import tempfile
from pathlib import Path

import cv2
import numpy as np

from local_registration import LocalSettings, dense_field, fit_sequence, jacobian, native_maps, sequence_links
from run_local import masked_dice, save_intermediate_stages


def geometry(z=0, spacing=1.):
    basis = np.eye(4)
    basis[:2, :2] *= spacing
    return {"native_pixel_xy_to_patient_mm_4x4": basis.tolist(), "orientation_patient": [1, 0, 0, 0, 1, 0],
            "position_patient_mm": [0, 0, z], "series_uid": "test"}


class LocalRegistrationTests(unittest.TestCase):
    def test_level_snapshots_are_saved_independently(self):
        body = np.ones((64, 64), np.uint8)
        cfg = LocalSettings(working_size=64, nodes=(3, 5, 7), iterations=2)
        fit = fit_sequence([body], [body], [body], [body], [body], [geometry()], [np.eye(3)], cfg)
        self.assertEqual([s["nodes"].shape for s in fit["stages"]], [(1, 2, n, n) for n in cfg.nodes])
        np.testing.assert_array_equal(fit["stages"][-1]["nodes"], fit["nodes"])
        self.assertFalse(np.shares_memory(fit["stages"][-1]["nodes"], fit["nodes"]))
        with tempfile.TemporaryDirectory() as temp:
            stages = save_intermediate_stages(Path(temp), fit, 0, body.shape, np.eye(3),
                body, body, body, body, body, {"body_before": 1., "bone_before": 1.})
            self.assertEqual([s["count"] for s in stages], [3, 5, 7])
            for stage in stages:
                self.assertTrue(Path(stage["field_path"]).is_file())
                self.assertEqual(stage["status"], "intermediate_unvalidated")
                self.assertEqual(stage["metrics"]["body_after"], 1.)

    def test_default_body_and_bone_weights_are_equal(self):
        cfg = LocalSettings()
        self.assertEqual(cfg.body_weight, 1.)
        self.assertEqual(cfg.bone_weight, cfg.body_weight)

    def test_cooperative_cancellation_during_optimization(self):
        body = np.ones((64, 64), np.uint8)
        def cancel():
            raise KeyboardInterrupt
        with self.assertRaises(KeyboardInterrupt):
            fit_sequence([body], [body], [body], [body], [body], [geometry()], [np.eye(3)],
                         LocalSettings(working_size=64, iterations=1), cancel_check=cancel)

    def test_identical_anatomy_requires_no_deformation_and_nearest_preserves_labels(self):
        body = np.zeros((80, 80), np.uint8)
        cv2.circle(body, (40, 40), 30, 1, -1)
        bone = np.zeros_like(body)
        cv2.circle(bone, (40, 40), 9, 1, -1)
        cfg = LocalSettings(working_size=80, nodes=(5,), iterations=10, max_displacement_px=10)
        fit = fit_sequence([body], [body], [bone], [bone], [np.ones_like(body)], [geometry()], [np.eye(3)], cfg)
        roi = fit["roi"]
        field = dense_field(fit["nodes"][0], (roi[3], roi[2]), 10)
        self.assertLess(float(np.abs(field).max()), .01)
        labels = bone * 7
        from local_registration import remap_native
        result = remap_native(labels, np.eye(3), labels.shape, field, roi, nearest=True)
        self.assertEqual(set(np.unique(result)), {0, 7})

    def test_regional_corrections_recover_opposite_bone_displacements(self):
        body = np.zeros((128, 128), np.uint8)
        cv2.ellipse(body, (64, 64), (55, 48), 0, 0, 360, 1, -1)
        target, source = np.zeros_like(body), np.zeros_like(body)
        for x in [37, 91]:
            cv2.circle(target, (x, 64), 9, 1, -1)
        for x in [43, 85]:
            cv2.circle(source, (x, 64), 9, 1, -1)
        cfg = LocalSettings(working_size=128, iterations=100, max_displacement_px=14, continuity_weight=.01)
        fit = fit_sequence([body], [body], [target], [source], [np.ones_like(body)], [geometry()], [np.eye(3)], cfg)
        roi = fit["roi"]
        field = dense_field(fit["nodes"][0], (roi[3], roi[2]), cfg.max_displacement_px)
        from local_registration import remap_native
        warped = remap_native(source, np.eye(3), body.shape, field, roi, nearest=True)
        self.assertGreater(masked_dice(target, warped), masked_dice(target, source) + .2)
        self.assertGreater(float(field[64 - roi[1], 37 - roi[0], 0]), 2.)
        self.assertLess(float(field[64 - roi[1], 91 - roi[0], 0]), -2.)
        self.assertGreaterEqual(float(jacobian(field).min()), cfg.minimum_jacobian)
        self.assertLessEqual(float(np.linalg.norm(field, axis=2).max()), cfg.max_displacement_px)

    def test_inverse_composition_and_physical_link_across_spacing_change(self):
        transform = np.diag([2., 2., 1.])
        field = np.zeros((10, 10, 2), np.float32)
        field[:, :, 0] = 4
        mx, my = native_maps(transform, (10, 10), field, [0, 0, 10, 10])
        self.assertEqual(float(mx[2, 4]), 4.)
        self.assertEqual(float(my[2, 4]), 1.)
        physical, links = sequence_links([geometry(0, 1), geometry(1, 2), geometry(5, 2)],
                                          [np.eye(3), transform, transform], 2.)
        np.testing.assert_allclose(physical[0], physical[1])
        self.assertEqual(links, [(0, 1, 1.)])
        self.assertTrue(np.all(jacobian(np.zeros_like(field)) == 1))


if __name__ == "__main__":
    unittest.main()
