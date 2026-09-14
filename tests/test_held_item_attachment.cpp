/**
 * T472 -- held items and model facing (boards B587, B588).
 *
 * Every assertion here is GEOMETRIC and derived from the S3D data, not from a
 * picture:
 *
 *  1. The lantern (IT48) was never drawn because EquipmentModelLoader indexed a
 *     mesh only when its NAME contained "IT48_". IT48's actor is hierarchical
 *     (0x14 -> 0x11 -> IT48_HS_DEF -> LANTERN01_DAG -> LANTERN01_DMSPRITEDEF),
 *     so no mesh carries that name. The census below re-derives, from the
 *     archives, every IT actor whose reference chain draws something and
 *     requires the index to hold all of them (pre-fix: 19 missing).
 *
 *  2. An item held at a point bone is placed in that BONE'S frame with one
 *     constant correction, a half turn about the item's Y axis, and the mesh
 *     centre added (see bone_attachment.h computeHeldItemTransform()). Pinned
 *     by what that correction was derived from: a one-handed blade's tip hangs
 *     BELOW the grip at idle, the lantern hangs below the hand, and a shield's
 *     front-textured face points AWAY from the body.
 *
 *  3. A player who has not moved since zone-in is drawn at the renderer's own
 *     heading -- the one the camera orbits -- and so looks exactly like any
 *     other client's view of the same server heading (an NPC-path entity).
 *
 * The first-person draw rule (T307/T309) is guarded separately by
 * tests/test_first_person_weapon_attachment.cpp.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <irrlicht.h>

#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/equipment_actor_geometry.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/player/camera_rig.h"
#include "client/willeq_database.h"

using namespace EQT::Graphics;

namespace {

bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

std::string eqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    std::string p = env ? env : EQ_CLIENT_DATA_PATH;
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}

// The seventeen archives EquipmentModelLoader::buildEquipmentIndex() reads.
const std::vector<std::string> kEquipArchives = {
    "obequip_lexit.s3d", "frogequip.s3d", "lgequip_amr2.s3d", "lgequip_amr.s3d", "loyequip.s3d",
    "vequip.s3d", "lgequip2.s3d", "lgequip.s3d", "ldonequip.s3d", "gatesequip.s3d", "gequip8.s3d",
    "gequip6.s3d", "gequip5.s3d", "gequip4.s3d", "gequip3.s3d", "gequip2.s3d", "gequip.s3d"};

struct V3 { float x, y, z; };

V3 boneXf(const BoneMat4& m, float x, float y, float z) {
    return {m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12], m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13],
            m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14]};
}

// Where the renderer DRAWS a bone-space point: the player's body is baked by
// (nodeRot.Y + 90) about EQ Z and drawn translation-only
// (EQAnimatedMeshSceneNode::applyAnimation/render); every other entity is drawn
// through its full absolute transform.
irr::core::vector3df drawn(EQAnimatedMeshSceneNode* node, V3 eq) {
    if (node->isPlayerNode()) {
        const float th = (node->getRotation().Y + 90.0f) * 3.14159f / 180.0f;
        const float c = std::cos(th), s = std::sin(th);
        return irr::core::vector3df(eq.x * c - eq.y * s, eq.z, eq.x * s + eq.y * c) +
               node->getAbsoluteTransformation().getTranslation();
    }
    irr::core::vector3df p(eq.x, eq.z, eq.y);
    node->getAbsoluteTransformation().transformVect(p);
    return p;
}

class HeldItemTest : public ::testing::Test {
protected:
    std::string eq_;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<EntityRenderer> ents_;
    EQT::WilleqDatabase db_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath));
        EQT::WilleqDatabase::setGlobalInstance(&db_);
        eq_ = eqClientPath();
        if (!fileExists(eq_ + "global_chr.s3d") || !fileExists(eq_ + "gequip.s3d"))
            GTEST_SKIP() << "no client data at " << eq_;
        irr::SIrrlichtCreationParameters p;
        p.DriverType = irr::video::EDT_NULL;
        p.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(p);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        smgr_ = device_->getSceneManager();
        ents_ = std::make_unique<EntityRenderer>(smgr_, device_->getVideoDriver(), device_->getFileSystem());
        ents_->setClientPath(eq_);
        cache_ = std::make_unique<ConstrainedTextureCache>(
            ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT));
        cache_->setDriver(device_->getVideoDriver());
        ents_->setConstrainedTextureCache(cache_.get());
        if (!ents_->loadGlobalCharacters()) GTEST_SKIP() << "global_chr.s3d would not load";
        ents_->loadEquipmentModels();
    }

    void TearDown() override {
        ents_.reset();
        cache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // A Human male at (100, 200, 3), idle, with the production per-frame
    // equipment placement run once.
    EQAnimatedMeshSceneNode* spawnIdle(uint16_t spawn, bool player, float headingDeg, uint32_t primary,
                                       uint32_t secondary) {
        EntityAppearance a;
        a.equipment[7] = primary;
        a.equipment[8] = secondary;
        ents_->clearEntities();
        EXPECT_TRUE(ents_->createEntity(spawn, 1, "T472", 100.0f, 200.0f, 3.0f, headingDeg, player, 0, a, !player));
        if (player) {
            ents_->setPlayerSpawnId(spawn);
            ents_->setPlayerEntityAnimation("o01", true);
        } else {
            ents_->setEntityAnimation(spawn, "o01", true);
        }
        auto* node = static_cast<EQAnimatedMeshSceneNode*>(ents_->getEntities().at(spawn).animatedNode);
        for (int t = 0; t < 5; ++t) node->OnAnimate(200 + t * 16);
        ents_->updateMainThreadEntityState(0.016f);
        node->updateAbsolutePosition();
        const EntityVisual& vis = ents_->getEntities().at(spawn);
        if (vis.primaryEquipNode) vis.primaryEquipNode->updateAbsolutePosition();
        if (vis.secondaryEquipNode) vis.secondaryEquipNode->updateAbsolutePosition();
        return node;
    }

    static V3 boneOrigin(EQAnimatedMeshSceneNode* node, const std::string& name) {
        auto sk = node->getAnimator().getSkeleton();
        int idx = sk ? sk->getBoneIndex(name) : -1;
        EXPECT_GE(idx, 0) << name;
        if (idx < 0) return {0, 0, 0};
        return boneXf(node->getAnimator().getBoneStates()[idx].worldTransform, 0, 0, 0);
    }

    static std::vector<irr::core::vector3df> worldVertices(irr::scene::IMeshSceneNode* equip,
                                                           std::vector<irr::core::vector3df>* local = nullptr) {
        std::vector<irr::core::vector3df> out;
        const irr::core::matrix4& abs = equip->getAbsoluteTransformation();
        irr::scene::IMesh* mesh = equip->getMesh();
        for (irr::u32 b = 0; mesh && b < mesh->getMeshBufferCount(); ++b) {
            irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(b);
            for (irr::u32 i = 0; i < buf->getVertexCount(); ++i) {
                irr::core::vector3df p = buf->getPosition(i);
                if (local) local->push_back(p);
                abs.transformVect(p);
                out.push_back(p);
            }
        }
        return out;
    }
};

// ---------------------------------------------------------------------------
// 1. Indexing: every actor whose WLD reference chain draws a mesh is indexed.
// ---------------------------------------------------------------------------

TEST_F(HeldItemTest, EveryActorWhoseReferenceChainDrawsIsIndexed) {
    std::map<int, EquipmentModelLoader::EquipmentModelRef> index;
    std::map<std::string, EquipmentModelLoader::EquipmentTextureRef> textures;
    ASSERT_TRUE(EquipmentModelLoader::buildEquipmentIndex(eq_, index, textures));

    const std::regex itName("IT(\\d+)");
    int chainActors = 0;
    std::vector<std::string> missing;
    for (const auto& archive : kEquipArchives) {
        PfsArchive pfs;
        if (!pfs.open(eq_ + archive)) continue;
        std::vector<std::string> wlds;
        pfs.getFilenames(".wld", wlds);
        for (const auto& wldName : wlds) {
            WldLoader wld;
            if (!wld.parseFromArchive(eq_ + archive, wldName)) continue;
            for (const auto& [name, def] : wld.getObjectDefs()) {
                std::smatch m;
                if (!std::regex_match(name, m, itName)) continue;
                bool draws = false;
                for (uint32_t ref : def.meshRefs) {
                    auto mr = wld.getModelRefs().find(ref);
                    if (mr != wld.getModelRefs().end() && wld.getGeometryByFragmentIndex(mr->second.geometryFragRef))
                        draws = true;
                    auto sr = wld.getSkeletonRefs().find(ref);
                    if (sr == wld.getSkeletonRefs().end()) continue;
                    auto tr = wld.getSkeletonTracks().find(sr->second);
                    if (tr == wld.getSkeletonTracks().end()) continue;
                    for (const auto& bone : tr->second->allBones) {
                        auto bm = wld.getModelRefs().find(bone->modelRef);
                        if (bone->modelRef && bm != wld.getModelRefs().end() &&
                            wld.getGeometryByFragmentIndex(bm->second.geometryFragRef))
                            draws = true;
                    }
                }
                if (!draws) continue;
                ++chainActors;
                if (!index.count(std::stoi(m[1].str()))) missing.push_back(archive + ":" + name);
            }
        }
    }
    std::ostringstream list;
    for (const auto& s : missing) list << " " << s;
    EXPECT_GT(chainActors, 700) << "the census found too few actors to mean anything";
    EXPECT_TRUE(missing.empty()) << missing.size() << " drawable actors are not indexed:" << list.str();
    EXPECT_TRUE(index.count(48)) << "IT48, the Gloomingdeep Lantern";
}

TEST_F(HeldItemTest, LanternMeshBuildsOnTheZoneLoadAndTheOnDemandPaths) {
    EquipmentModelLoader* loader = ents_->getEquipmentModelLoader();
    ASSERT_NE(loader, nullptr);
    const EquipmentModelLoader::EquipmentModelRef* ref = loader->getModelRef(48);
    ASSERT_NE(ref, nullptr) << "IT48 is not in the equipment index";
    // renderer_zone_load_entities.cpp / entity_prep_worker.cpp
    auto data = EquipmentModelLoader::extractEquipmentModelOffThread(*ref, 48);
    ASSERT_TRUE(data && data->geometry);
    EXPECT_EQ(data->geometry->vertices.size(), 167u) << "LANTERN01_DMSPRITEDEF has 167 vertices";
    // EntityRenderer::attachEquipment()
    irr::scene::IMesh* mesh = loader->getEquipmentMeshByModelId(48);
    ASSERT_NE(mesh, nullptr);
    EXPECT_GT(mesh->getMeshBufferCount(), 0u);
}

// ---------------------------------------------------------------------------
// 2. Orientation, from the data.
// ---------------------------------------------------------------------------

TEST_F(HeldItemTest, OneHandedBladeHangsBelowTheGripAtIdle) {
    struct Case { bool player; float heading; };
    for (Case c : {Case{true, 0.0f}, Case{true, 90.0f}, Case{true, 337.5f}, Case{false, 0.0f}, Case{false, 112.5f}}) {
        EQAnimatedMeshSceneNode* node = spawnIdle(7, c.player, c.heading, /*IT1 sword*/ 1, 0);
        const EntityVisual& vis = ents_->getEntities().at(7);
        ASSERT_NE(vis.primaryEquipNode, nullptr);
        const irr::core::vector3df hand = drawn(node, boneOrigin(node, "humr_point"));
        std::vector<irr::core::vector3df> local;
        auto world = worldVertices(vis.primaryEquipNode, &local);
        ASSERT_FALSE(world.empty());
        size_t tip = 0;
        for (size_t i = 1; i < local.size(); ++i)
            if (local[i].getLengthSQ() > local[tip].getLengthSQ()) tip = i;
        const irr::core::vector3df grip = vis.primaryEquipNode->getAbsoluteTransformation().getTranslation();
        SCOPED_TRACE(::testing::Message() << (c.player ? "player" : "npc") << " heading " << c.heading);
        EXPECT_LT(world[tip].Y, hand.Y - 1.5f) << "blade tip is not below the hand (B588: it stood UP)";
        EXPECT_LT(grip.getDistanceFrom(hand), 0.05f) << "the item origin (its grip) is not at R_POINT";
    }
}

