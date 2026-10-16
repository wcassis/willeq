#!/usr/bin/env bash
# T410 (board B138): run one live scenario end to end and exit with its code.
#
# This is the piece every live unit on the board re-implements by hand
# (review/T377|T386|T391|T401/scripts): take the eqemu lock, bring the server
# up, snapshot the character rows, run a client, restore the snapshot with the
# world DOWN, bring the server down, release the lock. The difference is that
# the RESULT is this script's exit status, not a transcript a human reads.
#
# Usage:
#   scripts/scenario.sh <scenario> [--timeout N] [--hold N] [--spell ID]
#                                  [--config FILE] [--peer-config FILE]
#                                  [--unit T###] [--keep-server]
#                                  [--inventory-chars "67,68"] [--seed-sql FILE]
#                                  [--expect-material N]
#                                  [--create-character "Name,Race,Gender,Class,Deity,City"]
#                                  [--log-level INFO|DEBUG]
#   scripts/scenario.sh --selftest [--unit T###] [--config FILE] [...]
#
# --selftest runs scripts/eqemu/restore_selftest.sh, the regression net for the
# snapshot restore this script performs (T493, board B784): it logs a character
# in, makes the snapshot and the world disagree, restores, tears the server
# down and checks that the snapshot survived. Everything after --selftest is
# passed to it. Run it after ANY change to the restore path.
#
# Exit codes are the client's, passed through unchanged:
#   0 pass   1 fail   2 timeout   3 could-not-start
# plus this script's own operator errors:
#   64 lock held by another session   65 server would not come up
#   66 snapshot/restore mismatch   72 the cast_lands spell is not castable
#   by the config's character (B484, issue #35)
#
# It NEVER waits for the lock and never overwrites one: a held lock is exit
# 64 immediately (T224 clobbered another session's lock 90 s after seeing it
# absent -- the take below is `set -o noclobber`, atomic).

set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# T493: `--selftest` is how a user reaches the restore's regression net. It
# takes the lock itself, so hand it the whole command line and get out of the
# way.
if [ "${1:-}" = "--selftest" ]; then
    shift
    exec bash "$REPO/scripts/eqemu/restore_selftest.sh" "$@"
fi

SCENARIO="${1:?usage: scenario.sh <scenario> [options] | scenario.sh --selftest [options]}"
shift

# eqemu runs on its own VM; env.sh supplies eq_docker/mysql + the lock on that host.
source "$REPO/scripts/eqemu/env.sh"
# T493 (board B784): ONE snapshot/restore implementation, shared with
# scripts/llmbench/{record,teardown}.sh. Do not inline another copy here.
source "$REPO/scripts/eqemu/restore_lib.sh"
UNIT="T410"
TIMEOUT=300
HOLD=300
# B484 (issue #35): 288 = Minor Shielding, classes12 = 1, the spell T449
# proved lands for the config's Wizard (Testcaster). The old default, 46
# (Ultravision), has classes12 = 255 -- uncastable by that class -- so the
# lane fizzled 3/3 and could never pass as shipped. The pre-flight below
# re-checks this against spells_new.classesN for the config's own class and
# level, so a default or --spell that stops being castable is a fast exit 72,
# not a fizzle loop.
SPELL=288
# B630 fix pass (QA verdict on MR !20): the chain's A leg is a RIG constant,
# not an operator option. The lane's own B630Options pins it (the unit test's
# declared A-leg spell) and b630_analyze.py asserts it (A_SPELL_ID); the
# operator's --spell applies to cast_lands and the standalone legs only.
# Changing one side without the other is exactly the 378-vs-288 split the QA
# caught: with the 288 default the chain's A leg could never pass its own
# "declared spell (ID 378)" cell.
CHAIN_A_SPELL=378
CONFIG=""
PEER_CONFIG=""
KEEP_SERVER=0
# T494: the client's log level, for both processes. INFO is the default a
# verdict needs; a run being DIAGNOSED wants DEBUG.
LOG_LEVEL="INFO"
# B630 item 3: the B760 deliverable (#45) is a per-opcode cast-tail table, and
# the only record of the wire order is the per-opcode [ZonePacket] DEBUG line
# (ZoneComponent::DispatchZonePacket, with rel=0/1 since B760) plus the [SPELL] handler lines. The whole
# interrupted-cast family needs ZONE and SPELL at DEBUG; every OTHER scenario
# stays at plain INFO. The module list is the comma form parsed by
# --log-module (application_config.cpp). A run being DIAGNOSED with full
# --log-level DEBUG subsumes this, so it is never forced on.
log_module_for() {
    case "$1" in
        cast_interrupt_chain|cast_interrupt|cast_interrupt_observe|cast_success_tail|cast_recast_stop|cast_buff_notake|cast_fizzle|cast_tail_*)
            echo "ZONE:DEBUG,SPELL:DEBUG" ;;
        # UIGAP-live: ignore_tell's DELIVERED half is the client's own
        # MAIN:DEBUG "Ignored tell message from <peer>" line (the server sent
        # the tell; the ignore filter dropped it). ignore_tell.post.sh greps it.
        ignore_tell)
            echo "MAIN:DEBUG,ZONE:DEBUG" ;;
        *)
            echo "" ;;
    esac
}
# T494: a scenario that MOVES AN ITEM needs the inventory rows snapshotted and
# put back too -- eqemu_snapshot only covers character_data/character_currency,
# so before T494 an /equip left the row where the run put it. Both are
# restore_lib.sh's (eqemu_inventory_snapshot / eqemu_inventory_restore); this
# script still owns no restore SQL of its own.
INVENTORY_CHARS=""
SEED_SQL=""
EXPECT_MATERIAL="-1"
CREATE_CHARACTER=""

