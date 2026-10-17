#!/usr/bin/env bash
# T492 Leg A -- the three-player recorder for the Inspur LLM serving benchmark.
#
#   scripts/llmbench/record.sh <unit> <timeline.tsv> <corpus-name> [options]
#
# One timeline drives three simultaneous, interacting players against the real
# server, and every step yields, PER PLAYER, a frame + a marks line + a truth
# line the DRIVER writes from what it commanded.  Nothing here judges a frame.
#
#   p1  official Titanium client, dev host, Wine/Xvfb :99, Oraclealpha/willeq_a
#       (GM 250 -- every `#` command in the whole recording is issued here)
#   p2  willeq, dev host, surfaceless EGL/llvmpipe, Testcaster/willeq_test
#   p3  willeq, hermes-ext 172.16.200.100 over ssh, Testbuddy/willeq_buddy
#
# Options:
#   --lead N        seconds the willeq scripts burn before step 0 (default 150)
#   --hard-stop N   minutes after T0 at which the run aborts (default 75)
#   --stall N       seconds with no new client log line before abort (default 120)
#   --keep-server   leave the stack up after the restore (lock is still released)
#   --no-place      do not move the characters to the qeynos2 spot
#
# Exit codes: 0 ok | 64 lock held by another session | 65 server would not come
# up | 66 snapshot/restore mismatch | 67 a client never reached the world |
# 68 watchdog abort (dead client or hard stop).
set -u

UNIT="${1:?usage: record.sh <unit> <timeline.tsv> <corpus-name> [options]}"
TIMELINE="${2:?timeline}"
CORPUS="${3:?corpus name}"
shift 3

LEAD=150; HARD_STOP=75; STALL=120; KEEP_SERVER=0; PLACE=1; SETTLE=0; ORACLE_COND=0; GATE=""; SEED_WEAPON=0
while [ $# -gt 0 ]; do
    case "$1" in
        --lead) LEAD="$2"; shift 2 ;;
        --settle) SETTLE="$2"; shift 2 ;;
        --oracle-conditions) ORACLE_COND=1; shift ;;
        --gate) GATE="$2"; shift 2 ;;
        --seed-weapon) SEED_WEAPON=1; shift ;;
        --hard-stop) HARD_STOP="$2"; shift 2 ;;
        --stall) STALL="$2"; shift 2 ;;
        --keep-server) KEEP_SERVER=1; shift ;;
        --no-place) PLACE=0; shift ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"
# T493 (board B784): the snapshot/restore recipe lives in ONE place now and
# scenario.sh uses the same one. Do not re-inline it here.
source "$REPO/scripts/eqemu/restore_lib.sh"
# build-gles2/ is bootstrapped once per checkout and is NOT built inside a task
# worktree, so the binary comes from the main checkout unless one is told.
BIN_SRC="${LLMB_BIN:-}"
if [ -z "$BIN_SRC" ]; then
    for c in "$REPO/build-gles2/bin/willeq" /mnt/projects/fable/main/build-gles2/bin/willeq; do
        [ -x "$c" ] && { BIN_SRC="$c"; break; }
    done
fi

OUT="/mnt/projects/fable/review/${UNIT}/${CORPUS}"
SCRATCH="${LLMB_SCRATCH:-/tmp/llmbench-${UNIT}}"
HERMES="${LLMB_HERMES:-user@172.16.200.100}"
HERMES_OUT="/tmp/llmbench-${UNIT}-p3"
HERMES_REPO="${LLMB_HERMES_REPO:-/home/user/src/willeq}"
export LLMB_SCRATCH="$SCRATCH"
export LLMB_P1_DIR="$OUT/players/p1"

rm -rf "$OUT"; mkdir -p "$OUT/players/p1/frames" "$OUT/players/p1/chat" "$OUT/shared" "$SCRATCH"
LOG="$OUT/shared/record.log"
say() { printf '[record] %s %s\n' "$(date '+%H:%M:%S')" "$*" | tee -a "$LOG" >&2; }
ABORT="$SCRATCH/abort"; rm -f "$ABORT"

