#!/usr/bin/env bash
# B1200 (#519) census: which P1999 items carry a WORN equipment effect, and
# what that effect resolves to. Run against the lane-5 akk-stack DB (peq).
#
# The worn effect lives in the items table column `worneffect` (a spell id).
# The sentinel for "no worn effect" is -1, NOT 0: every one of the 117,944
# P1999 items has minstatus=0, and 110,385 of them have worneffect=-1, so the
# only meaningful predicate is `worneffect >= 0`.
#
# The chain (confirmed client-side, see README sec. 2):
#   items.worneffect  ->  spells_new.id  ->  spells_new.spellanim  ->  spellsnew.eff record
# `spellanim` is the index into the client's spellsnew.eff table (0x10c-byte
# records, the same table the spell-FX project already loads).
#
# usage: scripts/census_worn.sh            (prints every section)
#        scripts/census_worn.sh --full     (also prints all 7,5xx rows)
set -u
cd "$(cd "$(dirname "$0")/../../.." && pwd)"
export EQEMU_LANE=5
source scripts/eqemu/env.sh
source scripts/eqemu/restore_lib.sh

run() { eqemu_sql "$1"; }

echo "## 1. scale"
run "SELECT COUNT(*) AS total_items FROM items;"
run "SELECT COUNT(*) AS with_worn_effect FROM items WHERE worneffect >= 0;"
run "SELECT COUNT(*) AS sentinel_minus1 FROM items WHERE worneffect = -1;"

echo
echo "## 2. the epic-1.0 question (the issue's premise)"
echo "No P1999 item has minstatus > 0, so the Kunark epic 1.0s are not flagged"
echo "by status in this DB; the issue's example item reads -1 (no worn effect):"
run "SELECT MAX(minstatus) AS max_minstatus, COUNT(*) AS n FROM items;"
run "SELECT id, Name, worneffect, worntype, minstatus, classes, artifactflag FROM items WHERE Name = 'Jagged Blade of War'\G"

echo
echo "## 3. what the worn items actually carry (top worneffect -> spell)"
run "SELECT i.worneffect, COUNT(*) AS n_items, s.name AS spell, s.spellanim, s.nimbuseffect
     FROM items i LEFT JOIN spells_new s ON s.id = i.worneffect
     WHERE i.worneffect >= 0
     GROUP BY i.worneffect ORDER BY n_items DESC LIMIT 25;"

echo
echo "## 4. how many worn spells carry a visible spellsnew record (spellanim<>0)"
run "SELECT COUNT(*) AS distinct_worn_spells,
            SUM(CASE WHEN s.spellanim <> 0 THEN 1 ELSE 0 END) AS with_spellanim,
            SUM(CASE WHEN s.nimbuseffect <> 0 THEN 1 ELSE 0 END) AS with_nimbus
     FROM (SELECT DISTINCT worneffect FROM items WHERE worneffect >= 0) w
     JOIN spells_new s ON s.id = w.worneffect;"

if [[ "${1:-}" == "--full" ]]; then
  echo
  echo "## 5. FULL list of items carrying a worn effect"
  run "SELECT i.id, i.Name, i.worneffect, s.name AS spell, s.spellanim,
              i.worntype, i.classes, i.artifactflag
       FROM items i LEFT JOIN spells_new s ON s.id = i.worneffect
       WHERE i.worneffect >= 0 ORDER BY i.worneffect, i.id;"
fi
