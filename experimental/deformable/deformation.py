"""Lightweight composition of global matrices and regional inverse fields."""
import cv2
import numpy as np

FIELD_CONVENTION = "output_photo_x -> inverse_global(output_photo_x + residual_xy)"


def jacobian(field):
    dx_y, dx_x = np.gradient(field[:, :, 0])
    dy_y, dy_x = np.gradient(field[:, :, 1])
    return (1 + dx_x) * (1 + dy_y) - dx_y * dy_x


def native_maps(transform, shape, field=None, roi=None):
    y, x = np.indices(shape[:2], dtype=np.float32)
    if field is not None:
        if roi is None:
            raise ValueError("A residual field requires an explicit photo ROI")
        left, top, width, height = map(int, roi)
        if field.shape != (height, width, 2) or left < 0 or top < 0 or left + width > x.shape[1] or top + height > x.shape[0]:
            raise ValueError("Field domain does not match photograph grid")
        x[top:top + height, left:left + width] += field[:, :, 0]
        y[top:top + height, left:left + width] += field[:, :, 1]
    inverse = np.linalg.inv(transform)
    return ((inverse[0, 0] * x + inverse[0, 1] * y + inverse[0, 2]).astype(np.float32),
            (inverse[1, 0] * x + inverse[1, 1] * y + inverse[1, 2]).astype(np.float32))


def remap_native(image, transform, shape, field=None, roi=None, nearest=False, border=0):
    mx, my = native_maps(transform, shape, field, roi)
    return cv2.remap(image, mx, my, cv2.INTER_NEAREST if nearest else cv2.INTER_LINEAR,
                     borderMode=cv2.BORDER_CONSTANT, borderValue=border)
