// T169 -- OP_RaidUpdate decoding.
//
// The old ZoneProcessRaidUpdate read one uint32 and switched on six guessed
// values, four of which disagreed with eqemu's own enum (common/raid.h), and
// never parsed the body at all. These tests pin the action codes and the
// per-action BODY SHAPES against that authority, because the shapes are the
// part that cannot be checked by reading our own code:
//
//   * action 8 (raidCreate) is a different struct entirely, arriving on the
//     same wire opcode because Titanium maps OP_RaidJoin and OP_RaidUpdate to
//     the same value (0x1f21).
//   * action 10 is TWO packets wearing one number (T180): 136 bytes is the
//     zone-in name handshake, 140 bytes a real raidNoRaid sent RAW in eqemu's
//     INTERNAL layout, whose names sit 4 bytes later than every other
//     action's.
//   * actions 14/30 put leadership AA data where every other action has
//     `parameter`.
//
// A decoder that reads `parameter` at a fixed offset for every action -- the
// obvious implementation, and the one a reviewer would wave through -- is
// wrong for four of them. That is what these tests exist to catch.

#include <gtest/gtest.h>

#include "client/raid_update.h"
#include "common/net/packet.h"

#include <string>

namespace {

constexpr size_t kBase = 2;  // the opcode precedes the struct, as on the wire

constexpr size_t kGenSize = 136;
constexpr size_t kAddSize = 140;
constexpr size_t kCreateSize = 72;
constexpr size_t kEmuGenSize = 140;

void putName(EQ::Net::DynamicPacket& p, size_t offset, const std::string& name) {
    for (size_t i = 0; i < 64; ++i) {
        p.PutUInt8(offset + i, i < name.size() ? static_cast<uint8_t>(name[i]) : 0);
    }
}

// Titanium RaidGeneral_Struct: action@0, player_name@4, leader_name@68,
// parameter@132.
EQ::Net::DynamicPacket makeGeneral(uint32_t action, const std::string& player,
                                   const std::string& leader, uint32_t parameter,
                                   size_t size = kGenSize) {
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + size);
    p.PutUInt32(kBase + 0, action);
    putName(p, kBase + 4, player);
    putName(p, kBase + 68, leader);
    p.PutUInt32(kBase + 132, parameter);
    return p;
}

}  // namespace

// ---------------------------------------------------------------------
// The action codes themselves. These four were WRONG before T169.
// ---------------------------------------------------------------------

TEST(RaidActionCodes, MatchEqemuRaidHeaderExactly) {
    // eqemu common/raid.h. The old switch claimed 2=member joined,
    // 3=leader changed, 6=disbanded, 10=lock state.
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::Add), 0u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::Remove2), 1u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::MemberNameChange), 2u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::Remove1), 3u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::Disband), 5u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::Members), 6u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::Create), 8u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::NoRaid), 10u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::Lock), 17u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::Unlock), 18u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::SetLeader), 20u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::MakeLeader), 30u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::SetMotd), 35u);
    EXPECT_EQ(static_cast<uint32_t>(EQT::RaidAction::SetNote), 36u);
}

TEST(RaidActionCodes, NamesAreEqemusOwnNames) {
    EXPECT_EQ(EQT::RaidActionName(6), "raidMembers");
    EXPECT_EQ(EQT::RaidActionName(10), "raidNoRaid");
    EXPECT_EQ(EQT::RaidActionName(20), "raidSetLeader");
    EXPECT_EQ(EQT::RaidActionName(99), "Unknown(99)");
}

// ---------------------------------------------------------------------
// The general body -- the shape most actions arrive in.
// ---------------------------------------------------------------------

TEST(RaidUpdateDecode, GeneralBodyReadsBothNamesAndParameter) {
    auto p = makeGeneral(1, "Testmelee", "Raidleader", 7);
    auto u = EQT::DecodeRaidUpdate(p, kBase);

    ASSERT_TRUE(u.valid);
    EXPECT_TRUE(u.known_action);
    EXPECT_EQ(u.action, EQT::RaidAction::Remove2);
    EXPECT_EQ(u.player_name, "Testmelee");
    EXPECT_EQ(u.leader_name, "Raidleader");
    EXPECT_TRUE(u.has_parameter);
    EXPECT_EQ(u.parameter, 7u);
}

