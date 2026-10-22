"""Automatic global mask registration. No session or initial matrix is accepted."""
from dataclasses import dataclass
import math

import cv2
import numpy as np
from scipy.optimize import minimize


@dataclass
class Settings:
    working_size: int = 256
    hypotheses: int = 4
    max_evaluations: int = 1800
    allow_vertical_reflection: bool = False


def binary(mask):
    if mask.ndim != 2:
        raise ValueError("Masks must be single channel")
    result = (mask != 0).astype(np.uint8)
    if np.count_nonzero(result) < 16:
        raise ValueError("Mask is empty or too small for registration")
    return result


def largest_filled(mask):
    contours, _ = cv2.findContours(mask.astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        raise ValueError("Could not extract a body component")
    result = np.zeros(mask.shape, np.uint8)
    cv2.drawContours(result, [max(contours, key=cv2.contourArea)], -1, 1, cv2.FILLED)
    return binary(result)


def extract_body(image, modality):
    if modality == "photo":
        hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
        blue = cv2.inRange(hsv, (80, 30, 20), (145, 255, 255))
        b, g, r = cv2.split(image.astype(np.float32))
        # Full photographs include dark equipment outside the blue embedding medium.
        # Restrict the component seed to warm tissue before filling its exterior.
        mask = ((blue == 0) & (hsv[:, :, 2] > 35) & (r > b * 1.05) & (g > b * .8)).astype(np.uint8)
    elif modality == "ct_png":
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        _, mask = cv2.threshold(gray, 0, 1, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    else:
        raise ValueError(f"Unsupported modality: {modality}")
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    return largest_filled(mask)


def scaled(mask, size):
    mask = binary(mask)
    factor = min(1.0, size / max(mask.shape))
    width, height = max(1, round(mask.shape[1] * factor)), max(1, round(mask.shape[0] * factor))
    transform = np.array([[width / mask.shape[1], 0, .5 * (width / mask.shape[1] - 1)],
                          [0, height / mask.shape[0], .5 * (height / mask.shape[0] - 1)], [0, 0, 1.]])
    return binary(cv2.resize(mask, (width, height), interpolation=cv2.INTER_NEAREST)), transform


def boundary(mask, limit=350):
    contours, _ = cv2.findContours(mask, cv2.RETR_LIST, cv2.CHAIN_APPROX_NONE)
    points = np.concatenate(contours).reshape(-1, 2).astype(float)
    return points[np.linspace(0, len(points) - 1, min(limit, len(points))).astype(int)]


def distance(mask):
    edges = np.zeros(mask.shape, np.uint8)
    contours, _ = cv2.findContours(mask, cv2.RETR_LIST, cv2.CHAIN_APPROX_NONE)
    cv2.drawContours(edges, contours, -1, 1, 1)
    return cv2.distanceTransform(1 - edges, cv2.DIST_L2, cv2.DIST_MASK_PRECISE)


def sample(field, points):
    h, w = field.shape
    clipped = np.clip(points, [0, 0], [w - 1, h - 1])
    values = cv2.remap(field, clipped[:, 0].astype(np.float32).reshape(-1, 1),
                       clipped[:, 1].astype(np.float32).reshape(-1, 1), cv2.INTER_LINEAR).ravel()
    return values + np.linalg.norm(points - clipped, axis=1)


def apply_points(points, transform):
    return points @ transform[:2, :2].T + transform[:2, 2]


def center(mask):
    y, x = np.nonzero(mask)
    return np.array([x.mean(), y.mean()])


def make_transform(parameters, moving_center, fixed_center, reflection):
    angle, lx, ly, shear, tx, ty = parameters
    c, s = math.cos(angle), math.sin(angle)
    linear = np.array([[c, -s], [s, c]]) @ np.array([[math.exp(lx), shear * math.exp(ly)], [0, math.exp(ly)]]) @ reflection
    result = np.eye(3)
    result[:2, :2] = linear
    result[:2, 2] = fixed_center + [tx, ty] - linear @ moving_center
    return result


def dice(a, b):
    total = np.count_nonzero(a) + np.count_nonzero(b)
    return 2 * np.count_nonzero((a != 0) & (b != 0)) / total if total else None


def register(fixed_body, moving_body, fixed_bone=None, moving_bone=None, settings=None):
    settings = settings or Settings()
    if settings.working_size < 64 or settings.hypotheses < 1 or settings.max_evaluations < 100:
        raise ValueError("Invalid registration budget")
    if (fixed_bone is None) != (moving_bone is None):
        raise ValueError("Provide both bone masks, or neither")
    original_fixed, original_moving = binary(fixed_body), binary(moving_body)
    fixed, sf = scaled(original_fixed, settings.working_size)
    moving, sm = scaled(original_moving, settings.working_size)
    channels = [(fixed, moving, 1.0)]
    if fixed_bone is not None:
        if fixed_bone.shape != original_fixed.shape or moving_bone.shape != original_moving.shape:
            raise ValueError("Bone/body masks must share their source grid")
        fb = binary(cv2.resize(binary(fixed_bone), (fixed.shape[1], fixed.shape[0]), interpolation=cv2.INTER_NEAREST))
        mb = binary(cv2.resize(binary(moving_bone), (moving.shape[1], moving.shape[0]), interpolation=cv2.INTER_NEAREST))
        channels.append((fb, mb, 2.0))
    prepared = [(boundary(a), boundary(b), distance(a), distance(b), weight) for a, b, weight in channels]
    fc, mc = center(fixed), center(moving)
    base_scale = math.sqrt(np.count_nonzero(fixed) / np.count_nonzero(moving))
    extent = max(fixed.shape)
    reflections = [np.eye(2)]
    if settings.allow_vertical_reflection:
        reflections.append(np.diag([1., -1.]))

    def objective(parameters, reflection):
        transform = make_transform(parameters, mc, fc, reflection)
        inverse = np.linalg.inv(transform)
        total = 0.
        for ap, bp, ad, bd, weight in prepared:
            forward_dist = sample(ad, apply_points(bp, transform))
            # Express reverse distances in fixed-grid pixels as well.
            reverse_dist = sample(bd, apply_points(ap, inverse)) * math.sqrt(abs(np.linalg.det(transform[:2, :2])))
            total += weight * (np.mean(np.sqrt(1 + forward_dist ** 2) - 1) +
                               np.mean(np.sqrt(1 + reverse_dist ** 2) - 1)) / extent
        # Mild global shape constraints; local warping is intentionally absent.
        return float(total + .01 * (parameters[1] - parameters[2]) ** 2 + .03 * parameters[3] ** 2)

    starts = []
    for ri, reflection in enumerate(reflections):
        for angle in np.linspace(-math.pi, math.pi, 24, endpoint=False):
            initial = np.array([angle, math.log(base_scale), math.log(base_scale), 0., 0., 0.])
            starts.append((objective(initial, reflection), ri, initial))
    starts.sort(key=lambda value: value[0])
    selected = []
    for item in starts:
        if any(item[1] == old[1] and abs(math.atan2(math.sin(item[2][0] - old[2][0]), math.cos(item[2][0] - old[2][0]))) < math.radians(45) for old in selected):
            continue
        selected.append(item)
        if len(selected) >= settings.hypotheses:
            break
    candidates = []
    for _, ri, initial in selected:
        bounds = [(initial[0] - math.pi / 3, initial[0] + math.pi / 3),
                  (math.log(base_scale / 2.5), math.log(base_scale * 2.5)),
                  (math.log(base_scale / 2.5), math.log(base_scale * 2.5)),
                  (-.4, .4), (-extent, extent), (-extent, extent)]
        fit = minimize(objective, initial, args=(reflections[ri],), method="Powell", bounds=bounds,
                       options={"maxfev": settings.max_evaluations, "xtol": .001, "ftol": .00005})
        work = make_transform(fit.x, mc, fc, reflections[ri])
        full = np.linalg.inv(sf) @ work @ sm
        candidates.append({"objective": float(fit.fun), "forward_matrix_3x3": full.ravel().tolist(),
                           "reflected": bool(ri), "optimizer_converged": bool(fit.success),
                           "evaluations": int(fit.nfev)})
    candidates.sort(key=lambda value: value["objective"])
    best = candidates[0]
    transform = np.asarray(best["forward_matrix_3x3"]).reshape(3, 3)
    warped = cv2.warpAffine(original_moving, transform[:2], (original_fixed.shape[1], original_fixed.shape[0]), flags=cv2.INTER_NEAREST)
    warnings = ["Global candidate requires anatomical review; no local deformation or longitudinal coupling applied."]
    if len(channels) == 1:
        warnings.append("Body only: internal anatomy and symmetric orientations may be ambiguous.")
    if not best["optimizer_converged"]:
        warnings.append("Optimization budget exhausted; convergence not established.")
    gap = candidates[1]["objective"] - best["objective"] if len(candidates) > 1 else None
    if gap is not None and gap < .01:
        warnings.append("Competing initialization has a similar objective; inspect orientation.")
    return {"algorithm": "global_masks_multistart_v1", "status": "needs_review",
            "forward_matrix_3x3": best["forward_matrix_3x3"],
            "inverse_matrix_3x3": np.linalg.inv(transform).ravel().tolist(),
            "body_dice": dice(original_fixed, warped), "candidates": candidates,
            "warnings": warnings, "used_session_transform": False}
