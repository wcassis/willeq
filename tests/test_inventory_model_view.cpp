/**
 * Headless coverage for the character-model loading/animation layer that
 * the deleted inventory-window paperdoll (WindowManager/InventoryWindow/
 * CharacterModelView, removed in 0f25cb57) used to render through.
 *
 * T54: this file used to also hold InventoryModelViewTest, a fixture whose
 * six TEST_F cases were unconditionally GTEST_SKIP()'d ("WindowManager has
 * been deleted") — not environment-gated, the subject itself was gone. Its
 * ~150 lines of live-EQEmu-server/DISPLAY plumbing (config loader,
 * createClientWithGraphics(), waitForZoneReady(), processFrames()) were
 * unreachable dead weight and have been deleted along with it. The three
 * salvageable assertions (textures applied, loads for race/gender, and the
 * animation-coherence case that already existed below) were ported into
 * RaceModelAnimationHeadlessTest, which needs neither a server nor a
 * display. The view-only assertions (render-to-texture, survives zoning)
 * had no production code behind them at all — that's not a test gap, it's
 * a feature that was removed and never rebuilt; see refactor/deferred.md's
 * "inventory character preview" entry.
 *
 * This file is now purely a headless model-loading/animation suite: EDT_NULL
 * Irrlicht device, RaceModelLoader, EQAnimatedMeshSceneNode. No EQEmu
 * server, no DISPLAY, no eq.h dependency.
 */

#include <gtest/gtest.h>
#include <fstream>
#include <chrono>
#include <thread>
#include <limits>
#include <cmath>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/entity_renderer.h"  // EntityAppearance
#include "client/graphics/first_person_camera.h"  // T300: the near-plane derivation
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/willeq_database.h"
#endif

// ============================================================================
// P12B: Headless animation-coherence coverage
//
// The tests above need a live EQEmu server plus the paperdoll UI
// (WindowManager/CharacterModelView), which was deleted — genuinely
// un-runnable in CI without a UI rewrite (out of scope for this unit).
//
// This fixture exercises the SAME underlying mechanism the paperdoll view
// renders through — RaceModelLoader::createAnimatedNode() and
// EQAnimatedMeshSceneNode's per-instance OnAnimate()/applyAnimation() —
// using an EDT_NULL Irrlicht device (no window, no DISPLAY, no server).
// It asserts the class of regression P12b found and fixed (gles2_zone_
// benchmark's animated entities silently freezing at their raw, unposed,
// per-bone-local vertex layout — see refactor/P12b_benchmark_node_wiring.md):
// after playing an animation and stepping OnAnimate across several frames,
// the per-instance mesh's vertex data must actually move, and stay within a
// plausible bounding-box envelope for a humanoid model (not collapse to a
// degenerate point, not explode).
// ============================================================================
#ifdef EQT_HAS_GRAPHICS
class RaceModelAnimationHeadlessTest : public ::testing::Test {
protected:
    std::string eqClientPath_;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::io::IFileSystem* fs_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;
    EQT::WilleqDatabase willeqDb_;

    void SetUp() override {
        // T270: race codes come from the db and nowhere else (the pre-JSON
        // hardcoded switch getRaceCode() used to fall back to is deleted), so
        // this fixture has to open the shipped db exactly like
        // Application::run() does -- without it every race is "unknown" and
        // createAnimatedNode() returns null for all of them.
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) {
            GTEST_SKIP() << "no shipped db at " << dbPath
                         << " - race lookups need it";
        }
        ASSERT_TRUE(willeqDb_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&willeqDb_);

        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        eqClientPath_ = envPath ? envPath : EQ_CLIENT_DATA_PATH;
        if (!eqClientPath_.empty() && eqClientPath_.back() != '/') {
            eqClientPath_ += '/';
        }
        if (!fileExists(eqClientPath_ + "global_chr.s3d")) {
            GTEST_SKIP() << "global_chr.s3d not found at " << eqClientPath_
                         << " - required for headless animation coverage";
        }