# ------------------------------------------------------------- preflight ----
# Five minutes of Wine/llvmpipe login is the expensive part of a live run, and
# T492's first dry run spent all of it before discovering that the willeq
# binary was not where the script looked.  Everything cheap is checked first.
pf=0
[ -n "$BIN_SRC" ] && [ -x "$BIN_SRC" ] || { echo "PREFLIGHT: no willeq binary (set LLMB_BIN)" >&2; pf=1; }
[ -d "$HOME/eq-oracle/client" ] || { echo "PREFLIGHT: no official client copy at ~/eq-oracle/client" >&2; pf=1; }
[ -f "$TIMELINE" ] || { echo "PREFLIGHT: no timeline at $TIMELINE" >&2; pf=1; }
[ -d /mnt/projects/EverQuest_ETC1 ] || { echo "PREFLIGHT: no --eqe1-path assets" >&2; pf=1; }
ssh -n -o BatchMode=yes -o ConnectTimeout=10 "$HERMES" \
    "test -x $HERMES_REPO/build-gles2/bin/willeq && test -d /mnt/projects/EverQuest_ETC1" \
    || { echo "PREFLIGHT: hermes ($HERMES) unreachable, or no binary/assets there" >&2; pf=1; }
for t in wine xdotool import tesseract ffmpeg identify; do
    command -v "$t" >/dev/null || { echo "PREFLIGHT: missing tool $t" >&2; pf=1; }
done
[ "$pf" -eq 0 ] || { echo "PREFLIGHT FAILED -- nothing was started, no lock was taken" >&2; exit 64; }
echo "[record] preflight ok: binary $BIN_SRC" >&2

# ---------------------------------------------------------------- the lock --
if ! eqemu_lock_take "${UNIT} llmbench record ${CORPUS}"; then
    say "eqemu lease HELD by: $(eqemu_lock_show) -- refusing to run"
    exit 64
fi
say "lease taken by ${UNIT}"
eqemu_lease_start_renewer

# scenario.sh's sql(), unchanged.  The statement travels as ARGV, never on
# stdin: `eq_remote` is `ssh -n`, which drops stdin, and a `mysql < dump.sql`
# through it silently imports nothing (board B729, which cost T478 a restore).
sql() { eqemu_sql "$@"; }   # restore_lib.sh

BEFORE="$OUT/shared/db_before/snapshot.tsv"
AFTER="$OUT/shared/db_after/snapshot.tsv"
mkdir -p "$OUT/shared/db_before" "$OUT/shared/db_after"

snapshot() { eqemu_snapshot "$1"; }   # scenario.sh's exact contract, shared
full_rows() {
    sql "SELECT * FROM character_data WHERE name IN ('Oraclealpha','Testcaster','Testbuddy','Testchar') ORDER BY id;" > "$1"
    sql "SELECT * FROM inventory WHERE character_id IN (65,67,68,74) ORDER BY character_id,slot_id;" >> "$1"
}
CHARS="65,67,68,74"
inv_snapshot() {
    # /equip, #summonitem and a death all move inventory rows, and scenario.sh's
    # snapshot does not cover them.  Kept as a full row dump so the restore is a
    # wholesale replace rather than a guess at which rows moved.
    eqemu_inventory_snapshot "$1" "${CHARS}"
}
inv_restore() {
    [ -s "$1" ] || return 0
    eqemu_inventory_restore "$1" "${CHARS}"
    say "inventory: $(wc -l < "$1") rows written back in one batch"
}

