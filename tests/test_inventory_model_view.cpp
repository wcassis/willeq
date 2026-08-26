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

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/entity_renderer.h"  // EntityAppearance
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

// T74: per-race/gender verification of buildArmBoneIndices()'s name-
// substring heuristic ("ARM", "POINT", "DAG", "SHIELD", ... plus children --
// see the plan's caution that this is the same class of fragile matching
// T60 had to converge and T65 had to correct). Picking nothing gives an
// invisible player; picking everything gives a floating torso -- this
// asserts every tested race lands strictly between those two extremes, and
// that the fallback (hasValidFirstPersonGeometry()==false when a skeleton
// yields zero arm bones) is at least reachable in principle (none of these
// real skeletons are expected to hit it, but the assertion shape covers
// both outcomes so a future race that DOES hit it fails loudly here instead
// of silently rendering wrong in-game).
TEST_F(RaceModelAnimationHeadlessTest, FirstPersonArmFilter_PerRace_Headless) {
    struct RaceCase { uint16_t raceId; uint8_t gender; const char* label; };
    const RaceCase races[] = {
        {1, 0, "Human male"},
        {1, 1, "Human female"},
        {2, 0, "Barbarian male"},
        {8, 0, "Dwarf male"},
        {6, 1, "Dark Elf female"},   // T65's exact race/gender (secondary-weapon regression case)
        {9, 0, "Troll male"},        // large/non-standard build
        {128, 0, "Iksar male"},      // non-humanoid-proportioned skeleton
    };

    std::cout << "\nT74 per-race arm-bone selection:\n";
    std::cout << "  race                | armBones | totalBones | validGeometry | "
                 "idxFull -> idxFiltered\n";

    int racesCovered = 0;
    for (const auto& rc : races) {
        SCOPED_TRACE(rc.label);

        auto* node = loader_->createAnimatedNode(rc.raceId, rc.gender,
                                                   smgr_->getRootSceneNode());
        if (!node) {
            std::cout << "  " << rc.label << ": createAnimatedNode() returned "
                         "null (race data unavailable) - skipping\n";
            continue;
        }
        ASSERT_NE(node->getInstanceMesh(), nullptr)
            << rc.label << ": animated node has no per-instance mesh";

        // Step one animation frame first, matching production (setFirstPersonMode
        // is only ever called on an already-live, already-animated node).
        node->playAnimation("l01", true);
        node->OnAnimate(100);

        size_t fullIndexCount = node->getInstanceMeshIndexCount();
        ASSERT_GT(fullIndexCount, 0u) << rc.label << ": bind mesh has no triangles at all";

        node->setFirstPersonMode(true);
        size_t armBones = node->getArmBoneCount();
        bool validGeometry = node->hasValidFirstPersonGeometry();
        size_t filteredIndexCount = node->getInstanceMeshIndexCount();

        std::cout << "  " << rc.label << " (race=" << rc.raceId << " gender="
                  << static_cast<int>(rc.gender) << ") | armBones=" << armBones
                  << " | validGeometry=" << validGeometry
                  << " | idx " << fullIndexCount << " -> " << filteredIndexCount << "\n";
        ++racesCovered;

        if (validGeometry) {
            // "Picking nothing gives an invisible player" -- must have kept
            // at least some geometry.
            EXPECT_GT(filteredIndexCount, 0u)
                << rc.label << ": hasValidFirstPersonGeometry()==true but the "
                   "filtered mesh has zero triangles (picked nothing)";
            // "Picking too much gives a floating torso" -- must have dropped
            // a meaningful fraction of the full body (arms are a small
            // fraction of a humanoid's total triangle budget).
            EXPECT_LT(filteredIndexCount, fullIndexCount)
                << rc.label << ": first-person filter kept ALL geometry -- "
                   "arm bone selection matched the whole skeleton";
            EXPECT_GT(armBones, 0u)
                << rc.label << ": validGeometry true but armBoneCount is 0 "
                   "(inconsistent with the filtered mesh)";
        } else {
            // Fallback path: no usable arm bones -- the filter must not have
            // been applied (instance mesh left exactly as
            // createInstanceMesh() built it, full geometry), since
            // EntityRenderer::setPlayerFirstPersonMode() is what actually
            // hides the whole entity in this case, not the mesh itself.
            EXPECT_EQ(filteredIndexCount, fullIndexCount)
                << rc.label << ": no valid arm geometry but the instance mesh "
                   "was filtered anyway (should stay untouched; the ENTITY-level "
                   "fallback is what hides it)";
        }

        // Leaving first person must restore full geometry exactly.
        node->setFirstPersonMode(false);
        EXPECT_EQ(node->getInstanceMeshIndexCount(), fullIndexCount)
            << rc.label << ": leaving first person did not restore full geometry";
    }

    EXPECT_GT(racesCovered, 0) << "no race in the table had loadable model data -- "
                                   "this test verified nothing";
}

