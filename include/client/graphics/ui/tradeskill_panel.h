/*
 * The tradeskill container window — renderer-side state and draw (T415,
 * board B321).
 *
 * Before this, `TradeskillContainerOpenedEvent`'s `containerName`,
 * `isWorldObject` and `slotCount` and `TradeskillContainerClosedEvent`'s
 * `wasWorldObject` reached `IrrlichtBridge` and died in a `LOG_TRACE`:
 * fable had no container window for them to describe. They are exactly a
 * container window's title, its close behaviour and its grid size, so this
 * is the panel they were always for.
 *
 * Same static-panel idiom as T290's loot window and T393's purse: a plain
 * state struct plus free functions, so a test drives the REAL publisher's
 * data struct through the REAL applier with no Irrlicht device.
 *
 * WHERE THE CONTENTS COME FROM. Not a new event — the existing one.
 * eqemu's `Object::HandleClick` sends a world container's items one
 * `ITEM_PACKET_WORLD_CONTAINER` packet per occupied index;
 * `InventoryManager::processItemPacket` maps index i to
 * `WORLD_BEGIN + i` (T282) and `zone_dispatch.cpp`'s generic ItemPacket
 * fallback publishes an ENRICHED `InventorySlotChanged` for it — name, icon
 * and quantity included. That event already crossed the bridge; the bridge
 * arm just dropped every slot id outside 0..29. `applyTradeskillSlotChanged`
 * below is the missing branch. `ZoneProcessClearObject` publishes the same
 * event with `hasItem = false` for all ten slots when the server empties the
 * container, so the clear path needs nothing extra either.
 */

#pragma once

#include <cstdint>
#include <string>

#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state {
struct TradeskillContainerOpenedEvent;
struct TradeskillContainerClosedEvent;
struct InventorySlotChangedData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;

/** One container cell, the same shape as SlotDisplayInfo. */
struct TradeskillSlotDisplay {
    bool hasItem = false;
    std::string itemName;
    uint32_t iconId = 0;
    int32_t quantity = 1;
};

struct TradeskillPanelState {
    bool isOpen = false;

    // The four fields B321 named.
    std::string containerName;   // -> the window title (COMBW_ContainerName)
    bool isWorldObject = false;  // -> which close the Close button sends
    uint8_t slotCount = 0;       // -> how many of the ten cells are drawn
    uint32_t objectId = 0;       // world drop id, for TradeskillCloseIntent
    int16_t inventorySlot = -1;  // inventory container slot, when not a world one

    // COMBW_RequiredIcon0..9 — the oracle has exactly ten.
    static constexpr int MAX_SLOTS = 10;
    TradeskillSlotDisplay slots[MAX_SLOTS];

    int hoveredSlot = -1;
};

/** The window's two buttons. */
enum class TradeskillButton : uint8_t {
    None = 0,
    Combine,
    Close
};

/**
 * How many of the ten cells this container actually has, clamped into
 * 0..MAX_SLOTS. The server's `slotCount` is not trusted to be in range;
 * the drawn grid, the hit test and the tests all ask this one body.
 */
int tradeskillVisibleSlots(const TradeskillPanelState& state);

/** The three bridge arms, as free functions (T290's idiom). */
void applyTradeskillContainerOpened(
    TradeskillPanelState& state,
    const eqt::state::TradeskillContainerOpenedEvent& data);
void applyTradeskillContainerClosed(
    TradeskillPanelState& state,
    const eqt::state::TradeskillContainerClosedEvent& data);
/**
 * Route one InventorySlotChanged into the container grid. A slot id outside
 * WORLD_BEGIN..WORLD_END is not this panel's and is ignored, so this can be
 * called for every inventory event unconditionally.
 */
void applyTradeskillSlotChanged(
    TradeskillPanelState& state,
    const eqt::state::InventorySlotChangedData& data);

/** Render the container window. Only when state.isOpen. */
void renderTradeskillPopup(UIRenderer& ui, const UILayout& layout,
                           const TradeskillPanelState& state);

/**
 * Hit-test the two buttons. `TradeskillButton::None` on a miss and on a
 * CLOSED window — a hidden panel must not eat clicks (T290's rule).
 */
TradeskillButton hitTestTradeskillButton(const UILayout& layout,
                                         const TradeskillPanelState& state,
                                         int mouseX, int mouseY);

/**
 * Hit-test the container cells; returns 0..slotCount-1 or -1. The twin of
 * renderTradeskillPopup()'s cell loop — same geometry, same bound — so the
 * cell the cursor is over and the cell a click addresses cannot disagree
 * (T277's rule).
 */
int hitTestTradeskillSlot(const UILayout& layout,
                          const TradeskillPanelState& state,
                          int mouseX, int mouseY);

} // namespace Graphics
} // namespace EQT
