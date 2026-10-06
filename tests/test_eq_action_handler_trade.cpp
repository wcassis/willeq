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
    EXPECT_EQ(lastSystemMessage(events), "Declined trade request from Buddy.");
}

TEST(EqActionHandlerTrade, DeclineTrade_ClearsProjectionViaCancelledCallback) {
    // T103 finding, fixed in T222: TradeManager::rejectTradeRequest() used
    // to be the one trade-state transition that never invoked
    // m_on_cancelled(), so a local decline published no TradeCancelled
    // bridge event and EqActionHandler::declineTrade() had to reset the
    // projection itself. That workaround is gone; the projection reset AND
    // the TradeCancelled event now both come from the callback. Without the
    // trade_manager.cpp fix, both assertions below fail.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    simulateIncomingRequest(eq, 555, "Buddy");
    ASSERT_TRUE(eq.GetGameState().trade().hasPendingRequest());
    bridge.drainEvents();

    handler.declineTrade();

    EXPECT_FALSE(eq.GetGameState().trade().hasPendingRequest());
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::None);

    bool sawCancelled = false;
    std::vector<std::string> messages;
    for (auto& e : bridge.drainEvents()) {
        if (e.type == eqt::state::GameEventType::TradeCancelled) sawCancelled = true;
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            messages.push_back(std::get<eqt::state::ChatMessageData>(e.data).message);
        }
    }
    EXPECT_TRUE(sawCancelled);
    // T222 follow-up: exactly ONE chat line per decline, printed by the
    // shared setOnCancelled lambda from TradeEndReason::DeclinedLocal.
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0], "Declined trade request from Buddy.");
}

TEST(EqActionHandlerTrade, TradeManager_RejectTradeRequest_FiresOnCancelled) {
    // T222: the TradeManager-level contract, independent of EqActionHandler.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);

    int fired = 0;
    TradeEndReason lastReason = TradeEndReason::CancelledLocal;
    std::string lastName;
    eq.GetTradeManager()->setOnCancelled([&](TradeEndReason r, const std::string& n) {
        ++fired; lastReason = r; lastName = n;
    });

    simulateIncomingRequest(eq, 555, "Buddy");
    ASSERT_EQ(eq.GetTradeManager()->getState(), TradeState::PendingAccept);

    eq.GetTradeManager()->rejectTradeRequest();
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(lastReason, TradeEndReason::DeclinedLocal);
    EXPECT_EQ(lastName, "Buddy");
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::None);

    // Not in PendingAccept: no-op, no callback.
    eq.GetTradeManager()->rejectTradeRequest();
    EXPECT_EQ(fired, 1);
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

    // B896 (#212): exactly ONE line, the oracle's eqstr 1450 (MEASURED,
    // review/UIGAP-ORACLE/212/trade.tsv row 8) -- fable used to print its own
    // "Trade cancelled." from the callback AND "Trade cancelled" from here.
    auto events = bridge.drainEvents();
    int lines = 0;
    for (const auto& e : events) {
        if (e.type == eqt::state::GameEventType::SystemMessage) ++lines;
    }
    EXPECT_EQ(lines, 1);
    EXPECT_EQ(lastSystemMessage(events), "You have cancelled the trade.");
    EXPECT_FALSE(eq.GetGameState().trade().active());
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::None);
}

// ---------------------------------------------------------------------
// T259: /accept during an ACTIVE trade is the trade window's Accept
// button. TradeManager::clickAccept() was reachable only through
// TradeAcceptIntent, which had NO producer anywhere (eq_bridge.cpp consumes
// it; nothing pushes it), so a trade could never be completed from a
// scripted/headless client -- and the live leg needs exactly that.
// ---------------------------------------------------------------------

TEST(EqActionHandlerTrade, AcceptTrade_ActiveTrade_ClicksAcceptInsteadOfRefusing) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    simulateIncomingRequest(eq, 555, "Buddy");
    handler.acceptTrade();
    ASSERT_TRUE(eq.GetGameState().trade().active());
    ASSERT_FALSE(eq.GetTradeManager()->isOwnAccepted());
    bridge.drainEvents();

    handler.acceptTrade();

    EXPECT_TRUE(eq.GetTradeManager()->isOwnAccepted());
    EXPECT_TRUE(eq.GetGameState().trade().ownAccepted());
    const std::string msg = lastSystemMessage(bridge.drainEvents());
    EXPECT_EQ(msg.find("No pending trade request"), std::string::npos) << msg;
    EXPECT_NE(msg.find("Buddy"), std::string::npos) << msg;
}
