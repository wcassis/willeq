// B900 (#216): a single ranged attack.
//
// The server takes a manual ranged attack as OP_CombatAbility with
// m_atk == EQ::invslot::slotRange and m_skill Archery(7) or Throwing(51)
// (akk-stack zone/special_attacks.cpp:376-397; the autofire split reads the
// range item's ItemType, client_process.cpp:345-380). fable's only
// OP_CombatAbility sender, CombatManager::UseAbility, hard-codes m_atk = 100
// (combat.cpp:645), so no fable packet could ever reach RangedAttack()/
// ThrowingAttack() before this.
//
// CombatManager::RangedAttack() is the single-shot trigger shared by /range
// (and its aliases) and the Range Attack key. It:
//   * resolves the range-slot item (SLOT_RANGE = 11, Titanium numbering --
//     the same 11 as EQ::invslot::slotRange, rof2/titanium_limits.h),
//   * picks m_skill from the item's eqemu ItemType (ItemInstance::skillType
//     carries ItemType -- bow=5 -> Archery, throwing=7/19 -> Throwing),
//   * sends OP_CombatAbility {target, slotRange, skill} for exactly one shot,
//   * refuses with the official client's eqstr 9150 and sends NOTHING when
//     no range item is equipped (special_attacks.cpp:880 "you have no bow").
//
// Seam: the T268 queued-packet capture (packet_sender.h) records the exact
// outbound body for OP_CombatAbility; a TestBridge records the refusal
// SystemMessage. A target is replayed as a real NewSpawn so SetTarget has a
// live entity to resolve. EverQuest is non-copyable and non-movable, so (as
// in the test_combat_manager family) each test builds a local one via
// setup() rather than holding one in the fixture.

#include "combat_manager_test_fixture.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
#include "client/skill/skill_constants.h"
#include "client/skill/skill_packet_bodies.h"
#include "client/input/hotkey_manager.h"
#include "client/graphics/ui/options_keys.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace zonetest;
using namespace eqt::inventory;

namespace {

// The official client's refusal string (data/eq_client/eqstr_us.txt:9150).
const char* const kNoRangedWeaponEqstr =
    "You do not have a ranged weapon equipped.";

// Reads the 3x uint32 OP_CombatAbility body (target, m_atk, m_skill) that
// UseAbility/RangedAttack write (combat.cpp:643-646), little-endian.
bool parseCombatAbility(const std::vector<uint8_t>& body, uint32_t& target,
                        uint32_t& atk, uint32_t& skill) {
    if (body.size() != 12) return false;
    auto rd = [&](size_t off) {
        return static_cast<uint32_t>(body[off]) |
               (static_cast<uint32_t>(body[off + 1]) << 8) |
               (static_cast<uint32_t>(body[off + 2]) << 16) |
               (static_cast<uint32_t>(body[off + 3]) << 24);
    };
    target = rd(0);
    atk = rd(4);
    skill = rd(8);
    return true;
}

// The local fixture: a live EverQuest with the T268 capture armed and one
// resolved target, plus the shared read-back helpers.
struct RangedHarness {
    RangedHarness() : eq(MakeEq()) {
        eq.setBridge(&bridge);
        eq.Net().Test_SetCaptureQueuedPacketsForSeam(true);
        eq.Net().Test_ClearQueuedPackets();
        EXPECT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
            NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
        cm = eq.GetCombatManager();
        // Deterministic: the replayed NewSpawn above populates the entity
        // table (see test_combat_manager_targeting), so SetTarget(100) holds.
        cm->SetTarget(100);
    }

    void seedRangeItem(uint32_t itemId, uint8_t itemType) {
        ItemInstance item;
        item.itemId = itemId;
        item.skillType = itemType;  // wire field 56 = eqemu ItemType
        eq.GetInventoryManager()->setItem(SLOT_RANGE, item);
    }

    std::vector<const eqt::PacketSender::TestQueuedPacket*> combatAbilityPackets() {
        std::vector<const eqt::PacketSender::TestQueuedPacket*> out;
        for (auto& pkt : eq.Net().Test_GetQueuedPackets()) {
            if (pkt.opcode == HC_OP_CombatAbility) out.push_back(&pkt);
        }
        return out;
    }

