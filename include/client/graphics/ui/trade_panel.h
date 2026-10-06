/*
 * The trade window (EQUI_TradeWnd.xml) and the NPC give window
 * (EQUI_GiveWnd.xml) -- renderer-side state, appliers, draw and hit tests
 * (B896, GitLab #212).
 *
 * ONE state for both, because the official client opens one or the other for
 * the SAME session: clicking a player with an item on the cursor opens the
 * trade window, clicking an NPC the give window, and both are a trade with
 * the server (OP_TradeRequest / OP_TradeRequestAck, items to 3000+n,
 * OP_TradeAcceptClick). MEASURED: review/UIGAP-ORACLE/212/trade.tsv (rows
 * cited at each rule in trade_panel.cpp). `isNpc` picks the window.
 *
 * Same static-panel idiom as the bank and container windows: plain state and
 * free functions, so a test drives the REAL publishers' data structs through
 * the REAL appliers with no Irrlicht device. Nothing here is optimistic: the
 * window opens on TradeStarted, our own slots fill from the game thread's
 * InventorySlotChanged for 3000..3007, the partner's from TradeItemUpdated,
 * the coin rows from TradeMoneyUpdated, the names' colour from
 * TradeAcceptStateChanged, and it closes on TradeCompleted / TradeCancelled.
 * A click pushes an intent and changes nothing here.
 */

#pragma once

#include <cstdint>
#include <string>

#include "client/graphics/ui/give_layout.h"
#include "client/graphics/ui/trade_layout.h"
#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state {
struct TradeStartedData;
struct TradeItemUpdatedData;
struct TradeMoneyUpdatedData;
struct TradeAcceptStateChangedData;
struct InventorySlotChangedData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;

struct TradeSlotDisplay {
    bool hasItem = false;
    std::string itemName;
    uint32_t iconId = 0;
    int32_t quantity = 0;
};

struct TradePanelState {
    static constexpr int SLOT_COUNT = TradeLayout::SLOTS_PER_SIDE;   // 8 a side
    static constexpr int GIVE_SLOT_COUNT = GiveLayout::SLOT_COUNT;    // 4
    static constexpr int COIN_COUNT = TradeLayout::COIN_COUNT;        // pp gp sp cp

    bool isOpen = false;
    bool isNpc = false;             // true: EQUI_GiveWnd, false: EQUI_TradeWnd
    uint32_t partnerId = 0;
    std::string partnerName;        // TRDW_HisName / GVW_NPCName (display form)
    std::string ownName;            // TRDW_MyName

    TradeSlotDisplay own[SLOT_COUNT];       // 3000 + i
    TradeSlotDisplay partner[SLOT_COUNT];   // the partner's slot i (read-only)
    uint32_t ownCoins[COIN_COUNT] = {0, 0, 0, 0};       // 0 pp .. 3 cp
    uint32_t partnerCoins[COIN_COUNT] = {0, 0, 0, 0};

    bool ownAccepted = false;       // our name turns green
    bool partnerAccepted = false;   // the partner's name turns green

    // Hover, written by the input pass.
    int hoveredOwnSlot = -1;
    int hoveredCoin = -1;
    bool acceptHovered = false;     // TRDW_Trade_Button / GVW_Give_Button
    bool cancelHovered = false;
    bool closeHovered = false;
};

/** Which part of the open window a point is over. */
enum class TradePart : uint8_t {
    None = 0,      // not over the window (or no window is open)
    Body,          // inside the window, on no control
    OwnSlot,       // TRDW_TradeSlot0..7 / GVW_MyItemSlot0..3 -- index in `index`
    PartnerSlot,   // TRDW_TradeSlot8..15 (trade window only)
    OwnCoin,       // TRDW_MyMoney0..3 / GVW_MyMoney0..3 -- 0 pp .. 3 cp
    PartnerCoin,   // TRDW_HisMoney0..3
    Accept,        // TRDW_Trade_Button / GVW_Give_Button
    Cancel,        // TRDW_Cancel_Button / GVW_Cancel_Button
    CloseBox
};

struct TradeHit {
    TradePart part = TradePart::None;
    int index = -1;
};

/** The model (Titanium) slot of our own slot `i`: TRADE_BEGIN + i. */
int16_t tradeOwnSlotId(int i);

/** How many own slots the open window shows: 8 (trade) or 4 (give). */
int tradeOwnSlotCount(const TradePanelState& state);

/**
 * The colour rule, MEASURED (trade.tsv rows 1, 4, 6): both names are drawn
 * RED and a side's name turns GREEN while that side has pressed Trade. The
 * give window's NPC name keeps the XML's white (row 9's capture).
 */
enum class TradeNameColor : uint8_t { Red, Green, White };
TradeNameColor tradeOwnNameColor(const TradePanelState& state);
TradeNameColor tradePartnerNameColor(const TradePanelState& state);

/**
 * A motionless world click with an ITEM held: what it is. MEASURED, trade.tsv
 * rows 1 and 9: on a player it opens a trade, on an NPC the give window
 * (both through TargetIntent -> TradeManager::requestTrade on the game
 * thread); anywhere else -- the ground, a corpse -- it is B442b's drop prompt
 * (UIGAP-ORACLE/151 rows 31-33). The trade/give cases take precedence.
 * `spawnId` 0 means no visible entity was under the click.
 */
enum class HeldItemWorldClick : uint8_t { Drop, Trade, Give };
HeldItemWorldClick classifyHeldItemWorldClick(uint16_t spawnId, bool isNpc, bool isCorpse);

// --- bridge arms (T290's free-function idiom) --------------------------------

/** TradeStarted: open the window (trade or give) for a fresh session. */
void applyTradeStarted(TradePanelState& state, const eqt::state::TradeStartedData& data,
                       const std::string& ownName);
/** TradeItemUpdated: who == 1 is the partner's slot; who == 0 our own. */
void applyTradeItemUpdated(TradePanelState& state, const eqt::state::TradeItemUpdatedData& data);
/** TradeMoneyUpdated: who == 0 our row, 1 the partner's. */
void applyTradeMoneyUpdated(TradePanelState& state, const eqt::state::TradeMoneyUpdatedData& data);
/** TradeAcceptStateChanged: the two names' colours. */
void applyTradeAcceptStateChanged(TradePanelState& state,
                                  const eqt::state::TradeAcceptStateChangedData& data);
/** TradeCompleted / TradeCancelled: the window closes and empties. */
void applyTradeEnded(TradePanelState& state);
/**
 * Route one InventorySlotChanged into our own column. Returns true for the
 * trade range 3000..3007 (whether or not a window is open), false otherwise.
 */
bool applyTradeSlotChanged(TradePanelState& state, const eqt::state::InventorySlotChangedData& data);

// --- draw / hit test -----------------------------------------------------------

/** Draw the open window (trade or give); nothing when closed. */
void renderTradeWindow(UIRenderer& ui, const UILayout& layout, const TradePanelState& state);

/**
 * What the point is over. A closed window is never hit (T290's rule), the
 * give window has no partner column, and a give slot past 3 does not exist.
 */
TradeHit hitTestTradeWindow(const UILayout& layout, const TradePanelState& state,
                            int mouseX, int mouseY);

}  // namespace Graphics
}  // namespace EQT
