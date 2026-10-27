/**
 * T318 -- shrink, grow and illusion actually reach the renderer, and the
 * guild-invite accept joins you as a member rather than an officer.
 *
 * THE DEFECT. SPA 89 (ModelSize) and SPA 58 (Illusion) were dispatched in
 * the since-deleted spell_effects.cpp (T327) to three handlers that were one
 * LOG_DEBUG each, while the
 * client told the player "shrunk" / "enlarged" / "illusioned". The plan
 * assumed a local prediction path had to be built. IT DID NOT: the server is
 * authoritative and already tells us.
 *
 *   eqemu Mob::ChangeSize() (zone/mob.cpp:4488) clamps the size and calls
 *   SendAppearancePacket(AppearanceType::Size, (uint32) size) -- whole zone,
 *   self included -- and Mob::SendIllusionPacket() sends OP_Illusion carrying
 *   race, gender, textures AND size.
 *
 * Both packets were already decoded in eq_entities.cpp. What was missing was
 * everything after the decode: AT_SIZE divided the parameter by 10 ("EQ uses
 * size*10", which the server source contradicts) and then dropped it under a
 * TODO, and the illusion's size was written to the Entity and never published.
 * There was no size-update path in the renderer at all -- serverSize reached
 * EntityRenderer exactly once, at registerEntity(), smuggled through
 * collisionHeight and destroyed by the mesh build.
 *
 * WHAT IS PINNED HERE
 *   Part 1: the wire -> event path, from a real packet through a real
 *           EverQuest. This is the path a spell takes; nothing here pokes the
 *           renderer entry point directly.
 *   Part 2: the scale arithmetic, and (with client data present) a real
 *           EDT_NULL scene node whose scale is read before and after a size
 *           change.
 *   Part 3: the guild-invite accept bytes, through the T317 builder.
 *
 * The bridge arm in between (EntitySizeChanged -> setEntitySize) is pinned by
 * test_reachability_sweep, which requires every GameEventType to have both a
 * publisher and a consumer arm.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <optional>
#include <vector>

#include <fstream>
#include <irrlicht.h>

#include "client/bridge/game_state_bridge.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/willeq_database.h"
#include "client/graphics/entity_renderer.h"
#include "client/guild_packet_bodies.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;
using EQT::Graphics::clientActorScale;
using EQT::Graphics::entityNodeScale;

namespace {

std::vector<uint8_t> SpawnAppearancePayload(uint16_t spawnId, uint16_t type, uint32_t parameter) {
    return BuildPayload(10, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawnId);
        p.PutUInt16(4, type);
        p.PutUInt32(6, parameter);
    });
}

std::vector<uint8_t> IllusionPayload(uint32_t spawnId, int32_t race, uint8_t gender,
                                     uint8_t texture, float size) {
    return BuildPayload(2 + sizeof(EQT::Illusion_Struct), [&](EQ::Net::DynamicPacket& p) {
        EQT::Illusion_Struct body{};
        std::memset(&body, 0, sizeof(body));
        body.spawnid = spawnId;
        body.race = race;
        body.gender = gender;
        body.texture = texture;
        body.size = size;
        std::memcpy(static_cast<uint8_t*>(p.Data()) + 2, &body, sizeof(body));
    });
}

// Every EntitySizeChanged the bridge saw, in order.
std::vector<eqt::state::EntitySizeChangedData> DrainSizeEvents(
        eqt::bridge::GameStateBridge& bridge) {
    std::vector<eqt::state::EntitySizeChangedData> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::EntitySizeChanged) {
            out.push_back(std::get<eqt::state::EntitySizeChangedData>(ev.data));
        }
    }
    return out;
}

class EntitySizeWireTest : public ::testing::Test {
protected:
    EntitySizeWireTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~EntitySizeWireTest() override { eq.setBridge(nullptr); }

    // A humanoid NPC at the default server size.
    void SpawnTarget(uint16_t id = 42) {
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(id, "Shrinkme", 10, 20, 3));
        bridge.drainEvents();
    }

    EverQuest eq;
    TestBridge bridge;
};

// =========================================================================
// Part 1 -- the wire. OP_SpawnAppearance AT_SIZE and OP_Illusion.
// =========================================================================

TEST_F(EntitySizeWireTest, ShrinkOnTheWireBecomesASizeEventAtTheServersOwnScale) {
    SpawnTarget();
    // eqemu clamps a client/pet to [3, 15]; 3 is a fully shrunk player.
    ReplayZone(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(42, AT_SIZE, 3));

    auto events = DrainSizeEvents(bridge);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].spawnId, 42);
    // NOT 0.3. The parameter is the size itself, not size*10 -- see
    // Mob::ChangeSize's `SendAppearancePacket(..., (uint32) size)`.
    EXPECT_FLOAT_EQ(events[0].size, 3.0f);
}

TEST_F(EntitySizeWireTest, GrowOnTheWireBecomesASizeEvent) {
    SpawnTarget();
    ReplayZone(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(42, AT_SIZE, 15));

    auto events = DrainSizeEvents(bridge);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FLOAT_EQ(events[0].size, 15.0f);
}

TEST_F(EntitySizeWireTest, TheEntityRecordCarriesTheSameSizeTheEventDoes) {
    SpawnTarget();
    ReplayZone(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(42, AT_SIZE, 9));

    const auto& entities = eq.GetEntities();
    auto it = entities.find(42);
    ASSERT_NE(it, entities.end());
    EXPECT_FLOAT_EQ(it->second.size, 9.0f);
}

// The negative the plan asks for: an unrelated appearance change must not
// rescale anything.
TEST_F(EntitySizeWireTest, UnrelatedAppearanceTypesPublishNoSizeEvent) {
    SpawnTarget();
    ReplayZone(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(42, AT_SNEAK, 1));
    ReplayZone(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(42, AT_LIGHT, 5));
    ReplayZone(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(42, AT_ANIMATION, 110));
    ReplayZone(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(42, AT_AFK, 1));

    EXPECT_TRUE(DrainSizeEvents(bridge).empty());
}

// A size of 0 is not a size the server can mean; publishing it would collapse
// the entity to nothing.
TEST_F(EntitySizeWireTest, AZeroSizeIsNotPublished) {
    SpawnTarget();
    ReplayZone(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(42, AT_SIZE, 0));
    EXPECT_TRUE(DrainSizeEvents(bridge).empty());
}

TEST_F(EntitySizeWireTest, IllusionPublishesItsSizeAlongsideTheModelSwap) {
    SpawnTarget();
    // Race 60 (skeleton), size 2 -- a classic illusion.
    ReplayZone(eq, HC_OP_Illusion, IllusionPayload(42, 60, 1, 3, 2.0f));

    auto all = bridge.drainEvents();
    bool sawAppearance = false;
    std::optional<eqt::state::EntitySizeChangedData> size;
    for (auto& ev : all) {
        if (ev.type == eqt::state::GameEventType::EntityAppearanceChanged) {
            auto& d = std::get<eqt::state::EntityAppearanceChangedData>(ev.data);
            EXPECT_EQ(d.raceId, 60);
            EXPECT_EQ(d.gender, 1);
            sawAppearance = true;
        } else if (ev.type == eqt::state::GameEventType::EntitySizeChanged) {
            size = std::get<eqt::state::EntitySizeChangedData>(ev.data);
        }
    }
    // Illusion is a MODEL SWAP as well as a resize, and the model swap was
    // already wired -- only the size was being dropped.
    EXPECT_TRUE(sawAppearance);
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(size->spawnId, 42);
    EXPECT_FLOAT_EQ(size->size, 2.0f);
}

TEST_F(EntitySizeWireTest, AnIllusionWithNoSizeDoesNotResize) {
    SpawnTarget();
    ReplayZone(eq, HC_OP_Illusion, IllusionPayload(42, 60, 1, 3, 0.0f));

    bool sawSize = false;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::EntitySizeChanged) sawSize = true;
    }
    EXPECT_FALSE(sawSize);
}

// =========================================================================
// Part 2 -- the arithmetic, and a real node's scale.
// =========================================================================

// B1172 (#491): the scale is the official client's actor scale
// (actor_scale.h, size x 0.2 for the classic races); the full race / gender /
// size table is pinned by test_actor_scale.
TEST(EntitySizeScale, ShrinkHalvesAndGrowMultiplies) {
    EXPECT_FLOAT_EQ(clientActorScale(1, 0, 6.0f), 1.2f);
    EXPECT_FLOAT_EQ(clientActorScale(1, 0, 3.0f), 0.6f);
    EXPECT_FLOAT_EQ(clientActorScale(1, 0, 15.0f), 3.0f);
}

TEST(EntitySizeScale, NoSizeFromTheServerIsTheRaceDefaultNotZero) {
    // The trap: "the server did not say" must be the client's race/gender
    // default size (0x47a147), not zero.
    EXPECT_FLOAT_EQ(clientActorScale(6, 1, 0.0f), 1.0f);   // Dark Elf: 5.0
    EXPECT_FLOAT_EQ(clientActorScale(8, 0, -1.0f), 0.8f);  // Dwarf: 4.0
}

// The local player's node draws translation-only, so its node scale (and its
// collisionZOffset) is the 1.0 it is drawn at; every other spawn gets the
// client's actor scale.
TEST(EntitySizeScale, TheLocalPlayerNodeKeepsTheScaleItIsDrawnAt) {
    EXPECT_FLOAT_EQ(entityNodeScale(false, 1, 0, 6.0f), 1.2f);
    EXPECT_FLOAT_EQ(entityNodeScale(true, 1, 0, 6.0f), 1.0f);
    EXPECT_FLOAT_EQ(entityNodeScale(true, 8, 0, 4.0f), 1.0f);
}

// -------------------------------------------------------------------------
// Part 2b -- a REAL node in a real (EDT_NULL) scene graph. Skips without the
// shipped db or the client tree. Same gate as test_entity_collision_z_offset.
// -------------------------------------------------------------------------

class EntityRescaleNodeTest : public ::testing::Test {
protected:
    static bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(willeqDb_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&willeqDb_);

        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        eqClientPath_ = envPath ? envPath : EQ_CLIENT_DATA_PATH;
        if (!eqClientPath_.empty() && eqClientPath_.back() != '/') eqClientPath_ += '/';
        if (!fileExists(eqClientPath_ + "global_chr.s3d"))
            GTEST_SKIP() << "no client data at " << eqClientPath_;

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        smgr_ = device_->getSceneManager();

        entities_ = std::make_unique<EQT::Graphics::EntityRenderer>(
            smgr_, device_->getVideoDriver(), device_->getFileSystem());
        entities_->setClientPath(eqClientPath_);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        entities_->setConstrainedTextureCache(textureCache_.get());
        if (!entities_->loadGlobalCharacters()) GTEST_SKIP() << "global_chr.s3d would not load";
    }

    void TearDown() override {
        entities_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // The entity's node, found by walking the scene graph -- the node
    // production actually built, not one this test re-derives.
    irr::scene::ISceneNode* findEntityNode() const {
        for (auto* child : smgr_->getRootSceneNode()->getChildren()) {
            if (dynamic_cast<irr::scene::IMeshSceneNode*>(child) ||
                dynamic_cast<EQT::Graphics::EQAnimatedMeshSceneNode*>(child)) {
                return child;
            }
        }
        return nullptr;
    }

    std::string eqClientPath_;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::EntityRenderer> entities_;
    EQT::WilleqDatabase willeqDb_;
};

TEST_F(EntityRescaleNodeTest, ShrinkAndGrowChangeTheBuiltNodesScale) {
    constexpr uint16_t kSpawn = 7;
    constexpr uint16_t kHumanMale = 1;
    ASSERT_TRUE(entities_->createEntity(kSpawn, kHumanMale, "Shrinkme", 10.f, 20.f, 3.f, 0.f,
                                        /*isPlayer=*/false, /*gender=*/0,
                                        EQT::Graphics::EntityAppearance(), /*isNPC=*/true,
                                        /*isCorpse=*/false, /*serverSize=*/6.0f));
    auto* node = findEntityNode();
    ASSERT_NE(node, nullptr) << "no entity node in the scene graph";
    const float baseScale = node->getScale().Y;
    ASSERT_GT(baseScale, 0.0f);

    // Shrink to the eqemu client floor.
    EXPECT_TRUE(entities_->setEntitySize(kSpawn, 3.0f));
    EXPECT_NEAR(node->getScale().Y, baseScale * 0.5f, baseScale * 1e-4f);
    EXPECT_NEAR(node->getScale().X, node->getScale().Y, 1e-5f);

    // Grow to the eqemu client ceiling.
    EXPECT_TRUE(entities_->setEntitySize(kSpawn, 15.0f));
    EXPECT_NEAR(node->getScale().Y, baseScale * 2.5f, baseScale * 1e-4f);

    // Back to normal.
    EXPECT_TRUE(entities_->setEntitySize(kSpawn, 6.0f));
    EXPECT_NEAR(node->getScale().Y, baseScale, baseScale * 1e-4f);
}

