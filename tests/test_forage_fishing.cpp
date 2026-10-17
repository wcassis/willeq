// B894 (GitLab #210) -- Forage (skill 27) and Fishing (skill 55) are sent but
// the client-side preconditions are missing: eqstr 148/149 (forage while
// attacking / not standing), 156/159/160/161/162 (already fishing, fishing
// while attacking, no pole, pole not in primary, no bait) and 165 (moving
// stops fishing). This file is the offline leg: every refusal case must
// refuse with the SHIPPED eqstr line (fetched through the one eqstr
// formatter, eqt::EqStrLine, so a transcription typo is invisible) and send
// NOTHING, and a satisfied precondition must send exactly one empty
// OP_Forage / OP_Fishing -- the wire form T152 audited for these two
// opcodes.
//
// Same headless EverQuest + TestBridge seam as test_skill_gate_bypasses.cpp;
// no network, no graphics, no eqemu. The refusal reason is observed through
// the D20b2 activation callback wiring in everquest.cpp ("Cannot use {}:" +
// the SkillManager's getSkillUseError text), the exact channel the T162
// tests read. "Sends nothing" is witnessed two ways: the skill's cooldown
// stays cold (activateSkill only starts the recast after
// sendSkillUsePacket) and the queued-packet capture (PacketSender's
// Test_ seams) stays empty of HC_OP_Forage / HC_OP_Fishing.
//
// RED-FIRST STATE (2026-10-08): the not-trained test below is the only test
// in the file for the first commit; it exercises the T159 possession gate
// that already exists, so it is the GREEN baseline. The forage/fishing
// precondition tests are added one at a time, each RED before its fix.

#include <gtest/gtest.h>

#include "zone_stream_test_utils.h"

#include "client/display_eqstr.h"
#include "client/skill/skill_constants.h"
#include "client/skill/skill_cap_table.h"
#include "client/skill/skill_manager.h"
#include "client/combat.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/string_database.h"
#include "client/willeq_database.h"
#include "common/net/packet.h"
#include "common/packets/merchant.h"  // EQT::ITEM_PACKET_INVENTORY

#include <string>
#include <vector>

using namespace zonetest;
using namespace eqt::inventory;  // SLOT_PRIMARY, GENERAL_BEGIN, ...
// NO `using eqt::state::PositionState` here: eq_enums.h declares a global
// `enum PositionState` (POS_*) and the two names cannot share this scope.
using eqt::state::PlayerPositionStateChangedData;
using eqt::state::PlayerMovedData;
using eqt::state::GameEvent;
using eqt::state::GameEventType;

