// B1027 (#347): headless ConsoleRenderer target HP stays frozen -- the
// EntityStatsChanged that the state EventBus feeds it has no publisher.
//
// THE DEFECT. ConsoleRenderer (src/client/output/console_renderer.cpp:113,
// 193) takes the current target's HP off GameEventType::EntityStatsChanged
// on the STATE EventBus (state.events(), the bus a headless run has). The
// only src publisher of that enumerator was the RENDERER-BRIDGE pushEvent in
// EntitiesComponent::ZoneProcessMobHealth (entities_component_appearance.cpp
// 200-207) -- a channel that, on a headless run, is drained into
// ConsoleBridge (LOG lines) and never reaches the state bus; combat_state.cpp
// states nothing forwards between the two channels. B602 deleted the one
// state-bus publisher (EntityManager::fireEntityStatsChangedEvent) and
// claimed the bridge path fed the ConsoleRenderer -- false. Result: after a
// target was acquired its status-line HP froze at the value TargetChanged
// happened to carry (T319's original bug, still live).
//
// THE FIX, PINNED HERE. ZoneProcessMobHealth now publishes
// EntityStatsChanged on the state bus (ctx_.game_state.events(),
// entities_component_appearance.cpp), alongside the unchanged bridge
// publish. Exactly one state-bus publisher of the enumerator exists
// (measured by grep in refactor/B1027/README.md), so these two tests --
// the wire-to-ConsoleRenderer leg and the state-bus census -- are the net:
// deleting the state-bus publish again is a red ctest on both.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "client/combat.h"
#include "client/eq_opcodes.h"
#include "client/output/console_renderer.h"
#include "client/state/event_bus.h"
#include "client/state/game_state.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

std::vector<uint8_t> MobHealthPayload(uint16_t spawnId, uint8_t hp) {
    // ZoneProcessMobHealth's read (entities_component_appearance.cpp:
    // 173-174): spawn_id at packet offset 2, hp_percent at 4; guard is
    // Length() >= 5. Same shape test_zone_entities.cpp builds.
    return BuildPayload(5, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawnId);
        p.PutUInt8(4, hp);
    });
}

// One status-line print, captured.
std::string StatusLine(eqt::output::ConsoleRenderer& console) {
    ::testing::internal::CaptureStdout();
    console.update(1.0f);
    return ::testing::internal::GetCapturedStdout();
}

// =========================================================================
// 1. The wire to the console -- the exact headless route a player lives
//    in: OP_MobHealth replayed through DispatchZonePacket (the same entry
//    ZoneOnPacketRecv uses), the real CombatManager's target write, and
//    the real ConsoleRenderer reading its status line off the state bus.
//
//    THE FIX (step 2 of the card): ZoneProcessMobHealth (entities_component_
//    appearance.cpp) now publishes EntityStatsChanged on the state bus
//    (ctx_.game_state.events()) right above its unchanged bridge pushEvent --
//    the one state-bus publisher of the enumerator (measured: grep -rn
//    "GameEventType::EntityStatsChanged" src include, the only occurrence
//    outside the two consumers' case/subscribe lines and this test).
//    Routing it through CombatState was ruled out: EntityStatsChanged is a
//    whole-entity event (the payload is spawnId + hp), not target state, and
//    the established pattern here is the T221 "source-side writes" already
//    in this handler (combat().setTarget / pet().updatePetStats, lines 188-
//    196). combat_state.cpp:76-84's two-channel comment is about
//    TargetChanged, which still flows CombatManager -> CombatState -> bus.
// =========================================================================

class ConsoleTargetHpFeedTest : public ::testing::Test {
protected:
    ConsoleTargetHpFeedTest() : eq(MakeEq()) {
        console.connectToGameState(eq.GetGameState());
        console.setStatusUpdateInterval(0.0f);  // every update() prints
        console.setZoneReady(true);
    }
    ~ConsoleTargetHpFeedTest() override {
        eq.setBridge(nullptr);
    }

    void ZoneInAndTarget(uint16_t spawnId, const std::string& name) {
        ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
                                   NewSpawnPayloadAt(spawnId, name, 10, 10, 0,
                                                     /*level=*/10,
                                                     /*npc_type=*/1,
                                                     /*char_class=*/1,
                                                     /*hp_percent=*/100)));
        ASSERT_TRUE(eq.GetCombatManager()->SetTarget(spawnId));
        ASSERT_EQ(eq.GetGameState().combat().targetId(), spawnId);
    }

    EverQuest eq;
    TestBridge bridge;
    eqt::output::ConsoleRenderer console;
};

// RED at origin/fable: nothing publishes EntityStatsChanged on the state
// bus in a headless run, so the status line freezes at the 100% the
// TargetChanged carried when the target was acquired -- the mob was hit to
// 37% and the console still says 100%.
TEST_F(ConsoleTargetHpFeedTest, StatusLineFollowsTheTargetsHpDownAfterMobHealth) {
    ZoneInAndTarget(42, "Grimjaw");
    eq.setBridge(&bridge);
    bridge.drainEvents();  // discard target-time events

    ASSERT_NE(StatusLine(console).find("Target: Grimjaw 100%"),
              std::string::npos);

    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_MobHealth, MobHealthPayload(42, 37)));

    EXPECT_NE(StatusLine(console).find("Target: Grimjaw 37%"),
              std::string::npos)
        << "The status line never learned the new HP: EntityStatsChanged has "
           "no publisher on the state EventBus (the MobHealth bridge publish "
           "feeds the render-thread bridge, not the bus ConsoleRenderer "
           "subscribes to).";
}

// =========================================================================
// 2. The state-bus census: the MobHealth entry publishes
//    EntityStatsChanged on the state bus exactly once, with the MobHealth
//    payload. "Exactly one" is the acceptance rule -- a second publish on
//    this bus is a duplicate on the same bus and fails here.
// =========================================================================

TEST_F(ConsoleTargetHpFeedTest, MobHealthPublishesEntityStatsChangedOnTheStateBusExactlyOnce) {
    ZoneInAndTarget(42, "Grimjaw");
    eq.setBridge(&bridge);
    bridge.drainEvents();

    eqt::state::GameState& state = eq.GetGameState();
    size_t seen = 0;
    uint16_t seenSpawn = 0;
    uint8_t seenHp = 0;
    auto handle = state.events().subscribe(
        eqt::state::GameEventType::EntityStatsChanged,
        [&](const eqt::state::GameEvent& ev) {
            ++seen;
            const auto& d =
                std::get<eqt::state::EntityStatsChangedData>(ev.data);
            seenSpawn = d.spawnId;
            seenHp = d.hpPercent;
        });

    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_MobHealth, MobHealthPayload(42, 37)));

    state.events().unsubscribe(handle);
    ASSERT_EQ(seen, 1u)
        << "expected EXACTLY ONE state-bus EntityStatsChanged per MobHealth "
           "packet (the bridge publish is a different channel and does not "
           "count); got " << seen;
    EXPECT_EQ(seenSpawn, 42u);
    EXPECT_EQ(seenHp, 37u);
}

}  // namespace
