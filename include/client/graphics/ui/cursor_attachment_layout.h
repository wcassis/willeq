/*
 * CursorAttachmentLayout -- where the item held on the cursor is drawn
 * (T436, board B429).
 *
 * THE PLACEMENT IS THE ORACLE'S, from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_CursorAttachment.xml:
 *
 *   <Screen item = "CursorAttachment">
 *       <ScreenID/>
 *       <RelativePosition>false</RelativePosition>
 *       <Location><X>0</X><Y>0</Y></Location>
 *       <Size><CX>50</CX><CY>50</CY></Size>
 *       <Style_Transparent>true</Style_Transparent>
 *       <Style_Titlebar>false</Style_Titlebar>
 *       <Style_Closebox>false</Style_Closebox>
 *       <Style_Border>false</Style_Border>
 *       <Style_Sizable>false</Style_Sizable>
 *       <Pieces>CA_Anim</Pieces>
 *       <Pieces>CA_Anim2</Pieces>
 *       <Pieces>CA_SpellGem</Pieces>
 *
 *   <StaticAnimation item = "CA_Anim">   <Location>(5,5)</Location>
 *                                        <Size>40x40</Size>
 *                                        <Animation>BuffIcons</Animation>
 *   <StaticAnimation item = "CA_Anim2">  identical: (5,5) 40x40
 *   <SpellGem item = "CA_SpellGem">      <Location>(5,5)</Location>
 *                                        <Size>40x32</Size>
 *
 * The window's <Location> is (0,0) and it is `RelativePosition false`, but
 * that is a PLACEHOLDER, not a placement: the official client moves this
 * screen to the mouse every frame -- that is the entire point of the
 * attachment. So the 50x50 box below follows the cursor and the (5,5) 40x40
 * child is where the icon goes; the numbers are the oracle's, the tracking
 * is the behaviour the file describes.
 *
 * ALL THREE PIECES SHARE THE SAME (5,5) ORIGIN, so at most one is ever
 * visible -- the official client shows CA_Anim for an item, CA_SpellGem for a
 * spell being scribed. fable's cursor holds an ITEM and nothing else
 * (`InventoryManager`'s `cursorQueue_` is a queue of `ItemInstance`), so
 * CA_Anim is the piece that is drawn. CA_Anim2 and CA_SpellGem have no fable
 * producer and are not drawn; board B440.
 *
 * NOTE the oracle's own decal size: 40x40, the same cell the item icon
 * sheets are cut at (item_icon_atlas.h), so the icon is a 1:1 blit -- the
 * same property T421 recorded for IDW_IconButton.
 *
 * THERE IS NO COUNT LABEL. T413 (board B296) already recorded this when it
 * dropped `CursorItemChangedData::queueSize`: this screen's whole body is
 * CA_Anim / CA_Anim2 / CA_SpellGem, three icon boxes and no text element
 * anywhere, so a stack's quantity and the cursor queue's depth are simply
 * never shown. fable draws no number here either.
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace CursorAttachmentLayout {

// <Size>, verbatim.
constexpr irr::s32 WINDOW_W = 50;
constexpr irr::s32 WINDOW_H = 50;

// CA_Anim's <Location> and <Size>, verbatim.
constexpr irr::s32 ICON_X = 5;
constexpr irr::s32 ICON_Y = 5;
constexpr irr::s32 ICON_W = 40;
constexpr irr::s32 ICON_H = 40;

/**
 * The attachment box, with its top-left at the mouse. Clamped so the icon
 * stays wholly on screen when the pointer is at the right or bottom edge --
 * the one number here that is not the oracle's, and it is a clamp rather
 * than a placement.
 */
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout, int mouseX,
                                            int mouseY) {
    irr::s32 x = mouseX;
    irr::s32 y = mouseY;
    if (x + WINDOW_W > layout.screenW) x = layout.screenW - WINDOW_W;
    if (y + WINDOW_H > layout.screenH) y = layout.screenH - WINDOW_H;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return irr::core::rect<irr::s32>(x, y, x + WINDOW_W, y + WINDOW_H);
}

/** CA_Anim, the 40x40 icon at (5,5) inside it. */
inline irr::core::rect<irr::s32> iconRect(const UILayout& layout, int mouseX,
                                          int mouseY) {
    const auto w = windowRect(layout, mouseX, mouseY);
    const irr::s32 x = w.UpperLeftCorner.X + ICON_X;
    const irr::s32 y = w.UpperLeftCorner.Y + ICON_Y;
    return irr::core::rect<irr::s32>(x, y, x + ICON_W, y + ICON_H);
}

/**
 * B442b (#151 row 29): the count strip under a held COIN. The XML has no
 * element for it (the client draws it), so it is MEASURED, not quoted: in
 * review/UIGAP-ORACLE/151/run1.G33a_coin_quantity_set_3.png the pointer is at
 * (646,341) and the strip is the opaque (64,64,64) box x 646..695, y 388..399
 * -- the box's full 50px width, 12px tall, its top 47px below the box's top
 * (2px under CA_Anim's bottom edge), the count centred in near-white.
 */
constexpr irr::s32 COIN_COUNT_Y = 47;
constexpr irr::s32 COIN_COUNT_H = 12;

inline irr::core::rect<irr::s32> coinCountRect(const UILayout& layout, int mouseX,
                                               int mouseY) {
    const auto w = windowRect(layout, mouseX, mouseY);
    const irr::s32 y = w.UpperLeftCorner.Y + COIN_COUNT_Y;
    return irr::core::rect<irr::s32>(w.UpperLeftCorner.X, y,
                                     w.LowerRightCorner.X, y + COIN_COUNT_H);
}

}  // namespace CursorAttachmentLayout
}  // namespace Graphics
}  // namespace EQT
