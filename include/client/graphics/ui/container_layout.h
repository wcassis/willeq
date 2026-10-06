/*
 * ContainerLayout -- the container window's geometry, and the only place it
 * is computed (B899, GitLab #215 / #151 Leg B).
 *
 * THE PLACEMENT IS THE ORACLE'S, from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_Container.xml:
 *
 *   <Screen item="ContainerWindow">
 *     <RelativePosition>false</RelativePosition>
 *     <Location><X>350</X><Y>100</Y></Location>
 *     <Size><CX>100</CX><CY>366</CY></Size>
 *     <Text>Container</Text>
 *     <DrawTemplate>WDT_Rounded</DrawTemplate>
 *     <Style_Titlebar>true</Style_Titlebar>
 *     <Style_Closebox>true</Style_Closebox>
 *     <Style_Sizable>false</Style_Sizable>
 *
 * UNSCALED, for the oracle's own reason: `Style_Sizable false`, the condition
 * under which T415 drew EQUI_CombatAbilityWnd.xml and T436 EQUI_BankWnd.xml
 * at their own offsets. 366px fits inside 640x480.
 *
 * Every child is <RelativePosition>true</RelativePosition> with a plain
 * <Location>/<Size>, so the numbers below are TRANSCRIBED, not derived:
 *
 *   <Label  item="Container_Label">   (2,4)     88x32  AlignCenter true
 *   <Button item="Container_Icon">    (26,38)   40x40  A_RecessedBox +
 *                                                      A_DragItem decal,
 *                                                      DecalOffset (0,0),
 *                                                      DecalSize 40x40
 *   <InvSlot item="ContainerSlot1">   (6,84)    40x40  EQType 30
 *   <InvSlot item="ContainerSlot2">   (46,84)   40x40  EQType 31
 *   <InvSlot item="ContainerSlot3">   (6,124)   40x40  EQType 32
 *   <InvSlot item="ContainerSlot4">   (46,124)  40x40  EQType 33
 *   <InvSlot item="ContainerSlot5">   (6,164)   40x40  EQType 34
 *   <InvSlot item="ContainerSlot6">   (46,164)  40x40  EQType 35
 *   <InvSlot item="ContainerSlot7">   (6,204)   40x40  EQType 36
 *   <InvSlot item="ContainerSlot8">   (46,204)  40x40  EQType 37
 *   <InvSlot item="ContainerSlot9">   (6,244)   40x40  EQType 38
 *   <InvSlot item="ContainerSlot10">  (46,244)  40x40  EQType 39
 *     each <Background>A_RecessedBox</Background> and NO <ItemOffsetX/Y>
 *     (BW_BankSlot has 2,2; these do not), so the item art fills the cell.
 *     THE GRID IS ROW-MAJOR, two wide: 1 and 2 share a row. The bank's
 *     column-major grid is the opposite, and neither is a typo.
 *     EQType 30..39 is a slot index RELATIVE to "the container this window
 *     shows", not a wire slot: the wire slot of cell i is the bag's own
 *     sub-slot (inventory_constants.h calcBagSlotId) or, for a world
 *     container, WORLD_BEGIN + i. containerSlotId() in container_panel.h
 *     is that mapping.
 *   <Button item="Container_Combine"> (16,288)  60x20  Text "Combine",
 *                                                      A_SmallBtn* template
 *   <Button item="Container_DoneButton"> (21,316) 50x24 Text "Done"
 *
 * OUT OF ERA, NOT DRAWN, NO RECT HERE: Augment_Label, AugmentInfoLabel,
 * AugmentList, InsertButton, RemoveButton, DeleteButton. They are in the
 * Screen's <Pieces> list and they are augments, which are LDoN+ (#215, #151:
 * "augment controls in EQUI_Container.xml are post-Velious"). Their
 * <Location>s (x >= 95) lie OUTSIDE the 100px window anyway.
 *
 * THE ONE NUMBER THAT IS NOT THE ORACLE'S: TITLE_H, fable's own 18 -- the
 * identical reasoning BankLayout gives: the title bar's height comes from the
 * window font and the WDT_Rounded art, neither of which is a number in a
 * shipped file. Every child rect is measured from the bottom of that bar.
 * 18 + 316 + 24 = 358 < 366, so the Done button still fits inside the
 * oracle's own window height.
 *
 * SEVERAL WINDOWS, ONE PLACE. The official client opens one ContainerWindow
 * per open bag, all from this one Screen, and a second bag opens at the SAME
 * screen position, on top of the first (MEASURED: UIGAP-ORACLE 151
 * gestures.tsv row 20). So every window sits at the oracle's <Location>,
 * clamped on screen, and the most recently opened one is drawn on top.
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace ContainerLayout {

// <Screen> <Size>, verbatim.
constexpr irr::s32 WINDOW_W = 100;
constexpr irr::s32 WINDOW_H = 366;

// <Screen> <Location>, verbatim (RelativePosition false: screen pixels).
constexpr irr::s32 ORACLE_X = 350;
constexpr irr::s32 ORACLE_Y = 100;

// NOT the oracle's -- see the header comment.
constexpr irr::s32 TITLE_H = 18;

// Container_Label.
constexpr irr::s32 LABEL_X = 2;
constexpr irr::s32 LABEL_Y = 4;
constexpr irr::s32 LABEL_W = 88;
constexpr irr::s32 LABEL_H = 32;

// Container_Icon (DecalOffset 0,0 / DecalSize 40x40: the decal IS the button).
constexpr irr::s32 ICON_X = 26;
constexpr irr::s32 ICON_Y = 38;
constexpr irr::s32 ICON_SIZE = 40;

// ContainerSlot1..10, transcribed above.
constexpr int SLOT_COUNT = 10;
constexpr irr::s32 SLOT_SIZE = 40;
constexpr irr::s32 SLOT_X[SLOT_COUNT] = {6, 46, 6, 46, 6, 46, 6, 46, 6, 46};
constexpr irr::s32 SLOT_Y[SLOT_COUNT] = {84, 84, 124, 124, 164, 164,
                                         204, 204, 244, 244};
// <EQType> of ContainerSlot1. Slot i carries EQTYPE_BEGIN + i.
constexpr int EQTYPE_BEGIN = 30;

// Container_Combine.
constexpr irr::s32 COMBINE_X = 16;
constexpr irr::s32 COMBINE_Y = 288;
constexpr irr::s32 COMBINE_W = 60;
constexpr irr::s32 COMBINE_H = 20;

// Container_DoneButton.
constexpr irr::s32 DONE_X = 21;
constexpr irr::s32 DONE_Y = 316;
constexpr irr::s32 DONE_W = 50;
constexpr irr::s32 DONE_H = 24;

/**
 * The window's top-left corner: the oracle's own <Location>, clamped so the
 * whole 100x366 window is on screen. Every container window shares it.
 */
