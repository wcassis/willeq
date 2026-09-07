// T133 / T260 -- the player-identity fields a zone-in populates (guild id,
// GM mode) and the BSP region CreateZoneLoadSnapshot() resolves for the
// player's start position.
//
// T446 split test_zone_connection_flow.cpp (1,367 lines) by functional
// group; the packet builders and the RecordingBridge fake live in
// zone_connection_flow_test_fixture.h. Same target, same suites, same
// case names.

#include "zone_connection_flow_test_fixture.h"

// ============================================================================
// 4. T133: m_guild_id population from the player's own PlayerProfile.
//
// Before T133, EverQuest::m_guild_id was declared, read by every
// SendGuild*() sender's `if (m_guild_id == 0) return;` guard in
// eq_guild.cpp, and assigned nowhere -- so guildinvite/remove/demote/leader
// and guildmotd's set-path were permanently dead on the wire (T132's
// finding). ZoneProcessPlayerProfile already decoded a `guild_id` local at
// offset 13036 and wrote it into self_entity.guild_id (display-only, per-
// entity data) but never into m_guild_id.
//
// Two things this test suite has to pin down (see the plan's "two things
// that decide whether the minimal fix actually works"):
//   1. The player's own PlayerProfile sets GetGuildID() -- the fix works.
//   2. Another entity's ZoneSpawns/NewSpawn entry, which also carries a
//      guildID field (Spawn_Struct offset 238, decoded into per-entity
//      Entity::guild_id in eq_entities.cpp), must NOT leak into
//      m_guild_id -- that field describes the OTHER entity's guild, not
//      necessarily the player's, and conflating them would be a new bug,
//      not a fix. This is the "second half that would rot" the plan warns
//      about.
// ============================================================================

TEST(GuildIdPopulation, OwnPlayerProfile_PopulatesGuildId) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_EQ(eq.GetGuildID(), 0u) << "guild id must start at 0 (guildless) before any PlayerProfile arrives";

    const uint32_t kGuildId = 17;
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 8, 1);     // race
        p.PutUInt32(2 + 12, 1);    // class
        p.PutUInt8(2 + 20, 10);    // level
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 13036, kGuildId);   // guildID -- the field this unit wires up
        p.PutUInt32(2 + 14384, 5005);       // our own spawn id
    });
    Replay(eq, HC_OP_PlayerProfile, payload);

    EXPECT_EQ(eq.GetGuildID(), kGuildId)
        << "PlayerProfile's guildID (offset 13036) must reach m_guild_id, not just self_entity.guild_id";
}

TEST(GuildIdPopulation, GuildlessPlayerProfile_LeavesGuildIdZero) {
    // A guild id of 0 in the profile (the common case -- most characters
    // are guildless) must not be misread as "unset" vs "explicitly zero";
    // both look identical on the wire and both correctly leave the
    // SendGuild*() guards active.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, 5005);
        // guildID at offset 13036 left at the packet's default zero fill.
    });
    Replay(eq, HC_OP_PlayerProfile, payload);
    EXPECT_EQ(eq.GetGuildID(), 0u);
}

TEST(GuildIdPopulation, OtherEntitySpawn_DoesNotPopulateOwnGuildId) {
    // The second half the plan calls out as "the one that would rot": a
    // ZoneSpawns entry for a DIFFERENT spawn (some other player/NPC, not
    // us) carries its own guildID at Spawn_Struct offset 238. That value
    // belongs to Entity::guild_id for THAT spawn (ZoneProcessZoneSpawns,
    // eq_entities.cpp) --
    // it must never be mistaken for, or copied into, the player's own
    // m_guild_id.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Establish our own spawn id first (via a minimal PlayerProfile, guild
    // id left at 0 -- guildless), the same way a real zone-in sequences it.
    const uint16_t kMySpawnId = 5005;
    auto profilePayload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, kMySpawnId);
    });
    Replay(eq, HC_OP_PlayerProfile, profilePayload);
    ASSERT_EQ(eq.GetGuildID(), 0u);

    // Now a ZoneSpawns entry for a DIFFERENT spawn, guilded.
    const uint16_t kOtherSpawnId = 4242;
    auto spawnPayload = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
        size_t off = 2;
        p.PutCString(off + 7, "a_guilded_rat");
        p.PutUInt32(off + 340, kOtherSpawnId);
        p.PutUInt8(off + 151, 10);
        p.PutUInt8(off + 86, 100);
        p.PutUInt8(off + 83, 1);
        p.PutFloat(off + 75, 5.0f);
        p.PutUInt32(off + 238, 99);  // guildID -- belongs to the OTHER entity
    });
    Replay(eq, HC_OP_ZoneSpawns, spawnPayload);

    ASSERT_EQ(eq.GetEntities().count(kOtherSpawnId), 1u);
    EXPECT_EQ(eq.GetEntities().at(kOtherSpawnId).guild_id, 99u)
        << "the other entity's own guild_id field should still be recorded (display data)";
    EXPECT_EQ(eq.GetGuildID(), 0u)
        << "another entity's guildID must NOT leak into the player's own m_guild_id";
}