CLEANED=0; RESTORE_BAD=0; MAXSPAWN=""
kill_clients() {
    say "killing clients"
    pkill -f 'wine.*eqgame'    >/dev/null 2>&1
    kill ${P2_PID:-} >/dev/null 2>&1; sleep 2; kill -9 ${P2_PID:-} >/dev/null 2>&1
    kill ${P3_SSH_PID:-} >/dev/null 2>&1
    ssh -n -o BatchMode=yes "$HERMES" "pkill -f 'build-gles2/bin/willeq'; sleep 2; pkill -9 -f 'build-gles2/bin/willeq'; true" >/dev/null 2>&1
    pkill -f "Xvfb ${EQO_XVFB_DISPLAY:-:99}" >/dev/null 2>&1
}
restore() {
    [ -s "$BEFORE" ] || return 0
    say "restoring with the WORLD DOWN (the world writes character rows back on shutdown)"
    # restore_lib.sh's eqemu_world_stop: STOP THE CONTAINER, leave mariadb up.
    # `pkill` inside the container never worked (T493 measured the chain killing
    # its own shell) and the entrypoint supervisor respawns the fleet anyway.
    eqemu_world_stop
    if [ -n "$MAXSPAWN" ]; then
        # B720: `#npctypespawn` lands in zone_state_spawns at the zone save and
        # is restored at the next boot.  Every row this recording created has an
        # id above the one we noted before it started.
        local n
        n=$(sql "SELECT COUNT(*) FROM zone_state_spawns WHERE id > ${MAXSPAWN};")
        say "removing ${n} runtime spawn rows created by this run (id > ${MAXSPAWN})"
        sql "DELETE FROM zone_state_spawns WHERE id > ${MAXSPAWN};" >/dev/null
    fi
    if [ -n "${MAXCORPSE:-}" ]; then
        local c
        c=$(sql "SELECT COUNT(*) FROM character_corpses WHERE id > ${MAXCORPSE};")
        say "removing ${c} corpse rows this run created (id > ${MAXCORPSE})"
        sql "DELETE FROM character_corpse_items WHERE corpse_id > ${MAXCORPSE};" >/dev/null
        sql "DELETE FROM character_corpses WHERE id > ${MAXCORPSE};" >/dev/null
    fi
    inv_restore "$OUT/shared/db_before/inventory.tsv"
    eqemu_sql "$(eqemu_restore_sql_from "$BEFORE")" >/dev/null
    snapshot "$AFTER"
    full_rows "$OUT/shared/db_after/full_rows.tsv"
    inv_snapshot "$OUT/shared/db_after/inventory.tsv"
    if diff -q "$OUT/shared/db_before/inventory.tsv" "$OUT/shared/db_after/inventory.tsv" >/dev/null 2>&1; then
        say "inventory restored BYTE-IDENTICALLY"
    else
        say "INVENTORY MISMATCH -- see shared/db_{before,after}/inventory.tsv"
        RESTORE_BAD=1
    fi
    if diff -q "$BEFORE" "$AFTER" >/dev/null 2>&1; then
        say "snapshot restored BYTE-IDENTICALLY"
    else
        say "SNAPSHOT MISMATCH -- $BEFORE vs $AFTER"
        diff "$BEFORE" "$AFTER" | head -20 | tee -a "$LOG" >&2
        RESTORE_BAD=1
    fi
    diff "$OUT/shared/db_before/full_rows.tsv" "$OUT/shared/db_after/full_rows.tsv" \
        > "$OUT/shared/db_after/full_rows.diff" 2>&1
    say "full-row diff: $(wc -l < "$OUT/shared/db_after/full_rows.diff") lines (non-snapshot columns such as last_login always move -- T478)"
}
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    kill ${WATCHDOG_PID:-} >/dev/null 2>&1
    kill_clients
    restore
    if [ "$KEEP_SERVER" -eq 0 ]; then
        bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/shared/server.log" 2>&1
        say "server down (whole compose project)"
    else
        eqemu_world_start
        say "eqemu-server container restarted (--keep-server)"
    fi
    eqemu_lease_stop_renewer
    eqemu_lock_release
    say "lease released"
}
trap cleanup EXIT INT TERM

# ------------------------------------------------------------- server up ----
# T497 (board B797): this gate used to be `eqemu_port_open 5998 && 9000`, a
# host-side connect() that docker-proxy answers with the fleet at zero
# processes -- so this script skipped up.sh against a dead stack.
if ! eqemu_stack_ready; then
    say "bringing eqemu up"
    bash "$REPO/scripts/eqemu/up.sh" > "$OUT/shared/server.log" 2>&1 || { say "up.sh failed"; exit 65; }
else
    say "eqemu already up (world + loginserver running AND listening inside the container)"
fi

snapshot "$BEFORE"; full_rows "$OUT/shared/db_before/full_rows.tsv"
inv_snapshot "$OUT/shared/db_before/inventory.tsv"
MAXSPAWN=$(sql "SELECT COALESCE(MAX(id),0) FROM zone_state_spawns;")
MAXCORPSE=$(sql "SELECT COALESCE(MAX(id),0) FROM character_corpses;")
say "snapshot: $(wc -l < "$BEFORE") rows, $(wc -l < "$OUT/shared/db_before/inventory.tsv") inventory rows; zone_state_spawns max id ${MAXSPAWN}; character_corpses max id ${MAXCORPSE}"

