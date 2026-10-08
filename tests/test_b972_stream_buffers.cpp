// B972 (GitLab #290): the self-contained stream-buffer cuts.
//
// Two of the client's client-array draws move onto bounded reused VBOs
// (B975's pattern): the unified particle point draw (previously the ONE
// untagged raw-GL draw in the census) and the impostor page draws
// (previously drawVertexPrimitiveList, i.e. `draw3d:client`). The pixel
// identity of that move is pinned here, per card, on a real headless GLES2
// device (surfaceless EGL / llvmpipe -- the test_ui_atlas_pixels harness):
//
//  - the particle batches render byte-identically through the old
//    client-array path and the new VBO path (both blend modes, the whole
//    4x4 procedural atlas, sizes on both sides of the perspective
//    clamp);
//  - the impostor page renders byte-identically the same way (shipped
//    billboard program, fog on, a page texture with real alpha);
//  - the new path's vertex storage enters the driver's LOGICAL buffer
//    tally (rawBufferBytes via gles2GetHWBufferMemoryUsage) and the old
//    path's does not -- that is what turns the census row from unnamed
//    GEM into named storage.
//
// Self-skips when no GLES2 device is available (no surfaceless EGL). The
// whole body is EQT_HAS_GLES2-guarded: the renderer class itself is.

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

#ifdef EQT_HAS_GLES2
#include "client/graphics/environment/unified_particle_renderer.h"
#include "client/graphics/impostor/impostor_batch_node.h"
#include "client/graphics/texture_generators.h"
#include <GLES2/gl2.h>
#include <irrlicht.h>
#include <glm/gtc/matrix_transform.hpp>  // glm::translate / glm::perspective

using namespace EQT::Graphics;
using namespace EQT::Graphics::Environment;

extern size_t gles2GetHWBufferMemoryUsage(void* driver);

namespace {

class B972StreamBuffersTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::ITexture* atlas_ = nullptr;
    irr::video::ITexture* pageTex_ = nullptr;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(800, 600);
        params.Fullscreen = true;  // surfaceless-EGL idiom (test_ui_atlas_pixels)
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_OGLES2 device (surfaceless EGL unavailable)";
        driver_ = device_->getVideoDriver();
        smgr_ = device_->getSceneManager();
        auto* cam = smgr_->addCameraSceneNode(nullptr, irr::core::vector3df(0, 2, 8),
                                              irr::core::vector3df(0, 2, -32));
        cam->setNearValue(0.1f);
        cam->setFarValue(1000.0f);

        // The driver's default fog (start 50 / end 100, the FogColor default
        // of (0,0,0,0)) would paint billboards at ~40 world units fully to the
        // TRANSPARENT fog colour -- the impostor program computes its own
        // fog from the driver's getFog() values, so this is what made the
        // frame black in BOTH paths. Disable it: the pixel-identity claim is
        // about the VBO vs client-array vertex paths, not about fog.
        driver_->setFog(irr::video::SColor(0, 0, 0, 0), irr::video::EFT_FOG_LINEAR,
                        1e5f, 1e6f, 0.0f, true, false);

        // The real particle atlas (generateParticleAtlas; test_particle_atlas_alpha
        // pins it byte-for-byte): 16x16 tiles, 4x4.
        irr::video::IImage* atlasImg = generateParticleAtlas(driver_);
        ASSERT_NE(atlasImg, nullptr);
        atlas_ = driver_->addTexture("particle_atlas", atlasImg);
        atlasImg->drop();
        ASSERT_NE(atlas_, nullptr);

