// T224: OP_WhoAllResponse entries.
//
// Before T224 the handler read the 64-byte header, printed the count, and
// threw the entries away, so /who showed "There is 1 player in EverQuest."
// and nothing else. These tests pin (a) the packed entry layout eqemu writes
// (WhoAllPlayerPart1..4 under #pragma pack(1), 47 fixed bytes + the three
// strings) and (b) the rendered lines, through the pure functions in
// who_response.cpp and end-to-end through the ReplayZonePacket seam.
//
// Packet bytes are constructed by hand at the offsets the wire uses -- there
// is no OP_WhoAllResponse in data/captures/session.sleq (checked T224:
// zero occurrences of the 5023/5024/5025 format-id + 0xFFFFFFFF pid pair).

#include <gtest/gtest.h>

#include "client/who_response.h"
#include "client/willeq_database.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace zonetest;
using namespace EQT;

namespace {

void putU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 24));
}

void putCStr(std::vector<uint8_t>& v, const std::string& s) {
    v.insert(v.end(), s.begin(), s.end());
    v.push_back(0);
}

// One entry exactly as zone/entity.cpp EntityList::ZoneWho and
// world/clientlist.cpp SendWhoAll lay it down.
std::vector<uint8_t> EntryBytes(uint32_t format, const std::string& name, uint32_t rank,
                                const std::string& guild, uint32_t ldString,
                                uint32_t zoneString, uint32_t zone, uint32_t cls,
                                uint32_t level, uint32_t race, uint32_t unknown100 = 0) {
    std::vector<uint8_t> v;
    putU32(v, format);
    putU32(v, kWhoStringNone);       // pidstring
    putCStr(v, name);
    putU32(v, rank);
    putCStr(v, guild);
    putU32(v, kWhoStringNone);       // unknown80[0]
    putU32(v, ldString);             // unknown80[1]
    putU32(v, zoneString);
    putU32(v, zone);
    putU32(v, cls);
    putU32(v, level);
    putU32(v, race);
    putCStr(v, "");                  // account
    putU32(v, unknown100);
    return v;
}

// Full S->C payload (no opcode prefix): 64-byte header + entries, header
// filled the way EntityList::ZoneWho fills it.
std::vector<uint8_t> WhoPayload(const std::vector<std::vector<uint8_t>>& entries,
                                uint32_t playerineqstring = kWhoStrPlayersInEq) {
    WhoAllReturnHeader h{};
    h.id = 0;
    h.playerineqstring = playerineqstring;
    std::strncpy(h.line, "---------------------------", sizeof(h.line));
    h.unknown035 = 0x0a;
    const uint32_t n = static_cast<uint32_t>(entries.size());
    h.playersinzonestring = n == 0 ? kWhoStrNoPlayers
                          : n == 1 ? kWhoStrOnePlayer : kWhoStrManyPlayers;
    h.unknown052 = n;
    h.unknown056 = n;
    h.playercount = n;

    std::vector<uint8_t> v(sizeof(h));
    std::memcpy(v.data(), &h, sizeof(h));
    for (const auto& e : entries) {
        v.insert(v.end(), e.begin(), e.end());
    }
    return v;
}

WhoFormatContext DefaultCtx() {
    WhoFormatContext ctx;
    ctx.eqstr = [](uint32_t) { return std::string(); };   // force the built-in defaults
    ctx.className = [](uint32_t c) { return c == 1 ? "Warrior" : c == 2 ? "Cleric" : "Class"; };
    ctx.raceName = [](uint32_t r) { return r == 1 ? "Human" : r == 8 ? "Dwarf" : "Race"; };
    ctx.zoneName = [](uint32_t z) { return z == 2 ? "qeynos2" : ""; };
    return ctx;
}

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

TEST(WhoResponseParse, FixedPartIs47BytesPlusStrings) {
    // eqemu sizes the packet as 47*entries + strlen(name) + strlen(guild);
    // the 47 is the packed layout with three empty strings.
    auto e = EntryBytes(kWhoStrFormatNormal, "", kWhoStringNone, "", kWhoStringNone,
                        kWhoStringNone, 0, 1, 50, 1);
    EXPECT_EQ(e.size(), 47u);
}

