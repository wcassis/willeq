// T167 -- the player's own appearance: parsed from OP_PlayerProfile, stored,
// and merged faithfully by /appearance.
//
// Before this unit ZoneProcessPlayerProfile parsed all seven appearance
// fields into locals and read none of them (the implemented-but-unreached
// pattern, first found here on a RECEIVE path). The user-visible consequence
// was that /appearance had no source for eyecolor1/eyecolor2 and reset both
// eyes to 0 on every use, because eqemu's Handle_OP_FaceChange
// (zone/client_packet.cpp:6387) copies all seven fields into the profile and
// Save()s unconditionally -- a field you do not re-send is a field you have
// set to zero.
//
// Two things are pinned here:
//   1. the OFFSETS, which are what can silently rot (verified against eqemu's
//      common/patches/titanium_structs.h, struct at :814);
//   2. the MERGE, which is the behaviour the bug lived in.
//
// Same headless EverQuest + EqActionHandler idiom as T88/T90/T93 -- no
// network, no graphics, no eqemu.

#include <gtest/gtest.h>

#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/player/profile_appearance.h"
#include "common/net/packet.h"

using namespace eqt;

namespace {

// Distinct, non-zero, non-consecutive values: any offset that is off by one
// -- the whole failure mode this test exists for -- lands on a value that
// belongs to a different field, and a swapped pair is visible too. Zero is
// deliberately never used as an expected value.
constexpr uint8_t kHairColor  = 11;
constexpr uint8_t kBeardColor = 22;
constexpr uint8_t kEyeColor1  = 33;
constexpr uint8_t kEyeColor2  = 44;
constexpr uint8_t kHairStyle  = 55;
constexpr uint8_t kBeard      = 66;
constexpr uint8_t kFace       = 77;

// A PlayerProfile packet body with only the appearance fields populated.
// `base` mirrors the live path: 2, because the opcode precedes the struct.
EQ::Net::DynamicPacket makeProfilePacket(size_t base) {
    EQ::Net::DynamicPacket p;
    p.Resize(base + 19588);  // full PlayerProfile_Struct, zero-filled
    p.PutUInt8(base + EQT::PP_OFFSET_HAIRCOLOR, kHairColor);
    p.PutUInt8(base + EQT::PP_OFFSET_BEARDCOLOR, kBeardColor);
    p.PutUInt8(base + EQT::PP_OFFSET_EYECOLOR1, kEyeColor1);
    p.PutUInt8(base + EQT::PP_OFFSET_EYECOLOR2, kEyeColor2);
    p.PutUInt8(base + EQT::PP_OFFSET_HAIRSTYLE, kHairStyle);
    p.PutUInt8(base + EQT::PP_OFFSET_BEARD, kBeard);
    p.PutUInt8(base + EQT::PP_OFFSET_FACE, kFace);
    return p;
}

EQT::ProfileAppearance knownAppearance() {
    EQT::ProfileAppearance a;
    a.haircolor = kHairColor;
    a.beardcolor = kBeardColor;
    a.eyecolor1 = kEyeColor1;
    a.eyecolor2 = kEyeColor2;
    a.hairstyle = kHairStyle;
    a.beard = kBeard;
    a.face = kFace;
    a.valid = true;
    return a;
}

// ---------------------------------------------------------------------
// 1. The offsets.
// ---------------------------------------------------------------------

TEST(ProfileAppearanceParse, EverySevenFieldsReadsItsOwnOffset) {
    auto p = makeProfilePacket(2);
    auto a = EQT::ParseProfileAppearance(p, 2);

    EXPECT_EQ(a.haircolor, kHairColor);
    EXPECT_EQ(a.beardcolor, kBeardColor);
    EXPECT_EQ(a.eyecolor1, kEyeColor1);
    EXPECT_EQ(a.eyecolor2, kEyeColor2);
    EXPECT_EQ(a.hairstyle, kHairStyle);
    EXPECT_EQ(a.beard, kBeard);
    EXPECT_EQ(a.face, kFace);
    EXPECT_TRUE(a.valid);
}

// haircolor..beard are six CONSECUTIVE bytes, so an off-by-one in that run
// is the likeliest rot and the hardest to see by eye. Pin the run itself.
TEST(ProfileAppearanceParse, TheSixConsecutiveFieldsAreNotShifted) {
    EXPECT_EQ(EQT::PP_OFFSET_BEARDCOLOR, EQT::PP_OFFSET_HAIRCOLOR + 1);
    EXPECT_EQ(EQT::PP_OFFSET_EYECOLOR1, EQT::PP_OFFSET_HAIRCOLOR + 2);
    EXPECT_EQ(EQT::PP_OFFSET_EYECOLOR2, EQT::PP_OFFSET_HAIRCOLOR + 3);
    EXPECT_EQ(EQT::PP_OFFSET_HAIRSTYLE, EQT::PP_OFFSET_HAIRCOLOR + 4);
    EXPECT_EQ(EQT::PP_OFFSET_BEARD, EQT::PP_OFFSET_HAIRCOLOR + 5);

    // Ground truth: titanium_structs.h's annotated offset column.
    EXPECT_EQ(EQT::PP_OFFSET_HAIRCOLOR, 172u);
    EXPECT_EQ(EQT::PP_OFFSET_FACE, 2264u);
}

TEST(ProfileAppearanceParse, DefaultConstructedIsNotValid) {
    EQT::ProfileAppearance a;
    EXPECT_FALSE(a.valid)
        << "zero is a legal value for every field, so .valid is the ONLY way "
           "to tell an unknown appearance from a real all-zero one";
}

// ---------------------------------------------------------------------
// 2. The merge. This is where the bug lived.
// ---------------------------------------------------------------------

TEST(AppearanceMerge, UnnamedFieldsAreResentFromTheProfile) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    eq.SetProfileAppearance(knownAppearance());
    EqActionHandler handler(eq);

