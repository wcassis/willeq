// T417 (board B300): the SECOND particle sheet reaches the point sprite, and
// a particle's cell index selects the right cell on it.
//
// The defect this guards. `UnifiedParticleRenderer` had one texture, one
// `uAtlasRegionSize` and a hard-coded 4x4 grid (`index % 4`, `* 0.25f`). The
// spell sprite sheet is 8x8, addressed from `SpellSpriteAtlas::kBase` up, and
// it is bound in its OWN batch because the sheet and its region size are
// per-draw state. Three things can silently go wrong and none of them logs:
//   (a) the sprite batch never draws at all, and a spell paints nothing;
//   (b) it draws with the procedural atlas's 0.25 region size, so every
//       sprite is a quarter of the sheet -- four cells at once;
//   (c) it draws the right size at the wrong offset, i.e. a DIFFERENT
//       sprite -- a skull where a star belongs, which looks deliberate.
//
// Method, and why it is decisive. A synthetic 512x512 sheet is painted so
// every 64x64 cell is a UNIQUE solid colour derived from its index. A single
// particle is drawn with `textureIndex = kBase + slot`, white, alpha blend,
// over a magenta clear, and the centre pixel is read back. The colour read
// back names the cell that was actually sampled, so a wrong offset is not
// merely a failure -- the failure message says which cell was drawn instead.
// A uniform cell colour also makes the result independent of filtering.
//
// Frost Bolt (54) is the worked case: record 23's Target stage resolves to
// speld2.tga and spelu2.tga (T389, oracle-measured), and those two cells must
// come back distinct and correct.
//
// Self-skips when a GLES2 device is unavailable, following
// test_alpha_blend_companion.cpp and test_eqe1_alpha_companion_mip.cpp.

#include <gtest/gtest.h>

#include "client/graphics/environment/particle_types.h"
#include "client/graphics/environment/spell_sprite_atlas.h"
#include "client/graphics/environment/unified_particle_renderer.h"

#include <irrlicht.h>

#include <cstdint>
#include <memory>
#include <string>

using namespace EQT::Graphics::Environment;

namespace {

// A cell's identifying colour. Spread over the byte range so neighbouring
// cells are far apart and a one-cell offset cannot read as a rounding error.
void cellColour(int slot, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = static_cast<uint8_t>(20 + (slot % 8) * 30);
    g = static_cast<uint8_t>(20 + (slot / 8) * 30);
    b = static_cast<uint8_t>(255 - slot * 3);
}

int slotOf(const char* file) { return SpellSpriteAtlas::slotForFile(file); }

}  // namespace

class SpellSpriteSheetDrawTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::video::ITexture* sheet_ = nullptr;

    static constexpr irr::u32 kW = 256;
    static constexpr irr::u32 kH = 256;

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
        sheet_ = makeSheet();
        ASSERT_NE(sheet_, nullptr) << "could not build the synthetic sprite sheet";
    }

    void TearDown() override {
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    // 512x512, one flat colour per 64x64 cell, fully opaque.
    irr::video::ITexture* makeSheet() {
        irr::video::IImage* img = driver_->createImage(
            irr::video::ECF_A8R8G8B8,
            irr::core::dimension2d<irr::u32>(SpellSpriteAtlas::kWidth,
                                             SpellSpriteAtlas::kHeight));
        if (!img) return nullptr;

        for (int slot = 0; slot < SpellSpriteAtlas::kCapacity; ++slot) {
            int ox = 0, oy = 0;
            SpellSpriteAtlas::cellOrigin(slot, ox, oy);
            uint8_t r = 0, g = 0, b = 0;
            cellColour(slot, r, g, b);
            for (int y = 0; y < SpellSpriteAtlas::kCellSize; ++y) {
                for (int x = 0; x < SpellSpriteAtlas::kCellSize; ++x) {
                    img->setPixel(ox + x, oy + y, irr::video::SColor(255, r, g, b));
                }
            }
        }

        // Mip maps off: a point sprite this size would otherwise select a
        // minified level where neighbouring cells have been averaged
        // together, which is exactly the ambiguity this test must not have.
        const bool hadMips = driver_->getTextureCreationFlag(
            irr::video::ETCF_CREATE_MIP_MAPS);
        driver_->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, false);
        irr::video::ITexture* tex = driver_->addTexture("t417_sheet", img);
        driver_->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS, hadMips);
        img->drop();
        return tex;
    }

    irr::video::SColor centrePixel() {
        irr::video::IImage* img = driver_->createScreenShot();
        if (!img) return irr::video::SColor(0, 0, 0, 0);
        irr::video::SColor c = img->getPixel(kW / 2, kH / 2);
        img->drop();
        return c;
    }

    // Draw ONE particle at the given flat texture index and read the centre.
    // `atlasTexture` is deliberately a different texture (or 0) so that a
    // sprite particle drawn from the wrong sheet cannot accidentally pass.
    irr::video::SColor drawOne(uint16_t textureIndex, GLuint atlasTexture = 0) {
        UnifiedParticleRenderer renderer;
        if (!renderer.init()) return irr::video::SColor(0, 0, 0, 0);

        UnifiedParticle p{};
        p.position = glm::vec3(0.0f, 0.0f, 0.0f);
        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        p.textureIndex = textureIndex;
        p.size = 4.0f;
        p.setAlive(true);
        p.setBlendMode(UnifiedBlendMode::ALPHA);

        const irr::core::matrix4 ident;
        const float fogColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        driver_->beginScene(true, true, irr::video::SColor(255, 255, 0, 255));
        renderer.render(&p, 1, ident.pointer(), ident.pointer(), 1.0e9f, 2.0e9f,
                        fogColor, static_cast<float>(kH), atlasTexture, 0u,
                        static_cast<GLuint>(sheet_->getDriverTextureHandle()), 0u);
        driver_->endScene();
        return centrePixel();
    }

    // Which cell does this colour name? -1 if none matches. Turns a wrong-UV
    // failure into a statement about which sprite was drawn.
    static int identifyCell(const irr::video::SColor& c) {
        for (int slot = 0; slot < SpellSpriteAtlas::kCapacity; ++slot) {
            uint8_t r = 0, g = 0, b = 0;
            cellColour(slot, r, g, b);
            if (std::abs(static_cast<int>(c.getRed()) - r) <= 6 &&
                std::abs(static_cast<int>(c.getGreen()) - g) <= 6 &&
                std::abs(static_cast<int>(c.getBlue()) - b) <= 6) {
                return slot;
            }
        }
        return -1;
    }
};