        // EDT_NULL: no window, no GPU, no DISPLAY needed — matches the
        // pattern in test_door_loading.cpp's DoorManagerGraphicsTest.
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        }
        smgr_ = device_->getSceneManager();
        driver_ = device_->getVideoDriver();
        fs_ = device_->getFileSystem();

        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(smgr_, driver_, fs_);
        loader_->setClientPath(eqClientPath_);

        // T54: without a constrained texture cache, ZoneMeshBuilder::
        // loadTextureFromBMP() has nowhere to decode/upload BMP bytes and
        // every material ends up textureless (LOG_FATAL "no constrained
        // cache for ..." — zone_geometry.cpp:273). This is a real
        // production wiring requirement, not test-only plumbing — see
        // tools/hwcaps/p50_mask_plumbing_probe.cpp for the same
        // ConstrainedTextureCache+setDriver+setConstrainedTextureCache
        // idiom used there.
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(driver_);
        // Two separate wiring points, both required (found by this unit —
        // RaceModelLoader::setConstrainedTextureCache() only feeds its own
        // P17 isLiveTexture() defensive check; the ZoneMeshBuilder that
        // actually decodes/uploads BMP bytes in buildMeshFromGeometry() /
        // buildTexturedMesh() needs its OWN setConstrainedTextureCache()
        // call. tools/hwcaps/p50_mask_plumbing_probe.cpp wires both too —
        // this is the established idiom, not a workaround.
        loader_->setConstrainedTextureCache(textureCache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(textureCache_.get());
    }

    void TearDown() override {
        loader_.reset();
        textureCache_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    bool fileExists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }
};

TEST_F(RaceModelAnimationHeadlessTest, ModelAnimatesProperly_Headless) {
    // Human male (1) and Barbarian male (2): two distinct skeletons/meshes,
    // matching the plan's "coherent walk/idle cycles across races" bar at a
    // scale that stays fast for CI.
    struct RaceCase { uint16_t raceId; uint8_t gender; const char* label; };
    const RaceCase races[] = {
        {1, 0, "HUM"},
        {2, 0, "BAM"},
    };

    for (const auto& rc : races) {
        SCOPED_TRACE(rc.label);

        auto* node = loader_->createAnimatedNode(rc.raceId, rc.gender,
                                                   smgr_->getRootSceneNode());
        if (!node) {
            // Some builds may not ship every race's chr data — skip that
            // race rather than fail the whole test on missing assets.
            std::cout << "  " << rc.label << ": createAnimatedNode() returned "
                         "null (race data unavailable) - skipping\n";
            continue;
        }
        ASSERT_NE(node->getInstanceMesh(), nullptr)
            << rc.label << ": animated node has no per-instance mesh";

        ASSERT_TRUE(node->playAnimation("l01", true) || node->playAnimation("o01", true))
            << rc.label << ": neither l01 (walk) nor o01 (idle) animation available";

        // Step OnAnimate across several synthetic frames, exactly like
        // production's drawAll()-driven idiom and the (now-fixed) benchmark's
        // OnAnimate(frame * 33.3ms) idiom.
        const irr::video::S3DVertex* firstBuf =
            static_cast<const irr::video::S3DVertex*>(
                node->getInstanceMesh()->getMeshBuffer(0)->getVertices());
        irr::core::vector3df vertexAtT0 = firstBuf[0].Pos;

        irr::u32 simTimeMs = 0;
        irr::core::aabbox3df bboxAtT[4];
        irr::core::vector3df vertexAtT[4];
        for (int f = 0; f < 4; ++f) {
            simTimeMs += 100;  // 100ms steps
            node->OnAnimate(simTimeMs);
            bboxAtT[f] = node->getBoundingBox();
            const irr::video::S3DVertex* buf =
                static_cast<const irr::video::S3DVertex*>(
                    node->getInstanceMesh()->getMeshBuffer(0)->getVertices());
            vertexAtT[f] = buf[0].Pos;
        }

        // 1) Not degenerate: a humanoid model's bounding box must have a
        //    plausible extent on every axis (the frozen-raw-mesh bug this
        //    guards produced a bbox that was either near-zero on some axis
        //    or wildly larger than a human, depending on race/pose — the
        //    P12b contact sheet in benchmarks/contact_sheets/p12b_animated/
        //    has visual references).
        for (int f = 0; f < 4; ++f) {
            auto ext = bboxAtT[f].getExtent();
            EXPECT_GT(ext.Y, 1.0f) << rc.label << " frame " << f
                << ": bounding-box height implausibly small (collapsed pose?)";
            EXPECT_LT(ext.Y, 20.0f) << rc.label << " frame " << f
                << ": bounding-box height implausibly large (exploded pose?)";
            EXPECT_LT(ext.X, 20.0f) << rc.label << " frame " << f
                << ": bounding-box width implausibly large (exploded pose?)";
            EXPECT_LT(ext.Z, 20.0f) << rc.label << " frame " << f
                << ": bounding-box depth implausibly large (exploded pose?)";
        }

        // 2) Actually animating: at least one of the sampled frames must
        //    show the tracked vertex having moved from both its bind-pose
        //    position AND from at least one other sampled frame. A node
        //    silently frozen at one pose (this unit's root cause, when a
        //    stale GPU snapshot was drawn instead of the live CPU vertex
        //    data) would show byte-identical positions across every frame.
        bool sawMovementFromBindPose = false;
        bool sawMovementBetweenFrames = false;
        for (int f = 0; f < 4; ++f) {
            if (vertexAtT[f].getDistanceFromSQ(vertexAtT0) > 1e-6f) {
                sawMovementFromBindPose = true;
            }
            if (f > 0 && vertexAtT[f].getDistanceFromSQ(vertexAtT[f - 1]) > 1e-6f) {
                sawMovementBetweenFrames = true;
            }
        }
        EXPECT_TRUE(sawMovementFromBindPose)
            << rc.label << ": tracked vertex never moved from its bind pose "
               "across 4 animated frames - model appears frozen";
        EXPECT_TRUE(sawMovementBetweenFrames)
            << rc.label << ": tracked vertex never moved between consecutive "
               "animated frames - model appears frozen mid-animation";
    }
}

