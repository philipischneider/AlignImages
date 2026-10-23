"""Original CT decoding, HU masks and explicit in-plane physical resampling."""
from dataclasses import dataclass

import cv2
import numpy as np
import pydicom

from global_registration import binary, largest_filled


@dataclass
class CtSlice:
    hu: np.ndarray
    valid: np.ndarray
    metadata: dict


def read_ct(path):
    ds = pydicom.dcmread(path)
    if str(getattr(ds, "Modality", "")) != "CT":
        raise ValueError("DICOM source must be CT")
    pixels = ds.pixel_array
    if pixels.ndim != 2 or int(getattr(ds, "NumberOfFrames", 1)) != 1:
        raise ValueError("Only single-frame CT is supported")
    slope, intercept = float(getattr(ds, "RescaleSlope", 1)), float(getattr(ds, "RescaleIntercept", 0))
    if not np.isfinite([slope, intercept]).all() or slope == 0:
        raise ValueError("Invalid DICOM rescale")
    spacing = np.asarray(getattr(ds, "PixelSpacing", []), dtype=float)
    position = np.asarray(getattr(ds, "ImagePositionPatient", []), dtype=float)
    orientation = np.asarray(getattr(ds, "ImageOrientationPatient", []), dtype=float)
    if spacing.shape != (2,) or position.shape != (3,) or orientation.shape != (6,):
        raise ValueError("CT requires PixelSpacing, ImagePositionPatient and ImageOrientationPatient")
    if not np.isfinite(np.r_[spacing, position, orientation]).all() or np.any(spacing <= 0):
        raise ValueError("Invalid CT geometry")
    if not np.allclose([np.linalg.norm(orientation[:3]), np.linalg.norm(orientation[3:])], 1, atol=.001) or abs(np.dot(orientation[:3], orientation[3:])) > .001:
        raise ValueError("CT orientation is not orthonormal")
    valid = np.ones(pixels.shape, bool)
    if hasattr(ds, "PixelPaddingValue"):
        low, high = sorted([float(ds.PixelPaddingValue), float(getattr(ds, "PixelPaddingRangeLimit", ds.PixelPaddingValue))])
        valid &= (pixels < low) | (pixels > high)
    hu = pixels.astype(np.float32) * slope + intercept
    if not np.isfinite(hu).all():
        raise ValueError("Non-finite HU values")
    patient = np.eye(4)
    patient[:3, 0] = orientation[:3] * spacing[1]
    patient[:3, 1] = orientation[3:] * spacing[0]
    patient[:3, 2] = np.cross(orientation[:3], orientation[3:])
    patient[:3, 3] = position
    return CtSlice(hu, valid, {"pixel_spacing_row_column_mm": spacing.tolist(),
                               "position_patient_mm": position.tolist(), "orientation_patient": orientation.tolist(),
                               "native_pixel_xy_to_patient_mm_4x4": patient.tolist(),
                               "rescale_slope": slope, "rescale_intercept": intercept,
                               "shape": list(hu.shape), "hu_min": float(hu[valid].min()) if valid.any() else None,
                               "hu_max": float(hu[valid].max()) if valid.any() else None,
                               "series_uid": str(getattr(ds, "SeriesInstanceUID", "")),
                               "frame_of_reference_uid": str(getattr(ds, "FrameOfReferenceUID", "")),
                               "sop_uid": str(getattr(ds, "SOPInstanceUID", ""))})


def window_hu(hu, center=40., width=400.):
    if not np.isfinite([center, width]).all() or width <= 0:
        raise ValueError("Invalid display window")
    image = np.clip((hu - (center - width / 2)) * (255 / width), 0, 255).astype(np.uint8)
    return cv2.cvtColor(image, cv2.COLOR_GRAY2BGR)


def ct_body(ct, threshold=-400.):
    mask = ((ct.hu > threshold) & ct.valid).astype(np.uint8)
    spacing = ct.metadata["pixel_spacing_row_column_mm"]
    # Structuring element specified in mm, not an arbitrary number of pixels.
    size = tuple(max(3, int(round(2.5 / step)) | 1) for step in reversed(spacing))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, size))
    return largest_filled(mask)


def normalize_mask(mask, spacing_row_column, target_mm):
    if not np.isfinite(target_mm) or target_mm <= 0:
        raise ValueError("Target spacing must be positive")
    sy, sx = np.asarray(spacing_row_column, float) / target_mm
    # Pixel centers at index 0 remain at the same patient position.
    transform = np.diag([sx, sy, 1.])
    size = (int(np.ceil((mask.shape[1] - 1) * sx)) + 1,
            int(np.ceil((mask.shape[0] - 1) * sy)) + 1)
    if min(size) < 1 or max(size) > 4096:
        raise ValueError("Requested CT registration grid exceeds supported size")
    return binary(cv2.warpAffine(mask, transform[:2], size, flags=cv2.INTER_NEAREST)), transform
