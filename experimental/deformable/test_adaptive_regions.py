import unittest

import cv2
import numpy as np
import torch

from adaptive_regions import patch_delta, protection_weight, validate_regions
from local_registration import LocalSettings, fit_sequence, fitted_field, dense_field
from test_local_registration import geometry
from run_local import masked_dice


class AdaptiveTests(unittest.TestCase):
    def test_cancellation_reaches_adaptive_phase(self):
        body = np.ones((64, 64), np.uint8)
        stop = [False]
        def progress(record):
            stop[0] = bool(record.get("adaptive"))
        def cancel():
            if stop[0]:
                raise KeyboardInterrupt
        with self.assertRaises(KeyboardInterrupt):
            fit_sequence([body], [body], [body], [body], [body], [geometry()], [np.eye(3)],
                LocalSettings(working_size=64, nodes=(3,), iterations=2), callback=progress,
                cancel_check=cancel, regions=[{"sequence_index": 0, "mode": "refine", "rect": [10, 10, 40, 40], "nodes": 9}])

    def test_patch_support_protection_and_smooth_boundary(self):
        nodes = torch.ones((1, 2, 9, 9), requires_grad=True)
        roi, shape = [0, 0, 128, 128], (128, 128)
        field = patch_delta(nodes, [20, 20, 80, 80], roi, shape)
        self.assertEqual(float(field[:, :, :20].detach().abs().max()), 0.)
        self.assertEqual(float(field[:, :, :, 100:].detach().abs().max()), 0.)
        self.assertLess(float(field[:, :, 20].detach().abs().max()), 1e-5)
        weight = protection_weight([{"mode": "protect", "rect": [45, 45, 20, 20]}], roi, shape, "cpu")
        protected = field * weight
        self.assertEqual(float(protected[:, :, 45:65, 45:65].detach().abs().max()), 0.)
        protected.sum().backward()
        self.assertGreater(float(nodes.grad.abs().sum()), 0.)

    def test_invalid_region_bounds_and_resolution(self):
        for rect in [[-1, 0, 20, 20], [0, 0, 2, 20], [50, 50, 60, 60]]:
            with self.assertRaises(ValueError):
                validate_regions([{"mode": "refine", "rect": rect, "nodes": 9}], (100, 100))
        with self.assertRaises(ValueError):
            validate_regions([{"mode": "refine", "rect": [0, 0, 20, 20], "nodes": 34}])

    def test_local_lattice_preserves_other_slice_and_protected_region(self):
        body = np.zeros((128, 128), np.uint8)
        cv2.ellipse(body, (64, 64), (57, 53), 0, 0, 360, 1, -1)
        target, source = np.zeros_like(body), np.zeros_like(body)
        for x in [35, 90]:
            cv2.circle(target, (x, 64), 8, 1, -1)
        for x in [35, 95]:
            cv2.circle(source, (x, 64), 8, 1, -1)
        regions = [{"sequence_index": 0, "mode": "refine", "rect": [65, 30, 60, 70], "nodes": 9},
                   {"sequence_index": 0, "mode": "protect", "rect": [20, 45, 30, 35]}]
        cfg = LocalSettings(working_size=128, nodes=(3,), iterations=80, max_displacement_px=14,
                            rigidity_weight=.05, smoothness_weight=.05, continuity_weight=.01)
        fit = fit_sequence([body, body], [body, body], [target, target], [source, source],
                           [np.ones_like(body)] * 2, [geometry(0), geometry(1)], [np.eye(3)] * 2,
                           cfg, regions=regions)
        self.assertTrue(fit["stages"][-1]["adaptive"])
        roi = fit["roi"]
        shape = (roi[3], roi[2])
        base = dense_field(fit["nodes"][0], shape, cfg.max_displacement_px)
        final = fitted_field(fit, 0, shape)
        delta = final - base
        self.assertEqual(float(np.abs(delta[45:80, 20:50]).max()), 0.)
        self.assertEqual(float(np.abs(delta[:, :65]).max()), 0.)
        second = fitted_field(fit, 1, shape)
        np.testing.assert_array_equal(second, dense_field(fit["nodes"][1], shape, cfg.max_displacement_px))
        from deformation import remap_native
        before = remap_native(source, np.eye(3), body.shape, base, roi, True)
        after = remap_native(source, np.eye(3), body.shape, final, roi, True)
        self.assertGreater(masked_dice(target, after), masked_dice(target, before) + .01)
