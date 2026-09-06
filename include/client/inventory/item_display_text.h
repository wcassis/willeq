/*
 * T421 (board B298, item half) -- the item display window's TEXT, and the
 * only place it is composed.
 *
 * WHAT WAS BROKEN. `ItemInstance` carries the whole Titanium item row --
 * `TitaniumItemParser` fills a hundred-odd fields off the wire -- and fable
 * drew NONE of them anywhere. There was no item display window at all; the
 * inventory popup shows three characters of an item's name in its slot and
 * nothing else. Every combat stat, every resist, every regen, the weapon's
 * delay, the recommended level, the charge count and the lore line were
 * parsed and dropped. Board B298's item half is that census.
 *
 * THE ORACLE. `/mnt/projects/EverQuestP1999/uifiles/default/EQUI_ItemDisplay.xml`
 * is a 400x190 `<Screen item="ItemDisplayWindow">` holding exactly two
 * children: `IDW_IconButton` (a 40x40 decal) and `IDW_ItemDescription`, an
 *
 *     <STMLbox item="IDW_ItemDescription">
 *         <DrawTemplate>WDT_Inner</DrawTemplate>
 *         <Style_VScroll>true</Style_VScroll>
 *         <AutoStretch>true</AutoStretch>
 *
 * -- a scrolling free-text box. So the XML gives the window's SHAPE and says
 * nothing at all about which stats go in it: the official client composes the
 * STML in its own code.
 *
 * THE LINE FORMATS ARE STILL QUOTABLE, and they are quoted here. They are
 * shipped in the client's own string table,
 * `/mnt/projects/EverQuestP1999/eqstr_us.txt`, and every format below carries
 * its string id in a comment. Nothing here is invented text.
 *
 * WHAT IS *NOT* THE ORACLE'S, flagged rather than dressed up as a quotation
 * (the T419 ORACLE_ROW_H rule): the ORDER of the lines and which of them
 * share a row. The string table is a flat id->format map with no ordering
 * information, and the composing code is in the executable. The order used
 * here is the string table's OWN id order within each group, which is the
 * only ordering evidence the shipped data carries.
 *
 * FIELDS THE ORACLE DOES NOT SHOW, and which therefore stay hidden:
 *   * `currentSlot` -- where the item is RIGHT NOW. `Slot:` (id 3000) is the
 *     item's EQUIPPABLE slot list, a different thing; the item display window
 *     has no element and no string for the item's present location.
 *   * the seven `heroic*` and `bardEffect` -- DELETED from `ItemInstance` by
 *     T425 Leg B (board B375). eqstr_us.txt has no heroic-stat format string
 *     at all (grep for "Heroic" returns nothing -- heroic stats postdate this
 *     client) and no instrument-modifier item-display string (5750 "All
 *     Instrument Types" is a spell-requirement line, not an item stat); and
 *     neither is on Titanium's wire, so there was nothing to parse them from
 *     either. They were dead struct members, not a missing display.
 *   * `loreGroup`, `flags`, `material`, `color`, `bookFilename`, the augment
 *     slot `type` bitmasks -- structural, never printed.
 *
 * This function is FREE and takes only an `ItemInstance`, so a test drives
 * the real fields through the real composer with no bridge, no renderer and
 * no device -- the T290/T393 static-panel idiom, one layer lower.
 */

#pragma once

#include <string>
#include <vector>

namespace eqt {
namespace inventory {

struct ItemInstance;

/**
 * The item display window's body text, one entry per drawn line. An item
 * with no stats yields the flag line (or nothing at all): a zero-valued
 * field is OMITTED, exactly as the official window omits it, rather than
 * printed as "0".
 */
std::vector<std::string> BuildItemDisplayText(const ItemInstance& item);

} // namespace inventory
} // namespace eqt
