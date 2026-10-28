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
 *     constant correction and the mesh centre added (see bone_attachment.h
 *     computeHeldItemTransform() / placeHeldItem()). T485 chose the correction
 *     by SEARCH against the official client (review/T478/oracle, README
 *     section 4) and these pin the oracle facts, not an argument: a one-handed
 *     blade points FORWARD from the fist and roughly level, a two-handed blade
 *     is level in front of the waist with its crossguard vertical, an arrow's
 *     long end hangs DOWN along the leg; the lantern hangs below the hand, and
 *     a shield's front-textured face (item +Y, which the correction keeps)
 *     points AWAY from the body.
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
#include "client/graphics/eq/bone_attachment.h"
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
const std::vector<std::string>& kEquipArchives = EquipmentModelLoader::indexedArchiveNames();   // T504c Leg 2f: one owner

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
    // T504c Leg 2f: the era set (GlobalLoad.txt's six gequip archives) holds
    // 459 models, 427 of them drawn through the chain (the 17-archive set had
    // over 700). The floor is a sanity check, not a target.
    EXPECT_GT(chainActors, 400) << "the census found too few actors to mean anything";
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

// T485 (B724, B725), as ruled by Will on the sheets, 2026-09-17: an item held
// at a point bone is gripped in a FIST, so its long axis is PERPENDICULAR to
// the FOREARM. Verbatim: "ALL held items are wrongly oriented in both. In the
// after (c20) sheet the item's long axis FOLLOWS the forearm; it must be
// PERPENDICULAR to the forearm, a fist grip, so c20 is off by 90 degrees. In
// the before (T472) sheet they are off by 180 degrees."
//
// These tests pin THAT, and not the earlier reading they replace ("the blade
// points body-forward and level"), for a reason that is measured rather than
// argued: R_POINT's -Z IS the forearm (humfo_r -> humfi_r), dot 1.001 at all 41
// frames of o01, so the angle between the forearm and a rigidly attached item
// is the SAME AT EVERY FRAME. The old target was not: body-forward swung 50 deg
// over the same loop, and at the bent-elbow frame the old sheets were captured
// at, body-forward and the forearm nearly coincide -- which is exactly how a
// correction that lays the blade ALONG the forearm came to score well against
// it. Nothing here depends on the fixture's OnAnimate schedule any more.
//
// The long axis is the vertex farthest from the item's origin (its grip).
// Blades (IT10649, IT10004) are +X_item, so they must come out perpendicular;
// the arrow IT10 is -Z_item, which the same correction sends ALONG the forearm,
// tip at the fingers -- which is what the official client shows (its idle hangs
// the arm at the side and the arrow runs from the hand at the hip down past the
// knee, 13 deg off vertical: review/T478/oracle/T478_oracle_held_IT10_idle_Tether_noui_root.png).
// Measured for the shipped identity correction at o01 frame 25, fable's
// arm-hanging frame (review/T485/data/candidates.tsv): blade 88.9 deg to the
// forearm, 2H 89.0, arrow 1.2.
namespace {
struct LongAxis {
    irr::core::vector3df dir;      // unit, world (Irrlicht)
    irr::core::vector3df tip;      // world
    irr::core::vector3df grip;     // world
};

LongAxis longAxisOf(irr::scene::IMeshSceneNode* equip) {
    std::vector<irr::core::vector3df> local;
    irr::scene::IMesh* mesh = equip->getMesh();
    irr::core::vector3df tipLocal;
    for (irr::u32 b = 0; mesh && b < mesh->getMeshBufferCount(); ++b) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(b);
        for (irr::u32 i = 0; i < buf->getVertexCount(); ++i)
            if (buf->getPosition(i).getLengthSQ() > tipLocal.getLengthSQ()) tipLocal = buf->getPosition(i);
    }
    LongAxis out;
    out.grip = equip->getAbsoluteTransformation().getTranslation();
    out.tip = tipLocal;
    equip->getAbsoluteTransformation().transformVect(out.tip);
    out.dir = (out.tip - out.grip).normalize();
    return out;
}

// The RIGHT FOREARM, elbow to fingers, in the same drawn world frame the item's
// long axis is measured in. This is what perpendicularity is scored against.
irr::core::vector3df forearmAxis(EQAnimatedMeshSceneNode* node, V3 elbow, V3 fingers) {
    return (drawn(node, fingers) - drawn(node, elbow)).normalize();
}

