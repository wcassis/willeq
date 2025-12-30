#include "client/trade_manager.h"
#include "client/graphics/ui/inventory_manager.h"
#include "client/graphics/ui/item_instance.h"
#include "client/graphics/ui/inventory_constants.h"
#include "common/logging.h"

TradeManager::TradeManager()
{
    // Initialize own item slots to invalid
    m_own_item_slots.fill(eqt::inventory::SLOT_INVALID);
}

TradeManager::~TradeManager() = default;

bool TradeManager::isTrading() const
{
    return m_state == TradeState::Active;
}

void TradeManager::requestTrade(uint32_t targetSpawnId, const std::string& targetName, bool isNpc)
{
    if (m_state != TradeState::None) {
        LOG_WARN(MOD_MAIN, "Cannot request trade while already in a trade");
        return;
    }

    if (targetSpawnId == 0) {
        LOG_WARN(MOD_MAIN, "Cannot request trade with invalid target");
        return;
    }

    m_partner_spawn_id = targetSpawnId;
    m_partner_name = targetName;
    m_is_npc_trade = isNpc;

    sendTradeRequest(targetSpawnId);
    setState(TradeState::PendingRequest);
}

void TradeManager::acceptTradeRequest()
{
    if (m_state != TradeState::PendingAccept) {
        LOG_WARN(MOD_MAIN, "No pending trade request to accept");
        return;
    }

    m_partner_spawn_id = m_pending_request_spawn_id;
    m_partner_name = m_pending_request_name;

    sendTradeRequestAck();
    setState(TradeState::Active);
}

void TradeManager::rejectTradeRequest()
{
    if (m_state != TradeState::PendingAccept) {
        LOG_WARN(MOD_MAIN, "No pending trade request to reject");
        return;
    }

    sendCancelTrade();
    resetTradeState();
}

void TradeManager::cancelTrade()
{
    if (m_state == TradeState::None) {
        return;
    }

    sendCancelTrade();
    resetTradeState();

    if (m_on_cancelled) {
        m_on_cancelled();
    }
}

bool TradeManager::hasOwnItem(int slot) const
{
    if (slot < 0 || slot >= TRADE_SLOT_COUNT) {
        return false;
    }
    return m_own_item_slots[slot] != eqt::inventory::SLOT_INVALID;
}

const eqt::inventory::ItemInstance* TradeManager::getOwnItem(int slot) const
{
    if (!hasOwnItem(slot) || !m_inventory_manager) {
        return nullptr;
    }
    // Items in trade are still in the trade slots (3000-3003)
    return m_inventory_manager->getItem(eqt::inventory::TRADE_BEGIN + slot);
}

bool TradeManager::addItemToTrade(int16_t inventorySlot, int tradeSlot)
{
    if (m_state != TradeState::Active) {
        LOG_WARN(MOD_MAIN, "Cannot add item to trade - trade not active");
        return false;
    }

    if (tradeSlot < 0 || tradeSlot >= TRADE_SLOT_COUNT) {
        LOG_WARN(MOD_MAIN, "Invalid trade slot: {}", tradeSlot);
        return false;
    }

    if (hasOwnItem(tradeSlot)) {
        LOG_WARN(MOD_MAIN, "Trade slot {} already occupied", tradeSlot);
        return false;
    }

    // TODO: Validate item can be traded (not NO_DROP, etc.)
    // The actual item movement is handled by the inventory system via MoveItem packet
    // This just tracks that we've put something in the trade

    m_own_item_slots[tradeSlot] = inventorySlot;

    // Reset accept state when items change
    resetAccept();

    if (m_on_item_updated) {
        m_on_item_updated(true, tradeSlot);
    }

    return true;
}

