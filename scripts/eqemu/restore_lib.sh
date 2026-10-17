#!/usr/bin/env bash
# T493 (board B784): the ONE snapshot/restore implementation.
#
# Sourced, never executed. Requires scripts/eqemu/env.sh to have been sourced
# first (it uses eq_docker, MARIADB_CONTAINER, EQEMU_CONTAINER,
# mariadb_root_password).
#
# Why this file exists: `scripts/scenario.sh`, `scripts/llmbench/teardown.sh`
# and `scripts/llmbench/record.sh` each carried their own copy of "snapshot the
# character rows, put them back, diff". Two of the three copies were fixed by
# T492 and the third -- scenario.sh's, the one EVERY live unit on the board
# runs through -- was not, so every scenario run kept the defect:
#
#   * it stopped world/zone/loginserver with `pkill` INSIDE the eqemu-server
#     container, and that container's entrypoint supervisor brings the fleet
#     straight back (board B483 names the launcher); and
#   * it sent one `UPDATE` per row over a separate `docker exec` (an ssh round
#     trip in remote mode) -- ~90 rows, MEASURED at 2 min -- so the world was
#     up again long before the loop ended and any character that was IN WORLD
#     got its row written back over the restore.
#
# The correct recipe, measured byte-identical: STOP THE CONTAINER (mariadb
# stays up), send the whole restore as ONE statement batch, snapshot again,
# diff. `scripts/eqemu/restore_selftest.sh` is its regression net and
# `scripts/scenario.sh --selftest` is how a user reaches it.

# ---- SQL ------------------------------------------------------------------
eqemu_sql() {
    # eqemu_sql <sql> -- batch mode (-B -N: tab separated, no column names),
    # which is what makes a snapshot a diffable TSV.
    eq_docker exec "${MARIADB_CONTAINER}" \
        mysql -uroot -p"$(mariadb_root_password)" peq -B -N -e "$1"
}

# ---- the roster zone seed (B1047 #367, B1025-H5 #345) ----------------------
# eqemu_roster_zone_seed -- move any test-roster character still STORED in
# timorous (never finishes loading on the lanes) to befallen's safe point.
# Callers run it BEFORE their snapshot, so the per-run restore keeps it: it is
# a persistent lane fix, idempotent, and a no-op once applied. The SQL and its
# reasoning: scripts/eqemu/roster_zone_seed.sql. Prints how many rows moved.
eqemu_roster_zone_seed() {
    local sql_file
    sql_file="$(dirname "${BASH_SOURCE[0]}")/roster_zone_seed.sql"
    eqemu_sql "$(cat "$sql_file") SELECT ROW_COUNT();"
}

# ---- the snapshot ---------------------------------------------------------
eqemu_snapshot() {
    # eqemu_snapshot <file> -- the rows a scenario can plausibly move, ordered
    # so a diff is meaningful. This IS the contract: what comes back has to be
    # byte-identical to what went out.
    eqemu_sql "SELECT id,name,zone_id,x,y,z,heading,level,exp FROM character_data ORDER BY id;" > "$1"
    eqemu_sql "SELECT id,platinum,gold,silver,copper FROM character_currency ORDER BY id;" >> "$1"
}

eqemu_inventory_snapshot() {
    # eqemu_inventory_snapshot <file> <character-id-list> -- a full row dump, so
    # the restore is a wholesale replace rather than a guess at which rows moved
    # (/equip, #summonitem and a death all move inventory rows).
    eqemu_sql "SELECT character_id,slot_id,item_id,charges,color,augment_one,augment_two,augment_three,augment_four,augment_five,augment_six,instnodrop,custom_data,ornament_icon,ornament_idfile,ornament_hero_model,guid FROM inventory WHERE character_id IN ($2) ORDER BY character_id,slot_id;" > "$1"
}

# ---- the batch builders ---------------------------------------------------
eqemu_restore_sql_from() {
    # eqemu_restore_sql_from <snapshot.tsv> -- the whole character restore as
    # ONE statement batch on stdout. The two tables are told apart by field
    # count, which is why eqemu_snapshot's two SELECTs have 9 and 5 columns.
    awk -F'\t' '
        NF==9 && $1!="" {printf "UPDATE character_data SET zone_id=%s,x=%s,y=%s,z=%s,heading=%s,level=%s,exp=%s WHERE id=%s;", $3,$4,$5,$6,$7,$8,$9,$1}
        NF==5 && $1!="" {printf "UPDATE character_currency SET platinum=%s,gold=%s,silver=%s,copper=%s WHERE id=%s;", $2,$3,$4,$5,$1}
    ' "$1"
}

eqemu_inventory_sql_from() {
    # eqemu_inventory_sql_from <inventory.tsv> -- the INSERTs for a wholesale
    # replace; the caller pairs it with a DELETE in the same batch.
    awk -F'\t' 'NF>=17 && $1!="" {
        cd = ($13=="NULL") ? "NULL" : "\x27" $13 "\x27"
        printf "INSERT INTO inventory (character_id,slot_id,item_id,charges,color,augment_one,augment_two,augment_three,augment_four,augment_five,augment_six,instnodrop,custom_data,ornament_icon,ornament_idfile,ornament_hero_model,guid) VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s);", $1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,cd,$14,$15,$16,$17
    }' "$1"
}

eqemu_inventory_restore() {
    # eqemu_inventory_restore <inventory.tsv> <character-id-list>
    [ -s "$1" ] || return 0
    eqemu_sql "DELETE FROM inventory WHERE character_id IN ($2);$(eqemu_inventory_sql_from "$1")" >/dev/null
}

# ---- the world, down ------------------------------------------------------
EQEMU_WORLD_STOPPED=0
eqemu_world_stop() {
    # Idempotent: a caller that needs the world down for its own extra work
    # (record.sh's spawn/corpse cleanup) calls this first, and
    # eqemu_restore_snapshot's own call is then a no-op.
    #
    # `pkill` inside the container is NOT enough -- the entrypoint supervisor
    # respawns the fleet. Stop the CONTAINER; mariadb is a separate container
    # and stays up, which is what makes the restore possible at all.
    [ "${EQEMU_WORLD_STOPPED}" -eq 1 ] && return 0
    eq_docker stop "${EQEMU_CONTAINER}" >/dev/null 2>&1
    EQEMU_WORLD_STOPPED=1
    sleep 3
}

eqemu_world_start() {
    eq_docker start "${EQEMU_CONTAINER}" >/dev/null 2>&1
    EQEMU_WORLD_STOPPED=0
}

# ---- the restore ----------------------------------------------------------
eqemu_restore_snapshot() {
    # eqemu_restore_snapshot <before.tsv> <after.tsv> [say-fn]
    #   0  restored byte-identically (or nothing to restore)
    #  66  MISMATCH -- the caller's operator exit code for "did not restore"
    local before="$1" after="$2" say_fn="${3:-:}"
    [ -s "$before" ] || { "$say_fn" "no snapshot at $before -- nothing to restore"; return 0; }
    "$say_fn" "restoring with the WORLD DOWN (the world writes character rows back on shutdown)"
    eqemu_world_stop
    eqemu_sql "$(eqemu_restore_sql_from "$before")" >/dev/null
    eqemu_snapshot "$after"
    if diff -q "$before" "$after" >/dev/null 2>&1; then
        "$say_fn" "snapshot restored BYTE-IDENTICALLY"
        return 0
    fi
    "$say_fn" "SNAPSHOT MISMATCH -- $before vs $after"
    diff "$before" "$after" | head -20 >&2
    return 66
}