// leader_name is at 68, not 72. eqemu's INTERNAL struct has an extra
// unknown1@68 and puts leader_name at 72; the Titanium one does not. Using
// the internal layout would read the last 60 bytes of player_name plus 4
// bytes of leader_name -- which for short names looks like an EMPTY string,
// i.e. a silent failure, not a visible one.
TEST(RaidUpdateDecode, LeaderNameIsAtTheTitaniumOffsetNotTheInternalOne) {
    auto p = makeGeneral(0, "Aaa", "Zzz", 0);
    auto u = EQT::DecodeRaidUpdate(p, kBase);

    ASSERT_TRUE(u.valid);
    EXPECT_EQ(u.leader_name, "Zzz");
    EXPECT_NE(u.leader_name, "");
}

TEST(RaidUpdateDecode, ANameFillingTheWholeFieldDoesNotRunIntoTheNextOne) {
    const std::string full(64, 'A');   // no NUL inside the field
    auto p = makeGeneral(0, full, "Leader", 0);
    auto u = EQT::DecodeRaidUpdate(p, kBase);

    ASSERT_TRUE(u.valid);
    EXPECT_EQ(u.player_name.size(), 64u);
    EXPECT_EQ(u.leader_name, "Leader");
}

// THE HEADLINE. Action 20 is what a raid invite produces (eqemu builds it in
// Handle_OP_RaidCommand's RaidCommandInvite arm, client_packet.cpp:12356:
// leader_name = the inviter, player_name = the invited). It used to fall
// through to "Unknown raid action: 20", so an invited player was told nothing.
TEST(RaidUpdateDecode, AnInviteNamesTheInviter) {
    auto p = makeGeneral(20, "Testmelee", "Raidleader", 0);
    auto u = EQT::DecodeRaidUpdate(p, kBase);

    ASSERT_TRUE(u.valid);
    EXPECT_TRUE(u.known_action);
    EXPECT_EQ(u.action, EQT::RaidAction::SetLeader);
    EXPECT_EQ(u.leader_name, "Raidleader") << "who is inviting";
    EXPECT_EQ(u.player_name, "Testmelee") << "who is being invited -- us";
}

// ---------------------------------------------------------------------
// Action 0 -- RaidAddMember_Struct's tail.
// ---------------------------------------------------------------------

TEST(RaidUpdateDecode, AddCarriesClassLevelAndGroupLeaderFlag) {
    auto p = makeGeneral(0, "Newbie", "Raidleader", 2, kAddSize);
    p.PutUInt8(kBase + 136, 9);    // _class (Rogue)
    p.PutUInt8(kBase + 137, 42);   // level
    p.PutUInt8(kBase + 138, 1);    // isGroupLeader

    auto u = EQT::DecodeRaidUpdate(p, kBase);
    ASSERT_TRUE(u.valid);
    EXPECT_EQ(u.action, EQT::RaidAction::Add);
    EXPECT_TRUE(u.has_member_info);
    EXPECT_EQ(u.member_class, 9);
    EXPECT_EQ(u.member_level, 42);
    EXPECT_TRUE(u.is_group_leader);
    EXPECT_EQ(u.parameter, 2u) << "parameter is the group number on an add";
}

TEST(RaidUpdateDecode, AGeneralSizedAddHasNoMemberInfoRatherThanGarbage) {
    auto p = makeGeneral(0, "Newbie", "Raidleader", 2, kGenSize);
    auto u = EQT::DecodeRaidUpdate(p, kBase);

    ASSERT_TRUE(u.valid);
    EXPECT_FALSE(u.has_member_info);
    EXPECT_EQ(u.member_level, 0);
}

// ---------------------------------------------------------------------
// The three bodies that are NOT the general shape.
// ---------------------------------------------------------------------

