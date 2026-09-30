#!/usr/bin/env bash
# B932: the GL floor on the Orange Pi -- kmscube's memory with a given Mesa.
# Runs ON THE PI (take pi_lock_take first). kmscube quits on stdin EOF, so it
# is fed `sleep` and bounded by `timeout`; a snapshot of /proc/<pid>/status +
# smaps + the mapped .so list (scripts/pi/proc_mem_snapshot.py) is taken
# --at seconds after launch.
#
# usage: kmscube_floor.sh <kmscube-binary> <out-dir> [runs=3] [at=12]
#   stock:    kmscube_floor.sh /home/user/b932/bin/kmscube.stock  DIR
#   no-LLVM:  kmscube_floor.sh /home/user/b932/bin/kmscube.nollvm DIR
#             (a copy with DT_RPATH=/home/user/mesa-nollvm/lib, made by
#              scripts/pi/mesa_nollvm_select.sh)
set -u
BIN="$1"; OUT="$2"; RUNS="${3:-3}"; AT="${4:-12}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$OUT"
for i in $(seq 1 "$RUNS"); do
    ( sleep 25 | sudo -n timeout 20 "$BIN" > "$OUT/run$i.stdout" 2>&1 ) &
    wrapper=$!
    sleep "$AT"
    pid="$(pgrep -n -f "^${BIN}( |\$)")"
    if [ -z "$pid" ]; then
        echo "run $i: no kmscube process found" | tee "$OUT/run$i.error"
    else
        sudo -n python3 "$HERE/proc_mem_snapshot.py" "$pid" --label "$(basename "$BIN") run$i" > "$OUT/run$i.json"
        echo "run $i: pid $pid snapshot $(sudo -n grep -E 'VmRSS|RssAnon|RssFile' /proc/"$pid"/status | tr -s ' \t' ' ' | tr '\n' ' ')"
    fi
    wait "$wrapper"
    sleep 3
done