        // The impostor page: a 64x64 RGBA image -- a solid middle (the central
        // 40x40 block, alpha 255) ringed by a 12-px TRANSPARENT margin, so
        // both the 0.5 alpha test (the margin) AND the bilinear edge fringe
        // (the 40x40 boundary) are exercised. edge is the NEAREST-edge
        // distance: with max() it is >= 31 everywhere, the "solid middle"
        // branch never fired and the whole page uploaded as alpha 0 -- the
        // impostor program's `discard if t.a < 0.5` then ate every fragment
        // and both paths rendered an entirely black frame.
        irr::video::IImage* pageImg = driver_->createImage(
            irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(64, 64));
        ASSERT_NE(pageImg, nullptr);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const int edge = std::min({x, 63 - x, y, 63 - y});
                pageImg->setPixel(x, y,
                                  edge < 12
                                      ? irr::video::SColor(0, 200, 120, 60)
                                      : irr::video::SColor(255, 200, 120, 60));
            }
        }
        pageTex_ = driver_->addTexture("impostor_page", pageImg);
        pageImg->drop();
        ASSERT_NE(pageTex_, nullptr);
    }

    void TearDown() override {
        // B972: the driver OWNS these textures (addTexture grabs them and
        // deleteAllTextures drops the last reference at device destruction).
        // Dropping them here would release the reference the driver still
        // lists, and device drop's deleteAllTextures would read the freed
        // COGLES2Texture (heap-use-after-free, pinned by ASan). Every other
        // device test in the tree leaves driver-made textures to the device.
        if (device_) device_->drop();
        driver_ = nullptr;
        smgr_ = nullptr;
        atlas_ = nullptr;
        pageTex_ = nullptr;
        device_ = nullptr;
    }

    // The prime + repeat frames the surfaceless backend needs
    // (test_ui_atlas_pixels: the first frame after device creation loses
    // content, so render the content frame twice before the capture).
    irr::video::IImage* renderFrame(const std::function<void()>& content) {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->endScene();
        irr::video::IImage* img = nullptr;
        for (int frame = 0; frame < 2; ++frame) {
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            content();
            driver_->endScene();
            if (frame == 1) img = driver_->createScreenShot();
        }
        return img;
    }

    static size_t diffPixels(const irr::video::IImage& a, const irr::video::IImage& b) {
        const auto dim = a.getDimension();
        size_t diff = 0;
        for (irr::u32 y = 0; y < dim.Height; ++y) {
            for (irr::u32 x = 0; x < dim.Width; ++x) {
                if (a.getPixel(x, y) != b.getPixel(x, y)) ++diff;
            }
        }
        return diff;
    }

    static bool anyNonBlack(const irr::video::IImage& img) {
        const auto dim = img.getDimension();
        for (irr::u32 y = 0; y < dim.Height; y += 7) {
            for (irr::u32 x = 0; x < dim.Width; x += 7) {
                const auto c = img.getPixel(x, y);
                if (c.getRed() + c.getGreen() + c.getBlue() > 0) return true;
            }
        }
        return false;
    }
};

// The particle population: both blend modes, every procedural atlas tile,
// point sizes on both sides of the perspective clamp, colours that mix
// differently per blend mode.
std::vector<UnifiedParticle> makeParticles() {
    std::vector<UnifiedParticle> ps;
    // z=-28: INSIDE the camera node's own far plane (100, see SetUp) AND the
    // renderer's hand-rolled proj (1000). The old z=-40 was exactly the
    // camera's 100-unit far distance, so its w=0 clip plane ate the impostor
    // quad under drawAll() while the particle pass (1000) kept its points.
    const glm::vec3 base(0, 2, -28);
    int i = 0;
    for (int tile = 0; tile < 16 && i < 48; ++tile) {
        for (int k = 0; k < 3 && i < 48; ++k, ++i) {
            UnifiedParticle p;
            p.position = base + glm::vec3(k * 0.5f - 0.5f, i * 0.3f - 4.0f, 0.0f);
            p.color = {static_cast<float>(0.2f + 0.8f * (i % 7) / 7.0f),
                       static_cast<float>(0.2f + 0.8f * (i % 5) / 5.0f),
                       static_cast<float>(0.2f + 0.8f * (i % 3) / 3.0f),
                       0.9f};
            // k=0 is small (perspective shrinks it), k=2 is huge (the clamp
            // engages at 100). k=1 is the mid range.
            p.size = (k == 0) ? 0.1f : (k == 2 ? 200.0f : 2.0f);
            p.textureIndex = tile;  // procedural atlas (below kBase)
            p.setBlendMode(k % 2 ? UnifiedBlendMode::ALPHA : UnifiedBlendMode::ADDITIVE);
            p.setAlive(true);
            p.age = 0.3f;
            p.maxLifetime = 1.0f;
            ps.push_back(p);
        }
    }
    return ps;
}