while [ $# -gt 0 ]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --hold) HOLD="$2"; shift 2 ;;
        --spell) SPELL="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --peer-config) PEER_CONFIG="$2"; shift 2 ;;
        --unit) UNIT="$2"; shift 2 ;;
        --keep-server) KEEP_SERVER=1; shift ;;
        --log-level) LOG_LEVEL="$2"; shift 2 ;;
        --inventory-chars) INVENTORY_CHARS="$2"; shift 2 ;;
        --seed-sql) SEED_SQL="$2"; shift 2 ;;
        # T498: the peer's SECOND login asserts its OWN primary material is this
        # number -- the server's items.idfile for the seeded weapon. -1 (the
        # default) means it only stays logged in, as before.
        --expect-material) EXPECT_MATERIAL="$2"; shift 2 ;;
        # UIGAP-live2 (B888, #204): the world script for the create_character
        # scenario, passed to the MAIN client only (create_character.post.sh
        # runs the --delete-character half against the same config).
        --create-character) CREATE_CHARACTER="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done

# B1008 (#329): one output dir PER LANE. Parallel lanes under one unit id
# shared a snapshot file, and the second lane's restore compared against the
# first lane's rows (exit 66 on lane 2, 2026-10-08). Lane 1 keeps the old path.
LANE_TAG=""; [ "${EQEMU_LANE:-1}" != "1" ] && LANE_TAG="-lane${EQEMU_LANE}"
OUT="/mnt/projects/fable/review/${UNIT}/scenario${LANE_TAG}"
mkdir -p "$OUT"
LOG="$OUT/${SCENARIO}_client.log"
PEERLOG="$OUT/${SCENARIO}_peer.log"
BEFORE="$OUT/${SCENARIO}_snapshot_before.tsv"
AFTER="$OUT/${SCENARIO}_snapshot_after.tsv"
INV_BEFORE="$OUT/${SCENARIO}_inventory_before.tsv"
# B1025-H5 (#345): the clients' PER-CHARACTER generated files (the hotbar INI,
# the UI preferences, the shader cache) go under $WILLEQ_CACHE_DIR
# (ResolveCachePathForWrite, data_paths.h) -- never the checkout. The lane pins
# it to OUT so the post-check can read the hotbar INI from a known place, and
# export it once here: it rides into the MAIN client (this shell) AND the peer
# subshell (it inherits the environment), so both characters' INIs land in the
# same dir, named by the character (UI_<Char>_<Server>.ini).
export WILLEQ_CACHE_DIR="$OUT/cache"
# A task worktree has no build/ of its own; the shared build lives in main.
BIN="${WILLEQ_BIN:-}"
if [ -z "$BIN" ]; then
    for c in "$REPO/build/bin/willeq" /mnt/projects/fable/main/build/bin/willeq; do
        [ -x "$c" ] && { BIN="$c"; break; }
    done
fi

# T494: the default config follows the LANE, exactly as
# scripts/eqemu/restore_selftest.sh:85-94 already did. EQEMU_HOST=local means
# this host's stack, whose configs are the gitignored `<name>.local.json`
# copies; anything else is the VM and gets the shipped `<name>.json`. Without
# this a `EQEMU_HOST=local` run took the local lock, brought the local stack up
# and then pointed the client at the VM -- the lane leak T489/T493 hit.
LANE_SUFFIX="json"
[ "${EQEMU_HOST:-}" = "local" ] && LANE_SUFFIX="local.json"
CONFIG_GIVEN="$CONFIG"; PEER_GIVEN="$PEER_CONFIG"
[ -n "$CONFIG" ] || CONFIG="$REPO/scripts/scenario_configs/${SCENARIO}.${LANE_SUFFIX}"
[ -n "$PEER_CONFIG" ] || PEER_CONFIG="$REPO/scripts/scenario_configs/peer.${LANE_SUFFIX}"
# B1008 (#329): lanes 2-4 DERIVE their client configs from the shipped lane-1
# files by substituting the lane's server for every "host" value, into the
# per-lane OUT -- no per-lane copies in the tree to drift. An explicit
# --config/--peer-config is taken as given (and still lane-checked below).
derive_lane_config() {  # derive_lane_config <shipped.json> -> prints the per-lane path
    local src="$1" dst
    dst="$OUT/$(basename "$1" .json).lane${EQEMU_LANE}.json"
    [ -f "$src" ] || { echo "$src"; return; }
    sed "s/\"host\"\([[:space:]]*:[[:space:]]*\)\"[^\"]*\"/\"host\"\1\"${EQEMU_HOST}\"/g" "$src" > "$dst" && echo "$dst"
}
if [ "${EQEMU_LANE:-1}" != "1" ] && [ "${EQEMU_HOST:-}" != "local" ]; then
    [ -n "$CONFIG_GIVEN" ] || CONFIG="$(derive_lane_config "$CONFIG")"
    [ -n "$PEER_GIVEN" ] || PEER_CONFIG="$(derive_lane_config "$PEER_CONFIG")"
