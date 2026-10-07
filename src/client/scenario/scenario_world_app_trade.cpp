// UIGAP-live2 (#212 #151 #26): the trade / give, drop / destroy and coin
// observables and actions, over the real client. Contracts in scenario.h.
//
// Every action pushes the SAME renderer intent the window or the world click
// pushes (TargetIntent, TradeWindowIntent, CursorGestureIntent), so what the
// game thread runs is what a real click runs -- the scenario never calls a
// manager directly.

#include "client/scenario/scenario_world_app.h"

#include "client/application.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/entities/entities_component.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/state/game_state.h"
#include "client/trade_manager.h"

#include <limits>

namespace eqt {
namespace scenario {

namespace {

// inventory.slot_id numbering (scenario.h): general 23-30, each general
// slot's bag contents 4010 + (g - 23) * 200 + n, cursor 33, trade 3000-3007.
constexpr int kGeneralFirst = 23;
constexpr int kGeneralLast = 30;
constexpr int kServerBagBase = 4010;
constexpr int kServerBagStride = 200;
constexpr int kBagContentsMax = 10;  // Titanium's largest bag
constexpr int kCursor = 33;
constexpr int kTradeFirst = 3000;
constexpr int kTradeLast = 3007;

long long ItemIdAtServer(eqt::inventory::InventoryManager& inv, int serverSlot) {
    if (serverSlot < 0 || serverSlot > std::numeric_limits<int16_t>::max()) return 0;
    const int16_t model = eqt::inventory::serverSlotToClient(static_cast<int16_t>(serverSlot));
    if (model == eqt::inventory::SLOT_INVALID) return 0;
    const auto* item = model == eqt::inventory::CURSOR_SLOT ? inv.getCursorItem() : inv.getItem(model);
    return item ? static_cast<long long>(item->itemId) : 0;
}

}  // namespace

int ApplicationScenarioWorld::TradeFlags() const {
    auto* trade = client_.GetTradeManager();
    if (!trade) return -1;
    int flags = 0;
    if (trade->isTrading()) flags |= kTradeActive;
    if (trade->isOwnAccepted()) flags |= kTradeOwnAccepted;
    if (trade->isPartnerAccepted()) flags |= kTradePartnerAccepted;
    bool offered = trade->getPartnerMoney().toCopper() > 0;
    for (int s = 0; s < TRADE_SLOT_COUNT && !offered; ++s) offered = trade->hasPartnerItem(s);
    if (offered) flags |= kTradePartnerOffered;
    if (trade->isNpcTrade()) flags |= kTradeWithNpc;
    return flags;
}

long long ApplicationScenarioWorld::FindItemSlot(long long itemId) const {
    auto* inv = client_.GetInventoryManager();
    if (!inv) return -1;
    for (int g = kGeneralFirst; g <= kGeneralLast; ++g) {
        if (ItemIdAtServer(*inv, g) == itemId) return g;
        for (int n = 0; n < kBagContentsMax; ++n) {
            const int s = kServerBagBase + (g - kGeneralFirst) * kServerBagStride + n;
            if (ItemIdAtServer(*inv, s) == itemId) return s;
        }
    }
    if (ItemIdAtServer(*inv, kCursor) == itemId) return kCursor;
    for (int s = kTradeFirst; s <= kTradeLast; ++s) {
        if (ItemIdAtServer(*inv, s) == itemId) return s;
    }
    return 0;
}

std::vector<long long> ApplicationScenarioWorld::Coins(bool bank) const {
    const auto& p = client_.GetGameState().player();
    if (bank) {
        return {static_cast<long long>(p.bankPlatinum()), static_cast<long long>(p.bankGold()),
                static_cast<long long>(p.bankSilver()), static_cast<long long>(p.bankCopper())};
    }
    return {static_cast<long long>(p.platinum()), static_cast<long long>(p.gold()),
            static_cast<long long>(p.silver()), static_cast<long long>(p.copper())};
}

long long ApplicationScenarioWorld::CursorCoinCopper() const {
    auto* inv = client_.GetInventoryManager();
    if (!inv) return -1;
    if (!inv->hasCursorMoney()) return 0;
    using MT = eqt::inventory::InventoryManager::CursorMoneyType;
    long long unit = 1;
    switch (inv->getCursorMoneyType()) {
        case MT::Platinum: unit = 1000; break;
        case MT::Gold: unit = 100; break;
        case MT::Silver: unit = 10; break;
        default: unit = 1; break;
    }
    return unit * static_cast<long long>(inv->getCursorMoneyAmount());
}

long long ApplicationScenarioWorld::WorldObjectCount() const {
    return static_cast<long long>(client_.Entities().GetWorldObjects().size());
}

bool ApplicationScenarioWorld::ClickEntity(const std::string& name) {
    if (!bridge_ || name.empty()) return false;
    const auto& player = client_.GetGameState().player();
    const auto* entity =
        client_.GetGameState().entities().findEntityByName(name, player.x(), player.y(), player.z());
    if (!entity) return false;
    bridge_->pushIntent(eqt::events::TargetIntent{static_cast<uint16_t>(entity->spawnId)});
    return true;
}

bool ApplicationScenarioWorld::TradeButton(int op) {
    if (!bridge_ || op < 0 || op > 2) return false;
    eqt::events::TradeWindowIntent intent;
    intent.op = static_cast<eqt::events::TradeWindowOp>(op);
    bridge_->pushIntent(intent);
    return true;
}

bool ApplicationScenarioWorld::CursorGesture(int op, int coinType, int quantity, int coinSource) {
    using Op = eqt::events::CursorGestureOp;
    if (!bridge_ || op < static_cast<int>(Op::CoinClick) || op > static_cast<int>(Op::ConfirmDestroy) ||
        op == static_cast<int>(Op::ToggleBag)) {
        return false;
    }
    eqt::events::CursorGestureIntent intent;
    intent.op = static_cast<Op>(op);
    intent.coinType = static_cast<uint8_t>(coinType);
    intent.quantity = quantity;
    intent.coinSource = static_cast<uint8_t>(coinSource);
    bridge_->pushIntent(intent);
    return true;
}

}  // namespace scenario
}  // namespace eqt