TEST_F(B972StreamBuffersTest, ParticleVboPathIsPixelIdenticalToClientArray) {
    auto particles = makeParticles();
    // View puts the particles 40 units in front of the eye (camera at
    // (0,2,8) looking -Z); proj matches the camera node's own matrices.
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, -8.0f));
    const glm::mat4 proj =
        glm::perspective(glm::radians(60.0f), 800.0f / 600.0f, 0.1f, 1000.0f);
    const float fogColor[4] = {0.5f, 0.5f, 0.5f, 1.0f};

    size_t logicalBefore = gles2GetHWBufferMemoryUsage(driver_);

    UnifiedParticleRenderer renderer;
    ASSERT_TRUE(renderer.init(driver_));
    ASSERT_TRUE(renderer.isVboPathEnabled());

    auto render = [&](UnifiedParticleRenderer& r) {
        r.render(particles.data(), static_cast<int>(particles.size()),
                 &view[0][0], &proj[0][0], 20.0f, 200.0f, fogColor, 600.0f,
                 static_cast<GLuint>(atlas_->getDriverTextureHandle()));
    };

    // New path (default): VBO.
    renderer.setVboPathEnabled(true);
    irr::video::IImage* withVbo = renderFrame([&] { render(renderer); });

    // Old path: the pre-B972 client-array draw, same renderer state.
    renderer.setVboPathEnabled(false);
    irr::video::IImage* withArray = renderFrame([&] { render(renderer); });
    renderer.setVboPathEnabled(true);

    ASSERT_NE(withVbo, nullptr);
    ASSERT_NE(withArray, nullptr);
    ASSERT_TRUE(anyNonBlack(*withVbo))
        << "the particle frame is entirely black -- the draw never landed in the "
           "framebuffer (first-frame quirk, or the scene is wrong)";

    const size_t diff = diffPixels(*withVbo, *withArray);
    withVbo->drop();
    withArray->drop();
    ASSERT_EQ(diff, 0u)
        << diff << " pixel(s) differ between the VBO and client-array particle paths";

    // The VBO's storage is in the driver's LOGICAL buffer tally
    // (rawBufferBytes): named storage, not the 1 MiB unnamed stream slot
    // the old path warmed. The client-array path above added none.
    const size_t logicalAfter = gles2GetHWBufferMemoryUsage(driver_);
    EXPECT_GT(logicalAfter, logicalBefore)
        << "the particle VBO's " << renderer.vertexBufferBytes()
        << " bytes did not enter the driver's logical buffer tally, so the "
           "census still carries it as unnamed GEM";
    EXPECT_EQ(logicalAfter, logicalBefore + renderer.vertexBufferBytes())
        << "the logical tally gained " << (logicalAfter - logicalBefore)
        << " bytes, not the VBO's own " << renderer.vertexBufferBytes();
}