fi

say() { printf '[scenario.sh] %s\n' "$*" >&2; }

# Lane guard, same shape as restore_selftest.sh:120-131: the config's host must
# be this lane's server, whichever way the mismatch runs.
lane_check() {  # lane_check <config file>
    local cfg="$1" cfg_host lane_host
    [ -f "$cfg" ] || { say "no client config at $cfg"; return 1; }
    cfg_host="$(sed -n 's/.*"host"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$cfg" | head -1)"
    lane_host="$(eqemu_port_host)"
    case "$cfg_host" in
        "$lane_host"|127.0.0.1|localhost) return 0 ;;
    esac
    if [ "$lane_host" = 127.0.0.1 ] && hostname -I | tr ' ' '\n' | grep -qx "$cfg_host"; then
        return 0
    fi
    say "LANE MISMATCH: ${cfg}'s host ${cfg_host} is not this lane's server (${lane_host}, EQEMU_HOST=${EQEMU_HOST:-unset}) -- refusing"
    return 1
}
lane_check "$CONFIG" || exit 65

# ---- 1. the lock, taken atomically -----------------------------------------
if ! eqemu_lock_take "${UNIT} scenario.sh ${SCENARIO}"; then
    say "eqemu lock is HELD by: $(eqemu_lock_show) -- refusing to run"
    exit 64
fi
say "lock taken"

CLEANED=0
RESTORE_BAD=0
cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    kill ${PEER_PID:-} ${CLIENT_PID:-} >/dev/null 2>&1
    sleep 2
    kill -9 ${PEER_PID:-} ${CLIENT_PID:-} >/dev/null 2>&1
    [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" >/dev/null 2>&1
    # The restore runs on EVERY path now, --keep-server included: it is what
    # leaves the database as the run found it, and leaving a moved character
    # behind for the next unit is the whole point of B784. It stops the
    # eqemu-server container itself, so --keep-server starts it again.
    restore_snapshot
    if [ "$KEEP_SERVER" -eq 0 ]; then
        bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/${SCENARIO}_server.log" 2>&1
    else
        eqemu_world_start
        say "eqemu-server container restarted (--keep-server)"
    fi
    eqemu_lock_release
    say "lock released"
}
trap cleanup EXIT INT TERM

# The snapshot and the restore are restore_lib.sh's, not this script's.
snapshot() { eqemu_snapshot "$1"; }

restore_snapshot() {
    # T493 (board B784). What this used to do, and why none of it survives:
    # it `pkill`ed world/zone/loginserver INSIDE the eqemu-server container and
    # then issued one UPDATE per row. Both halves were broken. The pkill chain
    # matched its OWN command line on the first pattern (`/tmp/spire-launcher`)
    # and killed the exec's shell before the other three ran -- MEASURED: rc
    # 143, and 27 world/zone processes still alive afterwards -- and the
    # per-row loop cost 22 s locally and 2 min over ssh. So the restore ran
    # against a live world, reported itself byte-identical, and was then undone
    # by the zone's shutdown save when down.sh followed it: MEASURED, Testbuddy
    # back at 0/0 over the snapshot's 1111/2222.
    #
    # eqemu_restore_snapshot stops the CONTAINER (mariadb is a separate one and
    # stays up) and sends the whole restore as one statement batch.
    # scripts/eqemu/restore_selftest.sh pins it; `scenario.sh --selftest` runs
    # that.
    eqemu_restore_snapshot "$BEFORE" "$AFTER" say || RESTORE_BAD=1
    # T494: the inventory half. eqemu_restore_snapshot has already stopped the
    # eqemu-server container (eqemu_world_stop is idempotent), so this replaces
    # the rows with the world down, same as the character rows above.
    if [ -n "$INVENTORY_CHARS" ]; then
        eqemu_world_stop
        eqemu_inventory_restore "$INV_BEFORE" "$INVENTORY_CHARS"
        say "inventory rows for ${INVENTORY_CHARS} restored from $(wc -l < "$INV_BEFORE") snapshot rows"
    fi
}

