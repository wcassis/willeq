// B1081 (GitLab #401): the character-select screen must not flash on a
// zone-change world reconnect.
//
// The measured mechanism (issue #401, Will's XU4 sessions): a --character-
// select run leaves m_select_screen ON after its first Enter World (the
// screen only goes down for good when OP_EnterWorld is out). A zone change
// then tears the world session down (ReconnectWorld resets m_enter_world_sent
// to false, login_world_char_select.cpp:60) and the zone-change pass never
// re-sends OP_SendCharInfo (eqemu's world skips the character block when
// is_player_zoning is set, world/client.cpp 516-523; the zoning pass is the
// OP_SendLoginInfo zoning==1 / OP_PostEnterWorld pair, WorldProcessPost-
// EnterWorld, login_world_component.cpp:811). With the stale first-login
// list still held, CharacterSelectService::tick() re-activates the channel
// from it for the frames until the automatic OP_EnterWorld goes out -- the
// flash.
//
// The fix: LoginWorldComponent::IsZoningReconnect() (true from the zone-
// change world reconnect, i.e. ZoneToZoneInProgress latched AND the zoning
// pass's OP_EnterWorld not yet out, until it goes out; false for a fresh
// login, the B1005 camp return and the B1072 give-up) gates activation in
// CharacterSelectService::tick(). The screen's up/down is counted in
// CharacterSelectService::Activations() -- the game-thread equivalent of the
// render thread's "[CHARSELECT] screen up" log line, so a headless --scenario
// run can assert it.
//
// The zone_change_no_flash scenario (src/client/scenario/
// scenario_zone_change_no_flash.cpp) is the live arm: a GM character zones
// in with the screen on (WantsCharacterSelectScreen), /gmzones to a
// different zone (the zone change that drives ProcessDeferredZoneChange, the
// same path a zone-line crossing or a death-to-bind re-entry take), and the
// assertion is Activations() == 1 -- the fresh login's, and no more.

#include <gtest/gtest.h>

#include "client/eq.h"
#include "client/eq_opcodes.h"
#include "client/login_world/character_select_service.h"
#include "client/login_world/login_world_component.h"
#include "client/player/player_component.h"
#include "client/scenario/scenarios.h"
#include "client/zone/zone_component.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

using eqt::login_world::CharacterSelectChannel;
using eqt::login_world::CharacterSelectService;

// A 10-slot Titanium character list naming the given characters (test_camp's
// shape).
std::vector<uint8_t> CharInfo(const std::vector<std::string>& names) {
    std::vector<uint8_t> body(1704, 0);
    for (int i = 0; i < 10; ++i) {
        const std::string n = i < static_cast<int>(names.size()) ? names[static_cast<size_t>(i)] : "<none>";
        std::memcpy(body.data() + 1024 + 64 * i, n.data(), n.size());
        body[1694 + i] = 1;
        body[1004 + i] = 1;   // Warrior
    }
    return body;
}

// The one game tick of the screen's game-thread half.
void Tick(CharacterSelectService& service, eqt::login_world::LoginWorldComponent& lw,
          CharacterSelectChannel& channel) {
    service.tick(lw, channel, [](uint32_t id) { return "eqstr" + std::to_string(id); });
}

// A headless client with the character-select screen up, the same rig shape
// as tests/test_character_select.cpp's (the real EverQuest, the real
// service, the replay seam -- the world stream is replayed, never a socket).
struct Rig {
    Rig() : eq("127.0.0.1", 0, "test", "test", "test", "Testchar") {
        eq.Test_SetCaptureQueuedPacketsForSeam(true);
        eq.Test_ClearQueuedPackets();
        lw.SetCharacterSelectScreen(true);
    }
    void World(uint16_t op, const std::vector<uint8_t>& body) {
        lw.ReplayWorldPacket(op, body.data(), body.size());
    }
    void OneTick() { Tick(service, lw, channel); }

    EverQuest eq;
    eqt::login_world::LoginWorldComponent& lw = eq.LoginWorld();
    CharacterSelectService service;
    CharacterSelectChannel channel;
};

// ---------------------------------------------------------------------------
// IsZoningReconnect(): the derived flag, all four states
// ---------------------------------------------------------------------------

TEST(ZoningReconnectGate, IsZoningReconnectIsTheLatchUntilTheZoningEnterWorld) {
    Rig r;
    auto& zone = r.eq.Zone();
    // Fresh login: the latch was never set.
    EXPECT_FALSE(r.lw.IsZoningReconnect());
    // A zone change in flight: latched, the automatic OP_EnterWorld not out.
    zone.Test_SetZoneToZoneInProgressForSeam(true);
    EXPECT_TRUE(r.lw.IsZoningReconnect());
    // The zoning pass's OP_EnterWorld is out: false again.
    r.lw.Test_EnterWorldSentForSeam(true);
    EXPECT_FALSE(r.lw.IsZoningReconnect());
    // B1005 camp return / B1072 give-up: the latch clears (player_camp.cpp /
    // zone_connect.cpp), so even with the enter-world flag down this is not
    // a zoning reconnect -- the screen must come up there.
    zone.Test_SetZoneToZoneInProgressForSeam(false);
    r.lw.Test_EnterWorldSentForSeam(false);
    EXPECT_FALSE(r.lw.IsZoningReconnect());
}

