# T425 — the spell info window from the official format strings (B298 spell half); members with no writer (B375)

Class B ×2, slot3, no server. Read B298 (its spell/skill/entity halves —
`SpellData` carries `resist_diff`, `zone_type`, `teleport_zone`,
`spell_fades`, `player_tag`, `environment_type`, `time_of_day`,
`aoe_duration`, `push_up`, `allow_rest`, `is_dispellable`, … parsed and
never shown), B375, and T421's §Review — the KEY finding: the official
client ships its item/spell/skill display vocabulary as numbered format
strings in `/mnt/projects/EverQuestP1999/eqstr_us.txt`; T421 quoted the
item lines; the spell-info lines are their siblings (grep the file for
the spell description strings: mana, casting time, recast, duration,
target type, range, resist, "Cast on you:", "Cast on other:",
"Effect Fades:" etc. — quote each id). Read the spellbook/gem code
(`static_panels.cpp`, `spellbook*`), `EQUI_SpellBookWnd.xml` (the spell
info/description area) and `EQUI_SpellDisplay.xml` if present.

## Leg A — spell info

1. Table: each `SpellData` field the census names → the eqstr line that
   shows it (id quoted) → the window element (XML quoted). Fields with
   no eqstr line stay hidden (say which — e.g. `player_tag`, `push_up` may
   be mechanics only).
2. Draw the spell display window (the oracle opens it on a gem/spellbook
   click — fable's left click, as T421 did for items) with the composed
   lines in eqstr order, using T421's composer/text-entry/scroll
   machinery (no second composer). Tests pre-fix failing per line;
   `ui_panel_capture` gains the window; capture into `review/T425/`.
3. Strike the spell half of B298; annotate the skill/entity halves with
   the eqstr ids you find for them.

## Leg B — B375

Nine `ItemInstance` members with no writer (`expendable`, the seven
`heroic*`, `bardEffect`): read the wire struct (`Item_Struct` in eqemu's
`titanium_structs.h`) — if the field is on the wire, parse it (writer
added, then T421's display shows the ones eqstr has a line for); if not
on Titanium's wire, delete the member (compiler proof) and the exemption
row. Strike B375.

## Proof

Build (background), `ctest -j6` (no expected failures unless T424 (slot2)
replaces the pack: `PackReproducible`/`eqe1MappedBytes` only), pre-fix/
post-fix, G05 in place if a pass is added, visual gate standalone + ARM
(no image golden should move), file sizes.

STOP: none expected.
