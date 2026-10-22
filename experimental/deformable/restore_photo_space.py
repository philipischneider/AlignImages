"""Lift annotations with the fixed crop applied to the anatomically aligned photo stack."""
import argparse
import csv
import json
from pathlib import Path

import cv2
import numpy as np

from prepare import digest, read_image, write_png


def restore_mask(mask, original_shape, crop):
    x, y, width, height = crop
    if mask.ndim != 2 or mask.shape != (height, width):
        raise ValueError("Mask must match the documented crop exactly; no implicit resize")
    h, w = original_shape[:2]
    if min(x, y) < 0 or x + width > w or y + height > h:
        raise ValueError("Crop exceeds original photograph bounds")
    restored = np.zeros((h, w), mask.dtype)
    restored[y:y + height, x:x + width] = mask
    support = np.zeros((h, w), np.uint8)
    support[y:y + height, x:x + width] = 255
    return restored, support


def gradient_correlation(original_crop, processed):
    a = cv2.cvtColor(original_crop, cv2.COLOR_BGR2GRAY).astype(np.float32)
    b = cv2.cvtColor(processed, cv2.COLOR_BGR2GRAY).astype(np.float32)
    def magnitude(image):
        image = cv2.GaussianBlur(image, (3, 3), .8)
        return cv2.magnitude(cv2.Sobel(image, cv2.CV_32F, 1, 0), cv2.Sobel(image, cv2.CV_32F, 0, 1))
    hsv = cv2.cvtColor(processed, cv2.COLOR_BGR2HSV)
    tissue = cv2.inRange(hsv, (80, 30, 20), (145, 255, 255)) == 0
    tissue = cv2.erode(tissue.astype(np.uint8), np.ones((7, 7), np.uint8)) != 0
    av, bv = magnitude(a)[tissue], magnitude(b)[tissue]
    if len(av) < 100 or av.std() < 1e-5 or bv.std() < 1e-5:
        return None
    return float(np.corrcoef(av, bv)[0, 1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--crop-table", required=True)
    parser.add_argument("--original-photos", required=True)
    parser.add_argument("--processed-photos", required=True)
    parser.add_argument("--masks", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--crop", default="699,183,650,850", help="One fixed x,y,width,height crop for the entire stack")
    args = parser.parse_args()
    common_crop = tuple(int(value) for value in args.crop.split(","))
    if len(common_crop) != 4 or min(common_crop[:2]) < 0 or min(common_crop[2:]) <= 0:
        parser.error("Crop must be x,y,width,height with positive dimensions")
    with Path(args.crop_table).open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    indices = [int(row["index"]) for row in rows]
    if len(set(indices)) != len(indices) or sorted(indices) != list(range(265)):
        parser.error("Expected unique documented indices 0..264")
    output = Path(args.output).resolve()
    for folder in [args.original_photos, args.processed_photos, args.masks, str(Path(args.crop_table).parent)]:
        folder = Path(folder).resolve()
        if output == folder or folder in output.parents:
            parser.error("Output cannot be inside an input directory")
    output.mkdir(parents=True, exist_ok=False)
    for name in ["masks", "annotation_support", "overlays"]:
        (output / name).mkdir()
    manifest = {"status": "running", "crop_table": str(Path(args.crop_table).resolve()),
                "crop_table_sha256": digest(args.crop_table), "parameters": vars(args),
                "method": "Exact integer translation by one fixed crop for all slices; no warping or resize",
                "common_crop": list(common_crop),
                "historical_table_role": "Filename correspondence and diagnostic template estimates, not applied crop coordinates",
                "outside_crop": "Unknown/unannotated; zero values outside support are not negative bone labels",
                "photos_are_referenced_not_modified": True, "slices": []}
    preview_indices = {0, 5, 100, 234, 235, 236, 237, 240, 250, 260, 264}
    try:
        for row in sorted(rows, key=lambda row: int(row["index"])):
            index = int(row["index"])
            original = Path(args.original_photos) / row["original_file"]
            processed = Path(args.processed_photos) / row["head_file"]
            mask_path = Path(args.masks) / f"slice_{index:04d}.png"
            image, cropped, mask = read_image(original), read_image(processed), read_image(mask_path, cv2.IMREAD_UNCHANGED)
            if image.shape[:2] != (int(row["original_height"]), int(row["original_width"])):
                raise ValueError(f"Original grid differs from crop provenance at {index}")
            crop = common_crop
            if cropped.shape[:2] != mask.shape:
                raise ValueError(f"Processed photograph and mask grids differ at {index}")
            restored, support = restore_mask(mask, image.shape, crop)
            x, y, width, height = crop
            if not np.array_equal(restored[y:y + height, x:x + width], mask):
                raise ValueError(f"Mask round-trip failed at {index}")
            corr = gradient_correlation(image[y:y + height, x:x + width], cropped)
            confidence = float(row["match_score"])
            needs_review = corr is None or corr < .99
            destination = output / "masks" / mask_path.name
            write_png(destination, restored)
            write_png(output / "annotation_support" / mask_path.name, support)
            manifest["slices"].append({"index": index, "photo_path": str(original.resolve()),
                                       "mask_path": str(destination), "crop": list(crop),
                                       "crop_to_original_matrix_3x3": [1, 0, x, 0, 1, y, 0, 0, 1],
                                       "original_match_score": confidence, "gradient_correlation": corr,
                                       "historical_template_estimated_crop": [int(row[key]) for key in ["crop_x", "crop_y", "crop_width", "crop_height"]],
                                       "status": "needs_review" if needs_review else "restored",
                                       "foreground_preserved": int(np.count_nonzero(restored)) == int(np.count_nonzero(mask)),
                                       "input_sha256": {"photo": digest(original), "mask": digest(mask_path)}})
            if index in preview_indices:
                overlay = image.copy()
                contours, _ = cv2.findContours((restored != 0).astype(np.uint8), cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
                cv2.drawContours(overlay, contours, -1, (220, 80, 220), 2)
                cv2.rectangle(overlay, (x, y), (x + width - 1, y + height - 1), (0, 230, 255), 2)
                write_png(output / "overlays" / f"Head{index:03d}.png", overlay)
            if index % 25 == 0:
                print(f"Restored {index + 1}/265", flush=True)
        manifest["status"] = "needs_review"
    except KeyboardInterrupt:
        manifest["status"] = "cancelled"
        raise
    except Exception as exc:
        manifest.update(status="failed", error=str(exc))
        raise
    finally:
        (output / "photo_stack.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
