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
#include "client/hotbar/hotbar_model.h"
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

// The loot cluster (K05/K06 recorded it as K11's) moved whole: the map and
// the pending-slot queue, plus the const getter the action handler reads.
// (The completed-corpse id that moved with them was deleted by B1133: it fed
// only the corpse DeleteSpawn gate.)
TEST_F(BridgeComponentTest, LootClusterMovedWithItsAccessors) {
    EXPECT_TRUE(eq.Bridge().GetLootItems().empty());
    EXPECT_TRUE(eq.Bridge().PendingLootSlots().empty());

    eq.Bridge().LootItems()[3] =
        eqt::bridge::BridgeComponent::GameLootItem{1001, "Rusty Dagger"};
    eq.Bridge().PendingLootSlots().push_back(3);

    // The const getter and the reference getter are the same storage.
    ASSERT_EQ(eq.Bridge().GetLootItems().size(), 1u);
    EXPECT_EQ(eq.Bridge().GetLootItems().at(3).name, "Rusty Dagger");
    EXPECT_EQ(eq.Bridge().PendingLootSlots().front(), 3);
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

// B1025-H2: the stock window's page buttons push HotbarPageChangedIntent;
// the pump routes it to the model's setCurrentPage(), which clamps and
// rejects out-of-range pages.
TEST_F(BridgeComponentTest, HotbarPageChangedIntentMovesTheModelPage) {
    ASSERT_NE(eq.GetHotbarModel(), nullptr);
    EXPECT_EQ(eq.GetHotbarModel()->currentPage(), 0);

    bridge.pushIntent(eqt::events::HotbarPageChangedIntent{4});
    eq.Bridge().ProcessBridgeIntents();
    EXPECT_EQ(eq.GetHotbarModel()->currentPage(), 4);

    // Out-of-range pages are the model's to refuse, not the intent's.
    bridge.pushIntent(eqt::events::HotbarPageChangedIntent{77});
    eq.Bridge().ProcessBridgeIntents();
    EXPECT_EQ(eq.GetHotbarModel()->currentPage(), 4);
}

// B1025-H3 (GitLab #345): a placed hot button (the hold gesture's, or
// /hotbutton's) assigns a slot -- the first PRODUCTION caller of
// HotbarModel::assignSlot(). The intent already carries the destination and
// the button in the model's own numbering.
TEST_F(BridgeComponentTest, HotbarAssignIntentAssignsTheModelSlot) {
    auto* model = eq.GetHotbarModel();
    ASSERT_NE(model, nullptr);

    bridge.pushIntent(eqt::events::HotbarAssignIntent{
        2, 5, eqt::HotbarSlotType::Emote, 0, "/say hi", 0});
    eq.Bridge().ProcessBridgeIntents();

    const auto slot = model->getSlot({2, 5});
    EXPECT_EQ(slot.type, eqt::HotbarSlotType::Emote);
    EXPECT_EQ(slot.id, 0u);
    EXPECT_EQ(slot.emoteText, "/say hi");
    EXPECT_EQ(slot.name, "/say hi");  // an Emote is named by its text
}

// B1025-H3 (GitLab #345): the pickup half -- a hold on an occupied slot
// cleared its source (H-O "hotbar-pickup": "source INI key removed"). The
// intent carries only the address; the clear is the model's.
TEST_F(BridgeComponentTest, HotbarClearIntentClearsTheModelSlot) {
    auto* model = eq.GetHotbarModel();
    ASSERT_NE(model, nullptr);
    model->assignSlot({1, 3}, eqt::HotbarSlotType::Emote, 0, "/wave", 0);
    ASSERT_EQ(model->getSlot({1, 3}).type, eqt::HotbarSlotType::Emote);
    bridge.drainEvents();  // the assign's publish is the test's own, not the clear's

    bridge.pushIntent(eqt::events::HotbarClearIntent{1, 3});
    eq.Bridge().ProcessBridgeIntents();
    bridge.drainEvents();

    EXPECT_EQ(model->getSlot({1, 3}).type, eqt::HotbarSlotType::Empty);
    // The neighbour is untouched: the clear is per-address, not per-page.
    model->assignSlot({1, 4}, eqt::HotbarSlotType::Emote, 0, "/other", 0);
    bridge.drainEvents();
    bridge.pushIntent(eqt::events::HotbarClearIntent{1, 3});
    eq.Bridge().ProcessBridgeIntents();
    bridge.drainEvents();
    EXPECT_EQ(model->getSlot({1, 4}).type, eqt::HotbarSlotType::Emote);
}

// B1025-H3 (GitLab #345): /hotbutton's publisher -- the game thread's one
// hot-button producer. The HotbarAssignPending event lands on the cursor on
// the render thread (IrrlichtBridge's arm); the model never knows the button
// exists until it is placed (HotbarAssignIntent).
TEST_F(BridgeComponentTest, PublishHotbarAssignPendingPushesTheEvent) {
    bridge.drainEvents();
    EXPECT_TRUE(eq.Bridge().PublishHotbarAssignPending(
        eqt::HotbarSlotType::Emote, 0, "/say hi", 0));

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].type, eqt::state::GameEventType::HotbarAssignPending);
    const auto& data =
        std::get<eqt::state::HotbarAssignPendingData>(events[0].data);
    EXPECT_EQ(data.type, eqt::HotbarSlotType::Emote);
    EXPECT_EQ(data.text, "/say hi");
}

// The headless refusal: no bridge, no event, a false the caller reports with
// eqstr 13283.
TEST_F(BridgeComponentTest, PublishHotbarAssignPendingRefusesHeadless) {
    eq.setBridge(nullptr);
    EXPECT_FALSE(eq.Bridge().PublishHotbarAssignPending(
        eqt::HotbarSlotType::Emote, 0, "/say hi", 0));
    eq.setBridge(&bridge);
}

}  // namespace
