#!/usr/bin/env python3
"""B1213: build the per-character timestamp table from the observer session.

Inputs (the committed session artefacts):
  session/timeline.tsv   -- driver step markers: "<epoch>  BEGIN|END  <step>"
  session/video.mp4.t0   -- the epoch at which the continuous video started
  tables/row_layout.tsv  -- the row: row_index, char, class, items, x, y, note

Output:
  tables/timestamps.tsv  -- one row per character: the video-relative window in
                            which the observer observed it.

The window for character i is [BEGIN goto_ii, END still_ii]: from when the
observer starts moving to the character through the end of its 20 s hold. The
20 s hold sub-window (still_ii) is reported separately. All times are seconds
from the start of the ONE continuous video (session/video.mp4), so a clip of
[ t_start, t_end ] shows character i.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SESSION = ROOT.parent / "session" if (ROOT.parent / "session").is_dir() \
    else Path("/mnt/projects/fable/review/B1213/session")
TIMELINE = SESSION / "timeline.tsv"
T0 = SESSION / "video.mp4.t0"
LAYOUT = ROOT / "tables" / "row_layout.tsv"
OUT = ROOT / "tables" / "timestamps.tsv"


def read_timeline(path):
    marks = {}
    for line in path.read_text().splitlines():
        p = line.split("\t")
        if len(p) != 3:
            continue
        epoch, kind, step = float(p[0]), p[1], p[2]
        # the driver prefixes each step name with its 1-based number
        # ("09_goto_00"); strip it so we can match on the logical name.
        if "_" in step:
            head, _, tail = step.partition("_")
            if head.isdigit():
                step = tail
        marks.setdefault(step, {})[kind] = epoch
    return marks


def read_layout(path):
    rows = []
    for line in path.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        p = line.split("\t")
        if len(p) < 8 or p[0] == "row_index":
            continue
        rows.append(p)
    return rows


def main():
    t0 = float(T0.read_text().strip())
    marks = read_timeline(TIMELINE)
    rows = read_layout(LAYOUT)
    out = ["row_index\tchar\tclass\titems\tt_start_s\tt_end_s\t"
           "hold_start_s\thold_end_s\thold_s"]
    missing = []
    for p in rows:
        i = int(p[0]); ii = "%02d" % i
        g = marks.get("goto_%s" % ii, {})
        s = marks.get("still_%s" % ii, {})
        if "BEGIN" not in g or "END" not in s:
            missing.append(p[1])
            continue
        t_start = g["BEGIN"] - t0
        t_end = s["END"] - t0
        h_start = s["BEGIN"] - t0
        h_end = s["END"] - t0
        out.append("%s\t%s\t%s\t%s\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f"
                   % (p[0], p[1], p[2], p[3], t_start, t_end, h_start, h_end,
                      h_end - h_start))
    OUT.write_text("\n".join(out) + "\n")
    print("wrote %s (%d/%d characters)" % (OUT, len(out) - 1, len(rows)))
    if missing:
        print("MISSING windows for: %s" % ", ".join(missing), file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
