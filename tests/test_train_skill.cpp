// T280 -- train a skill at a trainer / close the trainer window (T264 F8).
//
// The route, end to end, against the REAL EverQuest -- no eqemu, no graphics:
//
//   server OP_GMTraining reply (GMTrainee_Struct, 448 bytes)
//     -> EverQuest::ZoneProcessGMTraining   (session opens, CAPS kept)
//   /train <skill|id|list>
//     -> CommandProcessor::cmdTrain
//     -> ActionDispatcher::trainSkill       (zone gate + usage string)
//     -> EqActionHandler::trainSkill
//     -> EverQuest::RequestTrainSkill       (resolve the skill, refuse
//                                            locally against the caps)
//     -> TrainSkillIntent                   (T264 F8's BLOCKED intent)
//     -> ProcessBridgeIntents
//     -> EverQuest::TrainSkill              (OP_GMTrainSkill, 12 bytes)
//     -> server OP_SkillUpdate              (the ONLY reply on Titanium)
//     -> SkillManager::updateSkill
//   /trainclose -> ... -> CloseTrainerIntent -> OP_GMEndTraining (8 bytes)
//
// Wire truth pinned here, read out of eqemu rather than assumed:
//   * GMSkillChange_Struct is 12 bytes and its npcid is a **uint16**
//     (common/eq_packet_structs.h -- whose per-field offset comments are
//     wrong, every one of them says /*002*/). Handle_OP_GMTrainSkill gates
//     on size == 12 exactly.
//   * skillbank is 0 for skills (1 is languages).
//   * Cost = (current - 10)^3 / 100 copper, integer, off the PRE-train
//     value (Client::OPGMTrainSkill). The client's old "(cur + 1) * 10"
//     was invented and matched the server at no skill value at all.
//   * Titanium gets NO GMTrainSkillConfirm (gated on >= SoF) and NO money
//     update (TakeMoneyFromPP's update_client defaults to false). So the
//     only thing that comes back is OP_SkillUpdate, and nothing here may
//     write the skill value or the coin locally.

#include <gtest/gtest.h>

// eq.h only -- m_zone_connection / m_trainer_npc_id are
// private (test_scribe_scroll.cpp's precedent).
#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq_action_handler.h"
#include "client/events/renderer_intents.h"
#include "client/skill/skill_constants.h"
#include "client/skill/skill_manager.h"
#include "common/net/daybreak_connection.h"
#include "common/packet_structs.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using namespace zonetest;

namespace {

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

struct SentAppPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> body;
};

constexpr uint16_t kTrainerId = 4242;
constexpr uint8_t kSkill1HSlash = 1;    // "1H Slash"
constexpr uint8_t kSkillBash = 10;      // "Bash"
constexpr uint8_t kSkillForage = 27;    // "Forage" -- left un-taught on purpose

class TrainHarness {
public:
    TrainHarness()
        : eq(MakeEq()),
          handler(eq),
          dispatcher(eq.GetGameState()),
          processor(eq.GetGameState(), dispatcher) {
        EQ::Net::DaybreakConnectionManagerOptions opts;
        opts.crc_length = 2;
        opts.encode_passes[0] = EQ::Net::EncodeNone;
        opts.encode_passes[1] = EQ::Net::EncodeNone;
        opts.connect_delay_ms = 0;
        opts.keepalive_delay_ms = 0;
        manager = std::make_unique<EQ::Net::DaybreakConnectionManager>(
            opts, std::make_unique<EQ::Test::FakeUdpTransport>(&c2s, &s2c));
        manager->OnNewConnection([this](std::shared_ptr<EQ::Net::DaybreakConnection> c) {
            conn = c;
        });
        manager->Connect("peer", 12345);
        EXPECT_NE(conn, nullptr);

        eq.setBridge(&bridge);
        eq.Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetZoneConnectedForSeam(true);
        eq.m_zone_connection = conn;

        dispatcher.setActionHandler(&handler);
        eq.GetGameState().world().setZoneConnected(true);

        eq.m_my_spawn_id = 77;
        // T283: practice points live in ONE place -- PlayerState. Seeding
        // that single store is all a zone-in does and all a test may need;
        // if some other copy exists, this seeding is silently incomplete
        // and the OP_SkillUpdate arm below will not spend a point.
        eq.GetGameState().player().setPracticePoints(5);
    }