// T74: the arm-only filter must be strictly per-NODE state (armBoneIndices_/
// firstPersonMode_ are EQAnimatedMeshSceneNode instance members, not shared
// via EQAnimatedMesh, which multiple nodes of the same race/gender DO
// share). This is what CharacterModelView's paperdoll -- and every other
// entity's in-world node -- relies on to stay untouched when the PLAYER's
// own node enters first person. Two nodes built from the same race/gender
// (so they share the same underlying EQAnimatedMesh, exactly like a player
// and an NPC of the same race, or a live entity and its paperdoll instance
// would) must not affect each other.
TEST_F(RaceModelAnimationHeadlessTest, FirstPersonMode_IsPerNode_NotShared_Headless) {
    auto* nodeA = loader_->createAnimatedNode(1, 0, smgr_->getRootSceneNode());
    auto* nodeB = loader_->createAnimatedNode(1, 0, smgr_->getRootSceneNode());
    if (!nodeA || !nodeB) {
        GTEST_SKIP() << "createAnimatedNode() returned null (race data unavailable)";
    }

    nodeA->playAnimation("l01", true);
    nodeA->OnAnimate(100);
    nodeB->playAnimation("l01", true);
    nodeB->OnAnimate(100);

    size_t bFullIndexCount = nodeB->getInstanceMeshIndexCount();
    ASSERT_GT(bFullIndexCount, 0u);

    // Only node A enters first person (models the player's own node
    // switching while everything else -- other entities, the paperdoll --
    // stays in its normal, untouched mode).
    nodeA->setFirstPersonMode(true);

    EXPECT_TRUE(nodeA->isFirstPersonMode());
    EXPECT_FALSE(nodeB->isFirstPersonMode())
        << "node B's first-person flag changed when only node A was toggled "
           "-- firstPersonMode_ is not per-node";
    EXPECT_EQ(nodeB->getInstanceMeshIndexCount(), bFullIndexCount)
        << "node B's geometry was filtered when only node A entered first "
           "person -- armBoneIndices_/the index filter leaked across nodes";
}
// T74: the fallback path (a skeleton with zero usable arm bones must fall
// back to today's hide-everything behaviour, not render an unfiltered or
// empty-filtered mesh). None of the real skeletons in
// FirstPersonArmFilter_PerRace_Headless hit this path -- EQ's real race
// skeletons all have SOME bone matching the name-substring heuristic -- so
// it needs a synthetic skeleton to exercise directly, same technique
// test_bone_attachment.cpp uses for findBoneIndex()'s fallback chain.
// Swaps a real node's skeleton for one with no arm-related bone names
// (no "ARM"/"POINT"/"DAG"/"SHIELD"/"HAND"/"WRIST"/"FINGER"/"FOREARM"/
// "BI_R"/"BI_L"/"FO_R"/"FO_L"/"FI_R"/"FI_L" substring anywhere) BEFORE the
// first setFirstPersonMode(true) call, so buildArmBoneIndices() runs
// against it.
TEST_F(RaceModelAnimationHeadlessTest, FirstPersonArmFilter_FallbackWhenNoArmBones_Headless) {
    auto* node = loader_->createAnimatedNode(1, 0, smgr_->getRootSceneNode());
    if (!node) {
        GTEST_SKIP() << "createAnimatedNode() returned null (race data unavailable)";
    }
    node->playAnimation("l01", true);
    node->OnAnimate(100);
    size_t fullIndexCount = node->getInstanceMeshIndexCount();
    ASSERT_GT(fullIndexCount, 0u);

    auto barebonesSkeleton = std::make_shared<EQT::Graphics::CharacterSkeleton>();
    barebonesSkeleton->modelCode = "XXX";
    for (const char* name : {"TORSO", "HEAD", "PELVIS", "SPINE"}) {
        EQT::Graphics::AnimatedBone bone;
        bone.name = name;
        bone.parentIndex = -1;
        barebonesSkeleton->bones.push_back(bone);
    }
    node->getAnimator().setSkeleton(barebonesSkeleton);

    node->setFirstPersonMode(true);

    EXPECT_FALSE(node->hasValidFirstPersonGeometry())
        << "a skeleton with no ARM/POINT/DAG/SHIELD/... bone names should not "
           "yield valid first-person geometry";
    EXPECT_EQ(node->getArmBoneCount(), 0u);
    // The fallback contract: the instance mesh itself must be left
    // untouched (full geometry) -- EntityRenderer::setPlayerFirstPersonMode()
    // is what actually hides the whole entity in this case, not the mesh.
    EXPECT_EQ(node->getInstanceMeshIndexCount(), fullIndexCount)
        << "fallback path must not filter the instance mesh -- the caller "
           "hides the whole entity instead";
}
// T75 Step 1: measure, numerically, where the arm-filtered geometry sits
// relative to the first-person camera -- in idle AND in an attack
// animation. Reproduces the two production formulas exactly (rather than
// re-deriving them) so this test would break, not silently drift, if either
// changes:
//   - EntityRenderer::getPlayerEyeHeightFromFeet(): camera height above feet
//     == the FULL model bounding-box height (bbox.MaxEdge.Y - MinEdge.Y),
//     not a fraction of it -- entity_renderer.cpp:3027.
//   - IrrlichtRenderer::repositionFirstPersonCamera(): camZ = playerZ(feet)
//     + eyeHeight + playerConfig_.eyeHeight (default adjust 0.0) --
//     irrlicht_renderer.cpp:2421-2440. The camera looks perfectly level
//     (target Y == camera Y), no pitch.
// This does not touch T74's filter -- it only reads getArmBoneIndices()
// (T75's new read-only accessor) and getBoneWorldPosition(), both
// pre-existing/added-but-inert data, never mutated here.
TEST_F(RaceModelAnimationHeadlessTest, ArmVsCameraGeometryMeasurement_Headless) {
    auto* node = loader_->createAnimatedNode(1, 0, smgr_->getRootSceneNode());  // Human male
    if (!node) {
        GTEST_SKIP() << "createAnimatedNode() returned null (race data unavailable)";
    }

    // Node sits at the scene origin (feet at Y=0), matching how
    // getBoneWorldPosition()'s node-relative math is exercised in the other
    // fixture tests in this file -- the absolute world offset cancels out of
    // every delta computed below, only the RELATIVE geometry matters.
    ASSERT_FLOAT_EQ(node->getAbsolutePosition().Y, 0.0f);

    node->playAnimation("l01", true);
    node->OnAnimate(100);

    // Reproduces EntityRenderer::getPlayerEyeHeightFromFeet() exactly: the
    // BASE mesh bounding box's top edge (not the instance/filtered mesh).
    // T75: this is bbox.MaxEdge.Y ALONE, not bbox.MaxEdge.Y - bbox.MinEdge.Y
    // -- that "full range" formula was the bug this measurement found (see
    // getPlayerEyeHeightFromFeet()'s comment): MinEdge.Y is NOT 0 for these
    // models (the [DIAG] line below prints the real value), so subtracting
    // it doubled the effective camera height instead of stopping at the
    // model's actual top.
    EQT::Graphics::EQAnimatedMesh* eqMesh = node->getEQMesh();
    ASSERT_NE(eqMesh, nullptr);
    irr::core::aabbox3df bbox = eqMesh->getBoundingBox();
    float eyeHeightFromFeet = bbox.MaxEdge.Y;
    ASSERT_GT(eyeHeightFromFeet, 0.0f) << "degenerate bounding box -- cannot measure";

    // T76: reproduce the CURRENT production getPlayerEyeHeightFromFeet(),
    // which now prefers the live HEAD bone Y over the static bbox top
    // (falling back to the bbox when no HEAD bone resolves -- see that
    // function's comment for the measured rationale).
    int headIdx = node->getBoneIndexByName("HEAD");
    if (headIdx < 0) {
        auto skel = eqMesh->getSkeleton();
        if (skel) {
            for (size_t i = 0; i < skel->bones.size(); ++i) {
                std::string up = skel->bones[i].name;
                std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                if (up.find("HEAD") != std::string::npos) { headIdx = static_cast<int>(i); break; }
            }
        }
    }
    float productionEyeHeightFromFeet = eyeHeightFromFeet;  // fallback value
    if (headIdx >= 0) {
        irr::core::vector3df headPosEarly;
        if (node->getBoneWorldPosition(headIdx, headPosEarly) && headPosEarly.Y > 0.0f) {
            productionEyeHeightFromFeet = headPosEarly.Y;
        }
    }

    // Reproduces IrrlichtRenderer::repositionFirstPersonCamera(): feet(0) +
    // eyeHeight + playerConfig_.eyeHeight (default adjustment is 0.0, no
    // config loaded in this headless fixture).
    constexpr float kPlayerConfigEyeHeightAdjustDefault = 0.0f;  // playerConfig_.eyeHeight default, irrlicht_renderer.h:325
    float cameraY = 0.0f + productionEyeHeightFromFeet + kPlayerConfigEyeHeightAdjustDefault;

    // Populate armBoneIndices_ once (name-based, not pose-based -- doesn't
    // need to happen per-animation).
    node->setFirstPersonMode(true);
    const std::vector<int>& armIndices = node->getArmBoneIndices();
    ASSERT_FALSE(armIndices.empty()) << "Human male must have arm bones -- see T74's per-race table";

    // Irrlicht's default camera FOV (CCameraSceneNode.cpp: Fovy = PI/2.5,
    // ~72 degrees vertical) -- the renderer never calls setFOV() on the
    // main/first-person camera (only the deleted paperdoll's separate
    // camera did), so this is the live value.
    const float kDefaultVerticalFovRad = irr::core::PI / 2.5f;
    const float kHalfFovRad = kDefaultVerticalFovRad / 2.0f;

    std::cout << "\nT75/T76 arm-vs-camera geometry measurement (Human male):\n";
    std::cout << "  eyeHeightFromFeet (bbox.MaxEdge.Y, T75 fallback)   = " << eyeHeightFromFeet << "\n";
    std::cout << "  eyeHeightFromFeet (T76 production value, head bone preferred) = " << productionEyeHeightFromFeet << "\n";
    std::cout << "  camera Y (feet=0 + eyeHeight + adjust=0)       = " << cameraY << "\n";
    std::cout << "  camera vertical FOV                            = "
              << (kDefaultVerticalFovRad * 180.0f / irr::core::PI) << " deg (half="
              << (kHalfFovRad * 180.0f / irr::core::PI) << " deg)\n";
    std::cout << "  arm bone count                                 = " << armIndices.size() << "\n";
    std::cout << "  raw bbox Y range = [" << bbox.MinEdge.Y << ", " << bbox.MaxEdge.Y
              << "] -- MinEdge != 0 is exactly the pre-fix bug (old formula used "
                 "MaxEdge-MinEdge = " << (bbox.MaxEdge.Y - bbox.MinEdge.Y)
              << " instead of MaxEdge.Y = " << bbox.MaxEdge.Y << ")\n";
    if (headIdx >= 0) {
        std::cout << "  head bone Y = " << productionEyeHeightFromFeet
                  << " (T76: this IS the production eye height now -- see"
                     " getPlayerEyeHeightFromFeet()'s comment)\n";
    } else {
        std::cout << "  [DIAG] no HEAD bone found\n";
    }

    struct AnimSample { const char* code; const char* label; };
    // "l01" = idle/stand loop. "c06" = 1H Slash/Blunt attack swing (the
    // animation code Testchar's equipped blunt weapon resolves to via
    // EQ::getWeaponAttackAnimation(WEAPON_1H_BLUNT) -- animation_constants.h).
    const AnimSample anims[] = {
        {"l01", "idle"},
        {"c06", "attack (1H slash/blunt swing)"},
        {"c02", "attack (1H piercing swing)"},
        {"c03", "attack (2H slash swing)"},
        {"c04", "attack (2H blunt swing)"},
        {"c08", "attack (hand-to-hand swing)"},
    };

    for (const auto& sample : anims) {
        SCOPED_TRACE(sample.label);
        node->playAnimation(sample.code, true);
        // Step across several frames so an attack swing (which moves
        // through a pose, not a single static frame) is sampled mid-motion,
        // not just at its first frame.
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        // Camera looks along heading=0 -> forward == (0,0,+1) (repositionFirstPersonCamera()'s
        // headingRad math: sin(0)=0 X-component, cos(0)=1 Z-component). Track how far
        // in front of (Z>0, toward the look target) vs. beside (|X|) the camera axis
        // the arm bones actually sit -- needed alongside the Y delta because the
        // camera's near clip plane is 1.0 (setupCamera(), irrlicht_renderer.cpp:1517):
        // geometry inside that distance is clipped regardless of vertical angle.
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();
        float maxAbsX = 0.0f;
        int sampledFrames = 0;
        for (int t = 0; t <= 400; t += 50) {
            node->OnAnimate(static_cast<irr::u32>(t));
            bool anyBone = false;
            for (int boneIdx : armIndices) {
                irr::core::vector3df pos;
                if (node->getBoneWorldPosition(boneIdx, pos)) {
                    minY = std::min(minY, pos.Y);
                    maxY = std::max(maxY, pos.Y);
                    minZ = std::min(minZ, pos.Z);
                    maxZ = std::max(maxZ, pos.Z);
                    maxAbsX = std::max(maxAbsX, std::fabs(pos.X));
                    anyBone = true;
                }
            }
            if (anyBone) ++sampledFrames;
        }
        ASSERT_GT(sampledFrames, 0) << sample.label << ": no arm bone ever resolved a world position";

        float deltaMin = minY - cameraY;  // negative == below camera
        float deltaMax = maxY - cameraY;
        std::cout << "  [" << sample.label << "] arm bone Y range = [" << minY << ", " << maxY
                  << "]  (delta vs camera Y=" << cameraY << ": [" << deltaMin << ", " << deltaMax << "])\n";
        std::cout << "  [" << sample.label << "] arm bone Z (forward) range = [" << minZ << ", " << maxZ
                  << "], max |X| (sideways) = " << maxAbsX
                  << "  (camera at Z=0 looking toward +Z, near clip = 1.0)\n";

        // Not asserted pass/fail (this is a measurement, not a regression
        // gate) -- reported here and in the plan's Review for Will/the next
        // executor to read the raw numbers from, per the plan's Step 1.

        // T76: bone JOINT positions (above) understate how close the actual
        // skin TRIANGLES get -- a forearm's mesh extends outward from its
        // bone by the limb's radius, so the true nearest vertex can sit
        // closer to the camera than any bone. Walk the arm-FILTERED instance
        // mesh's own triangle indices (post applyFirstPersonIndexFilter(),
        // post applyAnimation() skinning) and measure the actual vertex
        // extents T74's filter left behind -- this is what the GPU actually
        // rasterizes, not a proxy for it.
        irr::scene::SMesh* filteredMesh = node->getInstanceMesh();
        ASSERT_NE(filteredMesh, nullptr);
        float vMinY = std::numeric_limits<float>::max();
        float vMaxY = std::numeric_limits<float>::lowest();
        float vMinZ = std::numeric_limits<float>::max();
        float vMaxZ = std::numeric_limits<float>::lowest();
        size_t triCount = 0;
        for (irr::u32 b = 0; b < filteredMesh->getMeshBufferCount(); ++b) {
            irr::scene::IMeshBuffer* mb = filteredMesh->getMeshBuffer(b);
            if (!mb) continue;
            const irr::u16* idx = mb->getIndices();
            irr::u32 idxCount = mb->getIndexCount();
            for (irr::u32 t = 0; t + 2 < idxCount; t += 3) {
                for (int k = 0; k < 3; ++k) {
                    const irr::core::vector3df& p = mb->getPosition(idx[t + k]);
                    vMinY = std::min(vMinY, p.Y);
                    vMaxY = std::max(vMaxY, p.Y);
                    vMinZ = std::min(vMinZ, p.Z);
                    vMaxZ = std::max(vMaxZ, p.Z);
                }
                ++triCount;
            }
        }
        std::cout << "  [" << sample.label << "] ARM-FILTERED TRIANGLE VERTEX extents: Y=["
                  << vMinY << ", " << vMaxY << "], Z(forward)=[" << vMinZ << ", " << vMaxZ
                  << "], triangle count=" << triCount << "\n";

        // T76: the near clip plane is 1.0 (setupCamera()). A candidate
        // camera sitting at world Z=cameraForwardOffset (i.e. pushed
        // `cameraForwardOffset` units forward of the body origin along the
        // look axis, matching how repositionFirstPersonCamera() would need
        // to change) would see this triangle set at distance range
        // [vMinZ - offset, vMaxZ - offset]. Report whether the CURRENT
        // (offset=0, matches shipped repositionFirstPersonCamera()) camera
        // has triangles straddling (spanning across) the near plane --
        // exactly the condition the plan warns produces the smeared mass
        // regardless of pitch.
        const float kNearPlane = 1.0f;  // setupCamera(), irrlicht_renderer.cpp:1517
        bool straddlesAtZeroOffset = (vMinZ < kNearPlane) && (vMaxZ > kNearPlane);
        bool entirelyInsideNear = (vMaxZ <= kNearPlane);
        std::cout << "  [" << sample.label << "] at CURRENT camera offset (0): straddles near plane="
                  << (straddlesAtZeroOffset ? "YES" : "no") << ", entirely inside near clip (invisible)="
                  << (entirelyInsideNear ? "YES" : "no") << "\n";

        // T76: evaluate candidate STANDOFF (pullback) distances -- moving the
        // camera BACKWARD along the look axis by `pullback` units, i.e.
        // camera position Z -> -pullback, increases distance-to-vertex for
        // every vertex uniformly (distance = vertexZ - (-pullback) =
        // vertexZ + pullback). This is the opposite sign from the plan's a
        // priori guess ("push camera forward of the model origin"): a
        // FORWARD push (positive offset) SHRINKS distance to the
        // forward-swinging (visible, important) geometry -- exactly the
        // wrong direction, since several poses' forward reach barely clears
        // the near plane already (idle max=1.106, 2H-slash max=1.035, both
        // <0.11 above kNearPlane). A BACKWARD pullback is what the unit's
        // own name describes. It is not monotonically safe, though: the
        // near-side (currently-negative, safely-behind) vertices move
        // TOWARD zero as pullback grows, and vertices sitting at distance
        // near 0 (not just <kNearPlane) are the ones that blow up under
        // perspective divide -- so an overlarge pullback can walk a
        // comfortably-behind vertex into that danger zone before it clears
        // kNearPlane on the far side. Report several candidates so the
        // choice is evidence-based, not a guess in either direction.
        for (float pullback : {0.0f, 0.2f, 0.3f, 0.5f, 1.0f}) {
            float distMin = vMinZ + pullback;
            float distMax = vMaxZ + pullback;
            bool straddles = (distMin < kNearPlane) && (distMax > kNearPlane);
            bool nearZeroDanger = (distMin > -0.1f && distMin < kNearPlane);
            std::cout << "    pullback=" << pullback << ": distance range=[" << distMin << ", " << distMax
                      << "], straddles=" << (straddles ? "YES" : "no")
                      << ", near-side within [-0.1,near) danger band=" << (nearZeroDanger ? "YES" : "no") << "\n";
        }
    }
}
#endif // EQT_HAS_GRAPHICS

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