TEST(WhoResponseParse, ReadsEveryFieldOfTwoEntries) {
    auto a = EntryBytes(kWhoStrFormatNormal, "Bob", kWhoStrGm, "<Guildy>", kWhoStrLinkdead,
                        kWhoStrZone, 2, 1, 50, 1, 207);
    auto b = EntryBytes(kWhoStrFormatAnon, "Alice", kWhoStringNone, "", kWhoStringNone,
                        kWhoStringNone, 0, 0, 0, 0);
    std::vector<uint8_t> buf(a);
    buf.insert(buf.end(), b.begin(), b.end());

    std::vector<WhoAllEntry> out;
    ASSERT_TRUE(parseWhoAllEntries(buf.data(), buf.size(), 2, out));
    ASSERT_EQ(out.size(), 2u);

    EXPECT_EQ(out[0].formatString, kWhoStrFormatNormal);
    EXPECT_EQ(out[0].pidString, kWhoStringNone);
    EXPECT_EQ(out[0].name, "Bob");
    EXPECT_EQ(out[0].rankString, kWhoStrGm);
    EXPECT_EQ(out[0].guild, "<Guildy>");
    EXPECT_EQ(out[0].unknown80[0], kWhoStringNone);
    EXPECT_EQ(out[0].unknown80[1], kWhoStrLinkdead);
    EXPECT_EQ(out[0].zoneString, kWhoStrZone);
    EXPECT_EQ(out[0].zone, 2u);
    EXPECT_EQ(out[0].classId, 1u);
    EXPECT_EQ(out[0].level, 50u);
    EXPECT_EQ(out[0].race, 1u);
    EXPECT_EQ(out[0].account, "");
    EXPECT_EQ(out[0].unknown100, 207u);

    EXPECT_EQ(out[1].formatString, kWhoStrFormatAnon);
    EXPECT_EQ(out[1].name, "Alice");
    EXPECT_EQ(out[1].guild, "");
    EXPECT_FALSE(out[1].unknown80[1] == kWhoStrLinkdead);
    EXPECT_EQ(out[1].level, 0u);
}

TEST(WhoResponseParse, TruncatedBufferReturnsFalseAndKeepsCompleteEntries) {
    auto a = EntryBytes(kWhoStrFormatNormal, "Bob", kWhoStringNone, "", kWhoStringNone,
                        kWhoStringNone, 0, 1, 50, 1);
    auto b = EntryBytes(kWhoStrFormatNormal, "Alice", kWhoStringNone, "", kWhoStringNone,
                        kWhoStringNone, 0, 2, 40, 8);
    std::vector<uint8_t> buf(a);
    buf.insert(buf.end(), b.begin(), b.end() - 5);   // chop the second entry

    std::vector<WhoAllEntry> out;
    EXPECT_FALSE(parseWhoAllEntries(buf.data(), buf.size(), 2, out));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].name, "Bob");
}

TEST(WhoResponseParse, UnterminatedNameDoesNotReadPastBuffer) {
    std::vector<uint8_t> buf;
    putU32(buf, kWhoStrFormatNormal);
    putU32(buf, kWhoStringNone);
    buf.insert(buf.end(), {'B', 'o', 'b'});  // no NUL, buffer ends
    std::vector<WhoAllEntry> out;
    EXPECT_FALSE(parseWhoAllEntries(buf.data(), buf.size(), 1, out));
    EXPECT_TRUE(out.empty());
}