// T54: ported from the deleted InventoryModelViewTest.ModelHasTexturesApplied.
// The original asserted the paperdoll VIEW had textures; there is no view
// left to assert on, so this asserts the same thing at the layer that's
// actually still alive — the animated node's own material/texture state,
// which is exactly what a paperdoll would read from if one existed.
TEST_F(RaceModelAnimationHeadlessTest, ModelHasTexturesApplied_Headless) {
    struct RaceCase { uint16_t raceId; uint8_t gender; const char* label; };
    const RaceCase races[] = {
        {1, 0, "HUM"},
        {2, 0, "BAM"},
    };

    for (const auto& rc : races) {
        SCOPED_TRACE(rc.label);

        auto* node = loader_->createAnimatedNode(rc.raceId, rc.gender,
                                                   smgr_->getRootSceneNode());
        if (!node) {
            std::cout << "  " << rc.label << ": createAnimatedNode() returned "
                         "null (race data unavailable) - skipping\n";
            continue;
        }

        ASSERT_GT(node->getMaterialCount(), 0u)
            << rc.label << ": animated node has no materials";

        bool sawNonNullTexture = false;
        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
            if (node->getMaterial(i).getTexture(0) != nullptr) {
                sawNonNullTexture = true;
                break;
            }
        }
        EXPECT_TRUE(sawNonNullTexture)
            << rc.label << ": no material on the animated node has a texture bound "
               "(model would render untextured)";
    }
}

// T54: ported from the salvageable half of the deleted
// InventoryModelViewTest.ModelViewInitializesAndLoadsCharacter. The original
// also asserted hasModelView()/isReady() — pure view predicates with no
// production code behind them anymore — which are dropped, not ported.
TEST_F(RaceModelAnimationHeadlessTest, LoadsForRaceAndGender_Headless) {
    struct RaceCase { uint16_t raceId; uint8_t gender; const char* label; };
    const RaceCase races[] = {
        {1, 0, "HUM male"},
        {1, 1, "HUM female"},
        {2, 0, "BAM male"},
        {8, 0, "DWM male"},
        {9999, 0, "unknown/invalid race (should fail gracefully)"},
    };

    for (const auto& rc : races) {
        SCOPED_TRACE(rc.label);
        auto* node = loader_->createAnimatedNode(rc.raceId, rc.gender,
                                                   smgr_->getRootSceneNode());
        if (rc.raceId == 9999) {
            // T270: race 330 used to stand in for "no model data" here, but
            // T265/T267 gave it a real row (FRM/FRF in globalpcfroglok_chr)
            // and it loads -- this fixture only kept passing because it had
            // no db open at all. 9999 has no race_models row, so its code is
            // empty by construction: assert the loader degrades to nullptr
            // rather than crashing, not that it succeeds.
            EXPECT_EQ(node, nullptr)
                << rc.label << ": expected graceful nullptr for a race with "
                   "no model data, got a node instead";
            continue;
        }
        if (!node) {
            std::cout << "  " << rc.label << ": createAnimatedNode() returned "
                         "null (race data unavailable) - skipping\n";
            continue;
        }
        EXPECT_NE(node->getInstanceMesh(), nullptr)
            << rc.label << ": animated node has no per-instance mesh";
    }
}

