#!/usr/bin/env bash
# B1067 (#387): the KEYBOARD step walk -- does a player walking into a small
# ground step get up it (and off a small drop), on the renderer's collision
# path (PlayerCameraComponent::resolveIrrlichtCollision, the region-triangle
# rays), the one only keyboard movement reaches? /move, --scenario and
# --script go through the game thread and the zone map instead, so this rig
# drives a real key: an x86 GLES2 build on llvmpipe in an Xvfb display
# (--egl-x11), W held with xdotool, the pose read from --pose-log.
#
# Usage: scripts/eqemu/step_walk.sh [--leg rise|drop|both] [--bin PATH]
#            [--out DIR] [--hold SECONDS] [--constrained PRESET]
#            [--unit B1067] [--display :67]
#   EQEMU_HOST / EQEMU_LANE pick the lane exactly as everywhere else.
#   The binary must be an EQT_GLES2 x86 build (scripts/build-gles2-x86.sh
#   -> build-gles2/bin/willeq) -- --egl-x11 is the only surface that takes
#   X key events.
#
# The legs (qeynos2, geometry from the zone's own map,
# /mnt/projects/maps/base/qeynos2.map, review/B1067/README.md):
#   a straight curb along renderer Y = 391.75 for renderer X = 16..76: floor
#   z = 0.0 for Y < 391.75, z = -1.0 for Y > 391.75, one level only (nothing
#   above it) between Y = 377 and Y = 418. RENDERER coordinates are the
#   client's (the pose log's playerX/playerY, HCMap after its INVERSEXY
#   undo): renderer X = SERVER y and renderer Y = SERVER x, so the seed below
#   writes character_data.x = renderer Y, .y = renderer X. Heading: the
#   client's heading0_512 = (640 - server heading) mod 512 (measured, pose
#   log: server 256 -> client 384, server 128 -> client 0), and the
#   client moves (sin h, cos h) in renderer XY.
#   rise: renderer start (40, 405) on z = -1, facing renderer -Y, walk UP.
#   drop: renderer start (40, 384) on z =  0, facing renderer +Y, walk OFF.
#   The seed z is 3 units above the floor; the first moving frame snaps the
#   player to it (a drop well inside the 2 x step-height snap band).
#
# Verdict per leg, from the pose log alone (player centre Z, player XY):
#   PASS when the player's Y crossed the edge by >= 2 units AND Z changed, from
#   the last row still short of the edge to the last row, by >= 0.75 of the
#   measured rise in the right direction (step_walk_verdict.py).
# Exit: 0 every leg PASS, 1 a leg FAILED, 2 a leg produced no verdict
# (no pose rows / never zoned), 64 lock held, 65 lane/server problem.
#
# Lane rules (docs/ci.md, local lane): the LOCAL stack (EQEMU_HOST=local) is
# also the server Will's XU4 play rig logs into, so on that lane this script
# NEVER stops the world, never bounces a zone, and puts the character's pose
# back with a plain UPDATE after the client has logged out. On a VM lane it
# brings the stack up if it is down and uses the standard snapshot restore
# (restore_lib.sh), downing the stack again only if it brought it up.

set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LEG="both"; BIN=""; OUT=""; HOLD="1.0"; PRESET=""; UNIT="B1067"; DISP=":67"
while [ $# -gt 0 ]; do
    case "$1" in
        --leg) LEG="$2"; shift 2 ;;
        --bin) BIN="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --hold) HOLD="$2"; shift 2 ;;
        --constrained) PRESET="$2"; shift 2 ;;
        --unit) UNIT="$2"; shift 2 ;;
        --display) DISP="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
case "$LEG" in rise|drop|both) ;; *) echo "--leg must be rise, drop or both" >&2; exit 64 ;; esac
[ -n "$BIN" ] || BIN="$REPO/build-gles2/bin/willeq"
[ -x "$BIN" ] || { echo "no willeq binary at $BIN (build with scripts/build-gles2-x86.sh)" >&2; exit 64; }
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/${UNIT}/step_walk-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT"

source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"
say() { printf '[step_walk] %s\n' "$*" | tee -a "$OUT/step_walk.log" >&2; }

CHAR="$EQ_TEST_CHARACTER"
ZONE_ID=2   # qeynos2's zoneidnumber (NOT the zone table PK -- zone_place_lib.sh's column trap)
EDGE_Y=391.75
RISE=1.0
# leg -> "server_x server_y server_z server_heading renderer_direction(+1/-1)"
leg_spec() {
    case "$1" in
        rise) echo "405 40 2 384 -1" ;;
        drop) echo "384 40 3 128 1" ;;
    esac
}

