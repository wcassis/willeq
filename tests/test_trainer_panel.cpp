// B898 (GitLab #214) -- the skill trainer window.
//
// `/train` opened a server session, the OP_GMTraining reply was decoded and
// published as TrainerWindowOpened, and the renderer arm DISCARDED it: nothing
// was drawn. These cases cover, in order:
//
//   1. a SOURCE CENSUS of where the defect lived (the bridge arms were
//      LOG_TRACEs, the window was never drawn). Pre-fix red.
//   2. the geometry, against EQUI_TrainWindow.xml -- transcribed constants,
//      and (when the stock client is on this host) the XML itself.
//   3. reply -> event -> panel, end to end against the REAL EverQuest: a
//      server-shaped OP_GMTraining reply through ReplayZonePacket, the
//      published event through the real applier.
//   4. the clicks: Train pushes the SAME intent `/train <id>` pushes, Done
//      the one `/trainclose` pushes, and the consumer sends the exact bytes.
//   5. refresh from the server only: OP_SkillUpdate re-publishes the window.
//   6. negatives: no selection, a closed window, a refused train, scroll
//      clamps, a different trainer, the Ctrl+click (right-click stand-in)
//      opener.
//
//   7. the RECORDED official reply against the oracle's row set.
//
// ORACLE LEDGER ROWS PINNED (review/UIGAP-ORACLE/214/train.tsv):
//   row 1  OP_GMTraining both ways, 448 B; Train disabled until a selection
//          -> TrainOpeners.*, TrainerClickTest.TrainWithNothingSelectedSendsNothing
//   row 3  cost per row, "--" when free -> TrainLayoutOracle.CostColumns*,
//          TrainerRecordedReply.AFreeRowIsPricedZero
//   row 4  the row set, by skill id      -> TrainerRecordedReply.EveryRow*
//   row 5  a row click selects, nothing sent -> ClickingARowSelectsItAndSendsNothing
//   row 6  Train = OP_GMTrainSkill skillbank 0, one practice, OP_SkillUpdate
//          back, the selection persists -> TrainReachesTheWire*, OpSkillUpdateRefreshes*
//   row 8  Done = OP_GMEndTraining (8 B)  -> DonePushesCloseTrainer*
//   rows 10-15 (UIGAP-live2): the selection line, Train greyed on 0 practices
//          or a short purse, at cap enabled + no packet + eqstr 12930
//          -> TrainerMeasured.*, ASkillAlreadyAtTheTrainersCapIsStillListed
// NOT pinned (owed): row 2 Rank words (only Awful/Master seen), row 4's
// languages, the at-cap rule against the cap at the CURRENT level (fable has
// no per-level cap table; it applies the session's cap), row 12/14's
// client-side money deduction.
//
// No Irrlicht device, no server.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/bridge/bridge_component.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq_action_handler.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/train_layout.h"
#include "client/graphics/ui/trainer_panel.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/skill/skill_manager.h"
#include "client/state/event_bus.h"
#include "common/net/daybreak_connection.h"
#include "common/packet_structs.h"
#include "eqstr_test_db.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

using namespace zonetest;
using namespace EQT::Graphics;

namespace {

const std::string kRoot = WILLEQ_REPO_ROOT;
const char* kOracleXml =
    "/mnt/projects/EverQuestP1999/uifiles/default/EQUI_TrainWindow.xml";

std::string slurp(const std::string& path) {
    std::ifstream in(path);
    if (!in.good()) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

constexpr uint16_t kTrainerId = 4242;
constexpr uint8_t kSkill1HSlash = 1;   // "1H Slashing"
constexpr uint8_t kSkillBash = 10;
constexpr uint8_t kSkillForage = 27;   // not taught by this trainer

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

struct SentAppPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> body;
};

// The T280 harness (tests/test_train_skill.cpp): the real EverQuest on a
// fake Daybreak connection, so a consumer's packet can be read back.
class TrainerHarness {
public:
    TrainerHarness()
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
        eq.Net().Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetZoneConnectedForSeam(true);
        eq.m_zone_connection = conn;
        dispatcher.setActionHandler(&handler);
        eq.GetGameState().world().setZoneConnected(true);
        eq.Test_SetMySpawnIdForSeam(77);
        eq.GetGameState().player().setPracticePoints(5);
        eq.GetGameState().player().setCurrency(12, 3, 4, 5);
    }

    ~TrainerHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    // The server's OP_GMTraining answer, shaped as Client::OPGMTraining
    // builds it: our request echoed back, skills[] filled with the CAPS this
    // trainer can reach, and the 40 fixed trailing bytes it memcpys over
    // unknown408.
    void openSession(uint16_t npc = kTrainerId, uint32_t cap1h = 100,
                     uint32_t capBash = 75) {
        EQT::GMTrainee_Struct t;
        std::memset(&t, 0, sizeof(t));
        t.npcid = npc;
        t.playerid = eq.GetMySpawnID();
        t.skills[kSkill1HSlash] = cap1h;
        t.skills[kSkillBash] = capBash;
        std::vector<uint8_t> payload(sizeof(t));
        std::memcpy(payload.data(), &t, sizeof(t));
        static const uint8_t kEnding[] = {
            0x34, 0x87, 0x8a, 0x3F, 0x01, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9,
            0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9,
            0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9,
            0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0xC9, 0x76, 0x75, 0x3f};
        std::memcpy(payload.data() + payload.size() - sizeof(kEnding), kEnding,
                    sizeof(kEnding));
        ReplayZone(eq, HC_OP_GMTraining, payload);
    }