namespace {

const char* const kShippedCaps = WILLEQ_REPO_PATH("config/skill_caps.json");  // B1090: run-time root

// Class ids as the server (and the PlayerProfile) numbers them.
constexpr uint8_t kWarrior = 1;
constexpr uint8_t kRanger  = 4;
constexpr uint8_t kDruid   = 6;
constexpr uint8_t kHuman   = 1;
constexpr uint8_t kLevel   = 30;

// The two skills B894 names, and their dedicated opcodes (getSkillOpcode,
// skill_manager.cpp: Forage 27 -> HC_OP_Forage, Fishing 55 -> HC_OP_Fishing).
constexpr uint8_t kForage   = 27;
constexpr uint8_t kFishing  = 55;
constexpr uint16_t kOpForage   = HC_OP_Forage;
constexpr uint16_t kOpFishing  = HC_OP_Fishing;

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

// The inbound zone path (a replayed OP_SimpleMessage) publishes a
// ChatMessage event, not a SystemMessage one (ChatComponent's
// ZoneProcessSimpleMessage pushes GameEventType::ChatMessage).
std::vector<std::string> DrainChatMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::ChatMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

bool AnyContains(const std::vector<std::string>& msgs, const std::string& needle) {
    for (const auto& m : msgs) {
        if (m.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// True if `eq` queued a packet for `opcode` since the last ClearQueuedPackets.
bool Sent(const EverQuest& eq, uint16_t opcode) {
    for (const auto& p : eq.Test_GetQueuedPackets()) {
        if (p.opcode == opcode) {
            return true;
        }
    }
    return false;
}

class ForageFishingTest : public ::testing::Test {
protected:
    ForageFishingTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~ForageFishingTest() override { eq.setBridge(nullptr); }

    void SetUp() override {
        ASSERT_TRUE(EQ::SkillCapTable::instance().load(kShippedCaps))
            << "the shipped config/skill_caps.json must load: " << kShippedCaps;
        // The eqstr ids this unit's refusal lines come from, fetched from the
        // SHIPPED string table, not transcribed here.
        ASSERT_TRUE(db_.open(WILLEQ_DB_REAL_PATH)) << WILLEQ_DB_REAL_PATH;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
        ASSERT_TRUE(strings_.initFromDatabase());
        // The SAME rows, loaded into the EverQuest-under-test's own string db
        // so the SkillManager's refusal fetches the SHIPPED line and the
        // literal fallback (a never-loaded table) cannot be what the test
        // sees.
        ASSERT_TRUE(eq.Strings().LoadStringFiles(WILLEQ_REPO_ROOT));
    }
    void TearDown() override {
        EQ::SkillCapTable::instance().load(kShippedCaps);
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    EQ::SkillManager& Skills(uint8_t player_class, uint8_t level = kLevel) {
        // EXPECT, not ASSERT: this returns a reference, and an ASSERT_* would
        // `return;` its way out of the wrong type (compile error).
        auto* sm = eq.GetSkillManager();
        EXPECT_NE(sm, nullptr);
        sm->initialize(player_class, kHuman, level);
        return *sm;
    }

    // The shipped line for an eqstr id, via the one formatter.
    std::string Line(uint32_t id) const { return eqt::EqStrLine(strings_, id); }

    // The eqstr ids B894's table names (eqstr_us.txt, verified against
    // data/willeq.db's eqstrings below).
    static constexpr uint32_t kEqstrForageAttacking = 148;  // You can't try to forage while attacking.
    static constexpr uint32_t kEqstrForageStanding  = 149;  // You must be standing to forage.
    static constexpr uint32_t kEqstrAlreadyFishing  = 156;  // You are already fishing!
    static constexpr uint32_t kEqstrFishingAttacking = 159; // You can't start fishing while attacking!
    static constexpr uint32_t kEqstrNoPole          = 160;  // ...without a fishing pole...
    static constexpr uint32_t kEqstrPolePrimary     = 161;  // ...pole in your primary hand.
    static constexpr uint32_t kEqstrNoBait          = 162;  // ...without fishing bait...
    static constexpr uint32_t kEqstrForageFood      = 152;  // You have scrounged up some food.
    static constexpr uint32_t kEqstrStopFishing     = 165;  // You stop fishing and go on your way.

    EverQuest eq;
    TestBridge bridge;
    EQT::WilleqDatabase db_;
    EQT::StringDatabase strings_;
};

// ===================================================================
// 0. The data the table gates on
// ===================================================================
//
// config/skill_caps.json (peq.skill_caps export) says: Forage is Ranger(4),
// Druid(6), Bard(8) and Monk(12)-adjacent rows only; Fishing is EVERY class
// (skill 55 appears in all sixteen rows). Warrior has neither skill 27 nor a
// way past the possession gate for it. Pin those two rows so a caps-file
// change fails here instead of silently re-opening a refusal the live lane
// already proved.

TEST_F(ForageFishingTest, CapTableSaysWhoMayForageAndEveryoneMayFish) {
    const auto& table = EQ::SkillCapTable::instance();
    ASSERT_TRUE(table.isLoaded());
    EXPECT_FALSE(table.classCanEverHaveSkill(kWarrior, kForage))
        << "Warrior must not own Forage in the shipped caps";
    EXPECT_TRUE(table.classCanEverHaveSkill(kRanger, kForage));
    EXPECT_TRUE(table.classCanEverHaveSkill(kDruid, kForage));
    for (uint8_t cls = 1; cls <= 16; ++cls) {
        EXPECT_TRUE(table.classCanEverHaveSkill(cls, kFishing))
            << "class " << int(cls) << " must be able to fish";
    }
    // The shipped eqstr rows this unit's refusals draw -- they exist.
    EXPECT_EQ(Line(kEqstrForageAttacking), "You can't try to forage while attacking.");
    EXPECT_EQ(Line(kEqstrForageStanding), "You must be standing to forage.");
    EXPECT_EQ(Line(kEqstrAlreadyFishing), "You are already fishing!");
    EXPECT_EQ(Line(kEqstrFishingAttacking), "You can't start fishing while attacking!");
    EXPECT_EQ(Line(kEqstrNoPole), "You can't fish without a fishing pole, go buy one.");
    EXPECT_EQ(Line(kEqstrPolePrimary), "You need to put your fishing pole in your primary hand.");
    EXPECT_EQ(Line(kEqstrNoBait), "You can't fish without fishing bait, go buy some.");
    EXPECT_EQ(Line(kEqstrStopFishing), "You stop fishing and go on your way.");
}

// ===================================================================
// 1. Not trained: the possession gate refuses with the right line, sends
//    nothing. (GREEN baseline -- this is T159's gate, exercised for the two
//    B894 skills for the first time.)
// ===================================================================

TEST_F(ForageFishingTest, ForageRefusedWhenTheClassCannotHaveIt) {
    auto& skills = Skills(kWarrior);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    ASSERT_FALSE(skills.canUseSkill(kForage));
    const std::string error = skills.getSkillUseError(kForage);
    ASSERT_EQ(error, "Your class cannot use Forage");

    EXPECT_FALSE(skills.activateSkill(kForage));
    EXPECT_FALSE(skills.isSkillOnCooldown(kForage))
        << "a refusal must not have reached sendSkillUsePacket()";
    EXPECT_FALSE(Sent(eq, kOpForage))
        << "no OP_Forage may go out for a skill the class cannot have";

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "Cannot use Forage: Your class cannot use Forage"))
        << "the player must be told the real reason; got " << msgs.size() << " message(s)";
}

// ===================================================================
// 2. Forage while attacking -> eqstr 148, nothing sent.
//    RED: no such gate exists yet; a Ranger in an active attack (auto-attack
//    on, COMBAT_STATE_ENGAGED -- the client's in-combat signal, same witness
//    test_skill_gates uses) sends OP_Forage.
// ===================================================================

TEST_F(ForageFishingTest, ForageRefusedWhileAttackingWithEqstr148) {
    auto* sm = eq.GetSkillManager();
    ASSERT_NE(sm, nullptr);
    sm->initialize(kRanger, kHuman, kLevel);
    auto& skills = *sm;
    skills.updateSkill(kForage, 100);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    auto* combat = eq.GetCombatManager();
    // Auto-attack engages a target; a target is required to engage.
    ReplayZone(eq, HC_OP_NewSpawn,
               NewSpawnPayloadAt(500, "a_probe_dummy", 0, 10, 0));
    ASSERT_TRUE(combat->SetTarget(500));
    combat->EnableAutoAttack();
    ASSERT_TRUE(combat->IsAutoAttackEnabled());
    ASSERT_EQ(combat->GetCombatState(), COMBAT_STATE_ENGAGED);

    EXPECT_FALSE(skills.canUseSkill(kForage))
        << "forage while attacking must refuse (eqstr 148)";
    EXPECT_EQ(skills.getSkillUseError(kForage), Line(kEqstrForageAttacking))
        << "the refusal must be the SHIPPED eqstr 148 line";

    EXPECT_FALSE(skills.activateSkill(kForage));
    EXPECT_FALSE(skills.isSkillOnCooldown(kForage))
        << "a refusal must not have reached sendSkillUsePacket()";
    EXPECT_FALSE(Sent(eq, kOpForage)) << "no OP_Forage may go out while attacking";

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, Line(kEqstrForageAttacking)))
        << "the player must be told " << Line(kEqstrForageAttacking) << "; got "
        << msgs.size() << " message(s)";
}

// ===================================================================
// 3. The rest of the table.
//    Item seeding follows tests/eq_commands_equip_test_fixture.h's idiom:
//    InventoryManager::setItem with a full ItemInstance. The pole/bait
//    witness is ItemInstance::skillType (the full ItemType, eqemu's
//    item->ItemType off serialized field 56): FishingPole is 36, Bait 37
//    (common_t common/item_data.h).
// ===================================================================

void SeedPole(EverQuest& eq, int16_t slot) {
    ItemInstance pole;
    pole.itemId = 99001;
    pole.name = "Worn Fishing Rod";
    pole.skillType = 36;  // ItemTypeFishingPole
    eq.GetInventoryManager()->setItem(slot, pole);
}

void SeedBait(EverQuest& eq, int16_t slot) {
    ItemInstance bait;
    bait.itemId = 99002;
    bait.name = "Fishing Bait";
    bait.skillType = 37;  // ItemTypeFishingBait
    eq.GetInventoryManager()->setItem(slot, bait);
}

// A ready fisher: pole in primary, bait in general, nothing else.
void ReadyFisher(EverQuest& eq) {
    SeedPole(eq, SLOT_PRIMARY);
    SeedBait(eq, GENERAL_BEGIN);
}

// Rewind a skill's recast so the COOLDOWN gate cannot be the reason a
// second activation refuses -- the refusal under test must be the one that
// sits behind it.
void ExpireRecast(EQ::SkillManager& skills, uint8_t skill_id) {
    skills.getSkill(skill_id)->last_used =
        std::chrono::steady_clock::now() - std::chrono::hours(1);
}

TEST_F(ForageFishingTest, ForageRefusedWhileSittingWithEqstr149) {
    auto& skills = Skills(kRanger);
    skills.updateSkill(kForage, 100);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    eq.GetGameState().player().setPositionState(eqt::state::PositionState::Sitting);
    ASSERT_EQ(eq.GetGameState().player().positionState(), eqt::state::PositionState::Sitting);

    EXPECT_FALSE(skills.canUseSkill(kForage));
    EXPECT_EQ(skills.getSkillUseError(kForage), Line(kEqstrForageStanding));
    EXPECT_FALSE(skills.activateSkill(kForage));
    EXPECT_FALSE(Sent(eq, kOpForage));

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, Line(kEqstrForageStanding)));

    // Back to standing clears the gate: the same activation now sends.
    eq.GetGameState().player().setPositionState(eqt::state::PositionState::Standing);
    EXPECT_TRUE(skills.canUseSkill(kForage));
    EXPECT_TRUE(skills.activateSkill(kForage));
    EXPECT_TRUE(Sent(eq, kOpForage));
}

