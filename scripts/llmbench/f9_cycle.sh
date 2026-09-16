#!/usr/bin/env bash
# T492 Leg F step 1 -- MEASURE the official client's in-world F9 cycle.
#
# Will's review of the Leg E sheets found the official client in the TOP-DOWN
# camera where the recorder's truth line said Chase, so the press->camera map
# the recorder assumed (T466's successor matrix: FP -> Overhead -> Chase ->
# User1 -> User2 -> Tether -> FP, period 6) does not hold at this spot, and the
# hermes-ext note of 2026-09-16 saw a period of ~7.  This settles it by
# measurement, with nobody looking at a frame: press F9 nine times from the
# untouched login camera and score every grab against T466's six DIRECT-mode
# reference stills, taken at this same pose with the UI off.
#
#   f9_cycle.sh [presses]        (default 9)
#
# Output: review/T492/f9_cycle/{frames,scores.tsv,table.md,contact_sheet.png}
set -u
PRESSES="${1:-9}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"
OUT=/mnt/projects/fable/review/T492/f9_cycle
SCRATCH="${LLMB_SCRATCH:-/tmp/llmbench-T492F}"
export LLMB_SCRATCH="$SCRATCH" LLMB_P1_DIR="$OUT"
REF=/mnt/projects/fable/review/T466/oracle
# T466's stills are ROOT grabs (1024x768) with the UI hidden, so these are root
# grabs too and the comparison needs no geometry mapping at all (T478 section 2).
CAMS="FirstPerson Overhead Chase User1 User2 Tether"
# The scoring crop excludes the bottom UI band, so the number does not depend on
# whether F10 actually hid the UI -- the camera identity is in the scene.
CROP="1000x480+12+40"
SETTLE=3

rm -rf "$OUT"; mkdir -p "$OUT/frames" "$SCRATCH"
LOG="$OUT/f9_cycle.log"
say() { printf '[f9] %s %s\n' "$(date '+%H:%M:%S')" "$*" | tee -a "$LOG" >&2; }

if ! eqemu_lock_take "T492 Leg F f9_cycle"; then
    say "eqemu lock HELD by: $(eqemu_lock_show) -- refusing to run"; exit 64
fi
say "lock taken"
CLEANED=0
cleanup() {
    [ "$CLEANED" -eq 1 ] && return; CLEANED=1
    bash "$REPO/scripts/llmbench/teardown.sh" "$OUT" --release-lock >>"$LOG" 2>&1
    say "teardown done (world down, snapshot restored, lock released)"
}
trap cleanup EXIT INT TERM

if ! eqemu_port_open 5998 || ! eqemu_port_open 9000; then
    say "bringing eqemu up"; bash "$REPO/scripts/eqemu/up.sh" >>"$LOG" 2>&1 || { say "up.sh failed"; exit 65; }
fi
sql() { eq_docker exec "${MARIADB_CONTAINER}" mysql -uroot -p"$(mariadb_root_password)" peq -B -N -e "$1"; }
mkdir -p "$OUT/shared/db_before"
sql "SELECT id,name,zone_id,x,y,z,heading,level,exp FROM character_data ORDER BY id;"  > "$OUT/shared/db_before/snapshot.tsv"
sql "SELECT id,platinum,gold,silver,copper FROM character_currency ORDER BY id;"      >> "$OUT/shared/db_before/snapshot.tsv"
say "snapshot taken ($(wc -l < "$OUT/shared/db_before/snapshot.tsv") rows)"

# T310's pose pinning: bounce the zone (a linkdead cache would hand back the old
# pose and give a perfectly flat, perfectly meaningless sweep), write, read back.
bash "$REPO/scripts/camera_match_pose.sh" Oraclealpha 2 -74 428 3.751 160 qeynos2 >>"$LOG" 2>&1 \
    || { say "pose pinning failed"; exit 65; }
say "pose pinned at T466/T478's spot: -74 428 3.751 h160"

bash "$REPO/scripts/llmbench/oracle_start.sh" willeq_a WillEqH03Pass f9cycle >>"$LOG" 2>&1 \
    || { say "official client never reached the world"; exit 67; }
say "official client in world"
O="$REPO/scripts/llmbench/o.sh"
bash "$O" cmd "#set time 11 0" "" </dev/null >>"$LOG" 2>&1
bash "$O" cmd "#set weather 0" "" </dev/null >>"$LOG" 2>&1
sleep 3
bash "$O" key F10 </dev/null >>"$LOG" 2>&1     # hide the UI, as T466's stills are
sleep "$SETTLE"
say "UI toggled off (F10); grabbing press 0 = the untouched login camera"
bash "$O" pair "press0" </dev/null >>"$LOG" 2>&1
for i in $(seq 1 "$PRESSES"); do
    bash "$O" key F9 </dev/null >>"$LOG" 2>&1
    sleep "$SETTLE"                              # the Leg E settle: a grab taken
    bash "$O" pair "press${i}" </dev/null >>"$LOG" 2>&1   # right after a keypress
    say "press ${i} grabbed"                     # is a picture of the frame before it
done

# ---- score every grab against the six T466 direct-mode stills ---------------
say "scoring ${PRESSES} presses against the six T466 reference stills"
python3 - "$OUT" "$REF" "$CROP" "$PRESSES" "$CAMS" <<'PY' | tee -a "$LOG"
import glob, os, re, subprocess, sys
out, ref, crop, presses, cams = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5].split()
score_py = "/mnt/projects/fable/wt/t3/scripts/camera_match_score.py"
def score(a, b):
    r = subprocess.run(["python3", score_py, "--oracle", a, "--candidate", b,
                        "--crop", crop, "--candidate-crop", crop],
                       capture_output=True, text=True)
    m = re.search(r"([0-9.]+)\s+([0-9.]+)\s*$", r.stdout.strip().splitlines()[-1]) if r.stdout.strip() else None
    return float(m.group(1)) if m else float("nan")
rows = []
for i in range(0, presses + 1):
    g = glob.glob(f"{out}/frames/*_press{i}_root.png")
    if not g:
        rows.append((i, None, {})); continue
    s = {c: score(f"{ref}/T466_oracle_h160_direct_{c}_noui_root_1024x768.png", g[0]) for c in cams}
    best = min(s, key=s.get)
    rows.append((i, (best if s[best] < 0.30 else f"unnamed_{i}"), s))
with open(f"{out}/scores.tsv", "w") as f:
    f.write("press\tbest\t" + "\t".join(cams) + "\n")
    for i, best, s in rows:
        f.write(f"{i}\t{best}\t" + "\t".join(f"{s.get(c,float('nan')):.4f}" for c in cams) + "\n")
for i, best, s in rows:
    print(f"press {i}: {best}  " + "  ".join(f"{c}={s.get(c,float('nan')):.3f}" for c in cams))
PY
say "scores in $OUT/scores.tsv"
cleanup