# ---- the castability pre-flight (cast_lands only) ---------------------------
# B484 (issue #35): the shipped default was spell 46 (Ultravision), whose
# spells_new.classes12 is 255 -- the MINIMUM level at which class 12 (Wizard)
# may cast it. The config's character is Testcaster, class 12, level 1: below
# every class's minimum for spell 46 (min over all 16 columns is 26). Neither
# fizzle model in zone/spells.cpp Client::CheckFizzle is a hard block; both
# clamp this caster's fizzle chance to 95% (legacy: spell_difficulty = 26*5
# = 130 + basediff 25, no skill to offset; non-legacy: par_skill 235 + 255,
# capped, + basediff, level/skill 0). So a gem cast lands ~1 in 20 and the
# 3-attempt lane passes ~1 in 7. Measured: the shipped lane fizzled 3/3 and
# failed (RED), and the same run repeated by hand landed 1/3 (RED2). Three
# 20-second windows on a coin that comes down fizzle 19 times out of 20 is
# not a lane.
#
# A comment is not a guard: a config that points at another character, or a
# --spell an operator passes, can both be uncastable. So before the client
# runs, the rig asks the database. spells_new.classes<N> is the minimum level
# at which class N may cast the spell (eqemu's own table documents it,
# common/spdat.h: "Classes, and their min levels"); a character at or above
# it is in the spell's intended band. A character below it, or a config whose
# character the server does not know, is reported here -- naming the spell,
# class and level -- instead of burning three gem casts on a
# "Your spell fizzles!" loop the operator has to decode.
#
# Exit 72: the spell is not in the config's character's castable band (64/65/
# 66 are this script's existing operator codes; the selftest family owns
# 67-71).
spell_castability_check() {  # spell_castability_check <character name>
    local char_name="$1" class_num char_level min_level class_name
    class_num="$(eqemu_sql "SELECT class FROM character_data WHERE name='${char_name}';" | head -1 | tr -d '[:space:]')"
    if [ -z "$class_num" ]; then
        say "no character_data row for '${char_name}' -- the config points at a character the server does not know"
        exit 64
    fi
    char_level="$(eqemu_sql "SELECT level FROM character_data WHERE name='${char_name}';" | head -1 | tr -d '[:space:]')"
    # spells_new has one classes<N> column per eqemu class (1-16, Warrior-
    # Berserker); the class number IS the column number. A class outside that
    # range means the pre-flight cannot reason about castability.
    if ! [ "$class_num" -ge 1 ] 2>/dev/null || ! [ "$class_num" -le 16 ] 2>/dev/null; then
        say "castability pre-flight: character '${char_name}' has class ${class_num}, outside spells_new's classes1-16 -- refusing to guess"
        exit 72
    fi
    min_level="$(eqemu_sql "SELECT classes${class_num} FROM spells_new WHERE id=${SPELL};" | head -1 | tr -d '[:space:]')"
    if [ -z "$min_level" ]; then
        say "spell ${SPELL} has no spells_new row -- the server knows no such spell"
        exit 72
    fi
    case "$class_num" in
        1) class_name=Warrior ;; 2) class_name=Cleric ;; 3) class_name=Paladin ;;
        4) class_name=Ranger ;; 5) class_name=Shadowknight ;; 6) class_name=Druid ;;
        7) class_name=Monk ;; 8) class_name=Bard ;; 9) class_name=Rogue ;;
        10) class_name=Shaman ;; 11) class_name=Necromancer ;; 12) class_name=Wizard ;;
        13) class_name=Magician ;; 14) class_name=Enchanter ;; 15) class_name=Beastlord ;;
        16) class_name=Berserker ;;
    esac
    # 255 means "no class carries this spell at a reachable level", and any
    # minimum the character sits below does the same job: neither fizzle model
    # in zone/spells.cpp Client::CheckFizzle is a hard block; both clamp this
    # kind of caster's fizzle chance to 95%, so a gem cast lands ~1 in 20 and
    # the 3-attempt lane passes ~1 in 7 (measured on this card: RED1 fizzled
    # 3/3, RED2 landed 1/3, same server/character/spell). That is a coin, not
    # a lane. One message covers both, naming the spell, the class, and the
    # level the operator actually has.
    if ! [ "$char_level" -ge "$min_level" ] 2>/dev/null; then
        if [ "$min_level" -ge 255 ]; then
            say "spell ${SPELL} has no minimum level for ${class_name} (spells_new.classes${class_num} = ${min_level}, the table's 255 sentinel); ${char_name} (level ${char_level}) cannot cast it reliably -- pick a spell this class carries, e.g. --spell 288 (Minor Shielding)"
        else
            say "spell ${SPELL} needs at least level ${min_level} for ${class_name}, but ${char_name} is level ${char_level} -- the server will fizzle it hard (a level this far under the minimum is a ~95% fizzle, so the lane would pass only about 1 in 7 runs); raise the character or pick another spell"
        fi
        exit 72
    fi
    say "castability pre-flight: spell ${SPELL} is castable by ${char_name} (${class_name}, level ${char_level}; minimum level ${min_level})"
}

# ---- 2. server up ----------------------------------------------------------
say "bringing eqemu up"
if ! bash "$REPO/scripts/eqemu/up.sh" > "$OUT/${SCENARIO}_server.log" 2>&1; then
    say "up.sh failed -- see $OUT/${SCENARIO}_server.log"
    exit 65
fi

