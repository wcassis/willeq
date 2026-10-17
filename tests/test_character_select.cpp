// B998 (GitLab #318, #204 Legs S/M): the character-select screen.
//
//   * geometry against EQUI_CharacterSelect.xml (skips without the oracle
//     install) and the measured placement;
//   * the list from a RECORDED OP_SendCharInfo payload (data/captures/session.sleq,
//     the P29 recording -- embedded below, and checked against the capture
//     itself when it is present);
//   * buttons -> requests -> packets, end to end through the real
//     CharacterSelectService and LoginWorldComponent's replay seam;
//   * the auto-select path UNCHANGED with a configured character (the hard
//     negative), and Create opening B888's screen.
//
// Oracle frames and the rules each case pins: include/client/graphics/ui/char_select_screen.h,
// include/client/login_world/character_select.h.

#include <gtest/gtest.h>

#include "client/application.h"
#include "client/eq.h"
#include "client/eq_opcodes.h"
#include "client/graphics/ui/char_select_layout.h"
#include "client/graphics/ui/char_select_panel.h"
#include "client/graphics/ui/char_select_screen.h"
#include "client/graphics/ui/confirmation_layout.h"
#include "client/login_world/char_create_data.h"
#include "client/login_world/char_create_rules.h"
#include "client/login_world/character_select.h"
#include "client/login_world/character_select_service.h"
#include "client/string_database.h"
#include "client/willeq_database.h"
#include "common/net/session_log.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace EQT::Graphics;
using namespace eqt::login_world;
using eqt::charcreate::CharCreateData;
namespace fs = std::filesystem;
using L = CharSelectLayout;

namespace {

// ---------------------------------------------------------------------------
// The oracle XML
// ---------------------------------------------------------------------------

const fs::path kXml = "/mnt/projects/EverQuestP1999/uifiles/default/EQUI_CharacterSelect.xml";

std::string ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int Tag(const std::string& body, const std::string& tag) {
    const size_t at = body.find("<" + tag + ">");
    if (at == std::string::npos) return -99999;
    return std::atoi(body.c_str() + at + tag.size() + 2);
}

std::string Body(const std::string& xml, const std::string& name) {
    const size_t at = xml.find("item=\"" + name + "\"");
    if (at == std::string::npos) return std::string();
    const size_t next = xml.find("item=\"", at + 6);
    return xml.substr(at, next == std::string::npos ? std::string::npos : next - at);
}

struct XRect {
    int x, y, w, h;
    bool operator==(const XRect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};
std::ostream& operator<<(std::ostream& os, const XRect& r) {
    return os << "(" << r.x << "," << r.y << " " << r.w << "x" << r.h << ")";
}

XRect Loc(const std::string& xml, const std::string& name) {
    const std::string b = Body(xml, name);
    return {Tag(b, "X"), Tag(b, "Y"), Tag(b, "CX"), Tag(b, "CY")};
}

XRect Rel(const L::Rect& r, int sw, int sh) {
    const auto o = L::clientOrigin(sw, sh);
    return {r.UpperLeftCorner.X - o.X, r.UpperLeftCorner.Y - o.Y, r.getWidth(), r.getHeight()};
}

// ---------------------------------------------------------------------------
// The recorded OP_SendCharInfo (data/captures/session.sleq, P29: the first
// World S->C record, 1704 bytes). Non-zero byte runs, offset -> bytes; every
// other byte is zero. Slots 0-2: Testcaster (race 6, class 12 Wizard, zone 25),
// Testchar (race 1, class 1 Warrior, zone 36), Testmelee (race 6, class 1,
// zone 25); 3-9 "<none>".
// ---------------------------------------------------------------------------

struct Run {
    size_t offset;
    const char* hex;
};
const Run kRecordedCharInfo[] = {
    {0, "06"}, {4, "01"}, {8, "06"}, {448, "05"}, {452, "30"}, {520, "9929"}, {524, "30"},
    {780, "30"}, {788, "30"}, {820, "ffffffffffffffffffff"}, {832, "ce"}, {836, "8c01"}, {840, "ce"},
    {872, "01"}, {874, "01"}, {882, "010101"}, {902, "ffffffffffffffffffff05"}, {920, "9929"},
    {964, "19"}, {968, "24"}, {972, "19"}, {1004, "0c0101"}, {1014, "03"}, {1016, "04"},
    {1024, "54657374636173746572"}, {1088, "5465737463686172"}, {1152, "546573746d656c6565"},
    {1216, "3c6e6f6e653e"}, {1280, "3c6e6f6e653e"}, {1344, "3c6e6f6e653e"}, {1408, "3c6e6f6e653e"},
    {1472, "3c6e6f6e653e"}, {1536, "3c6e6f6e653e"}, {1600, "3c6e6f6e653e"}, {1664, "01"}, {1666, "01"},
    {1694, "010101"},
};

std::vector<uint8_t> RecordedCharInfo() {
    std::vector<uint8_t> body(1704, 0);
    for (const auto& r : kRecordedCharInfo) {
        const std::string h = r.hex;
        for (size_t i = 0; i + 1 < h.size(); i += 2) {
            body[r.offset + i / 2] = static_cast<uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16));
        }
    }
    return body;
}