    ~TrainHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    // The server's OP_GMTraining answer: our own request echoed back with
    // skills[] filled in with the per-skill CAPS this trainer can reach.
    void openSession(uint32_t cap1hSlash = 100, uint32_t capBash = 75) {
        EQT::GMTrainee_Struct t;
        std::memset(&t, 0, sizeof(t));
        t.npcid = kTrainerId;
        t.playerid = eq.m_my_spawn_id;
        t.skills[kSkill1HSlash] = cap1hSlash;
        t.skills[kSkillBash] = capBash;
        // kSkillForage deliberately left at 0 == "not taught here".
        std::vector<uint8_t> payload(sizeof(t));
        std::memcpy(payload.data(), &t, sizeof(t));
        ReplayZone(eq, HC_OP_GMTraining, payload);
    }

    void replaySkillUpdate(uint32_t skillId, uint32_t value) {
        auto payload = BuildPayload(10, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2, skillId);
            p.PutUInt32(6, value);
        });
        ReplayZone(eq, HC_OP_SkillUpdate, payload);
    }

    std::vector<SentAppPacket> sentPackets() {
        std::vector<SentAppPacket> out;
        for (auto& [seq, sent] : conn->m_streams[0].sent_packets) {
            const auto* d = static_cast<const uint8_t*>(sent.packet.Data());
            size_t n = sent.packet.Length();
            size_t off = EQ::Net::DaybreakReliableHeader::size();
            if (off < n && d[off] == 0) ++off;
            if (off + 2 > n) continue;
            SentAppPacket pkt;
            pkt.opcode = static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
            pkt.body.assign(d + off + 2, d + n);
            out.push_back(std::move(pkt));
        }
        return out;
    }

    std::vector<SentAppPacket> packetsWithOpcode(uint16_t op) {
        std::vector<SentAppPacket> out;
        for (auto& p : sentPackets()) {
            if (p.opcode == op) out.push_back(p);
        }
        return out;
    }

    void clearSent() { conn->m_streams[0].sent_packets.clear(); }

    std::vector<eqt::events::RendererIntent> drainIntents() { return bridge.drainIntents(); }

    std::vector<std::string> systemMessages() {
        std::vector<std::string> out;
        for (auto& e : bridge.drainEvents()) {
            if (e.type == eqt::state::GameEventType::SystemMessage) {
                out.push_back(std::get<eqt::state::ChatMessageData>(e.data).message);
            }
        }
        return out;
    }

    bool anyMessageContains(const std::string& needle) {
        for (const auto& m : systemMessages()) {
            if (m.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    EverQuest eq;
    RecordingBridge bridge;
    eqt::EqActionHandler handler;
    eqt::action::ActionDispatcher dispatcher;
    eqt::action::CommandProcessor processor;
    EQ::Test::FakeChannel c2s;
    EQ::Test::FakeChannel s2c;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
};

const eqt::events::TrainSkillIntent* FindTrainIntent(
        const std::vector<eqt::events::RendererIntent>& intents) {
    for (const auto& i : intents) {
        if (std::holds_alternative<eqt::events::TrainSkillIntent>(i)) {
            return &std::get<eqt::events::TrainSkillIntent>(i);
        }
    }
    return nullptr;
}

bool HasCloseTrainerIntent(const std::vector<eqt::events::RendererIntent>& intents) {
    for (const auto& i : intents) {
        if (std::holds_alternative<eqt::events::CloseTrainerIntent>(i)) return true;
    }
    return false;
}

// ===================================================================
// 1. The wire structs -- eqemu's layout, not the comment block's
// ===================================================================

TEST(TrainWire, SkillChangeStructIsTwelveBytesWithSixteenBitNpcId) {
    EXPECT_EQ(sizeof(EQT::GMSkillChange_Struct), 12u);
    EXPECT_EQ(sizeof(EQT::GMSkillChange_Struct{}.npcid), 2u)
        << "eqemu declares npcid as uint16; Handle_OP_GMTrainSkill gates on size == 12";
    EXPECT_EQ(offsetof(EQT::GMSkillChange_Struct, npcid), 0u);
    EXPECT_EQ(offsetof(EQT::GMSkillChange_Struct, skillbank), 4u);
    EXPECT_EQ(offsetof(EQT::GMSkillChange_Struct, skill_id), 8u);
}

TEST(TrainWire, OpcodesAreTitaniumsOwn) {
    // utils/patches/patch_Titanium.conf. GMEndTrainingResponse is COMMENTED
    // OUT there -- Titanium never receives an end-training ack.
    EXPECT_EQ(static_cast<uint16_t>(HC_OP_GMTraining), 0x238f);
    EXPECT_EQ(static_cast<uint16_t>(HC_OP_GMTrainSkill), 0x11d2);
    EXPECT_EQ(static_cast<uint16_t>(HC_OP_GMEndTraining), 0x613d);
}

// ===================================================================
// 2. The cost formula -- eqemu's, verbatim
// ===================================================================

TEST(TrainCost, MatchesEqemusCubicFormula) {
    // Client::OPGMTrainSkill: AdjustedSkillLevel = skilllevel - 10;
    //                         if (AdjustedSkillLevel > 0)
    //                             Cost = Adj*Adj*Adj / 100;
    EXPECT_EQ(EverQuest::TrainingCost(0), 0u);
    EXPECT_EQ(EverQuest::TrainingCost(10), 0u);
    EXPECT_EQ(EverQuest::TrainingCost(11), 0u);     // 1/100 == 0, integer
    EXPECT_EQ(EverQuest::TrainingCost(15), 1u);     // 125/100
    EXPECT_EQ(EverQuest::TrainingCost(20), 10u);    // 1000/100
    EXPECT_EQ(EverQuest::TrainingCost(50), 640u);   // 64000/100
    EXPECT_EQ(EverQuest::TrainingCost(100), 7290u); // 729000/100
    EXPECT_EQ(EverQuest::TrainingCost(200), 6859u * 10u);  // 190^3/100
}

// ===================================================================
// 3. The session -- caps kept, not thrown away
// ===================================================================

TEST(TrainSession, GmTrainingReplyOpensTheSessionAndKeepsTheCaps) {
    TrainHarness h;
    EXPECT_FALSE(h.eq.IsTrainerWindowOpen());
    h.openSession(100, 75);
    EXPECT_TRUE(h.eq.IsTrainerWindowOpen());
    EXPECT_EQ(h.eq.GetTrainerNpcId(), kTrainerId);
    EXPECT_EQ(h.eq.GetTrainerSkillCap(kSkill1HSlash), 100u);
    EXPECT_EQ(h.eq.GetTrainerSkillCap(kSkillBash), 75u);
    EXPECT_EQ(h.eq.GetTrainerSkillCap(kSkillForage), 0u);
}

TEST(TrainSession, TrainerWindowEventQuotesTheServersPrice) {
    TrainHarness h;
    h.eq.m_skill_manager->updateSkill(kSkill1HSlash, 50);
    h.openSession(100, 75);

    bool found = false;
    for (auto& e : h.bridge.drainEvents()) {
        if (e.type != eqt::state::GameEventType::TrainerWindowOpened) continue;
        const auto& d = std::get<eqt::state::TrainerWindowOpenedData>(e.data);
        for (const auto& s : d.skills) {
            if (s.skillId != kSkill1HSlash) continue;
            found = true;
            EXPECT_EQ(s.currentValue, 50u);
            EXPECT_EQ(s.maxTrainable, 100u);
            // eqemu: (50-10)^3/100 = 640 copper. The pre-T280 client said
            // (50+1)*10 = 510, which the server never charged.
            EXPECT_EQ(s.cost, 640u);
        }
    }
    EXPECT_TRUE(found) << "1H Slash should be offered";
}

TEST(TrainSession, CapsAreClearedWhenTheSessionEnds) {
    TrainHarness h;
    h.openSession();
    h.eq.CloseTrainerWindow();
    EXPECT_FALSE(h.eq.IsTrainerWindowOpen());
    EXPECT_EQ(h.eq.GetTrainerSkillCap(kSkill1HSlash), 0u);
}

// ===================================================================
// 4. Local refusals -- message, no intent, nothing on the wire
// ===================================================================

TEST(TrainRefusal, NoSessionRefusesAndSendsNothing) {
    TrainHarness h;
    h.clearSent();
    h.processor.processCommand("/train 1H Slash");
    EXPECT_TRUE(h.anyMessageContains("not interacting with a trainer"));
    EXPECT_EQ(FindTrainIntent(h.drainIntents()), nullptr);
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_GMTrainSkill).empty());
}

