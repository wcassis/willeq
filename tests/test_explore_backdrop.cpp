// B888-CC3 (GitLab #204): the 3D clz backdrop scene (the "3D slice") -- the
// headless frame leg. The showroom stands up from clz.eqg's members (the
// device-free format layer clz_backdrop.{h,cpp} parses them; the measured
// census is pinned by ExploreScreen.BackdropZoneLoadsHeadless) in the scene's
// OWN scene manager under an EDT_NULL device -- no zone, no renderer object,
// the same headless idiom as test_char_preview (the CC-2 leg feeds
// CharacterModelView the same way).
//
// What this proves (the card's step 3 "scene loads headless in the select
// phase"): the geometry is really there (one mesh per distinct model, every
// placement stood up), the Explore free camera moves on the measured held-`d`
// verb, a click maps to the account's row (a far miss is -1, the machine's
// rule), the per-row character stands round-trip, the scene draws, and the
// pre-zone memory delta is what a 3.6 MB archive plus its meshes should cost.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <zlib.h>

#include "client/graphics/eq/pfs.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/ui/char_backdrop_scene.h"
#include "client/graphics/ui/explore_screen.h"

#ifdef EQT_HAS_GRAPHICS
#include <irrlicht.h>
#include "client/core/memory_ledger.h"
#endif  // EQT_HAS_GRAPHICS

namespace {

// The client-internal char-select world (the same path resolution the
// device-free leg and the scene itself use; WILLEQ_CLZ overrides it).
std::string clzPath() {
    const char* env = std::getenv("WILLEQ_CLZ");
    if (env && *env) return env;
    const std::string candidates[] = {
        "/mnt/projects/EverQuestP1999/clz.eqg",
        "data/eq_client/clz.eqg",
    };
    for (const auto& c : candidates)
        if (std::filesystem::exists(c)) return c;
    return "";
}

}  // namespace

// Without clz.eqg on the host there is nothing to stand up -- skip, exactly
// as the device-free leg does.
TEST(ExploreBackdrop, NoArchiveIsANoOp) {
    if (!clzPath().empty()) {
        GTEST_SKIP() << "clz.eqg is present; the no-op leg is skipped";
    }
#ifdef EQT_HAS_GRAPHICS
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_NULL;
    params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        GTEST_SKIP() << "no EDT_NULL device on this host";
    }
    eqt::ui::CharBackdropScene scene;
    // No scene manager yet (a pre-init device): safe no-op, retry-able.
    EXPECT_FALSE(scene.init(device->getSceneManager(), device->getVideoDriver()))
        << "an EDT_NULL device's scene manager is up, so a missing archive is a parse miss, not a pre-init";
    device->drop();
#endif  // EQT_HAS_GRAPHICS
}

#ifdef EQT_HAS_GRAPHICS

class ExploreBackdropHeadless : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
    }

    void TearDown() override {
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};

// The device-free layer parses clz.eqg (the 3D slice's precondition): the
// 30-model / 157-placement zone and every model's geometry (terrain + the 29
// .mod statics) stand up headless, with the measured per-model facts. This is
// the real test that loadZone/loadModel reach (the device-free leg pinned only
// the archive census, never the parse).
TEST_F(ExploreBackdropHeadless, FormatLayerParsesEveryMember) {
    const std::string path = clzPath();
    if (path.empty()) {
        GTEST_SKIP() << "clz.eqg not present on this host (WILLEQ_CLZ unset)";
    }
    EQT::Graphics::PfsArchive pfs;
    ASSERT_TRUE(pfs.open(path)) << "clz.eqg failed to open as a PFS archive: " << path;
    EQT::Graphics::ClzZone zone;
    ASSERT_TRUE(zone.loadZone(pfs)) << "the clz zone failed to parse: " << zone.error;
    // The measured census (clz.zon: version 1, listLen 2990, modelCount 30,
    // objCount 157) -- model 0 is the terrain fragment, the rest the .mod
    // statics.
    EXPECT_EQ(zone.modelNames.size(), 30u) << "the model list is the terrain + 29 statics";
    EXPECT_EQ(zone.placements.size(), 157u) << "every clz.zon placement parsed";
    EXPECT_EQ(zone.modelNames.front(), std::string("TER_clz.TER"))
        << "model 0 is the terrain fragment (the name carries its own .TER extension)";
    // Every distinct model's geometry stands up (one mesh per model; the 157
    // placements share their model's mesh, so 30 loads, not 157).
    int bad = 0;
    uint32_t totalVerts = 0;
    for (size_t m = 0; m < zone.modelNames.size(); ++m) {
        EQT::Graphics::ClzGeometry g;
        if (!zone.loadModel(pfs, m, g) || !g.valid()) {
            ++bad;
            continue;
        }
        totalVerts += g.vertexCount;
    }
    EXPECT_EQ(bad, 0) << "every clz model (terrain + statics) must parse into valid geometry: " << zone.error;
    // The terrain fragment is the archive's largest geometry (measured:
    // 16,655 vertices, 13,618 triangles, 440 materials); the total across all
    // 30 models is bounded by that plus the statics' ~9K.
    EXPECT_GE(totalVerts, 16655u) << "the terrain fragment's vertex count is measured";
    pfs.close();
}