// A Titanium CharacterSelect_Struct with the given names (test_char_create.cpp's shape).
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

const std::vector<uint8_t> kExpansion = {0xff, 0, 0, 0};

// eqstr_us.txt, verbatim, for the strings the screen draws (the shipped db is
// checked against these in TheShippedStringsAreTheOnesTheScreenQuotes).
std::string Eqstr(uint32_t id) {
    switch (id) {
    case 9065: return "Are you absolutely sure you want to delete %1?";
    case 1947: return "Create a New Character";
    case 1581: return "Name has been rejected. Please choose another.";
    default: return "eqstr" + std::to_string(id);
    }
}

const CharCreateData& Data() {
    static CharCreateData d = [] {
        CharCreateData x;
        EXPECT_TRUE(x.loadShipped()) << x.error();
        return x;
    }();
    return d;
}

// The two threads, in one: a client whose world stream is replayed, the real
// service, the channel and the screen.
struct Rig {
    explicit Rig(const std::string& configured = "", bool screen = true)
        : eq("127.0.0.1", 0, "test", "test", "test", configured) {
        eqt::ResetFatalExitForTest();
        eq.Test_SetCaptureQueuedPacketsForSeam(true);
        eq.Test_ClearQueuedPackets();
        eq.LoginWorld().SetCharacterSelectScreen(screen);
        layout.computeLayout(kW, kH);
    }
    void World(uint16_t op, const std::vector<uint8_t>& body) {
        eq.LoginWorld().ReplayWorldPacket(op, body.data(), body.size());
    }
    // One game tick, then the render thread takes the view.
    void Tick() {
        service.tick(eq.LoginWorld(), channel, Eqstr);
        CharacterSelectView v;
        if (channel.takeViewIfNewer(seen, v)) applyCharacterSelectView(screen, v);
    }
    void Do(const CharSelectAction& a) {
        if (a.kind == CharSelectAction::Kind::Post) channel.post(a.request);
        if (a.kind == CharSelectAction::Kind::Quit) quit = true;
        Tick();
    }
    void Click(const L::Rect& r, bool right = false) {
        const auto c = r.getCenter();
        Do(applyCharSelectClick(screen, layout, c.X, c.Y, right, Data(), 7));
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
    void Clear() { eq.Test_ClearQueuedPackets(); }

    static constexpr int kW = 1024, kH = 768;
    EverQuest eq;
    CharacterSelectService service;
    CharacterSelectChannel channel;
    uint64_t seen = 0;
    CharSelectScreenState screen;
    UILayout layout;
    bool quit = false;
};

std::string NameAt(const std::vector<uint8_t>* bytes) {
    return bytes ? std::string(reinterpret_cast<const char*>(bytes->data())) : std::string();
}

}  // namespace

// ===========================================================================
// When the screen is shown -- and, hard, when it is NOT
// ===========================================================================

TEST(CharacterSelectDecision, AConfiguredCharacterKeepsTheAutoSelect) {
    // The four-way rule (character_select.h).
    EXPECT_FALSE(CharacterSelectScreenWanted("Testchar", false, true, false, false));
    EXPECT_TRUE(CharacterSelectScreenWanted("", false, true, false, false));
    EXPECT_TRUE(CharacterSelectScreenWanted("Testchar", true, true, false, false));
    EXPECT_FALSE(CharacterSelectScreenWanted("", true, false, false, false)) << "headless never";
    EXPECT_FALSE(CharacterSelectScreenWanted("", true, true, true, false)) << "a world script owns select";
    EXPECT_FALSE(CharacterSelectScreenWanted("", false, true, false, true)) << "--scenario: flag only";
    EXPECT_TRUE(CharacterSelectScreenWanted("", true, true, false, true));

    // The same rule through Application's own entry point, on the config a
    // CI / scenario / visual-gate run has: a configured character, no flag.
    eqt::ApplicationConfig cfg;
    cfg.character = "Testchar";
    cfg.graphicsEnabled = true;
    EXPECT_FALSE(eqt::Application::characterSelectWanted(cfg));
    cfg.scenarioName = "zone_in";
    cfg.graphicsEnabled = false;
    EXPECT_FALSE(eqt::Application::characterSelectWanted(cfg));
    cfg.character.clear();
    EXPECT_FALSE(eqt::Application::characterSelectWanted(cfg)) << "a headless scenario still needs a character";
}

