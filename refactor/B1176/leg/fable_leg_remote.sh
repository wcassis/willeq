#!/bin/bash
# B1176 fable leg, runs ON the ThinkPad (lane 5). Two fable clients:
#   A = the windowed client under test (a build tree's bin/willeq) as <A_CHAR>
#       on willeqwatch. Its screen is fable's own --dump-frames-dir (allowed on
#       fable; B1142) -- never the official client's. The willeqwatch password
#       is read at run time from ~/eq-oracle/observer-Oraclewatch.cred (the
#       B1134 observer credential); it is written into cfgA.json and never
#       echoed or logged.
#   B = a second, HEADLESS willeq (same build tree) as Testcaster on
#       willeq_test (the B1142 rig's credential, already committed in the
#       B1142 scripts). B is the standing reference figure and the GM (it runs
#       the scene commands from fable_b_tour.txt).
# tcpdump -i any takes the wire (both clients' OP_ClientUpdate z; the veth
# copies are dropped, B1057).
# usage: fable_leg_remote.sh <out_dir> <build_dir> <a_char>
set -u
OUT=$1 BUILD=$2 ACHAR=$3
W=1280 H=800
BIN=$BUILD/bin/willeq
[ -x "$BIN" ] || { echo "no binary: $BIN" >&2; exit 65; }
mkdir -p "$OUT"
cp "$LEG_DIR/fable_tour.txt" "$OUT/tour.txt" 2>/dev/null || true
cp "$LEG_DIR/fable_b_tour.txt" "$OUT/b_tour.txt" 2>/dev/null || true
# --- credentials -------------------------------------------------------------
CRED=/home/user/eq-oracle/observer-Oraclewatch.cred
[ -r "$CRED" ] || { echo "observer credential unreadable" >&2; exit 65; }
APASS=$(awk -F: '{print $NF}' "$CRED")   # 'user:pass' -> pass; never printed
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
mkcfg "$OUT/cfgA.json" willeqwatch "$APASS" "$ACHAR"
# --- B's per-leg cache: nothing to suppress (B is headless) ------------------
CACHE_A=/home/user/b1176-cache/"$(basename "$OUT")"
rm -rf "$CACHE_A"; mkdir -p "$CACHE_A/ui"
# B834: fable's Tip of the Day pops at zone-in for a character whose ui ini has
# no [Tips] ShowTOTD=0; the window sits over the right of the screen -- exactly
# where the reference figure B stands in these frames -- so seed it off.
printf '[Tips]\nShowTOTD=0\n' > "$CACHE_A/ui/UI_${ACHAR}_WillEQ.ini"
echo "cache=$CACHE_A char=$ACHAR" > "$OUT/rig.txt"
export WILLEQ_CACHE_DIR="$CACHE_A"
# --- wire --------------------------------------------------------------------
filter='udp and (port 5998 or port 5999 or portrange 9000-9001 or portrange 7000-7030)'
veths=$(ip -o link show type veth 2>/dev/null | awk -F': ' '{printf "%sifindex %s", (n++ ? " or " : ""), $1}')
[[ -z $veths ]] || filter="$filter and not ($veths)"
printf '%s\n' "$filter" > "$OUT/pcap.filter"
sudo -n tcpdump -i any -s 0 -U -Z user -w "$OUT/capture.pcap" "$filter" >"$OUT/pcap.log" 2>&1 &
PCAP=$!
# --- B (headless) first -------------------------------------------------------
export DISPLAY=:99
xset s reset 2>/dev/null || :
xset s off -dpms 2>/dev/null || :
"$BIN" -c "$OUT/cfgB.json" --no-graphics --script "$OUT/b_tour.txt" \
    --log-level=DEBUG --log-module=MOVEMENT:DEBUG \
    >"$OUT/B.log" 2>&1 &
BPID=$!
echo "$BPID" > "$OUT/willeqB.pid"
# --- A (windowed, the subject) ------------------------------------------------
sleep 50
# A zones in during its tour's first wait (60 s). Launch it only once B's
# process is demonstrably alive and its config resolves.
if ! kill -0 "$BPID" 2>/dev/null; then echo "B died early" > "$OUT/B.early"; fi
"$BIN" -c "$OUT/cfgA.json" --renderer gles2 --egl-window -r "$W" "$H" --no-audio \
    --eqe1-path /mnt/projects/EverQuest_ETC1 \
    --dump-frames-dir "$OUT/frames" --dump-every 10 --dump-max-frames 800 \
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
# A is out; B is still holding the zone pose -- let it finish (it will /q on
# its tour) but do not wait the whole tour.
sleep 5
kill -INT "$BPID" 2>/dev/null || :
sleep 2
kill -TERM "$BPID" 2>/dev/null || :
wait "$BPID" 2>/dev/null || :
kill -INT $PCAP 2>/dev/null || :
sleep 1
wait $PCAP 2>/dev/null || :
sudo -n chown user "$OUT/capture.pcap" 2>/dev/null || :
# keep the per-leg cache's written-back ini as evidence, drop the rest
cp "$CACHE_A/ui/UI_${ACHAR}_WillEQ.ini" "$OUT/ui-after.ini" 2>/dev/null || :
rm -rf "$CACHE_A"
echo "done" > "$OUT/leg.done"
exit 0
