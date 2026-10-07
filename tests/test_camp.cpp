// B1005 (GitLab #324): /camp back to Character Select, /camp desktop exits.
//
// What the official Titanium client does, measured on the local lane
// (review/UIGAP-ORACLE/324/camp.tsv, row numbers in brackets), pinned here:
//   * the state machine (camp_timer.h): the timing of the lines and of
//     OP_Logout, the two cancels, the destination rule;
//   * the eqstr lines byte-exact against the SHIPPED string table;
//   * the packet bytes through a real EverQuest (sit appearance + OP_Camp
//     with its u32 mode, OP_Logout empty);
//   * the exit-code rule and the Character Select re-entry on the server's
//     OP_LogoutReply, through the real EverQuest, CharacterSelectService and
//     the world replay seam, as B998's tests drive them;
//   * the two scenarios' state machines over a fake world.

#include <gtest/gtest.h>

// The T17 idiom (test_click_senders_packet_sender.cpp): IsFullyZonedIn()'s
// third term, m_update_running, has no public setter, and the sit appearance
// is only sent zoned in.
#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/display_eqstr.h"
#include "client/eq_opcodes.h"
#include "client/login_world/character_select.h"
#include "client/login_world/character_select_service.h"
#include "client/login_world/login_world_component.h"
#include "client/movement/movement_component.h"
#include "client/player/camp_timer.h"
#include "client/player/player_component.h"
#include "client/scenario/scenario_runner.h"
#include "client/scenario/scenarios.h"
#include "client/willeq_database.h"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace eqt::player;