TEST(CharacterSelectDecision, WithTheScreenOffTheHandshakeIsByteForByteUnchanged) {
    // The T14 auto-select: OP_EnterWorld for the configured name at
    // OP_ExpansionInfo, the exact 74 bytes WorldSendEnterWorld has always sent.
    Rig r("Testchar", /*screen=*/false);
    r.World(HC_OP_ExpansionInfo, kExpansion);
    ASSERT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_AckPacket, HC_OP_WorldClientReady, HC_OP_EnterWorld}));
    std::vector<uint8_t> expected(72, 0);
    std::memcpy(expected.data(), "Testchar", 8);
    EXPECT_EQ(*r.Last(HC_OP_EnterWorld), expected);
    r.Clear();
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    EXPECT_TRUE(r.Ops().empty());
    // The service is never constructed on such a run; were it ticked, it does nothing.
    r.Tick();
    EXPECT_FALSE(r.channel.active());
    EXPECT_TRUE(r.channel.drain().empty());
}

TEST(CharacterSelectDecision, WithTheScreenOnNothingEntersWorldByItself) {
    Rig r;
    r.World(HC_OP_ExpansionInfo, kExpansion);
    EXPECT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_AckPacket, HC_OP_WorldClientReady}));
    r.Tick();
    EXPECT_FALSE(r.channel.active()) << "no list yet: the loading screen stays";
    r.Clear();
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    EXPECT_TRUE(r.Ops().empty());
    EXPECT_TRUE(r.channel.active());
}

// ===========================================================================
// Geometry
// ===========================================================================

TEST(CharacterSelectLayout, EveryControlIsTheXmlsOwnRect) {
    if (!fs::exists(kXml)) GTEST_SKIP() << "no oracle install at " << kXml;
    const std::string xml = ReadFile(kXml);
    const int sw = 1024, sh = 768;
    const XRect win = Loc(xml, "CharacterSelectWindow");
    EXPECT_EQ(win.w, L::kWindowW);
    EXPECT_EQ(win.h, L::kWindowH);
    EXPECT_EQ(Rel(L::exploreRect(sw, sh), sw, sh), Loc(xml, "CSW_Explore_Button"));
    EXPECT_EQ(Rel(L::deleteRect(sw, sh), sw, sh), Loc(xml, "CSW_Delete_Button"));
    EXPECT_EQ(Rel(L::rotateRect(sw, sh), sw, sh), Loc(xml, "CSW_Rotate_Button"));
    EXPECT_EQ(Rel(L::quitRect(sw, sh), sw, sh), Loc(xml, "CSW_Quit_Button"));
    EXPECT_EQ(Rel(L::enterWorldRect(sw, sh), sw, sh), Loc(xml, "CSW_Enter_World_Button"));
    EXPECT_EQ(Rel(L::titleLabelRect(sw, sh), sw, sh), Loc(xml, "CS_TitleLabel"));
    for (int n = 0; n < L::kCharButtons; ++n) {
        EXPECT_EQ(Rel(L::charButtonRect(sw, sh, n), sw, sh), Loc(xml, "CSW_Char" + std::to_string(n + 1) + "_Button"))
            << "Char" << n + 1;
    }
    // Eight, not ten: Titanium declares CSW_Char1..8 and nothing after.
    EXPECT_TRUE(Body(xml, "CSW_Char9_Button").empty());
    // The labels the screen draws are the XML's (and eqstr 1947's, which the
    // oracle frame shows in place of the XML's "Create New Character").
    for (const char* text : {"<Text>Explore</Text>", "<Text>Delete</Text>", "<Text>Rotate</Text>",
                             "<Text>Enter World</Text>", "<Text>Quit</Text>", "<Text>Character Select</Text>"}) {
        EXPECT_NE(xml.find(text), std::string::npos) << text;
    }
}

