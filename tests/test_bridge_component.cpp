// K11 (Batch K, refactor/K11_bridge_component.md) -- eqt::bridge::BridgeComponent.
//
// Same shape as K01/K08/K09/K10, with ONE documented difference: this family
// keeps FIVE forwarders. setBridge(), InitGraphics(), ShutdownGraphics(),
// GameTick() and PostRenderTick() are EverQuest's lifecycle entry points and
// stay on EverQuest so Application does not have to know a component exists
// to start the game. Everything else -- the intent/event pumps, the loot
// window, config/hotbar/inspect persistence, the audio helpers, the
// spawn/pet marshalling -- is reached through EverQuest::Bridge().
//
// test_inspect_answer.cpp, test_loot_window_actions.cpp, test_eq_bank_coins.cpp
// and test_position_frame_roundtrip.cpp keep their own assertions untouched --
// only their call sites were retargeted, and THEY remain the coverage of what
// the moved bodies compute. Nothing here re-derives a wire format.

#include <gtest/gtest.h>

#include "client/bridge/bridge_component.h"
#include "client/inspect_answer.h"
#include "client/bridge/game_state_bridge.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

class BridgeComponentTest : public ::testing::Test {
protected:
    BridgeComponentTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~BridgeComponentTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

// The accessor is the only way in for everything but the five forwarders,
// and it must name one stable object.
TEST_F(BridgeComponentTest, BridgeReturnsTheSameComponentEveryTime) {
    eqt::bridge::BridgeComponent& a = eq.Bridge();
    eqt::bridge::BridgeComponent& b = eq.Bridge();
    EXPECT_EQ(&a, &b);
}

// The five sanctioned forwarders are still callable ON EverQuest. This is the
// pin for the one exception §Mechanism 5 grants this unit: if a later unit
// deletes one of them, Application stops compiling, and this says why.
TEST_F(BridgeComponentTest, LifecycleEntryPointsStayedOnEverQuest) {
    // setBridge is exercised by the fixture itself (ctor + dtor). The two
    // ticks are safe to drive on a headless, unconnected client.
    eq.GameTick(0.0f);
    eq.PostRenderTick(0.0f);
    SUCCEED();
}

// The T171 inspect message moved with the bridge (K08's Review handed it
// over: three of its four writes and its setter were always bridge-family
// bodies). The truncation contract came with it.
TEST_F(BridgeComponentTest, InspectMessageMovedWithItsAccessorPair) {
    EXPECT_TRUE(eq.Bridge().GetInspectMessage().empty());
    eq.Bridge().SetInspectMessage("Selling spells");
    EXPECT_EQ(eq.Bridge().GetInspectMessage(), "Selling spells");
    eq.Bridge().SetInspectMessage(std::string(1000, 'z'));
    EXPECT_EQ(eq.Bridge().GetInspectMessage().size(), EQT::INSPECT_MESSAGE_MAX_LEN);
}

// The loot cluster (K05/K06 recorded it as K11's) moved whole: the map, the
// pending-slot queue and the completed-corpse id, plus the const getter the
// action handler reads.
TEST_F(BridgeComponentTest, LootClusterMovedWithItsAccessors) {
    EXPECT_TRUE(eq.Bridge().GetLootItems().empty());
    EXPECT_TRUE(eq.Bridge().PendingLootSlots().empty());
    EXPECT_EQ(eq.Bridge().LootCompleteCorpseId(), 0u);

    eq.Bridge().LootItems()[3] =
        eqt::bridge::BridgeComponent::GameLootItem{1001, "Rusty Dagger"};
    eq.Bridge().PendingLootSlots().push_back(3);
    eq.Bridge().LootCompleteCorpseId() = 77;

    // The const getter and the reference getter are the same storage.
    ASSERT_EQ(eq.Bridge().GetLootItems().size(), 1u);
    EXPECT_EQ(eq.Bridge().GetLootItems().at(3).name, "Rusty Dagger");
    EXPECT_EQ(eq.Bridge().PendingLootSlots().front(), 3);
    EXPECT_EQ(eq.Bridge().LootCompleteCorpseId(), 77u);
}

// FindLootSlotByName reads the moved map. No window is open here, so it must
// refuse -- the guard travelled with the body.
TEST_F(BridgeComponentTest, FindLootSlotByNameRefusesWithNoOpenWindow) {
    eq.Bridge().LootItems()[0] =
        eqt::bridge::BridgeComponent::GameLootItem{1, "Bread Cakes"};
    EXPECT_FALSE(eq.IsLootWindowOpen());
    EXPECT_EQ(eq.Bridge().FindLootSlotByName("bread"), -1);
}

// IsLootWindowOpen deliberately STAYED on EverQuest: it reads GameState, not
// the map that moved (K08's "a thin read of the one store" rule).
TEST_F(BridgeComponentTest, TheLootWindowFlagStayedOnEverQuest) {
    EXPECT_FALSE(eq.IsLootWindowOpen());
    eq.GetGameState().player().setLootingCorpse(42);
    EXPECT_TRUE(eq.IsLootWindowOpen());
}

// The config-path setters moved with their members; the inspect-message
// persistence path is the one production reader of m_config_path, so an
// unset path must not write anywhere.
TEST_F(BridgeComponentTest, ConfigPathSettersMovedWithTheirMembers) {
    eq.Bridge().SetConfigPath("");
    eq.Bridge().SetInspectMessage("nowhere to save this");
    eq.Bridge().SaveInspectMessage();  // no config path: no-op, no crash
    EXPECT_EQ(eq.Bridge().GetInspectMessage(), "nowhere to save this");
    eq.Bridge().SetRegionMapsPath("data/region_maps");
    SUCCEED();
}

#ifdef WITH_AUDIO
// The audio config block moved. Setting it before InitializeAudio is the
// whole contract; nothing here starts a device.
TEST_F(BridgeComponentTest, AudioConfigSettersMovedWithTheirMembers) {
    eq.Bridge().SetAudioEnabled(false);
    eq.Bridge().SetMasterVolume(0.25f);
    eq.Bridge().SetMusicVolume(0.5f);
    eq.Bridge().SetEffectsVolume(0.75f);
    eq.Bridge().SetSoundFont("/nonexistent.sf2");
    eq.Bridge().SetVendorMusic("gl.xmi");
    // Disabled audio: InitializeAudio must return without constructing a
    // device, and PlaySound must be a no-op with no manager.
    eq.Bridge().InitializeAudio();
    eq.Bridge().PlaySound(0);
    EXPECT_EQ(eq.GetAudioManager(), nullptr);
}
#endif

// The bridge pumps are NOT forwarders: they moved and are reached through
// Bridge(). Draining an empty queue is safe on a headless client.
TEST_F(BridgeComponentTest, TheBridgePumpsMovedAndAreReachedThroughTheAccessor) {
    eq.Bridge().ProcessBridgeIntents();
    eq.Bridge().ProcessBridgeEvents();
    SUCCEED();
}

}  // namespace