    void replaySkillUpdate(uint32_t skillId, uint32_t value) {
        auto payload = BuildPayload(10, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2, skillId);
            p.PutUInt32(6, value);
        });
        ReplayZone(eq, HC_OP_SkillUpdate, payload);
    }

    void spawnNpc(uint32_t id, const std::string& name, uint8_t classId) {
        ReplayZone(eq, HC_OP_NewSpawn,
                   NewSpawnPayloadAt(id, name, 0, 0, 0, 30, 1, classId));
    }

    // Every event the game thread published since the last drain goes
    // through the renderer's real appliers, the way IrrlichtBridge does.
    // Returns how many TrainerWindowOpened events there were.
    int pumpToPanel() {
        int opened = 0;
        for (auto& e : bridge.drainEvents()) {
            if (e.type == eqt::state::GameEventType::TrainerWindowOpened) {
                applyTrainerWindowOpened(
                    panel, std::get<eqt::state::TrainerWindowOpenedData>(e.data));
                ++opened;
            } else if (e.type == eqt::state::GameEventType::TrainerWindowClosed) {
                applyTrainerWindowClosed(
                    panel, std::get<eqt::state::WindowClosedData>(e.data));
            } else if (e.type == eqt::state::GameEventType::SystemMessage) {
                messages.push_back(
                    std::get<eqt::state::ChatMessageData>(e.data).message);
            }
        }
        return opened;
    }

    std::vector<SentAppPacket> packetsWithOpcode(uint16_t op) {
        std::vector<SentAppPacket> out;
        for (auto& [seq, sent] : conn->m_streams[0].sent_packets) {
            const auto* d = static_cast<const uint8_t*>(sent.packet.Data());
            size_t n = sent.packet.Length();
            size_t off = EQ::Net::DaybreakReliableHeader::size();
            if (off < n && d[off] == 0) ++off;
            if (off + 2 > n) continue;
            SentAppPacket pkt;
            pkt.opcode = static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
            if (pkt.opcode != op) continue;
            pkt.body.assign(d + off + 2, d + n);
            out.push_back(std::move(pkt));
        }
        return out;
    }
    void clearSent() { conn->m_streams[0].sent_packets.clear(); }

    bool anyMessageContains(const std::string& needle) const {
        for (const auto& m : messages) {
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

    TrainerPanelState panel;
    std::vector<std::string> messages;
};

UILayout layoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}
int cx(const irr::core::rect<irr::s32>& r) {
    return (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
}
int cy(const irr::core::rect<irr::s32>& r) {
    return (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;
}

int rowOfSkill(const TrainerPanelState& p, uint8_t skillId) {
    for (size_t i = 0; i < p.rows.size(); ++i) {
        if (p.rows[i].skillId == skillId) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

// ===========================================================================
// 1. Source census -- the two halves of B898. Pre-fix red.
// ===========================================================================

TEST(B898Census, TheTrainerBridgeArmsAreNotStubs) {
    const std::string src = slurp(kRoot + "/src/client/bridge/irrlicht_bridge.cpp");
    ASSERT_FALSE(src.empty());
    EXPECT_NE(src.find("applyTrainerWindowOpened"), std::string::npos)
        << "the decoded OP_GMTraining reply is discarded (B898)";
    EXPECT_NE(src.find("applyTrainerWindowClosed"), std::string::npos);
    EXPECT_EQ(src.find("LOG_TRACE(MOD_GRAPHICS, \"Bridge: TrainerWindowOpened\")"),
              std::string::npos);
}

TEST(B898Census, TheTrainerWindowIsDrawnAndHitTested) {
    const std::string src =
        slurp(kRoot + "/src/client/graphics/frame/renderer_frame_render.cpp");
    ASSERT_FALSE(src.empty());
    EXPECT_NE(src.find("renderTrainerWindow"), std::string::npos);
    EXPECT_NE(src.find("processTrainerInput"), std::string::npos);
    const std::string esc =
        slurp(kRoot + "/src/client/graphics/frame/renderer_frame_input.cpp");
    EXPECT_NE(esc.find("trainerState_.isOpen"), std::string::npos)
        << "ESC must end an open trainer session";
}

// ===========================================================================
// 2. Geometry, against EQUI_TrainWindow.xml
// ===========================================================================

TEST(TrainLayoutOracle, TranscribedNumbers) {
    EXPECT_EQ(TrainLayout::WINDOW_W, 580);
    EXPECT_EQ(TrainLayout::WINDOW_H, 280);
    EXPECT_EQ(TrainLayout::LIST_W, 450);
    EXPECT_EQ(TrainLayout::LIST_H, 245);
    EXPECT_EQ(TrainLayout::columnsWidth(), 125 + 95 + 50 + 4 * 40);
    EXPECT_EQ(TrainLayout::VSCROLL_W, 20) << "450 - 430: derived, not quoted";
    EXPECT_EQ(TrainLayout::ROWS_VISIBLE, 11);
    EXPECT_EQ(TrainLayout::TRAIN_X, 460);
    EXPECT_EQ(TrainLayout::TRAIN_Y, 78);
    EXPECT_EQ(TrainLayout::DONE_Y, 226);
    EXPECT_EQ(TrainLayout::COIN_Y[3], 196);
}

// The XML is the oracle; a stock Titanium install is not on every CI host,
// so this case reads it when it is there and says so when it is not.
TEST(TrainLayoutOracle, ConstantsMatchTheShippedXml) {
    const std::string xml = slurp(kOracleXml);
    if (xml.empty()) GTEST_SKIP() << kOracleXml << " is not on this host";

    // item -> (X, Y, CX, CY), the first <Location>/<Size> after the item.
    auto geom = [&](const std::string& item) {
        std::vector<int> v;
        const size_t at = xml.find("\"" + item + "\"");
        EXPECT_NE(at, std::string::npos) << item;
        if (at == std::string::npos) return v;
        std::smatch m;
        const std::string tail = xml.substr(at);
        const std::regex re(
            R"(<X>(\d+)</X>\s*<Y>(\d+)</Y>[\s\S]*?<CX>(\d+)</CX>\s*<CY>(\d+)</CY>)");
        if (std::regex_search(tail, m, re)) {
            for (int i = 1; i <= 4; ++i) v.push_back(std::stoi(m[i].str()));
        }
        return v;
    };
    using V = std::vector<int>;
    using namespace TrainLayout;
    EXPECT_EQ(geom("TrainWindow"), (V{120, 20, WINDOW_W, WINDOW_H}));
    EXPECT_EQ(geom("TRNW_SkillList"), (V{LIST_X, LIST_Y, LIST_W, LIST_H}));
    EXPECT_EQ(geom("TRNW_ExpLabel"), (V{EXP_LABEL_X, EXP_LABEL_Y, EXP_LABEL_W, EXP_LABEL_H}));
    EXPECT_EQ(geom("TRNW_ExpGauge"), (V{EXP_GAUGE_X, EXP_GAUGE_Y, EXP_GAUGE_W, EXP_GAUGE_H}));
    EXPECT_EQ(geom("TRNW_PracticeLabel"),
              (V{PRACTICE_LABEL_X, PRACTICE_LABEL_Y, PRACTICE_LABEL_W, PRACTICE_LABEL_H}));
    EXPECT_EQ(geom("TRNW_PracticeCount"),
              (V{PRACTICE_COUNT_X, PRACTICE_COUNT_Y, PRACTICE_COUNT_W, PRACTICE_COUNT_H}));
    EXPECT_EQ(geom("TRNW_TrainButton"), (V{TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H}));
    EXPECT_EQ(geom("TRNW_MoneyLabel"),
              (V{MONEY_LABEL_X, MONEY_LABEL_Y, MONEY_LABEL_W, MONEY_LABEL_H}));
    for (int i = 0; i < COIN_COUNT; ++i) {
        EXPECT_EQ(geom("TRNW_CoinCount" + std::to_string(i)),
                  (V{COIN_COUNT_X, COIN_Y[i], COIN_COUNT_W, COIN_COUNT_H}));
        EXPECT_EQ(geom("TRNW_Purse" + std::to_string(i)),
                  (V{PURSE_X, COIN_Y[i], PURSE_W, PURSE_H}));
    }
    EXPECT_EQ(geom("TW_DoneButton"), (V{DONE_X, DONE_Y, DONE_W, DONE_H}));

    // The seven column widths, in order.
    std::vector<int> widths;
    const std::string list = xml.substr(xml.find("TRNW_SkillList"),
                                        xml.find("</Listbox>") - xml.find("TRNW_SkillList"));
    const std::regex wre(R"(<Width>(\d+)</Width>)");
    for (std::sregex_iterator it(list.begin(), list.end(), wre), end; it != end; ++it) {
        widths.push_back(std::stoi((*it)[1].str()));
    }
    EXPECT_EQ(widths, (V(std::begin(COL_W), std::end(COL_W))));
    EXPECT_NE(list.find("<Heading>Skill Name</Heading>"), std::string::npos);
    EXPECT_NE(list.find("<Heading>Rank</Heading>"), std::string::npos);
    EXPECT_NE(list.find("<Heading>Value</Heading>"), std::string::npos);
}

TEST(TrainLayoutOracle, EveryElementIsInsideTheWindowAtEveryResolution) {
    for (auto rc : {std::pair<int, int>{640, 480}, {800, 600}, {1024, 768}}) {
        const auto l = layoutAt(rc.first, rc.second);
        const auto w = TrainLayout::windowRect(l);
        EXPECT_GE(w.UpperLeftCorner.X, 0);
        EXPECT_LE(w.LowerRightCorner.X, rc.first) << "580 must fit " << rc.first;
        EXPECT_LE(w.LowerRightCorner.Y, rc.second);
        for (const auto& r : {TrainLayout::listRect(l), TrainLayout::trainButtonRect(l),
                              TrainLayout::doneButtonRect(l), TrainLayout::expGaugeRect(l),
                              TrainLayout::coinCountRect(l, 3),
                              TrainLayout::rowRect(l, TrainLayout::ROWS_VISIBLE - 1),
                              TrainLayout::scrollDownRect(l)}) {
            EXPECT_TRUE(w.isRectCollided(r));
            EXPECT_GE(r.UpperLeftCorner.X, w.UpperLeftCorner.X);
            EXPECT_LE(r.LowerRightCorner.X, w.LowerRightCorner.X);
            EXPECT_LE(r.LowerRightCorner.Y, w.LowerRightCorner.Y);
        }
        // The last row stays inside the list box (heading + 11 rows <= 245).
        EXPECT_LE(TrainLayout::rowRect(l, TrainLayout::ROWS_VISIBLE - 1).LowerRightCorner.Y,
                  TrainLayout::listRect(l).LowerRightCorner.Y);
        // Unscaled: Style_Sizable false.
        EXPECT_EQ(w.getWidth(), 580);
        EXPECT_EQ(w.getHeight(), 280);
    }
    // Out-of-range indices are degenerate, never a wrapped rect.
    const auto l = layoutAt(800, 600);
    EXPECT_EQ(TrainLayout::rowRect(l, TrainLayout::ROWS_VISIBLE).getWidth(), 0);
    EXPECT_EQ(TrainLayout::cellRect(l, 0, TrainLayout::COLUMN_COUNT).getWidth(), 0);
    EXPECT_EQ(TrainLayout::coinCountRect(l, 4).getWidth(), 0);
}

TEST(TrainLayoutOracle, CostColumnsSplitCopperLikeTheMerchantWindow) {
    EXPECT_EQ(trainerCostDigit(7290, 0), 7u);
    EXPECT_EQ(trainerCostDigit(7290, 1), 2u);
    EXPECT_EQ(trainerCostDigit(7290, 2), 9u);
    EXPECT_EQ(trainerCostDigit(7290, 3), 0u);
    EXPECT_EQ(trainerCostDigit(0, 0) + trainerCostDigit(0, 3), 0u);
}

// ===========================================================================
// 3. Reply -> event -> panel, against the REAL EverQuest
// ===========================================================================

TEST(TrainerReplyToPanel, TheServersReplyBecomesTheWindowsRows) {
    TrainerHarness h;
    h.eq.m_skill_manager->updateSkill(kSkill1HSlash, 50);
    h.eq.m_skill_manager->updateSkill(kSkillBash, 5);
    h.openSession();
    ASSERT_EQ(h.pumpToPanel(), 1);

    ASSERT_TRUE(h.panel.isOpen);
    EXPECT_EQ(h.panel.npcId, kTrainerId);
    EXPECT_EQ(h.panel.practicePoints, 5u);
    EXPECT_EQ(h.panel.coins[0], 12u);
    EXPECT_EQ(h.panel.coins[1], 3u);
    EXPECT_EQ(h.panel.coins[2], 4u);
    EXPECT_EQ(h.panel.coins[3], 5u);

    // The row set is exactly the skills this trainer teaches that are below
    // its cap, in skill-id order. Forage (cap 0) is absent.
    ASSERT_EQ(h.panel.rows.size(), 2u);
    EXPECT_EQ(h.panel.rows[0].skillId, kSkill1HSlash);
    EXPECT_EQ(h.panel.rows[0].currentValue, 50u);
    EXPECT_EQ(h.panel.rows[0].maxTrainable, 100u);
    EXPECT_EQ(h.panel.rows[0].cost, 640u) << "(50-10)^3/100, eqemu's price";
    EXPECT_EQ(h.panel.rows[1].skillId, kSkillBash);
    EXPECT_EQ(h.panel.rows[1].cost, 0u) << "the first ten points are free";
    EXPECT_EQ(rowOfSkill(h.panel, kSkillForage), -1);
    EXPECT_EQ(h.panel.selectedRow, -1);
}

// train.tsv row 13 (UIGAP-live2): a skill AT its cap is still listed (1H
// Blunt 125, Rank "Master", its price shown) -- B898 used to drop it.
TEST(TrainerReplyToPanel, ASkillAlreadyAtTheTrainersCapIsStillListed) {
    TrainerHarness h;
    h.eq.m_skill_manager->updateSkill(kSkillBash, 75);
    h.openSession(kTrainerId, 100, 75);
    h.pumpToPanel();
    const int bash = rowOfSkill(h.panel, kSkillBash);
    ASSERT_GE(bash, 0);
    EXPECT_EQ(h.panel.rows[static_cast<size_t>(bash)].currentValue, 75u);
    EXPECT_EQ(h.panel.rows[static_cast<size_t>(bash)].cost, 2746u) << "(75-10)^3/100, still priced";
    EXPECT_GE(rowOfSkill(h.panel, kSkill1HSlash), 0);
}

// ===========================================================================
// 4. Clicks -> the same intents the commands push -> the exact bytes
// ===========================================================================

class TrainerClickTest : public ::testing::Test {
protected:
    TrainerHarness h;
    UILayout layout = layoutAt(800, 600);
    StaticUIInput input;

    void SetUp() override {
        input.init(&h.bridge);
        h.openSession();
        h.pumpToPanel();
        h.clearSent();
    }
    bool click(int x, int y) {
        return input.processTrainerInput(x, y, true, layout, h.panel);
    }
    bool clickRowOfSkill(uint8_t skill) {
        const int idx = rowOfSkill(h.panel, skill);
        EXPECT_GE(idx, 0);
        const auto r = TrainLayout::rowRect(layout, idx - h.panel.scrollOffset);
        return click(cx(r), cy(r));
    }
    bool clickTrain() {
        const auto b = TrainLayout::trainButtonRect(layout);
        return click(cx(b), cy(b));
    }
};

// train.tsv rows 5 and 10: a row click selects and sends NOTHING; the only
// thing it asks of the game thread is the client-side "Interested in
// training" line (a select-only TrainSkillIntent).
TEST_F(TrainerClickTest, ClickingARowSelectsItAndSendsNothing) {
    ASSERT_TRUE(clickRowOfSkill(kSkillBash));
    EXPECT_EQ(h.panel.selectedSkillId(), kSkillBash);
    auto intents = h.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* sel = std::get_if<eqt::events::TrainSkillIntent>(&intents[0]);
    ASSERT_NE(sel, nullptr);
    EXPECT_TRUE(sel->selectOnly);
    EXPECT_EQ(sel->skillId, kSkillBash);
    for (auto& i : intents) h.bridge.pushIntent(i);
    h.eq.Bridge().ProcessBridgeIntents();
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_GMTrainSkill).empty());
}

TEST_F(TrainerClickTest, TrainPushesTheIntentSlashTrainPushes) {
    h.processor.processCommand("/train 10");
    auto viaCommand = h.bridge.drainIntents();
    ASSERT_EQ(viaCommand.size(), 1u);
    const auto* cmd = std::get_if<eqt::events::TrainSkillIntent>(&viaCommand[0]);
    ASSERT_NE(cmd, nullptr);

    ASSERT_TRUE(clickRowOfSkill(kSkillBash));
    h.bridge.drainIntents();   // the selection's own select-only intent (row 10)
    ASSERT_TRUE(clickTrain());
    auto viaButton = h.bridge.drainIntents();
    ASSERT_EQ(viaButton.size(), 1u);
    const auto* btn = std::get_if<eqt::events::TrainSkillIntent>(&viaButton[0]);
    ASSERT_NE(btn, nullptr);
    EXPECT_EQ(btn->skillId, cmd->skillId);
    EXPECT_EQ(btn->skillId, kSkillBash) << "the SKILL id, never the row index";
}

TEST_F(TrainerClickTest, TrainReachesTheWireAsTwelveExactBytesAndChangesNothingLocally) {
    ASSERT_TRUE(clickRowOfSkill(kSkill1HSlash));
    ASSERT_TRUE(clickTrain());
    h.eq.Bridge().ProcessBridgeIntents();

    auto pkts = h.packetsWithOpcode(HC_OP_GMTrainSkill);
    ASSERT_EQ(pkts.size(), 1u);
    ASSERT_EQ(pkts[0].body.size(), sizeof(EQT::GMSkillChange_Struct));
    EQT::GMSkillChange_Struct got{};
    std::memcpy(&got, pkts[0].body.data(), sizeof(got));
    EXPECT_EQ(got.npcid, kTrainerId);
    EXPECT_EQ(got.skillbank, 0u);
    EXPECT_EQ(got.skill_id, kSkill1HSlash);

    // Server authoritative: the panel and the game state are untouched until
    // the server answers.
    EXPECT_EQ(h.pumpToPanel(), 0);
    EXPECT_EQ(h.panel.practicePoints, 5u);
    EXPECT_EQ(h.panel.rows[0].currentValue, 0u);
    EXPECT_EQ(h.eq.GetPracticePoints(), 5u);
}

// train.tsv row 8: Done = OP_GMEndTraining (8 B).
TEST_F(TrainerClickTest, DonePushesCloseTrainerAndTheServerSessionEnds) {
    const auto done = TrainLayout::doneButtonRect(layout);
    ASSERT_TRUE(click(cx(done), cy(done)));
    auto intents = h.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_NE(std::get_if<eqt::events::CloseTrainerIntent>(&intents[0]), nullptr);
    EXPECT_TRUE(h.panel.isOpen) << "T290's rule: the panel does not close itself";

    // The consumer sends OP_GMEndTraining -- what /trainclose sends -- and the
    // Closed event closes the window.
    for (auto& i : intents) h.bridge.pushIntent(i);
    h.eq.Bridge().ProcessBridgeIntents();
    auto pkts = h.packetsWithOpcode(HC_OP_GMEndTraining);
    ASSERT_EQ(pkts.size(), 1u);
    EQT::GMTrainEnd_Struct end{};
    ASSERT_EQ(pkts[0].body.size(), sizeof(end));
    std::memcpy(&end, pkts[0].body.data(), sizeof(end));
    EXPECT_EQ(end.npcid, kTrainerId);
    h.pumpToPanel();
    EXPECT_FALSE(h.panel.isOpen);
    EXPECT_TRUE(h.panel.rows.empty());
}

TEST_F(TrainerClickTest, TheCloseBoxDoesWhatDoneDoes) {
    const auto box = TrainLayout::closeBoxRect(layout);
    ASSERT_TRUE(click(cx(box), cy(box)));
    auto intents = h.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::CloseTrainerIntent>(&intents[0]), nullptr);
}

// ===========================================================================
// 5. The refresh comes from the server, and only from the server
// ===========================================================================

// train.tsv row 6: one practice per press, OP_SkillUpdate back, the
// selection persists so Train can be pressed again.
TEST_F(TrainerClickTest, OpSkillUpdateRefreshesValuePriceAndPractices) {
    ASSERT_TRUE(clickRowOfSkill(kSkillBash));
    h.eq.m_skill_manager->updateSkill(kSkill1HSlash, 50);  // (no event: setup)
    h.replaySkillUpdate(kSkill1HSlash, 51);
    ASSERT_EQ(h.pumpToPanel(), 1) << "OP_SkillUpdate re-publishes the open window";

    const int r = rowOfSkill(h.panel, kSkill1HSlash);
    ASSERT_GE(r, 0);
    EXPECT_EQ(h.panel.rows[static_cast<size_t>(r)].currentValue, 51u);
    EXPECT_EQ(h.panel.rows[static_cast<size_t>(r)].cost, 689u) << "(51-10)^3/100 = 68921/100";
    EXPECT_EQ(h.panel.practicePoints, 4u) << "the practice the server spent";
    EXPECT_EQ(h.panel.selectedSkillId(), kSkillBash)
        << "a refresh keeps the selection, by skill";
}

TEST(TrainerRefresh, NoSessionNoRefresh) {
    TrainerHarness h;
    h.replaySkillUpdate(kSkill1HSlash, 3);
    EXPECT_EQ(h.pumpToPanel(), 0);
    EXPECT_FALSE(h.panel.isOpen);
}

// ===========================================================================
// 6. Negatives
// ===========================================================================

// train.tsv row 1: Train is DISABLED until a row is selected.
TEST_F(TrainerClickTest, TrainWithNothingSelectedSendsNothing) {
    ASSERT_TRUE(clickTrain());  // consumed, but no intent
    EXPECT_TRUE(h.bridge.drainIntents().empty());
}

TEST_F(TrainerClickTest, AClosedWindowEatsNothing) {
    eqt::state::WindowClosedData c;
    c.npcId = kTrainerId;
    applyTrainerWindowClosed(h.panel, c);
    EXPECT_FALSE(clickTrain());
    const auto done = TrainLayout::doneButtonRect(layout);
    EXPECT_FALSE(click(cx(done), cy(done)));
    EXPECT_TRUE(h.bridge.drainIntents().empty());
}

TEST_F(TrainerClickTest, ATrainWithNoPracticesIsRefusedAtTheConsumer) {
    h.eq.GetGameState().player().setPracticePoints(0);
    ASSERT_TRUE(clickRowOfSkill(kSkill1HSlash));
    ASSERT_TRUE(clickTrain());
    h.eq.Bridge().ProcessBridgeIntents();
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_GMTrainSkill).empty())
        << "the server's first line is `if(!m_pp.points) return;`";
    h.pumpToPanel();
    EXPECT_TRUE(h.anyMessageContains("no training points"));
}