# ---- the client config, derived from the shipped zone_in config ----------
CFG="$OUT/client.json"
CLIENT_HOST="$(eqemu_port_host)"
eqemu_is_local && CLIENT_HOST="$(hostname -I | awk '{print $1}')"
sed "s/\"host\"\([[:space:]]*:[[:space:]]*\)\"[^\"]*\"/\"host\"\1\"${CLIENT_HOST}\"/" \
    "$REPO/scripts/scenario_configs/zone_in.json" > "$CFG"

# ---- 1. the lock ----------------------------------------------------------
if ! eqemu_lock_take "${UNIT} step_walk.sh"; then
    say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run"
    exit 64
fi
say "lock taken (lane ${EQEMU_LANE}, host ${EQEMU_HOST})"

UPPED=0
SNAP="$OUT/snapshot_before.tsv"
XVFB_PID=""; CLIENT_PID=""
POSE0=""
cleanup() {
    [ -n "$CLIENT_PID" ] && kill "$CLIENT_PID" 2>/dev/null
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
    if eqemu_is_local; then
        # The world stays up (Will's XU4 logs into it): put the pose back with
        # a plain UPDATE once the client is gone, read back until it sticks
        # (the zone's logout save can land after the first write).
        if [ -n "$POSE0" ]; then
            read -r z0 x0 y0 h0 zz0 <<<"$POSE0"
            for _ in 1 2 3 4 5 6; do
                sleep 5
                eqemu_sql "UPDATE character_data SET zone_id=${z0}, x=${x0}, y=${y0}, z=${zz0}, heading=${h0} WHERE name='${CHAR}';" >/dev/null
                got="$(eqemu_sql "SELECT CONCAT_WS(' ',zone_id,x,y,heading,z) FROM character_data WHERE name='${CHAR}';" | tail -1)"
                [ "$got" = "$POSE0" ] && { say "pose restored: $got"; break; }
                say "pose restore not settled (db='$got' want='$POSE0'), retrying"
            done
        fi
    elif [ -f "$SNAP" ]; then
        eqemu_restore_snapshot "$SNAP" "$OUT/snapshot_after.tsv" say || say "SNAPSHOT RESTORE MISMATCH"
        if [ "$UPPED" -eq 1 ]; then
            bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/server.log" 2>&1
        else
            eqemu_world_start
        fi
    fi
    eqemu_lock_release
    say "lock released"
}
trap cleanup EXIT INT TERM

# ---- 2. the server --------------------------------------------------------
if ! eqemu_stack_ready >/dev/null 2>&1; then
    if eqemu_is_local; then
        say "local stack is not ready -- this rig never starts or stops the local stack"; exit 65
    fi
    say "bringing lane ${EQEMU_LANE} up"
    bash "$REPO/scripts/eqemu/up.sh" >>"$OUT/server.log" 2>&1 || { say "up.sh failed"; exit 65; }
    UPPED=1
fi
if eqemu_is_local; then
    POSE0="$(eqemu_sql "SELECT CONCAT_WS(' ',zone_id,x,y,heading,z) FROM character_data WHERE name='${CHAR}';" | tail -1)"
    say "pose before: $POSE0"
else
    eqemu_snapshot "$SNAP"
fi