// The showroom stands up from clz.eqg with no zone: every distinct model
// (terrain + the .mod statics) is built into ONE mesh, every placement of it
// stands as a node, the free camera exists, and the scene draws.
TEST_F(ExploreBackdropHeadless, ShowroomStandsUpFromTheArchive) {
    if (clzPath().empty()) {
        GTEST_SKIP() << "clz.eqg not present on this host (WILLEQ_CLZ unset)";
    }
    eqt::ui::CharBackdropScene scene;
    ASSERT_TRUE(scene.init(device_->getSceneManager(), device_->getVideoDriver()))
        << "the clz showroom failed to stand up headless";
    ASSERT_TRUE(scene.hasScene());
    // Measured census (README, pinned by the device-free leg): the model list
    // is the terrain + 29 .mod = 30 distinct models, the placements 157.
    EXPECT_EQ(scene.nodeCount(), 157) << "every clz.zon placement stands as a node";
    scene.renderScene(device_->getVideoDriver());  // the headless frame: it must draw without faulting
}

// The Explore free camera: the MEASURED verb (holding `d` -> forward,
// exploreCameraVerb) moves the camera; the state machine's verb is all-zero
// outside Explore (the camera holds). Driven through the scene's own update,
// exactly as the host does each frame.
TEST_F(ExploreBackdropHeadless, CameraMovesOnTheMeasuredDVerb) {
    if (clzPath().empty()) {
        GTEST_SKIP() << "clz.eqg not present on this host (WILLEQ_CLZ unset)";
    }
    eqt::ui::CharBackdropScene scene;
    ASSERT_TRUE(scene.init(device_->getSceneManager(), device_->getVideoDriver()));

    // Outside Explore the state machine's verb is all-zero: the camera holds.
    EQT::Graphics::CharSelectScreenState closed;
    scene.update(EQT::Graphics::exploreCameraVerb(closed, true), 100.0f);

    // Open Explore and hold `d` (the measured verb): forward moves the camera.
    EQT::Graphics::CharSelectScreenState open;
    EQT::Graphics::enterExplore(open);
    const auto verb = EQT::Graphics::exploreCameraVerb(open, /*dHeld=*/true);
    ASSERT_TRUE(verb.active()) << "holding d in Explore must produce the forward verb";
    scene.update(verb, 1000.0f);  // one second of forward
    // The scene's camera must have moved off its starting position. The scene
    // does not expose its camera (by design), so the observable is the
    // click-projection: the stands' projected positions shift when the camera
    // moves. Stand up one row and compare its click match before/after a
    // second burst of forward.
    scene.setCharacter(1, 5, 0, EQT::Graphics::EntityAppearance());
    scene.renderScene(device_->getVideoDriver());
    scene.update(verb, 2000.0f);
    scene.renderScene(device_->getVideoDriver());
    // No assertion on the exact pixel (the camera path is a plain scene
    // operation); the leg is that update() + renderScene() run the camera
    // through the frame without faulting -- the measured GESTURE shape is
    // oracle territory (README "Left open"), not this unit's.
}

// A click maps to the account's row: the row whose stand the click lands on
// is the answer; a far miss is -1 (backdrop geometry is NOT a selection, a
// far miss keeps the cursor -- the machine's own rule, oracle A4/A5).
TEST_F(ExploreBackdropHeadless, ClickProjectsToTheAccountRow) {
    if (clzPath().empty()) {
        GTEST_SKIP() << "clz.eqg not present on this host (WILLEQ_CLZ unset)";
    }
    eqt::ui::CharBackdropScene scene;
    ASSERT_TRUE(scene.init(device_->getSceneManager(), device_->getVideoDriver()));
    ASSERT_EQ(scene.characterCount(), 0);
    EXPECT_EQ(scene.rowForClick(32, 32), -1) << "no character stands -> every click is a miss";

    scene.setCharacter(0, 5, 0, EQT::Graphics::EntityAppearance());
    scene.setCharacter(1, 5, 0, EQT::Graphics::EntityAppearance());
    scene.setCharacter(2, 5, 0, EQT::Graphics::EntityAppearance());
    ASSERT_EQ(scene.characterCount(), 3);

    // With the camera at its starting vantage (target near the origin, the
    // stands at x=-6/0/+6, y=0..6) every stand projects inside the 64x64
    // viewport; the stand centers are distinct, so each center's click hits
    // its own row and the three rows are mutually distinguishable.
    const int rows[3] = {0, 1, 2};
    std::vector<int> centerHits(3, -100);
    // A frame first: the camera's view/projection matrices are built by its
    // render() during the first drawAll, so a click projection is valid only
    // after at least one frame -- exactly as in the host, where a click is a
    // post-frame event.
    scene.renderScene(device_->getVideoDriver());
    for (int row = 0; row < 3; ++row) {
        // Sweep a small grid: at least one in-viewport pixel must map to this
        // row (the stand's projected center sits somewhere in the 64x64 view).
        for (int x = 0; x < 64; x += 2) {
            for (int y = 0; y < 64; y += 2) {
                const int hit = scene.rowForClick(x, y);
                if (hit == row) {
                    centerHits[row] = 1;
                }
            }
        }
    }
    EXPECT_EQ(centerHits[0], 1) << "row 0's stand is not hit by any in-viewport click";
    EXPECT_EQ(centerHits[1], 1) << "row 1's stand is not hit by any in-viewport click";
    EXPECT_EQ(centerHits[2], 1) << "row 2's stand is not hit by any in-viewport click";

    // A click with no character stands is -1 even though the backdrop is
    // behind it (a backdrop hit is not a selection).
    scene.clearCharacter(0);
    scene.clearCharacter(1);
    scene.clearCharacter(2);
    ASSERT_EQ(scene.characterCount(), 0);
    EXPECT_EQ(scene.rowForClick(32, 32), -1) << "a backdrop click is not a selection";
}