// ---------------------------------------------------------------------------
// UIGAP-residue: the refusals and the selection line, MEASURED by UIGAP-live2
// (review/UIGAP-ORACLE/214/train.tsv rows 10-15, run2.*).
// ---------------------------------------------------------------------------

namespace {

// A session with the trainer spawned, so its display name is the oracle's,
// and the shipped eqstr table loaded.
struct MeasuredTrainer {
    TrainerHarness h;
    UILayout layout = layoutAt(800, 600);
    StaticUIInput input;

    explicit MeasuredTrainer(uint32_t pp = 100, uint32_t practices = 2) {
        EXPECT_TRUE(eqt_test::ShippedStrings().isLoaded()) << "data/willeq.db";
        EXPECT_TRUE(h.eq.Strings().LoadStringFiles(""));
        h.spawnNpc(kTrainerId, "Brin_Stolunger000", 20);
        h.eq.GetGameState().player().setCurrency(pp, 0, 0, 0);
        h.eq.GetGameState().player().setPracticePoints(practices);
        input.init(&h.bridge);
    }
    void open(uint32_t cap1h, uint32_t capBash) {
        h.openSession(kTrainerId, cap1h, capBash);
        h.pumpToPanel();
        h.messages.clear();
        h.clearSent();
    }
    bool click(const irr::core::rect<irr::s32>& r) {
        return input.processTrainerInput(cx(r), cy(r), true, layout, h.panel);
    }
    void selectSkill(uint8_t skill) {
        const int idx = rowOfSkill(h.panel, skill);
        ASSERT_GE(idx, 0);
        click(TrainLayout::rowRect(layout, idx - h.panel.scrollOffset));
    }
    // What the game thread does with whatever the window pushed.
    void process() {
        h.eq.Bridge().ProcessBridgeIntents();
        h.pumpToPanel();
    }
};

}  // namespace

