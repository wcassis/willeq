// T95: regression tests for the previously-unvalidated int->enum casts in
// EqActionHandler::setMovementMode() / setPositionState() / sendPetCommand().
//
// The defect (see refactor/T95_unvalidated_enum_casts.md): all three cast a
// caller-supplied int/uint8_t straight into an enum with no range check.
// MovementMode and PositionState (client/eq.h) are plain enums with NO fixed
// underlying type, so an out-of-range static_cast into them is undefined
// behaviour, not merely a wrong result. RecordingActionHandler
// (tests/action_test_fakes.h) records the raw int and never performs the
// cast, so no existing test could hit any of this -- the same shape as T90's
// setHeading gap, with worse consequences.
//
// Uses the same headless EverQuest + EqActionHandler construction as
// test_setheading_conversion.cpp (T90) / test_eq_action_handler_transforms.cpp
// (T93) -- no network, no graphics, no eqemu.
//
// Observability: EverQuest::SetMovementMode()/SetPositionState() have no
// public getters, but both push a PlayerMovementModeChanged/
// PlayerPositionStateChanged event through the bridge (only when the value
// actually changes from the current one -- eq_movement.cpp's no-op guard).
// A minimal concrete GameStateBridge subclass here just records what was
// pushed, giving a direct, non-tautological window into whether the real
// cast/setter ran and with which value.
//
// sendPetCommand()'s cast target (EQT::PetCommand) has a FIXED underlying
// type (uint32_t), so per the standard an out-of-range static_cast into it
// is not UB the way the other two are -- but it is still a caller-bug-
// hiding silent-garbage-to-the-wire defect, and the plan asks for the same
// reject-and-report treatment. EverQuest::SendPetCommand() itself is gated
// behind "connected to zone" + "has a pet" (no test seam exists for either
// without a new production touch outside this unit's territory -- the same
// shape T93 hit and declined to force for targetSelf()), so full local
// round-trip proof through to the wire isn't reachable headlessly. What IS
// provable, and what's tested here: EqActionHandler's own guard rejects
// invalid command bytes -- observable because the rejection path pushes its
// own distinct SystemMessage bridge event -- BEFORE EverQuest::SendPetCommand
// is ever entered, while valid enumerator bytes pass the guard silently
// (no rejection message) and proceed into EverQuest::SendPetCommand (which
// then no-ops harmlessly on the unrelated, pre-existing zone/pet gate).

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/pet_constants.h"

using namespace eqt;

namespace {

// Minimal concrete GameStateBridge: just records every event pushed to it,
// in order. GameStateBridge::applyEvent() is pure virtual and irrelevant
// here (nothing ever drains through it) -- only pushEvent()/drainEvents()
// (both non-virtual, defined on the base) are exercised.
class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// ---------------------------------------------------------------------
// setMovementMode()
// ---------------------------------------------------------------------

TEST(EqActionHandlerEnumGuards, SetMovementMode_ValidBoundaryLow_Run_ReachesRealSetter) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    // Default mode is already MOVE_MODE_RUN=0, so move away from it first
    // (WALK) to guarantee the eq.h no-op guard doesn't suppress the event,
    // then set RUN explicitly -- this is the low boundary of the enum.
    handler.setMovementMode(1);  // MOVE_MODE_WALK
    bridge.drainEvents();

    handler.setMovementMode(0);  // MOVE_MODE_RUN -- low boundary

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::PlayerMovementModeChanged);
    EXPECT_EQ(std::get<eqt::state::PlayerMovementModeChangedData>(events[0].data).mode, 0);
}

TEST(EqActionHandlerEnumGuards, SetMovementMode_ValidBoundaryHigh_Sneak_ReachesRealSetter) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.setMovementMode(2);  // MOVE_MODE_SNEAK -- high boundary

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::PlayerMovementModeChanged);
    EXPECT_EQ(std::get<eqt::state::PlayerMovementModeChangedData>(events[0].data).mode, 2);
}

TEST(EqActionHandlerEnumGuards, SetMovementMode_NegativeOutOfRange_RejectedNoEventPushed) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.setMovementMode(-1);

    auto events = bridge.drainEvents();
    // If the guard didn't fire, the cast would run (UB) and, if it happened
    // to "work" on this platform, SetMovementMode would push a
    // PlayerMovementModeChanged event (mode differs from the default RUN).
    // The guard itself also reports via AddChatSystemMessage() (a
    // SystemMessage event) -- expected and asserted separately below -- so
    // the discriminator is specifically the ABSENCE of
    // PlayerMovementModeChanged, not the absence of every event.
    for (auto& e : events) {
        EXPECT_NE(e.type, eqt::state::GameEventType::PlayerMovementModeChanged)
            << "the real setter (and therefore the unvalidated cast) ran "
               "for an out-of-range mode";
    }
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SystemMessage);
}

TEST(EqActionHandlerEnumGuards, SetMovementMode_JustPastHighEnd_RejectedNoEventPushed) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.setMovementMode(3);  // one past MOVE_MODE_SNEAK=2

    auto events = bridge.drainEvents();
    for (auto& e : events) {
        EXPECT_NE(e.type, eqt::state::GameEventType::PlayerMovementModeChanged);
    }
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SystemMessage);
}

TEST(EqActionHandlerEnumGuards, SetMovementMode_WayOutOfRange_RejectedNoEventPushed) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.setMovementMode(9999);

    auto events = bridge.drainEvents();
    for (auto& e : events) {
        EXPECT_NE(e.type, eqt::state::GameEventType::PlayerMovementModeChanged);
    }
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SystemMessage);
}