// T94: proves the negative model-lookup cache actually memoizes a failed
// search instead of silently doing nothing. T270: the race used here is 9999,
// which has no race_models row -- so getRaceCode() is empty by construction
// and preloadModelData()'s full search chain (zoneCharacters_, otherChrCaches_,
// the row's S3D, global_chr.s3d, global2-7_chr.s3d) fails deterministically
// regardless of which client data set is configured. It used to be race 330,
// which T265/T267 turned into a race that loads.
TEST_F(RaceModelAnimationHeadlessTest, NegativeModelCache_KnownMissingRacePersists_Headless) {
    const uint16_t raceId = 9999;
    const uint8_t gender = 0;
    const uint32_t key = (static_cast<uint32_t>(raceId) << 8) | gender;

    ASSERT_FALSE(loader_->isRaceModelKnownMissing(key))
        << "race=9999 must not already be in the negative cache before it has been searched even once "
           "(this loader instance is fresh per-test, see SetUp())";

    bool firstSearch = loader_->preloadModelData(raceId, gender);
    EXPECT_FALSE(firstSearch) << "race=9999 has no race_models row, so it has no model anywhere — "
                                  "the first full search must fail";

    EXPECT_TRUE(loader_->isRaceModelKnownMissing(key))
        << "T94: after preloadModelData() exhausts the full search chain and finds nothing, "
           "the (raceId, gender) pair must be memoized permanently — this is what lets "
           "loadZoneStep8a_BaseModelPrep and EntityPrepWorker::processRequest skip repeating "
           "the search (and its per-entity log line) for every subsequent entity of this race";

    // Repeat calls must remain false and the memo must stay set — this is
    // the exact contract Step8a/EntityPrepWorker depend on to keep skipping
    // the search for every later entity of this race/gender in the zone.
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(loader_->preloadModelData(raceId, gender))
            << "repeat call " << i << " must still report failure";
        EXPECT_TRUE(loader_->isRaceModelKnownMissing(key))
            << "repeat call " << i << " must not clear the negative memo";
    }
}

// T54: ported from the deleted InventoryModelViewTest.EquippedWeaponsShowOnModel.
//
// FINDING (not a weakened test — see this unit's plan, Step 1): the original
// test's premise ("equipped weapons show on model") cannot be ported as
// literally stated. RaceModelLoader::createAnimatedNodeWithEquipment() —
// the only equipment-aware entry point RaceModelLoader exposes — does NOT
// attach weapon geometry to the returned node. Read end-to-end
// (animated_mesh_creation.cpp:315-624): it only (a) selects a robe body
// variant/texture and (b) overrides body-part (chest/legs/feet/arms/
// wrist/hands/head) textures and tints from EntityAppearance::equipment[].
// EquipSlot::Primary/Secondary (indices 7/8, the weapon slots) are read
// only to detect robes (equipment[Chest]), never to attach a weapon mesh.
// Actual weapon attachment (primaryEquipNode/secondaryEquipNode) is built
// entirely inside EntityRenderer via a separate EquipmentModelLoader mesh
// lookup — a much heavier production dependency (full entity visual
// pipeline) out of this unit's territory and disproportionate to add here.
//
// So this test asserts what createAnimatedNodeWithEquipment() actually
// does and had zero coverage of before this unit: it succeeds and returns
// a fully-formed node when given a non-trivial EntityAppearance (a body
// equipment texture override), matching createAnimatedNode()'s baseline
// guarantees. The weapon-attachment gap is recorded on the board
// (refactor/deferred.md) rather than silently dropped.
TEST_F(RaceModelAnimationHeadlessTest, CreateAnimatedNodeWithEquipment_Headless) {
    EQT::Graphics::EntityAppearance appearance;
    appearance.equipment[static_cast<uint8_t>(EQT::Graphics::EquipSlot::Chest)] = 1;  // leather

    auto* node = loader_->createAnimatedNodeWithEquipment(
        /*raceId=*/1, /*gender=*/0, appearance, smgr_->getRootSceneNode());
    if (!node) {
        GTEST_SKIP() << "createAnimatedNodeWithEquipment returned null "
                         "(race data unavailable)";
    }

    EXPECT_NE(node->getInstanceMesh(), nullptr)
        << "equipped node has no per-instance mesh";
    EXPECT_GT(node->getMaterialCount(), 0u)
        << "equipped node has no materials";
}

