// T417 (board B300) / T420 (board B355): the SECOND particle sheet reaches the
// point sprite, a particle's ordinal selects the right SPRITE on it, and the
// particle's AGE selects the right animation FRAME within that sprite.
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
// T420 adds a fourth: (d) the sheet grew to 32x32 cells of 32x32 holding all
// sixteen frames of each sprite, and the particle draws the same frame for its
// whole life -- an animation that does not animate, which no still capture
// distinguishes from T417's behaviour.
//
// Method, and why it is decisive. A synthetic 1024x1024 sheet is painted so
// every 32x32 cell is a UNIQUE solid colour derived from its index. A single
// particle is drawn with `textureIndex = kBase + sprite` at a chosen
// normalized age, white, alpha blend, over a magenta clear, and the centre
// pixel is read back. The colour read back names the cell that was actually
// sampled, so a wrong offset is not merely a failure -- the failure message
// says which cell was drawn instead. A uniform cell colour also makes the
// result independent of filtering.
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

// A cell's identifying colour. 1024 cells do not fit in one byte channel, so
// the index is split across red (column) and green (row) on an 8-level pitch
// -- far enough apart that a one-cell offset cannot read as a rounding error,
// and unique for every one of the 1024 cells.
void cellColour(int cell, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = static_cast<uint8_t>(4 + (cell % SpellSpriteAtlas::kColumns) * 8);
    g = static_cast<uint8_t>(4 + (cell / SpellSpriteAtlas::kColumns) * 8);
    b = 128;
}

// B1188: a frame that stores MORE than one cell (storedSpan >= 2) owns a
// span x span cell region. The cell palette above cannot name one -- a frame
// region reads as its top-left cell. Its slot palette instead encodes the
// FRAME NUMBER in blue on a 14-level pitch (16 frames: 12..236, no two within
// the +/-3 read-back tolerance, and no level collides with the base palette's
// flat 128 by more than the tolerance either).
void slotColour(int frame, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = 4;
    g = 4;
    b = static_cast<uint8_t>(12 + frame * 14);
}

// Names the (frame) slot whose palette this colour is. -1 if none.
int identifySlot(const irr::video::SColor& c) {
    for (int f = 0; f < SpellSpriteAtlas::kFramesPerSprite; ++f) {
        uint8_t r = 0, g = 0, b = 0;
        slotColour(f, r, g, b);
        if (std::abs(static_cast<int>(c.getRed()) - r) <= 3 &&
            std::abs(static_cast<int>(c.getGreen()) - g) <= 3 &&
            std::abs(static_cast<int>(c.getBlue()) - b) <= 3) {
            return f;
        }
    }
    return -1;
}

int slotOf(const char* file) { return SpellSpriteAtlas::slotForFile(file); }

}  // namespace

class SpellSpriteSheetDrawTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::video::ITexture* sheet_ = nullptr;

    // Which frame the renderer must pick for a given normalized age, derived
    // here from the layout header rather than copied, so a change to the
    // frame count cannot leave this test asserting stale numbers.
    static int frameAt(float normalizedAge) {
        return SpellSpriteAtlas::frameForNormalizedAge(normalizedAge);
    }

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

    // 1024x1024, one flat colour per 32x32 cell, fully opaque.
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

        // B1188: a frame that stores more than one cell gets its SLOT palette
        // (frame number in blue) over its whole stored region, so the probe
        // can tell which frame -- not just which cell -- was sampled.
        for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
            const int span = SpellSpriteAtlas::storedSpan(sprite);
            if (span < 2) continue;
            for (int f = 0; f < SpellSpriteAtlas::framesFor(sprite); ++f) {
                int fx = 0, fy = 0;
                SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, f), fx, fy);
                uint8_t r = 0, g = 0, b = 0;
                slotColour(f, r, g, b);
                const int px = span * SpellSpriteAtlas::kCellSize;
                for (int y = 0; y < px; ++y) {
                    for (int x = 0; x < px; ++x) {
                        img->setPixel(fx + x, fy + y, irr::video::SColor(255, r, g, b));
                    }
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

    // Draw ONE particle at the given flat texture index and normalized age,
    // and read the centre. `atlasTexture` is deliberately a different texture
    // (or 0) so that a sprite particle drawn from the wrong sheet cannot
    // accidentally pass.
    //
    // The age is expressed as a fraction of a fixed one-second lifetime, so
    // the caller says "a tenth of the way through its life" and the renderer's
    // own `getNormalizedAge()` is exercised rather than bypassed.
    irr::video::SColor drawOne(uint16_t textureIndex, float normalizedAge = 0.0f,
                               GLuint atlasTexture = 0) {
        UnifiedParticleRenderer renderer;
        if (!renderer.init()) return irr::video::SColor(0, 0, 0, 0);

        UnifiedParticle p{};
        p.position = glm::vec3(0.0f, 0.0f, 0.0f);
        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        p.textureIndex = textureIndex;
        p.size = 4.0f;
        p.maxLifetime = 1.0f;
        p.age = normalizedAge;
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
        for (int cell = 0; cell < SpellSpriteAtlas::kCapacity; ++cell) {
            uint8_t r = 0, g = 0, b = 0;
            cellColour(cell, r, g, b);
            if (std::abs(static_cast<int>(c.getRed()) - r) <= 3 &&
                std::abs(static_cast<int>(c.getGreen()) - g) <= 3 &&
                std::abs(static_cast<int>(c.getBlue()) - b) <= 3) {
                return cell;
            }
        }
        return -1;
    }
};

// ---------------------------------------------------------------------------
// Frost Bolt (54) -- the worked, oracle-measured case.
// ---------------------------------------------------------------------------

TEST_F(SpellSpriteSheetDrawTest, FrostBoltImpactSamplesTheSpelD2Cell) {
    const int sprite = slotOf("speld2.tga");
    ASSERT_GE(sprite, 0);
    const int want = SpellSpriteAtlas::cellFor(sprite, 0);

    const irr::video::SColor c =
        drawOne(static_cast<uint16_t>(SpellSpriteAtlas::kBase + sprite));
    const int drawn = identifyCell(c);

    EXPECT_EQ(drawn, want)
        << "GEND10_SPRITE -> speld2.tga is sprite " << sprite << ", whose frame "
        << "0 is cell " << want << ", but the point sprite sampled cell " << drawn
        << " (read back " << c.getRed() << "," << c.getGreen() << ","
        << c.getBlue()
        << "; a magenta 255,0,255 means the sprite batch did not draw at all)";
}

TEST_F(SpellSpriteSheetDrawTest, FrostBoltImpactSamplesTheSpelU2Cell) {
    const int sprite = slotOf("spelu2.tga");
    ASSERT_GE(sprite, 0);

    const irr::video::SColor c =
        drawOne(static_cast<uint16_t>(SpellSpriteAtlas::kBase + sprite));
    EXPECT_EQ(identifyCell(c), SpellSpriteAtlas::cellFor(sprite, 0))
        << "GENU10_SPRITE -> spelu2.tga sampled the wrong cell; read back "
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
        << "both Frost Bolt sprites drew the same pixels -- the sprite ordinal "
           "is not reaching the UV offset";
}

// ---------------------------------------------------------------------------
// T420 (board B355) -- THE SPRITE ANIMATES OVER THE PARTICLE'S LIFE.
//
// This is the unit's headline assertion, stated exactly as the plan does: a
// particle at age 0 samples cell 0 of its sprite, at half life the middle
// cell, and at the end the last one. Before T420 all three drew the same cell,
// because `textureIndex` was a flat ordinal the renderer never changed.
// ---------------------------------------------------------------------------

