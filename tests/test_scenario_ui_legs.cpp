// UIGAP-live: offline tests for the six wave-1 UI units' live legs
// (src/client/scenario/scenario_ui_inventory.cpp, scenario_ui_social.cpp).
//
// The world here is a small SIMULATION: it answers /pickup /putdown /combine
// /train /friends /ignore /track and synthetic slot clicks the way the server
// and the client model are expected to, on a hand-cranked clock. Every leg is
// driven to a PASS through the real ScenarioRunner, and every assertion a leg
// makes is shown to FAIL when the world misbehaves in exactly that way.

#include <gtest/gtest.h>

#include "client/scenario/scenario_runner.h"
#include "client/scenario/scenarios.h"
#include "client/state/event_types.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace eqt::scenario;
using eqt::state::GameEventType;

namespace {

class SimWorld : public IScenarioWorld {
public:
    // --- the basics ---
    bool zonedIn = true;
    int otherPlayers = 1;
    long long peerMaterial = 0;
    std::vector<std::string> commands;
    std::vector<std::string> notes;
    double clock = 0.0;

    // --- inventory (inventory.slot_id numbering) ---
    std::map<int, long long> items;
    std::deque<long long> cursor;
    bool revertBankMoves = false;  // the server refuses bank moves (resync)

    // --- skills ---
    long long skill0 = 1;
    long long points = 160;
    bool optimisticTrain = false;  // the client writes the value itself (the defect)
    double trainUpdateAt = -1.0;

    // --- chat / events ---
    std::vector<std::string> chat;
    std::map<int, long long> events;
    std::map<int, long long> lastSkill;
    long long trackingRows = -1;
    std::vector<std::string> ignored;
    bool ignoreFilterBroken = false;
    double tell1At = 20.0, tell2At = 50.0;
    double directionEvery = 2.0;
    double nextDirection = -1.0;
    double peerLeavesAt = -1.0;

    bool IsConnected() const override { return true; }
    bool IsFullyZonedIn() const override { return zonedIn; }
    std::string ZoneName() const override { return "qeynos"; }
    int OtherPlayerCount() const override { return otherPlayers; }
    std::string PlayerName() const override { return "Testchar"; }
    std::vector<uint32_t> PlayerBuffSpellIds() const override { return {}; }
    long long PeerEquipmentMaterial(const std::string&, int) const override {
        return otherPlayers >= 1 ? peerMaterial : -1;
    }
    long long SelfEquipmentMaterial(int) const override { return -1; }

    long long ClientItemAt(int slot) const override {
        if (slot == 33) return cursor.empty() ? 0 : cursor.front();
        auto it = items.find(slot);
        return it == items.end() ? 0 : it->second;
    }
    long long ClientChargesAt(int slot) const override { return ClientItemAt(slot) ? 1 : 0; }
    long long CursorQueueSize() const override { return static_cast<long long>(cursor.size()); }
    long long SkillValue(int id) const override { return id == 0 ? skill0 : 0; }
    long long PracticePoints() const override { return points; }
    size_t ChatLineCount() const override { return chat.size(); }
    std::string ChatLine(size_t i) const override { return i < chat.size() ? chat[i] : ""; }
    long long EventCount(int type) const override {
        auto it = events.find(type);
        return it == events.end() ? 0 : it->second;
    }
    long long LastSkillUpdateValue(int id) const override {
        auto it = lastSkill.find(id);
        return it == lastSkill.end() ? -1 : it->second;
    }
    long long TrackingRowCount() const override { return trackingRows; }
    long long NearestWorldObjectId(int type) const override { return type == 17 ? 178 : 0; }

    void Pickup(int slot) {
        auto it = items.find(slot);
        if (it == items.end() || !cursor.empty()) return;
        cursor.push_front(it->second);
        items.erase(it);
    }
    void Putdown(int slot) {
        if (cursor.empty() || ClientItemAt(slot) != 0) return;
        if (revertBankMoves && slot >= 2000 && slot < 2024) {
            pendingRevert = slot;
        }
        items[slot] = cursor.front();
        cursor.pop_front();
    }
    int ClickSlot(int slot, bool, bool, bool) override {
        commands.push_back("CLICK " + std::to_string(slot));
        if (cursor.empty()) Pickup(slot); else Putdown(slot);
        return 1;
    }
    void System(const std::string& text) { chat.push_back("system||" + text); }
    void Event(GameEventType t) { ++events[static_cast<int>(t)]; }

