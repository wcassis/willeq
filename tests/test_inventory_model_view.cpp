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

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/entity_renderer.h"  // EntityAppearance
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
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

    void SetUp() override {
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
        {330, 0, "unknown/invalid race (should fail gracefully)"},
    };

    for (const auto& rc : races) {
        SCOPED_TRACE(rc.label);
        auto* node = loader_->createAnimatedNode(rc.raceId, rc.gender,
                                                   smgr_->getRootSceneNode());
        if (rc.raceId == 330) {
            // race=330 has no model data on this asset set (deferred.md:
            // "gfaydark zone-in FATAL, missing race=330 model data") —
            // assert the loader degrades to nullptr rather than crashing,
            // not that it succeeds.
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
#endif // EQT_HAS_GRAPHICS

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
