"""Input and image operations for the standalone processed-slice viewer."""
from dataclasses import dataclass, field
import glob
import json
from pathlib import Path
from functools import lru_cache

import cv2
import numpy as np
import pydicom

from prepare import build_preview, orient, digest
from deformation import native_maps, remap_native, FIELD_CONVENTION


@dataclass
class Entry:
    label: str
    fixed: Path
    moving: Path
    transform: np.ndarray
    origin: Path
    candidates: list = field(default_factory=list)
    warnings: list = field(default_factory=list)
    score: float | None = None
    fixed_orientation: dict = field(default_factory=dict)
    moving_orientation: dict = field(default_factory=dict)
    masks: dict = field(default_factory=dict)
    local: dict = field(default_factory=dict)


def validated_matrix(values):
    result = np.asarray(values, dtype=float).reshape(3, 3)
    if not np.isfinite(result).all() or not np.allclose(result[2], [0, 0, 1]) or abs(np.linalg.det(result[:2, :2])) < 1e-10:
        raise ValueError("Invalid affine transform")
    return result


def discover(pattern):
    """Open global results or preparation manifests, preserving provenance."""
    entries, errors, files = [], [], set()
    for match in glob.glob(str(pattern)):
        path = Path(match).resolve()
        if path.is_dir():
            files.update(path.rglob("result.json"))
            if (path / "manifest.json").is_file():
                files.add(path / "manifest.json")
        elif path.is_file():
            files.add(path)
    for path in sorted(files):
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
            if "inputs" in data and "forward_matrix_3x3" in data:
                inputs = data["inputs"]
                fixed, moving = Path(inputs["fixed"]["path"]), Path(inputs["moving"]["path"])
                entries.append(Entry(f"{fixed.stem} · {path.parent.name}", fixed, moving,
                                     validated_matrix(data["forward_matrix_3x3"]), path,
                                     data.get("candidates", []), data.get("warnings", []), data.get("body_dice"),
                                     masks={("fixed_bone" if key == "reference_fixed_bone" else key): Path(value["path"])
                                            for key, value in inputs.items() if key.endswith("bone")},
                                     local=data.get("local_deformation", {})))
            elif "pairs" in data and "session_path" in data:
                from prepare import load_session
                _, _, fixed_stack, moving_stack, _ = load_session(data["session_path"])
                a = {r["index"]: r for r in fixed_stack["slices"]}
                b = {r["index"]: r for r in moving_stack["slices"]}
                for row in data["pairs"]:
                    if "forward_matrix_3x3" not in row:
                        continue
                    fi, mi = row["fixed_index"], row["moving_index"]
                    entries.append(Entry(f"{Path(row['fixed_path']).stem} · {path.parent.name}",
                                         Path(row["fixed_path"]), Path(row["moving_path"]),
                                         validated_matrix(row["forward_matrix_3x3"]), path,
                                         warnings=data.get("warnings", []), fixed_orientation=a[fi], moving_orientation=b[mi],
                                         masks={"fixed_bone": Path(row["mask_path"])}))
            else:
                errors.append(f"{path}: formato sem resultados de alinhamento")
        except (ValueError, KeyError, OSError) as exc:
            errors.append(f"{path}: {exc}")
    entries.sort(key=lambda item: (item.fixed.stem, item.origin.parent.name))
    return entries, errors


class Source:
    def __init__(self, path, orientation=None):
        self.path = Path(path)
        self.orientation = orientation or {}
        self.dicom = self.path.suffix.lower() == ".dcm"
        if self.dicom:
            ds = pydicom.dcmread(self.path)
            pixels = ds.pixel_array
            if pixels.ndim != 2:
                raise ValueError("Only single-frame grayscale DICOM is supported")
            self.raw = pixels.astype(np.float32) * float(getattr(ds, "RescaleSlope", 1)) + float(getattr(ds, "RescaleIntercept", 0))
            def first(value):
                return float(value[0] if hasattr(value, "__len__") and not isinstance(value, str) else value)
            self.default_center = first(getattr(ds, "WindowCenter", (float(self.raw.max()) + float(self.raw.min())) / 2))
            self.default_width = max(1., first(getattr(ds, "WindowWidth", float(self.raw.max()) - float(self.raw.min()))))
            self.monochrome1 = str(getattr(ds, "PhotometricInterpretation", "")) == "MONOCHROME1"
        else:
            self.raw = cv2.imdecode(np.fromfile(str(self.path), np.uint8), cv2.IMREAD_COLOR)
            if self.raw is None:
                raise ValueError(f"Cannot decode {self.path}")
            self.default_center, self.default_width = 127.5, 255.
            self.monochrome1 = False

    def display(self, center=None, width=None):
        center = self.default_center if center is None else center
        width = self.default_width if width is None else max(1., width)
        if not self.dicom and center == self.default_center and width == self.default_width:
            return orient(self.raw.copy(), self.orientation)
        image = np.clip((self.raw.astype(float) - (center - width / 2)) * 255 / width, 0, 255).astype(np.uint8)
        if self.monochrome1:
            image = 255 - image
        if image.ndim == 2:
            image = cv2.cvtColor(image, cv2.COLOR_GRAY2BGR)
        return orient(image, self.orientation)


