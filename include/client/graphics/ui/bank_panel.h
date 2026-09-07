/*
 * The bank window -- renderer-side state and draw (T436, board B429).
 *
 * WHAT WAS BROKEN. fable had no bank window: `/bank` opened a server-side
 * session and `/bank deposit|withdraw|convert` moved coins by typing. The
 * eight bank slots were populated over the wire (the generic ItemPacket
 * choke point in zone_dispatch.cpp publishes an enriched
 * `InventorySlotChanged` for slot ids 2000..2007, icon and all) and the
 * bridge dropped every one of them, because its arm covered 0..29 and the
 * WORLD container range and nothing else.
 *
 * EIGHT SLOTS, NOT SIXTEEN -- the era rule; see bank_layout.h, which quotes
 * EQUI_BankWnd.xml's BW_BankSlot0..7 and records that the file's shared-bank
 * pieces are declared with NO geometry at all.
 *
 * Same static-panel idiom as the loot, tradeskill and item-display windows.
 * The geometry is in include/client/graphics/ui/bank_layout.h; this file
 * computes no rect.
 *
 * NO NEW PROTOCOL CODE beyond one close intent. The four BW_Money rows are
 * fed by the existing `BankCurrencyChanged` event (a LOG_TRACE stub with no
 * consumer until now). BW_ChangeButton has no fable setting behind it -- the
 * oracle's Change opens a coin-denomination dialog fable does not have, and
 * `/bank convert` needs a from/to/amount triple no single click supplies --
 * so it is drawn GREYED and is NOT hit-testable, the T363 rule T434 made
 * mechanical. Board B441. BW_DoneButton pushes `CloseBankIntent`, the one
 * new intent, whose handler is the existing `CloseBankWindow()`; the panel
 * closes when the server-side session is torn down and `BankWindowClosed`
 * comes back, never optimistically (T290's rule).
 */

#pragma once

#include <cstdint>
#include <string>

#include "client/graphics/ui/bank_layout.h"
#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state {
struct WindowOpenedData;
struct WindowClosedData;
struct BankCurrencyChangedData;
struct InventorySlotChangedData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;

/** One BW_BankSlot cell. The fields InventorySlotChangedData carries. */
struct BankSlotDisplay {
    bool hasItem = false;
    std::string itemName;
    uint32_t iconId = 0;
    int32_t quantity = 0;
};

struct BankPanelState {
    static constexpr int SLOT_COUNT = BankLayout::SLOT_COUNT;   // 8
    static constexpr int COIN_COUNT = BankLayout::COIN_COUNT;   // 4

    bool isOpen = false;

    uint16_t npcId = 0;
    std::string bankerName;                 // -> BW_BankerName

    BankSlotDisplay slots[SLOT_COUNT];      // -> BW_BankSlot0..7
    // BW_Money0..3, platinum/gold/silver/copper in the oracle's element order.
    int32_t coins[COIN_COUNT] = {0, 0, 0, 0};

    int hoveredSlot = -1;
    bool doneHovered = false;
    bool closeHovered = false;
};

// --- the bridge arms, as free functions (T290's idiom) ---------------------

/** BankWindowOpened. */
void applyBankWindowOpened(BankPanelState& state,
                           const eqt::state::WindowOpenedData& data);

/** BankWindowClosed. */
void applyBankWindowClosed(BankPanelState& state,
                           const eqt::state::WindowClosedData& data);

/** BankCurrencyChanged -> the four BW_Money rows. */
void applyBankCurrencyChanged(BankPanelState& state,
                              const eqt::state::BankCurrencyChangedData& data);

/**
 * The bank arm of the InventorySlotChanged bridge case. Returns false and
 * changes nothing for a slot outside 2000..2007, so the caller can hand it
 * every event the way applyTradeskillSlotChanged() is handed every event.
 */
bool applyBankSlotChanged(BankPanelState& state,
                          const eqt::state::InventorySlotChangedData& data);

// --- draw and hit test -----------------------------------------------------

/** Draw the window. Only when state.isOpen. */
void renderBankWindow(UIRenderer& ui, const UILayout& layout,
                      const BankPanelState& state);

/** Which BW_BankSlot is at the point? -1 on a miss or a closed window. */
int hitTestBankSlot(const UILayout& layout, const BankPanelState& state,
                    int mouseX, int mouseY);

/** BW_DoneButton, and Style_Closebox, which do the same thing. */
bool hitTestBankDoneButton(const UILayout& layout, const BankPanelState& state,
                           int mouseX, int mouseY);
bool hitTestBankCloseBox(const UILayout& layout, const BankPanelState& state,
                         int mouseX, int mouseY);

/** Is the point anywhere inside the open window? (Click consumption.) */
bool bankContains(const UILayout& layout, const BankPanelState& state,
                  int mouseX, int mouseY);

}  // namespace Graphics
}  // namespace EQT