TEST_F(HeldItemTest, LanternHangsBelowTheLeftHand) {
    for (bool player : {true, false}) {
        EQAnimatedMeshSceneNode* node = spawnIdle(7, player, player ? 337.5f : 112.5f, 0, /*IT48*/ 48);
        const EntityVisual& vis = ents_->getEntities().at(7);
        ASSERT_NE(vis.secondaryEquipNode, nullptr) << "no lantern node (B588: it was never drawn)";
        const irr::core::vector3df hand = drawn(node, boneOrigin(node, "huml_point"));
        auto world = worldVertices(vis.secondaryEquipNode);
        ASSERT_FALSE(world.empty());
        float mean = 0, lowest = 1e9f;
        for (const auto& p : world) { mean += p.Y; lowest = std::min(lowest, p.Y); }
        mean /= static_cast<float>(world.size());
        SCOPED_TRACE(player ? "player" : "npc");
        EXPECT_LT(mean, hand.Y) << "the lantern's mass is not below the hand";
        EXPECT_LT(lowest, hand.Y - 0.5f) << "the lantern does not hang";
    }
}

TEST_F(HeldItemTest, ShieldFrontFacesAwayFromTheBody) {
    // The front of IT202 is the face textured kiteshield-m.bmp (its back is
    // kiteshield-mback.bmp); its outward direction comes from the winding.
    WldLoader wld;
    ASSERT_TRUE(wld.parseFromArchive(eq_ + "gequip.s3d", "gequip.wld"));
    auto shield = resolveEquipmentActorGeometry(wld, "IT202");
    ASSERT_TRUE(shield);
    double f[3] = {0, 0, 0};
    for (const Triangle& t : shield->triangles) {
        if (t.textureIndex >= shield->textureNames().size() ||
            shield->textureNames()[t.textureIndex] != "kiteshield-m.bmp")
            continue;
        const Vertex3D& a = shield->vertices[t.v1];
        const Vertex3D& b = shield->vertices[t.v2];
        const Vertex3D& c = shield->vertices[t.v3];
        const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        f[0] += uy * vz - uz * vy; f[1] += uz * vx - ux * vz; f[2] += ux * vy - uy * vx;
    }
    ASSERT_GT(std::fabs(f[1]), 1.0) << "no front-textured face found";

    for (bool player : {false, true}) {
        EQAnimatedMeshSceneNode* node = spawnIdle(7, player, player ? 337.5f : 112.5f, 0, /*IT202*/ 202);
        const EntityVisual& vis = ents_->getEntities().at(7);
        ASSERT_NE(vis.secondaryEquipNode, nullptr);
        irr::core::vector3df front(static_cast<float>(f[0]), static_cast<float>(f[2]), static_cast<float>(f[1]));
        vis.secondaryEquipNode->getAbsoluteTransformation().rotateVect(front);
        irr::core::vector3df out = drawn(node, boneOrigin(node, "humshield_point")) - drawn(node, boneOrigin(node, "humch"));
        out.Y = 0;
        front.Y = 0;
        SCOPED_TRACE(player ? "player" : "npc");
        EXPECT_GT(front.normalize().dotProduct(out.normalize()), 0.5f) << "the shield's painted face points at the body";
    }
}

