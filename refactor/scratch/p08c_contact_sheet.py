#!/usr/bin/env python3
"""P08c contact-sheet builder: pixel-diff each entity technique variant
against the baseline (default entity path), at the 3 entity-heavy pinned
poses (bf355, bf626, bf791 -- top-3 by entities_visible per P08b's e205
data), x86 llvmpipe frame dumps already produced by
--dump-frames 355,626,791 --dump-png-prefix <label>. Same PIL
ImageChops.difference() method as scripts/ci/visual_gate.py
(compare_images_exact / save_diff_artifact) for consistency with the
project's existing pixel-diff convention. Deleted after the unit? NO --
this one stays (it's a reusable reporting helper, not a Pi driver), but is
not wired into CI; ad hoc per this unit's plan.
"""
import json
from pathlib import Path
from PIL import Image, ImageChops

OUT = Path("/mnt/projects/fable/wt/slot1/benchmarks/contact_sheets/p08c_entity_drawpath")
POSES = [355, 626, 791]
VARIANTS = ["batch", "composite", "lod", "directgl", "directgl_composite"]
BASELINE = "baseline"


def diff_stats(path_a, path_b, diff_out):
    a = Image.open(path_a).convert("RGB")
    b = Image.open(path_b).convert("RGB")
    if a.size != b.size:
        return {"error": f"size mismatch {a.size} vs {b.size}"}
    diff = ImageChops.difference(a, b)
    diff.save(diff_out)
    data = list(diff.getdata())
    total_px = len(data)
    diff_px = sum(1 for px in data if any(px))
    max_channel_diff = max((max(px) for px in data), default=0)
    sum_abs = sum(sum(px) for px in data)
    mean_abs_diff = sum_abs / (total_px * 3)
    return {
        "size": list(a.size),
        "total_pixels": total_px,
        "differing_pixels": diff_px,
        "differing_pct": round(100.0 * diff_px / total_px, 3),
        "max_channel_diff": max_channel_diff,
        "mean_abs_channel_diff": round(mean_abs_diff, 4),
    }


def main():
    stats = {}
    for pose in POSES:
        stats[str(pose)] = {}
        base_png = OUT / f"{BASELINE}_f{pose}.png"
        if not base_png.exists():
            print(f"MISSING baseline frame: {base_png}")
            continue
        for variant in VARIANTS:
            v_png = OUT / f"{variant}_f{pose}.png"
            if not v_png.exists():
                print(f"MISSING variant frame: {v_png}")
                continue
            diff_out = OUT / f"{variant}_f{pose}_diff.png"
            s = diff_stats(base_png, v_png, diff_out)
            stats[str(pose)][variant] = s
            print(f"pose={pose} variant={variant}: {s}")

    (OUT / "stats.json").write_text(json.dumps(stats, indent=2))
    write_html(stats)


def write_html(stats):
    rows = []
    for pose in POSES:
        rows.append(f"<h2>Pose bf{pose}</h2>")
        rows.append("<div style='display:flex;gap:8px;flex-wrap:wrap;align-items:flex-start'>")
        rows.append(
            f"<div><div>baseline</div><img src='{BASELINE}_f{pose}.png' width='320'></div>"
        )
        for variant in VARIANTS:
            s = stats.get(str(pose), {}).get(variant, {})
            if not s or "error" in s:
                continue
            rows.append(
                f"<div><div>{variant}: {s['differing_pct']}% px, "
                f"mean|Δ|={s['mean_abs_channel_diff']}</div>"
                f"<img src='{variant}_f{pose}.png' width='320'>"
                f"<img src='{variant}_f{pose}_diff.png' width='320'></div>"
            )
        rows.append("</div>")
    html = (
        "<!doctype html><html><head><title>P08c entity draw-path contact sheet</title>"
        "<style>body{font-family:sans-serif;background:#111;color:#eee}"
        "img{border:1px solid #444;display:block}</style></head><body>"
        "<h1>P08c: entity draw-path technique contact sheet (befallen, e205, x86 llvmpipe)</h1>"
        "<p>Baseline (default/unbatched entity path) vs each technique, at 3 entity-heavy "
        "pinned poses. Second image per variant is the abs-diff (ImageChops.difference, "
        "same method as scripts/ci/visual_gate.py). Diffs are EXPECTED for these "
        "techniques (billboards / composite textures) -- reported for Will to judge, "
        "not self-declared acceptable.</p>"
        + "".join(rows)
        + "</body></html>"
    )
    (OUT / "index.html").write_text(html)
    print(f"wrote {OUT / 'index.html'}")


if __name__ == "__main__":
    main()