    void SendCommand(const std::string& line) override {
        commands.push_back(line);
        chat.push_back("system||" + line);  // the command processor's echo
        auto arg = [&line](const char* verb) {
            const std::string v = std::string(verb) + " ";
            return line.rfind(v, 0) == 0 ? line.substr(v.size()) : std::string();
        };
        if (!arg("/pickup").empty()) Pickup(std::stoi(arg("/pickup")));
        else if (!arg("/putdown").empty()) Putdown(std::stoi(arg("/putdown")));
        else if (line == "/combine 23") {
            items.erase(4010); items.erase(4011);
            cursor.push_back(21565); cursor.push_back(13782);
        } else if (line == "/combine") {
            items.erase(4000); items.erase(4001);
            cursor.push_back(12896);
        } else if (line.rfind("/clickobject", 0) == 0) {
            Event(GameEventType::TradeskillContainerOpened);
        } else if (line == "/train") {
            Event(GameEventType::TrainerWindowOpened);
        } else if (line == "/train 0") {
            trainUpdateAt = clock + 1.0;
            if (optimisticTrain) ++skill0;
        } else if (!arg("/friends").empty()) {
            const std::string who = arg("/friends");
            if (friends) System(who + " is no longer your friend.");
            else System(who + " is now your friend.");
            friends = !friends;
        } else if (line == "/friendswho") {
            System("Friends currently on EverQuest:");
            System("[1 Magician] Testbuddy (Gnome) ZONE: qeynos");
        } else if (!arg("/ignore").empty()) {
            const std::string who = arg("/ignore");
            if (ignored.empty()) { ignored.push_back(who); System("You have added " + who + " to your IGNORE list."); }
            else { ignored.clear(); System("You have removed " + who + " from your IGNORE list."); }
        } else if (line == "/useskill tracking") {
            trackingRows = 12;
            Event(GameEventType::TrackingListChanged);
        } else if (line == "/tracklist") {
            System("=== Tracking (12 targets) ===");
            System("  Testbuddy - 20 units - white (player)");
        } else if (!arg("/track").empty()) {
            System("You begin tracking " + arg("/track") + ".");
            System(arg("/track") + " is to the left.");
            nextDirection = clock + directionEvery;
        }
    }
    void Note(const std::string& l) override { notes.push_back(l); }

    void OnTick(double dt) {
        clock += dt;
        if (pendingRevert >= 0 && clock > revertAt(clock)) {
            // the server's resync: the item goes back on the cursor
            cursor.push_front(items[pendingRevert]);
            items.erase(pendingRevert);
            pendingRevert = -1;
        }
        if (trainUpdateAt >= 0 && clock >= trainUpdateAt) {
            trainUpdateAt = -1;
            if (!optimisticTrain) ++skill0;
            --points;
            lastSkill[0] = skill0;
            Event(GameEventType::SkillValueChanged);
        }
        if (tell1At >= 0 && clock >= tell1At) {
            tell1At = -1;
            chat.push_back("tell|Testbuddy|UIGAPLIVE-tell-1");
        }
        if (tell2At >= 0 && clock >= tell2At) {
            tell2At = -1;
            if (ignored.empty() || ignoreFilterBroken) chat.push_back("tell|Testbuddy|UIGAPLIVE-tell-2");
        }
        if (nextDirection >= 0 && clock >= nextDirection) {
            if (peerLeavesAt >= 0 && clock >= peerLeavesAt) {
                System("You have lost your target.");
                nextDirection = -1;
            } else {
                System("Testbuddy is behind you.");
                nextDirection = clock + directionEvery;
            }
        }
    }

private:
    bool friends = false;
    int pendingRevert = -1;
    static double revertAt(double now) { return now - 0.5; }  // next tick
};

int RunLeg(const std::string& name, SimWorld& world, double seconds, std::string* reason = nullptr,
        const std::string& peer = "Testbuddy") {
    ScenarioOptions o;
    o.castTargetName = peer;
    ScenarioRunner runner(MakeScenario(name, o), world, /*timeout=*/900.0, /*startDeadline=*/10.0);
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < static_cast<int>(seconds * 60.0); ++i) {
        world.OnTick(dt);
        if (!runner.Tick(dt)) break;
    }
    if (reason) *reason = runner.reason();
    return runner.finished() ? runner.exitCode() : -1;
}