// Row 10: selecting a row prints "<trainer> says, 'Interested in training in
// <skill> ?'" (eqstr 12056) client-side and sends nothing.
TEST(TrainerMeasured, SelectingARowPrintsTheInterestedLineAndSendsNothing) {
    MeasuredTrainer t;
    t.h.eq.m_skill_manager->updateSkill(kSkill1HSlash, 50);
    t.open(310, 75);
    t.selectSkill(kSkill1HSlash);
    t.process();
    ASSERT_EQ(t.h.messages.size(), 1u);
    EXPECT_EQ(t.h.messages[0], "Brin Stolunger says, 'Interested in training in 1H Slashing ?'")
        << "run2.chat.txt 16:24:51";
    EXPECT_TRUE(t.h.packetsWithOpcode(HC_OP_GMTrainSkill).empty());
}

// Row 11: the purse below the selected row's cost -> Train DISABLED; pressing
// it sends nothing and prints nothing. (1H Slashing 50: (50-10)^3/100 = 640 cp.)
TEST(TrainerMeasured, APurseShortOfTheRowsCostDisablesTrain) {
    MeasuredTrainer t(/*pp=*/0, /*practices=*/2);
    t.h.eq.m_skill_manager->updateSkill(kSkill1HSlash, 50);
    t.open(310, 75);
    t.selectSkill(kSkill1HSlash);
    t.h.bridge.drainIntents();   // the selection line's intent
    EXPECT_FALSE(t.h.panel.trainEnabled());
    t.click(TrainLayout::trainButtonRect(t.layout));
    EXPECT_TRUE(t.h.bridge.drainIntents().empty()) << "row 11: a disabled press pushes nothing";
    t.process();
    EXPECT_TRUE(t.h.packetsWithOpcode(HC_OP_GMTrainSkill).empty());
    EXPECT_TRUE(t.h.messages.empty()) << "row 11: no chat line";
    // A FREE row (Bash 5) is trainable with the same empty purse.
    t.selectSkill(kSkillBash);
    EXPECT_TRUE(t.h.panel.trainEnabled());
}