TEST(CharacterSelectLayout, TheWindowIsLeftAndVerticallyCentredAsMeasured) {
    // Pixel edges on review/UIGAP-ORACLE/318/charselect.00_initial_Testcaster_selected.png
    // (1024x768): the frame x 6..241, y 145..625; Char1..8 left x 19, top
    // 255 + 35n; Quit 19..117 x 579..610; Enter World left x 131.
    EXPECT_EQ(L::windowRect(1024, 768), L::Rect(6, 145, 241, 625));
    for (int n = 0; n < L::kCharButtons; ++n) {
        EXPECT_EQ(L::charButtonRect(1024, 768, n).UpperLeftCorner, irr::core::vector2di(19, 255 + 35 * n)) << n;
    }
    EXPECT_EQ(L::quitRect(1024, 768).UpperLeftCorner, irr::core::vector2di(19, 579));
    EXPECT_EQ(L::enterWorldRect(1024, 768).UpperLeftCorner.X, 131);
    // Every control stays inside the window at the smallest resolution.
    for (int n = 0; n < L::kCharButtons; ++n) {
        EXPECT_TRUE(L::windowRect(640, 480).isRectCollided(L::charButtonRect(640, 480, n)));
    }
    EXPECT_GE(L::windowRect(800, 600).LowerRightCorner.Y, L::enterWorldRect(800, 600).LowerRightCorner.Y);
}

// ===========================================================================
// The list
// ===========================================================================

TEST(CharacterSelectList, TheRecordedPayloadBecomesTheRows) {
    Rig r;
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    ASSERT_EQ(r.screen.rows.size(), 3u);
    EXPECT_EQ(r.screen.rows[0].name, "Testcaster");
    EXPECT_EQ(r.screen.rows[0].infoLine, "Testcaster [1 Wizard]");
    EXPECT_EQ(r.screen.rows[0].zoneLine, "The Nektulos Forest");
    EXPECT_EQ(r.screen.rows[0].race, 6u);
    EXPECT_EQ(r.screen.rows[1].infoLine, "Testchar [1 Warrior]");
    EXPECT_EQ(r.screen.rows[1].zoneLine, "Befallen");
    EXPECT_EQ(r.screen.rows[2].infoLine, "Testmelee [1 Warrior]");
    EXPECT_EQ(r.screen.selected, 0) << "the first is selected at open (MEASURED, 318 rule 4)";
    EXPECT_EQ(charSelectRowForButton(r.screen, 2), 2);
    EXPECT_EQ(charSelectRowForButton(r.screen, 3), -1) << "Create a New Character";
    EXPECT_EQ(charSelectEqstr(r.screen, 1947), "Create a New Character");
}

TEST(CharacterSelectList, TheEmbeddedPayloadIsTheCapturesOwnRecord) {
    const fs::path cap = fs::path(WILLEQ_SOURCE_DIR) / "data/captures/session.sleq";
    const fs::path main = "/mnt/projects/fable/main/data/captures/session.sleq";
    const fs::path path = fs::exists(cap) ? cap : main;
    if (!fs::exists(path)) GTEST_SKIP() << "no session capture";
    const std::string all = ReadFile(path);
    size_t off = sizeof(EQT::Net::SessionLogFileHeader);
    while (off + sizeof(EQT::Net::SessionLogRecordHeader) <= all.size()) {
        EQT::Net::SessionLogRecordHeader h{};
        std::memcpy(&h, all.data() + off, sizeof(h));
        off += sizeof(h);
        if (h.stream == static_cast<uint8_t>(EQT::Net::SessionStream::World) &&
            h.direction == static_cast<uint8_t>(EQT::Net::SessionDirection::ServerToClient) &&
            h.opcode == HC_OP_SendCharInfo) {
            const std::vector<uint8_t> rec(all.begin() + static_cast<long>(off),
                                           all.begin() + static_cast<long>(off + h.size));
            EXPECT_EQ(rec, RecordedCharInfo());
            return;
        }
        off += h.size;
    }
    FAIL() << "no OP_SendCharInfo record in " << path;
}

TEST(CharacterSelectList, AnUnknownZoneSaysItsIdAndEraNamesLoad) {
    const auto names = LoadEraZoneLongNames();
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names.at(2), "North Qeynos");
    CharacterSelectEntry e;
    e.slot = 0;
    e.name = "Newbie";
    e.level = 1;
    e.char_class = 1;
    e.zone = 189;   // the tutorial: not an era zone
    const auto rows = BuildCharacterSelectRows({e}, names);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].infoLine, "Newbie [1 Warrior]");
    EXPECT_EQ(rows[0].zoneLine, "Zone 189");
}