TEST_F(SpellSpriteSheetDrawTest, AParticleWalksItsSheetFromBirthToDeath) {
    const int sprite = slotOf("speln1.tga");  // Weaken's sprite, T389-measured
    ASSERT_GE(sprite, 0);
    const uint16_t index = static_cast<uint16_t>(SpellSpriteAtlas::kBase + sprite);

    const int born = identifyCell(drawOne(index, 0.0f));
    const int half = identifyCell(drawOne(index, 0.5f));
    const int dying = identifyCell(drawOne(index, 0.999f));

    EXPECT_EQ(born, SpellSpriteAtlas::cellFor(sprite, 0))
        << "a newborn particle must sample the FIRST frame; it sampled cell "
        << born;
    EXPECT_EQ(half, SpellSpriteAtlas::cellFor(sprite, frameAt(0.5f)))
        << "at half life the particle must sample the middle frame (frame "
        << frameAt(0.5f) << "); it sampled cell " << half;
    EXPECT_EQ(dying, SpellSpriteAtlas::cellFor(
                         sprite, SpellSpriteAtlas::kFramesPerSprite - 1))
        << "at the end of its life the particle must sample the LAST frame; it "
           "sampled cell "
        << dying;

    EXPECT_NE(born, half);
    EXPECT_NE(half, dying);
}

TEST_F(SpellSpriteSheetDrawTest, EveryFrameOfASpriteIsReachableByAge) {
    // Sixteen distinct cells over sixteen equal slices of the life -- the
    // failure this catches is an animation that advances but skips or repeats
    // frames (an off-by-one in the age-to-frame map, or a rate that runs the
    // sheet out before the particle dies).
    const int sprite = slotOf("spelg1.tga");  // the rotating sword
    ASSERT_GE(sprite, 0);
    const uint16_t index = static_cast<uint16_t>(SpellSpriteAtlas::kBase + sprite);

    for (int f = 0; f < SpellSpriteAtlas::kFramesPerSprite; ++f) {
        // The middle of frame f's slice of the life.
        const float age =
            (static_cast<float>(f) + 0.5f) / SpellSpriteAtlas::kFramesPerSprite;
        EXPECT_EQ(identifyCell(drawOne(index, age)),
                  SpellSpriteAtlas::cellFor(sprite, f))
            << "age " << age << " should show frame " << f;
    }
}

TEST_F(SpellSpriteSheetDrawTest, AnOverrunParticleHoldsTheLastFrameRatherThanWrapping) {
    // A particle drawn on the tick after its lifetime expires must not wrap
    // round to frame 0 -- for a sheet whose frame 0 is faint that would be a
    // one-frame dip at the end of every particle's life. (B1124: was spelb3,
    // which is a single image, not a grid.)
    const int sprite = slotOf("spelg1.tga");
    ASSERT_GE(sprite, 0);
    const uint16_t index = static_cast<uint16_t>(SpellSpriteAtlas::kBase + sprite);

    const int last =
        SpellSpriteAtlas::cellFor(sprite, SpellSpriteAtlas::kFramesPerSprite - 1);
    EXPECT_EQ(identifyCell(drawOne(index, 1.0f)), last);
    EXPECT_EQ(identifyCell(drawOne(index, 3.7f)), last)
        << "a particle past its lifetime wrapped instead of holding";
}

// ---------------------------------------------------------------------------
// The whole sheet, and the failure modes.
// ---------------------------------------------------------------------------

TEST_F(SpellSpriteSheetDrawTest, EveryShippedSpriteDrawsItsOwnPixels) {
    // The region-size failure (b) shows up here as an early cell reading a
    // neighbour's colour; the offset failure (c) as a consistent shift.
    // Three frames of every sprite -- first, middle and last -- covers every
    // row of the sheet and both ends of every animation without paying for
    // 528 separate GL draws.
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        const uint16_t index =
            static_cast<uint16_t>(SpellSpriteAtlas::kBase + sprite);
        for (float age : {0.0f, 0.5f, 0.999f}) {
            // B1127: a 2x2 sheet walks its 4 frames, a single image holds 0.
            const int frame =
                SpellSpriteAtlas::frameForNormalizedAge(age, SpellSpriteAtlas::framesFor(sprite));
            const irr::video::SColor c = drawOne(index, age);
            if (SpellSpriteAtlas::storedSpan(sprite) >= 2) {
                // B1188: a stored frame spans several cells and wears its
                // slot palette -- the read-back must name the FRAME.
                EXPECT_EQ(identifySlot(c), frame)
                    << "sprite " << sprite << " (" << SpellSpriteAtlas::kFiles[sprite]
                    << ") at age " << age << " read back " << c.getRed() << ","
                    << c.getGreen() << "," << c.getBlue();
            } else {
                EXPECT_EQ(identifyCell(c), SpellSpriteAtlas::cellFor(sprite, frame))
                    << "sprite " << sprite << " (" << SpellSpriteAtlas::kFiles[sprite]
                    << ") at age " << age << " read back " << c.getRed() << ","
                    << c.getGreen() << "," << c.getBlue();
            }
        }
    }
}