inline irr::core::vector2di windowOrigin(const UILayout& layout) {
    irr::s32 x = ORACLE_X;
    irr::s32 y = ORACLE_Y;
    if (x + WINDOW_W > layout.screenW) x = layout.screenW - WINDOW_W;
    if (y + WINDOW_H > layout.screenH) y = layout.screenH - WINDOW_H;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return irr::core::vector2di(x, y);
}

/** The whole window, in screen coordinates. */
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    const auto o = windowOrigin(layout);
    return irr::core::rect<irr::s32>(o.X, o.Y, o.X + WINDOW_W, o.Y + WINDOW_H);
}

/** Style_Titlebar: the row holding the window's title. */
inline irr::core::rect<irr::s32> titleRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return irr::core::rect<irr::s32>(w.UpperLeftCorner.X, w.UpperLeftCorner.Y,
                                     w.LowerRightCorner.X,
                                     w.UpperLeftCorner.Y + TITLE_H);
}

/** Style_Closebox: the square at the right-hand end of the title bar. */
inline irr::core::rect<irr::s32> closeBoxRect(const UILayout& layout) {
    const auto t = titleRect(layout);
    return irr::core::rect<irr::s32>(t.LowerRightCorner.X - TITLE_H,
                                     t.UpperLeftCorner.Y, t.LowerRightCorner.X,
                                     t.LowerRightCorner.Y);
}

/**
 * THE one placement body: a child's <Location>/<Size> offset from the client
 * area's origin (the bottom of the title bar), so the draw and every hit test
 * read the same rect (T57's rule).
 */
inline irr::core::rect<irr::s32> at(const UILayout& layout, irr::s32 x,
                                    irr::s32 y, irr::s32 w, irr::s32 h) {
    const auto o = windowOrigin(layout);
    const irr::s32 sx = o.X + x;
    const irr::s32 sy = o.Y + TITLE_H + y;
    return irr::core::rect<irr::s32>(sx, sy, sx + w, sy + h);
}

/** Container_Label. */
inline irr::core::rect<irr::s32> labelRect(const UILayout& layout) {
    return at(layout, LABEL_X, LABEL_Y, LABEL_W, LABEL_H);
}

/** Container_Icon. */
inline irr::core::rect<irr::s32> iconRect(const UILayout& layout) {
    return at(layout, ICON_X, ICON_Y, ICON_SIZE, ICON_SIZE);
}

/** ContainerSlot<i+1>, i in 0..9. Degenerate rect out of range. */
inline irr::core::rect<irr::s32> slotRect(const UILayout& layout, int i) {
    if (i < 0 || i >= SLOT_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, SLOT_X[i], SLOT_Y[i], SLOT_SIZE, SLOT_SIZE);
}

/** Container_Combine. */
inline irr::core::rect<irr::s32> combineButtonRect(const UILayout& layout) {
    return at(layout, COMBINE_X, COMBINE_Y, COMBINE_W, COMBINE_H);
}

/** Container_DoneButton. */
inline irr::core::rect<irr::s32> doneButtonRect(const UILayout& layout) {
    return at(layout, DONE_X, DONE_Y, DONE_W, DONE_H);
}

}  // namespace ContainerLayout
}  // namespace Graphics
}  // namespace EQT