// T300: the four tests that lived here -- FirstPersonArmFilter_PerRace_Headless,
// FirstPersonMode_IsPerNode_NotShared_Headless,
// FirstPersonArmFilter_FallbackWhenNoArmBones_Headless and
// ArmVsCameraGeometryMeasurement_Headless -- were DELETED with their subject.
// All four exercised T74's arm-bone index filter and T76's standoff search,
// which T300 removed: first person draws the ordinary, unfiltered player
// model and hides it with a large near clip plane (T299 measured that
// mechanism in two client versions). There is no filter left to verify per
// race, no per-node filter state to keep isolated, no zero-arm-bone fallback,
// and no pullback distance to search for. The measurement below replaces
// them with the question the new mechanism actually raises.
//
// HONEST LIMIT, stated once for everything in this file: none of these tests
// RASTERIZE. They measure geometry in model space. They cannot prove the body
// is invisible on screen, cannot prove a swing is visible, and cannot prove
// the clip happens at all -- the GPU is not involved. T296 shipped a viewmodel
// pass whose depth clear was silently compiled out and EVERY headless test
// still passed. The live video gate (T300 Leg C) is the only thing that can
// judge this feature.
TEST_F(RaceModelAnimationHeadlessTest, FirstPersonPitchClampKeepsTheBodyOutOfFrame_Headless) {
    // T300 post-Leg-A. The hiding mechanism is a DOWN-PITCH CLAMP, not a near
    // plane: Leg A measured the official client clipping nothing (geometry
    // draws at point-blank range) and its size sweep killed both the fixed and
    // the size-derived plane at once (at 5x scale, zero player geometry while
    // ground 34 units below still drew). The body hangs directly below the
    // eye, so a view that cannot look past straight down cannot contain it.
    //
    // What this measures, per race, against real S3D meshes: the down-pitch at
    // which the player's own geometry FIRST enters the frustum. The shipped
    // clamp (90 - vFOV/2) must sit below that angle.
    //
    // Scale invariance is the property that makes this mechanism right where a
    // plane was wrong, and it is asserted directly: the entry angle is
    // measured again at 5x scale (Leg A's #size 30 case) and must be
    // unchanged, because an angle from the eye does not depend on how big the
    // body is.
    using EQT::Graphics::firstPersonMaxDownPitchDegrees;
    using EQT::Graphics::firstPersonVerticalFovRadians;
    using EQT::Graphics::kFirstPersonHorizontalFovDegrees;

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kAspect = 4.0f / 3.0f;
    const float vFov = firstPersonVerticalFovRadians(kAspect);
    const float clampDeg = firstPersonMaxDownPitchDegrees(vFov);
    const float tanHalfV = std::tan(vFov * 0.5f);
    const float tanHalfH = std::tan(kFirstPersonHorizontalFovDegrees * 0.5f * kPi / 180.0f);
    // The world near plane -- first person clips exactly as third person does
    // (Leg A: the official client clips nothing).
    const float nearPlane = EQT::Graphics::kWorldNearPlaneUnits;

    struct RaceCase { uint16_t raceId; uint8_t gender; const char* label; };
    const RaceCase races[] = {
        {1, 0, "Human male"},        // the race Leg A measured the client on
        {1, 1, "Human female"},
        {2, 0, "Barbarian male"},
        {8, 0, "Dwarf male"},
        {6, 1, "Dark Elf female"},
        {9, 0, "Troll male"},
        {128, 0, "Iksar male"},
    };
    // AT REST (idle) is what the clamp has to hide. The ATTACK animations are
    // the opposite case -- a swing rotates the arm and weapon UP into frame,
    // and T299/Leg A both saw exactly that (the arm running off the frame's
    // bottom-right corner). Geometry in frame during a swing is the mechanism
    // working, not a failure, so the two are measured separately.
    const char* idleAnims[] = {"l01"};
    const char* attackAnims[] = {"c06", "c02", "c03", "c04", "c08"};

    std::cout << "\nT300 first-person down-pitch clamp = 90 - vFOV/2 = " << clampDeg
              << " deg (vFOV " << (vFov * 180.0f / kPi) << " at 4:3).\n";
    std::cout << "  race | scale | down-pitch at which the body first enters frame"
                 " (clamp must be BELOW this)\n";

    int racesCovered = 0;
    bool humanMaleCovered = false;
    for (const auto& rc : races) {
        SCOPED_TRACE(rc.label);
        auto* node = loader_->createAnimatedNode(rc.raceId, rc.gender,
                                                 smgr_->getRootSceneNode());
        if (!node) {
            std::cout << "  " << rc.label << ": createAnimatedNode() returned null "
                         "(race data unavailable) - skipping\n";
            continue;
        }
        EQT::Graphics::EQAnimatedMesh* eqMesh = node->getEQMesh();
        if (!eqMesh) { node->remove(); continue; }
        const float eye = eqMesh->getBoundingBox().MaxEdge.Y;
        if (!(eye > 0.0f)) { node->remove(); continue; }

        // MUST be set before animating: the player's node bakes its rotation
        // into the vertices (applyAnimation()), and only the BAKED mesh is in
        // the world frame the camera looks along. Measuring the unbaked model
        // frame answers a different question -- the raw mesh's +X is the
        // character's forward (T298 section 3), not +Z. With the node at
        // rotation 0 the bake is RotY(-90), which maps character-forward +X
        // onto world +Z, i.e. exactly production's arrangement at heading 0.
        node->setIsPlayerNode(true);

        // Smallest down-pitch at which ANY body vertex falls inside the
        // frustum, over idle and every attack animation. `scale` multiplies
        // the whole body AND the eye height, exactly as a shrink/grow would.
        auto entryPitchDegrees = [&](float scale, const char* const* codes, size_t nCodes) {
            float entry = 90.0f;  // nothing found -> the body never enters
            for (size_t ci = 0; ci < nCodes; ++ci) {
                const char* code = codes[ci];
                node->playAnimation(code, true);
                for (int t = 0; t <= 400; t += 50) {
                    node->OnAnimate(static_cast<irr::u32>(t));
                    irr::scene::SMesh* mesh = node->getInstanceMesh();
                    if (!mesh) continue;
                    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(b);
                        if (!mb) continue;
                        const irr::u32 vcount = mb->getVertexCount();
                        for (irr::u32 v = 0; v < vcount; ++v) {
                            const irr::core::vector3df& raw = mb->getPosition(v);
                            // Relative to the eye, at this scale.
                            const float px = raw.X * scale;
                            const float py = (raw.Y - eye) * scale;
                            const float pz = raw.Z * scale;
                            // Sweep down-pitch; camera looks along +Z at 0.
                            for (float deg = 0.0f; deg < entry; deg += 0.25f) {
                                const float th = deg * kPi / 180.0f;
                                const float st = std::sin(th), ct = std::cos(th);
                                const float zc = -py * st + pz * ct;
                                if (zc <= nearPlane) continue;
                                const float yc = py * ct + pz * st;
                                if (std::fabs(yc) > tanHalfV * zc) continue;
                                if (std::fabs(px) > tanHalfH * zc) continue;
                                entry = deg;   // inside at this pitch
                                break;
                            }
                        }
                    }
                }
            }
            return entry;
        };

        // Diagnostic: WHAT is in frame at level pitch, if anything. An entry
        // angle of 0 means the player's own geometry is on screen looking
        // straight ahead, which no pitch clamp can fix -- report the example
        // vertex rather than leaving the reader to guess.
        auto probeLevel = [&](const char* const* codes, size_t nCodes, const char* what) {
            int inside = 0;
            irr::core::vector3df worst(0, 0, 0);
            float worstZ = -1.0f;
            for (size_t ci = 0; ci < nCodes; ++ci) {
                const char* code = codes[ci];
                node->playAnimation(code, true);
                for (int t = 0; t <= 400; t += 50) {
                    node->OnAnimate(static_cast<irr::u32>(t));
                    irr::scene::SMesh* mesh = node->getInstanceMesh();
                    if (!mesh) continue;
                    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(b);
                        if (!mb) continue;
                        for (irr::u32 v = 0; v < mb->getVertexCount(); ++v) {
                            const irr::core::vector3df& raw = mb->getPosition(v);
                            const float px = raw.X, py = raw.Y - eye, pz = raw.Z;
                            if (pz <= nearPlane) continue;
                            if (std::fabs(py) > tanHalfV * pz) continue;
                            if (std::fabs(px) > tanHalfH * pz) continue;
                            ++inside;
                            if (pz > worstZ) { worstZ = pz; worst = irr::core::vector3df(px, py, pz); }
                        }
                    }
                }
            }
            if (inside > 0) {
                std::cout << "  " << rc.label << " | " << what << ", AT LEVEL PITCH: " << inside
                          << " vertex-samples inside the frustum; furthest at"
                             " (right " << worst.X << ", up " << worst.Y << ", forward "
                          << worst.Z << ") from the eye\n";
            }
            return inside;
        };
        const int insideAtLevel = probeLevel(idleAnims, 1, "AT REST");
        const int swingInsideAtLevel = probeLevel(attackAnims, 5, "MID-SWING");

        const float entry1x = entryPitchDegrees(1.0f, idleAnims, 1);
        const float entry5x = entryPitchDegrees(5.0f, idleAnims, 1);
        std::cout << "  " << rc.label << " | 1x | " << entry1x
                  << (entry1x > clampDeg ? "  (clamp holds)"
                      : (insideAtLevel > 0 ? "  <-- ALREADY IN FRAME AT LEVEL PITCH -- no clamp can fix this"
                                           : "  <-- BELOW THE CLAMP: body enters frame"))
                  << "\n";
        std::cout << "  " << rc.label << " | 5x | " << entry5x
                  << "   [mid-swing vertices in frame at level pitch: " << swingInsideAtLevel
                  << " -- that is the swing the mechanism is supposed to show]\n";
        ++racesCovered;
        if (rc.raceId == 1 && rc.gender == 0) humanMaleCovered = true;

        // The CLAMP is scale-invariant by construction (it is a function of
        // vFOV alone -- asserted numerically in
        // test_first_person_camera_single_formula.cpp). The measured ENTRY
        // angle is not quite, and the difference is instructive: the only
        // scale-dependent term left in the view is the 1.0 world near plane,
        // which clips more of a small model than of a large one. So scaling UP
        // can only lose that help, never gain it.
        EXPECT_LE(entry5x, entry1x + 0.26f)
            << rc.label << ": scaling the model up moved the body's entry angle LATER ("
            << entry1x << " -> " << entry5x << "), which no geometry explains -- the near "
               "plane is the only scale-dependent term and it can only clip less as the "
               "model grows.";

        node->remove();
    }

    EXPECT_TRUE(humanMaleCovered)
        << "Human male model data unavailable -- this test verified nothing about the race "
           "Leg A measured the official client on";
    EXPECT_GT(racesCovered, 0);

    // The clamp itself: derived, inside Leg A's measured bracket, and
    // independent of any model.
    EXPECT_GT(clampDeg, 55.0f) << "clamp below Leg A's measured bracket (55-75 degrees)";
    EXPECT_LT(clampDeg, 75.0f) << "clamp above Leg A's measured bracket (55-75 degrees)";

    // ------------------------------------------------------------------
    // THE PER-RACE ENTRY ANGLES ABOVE ARE REPORTED, NOT ASSERTED, AND THEY DO
    // NOT AGREE WITH THE CLAMP. Read the numbers: fable's own resting bodies
    // enter this frustum at roughly 35-41 degrees for the human-shaped races,
    // and a Troll or Iksar is inside it at LEVEL pitch -- all well short of the
    // 65.4-degree clamp. Taken at face value that says the clamp alone does not
    // hide fable's body, whatever it does in the official client.
    //
    // Two reasons this is reported rather than asserted:
    //   1. The measurement assumes the baked player mesh's +Z is the camera's
    //      forward at heading 0 (setIsPlayerNode() + rotation 0 -> RotY(-90),
    //      T298 section 3). That mapping is derived, not confirmed against a
    //      live frame. If it is wrong, "forward" here is really "sideways" and
    //      the shoulder width is being read as forward reach -- which would
    //      explain resting extents of 2.7-3.0 units in front of the eye.
    //   2. fable's models, animations and eye height (the crown of the head,
    //      not eye level) are not the official client's, so an angle measured
    //      there does not have to reproduce here.
    //
    // Either way this is the first thing T300 Leg C should look at, and it is
    // a concrete prediction for it to confirm or refute: at max down-pitch,
    // does fable show the player's body? See
    // refactor/T300_leg_b_implementation.md.
}

#endif // EQT_HAS_GRAPHICS

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