// Action 8 arrives on the same wire opcode with RaidCreate_Struct:
// action@0, leader_name@4, leader_id@68. Decoding it as a general body would
// report the leader's name as the *player* name and read 4 bytes of the name
// as a parameter.
TEST(RaidUpdateDecode, CreateUsesItsOwnStructNotTheGeneralOne) {
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + kCreateSize);
    p.PutUInt32(kBase + 0, 8);
    putName(p, kBase + 4, "Raidleader");
    p.PutUInt32(kBase + 68, 1234);

    auto u = EQT::DecodeRaidUpdate(p, kBase);
    ASSERT_TRUE(u.valid);
    EXPECT_EQ(u.action, EQT::RaidAction::Create);
    EXPECT_EQ(u.leader_name, "Raidleader");
    EXPECT_TRUE(u.has_leader_id);
    EXPECT_EQ(u.leader_id, 1234u);
    EXPECT_TRUE(u.player_name.empty()) << "RaidCreate_Struct has no player_name";
    EXPECT_FALSE(u.has_parameter);
}

// Action 10 is passed through UNENCODED (titanium.cpp's
// `case raidNoRaid: dest->QueuePacket(inapp)`), so it is eqemu's 140-byte
// internal layout. Only `action` is at a shared offset -- the decoder must
// not report names it cannot correctly locate.
TEST(RaidUpdateDecode, NoRaidIsRecognisedAndReportsNoMisalignedNames) {
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + kEmuGenSize);
    p.PutUInt32(kBase + 0, 10);
    putName(p, kBase + 4, "Testmelee");
    putName(p, kBase + 72, "Raidleader");  // internal layout: leader at 72

    auto u = EQT::DecodeRaidUpdate(p, kBase);
    ASSERT_TRUE(u.valid);
    EXPECT_EQ(u.action, EQT::RaidAction::NoRaid);
    EXPECT_TRUE(u.player_name.empty());
    EXPECT_TRUE(u.leader_name.empty());
    EXPECT_FALSE(u.has_parameter);
}

// ---------------------------------------------------------------------
// T180: the OTHER action 10. Client::CompleteConnect (zone/client.cpp:859)
// sends OP_RaidUpdate carrying a ZoneInSendName_Struct -- {unknown0 = 0x0A,
// name[64], name2[64], unknown132}, 136 bytes -- on EVERY zone entry, with or
// without a raid in existence. We read `unknown0` as the action and see 10.
//
// Before T180 the decoder demanded 140 bytes for action 10, so this rejected
// as "too short" and logged a WARN per zone-in (found live, T172). Accepting
// it as a raidNoRaid instead would be strictly worse: it would clear real raid
// membership every time the player zoned.
// ---------------------------------------------------------------------

// A ZoneInSendName_Struct as CompleteConnect builds it: both names are ours.
EQ::Net::DynamicPacket makeZoneInName(const std::string& me) {
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + kGenSize);
    p.PutUInt32(kBase + 0, 0x0A);
    putName(p, kBase + 4, me);
    putName(p, kBase + 68, me);
    p.PutUInt32(kBase + 132, 0);
    return p;
}

TEST(RaidUpdateDecode, Action10At136BytesIsTheZoneInHandshakeNotARaidEvent) {
    auto p = makeZoneInName("Testmelee");
    auto u = EQT::DecodeRaidUpdate(p, kBase);

    ASSERT_TRUE(u.valid) << "a complete 138-byte packet must not read as truncated";
    EXPECT_TRUE(u.zone_in_name);
    EXPECT_EQ(u.raw_action, 10u);
    // Its name fields DO sit at the Titanium general offsets, so they decode --
    // and both being our own name is what makes the log self-evident.
    EXPECT_EQ(u.player_name, "Testmelee");
    EXPECT_EQ(u.leader_name, "Testmelee");
}

TEST(RaidUpdateDecode, Action10At140BytesIsStillARealNoRaid) {
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + kEmuGenSize);
    p.PutUInt32(kBase + 0, 10);

    auto u = EQT::DecodeRaidUpdate(p, kBase);
    ASSERT_TRUE(u.valid);
    EXPECT_FALSE(u.zone_in_name) << "the 140-byte layout is the genuine raidNoRaid";
}

