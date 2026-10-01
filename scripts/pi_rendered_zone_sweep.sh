#!/usr/bin/env bash
# #102 (B854 reopened): the RENDERED Kunark + Velious zone sweep on the
# Orange Pi, with the memory ledger, on the orangepi preset.
#
# Why a sibling of scripts/scenario_zone_sweep.sh: that sweep's --pi mode runs
# `--scenario zone_in`, and --scenario forces the headless Automated lane --
# no GPU pipeline is ever initialised (refactor/B854/SUMMARY.md's caveat), so
# its memory figures are not rendered-client figures. This sweep drives a
# RENDERED client the way scripts/pose_live_capture.sh does: `--script`
# (a command line, which the client holds until fully zoned in, then
# `wait N`, then `/q`), on the Pi's DRM/KMS GLES2 device.
#
# One sweep, all inside ONE hold of BOTH locks (eqemu first, Pi second,
# released in reverse on every exit path):
#   1. stage the client on the Pi: copy the CI-published ARM build's binary
#      to the Pi's ext4 home (NOT /tmp: tmpfs is mounted nosuid there, which
#      makes the kernel ignore file capabilities) and `setcap
#      cap_net_raw,cap_net_admin+ep` it (CLAUDE.md "Linux Capabilities";
#      B751/#73: without CAP_NET_ADMIN the zone-in burst overflows the
#      socket buffer). getcap/sysctl are recorded in the evidence.
#   2. local eqemu lane up (EQEMU_HOST=local, clients reach 10.0.30.23),
#      snapshot the character rows ONCE (restore_lib.sh).
#   3. per zone, HEAVIEST FIRST (timorous, iceclad, growthplane,
#      emeraldjungle -- the Pi OOM history, B800/B750 -- then the rest by
#      s3d_bytes): boot the zone fresh, seed the character at its safe point
#      (scripts/eqemu/zone_place_lib.sh, shared with the headless sweep),
#      run scripts/pi/rendered_zone_run.py ON THE PI (dead-client watchdog:
#      --stall s without a log line, --cap s overall), diff the Pi's dmesg,
#      summarize into ONE TSV row (scripts/pi/rendered_zone_summarize.py),
#      free the zone's port. A failing zone NEVER stops the sweep.
#   4. every failed zone is retried ONCE at the end, into a SEPARATE TSV.
#   5. restore the snapshot with the world DOWN, down.sh, release the Pi
#      lock then the eqemu lock, print both lock-show lines.
#
# Usage: scripts/pi_rendered_zone_sweep.sh [--zones z1,z2] [--out DIR]
#          [--cap 300] [--stall 180] [--hold 75] [--no-retry]
#          [--pi-dir /mnt/projects/fable/ci-arm/<sha>] [-r "1280 720"]
#          [--char-level N --char-exp E]   (the eligibility leg; see CHAR_LEVEL)
#          [--owner NAME] [--tour FILE] [--env K=V]... [--drive-keys]
#          [--snap-points startup,ready,steady_60s]   (B932: a smaps/maps
#          snapshot at each named ledger point, rendered_zone_run.py)
#          [--mesa nollvm|system]   (B935; default $WILLEQ_MESA, else nollvm)
#          [--dump-every N --dump-max M]   (B960; default 100 / 3: the
#          client's --dump-every / --dump-max-frames, Pi /tmp is ~242 MB)
#          [--account NAME --char NAME]   (B960; default the lane's test
#          account/character from scripts/eqemu/env.sh, same password)
#          [--shared-cgroup]   (B955: by default the client runs in its OWN
#          memory cgroup -- rendered_zone_run.py --own-cgroup -- so its
#          gpu_buffers ledger row reads the Lima GEM bytes from memory.stat;
#          this opts out, and the row falls back to the mapped lower bound)
#          [--allow-vm-lane]   (B909's A/B, scripts/pi/collision_ab.sh:
#          --tour replaces the /loc-wait-/q script, --env adds client
#          environment, --drive-keys plays scripts/pi/uinput_drive.py's fixed
#          keyboard sequence so the renderer's collision rays run,
#          --allow-vm-lane lets EQEMU_HOST name the eqemu VM -- the Pi reaches
#          it, measured 2026-09-29: 0.7 ms, TCP 22 open)
#
# THE MESA (B935, #253 -- Will 2026-09-30 "ship the prefix"). By default the
# client runs on the SHIPPED LLVM-less Mesa: the sweep stages the build dir's
# bin/willeq-nollvm (DT_RPATH=<prefix>/lib, scripts/pi/publish_arm_build.sh)
# and installs the pinned prefix at $MESA_NOLLVM_PI_PREFIX on the Pi
# (scripts/pi/deploy_mesa_prefix.sh). WILLEQ_MESA=system (or --mesa system)
# is the explicit opt-out for A/B work: it stages bin/willeq, the system Mesa.
# Every run snapshots /proc/<pid>/maps at `startup` (plus --snap-points), the
# row records the libgallium it mapped and whether libLLVM was mapped, and a
# run that loaded the other Mesa FAILS (mesa_mismatch) and ABORTS the sweep
# with exit 67; a run with no snapshot fails as mesa_unproven.
# Exit: 0 every first-attempt row passed, 1 a row failed, 64 a lock is held,
#       65 the stack/lane would not come up, 66 snapshot mismatch,
#       67 the loaded Mesa is not the one asked for,
#       68 bad inputs (no build, no bin/willeq-nollvm, Pi unreachable,
#       the prefix would not deploy).

