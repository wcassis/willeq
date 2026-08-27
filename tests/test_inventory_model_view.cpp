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
TEST_F(RaceModelAnimationHeadlessTest, FirstPersonNearPlaneSwallowsTheBody_Headless) {
    // The mechanism: eye at the top of the head (the production formula,
    // EntityRenderer::getPlayerEyeHeightAboveNodeOrigin() == base mesh
    // bbox.MaxEdge.Y), near clip plane DERIVED from the player's own model
    // (firstPersonNearPlaneFromBodyRadius(restRadius)). The player pitches the
    // camera freely with the mouse, and T299 observed nothing of the body at
    // ANY pitch including straight down at the feet, so the requirement is
    // RADIAL: every vertex of the player's own body must lie inside the plane,
    // because any of them can be brought onto the view axis by looking at it.
    //
    // Two things are checked per race, against real S3D mesh data:
    //   1. the plane derived from THAT race's model clears THAT race's resting
    //      body -- which a fixed distance could not do (a Dwarf male's resting
    //      body reaches 7.09 units and a Human female's 6.14);
    //   2. the cheap bounding-box radius production computes every frame
    //      (EntityRenderer::getPlayerBodyRestRadius()) is never SMALLER than
    //      the true furthest vertex -- if it under-reports, the derived plane
    //      lands inside the body and the player sees himself.
    struct RaceCase { uint16_t raceId; uint8_t gender; const char* label; };
    const RaceCase races[] = {
        {1, 0, "Human male"},        // the race T298/T299 measured the client on
        {1, 1, "Human female"},
        {2, 0, "Barbarian male"},
        {8, 0, "Dwarf male"},
        {6, 1, "Dark Elf female"},
        {9, 0, "Troll male"},        // large build -- expected to EXCEED a human-sized plane
        {128, 0, "Iksar male"},
    };
    // AT REST (l01 idle) is the requirement: T299 saw nothing of the player
    // at any pitch while standing. The ATTACK animations are the opposite
    // measurement -- geometry that reaches BEYOND the plane is exactly the
    // blade and the boot that T299 saw reappear, so a swing exceeding the
    // plane is the mechanism working, not a failure.
    const char* kIdleAnim = "l01";
    const char* attackAnims[] = {"c06", "c02", "c03", "c04", "c08"};

    std::cout << "\nT300 first-person near plane, DERIVED per model (margin "
              << EQT::Graphics::kFirstPersonNearPlaneMargin << " x rest radius):\n";
    std::cout << "  race | eye | max radial AT REST (must be < plane) | bbox-corner radius"
                 " (the per-frame proxy) | derived plane | max radial IN ATTACK"
                 " (may exceed -- that is the visible swing)\n";

    bool humanMaleCovered = false;
    int racesCovered = 0;
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

        auto measure = [&](const char* code) {
            float worst = 0.0f;
            node->playAnimation(code, true);
            for (int t = 0; t <= 400; t += 50) {
                node->OnAnimate(static_cast<irr::u32>(t));
                irr::scene::SMesh* mesh = node->getInstanceMesh();
                if (!mesh) return worst;
                for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                    irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(b);
                    if (!mb) continue;
                    const irr::u32 vcount = mb->getVertexCount();
                    for (irr::u32 v = 0; v < vcount; ++v) {
                        const irr::core::vector3df& p = mb->getPosition(v);
                        const float dx = p.X, dy = p.Y - eye, dz = p.Z;
                        worst = std::max(worst, std::sqrt(dx*dx + dy*dy + dz*dz));
                    }
                }
            }
            return worst;
        };

        const float restRadial = measure(kIdleAnim);
        float attackRadial = 0.0f;
        for (const char* code : attackAnims) attackRadial = std::max(attackRadial, measure(code));

        // Reproduce EntityRenderer::getPlayerBodyRestRadius() exactly (node
        // scale 1 here): the furthest bounding-box corner from the eye.
        const irr::core::aabbox3df bbox = eqMesh->getBoundingBox();
        float bboxRadius = 0.0f;
        for (int corner = 0; corner < 8; ++corner) {
            const float cx = (corner & 1) ? bbox.MaxEdge.X : bbox.MinEdge.X;
            const float cy = (corner & 2) ? bbox.MaxEdge.Y : bbox.MinEdge.Y;
            const float cz = (corner & 4) ? bbox.MaxEdge.Z : bbox.MinEdge.Z;
            const float dy = cy - eye;
            bboxRadius = std::max(bboxRadius, std::sqrt(cx*cx + dy*dy + cz*cz));
        }
        const float derivedPlane =
            EQT::Graphics::firstPersonNearPlaneFromBodyRadius(bboxRadius);

        ASSERT_GT(restRadial, 0.0f) << rc.label << ": no vertices measured";
        const bool swallowed = restRadial < derivedPlane;
        std::cout << "  " << rc.label << " | eye " << eye << " | rest " << restRadial
                  << " | bbox radius " << bboxRadius << " | derived plane " << derivedPlane
                  << (swallowed ? " (swallowed)" : " (NOT SWALLOWED -- body on screen)")
                  << " | attack " << attackRadial << "\n";
        ++racesCovered;

        // (1) The plane derived from THIS race's model must clear THIS race's
        // resting body -- asserted for EVERY race, which is exactly what a
        // fixed distance could not deliver.
        EXPECT_LT(restRadial, derivedPlane)
            << rc.label << ": the near plane derived from this model (" << derivedPlane
            << ") does not clear its own resting body (" << restRadial << "). The plane is "
               "the ONLY thing hiding the player in first person -- if this fails, the body "
               "is on screen. The margin lives in include/client/graphics/first_person_camera.h.";

        // (2) The cheap per-frame bbox proxy must not UNDER-report the true
        // vertex extent, or the derivation is unsafe.
        EXPECT_GE(bboxRadius, restRadial)
            << rc.label << ": getPlayerBodyRestRadius()'s bounding-box proxy (" << bboxRadius
            << ") is smaller than the furthest actual vertex at rest (" << restRadial
            << "). The proxy must be conservative.";

        const float maxRadial = restRadial;

        if (rc.raceId == 1 && rc.gender == 0) {
            humanMaleCovered = true;
            // The reference race: T298 measured the official client's eye at
            // 6.75 +/- 0.20 above the server z on a human, and T299 saw no body
            // geometry at any pitch. The derived plane must land above that
            // hard lower bound for this race specifically.
            EXPECT_GT(derivedPlane, 6.75f)
                << "the plane derived for a Human male (" << derivedPlane << ") is inside "
                   "T299's hard lower bound of 6.75 -- the client showed nothing of the body "
                   "with the camera aimed straight down at feet that far below the eye.";
            (void)maxRadial;
        }
        node->remove();
    }

    EXPECT_TRUE(humanMaleCovered)
        << "Human male model data unavailable -- this test verified nothing about the "
           "race the official-client measurement was taken on";
    EXPECT_GT(racesCovered, 0);

    // NOTE the figures above are at node scale 1. Production multiplies the
    // rest radius by the LIVE scene node scale, so the server's spawn size
    // field and any runtime shrink/grow ride through the same derivation
    // without changing anything measured here.
    //
    // UNVALIDATED: that the official client derives its hiding from model size
    // at all is a hypothesis, not a measurement. T300 Leg A is determining
    // whether the mechanism is one global plane, a size-derived plane, or
    // per-object culling that is not a near plane. See
    // refactor/T300_leg_b_implementation.md.
}

#endif // EQT_HAS_GRAPHICS

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