TEST(RaidUpdateDecode, ShortAction10IsStillRejected) {
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + 100);
    p.PutUInt32(kBase + 0, 10);

    auto u = EQT::DecodeRaidUpdate(p, kBase);
    EXPECT_FALSE(u.valid);
    EXPECT_FALSE(u.zone_in_name);
}

TEST(RaidUpdateDecode, OnlyAction10EverSetsZoneInName) {
    for (uint32_t action : {0u, 1u, 3u, 5u, 8u, 14u, 20u, 30u, 35u}) {
        auto p = makeGeneral(action, "Testmelee", "Raidleader", 0);
        auto u = EQT::DecodeRaidUpdate(p, kBase);
        EXPECT_FALSE(u.zone_in_name) << "action " << action;
    }
}

// Actions 14 and 30 are RaidLeadershipUpdate_Struct: offset 132 is the start
// of the leadership AA block, not `parameter`. Reporting it as a parameter
// would surface AA data as a group number.
TEST(RaidUpdateDecode, LeadershipActionsDoNotReportAParameter) {
    for (uint32_t action : {14u, 30u}) {
        auto p = makeGeneral(action, "Testmelee", "Raidleader", 0xDEADBEEF, 400);
        auto u = EQT::DecodeRaidUpdate(p, kBase);

        ASSERT_TRUE(u.valid) << "action " << action;
        EXPECT_EQ(u.player_name, "Testmelee");
        EXPECT_EQ(u.leader_name, "Raidleader");
        EXPECT_FALSE(u.has_parameter) << "action " << action
            << ": offset 132 is leadership AA data, not a parameter";
        EXPECT_EQ(u.parameter, 0u);
    }
}

TEST(RaidUpdateDecode, MotdAndNoteCarryTrailingText) {
    auto p = makeGeneral(35, "Testmelee", "Raidleader", 0, kGenSize + 32);
    const std::string motd = "Pull on my mark";
    for (size_t i = 0; i < motd.size(); ++i) {
        p.PutUInt8(kBase + 136 + i, static_cast<uint8_t>(motd[i]));
    }

    auto u = EQT::DecodeRaidUpdate(p, kBase);
    ASSERT_TRUE(u.valid);
    EXPECT_EQ(u.action, EQT::RaidAction::SetMotd);
    EXPECT_EQ(u.text, motd);
}

// ---------------------------------------------------------------------
// Short and unknown packets: named, not guessed, and never a crash.
// ---------------------------------------------------------------------

TEST(RaidUpdateDecode, ShortBodyIsInvalidRatherThanPartlyDecoded) {
    // Built by hand rather than through makeGeneral(): DynamicPacket's Put*
    // accessors GROW the buffer, so writing `parameter` at 132 would silently
    // undo the truncation this test is about.
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + 100);  // < 136
    p.PutUInt32(kBase + 0, 1);
    putName(p, kBase + 4, "Testmelee");

    auto u = EQT::DecodeRaidUpdate(p, kBase);

    EXPECT_FALSE(u.valid);
    EXPECT_EQ(u.raw_action, 1u) << "the action still decodes; only the body is short";
}

TEST(RaidUpdateDecode, TruncatedToTheActionAloneDoesNotThrow) {
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + 2);
    EXPECT_NO_THROW({
        auto u = EQT::DecodeRaidUpdate(p, kBase);
        EXPECT_FALSE(u.valid);
    });
}

TEST(RaidUpdateDecode, AnUnlistedActionIsMarkedUnknownButStillDecodesNames) {
    auto p = makeGeneral(99, "Testmelee", "Raidleader", 5);
    auto u = EQT::DecodeRaidUpdate(p, kBase);

    ASSERT_TRUE(u.valid);
    EXPECT_FALSE(u.known_action);
    EXPECT_EQ(u.raw_action, 99u);
    EXPECT_EQ(u.player_name, "Testmelee")
        << "an unknown action still arrives in the general shape; reporting "
           "the names is what makes the next one diagnosable";
}