# ---- 3. snapshot -----------------------------------------------------------
snapshot "$BEFORE"
say "snapshot taken ($(wc -l < "$BEFORE") rows)"
# B900 (#216) / B1011: a scenario whose kit is DB-seeded into INVENTORY names
# its characters HERE, before the inventory snapshot, so the seed lands after
# the snapshot and the restore takes it out again. (B900's first shape put this
# derivation -- and the snapshot itself -- inside the --seed-sql branch, so a
# run with --inventory-chars and no seed took NO inventory snapshot and its
# restore was a silent no-op; and a run with a custom --seed-sql skipped the
# derivation and left the kit behind. Both are B1011's fix: the snapshot is
# unconditional on INVENTORY_CHARS, the derivation unconditional on the seed.)
if [ "$SCENARIO" = "ranged_single_shot" ] && [ -z "$INVENTORY_CHARS" ]; then
    INVENTORY_CHARS="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testchar';" | head -1)"
fi
# B1025-H5 (#345): the hotbar item/skill kit is DB-seeded into Testcaster and
# the item leg CONSUMES a charge, so Testcaster's inventory rows get the
# snapshot/restore pair (the item slot's 10-charge stack is what the run
# mutates; the restore takes the whole seeded kit out again).
if [ "$SCENARIO" = "hotbar_lifecycle" ] && [ -z "$INVENTORY_CHARS" ]; then
    INVENTORY_CHARS="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testcaster';" | head -1)"
fi
# The inventory snapshot, whenever the run names inventory characters -- always
# BEFORE any seed (a seed after the snapshot is invisible to the restore and
# survives the run: duplicate-key on the next seed, the kit left behind;
# measured 2026-10-08, lane 4).
if [ -n "$INVENTORY_CHARS" ]; then
    eqemu_inventory_snapshot "$INV_BEFORE" "$INVENTORY_CHARS"
    say "inventory snapshot taken ($(wc -l < "$INV_BEFORE") rows for ${INVENTORY_CHARS})"
fi

# ---- 3b. seed, AFTER the snapshots so the restore takes it out again --------
# B630 (issue #48): the co-location seed is a LANE concern, not an operator
# option -- default it HERE, before this block runs. Setting it later (at the
# client launch) leaves this block seeing an empty SEED_SQL and the two
# characters log into their stored zones, unco-located. (First B630 run
# 2026-09-19: seed never applied, caster 'nro' / observer 'befallen'.)
case "$SCENARIO" in
    cast_interrupt_chain|cast_tail_repeat_*)
        # B760 (#45): the repeated cast-tail lane uses B630's co-location seed.
        [ -n "$SEED_SQL" ] || SEED_SQL="$REPO/scripts/scenario_configs/b630_seed.sql" ;;
esac
# B900 (#216): the bow + arrow kit is DB-seeded (the client cannot seat the
# ammo slot -- server bitmask bit 22 vs Titanium slot 21), and a shot that
# consumes an arrow is an INVENTORY mutation, so Testchar's rows get the
# snapshot/restore pair exactly like T494's equip lane.
if [ "$SCENARIO" = "ranged_single_shot" ] && [ -z "$SEED_SQL" ]; then
    SEED_SQL="$REPO/scripts/scenario_configs/ranged_single_shot_seed.sql"
fi
# B1025-H5 (#345): the hotbar kit is DB-seeded (Testcaster's primary and
# general slots are already full, so #summonitem + /putdown cannot seat the
# pole without swapping the old primary onto the cursor). The restore
# (INVENTORY_CHARS above) takes it out again.
if [ "$SCENARIO" = "hotbar_lifecycle" ] && [ -z "$SEED_SQL" ]; then
    SEED_SQL="$REPO/scripts/scenario_configs/hotbar_lifecycle_seed.sql"
fi
# B1068 (#388): the walked zone-line crossing starts a few units short of
# qeynos -> qeynos2's BSP zone-line region, facing it; the restore moves the
# character back.
if [ "$SCENARIO" = "zone_line_walk" ] && [ -z "$SEED_SQL" ]; then
    SEED_SQL="$REPO/scripts/scenario_configs/zone_line_walk_seed.sql"
fi
if [ -n "$SEED_SQL" ]; then
    [ -f "$SEED_SQL" ] || { say "no --seed-sql file at $SEED_SQL"; exit 64; }
    eqemu_sql "$(cat "$SEED_SQL")" >/dev/null || { say "--seed-sql failed"; exit 65; }
    say "seeded from $SEED_SQL"
fi

# ---- 3c. the castability pre-flight, cast_lands only ------------------------
# The server is up (3) and the character is in the world the client is about
# to log into, so its class/level is the row the server will hand the client.
# Everything up to here is undone by the trap; an exit 72 here is a fast,
# operator-readable failure instead of the fizzle loop (B484, issue #35).
if [ "$SCENARIO" = "cast_lands" ]; then
    CAST_LANDS_CHAR="$(sed -n 's/.*"character"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$CONFIG" | head -1)"
    if [ -z "$CAST_LANDS_CHAR" ]; then
        say "no \"character\" in $CONFIG"
        exit 64
    fi
    spell_castability_check "$CAST_LANDS_CHAR"
fi

