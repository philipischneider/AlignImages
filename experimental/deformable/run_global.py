"""Run automatic global registration of a new CT/photo pair without session data."""
import argparse
from dataclasses import asdict
import json
import html
from pathlib import Path

import cv2
import numpy as np

from global_registration import Settings, binary, extract_body, register
from prepare import digest, read_image, write_png
from dicom_ct import read_ct, ct_body, normalize_mask, window_hu


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixed", required=True, help="Reference photograph PNG")
    parser.add_argument("--moving", required=True, help="Original CT DICOM (PNG retained for older experiments)")
    parser.add_argument("--ct-spacing-mm", type=float, default=0.527344, help="DICOM in-plane registration grid spacing, mm")
    parser.add_argument("--ct-body-threshold-hu", type=float, default=-400.)
    parser.add_argument("--fixed-body", help="Optional reviewed photograph body mask")
    parser.add_argument("--moving-body", help="Optional reviewed CT body mask")
    parser.add_argument("--fixed-bone")
    parser.add_argument("--reference-fixed-bone", help="Reviewed photo mask for visualization only, not an optimizer input")
    parser.add_argument("--moving-bone")
    parser.add_argument("--allow-vertical-reflection", action="store_true", help="Explicitly search mirrored acquisition orientations")
    parser.add_argument("--working-size", type=int, default=256)
    parser.add_argument("--max-evaluations", type=int, default=1800)
    parser.add_argument("--output", required=True, help="New directory outside input directories")
    args = parser.parse_args(argv)
    inputs = {key: Path(value).resolve() for key, value in vars(args).items()
              if key in ["fixed", "moving", "fixed_body", "moving_body", "fixed_bone", "moving_bone", "reference_fixed_bone"] and value}
    if bool(args.fixed_bone) != bool(args.moving_bone):
        parser.error("Bone guidance requires both --fixed-bone and --moving-bone")
    output = Path(args.output).resolve()
    for path in inputs.values():
        if not path.is_file():
            parser.error(f"Input does not exist: {path}")
        if output == path.parent or path.parent in output.parents:
            parser.error("Output must be outside input directories")
    if output.exists():
        parser.error("Output directory already exists")
    ct = read_ct(args.moving) if Path(args.moving).suffix.lower() == ".dcm" else None
    fixed = read_image(args.fixed)
    moving = window_hu(ct.hu) if ct else read_image(args.moving)
    if args.reference_fixed_bone:
        reference = read_image(args.reference_fixed_bone, cv2.IMREAD_UNCHANGED)
        if reference.ndim != 2 or reference.shape != fixed.shape[:2]:
            raise ValueError("Reference mask must share the full photograph grid")
    def mask(name, image, modality):
        value = read_image(inputs[name], cv2.IMREAD_UNCHANGED) if name in inputs else extract_body(image, modality)
        if value.shape != image.shape[:2]:
            raise ValueError(f"{name} must share its image grid")
        return binary(value)
    fixed_body = mask("fixed_body", fixed, "photo")
    moving_body = ct_body(ct, args.ct_body_threshold_hu) if ct and not args.moving_body else mask("moving_body", moving, "ct_png")
    fb = read_image(args.fixed_bone, cv2.IMREAD_UNCHANGED) if args.fixed_bone else None
    mb = read_image(args.moving_bone, cv2.IMREAD_UNCHANGED) if args.moving_bone else None
    source_to_registration = np.eye(3)
    registration_body, registration_bone = moving_body, mb
    if ct:
        spacing = ct.metadata["pixel_spacing_row_column_mm"]
        registration_body, source_to_registration = normalize_mask(moving_body, spacing, args.ct_spacing_mm)
        if mb is not None:
            if mb.shape != moving_body.shape:
                raise ValueError("Moving bone mask must share the native DICOM grid")
            registration_bone, _ = normalize_mask(mb, spacing, args.ct_spacing_mm)
    settings = Settings(working_size=args.working_size, max_evaluations=args.max_evaluations,
                        allow_vertical_reflection=args.allow_vertical_reflection)
    output.mkdir(parents=True)
    record = {"status": "running", "parameters": vars(args), "settings": asdict(settings),
              "inputs": {key: {"path": str(path), "sha256": digest(path)} for key, path in inputs.items()},
              "versions": {"opencv": cv2.__version__, "numpy": np.__version__},
              "body_extraction": {"fixed": "provided" if args.fixed_body else "warm_tissue_nonblue_largest_filled_component",
                                  "moving": "provided" if args.moving_body else "dicom_hu_threshold_largest_filled_component" if ct else "png_otsu_largest_filled_component"},
              "moving_source_type": "original_dicom_ct" if ct else "png",
              "units": "Forward maps native moving pixel XY to photo pixel XY; photo XY calibration and longitudinal coupling remain pending"}
    if ct:
        record["dicom_geometry"] = ct.metadata
        record["registration_grid"] = {"spacing_mm": args.ct_spacing_mm, "shape": list(registration_body.shape),
                                       "native_to_registration_matrix_3x3": source_to_registration.ravel().tolist(),
                                       "note": "In-plane isotropic resampling; no common patient FOV or longitudinal registration yet"}
        record["display_window"] = {"center": 40., "width": 400., "units": "HU"}
    write_png(output / "fixed_body.png", fixed_body * 255)
    write_png(output / "moving_body.png", moving_body * 255)
    try:
        result = register(fixed_body, registration_body, fb, registration_bone, settings)
        if ct:
            for candidate in result["candidates"]:
                candidate["registration_grid_forward_matrix_3x3"] = candidate["forward_matrix_3x3"]
                native = np.asarray(candidate["forward_matrix_3x3"]).reshape(3, 3) @ source_to_registration
                candidate["forward_matrix_3x3"] = native.ravel().tolist()
            result["forward_matrix_3x3"] = result["candidates"][0]["forward_matrix_3x3"]
            result["inverse_matrix_3x3"] = np.linalg.inv(np.asarray(result["forward_matrix_3x3"]).reshape(3, 3)).ravel().tolist()
        record.update(result)
        transform = np.asarray(result["forward_matrix_3x3"]).reshape(3, 3)
        aligned = cv2.warpAffine(moving, transform[:2], (fixed.shape[1], fixed.shape[0]), flags=cv2.INTER_LINEAR)
        if ct:
            aligned_hu = cv2.warpAffine(ct.hu, transform[:2], (fixed.shape[1], fixed.shape[0]), flags=cv2.INTER_LINEAR, borderValue=-1024.)
            support = cv2.warpAffine(ct.valid.astype(np.uint8), transform[:2], (fixed.shape[1], fixed.shape[0]), flags=cv2.INTER_NEAREST)
            np.savez_compressed(output / "aligned_ct_hu.npz", hu=aligned_hu, valid=support)
        for name, image in {"fixed": fixed, "aligned": aligned, "blend": cv2.addWeighted(fixed, .5, aligned, .5, 0)}.items():
            write_png(output / f"{name}.png", image)
        page = '''<!doctype html><html lang="pt-BR"><meta charset="utf-8"><title>Alinhamento automático global</title>
<style>body{font:16px system-ui;background:#20252b;color:#eee;margin:24px}img{max-width:48%;max-height:75vh}a{color:#9cf}</style>
<h1>Candidato automático global</h1><p>Sem matrizes de sessão. Resultado exige revisão anatômica. Ainda sem deformação local, continuidade Z ou calibração física.</p>
<p>BODY_SCORE</p><ul>WARNINGS</ul>
<p>Dados, hipóteses e avisos: <a href="result.json">result.json</a>. Confira as máscaras de entrada: <a href="fixed_body.png">fotografia</a> / <a href="moving_body.png">CT</a>.</p>
<img src="fixed.png" alt="Fotografia"><img src="blend.png" alt="Candidato sobreposto à fotografia">
<p><a href="aligned.png">CT transformada</a></p></html>'''
        page = page.replace("BODY_SCORE", f"Dice do limite corporal: {result['body_dice']:.4f} (não mede alinhamento interno)")
        page = page.replace("WARNINGS", "".join(f"<li>{html.escape(warning)}</li>" for warning in result["warnings"]))
        (output / "review.html").write_text(page, encoding="utf-8")
        print(f"Body Dice: {result['body_dice']:.4f}; state: {result['status']}")
        print(output / "review.html")
    except KeyboardInterrupt:
        record["status"] = "cancelled"
        raise
    except Exception as exc:
        record.update(status="failed", error=str(exc))
        raise
    finally:
        (output / "result.json").write_text(json.dumps(record, indent=2, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