TEST(CharacterSelectList, TheEqstrTableCoversBothScreens) {
    const auto ids = CharacterSelectEqstrIds(Data());
    auto has = [&](uint32_t id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    for (uint32_t id : {9065u, 1947u, 1576u, 1577u, 1580u, 1581u, 3238u, 5874u, 5875u}) EXPECT_TRUE(has(id)) << id;
    for (const auto& e : Data().visibleRaces()) EXPECT_TRUE(has(e.eqstrId)) << e.label;
    for (const auto& e : Data().visibleClasses()) EXPECT_TRUE(!e.eqstrId || has(e.eqstrId)) << e.label;
    // A hidden race's description is never asked for (Will 2026-09-23).
    for (const auto& e : Data().allRaces()) {
        if (!eqt::charcreate::isInEra(e.era) && e.eqstrId) EXPECT_FALSE(has(e.eqstrId)) << e.label;
    }
}

TEST(CharacterSelectList, TheShippedStringsAreTheOnesTheScreenQuotes) {
    const fs::path db = fs::path(WILLEQ_SOURCE_DIR) / "data/willeq.db";
    EQT::WilleqDatabase wdb;
    if (!wdb.open(db.string())) GTEST_SKIP() << "no " << db;
    EQT::WilleqDatabase::setGlobalInstance(&wdb);
    EQT::StringDatabase strings;
    strings.initFromDatabase();
    for (uint32_t id : {9065u, 1947u, 1581u}) EXPECT_EQ(strings.getString(id), Eqstr(id)) << id;
    EQT::WilleqDatabase::setGlobalInstance(nullptr);
}

// ===========================================================================
// Buttons -> requests -> packets
// ===========================================================================

TEST(CharacterSelectButtons, EnterWorldSendsTheSelectedCharacter) {
    Rig r;
    r.World(HC_OP_ExpansionInfo, kExpansion);
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    r.Clear();
    r.Click(L::charButtonRect(Rig::kW, Rig::kH, 1));   // Testchar
    EXPECT_EQ(r.screen.selected, 1);
    EXPECT_TRUE(r.Ops().empty()) << "selecting sends nothing";
    r.Click(L::enterWorldRect(Rig::kW, Rig::kH));
    ASSERT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_EnterWorld}));
    EXPECT_EQ(NameAt(r.Last(HC_OP_EnterWorld)), "Testchar");
    EXPECT_EQ(r.eq.LoginWorld().GetCharacterSelectIndex(), 1);
    // OP_EnterWorld is out: the screen comes down for good.
    r.Tick();
    EXPECT_FALSE(r.channel.active());
    r.Click(L::enterWorldRect(Rig::kW, Rig::kH));
    EXPECT_EQ(r.Ops().size(), 1u) << "a second press sends nothing";
}

TEST(CharacterSelectButtons, DeleteAsksThenSendsTheNameAndTheListShrinks) {
    Rig r;
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    r.Clear();
    r.Click(L::charButtonRect(Rig::kW, Rig::kH, 2));   // Testmelee
    r.Click(L::deleteRect(Rig::kW, Rig::kH));
    ASSERT_TRUE(r.screen.confirm.isOpen);
    EXPECT_EQ(r.screen.confirm.text, "Are you absolutely sure you want to delete Testmelee?");
    EXPECT_TRUE(r.Ops().empty()) << "nothing is sent before Yes";
    // The prompt is modal: a character button underneath does nothing.
    r.Click(L::charButtonRect(Rig::kW, Rig::kH, 0));
    EXPECT_EQ(r.screen.selected, 2);
    r.Click(ConfirmationLayout::yesRect(r.layout));
    ASSERT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_DeleteCharacter}));
    EXPECT_EQ(NameAt(r.Last(HC_OP_DeleteCharacter)), "Testmelee");
    EXPECT_FALSE(r.screen.confirm.isOpen);
    EXPECT_TRUE(charSelectBlocked(r.screen));
    // MEASURED (review/UIGAP-ORACLE/318, opcodes_hex 15:42:08): "Testmelee\0", 10 bytes.
    EXPECT_EQ(r.Last(HC_OP_DeleteCharacter)->size(), 10u);
    r.Clear();
    // World answers with the new list (world/client.cpp:1072-1082); the
    // official screen answers that with the CRC pair (318, 15:42:09).
    r.World(HC_OP_SendCharInfo, CharInfo({"Testcaster", "Testchar"}));
    r.Tick();
    EXPECT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_World_Client_CRC1, HC_OP_World_Client_CRC2}));
    EXPECT_FALSE(charSelectBlocked(r.screen));
    ASSERT_EQ(r.screen.rows.size(), 2u);
    EXPECT_EQ(r.screen.selected, 0) << "the deleted one's selection falls back to the first";
    EXPECT_FALSE(r.quit);
    EXPECT_TRUE(r.channel.active()) << "a delete does not leave character select";
}

