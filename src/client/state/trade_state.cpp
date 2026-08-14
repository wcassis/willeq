#include "client/state/trade_state.h"
#include "client/state/event_bus.h"

namespace eqt {
namespace state {

TradeState::TradeState() = default;

void TradeState::startTrade(uint16_t partnerId, const std::string& partnerName, bool isNpc) {
    m_active = true;
    m_partnerId = partnerId;
    m_partnerName = partnerName;
    m_isNpc = isNpc;

    // A request that just became active is no longer "pending" -- clear it
    // rather than leave a stale pending-request entry behind (matches
    // TradeManager::acceptTradeRequest()'s own m_pending_request_spawn_id/
    // m_pending_request_name -> m_partner_spawn_id/m_partner_name handoff).
    m_hasPendingRequest = false;
    m_pendingPartnerId = 0;
    m_pendingPartnerName.clear();

    if (m_eventBus) {
        TradeStartedData data;
        data.partnerId = partnerId;
        data.partnerName = partnerName;
        data.isNpc = isNpc;
        m_eventBus->publish(GameEventType::TradeStarted, data);
    }
}

void TradeState::setPendingRequest(uint32_t partnerId, const std::string& partnerName) {
    m_hasPendingRequest = true;
    m_pendingPartnerId = partnerId;
    m_pendingPartnerName = partnerName;

    if (m_eventBus) {
        TradeRequestReceivedData data;
        data.spawnId = partnerId;
        data.name = partnerName;
        m_eventBus->publish(GameEventType::TradeRequestReceived, data);
    }
}

void TradeState::setAcceptState(bool ownAccepted, bool partnerAccepted) {
    m_ownAccepted = ownAccepted;
    m_partnerAccepted = partnerAccepted;

    if (m_eventBus) {
        TradeAcceptStateChangedData data;
        data.ownAccepted = ownAccepted;
        data.partnerAccepted = partnerAccepted;
        m_eventBus->publish(GameEventType::TradeAcceptStateChanged, data);
    }
}

void TradeState::setMoney(bool isOwn, const TradeMoney& money) {
    if (isOwn) {
        m_ownMoney = money;
    } else {
        m_partnerMoney = money;
    }

    if (m_eventBus) {
        TradeMoneyUpdatedData data;
        data.who = isOwn ? 0 : 1;
        data.platinum = money.platinum;
        data.gold = money.gold;
        data.silver = money.silver;
        data.copper = money.copper;
        m_eventBus->publish(GameEventType::TradeMoneyUpdated, data);
    }
}

bool TradeState::hasPartnerItem(int slot) const {
    if (slot < 0 || slot >= TRADE_ITEM_SLOT_COUNT) {
        return false;
    }
    return m_partnerItems[slot].hasItem;
}

uint32_t TradeState::partnerItemId(int slot) const {
    static const uint32_t kZero = 0;
    if (slot < 0 || slot >= TRADE_ITEM_SLOT_COUNT) {
        return kZero;
    }
    return m_partnerItems[slot].itemId;
}

const std::string& TradeState::partnerItemName(int slot) const {
    static const std::string kEmpty;
    if (slot < 0 || slot >= TRADE_ITEM_SLOT_COUNT) {
        return kEmpty;
    }
    return m_partnerItems[slot].itemName;
}

void TradeState::setPartnerItem(int slot, uint32_t itemId, const std::string& itemName) {
    if (slot < 0 || slot >= TRADE_ITEM_SLOT_COUNT) {
        return;
    }
    m_partnerItems[slot].hasItem = (itemId != 0) || !itemName.empty();
    m_partnerItems[slot].itemId = itemId;
    m_partnerItems[slot].itemName = itemName;

    if (m_eventBus) {
        TradeItemUpdatedData data;
        data.who = 1;  // partner
        data.slot = static_cast<uint8_t>(slot);
        data.itemId = itemId;
        data.itemName = itemName;
        m_eventBus->publish(GameEventType::TradeItemUpdated, data);
    }
}

void TradeState::reset() {
    // Does not self-publish TradeCancelled/TradeCompleted -- reset() only
    // clears fields; the two callers (GameStateSyncBridge's TradeCancelled/
    // TradeCompleted cases, and EverQuest::SetupTradeWindowCallbacks()'s
    // m_on_cancelled/m_on_completed lambdas) already know which of the two
    // actually happened and publish the correctly-typed event themselves,
    // matching TradeManager::resetTradeState()'s own shape (it is a private
    // helper with no event of its own either -- callers publish).
    m_active = false;
    m_partnerId = 0;
    m_partnerName.clear();
    m_isNpc = false;

    m_hasPendingRequest = false;
    m_pendingPartnerId = 0;
    m_pendingPartnerName.clear();

    m_ownAccepted = false;
    m_partnerAccepted = false;

    m_ownMoney.clear();
    m_partnerMoney.clear();

    for (auto& slot : m_partnerItems) {
        slot.clear();
    }
}

} // namespace state
} // namespace eqt
