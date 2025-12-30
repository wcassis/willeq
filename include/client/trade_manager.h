#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <memory>
#include <functional>
#include "common/packet_structs.h"

namespace eqt {
namespace inventory {
class ItemInstance;
class InventoryManager;
}
}

// Trade state enum
enum class TradeState {
    None,           // Not trading
    PendingRequest, // Waiting for target to accept our request
    PendingAccept,  // We received a request, waiting for us to accept/decline
    Active,         // Trade window open, exchanging items
    Completed       // Trade finished (brief state before returning to None)
};

// Trade money structure
struct TradeMoney {
    uint32_t platinum = 0;
    uint32_t gold = 0;
    uint32_t silver = 0;
    uint32_t copper = 0;

    void clear() {
        platinum = gold = silver = copper = 0;
    }

    uint64_t toCopper() const {
        return static_cast<uint64_t>(platinum) * 1000 +
               static_cast<uint64_t>(gold) * 100 +
               static_cast<uint64_t>(silver) * 10 +
               static_cast<uint64_t>(copper);
    }
};

// Number of trade slots per player
static constexpr int TRADE_SLOT_COUNT = 8;

// UI callbacks
using TradeStateChangedCallback = std::function<void(TradeState newState)>;
using TradeRequestReceivedCallback = std::function<void(uint32_t fromSpawnId, const std::string& fromName)>;
using TradeItemUpdatedCallback = std::function<void(bool isOwnItem, int slot)>;
using TradeMoneyUpdatedCallback = std::function<void(bool isOwnMoney)>;
using TradeAcceptStateChangedCallback = std::function<void(bool ownAccepted, bool partnerAccepted)>;
using TradeCompletedCallback = std::function<void()>;
using TradeCancelledCallback = std::function<void()>;

// Network callbacks (for sending packets to server)
using SendTradeRequestCallback = std::function<void(const EQT::TradeRequest_Struct&)>;
using SendTradeRequestAckCallback = std::function<void(const EQT::TradeRequestAck_Struct&)>;
using SendMoveCoinCallback = std::function<void(const EQT::MoveCoin_Struct&)>;
using SendTradeAcceptClickCallback = std::function<void(const EQT::TradeAcceptClick_Struct&)>;
using SendCancelTradeCallback = std::function<void(const EQT::CancelTrade_Struct&)>;

class TradeManager {
public:
    TradeManager();
    ~TradeManager();

    // Trade state
    bool isTrading() const;
    TradeState getState() const { return m_state; }
    uint32_t getPartnerSpawnId() const { return m_partner_spawn_id; }
    const std::string& getPartnerName() const { return m_partner_name; }
    bool isNpcTrade() const { return m_is_npc_trade; }

    // Initiate/accept/reject trade
    void requestTrade(uint32_t targetSpawnId, const std::string& targetName, bool isNpc = false);
    void acceptTradeRequest();
    void rejectTradeRequest();
    void cancelTrade();

    // Item management (own side, slots 0-3)
    bool hasOwnItem(int slot) const;
    const eqt::inventory::ItemInstance* getOwnItem(int slot) const;
    bool addItemToTrade(int16_t inventorySlot, int tradeSlot);
    bool removeItemFromTrade(int tradeSlot);

    // Partner items (slots 0-3, read-only)
    bool hasPartnerItem(int slot) const;
    const eqt::inventory::ItemInstance* getPartnerItem(int slot) const;

    // Money management (own side)
    void setOwnMoney(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper);
    const TradeMoney& getOwnMoney() const { return m_own_money; }
    const TradeMoney& getPartnerMoney() const { return m_partner_money; }

    // Accept state
    void clickAccept();
    void resetAccept();
    bool isOwnAccepted() const { return m_own_accepted; }
    bool isPartnerAccepted() const { return m_partner_accepted; }

    // Packet handlers (called from EverQuest class)
    void handleTradeRequest(const EQT::TradeRequest_Struct& req, const std::string& fromName);
    void handleTradeRequestAck(const EQT::TradeRequestAck_Struct& ack);
    void handleTradeCoins(const EQT::TradeCoins_Struct& coins);
    void handleTradeAcceptClick(const EQT::TradeAcceptClick_Struct& accept);
    void handleFinishTrade(const EQT::FinishTrade_Struct& finish);
    void handleCancelTrade(const EQT::CancelTrade_Struct& cancel);
    void handlePartnerItemPacket(int slot, std::unique_ptr<eqt::inventory::ItemInstance> item);
    void handlePartnerItemRemoved(int slot);