set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export EQEMU_HOST="${EQEMU_HOST:-local}"
source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"
source "$REPO/scripts/pi/env.sh"
source "$REPO/scripts/pi/mesa_nollvm.env"

OWNER="pi-rendered-sweep"
ZONES=""
OUT=""
CAP_S=300
STALL_S=180
HOLD_S=240
RETRY=1
RES="1280 720"
PI_DIR="/mnt/projects/fable/ci-arm/by-branch/fable"
PI_EQE1="${PI_EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
PI_EQ="${PI_EQ_PATH:-/mnt/projects/EverQuestP1999}"
ZONE_BOOT_S=180
LATE_SAVE_S=10
# --char-level N: the ELIGIBILITY leg. The server kicks an under-level
# character out of a min_level zone (zone/zoning.cpp CanEnterZone: veeshan
# 55, growthplane/templeveeshan/sleeper 46) into qeynos, so that zone's
# rendered load is never tested. Set after the snapshot, so the snapshot's
# restore puts the level back (level is one of its columns).
# The server RE-DERIVES level from exp at zone-in (measured: chardok's log
# "SetLevel Setting Level for [Testchar] to [49]" with level=20 in the row),
# so the leg sets exp too: zone/exp.cpp GetEXPForLevel(60) = 59^3 * 3000 =
# 616,137,000 before race/class modifiers. exp is also a snapshot column.
# The level-49 character also DIES at some safe points (chardok, charasis:
# ZonePC zone_mode [3] = ZoneToBindPoint, "Death Only", zone/client.h).
CHAR_LEVEL=""
CHAR_EXP=""
TOUR_IN=""
EXTRA_ENV=""
DRIVE_KEYS=0
ALLOW_VM=0
SNAP_POINTS=""
DUMP_EVERY=100
DUMP_MAX=3
ACCOUNT_OVERRIDE=""
CHAR_OVERRIDE=""
OWN_CGROUP=1
MESA="${WILLEQ_MESA:-nollvm}"
while [ $# -gt 0 ]; do
    case "$1" in
        --zones) ZONES="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --cap) CAP_S="$2"; shift 2 ;;
        --stall) STALL_S="$2"; shift 2 ;;
        --hold) HOLD_S="$2"; shift 2 ;;
        --no-retry) RETRY=0; shift ;;
        --pi-dir) PI_DIR="$2"; shift 2 ;;
        -r) RES="$2"; shift 2 ;;
        --char-level) CHAR_LEVEL="$2"; shift 2 ;;
        --char-exp) CHAR_EXP="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        --tour) TOUR_IN="$2"; shift 2 ;;
        --env) case "$2" in [A-Z_]*=*) EXTRA_ENV="$EXTRA_ENV '$2'";; *) echo "bad --env $2" >&2; exit 64;; esac; shift 2 ;;
        --drive-keys) DRIVE_KEYS=1; shift ;;
        --allow-vm-lane) ALLOW_VM=1; shift ;;
        --snap-points) SNAP_POINTS="$2"; shift 2 ;;
        --mesa) MESA="$2"; shift 2 ;;
        --dump-every) DUMP_EVERY="$2"; shift 2 ;;
        --dump-max) DUMP_MAX="$2"; shift 2 ;;
        --account) ACCOUNT_OVERRIDE="$2"; shift 2 ;;
        --char) CHAR_OVERRIDE="$2"; shift 2 ;;
        --shared-cgroup) OWN_CGROUP=0; shift ;;
        -h|--help) sed -n '2,72p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