TEST(CharacterSelectButtons, NoAndTheTimeoutSendNothing) {
    Rig r;
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    r.Clear();
    r.Click(L::deleteRect(Rig::kW, Rig::kH));
    ASSERT_TRUE(r.screen.confirm.isOpen);
    r.Click(ConfirmationLayout::noRect(r.layout));
    EXPECT_FALSE(r.screen.confirm.isOpen);
    r.Click(L::deleteRect(Rig::kW, Rig::kH));
    tickCharSelect(r.screen, CharSelectScreenState::kDeleteConfirmSeconds - 0.5f);
    EXPECT_TRUE(r.screen.confirm.isOpen);
    tickCharSelect(r.screen, 1.0f);
    EXPECT_FALSE(r.screen.confirm.isOpen) << "it closes itself unanswered (MEASURED ~10 s, 318/delete_dialog_timing.txt)";
    r.Click(L::deleteRect(Rig::kW, Rig::kH));
    applyCharSelectKey(r.screen, irr::KEY_ESCAPE, 0);
    EXPECT_FALSE(r.screen.confirm.isOpen);
    r.Tick();
    EXPECT_TRUE(r.Ops().empty());
}

TEST(CharacterSelectButtons, QuitAndTheGreyedControls) {
    Rig r;
    r.World(HC_OP_SendCharInfo, CharInfo({}));   // an empty account
    r.Tick();
    EXPECT_EQ(r.screen.selected, -1);
    const int sw = Rig::kW, sh = Rig::kH;
    auto hit = [&](const L::Rect& rc) {
        const auto c = rc.getCenter();
        return hitTestCharSelect(sw, sh, r.screen, c.X, c.Y).hit;
    };
    // T363: a control with nothing behind it is greyed and inert.
    EXPECT_EQ(hit(L::exploreRect(sw, sh)), CharSelectHit::None);
    EXPECT_EQ(hit(L::rotateRect(sw, sh)), CharSelectHit::None);
    EXPECT_EQ(hit(L::enterWorldRect(sw, sh)), CharSelectHit::None) << "nothing selected";
    EXPECT_EQ(hit(L::deleteRect(sw, sh)), CharSelectHit::None) << "nothing selected";
    EXPECT_EQ(hit(L::quitRect(sw, sh)), CharSelectHit::Quit);
    r.Click(L::quitRect(sw, sh));
    EXPECT_TRUE(r.quit);
    EXPECT_TRUE(r.Ops().empty());
}

// ===========================================================================
// B1082 (GitLab #402): the select screen's keyboard -- MEASURED on the official
// client (refactor/B1082/README.md)
// ===========================================================================

TEST(CharacterSelectKeys, UpDownMoveTheSelectionOneRowAndWrapWithoutSending) {
    Rig r;
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    r.Clear();
    ASSERT_EQ(r.screen.selected, 0);
    r.Do(applyCharSelectKey(r.screen, irr::KEY_DOWN, 0));
    EXPECT_EQ(r.screen.selected, 1) << "Down -> the next occupied row";
    EXPECT_TRUE(r.Ops().empty()) << "MEASURED: selecting sends nothing";
    r.Do(applyCharSelectKey(r.screen, irr::KEY_DOWN, 0));
    EXPECT_EQ(r.screen.selected, 2);
    // Wraps within the occupied rows -- never into the empty "Create a New
    // Character" slots (the model stays present on the official client).
    r.Do(applyCharSelectKey(r.screen, irr::KEY_DOWN, 0));
    EXPECT_EQ(r.screen.selected, 0) << "Down on the last occupied row wraps to the first";
    r.Do(applyCharSelectKey(r.screen, irr::KEY_UP, 0));
    EXPECT_EQ(r.screen.selected, 2) << "Up on the first occupied row wraps to the last";
    EXPECT_TRUE(r.Ops().empty()) << "and still nothing on the wire";
    r.Tick();
    EXPECT_TRUE(r.channel.active()) << "navigation does not leave the screen";
}