TEST(TrainRefusal, NoTrainingPointsRefuses) {
    TrainHarness h;
    h.openSession();
    h.eq.GetGameState().player().setPracticePoints(0);
    h.systemMessages();
    h.processor.processCommand("/train 1H Slash");
    EXPECT_TRUE(h.anyMessageContains("no training points"));
    EXPECT_EQ(FindTrainIntent(h.drainIntents()), nullptr);
}

TEST(TrainRefusal, SkillThisTrainerDoesNotTeachRefusesByName) {
    TrainHarness h;
    h.openSession();
    h.systemMessages();
    h.processor.processCommand("/train Forage");
    EXPECT_TRUE(h.anyMessageContains("Forage"));
    EXPECT_EQ(FindTrainIntent(h.drainIntents()), nullptr);
}

TEST(TrainRefusal, AlreadyAtThisTrainersCapRefuses) {
    TrainHarness h;
    h.eq.m_skill_manager->updateSkill(kSkillBash, 75);
    h.openSession(100, 75);
    h.systemMessages();
    h.processor.processCommand("/train Bash");
    EXPECT_TRUE(h.anyMessageContains("more skilled"));
    EXPECT_EQ(FindTrainIntent(h.drainIntents()), nullptr);
}

TEST(TrainRefusal, UnknownSkillNameRefusesAndNamesWhatWasAsked) {
    TrainHarness h;
    h.openSession();
    h.systemMessages();
    h.processor.processCommand("/train Underwater Basketweaving");
    EXPECT_TRUE(h.anyMessageContains("Underwater Basketweaving"));
    EXPECT_EQ(FindTrainIntent(h.drainIntents()), nullptr);
}