// ---------------------------------------------------------------------------
// Frost Bolt (54) -- the worked, oracle-measured case.
// ---------------------------------------------------------------------------

TEST_F(SpellSpriteSheetDrawTest, FrostBoltImpactSamplesTheSpelD2Cell) {
    const int slot = slotOf("speld2.tga");
    ASSERT_GE(slot, 0);

    const irr::video::SColor c =
        drawOne(static_cast<uint16_t>(SpellSpriteAtlas::kBase + slot));
    const int drawn = identifyCell(c);

    EXPECT_EQ(drawn, slot)
        << "GEND10_SPRITE -> speld2.tga is cell " << slot << ", but the point "
        << "sprite sampled cell " << drawn << " (read back " << c.getRed() << ","
        << c.getGreen() << "," << c.getBlue()
        << "; a magenta 255,0,255 means the sprite batch did not draw at all)";
}

TEST_F(SpellSpriteSheetDrawTest, FrostBoltImpactSamplesTheSpelU2Cell) {
    const int slot = slotOf("spelu2.tga");
    ASSERT_GE(slot, 0);

    const irr::video::SColor c =
        drawOne(static_cast<uint16_t>(SpellSpriteAtlas::kBase + slot));
    EXPECT_EQ(identifyCell(c), slot)
        << "GENU10_SPRITE -> spelu2.tga is cell " << slot << "; read back "
        << c.getRed() << "," << c.getGreen() << "," << c.getBlue();
}

TEST_F(SpellSpriteSheetDrawTest, TheTwoFrostBoltSpritesDrawDifferentPixels) {
    const int d2 = slotOf("speld2.tga");
    const int u2 = slotOf("spelu2.tga");
    ASSERT_GE(d2, 0);
    ASSERT_GE(u2, 0);

    const irr::video::SColor a =
        drawOne(static_cast<uint16_t>(SpellSpriteAtlas::kBase + d2));
    const irr::video::SColor b =
        drawOne(static_cast<uint16_t>(SpellSpriteAtlas::kBase + u2));
    EXPECT_NE(a.color, b.color)
        << "both Frost Bolt sprites drew the same pixels -- the cell index is "
           "not reaching the UV offset";
}

// ---------------------------------------------------------------------------
// The whole sheet, and the failure modes.
// ---------------------------------------------------------------------------

TEST_F(SpellSpriteSheetDrawTest, EveryShippedCellDrawsItsOwnPixels) {
    // The region-size failure (b) shows up here as an early cell reading a
    // neighbour's colour; the offset failure (c) as a consistent shift.
    for (int slot = 0; slot < SpellSpriteAtlas::kFileCount; ++slot) {
        const irr::video::SColor c =
            drawOne(static_cast<uint16_t>(SpellSpriteAtlas::kBase + slot));
        EXPECT_EQ(identifyCell(c), slot)
            << "cell " << slot << " (" << SpellSpriteAtlas::kFiles[slot]
            << ") read back " << c.getRed() << "," << c.getGreen() << ","
            << c.getBlue();
    }
}

TEST_F(SpellSpriteSheetDrawTest, TheSpriteSheetIsNotSampledAtTheAtlasRegionSize) {
    // If uAtlasRegionSize were left at 0.25 the sprite would cover a 2x2
    // block of cells, so the last row and column would sample outside the
    // sheet. Cell 32 is in row 4 of 8; at 0.25 its footprint would run into
    // row 6, whose colour is different in green.
    const int slot = SpellSpriteAtlas::kFileCount - 1;
    const irr::video::SColor c =
        drawOne(static_cast<uint16_t>(SpellSpriteAtlas::kBase + slot));
    EXPECT_EQ(identifyCell(c), slot)
        << "the last shipped cell sampled wrong -- region size is likely still "
           "0.25 rather than 1/8";
}

TEST_F(SpellSpriteSheetDrawTest, AProceduralIndexDoesNotReachTheSpriteSheet) {
    // A tile index below kBase with NO procedural atlas bound must draw
    // nothing at all: it must not fall through onto the sprite sheet.
    const irr::video::SColor c = drawOne(ParticleAtlas::SoftCircle, /*atlas=*/0);
    EXPECT_EQ(identifyCell(c), -1)
        << "a procedural tile index sampled the SPRITE sheet (cell "
        << identifyCell(c) << ")";
}