// ----------------------------------------------------------------------------
// Mid-session join/leave: the plan's scope-boundary question. Checked against
// the eqemu server source (zone/guild.cpp): every Client::SetGuild() call --
// which is what a guild invite accept, kick, or leave triggers server-side --
// sends OP_SpawnAppearance(type=AppearanceType::GuildID) to the WHOLE ZONE
// (whole_zone=true), including self. So the wire mechanism for mid-session
// updates DOES exist; before T133, ZoneProcessSpawnAppearance's switch had no
// AT_GUILD_ID case, so the packet arrived and was silently dropped (logged
// as "Unhandled SpawnAppearance type"). This is a small addition to an
// EXISTING handler, not a new packet handler, so it's covered in this unit
// per the plan's "small" carve-out.
// ----------------------------------------------------------------------------

std::vector<uint8_t> SpawnAppearancePayload(uint16_t spawnId, uint16_t type, uint32_t parameter) {
    return BuildPayload(2 + 8, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawnId);
        p.PutUInt16(4, type);
        p.PutUInt32(6, parameter);
    });
}

TEST(GuildIdPopulation, MidSessionSpawnAppearance_UpdatesOwnGuildId) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Establish our own spawn id, guildless, as at a normal zone-in.
    const uint16_t kMySpawnId = 5005;
    auto profilePayload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, kMySpawnId);
    });
    Replay(eq, HC_OP_PlayerProfile, profilePayload);
    ASSERT_EQ(eq.GetGuildID(), 0u);

    // Server-driven mid-session join (e.g. accepting a guild invite):
    // OP_SpawnAppearance(spawn_id=us, type=AT_GUILD_ID, parameter=new id).
    Replay(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kMySpawnId, AT_GUILD_ID, 42));
    EXPECT_EQ(eq.GetGuildID(), 42u)
        << "a mid-session AT_GUILD_ID SpawnAppearance for our own spawn must update m_guild_id";

    // Server-driven mid-session leave/kick: parameter is GUILD_NONE
    // (0xFFFFFFFF, common/guilds.h on the server), which must normalize to
    // 0 -- not latch as a bogus nonzero id that would defeat the
    // `m_guild_id == 0` guildless guards.
    Replay(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kMySpawnId, AT_GUILD_ID, 0xFFFFFFFFu));
    EXPECT_EQ(eq.GetGuildID(), 0u)
        << "GUILD_NONE (0xFFFFFFFF) on leave must normalize to 0, not latch as a nonzero guild id";
}

TEST(GuildIdPopulation, MidSessionSpawnAppearance_OtherEntity_DoesNotAffectOwnGuildId) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    const uint16_t kMySpawnId = 5005;
    auto profilePayload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, kMySpawnId);
    });
    Replay(eq, HC_OP_PlayerProfile, profilePayload);

    // AT_GUILD_ID for a DIFFERENT spawn (another player in the zone joining
    // some guild) must not touch our own m_guild_id.
    const uint16_t kOtherSpawnId = 4242;
    Replay(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kOtherSpawnId, AT_GUILD_ID, 7));
    EXPECT_EQ(eq.GetGuildID(), 0u);
}