TEST_F(ForageFishingTest, FishingRefusedWithoutAPoleWithEqstr160) {
    auto& skills = Skills(kWarrior);
    skills.updateSkill(kFishing, 100);
    ReadyFisher(eq);
    // Take the pole AND the bait: nothing anywhere.
    eq.GetInventoryManager()->setItem(SLOT_PRIMARY, ItemInstance());
    eq.GetInventoryManager()->setItem(GENERAL_BEGIN, ItemInstance());
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    EXPECT_FALSE(skills.canUseSkill(kFishing));
    EXPECT_EQ(skills.getSkillUseError(kFishing), Line(kEqstrNoPole))
        << "no pole anywhere is 160 (buy one), not 161 (equip it)";
    EXPECT_FALSE(skills.activateSkill(kFishing));
    EXPECT_FALSE(Sent(eq, kOpFishing));

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, Line(kEqstrNoPole)));
}

TEST_F(ForageFishingTest, FishingRefusedWithAPoleOutOfPrimaryWithEqstr161) {
    auto& skills = Skills(kWarrior);
    skills.updateSkill(kFishing, 100);
    ReadyFisher(eq);
    // The pole is carried -- in general 2, not primary.
    auto pole = *eq.GetInventoryManager()->getItem(SLOT_PRIMARY);
    eq.GetInventoryManager()->setItem(SLOT_PRIMARY, ItemInstance());
    eq.GetInventoryManager()->setItem(GENERAL_BEGIN + 1, pole);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    EXPECT_FALSE(skills.canUseSkill(kFishing));
    EXPECT_EQ(skills.getSkillUseError(kFishing), Line(kEqstrPolePrimary))
        << "a carried pole is 161 (put it in primary), not 160 (buy one)";
    EXPECT_FALSE(skills.activateSkill(kFishing));
    EXPECT_FALSE(Sent(eq, kOpFishing));

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, Line(kEqstrPolePrimary)));
}