// ---------------------------------------------------------------------------
// 3. Facing: a stationary player is drawn at the renderer's heading.
// ---------------------------------------------------------------------------

TEST_F(HeldItemTest, StationaryPlayerIsDrawnAtTheRendererHeading) {
    // T466's pose: server heading 160 units = 112.5 deg. The self spawn row
    // carries that raw value; the renderer's own heading is 90 - 112.5 = -22.5
    // deg = 480 units (ZoneProcessPlayerProfile's load-bearing flip).
    constexpr float kServerDeg = 112.5f;
    constexpr float kRendererHeading512 = 480.0f;

    // What every other client's view of that server heading looks like.
    EQAnimatedMeshSceneNode* npc = spawnIdle(8, false, kServerDeg, 0, 0);
    // Same animation clock as the player below, so the idle pose matches.
    for (int t = 0; t < 3; ++t) npc->OnAnimate(400 + t * 16);
    npc->updateAbsolutePosition();
    const irr::core::vector3df npcR = drawn(npc, boneOrigin(npc, "humr_point")) - npc->getAbsolutePosition();
    const irr::core::vector3df npcL = drawn(npc, boneOrigin(npc, "huml_point")) - npc->getAbsolutePosition();

    // The player, built the way zone-in builds it: from the self row's raw heading.
    EQAnimatedMeshSceneNode* node = spawnIdle(7, true, kServerDeg, 0, 0);
    // The frame loop's sync (FrameComponent::processFrameSimulation).
    ents_->syncLocalPlayerHeading(kRendererHeading512);
    for (int t = 0; t < 3; ++t) node->OnAnimate(400 + t * 16);
    node->updateAbsolutePosition();
    const irr::core::vector3df r = drawn(node, boneOrigin(node, "humr_point"));
    const irr::core::vector3df l = drawn(node, boneOrigin(node, "huml_point"));

    EXPECT_LT((r - node->getAbsolutePosition()).getDistanceFrom(npcR), 0.02f)
        << "the stationary player's right hand is not where an observer's view puts it";
    EXPECT_LT((l - node->getAbsolutePosition()).getDistanceFrom(npcL), 0.02f);

    // And the Chase-equivalent camera (rig azimuth 0, behind the renderer
    // heading) sees the BACK: the right hand on screen RIGHT.
    const CameraRigPose pose = computeCameraRigPose(CameraRig{0.0f, 6.0f, 12.0f, 7.0f}, 100.0f, 200.0f, 0.0f,
                                                    kRendererHeading512);
    const irr::core::vector3df cam(pose.camX, pose.camZ, pose.camY);
    const irr::core::vector3df look = (irr::core::vector3df(pose.targetX, pose.targetZ, pose.targetY) - cam).normalize();
    const irr::core::vector3df screenRight = irr::core::vector3df(0, 1, 0).crossProduct(look).normalize();
    EXPECT_GT((r - cam).dotProduct(screenRight), (l - cam).dotProduct(screenRight))
        << "B587: the camera behind the player sees his face";

    // A later self echo (server heading, unflipped) must not turn him back.
    ents_->updateEntity(7, 100.0f, 200.0f, 3.0f, kServerDeg, 0, 0, 0, 0);
    ents_->flushPendingUpdatesForAnimations();
    ents_->syncLocalPlayerHeading(kRendererHeading512);
    EXPECT_NEAR(node->getRotation().Y, -(kRendererHeading512 * 360.0f / 512.0f), 0.01f);
}