namespace {

std::vector<CampLine> Lines(std::initializer_list<CampLine> l) { return std::vector<CampLine>(l); }

// ---------------------------------------------------------------------------
// The state machine
// ---------------------------------------------------------------------------

TEST(CampTimer, StandingCampSitsSendsModeZeroAndPrints12293) {
    CampTimer t;
    const CampEffects fx = t.command(CampMode::CharacterSelect, 100.0, /*sitting=*/false);
    EXPECT_TRUE(fx.sit) << "[1] the client sits itself";
    EXPECT_TRUE(fx.sendCamp);
    EXPECT_EQ(fx.campBody, 0u) << "[3]";
    EXPECT_EQ(fx.lines, Lines({{kEqstrCampStart, ""}})) << "[4]";
    EXPECT_FALSE(fx.sendLogout);
    EXPECT_EQ(t.phase(), CampPhase::Counting);
}

TEST(CampTimer, SittingCampDoesNotSitAgain) {
    CampTimer t;
    EXPECT_FALSE(t.command(CampMode::CharacterSelect, 0.0, true).sit) << "[2]";
}

TEST(CampTimer, TheModeWordIsTheBody) {
    EXPECT_EQ(ParseCampMode(""), CampMode::CharacterSelect);
    EXPECT_EQ(ParseCampMode(" desktop "), CampMode::Desktop);
    EXPECT_EQ(ParseCampMode("DESKTOP"), CampMode::Desktop);
    EXPECT_EQ(ParseCampMode("server"), CampMode::Server);
    EXPECT_EQ(ParseCampMode("nonsense"), CampMode::CharacterSelect);
    for (auto [mode, body] : {std::pair{CampMode::CharacterSelect, 0u}, std::pair{CampMode::Desktop, 1u},
                              std::pair{CampMode::Server, 2u}}) {
        CampTimer t;
        EXPECT_EQ(t.command(mode, 0.0, false).campBody, body) << "[3]";
    }
}

TEST(CampTimer, CountdownLinesAt4_2PlusFiveAndLogoutAt30) {
    CampTimer t;
    t.command(CampMode::CharacterSelect, 10.0, false);
    // [5]: 25/20/15/10/5 at +4.2 +9.2 +14.2 +19.2 +24.2.
    const struct { double at; const char* rem; } steps[] = {
        {4.2, "25"}, {9.2, "20"}, {14.2, "15"}, {19.2, "10"}, {24.2, "5"}};
    for (const auto& s : steps) {
        EXPECT_TRUE(t.tick(10.0 + s.at - 0.05).lines.empty()) << "not before +" << s.at;
        EXPECT_EQ(t.tick(10.0 + s.at).lines, Lines({{kEqstrCampRemaining, s.rem}})) << "+" << s.at;
    }
    // [6]: OP_Logout at +30 s, and no line with it.
    EXPECT_FALSE(t.tick(39.95).sendLogout);
    const CampEffects end = t.tick(40.0);
    EXPECT_TRUE(end.sendLogout);
    EXPECT_TRUE(end.lines.empty()) << "[6] no line at the end";
    EXPECT_EQ(t.phase(), CampPhase::LogoutSent);
    EXPECT_FALSE(t.tick(50.0).sendLogout) << "once";
    EXPECT_FALSE(t.command(CampMode::CharacterSelect, 51.0, true).sendCamp) << "the server decides now";
}

TEST(CampTimer, ALateTickCatchesUpEveryLineInOrder) {
    CampTimer t;
    t.command(CampMode::Desktop, 0.0, false);
    const CampEffects fx = t.tick(31.0);
    ASSERT_EQ(fx.lines.size(), 5u);
    EXPECT_EQ(fx.lines.front().arg, "25");
    EXPECT_EQ(fx.lines.back().arg, "5");
    EXPECT_TRUE(fx.sendLogout);
}

TEST(CampTimer, CampAgainAbandonsWithNoPacketAndNoStand) {
    CampTimer t;
    t.command(CampMode::CharacterSelect, 0.0, false);
    t.tick(10.0);
    const CampEffects fx = t.command(CampMode::CharacterSelect, 14.0, true);
    EXPECT_EQ(fx.lines, Lines({{kEqstrCampAbandon, ""}})) << "[13]";
    EXPECT_FALSE(fx.sendCamp);
    EXPECT_FALSE(fx.sit);
    EXPECT_FALSE(fx.sendLogout);
    EXPECT_EQ(t.phase(), CampPhase::Idle);
    EXPECT_TRUE(t.tick(40.0).lines.empty());
    EXPECT_FALSE(t.tick(40.0).sendLogout) << "abandoned: no OP_Logout";
}

TEST(CampTimer, StandingAbandons) {
    CampTimer t;
    EXPECT_TRUE(t.stood().lines.empty()) << "not camping: nothing";
    t.command(CampMode::CharacterSelect, 0.0, false);
    EXPECT_EQ(t.stood().lines, Lines({{kEqstrCampAbandon, ""}})) << "[12]";
    EXPECT_EQ(t.phase(), CampPhase::Idle);
}

TEST(CampTimer, TheServersReplyDecidesWhereTheClientGoes) {
    {   // /camp, the screen available: Character Select [8-10]
        CampTimer t;
        t.command(CampMode::CharacterSelect, 0.0, false);
        t.tick(30.0);
        EXPECT_EQ(t.logoutReply(true), CampPhase::ToSelect);
    }
    {   // /camp desktop: exit [14]
        CampTimer t;
        t.command(CampMode::Desktop, 0.0, false);
        t.tick(30.0);
        EXPECT_EQ(t.logoutReply(true), CampPhase::ToDesktop);
    }
    {   // /camp server: fable has no server select -> Character Select (deviation)
        CampTimer t;
        t.command(CampMode::Server, 0.0, false);
        t.tick(30.0);
        EXPECT_EQ(t.logoutReply(true), CampPhase::ToSelect);
    }
    {   // headless: no screen -> exit, as before
        CampTimer t;
        t.command(CampMode::CharacterSelect, 0.0, false);
        t.tick(30.0);
        EXPECT_EQ(t.logoutReply(false), CampPhase::ToDesktop);
    }
    {   // a GM: the reply arrives while counting [16]
        CampTimer t;
        t.command(CampMode::CharacterSelect, 0.0, false);
        EXPECT_EQ(t.logoutReply(true), CampPhase::ToSelect);
    }
    {   // a reply that answers no camp: exit, exactly as before
        CampTimer t;
        EXPECT_EQ(t.logoutReply(true), CampPhase::ToDesktop);
    }
}

// ---------------------------------------------------------------------------
// The lines, byte-exact against the shipped table
// ---------------------------------------------------------------------------

class ShippedStrings : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::ifstream(WILLEQ_DB_REAL_PATH).good()) GTEST_SKIP() << "no shipped db";
        ASSERT_TRUE(db_.open(WILLEQ_DB_REAL_PATH));
        EQT::WilleqDatabase::setGlobalInstance(&db_);
        ASSERT_TRUE(strings_.initFromDatabase());
    }
    void TearDown() override { EQT::WilleqDatabase::setGlobalInstance(nullptr); }
    EQT::WilleqDatabase db_;
    EQT::StringDatabase strings_;
};