bool TradeManager::removeItemFromTrade(int tradeSlot)
{
    if (m_state != TradeState::Active) {
        LOG_WARN(MOD_MAIN, "Cannot remove item from trade - trade not active");
        return false;
    }

    if (tradeSlot < 0 || tradeSlot >= TRADE_SLOT_COUNT) {
        LOG_WARN(MOD_MAIN, "Invalid trade slot: {}", tradeSlot);
        return false;
    }

    if (!hasOwnItem(tradeSlot)) {
        return false;
    }

    m_own_item_slots[tradeSlot] = eqt::inventory::SLOT_INVALID;

    // Reset accept state when items change
    resetAccept();

    if (m_on_item_updated) {
        m_on_item_updated(true, tradeSlot);
    }

    return true;
}

bool TradeManager::hasPartnerItem(int slot) const
{
    if (slot < 0 || slot >= TRADE_SLOT_COUNT) {
        return false;
    }
    return m_partner_items[slot] != nullptr;
}

const eqt::inventory::ItemInstance* TradeManager::getPartnerItem(int slot) const
{
    if (slot < 0 || slot >= TRADE_SLOT_COUNT) {
        return nullptr;
    }
    return m_partner_items[slot].get();
}

void TradeManager::setOwnMoney(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper)
{
    if (m_state != TradeState::Active) {
        LOG_WARN(MOD_MAIN, "Cannot set trade money - trade not active");
        return;
    }

    // Send MoveCoin packets for any coin increases
    // MoveCoin moves coins from cursor to trade
    // Coin types: 0=copper, 1=silver, 2=gold, 3=platinum
    if (copper > m_own_money.copper) {
        int32_t delta = copper - m_own_money.copper;
        sendMoveCoin(EQT::COINTYPE_CP, delta);
    }
    if (silver > m_own_money.silver) {
        int32_t delta = silver - m_own_money.silver;
        sendMoveCoin(EQT::COINTYPE_SP, delta);
    }
    if (gold > m_own_money.gold) {
        int32_t delta = gold - m_own_money.gold;
        sendMoveCoin(EQT::COINTYPE_GP, delta);
    }
    if (platinum > m_own_money.platinum) {
        int32_t delta = platinum - m_own_money.platinum;
        sendMoveCoin(EQT::COINTYPE_PP, delta);
    }

    m_own_money.platinum = platinum;
    m_own_money.gold = gold;
    m_own_money.silver = silver;
    m_own_money.copper = copper;

    // Reset accept state when money changes
    resetAccept();

    if (m_on_money_updated) {
        m_on_money_updated(true);
    }
}

void TradeManager::clickAccept()
{
    if (m_state != TradeState::Active) {
        LOG_WARN(MOD_MAIN, "Cannot accept trade - trade not active");
        return;
    }

    m_own_accepted = true;
    sendTradeAcceptClick();

    if (m_on_accept_state_changed) {
        m_on_accept_state_changed(m_own_accepted, m_partner_accepted);
    }
}

void TradeManager::resetAccept()
{
    bool wasAccepted = m_own_accepted || m_partner_accepted;
    m_own_accepted = false;
    m_partner_accepted = false;

    if (wasAccepted && m_on_accept_state_changed) {
        m_on_accept_state_changed(m_own_accepted, m_partner_accepted);
    }
}

// Packet handlers

void TradeManager::handleTradeRequest(const EQT::TradeRequest_Struct& req, const std::string& fromName)
{
    if (m_state != TradeState::None) {
        // Already in a trade, auto-reject
        LOG_INFO(MOD_MAIN, "Received trade request from {} while already trading, ignoring",
                 fromName);
        return;
    }

    m_pending_request_spawn_id = req.from_spawn_id;
    m_pending_request_name = fromName;
    setState(TradeState::PendingAccept);

    if (m_on_request_received) {
        m_on_request_received(req.from_spawn_id, fromName);
    }

    LOG_INFO(MOD_MAIN, "{} wants to trade with you", fromName);
}

void TradeManager::handleTradeRequestAck(const EQT::TradeRequestAck_Struct& ack)
{
    if (m_state == TradeState::PendingRequest) {
        // Our request was accepted
        setState(TradeState::Active);
        LOG_INFO(MOD_MAIN, "Trade request accepted by {}", m_partner_name);
    }
    else if (m_state == TradeState::PendingAccept) {
        // We accepted their request, trade is now active
        setState(TradeState::Active);
        LOG_INFO(MOD_MAIN, "Trade started with {}", m_partner_name);
    }
}