// ============================================================================
// 5. T148: m_gm_mode population from the player's own PlayerProfile.
//
// PlayerProfile.gm sits at offset 13054 -- 18 bytes past the guild_id at
// 13036 that suite 4 above covers, with pvp@13052 and anon@13053 in between.
// eqemu's own header comments the field `// 0=no, 1=yes (guessing!)`, so it
// was captured off the wire across three logins with `#set gm on|off`
// toggled between them BEFORE any of this was written --
// review/T148/playerprofile_gm_offset_annotated.txt has the annotated bytes.
//
// What the flag means matters as much as where it lives: its only
// server-side writer is Client::SetGM(bool), so it is "GM MODE WAS ON AT
// LOGIN", not "this account has GM privileges". Account status never
// reaches a Titanium client at all. The read is deliberately once-per-login;
// the AT_GM live-update path was explicitly declined (Will, 2026-08-19), and
// GMModeIsLoginTimeOnly_NoMidSessionRefresh below PINS that decision so a
// future reader does not "fix" it by accident.
// ============================================================================

TEST(GMModeGate, OwnPlayerProfile_GMFlagSet_PopulatesGMMode) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_FALSE(eq.Zone().GetGMMode()) << "GM mode must start false before any PlayerProfile arrives";

    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt8(2 + 13054, 1);       // gm -- the field this unit wires up
        p.PutUInt32(2 + 14384, 5005);
    });
    Replay(eq, HC_OP_PlayerProfile, payload);

    EXPECT_TRUE(eq.Zone().GetGMMode())
        << "PlayerProfile.gm (offset 13054) must reach m_gm_mode";
}

TEST(GMModeGate, OwnPlayerProfile_GMFlagClear_LeavesGMModeFalse) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, 5005);
        // gm at offset 13054 left at the packet's default zero fill.
    });
    Replay(eq, HC_OP_PlayerProfile, payload);
    EXPECT_FALSE(eq.Zone().GetGMMode());
}

TEST(GMModeGate, NeighbouringPvpAndAnonBytesDoNotBleedIntoGMMode) {
    // The captured region is pvp@13052, anon@13053, gm@13054, guildrank@13055.
    // An off-by-one in either direction would make one of these look like the
    // GM flag. Set every neighbour and leave gm itself clear.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 13036, 42);     // guild_id
        p.PutUInt8(2 + 13052, 1);       // pvp
        p.PutUInt8(2 + 13053, 2);       // anon (2 = roleplay)
        p.PutUInt8(2 + 13055, 2);       // guildrank
        p.PutUInt32(2 + 14384, 5005);
    });
    Replay(eq, HC_OP_PlayerProfile, payload);

    EXPECT_FALSE(eq.Zone().GetGMMode())
        << "an off-by-one at the parse site would read pvp, anon or guildrank as the GM flag";
    EXPECT_EQ(eq.GetGuildID(), 42u) << "guild_id must still parse from the same region";
}

TEST(GMModeGate, GMModeIsLoginTimeOnly_NoMidSessionRefresh) {
    // PINS Will's decision of 2026-08-19. An AT_GM SpawnAppearance for our
    // OWN spawn -- exactly the packet an AT_GM live-update path would have
    // consumed, and exactly what T133 DID wire up for AT_GUILD_ID -- must
    // NOT change GetGMMode(). A GM who toggles mid-session relogs. If this
    // test ever fails because someone "fixed" the staleness, read
    // refactor/T148_gm_status_gate.md before changing the test.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    const uint16_t kMySpawnId = 5005;
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, kMySpawnId);
        // gm left clear -- logged in with GM mode OFF.
    });
    Replay(eq, HC_OP_PlayerProfile, payload);
    ASSERT_FALSE(eq.Zone().GetGMMode());

    Replay(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kMySpawnId, AT_GM, 1));
    EXPECT_FALSE(eq.Zone().GetGMMode())
        << "GM mode is checked ONCE AT LOGIN by design; AT_GM must not refresh it mid-session";
}

TEST(GMModeGate, SecondPlayerProfileRefreshesGMMode) {
    // The flip side of the staleness decision: a relog (or any subsequent
    // PlayerProfile) DOES pick up the new value. That is the sanctioned way
    // for a GM to refresh, so it has to actually work.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto off = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, 5005);
    });
    Replay(eq, HC_OP_PlayerProfile, off);
    ASSERT_FALSE(eq.Zone().GetGMMode());

    auto on = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt8(2 + 13054, 1);
        p.PutUInt32(2 + 14384, 5005);
    });
    Replay(eq, HC_OP_PlayerProfile, on);
    EXPECT_TRUE(eq.Zone().GetGMMode());
}