case "$MESA" in nollvm|system) ;; *) echo "WILLEQ_MESA/--mesa must be nollvm or system, not '$MESA'" >&2; exit 64;; esac
# The proof of which Mesa loaded is taken in EVERY run: `startup` is always a
# snapshot point (the driver is mapped by then).
case ",$SNAP_POINTS," in *,startup,*) ;; *) SNAP_POINTS="startup${SNAP_POINTS:+,$SNAP_POINTS}";; esac
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/B854-rendered/sweep-${STAMP}"
LOGS="$OUT/zones"
mkdir -p "$LOGS"
TSV="$OUT/results.tsv"
RETRY_TSV="$OUT/results_retry.tsv"
RIG_LOG="$OUT/sweep.log"
say() { printf '[pi_rendered_sweep %s] %s\n' "$(date -u +%H:%M:%S)" "$*" | tee -a "$RIG_LOG" >&2; }
CHAR="${CHAR_OVERRIDE:-${EQ_TEST_CHARACTER}}"
[ -n "$ACCOUNT_OVERRIDE" ] && EQ_TEST_ACCOUNT="$ACCOUNT_OVERRIDE"

# ---- the zone set: B854's 46 Kunark + Velious rows, heavy four first --------
CENSUS="$REPO/scripts/ci/zone_content_sweep_zones.tsv"
if [ -z "$ZONES" ]; then
    ZONES="$(awk -F'\t' 'NR>1 {print $1}' "$REPO/refactor/B854/sweep_results.tsv" | tr '\n' ',' | sed 's/,$//')"
fi
HEAVY="timorous iceclad growthplane emeraldjungle"
ORDERED="$(
    for z in $HEAVY; do case ",$ZONES," in *",$z,"*) echo "$z";; esac; done
    tr ',' '\n' <<< "$ZONES" | grep . | while read -r z; do
        case " $HEAVY " in *" $z "*) continue;; esac
        b="$(awk -F'\t' -v z="$z" 'NR>1 && $1==z {print $3; exit}' "$CENSUS")"
        printf '%s\t%s\n' "${b:-0}" "$z"
    done | sort -t"$(printf '\t')" -k1,1nr | cut -f2
)"
mapfile -t ZONE_LIST <<< "$ORDERED"
say "zones (${#ZONE_LIST[@]}, run order): ${ZONE_LIST[*]}"
zone_meta() {  # zone_meta <zone> -> "expansion<TAB>s3d_bytes"
    awk -F'\t' -v z="$1" 'NR>1 && $1==z {print $2 "\t" $3; exit}' "$CENSUS"
}

# ---- inputs -----------------------------------------------------------------
PI_SHA_DIR="$(readlink -f "$PI_DIR")"
[ -x "$PI_SHA_DIR/bin/willeq" ] || { say "no ARM build at $PI_SHA_DIR/bin/willeq"; exit 68; }
STAGE_SRC="$PI_SHA_DIR/bin/willeq"
if [ "$MESA" = nollvm ]; then
    STAGE_SRC="$PI_SHA_DIR/bin/willeq-nollvm"
    if [ ! -x "$STAGE_SRC" ] || [ ! -f "$PI_SHA_DIR/mesa/prefix/.willeq-mesa-pin" ]; then
        say "no bin/willeq-nollvm or mesa/prefix in $PI_SHA_DIR (a publish before B935, or its Mesa step failed) -- use a newer build, or WILLEQ_MESA=system for the system Mesa"
        exit 68
    fi
fi
BUILD_SHA="$(sed -n 's/^sha=//p' "$PI_SHA_DIR/BUILD_INFO")"
BIN_SHA256="$(sha256sum "$STAGE_SRC" | cut -d' ' -f1)"
say "ARM build: $PI_SHA_DIR (BUILD_INFO sha=$BUILD_SHA); staging $(basename "$STAGE_SRC") sha256=$BIN_SHA256; Mesa: $MESA"
pi_remote true </dev/null || { say "the Pi ($PI_HOST) is unreachable"; exit 68; }

