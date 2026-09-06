// T404 Leg B (board B251): the local player's own row in the entity map was
// only written by four discrete relocation sites and never by the two funnels
// through which the player's position actually changes.
//
// T401 measured the consequence twice in one live session:
//   (1) SwordBlk.WAV 120 was played 30 times and is ABSENT from a bit-exact
//       loopback recording at every playback rate. Its log line reads
//       pos=(428,-74,3) -- the character_data spawn pose in wire order --
//       while the player stood at (146,-1009,51), ~1,020 units away, where
//       the 3D attenuation takes it to zero.
//   (2) SpellManager::checkRange refused a SELF-targeted gem cast after a
//       #goto with "distance=1019.0 range=200", caster == target. checkRange
//       takes the caster from GetPosition() (PlayerState, correct) and the
//       target from the entity map (the stale row).
//
// There is ONE authoritative position store and it is m_game_state.player()
// (T288). The self entity row is a PROJECTION of it; these tests pin that the
// projection follows both funnels -- SetPosLocal (the per-tick integrator and
// the renderer's PlayerPositionChanged intent) and SetPosition (discrete
// server-forced relocation) -- and pin the two symptoms directly.

#include <gtest/gtest.h>

#include "client/eq.h"
#include "client/player_facade.h"
#include "zone_stream_test_utils.h"

#include <cmath>

using namespace zonetest;

namespace {

// Spawn the player's own entity at the pose T401's character_data held, take
// that spawn id as ours, and hand back the EverQuest under test.
constexpr uint16_t kSelfSpawnId = 4242;
// Spawn_Struct positions are 8ths (the handler divides the 19-bit field by 8),
// so the wire values are the pose times eight.
constexpr float    kSpawnServerX = -74.0f;   // -> entity.y
constexpr float    kSpawnServerY = 428.0f;   // -> entity.x
constexpr float    kSpawnZ = 3.0f;

class SelfEntityPositionTest : public ::testing::Test {
protected:
    SelfEntityPositionTest() : eq(MakeEq()) {}

    void SetUp() override {
        ReplayZone(eq, HC_OP_NewSpawn,
                   NewSpawnPayloadAt(kSelfSpawnId, "Testchar",
                                     static_cast<int32_t>(kSpawnServerX * 8),
                                     static_cast<int32_t>(kSpawnServerY * 8),
                                     static_cast<int32_t>(kSpawnZ * 8),
                                     /*level=*/5, /*npc_type=*/0));
        eq.Test_SetMySpawnIdForSeam(kSelfSpawnId);

        const Entity* self = Self();
        ASSERT_NE(self, nullptr) << "self spawn was not injected";
        // The spawn pose, in client coordinates (INVERSEXY).
        ASSERT_FLOAT_EQ(self->x, kSpawnServerY);
        ASSERT_FLOAT_EQ(self->y, kSpawnServerX);
    }

    const Entity* Self() const {
        const auto& ents = eq.GetEntities();
        auto it = ents.find(kSelfSpawnId);
        return it == ents.end() ? nullptr : &it->second;
    }