// B1172 (#491): the oracle's two figures through the production build --
// the human target (race 1, gender 0, size 6.000 on the wire) is drawn at the
// client's 1.2, the dark-elf caster (race 6, gender 1, size 0.000) at her
// default 1.0, so the drawn height is the mesh's own height times exactly
// that (the oracle draws them 288 / 209 px at r51).
TEST_F(EntityRescaleNodeTest, TheOracleTargetAndCasterAreDrawnAtTheClientsActorScale) {
    struct Fig { uint16_t spawn, race; uint8_t gender; float size, scale; };
    for (const Fig& f : {Fig{11, 1, 0, 6.0f, 1.2f}, Fig{12, 6, 1, 0.0f, 1.0f}}) {
        ASSERT_TRUE(entities_->createEntity(f.spawn, f.race, "Figure", 0.f, 0.f, 0.f, 0.f, false, f.gender,
                                            EQT::Graphics::EntityAppearance(), true, false, f.size));
        auto* node = entities_->getEntities().at(f.spawn).sceneNode;
        ASSERT_NE(node, nullptr) << "race " << f.race;
        EXPECT_FLOAT_EQ(node->getScale().Y, f.scale) << "race " << f.race;
        auto drawnHeight = [&]() {
            node->updateAbsolutePosition();
            irr::core::aabbox3df world = node->getBoundingBox();
            node->getAbsoluteTransformation().transformBoxEx(world);
            return world.MaxEdge.Y - world.MinEdge.Y;
        };
        const float drawn = drawnHeight();
        const irr::core::vector3df kept = node->getScale();
        node->setScale(irr::core::vector3df(1.0f, 1.0f, 1.0f));
        const float mesh = drawnHeight();
        node->setScale(kept);
        ASSERT_GT(mesh, 0.0f) << "race " << f.race;
        EXPECT_NEAR(drawn / mesh, f.scale, 1e-4f) << "race " << f.race;
    }
}