    // UI callbacks
    void setOnStateChanged(TradeStateChangedCallback callback) { m_on_state_changed = callback; }
    void setOnRequestReceived(TradeRequestReceivedCallback callback) { m_on_request_received = callback; }
    void setOnItemUpdated(TradeItemUpdatedCallback callback) { m_on_item_updated = callback; }
    void setOnMoneyUpdated(TradeMoneyUpdatedCallback callback) { m_on_money_updated = callback; }
    void setOnAcceptStateChanged(TradeAcceptStateChangedCallback callback) { m_on_accept_state_changed = callback; }
    void setOnCompleted(TradeCompletedCallback callback) { m_on_completed = callback; }
    void setOnCancelled(TradeCancelledCallback callback) { m_on_cancelled = callback; }

    // Set inventory manager reference (needed for item validation)
    void setInventoryManager(eqt::inventory::InventoryManager* invManager) { m_inventory_manager = invManager; }

    // Set our spawn ID (called when zoning in)
    void setMySpawnId(uint16_t spawnId) { m_my_spawn_id = spawnId; }
    uint16_t getMySpawnId() const { return m_my_spawn_id; }

    // Network callbacks (for sending packets - set by EverQuest class)
    void setSendTradeRequest(SendTradeRequestCallback callback) { m_send_trade_request = callback; }
    void setSendTradeRequestAck(SendTradeRequestAckCallback callback) { m_send_trade_request_ack = callback; }
    void setSendMoveCoin(SendMoveCoinCallback callback) { m_send_move_coin = callback; }
    void setSendTradeAcceptClick(SendTradeAcceptClickCallback callback) { m_send_trade_accept_click = callback; }
    void setSendCancelTrade(SendCancelTradeCallback callback) { m_send_cancel_trade = callback; }

private:
    // Reset all trade state
    void resetTradeState();

    // Set state and notify
    void setState(TradeState newState);

    // Send packets to server (via callbacks)
    void sendTradeRequest(uint32_t targetSpawnId);
    void sendTradeRequestAck();
    void sendMoveCoin(int32_t coinType, int32_t amount);
    void sendTradeAcceptClick();
    void sendCancelTrade();

    // Our spawn ID
    uint16_t m_my_spawn_id = 0;

    // References
    eqt::inventory::InventoryManager* m_inventory_manager = nullptr;

    // Trade state
    TradeState m_state = TradeState::None;
    uint32_t m_partner_spawn_id = 0;
    std::string m_partner_name;
    bool m_is_npc_trade = false;

    // Items in trade (own items are references to inventory, partner items are copies)
    std::array<int16_t, TRADE_SLOT_COUNT> m_own_item_slots;  // Source inventory slots
    std::array<std::unique_ptr<eqt::inventory::ItemInstance>, TRADE_SLOT_COUNT> m_partner_items;

    // Money in trade
    TradeMoney m_own_money;
    TradeMoney m_partner_money;

    // Accept state
    bool m_own_accepted = false;
    bool m_partner_accepted = false;

    // Pending trade request (when we receive one)
    uint32_t m_pending_request_spawn_id = 0;
    std::string m_pending_request_name;

    // UI Callbacks
    TradeStateChangedCallback m_on_state_changed;
    TradeRequestReceivedCallback m_on_request_received;
    TradeItemUpdatedCallback m_on_item_updated;
    TradeMoneyUpdatedCallback m_on_money_updated;
    TradeAcceptStateChangedCallback m_on_accept_state_changed;
    TradeCompletedCallback m_on_completed;
    TradeCancelledCallback m_on_cancelled;

    // Network Callbacks
    SendTradeRequestCallback m_send_trade_request;
    SendTradeRequestAckCallback m_send_trade_request_ack;
    SendMoveCoinCallback m_send_move_coin;
    SendTradeAcceptClickCallback m_send_trade_accept_click;
    SendCancelTradeCallback m_send_cancel_trade;
};