void TradeManager::handleTradeCoins(const EQT::TradeCoins_Struct& coins)
{
    if (m_state != TradeState::Active) {
        return;
    }

    // Update partner's money based on coin slot
    // Coin slots: 0=copper, 1=silver, 2=gold, 3=platinum
    switch (coins.slot) {
        case 0:
            m_partner_money.copper = coins.amount;
            break;
        case 1:
            m_partner_money.silver = coins.amount;
            break;
        case 2:
            m_partner_money.gold = coins.amount;
            break;
        case 3:
            m_partner_money.platinum = coins.amount;
            break;
        default:
            LOG_WARN(MOD_MAIN, "Unknown coin slot in trade: {}", coins.slot);
            return;
    }

    // Reset accept state when partner changes money
    resetAccept();

    if (m_on_money_updated) {
        m_on_money_updated(false);
    }
}

void TradeManager::handleTradeAcceptClick(const EQT::TradeAcceptClick_Struct& accept)
{
    if (m_state != TradeState::Active) {
        return;
    }

    // This is the partner's accept click
    m_partner_accepted = (accept.accepted != 0);

    if (m_on_accept_state_changed) {
        m_on_accept_state_changed(m_own_accepted, m_partner_accepted);
    }

    LOG_INFO(MOD_MAIN, "Trade partner {} the trade",
             m_partner_accepted ? "accepted" : "unaccepted");
}

void TradeManager::handleFinishTrade(const EQT::FinishTrade_Struct& finish)
{
    (void)finish;  // Unused parameter

    if (m_state != TradeState::Active) {
        return;
    }

    setState(TradeState::Completed);
    LOG_INFO(MOD_MAIN, "Trade completed with {}", m_partner_name);

    if (m_on_completed) {
        m_on_completed();
    }

    resetTradeState();
}

void TradeManager::handleCancelTrade(const EQT::CancelTrade_Struct& cancel)
{
    (void)cancel;  // Unused parameter

    if (m_state == TradeState::None) {
        return;
    }

    LOG_INFO(MOD_MAIN, "Trade cancelled");

    if (m_on_cancelled) {
        m_on_cancelled();
    }

    resetTradeState();
}

void TradeManager::handlePartnerItemPacket(int slot, std::unique_ptr<eqt::inventory::ItemInstance> item)
{
    if (m_state != TradeState::Active) {
        return;
    }

    if (slot < 0 || slot >= TRADE_SLOT_COUNT) {
        LOG_WARN(MOD_MAIN, "Invalid partner trade slot: {}", slot);
        return;
    }

    m_partner_items[slot] = std::move(item);

    // Reset accept state when partner adds item
    resetAccept();

    if (m_on_item_updated) {
        m_on_item_updated(false, slot);
    }
}

void TradeManager::handlePartnerItemRemoved(int slot)
{
    if (m_state != TradeState::Active) {
        return;
    }

    if (slot < 0 || slot >= TRADE_SLOT_COUNT) {
        return;
    }

    m_partner_items[slot].reset();

    // Reset accept state when partner removes item
    resetAccept();

    if (m_on_item_updated) {
        m_on_item_updated(false, slot);
    }
}

// Private methods

void TradeManager::resetTradeState()
{
    m_state = TradeState::None;
    m_partner_spawn_id = 0;
    m_partner_name.clear();
    m_is_npc_trade = false;
    m_pending_request_spawn_id = 0;
    m_pending_request_name.clear();

    m_own_item_slots.fill(eqt::inventory::SLOT_INVALID);
    for (auto& item : m_partner_items) {
        item.reset();
    }

    m_own_money.clear();
    m_partner_money.clear();

    m_own_accepted = false;
    m_partner_accepted = false;
}