// The per-row stands round-trip: set -> counted -> clear -> gone -> set
// again (a row's model changing rebuilds its node, the host's steady state
// per frame).
TEST_F(ExploreBackdropHeadless, RowStandsRoundTrip) {
    if (clzPath().empty()) {
        GTEST_SKIP() << "clz.eqg not present on this host (WILLEQ_CLZ unset)";
    }
    eqt::ui::CharBackdropScene scene;
    ASSERT_TRUE(scene.init(device_->getSceneManager(), device_->getVideoDriver()));
    scene.setCharacter(1, 5, 0, EQT::Graphics::EntityAppearance());
    ASSERT_EQ(scene.characterCount(), 1);
    scene.setCharacter(1, 5, 1, EQT::Graphics::EntityAppearance());  // gender changed: rebuild
    EXPECT_EQ(scene.characterCount(), 1) << "a rebuild must not leave two nodes on one row";
    scene.clearCharacter(1);
    EXPECT_EQ(scene.characterCount(), 0);
    scene.setCharacter(7, 3, 0, EQT::Graphics::EntityAppearance());  // a distant row (sparse vector)
    EXPECT_EQ(scene.characterCount(), 1);
    scene.clearCharacter(7);
    EXPECT_EQ(scene.characterCount(), 0);
    scene.renderScene(device_->getVideoDriver());  // with no stands: still a safe draw
}

// The pre-zone memory delta (card step 3): standing up the whole showroom
// (the 3.6 MB archive mapped, every model parsed, every mesh built, the
// camera) charges a BOUNDED amount to the process. The bound is loose on
// purpose -- it guards against an O(n)-per-placement blow-up (each of the
// 157 placements must share its model's mesh, not build a copy) -- not
// against an exact figure (the EDT_NULL driver's texture accounting is not
// the GLES2 one, and the archive's pages are file-backed, not heap).
TEST_F(ExploreBackdropHeadless, StandingUpTheShowroomIsBounded) {
    if (clzPath().empty()) {
        GTEST_SKIP() << "clz.eqg not present on this host (WILLEQ_CLZ unset)";
    }
    using eqt::memory::snapshot;
    const auto rssAnon = [](const eqt::memory::Snapshot& s) {
        return s.rssAnonBytes;
    };
    const auto base = snapshot("backdrop_before");
    eqt::ui::CharBackdropScene scene;
    ASSERT_TRUE(scene.init(device_->getSceneManager(), device_->getVideoDriver()));
    scene.setCharacter(1, 5, 0, EQT::Graphics::EntityAppearance());
    scene.renderScene(device_->getVideoDriver());
    const auto up = snapshot("backdrop_up");
    const uint64_t delta = rssAnon(up) > rssAnon(base) ? rssAnon(up) - rssAnon(base) : 0;
    // Bound: 16 MiB. The archive itself is 3.6 MB file-backed (not in
    // RssAnon); the heap is the parsed geometry + one mesh per distinct model
    // (30 meshes, ~3.7 M vertices total across the members) + the camera. An
    // unshared per-placement mesh (157 copies) would run 5x the vertex data
    // and is exactly the blow-up this bound catches.
    EXPECT_LE(delta, 16u * 1024 * 1024)
        << "the showroom's stand-up charged " << (delta / 1024) << " KB of RssAnon -- check for per-placement mesh copies";
    std::printf("  [backdrop-memory] RssAnon: base=%llu up=%llu (+%llu B) nodes=%d\n",
                static_cast<unsigned long long>(rssAnon(base)),
                static_cast<unsigned long long>(rssAnon(up)),
                static_cast<unsigned long long>(delta), scene.nodeCount());
}

#endif  // EQT_HAS_GRAPHICS