# ------------------------------------------- put the three on one spot ------
if [ "$PLACE" -eq 1 ]; then
    say "placing the three characters in qeynos2 at T478's canonical spot"
    sql "UPDATE character_data SET zone_id=2,x=-74,y=428,z=3.751,heading=160 WHERE name='Oraclealpha';
         UPDATE character_data SET zone_id=2,x=-84,y=428,z=3.751,heading=160 WHERE name='Testcaster';
         UPDATE character_data SET zone_id=2,x=-64,y=428,z=3.751,heading=160 WHERE name='Testbuddy';" >/dev/null
    sql "SELECT name,zone_id,x,y,z,heading FROM character_data WHERE name IN ('Oraclealpha','Testcaster','Testbuddy');" \
        | tee "$OUT/shared/placement.tsv" >> "$LOG"
fi

# B793: neither willeq character has a WEAPON in any general slot -- their
# spares are food, drink, spells, bandages and (Testcaster) a Shuriken -- so an
# in-game `/equip <general> 13` has nothing legal to move and Leg C's attempt
# was refused outright.  Seed one, AFTER the inventory snapshot so the restore
# takes it out again.  9997 is Dagger* (IT5), a one-hand weapon both can hold.
#
# T494 (board B796): the slot is 23, NOT 21.  `inventory.slot_id` is the
# SERVER's (RoF2) numbering, in which 21 is slotPowerSource --
# common/patches/titanium.cpp:3576 ServerToTitaniumSlot(21) returns
# SLOT_INVALID, so a row seeded at 21 is never sent to a Titanium client and
# the timelines' `/equip 21 13` was addressing the AMMO slot (DB 22) instead.
# 23 is general slot 1.  This was the defect UNDER B796's experiment, which is
# why the experiment's own result could not be read straight.
if [ "$SEED_WEAPON" -eq 1 ]; then
    sql "DELETE FROM inventory WHERE character_id IN (67,68) AND slot_id=23;
         INSERT INTO inventory (character_id,slot_id,item_id,charges,color) VALUES
           (67,23,9997,1,0),(68,23,9997,1,0);" >/dev/null
    say "seeded item 9997 (Dagger*) into slot 23 (general 1) for 67 and 68 -- removed by the restore"
fi

# --------------------------------------------------- compile the timeline ---
python3 "$REPO/scripts/llmbench/compile_timeline.py" "$TIMELINE" "$OUT" \
    --lead "$LEAD" --settle "$SETTLE" --frames-root "$OUT" --p3-frames-root "$HERMES_OUT" \
    | tee -a "$LOG"
[ -s "$OUT/steps.tsv" ] || { say "timeline compiled to nothing"; exit 64; }
cp "$TIMELINE" "$OUT/shared/timeline.tsv"

# --------------------------------------------------- per-player configs -----
mk_cfg() {  # mk_cfg <file> <user> <pass> <character>
cat > "$1" <<JSON
{
  "clients": [
    { "host": "172.16.200.99", "port": 5998, "user": "$2", "pass": "$3",
      "server": "WillEQ", "character": "$4",
      "eq_client_path": "/mnt/projects/EverQuestP1999",
      "maps_path": "/mnt/projects/maps/base",
      "navmesh_path": "/mnt/projects/maps/nav" }
  ],
  "audio": { "enabled": false }
}
JSON
}
mk_cfg "$OUT/players/p2/client.json" willeq_test  WillEqH03Pass    Testcaster
mk_cfg "$OUT/players/p3/client.json" willeq_buddy WillEqBuddyPass  Testbuddy