TEST(TrainRefusal, AmbiguousSkillNameListsTheCandidates) {
    TrainHarness h;
    h.openSession();
    h.systemMessages();
    // "instruments" matches Brass/Percussion/Stringed/Wind Instruments.
    h.processor.processCommand("/train instruments");
    EXPECT_TRUE(h.anyMessageContains("matches"));
    EXPECT_EQ(FindTrainIntent(h.drainIntents()), nullptr);
}

TEST(TrainRefusal, OutOfRangeSkillIdRefuses) {
    TrainHarness h;
    h.openSession();
    h.systemMessages();
    h.processor.processCommand("/train 999");
    EXPECT_TRUE(h.anyMessageContains("no skill"));
    EXPECT_EQ(FindTrainIntent(h.drainIntents()), nullptr);
}

TEST(TrainRefusal, BareTrainWithNoTargetStillOpensTheWindowPath) {
    // /train with no argument keeps its pre-T280 meaning (open the window
    // with the current target); with no target that is T50's refusal, not
    // the new skill-resolver's.
    TrainHarness h;
    h.systemMessages();
    h.processor.processCommand("/train");
    EXPECT_TRUE(h.anyMessageContains("trainer targeted"));
}

// ===================================================================
// 5. The happy path -- exact bytes
// ===================================================================