// ---------------------------------------------------------------------------
// 4. T476 (B660): a SKINNED item is placed by its skeleton's rest pose.
//
// 266 of the index's items (census: review/T476/census/) are shaped
//   IT4_ACTORDEF (0x14) -> 0x11 -> IT4_HS_DEF (0x10, flags 0x202, no bone mesh)
//   IT4_DMSPRITEDEF (0x36): centre 0, bounds 0, vertex pieces naming bones
// so each vertex run is stored in the frame of the bone its piece names, and
// only the skeleton's rest transforms put the runs together.
// ---------------------------------------------------------------------------

struct SkinnedItem {
    std::shared_ptr<ZoneGeometry> raw;
    std::shared_ptr<SkeletonTrack> track;
};

// The actor's skeleton (0x14 -> 0x11 -> 0x10) and its DMSPRITEDEF by name.
SkinnedItem findSkinnedItem(const WldLoader& wld, const std::string& actor) {
    SkinnedItem out;
    auto def = wld.getObjectDefs().find(actor);
    if (def == wld.getObjectDefs().end()) return out;
    for (uint32_t ref : def->second.meshRefs) {
        auto sr = wld.getSkeletonRefs().find(ref);
        if (sr == wld.getSkeletonRefs().end()) continue;
        auto tr = wld.getSkeletonTracks().find(sr->second);
        if (tr != wld.getSkeletonTracks().end()) out.track = tr->second;
    }
    const std::string want = actor + "_DMSPRITEDEF";
    for (const auto& g : wld.getGeometries()) {
        if (!g || g->vertexPieces.empty()) continue;
        std::string up = g->name;
        for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (up == want) out.raw = g;
    }
    return out;
}