// ---------------------------------------------------------------------
// setPositionState()
// ---------------------------------------------------------------------

TEST(EqActionHandlerEnumGuards, SetPositionState_ValidBoundaryLow_Standing_ReachesRealSetter) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    // Default is already POS_STANDING=0; move away first so setting it back
    // isn't suppressed by eq.h's no-op guard.
    handler.setPositionState(1);  // POS_SITTING
    bridge.drainEvents();

    handler.setPositionState(0);  // POS_STANDING -- low boundary

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::PlayerPositionStateChanged);
    EXPECT_EQ(std::get<eqt::state::PlayerPositionStateChangedData>(events[0].data).state, 0);
}

TEST(EqActionHandlerEnumGuards, SetPositionState_ValidBoundaryHigh_Dead_ReachesRealSetter) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.setPositionState(4);  // POS_DEAD -- high boundary

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::PlayerPositionStateChanged);
    EXPECT_EQ(std::get<eqt::state::PlayerPositionStateChangedData>(events[0].data).state, 4);
}

TEST(EqActionHandlerEnumGuards, SetPositionState_NegativeOutOfRange_RejectedNoEventPushed) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.setPositionState(-1);

    auto events = bridge.drainEvents();
    for (auto& e : events) {
        EXPECT_NE(e.type, eqt::state::GameEventType::PlayerPositionStateChanged)
            << "the real setter (and therefore the unvalidated cast) ran "
               "for an out-of-range state";
    }
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SystemMessage);
}

TEST(EqActionHandlerEnumGuards, SetPositionState_JustPastHighEnd_RejectedNoEventPushed) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.setPositionState(5);  // one past POS_DEAD=4

    auto events = bridge.drainEvents();
    for (auto& e : events) {
        EXPECT_NE(e.type, eqt::state::GameEventType::PlayerPositionStateChanged);
    }
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SystemMessage);
}

// ---------------------------------------------------------------------
// sendPetCommand()
//
// EQT::PetCommand's valid set is NON-CONTIGUOUS (gaps at 0, 15, 22+), so
// boundary coverage here means both ends of the numeric range AND the
// interior gaps, not just min/max.
// ---------------------------------------------------------------------

TEST(EqActionHandlerEnumGuards, SendPetCommand_ValidLowEnumerator_NoRejectionMessage) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.sendPetCommand(static_cast<uint8_t>(EQT::PET_BACKOFF), 0);  // = 1, lowest valid

    auto events = bridge.drainEvents();
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            const auto& msg = std::get<eqt::state::ChatMessageData>(e.data).message;
            EXPECT_EQ(msg.find("is not valid"), std::string::npos)
                << "valid command PET_BACKOFF(1) was wrongly rejected: " << msg;
        }
    }
}

TEST(EqActionHandlerEnumGuards, SendPetCommand_ValidHighEnumerator_NoRejectionMessage) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.sendPetCommand(static_cast<uint8_t>(EQT::PET_HOLD_OFF), 0);  // = 21, highest valid

    auto events = bridge.drainEvents();
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            const auto& msg = std::get<eqt::state::ChatMessageData>(e.data).message;
            EXPECT_EQ(msg.find("is not valid"), std::string::npos)
                << "valid command PET_HOLD_OFF(21) was wrongly rejected: " << msg;
        }
    }
}

TEST(EqActionHandlerEnumGuards, SendPetCommand_ZeroBelowRange_RejectedBeforeCast) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.sendPetCommand(0, 0);  // below the lowest enumerator (1)

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u)
        << "expected exactly the guard's own rejection SystemMessage; "
           "EverQuest::SendPetCommand()'s own not-connected path only "
           "LOG_WARNs and pushes nothing, so any event here proves the "
           "guard, not a fallthrough";
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SystemMessage);
    const auto& msg = std::get<eqt::state::ChatMessageData>(events[0].data).message;
    EXPECT_NE(msg.find("is not valid"), std::string::npos) << msg;
    EXPECT_NE(msg.find("0"), std::string::npos) << msg;
}

TEST(EqActionHandlerEnumGuards, SendPetCommand_22AboveRange_RejectedBeforeCast) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.sendPetCommand(22, 0);  // one past the highest enumerator (21)

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SystemMessage);
    const auto& msg = std::get<eqt::state::ChatMessageData>(events[0].data).message;
    EXPECT_NE(msg.find("is not valid"), std::string::npos) << msg;
}

TEST(EqActionHandlerEnumGuards, SendPetCommand_255WayAboveRange_RejectedBeforeCast) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.sendPetCommand(255, 0);  // max uint8_t

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SystemMessage);
}

TEST(EqActionHandlerEnumGuards, SendPetCommand_15InteriorGap_RejectedBeforeCast) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    // 15 sits inside the numeric 1..21 span but has no enumerator (per
    // pet_constants.h: "15 = target command (doesn't send packet)") -- this
    // is the case a naive min/max range check would wrongly accept, which
    // is exactly why PetCommand needs membership checking, not a range
    // check.
    handler.sendPetCommand(15, 0);

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u)
        << "15 is numerically inside 1..21 but has no PetCommand enumerator; "
           "a min/max range check (wrong for this enum) would have let it "
           "through silently";
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SystemMessage);
    const auto& msg = std::get<eqt::state::ChatMessageData>(events[0].data).message;
    EXPECT_NE(msg.find("is not valid"), std::string::npos) << msg;
}

}  // namespace
