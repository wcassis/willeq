/*
 * T425 (board B298, spell half) -- the spell display window's TEXT, and the
 * only place it is composed.
 *
 * WHAT WAS BROKEN. `SpellData` carries the whole spells_us.txt row --
 * `SpellDatabase::queryAndCache()` fills mana cost, cast time, recast time,
 * duration formula, casting skill, range and the sixteen per-class levels --
 * and fable drew NONE of it anywhere. A spell gem shows an icon and a name;
 * the spellbook page shows a name. There was no way for a player to learn
 * what a spell costs or how long it lasts. Board B298's spell half is that
 * census.
 *
 * THE ORACLE, AND THE THING THAT SURPRISED THIS UNIT: there is NO
 * `EQUI_SpellDisplay.xml`. `grep -l 'Screen item=' uifiles/default/*.xml`
 * lists seventy-nine windows and not one of them is a spell display; the
 * whole `uifiles/default` tree contains no `SpellDisplay` and no `SpellInfo`
 * at all. The official Titanium client shows a spell in the SAME window it
 * shows an item in -- `EQUI_ItemDisplay.xml`'s `ItemDisplayWindow`, whose
 * body is one free-text `<STMLbox item="IDW_ItemDescription">`. Its own icon
 * button says so out loud:
 *
 *     <Button item="IDW_IconButton">
 *         <ButtonDrawTemplate>
 *             <Normal>RedIconBackground</Normal>
 *             <NormalDecal>BuffIcons</NormalDecal>
 *
 * -- `BuffIcons` is the SPELL icon sheet, not an item-icon sheet. So this
 * unit adds NO second window, NO second panel, NO second event and NO second
 * scroll target: it composes lines for the window T421 already built. What is
 * new is only this function, because the composer's INPUT type is a
 * `SpellData` and not an `ItemInstance`.
 *
 * THE LINE FORMATS ARE QUOTED, as T421's are, from the official client's own
 * string table `/mnt/projects/EverQuestP1999/eqstr_us.txt` (also shipped in
 * `data/willeq.db`'s `eqstrings` table, 5,901 rows). Every format below
 * carries its id. Nothing here is invented text.
 *
 * WHAT IS *NOT* THE ORACLE'S, flagged rather than dressed up (the T419
 * ORACLE_ROW_H rule):
 *   * the ORDER of the lines -- the string table is a flat id->format map,
 *     so the order used is the table's own ASCENDING ID order, exactly as
 *     BuildItemDisplayText() does;
 *   * the UNIT substituted into 3102's `%1`. The format is `Spell Duration:
 *     %1.` and the table says nothing about what fills it; fable substitutes
 *     the tick count `SpellData::calculateDuration()` returns.
 *
 * FIELDS THE ORACLE DOES NOT SHOW, and which therefore stay hidden. Each was
 * measured by grepping eqstr_us.txt, not assumed:
 *   * `resist_type` and `resist_diff` -- the table has NO spell-resist line.
 *     Every "Resist" hit is a combat message, `Sense NPC Resists` (an AA
 *     name) or 9044 `Stun Resist: %1%` (an item stat).
 *   * `zone_type`, `environment_type`, `time_of_day` -- no Indoor/Outdoor,
 *     no environment and no time-of-day format string exists.
 *   * `teleport_zone` -- no Destination/Location line exists.
 *   * `player_tag` -- a spells_us.txt internal tag ("PLAYER_1"); it names no
 *     player-visible thing and has no format string.
 *   * `target_type`, `aoe_range`, `aoe_duration`, `push_back`, `push_up`,
 *     `is_beneficial`, `allow_rest`, `is_dispellable`, `no_partial_resist` --
 *     no Target Type / Area of Effect / knockback / dispel line exists.
 *   * `endurance_cost` -- 5809 `Endurance: %1` is the ITEM stat line; there
 *     is no spell endurance-cost format.
 *   * the reagents. 6732 `Reagents Needed: %1` DOES exist, and it wants an
 *     item NAME. `SpellData` carries `reagent_id` only, and the client has
 *     no item-name-by-id source at all: `data/willeq.db` has no items table
 *     and the wire never sends one. Printing ids would be inventing a line
 *     the oracle never draws. Boarded rather than faked.
 *   * `spell_fades` -- NOT a window field. It is the sibling of
 *     `cast_on_you`/`cast_on_other`, which `spell_packet_handlers.cpp`
 *     already prints to chat on a landing; `spell_fades` is the message the
 *     client prints when the buff WEARS OFF. T425 wires it there, at
 *     `BuffManager`'s existing fade callback, not here.
 *   * `cast_anim`, `target_anim`, `travel_type`, `spell_affect_index`,
 *     `spell_group`, `spell_rank`, `effects[]`, `recovery_time_ms`,
 *     `duration_cap`, `duration_formula` -- mechanics and visuals, never
 *     printed.
 *   * eqstr 3101 `Spell:` is deliberately UNUSED. It is the ITEM window's
 *     label for an item's spell effect; the spell display's own name goes
 *     where the item's does, on the `Style_Titlebar` row.
 *
 * This function is FREE and takes only a `SpellData` plus the two player
 * facts eqstr 3004/3005 need, so a test drives the real fields through the
 * real composer with no bridge, no renderer, no database and no device --
 * the T290/T393/T421 idiom.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace EQ {
struct SpellData;
}

namespace eqt {
namespace spell {

/**
 * The spell display window's body text, one entry per drawn line. A
 * zero-valued field is OMITTED rather than printed as "0", exactly as
 * BuildItemDisplayText() omits an item's.
 *
 * `playerClassId` is `PlayerState::classId()` (1..16; 0 = unknown, which
 * omits the Level Needed line) and `playerLevel` is `PlayerState::level()`,
 * which the duration formula scales with.
 */
std::vector<std::string> BuildSpellDisplayText(const EQ::SpellData& spell,
                                               uint32_t playerClassId,
                                               uint8_t playerLevel);

} // namespace spell
} // namespace eqt