TEST_F(ShippedStrings, TheThreeCampLinesAreTheOracles) {
    // camp.chat.txt, verbatim.
    EXPECT_EQ(eqt::EqStrLine(strings_, kEqstrCampStart), "It will take you about 30 seconds to prepare your camp.");
    EXPECT_EQ(eqt::EqStrLine(strings_, kEqstrCampRemaining, {"25"}),
              "It will take about 25 more seconds to prepare your camp.");
    EXPECT_EQ(eqt::EqStrLine(strings_, kEqstrCampRemaining, {"5"}),
              "It will take about 5 more seconds to prepare your camp.");
    EXPECT_EQ(eqt::EqStrLine(strings_, kEqstrCampAbandon), "You abandon your preparations to camp.");
}

// ---------------------------------------------------------------------------
// The real client: packets, lines, the exit-code rule, Character Select
// ---------------------------------------------------------------------------

std::vector<uint8_t> CharInfo(const std::vector<std::string>& names) {
    std::vector<uint8_t> body(1704, 0);
    for (int i = 0; i < 10; ++i) {
        const std::string n = i < static_cast<int>(names.size()) ? names[static_cast<size_t>(i)] : "<none>";
        std::memcpy(body.data() + 1024 + 64 * i, n.data(), n.size());
        body[1694 + i] = 1;
        body[1004 + i] = 13;   // Magician
    }
    return body;
}

struct Client {
    Client() : eq("127.0.0.1", 0, "test", "test", "test", "Testbuddy") {
        eq.GetGameState().world().setZoneConnected(true);
        eq.GetGameState().world().setClientReady(true);
        eq.m_update_running = true;
        eq.Test_SetCaptureQueuedPacketsForSeam(true);
        eq.Test_ClearQueuedPackets();
    }
    std::vector<uint16_t> Ops() const {
        std::vector<uint16_t> v;
        for (const auto& p : eq.Test_GetQueuedPackets()) v.push_back(p.opcode);
        return v;
    }
    const std::vector<uint8_t>* Last(uint16_t op) const {
        const std::vector<uint8_t>* out = nullptr;
        for (const auto& p : eq.Test_GetQueuedPackets())
            if (p.opcode == op) out = &p.bytes;
        return out;
    }
    void LogoutReply() { eq.ReplayZonePacket(HC_OP_LogoutReply, nullptr, 0); }
    EverQuest eq;
};

TEST(CampClient, CampSendsTheSitThenOpCampWithItsMode) {
    Client c;
    c.eq.Player().StartCampTimer(CampMode::CharacterSelect);
    ASSERT_EQ(c.Ops(), (std::vector<uint16_t>{HC_OP_SpawnAppearance, HC_OP_Camp})) << "[1]";
    const auto* sit = c.Last(HC_OP_SpawnAppearance);
    ASSERT_EQ(sit->size(), 8u);
    EXPECT_EQ((*sit)[2], 14) << "type 14 Animation";
    EXPECT_EQ((*sit)[4], 110) << "param 110 Sitting";
    EXPECT_EQ(*c.Last(HC_OP_Camp), (std::vector<uint8_t>{0, 0, 0, 0})) << "[3]";
    EXPECT_TRUE(c.eq.IsCamping());

    // A second /camp: the abandon, nothing on the wire, still sitting [13].
    c.eq.Test_ClearQueuedPackets();
    c.eq.Player().StartCampTimer(CampMode::CharacterSelect);
    EXPECT_TRUE(c.Ops().empty());
    EXPECT_FALSE(c.eq.IsCamping());

    // /camp desktop while sitting: OP_Camp only, body 1 [2][3].
    c.eq.Player().StartCampTimer(CampMode::Desktop);
    EXPECT_EQ(c.Ops(), (std::vector<uint16_t>{HC_OP_Camp}));
    EXPECT_EQ(*c.Last(HC_OP_Camp), (std::vector<uint8_t>{1, 0, 0, 0}));
}

