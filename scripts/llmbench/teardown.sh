#!/usr/bin/env bash
# T492: the teardown half of record.sh, standalone.
#
# It exists because a recorder that dies without running its trap -- killed,
# crashed, or (T492's own mistake) edited on disk while bash was still reading
# it -- leaves three clients up, the characters moved and the eqemu lock held.
# The teardown must be runnable from OUTSIDE the thing that failed.
#
#   teardown.sh <corpus-dir> [--keep-server] [--release-lock]
set -u
C="${1:?usage: teardown.sh <corpus-dir> [--keep-server] [--release-lock]}"; shift
KEEP=0; RELEASE=0
while [ $# -gt 0 ]; do case "$1" in --keep-server) KEEP=1; shift;; --release-lock) RELEASE=1; shift;; *) shift;; esac; done
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$REPO/scripts/eqemu/env.sh"
HERMES="${LLMB_HERMES:-user@172.16.200.100}"
say() { printf '[teardown] %s %s\n' "$(date '+%H:%M:%S')" "$*" >&2; }
sql() { eq_docker exec "${MARIADB_CONTAINER}" mysql -uroot -p"$(mariadb_root_password)" peq -B -N -e "$1"; }
CHARS="65,67,68,74"
B="$C/shared/db_before/snapshot.tsv"; A="$C/shared/db_after/snapshot.tsv"
IB="$C/shared/db_before/inventory.tsv"; IA="$C/shared/db_after/inventory.tsv"
mkdir -p "$C/shared/db_after"

say "killing clients"
pkill -f 'wine.*eqgame' >/dev/null 2>&1
pkill -f 'llmbench-T492/willeq' >/dev/null 2>&1
pkill -x willeq >/dev/null 2>&1
ssh -o BatchMode=yes "$HERMES" "pkill -f build-gles2/bin/willeq; true" >/dev/null 2>&1
pkill -f 'Xvfb :99' >/dev/null 2>&1
sleep 3

say "stopping the eqemu-server CONTAINER (mariadb stays up) -- see the note below"
# T492 MEASURED: `pkill` inside the container is NOT enough.  The image's
# entrypoint supervisor brings world and the static-zone fleet straight back
# (board B483 names the launcher), and with ~90 restore statements each
# costing an ssh round trip the zone that still holds the character is up
# again long before the loop ends -- the first T492 dry run's teardown
# killed the processes, restored 90 rows over two minutes and found
# Testbuddy's row written back underneath it.  T478's restore v2 had it
# right: STOP THE CONTAINER, leave mariadb alone up.  And send the restore
# as ONE statement batch, not ninety.
eq_docker stop "${EQEMU_CONTAINER}" >/dev/null 2>&1
sleep 3

restore_sql_from() {
    awk -F'\t' '
        NF==9 && $1!="" {printf "UPDATE character_data SET zone_id=%s,x=%s,y=%s,z=%s,heading=%s,level=%s,exp=%s WHERE id=%s;", $3,$4,$5,$6,$7,$8,$9,$1}
        NF==5 && $1!="" {printf "UPDATE character_currency SET platinum=%s,gold=%s,silver=%s,copper=%s WHERE id=%s;", $2,$3,$4,$5,$1}
    ' "$1"
}
inv_sql_from() {
    awk -F'\t' 'NF>=16 && $1!="" {
        cd = ($13=="NULL") ? "NULL" : "\x27" $13 "\x27"
        printf "INSERT INTO inventory (character_id,slot_id,item_id,charges,color,augment_one,augment_two,augment_three,augment_four,augment_five,augment_six,instnodrop,custom_data,ornamenticon,ornamentidfile,ornament_hero_model) VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s);", $1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,cd,$14,$15,$16
    }' "$1"
}

RC=0
if [ -s "$B" ]; then
    sql "$(restore_sql_from "$B")" >/dev/null
    sql "SELECT id,name,zone_id,x,y,z,heading,level,exp FROM character_data ORDER BY id;" > "$A"
    sql "SELECT id,platinum,gold,silver,copper FROM character_currency ORDER BY id;" >> "$A"
    if diff -q "$B" "$A" >/dev/null 2>&1; then say "snapshot restored BYTE-IDENTICALLY"
    else say "SNAPSHOT MISMATCH"; diff "$B" "$A" | head -20 >&2; RC=66; fi
else
    say "no snapshot at $B -- nothing to restore"
fi
if [ -s "$IB" ]; then
    sql "DELETE FROM inventory WHERE character_id IN (${CHARS});$(inv_sql_from "$IB")" >/dev/null
    sql "SELECT character_id,slot_id,item_id,charges,color,augment_one,augment_two,augment_three,augment_four,augment_five,augment_six,instnodrop,custom_data,ornamenticon,ornamentidfile,ornament_hero_model FROM inventory WHERE character_id IN (${CHARS}) ORDER BY character_id,slot_id;" > "$IA"
    if diff -q "$IB" "$IA" >/dev/null 2>&1; then say "inventory restored BYTE-IDENTICALLY"
    else say "INVENTORY MISMATCH"; RC=66; fi
fi
if [ "$KEEP" -eq 0 ]; then
    bash "$REPO/scripts/eqemu/down.sh" >/dev/null 2>&1; say "server down (whole compose project)"
else
    eq_docker start "${EQEMU_CONTAINER}" >/dev/null 2>&1; say "eqemu-server container restarted (--keep-server)"
fi
[ "$RELEASE" -eq 1 ] && { eqemu_lock_release; say "lock released"; }
exit $RC