// ===================================================================
// T179 -- encodeRaidCommand: the outbound OP_RaidCommand body.
// 140-byte internal RaidGeneral_Struct: action@0, player_name@4[64],
// unknown1@68, leader_name@72[64], parameter@136. The handler length-checks
// against this size, so the layout is load-bearing.
// ===================================================================

namespace {
uint32_t readU32(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}
std::string readName(const std::vector<uint8_t>& b, size_t off) {
    std::string s;
    for (size_t i = 0; i < 64 && b[off + i] != 0; ++i) s.push_back(static_cast<char>(b[off + i]));
    return s;
}
}  // namespace

TEST(EncodeRaidCommand, SizeIsExactly136) {
    // Titanium wire layout; eqemu's DECODE(OP_RaidInvite) expands it to the
    // 140-byte internal struct. See raid_update.h.
    EXPECT_EQ(EQT::encodeRaidCommand(3, "A", "B").size(), 136u);
}

TEST(EncodeRaidCommand, FieldsLandAtTitaniumOffsets) {
    // AcceptInvite: player_name = inviter, leader_name = us. Titanium offsets.
    auto b = EQT::encodeRaidCommand(1, "Testbuddy", "Testmelee", 0);
    EXPECT_EQ(readU32(b, 0), 1u);            // action@0
    EXPECT_EQ(readName(b, 4), "Testbuddy");  // player_name@4 (inviter)
    EXPECT_EQ(readName(b, 68), "Testmelee"); // leader_name@68 (us)
    EXPECT_EQ(readU32(b, 132), 0u);          // parameter@132
}

TEST(EncodeRaidCommand, ParameterIsWrittenAt132) {
    auto b = EQT::encodeRaidCommand(20, "X", "Y", 0xDEADBEEF);
    EXPECT_EQ(readU32(b, 132), 0xDEADBEEFu);
}

TEST(EncodeRaidCommand, NamesAreNulTerminatedAndTruncatedToField) {
    // A 70-char name must not overrun its 64-byte field; byte 63 stays NUL.
    std::string longName(70, 'Z');
    auto b = EQT::encodeRaidCommand(3, longName, "us");
    EXPECT_EQ(b[4 + 63], 0) << "player_name field must keep its terminator";
    EXPECT_EQ(readName(b, 4).size(), 63u);
    // leader_name field (offset 68) is untouched by the overrun.
    EXPECT_EQ(readName(b, 68), "us");
}

TEST(EncodeRaidCommand, EmptyNamesLeaveZeroedFields) {
    auto b = EQT::encodeRaidCommand(5, "", "");
    EXPECT_EQ(readU32(b, 0), 5u);
    EXPECT_EQ(b[4], 0);
    EXPECT_EQ(b[68], 0);
}

// =====================================================================
// RaidRoster (T180)
//
// Titanium NEVER delivers a roster packet: eqemu's action 6 (raidMembers), the
// one carrying the member list, has no `case` in common/patches/titanium.cpp's
// ENCODE(OP_RaidUpdate) and falls through the `default` arm as a bare 136-byte
// general with the list stripped. So the roster is ASSEMBLED from the action-0
// stream that Raid::SendBulkRaid / SendRaidAddAll emit one-per-member.
//
// These tests pin that assembly against what the server actually sends:
//   * SendRaidAdd puts the member in BOTH names and the group in `parameter`
//   * a group change is Remove+Add (SendRaidMove), never an in-place edit
//   * SendBulkRaid REPLAYS every member on each zone-in -- duplicates here
//     would grow the roster without bound across zoning
//   * RAID_GROUPLESS (0xFFFFFFFF) means in the raid, in no group
// =====================================================================

