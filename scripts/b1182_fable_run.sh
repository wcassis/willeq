#!/bin/bash
# B1182 (#501): the live fable leg -- ONE command on the ThinkPad (lane 5).
#
#   bash scripts/b1182_fable_run.sh
#
# Seeds a cache dir with the lane's before-ini (junk SpellParticleDensity=
# banana, ShowSpellEffects absent -- byte-identical to the oracle's
# before-file, review/B1182/worker/oracle/out3/eqclient-before.ini), runs
# FABLE against the lane-5 eqemu, and reads the ini back.
#
# WHY A PLAIN GRAPHICS RUN, NOT --scenario: a --scenario run is Automated
# mode, which is headless -- no renderer is created at all
# (Application::initGraphics only initialises in GraphicalInteractive mode),
# so the load path (FrameComponent::syncEqClientIni) never runs in one. The
# load path needs no ZONE-IN: it runs on the first processFrame() frame --
# which starts when the loading progress reaches 50 %, i.e. zone data
# arriving -- AND again on EVERY clean shutdown (IrrlichtRenderer::shutdown
# calls syncUiPreferences). So a run that never connects still loads the
# ini and performs the single-key ShowSpellEffects rewrite on the way out:
# in graphics mode a pre-zone connect failure keeps the loading screen up
# (B1099, application_quit.cpp) and SIGTERM turns it into a clean shutdown.
# The leg therefore works with or without a working world:
#   - world up:   zone-in -> first processFrame frame rewrites during the run;
#   - world down: login gives up (30 s watchdog), SIGTERM clean shutdown
#                 rewrites on exit (exiting 3, the connect-failure code).
#
# Credentials stay on the ThinkPad: the observer creds file (env
# B1182_CREDS, default ~/eq-oracle/observer-Oraclewatch.cred) defines
# OBS_USER/OBS_PASS; nothing sensitive is in this script or the tree.
set -u
SRC=${B1182_SRC:-/home/user/src/willeq-wt/kb-b1182-ini-junk}
BIN=${B1182_BIN:-/home/user/willeq-builds/kb-b1182-ini-junk/build/bin/willeq}
TMP=${B1182_TMP:-/home/user/kb-b1182-fable}
CREDS=${B1182_CREDS:-$HOME/eq-oracle/observer-Oraclewatch.cred}
CHAR=${B1182_CHAR:-Oraclewatch}
SERVER=${B1182_SERVER:-"WillEQ Private Server"}
BEFORE=${B1182_BEFORE:-$TMP/eqclient.ini.bak2}
RUN_SECONDS=${B1182_RUN_SECONDS:-90}

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
echo "=== fable plain graphics run ($RUN_SECONDS s, cache dir $TMP) ==="
# timeout --signal=TERM: at the bound the run takes its CLEAN shutdown path
# (the SIGTERM latch -> requestQuit -> IrrlichtRenderer::shutdown -> the
# load path). Exit 124 = the bound fired (expected world-down); 0 = the run
# ended first (expected world-up, e.g. after a /camp -- not used here).
WILLEQ_CACHE_DIR="$TMP" timeout --signal=TERM "$RUN_SECONDS" \
    "$BIN" -c "$TMP/willeq.json" --log-level=INFO \
    > "$TMP/run.out" 2> "$TMP/run.log"
echo "client exit=$? (124 = clean-shutdown at the bound, 3 = pre-zone connect failure)"
cp "$TMP/eqclient.ini" "$TMP/eqclient-fable-after.ini"

echo "=== after ini: the two keys under test ==="
grep -nE "SpellParticleDensity=|ShowSpellEffects" "$TMP/eqclient.ini"
echo "=== diff before -> after ==="
diff "$TMP/eqclient-fable-before.ini" "$TMP/eqclient-fable-after.ini" || true
echo "=== the load path in the run log ==="
grep -n "eqclient.ini\|SIGTERM\|Render loop exited\|shutdown complete\|exiting" \
    "$TMP/run.out" "$TMP/run.log" 2>/dev/null | grep -v memmap | head -12

# Verdict: the after-file must be the oracle's own transform -- the junk
# density byte-identical and exactly one appended ShowSpellEffects=1.
if grep -q "^SpellParticleDensity=banana" "$TMP/eqclient.ini" \
   && [ "$(grep -c "^ShowSpellEffects=1\r\?$" "$TMP/eqclient.ini")" = "1" ]; then
    echo "=== PASS: fable's after-ini is the oracle's transform ==="
    exit 0
fi
echo "=== FAIL: the after-ini is not the oracle's transform ==="
exit 1
