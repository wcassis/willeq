#!/usr/bin/env bash
# B1214 proof (a, camera distance): set user camera 1's distance by writing the
# client's own camera object, read it back, and still the screen each time.
set -uo pipefail
B=/home/user/eq-oracle/b1214
OUT=$B/proof_a; mkdir -p "$OUT"
export DISPLAY=:99
T="python3 -I $B/mqtel.py --log $OUT/telnet-cam.log --wait 2"
CAM=$(python3 -I $B/mqtel.py --wait 1 "/camdump" | sed -n 's/.*B1214C user1 obj=\(0x[0-9a-f]*\).*/\1/p' | head -1)
DIST=$(printf '%x' $((CAM + 0x34)))
echo "user1 camera object $CAM, distance word 0x$DIST"
$T "/keypress USER1_CAMERA" "/face fast heading 0" "/look 0" "/camdump"
sleep 2
for d in 10 30 60 120; do
    $T "/poke $DIST $d" "/camdump"
    sleep 2
    import -window root "$OUT/cam_user1_dist$d.png"
done
$T "/keypress ZOOMOUT hold"
sleep 2
$T "/keypress ZOOMOUT" "/camdump"
import -window root "$OUT/cam_user1_after_zoomout_hold.png"
$T "/poke $DIST 30" "/camdump"
echo done