SimWorld PickupFixture() {
    SimWorld w;
    w.items = {{23, 17005}, {24, 7007}};
    return w;
}

}  // namespace

TEST(UiLegsRegistry, AllTenUiLegScenariosAreRegistered) {
    for (const char* n : {"pickup_roundtrip", "click_equip_observed", "click_equip_actor",
                          "combine_sewing", "train_skill", "friends_who", "ignore_tell",
                          "social_peer", "track_list", "track_peer"}) {
        EXPECT_TRUE(IsScenarioName(n)) << n;
    }
}

TEST(PickupRoundtrip, PassesGeneralBagBankGeneralByCommandsAndClicks) {
    SimWorld w = PickupFixture();
    std::string why;
    ASSERT_EQ(RunLeg("pickup_roundtrip", w, 200.0, &why), kScenarioPass) << why;
    EXPECT_EQ(w.ClientItemAt(24), 7007);
    // Three hops by command and three by synthetic click.
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "/pickup 24"), w.commands.end());
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "/putdown 4010"), w.commands.end());
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "CLICK 4010"), w.commands.end());
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "CLICK 2000"), w.commands.end());
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "/pickup 2000"), w.commands.end());
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "CLICK 24"), w.commands.end());
    EXPECT_NE(why.find("MODEL 24=7007"), std::string::npos) << why;
}

TEST(PickupRoundtrip, AServerResyncThatUndoesAMoveFailsTheLeg) {
    SimWorld w = PickupFixture();
    w.revertBankMoves = true;
    std::string why;
    EXPECT_EQ(RunLeg("pickup_roundtrip", w, 200.0, &why), kScenarioFail);
    EXPECT_NE(why.find("2000"), std::string::npos) << why;
}

TEST(PickupRoundtrip, AWrongFixtureFailsAtTheFixtureStep) {
    SimWorld w;  // empty inventory
    std::string why;
    EXPECT_EQ(RunLeg("pickup_roundtrip", w, 200.0, &why), kScenarioFail);
    EXPECT_NE(why.find("fixture"), std::string::npos) << why;
}

TEST(ClickEquip, ActorClicksBagSlotThenPrimary) {
    SimWorld w;
    w.items = {{23, 17005}, {4010, 7007}};
    std::string why;
    ASSERT_EQ(RunLeg("click_equip_actor", w, 200.0, &why), kScenarioPass) << why;
    EXPECT_EQ(w.ClientItemAt(13), 7007);
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "CLICK 4010"), w.commands.end());
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "CLICK 13"), w.commands.end());
}

TEST(ClickEquip, ObserverNamesALiveWearChangeApartFromARespawn) {
    {
        SimWorld w;
        ScenarioRunner runner(MakeScenario("click_equip_observed", {}), w, 900.0, 10.0);
        for (int i = 0; i < 600; ++i) runner.Tick(1.0f / 60.0f);
        w.peerMaterial = 7;  // live, the peer never left
        for (int i = 0; i < 60 && runner.Tick(1.0f / 60.0f); ++i) {}
        ASSERT_EQ(runner.exitCode(), kScenarioPass);
        EXPECT_NE(runner.reason().find("LIVE OP_WearChange"), std::string::npos) << runner.reason();
    }
    {
        SimWorld w;
        ScenarioRunner runner(MakeScenario("click_equip_observed", {}), w, 900.0, 10.0);
        for (int i = 0; i < 600; ++i) runner.Tick(1.0f / 60.0f);
        w.otherPlayers = 0;  // the actor exits
        for (int i = 0; i < 600; ++i) runner.Tick(1.0f / 60.0f);
        w.otherPlayers = 1;
        w.peerMaterial = 7;  // its next spawn carries the sword
        for (int i = 0; i < 60 && runner.Tick(1.0f / 60.0f); ++i) {}
        ASSERT_EQ(runner.exitCode(), kScenarioPass);
        EXPECT_NE(runner.reason().find("SPAWN packet"), std::string::npos) << runner.reason();
    }
}