// Each bone's accumulated rest transform through the file's own PARENT table
// (the loader walks children instead), T * R * S per bone.
std::vector<BoneMat4> restWorldByParentTable(const SkeletonTrack& t) {
    const size_t n = t.allBones.size();
    std::vector<BoneMat4> world(n, BoneMat4::identity());
    std::vector<int> done(n, 0);
    std::function<const BoneMat4&(size_t, int)> get = [&](size_t i, int depth) -> const BoneMat4& {
        if (done[i] || depth > 256) return world[i];
        BoneMat4 local = BoneMat4::identity();
        if (const auto& o = t.allBones[i]->orientation) {
            local = BoneMat4::translate(o->shiftX, o->shiftY, o->shiftZ) *
                    BoneMat4::fromQuaternion(o->quatX, o->quatY, o->quatZ, o->quatW) *
                    BoneMat4::scale(o->scale > 0.0f ? o->scale : 1.0f);
        }
        const int p = i < t.parentIndices.size() ? t.parentIndices[i] : -1;
        world[i] = (p >= 0 && static_cast<size_t>(p) < n) ? get(static_cast<size_t>(p), depth + 1) * local : local;
        done[i] = 1;
        return world[i];
    };
    for (size_t i = 0; i < n; ++i) get(i, 0);
    return world;
}

