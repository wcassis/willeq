#!/usr/bin/env bash
# B1183 (#502) -- live watcher for the caster client's stand byte +0x251.
# B1164 pattern (b1164_s_watch.sh): wait for the leg's `gdb_arm` timeline line,
# attach to the eqgame.exe of the CASTER client (the Xvfb session,
# DISPLAY :98 -- NOT the observer on :99) and run b1183_gdb.py to TMAX.
#
# Runs on the ORACLE HOST (lane 5: the ThinkPad). Usage:
#   b1183_watch.sh <evidence_dir> <tmax_seconds>
# Output: <evidence_dir>/gdb/{gdb.log,gdb.out}
set -u
EV="${1:?evidence dir}"
TMAX="${2:?tmax seconds}"
OUT="$EV/gdb"
mkdir -p "$OUT"
SCRIPT="$(cd "$(dirname "$0")" && pwd)/b1183_gdb.py"

# 1. Wait for the arm line in a session dir NEWER than this watch's start
#    (a stale dir from a previous leg must not consume the watch).
touch "$OUT/watch.started"
LATEST=""
for _ in $(seq 1 240); do
    d="$(ls -td /home/user/eq-oracle/sessions/* 2>/dev/null | head -1 || true)"
    if [[ -n $d && -f $d/timeline.tsv ]] \
            && [[ "$d" -nt "$OUT/watch.started" ]] \
            && grep -qP '\tEND\t[0-9]+_gdb_arm$' "$d/timeline.tsv"; then
        LATEST="$d"
        break
    fi
    sleep 5
done
[[ -n $LATEST ]] || { echo "b1183_watch: no gdb_arm line within 20 min" >&2; exit 1; }
echo "b1183_watch: session $LATEST"

# 2. The caster pid: the eqgame.exe whose environ has DISPLAY=:98 (Xvfb).
# Fallback: the lowest pid (the caster client launches before the observer).
pid=""
pids=$(pgrep -x eqgame.exe | sort -n)
for p in $pids; do
    env="$(tr '\0' '\n' < "/proc/$p/environ" 2>/dev/null || true)"
    case "$env" in
        *DISPLAY=:98*) pid="$p"; break ;;
    esac
done
[[ -n $pid ]] || pid="$(echo $pids | awk '{print $1}')"
[[ -n $pid ]] || { echo "b1183_watch: no eqgame.exe found" >&2; exit 2; }
echo "b1183_watch: caster pid $pid"

# 3. The eqgame.exe module base (first zero-offset mapping of the exe).
# /proc/pid/maps: ADDRESS PERMS OFFSET DEV INODE PATH -- field 1 is the
# "START-END" range, so cut the START half (B1164's cut -d- -f1; a bare
# 0x$1 feeds the range and int(..., 16) in b1183_gdb.py raises).
base="0x$(awk '$3=="00000000" && $6 ~ /eqgame\.exe$/ {print $1; exit}' /proc/$pid/maps | cut -d- -f1)"
[[ $base =~ ^0x[0-9a-f]+$ ]] || base=0x400000
echo "b1183_watch: eqgame.exe base $base"

# 4. Attach. sudo -n: ptrace_scope (B1164). The OUT dir must be writable by
# root (gdb) -- chmod 777, chown back after.
chmod 777 "$OUT"
sudo -n env S_OUT="$OUT/gdb" S_TMAX="$TMAX" S_BASE="$base" \
    timeout -s INT $((TMAX + 60)) \
    gdb -nx -q -p "$pid" --batch -x "$SCRIPT" >"$OUT/gdb.out" 2>&1
rc=$?
echo "b1183_watch: gdb rc=$rc"
sudo -n chown -R user:user "$OUT" 2>/dev/null || true
exit $rc