// Row 15: 0 practices -> Train DISABLED even on a free row; nothing sent.
TEST(TrainerMeasured, ZeroPracticesDisablesTrain) {
    MeasuredTrainer t(/*pp=*/98, /*practices=*/0);
    t.open(310, 75);
    t.selectSkill(kSkillBash);
    t.h.bridge.drainIntents();
    EXPECT_FALSE(t.h.panel.trainEnabled());
    t.click(TrainLayout::trainButtonRect(t.layout));
    EXPECT_TRUE(t.h.bridge.drainIntents().empty()) << "row 15";
    t.process();
    EXPECT_TRUE(t.h.packetsWithOpcode(HC_OP_GMTrainSkill).empty());
    EXPECT_TRUE(t.h.messages.empty()) << "row 15: no chat line";
}

// Row 13: a skill AT its cap is listed, Train stays ENABLED, and pressing it
// sends NO packet and prints, client-side, "<trainer> tells you, 'You are
// already trained to the limit of your abilities in <skill>.'" (eqstr 12930).
// Here the cap is the session's own (the server's per-skill cap); the
// measured case's per-LEVEL cap is not known to fable (see the report).
TEST(TrainerMeasured, AtCapTrainIsEnabledSendsNothingAndPrintsTheLimitLine) {
    MeasuredTrainer t;
    t.h.eq.m_skill_manager->updateSkill(kSkill1HSlash, 125);
    t.open(/*cap1h=*/125, 75);
    t.selectSkill(kSkill1HSlash);
    t.process();
    t.h.messages.clear();
    EXPECT_TRUE(t.h.panel.trainEnabled()) << "row 13: enabled at cap";
    t.click(TrainLayout::trainButtonRect(t.layout));
    t.process();
    EXPECT_TRUE(t.h.packetsWithOpcode(HC_OP_GMTrainSkill).empty()) << "row 13: no OP_GMTrainSkill";
    ASSERT_EQ(t.h.messages.size(), 1u);
    EXPECT_EQ(t.h.messages[0],
              "Brin Stolunger tells you, 'You are already trained to the limit of your "
              "abilities in 1H Slashing.'");
    EXPECT_EQ(t.h.eq.GetPracticePoints(), 2u) << "practices unchanged";
}

