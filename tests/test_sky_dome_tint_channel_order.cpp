// T484 (board B726): fable's daylight sky is tan where the official client's is
// blue. Measured in pixels through a real headless GLES2 device, on the real
// sky assets (sky.s3d + sky.ini + sky.eqe1), through the real SkyRenderer.
//
// The defect. SkyRenderer tints the dome by writing the time-of-day colour into
// every dome vertex (`updateSkyLayerColors()`), and the dome's textured
// EMT_TRANSPARENT_VERTEX_ALPHA material draws through the built-in SOLID3D
// program, whose fragment colour is `texColor * vColor` with `vColor = aColor`.
// COpenGLES2Driver hands the four bytes at S3DVertex offset 24 to GL as
// GL_UNSIGNED_BYTE RGBA with no conversion (both drawVertexPrimitiveList() and
// the drawMeshBuffer() VBO path). An irr::video::SColor is a u32 0xAARRGGBB, so
// on a little-endian host those bytes are B,G,R,A and the shader receives the
// tint with red and blue EXCHANGED. At 11:00 the tint is (140,175,220) -- a
// blue -- and the dome was multiplied by (220,175,140) instead. normalsky.bmp's
// mean (147,172,190) times that swapped tint is (127,118,104), which is exactly
// the (127,118,104) T478's fable frames measured in the qeynos2 sky crop; the
// correct order gives (81,118,164), and the official client reads (93,135,172).
//
// What these cases pin:
//   * DriverDeliversVertexColourBytesInMemoryOrder -- the driver convention the
//     fix compensates for. A pure-red vertex colour reaches the fragment shader
//     as blue. If the driver is ever fixed this case fails, and the sky's
//     compensation (gles2_vertex_color.h) must go with it.
//   * EtcSkyTextureKeepsItsChannelOrder -- suspect (b): the ETC1 normalsky entry
//     decodes blue, i.e. neither sky.eqe1 nor the ETC1 upload swaps channels.
//   * DaylightDomeTintReachesTheShaderInRgbOrder -- the fix. The dome drawn
//     through SkyRenderer at 11:00, divided per channel by the same texture
//     drawn with a white vertex colour, must give the tint in R,G,B order.
//     Red before the fix: the ratios came back (0.86,0.69,0.55).
//
// Set T484_DUMP_DIR to also write the rendered frames as PNGs.
// Self-skips when a GLES2 device or the EQ / ETC1 assets are unavailable.

#include <gtest/gtest.h>

#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/s3d_loader.h"  // TextureInfo
#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/sky_config.h"
#include "client/graphics/sky_renderer.h"

#include <irrlicht.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace EQT::Graphics;

namespace {

const char* kEqPath = "/mnt/projects/EverQuestP1999";
const char* kSkyPack = "/mnt/projects/EverQuest_ETC1/sky.eqe1";
const char* kZone = "qeynos2";
// The sky id qeynos2's NewZone carried in T478's live session (run.log line
// "Prepared sky type 1 (weather: DefaultClear, category: 0) for zone: qeynos2").
constexpr uint8_t kServerSkyType = 1;

struct Mean {
    double r = 0, g = 0, b = 0;
};

}  // namespace

class SkyDomeTintChannelOrderTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    EQE1Loader loader_;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<SkyLoader> skyLoader_;
    std::unique_ptr<SkyConfig> skyConfig_;
    int skyTypeId_ = -1;
    std::string bgTextureName_;
    irr::video::ITexture* bgTexture_ = nullptr;

    static constexpr irr::u32 kW = 320;
    static constexpr irr::u32 kH = 240;

    void SetUp() override {
        namespace fs = std::filesystem;
        if (!fs::exists(fs::path(kEqPath) / "sky.s3d") || !fs::exists(kSkyPack)) {
            GTEST_SKIP() << "EQ sky assets not present (" << kEqPath << "/sky.s3d, "
                         << kSkyPack << ") - skipping.";
        }
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(kW, kH);
        params.Fullscreen = true;  // surfaceless-EGL idiom
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_OGLES2 device (surfaceless EGL "
                            "unavailable in this environment) - skipping.";
        }
        driver_ = device_->getVideoDriver();
        smgr_ = device_->getSceneManager();

        // The production step-10 sequence (renderer_zone_load_env.cpp):
        // sky.s3d + sky.ini, the zone's weather -> sky type, the type's
        // textures through the constrained cache from the EQE1 pack.
        skyLoader_ = std::make_unique<SkyLoader>();
        ASSERT_TRUE(skyLoader_->load(kEqPath));
        skyConfig_ = std::make_unique<SkyConfig>();
        ASSERT_TRUE(skyConfig_->loadFromFile(std::string(kEqPath) + "/sky.ini"));
        skyTypeId_ = skyConfig_->getSkyTypeIdForWeather(
            skyConfig_->getWeatherTypeForZone(kZone));

        ASSERT_TRUE(loader_.open(kSkyPack));
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addEQE1Loader(&loader_);

        const auto names = skyLoader_->getTextureNamesForSkyType(skyTypeId_);
        ASSERT_FALSE(names.empty());
        bgTextureName_ = names.front();  // background layer first
        auto texInfo = skyLoader_->getTexture(bgTextureName_);
        ASSERT_NE(texInfo, nullptr) << bgTextureName_;
        std::string lower = bgTextureName_;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        cache_->getOrLoad(lower, texInfo->data, "sky", "T484SkyDomeTint");
        bgTexture_ = cache_->uploadTexture(lower);
        ASSERT_NE(bgTexture_, nullptr) << "uploadTexture(" << lower << ") failed";
    }

    void TearDown() override {
        cache_.reset();
        loader_.close();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    Mean frameMean() {
        Mean m;
        irr::video::IImage* img = driver_->createScreenShot();
        if (!img) return m;
        const irr::core::dimension2d<irr::u32> d = img->getDimension();
        double n = 0;
        for (irr::u32 y = 0; y < d.Height; ++y) {
            for (irr::u32 x = 0; x < d.Width; ++x) {
                const irr::video::SColor c = img->getPixel(x, y);
                m.r += c.getRed();
                m.g += c.getGreen();
                m.b += c.getBlue();
                n += 1;
            }
        }
        if (n > 0) {
            m.r /= n;
            m.g /= n;
            m.b /= n;
        }
        const char* dumpDir = std::getenv("T484_DUMP_DIR");
        if (dumpDir && *dumpDir) {
            const std::string name = std::string(dumpDir) + "/" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                "_" + std::to_string(dumpIndex_++) + ".png";
            driver_->writeImageToFile(img, name.c_str());
        }
        img->drop();
        return m;
    }

    // The background texture on a full-screen quad through SOLID3D, with a
    // constant vertex colour. Identity transforms: the quad is in clip space.
    Mean drawTexturedQuad(irr::video::SColor vertexColour) {
        irr::scene::SMeshBuffer* buf = new irr::scene::SMeshBuffer();
        buf->Vertices.reallocate(4);
        buf->Vertices.push_back(irr::video::S3DVertex(-1, -1, 0, 0, 0, -1, vertexColour, 0, 1));
        buf->Vertices.push_back(irr::video::S3DVertex( 1, -1, 0, 0, 0, -1, vertexColour, 1, 1));
        buf->Vertices.push_back(irr::video::S3DVertex( 1,  1, 0, 0, 0, -1, vertexColour, 1, 0));
        buf->Vertices.push_back(irr::video::S3DVertex(-1,  1, 0, 0, 0, -1, vertexColour, 0, 0));
        const irr::u16 idx[6] = {0, 1, 2, 0, 2, 3};
        buf->Indices.reallocate(6);
        for (irr::u16 i : idx) buf->Indices.push_back(i);
        buf->Material.MaterialType = irr::video::EMT_TRANSPARENT_VERTEX_ALPHA;
        buf->Material.setTexture(0, bgTexture_);
        buf->Material.FogEnable = false;
        buf->Material.Lighting = false;
        buf->Material.BackfaceCulling = false;
        buf->Material.ZBuffer = irr::video::ECFN_ALWAYS;
        buf->recalculateBoundingBox();

        const irr::core::matrix4 ident;
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->setTransform(irr::video::ETS_PROJECTION, ident);
        driver_->setTransform(irr::video::ETS_VIEW, ident);
        driver_->setTransform(irr::video::ETS_WORLD, ident);
        driver_->setMaterial(buf->Material);
        driver_->drawMeshBuffer(buf);
        driver_->endScene();
        buf->drop();
        return frameMean();
    }

    int dumpIndex_ = 0;
};

TEST_F(SkyDomeTintChannelOrderTest, DriverDeliversVertexColourBytesInMemoryOrder) {
    // normalsky is blue-dominant (mean B 190 vs R 147, sky.s3d measured), so a
    // pure-RED vertex colour must leave a red frame if channels are honoured.
    const Mean m = drawTexturedQuad(irr::video::SColor(255, 255, 0, 0));
    EXPECT_LT(m.r, 10.0) << "the GLES2 driver now honours SColor channel order for "
                            "vertex colours -- remove the sky's compensation "
                            "(include/client/graphics/gles2_vertex_color.h) and "
                            "re-check every vertex-coloured mesh (board B780). "
                            "Read back (" << m.r << "," << m.g << "," << m.b << ")";
    EXPECT_GT(m.b, 100.0) << "(" << m.r << "," << m.g << "," << m.b << ")";
}