namespace {

constexpr const char* kMe = "Testmelee";

// Titanium RaidAddMember_Struct as Raid::SendRaidAdd builds it (raids.cpp:1142).
EQT::RaidUpdate decodedAdd(const std::string& who, uint32_t group,
                           uint8_t class_id, uint8_t level, bool group_leader) {
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + kAddSize);
    p.PutUInt32(kBase + 0, 0);
    putName(p, kBase + 4, who);   // player_name
    putName(p, kBase + 68, who);  // leader_name -- eqemu sets both to the member
    p.PutUInt32(kBase + 132, group);
    p.PutUInt8(kBase + 136, class_id);
    p.PutUInt8(kBase + 137, level);
    p.PutUInt8(kBase + 138, group_leader ? 1 : 0);
    return EQT::DecodeRaidUpdate(p, kBase);
}

EQT::RaidUpdate decodedGeneral(uint32_t action, const std::string& who) {
    auto p = makeGeneral(action, who, who, 0);
    return EQT::DecodeRaidUpdate(p, kBase);
}

// A roster holding three members in two groups, as a zone-in would build it.
EQT::RaidRoster threeMemberRaid() {
    EQT::RaidRoster r;
    r.Apply(decodedAdd("Raidleader", 0, 1, 60, true), kMe);
    r.Apply(decodedAdd(kMe, 0, 7, 52, false), kMe);
    r.Apply(decodedAdd("Oraclerogue", 1, 9, 45, true), kMe);
    return r;
}

}  // namespace

TEST(RaidRoster, AddCarriesGroupClassLevelAndGroupLeader) {
    EQT::RaidRoster r;
    r.Apply(decodedAdd("Oraclerogue", 1, 9, 45, true), kMe);

    ASSERT_EQ(r.Size(), 1u);
    const auto* m = r.Find("Oraclerogue");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->name, "Oraclerogue");
    EXPECT_EQ(m->group, 1u);
    EXPECT_EQ(m->class_id, 9);
    EXPECT_EQ(m->level, 45);
    EXPECT_TRUE(m->is_group_leader);
}

TEST(RaidRoster, GrouplessMemberKeepsEqemuSentinel) {
    EQT::RaidRoster r;
    r.Apply(decodedAdd("Loner", EQT::kRaidGroupless, 1, 10, false), kMe);

    const auto* m = r.Find("Loner");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->group, EQT::kRaidGroupless)
        << "RAID_GROUPLESS (zone/raids.h:72) means in the raid, in no group";
}

// SendBulkRaid replays the WHOLE roster to a client on every zone-in.
//
// HONESTY NOTE (T180 live probe): this sequence -- repeated Adds with NO
// intervening Create -- is one the server does not currently produce. eqemu
// sends SendRaidCreate immediately before SendBulkRaid on every zone entry
// (zone/client_packet.cpp:598), and Apply clears on Create, so the replay
// lands in an empty roster and an appending implementation would pass the
// live case too. The upsert is defence in depth: it keeps the roster right if
// an Add ever arrives without a preceding Create. Kept deliberately, and
// labelled rather than dressed up as a live regression.
TEST(RaidRoster, ReplayedAddsUpsertRatherThanDuplicate) {
    auto r = threeMemberRaid();
    ASSERT_EQ(r.Size(), 3u);

    r.Apply(decodedAdd("Raidleader", 0, 1, 60, true), kMe);
    r.Apply(decodedAdd(kMe, 0, 7, 52, false), kMe);
    r.Apply(decodedAdd("Oraclerogue", 1, 9, 45, true), kMe);

    EXPECT_EQ(r.Size(), 3u) << "a re-zone must not grow the roster";
}

// Raid::SendRaidMove is Remove-then-Add, so a group change arrives as two
// packets and the second must overwrite the first member's row.
TEST(RaidRoster, ReAddAfterAGroupChangeOverwritesTheGroup) {
    auto r = threeMemberRaid();
    r.Apply(decodedGeneral(1, "Oraclerogue"), kMe);   // raidRemove2
    r.Apply(decodedAdd("Oraclerogue", 0, 9, 45, false), kMe);

    ASSERT_EQ(r.Size(), 3u);
    const auto* m = r.Find("Oraclerogue");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->group, 0u);
    EXPECT_FALSE(m->is_group_leader);
}