TEST(CharacterSelectKeys, EnterAndReturnEnterTheWorldForTheSelectedRow) {
    Rig r;
    r.World(HC_OP_ExpansionInfo, kExpansion);
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    r.Clear();
    ASSERT_EQ(r.screen.selected, 0);
    r.Do(applyCharSelectKey(r.screen, irr::KEY_DOWN, 0));
    r.Do(applyCharSelectKey(r.screen, irr::KEY_DOWN, 0));   // Testmelee (row 2)
    ASSERT_EQ(r.screen.selected, 2);
    r.Do(applyCharSelectKey(r.screen, irr::KEY_RETURN, 0));   // Enter/Return
    ASSERT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_EnterWorld}));
    EXPECT_EQ(NameAt(r.Last(HC_OP_EnterWorld)), "Testmelee") << "the arrow-chosen row, not the first";
    EXPECT_FALSE(r.channel.active());
}

TEST(CharacterSelectKeys, TheDeletePromptIgnoresEverythingButEscape) {
    Rig r;
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    r.Clear();
    r.Click(L::charButtonRect(Rig::kW, Rig::kH, 2));
    r.Click(L::deleteRect(Rig::kW, Rig::kH));
    ASSERT_TRUE(r.screen.confirm.isOpen);
    // MEASURED: the prompt does not answer Yes/No on Enter and does not
    // navigate or type while up.
    applyCharSelectKey(r.screen, irr::KEY_RETURN, 0);
    EXPECT_TRUE(r.screen.confirm.isOpen) << "Enter does not answer the prompt";
    applyCharSelectKey(r.screen, irr::KEY_DOWN, 0);
    applyCharSelectKey(r.screen, irr::KEY_KEY_A, static_cast<wchar_t>('a'));
    EXPECT_TRUE(r.screen.confirm.isOpen);
    EXPECT_EQ(r.screen.selected, 2) << "arrows are inert under the prompt";
    r.Tick();
    EXPECT_TRUE(r.Ops().empty());
    applyCharSelectKey(r.screen, irr::KEY_ESCAPE, 0);   // the measured No
    EXPECT_FALSE(r.screen.confirm.isOpen);
    r.Tick();
    EXPECT_TRUE(r.Ops().empty());
}

// ===========================================================================
// Create: B888's screen, hosted
// ===========================================================================

TEST(CharacterSelectCreate, AnEmptySlotOpensTheCreationScreen) {
    Rig r;
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    r.Click(L::charButtonRect(Rig::kW, Rig::kH, 3));   // the first "Create a New Character"
    ASSERT_TRUE(r.screen.create.isOpen);
    EXPECT_EQ(r.screen.create.page, 0);
    EXPECT_EQ(r.screen.create.races.size(), Data().visibleRaces().size()) << "B888's era data, unchanged";
    EXPECT_TRUE(r.screen.create.raceClassValid(r.screen.create.race, r.screen.create.cls));
    for (int n = 0; n < eqt::charcreate::kStatCount; ++n) {
        EXPECT_EQ(r.screen.create.alloc.value(n), r.screen.create.alloc.base(n)) << "nothing spent at open";
    }
    EXPECT_GT(r.screen.create.alloc.pointsLeft(), 0);
    // B888's Back on page 0 returns here, with the FIRST row selected again
    // (MEASURED, review/UIGAP-ORACLE/318 rule 4, still 04).
    r.screen.selected = 2;
    r.Click(CharCreateLayout::backButtonRect(Rig::kW, Rig::kH));
    EXPECT_FALSE(r.screen.create.isOpen);
    EXPECT_EQ(r.screen.selected, 0);
    EXPECT_TRUE(r.channel.active());
}

namespace {

// Page 0 -> page 1 with a fixed selection and every point spent, the name typed.
void FillCreation(Rig& r, const std::string& name) {
    using namespace eqt::charcreate;
    ASSERT_TRUE(openCharCreateWith(r.screen.create, Data(), Race::Human, Gender::Male, Class::Warrior));
    // The right-click on a stat name spends every point left (measured, 204/06).
    r.Click(CharCreateLayout::statButtonRect(Rig::kW, Rig::kH, AGI), /*right=*/true);
    ASSERT_EQ(r.screen.create.alloc.pointsLeft(), 0);
    r.Click(CharCreateLayout::nextButtonRect(Rig::kW, Rig::kH));
    ASSERT_EQ(r.screen.create.page, 1);
    r.Click(CharCreateLayout::nameInputRect(Rig::kW, Rig::kH));
    for (char c : name) applyCharSelectKey(r.screen, irr::KEY_KEY_A, static_cast<wchar_t>(c));
}

}  // namespace