    action::AppearanceChange change;
    change.face = 3;
    handler.setAppearance(change);

    auto sent = eq.Player().Test_GetLastFaceChange();
    ASSERT_TRUE(sent.sent);
    EXPECT_EQ(sent.body.face, 3) << "the field the user named must change";

    // THE HEADLINE. Before T167 both of these were sent as 0 -- eqemu would
    // have copied the zeros into the profile and saved them, permanently
    // resetting the character's eyes, for a command that only asked to
    // change the face.
    EXPECT_EQ(sent.body.eyecolor1, kEyeColor1);
    EXPECT_EQ(sent.body.eyecolor2, kEyeColor2);

    EXPECT_EQ(sent.body.haircolor, kHairColor);
    EXPECT_EQ(sent.body.beardcolor, kBeardColor);
    EXPECT_EQ(sent.body.hairstyle, kHairStyle);
    EXPECT_EQ(sent.body.beard, kBeard);
}

TEST(AppearanceMerge, EveryNamedFieldOverridesTheProfile) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    eq.SetProfileAppearance(knownAppearance());
    EqActionHandler handler(eq);

    action::AppearanceChange change;
    change.haircolor = 1;
    change.beardcolor = 2;
    change.eyecolor1 = 3;
    change.eyecolor2 = 4;
    change.hairstyle = 5;
    change.beard = 6;
    change.face = 7;
    handler.setAppearance(change);

    auto sent = eq.Player().Test_GetLastFaceChange();
    ASSERT_TRUE(sent.sent);
    EXPECT_EQ(sent.body.haircolor, 1);
    EXPECT_EQ(sent.body.beardcolor, 2);
    EXPECT_EQ(sent.body.eyecolor1, 3);
    EXPECT_EQ(sent.body.eyecolor2, 4);
    EXPECT_EQ(sent.body.hairstyle, 5);
    EXPECT_EQ(sent.body.beard, 6);
    EXPECT_EQ(sent.body.face, 7);
}

// Zero must be settable. It is the merge's own "unknown" value, so a naive
// implementation that treats 0 as "not supplied" would silently drop it.
TEST(AppearanceMerge, ZeroIsAValueNotAnAbsentField) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    eq.SetProfileAppearance(knownAppearance());
    EqActionHandler handler(eq);

    action::AppearanceChange change;
    change.eyecolor1 = 0;
    handler.setAppearance(change);

    auto sent = eq.Player().Test_GetLastFaceChange();
    ASSERT_TRUE(sent.sent);
    EXPECT_EQ(sent.body.eyecolor1, 0);
    EXPECT_EQ(sent.body.eyecolor2, kEyeColor2) << "the other eye is untouched";
}

// eqemu applies the whole body and Save()s, so our cached profile must
// advance to what we sent. Without that, the SECOND /appearance of a session
// re-sends the stale profile and silently undoes the first.
TEST(AppearanceMerge, ASecondChangeDoesNotRevertTheFirst) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    eq.SetProfileAppearance(knownAppearance());
    EqActionHandler handler(eq);

    action::AppearanceChange first;
    first.eyecolor1 = 99;
    handler.setAppearance(first);
    ASSERT_EQ(eq.Player().Test_GetLastFaceChange().body.eyecolor1, 99);

    action::AppearanceChange second;
    second.face = 5;
    handler.setAppearance(second);

    auto sent = eq.Player().Test_GetLastFaceChange();
    ASSERT_TRUE(sent.sent);
    EXPECT_EQ(sent.body.face, 5);
    EXPECT_EQ(sent.body.eyecolor1, 99)
        << "the first change is now what the server holds; re-sending the "
           "pre-change profile would undo it";
    EXPECT_EQ(eq.GetProfileAppearance().eyecolor1, 99);
}

// ---------------------------------------------------------------------
// 3. The refusal. Guessing here rewrites the character on the server.
// ---------------------------------------------------------------------

TEST(AppearanceMerge, NoProfileAndNoSelfEntitySendsNothing) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);   // no entity with this id exists
    EqActionHandler handler(eq);

    ASSERT_FALSE(eq.GetProfileAppearance().valid);

    action::AppearanceChange change;
    change.face = 3;
    handler.setAppearance(change);

    EXPECT_FALSE(eq.Player().Test_GetLastFaceChange().sent)
        << "an all-zero body would have been copied into the profile and "
           "saved by eqemu, wiping an appearance the user never asked to "
           "change";
}

}  // namespace
