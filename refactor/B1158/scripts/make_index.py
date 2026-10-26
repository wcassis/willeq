#!/usr/bin/env python3
"""B1158: index.tsv + index.html from the per-clip alignment JSON lines.

usage: make_index.py <alignments.jsonl> <out-dir>
"""
import html
import json
import os
import sys

VERDICT_COLOR = {"PASS": "#2ea043", "short": "#d29922", "over": "#cf222e",
                 "n-a": "#8b949e", "n/a": "#8b949e", "PASS*": "#2ea043"}


def main():
    align_path, out_dir = sys.argv[1], sys.argv[2]
    rows = []
    with open(align_path) as f:
        for line in f:
            if line.strip():
                rows.append(json.loads(line))
    # stable order: gate first, then missiles, then observers
    def key(r):
        g = r.get("group", "")
        return (0 if g.startswith("gate") else 1 if g == "missile" else 2, r["id"])
    rows.sort(key=key)

    tsv_cols = ["id", "group", "title", "clip", "strip",
                "oracle_src", "oracle_fps", "oracle_t0", "oracle_frame_bc",
                "oracle_frame_act", "willeq_src", "willeq_frame_bc",
                "willeq_frame_act", "window", "verdicts"]
    with open(os.path.join(out_dir, "index.tsv"), "w") as f:
        f.write("\t".join(tsv_cols) + "\n")
        for r in rows:
            f.write("\t".join(str(x) for x in [
                r["id"], r.get("group", ""), r["title"],
                r.get("clip", ""), r.get("strip", ""),
                r.get("oracle_label", ""), str(r.get("oracle_fps", "")),
                f"{r.get('oracle_t0', 0):.3f}" if r.get("oracle_t0") else "",
                r.get("oracle_frame_bc", ""), r.get("oracle_frame_act", ""),
                r.get("willeq_label", ""),
                r.get("willeq_frame_bc", "") or "",
                r.get("willeq_frame_act", "") or "",
                r.get("window", ""), r.get("verdicts", ""),
            ]) + "\n")

    cards = []
    for r in rows:
        vchips = ""
        for part in r.get("verdicts", "").split("; "):
            if not part or "=" not in part:
                continue
            name, verdict = part.split("=", 1)
            col = VERDICT_COLOR.get(verdict, "#8b949e")
            vchips += (f'<span class="chip" style="background:{col}">'
                       f'{html.escape(name)} {html.escape(verdict)}</span> ')
        w = (f"frame {r['willeq_frame_bc']} (BC) / {r['willeq_frame_act']} (Act) @ 30 fps"
             if r.get("willeq_frame_bc") is not None else "no render in this gate")
        cards.append(f"""
      <div class="card">
        <a href="clips/{html.escape(r['clip'])}">{html.escape(r['title'])}</a>
        <img src="clips/{html.escape(r['strip'])}" loading="lazy" alt="">
        <div class="meta">oracle: {html.escape(r.get('oracle_label', ''))}
             f{r.get('oracle_frame_bc', '')} (BC) / f{r.get('oracle_frame_act', '')} (Act)
             @ {r.get('oracle_fps', '')} fps &nbsp;|&nbsp; willeq: {html.escape(r.get('willeq_label', ''))} {w}</div>
        <div class="meta">window {html.escape(r.get('window', ''))} &nbsp;|&nbsp;
             <a href="clips/{html.escape(r['clip'])}">clip</a> /
             <a href="clips/{html.escape(r['strip'])}">strip</a></div>
        <div class="chips">{vchips}</div>
      </div>""")
    n_w = sum(1 for r in rows if r.get("willeq_frame_bc") is not None)
    page = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8">
<title>B1158 visual FX ledger</title>
<style>
  body {{ font-family: system-ui, sans-serif; margin: 24px; background: #0d1117; color: #c9d1d9; }}
  h1 {{ color: #e6edf3; }}
  .card {{ background: #161b22; border: 1px solid #30363d; border-radius: 6px;
           padding: 10px 14px; margin: 10px 0; max-width: 1500px; }}
  .card a {{ color: #58a6ff; text-decoration: none; font-weight: 600; }}
  .card img {{ max-width: 100%; margin: 6px 0; border-radius: 3px; }}
  .meta {{ font-size: 12px; color: #8b949e; font-family: monospace; }}
  .chip {{ display: inline-block; color: #fff; font-size: 11px; font-weight: 600;
           padding: 2px 7px; border-radius: 8px; margin-right: 4px;
           font-family: monospace; }}
</style></head><body>
<h1>B1158 visual FX ledger</h1>
<p>{len(rows)} clips ({n_w} side-by-side, {len(rows) - n_w} oracle-only) -- oracle LEFT /
willeq RIGHT, aligned at OP_BeginCast (the stated frame rule; no hand offsets).
Verdicts from the gate's banded table (PASS = in band, short / over = outside,
see refactor/B1158/README.md).</p>
{''.join(cards)}
</body></html>
"""
    with open(os.path.join(out_dir, "index.html"), "w") as f:
        f.write(page)
    print(f"[index] {len(rows)} clips -> index.tsv + index.html", file=sys.stderr)


if __name__ == "__main__":
    main()