# The client config: this lane's server, derived (never the VM's address --
# review/pi/willeq_pi.json hardcodes 172.16.200.99). The Pi reaches this
# host on its first address.
if eqemu_is_local; then CLIENT_HOST="$(hostname -I | awk '{print $1}')"; else CLIENT_HOST="$EQEMU_HOST"; fi
if [ "$CLIENT_HOST" = "172.16.200.99" ] && [ "$ALLOW_VM" -ne 1 ]; then
    say "refusing: the VM lane needs --allow-vm-lane"; exit 65
fi
CFG="$OUT/client.json"
cat > "$CFG" <<EOF
{
  "clients": [
    {
      "host": "${CLIENT_HOST}",
      "port": ${LOGIN_PORT},
      "user": "${EQ_TEST_ACCOUNT}",
      "pass": "${EQ_TEST_PASSWORD}",
      "server": "${EQ_TEST_SERVER_LONGNAME}",
      "character": "${CHAR}",
      "eq_client_path": "${PI_EQ}",
      "maps_path": "/mnt/projects/maps/base",
      "navmesh_path": "/mnt/projects/maps/nav"
    }
  ]
}
EOF
# The drive: a held command (the client holds a COMMAND line until fully
# zoned in; a leading `wait` would start counting at launch), HOLD_S of
# idle rendering -- past the ledger's steady_60s point, which fires 60 s
# after ready -- then /q.
TOUR="$OUT/tour.txt"
if [ -n "$TOUR_IN" ]; then
    cp "$TOUR_IN" "$TOUR"
else
    printf '/loc\nwait %s\n/loc\n/q\n' "$HOLD_S" > "$TOUR"
fi

# ---- locks (eqemu first, Pi second) -----------------------------------------
if ! eqemu_lock_take "$OWNER"; then
    say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run"
    exit 64
fi
say "eqemu lock taken: $(eqemu_lock_show)"
PI_LOCK_TAKEN=0
for i in $(seq 1 30); do   # up to 15 min for a held Pi lock to free
    if pi_lock_take "$OWNER" </dev/null; then PI_LOCK_TAKEN=1; break; fi
    say "pi lock held by '$(pi_lock_show </dev/null)' -- waiting ($i/30)"
    sleep 30
done
if [ "$PI_LOCK_TAKEN" -ne 1 ]; then
    say "pi lock never freed in 15 min -- STOP"
    eqemu_lock_release
    exit 64
fi
say "pi lock taken: $(pi_lock_show </dev/null)"

BEFORE="$OUT/snapshot_before.tsv"
AFTER="$OUT/snapshot_after.tsv"
CLEANED=0
RESTORE_BAD=0
SNAP_TAKEN=0
PI_BIN_DIR="/home/${PI_USER}/pi-rendered-sweep"
PI_BIN="$PI_BIN_DIR/willeq"
PI_TMP="/tmp/prs"
pi_kill_client() {
    pi_remote "pkill -TERM -f '^${PI_BIN} ' >/dev/null 2>&1; sleep 3; pkill -KILL -f '^${PI_BIN} ' >/dev/null 2>&1; pkill -KILL -f '[r]endered_zone_run[.]py' >/dev/null 2>&1; sudo -n pkill -KILL -f '[u]input_drive[.]py' >/dev/null 2>&1; true" </dev/null >/dev/null 2>&1
}
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    say "cleanup: stopping any Pi client, restoring, lane down, releasing locks"
    pi_kill_client
    pi_remote "rm -rf '$PI_TMP'" </dev/null >/dev/null 2>&1
    if [ "$SNAP_TAKEN" -eq 1 ]; then
        eqemu_restore_snapshot "$BEFORE" "$AFTER" say || RESTORE_BAD=1
    fi
    bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/server.log" 2>&1
    say "stack down"
    [ "$PI_LOCK_TAKEN" -eq 1 ] && pi_lock_release "$OWNER" </dev/null
    eqemu_lock_release
    say "locks released; eqemu lock now: '$(eqemu_lock_show)'; pi lock now: '$(pi_lock_show </dev/null)'"
}
trap 'cleanup; exit 130' INT TERM
trap cleanup EXIT