cat > "$OUT/players/p1/client.txt" <<TXT
official
host=devhost (this machine), Wine/Xvfb ${EQO_XVFB_DISPLAY:-:99} 1024x768
account=willeq_a character=Oraclealpha status=250 (GM -- the ONLY account that issues # commands)
window=1023x738 at root (-1,30); root grab 1024x768
TXT
cat > "$OUT/players/p2/client.txt" <<TXT
willeq
host=devhost (this machine), surfaceless EGL + llvmpipe, no display
account=willeq_test character=Testcaster
binary=${SCRATCH}/willeq (local copy of ${BIN_SRC} with cap_net_raw,cap_net_admin+ep)
flags=--renderer gles2 -r 1024 768 --no-audio --eqe1-path /mnt/projects/EverQuest_ETC1 -d 4 --log-level=DEBUG --log-module=GRAPHICS:INFO
TXT
cat > "$OUT/players/p3/client.txt" <<TXT
willeq
host=hermes-ext 172.16.200.100 (no NFS; assets at identical /mnt/projects paths), surfaceless EGL + llvmpipe
account=willeq_buddy character=Testbuddy status=0 (NOT a GM)
binary=${HERMES_REPO}/build-gles2/bin/willeq with cap_net_raw,cap_net_admin+ep
flags=--renderer gles2 -r 1024 768 --no-audio --eqe1-path /mnt/projects/EverQuest_ETC1 -d 4 --log-level=DEBUG --log-module=GRAPHICS:INFO
clock=see manifest.json hermes_clock_offset_s; run.log keeps this host's own clock, marks/truth are shifted to the dev host's
TXT

# ---------------------------------------------------------- p1: official ----
say "p1: starting the official client (this takes minutes under llvmpipe)"
if ! bash "$REPO/scripts/llmbench/oracle_start.sh" willeq_a WillEqH03Pass "$CORPUS" >>"$LOG" 2>&1; then
    say "p1 never reached the world -- see $LOG and $SCRATCH/oracle/"
    exit 67
fi
say "p1 in world"
if [ "$ORACLE_COND" -eq 1 ]; then
    # T492 Leg F, MEASURED: a camera-identification score against T466's
    # reference stills is only meaningful under the conditions those stills were
    # taken in.  f9_cycle.sh pins the pose with camera_match_pose.sh and sets
    # `#set time 11 0` / `#set weather 0`, and identified all ten of its grabs at
    # 0.058-0.191.  Leg F step 2 did neither, and the SAME scoring never got
    # below 0.45 for any camera -- every `camto` failed and the run was thrown
    # away.  Anything that scores against those stills must set this.
    bash "$REPO/scripts/llmbench/o.sh" cmd "#set time 11 0" "" </dev/null >>"$LOG" 2>&1
    bash "$REPO/scripts/llmbench/o.sh" cmd "#set weather 0" "" </dev/null >>"$LOG" 2>&1
    say "oracle conditions set (#set time 11 0, #set weather 0)"
fi

# ------------------------------------------------------------ p2, p3 --------
BIN2="$SCRATCH/willeq"
cp -f "$BIN_SRC" "$BIN2"
sudo -n setcap cap_net_raw,cap_net_admin+ep "$BIN2" || say "WARNING: setcap failed on p2's binary -- zone load fails ~40% without it (B511)"
mkdir -p "$OUT/players/p2/frames"
say "p2: launching willeq (Testcaster) on the dev host"
( cd "$REPO" && WILLEQ_DATA_ROOT="$REPO" "$BIN2" -c "$OUT/players/p2/client.json" \
    --renderer gles2 -r 1024 768 --no-audio --eqe1-path /mnt/projects/EverQuest_ETC1 \
    -d 4 --log-level=DEBUG --log-module=GRAPHICS:INFO --script "$OUT/players/p2/script.txt" \
    > "$OUT/players/p2/run.log" 2>&1 ) &
P2_PID=$!
P2_LAUNCH=$(date +%s)

say "p3: launching willeq (Testbuddy) on hermes-ext"
scp -q -o BatchMode=yes "$OUT/players/p3/script.txt" "$OUT/players/p3/client.json" "$HERMES:/tmp/" || { say "scp to hermes failed"; exit 67; }
# The remote side is prepared FIRST, in a call that does return.
ssh -n -o BatchMode=yes "$HERMES" "
  rm -rf $HERMES_OUT; mkdir -p $HERMES_OUT/players/p3/frames
  mv /tmp/script.txt /tmp/client.json $HERMES_OUT/
  sudo -n setcap cap_net_raw,cap_net_admin+ep $HERMES_REPO/build-gles2/bin/willeq" \
  </dev/null >>"$LOG" 2>&1 || { say "p3 preparation failed"; exit 67; }
# MEASURED T492 (dry runs 2 and 3, and an isolated probe with `setsid sleep 300`):
# an ssh whose remote command backgrounds a long-lived process does NOT return,
# even with `ssh -n`, `nohup`, `setsid` and stdin/stdout/stderr all redirected --
# the session channel stays open for the life of that process.  p3's client was
# in world ten seconds after launch both times while the launching ssh sat there
# for 4m40s and 5m40s, and T0 (= launch + lead) went by before the driver reached
# its loop.  So the ssh is backgrounded LOCALLY and never waited on; readiness is
# polled through separate short ssh calls, exactly as p2's is polled through its
# local log.
ssh -n -o BatchMode=yes "$HERMES" "
  cd $HERMES_REPO && WILLEQ_DATA_ROOT=$HERMES_REPO ./build-gles2/bin/willeq \
      -c $HERMES_OUT/client.json --renderer gles2 -r 1024 768 --no-audio \
      --eqe1-path /mnt/projects/EverQuest_ETC1 -d 4 --log-level=DEBUG --log-module=GRAPHICS:INFO \
      --script $HERMES_OUT/script.txt > $HERMES_OUT/run.log 2>&1;
  echo p3_exit=\$? >> $HERMES_OUT/run.log" </dev/null >>"$LOG" 2>&1 &
P3_SSH_PID=$!
say "p3 launch ssh backgrounded (pid $P3_SSH_PID), $(( $(date +%s) - P2_LAUNCH ))s after p2"

# ------------------------------------------------ wait for both in world ----
READY="Graphics loading complete. Game ready!"
say "waiting for p2 and p3 to zone in (deadline ${LEAD}s)"
DEADLINE=$((P2_LAUNCH + LEAD))
p2_ready() { grep -qF "$READY" "$OUT/players/p2/run.log" 2>/dev/null; }
p3_ready() { ssh -o BatchMode=yes "$HERMES" "grep -qF '$READY' $HERMES_OUT/run.log" 2>/dev/null; }
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
    p2_ready && p3_ready && break
    sleep 5
done
p2_ready || { say "p2 never reported '$READY'"; exit 67; }
p3_ready || { say "p3 never reported '$READY'"; exit 67; }
say "p2 and p3 in world"

O_GATE="$REPO/scripts/llmbench/o.sh"; O="$O_GATE"
# ----------------------------------------------------------------- gate -----
# T492 Leg G.  Leg F spent a whole lock session recording 32 steps against a
# check that never passed once: every `camto` failed and every target OCR
# returned a fragment, and none of it was usable.  So the checks are run ONCE,
# here, with all three clients in world -- which is the condition that matters,
# because the other two players stand 10 and 10 units away and are IN the
# official client's frame, unlike T466's reference stills -- and if they do not
# pass, nothing is recorded at all.
if [ -n "$GATE" ]; then
    GATEOUT="$OUT/shared/gate.tsv"; : > "$GATEOUT"
    gate_fail=0
    say "GATE: all three clients in world; checking the official client before recording"
    GCAM="${GATE%%,*}"; GTGT="${GATE#*,}"
    if [ -n "$GCAM" ]; then
        if out=$(bash "$O_GATE" camto "$GCAM" 9 0.30 </dev/null 2>&1); then
            say "GATE camera OK: $out"; printf 'camera	%s	PASS	%s
' "$GCAM" "$out" >> "$GATEOUT"
        else
            say "GATE camera FAILED for $GCAM"; printf '%s
' "$out" | tee -a "$LOG" >&2
            printf 'camera	%s	FAIL	%s
' "$GCAM" "$(printf '%s' "$out" | tr '\n' ' ')" >> "$GATEOUT"
            gate_fail=1
        fi
    fi
    if [ -n "$GTGT" ] && [ "$gate_fail" -eq 0 ]; then
        bash "$O_GATE" cmd "/target $GTGT" "" </dev/null >>"$LOG" 2>&1
        sleep "${SETTLE:-3}"
        if out=$(bash "$O_GATE" targetname "$GTGT" </dev/null 2>&1); then
            say "GATE target OK: $out"; printf 'target	%s	PASS	%s
' "$GTGT" "$out" >> "$GATEOUT"
        else
            say "GATE target FAILED for $GTGT: $out"
            printf 'target	%s	FAIL	%s
' "$GTGT" "$out" >> "$GATEOUT"
            gate_fail=1
        fi
    fi
    if [ "$gate_fail" -ne 0 ]; then
        say "GATE FAILED -- refusing to record. Scores are in $GATEOUT"
        exit 69
    fi
    say "GATE PASSED -- recording"
fi

# ------------------------------------------------------------- watchdog -----
# feedback-watch-long-live-runs: a dead client must not be discovered an hour
# late.  Stall = no new byte in a client's log for $STALL seconds.
(
  while :; do
    sleep 20
    now=$(date +%s)
    m2=$(stat -c %Y "$OUT/players/p2/run.log" 2>/dev/null || echo 0)
    m3=$(ssh -o BatchMode=yes "$HERMES" "stat -c %Y $HERMES_OUT/run.log 2>/dev/null || echo 0" 2>/dev/null || echo 0)
    if [ $((now - m2)) -gt "$STALL" ]; then echo "p2 stalled ($((now-m2))s)" > "$ABORT"; exit 0; fi
    if [ $((now - m3)) -gt "$STALL" ]; then echo "p3 stalled ($((now-m3))s)" > "$ABORT"; exit 0; fi
    # After Wine hands off, the `wine eqgame` wrapper is gone and only a
    # process matching `eqgame` remains -- the old pattern called a live client
    # dead and raised a false ABORT at the end of dry run 2.
    pgrep -f 'eqgame' >/dev/null 2>&1 || { echo "p1 (eqgame) is gone" > "$ABORT"; exit 0; }
  done
) & WATCHDOG_PID=$!

# -------------------------------------------------------------- the run -----
T0=$DEADLINE
# The willeq clients burn exactly $LEAD of `wait` before their first step,
# counted from their own launch, so T0 is fixed at launch + LEAD and the driver
# must reach this loop BEFORE it.  If it does not, the timeline is already in
# the past, the driver runs flat out, and the players never overlap -- which is
# exactly what dry run 2 recorded.  That corpus is not worth the lock: stop.
NOW=$(date +%s)
if [ "$NOW" -ge $((T0 - 15)) ]; then
    say "DESYNC: T0 ($(date -d @${T0} '+%H:%M:%S')) is already here or past at $(date -d @${NOW} '+%H:%M:%S')"
    say "-- the willeq scripts are on their own clock and the driver cannot catch up. Refusing to record."
    exit 67
fi
HARD=$((T0 + HARD_STOP * 60))
say "T0 = ${T0} ($(date -d @${T0} '+%H:%M:%S')); hard stop at $(date -d @${HARD} '+%H:%M:%S')"
echo -n > "$OUT/players/p1/truth.tsv"
DONE=0; ABORTED=""
while IFS=$'\t' read -r idx toff player label action truth; do
    [ -n "${idx:-}" ] || continue
    target=$(python3 -c "print(int($T0 + $toff))")
    while [ "$(date +%s)" -lt "$target" ]; do
        [ -f "$ABORT" ] && break 2
        [ "$(date +%s)" -ge "$HARD" ] && { echo "hard stop" > "$ABORT"; break 2; }
        sleep 1
    done
    say "step ${idx} t+${toff} ${player} ${label}"
    if [ "$player" = "p1" ] && [ "$action" != "nop" ]; then
        verb="${action%% *}"; rest="${action#* }"
        case "$verb" in
            # `cmd` takes ONE argument (chat text with spaces); the others are
            # argv lists.  Splitting cmd's text on spaces would type only the
            # first word -- the shape of a whole wasted live step.
            # `cmd <text>` or `cmd <text>|<expect regex>`: with an expect the
            # driver RETRIES up to three times and marks OK/FAILED from the
            # client's own chat, instead of typing once and hoping.
            cmd)  ctext="${rest%%|*}"; cexp=""
                  [ "$rest" != "$ctext" ] && cexp="${rest#*|}"
                  bash "$O" cmd "$ctext" "$cexp" >>"$LOG" 2>&1 </dev/null ;;
            *)    bash "$O" $verb $rest >>"$LOG" 2>&1 </dev/null ;;
        esac
    fi
    # T492 Leg F: on a targeting step, READ the official client's target window
    # and put what it says into p1's truth line, so the pair is checkable
    # against something the official client actually displayed rather than
    # against what the driver believes it commanded.
    TW=""
    case "$truth" in *target=*) TW=$(bash "$O" target 2>/dev/null </dev/null | tail -1) ;; esac
    # The official client needs the same settle the willeq scripts get: a grab
    # taken milliseconds after a keypress is a picture of the frame before it.
    [ "${SETTLE%.*}" -gt 0 ] 2>/dev/null && sleep "$SETTLE"
    # Every step gets an official window+root pair, so a match/discrepancy step
    # on ANY player has a paired official frame.
    PT=$(bash "$O" pair "$label" 2>/dev/null </dev/null | awk '{print $1}')
    [ -n "$PT" ] || PT=$(date '+%H:%M:%S.%3N')
    printf '%s\t%s;actor=%s;step=%s%s\n' "$PT" "$truth" "$player" "$idx" \
        "${TW:+;target_window=$TW}" >> "$OUT/players/p1/truth.tsv"
    DONE=$((DONE+1))
