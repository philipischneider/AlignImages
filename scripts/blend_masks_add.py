"""
Blend all mask sequence folders using additive blend (cv2.add with saturation at 255).
Excludes the 'composite' subfolder. Output goes to 'composite'.
"""

import cv2
import numpy as np
from pathlib import Path
import sys

BASE = Path(r"D:\Visible Human Project\nlm_visible_human_project - Working Folder\VHP-M\1.3.6.1.4.1.5962.1.2.1174.1672334394.26545\masks_png")
OUT = BASE / "composite"
OUT.mkdir(exist_ok=True)

folders = sorted([d for d in BASE.iterdir() if d.is_dir() and d.name != "composite"])
if not folders:
    print("No source folders found.")
    sys.exit(1)

print(f"Source folders ({len(folders)}): {[f.name for f in folders]}")

slices = sorted(folders[0].glob("*.png"))
total = len(slices)
print(f"Slices per folder: {total}")

for i, slice_path in enumerate(slices):
    name = slice_path.name
    result = None

    for folder in folders:
        src = folder / name
        if not src.exists():
            print(f"  WARNING: {src} not found, skipping.")
            continue
        img = cv2.imread(str(src), cv2.IMREAD_UNCHANGED)
        if img is None:
            print(f"  WARNING: could not read {src}, skipping.")
            continue
        if result is None:
            result = img.astype(np.uint16)
        else:
            if img.shape != result.shape:
                img = cv2.resize(img, (result.shape[1], result.shape[0]))
            result = result + img.astype(np.uint16)

    if result is not None:
        result = np.clip(result, 0, 255).astype(np.uint8)
        cv2.imwrite(str(OUT / name), result)

    if (i + 1) % 50 == 0 or (i + 1) == total:
        print(f"  {i + 1}/{total} done")

print(f"Done. Output: {OUT}")