TEST_F(ForageFishingTest, FishingRefusedWithoutBaitWithEqstr162) {
    auto& skills = Skills(kWarrior);
    skills.updateSkill(kFishing, 100);
    ReadyFisher(eq);
    // Pole in primary; the bait is gone.
    eq.GetInventoryManager()->setItem(GENERAL_BEGIN, ItemInstance());
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    EXPECT_FALSE(skills.canUseSkill(kFishing));
    EXPECT_EQ(skills.getSkillUseError(kFishing), Line(kEqstrNoBait));
    EXPECT_FALSE(skills.activateSkill(kFishing));
    EXPECT_FALSE(Sent(eq, kOpFishing));

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, Line(kEqstrNoBait)));
}

TEST_F(ForageFishingTest, FishingRefusedWhileAttackingWithEqstr159) {
    auto& skills = Skills(kWarrior);
    skills.updateSkill(kFishing, 100);
    ReadyFisher(eq);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    auto* combat = eq.GetCombatManager();
    ReplayZone(eq, HC_OP_NewSpawn,
               NewSpawnPayloadAt(500, "a_probe_dummy", 0, 10, 0));
    ASSERT_TRUE(combat->SetTarget(500));
    combat->EnableAutoAttack();
    ASSERT_EQ(combat->GetCombatState(), COMBAT_STATE_ENGAGED);

    EXPECT_FALSE(skills.canUseSkill(kFishing));
    EXPECT_EQ(skills.getSkillUseError(kFishing), Line(kEqstrFishingAttacking));
    EXPECT_FALSE(skills.activateSkill(kFishing));
    EXPECT_FALSE(skills.isFishing());
    EXPECT_FALSE(Sent(eq, kOpFishing));

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, Line(kEqstrFishingAttacking)));
}

