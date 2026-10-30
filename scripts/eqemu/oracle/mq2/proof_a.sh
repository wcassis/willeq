#!/usr/bin/env bash
# B1214 proof (a) on eq-oracle-1: each MQ2 command over telnet, then a still of :99.
set -uo pipefail
B=/home/user/eq-oracle/b1214
OUT=$B/proof_a; mkdir -p "$OUT"
export DISPLAY=:99
T="python3 -I $B/mqtel.py --log $OUT/telnet.log --wait 2"
step() { # <name> <cmd...>
    local n=$1; shift
    $T "$@" "/echo B1214A $n heading=\${Me.Heading.Degrees} look=\${Me.Look} loc=\${Me.Y},\${Me.X},\${Me.Z}"
    sleep 2
    import -window root "$OUT/$n.png"
}
step a00_start "/echo start"
step a01_face090 "/face fast heading 90"
step a02_face180 "/face fast heading 180"
step a03_face270 "/face fast heading 270"
step a04_look_down "/look -40"
step a05_look_level "/look 0"
step a06_first_person "/keypress FIRST_PERSON_CAMERA"
step a07_user1_cam "/keypress USER1_CAMERA"
step a08_user2_cam "/keypress USER2_CAMERA"
step a09_zoomout_x10 "/keypress ZOOMOUT" "/keypress ZOOMOUT" "/keypress ZOOMOUT" "/keypress ZOOMOUT" "/keypress ZOOMOUT" "/keypress ZOOMOUT" "/keypress ZOOMOUT" "/keypress ZOOMOUT" "/keypress ZOOMOUT" "/keypress ZOOMOUT"
step a10_zoomin_x10 "/keypress ZOOMIN" "/keypress ZOOMIN" "/keypress ZOOMIN" "/keypress ZOOMIN" "/keypress ZOOMIN" "/keypress ZOOMIN" "/keypress ZOOMIN" "/keypress ZOOMIN" "/keypress ZOOMIN" "/keypress ZOOMIN"
step a11_macro "/macro face_cam"
sleep 8
import -window root "$OUT/a12_after_macro.png"
echo done