# ---- 3. one leg -----------------------------------------------------------
run_leg() {
    local leg="$1" spec x y z h dir log pose i got wid
    spec="$(leg_spec "$leg")"; read -r x y z h dir <<<"$spec"
    log="$OUT/${leg}_client.log"; pose="$OUT/${leg}_pose.tsv"
    # A zone holds a LINKDEAD copy of a character it has seen and hands THAT
    # pose back at the next login (zone_place_lib.sh's boot_zone note;
    # measured here: a second leg zoned in at the first leg's pose even after
    # a qeynos2 bounce -- the lane's zones are dynamic processes). On a VM lane
    # the seed is therefore written with the world DOWN and the stack brought
    # up after it. NEVER on the local lane (Will's XU4 logs into it): there
    # the character is offline and one leg is one run.
    if ! eqemu_is_local; then
        # Stop the eqemu-server CONTAINER only (restore_lib.sh's own recipe):
        # mariadb is a separate container and must stay up for the seed.
        eqemu_world_stop
    fi
    for i in 1 2 3 4 5 6; do
        eqemu_sql "UPDATE character_data SET zone_id=${ZONE_ID}, x=${x}, y=${y}, z=${z}, heading=${h} WHERE name='${CHAR}';" >/dev/null
        sleep 1
        got="$(eqemu_sql "SELECT CONCAT_WS(' ',zone_id,ROUND(x),ROUND(y),ROUND(z),ROUND(heading)) FROM character_data WHERE name='${CHAR}';" | tail -1)"
        [ "$got" = "${ZONE_ID} ${x} ${y} ${z} ${h}" ] && break
        say "$leg: seed not settled (db='$got'), retrying"
    done
    say "$leg: seeded ${CHAR} at qeynos2 ($x, $y, $z) heading $h"
    if ! eqemu_is_local; then
        # up.sh starts the container AND the fleet (a bare `docker start`
        # does not: measured, eqemu_stack_ready stayed false for 120 s).
        EQEMU_WORLD_STOPPED=0
        bash "$REPO/scripts/eqemu/up.sh" >>"$OUT/server.log" 2>&1 || { say "$leg: up.sh failed"; return 2; }
    fi

    printf 'wait 150\n/loc\nwait 2\n/q\n' > "$OUT/${leg}_script.txt"
    Xvfb "$DISP" -screen 0 1024x768x24 -nolisten tcp >"$OUT/${leg}_xvfb.log" 2>&1 &
    XVFB_PID=$!
    sleep 2
    # setcap does not stick on NFS (the review dir) or tmpfs: a local-disk copy (B511).
    local bin_local="${STEP_WALK_BIN_DIR:-$HOME/.cache/willeq-step-walk}/willeq"
    mkdir -p "$(dirname "$bin_local")"
    cp -f "$BIN" "$bin_local"
    sudo -n setcap cap_net_raw,cap_net_admin+ep "$bin_local" 2>/dev/null || say "setcap failed (zone load may be flaky, B511)"
    local preset_args=()
    [ -n "$PRESET" ] && preset_args=(--constrained "$PRESET")
    ( cd "$REPO" && DISPLAY="$DISP" WILLEQ_DATA_ROOT="$REPO" "$bin_local" -c "$CFG" \
        --renderer gles2 --egl-x11 -r 800 600 --no-audio "${preset_args[@]}" --log-level=INFO --log-module=INPUT:DEBUG \
        --eqe1-path /mnt/projects/EverQuest_ETC1 \
        --pose-log "$pose" --pose-every 1 --pose-max-frames 200000 \
        --script "$OUT/${leg}_script.txt" --timeout 240 > "$log" 2>&1 ) &
    CLIENT_PID=$!
    # Zoned in AND the zone's graphics loaded: "Fully connected to zone!" comes
    # tens of seconds BEFORE the renderer has its collision world (measured: a
    # 45 s loading screen on lane 1 under llvmpipe, keys sent in it moved
    # nothing). The marker is the loader's own "Game ready!" line; then 5 s.
    for i in $(seq 1 180); do
        grep -q "Graphics loading complete. Game ready" "$log" 2>/dev/null && break
        kill -0 "$CLIENT_PID" 2>/dev/null || break
        sleep 1
    done
    if ! grep -q "Graphics loading complete. Game ready" "$log"; then
        say "$leg: never reached Game ready"; return 2
    fi
    sleep 5
    wid="$(DISPLAY="$DISP" xdotool search --name . 2>/dev/null | tail -1)"
    DISPLAY="$DISP" xdotool windowfocus "$wid" 2>/dev/null
    sleep 1
    # Diagnostics only: STEP_WALK_PRE_KEYS="ctrl+alt+c" sends xdotool key
    # specs before the walk (e.g. collision off, to tell "blocked" from "no
    # input"). Unset in every verdict run.
    for k in ${STEP_WALK_PRE_KEYS:-}; do DISPLAY="$DISP" xdotool key "$k"; sleep 1; done
    local t_down t_up
    t_down=$(date +%s%3N)
    DISPLAY="$DISP" xdotool keydown w
    sleep "$HOLD"
    DISPLAY="$DISP" xdotool keyup w
    t_up=$(date +%s%3N)
    printf 'keydown_ms\t%s\nkeyup_ms\t%s\n' "$t_down" "$t_up" > "$OUT/${leg}_keys.tsv"
    say "$leg: W held ${HOLD}s (window $wid)"
    wait "$CLIENT_PID" 2>/dev/null
    CLIENT_PID=""
    kill "$XVFB_PID" 2>/dev/null; XVFB_PID=""
    python3 -I "$REPO/scripts/eqemu/step_walk_verdict.py" --pose "$pose" \
        --keys "$OUT/${leg}_keys.tsv" --edge-y "$EDGE_Y" --dir "$dir" \
        --rise "$RISE" --leg "$leg" | tee -a "$OUT/verdict.txt"
    return "${PIPESTATUS[0]}"
}

RC=0
legs=("$LEG"); [ "$LEG" = both ] && legs=(rise drop)
if eqemu_is_local && [ "$LEG" = both ]; then
    say "local lane: one leg per run (no zone bounce here) -- pass --leg rise or --leg drop"; exit 64
fi
for l in "${legs[@]}"; do
    run_leg "$l"; r=$?
    [ "$r" -gt "$RC" ] && RC=$r
done
say "exit $RC (verdicts in $OUT/verdict.txt)"
exit "$RC"