    bool sawRefusal() {
        for (auto& e : bridge.drainEvents()) {
            if (e.type == eqt::state::GameEventType::SystemMessage) {
                if (std::get<eqt::state::ChatMessageData>(e.data).message ==
                    kNoRangedWeaponEqstr) {
                    return true;
                }
            }
        }
        return false;
    }

    EverQuest eq;
    zonetest::TestBridge bridge;
    CombatManager* cm = nullptr;
};

// Drives one RangedAttack and returns the single OP_CombatAbility packet (or
// null when nothing / more than one was sent), so each case asserts on the
// exact bytes without re-seeding.
const eqt::PacketSender::TestQueuedPacket* fireOne(RangedHarness& h) {
    h.cm->RangedAttack();
    const auto pkts = h.combatAbilityPackets();
    if (pkts.size() != 1) return nullptr;
    return pkts[0];
}

// ---------------------------------------------------------------------------
// Archery: a bow in the range slot -> m_atk = slotRange(11), m_skill = 7.
// ---------------------------------------------------------------------------
TEST(RangedAttackWire, BowInRangeSlot_SendsSlotRangeWithArchery) {
    RangedHarness h;
    h.seedRangeItem(8003, EQ::skill::kItemTypeBow);

    const auto* pkt = fireOne(h);
    ASSERT_NE(pkt, nullptr) << "exactly one shot per RangedAttack call";
    ASSERT_EQ(pkt->opcode, HC_OP_CombatAbility);
    uint32_t target = 0, atk = 0, skill = 0;
    ASSERT_TRUE(parseCombatAbility(pkt->bytes, target, atk, skill));
    EXPECT_EQ(target, 100u);
    EXPECT_EQ(atk, 11u) << "m_atk must be slotRange (11), not UseAbility's 100";
    EXPECT_EQ(skill, static_cast<uint32_t>(EQ::kSkillArchery));
}

// ---------------------------------------------------------------------------
// Throwing: a throwing item in the range slot -> m_skill = 51.
// ---------------------------------------------------------------------------
TEST(RangedAttackWire, ThrowingInRangeSlot_SendsSlotRangeWithThrowing) {
    RangedHarness h;
    h.seedRangeItem(8001, EQ::skill::kItemTypeSmallThrowing);

    const auto* pkt = fireOne(h);
    ASSERT_NE(pkt, nullptr);
    uint32_t target = 0, atk = 0, skill = 0;
    ASSERT_TRUE(parseCombatAbility(pkt->bytes, target, atk, skill));
    EXPECT_EQ(target, 100u);
    EXPECT_EQ(atk, 11u);
    EXPECT_EQ(skill, static_cast<uint32_t>(EQ::kSkillThrowing));
}

// A large throwing item routes to Throwing the same way (client_process.cpp:380).
TEST(RangedAttackWire, LargeThrowingInRangeSlot_SendsThrowing) {
    RangedHarness h;
    h.seedRangeItem(8001, EQ::skill::kItemTypeLargeThrowing);

    const auto* pkt = fireOne(h);
    ASSERT_NE(pkt, nullptr);
    uint32_t target = 0, atk = 0, skill = 0;
    ASSERT_TRUE(parseCombatAbility(pkt->bytes, target, atk, skill));
    EXPECT_EQ(atk, 11u);
    EXPECT_EQ(skill, static_cast<uint32_t>(EQ::kSkillThrowing));
}

// ---------------------------------------------------------------------------
// No range item: refuse with eqstr 9150, send nothing.
// ---------------------------------------------------------------------------
TEST(RangedAttackWire, NoRangeItem_RefusesWithEqstrAndSendsNothing) {
    RangedHarness h;

    h.cm->RangedAttack();

    EXPECT_TRUE(h.combatAbilityPackets().empty())
        << "no OP_CombatAbility may leave the client with no range weapon";
    EXPECT_TRUE(h.sawRefusal()) << "the refusal must use the official eqstr 9150";
}

// ---------------------------------------------------------------------------
// A non-range item (an arrow in the range slot) is not a ranged weapon: it is
// neither bow nor throwing, so it refuses exactly like "no item".
// ---------------------------------------------------------------------------
TEST(RangedAttackWire, NonBowNonThrowingItemInRangeSlot_Refuses) {
    RangedHarness h;
    h.seedRangeItem(8005, 27 /* EQ::item::ItemTypeArrow */);

    h.cm->RangedAttack();

    EXPECT_TRUE(h.combatAbilityPackets().empty());
    EXPECT_TRUE(h.sawRefusal());
}

// ---------------------------------------------------------------------------
// Key leg (B900 criterion 2): the Range Attack action is listed on the
// Options Keys page, is UNBOUND by default (the oracle's default key is
// [assumed]-owed -- Leg O reads it from the live eqclient.ini [KeyMaps]), and
// its two device-bound arms route the press to the same dispatch as /range.
// ---------------------------------------------------------------------------

namespace {
std::string ReadRepoFile(const std::string& rel) {
    const std::filesystem::path p = WILLEQ_REPO_ROOT;
    std::ifstream in(p / rel);
    EXPECT_TRUE(in.good()) << "cannot open " << rel;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace

// The Options Keys page must list it, so the user can bind the [assumed]-owed
// default key. test_options_wiring pins that EVERY action is in exactly one
// category; this pins that it is reachable AND that binding it is a no-op
// while unbound.
TEST(RangedAttackKey, ListedOnTheOptionsKeysPage) {
    bool found = false;
    for (int c = 0; c < EQT::Graphics::OptionsKeys::kCategoryCount; ++c) {
        const auto& actions = EQT::Graphics::OptionsKeys::actionsIn(c);
        for (const auto a : actions) {
            if (a == eqt::input::HotkeyAction::RangeAttack) found = true;
        }
    }
    EXPECT_TRUE(found)
        << "the Range Attack action is not on the Options Keys page -- the "
           "[assumed]-owed default key can never be bound";
}

// [assumed]-owed: unbound in BOTH the compiled-in defaults and the shipped
// file, so T464 (defaults<->file agreement) stays green and a press does
// nothing until the user binds it on the Options Keys page.
TEST(RangedAttackKey, UnboundByDefaultAndInTheShippedFile) {
    auto& mgr = eqt::input::HotkeyManager::instance();
    mgr.resetToDefaults();
    EXPECT_TRUE(mgr.getBindingsForAction(eqt::input::HotkeyAction::RangeAttack).empty())
        << "the oracle's default Range Attack key is not measured (Leg O); it is "
           "[assumed]-owed and must not be invented here";
    ASSERT_TRUE(mgr.loadFromFile(std::string(WILLEQ_REPO_ROOT) + "/config/hotkeys.json"));
    EXPECT_TRUE(mgr.getBindingsForAction(eqt::input::HotkeyAction::RangeAttack).empty())
        << "config/hotkeys.json binds a key the oracle leg has not measured";
    mgr.resetToDefaults();
}

// The two steps that live inside device-bound code -- the HA->RA translation
// in RendererEventReceiver::OnEvent and the RA->InputAction arm in
// GraphicsInputHandler -- are pinned by source, the idiom
// test_ui_hide_command.cpp uses for a handful of single-site invariants.
TEST(RangedAttackKey, TheReceiverAndInputHandlerCarryTheRangeAttackArm) {
    const std::string receiver =
        ReadRepoFile("src/client/graphics/renderer/renderer_event_receiver.cpp");
    EXPECT_NE(receiver.find("case HA::RangeAttack: pushBridgeAction({RA::RangeAttack});"),
              std::string::npos)
        << "a bound Range Attack key falls through OnEvent's default -- the key is dead";

    const std::string handler = ReadRepoFile("src/client/input/graphics_input_handler.cpp");
    EXPECT_NE(handler.find("case RA::RangeAttack:"), std::string::npos)
        << "the RA::RangeAttack arm is gone from GraphicsInputHandler -- the press "
           "never becomes InputAction::RangeAttack";
}

}  // namespace
