// T103: regression tests proving EqActionHandler's trade methods
// (requestTrade/acceptTrade/cancelTrade/declineTrade) now read their
// precondition/display state from state::TradeState (the projection this
// unit added to state::GameState) rather than reaching into TradeManager
// directly -- the layering compromise T102 recorded honestly rather than
// hiding (see refactor/T102_migrate_combat_social.md's Review, "GameState-
// SyncBridge coverage" section).
//
// Uses the same headless EverQuest + EqActionHandler construction as
// test_eq_action_handler_enum_guards.cpp (T95): no network, no graphics, no
// eqemu. TradeManager's Send*() calls all guard on `!m_zone_connection` and
// no-op harmlessly in this configuration (verified by reading eq_merchant.cpp
// before writing these tests) -- the client-side state machine
// (TradeManager::handleTradeRequest()/acceptTradeRequest()/
// rejectTradeRequest()/cancelTrade()) runs regardless, which is what these
// tests exercise.
//
// Observability: EqActionHandler's chat responses go through
// AddChatSystemMessage(), which pushes a SystemMessage bridge event
// (ChatMessageData) -- a minimal concrete GameStateBridge subclass records
// every event pushed, the same pattern test_eq_action_handler_enum_guards.cpp
// uses.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/trade_manager.h"
#include "common/packet_structs.h"

using namespace eqt;

namespace {

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// Returns the message text of the last SystemMessage event pushed, or empty
// if none.
std::string lastSystemMessage(const std::vector<eqt::state::GameEvent>& events) {
    std::string last;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            last = std::get<eqt::state::ChatMessageData>(e.data).message;
        }
    }
    return last;
}

// Drives TradeManager into PendingAccept (an incoming request from spawn
// 555, "Buddy") the same way a real TradeRequest packet handler would
// (EqActionHandler has no direct way to synthesize this itself -- it's
// what the *other* side's /trade produces).
void simulateIncomingRequest(EverQuest& eq, uint32_t fromSpawnId, const std::string& fromName) {
    EQT::TradeRequest_Struct req;
    req.target_spawn_id = 0;
    req.from_spawn_id = fromSpawnId;
    eq.GetTradeManager()->handleTradeRequest(req, fromName);
}

} // namespace

// ---------------------------------------------------------------------
// acceptTrade()
// ---------------------------------------------------------------------

TEST(EqActionHandlerTrade, AcceptTrade_NoPendingRequest_FailsCleanlyWithoutCallingTradeManager) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    ASSERT_FALSE(eq.GetGameState().trade().hasPendingRequest());

    handler.acceptTrade();

    auto events = bridge.drainEvents();
    EXPECT_EQ(lastSystemMessage(events), "No pending trade request");
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::None);
}

TEST(EqActionHandlerTrade, AcceptTrade_PendingRequest_ProjectionShowsPendingRequestWithPartnerName) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);

    simulateIncomingRequest(eq, 555, "Buddy");

    EXPECT_TRUE(eq.GetGameState().trade().hasPendingRequest());
    EXPECT_EQ(eq.GetGameState().trade().pendingPartnerId(), 555u);
    EXPECT_EQ(eq.GetGameState().trade().pendingPartnerName(), "Buddy");
}

TEST(EqActionHandlerTrade, AcceptTrade_PendingRequest_AcceptsAndProjectionReflectsActiveTrade) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    simulateIncomingRequest(eq, 555, "Buddy");
    bridge.drainEvents();  // discard the TradeRequestReceived bridge event

    handler.acceptTrade();

    auto events = bridge.drainEvents();
    const std::string msg = lastSystemMessage(events);
    EXPECT_NE(msg.find("Buddy"), std::string::npos)
        << "acceptTrade()'s message should name the partner: " << msg;
    EXPECT_EQ(msg, "Accepting trade with Buddy");

    EXPECT_TRUE(eq.GetGameState().trade().active());
    EXPECT_EQ(eq.GetGameState().trade().partnerName(), "Buddy");
    EXPECT_FALSE(eq.GetGameState().trade().hasPendingRequest())
        << "accepting should clear the now-stale pending-request entry";
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::Active);
}

// ---------------------------------------------------------------------
// declineTrade() -- T103's headline ask: can it now name the partner?
// ---------------------------------------------------------------------

TEST(EqActionHandlerTrade, DeclineTrade_NoPendingRequest_FailsCleanlyWithoutCallingTradeManager) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.declineTrade();

    auto events = bridge.drainEvents();
    EXPECT_EQ(lastSystemMessage(events), "No pending trade request");
}

TEST(EqActionHandlerTrade, DeclineTrade_PendingRequest_MessageNamesThePartner) {
    // T103's headline finding: T102 had to leave this message name-free
    // because TradeManager exposed no accessor for the pending request's
    // name on the decline path. With the projection (populated from the
    // TradeRequestReceived event fired when the request arrived,
    // independent of that gap) it can.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    simulateIncomingRequest(eq, 555, "Buddy");
    bridge.drainEvents();

    handler.declineTrade();

    auto events = bridge.drainEvents();
    EXPECT_EQ(lastSystemMessage(events), "Declined trade request from Buddy");
}

TEST(EqActionHandlerTrade, DeclineTrade_ClearsProjectionDespiteTradeManagerNotFiringCancelledCallback) {
    // T103 finding: TradeManager::rejectTradeRequest() (trade_manager.cpp)
    // never invokes m_on_cancelled(), unlike cancelTrade()/
    // handleCancelTrade() -- so no TradeCancelled bridge event is published
    // for a local decline. Without EqActionHandler::declineTrade()'s
    // explicit projection reset (added because trade_manager.cpp is outside
    // this unit's territory and could not be fixed directly), this
    // assertion would fail: hasPendingRequest() would stay stuck true.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    simulateIncomingRequest(eq, 555, "Buddy");
    ASSERT_TRUE(eq.GetGameState().trade().hasPendingRequest());

    handler.declineTrade();

    EXPECT_FALSE(eq.GetGameState().trade().hasPendingRequest());
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::None);
}

// ---------------------------------------------------------------------
// requestTrade() / cancelTrade()
// ---------------------------------------------------------------------

TEST(EqActionHandlerTrade, RequestTrade_AlreadyActive_FailsCleanlyUsingProjection) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    simulateIncomingRequest(eq, 555, "Buddy");
    handler.acceptTrade();
    ASSERT_TRUE(eq.GetGameState().trade().active());
    bridge.drainEvents();

    handler.requestTrade(999);

    auto events = bridge.drainEvents();
    EXPECT_EQ(lastSystemMessage(events), "You are already trading");
    // Partner should be unchanged -- no second request was dispatched.
    EXPECT_EQ(eq.GetGameState().trade().partnerName(), "Buddy");
}

TEST(EqActionHandlerTrade, CancelTrade_NotTrading_FailsCleanlyUsingProjection) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    ASSERT_FALSE(eq.GetGameState().trade().active());

    handler.cancelTrade();

    auto events = bridge.drainEvents();
    EXPECT_EQ(lastSystemMessage(events), "You are not trading");
}

TEST(EqActionHandlerTrade, CancelTrade_ActiveTrade_ResetsProjection) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    simulateIncomingRequest(eq, 555, "Buddy");
    handler.acceptTrade();
    ASSERT_TRUE(eq.GetGameState().trade().active());
    bridge.drainEvents();

    handler.cancelTrade();

    auto events = bridge.drainEvents();
    EXPECT_EQ(lastSystemMessage(events), "Trade cancelled");
    EXPECT_FALSE(eq.GetGameState().trade().active());
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::None);
}