TEST(WhoResponseParse, ZeroCountIsTrueWithNoEntries) {
    std::vector<WhoAllEntry> out;
    EXPECT_TRUE(parseWhoAllEntries(nullptr, 0, 0, out));
    EXPECT_TRUE(out.empty());
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

TEST(WhoResponseFormat, NormalEntryWithGuildAndZone) {
    WhoAllEntry e;
    e.formatString = kWhoStrFormatNormal;
    e.name = "Bob";
    e.guild = "<Guildy>";
    e.zoneString = kWhoStrZone;
    e.zone = 2;
    e.classId = 1;
    e.level = 50;
    e.race = 1;
    EXPECT_EQ(formatWhoAllEntry(e, DefaultCtx()),
              "[50 Warrior] Bob (Human) <Guildy> ZONE: qeynos2");
}

TEST(WhoResponseFormat, ZoneLocalWhoHasNoZoneAndNoGuild) {
    // EntityList::ZoneWho sends zonestring=0xFFFFFFFF, zone=0.
    WhoAllEntry e;
    e.formatString = kWhoStrFormatNormal;
    e.name = "Bob";
    e.classId = 2;
    e.level = 12;
    e.race = 8;
    EXPECT_EQ(formatWhoAllEntry(e, DefaultCtx()), "[12 Cleric] Bob (Dwarf)");
}

TEST(WhoResponseFormat, GmRankPrefixesAndLinkdeadSuffixes) {
    WhoAllEntry e;
    e.formatString = kWhoStrFormatNormal;
    e.name = "Bob";
    e.rankString = kWhoStrGm;
    e.unknown80[1] = kWhoStrLinkdead;
    e.classId = 1;
    e.level = 50;
    e.race = 1;
    EXPECT_EQ(formatWhoAllEntry(e, DefaultCtx()),
              "* GM * [50 Warrior] Bob (Human) <LINKDEAD>");
}

TEST(WhoResponseFormat, AnonymousHidesLevelClassRaceAndZone) {
    WhoAllEntry e;
    e.formatString = kWhoStrFormatAnon;
    e.name = "Alice";
    e.guild = "<Guildy>";
    // eqemu zeroes these for anon, but even if it did not they must not show.
    e.classId = 1;
    e.level = 50;
    e.race = 1;
    e.zoneString = kWhoStrZone;
    e.zone = 2;
    EXPECT_EQ(formatWhoAllEntry(e, DefaultCtx()), "[ANONYMOUS] Alice <Guildy>");
}

TEST(WhoResponseFormat, RoleplayShowsZoneButNotLevel) {
    WhoAllEntry e;
    e.formatString = kWhoStrFormatRoleplay;
    e.name = "Alice";
    e.zoneString = kWhoStrZone;
    e.zone = 2;
    EXPECT_EQ(formatWhoAllEntry(e, DefaultCtx()), "[ANONYMOUS] Alice ZONE: qeynos2");
}

TEST(WhoResponseFormat, UnknownZoneIdFallsBackToTheNumber) {
    WhoAllEntry e;
    e.formatString = kWhoStrFormatNormal;
    e.name = "Bob";
    e.zoneString = kWhoStrZone;
    e.zone = 9999;
    e.classId = 1;
    e.level = 1;
    e.race = 1;
    EXPECT_EQ(formatWhoAllEntry(e, DefaultCtx()), "[1 Warrior] Bob (Human) ZONE: 9999");
}

// T224 live leg (world ClientList::SendWhoAll, GM viewer): pidstring=5003
// "(USER %1: PID %2)" with the account name and unknown100 (207) as its two
// arguments, appended as %9. Before this the bare account name was glued
// onto the line ("ZONE: befallen willeq_test").
TEST(WhoResponseFormat, GmViewerSeesUserAndPidTrailer) {
    WhoAllEntry e;
    e.formatString = kWhoStrFormatNormal;
    e.pidString = kWhoStrUserPid;
    e.name = "Testchar";
    e.zoneString = kWhoStrZone;
    e.zone = 36;
    e.classId = 1;
    e.level = 2;
    e.race = 1;
    e.account = "willeq_test";
    e.unknown100 = 207;
    e.unknown80[0] = 250;   // world sends the numeric admin level here -- not a string id
    WhoFormatContext ctx = DefaultCtx();
    ctx.zoneName = [](uint32_t z) { return z == 36 ? "befallen" : ""; };
    EXPECT_EQ(formatWhoAllEntry(e, ctx),
              "[2 Warrior] Testchar (Human) ZONE: befallen (USER willeq_test: PID 207)");
}

// eqstr 5015 "* GM-Admin *" carries no trailing blank (12312 "* GM * " does)
// and the template starts "%T1[", so the faithful render has no gap
// before the bracket -- the strings are rendered as shipped, not prettified.
TEST(WhoResponseFormat, GmRankStringsFromWorldAndAnonSeenByGm) {
    WhoAllEntry e;
    e.formatString = kWhoStrFormatAnonGmView;   // 5022 "%T1[ANON (%2 %3)] %4 (%5) %6 %7 %8"
    e.rankString = 5015;                         // "* GM-Admin *"
    e.name = "Alice";
    e.guild = "<Guildy>";
    e.zoneString = kWhoStrZone;
    e.zone = 2;
    e.classId = 2;
    e.level = 40;
    e.race = 8;
    EXPECT_EQ(formatWhoAllEntry(e, DefaultCtx()),
              "* GM-Admin *[ANON (40 Cleric)] Alice (Dwarf) <Guildy> ZONE: qeynos2");
}

TEST(WhoResponseFormat, RankStringZeroMeansNoRank) {
    WhoAllEntry e;
    e.formatString = kWhoStrFormatNormal;
    e.rankString = 0;   // world sends 0, zone sends 0xFFFFFFFF
    e.name = "Bob";
    e.classId = 1;
    e.level = 1;
    e.race = 1;
    EXPECT_EQ(formatWhoAllEntry(e, DefaultCtx()), "[1 Warrior] Bob (Human)");
}

TEST(WhoResponseFormat, StringDatabaseTemplateWinsOverBuiltIn) {
    WhoFormatContext ctx = DefaultCtx();
    ctx.eqstr = [](uint32_t id) {
        return id == kWhoStrFormatNormal ? std::string("%4 is a %3 of level %2") : std::string();
    };
    WhoAllEntry e;
    e.formatString = kWhoStrFormatNormal;
    e.name = "Bob";
    e.classId = 1;
    e.level = 50;
    e.race = 1;
    EXPECT_EQ(formatWhoAllEntry(e, ctx), "Bob is a Warrior of level 50");
}

TEST(WhoResponseFormat, TemplateHandlesTLookupMissingArgsAndLiteralPercent) {
    WhoFormatContext ctx = DefaultCtx();
    EXPECT_EQ(formatWhoTemplate("%T1[%2]", {std::to_string(kWhoStrGm), "x"}, ctx), "* GM * [x]");
    EXPECT_EQ(formatWhoTemplate("%T1[%2]", {"", "x"}, ctx), "[x]");
    EXPECT_EQ(formatWhoTemplate("%1 %9 end", {"a"}, ctx), "a  end");
    EXPECT_EQ(formatWhoTemplate("100% %1", {"a"}, ctx), "100% a");
    EXPECT_EQ(formatWhoTemplate("%T1", {"notanumber"}, ctx), "notanumber");
}

// ---------------------------------------------------------------------------
// End to end through the handler
// ---------------------------------------------------------------------------

// T271: the rendered line carries the race name, and race names come from
// race_models now (they used to come from a hardcoded switch that was shifted
// -- 14 "Iksar", 128 "Vah Shir", 130 "Froglok", 330 "Drakkin"). Open the
// shipped db exactly as EverQuest::OpenDatabase() does, otherwise these
// assertions would be pinning "Unknown (3)".
class WhoResponseHandlerTest : public ::testing::Test {
protected:
    WhoResponseHandlerTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~WhoResponseHandlerTest() override { eq.setBridge(nullptr); }

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        std::ifstream probe(dbPath);
        if (!probe.good()) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
    }
    void TearDown() override {
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    EQT::WilleqDatabase db_;
    EverQuest eq;
    TestBridge bridge;
};

TEST_F(WhoResponseHandlerTest, TwoEntriesRenderAsLinesBetweenHeaderAndCount) {
    auto payload = WhoPayload({
        EntryBytes(kWhoStrFormatNormal, "Bob", kWhoStringNone, "<Guildy>", kWhoStringNone,
                   kWhoStrZone, 2, 1, 50, 1, 207),
        EntryBytes(kWhoStrFormatAnon, "Alice", kWhoStringNone, "", kWhoStringNone,
                   kWhoStringNone, 0, 0, 0, 0, 207),
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_WhoAllResponse, payload));

    auto lines = DrainSystemMessages(bridge);
    ASSERT_EQ(lines.size(), 5u) << "header, rule, 2 entries, count";
    EXPECT_EQ(lines[0], "Players in EverQuest:");
    EXPECT_EQ(lines[1], "---------------------------");
    EXPECT_EQ(lines[2], "[50 Warrior] Bob (Human) <Guildy> ZONE: qeynos2");
    EXPECT_EQ(lines[3], "[ANONYMOUS] Alice");
    EXPECT_EQ(lines[4], "There are 2 players in EverQuest.");
}

TEST_F(WhoResponseHandlerTest, SingleZoneLocalEntry) {
    auto payload = WhoPayload({
        EntryBytes(kWhoStrFormatNormal, "Testchar", kWhoStrGm, "", kWhoStringNone,
                   kWhoStringNone, 0, 9, 1, 3),
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_WhoAllResponse, payload));

    auto lines = DrainSystemMessages(bridge);
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[2], "* GM * [1 Rogue] Testchar (Erudite)");
    EXPECT_EQ(lines[3], "There is 1 player in EverQuest.");
}