// ---------------------------------------------------------------------------
// The fresh login: one screen-up, exactly as before B1081
// ---------------------------------------------------------------------------

TEST(ZoningReconnectGate, FreshLoginActivatesTheScreenOnce) {
    Rig r;
    EXPECT_FALSE(r.channel.active());
    EXPECT_EQ(r.service.Activations(), 0u);
    r.OneTick();
    EXPECT_FALSE(r.channel.active()) << "no list yet: the loading screen stays";
    r.World(HC_OP_SendCharInfo, CharInfo({"Testchar"}));
    EXPECT_FALSE(r.channel.active());
    r.OneTick();
    EXPECT_TRUE(r.channel.active());
    EXPECT_EQ(r.service.Activations(), 1u);
    // Enter World: the screen is down for good.
    EXPECT_TRUE(r.lw.EnterWorldFromSelect("Testchar"));
    r.OneTick();
    EXPECT_FALSE(r.channel.active());
    EXPECT_EQ(r.service.Activations(), 1u);
    r.OneTick();
    EXPECT_EQ(r.service.Activations(), 1u);
}

// ---------------------------------------------------------------------------
// The flash, red at the parent: zone-change reconnect, stale list, screen on
// ---------------------------------------------------------------------------

TEST(ZoningReconnectGate, ZoneChangeReconnectDoesNotReActivateTheScreen) {
    Rig r;
    // 1. Fresh login with the screen, one screen-up (above, condensed).
    r.World(HC_OP_SendCharInfo, CharInfo({"Testchar"}));
    r.OneTick();
    ASSERT_TRUE(r.channel.active());
    ASSERT_EQ(r.service.Activations(), 1u);
    ASSERT_TRUE(r.lw.EnterWorldFromSelect("Testchar"));
    r.OneTick();
    ASSERT_FALSE(r.channel.active());

    // 2. A zone change. The client latches ZoneToZoneInProgress in
    //    ProcessDeferredZoneChange (zone_component.cpp:646) and tears the
    //    world session down -- ReconnectWorld resets m_enter_world_sent to
    //    false (login_world_char_select.cpp:60). The seams model exactly
    //    those two writes; the enter-world flag is already true (Enter World
    //    went out in 1.) and the reset clears it.
    r.eq.Zone().Test_SetZoneToZoneInProgressForSeam(true);
    r.lw.Test_EnterWorldSentForSeam(false);
    ASSERT_TRUE(r.lw.IsZoningReconnect());

    // 3. The zone-change world pass: eqemu sends OP_SendLoginInfo (zoning==1)
    //    and OP_PostEnterWorld and SKIPS the character block -- no fresh
    //    OP_SendCharInfo (WorldProcessPostEnterWorld, login_world_component.
    //    cpp:811; eqemu world/client.cpp 516-523). The STALE list is still
    //    held. Ticks for the frames of the reconnect: without the gate,
    //    tick() re-activates the channel from the stale list here -- the
    //    flash, measured on the XU4 (issue #401).
    const uint32_t gen = r.lw.CharacterListGeneration();
    for (int i = 0; i < 5; ++i) r.OneTick();
    EXPECT_FALSE(r.channel.active()) << "the loading screen stays up during the zone-change reconnect";
    EXPECT_EQ(r.service.Activations(), 1u) << "no second screen-up from the stale list";
    EXPECT_EQ(r.lw.CharacterListGeneration(), gen) << "the zoning pass never re-lists";

    // 4. The automatic OP_EnterWorld goes out in the zoning pass.
    r.lw.Test_EnterWorldSentForSeam(true);
    EXPECT_FALSE(r.lw.IsZoningReconnect());
    r.OneTick();
    EXPECT_FALSE(r.channel.active());
    EXPECT_EQ(r.service.Activations(), 1u);

    // 5. The new zone session comes up: the latch clears (EverQuest::
    //    ZoneOnStatusChangeReconnectEnabled, everquest.cpp:865). Even with
    //    the stale list still held and the screen flag still on, nothing
    //    brings the screen back up -- a zone change is not a character-
    //    select stage.
    r.eq.Zone().Test_SetZoneToZoneInProgressForSeam(false);
    for (int i = 0; i < 5; ++i) r.OneTick();
    EXPECT_FALSE(r.channel.active());
    EXPECT_EQ(r.service.Activations(), 1u);
}