// Degrees between two unit vectors.
float angleDeg(const irr::core::vector3df& a, const irr::core::vector3df& b) {
    const float d = std::max(-1.0f, std::min(1.0f, a.dotProduct(b)));
    return std::acos(d) * 180.0f / 3.14159265f;
}

}  // namespace

TEST_F(HeldItemTest, OneHandedBladeIsPerpendicularToTheForearmAtIdle) {
    // Set A, T478: Short Sword IT10649, in the right fist. Will's ruling: the
    // blade crosses the fist, so it is PERPENDICULAR to the forearm. fable
    // before T472 stood it up the forearm; T472 hung it backwards; T485's first
    // pick (c20) laid it ALONG the forearm, 1.2 deg off -- which is what Will
    // saw and called "off by 90 degrees".
    struct Case { bool player; float heading; };
    for (Case c : {Case{true, 0.0f}, Case{true, 90.0f}, Case{true, 337.5f}, Case{false, 0.0f}, Case{false, 112.5f}}) {
        EQAnimatedMeshSceneNode* node = spawnIdle(7, c.player, c.heading, /*IT10649 short sword*/ 10649, 0);
        const EntityVisual& vis = ents_->getEntities().at(7);
        ASSERT_NE(vis.primaryEquipNode, nullptr);
        const irr::core::vector3df hand = drawn(node, boneOrigin(node, "humr_point"));
        const LongAxis blade = longAxisOf(vis.primaryEquipNode);
        const irr::core::vector3df forearm =
            forearmAxis(node, boneOrigin(node, "humfo_r"), boneOrigin(node, "humfi_r"));
        SCOPED_TRACE(::testing::Message() << (c.player ? "player" : "npc") << " heading " << c.heading);
        EXPECT_NEAR(angleDeg(blade.dir, forearm), 90.0f, 15.0f)
            << "the blade is not perpendicular to the forearm -- it is not held in a fist (B725). "
               "1 deg means it lies ALONG the forearm (T485's c20), 180 means it points back up it";
        EXPECT_LT(blade.grip.getDistanceFrom(hand), 0.05f) << "the item origin (its grip) is not at R_POINT";
    }
}

TEST_F(HeldItemTest, TwoHandedBladeIsPerpendicularToTheForearmAndItsCrossguardRunsAlongIt) {
    // Set C, T478: Spiny Two Handed Sword IT10004. Same fist grip as set A --
    // the blade crosses the fist -- and the crossguard (item Z, +/-0.68 against
    // +/-0.11 in Y, T476 census) runs the other way, along the forearm, which is
    // what puts it VERTICAL when the arm hangs: the official set C frames
    // (review/T485/oracle_crops/C_user2_zoom.png) show a vertical crossguard at
    // the belt. Measured for the shipped identity at frame 25: itemZ_up 0.917.
    for (bool player : {true, false}) {
        EQAnimatedMeshSceneNode* node = spawnIdle(7, player, player ? 337.5f : 112.5f, /*IT10004*/ 10004, 0);
        const EntityVisual& vis = ents_->getEntities().at(7);
        ASSERT_NE(vis.primaryEquipNode, nullptr);
        const LongAxis blade = longAxisOf(vis.primaryEquipNode);
        irr::core::vector3df crossguard(0, 1, 0);  // item EQ Z is Irrlicht Y
        vis.primaryEquipNode->getAbsoluteTransformation().rotateVect(crossguard);
        crossguard.normalize();
        const irr::core::vector3df forearm =
            forearmAxis(node, boneOrigin(node, "humfo_r"), boneOrigin(node, "humfi_r"));
        SCOPED_TRACE(player ? "player" : "npc");
        EXPECT_NEAR(angleDeg(blade.dir, forearm), 90.0f, 15.0f)
            << "the 2H blade is not perpendicular to the forearm (B724)";
        // A crossguard is a BAR: it runs along the forearm AXIS, and which of
        // its two ends is which carries no meaning. Measured under the shipped
        // identity: 178.8 deg, i.e. the bar lies on the forearm line pointing
        // back toward the elbow. What would be wrong is 90 -- a crossguard
        // sticking out sideways, which is what T485's first pick drew.
        const float guard = angleDeg(crossguard, forearm);
        EXPECT_LT(std::min(guard, 180.0f - guard), 15.0f)
            << "the crossguard does not lie along the forearm axis, so it will not stand vertical "
               "when the arm hangs (B724)";
    }
}