done < "$OUT/steps.tsv"
[ -f "$ABORT" ] && ABORTED="$(cat "$ABORT")"
say "driver finished ${DONE} of $(wc -l < "$OUT/steps.tsv") steps${ABORTED:+ -- ABORTED: $ABORTED}"
kill ${WATCHDOG_PID:-} >/dev/null 2>&1

# -------------------------------------------------- let the scripts end -----
say "waiting up to 120 s for the willeq scripts to /camp and /quit"
for _ in $(seq 1 24); do kill -0 ${P2_PID} 2>/dev/null || break; sleep 5; done

# ------------------------------------------------------------ harvest -------
kill_clients
say "harvesting p3 from hermes"
mkdir -p "$OUT/players/p3/frames"
scp -q -o BatchMode=yes "$HERMES:$HERMES_OUT/run.log" "$OUT/players/p3/run.log" 2>/dev/null
scp -q -o BatchMode=yes "$HERMES:$HERMES_OUT/players/p3/frames/*" "$OUT/players/p3/frames/" 2>/dev/null
OFFSET=$(bash "$REPO/scripts/llmbench/clock_offset.sh" "$HERMES" 5 2>/dev/null | tail -1)
OFFSET=${OFFSET:-0}
say "hermes clock offset vs dev host: ${OFFSET}s"
python3 "$REPO/scripts/llmbench/harvest.py" --steps "$OUT/steps.json" --player p2 \
    --player-dir "$OUT/players/p2" | tee -a "$LOG"
