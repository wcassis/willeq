#!/usr/bin/env bash
# T310 Leg C -- SOLVE THE FIRST-PERSON CAMERA POSE BY SWEEPING IT AGAINST THE
# OFFICIAL CLIENT'S OWN FRAMES.
#
# Will, 2026-08-29: "adjust willeq's output until it matches the official
# client exactly. Then you'll have your camera position. ... This should not
# require recompiling. Add whatever you need to get this done efficiently."
#
# WHAT IT DOES, per swept value:
#   1. writes the fixed pose straight into character_data (x/y/z/heading/zone),
#      so BOTH clients can be put at the byte-identical world position without
#      #goto -- which T309 trap 2 showed leaves fable's own model unrendered;
#   2. launches build-gles2/bin/willeq headless (EGL surfaceless, no X needed)
#      with --fpcam <param>=<value> plus any --fixed assignments;
#   3. drives it with --script: daylight, first person, /hideui, /fpcam dump,
#      then whatever extra steps the configuration needs (e.g. /attack);
#   4. captures EVERY rendered frame with --dump-frames-dir/--dump-every 1
#      (stills are barred in this arc -- T309/T307);
#   5. scores every captured frame against the stored ORACLE frame with
#      scripts/camera_match_score.py and keeps the best;
#   6. greps the run's [FPCAM-DUMP] lines so the matched frame comes with the
#      resolved pose that produced it.
#
# Output: <outdir>/curve.tsv -- value, best edge score, best mad, best frame.
# THE CURVE'S MINIMUM IS THE MEASUREMENT AND ITS WIDTH IS THE ERROR BAR. A
# single "it matches" is not a result.
#
# TRAPS THIS SCRIPT ENCODES (all paid for already):
#  * a renderer command always prints `CMD outcome=queued` -- registry 3 is
#    fire-and-forget. We assert on the [FPCAM-DUMP] LOG LINE, never on outcome.
#  * --eqe1-path is mandatory on any run whose pixels are judged (T226).
#  * #set time takes TWO numeric tokens: `#set time 11 0`, not `11:00`.
#  * eyeHeightFractionOfSkull is the live eye-height lever. crownAboveEye and
#    eyeHeightFractionOfStandingHeight are ADVISORY and move no pixels.
#  * nearPlane applies in EVERY camera mode, so sweeping it also moves third
#    person and the visual goldens.
#
# ORACLE FRAMES: see review/T310/legC/README.md for what each one is.
#
# Usage:
#   camera_match_sweep.sh --run-id ID --oracle FRAME.png \
#       --param eyeForwardOffset --values "0,0.3,0.6,0.9,1.2" \
#       [--fixed "pitchDown=3.65,hfov=62.8"] \
#       [--pose "X Y Z HEADING"] [--zone 2] \
#       [--char Testchar] [--account willeq_test] [--password PW] \
#       [--crop WxH+X+Y] [--extra-script FILE] [--settle 18] [--hold 14] \
#       [--outdir DIR] [--keep-frames]
#
# --extra-script is inserted after /hideui and before the capture hold; it is
# raw --script syntax ("wait N" lines and slash commands).

set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${REPO}/scripts/eqemu/env.sh"

RUN_ID=""; ORACLE=""; PARAM=""; VALUES=""; FIXED=""
POSE=""; ZONE="2"; CHAR="Testchar"; ACCOUNT="willeq_test"; PASSWORD="${EQ_TEST_PASSWORD}"
ZONE_SHORT="qeynos2"; CROP=""; CCROP="1023x768+1+0"; EXTRA_SCRIPT=""; SETTLE=18; HOLD=14; OUTDIR=""; KEEP_FRAMES=0
EQ_PATH="${EQ_CLIENT_PATH:-/mnt/projects/EverQuestP1999}"
EQE1_PATH="${EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
WIDTH=1024; HEIGHT=768

while [ $# -gt 0 ]; do
    case "$1" in
        --run-id) RUN_ID="$2"; shift 2;;
        --oracle) ORACLE="$2"; shift 2;;
        --param) PARAM="$2"; shift 2;;
        --values) VALUES="$2"; shift 2;;
        --fixed) FIXED="$2"; shift 2;;
        --pose) POSE="$2"; shift 2;;
        --zone) ZONE="$2"; shift 2;;
        --zone-short) ZONE_SHORT="$2"; shift 2;;
        --char) CHAR="$2"; shift 2;;
        --account) ACCOUNT="$2"; shift 2;;
        --password) PASSWORD="$2"; shift 2;;
        --crop) CROP="$2"; shift 2;;
        --candidate-crop) CCROP="$2"; shift 2;;
        --extra-script) EXTRA_SCRIPT="$2"; shift 2;;
        --settle) SETTLE="$2"; shift 2;;
        --hold) HOLD="$2"; shift 2;;
        --outdir) OUTDIR="$2"; shift 2;;
        --res) WIDTH="$2"; HEIGHT="$3"; shift 3;;
        --keep-frames) KEEP_FRAMES=1; shift;;
        *) echo "unknown argument: $1" >&2; exit 2;;
    esac