TEST(TrainSkill, ByNamePushesTheIntentAndSendsTwelveExactBytes) {
    TrainHarness h;
    h.openSession();
    h.clearSent();

    h.processor.processCommand("/train 1H Slash");

    auto intents = h.drainIntents();
    const auto* intent = FindTrainIntent(intents);
    ASSERT_NE(intent, nullptr) << "T264 F8's blocked intent must now be produced";
    EXPECT_EQ(intent->skillId, kSkill1HSlash);

    // Nothing is on the wire until the intent is drained on the game thread.
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_GMTrainSkill).empty());
    h.eq.TrainSkill(intent->skillId);

    auto pkts = h.packetsWithOpcode(HC_OP_GMTrainSkill);
    ASSERT_EQ(pkts.size(), 1u);
    ASSERT_EQ(pkts[0].body.size(), sizeof(EQT::GMSkillChange_Struct));
    EQT::GMSkillChange_Struct got{};
    std::memcpy(&got, pkts[0].body.data(), sizeof(got));
    EXPECT_EQ(got.npcid, kTrainerId);
    EXPECT_EQ(got.skillbank, 0u) << "0 == skills; 1 would be languages";
    EXPECT_EQ(got.skill_id, kSkill1HSlash);
}

TEST(TrainSkill, ByNumericIdWorksToo) {
    TrainHarness h;
    h.openSession();
    h.processor.processCommand("/train 10");
    auto intents = h.drainIntents();
    const auto* intent = FindTrainIntent(intents);
    ASSERT_NE(intent, nullptr);
    EXPECT_EQ(intent->skillId, kSkillBash);
}

TEST(TrainSkill, CaseInsensitiveExactNameBeatsSubstring) {
    TrainHarness h;
    h.openSession();
    h.processor.processCommand("/train bash");
    auto intents = h.drainIntents();
    const auto* intent = FindTrainIntent(intents);
    ASSERT_NE(intent, nullptr);
    EXPECT_EQ(intent->skillId, kSkillBash);
}

// ===================================================================
// 6. Nothing optimistic -- the value comes from the server
// ===================================================================

TEST(TrainSkill, WritesNoSkillValueAndNoMoneyLocally) {
    TrainHarness h;
    h.eq.m_skill_manager->updateSkill(kSkill1HSlash, 50);
    h.openSession();

    const uint32_t plat0 = h.eq.GetPlatinum();
    const uint32_t gold0 = h.eq.GetGold();
    const uint32_t silver0 = h.eq.GetSilver();
    const uint32_t copper0 = h.eq.GetCopper();

    h.processor.processCommand("/train 1H Slash");
    auto intents = h.drainIntents();
    ASSERT_NE(FindTrainIntent(intents), nullptr);
    h.eq.TrainSkill(kSkill1HSlash);

    EXPECT_EQ(h.eq.m_skill_manager->getSkillValue(kSkill1HSlash), 50u)
        << "the new value must come from the server's OP_SkillUpdate";
    EXPECT_EQ(h.eq.GetPlatinum(), plat0);
    EXPECT_EQ(h.eq.GetGold(), gold0);
    EXPECT_EQ(h.eq.GetSilver(), silver0);
    EXPECT_EQ(h.eq.GetCopper(), copper0)
        << "Titanium gets no money update at all -- guessing one is worse than "
           "leaving it stale";

    // The one packet that DOES come back.
    h.replaySkillUpdate(kSkill1HSlash, 51);
    EXPECT_EQ(h.eq.m_skill_manager->getSkillValue(kSkill1HSlash), 51u);
}