TEST(CombineSewing, KitAndForgeCombinesPutResultsOnTheCursorQueue) {
    SimWorld w;
    w.items = {{23, 17165}, {24, 13753}, {25, 21565}, {26, 6027}, {27, 12900}};
    std::string why;
    ASSERT_EQ(RunLeg("combine_sewing", w, 300.0, &why), kScenarioPass) << why;
    EXPECT_EQ(w.ClientItemAt(24), 21565);  // the knife came back first
    EXPECT_EQ(w.ClientItemAt(25), 13782);  // the pelt behind it
    EXPECT_EQ(w.ClientItemAt(26), 12896);  // the forge's ruby
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "/clickobject 178"), w.commands.end());
}

TEST(CombineSewing, AProductThatSkipsTheCursorFails) {
    SimWorld w;
    w.items = {{23, 17165}, {24, 13753}, {25, 21565}, {26, 6027}, {27, 12900}};
    // Simulate auto-inventory instead of the cursor queue: run the pass case
    // up to the combine, then empty the cursor before the leg can see it.
    ScenarioOptions o;
    ScenarioRunner runner(MakeScenario("combine_sewing", o), w, 900.0, 10.0);
    for (int i = 0; i < 300 * 60; ++i) {
        w.OnTick(1.0f / 60.0f);
        if (!w.cursor.empty() && w.ClientItemAt(4010) == 0 && w.cursor.size() == 2) {
            w.items[28] = w.cursor.back();
            w.cursor.clear();
        }
        if (!runner.Tick(1.0f / 60.0f)) break;
    }
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("CURSOR"), std::string::npos) << runner.reason();
}

TEST(TrainSkill, ValueAndPointsMoveOnlyOnTheSkillUpdate) {
    SimWorld w;
    std::string why;
    ASSERT_EQ(RunLeg("train_skill", w, 120.0, &why), kScenarioPass) << why;
    EXPECT_EQ(w.skill0, 2);
    EXPECT_EQ(w.points, 159);
    EXPECT_NE(why.find("MODEL skill0=2 points=159"), std::string::npos) << why;
}

TEST(TrainSkill, AnOptimisticLocalWriteFails) {
    SimWorld w;
    w.optimisticTrain = true;
    std::string why;
    EXPECT_EQ(RunLeg("train_skill", w, 120.0, &why), kScenarioFail);
    EXPECT_NE(why.find("BEFORE any OP_SkillUpdate"), std::string::npos) << why;
}

TEST(FriendsWho, AddWhoAndRemove) {
    SimWorld w;
    std::string why;
    ASSERT_EQ(RunLeg("friends_who", w, 120.0, &why), kScenarioPass) << why;
}

TEST(FriendsWho, TheCommandEchoIsNotTheReply) {
    SimWorld w;
    w.otherPlayers = 1;
    // A world whose /friendswho prints nothing but the echo.
    struct Mute : SimWorld {
        void SendCommand(const std::string& line) override {
            if (line == "/friendswho") { commands.push_back(line); chat.push_back("system||/friendswho"); return; }
            SimWorld::SendCommand(line);
        }
    } m;
    std::string why;
    EXPECT_EQ(RunLeg("friends_who", m, 120.0, &why), kScenarioFail);
    EXPECT_NE(why.find("friendswho"), std::string::npos) << why;
}

TEST(IgnoreTell, ControlShownThenIgnoredTellHidden) {
    SimWorld w;
    std::string why;
    ASSERT_EQ(RunLeg("ignore_tell", w, 200.0, &why), kScenarioPass) << why;
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "/ignore Testbuddy"), w.commands.end());
}

TEST(IgnoreTell, AShownIgnoredTellFails) {
    SimWorld w;
    w.ignoreFilterBroken = true;
    std::string why;
    EXPECT_EQ(RunLeg("ignore_tell", w, 200.0, &why), kScenarioFail);
    EXPECT_NE(why.find("SHOWN"), std::string::npos) << why;
}

TEST(IgnoreTell, NoPeerNameRefusesFast) {
    SimWorld w;
    std::string why;
    EXPECT_EQ(RunLeg("ignore_tell", w, 30.0, &why, ""), kScenarioFail);
    EXPECT_NE(why.find("--scenario-cast-target"), std::string::npos) << why;
}

