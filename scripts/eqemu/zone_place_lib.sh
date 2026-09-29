#!/usr/bin/env bash
# The zone PLACEMENT helpers shared by the live zone sweeps -- sourced, not
# executed. Extracted verbatim from scripts/scenario_zone_sweep.sh (B853/B854)
# so the headless sweep and the rendered Pi sweep
# (scripts/pi_rendered_zone_sweep.sh, #102) run ONE implementation of
# boot-then-seed-then-free-the-port, not two copies that drift.
#
# Requires, from the caller: scripts/eqemu/env.sh and restore_lib.sh sourced
# (eq_container_pkill, server_exec, server_exec_bg, eqemu_zone_booted,
# eqemu_sql), a `say` function, and these globals:
#   LOGS         directory for per-zone boot logs
#   ZONE_BOOT_S  seconds to wait for a zone's boot marker
#   CHAR         the character to place

# boot_zone <short> -- make sure a zone process for THIS zone is running and
# freshly booted. It holds the LINKDEAD client cache (camera_match_sweep.sh's
# second trap: a dead character's DB pose is never re-read by a live zone),
# so the run needs a process that started AFTER the seed. Bounce ONLY this
# zone's process (not the whole fleet -- the fleet's respawner would race the
# full pkill, and restarting 25 zones is both slow and wrong). The pkill goes
# through eq_container_pkill (board B798's guard); the pattern is safe because
# the literal "(" is not one of the alternatives.
boot_zone() {
    # Two statements: `local a=.. b=${a}` expands b BEFORE a is assigned, so
    # under set -u it only worked while a caller's own `local short` leaked
    # in by dynamic scope (scenario_zone_sweep.sh's run_zone).
    local short="$1"
    local bootlog="$LOGS/zone_${short}_boot.log"
    # If a process for this zone is already running, bounce it; otherwise just
    # start it. Either way we end with a freshly-booted process for this zone.
    if server_process_running zone >/dev/null 2>&1; then
        eq_container_pkill TERM "/bin/zone ${short}( |\$)" >/dev/null 2>&1 || true
        sleep 3
        eq_container_pkill KILL "/bin/zone ${short}( |\$)" >/dev/null 2>&1 || true
    fi
    # Truncate the boot log: eqemu_zone_booted greps it for the boot marker,
    # and a marker left by a previous run would make the check pass instantly
    # against a process that never booted.
    server_exec "rm -f logs/zone_${short}_sweep.log" >/dev/null 2>&1
    server_exec_bg "./bin/zone ${short} > logs/zone_${short}_sweep.log 2>&1"
    if ! eqemu_zone_booted "$short" "logs/zone_${short}_sweep.log" "$ZONE_BOOT_S" >"$bootlog" 2>&1; then
        say "zone $short did not boot -- last 40 log lines:"
        server_exec "tail -40 logs/zone_${short}_sweep.log" 2>&1 || true
        return 1
    fi
    return 0
}

# seed_char <short> -- place the character at the zone's safe point, with the
# server at rest, and verify the DB agrees. The safe point is the zone
# table's own row (safe_x/safe_y/safe_z), the same values eqemu's
# safe-return writes into character_data (world/worlddb.cpp). The
# write-readback-retry loop is camera_match_sweep.sh's: the late linkdead
# save can land after our UPDATE.
#
# THE COLUMN TRAP (selftest RED, 2026-09-21): character_data.zone_id stores
# the zone's STABLE zoneidnumber (eqemu's Zones:: enum space: qeynos2=2,
# relic=370, stonebrunt=100, dawnshroud=174 ...), NOT the zone table's
# auto-increment PK `id`. Seeding the PK sends the character into a DIFFERENT
# zone -- measured: seed zone_id=370 (stonebrunt's PK) and the client zoned
# into relic (370 is relic's zoneidnumber; relic.s3d is not a client asset,
# FATAL exit 2); seed zone_id=174 (iceclad's PK) and it zoned into
# dawnshroud (174 is dawnshroud's zoneidnumber; exit 0, wrong zone).
seed_char() {
    local short="$1" zone_id sx sy sz want i got
    zone_id="$(eqemu_sql "SELECT zoneidnumber FROM zone WHERE short_name='${short}';" | head -1 | tr -d '[:space:]')"
    if [ -z "$zone_id" ]; then
        say "no zone-table row for '${short}'"
        return 1
    fi
    sx="$(eqemu_sql "SELECT ROUND(safe_x,3) FROM zone WHERE short_name='${short}';" | head -1 | tr -d '[:space:]')"
    sy="$(eqemu_sql "SELECT ROUND(safe_y,3) FROM zone WHERE short_name='${short}';" | head -1 | tr -d '[:space:]')"
    sz="$(eqemu_sql "SELECT ROUND(safe_z,3) FROM zone WHERE short_name='${short}';" | head -1 | tr -d '[:space:]')"
    want="${zone_id},$(printf '%.3f' "$sx"),$(printf '%.3f' "$sy"),$(printf '%.3f' "$sz")"
    for i in 1 2 3 4 5 6; do
        # Do NOT touch last_login: the character-select row reads it
        # (world/worlddb.cpp: cse->LastLogin, and the GoHome rule does
        # now - last_login, so a 0/NULL row would look offline since the
        # epoch).
        eqemu_sql "UPDATE character_data SET zone_id=${zone_id}, x=${sx}, y=${sy}, z=${sz}, heading=0 WHERE name='${CHAR}';" >/dev/null
        sleep 5
        got="$(eqemu_sql "SELECT CONCAT_WS(',',zone_id,ROUND(x,3),ROUND(y,3),ROUND(z,3)) FROM character_data WHERE name='${CHAR}';" 2>/dev/null | tail -1 | tr -d '\r[:space:]')"
        if [ "$got" = "$want" ]; then
            say "seed confirmed: ${CHAR} -> ${short} (zoneidnumber ${zone_id}) safe point (${sx},${sy},${sz})"
            return 0
        fi
        say "seed not yet settled (db='${got}' want='${want}'), retrying"
    done
    say "could not pin ${CHAR}'s pose for ${short} (db='${got}' want='${want}')"
    return 1
}

# kill_zone <short> -- stop THIS zone's sweep process and free its port. The
# world assigns each zone process a port from a SMALL fixed range
# (measured: stonebrunt -> 7025; ~18 of my processes + 11 launcher-fleet
# processes exhausted it: "World did not have a port to assign from this
# server, the port range was not large enough" -- the zone boots, prints its
# marker, then shuts down, which the liveness check reports as
# zone_boot_failed). A dead zone's LINKDEAD client cache is gone with the
# process, so nothing is lost by stopping it: the NEXT run re-boots a fresh
# process after the next seed. eq_container_pkill (board B798's guard); the
# pattern is safe because the literal "(" is not one of the alternatives.
kill_zone() {
    local short="$1"
    eq_container_pkill TERM "/bin/zone ${short}( |\$)" >/dev/null 2>&1 || true
    sleep 2
    eq_container_pkill KILL "/bin/zone ${short}( |\$)" >/dev/null 2>&1 || true
}