TEST_F(EntityRescaleNodeTest, AnUnknownSpawnAndAnImpossibleSizeAreRefused) {
    constexpr uint16_t kSpawn = 7;
    ASSERT_TRUE(entities_->createEntity(kSpawn, 1, "Shrinkme", 0.f, 0.f, 0.f, 0.f, false, 0,
                                        EQT::Graphics::EntityAppearance(), true, false, 6.0f));
    auto* node = findEntityNode();
    ASSERT_NE(node, nullptr);
    const float baseScale = node->getScale().Y;

    EXPECT_FALSE(entities_->setEntitySize(9999, 3.0f));
    EXPECT_FALSE(entities_->setEntitySize(kSpawn, 0.0f));
    EXPECT_FALSE(entities_->setEntitySize(kSpawn, -4.0f));
    // The negative: nothing was rescaled by any of those.
    EXPECT_FLOAT_EQ(node->getScale().Y, baseScale);
}

// =========================================================================
// Part 3 -- OP_GuildInviteAccept's rank byte.
// =========================================================================

TEST(GuildInviteAcceptWire, AcceptJoinsAsAMemberNotAnOfficer) {
    auto body = EQ::guild::BuildGuildInviteAccept("Inviter", "Testchar", 7, /*accept=*/true);

    // GUILD_OFFICER_TI. This is what the client used to send, and it is what
    // made every accepted invite an officer.
    EXPECT_NE(body.response, 1u);

    // GUILD_MEMBER_TI. Handle_OP_GuildInviteAccept remaps it to GUILD_RECRUIT
    // (8) -- the bottom of the ladder, and the only ordinary-member rank a
    // Titanium client can reach. GUILD_MEMBER (5) is not addressable from
    // this client version.
    EXPECT_EQ(body.response, static_cast<uint32_t>(EQ::guild::kRankMember));
    EXPECT_EQ(body.response, 0u);
}