TEST(SocialPeer, SendsTwoTellsThirtySecondsApart) {
    SimWorld w;
    std::string why;
    ASSERT_EQ(RunLeg("social_peer", w, 200.0, &why, "Testchar"), kScenarioPass) << why;
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "/tell Testchar UIGAPLIVE-tell-1"),
              w.commands.end());
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "/tell Testchar UIGAPLIVE-tell-2"),
              w.commands.end());
}

TEST(TrackList, ListCadenceAndLostTarget) {
    SimWorld w;
    w.peerLeavesAt = 80.0;
    std::string why;
    ASSERT_EQ(RunLeg("track_list", w, 300.0, &why), kScenarioPass) << why;
    EXPECT_NE(why.find("mean interval=2.0"), std::string::npos) << why;
}

TEST(TrackList, ASlowCadenceFails) {
    SimWorld w;
    w.directionEvery = 4.0;
    w.peerLeavesAt = 200.0;
    std::string why;
    EXPECT_EQ(RunLeg("track_list", w, 300.0, &why), kScenarioFail);
    EXPECT_NE(why.find("cadence"), std::string::npos) << why;
}

TEST(TrackList, NoLostTargetLineFails) {
    SimWorld w;  // the peer never leaves
    std::string why;
    EXPECT_EQ(RunLeg("track_list", w, 400.0, &why), kScenarioFail);
    EXPECT_NE(why.find("lost your target"), std::string::npos) << why;
}

TEST(TrackPeer, StaysWalksOffAndCampsOut) {
    SimWorld w;
    ScenarioRunner runner(MakeScenario("track_peer", {}), w, 900.0, 10.0);
    for (int i = 0; i < 100 * 60 && runner.Tick(1.0f / 60.0f); ++i) {
        if (std::find(w.commands.begin(), w.commands.end(), "/camp") != w.commands.end()) {
            w.zonedIn = false;
        }
    }
    EXPECT_EQ(runner.exitCode(), kScenarioPass) << runner.reason();
}

// ===========================================================================
// UIGAP-live2: trade / give (#212), drop / destroy / coin (#151), bank
// Change (#26). TradeSim answers the new IScenarioWorld surface the way the
// client model and the server are expected to (review/UIGAP-ORACLE/212, 151).
// ===========================================================================

namespace {

class TradeSim : public SimWorld {
public:
    int flags = 0;
    std::vector<long long> purse{10, 0, 0, 0};
    std::vector<long long> bank{3, 0, 0, 0};
    long long cursorCoin = 0;
    long long objects = 5;
    // knobs: each makes one assertion fail
    bool printOnComplete = false;
    bool silentCancel = false;
    bool groundObjectAppears = true;
    bool questTakesFang = true;
    bool changeDoesNothing = false;
    // the peer's timeline (trade_peer): the actor's two trades
    bool peerMode = false;

    int TradeFlags() const override { return flags; }
    long long FindItemSlot(long long id) const override {
        for (const auto& [slot, item] : items)
            if (item == id && (slot < 3000 || slot > 3007)) return slot;
        if (!cursor.empty() && cursor.front() == id) return 33;
        for (const auto& [slot, item] : items)
            if (item == id) return slot;
        return 0;
    }
    std::vector<long long> Coins(bool b) const override { return b ? bank : purse; }
    long long CursorCoinCopper() const override { return cursorCoin; }
    long long WorldObjectCount() const override { return objects; }