// ============================================================================
// T260: the zone-load snapshot's player BSP region.
//
// CreateZoneLoadSnapshot() resolves the player's start position against
// the game-side BSP tree that LoadZoneSources() built from the new zone's
// own WLD. Real zone data (befallen.s3d / arena.s3d) is required; the
// expected region indices below were measured with an out-of-process probe
// that mirrors LoadZoneSources' main-WLD selection and the snapshot's
// findRegionIndexForPoint(m_x, m_y, m_z) call (see
// refactor/T260_bsp_region_size_max.md Review).
// ============================================================================

namespace {

std::string EqClientDataPath() {
    const char* envPath = std::getenv("EQ_CLIENT_PATH");
    return envPath ? std::string(envPath) : std::string(EQ_CLIENT_DATA_PATH);
}

bool FileReadable(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// NewZone with a real short name: ZoneProcessNewZone reads it at packet
// offset 66 and runs the per-zone loaders, including LoadZoneSources().
void SendNamedNewZone(EverQuest& eq, const char* zoneName, uint16_t zoneId) {
    auto payload = BuildPayload(2 + 700, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(66, zoneName);
        p.PutUInt16(686, zoneId);
    });
    Replay(eq, HC_OP_NewZone, payload);
}

// Testchar's documented restore coordinate in befallen (zone 36) -- the
// coordinate P15 recorded a "pre-existing BSP-FATAL" at. Probe: region 2220.
constexpr float kBefallenStartX = -77.9232f;
constexpr float kBefallenStartY = 21.9086f;
constexpr float kBefallenStartZ = 3.0f;
constexpr size_t kBefallenStartRegion = 2220;
// arena (0,0,0): probe region 93. The same point against befallen's tree
// is region 2216 -- distinct, so a wrong-tree lookup is detectable.
constexpr size_t kArenaOriginRegion = 93;
constexpr size_t kBefallenOriginRegion = 2216;

} // namespace

TEST(ZoneLoadSnapshot, PlayerRegionResolvedAfterZoneIn) {
    const std::string dataPath = EqClientDataPath();
    if (!FileReadable(dataPath + "/befallen.s3d")) {
        GTEST_SKIP() << "befallen.s3d not found under " << dataPath;
    }
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dataPath);

    SendNamedNewZone(eq, "befallen", 36);
    ASSERT_EQ(eq.GetCurrentZoneName(), "befallen");
    eq.SetPosition(kBefallenStartX, kBefallenStartY, kBefallenStartZ);

    const auto snap = eq.Bridge().CreateZoneLoadSnapshot();
    EXPECT_EQ(snap.zoneName, "befallen");
    EXPECT_NE(snap.playerBspRegion, SIZE_MAX)
        << "start position must resolve against the zone's own freshly loaded tree";
    EXPECT_EQ(snap.playerBspRegion, kBefallenStartRegion);
}

TEST(ZoneLoadSnapshot, TwoZoneSequenceResolvesAgainstSecondZoneTree) {
    const std::string dataPath = EqClientDataPath();
    if (!FileReadable(dataPath + "/befallen.s3d") || !FileReadable(dataPath + "/arena.s3d")) {
        GTEST_SKIP() << "befallen.s3d / arena.s3d not found under " << dataPath;
    }
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dataPath);

    SendNamedNewZone(eq, "befallen", 36);
    eq.SetPosition(kBefallenStartX, kBefallenStartY, kBefallenStartZ);
    ASSERT_EQ(eq.Bridge().CreateZoneLoadSnapshot().playerBspRegion, kBefallenStartRegion);

    // Re-zone: the next NewZone rebuilds the game-side tree for arena.
    SendNamedNewZone(eq, "arena", 77);
    ASSERT_EQ(eq.GetCurrentZoneName(), "arena");
    eq.SetPosition(0.0f, 0.0f, 0.0f);

    const auto snap = eq.Bridge().CreateZoneLoadSnapshot();
    EXPECT_EQ(snap.zoneName, "arena");
    EXPECT_NE(snap.playerBspRegion, SIZE_MAX);
    EXPECT_EQ(snap.playerBspRegion, kArenaOriginRegion)
        << "region must come from arena's tree (befallen's tree would give "
        << kBefallenOriginRegion << ")";
}

