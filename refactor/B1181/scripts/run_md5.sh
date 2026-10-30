#!/usr/bin/env bash
# B1181 fix leg: stream the census frame dirs from the ThinkPad into the
# evidence copy, then run the md5 census (make_census.py) and the
# same-binary determinism check (tour_93 probe1 vs the census run).
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
EV=/home/user/kb-evidence/kb-b1181-item-missile
G=/home/user/kb-frames/kb-b1181-item-missile/gate
ROWS=$EV/gate_census/rows.tsv
for tree in base tip; do
  mkdir -p "$EV/gate_frames/$tree"
  while IFS=$'\t' read -r tag rest; do
    [ -z "$tag" ] && continue
    rsync -a --delete "thinkpad:$G/$tree/$tag/" "$EV/gate_frames/$tree/$tag/" < /dev/null
  done < <(awk -F'\t' '{print $1}' "$ROWS")
done
rsync -a "thinkpad:$G/base/tour_93_probe1/" "$EV/gate_frames/base/tour_93_probe1/" < /dev/null
rsync -a "thinkpad:$G/tip/tour_93_probe1/" "$EV/gate_frames/tip/tour_93_probe1/" < /dev/null
python3 -I "$SCRIPT_DIR/make_census.py"
# determinism: two runs of the SAME binary, same plan (tour_93)
python3 - "$EV" <<'EOF'
import hashlib, os, sys
ev = sys.argv[1]
def md5(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()
for tree in ("base", "tip"):
    a = sorted(f for f in os.listdir(f"{ev}/gate_frames/{tree}/tour_93_probe1") if f.endswith(".png"))
    b = sorted(f for f in os.listdir(f"{ev}/gate_frames/{tree}/tour_93") if f.endswith(".png"))
    same = a == b and all(md5(f"{ev}/gate_frames/{tree}/tour_93_probe1/{f}") == md5(f"{ev}/gate_frames/{tree}/tour_93/{f}") for f in a)
    print(f"determinism {tree}/tour_93: {len(a)} frames, identical={'yes' if same else 'NO'}")
EOF
