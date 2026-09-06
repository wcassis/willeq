// T411 (board B261/B287): the ETC1 `_alpha` companion reaches the alpha-BLEND
// draws, measured in pixels through a real headless GLES2 device.
//
// The defect. The default EQE1 encode stores a texture's alpha in a SEPARATE
// `<name>_alpha` ETC1 entry uploaded to texture unit 1. Only the alpha-TEST
// shader variants (`ALPHA3D_FS`, `ATLAS_ALPHA3D_FS`) ever sampled it. Every
// alpha-BLEND consumer -- the storm cloud dome and the boids/tumbleweed
// billboards through `SOLID3D_FS`, the rain/snow overlays and the software
// cursor through `draw2DImage`'s `UI2D_FS`, and the particle point sprite --
// read `texture2D(uTexture, uv).a`, which for an ETC1 (RGB-only) texture is
// 1.0 EVERYWHERE. A texture could be `hasAlpha=1`, have a perfectly good
// companion, and still draw fully opaque, with no log line and no failing
// test. T400 and T406 worked around it by forcing 60 pack entries to an
// in-band RGBA8888 payload, which cost 17.2 MB of shared pack and the same
// again in mapped bytes per zone (board B287).
//
// The fix under test. `COGLES2Texture` now carries the companion
// (`setAlphaCompanion()`, recorded once by `ConstrainedTextureCache`), the
// driver binds unit 1 from the unit-0 texture in ONE place
// (`COpenGLES2Driver::bindAlphaCompanionUnit()`, used by the 3D material path
// AND both `draw2DImage` overloads), and the three blend shaders multiply:
// `texColor.a *= texture2D(uAlphaTexture, uv).r`. When there is no companion
// the driver binds a 1x1 opaque-white texture, so the multiply is an exact
// no-op for in-band alpha -- that is the second half of each test below.
//
// Method. A synthetic 2-entry EQE1 pack: an ETC1 RGB payload that decodes to
// solid WHITE, plus a companion that decodes to solid BLACK, i.e. alpha 0.
// Uploaded through the real `ConstrainedTextureCache` ETC1 path, drawn over a
// magenta clear, and read back.
//   * companion sampled  -> alpha 0 -> the magenta background survives.
//   * companion ignored   -> alpha 1 -> solid white (the pre-fix reading).
// A control pack with NO companion must still draw white, proving the change
// did not make every untextured-alpha draw transparent.
//
// Red/green, measured: with the `texColor.a *= ...` line removed from
// `UI2D_FS` / `SOLID3D_FS` / `POINT_SPRITE_FS`, the three
// `...CompanionMakesTheDrawTransparent` cases read (255,255,255) and fail.
//
// Self-skips when a GLES2 device is unavailable, following
// test_eqe1_alpha_companion_mip.cpp precedent.

#include <gtest/gtest.h>

#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_format.h"
#include "client/graphics/environment/unified_particle_renderer.h"

#include <irrlicht.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace EQT::Graphics;
using namespace EQT::Graphics::Environment;