TEST(GuildInviteAcceptWire, CarriesTheNamesAndGuildId) {
    auto body = EQ::guild::BuildGuildInviteAccept("Inviter", "Testchar", 7, true);
    EXPECT_STREQ(body.inviter, "Inviter");
    EXPECT_STREQ(body.newmember, "Testchar");
    EXPECT_EQ(body.guildeqid, 7u);
}

TEST(GuildInviteAcceptWire, DeclineSendsTheHonestNoRankByte) {
    auto body = EQ::guild::BuildGuildInviteAccept("Inviter", "Testchar", 7, /*accept=*/false);
    // GUILD_RANK_NONE_TI. It does not work -- the server's decline test runs
    // AFTER a remap that can only produce 8, 3 or 0, so a Titanium decline is
    // processed as an accept. That is a SERVER bug, recorded in
    // guild_packet_bodies.h for upstream and deliberately not worked around
    // here. What we can do is not send the accept byte.
    EXPECT_EQ(body.response, static_cast<uint32_t>(EQ::guild::kRankNone));
    EXPECT_EQ(body.response, 9u);
    EXPECT_NE(body.response, static_cast<uint32_t>(EQ::guild::kRankOfficer));
}

TEST(GuildInviteAcceptWire, AcceptAndDeclineAreDistinguishableOnTheWire) {
    auto yes = EQ::guild::BuildGuildInviteAccept("A", "B", 1, true);
    auto no  = EQ::guild::BuildGuildInviteAccept("A", "B", 1, false);
    EXPECT_NE(yes.response, no.response);
}

}  // namespace