void TradeManager::setState(TradeState newState)
{
    if (m_state != newState) {
        m_state = newState;
        if (m_on_state_changed) {
            m_on_state_changed(newState);
        }
    }
}

void TradeManager::sendTradeRequest(uint32_t targetSpawnId)
{
    if (!m_send_trade_request) {
        LOG_WARN(MOD_MAIN, "No send trade request callback set");
        return;
    }

    EQT::TradeRequest_Struct req;
    req.target_spawn_id = targetSpawnId;
    req.from_spawn_id = m_my_spawn_id;

    m_send_trade_request(req);
    LOG_DEBUG(MOD_MAIN, "Sent trade request to spawn {}", targetSpawnId);
}

void TradeManager::sendTradeRequestAck()
{
    if (!m_send_trade_request_ack) {
        LOG_WARN(MOD_MAIN, "No send trade request ack callback set");
        return;
    }

    EQT::TradeRequestAck_Struct ack;
    // First field (target_spawn_id) = who to forward packet to (trade initiator)
    // Second field (from_spawn_id) = who is accepting (us)
    ack.target_spawn_id = m_partner_spawn_id;
    ack.from_spawn_id = m_my_spawn_id;

    m_send_trade_request_ack(ack);
    LOG_DEBUG(MOD_MAIN, "Sent trade request ack to spawn {}", m_partner_spawn_id);
}

void TradeManager::sendMoveCoin(int32_t coinType, int32_t amount)
{
    if (!m_send_move_coin) {
        LOG_WARN(MOD_MAIN, "No send move coin callback set");
        return;
    }

    // Step 1: Move coins from inventory to cursor
    EQT::MoveCoin_Struct moveToCarousel;
    moveToCarousel.from_slot = EQT::COINSLOT_INVENTORY;  // Move from inventory
    moveToCarousel.to_slot = EQT::COINSLOT_CURSOR;       // Move to cursor
    moveToCarousel.cointype1 = coinType;
    moveToCarousel.cointype2 = coinType;
    moveToCarousel.amount = amount;

    m_send_move_coin(moveToCarousel);
    LOG_DEBUG(MOD_MAIN, "Sent MoveCoin (inv->cursor): from_slot={} to_slot={} cointype={} amount={}",
        moveToCarousel.from_slot, moveToCarousel.to_slot, coinType, amount);

    // Step 2: Move coins from cursor to trade
    EQT::MoveCoin_Struct moveToTrade;
    moveToTrade.from_slot = EQT::COINSLOT_CURSOR;  // Move from cursor
    moveToTrade.to_slot = EQT::COINSLOT_TRADE;     // Move to trade
    moveToTrade.cointype1 = coinType;
    moveToTrade.cointype2 = coinType;
    moveToTrade.amount = amount;

    m_send_move_coin(moveToTrade);
    LOG_DEBUG(MOD_MAIN, "Sent MoveCoin (cursor->trade): from_slot={} to_slot={} cointype={} amount={}",
        moveToTrade.from_slot, moveToTrade.to_slot, coinType, amount);
}

void TradeManager::sendTradeAcceptClick()
{
    if (!m_send_trade_accept_click) {
        LOG_WARN(MOD_MAIN, "No send trade accept click callback set");
        return;
    }

    EQT::TradeAcceptClick_Struct accept;
    accept.spawn_id = m_my_spawn_id;
    accept.accepted = 1;
    accept.unknown[0] = 0;
    accept.unknown[1] = 0;
    accept.unknown[2] = 0;

    m_send_trade_accept_click(accept);
    LOG_DEBUG(MOD_MAIN, "Sent trade accept click");
}

void TradeManager::sendCancelTrade()
{
    if (!m_send_cancel_trade) {
        LOG_WARN(MOD_MAIN, "No send cancel trade callback set");
        return;
    }

    EQT::CancelTrade_Struct cancel;
    cancel.spawn_id = m_my_spawn_id;
    cancel.action = 0x07;  // Cancel action code observed in logs

    m_send_cancel_trade(cancel);
    LOG_DEBUG(MOD_MAIN, "Sent cancel trade");
}
