"""Compare two saved baselines and verify the candidate against existing app exports."""
import argparse
from collections import Counter
from datetime import datetime
import html
import json
from pathlib import Path

import cv2
import numpy as np

from prepare import DEFAULT_INDICES, digest, load_session, matrix, orient, read_image, write_png


def render_inputs(fixed, moving, registrations, pair):
    fi, mi = pair["fixed_index"], pair["moving_index"]
    a = next(s for s in fixed["slices"] if s["index"] == fi)
    b = next(s for s in moving["slices"] if s["index"] == mi)
    photo = orient(read_image(a["file_path"]), a)
    ct = orient(read_image(b["file_path"]), b)
    aligned = cv2.warpAffine(ct, matrix(registrations[(fi, mi)])[:2],
                            (photo.shape[1], photo.shape[0]), flags=cv2.INTER_LINEAR,
                            borderMode=cv2.BORDER_CONSTANT)
    return photo, aligned


def compare(args):
    old = load_session(args.original)
    candidate = load_session(args.candidate)
    output = Path(args.output).resolve()
    for directory in [Path(args.original).resolve().parent, Path(args.candidate).resolve().parent,
                      Path(args.exports).resolve()]:
        if output == directory or directory in output.parents:
            raise ValueError("Output cannot be inside input directories")
    output.mkdir(parents=True, exist_ok=False)
    (output / "images").mkdir()
    selected = {int(v) for v in args.indices.split(",")}
    result = {"status": "running", "versions": {"opencv": cv2.__version__, "numpy": np.__version__},
              "sessions": [], "parity": [], "examples": []}
    for path, data in [(args.original, old), (args.candidate, candidate)]:
        result["sessions"].append({"path": str(Path(path).resolve()), "sha256": digest(path),
                                   "modified": datetime.fromtimestamp(Path(path).stat().st_mtime).isoformat(),
                                   "moving_directory": data[3]["directory"],
                                   "registration_types": dict(Counter(r.get("transform_type") for r in data[4].values()))})
    old_pairs = {p["fixed_index"]: p for p in old[1]["pairs"] if p.get("valid")}
    try:
        for pair in candidate[1]["pairs"]:
            if not pair.get("valid"):
                continue
            fi = pair["fixed_index"]
            try:
                photo, aligned = render_inputs(candidate[2], candidate[3], candidate[4], pair)
                export = Path(args.exports) / args.export_pattern.format(index=fi)
                if export.is_file():
                    actual = read_image(export)
                    if actual.shape != aligned.shape:
                        parity = {"status": "shape_mismatch"}
                    else:
                        delta = np.abs(actual.astype(float) - aligned.astype(float))
                        parity = {"status": "identical" if not np.any(delta) else "different",
                                  "max_absolute_error": float(delta.max()), "mean_absolute_error": float(delta.mean())}
                    parity.update(path=str(export), sha256=digest(export))
                else:
                    parity = {"status": "missing_export"}
                result["parity"].append({"fixed_index": fi, **parity})
                if fi in selected:
                    old_photo, old_aligned = render_inputs(old[2], old[3], old[4], old_pairs[fi])
                    if not np.array_equal(old_photo, photo):
                        raise ValueError("Reference photos differ between sessions")
                    for label, image in {"original": cv2.addWeighted(photo, .5, old_aligned, .5, 0),
                                         "candidate": cv2.addWeighted(photo, .5, aligned, .5, 0),
                                         "photo": photo, "ct": aligned}.items():
                        write_png(output / "images" / f"{fi:04d}_{label}.png", image)
                    result["examples"].append(fi)
            except (ValueError, KeyError, OSError, cv2.error, StopIteration) as exc:
                result["parity"].append({"fixed_index": fi, "status": "error", "error": str(exc)})
        result["summary"] = dict(Counter(r["status"] for r in result["parity"]))
        result["status"] = "compared"
        labels = ["Sessão inicialmente indicada (abril)", "Sessão candidata com landmarks/interpolação (maio)"]
        descriptions = "".join(f"<p>{label}: <code>{html.escape(s['path'])}</code><br>CT: <code>{html.escape(s['moving_directory'])}</code></p>"
                               for label, s in zip(labels, result["sessions"]))
        options = "".join(f'<option value="{i:04d}">Head{i:03d}</option>' for i in result["examples"])
        page = '''<!doctype html><html lang="pt-BR"><meta charset="utf-8"><title>Comparação das sessões</title>
<style>body{font:16px system-ui;background:#20252b;color:#eee;margin:24px}code{overflow-wrap:anywhere}.panels{display:flex;gap:20px}.panels div{flex:1}img{max-width:100%;max-height:75vh}select{font:inherit}a{color:#9cf}</style>
<h1>Comparação das sessões salvas</h1>DESCRIPTIONS
<p>As duas colunas usam sobreposição 50%. Nenhuma otimização ou deformação foi executada. A sessão de maio é candidata; a escolha do alinhamento de referência cabe à revisão.</p>
<p>Paridade com exportações existentes: SUMMARY. Igualdade comprova reprodução dos pixels exportados, não precisão anatômica.</p>
<select id="slice">OPTIONS</select><div class="panels"><div><h2>Sessão de abril</h2><img id="old"></div><div><h2>Sessão de maio</h2><img id="new"></div></div>
<p><a href="comparison.json">Auditoria por corte e identificação das entradas</a></p>
<script>const s=document.getElementById('slice');function u(){document.getElementById('old').src='images/'+s.value+'_original.png';document.getElementById('new').src='images/'+s.value+'_candidate.png'}s.onchange=u;u();</script></html>'''
        (output / "comparison.html").write_text(page.replace("DESCRIPTIONS", descriptions).replace("SUMMARY", html.escape(str(result["summary"]))).replace("OPTIONS", options), encoding="utf-8")
    finally:
        (output / "comparison.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(result["summary"])
    print(output / "comparison.html")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--exports", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--indices", default=DEFAULT_INDICES)
    parser.add_argument("--export-pattern", default="CT_{index:03d}.png")
    compare(parser.parse_args())