# ---- 1. stage the capable client on the Pi -----------------------------------
pi_remote "mkdir -p '$PI_BIN_DIR' && cp '$STAGE_SRC' '$PI_BIN' && sudo -n setcap cap_net_raw,cap_net_admin+ep '$PI_BIN'" </dev/null \
    || say "WARNING: staging/setcap on the Pi failed -- the run goes ahead; the rcvbuf column shows what the client got"
MESA_DEPLOY="not deployed (WILLEQ_MESA=system)"
if [ "$MESA" = nollvm ]; then
    if ! MESA_DEPLOY="$(bash "$REPO/scripts/pi/deploy_mesa_prefix.sh" "$PI_SHA_DIR/mesa/prefix" 2>&1)"; then
        say "the shipped Mesa prefix would not deploy: $MESA_DEPLOY"
        exit 68
    fi
    say "$MESA_DEPLOY"
fi
{
    echo "host_utc=$(date -u +%FT%TZ)"
    echo "build_dir=$PI_SHA_DIR"
    echo "build_sha=$BUILD_SHA"
    echo "staged=$STAGE_SRC"
    echo "published_sha256=$BIN_SHA256"
    echo "mesa=$MESA expect_prefix=$MESA_NOLLVM_PI_PREFIX pin=$(mesa_nollvm_pin)"
    echo "mesa_deploy=$MESA_DEPLOY"
    echo "pi_copy_sha256=$(pi_remote "sha256sum '$PI_BIN' | cut -d' ' -f1" </dev/null)"
    echo "pi_getcap=$(pi_remote "getcap '$PI_BIN'" </dev/null)"
    echo "published_getcap=$(pi_remote "getcap '$PI_SHA_DIR/bin/willeq'; echo rc=\$?" </dev/null | tr '\n' ' ')"
    echo "pi_tmp_mount=$(pi_remote "findmnt -no OPTIONS /tmp" </dev/null)"
    echo "pi_rmem=$(pi_remote "sysctl -n net.core.rmem_max net.core.rmem_default" </dev/null | tr '\n' ' ')"
    echo "pi_kernel=$(pi_remote "uname -r" </dev/null)"
    echo "pi_meminfo=$(pi_remote "grep -E 'MemTotal|MemAvailable|SwapTotal|SwapFree|CmaTotal|CmaFree' /proc/meminfo" </dev/null | tr -s ' ' | tr '\n' ';')"
    echo "worktree_commit=$(git -C "$REPO" rev-parse HEAD)"
    echo "resolution=$RES cap_s=$CAP_S stall_s=$STALL_S hold_s=$HOLD_S"
    echo "lane=$EQEMU_HOST tour=${TOUR_IN:-default} extra_env=${EXTRA_ENV:-none} drive_keys=$DRIVE_KEYS snap_points=${SNAP_POINTS:-none}"
} > "$OUT/binary_identity.txt"
say "$(tr '\n' ' ' < "$OUT/binary_identity.txt")"

# ---- 2. lane up + snapshot -----------------------------------------------------
say "up.sh (EQEMU_HOST=$EQEMU_HOST, client host $CLIENT_HOST)"
if ! timeout 300 bash "$REPO/scripts/eqemu/up.sh" >"$OUT/server.log" 2>&1; then
    say "up.sh failed -- see $OUT/server.log"
    exit 65
fi
eqemu_stack_ready || { say "eqemu_stack_ready is false after up.sh"; exit 65; }
eqemu_snapshot "$BEFORE"
SNAP_TAKEN=1
say "snapshot taken ($(wc -l < "$BEFORE") rows)"
source "$REPO/scripts/eqemu/zone_place_lib.sh"
if [ -n "$CHAR_LEVEL" ]; then
    eqemu_sql "UPDATE character_data SET level=${CHAR_LEVEL}${CHAR_EXP:+, exp=${CHAR_EXP}} WHERE name='${CHAR}';" >/dev/null
    got_level="$(eqemu_sql "SELECT level FROM character_data WHERE name='${CHAR}';" | tail -1 | tr -d '[:space:]')"
    got_exp="$(eqemu_sql "SELECT exp FROM character_data WHERE name='${CHAR}';" | tail -1 | tr -d '[:space:]')"
    say "ELIGIBILITY LEG: ${CHAR} level set to ${got_level} exp ${got_exp} (asked ${CHAR_LEVEL}/${CHAR_EXP:-unchanged}); the snapshot restore puts both back"
    echo "eligibility_leg_char_level=${got_level} exp=${got_exp}" >> "$OUT/binary_identity.txt"
    [ "$got_level" = "$CHAR_LEVEL" ] || { say "level did not read back"; exit 65; }