// T271: the user-visible half of the shifted-race-name defect. Race 14 is WER
// Werewolf in the audited race_models rows; the deleted switch printed
// "Iksar" here (Iksar is 128, which the switch called "Vah Shir").
TEST_F(WhoResponseHandlerTest, ShiftedRaceIdRendersTheRowName) {
    auto payload = WhoPayload({
        EntryBytes(kWhoStrFormatNormal, "Lupine", kWhoStringNone, "", kWhoStringNone,
                   kWhoStringNone, 0, 1, 12, 14),
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_WhoAllResponse, payload));

    auto lines = DrainSystemMessages(bridge);
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[2], "[12 Warrior] Lupine (Werewolf)");
}

TEST_F(WhoResponseHandlerTest, ZeroEntriesPrintsTheNoMatchString) {
    auto payload = WhoPayload({});
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_WhoAllResponse, payload));

    auto lines = DrainSystemMessages(bridge);
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines.back(), "There are no players in EverQuest that match those who filters.");
}

TEST_F(WhoResponseHandlerTest, CountLargerThanPayloadPrintsWhatParsedAndDoesNotCrash) {
    auto payload = WhoPayload({
        EntryBytes(kWhoStrFormatNormal, "Bob", kWhoStringNone, "", kWhoStringNone,
                   kWhoStringNone, 0, 1, 50, 1),
    });
    // Lie about the count: 3 entries claimed, 1 present.
    payload[60] = 3;
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_WhoAllResponse, payload));

    auto lines = DrainSystemMessages(bridge);
    bool sawBob = false;
    for (const auto& l : lines) {
        if (l.find("Bob") != std::string::npos) sawBob = true;
    }
    EXPECT_TRUE(sawBob);
}

