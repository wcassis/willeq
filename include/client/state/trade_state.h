#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

// Number of trade slots per side, matching TRADE_SLOT_COUNT in
// trade_manager.h / TRADE_BEGIN..TRADE_END in inventory_constants.h.
static constexpr int TRADE_ITEM_SLOT_COUNT = 8;

/**
 * TradeMoney - platinum/gold/silver/copper offered on one side of a trade.
 * Deliberately a plain local struct, not a reuse of trade_manager.h's
 * TradeMoney -- state:: modules do not depend on trade_manager.h, matching
 * every other domain (PetState doesn't include combat.h, etc.).
 */
struct TradeMoney {
    uint32_t platinum = 0;
    uint32_t gold = 0;
    uint32_t silver = 0;
    uint32_t copper = 0;

    void clear() { platinum = gold = silver = copper = 0; }
};

/**
 * TradeItemSlot - one partner-offered item slot.
 */
struct TradeItemSlot {
    bool hasItem = false;
    uint32_t itemId = 0;
    std::string itemName;

    void clear() {
        hasItem = false;
        itemId = 0;
        itemName.clear();
    }
};

/**
 * TradeState - player-to-player trade domain (T103).
 *
 * Projects the same session TradeManager (client/trade_manager.h) already
 * manages, from the bridge events TradeManager has published since before
 * this unit existed (TradeRequestReceived / TradeStarted / TradeItemUpdated /
 * TradeMoneyUpdated / TradeAcceptStateChanged / TradeCancelled /
 * TradeCompleted -- event_bus.h) but that nothing ever consumed
 * (IrrlichtBridge only LOG_TRACE'd them). No new event type was needed.
 *
 * Two fields are deliberately NOT tracked here, both verified rather than
 * assumed missing (see refactor/T103_trade_state_domain.md's Review for the
 * call-site tracing):
 *
 * 1. The player's OWN offered items. TradeManager::addItemToTrade()/
 *    removeItemFromTrade() -- the only path that would populate the "own"
 *    side of TradeItemUpdated -- has zero production callers; items
 *    actually move into a trade via the ordinary inventory MoveItem flow
 *    (slots TRADE_BEGIN..TRADE_END, inventory_constants.h), which
 *    InventoryState does not model either (it only tracks equipment/
 *    general/bank occupancy). setOnItemUpdated()'s isOwn branch
 *    (eq_merchant.cpp) is therefore structurally dead in production: no
 *    event ever carries "my own trade slot N now holds item X". A real
 *    reconstructibility gap for this one field, reported rather than
 *    guessed at. The player's own offered items remain visible only via
 *    the existing inventory-slot UI, unaffected by this unit.
 * 2. The outgoing "I sent a request, awaiting their response" sub-state
 *    (TradeManager::TradeState::PendingRequest). Entering it publishes no
 *    event at all -- setOnStateChanged()'s callback (eq_merchant.cpp) only
 *    acts on the Active and None transitions. A second field-loss case,
 *    reported for the same reason. No current consumer needs it:
 *    EqActionHandler::requestTrade()'s only precondition was isTrading()
 *    (== Active), which active() below reproduces exactly, so this gap
 *    changes no observable behavior.
 */
class TradeState {
public:
    TradeState();
    ~TradeState() = default;

    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Active trade ==========

    bool active() const { return m_active; }
    uint16_t partnerId() const { return m_partnerId; }
    const std::string& partnerName() const { return m_partnerName; }
    bool isNpcTrade() const { return m_isNpc; }

    /**
     * Trade became active (TradeStarted). Clears any pending-request state,
     * since a request that just became active is no longer "pending".
     */
    void startTrade(uint16_t partnerId, const std::string& partnerName, bool isNpc);

    // ========== Pending incoming request ==========

    bool hasPendingRequest() const { return m_hasPendingRequest; }
    uint32_t pendingPartnerId() const { return m_pendingPartnerId; }
    const std::string& pendingPartnerName() const { return m_pendingPartnerName; }

    void setPendingRequest(uint32_t partnerId, const std::string& partnerName);

    // ========== Accept state ==========

    bool ownAccepted() const { return m_ownAccepted; }
    bool partnerAccepted() const { return m_partnerAccepted; }
    void setAcceptState(bool ownAccepted, bool partnerAccepted);

    // ========== Money ==========

    const TradeMoney& ownMoney() const { return m_ownMoney; }
    const TradeMoney& partnerMoney() const { return m_partnerMoney; }
    void setMoney(bool isOwn, const TradeMoney& money);

    // ========== Partner items ==========
    // (own items are not tracked -- see class comment)

    bool hasPartnerItem(int slot) const;
    uint32_t partnerItemId(int slot) const;
    const std::string& partnerItemName(int slot) const;
    void setPartnerItem(int slot, uint32_t itemId, const std::string& itemName);

    // ========== Reset ==========

    /**
     * Clear all trade state (TradeCancelled / TradeCompleted, or zone
     * change). Mirrors TradeManager::resetTradeState().
     */
    void reset();

private:
    EventBus* m_eventBus = nullptr;

    bool m_active = false;
    uint16_t m_partnerId = 0;
    std::string m_partnerName;
    bool m_isNpc = false;

    bool m_hasPendingRequest = false;
    uint32_t m_pendingPartnerId = 0;
    std::string m_pendingPartnerName;

    bool m_ownAccepted = false;
    bool m_partnerAccepted = false;

    TradeMoney m_ownMoney;
    TradeMoney m_partnerMoney;

    std::array<TradeItemSlot, TRADE_ITEM_SLOT_COUNT> m_partnerItems;
};

} // namespace state
} // namespace eqt