struct SkinCheck {
    size_t vertices = 0;
    float maxError = 0.0f;  // resolved vs expected
    float maxMove = 0.0f;   // expected vs the raw bone-local vertex
};

// Compares what resolveEquipmentActorGeometry() returns with the raw mesh
// skinned by the rest pose. Returns false when the actor is not that shape.
bool checkSkinnedItem(const WldLoader& wld, const std::string& actor, SkinCheck& out) {
    const SkinnedItem item = findSkinnedItem(wld, actor);
    if (!item.raw || !item.track) return false;
    size_t pieceVerts = 0;
    for (const VertexPiece& p : item.raw->vertexPieces) {
        if (p.boneIndex >= item.track->allBones.size()) return false;
        pieceVerts += p.count;
    }
    if (pieceVerts != item.raw->vertices.size()) return false;
    const std::vector<BoneMat4> world = restWorldByParentTable(*item.track);
    auto resolved = resolveEquipmentActorGeometry(wld, actor);
    out.vertices = item.raw->vertices.size();
    if (!resolved || resolved->vertices.size() != item.raw->vertices.size()) {
        out.maxError = 1e9f;
        return true;
    }
    size_t v = 0;
    for (const VertexPiece& p : item.raw->vertexPieces) {
        for (int k = 0; k < p.count; ++k, ++v) {
            const Vertex3D& r = item.raw->vertices[v];
            const float px = r.x + item.raw->centerX, py = r.y + item.raw->centerY, pz = r.z + item.raw->centerZ;
            const V3 e = boneXf(world[p.boneIndex], px, py, pz);
            const Vertex3D& g = resolved->vertices[v];
            out.maxError = std::max(out.maxError, std::fabs(g.x - e.x) + std::fabs(g.y - e.y) + std::fabs(g.z - e.z));
            out.maxMove = std::max(out.maxMove, std::fabs(px - e.x) + std::fabs(py - e.y) + std::fabs(pz - e.z));
        }
    }
    return true;
}