TEST(CampClient, StandingUpAbandonsTheCamp) {
    Client c;
    c.eq.Player().StartCampTimer();
    c.eq.Test_ClearQueuedPackets();
    c.eq.Movement().SetPositionState(POS_STANDING);
    EXPECT_FALSE(c.eq.IsCamping());
    EXPECT_EQ(c.Ops(), (std::vector<uint16_t>{HC_OP_SpawnAppearance})) << "[12] the stand is the only packet";
    EXPECT_EQ(c.eq.Player().Camp().phase(), CampPhase::Idle);
}

TEST(CampClient, NothingIsSentBeforeThirtySecondsAndOpLogoutIsEmpty) {
    Client c;
    c.eq.Player().StartCampTimer();
    c.eq.Test_ClearQueuedPackets();
    c.eq.Player().UpdateCampTimer();   // the real clock: far less than 30 s in
    EXPECT_TRUE(c.Ops().empty());
    EXPECT_EQ(c.eq.Player().Camp().phase(), CampPhase::Counting);
    // The same machine on a test clock reaches +30 s (CountdownLines... above);
    // what goes on the wire then is SendLogout's empty body.
    c.eq.Player().SendLogout();
    ASSERT_NE(c.Last(HC_OP_Logout), nullptr);
    EXPECT_TRUE(c.Last(HC_OP_Logout)->empty()) << "[6]";
}

TEST(CampClient, WithoutTheScreenTheLogoutReplyQuitsAsBefore) {
    // Headless (and every run before B1005): exit 0 through IsQuitRequested.
    Client c;
    c.eq.Player().StartCampTimer();
    c.LogoutReply();
    EXPECT_TRUE(c.eq.IsQuitRequested());
    EXPECT_TRUE(c.eq.Test_IsPendingLogoutDisconnect());
    EXPECT_EQ(c.eq.Player().Camp().phase(), CampPhase::ToDesktop);
    EXPECT_EQ(eqt::scenario::ProcessExitCode(false, 0, false), 0);
}

TEST(CampClient, CampDesktopQuitsEvenWithTheScreen) {
    Client c;
    c.eq.Player().SetCampReturnsToCharacterSelect(true);
    c.eq.Player().StartCampTimer(CampMode::Desktop);
    c.LogoutReply();
    EXPECT_TRUE(c.eq.IsQuitRequested()) << "[14]";
    EXPECT_FALSE(c.eq.Player().ConsumeReturnToCharacterSelect());
}

TEST(CampClient, CampReturnsToCharacterSelectOnTheServersReplyWithAFreshList) {
    Client c;
    eqt::login_world::CharacterSelectService service;
    eqt::login_world::CharacterSelectChannel channel;
    auto& lw = c.eq.LoginWorld();
    // The first login's list, auto-selected (configured character, no screen).
    lw.ReplayWorldPacket(HC_OP_SendCharInfo, CharInfo({"Testbuddy"}).data(), 1704);
    const uint32_t gen0 = lw.CharacterListGeneration();
    c.eq.Player().SetCampReturnsToCharacterSelect(true);

    c.eq.Player().StartCampTimer();
    service.tick(lw, channel, nullptr);
    EXPECT_FALSE(channel.active()) << "in zone: no screen";

    // The server's reply: no quit, the teardown is deferred (H03) ...
    c.LogoutReply();
    EXPECT_FALSE(c.eq.IsQuitRequested());
    EXPECT_TRUE(c.eq.Test_IsPendingLogoutDisconnect());
    EXPECT_EQ(c.eq.Player().Camp().phase(), CampPhase::ToSelect);
    EXPECT_FALSE(lw.CharacterSelectScreen()) << "nothing happens inside the packet callback";

    // ... to the next game tick, which goes back to world as a fresh login.
    c.eq.Movement().UpdateMovement();
    EXPECT_FALSE(c.eq.Test_IsPendingLogoutDisconnect());
    EXPECT_TRUE(lw.CharacterSelectScreen());
    EXPECT_TRUE(lw.AwaitingFreshCharacterList());
    EXPECT_EQ(lw.CharacterSelectReturns(), 1u);
    EXPECT_FALSE(lw.HasSentEnterWorld());
    EXPECT_EQ(c.eq.Player().Camp().phase(), CampPhase::Idle);
    EXPECT_FALSE(c.eq.IsQuitRequested());

    // The old list never brings the screen up; the fresh OP_SendCharInfo does.
    service.tick(lw, channel, nullptr);
    EXPECT_FALSE(channel.active()) << "waits for the FRESH list [9][10]";
    c.eq.Test_ClearQueuedPackets();
    lw.ReplayWorldPacket(HC_OP_ExpansionInfo, std::vector<uint8_t>{0xff, 0, 0, 0}.data(), 4);
    EXPECT_EQ(c.Ops(), (std::vector<uint16_t>{HC_OP_AckPacket, HC_OP_WorldClientReady}))
        << "a fresh login's handshake; no automatic OP_EnterWorld with the screen up";
    lw.ReplayWorldPacket(HC_OP_SendCharInfo, CharInfo({"Testbuddy", "Testother"}).data(), 1704);
    EXPECT_GT(lw.CharacterListGeneration(), gen0);
    EXPECT_FALSE(lw.AwaitingFreshCharacterList());
    service.tick(lw, channel, nullptr);
    ASSERT_TRUE(channel.active());
    uint64_t seen = 0;
    eqt::login_world::CharacterSelectView view;
    ASSERT_TRUE(channel.takeViewIfNewer(seen, view));
    ASSERT_EQ(view.rows.size(), 2u);
    EXPECT_EQ(view.rows[0].name, "Testbuddy");
    EXPECT_EQ(view.rows[1].name, "Testother");

    // Enter World from the screen: the normal 72-byte OP_EnterWorld [11].
    c.eq.Test_ClearQueuedPackets();
    eqt::login_world::CharacterSelectRequest req;
    req.kind = eqt::login_world::CharacterSelectRequest::Kind::EnterWorld;
    req.name = "Testbuddy";
    channel.post(req);
    service.tick(lw, channel, nullptr);
    ASSERT_NE(c.Last(HC_OP_EnterWorld), nullptr);
    std::vector<uint8_t> expected(72, 0);
    std::memcpy(expected.data(), "Testbuddy", 9);
    EXPECT_EQ(*c.Last(HC_OP_EnterWorld), expected);
    EXPECT_FALSE(channel.active()) << "screen down once OP_EnterWorld is out";
}

