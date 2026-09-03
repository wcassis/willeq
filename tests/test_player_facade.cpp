// K13 (Batch K, refactor/K13_context_services.md) -- eqt::PlayerFacade, the
// second context service.
//
// The 46 accessors moved verbatim out of src/client/eq_player_info.cpp and the
// private position/spawn-id/zone-identity block moved out of eq.h. EverQuest
// keeps the public half as forwarders for the callers outside the family.
//
// What this file pins is the property the move must not break: the facade and
// EverQuest read and write ONE store. T288 deleted EverQuest's duplicate
// position/heading/spawn-id/zone members precisely so no second copy could
// drift; K13 moved the accessors one level down, and a facade that kept its
// own state would re-open exactly that defect (which is what
// test_duplicate_state_stores.cpp guards structurally). Here it is asserted
// behaviourally: write through one receiver, read through the other.

#include <gtest/gtest.h>

#include "client/player_facade.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

class PlayerFacadeTest : public ::testing::Test {
protected:
    PlayerFacadeTest() : eq(MakeEq()) {}
    EverQuest eq;
};

TEST_F(PlayerFacadeTest, ServiceIsStableAndIsTheOneTheContextCarries) {
    EXPECT_EQ(&eq.Self(), &eq.Self());
    EXPECT_EQ(&eq.Context().self, &eq.Self());
}

TEST_F(PlayerFacadeTest, WritesThroughTheFacadeAreVisibleThroughEverQuest) {
    eq.Self().SetPosition(11.0f, 22.0f, 33.0f);
    eq.Self().SetHeading(64.0f);

    const glm::vec3 p = eq.GetPosition();
    EXPECT_FLOAT_EQ(p.x, 11.0f);
    EXPECT_FLOAT_EQ(p.y, 22.0f);
    EXPECT_FLOAT_EQ(p.z, 33.0f);
    EXPECT_FLOAT_EQ(eq.GetHeading(), 64.0f);
}

TEST_F(PlayerFacadeTest, WritesThroughEverQuestAreVisibleThroughTheFacade) {
    eq.SetPosition(-5.0f, 6.5f, 7.25f);
    EXPECT_FLOAT_EQ(eq.Self().PosX(), -5.0f);
    EXPECT_FLOAT_EQ(eq.Self().PosY(), 6.5f);
    EXPECT_FLOAT_EQ(eq.Self().PosZ(), 7.25f);
}

TEST_F(PlayerFacadeTest, TheLocalWriteIsTheSameStoreAsTheEventFiringWrite) {
    // SetPosLocal is the per-tick integrator path (no PlayerMoved event);
    // SetPosition is the discrete relocation path. T288: same store.
    eq.Self().SetPosLocal(1.0f, 2.0f, 3.0f);
    const glm::vec3 p = eq.Test_GetGameStatePlayerPosition();
    EXPECT_FLOAT_EQ(p.x, 1.0f);
    EXPECT_FLOAT_EQ(p.y, 2.0f);
    EXPECT_FLOAT_EQ(p.z, 3.0f);
    EXPECT_FLOAT_EQ(eq.GetPosition().x, 1.0f);

    eq.Self().SetHeadingLocal(180.0f);
    EXPECT_FLOAT_EQ(eq.GetHeading(), 180.0f);
}

TEST_F(PlayerFacadeTest, SpawnIdRoundTripsAndEverQuestReadsTheSameOne) {
    EXPECT_EQ(eq.Self().MySpawnId(), 0);
    eq.Self().SetMySpawnId(4242);
    EXPECT_EQ(eq.Self().MySpawnId(), 4242);
    EXPECT_EQ(eq.GetEntityID(), 4242);
    EXPECT_EQ(eq.GetMySpawnID(), 4242);
    EXPECT_EQ(eq.Self().GetEntityID(), 4242);
}

TEST_F(PlayerFacadeTest, ForwardersAgreeWithTheFacadeOnAFreshClient) {
    EXPECT_EQ(eq.GetLevel(), eq.Self().GetLevel());
    EXPECT_EQ(eq.GetClass(), eq.Self().GetClass());
    EXPECT_EQ(eq.GetPlatinum(), eq.Self().GetPlatinum());
    EXPECT_EQ(eq.IsInGroup(), eq.Self().IsInGroup());
    EXPECT_EQ(eq.IsAFK(), eq.Self().IsAFK());
    EXPECT_EQ(eq.IsVendorWindowOpen(), eq.Self().IsVendorWindowOpen());
    EXPECT_EQ(eq.IsBankWindowOpen(), eq.Self().IsBankWindowOpen());
    EXPECT_EQ(eq.IsTrainerWindowOpen(), eq.Self().IsTrainerWindowOpen());
    EXPECT_EQ(eq.HasPet(), eq.Self().HasPet());
}

TEST_F(PlayerFacadeTest, NotFullyZonedInOnAFreshHeadlessClient) {
    // The third term of IsFullyZonedIn() is EverQuest's m_update_running,
    // which the facade holds by reference -- the one member in this service
    // that is not GameState. A fresh client has never ticked.
    EXPECT_FALSE(eq.Self().IsFullyZonedIn());
    EXPECT_FALSE(eq.IsFullyZonedIn());
    EXPECT_EQ(eq.Self().CurrentZoneId(), 0);
    EXPECT_TRUE(eq.Self().CurrentZoneName().empty());
}

}  // namespace
