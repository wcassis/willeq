/*
 * B899 (GitLab #215 / #151 Leg B) -- the era container window.
 *
 * Its own TU (M00). The geometry is in
 * include/client/graphics/ui/container_layout.h, which quotes
 * EQUI_Container.xml; this file computes no rect.
 */

#include "client/graphics/ui/container_panel.h"

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/state/event_bus.h"

#include <algorithm>
#include <string>

namespace EQT {
namespace Graphics {

namespace inv = eqt::inventory;

namespace {

const irr::video::SColor kText(255, 225, 225, 225);
const irr::video::SColor kTitle(255, 255, 215, 0);
const irr::video::SColor kFrame(255, 80, 80, 80);
const irr::video::SColor kQuantity(255, 180, 220, 180);

bool inside(const irr::core::rect<irr::s32>& r, int x, int y) {
    return r.isPointInside(irr::core::vector2di(x, y));
}

}  // namespace

// --- slot arithmetic -------------------------------------------------------

int containerWindowIndex(bool isWorldObject, int16_t inventorySlot) {
    if (isWorldObject) return ContainerWindowsState::WORLD_WINDOW;
    if (!inv::isGeneralSlot(inventorySlot)) return -1;
    return 1 + (inventorySlot - inv::GENERAL_BEGIN);
}

int containerVisibleSlots(const ContainerWindow& w) {
    int n = static_cast<int>(w.slotCount);
    if (n < 0) n = 0;
    if (n > ContainerLayout::SLOT_COUNT) n = ContainerLayout::SLOT_COUNT;
    return n;
}

bool containerHasCombine(const ContainerWindow& w) {
    if (w.isWorldObject) return true;
    // MEASURED (UIGAP-ORACLE 215 combine.tsv rows 1, 9-13, 16, 20; 151
    // gestures.tsv row 19): Combine is on every tradeskill container the
    // official client opened -- Medicine Bag and Toolbox included, even for a
    // class that cannot use them -- and on no plain storage bag (Large Bag,
    // Backpack). The plain storage types are eqemu's BagType 0..8 (Small Bag
    // .. Bandolier; eqstr 3400-3407). Not isTradeskillContainerType(), which
    // leaves the Medicine Bag out.
    return w.containerType > inv::CONTAINER_BANDOLIER;
}

const char* containerCombineLabel(const ContainerWindow& /*w*/) {
    // EQUI_Container.xml: <Button item="Container_Combine"> <Text>Combine</Text>,
    // and MEASURED "Combine" on every container (UIGAP-ORACLE 215 combine.tsv
    // rows 1, 8-13, 15-16): there is no Brew / Bake / Mix label.
    return "Combine";
}

int16_t containerSlotId(const ContainerWindow& w, int i) {
    if (i < 0 || i >= containerVisibleSlots(w)) return inv::SLOT_INVALID;
    if (w.isWorldObject) return static_cast<int16_t>(inv::WORLD_BEGIN + i);
    return inv::calcBagSlotId(w.inventorySlot, static_cast<int16_t>(i));
}

int16_t containerCombineSlot(const ContainerWindow& w) {
    return w.isWorldObject ? inv::SLOT_TRADESKILL_EXPERIMENT_COMBINE
                           : w.inventorySlot;
}

// --- bridge arms -----------------------------------------------------------

void applyContainerOpened(ContainerWindowsState& state,
                          const eqt::state::TradeskillContainerOpenedEvent& data) {
    const int idx = containerWindowIndex(data.isWorldObject, data.inventorySlot);
    if (idx < 0) return;
    auto& w = state.windows[idx];
    const bool wasOpen = w.isOpen;
    w.isOpen = true;
    w.isWorldObject = data.isWorldObject;
    w.objectId = data.isWorldObject ? data.objectId : 0;
    w.inventorySlot = data.isWorldObject ? inv::SLOT_INVALID : data.inventorySlot;
    w.name = data.containerName;
    w.iconId = data.iconId;
    w.containerType = data.containerType;
    w.slotCount = data.slotCount;
    w.hoveredSlot = -1;
    w.combineHovered = w.doneHovered = w.closeHovered = false;
    if (!wasOpen) {
        w.openSequence = state.nextSequence++;
    }
}

void applyContainerClosed(ContainerWindowsState& state,
                          const eqt::state::TradeskillContainerClosedEvent& data) {
    const int idx = containerWindowIndex(data.wasWorldObject, data.inventorySlot);
    if (idx < 0) return;
    auto& w = state.windows[idx];
    w.isOpen = false;
    w.hoveredSlot = -1;
    w.combineHovered = w.doneHovered = w.closeHovered = false;
    if (data.wasWorldObject) {
        // A world container's contents are the server's and go with it.
        for (auto& s : w.slots) s = ContainerSlotDisplay{};
        w.objectId = 0;
    }
}

bool applyContainerSlotChanged(ContainerWindowsState& state,
                               const eqt::state::InventorySlotChangedData& data) {
    int windowIdx = -1;
    int cell = -1;
    if (inv::isWorldContainerItemSlot(data.slotId)) {
        windowIdx = ContainerWindowsState::WORLD_WINDOW;
        cell = data.slotId - inv::WORLD_BEGIN;
    } else if (data.slotId >= inv::GENERAL_BAGS_BEGIN &&
               data.slotId <= inv::GENERAL_BAGS_END) {
        windowIdx = containerWindowIndex(false, inv::getParentGeneralSlot(data.slotId));
        cell = inv::getBagIndex(data.slotId);
    }
    if (windowIdx < 0 || cell < 0 || cell >= ContainerLayout::SLOT_COUNT) return false;
    auto& s = state.windows[windowIdx].slots[cell];
    s.hasItem = data.hasItem;
    s.itemName = data.hasItem ? data.itemName : std::string();
    s.iconId = data.hasItem ? data.iconId : 0;
    s.quantity = data.hasItem ? static_cast<int32_t>(data.quantity) : 0;
    return true;
}

int topmostContainerWindow(const ContainerWindowsState& state) {
    // The most recently opened window: every window shares the oracle's one
    // <Location>, so the newest covers the rest (UIGAP-ORACLE 151
    // gestures.tsv row 20). ESC and the hit test use this; the draw draws it
    // LAST (containerDrawOrder(), same tie-break).
    int best = -1;
    uint32_t bestSeq = 0;
    for (int i = 0; i < ContainerWindowsState::MAX_WINDOWS; ++i) {
        const auto& w = state.windows[i];
        if (w.isOpen && w.openSequence >= bestSeq) {
            best = i;
            bestSeq = w.openSequence;
        }
    }
    return best;
}

// --- draw ------------------------------------------------------------------

namespace {

void renderOne(UIRenderer& ui, const UILayout& layout, const ContainerWindow& w) {
    auto* tb = ui.getTextBatch();
    ui.drawPanelFor(ContainerLayout::windowRect(layout), UIWindow::Container);

    // --- Style_Titlebar: the Screen's <Text>Container</Text> ---
    const auto title = ContainerLayout::titleRect(layout);
    ui.drawRect(title, irr::video::SColor(200, 26, 26, 34));
    if (tb) {
        tb->addText("Container", title.UpperLeftCorner.X + 4,
                    title.UpperLeftCorner.Y + 2, kTitle);
    }

    // --- Style_Closebox ---
    const auto close = ContainerLayout::closeBoxRect(layout);
    ui.drawRect(close, w.closeHovered ? irr::video::SColor(220, 120, 60, 60)
                                      : irr::video::SColor(200, 60, 60, 72));
    if (tb) tb->addTextCentered("X", close, kText);

    // --- Container_Label: the container's own name, AlignCenter ---
    if (tb) {
        tb->addTextCentered(w.name.empty() ? std::string("Container") : w.name,
                            ContainerLayout::labelRect(layout), kText);
    }

    // --- Container_Icon: A_RecessedBox with the A_DragItem decal ---
    const auto icon = ContainerLayout::iconRect(layout);
    ui.drawSprite(icon, static_cast<uint8_t>(UISprite::SlotBackground));
    if (w.iconId != 0) ui.drawItemIcon(icon, w.iconId);

    // --- ContainerSlot1..N: A_RecessedBox, item art filling the cell ---
    const int n = containerVisibleSlots(w);
    for (int i = 0; i < n; ++i) {
        const auto cell = ContainerLayout::slotRect(layout, i);
        ui.drawSprite(cell, static_cast<uint8_t>(UISprite::SlotBackground));
        if (w.hoveredSlot == i) {
            ui.drawRect(cell, irr::video::SColor(60, 255, 255, 255));
        }
        const auto& s = w.slots[i];
        if (!s.hasItem) continue;
        const bool drewIcon = ui.drawItemIcon(cell, s.iconId);
        if (!tb) continue;
        if (!drewIcon) {
            tb->addText(s.itemName.substr(0, std::min<size_t>(3, s.itemName.size())),
                        cell.UpperLeftCorner.X + 2, cell.UpperLeftCorner.Y + 2, kText);
        }
        if (s.quantity > 1) {
            tb->addText(std::to_string(s.quantity), cell.UpperLeftCorner.X + 2,
                        cell.UpperLeftCorner.Y + 26, kQuantity);
        }
    }

    // --- Container_Combine: tradeskill containers only ---
    if (containerHasCombine(w)) {
        const auto combine = ContainerLayout::combineButtonRect(layout);
        ui.drawRect(combine, w.combineHovered ? irr::video::SColor(220, 90, 110, 60)
                                              : irr::video::SColor(200, 56, 62, 84));
        ui.drawFrame(combine, kFrame);
        if (tb) tb->addTextCentered(containerCombineLabel(w), combine, kText);
    }

    // --- Container_DoneButton ---
    const auto done = ContainerLayout::doneButtonRect(layout);
    ui.drawRect(done, w.doneHovered ? irr::video::SColor(220, 120, 60, 60)
                                    : irr::video::SColor(200, 56, 62, 84));
    ui.drawFrame(done, kFrame);
    if (tb) tb->addTextCentered("Done", done, kText);
}

}  // namespace

int containerDrawOrder(const ContainerWindowsState& state,
                       int (&out)[ContainerWindowsState::MAX_WINDOWS]) {
    int n = 0;
    for (int i = 0; i < ContainerWindowsState::MAX_WINDOWS; ++i) {
        if (state.windows[i].isOpen) out[n++] = i;
    }
    // Oldest first, so the newest is drawn last and is on top. The tie-break
    // on the index matches topmostContainerWindow()'s `>=` (the higher index
    // wins a tie).
    std::sort(out, out + n, [&](int a, int b) {
        const uint32_t sa = state.windows[a].openSequence;
        const uint32_t sb = state.windows[b].openSequence;
        return sa != sb ? sa < sb : a < b;
    });
    return n;
}

void renderContainerWindows(UIRenderer& ui, const UILayout& layout,
                            const ContainerWindowsState& state) {
    // Every open window is drawn, oldest first, each its own z-layer
    // (B992, #312): the newest covers the rest, text included, as the
    // official client stacks them (UIGAP-ORACLE 151 gestures.tsv row 20).
    int order[ContainerWindowsState::MAX_WINDOWS];
    const int n = containerDrawOrder(state, order);
    for (int k = 0; k < n; ++k) {
        ui.beginLayer();
        renderOne(ui, layout, state.windows[order[k]]);
    }
}

// --- hit tests -------------------------------------------------------------

ContainerHit hitTestContainer(const UILayout& layout,
                              const ContainerWindowsState& state,
                              int mouseX, int mouseY) {
    ContainerHit best;
    uint32_t bestSeq = 0;
    for (int i = 0; i < ContainerWindowsState::MAX_WINDOWS; ++i) {
        const auto& w = state.windows[i];
        if (!w.isOpen || w.openSequence < bestSeq) continue;
            if (!inside(ContainerLayout::windowRect(layout), mouseX, mouseY)) continue;

        ContainerHit hit;
        hit.window = i;
        hit.part = ContainerPart::Body;
        if (inside(ContainerLayout::closeBoxRect(layout), mouseX, mouseY)) {
            hit.part = ContainerPart::CloseBox;
        } else if (inside(ContainerLayout::doneButtonRect(layout), mouseX, mouseY)) {
            hit.part = ContainerPart::Done;
        } else if (containerHasCombine(w) &&
                   inside(ContainerLayout::combineButtonRect(layout), mouseX, mouseY)) {
            hit.part = ContainerPart::Combine;
        } else {
            const int n = containerVisibleSlots(w);
            for (int s = 0; s < n; ++s) {
                if (inside(ContainerLayout::slotRect(layout, s), mouseX, mouseY)) {
                    hit.part = ContainerPart::Slot;
                    hit.slot = s;
                    break;
                }
            }
        }
        best = hit;
        bestSeq = w.openSequence;
    }
    return best;
}

}  // namespace Graphics
}  // namespace EQT