TEST(CampClient, TheWorldLoginAfterACampSaysZoningZero) {
    // [8]: OP_SendLoginInfo zoning byte (offset 188) = 0 after a camp. The
    // logout reply clears the zone-to-zone latch the byte is read from.
    Client c;
    c.eq.Zone().Test_SetZoneToZoneInProgressForSeam(true);
    c.eq.Player().SetCampReturnsToCharacterSelect(true);
    c.eq.Player().StartCampTimer();
    c.LogoutReply();
    EXPECT_FALSE(c.eq.Zone().Test_IsZoneToZoneInProgress());
}

TEST(CampClient, ARunThatCanDrawTheScreenReturnsThere) {
    using eqt::login_world::CampReturnsToCharacterSelect;
    EXPECT_TRUE(CampReturnsToCharacterSelect(true, false));
    EXPECT_FALSE(CampReturnsToCharacterSelect(false, false)) << "headless: exit 0, as before";
    EXPECT_FALSE(CampReturnsToCharacterSelect(true, true)) << "a world script owns select";
}

// ---------------------------------------------------------------------------
// The scenarios, over a fake world
// ---------------------------------------------------------------------------

class CampWorld final : public eqt::scenario::IScenarioWorld {
public:
    bool IsConnected() const override { return true; }
    bool IsFullyZonedIn() const override { return zoned; }
    std::string ZoneName() const override { return zoned ? "befallen" : ""; }
    int OtherPlayerCount() const override { return 0; }
    std::string PlayerName() const override { return "Testbuddy"; }
    std::vector<uint32_t> PlayerBuffSpellIds() const override { return {}; }
    long long PeerEquipmentMaterial(const std::string&, int) const override { return -1; }
    long long SelfEquipmentMaterial(int) const override { return -1; }
    int CampPhase() const override { return static_cast<int>(camp.phase()); }
    bool AtCharacterSelect() const override { return atSelect; }
    std::vector<std::string> CharacterListNames() const override { return names; }
    long long CharacterListGeneration() const override { return gen; }
    bool EnterWorldAs(const std::string& n) override {
        entered = n;
        atSelect = false;
        return true;
    }
    void SendCommand(const std::string& line) override {
        commands.push_back(line);
        if (line.rfind("/camp", 0) == 0) camp.command(ParseCampMode(line.substr(5)), now, false);
    }
    void Note(const std::string&) override {}