done

[ -z "$RUN_ID" ] && { echo "--run-id is required" >&2; exit 2; }
[ -z "$PARAM" ]  && { echo "--param is required (use 'none' for a single fixed run)" >&2; exit 2; }
[ -z "$VALUES" ] && { echo "--values is required" >&2; exit 2; }
[ -n "$ORACLE" ] && [ ! -f "$ORACLE" ] && { echo "oracle frame not found: $ORACLE" >&2; exit 2; }

BIN="${REPO}/build-gles2/bin/willeq"
[ -x "$BIN" ] || { echo "willeq not built at ${BIN}" >&2; exit 2; }

[ -z "$OUTDIR" ] && OUTDIR="/mnt/projects/fable/review/T310/legC/sweeps/${RUN_ID}"
mkdir -p "${OUTDIR}"
CURVE="${OUTDIR}/curve.tsv"
printf 'value\tedge\tmad\tbest_frame\tframes\n' > "${CURVE}"

# ---------------------------------------------------------------- fixed pose
#
# THE TRAP THIS FUNCTION EXISTS FOR (cost two recon runs to find): fable's
# /quit drops the socket, the zone declares the client LINKDEAD, and the
# server then writes the character's in-memory position back to
# character_data SECONDS AFTER the process has already exited. A pose UPDATE
# issued straight after a run is silently overwritten by that late save, and
# the next run logs in at the PREVIOUS run's pose while the [FPCAM-DUMP]
# faithfully reports the pose it actually has -- i.e. a whole sweep renders
# the same frame and produces a FLAT, MEANINGLESS CURVE.
#
# So: wait out the late save, write the pose, and READ IT BACK. Never assume.
PX=""; PY=""; PZ=""; PH=""
if [ -n "$POSE" ]; then
    set -- $POSE
    PX="$1"; PY="$2"; PZ="$3"; PH="$4"
fi

# THE SECOND TRAP, and the one that silently flattens a whole sweep: after a
# disconnect the ZONE PROCESS KEEPS THE CHARACTER IN MEMORY AS LINKDEAD and
# RESUMES that entity on the next login. The DB pose is then never read --
# four runs at four different headings all reported headingEqUnits=480 in
# their own [FPCAM-DUMP] while character_data was verifiably 480/448/416/384.
# A fresh zone process has no cached client, so bounce it between runs. It
# also guarantees any late linkdead save has already happened before the pose
# is written.
ZONE_SHORT="${ZONE_SHORT:-qeynos2}"
reset_zone() {
    echo "== bouncing zone ${ZONE_SHORT} (clears the linkdead client cache)"
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -TERM -f '/bin/zone( |\$)'" >/dev/null 2>&1
    sleep 4
    eq_docker exec "${EQEMU_CONTAINER}" bash -c "pkill -KILL -f '/bin/zone( |\$)'" >/dev/null 2>&1
    sleep 2
    server_exec_bg "./bin/zone ${ZONE_SHORT} > logs/zone_${ZONE_SHORT}_sweep.log 2>&1"
    local i
    for i in $(seq 1 40); do
        if server_exec "grep -q 'Zone bootup type' logs/zone_${ZONE_SHORT}_sweep.log" >/dev/null 2>&1; then
            echo "   zone up (${i}s)"; return 0
        fi
        sleep 1
    done
    echo "STOP: zone ${ZONE_SHORT} did not boot" >&2
    return 1
}

apply_pose() {   # apply_pose [heading_override]
    [ -z "$POSE" ] && return 0
    local h="${1:-$PH}"
    local i got
    for i in 1 2 3 4 5 6 7 8 9 10; do
        mysql_exec "UPDATE character_data SET zone_id=${ZONE}, x=${PX}, y=${PY}, z=${PZ}, heading=${h} WHERE name='${CHAR}';" >/dev/null 2>&1
        sleep 6
        got=$(mysql_exec "SELECT CONCAT_WS(',',zone_id,ROUND(x,3),ROUND(y,3),ROUND(z,3),ROUND(heading,3)) FROM character_data WHERE name='${CHAR}';" 2>/dev/null | tail -1 | tr -d '\r')
        want="${ZONE},$(printf '%.3f' "$PX"),$(printf '%.3f' "$PY"),$(printf '%.3f' "$PZ"),$(printf '%.3f' "$h")"
        if [ "$got" = "$want" ]; then
            echo "== pose confirmed in DB: ${CHAR} -> ${got}"
            return 0
        fi
        echo "   pose not yet settled (db='${got}' want='${want}'), retrying"
    done
    echo "STOP: could not pin ${CHAR}'s pose in character_data" >&2
    return 1
}

CFG="${OUTDIR}/client.json"
cat > "${CFG}" <<EOF
{
  "clients": [
    {
      "host": "$(eqemu_port_host)",
      "port": ${LOGIN_PORT},
      "user": "${ACCOUNT}",
      "pass": "${PASSWORD}",
      "server": "${EQ_TEST_SERVER_LONGNAME}",
      "character": "${CHAR}",
      "eq_client_path": "${EQ_PATH}"
    }
  ]
}
EOF