fi

# ---- 3. one zone ----------------------------------------------------------------
dmesg_count() { pi_remote "sudo -n dmesg | wc -l" </dev/null | tr -d '[:space:]'; }
fail_row() {  # fail_row <tsv> <zone> <attempt> <signature>
    local meta dir; meta="$(zone_meta "$2")"
    dir="$LOGS/$2"; [ "$3" = retry ] && dir="$LOGS/${2}_retry"
    python3 "$REPO/scripts/pi/rendered_zone_summarize.py" --run "$dir" \
        --zone "$2" --attempt "$3" --tsv "$1" --expansion "${meta%%	*}" --s3d-bytes "${meta##*	}" \
        --preset-signature "$4" --expect-mesa "$MESA" --mesa-prefix "$MESA_NOLLVM_PI_PREFIX" 2>&1 | tee -a "$RIG_LOG" >&2
}
run_one() {  # run_one <zone> <attempt: first|retry> <tsv>
    local z="$1" attempt="$2" tsv="$3" meta exp s3d dir pidir before after rc
    meta="$(zone_meta "$z")"; exp="${meta%%	*}"; s3d="${meta##*	}"
    dir="$LOGS/$z"; [ "$attempt" = retry ] && dir="$LOGS/${z}_retry"
    rm -rf "$dir"; mkdir -p "$dir"
    say "=== $z ($exp, $s3d s3d bytes, $attempt) ==="
    if ! boot_zone "$z"; then
        fail_row "$tsv" "$z" "$attempt" "zone_boot_failed"; say "row: $z zone_boot_failed"; return 1
    fi
    if ! seed_char "$z"; then
        fail_row "$tsv" "$z" "$attempt" "seed_failed"; kill_zone "$z"; say "row: $z seed_failed"; return 1
    fi
    before="$(dmesg_count)"
    pidir="$PI_TMP/$z"
    # --drive-keys: the virtual keyboard must EXIST before the client's evdev
    # scan. Every pkill of it uses the bracket pattern '[u]input_drive[.]py':
    # a plain 'uinput_drive.py' matches THIS remote shell's own command line
    # and kills it (ssh exit 255 before the copy -- the B909 smoke leg).
    # scan, so it starts first; it plays its sequence after the ready point
    # and removes itself at the client's shutdown point (or its cap).
    local drive=""
    [ "$DRIVE_KEYS" -eq 1 ] && drive="sudo -n python3 '$REPO/scripts/pi/uinput_drive.py' --client-log '$pidir/client.log' \
--events '$pidir/keys.tsv' --cap $CAP_S > '$pidir/keys.out' 2>&1 & sleep 2; "
    # shellcheck disable=SC2086
    local remote="rm -rf '$pidir' && mkdir -p '$pidir/frames' '$PI_TMP/cache' && cd '$pidir' && ${drive}\
env TZ=UTC WILLEQ_DATA_ROOT='$PI_SHA_DIR' WILLEQ_CACHE_DIR='$PI_TMP/cache' $EXTRA_ENV \
python3 '$REPO/scripts/pi/rendered_zone_run.py' --out '$pidir' --cap $CAP_S --stall $STALL_S ${SNAP_POINTS:+--snap-points $SNAP_POINTS} $([ "$OWN_CGROUP" -eq 1 ] && echo --own-cgroup) -- \
'$PI_BIN' -c '$CFG' --drm --renderer gles2 --constrained orangepi -r $RES \
--eqe1-path '$PI_EQE1' --log-level=INFO --script '$TOUR' \
--dump-frames-dir '$pidir/frames' --dump-every $DUMP_EVERY --dump-max-frames $DUMP_MAX \
> '$pidir/runner.out' 2>&1; cp -rp '$pidir/.' '$dir/'; \
sudo -n pkill -TERM -f '[u]input_drive[.]py' >/dev/null 2>&1; sleep 1; cp '$pidir/keys.tsv' '$pidir/keys.out' '$dir/' 2>/dev/null; \
sudo -n rm -rf '$pidir'; true"
    timeout $((CAP_S + 120)) bash -c 'source "$1"; pi_remote "$2" </dev/null' _ "$REPO/scripts/pi/env.sh" "$remote"
    rc=$?
    [ "$rc" -eq 0 ] || { say "WARN: the Pi leg returned $rc (ssh/timeout) -- killing any leftover client"; pi_kill_client; }
    after="$(dmesg_count)"
    : > "$dir/dmesg_oom.txt"
    if [ -n "$after" ] && [ -n "$before" ] && [ "$after" -gt "$before" ]; then
        pi_remote "sudo -n dmesg -T | tail -n +$((before + 1))" </dev/null > "$dir/dmesg_new.txt" 2>/dev/null
        grep -iE "oom|killed process|out of memory" "$dir/dmesg_new.txt" > "$dir/dmesg_oom.txt" || true
    fi
    python3 "$REPO/scripts/pi/rendered_zone_summarize.py" --run "$dir" --zone "$z" --attempt "$attempt" \
        --expansion "$exp" --s3d-bytes "$s3d" --dmesg "$dir/dmesg_oom.txt" --tsv "$tsv" \
        --expect-mesa "$MESA" --mesa-prefix "$MESA_NOLLVM_PI_PREFIX" 2>&1 | tee -a "$RIG_LOG" >&2
    local mcheck
    mcheck="$(awk -F'\t' 'NR==1 {for (i=1;i<=NF;i++) if ($i=="mesa_check") c=i; next} END {if (c) print $c}' "$tsv")"
    if [ "${mcheck#mismatch}" != "$mcheck" ]; then
        say "!!!!!!!! MESA MISMATCH in $z: WILLEQ_MESA=$MESA but $mcheck -- ABORTING the sweep (exit 67) !!!!!!!!"
        MESA_MISMATCH=1
    fi
    sleep "$LATE_SAVE_S"
    kill_zone "$z"
    # The server may have moved the character to ANOTHER zone (a min-level
    # or flag kick sends it to qeynos). That zone's process -- one of the
    # static fleet, which kill_zone above does not touch -- now holds the
    # character LINKDEAD and writes it back over the NEXT zone's seed
    # (measured 2026-09-29: burningwood, run straight after the veeshan
    # kick, twice). Stop the zone the client actually landed in too.
    local landed
    landed="$(tail -1 "$tsv" | cut -f11)"
    if [ -n "$landed" ] && [ "$landed" != "$z" ]; then
        say "the client landed in '$landed', not '$z' -- stopping that zone too (its linkdead write-back would clobber the next seed)"
        sleep "$LATE_SAVE_S"
        kill_zone "$landed"
    fi
    tail -1 "$tsv" | cut -f5 | grep -qx pass
}