# ---- 4. the peer, for two_client_survive ------------------------------------
# The partner is a PEER PROCESS running zone_in on the second account -- the
# client never forks a second client (it would need another character's whole
# login config, which lives here). T391 staggered the partner by ~55 s so it
# arrives as a fresh spawn packet rather than inside the bulk zone-in list;
# the observer's peer window (180 s from ITS zone-in) covers that.
PEER_PID=""
# T494: equip_observed needs the peer to ACT (equip_actor), not just exist.
PEER_SCENARIO=""
PEER_SCENARIO_2=""
# B630 fix pass: the CHAIN observer's hold is NOT the lane's --hold/--timeout
# (300 s): the caster's four legs run back-to-back (~85 s incl. login offset)
# and then exit, so a 300 s hold always ended with the observer failing on
# "caster left the zone during the hold" (devrun-1 2026-09-19). 120 s covers
# the chain plus margin and is what the observer is asked to survive. The
# peer's --timeout stays the lane's, so the hold above it is never a problem.
CHAIN_OBSERVE_HOLD=120
case "$SCENARIO" in
    two_client_survive) PEER_SCENARIO="zone_in" ;;
    equip_observed)     PEER_SCENARIO="equip_actor"; PEER_SCENARIO_2="equip_actor_return" ;;
    # B1025-H5 (#345): acceptance C -- a SECOND character on the account. The
    # peer (Testbuddy) zones in and assigns ONE skill slot through the same
    # user path; its per-character INI must end up holding ONLY that slot and
    # none of Testcaster's. The main character's INI must hold its three typed
    # slots. The post-check reads both INI files (the isolation verdict).
    hotbar_lifecycle)   PEER_SCENARIO="hotbar_isolation_assign" ;;
    # B630 (issue #48): the caster runs the whole 4-leg chain, the observer
    # targets the caster and holds it for the chain's duration. Co-location is
    # the seed SQL, not a re-login.
    cast_interrupt_chain) PEER_SCENARIO="cast_interrupt_observe" ;;
    # B760 (#45): the repeated cast-tail lane; the caster's name picks the peer.
    cast_tail_repeat_quiet) PEER_SCENARIO="cast_tail_quiet_peer" ;;
    cast_tail_repeat_traffic) PEER_SCENARIO="cast_tail_traffic_peer" ;;
    # UIGAP-live (#151 #213 #211): the UI legs' peers. The click-equip pair is
    # equip_observed's shape (the actor equips, exits and logs back in);
    # social_peer and track_peer get the main character's name as their
    # --scenario-cast-target (the generic peer branch below passes it).
    click_equip_observed) PEER_SCENARIO="click_equip_actor"; PEER_SCENARIO_2="equip_actor_return" ;;
    friends_who|ignore_tell) PEER_SCENARIO="social_peer" ;;
    track_list) PEER_SCENARIO="track_peer" ;;
    # UIGAP-live2 (#212): the trade partner.
    trade_roundtrip) PEER_SCENARIO="trade_peer" ;;
esac
if [ -n "$PEER_SCENARIO" ]; then
    if [ ! -f "$PEER_CONFIG" ]; then
        say "$SCENARIO needs --peer-config <file> (a second account)"
        exit 64
    fi
    lane_check "$PEER_CONFIG" || exit 65
    # B630: the two characters' names, so each process targets the OTHER one.
    # The caster (this script's CONFIG) targets the buddy; the observer (the
    # PEER) targets the caster.
    CASTER_CHAR="$(sed -n 's/.*"character"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$CONFIG" | head -1)"
    PEER_CHAR="$(sed -n 's/.*"character"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$PEER_CONFIG" | head -1)"
    # T391 staggered the partner by ~55 s so it arrives as a fresh spawn packet
    # rather than inside the bulk zone-in list (two_client_survive). The B630
    # chain needs the observer to already be a tracked peer when the caster
    # starts leg A, so it only waits a few seconds.
    PEER_STAGGER=55
    [ "$PEER_SCENARIO" = "cast_interrupt_observe" ] && PEER_STAGGER=8
    case "$PEER_SCENARIO" in cast_tail_*_peer) PEER_STAGGER=8 ;; esac
    PEER_LOG_MODULE="$(log_module_for "$PEER_SCENARIO")"
    (
        sleep "$PEER_STAGGER"
        case "$PEER_SCENARIO" in
            cast_interrupt_observe)
                # The chain observer holds for CHAIN_OBSERVE_HOLD (120 s, not
                # the lane's --timeout): the chain's legs finish in ~85 s, and
                # its caster's departure ends the observer early via the
                # departure floor (see CastInterruptObserveScenario).
                "$BIN" -c "$PEER_CONFIG" --scenario "$PEER_SCENARIO" \
                    --timeout "$TIMEOUT" \
                    --scenario-hold "$CHAIN_OBSERVE_HOLD" \
                    --scenario-cast-target "$CASTER_CHAR" \
                    --log-level="$LOG_LEVEL" \
                    ${PEER_LOG_MODULE:+--log-module="$PEER_LOG_MODULE"} > "$PEERLOG" 2>&1
                ;;
            *)
                # UIGAP-live: every peer is told the main character's name
                # (social_peer tells it, the others ignore the flag).
                "$BIN" -c "$PEER_CONFIG" --scenario "$PEER_SCENARIO" --timeout "$TIMEOUT" \
                    --scenario-cast-target "$CASTER_CHAR" \
                    --log-level="$LOG_LEVEL" \
                    ${PEER_LOG_MODULE:+--log-module="$PEER_LOG_MODULE"} > "$PEERLOG" 2>&1
                ;;
        esac
        # T494: equip_observed's peer runs TWICE. eqemu does not broadcast a
        # weapon WearChange on an OP_MoveItem equip (Client::SlotConvert2,
        # zone/client.cpp:4128-4154, has no slotPrimary case, so the packet
        # describes the head and the dedupe in zone/mob_appearance.cpp drops
        # it) -- the observable the server WILL produce is the peer's next
        # SPAWN, whose materials come from the database. So the actor equips
        # and exits, and the same character logs straight back in.
        if [ -n "$PEER_SCENARIO_2" ]; then
            sleep 15
            "$BIN" -c "$PEER_CONFIG" --scenario "$PEER_SCENARIO_2" --timeout "$TIMEOUT" \
                --scenario-expect-material "$EXPECT_MATERIAL" \
                --log-level="$LOG_LEVEL" > "${PEERLOG%.log}_2.log" 2>&1
        fi
    ) &
    PEER_PID=$!
    say "peer scheduled ($PEER_SCENARIO${PEER_SCENARIO_2:+ then $PEER_SCENARIO_2}, +${PEER_STAGGER}s, pid $PEER_PID)"