    EverQuest eq;
};

// The integrator / renderer-intent funnel. Before T404 the row stayed at the
// spawn pose for the whole session.
TEST_F(SelfEntityPositionTest, SetPosLocalMovesTheSelfEntityRow) {
    eq.Self().SetPosLocal(146.0f, -1009.0f, 51.0f);

    const Entity* self = Self();
    ASSERT_NE(self, nullptr);
    EXPECT_FLOAT_EQ(self->x, 146.0f);
    EXPECT_FLOAT_EQ(self->y, -1009.0f);
    EXPECT_FLOAT_EQ(self->z, 51.0f);
}

// The discrete server-forced relocation funnel (#goto, zone-in warp, death
// respawn all land here or on SetPosLocal).
TEST_F(SelfEntityPositionTest, SetPositionMovesTheSelfEntityRow) {
    eq.SetPosition(146.0f, -1009.0f, 51.0f);

    const Entity* self = Self();
    ASSERT_NE(self, nullptr);
    EXPECT_FLOAT_EQ(self->x, 146.0f);
    EXPECT_FLOAT_EQ(self->y, -1009.0f);
    EXPECT_FLOAT_EQ(self->z, 51.0f);
}

// T401 symptom (1), stated as the property the sound path needs: whatever
// position a positioned sound is emitted from for our own spawn id equals the
// player's position. combat_actions.cpp's WITH_AUDIO block reads
// entities[target_id].x/y/z, so a self-targeted melee event emits from here.
TEST_F(SelfEntityPositionTest, SelfEntityRowEqualsThePlayerPositionAfterRelocation) {
    eq.Self().SetPosLocal(146.0f, -1009.0f, 51.0f);

    const Entity* self = Self();
    ASSERT_NE(self, nullptr);
    const glm::vec3 p = eq.GetPosition();
    EXPECT_FLOAT_EQ(self->x, p.x);
    EXPECT_FLOAT_EQ(self->y, p.y);
    EXPECT_FLOAT_EQ(self->z, p.z);
}

// T401 symptom (2): the exact quantity checkRange computes for a SELF-targeted
// cast. It read 1019.0 live; caster and target are the same character, so it
// must be 0.
TEST_F(SelfEntityPositionTest, SelfTargetedRangeDistanceIsZeroAfterRelocation) {
    eq.Self().SetPosLocal(146.0f, -1009.0f, 51.0f);

    const Entity* self = Self();
    ASSERT_NE(self, nullptr);
    const glm::vec3 caster = eq.GetPosition();
    const glm::vec3 target(self->x, self->y, self->z);
    EXPECT_FLOAT_EQ(glm::distance(caster, target), 0.0f);
}

// A relocation must move ONLY our own row.
TEST_F(SelfEntityPositionTest, AnotherEntitysRowIsUntouched) {
    ReplayZone(eq, HC_OP_NewSpawn,
               NewSpawnPayloadAt(99, "a_rat", 10 * 8, 20 * 8, 30 * 8));
    eq.Self().SetPosLocal(146.0f, -1009.0f, 51.0f);

    const auto& ents = eq.GetEntities();
    auto it = ents.find(99);
    ASSERT_NE(it, ents.end());
    EXPECT_FLOAT_EQ(it->second.x, 20.0f);
    EXPECT_FLOAT_EQ(it->second.y, 10.0f);
    EXPECT_FLOAT_EQ(it->second.z, 30.0f);
}

// HEADING is deliberately NOT mirrored: ZoneProcessClientUpdate stores
// server_h verbatim in the row while PlayerState gets 90 - server_h (the
// player-only flip in docs/CLAUDE.md's Heading/Rotation Conventions). Pin
// that, so a later unit does not "finish the job" and silently rotate every
// self row by 90 degrees plus a mirror.
TEST_F(SelfEntityPositionTest, HeadingIsNotMirroredIntoTheSelfEntityRow) {
    const Entity* before = Self();
    ASSERT_NE(before, nullptr);
    const float rowHeadingBefore = before->heading;

    eq.Self().SetHeadingLocal(123.0f);
    eq.Self().SetPosLocal(1.0f, 2.0f, 3.0f);

    const Entity* after = Self();
    ASSERT_NE(after, nullptr);
    EXPECT_FLOAT_EQ(after->heading, rowHeadingBefore);
}

// Before zone-in the spawn id is 0; a position write must not invent a row.
TEST(SelfEntityPositionNoSpawn, NoSelfRowIsCreatedBeforeZoneIn) {
    EverQuest eq = MakeEq();
    const size_t before = eq.GetEntities().size();
    eq.Self().SetPosLocal(5.0f, 6.0f, 7.0f);
    EXPECT_EQ(eq.GetEntities().size(), before);
    EXPECT_FLOAT_EQ(eq.GetPosition().x, 5.0f);
}

} // namespace