TEST_F(SpellSpriteSheetDrawTest, ASingleImageSpriteDrawsItsOnePictureForItsWholeLife) {
    // B1124 (#444, B1118 M8): spelc3 / spelc4 are ONE 64x64 picture each
    // (record 0 = spells 93 / 38). The renderer must sample the sprite's
    // first cell at every age -- before B1124 it walked fifteen more cells,
    // which held 16x16 fragments of the star and now hold nothing.
    for (const char* file : {"spelc3.tga", "spelc4.tga"}) {
        const int sprite = slotOf(file);
        ASSERT_GE(sprite, 0);
        const uint16_t index = static_cast<uint16_t>(SpellSpriteAtlas::kBase + sprite);
        const int first = SpellSpriteAtlas::cellFor(sprite, 0);
        for (float age : {0.0f, 0.3f, 0.5f, 0.999f, 3.7f}) {
            const irr::video::SColor c = drawOne(index, age);
            if (SpellSpriteAtlas::storedSpan(sprite) >= 2) {
                // B1188: the 64x64 picture is stored 1:1 in its 2x2 sub-block
                // and the particle's span covers it whole; the read-back must
                // name frame 0 at every age.
                EXPECT_EQ(identifySlot(c), 0)
                    << file << " at age " << age << " did not hold its one picture"
                    << " (read back " << c.getRed() << "," << c.getGreen() << ","
                    << c.getBlue() << ")";
            } else {
                EXPECT_EQ(identifyCell(c), first)
                    << file << " at age " << age << " did not hold its one picture";
            }
        }
    }
}

TEST_F(SpellSpriteSheetDrawTest, TheSpriteSheetIsNotSampledAtTheAtlasRegionSize) {
    // If uAtlasRegionSize were left at 0.25 the sprite would cover an 8x8
    // block of cells rather than one. The last shipped cell is the sharpest
    // probe: at any region size larger than 1/32 its footprint runs into the
    // unused, transparent cells beyond it and the read-back stops naming it.
    // B1127: the sheet now ends in single images, so the probe is the LAST
    // FRAME OF THE LAST 4x4 GRID: the transparent reserved cells of the
    // single image after it are what an oversized footprint would run into.
    int sprite = SpellSpriteAtlas::kFileCount - 1;
    while (sprite > 0 && SpellSpriteAtlas::framesFor(sprite) != SpellSpriteAtlas::kFramesPerSprite) --sprite;
    const int lastCell = SpellSpriteAtlas::cellFor(sprite, SpellSpriteAtlas::kFramesPerSprite - 1);
    const irr::video::SColor c =
        drawOne(static_cast<uint16_t>(SpellSpriteAtlas::kBase + sprite), 0.999f);
    EXPECT_EQ(identifyCell(c), lastCell)
        << "the last shipped cell sampled wrong -- region size is likely not "
           "1/32";
}

TEST_F(SpellSpriteSheetDrawTest, AProceduralIndexDoesNotReachTheSpriteSheet) {
    // A tile index below kBase with NO procedural atlas bound must draw
    // nothing at all: it must not fall through onto the sprite sheet. Its age
    // must not select a sprite cell either.
    const irr::video::SColor c =
        drawOne(ParticleAtlas::SoftCircle, 0.5f, /*atlas=*/0);
    EXPECT_EQ(identifyCell(c), -1)
        << "a procedural tile index sampled the SPRITE sheet (cell "
        << identifyCell(c) << ")";
}