@lru_cache(maxsize=4)
def local_field(path, expected_sha256):
    if expected_sha256 and digest(path) != expected_sha256:
        raise ValueError("Local field checksum mismatch")
    with np.load(path, allow_pickle=False) as data:
        if "convention" not in data or str(data["convention"]) != FIELD_CONVENTION:
            raise ValueError("Unsupported local field direction/convention")
        field, roi = data["displacement_xy"].copy(), data["roi"].tolist()
    if len(roi) != 4 or field.shape != (roi[3], roi[2], 2) or not np.isfinite(field).all():
        raise ValueError("Invalid local field")
    return field, roi


def warp(image, matrix, shape, nearest=False, local=None):
    if local is not None:
        field, roi = local
        return remap_native(image, matrix, shape, field, roi, nearest)
    return cv2.warpAffine(image, matrix[:2], (shape[1], shape[0]), flags=cv2.INTER_NEAREST if nearest else cv2.INTER_LINEAR)


def edges(image):
    return cv2.Canny(cv2.GaussianBlur(cv2.cvtColor(image, cv2.COLOR_BGR2GRAY), (5, 5), 1), 60, 150)


def mutual_information(fixed, moving, transform, local=None):
    """Same 32-bin partial-volume histogram and normalization as the C++ viewer."""
    scale = min(1., 220 / max(fixed.shape[:2]))
    size = (max(1, int(fixed.shape[1] * scale)), max(1, int(fixed.shape[0] * scale)))
    a = cv2.resize(cv2.cvtColor(fixed, cv2.COLOR_BGR2GRAY), size, interpolation=cv2.INTER_AREA)
    affine = transform[:2] * scale
    gray = cv2.cvtColor(moving, cv2.COLOR_BGR2GRAY)
    b = cv2.warpAffine(gray, affine, size, flags=cv2.INTER_LINEAR)
    valid = cv2.warpAffine(np.ones(gray.shape, np.uint8), affine, size, flags=cv2.INTER_NEAREST) != 0
    if local is not None:
        # Sample the same composed map as the preview, including validity of native CT support.
        mx, my = native_maps(transform, fixed.shape, *local)
        xx, yy = np.meshgrid(np.arange(size[0], dtype=np.float32) / scale,
                             np.arange(size[1], dtype=np.float32) / scale)
        small_x = cv2.remap(mx, xx, yy, cv2.INTER_LINEAR)
        small_y = cv2.remap(my, xx, yy, cv2.INTER_LINEAR)
        b = cv2.remap(gray, small_x, small_y, cv2.INTER_LINEAR)
        valid = cv2.remap(np.ones(gray.shape, np.uint8), small_x, small_y, cv2.INTER_NEAREST) != 0
    if not valid.any():
        return 0.
    af = np.minimum(31, (a[valid].astype(float) / 8).astype(int))
    bf = b[valid].astype(float) / 8
    lo = np.minimum(31, bf.astype(int))
    hi = np.minimum(31, lo + 1)
    frac = bf - lo
    joint = np.bincount(af * 32 + lo, weights=1 - frac, minlength=1024)
    joint += np.bincount(af * 32 + hi, weights=frac, minlength=1024)
    joint = joint.reshape(32, 32)
    def entropy(hist):
        p = hist.ravel() / valid.sum()
        p = p[p > 0]
        return float(-(p * np.log(p)).sum())
    ha, hb, hab = entropy(joint.sum(1)), entropy(joint.sum(0)), entropy(joint)
    return float(np.clip(2 * (ha + hb - hab) / (ha + hb), 0, 1)) if ha + hb > 1e-9 else 0.


def aligned_display(source, transform, shape, center, width, local=None):
    """For DICOM, interpolate physical intensities before applying a display window."""
    if not source.dicom:
        return warp(source.display(center, width), transform, shape, local=local)
    raw = orient(source.raw, source.orientation)
    value = remap_native(raw, transform, shape, *(local or (None, None)), border=-1024.)
    displayed = np.clip((value - (center - width / 2)) * 255 / max(1., width), 0, 255).astype(np.uint8)
    if source.monochrome1:
        displayed = 255 - displayed
    return cv2.cvtColor(displayed, cv2.COLOR_GRAY2BGR)


def compose(fixed, moving, transform, mode="blend", alpha=.5, checker=32, alignment=True, local=None, aligned_override=None):
    aligned = aligned_override if alignment and aligned_override is not None else warp(moving, transform, fixed.shape, local=local) if alignment else cv2.resize(moving, (fixed.shape[1], fixed.shape[0]))
    return build_preview(fixed, aligned, {"preview_mode": mode, "blend_alpha": alpha, "checker_size": checker})
