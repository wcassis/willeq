// T443 (board B418) -- OP_GuildsList parsed into a guild-id -> name lookup.
//
// The pre-fix red is the SOURCE CENSUS at the bottom: before this unit
// `LoginWorldComponent::WorldProcessGuildsList()` was one
// `LOG_DEBUG(MOD_WORLD, "Received guilds list")` that read none of the
// packet, and no `GuildNameTable` existed, so the parse cases below could
// not compile at all against the pre-fix tree (the T419/T436 shape). The
// census case CAN, and it fails there.
//
// Every wire fact asserted here is quoted in
// include/client/guild/guild_name_table.h from
// /mnt/projects/akk-stack/code/common/patches/titanium_structs.h:1412 and
// /mnt/projects/akk-stack/code/common/patches/titanium.cpp:819. Nothing is
// inferred from the shape of the bytes.
//
// Device-free, database-free, server-free.

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "client/guild/guild_name_table.h"

using eqt::guild::GuildNameTable;
using eqt::guild::kGuildNameBytes;
using eqt::guild::kGuildsListBodyBytes;
using eqt::guild::kGuildsListHeadBytes;
using eqt::guild::kMaxNumberGuilds;

namespace {

// A real Titanium GuildsList_Struct body: 64 zero bytes of head, then 1500
// 64-byte name fields, written AT THE GUILD ID -- which is what
// ENCODE(OP_GuildsList) does (`out->Guilds[g.guild_id].name`).
std::vector<uint8_t> BuildGuildsList(
    const std::vector<std::pair<uint32_t, std::string>>& guilds) {
    std::vector<uint8_t> body(kGuildsListBodyBytes, 0);
    for (const auto& [id, name] : guilds) {
        if (id >= kMaxNumberGuilds) continue;  // the server drops these
        uint8_t* field = body.data() + kGuildsListHeadBytes + id * kGuildNameBytes;
        const size_t n = std::min(name.size(), kGuildNameBytes - 1);
        std::memcpy(field, name.data(), n);
    }
    return body;
}

const std::string kRoot = WILLEQ_REPO_ROOT;

std::string slurp(const std::string& rel) {
    std::ifstream in(kRoot + "/" + rel);
    EXPECT_TRUE(in.good()) << "cannot open " << rel;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// A comment that NAMES a symbol must not satisfy a census that asks whether
// code USES it (T442's rule -- a tombstone or an explanatory note is not a
// call site).
std::string codeOnly(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    std::istringstream lines(src);
    std::string line;
    bool inBlock = false;
    while (std::getline(lines, line)) {
        if (inBlock) {
            const size_t end = line.find("*/");
            if (end == std::string::npos) continue;
            line = line.substr(end + 2);
            inBlock = false;
        }
        const size_t block = line.find("/*");
        if (block != std::string::npos) {
            const size_t end = line.find("*/", block + 2);
            if (end == std::string::npos) {
                line = line.substr(0, block);
                inBlock = true;
            } else {
                line = line.substr(0, block) + line.substr(end + 2);
            }
        }
        const size_t slashes = line.find("//");
        if (slashes != std::string::npos) line = line.substr(0, slashes);
        out += line;
        out += '\n';
    }
    return out;
}

}  // namespace

TEST(GuildNameTable, ResolvesAnIdToItsName) {
    const auto body = BuildGuildsList({{1, "The Ebon Mask"}});
    GuildNameTable t;
    ASSERT_EQ(t.parseTitaniumGuildsList(body.data(), body.size()), 1u);
    const std::string* n = t.find(1);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(*n, "The Ebon Mask");
}

// The one fact a reader would get backwards. The encoder does NOT append
// entries in order and sends no id field, so a sparse guild set must land on
// its own ids, not on 0,1,2.
TEST(GuildNameTable, TheArrayIndexIsTheGuildIdNotAnOrdinal) {
    const auto body = BuildGuildsList({{7, "Seven"}, {413, "FourOneThree"}});
    GuildNameTable t;
    ASSERT_EQ(t.parseTitaniumGuildsList(body.data(), body.size()), 2u);
    ASSERT_NE(t.find(7), nullptr);
    EXPECT_EQ(*t.find(7), "Seven");
    ASSERT_NE(t.find(413), nullptr);
    EXPECT_EQ(*t.find(413), "FourOneThree");
    // The negative: the ordinal reading would have put them at 0 and 1.
    EXPECT_EQ(t.find(0), nullptr);
    EXPECT_EQ(t.find(1), nullptr);
}

// The oracle's own fallback for a player with no guild is NOTHING -- the
// /who row 5025 is sent with an empty guild argument and the line simply has
// no guild fragment (world/clientlist.cpp only fills `guildbuffer` when
// GuildExists). So a miss returns nullptr; there is no string to invent.
TEST(GuildNameTable, AnUnknownIdResolvesToNothingAtAll) {
    const auto body = BuildGuildsList({{1, "The Ebon Mask"}});
    GuildNameTable t;
    t.parseTitaniumGuildsList(body.data(), body.size());
    EXPECT_EQ(t.find(0), nullptr);
    EXPECT_EQ(t.find(2), nullptr);
    EXPECT_EQ(t.find(999999), nullptr);
}

TEST(GuildNameTable, AnEmptySlotIsAnAbsentGuildNotAGuildNamedEmptyString) {
    const auto body = BuildGuildsList({{5, "Five"}});
    GuildNameTable t;
    EXPECT_EQ(t.parseTitaniumGuildsList(body.data(), body.size()), 1u);
    EXPECT_EQ(t.size(), 1u);
    EXPECT_EQ(t.find(4), nullptr);
}

// The head is `uint8 head[64]; // First on guild list seems to be empty...`
// -- it is NOT guild 0's name field.
TEST(GuildNameTable, TheSixtyFourByteHeadIsSkipped) {
    auto body = BuildGuildsList({{0, "GuildZero"}});
    std::memcpy(body.data(), "NOTAGUILD", 9);
    GuildNameTable t;
    ASSERT_EQ(t.parseTitaniumGuildsList(body.data(), body.size()), 1u);
    ASSERT_NE(t.find(0), nullptr);
    EXPECT_EQ(*t.find(0), "GuildZero");
}

// strn0cpy fills exactly the field, so a 64-character name has no NUL and
// the scan must be bounded by the field.
TEST(GuildNameTable, AFullSixtyFourByteNameHasNoTerminatorAndStillReads) {
    std::vector<uint8_t> body(kGuildsListBodyBytes, 0);
    uint8_t* field = body.data() + kGuildsListHeadBytes + 3 * kGuildNameBytes;
    std::memset(field, 'A', kGuildNameBytes);
    GuildNameTable t;
    ASSERT_EQ(t.parseTitaniumGuildsList(body.data(), body.size()), 1u);
    ASSERT_NE(t.find(3), nullptr);
    EXPECT_EQ(t.find(3)->size(), kGuildNameBytes);
    // And it does not bleed into guild 4's field.
    EXPECT_EQ(t.find(4), nullptr);
}

// Titanium::constants::MAX_GUILD_ID is 1500 and the array is 1500 long, so a
// guild whose id is >= 1500 is dropped BY THE SERVER: it can never be in
// this packet and can never be resolved.
TEST(GuildNameTable, IdsAtOrAboveTheTitaniumCeilingAreNotAddressable) {
    GuildNameTable t;
    std::vector<uint8_t> body(kGuildsListBodyBytes, 0);
    t.parseTitaniumGuildsList(body.data(), body.size());
    EXPECT_EQ(t.find(kMaxNumberGuilds), nullptr);
    EXPECT_EQ(t.find(kMaxNumberGuilds + 1), nullptr);
    // An over-long body cannot smuggle one in either.
    std::vector<uint8_t> over(kGuildsListBodyBytes + kGuildNameBytes, 0);
    uint8_t* past = over.data() + kGuildsListHeadBytes + kMaxNumberGuilds * kGuildNameBytes;
    std::memcpy(past, "TooBig", 6);
    ASSERT_EQ(t.parseTitaniumGuildsList(over.data(), over.size()), 0u);
    EXPECT_EQ(t.find(kMaxNumberGuilds), nullptr);
}

TEST(GuildNameTable, TheFixedStructIsNinetySixThousandAndSixtyFourBytes) {
    // 64 + 1500*64. Pinned because the parse offsets are derived from it and
    // a silent change would shift every id by one field.
    EXPECT_EQ(kGuildsListBodyBytes, 96064u);
}

TEST(GuildNameTable, AShortOrEmptyBodyIsParsedAsFarAsItGoes) {
    GuildNameTable t;
    EXPECT_EQ(t.parseTitaniumGuildsList(nullptr, 0), 0u);
    std::vector<uint8_t> headOnly(kGuildsListHeadBytes, 0);
    EXPECT_EQ(t.parseTitaniumGuildsList(headOnly.data(), headOnly.size()), 0u);
    std::vector<uint8_t> two(kGuildsListHeadBytes + 2 * kGuildNameBytes, 0);
    std::memcpy(two.data() + kGuildsListHeadBytes + kGuildNameBytes, "One", 3);
    ASSERT_EQ(t.parseTitaniumGuildsList(two.data(), two.size()), 1u);
    EXPECT_EQ(*t.find(1), "One");
}

// The server sends the WHOLE list, so a second packet replaces the first.
TEST(GuildNameTable, AReparseIsASnapshotNotADelta) {
    GuildNameTable t;
    auto a = BuildGuildsList({{1, "First"}, {2, "Second"}});
    ASSERT_EQ(t.parseTitaniumGuildsList(a.data(), a.size()), 2u);
    auto b = BuildGuildsList({{2, "SecondRenamed"}});
    ASSERT_EQ(t.parseTitaniumGuildsList(b.data(), b.size()), 1u);
    EXPECT_EQ(t.find(1), nullptr);
    ASSERT_NE(t.find(2), nullptr);
    EXPECT_EQ(*t.find(2), "SecondRenamed");
}

// ===========================================================================
// The source census -- these compile and run against the PRE-T443 tree.
// ===========================================================================

TEST(B418Census, WorldProcessGuildsListActuallyParsesThePacket) {
    const std::string src = slurp("src/client/login_world/login_world_component.cpp");
    const size_t at = src.find("void LoginWorldComponent::WorldProcessGuildsList");
    ASSERT_NE(at, std::string::npos);
    const std::string body = codeOnly(src.substr(at, 2500));
    EXPECT_TRUE(has(body, "parseTitaniumGuildsList"))
        << "WorldProcessGuildsList still reads none of OP_GuildsList (B418)";
}

TEST(B418Census, TheLookupIsReachableFromTheComponentThatOwnsIt) {
    const std::string hdr = slurp("include/client/login_world/login_world_component.h");
    EXPECT_TRUE(has(hdr, "GuildNameTable& GuildNames() const"));
    EXPECT_TRUE(has(hdr, "eqt::guild::GuildNameTable m_guild_names"));
}

// The wire shape is quoted, not inferred: the header must carry the two
// citations, because the index-is-the-id fact is invisible in the bytes.
TEST(B418Census, TheWireShapeCitesItsSource) {
    const std::string hdr = slurp("include/client/guild/guild_name_table.h");
    EXPECT_TRUE(has(hdr, "/mnt/projects/akk-stack/code/common/patches/titanium_structs.h"));
    EXPECT_TRUE(has(hdr, "/mnt/projects/akk-stack/code/common/patches/titanium.cpp"));
    EXPECT_TRUE(has(hdr, "THE ARRAY INDEX IS THE GUILD ID"));
}
