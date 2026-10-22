"""Collect independent global-registration experiments into a local review."""
import argparse
import glob
import html
import json
import os
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", required=True, help="Glob matching run directories")
    parser.add_argument("--output", required=True, help="New report directory")
    args = parser.parse_args()
    runs = sorted(Path(p).resolve() for p in glob.glob(args.runs) if (Path(p) / "result.json").is_file())
    if not runs:
        parser.error("No result.json files found")
    output = Path(args.output).resolve()
    if any(output == run or run in output.parents for run in runs):
        parser.error("Output must be outside run directories")
    output.mkdir(parents=True, exist_ok=False)
    cards, summary = [], []
    for run in runs:
        result = json.loads((run / "result.json").read_text(encoding="utf-8"))
        relative = Path(os.path.relpath(run, output)).as_posix()
        score = result.get("body_dice")
        row = {"run": str(run), "status": result["status"], "body_dice": score,
               "warnings": result.get("warnings", [])}
        summary.append(row)
        label = html.escape(Path(result["inputs"]["fixed"]["path"]).name)
        warnings = "".join(f"<li>{html.escape(w)}</li>" for w in row["warnings"])
        cards.append(f'<article><h2>{label}</h2><p>Dice corporal: {score:.4f}</p><a href="{relative}/review.html"><img src="{relative}/blend.png" alt="Sobreposição candidata"></a><ul>{warnings}</ul></article>')
    page = '''<!doctype html><html lang="pt-BR"><meta charset="utf-8"><title>Piloto automático</title>
<style>body{font:16px system-ui;background:#20252b;color:#eee;margin:24px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:20px}article{background:#303640;padding:16px}img{width:100%}a{color:#9cf}li{margin-bottom:8px}</style>
<h1>Piloto de alinhamento global automático</h1><p>Resultados independentes por corte, sem matrizes salvas. Clique numa imagem para revisar entradas e candidato.</p>
<p>O Dice avalia apenas o contorno corporal. Ainda não há continuidade entre cortes nem deformação local. Saltos de orientação e desvios internos precisam ser revisados.</p><main>CARDS</main></html>'''
    (output / "review.html").write_text(page.replace("CARDS", "".join(cards)), encoding="utf-8")
    (output / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(output / "review.html")


if __name__ == "__main__":
    main()