// A satisfied precondition sends exactly one empty OP_Fishing and the cast
// goes live.
TEST_F(ForageFishingTest, AReadyFisherSendsOneEmptyOpFishing) {
    auto& skills = Skills(kWarrior);
    skills.updateSkill(kFishing, 100);
    ReadyFisher(eq);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    ASSERT_TRUE(skills.canUseSkill(kFishing));
    ASSERT_TRUE(skills.activateSkill(kFishing));
    ASSERT_TRUE(skills.isFishing());

    const EverQuest::TestQueuedPacket* fishing = nullptr;
    size_t fishing_sends = 0;
    for (const auto& p : eq.Test_GetQueuedPackets()) {
        if (p.opcode == kOpFishing) {
            ++fishing_sends;
            fishing = &p;
        }
    }
    ASSERT_EQ(fishing_sends, 1u) << "exactly one OP_Fishing";
    EXPECT_TRUE(fishing->bytes.empty()) << "empty body, T152's wire form";
}

// The already-fishing refusal: the first cast went out (and is still live),
// the recast is expired so the COOLDOWN gate cannot be the reason -- the
// 156 refusal is the one that fires.
TEST_F(ForageFishingTest, ASecondCastWhileFishingIsRefusedWithEqstr156) {
    auto& skills = Skills(kWarrior);
    skills.updateSkill(kFishing, 100);
    ReadyFisher(eq);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    ASSERT_TRUE(skills.activateSkill(kFishing));
    ASSERT_TRUE(skills.isFishing());
    ExpireRecast(skills, kFishing);  // not the cooldown that refuses
    ASSERT_EQ(skills.getSkillCooldownRemaining(kFishing), 0u);

    EXPECT_FALSE(skills.canUseSkill(kFishing));
    EXPECT_EQ(skills.getSkillUseError(kFishing), Line(kEqstrAlreadyFishing));
    EXPECT_FALSE(skills.activateSkill(kFishing));
    EXPECT_TRUE(skills.isFishing()) << "the first cast is still live";
    EXPECT_EQ(skills.getSkillCooldownRemaining(kFishing), 0u)
        << "the refusal must not have started a second recast";
    size_t fishing_now = 0;
    for (const auto& p : eq.Test_GetQueuedPackets())
        if (p.opcode == kOpFishing) ++fishing_now;
    EXPECT_EQ(fishing_now, 1u) << "no second OP_Fishing goes out";

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, Line(kEqstrAlreadyFishing)));
}