TEST_F(TrainerClickTest, TheScrollArrowsMoveTheListAndClamp) {
    // A trainer that teaches more skills than the list shows.
    EQT::GMTrainee_Struct t;
    std::memset(&t, 0, sizeof(t));
    t.npcid = kTrainerId;
    t.playerid = h.eq.GetMySpawnID();
    for (int s = 0; s < 30; ++s) t.skills[s] = 50;
    std::vector<uint8_t> payload(sizeof(t));
    std::memcpy(payload.data(), &t, sizeof(t));
    ReplayZone(h.eq, HC_OP_GMTraining, payload);
    h.pumpToPanel();
    ASSERT_GT(static_cast<int>(h.panel.rows.size()), TrainLayout::ROWS_VISIBLE);

    const auto up = TrainLayout::scrollUpRect(layout);
    const auto down = TrainLayout::scrollDownRect(layout);
    ASSERT_TRUE(click(cx(up), cy(up)));
    EXPECT_EQ(h.panel.scrollOffset, 0) << "clamped at the top";
    for (int i = 0; i < 100; ++i) click(cx(down), cy(down));
    EXPECT_EQ(h.panel.scrollOffset, h.panel.maxScrollOffset());
    EXPECT_TRUE(h.bridge.drainIntents().empty());

    // A scrolled list selects what it shows.
    const auto row1 = TrainLayout::rowRect(layout, 1);
    ASSERT_TRUE(click(cx(row1), cy(row1)));
    EXPECT_EQ(h.panel.selectedRow, h.panel.scrollOffset + 1);
}

