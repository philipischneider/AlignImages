"""Read-only input audit and baseline review for deformable CT/photo experiments."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import sys

import cv2
import numpy as np
import pydicom

VERSION = "baseline_audit_v1"
DEFAULT_INDICES = "5,100,234,235,236,237,240,250,260,264"


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_image(path, flags=cv2.IMREAD_COLOR):
    image = cv2.imdecode(np.fromfile(str(path), dtype=np.uint8), flags)
    if image is None:
        raise ValueError(f"Cannot decode image: {path}")
    return image


def orient(image, record):
    rotation = record.get("rotation_degrees", 0)
    if rotation not in (0, 90, 180, 270):
        raise ValueError(f"Unsupported rotation: {rotation}")
    if rotation:
        image = np.rot90(image, -(rotation // 90))
    if record.get("flip_horizontal", False):
        image = np.flip(image, 1)
    if record.get("flip_vertical", False):
        image = np.flip(image, 0)
    return np.ascontiguousarray(image)


def matrix(registration):
    value = np.asarray(registration["forward_matrix_3x3"], dtype=float)
    if value.size != 9:
        raise ValueError("Forward matrix must have nine values")
    value = value.reshape(3, 3)
    if not np.isfinite(value).all() or not np.allclose(value[2], [0, 0, 1]):
        raise ValueError("Invalid affine matrix")
    if abs(np.linalg.det(value[:2, :2])) < 1e-10:
        raise ValueError("Singular affine matrix")
    return value


def build_preview(fixed, aligned, preferences):
    """Match BuildPreviewImage in ViewerPanel.cpp for saved UI preferences."""
    mode = preferences.get("preview_mode", "blend")
    if mode == "difference":
        return cv2.absdiff(fixed, aligned)
    if mode == "multiply":
        product = cv2.multiply(fixed.astype(np.float32) / 255, aligned.astype(np.float32) / 255)
        return (product * 255).round().clip(0, 255).astype(np.uint8)
    if mode == "checkerboard":
        tile = max(4, int(preferences.get("checker_size", 32)))
        y, x = np.indices(fixed.shape[:2])
        output = fixed.copy()
        use_moving = ((x // tile + y // tile) % 2) == 1
        output[use_moving] = aligned[use_moving]
        return output
    alpha = float(preferences.get("blend_alpha", .5))
    return cv2.addWeighted(fixed, 1 - alpha, aligned, alpha, 0)


def load_session(path, pairing_id=None):
    session = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    stacks = {stack["id"]: stack for stack in session["stacks"]}
    if len(stacks) != len(session["stacks"]):
        raise ValueError("Duplicate stack IDs")
    pairings = session.get("pairings")
    if pairings:
        selected = pairing_id or session.get("active_pairing_id")
        choices = [p for p in pairings if p.get("id") == selected]
        if not choices and len(pairings) == 1 and pairing_id is None:
            choices = pairings
        if len(choices) != 1:
            raise ValueError("Select an existing pairing using --pairing-id")
        pairing = choices[0]
    else:
        if pairing_id is not None:
            raise ValueError("Legacy session has no named pairings")
        pairing = session["pairing"]
    fixed_id = pairing.get("fixed_stack_id", session["stacks"][0]["id"])
    moving_id = pairing.get("moving_stack_id", session["stacks"][1]["id"])
    fixed, moving = stacks[fixed_id], stacks[moving_id]
    if fixed.get("is_dicom") or moving.get("is_dicom"):
        raise ValueError("This baseline tool currently requires PNG stacks")
    registrations = {}
    for reg in session.get("registrations", []):
        if reg.get("fixed_stack_id", fixed_id) != fixed_id or reg.get("moving_stack_id", moving_id) != moving_id:
            continue
        key = (reg["fixed_index"], reg["moving_index"])
        if key in registrations:
            raise ValueError(f"Ambiguous duplicate registration: {key}")
        registrations[key] = reg
    return session, pairing, fixed, moving, registrations


def geometry(path):
    ds = pydicom.dcmread(path, stop_before_pixels=True, specific_tags=[
        "Modality", "SeriesInstanceUID", "SOPInstanceUID", "Rows", "Columns",
        "PixelSpacing", "ImagePositionPatient", "ImageOrientationPatient", "InstanceNumber"])
    def numbers(key):
        return [float(v) for v in getattr(ds, key, [])]
    return {"path": str(path), "modality": str(getattr(ds, "Modality", "")),
            "series_uid": str(getattr(ds, "SeriesInstanceUID", "")),
            "sop_uid": str(getattr(ds, "SOPInstanceUID", "")),
            "rows": int(getattr(ds, "Rows", 0)), "columns": int(getattr(ds, "Columns", 0)),
            "pixel_spacing_mm": numbers("PixelSpacing"),
            "position_mm": numbers("ImagePositionPatient"),
            "orientation": numbers("ImageOrientationPatient")}


def audit_dicom(directory):
    records, errors = [], []
    for path in sorted(Path(directory).rglob("*.dcm")):
        try:
            records.append(geometry(path))
        except Exception as exc:
            errors.append({"path": str(path), "error": str(exc)})
    return {"records": records, "errors": errors}


def write_png(path, image):
    ok, encoded = cv2.imencode(".png", image)
    if not ok:
        raise ValueError(f"Cannot encode {path}")
    encoded.tofile(str(path))


def write_review(output, rows):
    cards = []
    for row in rows:
        if row["status"] != "rendered":
            continue
        stem = row["stem"]
        cards.append(f'<option value="{stem}">Head index {row["fixed_index"]} / CT index {row["moving_index"]}</option>')
    page = '''<!doctype html><html lang="pt-BR"><meta charset="utf-8">
<title>Revisão do alinhamento inicial</title>
<style>body{background:#20252b;color:#eee;font:16px system-ui;max-width:1100px;margin:24px auto}img{max-width:100%;max-height:76vh}label{margin-right:20px}p{line-height:1.5}button,select{font:inherit}</style>
<h1>Revisão do alinhamento inicial</h1>
<p>CT após a transformação salva. Máscara alvo revisada em magenta. Correspondência das máscaras ainda precisa de conferência. Nenhuma deformação local foi aplicada.</p>
<label>Corte <select id="slice">OPTIONS</select></label>
<label>Visualização <select id="mode"><option value="blend">Sobreposição foto / CT (50%)</option><option value="session">Modo salvo na sessão</option><option value="fixed">Fotografia</option><option value="aligned">CT alinhada</option><option value="target">Máscara alvo sobre fotografia</option></select></label>
<p><img id="view" alt="Selecione um corte válido"></p>
<p>Inventário e diagnósticos: <a href="manifest.json">manifest.json</a>. Esta revisão não demonstra correção anatômica.</p>
<script>const s=document.getElementById('slice'),m=document.getElementById('mode'),v=document.getElementById('view');function update(){if(s.value)v.src='images/'+s.value+'_'+m.value+'.png'}s.onchange=m.onchange=update;update();</script></html>'''
    (output / "review.html").write_text(page.replace("OPTIONS", "".join(cards)), encoding="utf-8")


def geometry_transitions(rows):
    """Diagnose acquisition changes; do not interpret them as anatomical jumps."""
    linked = [r for r in rows if "dicom_geometry" in r]
    changes = []
    for previous, current in zip(linked, linked[1:]):
        a, b = previous["dicom_geometry"], current["dicom_geometry"]
        keys = ["series_uid", "rows", "columns", "pixel_spacing_mm", "orientation"]
        changed = [key for key in keys if a[key] != b[key]]
        distance = None
        if len(a["orientation"]) == 6 and len(a["position_mm"]) == len(b["position_mm"]) == 3:
            direction = np.asarray(a["orientation"])
            normal = np.cross(direction[:3], direction[3:])
            if np.linalg.norm(normal) > 0:
                normal /= np.linalg.norm(normal)
                distance = abs(float(np.dot(np.asarray(b["position_mm"]) - a["position_mm"], normal)))
        current["distance_from_previous_linked_plane_mm"] = distance
        if changed:
            changes.append({"previous_fixed_index": previous["fixed_index"],
                            "fixed_index": current["fixed_index"], "changed_fields": changed,
                            "plane_distance_mm": distance})
    return changes


def prepare(args):
    session, pairing, fixed, moving, registrations = load_session(args.session, args.pairing_id)
    fixed_slices = {r["index"]: r for r in fixed["slices"]}
    moving_slices = {r["index"]: r for r in moving["slices"]}
    if len(fixed_slices) != len(fixed["slices"]) or len(moving_slices) != len(moving["slices"]):
        raise ValueError("Duplicate slice indices")
    chosen = {int(x) for x in args.indices.split(",")}
    output = Path(args.output).resolve()
    # Never reuse any directory or write into an input tree.
    input_dirs = [Path(args.session).resolve().parent, Path(args.masks).resolve(),
                  Path(fixed["directory"]).resolve(), Path(moving["directory"]).resolve()]
    if args.dicom_dir:
        input_dirs.append(Path(args.dicom_dir).resolve())
    if any(output == folder or folder in output.parents for folder in input_dirs):
        raise ValueError("Output must be outside all input directories")
    output.mkdir(parents=True, exist_ok=False)
    (output / "images").mkdir()
    manifest = {"version": VERSION, "status": "running", "session_path": str(Path(args.session).resolve()),
                "session_sha256": digest(args.session), "parameters": vars(args),
                "global_offset_informational_only": pairing.get("global_offset"),
                "warnings": ["Mask filenames are provisionally indexed by fixed slice; anatomical mapping is unconfirmed.",
                             "PNG to DICOM orientation/calibration is unconfirmed. No physical deformation is computed."],
                "pairs": [], "dicom": None}
    rows = manifest["pairs"]
    try:
        if args.dicom_dir:
            manifest["dicom"] = audit_dicom(args.dicom_dir)
        source_map = {}
        if args.source_map:
            manifest["source_map_sha256"] = digest(args.source_map)
            with Path(args.source_map).open(encoding="utf-8-sig", newline="") as stream:
                for record in csv.DictReader(stream):
                    number = int(record["source_number"])
                    if number in source_map:
                        raise ValueError(f"Duplicate source map number {number}")
                    source_map[number] = record
        seen = set()
        for pair in pairing["pairs"]:
            fi, mi = pair["fixed_index"], pair["moving_index"]
            row = {"fixed_index": fi, "moving_index": mi, "status": "invalid_pair"}
            rows.append(row)
            if not pair.get("valid", False):
                continue
            try:
                if (fi, mi) in seen:
                    raise ValueError("Duplicate valid pair")
                seen.add((fi, mi))
                a, b = fixed_slices[fi], moving_slices[mi]
                ap, bp = Path(a["file_path"]), Path(b["file_path"])
                mp = Path(args.masks) / args.mask_pattern.format(index=fi)
                for path in (ap, bp, mp):
                    if not path.is_file():
                        raise ValueError(f"Missing input: {path}")
                reg = registrations[(fi, mi)]
                transform = matrix(reg)
                row.update(fixed_path=str(ap), moving_path=str(bp), mask_path=str(mp),
                           forward_matrix_3x3=transform.ravel().tolist(),
                           registration_type=reg.get("transform_type"), status="validated_paths")
                # Filename lookup uses the explicit provenance CSV, never DICOM instance order.
                source = source_map.get(int(bp.stem)) if bp.stem.isdigit() else None
                if source:
                    row["dicom_provenance"] = source
                    row["dicom_geometry"] = geometry(source["source_file"])
                if fi not in chosen:
                    continue
                image_a = orient(read_image(ap), a)
                image_b = orient(read_image(bp), b)
                mask = orient(read_image(mp, cv2.IMREAD_UNCHANGED), a)
                if mask.ndim != 2 or mask.shape != image_a.shape[:2]:
                    raise ValueError(f"Mask grid mismatch: {mask.shape} vs {image_a.shape[:2]}")
                row["mask_values"] = np.unique(mask).tolist()
                row["mask_nonzero"] = int(np.count_nonzero(mask))
                row["input_sha256"] = {"fixed": digest(ap), "moving": digest(bp), "mask": digest(mp)}
                row["fixed_shape"] = list(image_a.shape)
                row["moving_shape"] = list(image_b.shape)
                aligned = cv2.warpAffine(image_b, transform[:2], (image_a.shape[1], image_a.shape[0]),
                                         flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
                target = image_a.copy()
                contours, _ = cv2.findContours((mask != 0).astype(np.uint8), cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
                cv2.drawContours(target, contours, -1, (220, 80, 220), 1)
                stem = f"fixed_{fi:04d}_moving_{mi:04d}"
                for label, image in {"fixed": image_a, "aligned": aligned, "blend": cv2.addWeighted(image_a, .5, aligned, .5, 0),
                                     "session": build_preview(image_a, aligned, session.get("ui", {})), "target": target}.items():
                    write_png(output / "images" / f"{stem}_{label}.png", image)
                row.update(status="rendered", stem=stem)
                print(f"Rendered fixed={fi}, moving={mi}", flush=True)
            except (ValueError, KeyError, OSError, cv2.error) as exc:
                row.update(status="error", error=str(exc))
        manifest["geometry_transitions"] = geometry_transitions(rows)
        manifest["requested_indices_without_render"] = sorted(chosen - {r["fixed_index"] for r in rows if r["status"] == "rendered"})
        manifest["status"] = "needs_review" if not any(r["status"] == "error" for r in rows) and not manifest["requested_indices_without_render"] else "incomplete"
        write_review(output, rows)
    except KeyboardInterrupt:
        manifest["status"] = "cancelled"
        raise
    except Exception:
        manifest["status"] = "failed"
        raise
    finally:
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"{manifest['status']}: {output / 'review.html'}")
    return 0 if manifest["status"] == "needs_review" else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", required=True)
    parser.add_argument("--masks", required=True)
    parser.add_argument("--output", required=True, help="New directory, outside input trees")
    parser.add_argument("--indices", default=DEFAULT_INDICES, help="Fixed stack indices to render")
    parser.add_argument("--mask-pattern", default="slice_{index:04d}.png")
    parser.add_argument("--pairing-id")
    parser.add_argument("--dicom-dir")
    parser.add_argument("--source-map", help="Provenance CSV containing source_number and source_file")
    args = parser.parse_args()
    try:
        return prepare(args)
    except KeyboardInterrupt:
        print("Cancelled; partial manifest saved when output was created.", file=sys.stderr)
        return 130
    except (ValueError, OSError, KeyError) as exc:
        parser.exit(2, f"Error: {exc}\n")


if __name__ == "__main__":
    sys.exit(main())
