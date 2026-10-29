#!/bin/bash
# B1182 (#501): the live fable leg -- ONE command on the ThinkPad (lane 5).
#
#   bash scripts/b1182_fable_run.sh
#
# Seeds a cache dir with the lane's before-ini (junk SpellParticleDensity=
# banana, ShowSpellEffects absent -- byte-identical to the oracle's
# before-file, review/B1182/worker/oracle/out3/eqclient-before.ini), runs
# FABLE as a plain GRAPHICS run against the lane-5 eqemu, and reads the ini
# back.
#
# WHY A PLAIN GRAPHICS RUN, NOT --scenario: a --scenario run is Automated
# mode, which is headless -- no renderer is created at all
# (Application::initGraphics only initialises in GraphicalInteractive mode),
# so the load path (FrameComponent::syncEqClientIni) never runs in one. The
# load path needs no ZONE-IN: it runs on the first processFrame() frame --
# which starts when the loading progress reaches 50 %, i.e. zone data
# arriving -- AND again on EVERY clean shutdown (IrrlichtRenderer::shutdown
# calls syncUiPreferences). So a run that never connects still loads the ini
# and performs the single-key ShowSpellEffects rewrite on the way out: in
# graphics mode a pre-zone connect failure keeps the loading screen up
# (B1099, application_quit.cpp) and a clean SIGTERM turns it into the
# shutdown that runs the load path. The leg therefore works with or without
# a working world:
#   - world up:   zone-in -> the first processFrame frame rewrites during the
#                 run; the final SIGTERM clean shutdown re-checks (no-op).
#   - world down: the login gives up (30 s watchdog), the loading screen stays
#                 up, and the SIGTERM clean shutdown performs the load +
#                 rewrite (the run exits 3, the pre-zone connect-failure code).
#
# The shutdown is a plain `kill -TERM <pid>` (the clean-quit path the client
# handles in ordinary code, quit_signal.h); the measured run (run3.log) took
# that path and ran Application::shutdown() + the load path. A SIGKILL
# fallback bounds the wait if a clean shutdown ever stalls.
#
# Credentials stay on the ThinkPad: the observer creds file (env B1182_CREDS,
# default ~/eq-oracle/observer-Oraclewatch.cred) defines OBS_USER/OBS_PASS;
# nothing sensitive is in this script or the tree.
set -u
SRC=${B1182_SRC:-/home/user/src/willeq-wt/kb-b1182-ini-junk}
BIN=${B1182_BIN:-/home/user/willeq-builds/kb-b1182-ini-junk/build/bin/willeq}
TMP=${B1182_TMP:-/home/user/kb-b1182-fable}
CREDS=${B1182_CREDS:-$HOME/eq-oracle/observer-Oraclewatch.cred}
CHAR=${B1182_CHAR:-Oraclewatch}
SERVER=${B1182_SERVER:-"WillEQ Private Server"}
BEFORE=${B1182_BEFORE:-$TMP/eqclient.ini.bak2}
# Settle before the clean shutdown: long enough for the 30 s login give-up to
# be logged (world down) or a zone-in to complete (world up).
SETTLE=${B1182_SETTLE:-60}
GRACE=${B1182_GRACE:-30}

[ -f "$BEFORE" ] || { echo "FATAL: before-ini not found: $BEFORE" >&2; exit 64; }
[ -f "$CREDS" ] || { echo "FATAL: creds file not found: $CREDS" >&2; exit 64; }
# Sanity: the seed must still be the edited before-file.
grep -q "^SpellParticleDensity=banana" "$BEFORE" || {
    echo "FATAL: $BEFORE does not carry the junk density -- wrong seed" >&2; exit 64; }
grep -q "^ShowSpellEffects=" "$BEFORE" && {
    echo "FATAL: $BEFORE already states ShowSpellEffects -- wrong seed" >&2; exit 64; }

. "$CREDS"
mkdir -p "$TMP"
cp "$BEFORE" "$TMP/eqclient.ini"
cp "$BEFORE" "$TMP/eqclient-fable-before.ini"
printf '{"eq_client_path": "/mnt/projects/EverQuestP1999", "host": "127.0.0.1", "user": "%s", "pass": "%s", "server": "%s", "character": "%s"}' \
    "$OBS_USER" "$OBS_PASS" "$SERVER" "$CHAR" > "$TMP/willeq.json"

cd "$SRC"
echo "=== fable plain graphics run (settle ${SETTLE}s, cache dir $TMP) ==="
WILLEQ_CACHE_DIR="$TMP" "$BIN" -c "$TMP/willeq.json" --log-level=INFO \
    > "$TMP/run.out" 2> "$TMP/run.log" &
PID=$!
sleep "$SETTLE"
if ! kill -0 "$PID" 2>/dev/null; then
    echo "FATAL: the client exited before the settle (see $TMP/run.out) -- the load path needs a live renderer" >&2
    exit 65
fi
# The clean-quit path (quit_signal.h): the first SIGTERM latches and the
# render loop turns it into requestQuit() -> Application::shutdown() -> the
# load path. A second SIGTERM is the forced exit.
kill -TERM "$PID"
for _ in $(seq 1 "$GRACE"); do
    kill -0 "$PID" 2>/dev/null || break
    sleep 1
done
if kill -0 "$PID" 2>/dev/null; then
    echo "WARN: clean shutdown did not finish in ${GRACE}s -- second SIGTERM (forced exit)"
    kill -TERM "$PID" 2>/dev/null
    sleep 2
    kill -0 "$PID" 2>/dev/null && kill -KILL "$PID" 2>/dev/null
fi
wait "$PID" 2>/dev/null
echo "client exit=$?"
cp "$TMP/eqclient.ini" "$TMP/eqclient-fable-after.ini"

echo "=== after ini: the two keys under test ==="
grep -nE "SpellParticleDensity=|ShowSpellEffects" "$TMP/eqclient.ini"
echo "=== diff before -> after ==="
diff "$TMP/eqclient-fable-before.ini" "$TMP/eqclient-fable-after.ini" || true
echo "=== the load path in the run log ==="
grep -n "eqclient.ini\|SIGTERM\|Render loop exited\|shutdown complete\|exiting\|Could not connect\|Zoned in\|Fully connected" \
    "$TMP/run.out" "$TMP/run.log" 2>/dev/null | grep -v memmap | head -14

# Verdict: the after-file must be the oracle's own transform -- the junk
# density byte-identical and exactly one appended ShowSpellEffects=1.
# (The file is CRLF, so the anchor carries a real CR byte -- grep patterns do
# not interpret \r escapes.)
CR=$(printf '\r')
if grep -q "^SpellParticleDensity=banana" "$TMP/eqclient.ini" \
   && [ "$(grep -c "^ShowSpellEffects=1${CR}\?$" "$TMP/eqclient.ini")" = "1" ]; then
    echo "=== PASS: fable's after-ini is the oracle's transform ==="
    exit 0
fi
echo "=== FAIL: the after-ini is not the oracle's transform ==="
exit 1