// Moving ends a live cast with eqstr 165 (the movement integrator's own
// PlayerMoved is the trigger; the test publishes the same event the
// integrator publishes, isMoving=true).
TEST_F(ForageFishingTest, MovingEndsAFishingCastWithEqstr165) {
    auto& skills = Skills(kWarrior);
    skills.updateSkill(kFishing, 100);
    ReadyFisher(eq);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    ASSERT_TRUE(skills.activateSkill(kFishing));
    ASSERT_TRUE(skills.isFishing());

    eq.GetGameState().events().publish(GameEvent(
        GameEventType::PlayerMoved,
        PlayerMovedData{10.0f, 10.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true}));

    EXPECT_FALSE(skills.isFishing());
    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, Line(kEqstrStopFishing)))
        << "moving must print eqstr 165; got " << msgs.size() << " message(s)";

    // A discrete relocation (isMoving=false) must NOT end a cast.
    ASSERT_TRUE(skills.activateSkill(kFishing));
    ASSERT_TRUE(skills.isFishing());
    eq.GetGameState().events().publish(GameEvent(
        GameEventType::PlayerMoved,
        PlayerMovedData{11.0f, 11.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false}));
    EXPECT_TRUE(skills.isFishing()) << "a discrete relocation is not a walk";

    // Sitting ends one too (position state, not movement).
    eq.GetGameState().player().setPositionState(eqt::state::PositionState::Sitting);
    eq.GetGameState().events().publish(GameEvent(
        GameEventType::PlayerPositionStateChanged,
        PlayerPositionStateChangedData{
            static_cast<uint8_t>(eqt::state::PositionState::Sitting)}));
    EXPECT_FALSE(skills.isFishing());
}

// ===================================================================
// 4. The landing leg (acceptance criterion 1b): what eqemu SENDS after a
//    ForageItem()/GoFish() success is a MessageString (Chat::Skills,
//    FORAGE_FOOD) plus the caught item pushed on the cursor. On the wire
//    (eqemu client.cpp:3803, entity.cpp) those are:
//      * OP_SimpleMessage, a 12-byte struct {u32 color; u32 string_id;
//        u32 unknown8}  (forage.cpp:499 -> MessageString(152));
//      * OP_ItemPacket carrying the item serialised at the SERVER cursor
//        slot 33 (forage.cpp:507-508: PushItemOnCursor + SendItemPacket
//        (slotCursor)).
//    Replaying both through the real zone dispatch must land the item on
//    the client cursor AND print the line. No production change: both
//    inbound paths already exist (ZoneComponent::OnItemPacket -> the
//    generic processItemPacket fallback, and Chat's
//    ZoneProcessSimpleMessage); this test is their regression net.
// ===================================================================

// A plain carried item the way titanium.cpp's SerializeItem writes it:
// instance fields 0..11, then a 135-field static block. Field 3 (item) is
// the material id, field 4 (static) is the db id, field 1 (static) the name,
// and instance field 2 is the SLOT the server is placing it in.
std::string CursorItemString(const std::string& name, uint32_t itemId) {
    // eqemu SendItemPacket(slotCursor) serialises at the SERVER cursor slot
    // (invslot::slotCursor == 33); the parser normalises it onto 30.
    const std::string instance =
        "0|0|33|0|1|0|0|0|0|0|0";  // 0|0|<slot>|0|1|0|0|0|0|0|0
    std::vector<std::string> fields(135, "");
    fields[0] = "0";
    fields[1] = name;
    fields[2] = name;
    fields[3] = "IT63";
    fields[4] = std::to_string(itemId);
    fields[5] = "10";
    fields[6] = "1";
    fields[7] = "1";
    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }
    return instance + "\"" + staticData + "\"";
}