TEST(ZoneLoadSnapshot, LateFinalizationTreeForPreviousZoneIsRejected) {
    // Application's graphicsCompleteReady finalization hands the renderer's
    // tree for the JUST-COMPLETED load back to EverQuest. If a fast re-zone
    // has already received the next zone's NewZone by then, that tree is the
    // previous zone's and must not displace the tree LoadZoneSources built
    // for the current zone (P15's "SIZE_MAX with zone=<source>" FATAL was
    // exactly this pairing: qeynos2's position against befallen's tree).
    const std::string dataPath = EqClientDataPath();
    if (!FileReadable(dataPath + "/befallen.s3d") || !FileReadable(dataPath + "/arena.s3d")) {
        GTEST_SKIP() << "befallen.s3d / arena.s3d not found under " << dataPath;
    }
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dataPath);

    SendNamedNewZone(eq, "arena", 77);
    eq.SetPosition(0.0f, 0.0f, 0.0f);
    ASSERT_EQ(eq.Bridge().CreateZoneLoadSnapshot().playerBspRegion, kArenaOriginRegion);

    // Build befallen's tree the way the renderer would have for the previous
    // load, and deliver it as the (late) finalization for that zone.
    EQT::Graphics::WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(dataPath + "/befallen.s3d", "befallen.wld"));
    auto befallenTree = loader.getBspTree();
    ASSERT_TRUE(befallenTree);
    ASSERT_EQ(befallenTree->findRegionIndexForPoint(0.0f, 0.0f, 0.0f), kBefallenOriginRegion);

    eq.Bridge().SetZoneBspTree(befallenTree, "befallen");

    const auto snap = eq.Bridge().CreateZoneLoadSnapshot();
    EXPECT_EQ(snap.zoneName, "arena");
    EXPECT_NE(snap.playerBspRegion, kBefallenOriginRegion)
        << "snapshot resolved the arena position against befallen's tree";
    EXPECT_EQ(snap.playerBspRegion, kArenaOriginRegion)
        << "the current zone's own tree must survive a stale finalization";
}

TEST(ZoneLoadSnapshot, MatchingFinalizationTreeIsInstalled) {
    const std::string dataPath = EqClientDataPath();
    if (!FileReadable(dataPath + "/arena.s3d")) {
        GTEST_SKIP() << "arena.s3d not found under " << dataPath;
    }
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dataPath);
    SendNamedNewZone(eq, "arena", 77);
    eq.SetPosition(0.0f, 0.0f, 0.0f);

    EQT::Graphics::WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(dataPath + "/arena.s3d", "arena.wld"));
    auto arenaTree = loader.getBspTree();
    ASSERT_TRUE(arenaTree);
    eq.Bridge().SetZoneBspTree(arenaTree, "arena");

    EXPECT_EQ(eq.Bridge().CreateZoneLoadSnapshot().playerBspRegion, kArenaOriginRegion);
}

TEST(ZoneLoadSnapshot, OutOfTreeStartPositionIsNotFatal) {
    // Legitimate start positions can fall outside every BSP region (P44's
    // befallen zone-in at x=-195,y=-982 probes SIZE_MAX against befallen's
    // own tree). Nothing consumes playerBspRegion, so SIZE_MAX is a
    // diagnostic, never an exit.
    const std::string dataPath = EqClientDataPath();
    if (!FileReadable(dataPath + "/befallen.s3d")) {
        GTEST_SKIP() << "befallen.s3d not found under " << dataPath;
    }
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dataPath);
    SendNamedNewZone(eq, "befallen", 36);
    eq.SetPosition(-195.0f, -982.0f, 3.0f);

    const auto snap = eq.Bridge().CreateZoneLoadSnapshot();
    EXPECT_EQ(snap.zoneName, "befallen");
    EXPECT_EQ(snap.playerBspRegion, SIZE_MAX);
    EXPECT_FALSE(eq.IsQuitRequested());
}