TEST_F(WhoResponseHandlerTest, HeaderOnlyPacketStillRejectedWhenTooShort) {
    std::vector<uint8_t> tooShort(10, 0);
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_WhoAllResponse, tooShort));
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
}

// T256 live leg: world's SendWhoAll COUNTS an anon player whose admin level
// equals the viewer's (a GM looking at itself while /anon) but then SKIPS
// writing that entry, so the packet says "2 players" and carries 49 zeroed
// bytes where the second entry would be. Before T256 that padding rendered
// as a phantom "[0 Class0] (Race0)" line.
TEST_F(WhoResponseHandlerTest, ZeroPaddedEntryFromSkippedPlayerIsNotPrinted) {
    auto payload = WhoPayload({
        EntryBytes(kWhoStrFormatAnonGmView, "Testbuddy", kWhoStringNone, "<TestGuild133>",
                   kWhoStringNone, kWhoStrZone, 36, 13, 1, 6, 207),
        std::vector<uint8_t>(49, 0),
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_WhoAllResponse, payload));

    auto lines = DrainSystemMessages(bridge);
    ASSERT_EQ(lines.size(), 4u) << "header, rule, ONE entry, count (server's count kept)";
    EXPECT_EQ(lines[2], "[ANON (1 Magician)] Testbuddy (Dark Elf) <TestGuild133> ZONE: befallen");
    EXPECT_EQ(lines[3], "There are 2 players in EverQuest.");
}