TEST_F(SkyDomeTintChannelOrderTest, EtcSkyTextureKeepsItsChannelOrder) {
    // Suspect (a), then (b). The sky type and texture: sky.ini maps qeynos2's
    // weather to 0, which SkyRenderer::prepareSkyType() reads as "use the
    // server's id"; the server sent 1 (T478 run.log: "Prepared sky type 1
    // (weather: DefaultClear ...)"). Types 0 and 1 share background layer 11,
    // NORMALSKY in sky.wld, so the texture below is the same either way.
    EXPECT_EQ(skyTypeId_, 0);
    EXPECT_NE(bgTextureName_.find("normalsky"), std::string::npos) << bgTextureName_;
    const Mean m = drawTexturedQuad(irr::video::SColor(255, 255, 255, 255));
    // normalsky.bmp, decoded from sky.s3d: mean (147,172,190).
    EXPECT_NEAR(m.r, 147.0, 15.0) << "(" << m.r << "," << m.g << "," << m.b << ")";
    EXPECT_NEAR(m.g, 172.0, 15.0);
    EXPECT_NEAR(m.b, 190.0, 15.0);
    EXPECT_GT(m.b, m.r + 20.0) << "the ETC1 sky texture lost its blue";
}

TEST_F(SkyDomeTintChannelOrderTest, DaylightDomeTintReachesTheShaderInRgbOrder) {
    const Mean ref = drawTexturedQuad(irr::video::SColor(255, 255, 255, 255));
    ASSERT_GT(ref.r, 20.0);
    ASSERT_GT(ref.b, 20.0);

    SkyRenderer sky(smgr_, driver_, device_->getFileSystem());
    ASSERT_TRUE(sky.initializeFromPreloaded(std::move(skyLoader_), std::move(skyConfig_)));
    sky.registerUploadedTexture(bgTextureName_, bgTexture_);
    sky.prepareSkyType(kServerSkyType, kZone);
    ASSERT_TRUE(sky.isSkyPrepared());
    std::vector<irr::video::S3DVertex> verts;
    std::vector<irr::u16> indices;
    SkyRenderer::precomputeDomeMesh(verts, indices);
    sky.createSkyDomeFromPrecomputed(verts, indices);
    sky.setEnabled(true);
    sky.setCameraPosition(irr::core::vector3df(0, 0, 0));
    sky.updateTimeOfDay(11, 0);  // T478's `#set time 11 0`

    // Look straight up: the zenith is fully opaque for every sky.ini fade.
    irr::scene::ICameraSceneNode* cam = smgr_->addCameraSceneNode(
        nullptr, irr::core::vector3df(0, 0, 0), irr::core::vector3df(0, 1000, 0));
    cam->setUpVector(irr::core::vector3df(0, 0, 1));
    cam->setNearValue(1.0f);
    cam->setFarValue(5000.0f);
    smgr_->setActiveCamera(cam);

    driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    smgr_->drawAll();
    driver_->endScene();
    const Mean dome = frameMean();

    // Also dump a horizon-facing view for review (not asserted: the fade band
    // and black clear are in it).
    if (const char* d = std::getenv("T484_DUMP_DIR"); d && *d) {
        cam->setTarget(irr::core::vector3df(0, 500, 1000));
        cam->setUpVector(irr::core::vector3df(0, 1, 0));
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        smgr_->drawAll();
        driver_->endScene();
        frameMean();
    }

    const double rr = dome.r / ref.r;
    const double rg = dome.g / ref.g;
    const double rb = dome.b / ref.b;
    // DAY zenith (100,150,220) + horizon (180,200,220), averaged, brightness 1.0
    // (sky_renderer_colors.cpp): tint (140,175,220) = (0.549,0.686,0.863).
    EXPECT_NEAR(rr, 140.0 / 255.0, 0.10)
        << "dome/reference per channel = (" << rr << "," << rg << "," << rb
        << "); dome (" << dome.r << "," << dome.g << "," << dome.b << "), reference ("
        << ref.r << "," << ref.g << "," << ref.b << "). (0.86,0.69,0.55) is the "
           "time-of-day tint with red and blue exchanged (board B726).";
    EXPECT_NEAR(rg, 175.0 / 255.0, 0.10);
    EXPECT_NEAR(rb, 220.0 / 255.0, 0.10);
    EXPECT_GT(rb, rr * 1.3) << "the daylight tint must be blue-dominant on screen";
}