TEST(CharacterSelectCreate, NextSubmitsThroughTheServiceAndACreateEntersWorld) {
    Rig r;
    r.World(HC_OP_ExpansionInfo, kExpansion);
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    r.Clear();
    r.Click(L::charButtonRect(Rig::kW, Rig::kH, 3));
    FillCreation(r, "Cinewbie");
    EXPECT_EQ(r.screen.create.nameEntry.inputText, "Cinewbie");
    r.Click(CharCreateLayout::nextButtonRect(Rig::kW, Rig::kH));
    ASSERT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_ApproveName}));
    EXPECT_EQ(NameAt(r.Last(HC_OP_ApproveName)), "Cinewbie");
    EXPECT_TRUE(r.screen.create.submitting);
    r.Clear();
    r.World(HC_OP_ApproveName, {1});
    r.Tick();
    ASSERT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_CharacterCreate}));
    uint32_t tutorial = 1;
    std::memcpy(&tutorial, r.Last(HC_OP_CharacterCreate)->data() + 76, 4);
    EXPECT_EQ(tutorial, 0u);
    r.Clear();
    // World lists it: the CRC pair, then straight into the world (204/14).
    r.World(HC_OP_SendCharInfo, CharInfo({"Testcaster", "Testchar", "Testmelee", "Cinewbie"}));
    r.Tick();
    EXPECT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_World_Client_CRC1, HC_OP_World_Client_CRC2, HC_OP_EnterWorld}));
    EXPECT_EQ(NameAt(r.Last(HC_OP_EnterWorld)), "Cinewbie");
    EXPECT_FALSE(r.channel.active());
}

TEST(CharacterSelectCreate, ARefusedNameComesBackAsTheModal) {
    Rig r;
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    r.Click(L::charButtonRect(Rig::kW, Rig::kH, 3));
    FillCreation(r, "Testchar");
    r.Clear();
    r.Click(CharCreateLayout::nextButtonRect(Rig::kW, Rig::kH));
    ASSERT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_ApproveName}));
    r.World(HC_OP_ApproveName, {0});
    r.Tick();
    EXPECT_FALSE(r.screen.create.submitting);
    EXPECT_FALSE(r.screen.create.messageText.empty()) << "eqstr 1581 (204/12)";
    EXPECT_TRUE(r.screen.create.isOpen);
    EXPECT_TRUE(r.channel.active());
    EXPECT_FALSE(r.quit);
    EXPECT_FALSE(eqt::FatalExitRecorded()) << "a screen create is not a world script: no FATAL";
}

TEST(CharacterSelectCreate, GetNameFillsTheField) {
    Rig r;
    r.World(HC_OP_SendCharInfo, RecordedCharInfo());
    r.Tick();
    r.Click(L::charButtonRect(Rig::kW, Rig::kH, 3));
    FillCreation(r, "");
    r.Clear();
    r.Click(CharCreateLayout::getNameButtonRect(Rig::kW, Rig::kH));
    ASSERT_EQ(r.Ops(), (std::vector<uint16_t>{HC_OP_RandomNameGenerator}));
    std::vector<uint8_t> echo(72, 0);
    std::memcpy(echo.data() + 8, "Ozanas", 6);
    r.World(HC_OP_RandomNameGenerator, echo);
    r.Tick();
    EXPECT_EQ(r.screen.create.nameEntry.inputText, "Ozanas");
}

TEST(CharacterSelectCreate, TheNameFieldHoldsFifteenAndLowerCasesAsMeasured) {
    CharSelectScreenState s;
    ASSERT_TRUE(openCharCreateWith(s.create, Data(), eqt::charcreate::Race::Human, eqt::charcreate::Gender::Male,
                                   eqt::charcreate::Class::Warrior));
    s.create.page = 1;
    focusTextEntry(s.create.nameEntry, "");
    for (char c : std::string("HELIKANATESTCHARX")) applyCharSelectKey(s, irr::KEY_KEY_A, static_cast<wchar_t>(c));
    // review/UIGAP-ORACLE/204/10: a 15-character field, letters after the first lower-cased.
    EXPECT_EQ(s.create.nameEntry.inputText, "Helikanatestcha");
}