TEST(TrainerApplier, ADifferentTrainerStartsClean) {
    TrainerPanelState p;
    eqt::state::TrainerWindowOpenedData a;
    a.npcId = 1;
    a.npcName = "A";
    a.platinum = a.gold = a.silver = a.copper = 0;
    a.practicePoints = 1;
    a.skills.push_back({kSkillBash, "Bash", 1, 50, 0});
    applyTrainerWindowOpened(p, a);
    p.selectedRow = 0;

    auto b = a;
    b.npcId = 2;
    applyTrainerWindowOpened(p, b);
    EXPECT_EQ(p.npcId, 2);
    EXPECT_EQ(p.selectedRow, -1) << "selection belongs to the trainer it was made at";
}

TEST(TrainerApplier, ARefreshThatDropsTheSelectedSkillClearsTheSelection) {
    TrainerPanelState p;
    eqt::state::TrainerWindowOpenedData a;
    a.npcId = 1;
    a.platinum = a.gold = a.silver = a.copper = 0;
    a.practicePoints = 1;
    a.skills.push_back({kSkillBash, "Bash", 49, 50, 0});
    applyTrainerWindowOpened(p, a);
    p.selectedRow = 0;
    a.skills.clear();  // Bash reached the cap and is no longer listed
    applyTrainerWindowOpened(p, a);
    EXPECT_EQ(p.selectedRow, -1);
    EXPECT_EQ(p.selectedSkillId(), -1);
}