// ---------------------------------------------------------------------------
// The negatives: a camp return and a B1072 give-up ARE character-select
// stages, and the screen still comes up there (B1005 must stay green)
// ---------------------------------------------------------------------------

TEST(ZoningReconnectGate, CampReturnStillBringsTheScreenUp) {
    Rig r;
    r.World(HC_OP_SendCharInfo, CharInfo({"Testchar"}));
    r.OneTick();
    ASSERT_TRUE(r.channel.active());
    ASSERT_EQ(r.service.Activations(), 1u);
    ASSERT_TRUE(r.lw.EnterWorldFromSelect("Testchar"));
    r.OneTick();
    ASSERT_FALSE(r.channel.active());

    // /camp, server-confirmed: ReturnToCharacterSelect -- the screen ON, the
    // fresh list awaited, the latch CLEARED (the logout reply, player_camp.
    // cpp), the world session torn down. The rig has no world socket, so
    // the tear-down's flag reset is modeled with the seam, exactly as in the
    // zone-change test.
    r.lw.Test_EnterWorldSentForSeam(false);
    r.lw.ReturnToCharacterSelect("camp");
    ASSERT_TRUE(r.lw.CharacterSelectScreen());
    ASSERT_TRUE(r.lw.AwaitingFreshCharacterList());
    ASSERT_FALSE(r.eq.Zone().Test_IsZoneToZoneInProgress());
    ASSERT_FALSE(r.lw.HasSentEnterWorld());
    // AwaitingFreshCharacterList gates the tick (the B1005 gate): nothing
    // from the stale list while the fresh one is owed.
    r.OneTick();
    EXPECT_FALSE(r.channel.active());
    EXPECT_EQ(r.service.Activations(), 1u);

    // The fresh OP_SendCharInfo: the screen comes up -- screen-up #2, the
    // B1005 contract.
    r.World(HC_OP_SendCharInfo, CharInfo({"Testchar"}));
    ASSERT_FALSE(r.lw.AwaitingFreshCharacterList());
    r.OneTick();
    EXPECT_TRUE(r.channel.active());
    EXPECT_EQ(r.service.Activations(), 2u);
}

TEST(ZoningReconnectGate, B1072GiveUpStillBringsTheScreenUp) {
    Rig r;
    r.World(HC_OP_SendCharInfo, CharInfo({"Testchar"}));
    r.OneTick();
    ASSERT_TRUE(r.channel.active());
    ASSERT_EQ(r.service.Activations(), 1u);
    ASSERT_TRUE(r.lw.EnterWorldFromSelect("Testchar"));
    r.OneTick();
    ASSERT_FALSE(r.channel.active());

    // The zone change was in flight (latched, the reconnect's flag reset
    // modeled with the seam), then the zone-in bound blew. The give-up's
    // order is AbandonZoneConnect's: the latch clears FIRST
    // (zone_connect.cpp:109), and the "returning to character select"
    // branch is ReturnToCharacterSelect itself -- screen ON, the fresh
    // list awaited, the enter-world flag down.
    r.eq.Zone().Test_SetZoneToZoneInProgressForSeam(true);
    r.lw.Test_EnterWorldSentForSeam(false);
    ASSERT_TRUE(r.lw.IsZoningReconnect());
    r.eq.Zone().Test_SetZoneToZoneInProgressForSeam(false);
    r.lw.ReturnToCharacterSelect("zone connect failed");
    ASSERT_TRUE(r.lw.CharacterSelectScreen());
    ASSERT_TRUE(r.lw.AwaitingFreshCharacterList());
    ASSERT_FALSE(r.lw.IsZoningReconnect());
    ASSERT_FALSE(r.lw.HasSentEnterWorld());

    // The fresh login's list arrives and the screen is up: the screen comes
    // up -- the give-up is a user stage, exactly as a fresh login (B1072's
    // own scenario asserts the same, live).
    r.World(HC_OP_SendCharInfo, CharInfo({"Testchar"}));
    ASSERT_FALSE(r.lw.AwaitingFreshCharacterList());
    r.OneTick();
    EXPECT_TRUE(r.channel.active());
    EXPECT_EQ(r.service.Activations(), 2u);
}

// ---------------------------------------------------------------------------
// The zone_change_no_flash scenario over a fake world
// ---------------------------------------------------------------------------

struct FakeWorld : eqt::scenario::IScenarioWorld {
    bool zonedIn = false;
    long long activations = -1;
    std::string me = "Testchar", zone = "";
    int enters = 0;
    std::vector<std::string> sent;