TEST_F(B972StreamBuffersTest, ImpostorVboPathIsPixelIdenticalToClientArray) {
    ImpostorBatchNode* node = new ImpostorBatchNode(smgr_->getRootSceneNode(), smgr_);
    ASSERT_TRUE(node->hasProgram());

    // Two pages: the first with three billboards at different depths, the
    // second with one. Every page uses the texture with the alpha fringe.
    node->addQuad(0, pageTex_, irr::core::vector3df(-2.0f, 2.0f, -30.0f), 1.5f, 2.0f,
                  0.0f, 0.0f, 1.0f, 1.0f);
    node->addQuad(0, pageTex_, irr::core::vector3df(2.0f, 3.0f, -34.0f), 1.0f, 1.5f,
                  0.0f, 0.0f, 0.5f, 0.5f);
    node->addQuad(0, pageTex_, irr::core::vector3df(0.0f, 1.0f, -26.0f), 0.75f, 1.0f,
                  0.5f, 0.5f, 1.0f, 1.0f);
    node->addQuad(1, pageTex_, irr::core::vector3df(4.0f, 2.0f, -32.0f), 1.2f, 1.2f,
                  0.0f, 0.25f, 0.5f, 0.75f);

    size_t logicalBefore = gles2GetHWBufferMemoryUsage(driver_);

    node->setVboPathEnabled(true);
    irr::video::IImage* withVbo = renderFrame([&] { smgr_->drawAll(); });

    node->setVboPathEnabled(false);
    irr::video::IImage* withArray = renderFrame([&] { smgr_->drawAll(); });

    ASSERT_NE(withVbo, nullptr);
    ASSERT_NE(withArray, nullptr);
    ASSERT_TRUE(anyNonBlack(*withVbo))
        << "the impostor frame is entirely black -- the billboards never landed "
           "(first-frame quirk, or the scene is wrong)";

    const size_t diff = diffPixels(*withVbo, *withArray);
    withVbo->drop();
    withArray->drop();
    ASSERT_EQ(diff, 0u)
        << diff << " pixel(s) differ between the VBO and client-array impostor paths";

    // B972-FT (alternative shape): the ring is BOUNDED and wraps without
    // allocating. ringDepth (default 3) = 3 pairs, ONE per FRAME row (the
    // frame's pages are packed into the row's single VBO/EBO pair).
    // renderFrame renders TWO frames (one prime + the capture frame) and
    // frameCount_ advances on every render() regardless of the VBO path.
    // A row's VBO grows at its OWN high-water, and the prime frame's pack
    // can differ from the capture frame's, so the wrap check must come only
    // AFTER one FULL rotation (3 renderFrames = 6 frames = every row visited
    // in both phases): from then on every row's high-water is already
    // reached and no write may allocate.
    //   rotation (3 renderFrames, VBO on):
    //     r1 rows 0,1 -> allocates pairs 0,1
    //     r2 rows 2,0 -> allocates pair 2 (pair 0 may grow)
    //     r3 rows 1,2 -> pairs 1,2 may grow
    //   -- bAfterRotation: all three pairs allocated, high-waters reached
    //   wrap (2 more renderFrames, VBO on):
    //     r4 rows 0,1, r5 rows 2,0 -> every row's high-water was seen in
    //     the rotation; nothing may allocate.
    node->setVboPathEnabled(true);
    for (int r = 0; r < 3; ++r) renderFrame([&] { smgr_->drawAll(); });  // full rotation
    const size_t bAfterRotation = gles2GetHWBufferMemoryUsage(driver_);
    EXPECT_GT(bAfterRotation, logicalBefore)
        << "the ring rows allocated no pairs -- the ring is not rotating "
           "across frames";
    renderFrame([&] { smgr_->drawAll(); });  // rows 0,1
    renderFrame([&] { smgr_->drawAll(); });  // rows 2,0: wrap, no allocation
    const size_t bAfterWrap = gles2GetHWBufferMemoryUsage(driver_);
    EXPECT_EQ(bAfterWrap, bAfterRotation)
        << "the ring grew on wrap -- it is not bounded at ringDepth pairs";

    const size_t logicalAfter = gles2GetHWBufferMemoryUsage(driver_);
    EXPECT_GT(logicalAfter, logicalBefore)
        << "the impostor VBO/EBO bytes did not enter the driver's logical buffer "
           "tally, so the census still carries them as unnamed GEM";
    EXPECT_EQ(logicalAfter, logicalBefore + node->vboBytes())
        << "the logical tally gained " << (logicalAfter - logicalBefore)
        << " bytes, not the impostor buffers' own " << node->vboBytes();

    node->drop();
}

}  // namespace
#else
// No GLES2 in this build: the cuts do not exist and neither does the net.
TEST(B972StreamBuffers, SkipsWithoutGles2) { GTEST_SKIP() << "EQT_HAS_GLES2 is not set"; }
#endif