    // The server and the client between ticks.
    void Advance(double t) {
        now = t;
        if (camp.tick(t).sendLogout) replyAt = t + 0.03;
        if (replyAt > 0 && t >= replyAt) {
            replyAt = -1;
            if (camp.logoutReply(selectAvailable) == CampPhase::ToSelect) {
                zoned = false;
                camp.reset();
                selectAt = t + 1.8;
            } else {
                zoned = false;
                stopped = true;
            }
        }
        if (selectAt > 0 && t >= selectAt) {
            selectAt = -1;
            gen = 2;
            atSelect = true;
        }
        if (!entered.empty() && !zoned && !atSelect && !stopped) zoned = true;
    }

    CampTimer camp;
    bool zoned = true, atSelect = false, stopped = false, selectAvailable = true;
    long long gen = 1;
    std::vector<std::string> names{"Testbuddy"};
    std::string entered;
    std::vector<std::string> commands;
    double now = 0, replyAt = -1, selectAt = -1;
};

int RunCamp(const char* name, CampWorld& w, std::string* why) {
    eqt::scenario::ScenarioOptions o;
    eqt::scenario::ScenarioRunner runner(eqt::scenario::MakeScenario(name, o), w, 200.0, 10.0);
    double t = 0;
    while (runner.Tick(0.05f)) {
        t += 0.05;
        w.Advance(t);
        if (w.stopped) {
            runner.Conclude();
            break;
        }
    }
    if (why) *why = runner.reason();
    return runner.exitCode();
}

TEST(CampScenarios, CampToCharselectPassesOnTheMeasuredFlow) {
    CampWorld w;
    std::string why;
    EXPECT_EQ(RunCamp("camp_to_charselect", w, &why), eqt::scenario::kScenarioPass) << why;
    EXPECT_EQ(w.entered, "Testbuddy");
    ASSERT_FALSE(w.commands.empty());
    EXPECT_EQ(w.commands.front(), "/camp");
    auto s = eqt::scenario::MakeScenario("camp_to_charselect", {});
    EXPECT_TRUE(s->WantsCampToCharacterSelect());
}

TEST(CampScenarios, CampToCharselectFailsWhenTheZoneIsLeftEarly) {
    CampWorld w;
    w.replyAt = 5.0;   // the server ends the session 5 s in (a GM)
    std::string why;
    EXPECT_EQ(RunCamp("camp_to_charselect", w, &why), eqt::scenario::kScenarioFail);
    EXPECT_NE(why.find("left the countdown"), std::string::npos) << why;
}

TEST(CampScenarios, CampToCharselectFailsOnAListWithoutUs) {
    CampWorld w;
    w.names = {"Someoneelse"};
    std::string why;
    EXPECT_EQ(RunCamp("camp_to_charselect", w, &why), eqt::scenario::kScenarioFail);
    EXPECT_NE(why.find("does not name"), std::string::npos) << why;
}

TEST(CampScenarios, CampDesktopPassesOnlyWhenTheClientStopsAfterTheReply) {
    CampWorld w;
    w.selectAvailable = false;
    std::string why;
    EXPECT_EQ(RunCamp("camp_desktop", w, &why), eqt::scenario::kScenarioPass) << why;
    EXPECT_EQ(w.commands.front(), "/camp desktop");
}

TEST(CampScenarios, CampDesktopStoppedByAnythingElseIsNotAPass) {
    // An external quit at 10 s: the runner's last look must not pass it.
    CampWorld w;
    eqt::scenario::ScenarioRunner runner(eqt::scenario::MakeScenario("camp_desktop", {}), w, 200.0, 10.0);
    double t = 0;
    while (t < 10.0 && runner.Tick(0.05f)) {
        t += 0.05;
        w.Advance(t);
    }
    runner.Conclude();
    EXPECT_EQ(runner.exitCode(), eqt::scenario::kScenarioFail) << runner.reason();
}

TEST(CampScenarios, OtherScenariosStayUnfinishedOnAnExternalStop) {
    CampWorld w;
    eqt::scenario::ScenarioRunner runner(eqt::scenario::MakeScenario("zone_in", {}), w, 200.0, 10.0);
    runner.Conclude();   // not started: nothing
    EXPECT_EQ(runner.exitCode(), eqt::scenario::kScenarioTimeout);
}

}  // namespace