    bool ClickEntity(const std::string& name) override {
        commands.push_back("CLICKENTITY " + name);
        if (cursor.empty() && cursorCoin == 0) return true;  // just a target
        flags = IScenarioWorld::kTradeActive | (name == "Tillin" ? IScenarioWorld::kTradeWithNpc : 0);
        items[3000] = cursor.front();
        cursor.pop_front();
        return true;
    }
    bool TradeButton(int op) override {
        commands.push_back("TRADEBUTTON " + std::to_string(op));
        if (op == 2) { cursorCoin = 0; return true; }
        if (op == 1) {
            items[FirstFree()] = items[3000];
            items.erase(3000);
            flags = 0;
            if (!silentCancel) System("You have cancelled the trade.");
            return true;
        }
        flags |= IScenarioWorld::kTradeOwnAccepted;
        if (peerMode) { peerCompleteAt = clock + 1.0; return true; }
        if (flags & IScenarioWorld::kTradeWithNpc) {
            const long long given = items[3000];
            items.erase(3000);
            flags = 0;
            if (given == 13915 && questTakesFang) {
                chat.push_back("say|Captain Tillin|Very good! One less gnoll the people of Qeynos need to fear.");
                cursor.push_back(10070);
            } else {
                cursor.push_back(given);
            }
            return true;
        }
        completeAt = clock + 2.0;  // the partner presses Trade
        return true;
    }
    bool CursorGesture(int op, int, int qty, int src) override {
        commands.push_back("GESTURE " + std::to_string(op));
        auto& store = src == 2 ? bank : purse;
        switch (op) {
            case 3:
                if (cursorCoin == 0) { store[0] -= qty; cursorCoin = qty * 1000; }
                else { store[0] += cursorCoin / 1000; cursorCoin = 0; }
                break;
            case 5: case 7: Event(GameEventType::ConfirmationRequested); break;
            case 6: cursor.pop_front(); if (groundObjectAppears) ++objects; break;
            case 8: cursor.pop_front(); break;
            default: return false;
        }
        return true;
    }
    void SendCommand(const std::string& line) override {
        if (line == "/coin pp 5") { commands.push_back(line); purse[0] -= 5; cursorCoin = 5000; return; }
        if (line == "/bank change") {
            commands.push_back(line);
            if (!changeDoesNothing) { purse = {2, 9, 9, 3}; bank = {4, 5, 4, 0}; }
            return;
        }
        SimWorld::SendCommand(line);
    }
    void Tick(double dt) {
        OnTick(dt);
        if (completeAt >= 0 && clock >= completeAt) {
            completeAt = -1;
            items.erase(3000);
            flags = 0;
            if (printOnComplete) System("The trade is complete.");
        }
        if (!peerMode) return;
        // the actor: opens at 30 s, offers + accepts at 40 s; second trade
        // at 70 s, cancelled at 80 s
        if (clock >= 30.0 && phase == 0) { phase = 1; flags = IScenarioWorld::kTradeActive; }
        if (clock >= 40.0 && phase == 1) {
            phase = 2;
            flags |= IScenarioWorld::kTradePartnerOffered | IScenarioWorld::kTradePartnerAccepted;
        }
        if (peerCompleteAt >= 0 && clock >= peerCompleteAt) {
            peerCompleteAt = -1;
            flags = 0;
            items[24] = 7007;
        }
        if (clock >= 70.0 && phase == 2) { phase = 3; flags = IScenarioWorld::kTradeActive; }
        if (clock >= 80.0 && phase == 3) { phase = 4; flags = 0; System("You have cancelled the trade."); }
    }

private:
    int FirstFree() const {
        for (int s = 23; s <= 30; ++s) if (!items.count(s)) return s;
        return 30;
    }
    double completeAt = -1.0;
    double peerCompleteAt = -1.0;
    int phase = 0;
};

int RunTradeLeg(const std::string& name, TradeSim& world, double seconds, std::string* reason = nullptr,
                const std::string& peer = "Testbuddy") {
    ScenarioOptions o;
    o.castTargetName = peer;
    ScenarioRunner runner(MakeScenario(name, o), world, /*timeout=*/900.0, /*startDeadline=*/10.0);
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < static_cast<int>(seconds * 60.0); ++i) {
        world.Tick(dt);
        if (!runner.Tick(dt)) break;
    }
    if (reason) *reason = runner.reason();
    return runner.finished() ? runner.exitCode() : -1;
}

TradeSim TradeFixture() {
    TradeSim w;
    w.items = {{23, 17005}, {24, 7007}, {25, 16598}, {26, 13915}};
    return w;
}

}  // namespace

TEST(UiLegs2Registry, TheFiveUigapLive2ScenariosAreRegistered) {
    for (const char* n : {"trade_roundtrip", "trade_peer", "give_npc", "drop_destroy_coin", "bank_change"}) {
        EXPECT_TRUE(IsScenarioName(n)) << n;
    }
}

TEST(TradeRoundtrip, DaggerAndCoinCompleteSilentlyThenACancelPrints1450) {
    TradeSim w = TradeFixture();
    std::string why;
    ASSERT_EQ(RunTradeLeg("trade_roundtrip", w, 200.0, &why), kScenarioPass) << why;
    EXPECT_EQ(w.FindItemSlot(7007), 0);     // the dagger left us
    EXPECT_GT(w.FindItemSlot(16598), 0);    // the cancelled bottle came back
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "CLICKENTITY Testbuddy"), w.commands.end());
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "TRADEBUTTON 2"), w.commands.end());
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "TRADEBUTTON 1"), w.commands.end());
}

