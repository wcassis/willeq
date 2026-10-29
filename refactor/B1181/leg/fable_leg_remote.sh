#!/bin/bash
# B1181 fable leg, runs ON the ThinkPad (lane 5). Two fable clients:
#   A = the WINDOWED willeq under test (the leg build's bin/willeq) as
#       Oraclewatch (char 103) on willeqwatch -- the OBSERVER. Its screen:
#       fable's own --dump-frames-dir (allowed on fable, B1142) for the
#       per-frame measurement AND an x11grab video of :99 for the ledger
#       clip (the B1142 video pattern). The willeqwatch password is read at
#       run time from ~/eq-oracle/observer-Oraclewatch.cred (B1134's
#       observer credential; key=value lines, OBS_PASS=*** is written
#       into cfgA.json and never echoed or logged.
#   B = a second, HEADLESS willeq (same build tree) as Testcaster (char 67)
#       on willeq_test -- the CASTER and the GM (B1142's rig credential,
#       already committed in the B1142 scripts). B runs the shots from
#       fable_b_tour.txt: the archery auto-attack, then the two bolt
#       #castspells.
# tcpdump -i any takes the wire (both clients; the veth copies are dropped,
# B1057). A's stream is the client port whose C->S OP_ZoneEntry names
# Oraclewatch; B's names Testcaster (the measurement picks per-stream).
# usage: fable_leg_remote.sh <out_dir> <build_dir>
set -u
OUT=$1 BUILD=$2
W=1280 H=800
BIN=$BUILD/bin/willeq
[ -x "$BIN" ] || { echo "no binary: $BIN" >&2; exit 65; }
# B1176 (#495): pin the data root to the source tree this build came from --
# each slug has its own tree on the ThinkPad; a leg dir holds only cfgs +
# frames, so ResolveDataPath would FATAL on the first missing shipped file.
SLUG=$(basename "$(dirname "$BUILD")")
SRC=/home/user/src/willeq-wt/"$SLUG"
for f in config/constrained_presets.json config/hotkeys.json data/item_models.json data/willeq.db; do
  [ -f "$SRC/$f" ] || { echo "data root $SRC missing $f" >&2; exit 65; }
done
export WILLEQ_DATA_ROOT="$SRC"
mkdir -p "$OUT"
cp "$LEG_DIR/fable_a_tour.txt" "$OUT/tour.txt"
cp "$LEG_DIR/fable_b_tour.txt" "$OUT/b_tour.txt"
# --- credentials -------------------------------------------------------------
CRED=/home/user/eq-oracle/observer-Oraclewatch.cred
[ -r "$CRED" ] || { echo "observer credential unreadable" >&2; exit 65; }
APASS=$(awk -F= '/^OBS_PASS=/{print $2}' "$CRED")   # never printed
# --- configs -----------------------------------------------------------------
mkcfg() { # $1 out  $2 user  $3 pass  $4 char
cat > "$1" <<JSON
{
  "clients": [
    { "host": "172.16.200.205", "port": 5998,
      "user": "$2", "pass": "$3", "server": "WillEQ", "character": "$4",
      "eq_client_path": "/mnt/projects/EverQuestP1999",
      "maps_path": "/mnt/projects/maps/base",
      "navmesh_path": "/mnt/projects/maps/nav" }
  ],
  "audio": { "enabled": false }
}
JSON
}
mkcfg "$OUT/cfgB.json" willeq_test WillEqH03Pass Testcaster
mkcfg "$OUT/cfgA.json" willeqwatch "$APASS" Oraclewatch
# --- A's per-leg cache: the B834 Tip-of-the-Day seed (the window sits over
# --- the right of the screen, exactly where the caster stands in the frames)
CACHE_A=/home/user/b1181-cache/"$(basename "$OUT")"
rm -rf "$CACHE_A"; mkdir -p "$CACHE_A/ui"
printf '[Tips]\nShowTOTD=0\n' > "$CACHE_A/ui/UI_Oraclewatch_WillEQ.ini"
echo "cache=$CACHE_A" > "$OUT/rig.txt"
export WILLEQ_CACHE_DIR="$CACHE_A"
# --- wire --------------------------------------------------------------------
filter='udp and (port 5998 or port 5999 or portrange 9000-9001 or portrange 7000-7030)'
veths=$(ip -o link show type veth 2>/dev/null | awk -F': ' '{printf "%sifindex %s", (n++ ? " or " : ""), $1}')
[[ -z $veths ]] || filter="$filter and not ($veths)"
printf '%s\n' "$filter" > "$OUT/pcap.filter"
sudo -n tcpdump -i any -s 0 -U -Z user -w "$OUT/capture.pcap" "$filter" >"$OUT/pcap.log" 2>&1 &
PCAP=$!
# --- B (headless caster) first ------------------------------------------------
export DISPLAY=:99
xset s reset 2>/dev/null || :
xset s off -dpms 2>/dev/null || :
"$BIN" -c "$OUT/cfgB.json" --no-graphics --script "$OUT/b_tour.txt" \
    --log-level=DEBUG --log-module=MOVEMENT:DEBUG \
    >"$OUT/B.log" 2>&1 &
BPID=$!
echo "$BPID" > "$OUT/willeqB.pid"
# --- A (windowed observer, the subject) ---------------------------------------
sleep 50
if ! kill -0 "$BPID" 2>/dev/null; then echo "B died early" > "$OUT/B.early"; fi
# the ledger video: x11grab of :99 (A's display) at the window size, 30 fps,
# killed with the leg (the B1142 video pattern; the official client's rig
# records its :99 the same way).
ffmpeg -loglevel info -f x11grab -framerate 30 -video_size "$W"x"$H" -i :99.0 \
    -t 480 -pix_fmt yuv420p "$OUT/video.mp4" >"$OUT/video.log" 2>&1 &
FF=$!
"$BIN" -c "$OUT/cfgA.json" --renderer gles2 --egl-window -r "$W" "$H" --no-audio \
    --eqe1-path /mnt/projects/EverQuest_ETC1 \
    --dump-frames-dir "$OUT/frames" --dump-every 3 --dump-max-frames 1200 \
    --script "$OUT/tour.txt" \
    --log-level=DEBUG --log-module=MOVEMENT:DEBUG,GRAPHICS:INFO \
    >"$OUT/A.log" 2>&1 &
APID=$!
echo "$APID" > "$OUT/willeqA.pid"
date -u +%s.%N > "$OUT/A.launch.epoch"
sha256sum "$BIN" > "$OUT/willeq.sha256" 2>&1
# --- wait for A to finish its tour -------------------------------------------
wait "$APID"; RC_A=$?
echo "A rc=$RC_A" > "$OUT/willeqA.exit"
# A is out; B finishes its tour (it /q's) or gets bounced.
sleep 5
kill -INT "$BPID" 2>/dev/null || :
sleep 2
kill -TERM "$BPID" 2>/dev/null || :
wait "$BPID" 2>/dev/null || :
kill $FF 2>/dev/null || :
wait $FF 2>/dev/null || :
kill -INT $PCAP 2>/dev/null || :
sleep 1
wait $PCAP 2>/dev/null || :
sudo -n chown user "$OUT/capture.pcap" 2>/dev/null || :
# keep the per-leg cache's written-back ini as evidence, drop the rest
cp "$CACHE_A/ui/UI_Oraclewatch_WillEQ.ini" "$OUT/ui-after.ini" 2>/dev/null || :
rm -rf "$CACHE_A"
echo "done" > "$OUT/leg.done"
exit 0