namespace {

// ETC1 "individual mode" blocks, both with all four codeword bits zero, no
// diff, no flip, and every pixel index 0 (modifier +2).
//   white: base nibbles 15 -> (255,255,255), +2 clamps to 255
//   black: base nibbles 0  -> (0,0,0),       +2 gives (2,2,2)
constexpr uint8_t kWhiteBlock[8] = {0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kBlackBlock[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void appendLevel(std::vector<uint8_t>& out, uint16_t w, uint16_t h, const uint8_t block[8]) {
    const uint32_t bytes = etc1DataSize(w, h);
    for (uint32_t i = 0; i < bytes; i += 8) out.insert(out.end(), block, block + 8);
}

std::vector<uint8_t> solidChain(uint16_t w, uint16_t h, uint8_t levels, const uint8_t block[8]) {
    std::vector<uint8_t> out;
    for (uint8_t level = 0; level < levels; ++level) {
        appendLevel(out, w, h, block);
        w = static_cast<uint16_t>(w / 2 < 1 ? 1 : w / 2);
        h = static_cast<uint16_t>(h / 2 < 1 ? 1 : h / 2);
    }
    return out;
}

void putEntry(std::vector<uint8_t>& index, const char* name, uint16_t w, uint16_t h,
              uint8_t mips, bool hasAlpha, uint8_t entryType,
              uint32_t dataOffset, uint32_t dataSize) {
    EQE1IndexEntry e{};
    std::snprintf(e.name, sizeof(e.name), "%s", name);
    e.width = w;
    e.height = h;
    e.mipLevels = mips;
    e.hasAlpha = hasAlpha ? 1 : 0;
    e.entryType = entryType;
    e.payloadFormat = EQE1_PAYLOAD_ETC1;
    e.dataOffset = dataOffset;
    e.dataSize = dataSize;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&e);
    index.insert(index.end(), p, p + sizeof(e));
}

// A white ETC1 texture with (withCompanion) or without a black `_alpha`
// companion. `withCompanion == false` is the in-band/no-alpha control.
bool writePack(const std::string& path, const std::string& texName,
               uint16_t dim, bool withCompanion) {
    const uint8_t levels = static_cast<uint8_t>(computeMipLevels(dim, dim));
    const std::vector<uint8_t> rgb = solidChain(dim, dim, levels, kWhiteBlock);
    const std::vector<uint8_t> alpha = solidChain(dim, dim, levels, kBlackBlock);

    EQE1Header hdr{};
    hdr.magic = EQE1_MAGIC;
    hdr.version = EQE1_VERSION;
    hdr.numEntries = withCompanion ? 2 : 1;

    const uint32_t indexEnd =
        sizeof(EQE1Header) + hdr.numEntries * EQE1_INDEX_ENTRY_SIZE;
    const uint32_t rgbOffset = alignTo16(indexEnd);
    const uint32_t alphaOffset = alignTo16(rgbOffset + static_cast<uint32_t>(rgb.size()));

    std::vector<uint8_t> index;
    putEntry(index, texName.c_str(), dim, dim, levels, /*hasAlpha=*/withCompanion,
             withCompanion ? EQE1_TYPE_ETC1_RGBA : EQE1_TYPE_ETC1_RGB,
             rgbOffset, static_cast<uint32_t>(rgb.size()));
    if (withCompanion) {
        putEntry(index, (texName + "_alpha").c_str(), dim, dim, levels,
                 /*hasAlpha=*/false, EQE1_TYPE_ETC1_RGB, alphaOffset,
                 static_cast<uint32_t>(alpha.size()));
    }

    std::vector<uint8_t> file;
    const uint8_t* h = reinterpret_cast<const uint8_t*>(&hdr);
    file.insert(file.end(), h, h + sizeof(hdr));
    file.insert(file.end(), index.begin(), index.end());
    file.resize(rgbOffset, 0);
    file.insert(file.end(), rgb.begin(), rgb.end());
    if (withCompanion) {
        file.resize(alphaOffset, 0);
        file.insert(file.end(), alpha.begin(), alpha.end());
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
    return f.good();
}

}  // namespace

class AlphaBlendCompanionTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    EQE1Loader loader_;
    std::string packPath_;

    static constexpr irr::u32 kW = 256;
    static constexpr irr::u32 kH = 256;
    static constexpr irr::u32 kDim = 64;

    void SetUp() override {
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
    }

    void TearDown() override {
        cache_.reset();
        loader_.close();
        if (!packPath_.empty()) std::remove(packPath_.c_str());
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    // Upload the white texture (with or without its black companion) and hand
    // back the ITexture*. nullptr on any failure, with the reason in `err`.
    irr::video::ITexture* upload(const char* tag, bool withCompanion, std::string& err) {
        const std::string name = std::string("t411_") + tag;
        packPath_ = std::string("/tmp/") + name + ".eqe1";
        if (!writePack(packPath_, name, kDim, withCompanion)) {
            err = "failed to write " + packPath_;
            return nullptr;
        }
        if (!loader_.open(packPath_)) {
            err = "failed to open " + packPath_;
            return nullptr;
        }
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addEQE1Loader(&loader_);

        std::vector<char> empty;  // EQE1 supplies the bytes
        cache_->getOrLoad(name, empty, "test", "T411AlphaBlendCompanion");
        irr::video::ITexture* rgb = cache_->uploadTexture(name);
        if (!rgb) {
            err = "uploadTexture failed";
            return nullptr;
        }
        if (withCompanion && !cache_->getAlphaTexture(name)) {
            err = "companion was not uploaded";
            return nullptr;
        }
        if (!withCompanion && cache_->getAlphaTexture(name)) {
            err = "control pack unexpectedly produced a companion";
            return nullptr;
        }
        return rgb;
    }

    irr::video::SColor centrePixel() {
        irr::video::IImage* img = driver_->createScreenShot();
        if (!img) return irr::video::SColor(0, 0, 0, 0);
        irr::video::SColor c = img->getPixel(kW / 2, kH / 2);
        img->drop();
        return c;
    }

    // ---- the three consumer paths -----------------------------------------

    // UI2D_FS: draw2DImage(..., useAlphaChannelOfTexture=true). This is the
    // rain/snow overlays, the software cursor and every ui_atlas sprite.
    irr::video::SColor drawThrough2D(irr::video::ITexture* tex) {
        driver_->beginScene(true, true, irr::video::SColor(255, 255, 0, 255));
        driver_->draw2DImage(tex,
                             irr::core::rect<irr::s32>(0, 0, kW, kH),
                             irr::core::rect<irr::s32>(0, 0, kDim, kDim),
                             nullptr, nullptr, /*useAlphaChannel=*/true);
        driver_->endScene();
        return centrePixel();
    }

    // SOLID3D_FS: a full-screen quad with EMT_TRANSPARENT_ALPHA_CHANNEL, which
    // COpenGLES2Driver maps to glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
    // This is the storm cloud dome and the boids/tumbleweed billboards.
    irr::video::SColor drawThrough3D(irr::video::ITexture* tex) {
        irr::scene::SMeshBuffer* buf = new irr::scene::SMeshBuffer();
        const irr::video::SColor white(255, 255, 255, 255);
        buf->Vertices.reallocate(4);
        buf->Vertices.push_back(irr::video::S3DVertex(-0.9f, -0.9f, 0.0f, 0, 0, -1, white, 0, 1));
        buf->Vertices.push_back(irr::video::S3DVertex( 0.9f, -0.9f, 0.0f, 0, 0, -1, white, 1, 1));
        buf->Vertices.push_back(irr::video::S3DVertex( 0.9f,  0.9f, 0.0f, 0, 0, -1, white, 1, 0));
        buf->Vertices.push_back(irr::video::S3DVertex(-0.9f,  0.9f, 0.0f, 0, 0, -1, white, 0, 0));
        const irr::u16 idx[6] = {0, 1, 2, 0, 2, 3};
        buf->Indices.reallocate(6);
        for (irr::u16 i : idx) buf->Indices.push_back(i);
        buf->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
        buf->Material.setTexture(0, tex);
        buf->Material.FogEnable = false;
        buf->Material.Lighting = false;
        buf->Material.BackfaceCulling = false;
        buf->Material.ZBuffer = irr::video::ECFN_ALWAYS;
        buf->recalculateBoundingBox();

        // Identity everywhere, so the vertices above are already in clip space.
        const irr::core::matrix4 ident;
        driver_->beginScene(true, true, irr::video::SColor(255, 255, 0, 255));
        driver_->setTransform(irr::video::ETS_PROJECTION, ident);
        driver_->setTransform(irr::video::ETS_VIEW, ident);
        driver_->setTransform(irr::video::ETS_WORLD, ident);
        driver_->setMaterial(buf->Material);
        driver_->drawMeshBuffer(buf);
        driver_->endScene();
        buf->drop();
        return centrePixel();
    }

    // POINT_SPRITE_FS: UnifiedParticleRenderer, which bypasses the driver
    // entirely and talks raw GL, so it binds unit 1 itself.
    irr::video::SColor drawThroughParticles(irr::video::ITexture* tex,
                                            irr::video::ITexture* companion) {
        UnifiedParticleRenderer renderer;
        if (!renderer.init()) return irr::video::SColor(0, 0, 0, 0);

        UnifiedParticle p{};
        p.position = glm::vec3(0.0f, 0.0f, 0.0f);
        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        p.textureIndex = 0;          // atlas cell (0,0)
        p.size = 4.0f;               // * screenHeight / w  -> clamped to 100 px
        p.setAlive(true);
        p.setBlendMode(UnifiedBlendMode::ALPHA);

        const irr::core::matrix4 ident;
        const float fogColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        driver_->beginScene(true, true, irr::video::SColor(255, 255, 0, 255));
        renderer.render(&p, 1, ident.pointer(), ident.pointer(),
                        1.0e9f, 2.0e9f, fogColor, static_cast<float>(kH),
                        static_cast<GLuint>(tex->getDriverTextureHandle()),
                        companion ? static_cast<GLuint>(companion->getDriverTextureHandle())
                                  : 0u);
        driver_->endScene();
        return centrePixel();
    }
};

// ── UI2D_FS (draw2DImage): rain, snow, the software cursor, ui_atlas ────────

TEST_F(AlphaBlendCompanionTest, TwoDCompanionMakesTheDrawTransparent) {
    std::string err;
    irr::video::ITexture* tex = upload("2d_companion", /*withCompanion=*/true, err);
    ASSERT_NE(tex, nullptr) << err;
    const irr::video::SColor c = drawThrough2D(tex);
    EXPECT_LT(c.getGreen(), 40) << "draw2DImage painted an ETC1 texture OPAQUE: the "
                                   "`_alpha` companion on unit 1 was not sampled by "
                                   "UI2D_FS (board B261). Read back ("
                                << c.getRed() << "," << c.getGreen() << ","
                                << c.getBlue() << "); the magenta clear should survive.";
    EXPECT_GT(c.getRed(), 200);
    EXPECT_GT(c.getBlue(), 200);
}

TEST_F(AlphaBlendCompanionTest, TwoDWithoutACompanionStaysOpaque) {
    std::string err;
    irr::video::ITexture* tex = upload("2d_plain", /*withCompanion=*/false, err);
    ASSERT_NE(tex, nullptr) << err;
    const irr::video::SColor c = drawThrough2D(tex);
    EXPECT_GT(c.getGreen(), 200) << "a texture with NO companion must draw exactly as "
                                    "before -- the driver binds a 1x1 opaque-white "
                                    "texture on unit 1 so the multiply is a no-op.";
    EXPECT_GT(c.getRed(), 200);
    EXPECT_GT(c.getBlue(), 200);
}

// ── SOLID3D_FS: the storm cloud dome, the boids/tumbleweed billboards ──────

TEST_F(AlphaBlendCompanionTest, ThreeDCompanionMakesTheDrawTransparent) {
    std::string err;
    irr::video::ITexture* tex = upload("3d_companion", /*withCompanion=*/true, err);
    ASSERT_NE(tex, nullptr) << err;
    const irr::video::SColor c = drawThrough3D(tex);
    EXPECT_LT(c.getGreen(), 40) << "an EMT_TRANSPARENT_ALPHA_CHANNEL draw painted an "
                                   "ETC1 texture OPAQUE: SOLID3D_FS did not sample the "
                                   "companion on unit 1 (board B261). Read back ("
                                << c.getRed() << "," << c.getGreen() << ","
                                << c.getBlue() << ").";
    EXPECT_GT(c.getRed(), 200);
    EXPECT_GT(c.getBlue(), 200);
}

TEST_F(AlphaBlendCompanionTest, ThreeDWithoutACompanionStaysOpaque) {
    std::string err;
    irr::video::ITexture* tex = upload("3d_plain", /*withCompanion=*/false, err);
    ASSERT_NE(tex, nullptr) << err;
    const irr::video::SColor c = drawThrough3D(tex);
    EXPECT_GT(c.getGreen(), 200) << "no companion must mean no change on the 3D path";
    EXPECT_GT(c.getRed(), 200);
    EXPECT_GT(c.getBlue(), 200);
}

// ── POINT_SPRITE_FS: the particle atlas ────────────────────────────────────

TEST_F(AlphaBlendCompanionTest, ParticleCompanionMakesTheSpriteTransparent) {
    std::string err;
    irr::video::ITexture* tex = upload("particle_companion", /*withCompanion=*/true, err);
    ASSERT_NE(tex, nullptr) << err;
    irr::video::ITexture* companion = cache_->getAlphaTexture(tex);
    ASSERT_NE(companion, nullptr);
    const irr::video::SColor c = drawThroughParticles(tex, companion);
    EXPECT_LT(c.getGreen(), 40) << "the particle point sprite painted an ETC1 atlas "
                                   "OPAQUE: POINT_SPRITE_FS did not sample the "
                                   "companion on unit 1 (board B261). Read back ("
                                << c.getRed() << "," << c.getGreen() << ","
                                << c.getBlue() << ").";
}

TEST_F(AlphaBlendCompanionTest, ParticleWithoutACompanionStaysOpaque) {
    std::string err;
    irr::video::ITexture* tex = upload("particle_plain", /*withCompanion=*/false, err);
    ASSERT_NE(tex, nullptr) << err;
    const irr::video::SColor c = drawThroughParticles(tex, nullptr);
    EXPECT_GT(c.getGreen(), 200) << "no companion must mean no change on the particle "
                                    "path -- the renderer binds its own 1x1 white "
                                    "fallback on unit 1.";
}