TEST_F(HeldItemTest, ArrowRunsAlongTheForearmPastTheFingers) {
    // IT10 (an arrow; no item wears it, T478 used #wc 7 10). Its long axis is
    // -Z_item, 3.35 long -- a DIFFERENT item axis from a blade's +X (B661) --
    // and the one correction therefore sends it the other way: ALONG the
    // forearm, out past the fingers. That is what the official client shows,
    // and it is why the arrow looks like it "hangs down the leg" there: the
    // official idle hangs the arm, so along-the-forearm IS down the leg
    // (review/T478/oracle/T478_oracle_held_IT10_idle_Tether_noui_root.png, the
    // arrow 13 deg off vertical from the hand at the hip to below the knee).
    // The sign is the half that matters: 1 deg is out past the fingers, 179
    // would be back up the arm through the elbow (T472 drew that).
    for (bool player : {true, false}) {
        EQAnimatedMeshSceneNode* node = spawnIdle(7, player, player ? 337.5f : 112.5f, /*IT10*/ 10, 0);
        const EntityVisual& vis = ents_->getEntities().at(7);
        ASSERT_NE(vis.primaryEquipNode, nullptr);
        const irr::core::vector3df hand = drawn(node, boneOrigin(node, "humr_point"));
        const LongAxis arrow = longAxisOf(vis.primaryEquipNode);
        const irr::core::vector3df forearm =
            forearmAxis(node, boneOrigin(node, "humfo_r"), boneOrigin(node, "humfi_r"));
        SCOPED_TRACE(player ? "player" : "npc");
        EXPECT_LT(angleDeg(arrow.dir, forearm), 15.0f)
            << "the arrow does not continue the forearm past the fingers (B725); "
               "near 180 means it runs back up the arm, near 90 that it sticks out sideways";
        EXPECT_GT(arrow.tip.getDistanceFrom(hand), 2.5f) << "the arrow's long end is not out beyond the hand";
    }
}

TEST_F(HeldItemTest, LanternHangsDownFromTheLeftHand) {
    // T472 pinned this as "the mean vertex is below the hand", which is a weak
    // proxy: MEASURED T485, the lantern's own long axis pointed straight UP
    // under T472's correction (up +0.964 at the arm-hanging frame) and BACKWARD
    // under T485's first pick (-0.93 forward), and that mean-vertex assertion
    // passed for both -- it was green on a lantern drawn upside-down, and under
    // the correct one it went red by 0.011 units. So it is replaced here.
    //
    // The pin is the angle to the LEFT forearm, for the same reason as the
    // blades: item and forearm are both fixed in the bone frame, so it is the
    // same at every frame of the loop, whereas "points down" is not -- at the
    // fixture's bent-elbow frame the correctly-placed lantern reads only -0.574
    // in Y, at the arm-hanging frame -0.994. IT48 is a compact mesh whose
    // farthest vertex is a corner, not an axis, so the angle is 27.7 rather than
    // 0 or 90; what matters is that it separates (T472 150, T485's first pick
    // 118).
    for (bool player : {true, false}) {
        EQAnimatedMeshSceneNode* node = spawnIdle(7, player, player ? 337.5f : 112.5f, 0, /*IT48*/ 48);
        const EntityVisual& vis = ents_->getEntities().at(7);
        ASSERT_NE(vis.secondaryEquipNode, nullptr) << "no lantern node (B588: it was never drawn)";
        const irr::core::vector3df hand = drawn(node, boneOrigin(node, "huml_point"));
        const LongAxis lantern = longAxisOf(vis.secondaryEquipNode);
        auto world = worldVertices(vis.secondaryEquipNode);
        ASSERT_FALSE(world.empty());
        float lowest = 1e9f;
        for (const auto& p : world) lowest = std::min(lowest, p.Y);
        SCOPED_TRACE(player ? "player" : "npc");
        const irr::core::vector3df lfore =
            forearmAxis(node, boneOrigin(node, "humfo_l"), boneOrigin(node, "humfi_l"));
        EXPECT_NEAR(angleDeg(lantern.dir, lfore), 27.7f, 15.0f)
            << "the lantern is not oriented on the left forearm the way the shipped correction puts it "
               "(T472 drew it at 150 deg -- upside-down; T485's first pick at 118 deg -- on its side)";
        EXPECT_LT(lowest, hand.Y - 0.5f) << "the lantern does not hang";
    }
}