// OP_ItemPacket, exactly the shape ZoneComponent::OnItemPacket hands to
// InventoryManager::processItemPacket: 2-byte opcode, u32 packet type,
// the serialised item, a trailing NUL (test_eq_commands_equip_bags.cpp's
// ItemPacketPacket carries the same layout).
void ReplayItemPacket(EverQuest& eq, const std::string& item) {
    EQ::Net::DynamicPacket p;
    p.Resize(2 + 4 + item.size() + 1);
    p.PutUInt16(0, HC_OP_ItemPacket);
    p.PutUInt32(2, EQT::ITEM_PACKET_INVENTORY);
    p.PutData(2 + 4, const_cast<char*>(item.data()), item.size());
    p.PutUInt8(2 + 4 + item.size(), 0);
    std::vector<uint8_t> payload(p.Length() - 2);
    std::memcpy(payload.data(), static_cast<const uint8_t*>(p.Data()) + 2, payload.size());
    ReplayZone(eq, HC_OP_ItemPacket, payload);
}

// OP_SimpleMessage: 2-byte opcode + SimpleMessage_Struct {u32 string_id;
// u32 color; u32 unknown8} (eqemu common/eq_packet_structs.h:3832 -- string_id
// first, then color; ChatComponent's ZoneProcessSimpleMessage reads the same
// order).
void ReplaySimpleMessage(EverQuest& eq, uint32_t stringId, uint32_t color = 0) {
    EQ::Net::DynamicPacket p;
    p.Resize(2 + 12);
    p.PutUInt16(0, HC_OP_SimpleMessage);
    p.PutUInt32(2, stringId);
    p.PutUInt32(6, color);
    p.PutUInt32(10, 0);
    std::vector<uint8_t> payload(p.Length() - 2);
    std::memcpy(payload.data(), static_cast<const uint8_t*>(p.Data()) + 2, payload.size());
    ReplayZone(eq, HC_OP_SimpleMessage, payload);
}

TEST_F(ForageFishingTest, AForagedItemLandsOnTheCursorAndTheLinePrints) {
    // The forage success (forage.cpp:499-508): the food line, then the item
    // on the cursor. String 152 is the FORAGE_FOOD line in the SHIPPED
    // table (fetched through the one formatter, not transcribed here).
    const std::string foodLine = Line(kEqstrForageFood);
    ASSERT_FALSE(foodLine.empty());
    EXPECT_EQ(foodLine, "You have scrounged up some food.");

    eq.GetInventoryManager()->clearAll();
    ASSERT_FALSE(eq.GetInventoryManager()->hasCursorItem());
    DrainSystemMessages(bridge);
    DrainChatMessages(bridge);

    // eqemu pushes the caught food on the cursor, then tells us what it was.
    ReplayItemPacket(eq, CursorItemString("Roasted Beef", 10470));
    ReplaySimpleMessage(eq, kEqstrForageFood);

    // The item landed ON the cursor (normalised server 33 -> client 30).
    ASSERT_TRUE(eq.GetInventoryManager()->hasCursorItem());
    const auto* cursor = eq.GetInventoryManager()->getCursorItem();
    ASSERT_NE(cursor, nullptr);
    EXPECT_EQ(cursor->name, "Roasted Beef");
    EXPECT_EQ(cursor->itemId, 10470u);
    EXPECT_EQ(eq.GetInventoryManager()->getCursorQueueSize(), 1u);
    EXPECT_EQ(cursor->currentSlot, CURSOR_SLOT);

    // The line printed, from the SHIPPED table.
    auto chat = DrainChatMessages(bridge);
    EXPECT_TRUE(AnyContains(chat, foodLine))
        << "the forage success line must print; got " << chat.size() << " chat message(s)";
}

}  // namespace