# ------------------------------------------------------------------ the run
run_one() {   # run_one <label> <fpcam-spec>
    local label="$1" spec="$2"
    local rdir="${OUTDIR}/${label}"
    local fdir="${rdir}/frames"
    rm -rf "${rdir}"; mkdir -p "${fdir}"

    local script="${rdir}/tour.txt"
    {
        echo "wait ${SETTLE}"
        echo "/say #set time 11 0"
        echo "wait 3"
        echo "/camera fp"
        echo "wait 2"
        echo "/hideui"
        echo "wait 2"
        echo "/fpcam dump"
        echo "wait 2"
        [ -n "$EXTRA_SCRIPT" ] && cat "$EXTRA_SCRIPT"
        echo "wait ${HOLD}"
        echo "/fpcam dump"
        echo "wait 2"
        echo "/quit"
    } > "${script}"

    reset_zone || return 8
    apply_pose "${POSE_HEADING_OVERRIDE:-}" || return 9

    local args=( -c "${CFG}" --renderer gles2 -r "${WIDTH}" "${HEIGHT}" --no-audio
                 --eqe1-path "${EQE1_PATH}"
                 --log-level=INFO
                 --script "${script}"
                 --dump-frames-dir "${fdir}" --dump-every 1 --dump-max-frames 900 )
    [ -n "$spec" ] && args+=( --fpcam "$spec" )

    echo "-- run ${label}: --fpcam '${spec}'"
    timeout 240 "${BIN}" "${args[@]}" > "${rdir}/run.log" 2>&1
    local rc=$?
    echo "   exit=${rc}  frames=$(find "${fdir}" -name 'frame_*.png' | wc -l)"

    # THE ASSERTION: the emitted dump line, never `outcome=queued`.
    grep -h 'FPCAM-DUMP' "${rdir}/run.log" > "${rdir}/fpcam_dump.txt" 2>/dev/null
    if [ ! -s "${rdir}/fpcam_dump.txt" ]; then
        echo "   WARNING: no [FPCAM-DUMP] line -- the run did not reach first person" >&2
    fi
    return $rc
}

# ------------------------------------------------------------------- sweep
IFS=',' read -r -a VALS <<< "$VALUES"
for v in "${VALS[@]}"; do
    v="$(echo "$v" | tr -d ' ')"
    POSE_HEADING_OVERRIDE=""
    if [ "$PARAM" = "none" ]; then
        spec="$FIXED"; label="fixed"
    elif [ "$PARAM" = "heading" ]; then
        # Not an fpcam tunable: sweeps the SERVER-SET heading instead, so the
        # harness can aim the fixed pose at the target geometry without a
        # human looking at frames one run at a time.
        POSE_HEADING_OVERRIDE="$v"; spec="$FIXED"; label="heading_${v}"
    else
        spec="${PARAM}=${v}"
        [ -n "$FIXED" ] && spec="${spec},${FIXED}"
        label="$(echo "${PARAM}_${v}" | tr -c 'A-Za-z0-9_.-' '_')"
    fi
    run_one "$label" "$spec"
    rdir="${OUTDIR}/${label}"; fdir="${rdir}/frames"
    nframes=$(find "${fdir}" -name 'frame_*.png' 2>/dev/null | wc -l)

    if [ -n "$ORACLE" ] && [ "$nframes" -gt 0 ]; then
        # Score every captured frame; the BEST is the run's score. Scoring the
        # whole capture rather than one still is deliberate: the draw rule's
        # duty cycle is 22.5% and a still-sampled null was wrong once already.
        python3 "${REPO}/scripts/camera_match_score.py" --oracle "$ORACLE" \
            --candidate-dir "${fdir}" ${CROP:+--crop "$CROP"} ${CCROP:+--candidate-crop "$CCROP"} --tsv \
            > "${rdir}/scores.tsv" 2>"${rdir}/scores.err"
        best=$(grep '^BEST' "${rdir}/scores.tsv" | head -1)
        bf=$(echo "$best" | cut -f2); be=$(echo "$best" | cut -f3); bm=$(echo "$best" | cut -f4)
        printf '%s\t%s\t%s\t%s\t%s\n' "$v" "$be" "$bm" "$bf" "$nframes" >> "${CURVE}"
        [ -n "$bf" ] && cp "${fdir}/${bf}" "${rdir}/BEST_${bf}" 2>/dev/null
    else
        printf '%s\t-\t-\t-\t%s\n' "$v" "$nframes" >> "${CURVE}"
    fi

    # Video for every run -- stills are barred in this arc.
    if [ "$nframes" -ge 2 ]; then
        bash "${REPO}/scripts/ci/encode_capture.sh" "${fdir}" "${RUN_ID}_${label}" "${rdir}" \
            > "${rdir}/encode.log" 2>&1
    fi
    [ "$KEEP_FRAMES" = "0" ] && rm -rf "${fdir}"
done

echo
echo "=== curve: ${CURVE} ==="
cat "${CURVE}"