python3 "$REPO/scripts/llmbench/harvest.py" --steps "$OUT/steps.json" --player p3 \
    --player-dir "$OUT/players/p3" --clock-offset "$OFFSET" | tee -a "$LOG"

python3 - "$OUT" "$OFFSET" "$T0" "$UNIT" "${ABORTED:-}" <<'PY' | tee -a "$LOG"
import json, os, sys, glob, datetime
out, off, t0, unit, aborted = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5]
steps = json.load(open(os.path.join(out, "steps.json")))["steps"]
players = []
for p, kind, host in (("p1","official","devhost"),("p2","willeq","devhost"),("p3","willeq","hermes-ext 172.16.200.100")):
    d = os.path.join(out, "players", p)
    players.append({
        "id": p, "client": kind, "host": host,
        "client_txt": f"players/{p}/client.txt",
        "run_log": f"players/{p}/run.log" if os.path.exists(os.path.join(d,"run.log")) else None,
        "marks": f"players/{p}/marks.tsv", "truth": f"players/{p}/truth.tsv",
        "frames_dir": f"players/{p}/frames",
        "frames": len(glob.glob(os.path.join(d, "frames", "*.png"))),
        "marks_lines": sum(1 for _ in open(os.path.join(d,"marks.tsv"))) if os.path.exists(os.path.join(d,"marks.tsv")) else 0,
        "truth_lines": sum(1 for _ in open(os.path.join(d,"truth.tsv"))) if os.path.exists(os.path.join(d,"truth.tsv")) else 0,
        "clock": "devhost" if p != "p3" else "hermes-ext, shifted to devhost in marks/truth",
    })
json.dump({
  "unit": unit, "corpus": os.path.basename(out),
  "generated": datetime.datetime.now().isoformat(timespec="seconds"),
  "clock": "dev host local time (UTC on this host); timestamps are HH:MM:SS.mmm",
  "hermes_clock_offset_s": off,
  "t0_epoch": t0, "t0_local": datetime.datetime.fromtimestamp(t0).strftime("%H:%M:%S"),
  "steps_planned": len(steps), "aborted": aborted or None,
  "players": players,
  "timeline": "shared/timeline.tsv", "steps_table": "steps.tsv", "steps_json": "steps.json",
  "db_before": "shared/db_before/", "db_after": "shared/db_after/",
  "frame_geometry": {"official_window": "1023x738", "official_root": "1024x768", "willeq": "1024x768"},
}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
print("manifest.json written:", " ".join(f"{p['id']}={p['frames']}f/{p['marks_lines']}m/{p['truth_lines']}t" for p in players))
PY

cleanup
[ "$RESTORE_BAD" -eq 1 ] && exit 66
[ -n "${ABORTED:-}" ] && exit 68
exit 0