SWEEP_T0=$(date +%s)
FAILED=()
MESA_MISMATCH=0
for z in "${ZONE_LIST[@]}"; do
    [ -n "$z" ] || continue
    run_one "$z" first "$TSV" || FAILED+=("$z")
    [ "$MESA_MISMATCH" -eq 1 ] && break
    say "progress: $(( $(grep -c . "$TSV") - 1 ))/${#ZONE_LIST[@]} rows, ${#FAILED[@]} failed, $(( ($(date +%s) - SWEEP_T0) / 60 )) min"
done
if [ "$RETRY" -eq 1 ] && [ "${#FAILED[@]}" -gt 0 ] && [ "$MESA_MISMATCH" -eq 0 ]; then
    say "retrying ${#FAILED[@]} failed zone(s) ONCE: ${FAILED[*]}"
    for z in "${FAILED[@]}"; do run_one "$z" retry "$RETRY_TSV" || true; done
fi
say "first attempt: $(awk -F'\t' 'NR>1 && $5=="pass"' "$TSV" | wc -l)/$(( $(grep -c . "$TSV") - 1 )) pass"
[ -f "$RETRY_TSV" ] && say "retry: $(awk -F'\t' 'NR>1 && $5=="pass"' "$RETRY_TSV" | wc -l)/$(( $(grep -c . "$RETRY_TSV") - 1 )) pass"

cleanup
[ "$RESTORE_BAD" -eq 1 ] && { say "restore mismatch"; exit 66; }
[ "$MESA_MISMATCH" -eq 1 ] && { say "the loaded Mesa was not the one asked for (WILLEQ_MESA=$MESA)"; exit 67; }
[ "${#FAILED[@]}" -eq 0 ] && exit 0
exit 1