// T283: the harness seeds ONLY PlayerState. If a second practice-point
// store existed, the OP_SkillUpdate arm would gate on the unseeded copy and
// no point would be spent -- so this test is the pin on "one store".
TEST(TrainSkill, PracticePointsHaveASingleStore) {
    TrainHarness h;
    h.openSession();
    h.eq.GetGameState().player().setPracticePoints(3);
    EXPECT_EQ(h.eq.GetPracticePoints(), 3u)
        << "GetPracticePoints() must read the store a zone-in writes";
    h.replaySkillUpdate(kSkill1HSlash, 2);
    EXPECT_EQ(h.eq.GetPracticePoints(), 2u);
    EXPECT_EQ(h.eq.GetGameState().player().practicePoints(), 2u)
        << "the accessor and the store must be the same object";
}

TEST(TrainSkill, SkillUpdateDuringASessionSpendsATrainingPoint) {
    TrainHarness h;
    h.openSession();
    const uint32_t before = h.eq.GetPracticePoints();
    h.replaySkillUpdate(kSkill1HSlash, 2);
    EXPECT_EQ(h.eq.GetPracticePoints(), before - 1)
        << "the server decrements m_pp.points and never tells Titanium; "
           "the open session is the only signal available";
}

// ===================================================================
// 7. /train list
// ===================================================================

TEST(TrainList, ListsWhatTheSessionOffersWithThePrice) {
    TrainHarness h;
    h.eq.m_skill_manager->updateSkill(kSkill1HSlash, 50);
    h.openSession(100, 75);
    h.systemMessages();
    h.processor.processCommand("/train list");
    auto msgs = h.systemMessages();
    bool sawSlash = false;
    bool sawForage = false;
    for (const auto& m : msgs) {
        if (m.find("1H Slash") != std::string::npos) sawSlash = true;
        if (m.find("Forage") != std::string::npos) sawForage = true;
    }
    EXPECT_TRUE(sawSlash);
    EXPECT_FALSE(sawForage) << "cap 0 means this trainer does not teach it";
    EXPECT_EQ(FindTrainIntent(h.drainIntents()), nullptr);
}

// ===================================================================
// 8. /trainclose
// ===================================================================

TEST(TrainClose, PushesTheIntentAndSendsEightExactBytes) {
    TrainHarness h;
    h.openSession();
    h.clearSent();

    h.processor.processCommand("/trainclose");
    auto intents = h.drainIntents();
    ASSERT_TRUE(HasCloseTrainerIntent(intents))
        << "T264 F8's other blocked intent must now be produced";

    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_GMEndTraining).empty());
    h.eq.CloseTrainerWindow();

    auto pkts = h.packetsWithOpcode(HC_OP_GMEndTraining);
    ASSERT_EQ(pkts.size(), 1u);
    ASSERT_EQ(pkts[0].body.size(), sizeof(EQT::GMTrainEnd_Struct));
    EQT::GMTrainEnd_Struct got{};
    std::memcpy(&got, pkts[0].body.data(), sizeof(got));
    EXPECT_EQ(got.npcid, kTrainerId);
    EXPECT_EQ(got.playerid, h.eq.m_my_spawn_id);
    EXPECT_FALSE(h.eq.IsTrainerWindowOpen());
}

TEST(TrainClose, WithNoSessionRefusesAndSendsNothing) {
    TrainHarness h;
    h.clearSent();
    h.processor.processCommand("/trainclose");
    EXPECT_TRUE(h.anyMessageContains("not interacting with a trainer"));
    EXPECT_FALSE(HasCloseTrainerIntent(h.drainIntents()));
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_GMEndTraining).empty());
}

// ===================================================================
// 9. The routing hops themselves
// ===================================================================

TEST(TrainRouting, CommandsAreRegisteredWithTheirAliases) {
    TrainHarness h;
    EXPECT_TRUE(h.processor.hasCommand("train"));
    EXPECT_TRUE(h.processor.hasCommand("trainclose"));
    EXPECT_TRUE(h.processor.hasCommand("traindone"));
}

}  // namespace