TEST_F(HeldItemTest, SkinnedItemIsPlacedByItsSkeletonRestPose) {
    WldLoader wld;
    ASSERT_TRUE(wld.parseFromArchive(eq_ + "gequip.s3d", "gequip.wld"));
    SkinCheck c;
    ASSERT_TRUE(checkSkinnedItem(wld, "IT4", c)) << "IT4 is no longer a skinned item (0x11 skeleton + vertex pieces)";
    ASSERT_GT(c.maxMove, 0.1f) << "IT4's rest pose moves nothing, so this test proves nothing";
    EXPECT_LT(c.maxError, 1e-3f) << "B660: IT4's vertices are left in their bones' local frames";
}

TEST_F(HeldItemTest, EverySkinnedItemIsPlacedByItsSkeletonRestPose) {
    int skinned = 0;
    std::vector<std::string> wrong;
    for (const auto& archive : kEquipArchives) {
        PfsArchive pfs;
        if (!pfs.open(eq_ + archive)) continue;
        std::vector<std::string> wlds;
        pfs.getFilenames(".wld", wlds);
        for (const auto& wldName : wlds) {
            WldLoader wld;
            if (!wld.parseFromArchive(eq_ + archive, wldName)) continue;
            for (const auto& [name, def] : wld.getObjectDefs()) {
                (void)def;
                if (name.rfind("IT", 0) != 0) continue;
                SkinCheck c;
                if (!checkSkinnedItem(wld, name, c)) continue;
                ++skinned;
                if (c.maxError >= 1e-3f) wrong.push_back(archive + ":" + name);
            }
        }
    }
    std::ostringstream list;
    for (size_t i = 0; i < wrong.size() && i < 20; ++i) list << " " << wrong[i];
    EXPECT_GT(skinned, 200) << "the census found too few skinned items to mean anything";
    EXPECT_TRUE(wrong.empty()) << wrong.size() << " of " << skinned
                               << " skinned items ignore their skeleton's rest pose:" << list.str();
}

TEST(HeldItemFrameOrder, PlayerHeadingIsSyncedAfterUpdatesAndBeforeEquipment) {
    std::ifstream f(std::string(WILLEQ_REPO_ROOT) + "/src/client/graphics/frame/renderer_frame_input.cpp");
    ASSERT_TRUE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();
    const size_t flush = src.find("ctx_.entityRenderer->flushPendingUpdatesForAnimations();");
    const size_t sync = src.find("ctx_.entityRenderer->syncLocalPlayerHeading(ctx_.playerHeading);");
    const size_t state = src.find("ctx_.entityRenderer->updateMainThreadEntityState(deltaTime);");
    ASSERT_NE(flush, std::string::npos);
    ASSERT_NE(sync, std::string::npos) << "the frame loop no longer syncs the player's heading (B587)";
    ASSERT_NE(state, std::string::npos);
    EXPECT_LT(flush, sync) << "a self echo flushed after the sync would turn the player for a frame";
    EXPECT_LT(sync, state) << "equipment would be placed against the stale heading";
}

}  // namespace