fi

# ---- 5. the run ------------------------------------------------------------
# B630: the caster runs the whole 4-leg chain. It targets the OBSERVER
# (Testbuddy), preps its own kit with GM commands, and the lane seeds the two
# characters co-located (b630_seed.sql) so the observer is a live peer.
if [ "$SCENARIO" = "cast_interrupt_chain" ]; then
    # Auto-seed unless the operator already passed --seed-sql. Co-location is
    # the whole point of the two-client lane.
    [ -n "$SEED_SQL" ] || SEED_SQL="$REPO/scripts/scenario_configs/b630_seed.sql"
    [ -f "$SEED_SQL" ] || { say "no B630 seed at $SEED_SQL"; exit 64; }
    say "B630 chain: caster targets ${PEER_CHAR:-<peer>}, seed=$SEED_SQL"
fi
say "running --scenario ${SCENARIO} --timeout ${TIMEOUT}"
MAIN_LOG_MODULE="$(log_module_for "$SCENARIO")"
# B630 fix pass (QA verdict on MR !20): the chain's A leg uses CHAIN_A_SPELL
# (378), the rig's declared id, not the operator's $SPELL (288, the
# cast_lands/standalone default). The D legs keep their client defaults
# (372 / 293 / 293, application.h) unless an operator overrides them, which
# is exactly what the analyzer's declared ids (B630Options in the unit test)
# pin. Feeding $SPELL=288 into the chain's A leg is how the "declared spell
# (ID 378)" cell could never pass.
MAIN_SPELL_FLAGS="--scenario-spell $SPELL"
case "$SCENARIO" in
    cast_interrupt_chain|cast_tail_repeat_*)
        # B760: the repeat lane's interrupted gem is the chain's A-leg spell.
        MAIN_SPELL_FLAGS="--scenario-spell $CHAIN_A_SPELL"
        ;;
esac
# UIGAP-live: an optional per-scenario SIDECAR (scripts/scenario_configs/
# <scenario>.sidecar.sh), sourced in a background subshell for the length of
# the client run so it can use eqemu_sql -- pickup_roundtrip's 1 Hz poll of
# the moving item's `inventory` row is the database half of its assertion.
# B1068 (#388): zone_line_walk walks with the KEYBOARD -- the renderer's own
# movement path -- so its client runs X11-windowed under a private Xvfb that
# the sidecar's xdotool drives. Every other scenario stays surfaceless.
XVFB_PID=""
if [ "$SCENARIO" = "zone_line_walk" ]; then
    command -v Xvfb >/dev/null 2>&1 && command -v xdotool >/dev/null 2>&1 \
        || { say "zone_line_walk needs Xvfb and xdotool on this host"; exit 64; }
    export DISPLAY=":${ZONE_LINE_WALK_DISPLAY:-68}"
    Xvfb "$DISPLAY" -screen 0 1024x768x24 -nolisten tcp > "$OUT/${SCENARIO}_xvfb.log" 2>&1 &
    XVFB_PID=$!
    sleep 2
    kill -0 "$XVFB_PID" 2>/dev/null || { say "Xvfb $DISPLAY did not start (see $OUT/${SCENARIO}_xvfb.log)"; exit 64; }
    say "Xvfb $DISPLAY up (pid $XVFB_PID); client runs --egl-x11"
fi
SIDECAR="$REPO/scripts/scenario_configs/${SCENARIO}.sidecar.sh"
SIDECAR_PID=""
if [ -f "$SIDECAR" ]; then
    ( source "$SIDECAR" ) > "$OUT/${SCENARIO}_sidecar.log" 2>&1 &
    SIDECAR_PID=$!
    say "sidecar started ($SIDECAR, pid $SIDECAR_PID)"
