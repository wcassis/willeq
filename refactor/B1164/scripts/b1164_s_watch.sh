#!/usr/bin/env bash
# B1164 (#483) dev5: run ON the lane-5 oracle host while a leg of
# refactor/B1164/plans/pin_s_unoccluded.tsv runs. Waits for the leg's
# steps.tsv to show the `gdb_arm` row, then attaches gdb (read-only probe
# b1164_s_gdb.py) to eqgame.exe for S_TMAX seconds and detaches.
# usage: b1164_s_watch.sh <out_dir> [tmax_s=40] [give_up_s=3600]
set -uo pipefail
OUT=$1 TMAX=${2:-40} GIVEUP=${3:-3600}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
stamp=$OUT/watch.start
touch "$stamp"
echo "watch start $(date -u +%s.%N)" >>"$OUT/watch.log"
end=$((SECONDS + GIVEUP))
while (( SECONDS < end )); do
    st=$(ls -t /home/user/eq-oracle/sessions/*/steps.tsv 2>/dev/null | head -1)
    if [[ -n $st && $st -nt $stamp ]] && grep -q $'^[0-9]*_gdb_arm\t' "$st"; then break; fi
    sleep 0.3
done
(( SECONDS < end )) || { echo "gave up" >>"$OUT/watch.log"; exit 1; }
pid=$(pgrep -x eqgame.exe | head -1)
[[ $pid =~ ^[0-9]+$ ]] || { echo "no eqgame.exe" >>"$OUT/watch.log"; exit 1; }
echo "arm row in $st; pid $pid at $(date -u +%s.%N)" >>"$OUT/watch.log"
grep -i 'eqgraphicsdx9' "/proc/$pid/maps" >"$OUT/maps.txt" 2>&1 || sudo -n grep -i 'eqgraphicsdx9' "/proc/$pid/maps" >"$OUT/maps.txt"
cp "$HERE/b1164_s_gdb.py" "$OUT/"
sudo -n env S_OUT="$OUT/s_probe.log" S_TMAX="$TMAX" \
    timeout -s INT $((TMAX + 60)) gdb -nx -p "$pid" --batch -x "$OUT/b1164_s_gdb.py" >"$OUT/gdb.out" 2>&1
echo "gdb exit $? at $(date -u +%s.%N)" >>"$OUT/watch.log"
sudo -n chown -R user:user "$OUT"