TEST(TradeRoundtrip, ACompletionLineFails) {
    TradeSim w = TradeFixture();
    w.printOnComplete = true;
    std::string why;
    EXPECT_EQ(RunTradeLeg("trade_roundtrip", w, 200.0, &why), kScenarioFail);
    EXPECT_NE(why.find("completion line"), std::string::npos) << why;
}

TEST(TradeRoundtrip, ACancelWithoutEqstr1450Fails) {
    TradeSim w = TradeFixture();
    w.silentCancel = true;
    std::string why;
    EXPECT_EQ(RunTradeLeg("trade_roundtrip", w, 200.0, &why), kScenarioFail);
    EXPECT_NE(why.find("Cancel"), std::string::npos) << why;
}

TEST(TradeRoundtrip, NoPeerNameRefusesFast) {
    TradeSim w = TradeFixture();
    std::string why;
    EXPECT_EQ(RunTradeLeg("trade_roundtrip", w, 60.0, &why, ""), kScenarioFail);
    EXPECT_NE(why.find("--scenario-cast-target"), std::string::npos) << why;
}

TEST(TradePeer, AcceptsAfterTheActorAndSeesTheCancel) {
    TradeSim w;
    w.peerMode = true;
    w.items = {{23, 17005}};
    std::string why;
    ASSERT_EQ(RunTradeLeg("trade_peer", w, 200.0, &why, "Testchar"), kScenarioPass) << why;
    EXPECT_EQ(w.FindItemSlot(7007), 24);
}

TEST(GiveNpc, TheQuestTakesTheFangAndPaysAMoonstone) {
    TradeSim w = TradeFixture();
    std::string why;
    ASSERT_EQ(RunTradeLeg("give_npc", w, 120.0, &why), kScenarioPass) << why;
    EXPECT_EQ(w.FindItemSlot(13915), 0);
    EXPECT_EQ(w.FindItemSlot(10070), 33);
}

TEST(GiveNpc, AFangHandedBackFails) {
    TradeSim w = TradeFixture();
    w.questTakesFang = false;
    std::string why;
    EXPECT_EQ(RunTradeLeg("give_npc", w, 120.0, &why), kScenarioFail);
    EXPECT_NE(why.find("Give"), std::string::npos) << why;
}

TEST(DropDestroyCoin, ConfirmationsCoinAndBankCoin) {
    TradeSim w = TradeFixture();
    std::string why;
    ASSERT_EQ(RunTradeLeg("drop_destroy_coin", w, 200.0, &why), kScenarioPass) << why;
    EXPECT_EQ(w.FindItemSlot(16598), 0);
    EXPECT_EQ(w.FindItemSlot(7007), 0);
    EXPECT_EQ(w.purse[0], 8);
    EXPECT_EQ(w.bank[0], 5);
    EXPECT_EQ(w.objects, 6);
}

TEST(DropDestroyCoin, ADropWithNoGroundObjectFails) {
    TradeSim w = TradeFixture();
    w.groundObjectAppears = false;
    std::string why;
    EXPECT_EQ(RunTradeLeg("drop_destroy_coin", w, 200.0, &why), kScenarioFail);
    EXPECT_NE(why.find("ground object"), std::string::npos) << why;
}

TEST(BankChange, BothPursesGoToTheFewestCoins) {
    TradeSim w;
    w.purse = {0, 25, 37, 123};
    w.bank = {3, 14, 9, 50};
    std::string why;
    ASSERT_EQ(RunTradeLeg("bank_change", w, 60.0, &why), kScenarioPass) << why;
    EXPECT_NE(std::find(w.commands.begin(), w.commands.end(), "/bank change"), w.commands.end());
}

TEST(BankChange, AChangeThatMovesNothingFails) {
    TradeSim w;
    w.purse = {0, 25, 37, 123};
    w.bank = {3, 14, 9, 50};
    w.changeDoesNothing = true;
    std::string why;
    EXPECT_EQ(RunTradeLeg("bank_change", w, 60.0, &why), kScenarioFail);
    EXPECT_NE(why.find("/bank change"), std::string::npos) << why;
}