fi
# UIGAP-live: SCENARIO_CLIENT_ARGS (environment) is appended to the MAIN
# client's command line -- the review video passes --dump-frames-dir here.
if [ "$SCENARIO" = "zone_line_walk" ]; then
    # B1068 (#388): --scenario forces a headless Automated run (no renderer,
    # application_config.cpp), and the defect lives in the RENDERER's movement
    # path, so this lane runs the ordinary windowed client and reads the
    # verdict from its log with the scenario.sh exit taxonomy: 0 = fully
    # zoned in to qeynos2 (the client's "Zone lines for 'qeynos2'" line, written
    # on the first zone-line check there), 3 = never finished loading qeynos,
    # 2 = loaded but never zoned within --timeout. The post-check adds the
    # server half.
    # shellcheck disable=SC2086
    "$BIN" -c "$CONFIG" --egl-x11 -r 1024 768 --log-level="$LOG_LEVEL" \
        --log-module="ZONE:DEBUG" ${SCENARIO_CLIENT_ARGS:-} > "$LOG" 2>&1 &
    CLIENT_PID=$!
    CODE=2
    for _ in $(seq 1 "$TIMEOUT"); do
        if grep -q "Zone lines for 'qeynos2'" "$LOG" 2>/dev/null; then CODE=0; break; fi
        kill -0 "$CLIENT_PID" 2>/dev/null || break
        sleep 1
    done
    grep -q "zone load #1 ('qeynos') complete" "$LOG" 2>/dev/null || CODE=3
    echo "SCENARIO zone_line_walk exit=${CODE} ($(grep -m1 -o "Zone line crossing:.*" "$LOG" || echo 'no crossing logged'))" >> "$LOG"
    kill "$CLIENT_PID" >/dev/null 2>&1
    wait "$CLIENT_PID" 2>/dev/null
    CLIENT_PID=""
else
# shellcheck disable=SC2086
"$BIN" -c "$CONFIG" --scenario "$SCENARIO" --timeout "$TIMEOUT" \
    --scenario-hold "$HOLD" \
    $MAIN_SPELL_FLAGS \
    --scenario-cast-target "${PEER_CHAR:-}" \
    --log-level="$LOG_LEVEL" \
    ${CREATE_CHARACTER:+--create-character "$CREATE_CHARACTER"} \
    ${MAIN_LOG_MODULE:+--log-module="$MAIN_LOG_MODULE"} ${SCENARIO_CLIENT_ARGS:-} > "$LOG" 2>&1 &
CLIENT_PID=$!
wait "$CLIENT_PID"
CODE=$?
CLIENT_PID=""
fi
say "client exit ${CODE}"
grep -E '\[SCENARIO\]|^SCENARIO ' "$LOG" | tail -20 >&2
if [ -n "$SIDECAR_PID" ]; then
    kill "$SIDECAR_PID" >/dev/null 2>&1
    wait "$SIDECAR_PID" 2>/dev/null
fi

# UIGAP-live: an optional per-scenario POST-CHECK (scripts/scenario_configs/
# <scenario>.post.sh), sourced in a subshell with the world still UP (before
# the restore) and LOG / PEERLOG / OUT set. It asserts what the client cannot
# see -- the database rows, a server-delivered packet the client filtered. A
# client PASS that the post-check contradicts becomes a FAIL (1); any other
# client code is left as it is.
POST="$REPO/scripts/scenario_configs/${SCENARIO}.post.sh"
if [ -f "$POST" ]; then
    if ( source "$POST" ) > "$OUT/${SCENARIO}_post.log" 2>&1; then
        say "post-check PASS ($(tail -1 "$OUT/${SCENARIO}_post.log"))"
    else
        say "post-check FAIL -- see $OUT/${SCENARIO}_post.log: $(tail -1 "$OUT/${SCENARIO}_post.log")"
        [ "$CODE" -eq 0 ] && CODE=1
    fi
fi

# B630: turn the two logs into the results table + the B760 packet-order table.
# The exit code here is advisory (informational) -- the client's own code above
# is the pass/fail for the lane; the analyzer records the assertions.
if [ "$SCENARIO" = "cast_interrupt_chain" ]; then
    say "analyzing caster+observer logs"
    python3 "$REPO/scripts/scenario_configs/b630_analyze.py" \
        "$LOG" "$PEERLOG" --out "$OUT" || say "analyzer reported FAIL(s) -- see $OUT/b630_results.txt"
fi
# B760 (#45): the per-cast tail table (packets between keepcasting 0 and the
# settle, reliability, outcome, latency). Informational, like B630's.
case "$SCENARIO" in
    cast_tail_repeat_*)
        say "analyzing the cast tails"
        python3 "$REPO/scripts/scenario_configs/b760_tail_analyze.py" "$LOG" \
            > "$OUT/${SCENARIO}_tail.txt" || say "tail analyzer reported a wrong outcome -- see $OUT/${SCENARIO}_tail.txt"
        ;;
esac

# ---- 6. teardown (trap) ----------------------------------------------------
cleanup
if [ "$RESTORE_BAD" -eq 1 ]; then
    say "restore mismatch overrides the scenario result"
    exit 66
fi
exit "$CODE"