    bool IsConnected() const override { return true; }
    bool IsFullyZonedIn() const override { return zonedIn; }
    std::string ZoneName() const override { return zone; }
    int OtherPlayerCount() const override { return 0; }
    std::string PlayerName() const override { return me; }
    std::vector<uint32_t> PlayerBuffSpellIds() const override { return {}; }
    long long PeerEquipmentMaterial(const std::string&, int) const override { return -1; }
    long long SelfEquipmentMaterial(int) const override { return -1; }
    long long CharacterSelectActivations() const override { return activations; }
    bool EnterWorldAs(const std::string& n) override {
        ++enters;
        zonedIn = false;   // the world pass: zone session down, then back up
        return n == me;
    }
    void SendCommand(const std::string& line) override { sent.push_back(line); }
    void Note(const std::string&) override {}
};

using eqt::scenario::StepResult;

struct ZoneChangeRig {
    FakeWorld w;
    eqt::scenario::ScenarioOptions options;
    std::unique_ptr<eqt::scenario::IScenario> s;

    ZoneChangeRig(long long target = 303) {
        options.zoneChangeTargetZone = target;
        s = eqt::scenario::MakeZoneChangeNoFlashScenario(options);
    }
    StepResult At(double t) {
        eqt::scenario::ScenarioTick tick;
        tick.sinceStart = t;
        return s->Step(w, tick);
    }
};

TEST(ZoneChangeNoFlashScenario, OptInAndTargetValidation) {
    ZoneChangeRig r;
    EXPECT_TRUE(r.s->WantsCharacterSelectScreen());
    EXPECT_FALSE(r.s->WantsCampToCharacterSelect());
    EXPECT_FALSE(r.s->WantsPreFlight());
    // No target: the scenario refuses once it is in a zone (it has nothing
    // to zone to).
    ZoneChangeRig none;
    none.options.zoneChangeTargetZone = 0;
    none.s = eqt::scenario::MakeZoneChangeNoFlashScenario(none.options);
    none.w.activations = 1;
    EXPECT_EQ(none.At(0.0), StepResult::Running);   // WaitList: Enter World
    none.w.zonedIn = true;
    none.w.zone = "qeynos";
    EXPECT_EQ(none.At(1.0), StepResult::Fail) << none.s->Reason();
}

TEST(ZoneChangeNoFlashScenario, PassesOnListEnterFirstZoneGmzoneSecondZone) {
    ZoneChangeRig r;
    // Wait for the fresh login's list.
    r.w.activations = 0;
    EXPECT_EQ(r.At(0.0), StepResult::Running) << "no list yet";
    r.w.activations = 1;
    // The screen's Enter World: the zone session goes down, then back up.
    EXPECT_EQ(r.At(10.0), StepResult::Running) << r.s->Reason();
    ASSERT_EQ(r.w.enters, 1);
    EXPECT_FALSE(r.w.zonedIn);
    r.w.zonedIn = true;
    r.w.zone = "qeynos";
    // The first zone in: the /gmzone to the target.
    EXPECT_EQ(r.At(20.0), StepResult::Running) << r.s->Reason();
    ASSERT_EQ(r.w.sent, (std::vector<std::string>{"/gmzone Testchar 303"}));
    // The zone change: the world pass (zone session down), then the target.
    r.w.zonedIn = false;
    r.w.zone = "";
    EXPECT_EQ(r.At(30.0), StepResult::Running) << "mid transition";
    r.w.zonedIn = true;
    r.w.zone = "gloamingdeep";
    EXPECT_EQ(r.At(40.0), StepResult::Pass) << r.s->Reason();
}

TEST(ZoneChangeNoFlashScenario, FailsOnASecondScreenUp) {
    ZoneChangeRig r;
    r.w.activations = 1;
    EXPECT_EQ(r.At(0.0), StepResult::Running);
    EXPECT_EQ(r.At(10.0), StepResult::Running);
    r.w.zonedIn = true;
    r.w.zone = "qeynos";
    EXPECT_EQ(r.At(20.0), StepResult::Running);
    r.w.zonedIn = false;
    r.w.zone = "";
    r.w.activations = 2;   // the flash
    r.w.zonedIn = true;
    r.w.zone = "gloamingdeep";
    EXPECT_EQ(r.At(40.0), StepResult::Fail) << r.s->Reason();
}

TEST(ZoneChangeNoFlashScenario, FailsWhenTheZoneChangeNeverCompletes) {
    ZoneChangeRig r;
    r.w.activations = 1;
    EXPECT_EQ(r.At(0.0), StepResult::Running);
    EXPECT_EQ(r.At(10.0), StepResult::Running);
    r.w.zonedIn = true;
    r.w.zone = "qeynos";
    EXPECT_EQ(r.At(20.0), StepResult::Running);
    // /gmzone went out, the zone session went down, and it never came back.
    r.w.zonedIn = false;
    r.w.zone = "";
    EXPECT_EQ(r.At(40.0), StepResult::Running);
    EXPECT_EQ(r.At(40.0 + 240.0), StepResult::Fail) << "the zone-in bound";
}

}  // namespace
