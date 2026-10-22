"""Process explicitly mapped original DICOM/photo pairs, without saved transforms."""
import argparse
import csv
import json
from pathlib import Path

from prepare import digest
from run_global import main as process_pair


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-map", required=True)
    parser.add_argument("--photos", required=True)
    parser.add_argument("--photo-pattern", default="Head{index:03d}.png", help="Use {source_number}.png for original VHP photographs")
    parser.add_argument("--reference-masks", help="Restored photo-space masks, for review only")
    parser.add_argument("--indices", default="234,235,236,237,240,250,260,264")
    parser.add_argument("--output", required=True)
    parser.add_argument("--ct-spacing-mm", type=float, default=.527344)
    parser.add_argument("--working-size", type=int, default=256)
    args = parser.parse_args()
    with Path(args.source_map).open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    mapping = {}
    for row in rows:
        index = int(row["aligned_mask_index"])
        if index in mapping:
            parser.error(f"Duplicate fixed index in source map: {index}")
        mapping[index] = row
    indices = [int(value) for value in args.indices.split(",")]
    for index in indices:
        if index not in mapping or not Path(mapping[index]["source_file"]).is_file():
            parser.error(f"Missing DICOM provenance for fixed index {index}")
        if not (Path(args.photos) / args.photo_pattern.format(index=index, source_number=int(mapping[index]["source_number"]))).is_file():
            parser.error(f"Missing photograph for fixed index {index}")
    output = Path(args.output).resolve()
    protected = [Path(args.photos).resolve(), Path(args.source_map).resolve().parent]
    protected.extend(Path(mapping[index]["source_file"]).resolve().parent for index in indices)
    if any(output == folder or folder in output.parents for folder in protected):
        parser.error("Output cannot be inside an input directory")
    output.mkdir(parents=True, exist_ok=False)
    manifest = {"status": "running", "source_map": str(Path(args.source_map).resolve()),
                "source_map_sha256": digest(args.source_map), "used_session_transform": False,
                "pairs": [], "requested_indices": indices}
    try:
        for index in indices:
            row = mapping[index]
            run = output / f"Head{index:03d}"
            command = ["--fixed", str(Path(args.photos) / args.photo_pattern.format(index=index, source_number=int(row["source_number"]))),
                          "--moving", row["source_file"], "--ct-spacing-mm", str(args.ct_spacing_mm),
                          "--working-size", str(args.working_size),
                          "--allow-vertical-reflection", "--output", str(run)]
            if args.reference_masks:
                command += ["--reference-fixed-bone", str(Path(args.reference_masks) / f"slice_{index:04d}.png")]
            process_pair(command)
            manifest["pairs"].append({"fixed_index": index, "source_number": int(row["source_number"]),
                                      "dicom_path": row["source_file"], "result": str(run / "result.json")})
        manifest["status"] = "needs_review"
    except KeyboardInterrupt:
        manifest["status"] = "cancelled"
        raise
    except Exception as exc:
        manifest.update(status="failed", error=str(exc))
        raise
    finally:
        (output / "dicom_pilot.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