// The official client opens training with a RIGHT click on the guildmaster
// (eqstr 6788). fable's right button is mouselook (#151), and Ctrl+click on
// an NPC is its stand-in -- the BankerInteractIntent gesture. A guildmaster
// there sends OP_GMTraining, the packet /train sends.
TEST(TrainerOpeners, CtrlClickOnAGuildmasterRequestsTraining) {
    TrainerHarness h;
    h.spawnNpc(kTrainerId, "Guildmaster_Rough", 20);  // WarriorGM
    h.bridge.drainEvents();
    h.clearSent();
    h.bridge.pushIntent(eqt::events::BankerInteractIntent{kTrainerId});
    h.eq.Bridge().ProcessBridgeIntents();
    auto pkts = h.packetsWithOpcode(HC_OP_GMTraining);
    ASSERT_EQ(pkts.size(), 1u);
    EQT::GMTrainee_Struct req{};
    ASSERT_EQ(pkts[0].body.size(), sizeof(req));
    std::memcpy(&req, pkts[0].body.data(), sizeof(req));
    EXPECT_EQ(req.npcid, kTrainerId);
    EXPECT_EQ(req.playerid, h.eq.GetMySpawnID());
}

TEST(TrainerOpeners, CtrlClickOnABankerStillOpensTheBank) {
    TrainerHarness h;
    h.spawnNpc(300, "Banker_Bob", 40);
    h.clearSent();
    h.bridge.pushIntent(eqt::events::BankerInteractIntent{300});
    h.eq.Bridge().ProcessBridgeIntents();
    EXPECT_TRUE(h.packetsWithOpcode(HC_OP_GMTraining).empty());
    EXPECT_TRUE(h.eq.IsBankWindowOpen());
}

// ===========================================================================
// 7. The RECORDED reply (review/UIGAP-ORACLE/214/run1.pcap, the official
//    client's session: S->C OP_GMTraining from Brin Stolunger, npc 78, to a
//    L20 Warrior) against the oracle's OCR'd row set (run1.window_ocr.tsv)
// ===========================================================================

namespace {
// The 448-byte body, extracted with review/UIGAP-ORACLE/scripts/eqpcap.py.
const char* kRecordedReplyHex =
    "4e00000005010000360100003601000036010000360100000000000000000000"
    "000000003601000000000000770100005e010000000000000000000000000000"
    "000000002c0100006301000000000000000000004a0100000401000000000000"
    "040100000000000000000000000000000000000000000000ff00000000000000"
    "4501000000000000000000000e0100004a010000000000003601000018010000"
    "0000000000000000c80000000000000000000000000000000000000000000000"
    "000000000000000000000000000000005e010000360100000000000000000000"
    "00000000c800000000000000000000002c010000000000002c0100002c010000"
    "000000002c0100002c0100002c010000770100005e0100002c0100002c010000"
    "000000000000000000000000e600000000000000000000001301000036010000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000034878a3f01c9c9c9"
    "c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c9c976753f";

std::vector<uint8_t> unhex(const char* s) {
    std::vector<uint8_t> out;
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoi(std::string(s + i, 2), nullptr, 16)));
    }
    return out;
}

// The non-language rows the official window listed, in its order (OCR,
// pages 1..8). Every one is a skill id.
const std::vector<uint8_t> kOracleSkillRows = {
    0, 1, 2, 3, 7, 9, 10, 16, 19, 20, 22, 28, 30, 34, 36, 37, 40, 50, 51,
    55, 60, 61, 63, 64, 65, 66, 67, 68, 69, 73};
}  // namespace

// train.tsv row 4 (and row 1's 448-byte reply).
TEST(TrainerRecordedReply, EveryRowTheOfficialWindowListedIsListedInItsOrder) {
    TrainerHarness h;
    auto payload = unhex(kRecordedReplyHex);
    ASSERT_EQ(payload.size(), 448u);
    ReplayZone(h.eq, HC_OP_GMTraining, payload);
    ASSERT_EQ(h.pumpToPanel(), 1);
    EXPECT_EQ(h.panel.npcId, 78);

    std::vector<uint8_t> ours;
    for (const auto& r : h.panel.rows) ours.push_back(r.skillId);
    // Subsequence: every oracle row present, in the same (skill id) order.
    size_t j = 0;
    for (uint8_t id : ours) {
        if (j < kOracleSkillRows.size() && kOracleSkillRows[j] == id) ++j;
    }
    EXPECT_EQ(j, kOracleSkillRows.size())
        << "an official row is missing or out of order at oracle index " << j;

    // The MEASURED divergence, pinned so it cannot change silently: the
    // official window did not list Defense (15), Offense (33), Research (58)
    // or ids 76/77, all of which carry a cap in this reply; fable lists them.
    // Whether the client hides them by a fixed rule or because they sat at
    // the level cap is NOT determinable from one session -- oracle-owed
    // (#214). Languages, which the official window lists below Taunt, are not
    // in this reply at all and fable does not list them yet.
    std::vector<uint8_t> extra;
    for (uint8_t id : ours) {
        if (std::find(kOracleSkillRows.begin(), kOracleSkillRows.end(), id) ==
            kOracleSkillRows.end()) {
            extra.push_back(id);
        }
    }
    EXPECT_EQ(extra, (std::vector<uint8_t>{15, 33, 58, 76, 77}));
}

// train.tsv row 3.
TEST(TrainerRecordedReply, AFreeRowIsPricedZero) {
    // The oracle showed "--" in all four coin columns of every value-0/1 row;
    // renderTrainerWindow() draws "--" for cost 0, so the row's cost is 0.
    TrainerHarness h;
    ReplayZone(h.eq, HC_OP_GMTraining, unhex(kRecordedReplyHex));
    h.pumpToPanel();
    for (const auto& r : h.panel.rows) EXPECT_EQ(r.cost, 0u) << r.name;
}