// =========================================================================
// T359 (board B011) -- the DOCUMENTED LIMIT of setEntitySize: it rescales the
// node and the collision Z offset, and does NOT recompute a boat's
// collisionHeight / deckZ. Both are computed once, at mesh-build time, from
// the unscaled mesh bounding box multiplied by the build-time scale
// (entity_mesh_build.cpp and entity_build_step.cpp hold the two copies of that
// formula), so a resized boat's deck surface would go stale.
//
// This is a limit, not a live defect: nothing in the game resizes a boat.
// eqemu's size changes come from Mob::ChangeSize() (spell SPA 89 / SPA 58
// illusions on players and NPCs) and boats are zone-owned static spawns that
// nothing casts on. The recompute was deliberately NOT added: it would be a
// THIRD copy of a formula that already disagrees with itself between its two
// existing sites (one reads mesh->getBoundingBox() at build, the other
// vis.meshNode->getMesh()->getBoundingBox() and vis.lastZ), and no reachable
// path could ever exercise it. Pinning the limit is what keeps it honest -- if
// a future unit DOES make boats resizable, this test fails and says why.
// =========================================================================

TEST_F(EntityRescaleNodeTest, ResizingABoatLeavesItsDeckSurfaceWhereItWas) {
    constexpr uint16_t kBoat = 41;
    constexpr uint16_t kShipRace = 72;
    // T504 resolver: models come from the zone's sources, as in the original
    // client; race 72's SHIP lives in oot_chr.s3d, so this entity is built in
    // Ocean of Tears (the directory scan that used to find it from any zone
    // is gone).
    entities_->setCurrentZone("oot");
    ASSERT_TRUE(entities_->createEntity(kBoat, kShipRace, "Barrel_Barge", 100.f, 200.f, 30.f, 0.f,
                                        /*isPlayer=*/false, /*gender=*/0,
                                        EQT::Graphics::EntityAppearance(), /*isNPC=*/true,
                                        /*isCorpse=*/false, /*serverSize=*/6.0f));

    const auto& built = entities_->getEntities();
    auto it = built.find(kBoat);
    ASSERT_NE(it, built.end());
    if (!it->second.hasCollision) {
        GTEST_SKIP() << "race " << kShipRace
                     << " did not build a collision mesh from this client tree -- "
                        "nothing to assert the limit against";
    }

    const float deckBefore = it->second.deckZ;
    const float heightBefore = it->second.collisionHeight;
    ASSERT_GT(heightBefore, 0.0f);

    // Double the boat's size word. B1172: the client forces a ship's actor
    // scale to 2.0 whatever its size (0x47a239 -> 0x47fc41), so the node
    // keeps 2.0...
    auto* node = it->second.sceneNode;
    ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->getScale().Y, 2.0f);
    EXPECT_TRUE(entities_->setEntitySize(kBoat, 12.0f));
    EXPECT_FLOAT_EQ(entities_->getEntities().at(kBoat).sceneNode->getScale().Y, 2.0f);

    // ...and the deck surface does NOT follow it. THIS IS THE LIMIT, pinned.
    EXPECT_FLOAT_EQ(entities_->getEntities().at(kBoat).collisionHeight, heightBefore);
    EXPECT_FLOAT_EQ(entities_->getEntities().at(kBoat).deckZ, deckBefore);
}