TEST(RaidRoster, RemoveOfAnotherMemberDropsOnlyThatRow) {
    auto r = threeMemberRaid();
    r.Apply(decodedGeneral(1, "Oraclerogue"), kMe);

    EXPECT_EQ(r.Size(), 2u);
    EXPECT_EQ(r.Find("Oraclerogue"), nullptr);
    EXPECT_NE(r.Find(kMe), nullptr);
}

TEST(RaidRoster, BothRemoveCodesAreHandled) {
    // eqemu declares raidRemove2 = 1 and raidRemove1 = 3 and uses both.
    for (uint32_t action : {1u, 3u}) {
        auto r = threeMemberRaid();
        r.Apply(decodedGeneral(action, "Oraclerogue"), kMe);
        EXPECT_EQ(r.Size(), 2u) << "action " << action;
    }
}

TEST(RaidRoster, RemoveOfOurselfEmptiesTheWholeRoster) {
    auto r = threeMemberRaid();
    r.Apply(decodedGeneral(1, kMe), kMe);

    EXPECT_TRUE(r.Empty())
        << "we left the raid: the other members are no longer ours to list";
}

TEST(RaidRoster, DisbandEmptiesTheRoster) {
    auto r = threeMemberRaid();
    r.Apply(decodedGeneral(5, "Raidleader"), kMe);
    EXPECT_TRUE(r.Empty());
}

TEST(RaidRoster, RealNoRaidEmptiesTheRoster) {
    auto r = threeMemberRaid();
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + kEmuGenSize);
    p.PutUInt32(kBase + 0, 10);
    r.Apply(EQT::DecodeRaidUpdate(p, kBase), kMe);
    EXPECT_TRUE(r.Empty());
}

// The regression this whole action-10 split exists to prevent: the zone-in
// handshake also presents 10, and it arrives on EVERY zone entry. Clearing on
// it would empty the roster of anyone who zoned while raided.
TEST(RaidRoster, ZoneInHandshakeLeavesTheRosterAlone) {
    auto r = threeMemberRaid();
    auto p = makeZoneInName(kMe);
    auto u = EQT::DecodeRaidUpdate(p, kBase);
    ASSERT_TRUE(u.zone_in_name);

    r.Apply(u, kMe);
    EXPECT_EQ(r.Size(), 3u);
}

// eqemu sends raidCreate to a joining client immediately BEFORE SendBulkRaid
// replays the members, so it marks the start of a fresh roster stream.
TEST(RaidRoster, CreateResetsTheRosterForTheStreamThatFollows) {
    auto r = threeMemberRaid();
    EQ::Net::DynamicPacket p;
    p.Resize(kBase + kCreateSize);
    p.PutUInt32(kBase + 0, 8);
    putName(p, kBase + 4, "Newleader");
    p.PutUInt32(kBase + 68, 1234);

    r.Apply(EQT::DecodeRaidUpdate(p, kBase), kMe);
    EXPECT_TRUE(r.Empty());
}

// Leadership, loot-rule, motd, lock and invite actions say nothing about who
// is in the raid. An over-eager switch here would drop members on a motd.
TEST(RaidRoster, NonMembershipActionsLeaveTheRosterUntouched) {
    for (uint32_t action : {4u, 11u, 13u, 14u, 16u, 17u, 18u, 20u, 30u, 35u, 36u}) {
        auto r = threeMemberRaid();
        r.Apply(decodedGeneral(action, "Raidleader"), kMe);
        EXPECT_EQ(r.Size(), 3u) << "action " << action;
    }
}

TEST(RaidRoster, InvalidUpdatesAreIgnored) {
    auto r = threeMemberRaid();
    EQ::Net::DynamicPacket truncated;
    truncated.Resize(kBase + 8);
    truncated.PutUInt32(kBase + 0, 5);  // a disband too short to be one

    auto u = EQT::DecodeRaidUpdate(truncated, kBase);
    ASSERT_FALSE(u.valid);
    r.Apply(u, kMe);
    EXPECT_EQ(r.Size(), 3u) << "a packet we could not decode must change nothing";
}