TEST_F(HeldItemTest, ShieldFrontFacesAwayFromTheBody) {
    // The front of IT202 is the face textured kiteshield-m.bmp (its back is
    // kiteshield-mback.bmp); its outward direction comes from the winding.
    // This is a DATA pin, not an oracle one: T478's set B frames show no shield
    // at all (B723, contaminated inventory rows), so the official client has
    // not been seen holding one. T485's correction keeps the item's Y, which
    // is why this pin survives the search unchanged.
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
    // B1172: the NPC node carries the client's actor scale (a human at its
    // default size 6 -> 1.2, actor_scale.h) and the player node draws with a
    // translation-only matrix (unscaled), so the facing is compared in the
    // model's own units: the NPC's offsets divided by its node scale.
    const float npcScale = npc->getScale().X;
    const irr::core::vector3df npcR =
        (drawn(npc, boneOrigin(npc, "humr_point")) - npc->getAbsolutePosition()) / npcScale;
    const irr::core::vector3df npcL =
        (drawn(npc, boneOrigin(npc, "huml_point")) - npc->getAbsolutePosition()) / npcScale;

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
    // T504c Leg 2f: the era set has 19 skinned items (the Luclin archives held most).
    EXPECT_GT(skinned, 10) << "the census found too few skinned items to mean anything";
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

// ---------------------------------------------------------------------------
// 5. B1065 (#385): every drawn entity's held items are placed, every frame.
// ---------------------------------------------------------------------------

namespace {

// The held item's drawn frame against where the bone says it must be: the
// body's absolute transform * computeHeldItemTransform(bone). The origin must
// coincide; each item axis must point the same way (placeHeldItem() carries the
// rotation only, so the bone's own scale -- 1.11 on the HUM skeleton -- is not
// part of the comparison).
void expectHeldAtBone(EQAnimatedMeshSceneNode* node, irr::scene::IMeshSceneNode* item, const BoneMat4& bone) {
    node->updateAbsolutePosition();
    item->updateAbsolutePosition();
    const irr::core::matrix4 want = node->getAbsoluteTransformation() * computeHeldItemTransform(bone);
    irr::core::vector3df gotO(0, 0, 0), expO(0, 0, 0);
    item->getAbsoluteTransformation().transformVect(gotO);
    want.transformVect(expO);
    EXPECT_LT(gotO.getDistanceFrom(expO), 1e-3f) << "the item's origin is not on the bone";
    for (const irr::core::vector3df p : {irr::core::vector3df(1, 0, 0), irr::core::vector3df(0, 1, 0),
                                         irr::core::vector3df(0, 0, 1)}) {
        irr::core::vector3df got = p, exp = p;
        item->getAbsoluteTransformation().transformVect(got);
        want.transformVect(exp);
        got = (got - gotO).normalize();
        exp = (exp - expO).normalize();
        EXPECT_GT(got.dotProduct(exp), 0.9999f) << "item axis (" << p.X << "," << p.Y << "," << p.Z << ")";
    }
}

}  // namespace

TEST_F(HeldItemTest, StandingNpcsHeldItemsFollowTheirBonesEveryFrame) {
    // A guard that never moves gets no position update, so it never entered
    // EntityRenderer::activeEntities_ -- and before B1065 only that set's
    // members had their items placed. Its sword and shield were drawn at the
    // model origin, unrotated, while the body played its idle.
    EntityAppearance a;
    a.equipment[7] = 58;   // IT58, the Bastard Sword Lieutenant Arathur carries
    a.equipment[8] = 200;  // IT200, the Qeynos Kite Shield
    ents_->clearEntities();
    ASSERT_TRUE(ents_->createEntity(7, 1, "B1065", 100.0f, 200.0f, 3.0f, 112.5f, false, 0, a, true));
    ents_->setEntityAnimation(7, "o01", true);
    auto* node = static_cast<EQAnimatedMeshSceneNode*>(ents_->getEntities().at(7).animatedNode);
    ASSERT_NE(node, nullptr);
    const EntityVisual& vis = ents_->getEntities().at(7);
    ASSERT_NE(vis.primaryEquipNode, nullptr);
    ASSERT_NE(vis.secondaryEquipNode, nullptr);
    auto sk = node->getAnimator().getSkeleton();
    const int shieldBone = sk->getBoneIndex("humshield_point");
    const int rightBone = sk->getBoneIndex("humr_point");
    ASSERT_GE(shieldBone, 0);
    ASSERT_GE(rightBone, 0);

    irr::core::vector3df shieldAt[2];
    for (int k = 0; k < 2; ++k) {
        // Two idle frames well apart: the item must be at the bone at each.
        for (int t = 0; t < 30; ++t) {
            node->OnAnimate(1000 + k * 1500 + t * 16);
            ents_->updateMainThreadEntityState(0.016f);
        }
        const auto& bones = node->getAnimator().getBoneStates();
        SCOPED_TRACE(k == 0 ? "first frame" : "second frame");
        expectHeldAtBone(node, vis.secondaryEquipNode, bones[shieldBone].worldTransform);
        expectHeldAtBone(node, vis.primaryEquipNode, bones[rightBone].worldTransform);
        shieldAt[k] = vis.secondaryEquipNode->getAbsolutePosition();
    }
    EXPECT_GT(shieldAt[0].getDistanceFrom(shieldAt[1]), 1e-3f)
        << "the idle moves the forearm, so the shield must move with it";
}

TEST_F(HeldItemTest, HeldItemsAreDrawnWithBackFaceCulling) {
    // IT200's BACK skin lies on the +Y side of its painted face; only culling
    // (which the official client does) shows the face from outside -- see
    // kHeldItemBackfaceCulling in bone_attachment.h.
    EXPECT_TRUE(kHeldItemBackfaceCulling);
    EntityAppearance a;
    a.equipment[7] = 58;
    a.equipment[8] = 200;
    ents_->clearEntities();
    ASSERT_TRUE(ents_->createEntity(7, 1, "B1065", 100.0f, 200.0f, 3.0f, 112.5f, false, 0, a, true));
    const EntityVisual& vis = ents_->getEntities().at(7);
    for (irr::scene::IMeshSceneNode* item : {vis.primaryEquipNode, vis.secondaryEquipNode}) {
        ASSERT_NE(item, nullptr);
        ASSERT_GT(item->getMaterialCount(), 0u);
        for (irr::u32 i = 0; i < item->getMaterialCount(); ++i) EXPECT_TRUE(item->getMaterial(i).BackfaceCulling);
    }
}

TEST_F(HeldItemTest, FallbackModelRaceHoldsItsItemsAtItsOwnPointBones) {
    // Race 71 (Qeynos Citizen, QCM) with only global_chr.s3d loaded is drawn
    // with its fallback HUM skeleton (config/race_models.json fallback_code):
    // the bones are "humr_point" / "humshield_point", never "QCM...". Before
    // B1065 the lookup used the race code only and placed nothing.
    EntityAppearance a;
    a.equipment[7] = 58;
    a.equipment[8] = 200;
    ents_->clearEntities();
    ASSERT_TRUE(ents_->createEntity(8, 71, "B1065", 100.0f, 200.0f, 3.0f, 112.5f, false, 0, a, true));
    ents_->setEntityAnimation(8, "o01", true);
    auto* node = static_cast<EQAnimatedMeshSceneNode*>(ents_->getEntities().at(8).animatedNode);
    ASSERT_NE(node, nullptr);
    auto sk = node->getAnimator().getSkeleton();
    ASSERT_TRUE(sk);
    if (sk->getBoneIndex("humshield_point") < 0) GTEST_SKIP() << "race 71 resolved to model '" << sk->modelCode << "'";
    for (int t = 0; t < 5; ++t) {
        node->OnAnimate(200 + t * 16);
        ents_->updateMainThreadEntityState(0.016f);
    }
    const EntityVisual& vis = ents_->getEntities().at(8);
    ASSERT_NE(vis.secondaryEquipNode, nullptr);
    const auto& bones = node->getAnimator().getBoneStates();
    expectHeldAtBone(node, vis.secondaryEquipNode, bones[sk->getBoneIndex("humshield_point")].worldTransform);
    expectHeldAtBone(node, vis.primaryEquipNode, bones[sk->getBoneIndex("humr_point")].worldTransform);
}

