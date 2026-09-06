// T406 (board B266): the other 63 shipped BMPs keep their alpha.
//
// B225 was not one file. `generate_textures --report` prints the alpha
// histogram of every generated image BEFORE it is written, and it shows that
// every generator except the particle atlas draws a real alpha channel --
// storm clouds that are meant to be 90% transparent, rain and snow sheets
// whose streaks and flakes ARE the alpha, an 8x10 cursor arrow on a
// transparent ground, a tumbleweed cutout. All of them shipped 24-bit,
// because Irrlicht's `CImageWriterBMP` emits 24 bpp and drops alpha with no
// diagnostic, and `ConstrainedTextureCache::decodeBMP()` then substitutes
// A=255 for every texel.
//
// This file is the census as a test. For each shipped BMP it pins:
//
//   1. the bit depth the CONSUMER needs (32 where the consumer's blend
//      samples alpha, 24 where it provably does not);
//   2. that a 32-bit file's alpha is actually non-trivial;
//   3. that the willeq.eqe1 entry carries that alpha in an ETC1 `<name>_alpha`
//      COMPANION -- see the T411 note below;
//   4. the consumer's blend mode itself, because (1) follows from it. Change
//      a consumer to additive and the file it reads has to change with it.
//
// The four files deliberately left 24-bit are listed with their reason in
// kNeverAlpha below; each is a board item or a measured "alpha is not
// sampled on this path", never an oversight.
//
// T411 (board B261/B287) INVERTED assertion (3). T406 required an in-band
// RGBA8888 payload for all 60, because the ETC1 `<name>_alpha` companion was
// sampled ONLY by the alpha-TEST shader variants and every consumer here is
// alpha-BLEND. That cost 17.2 MB of shared pack and the same again in mapped
// bytes per zone, to carry images whose entire content is one 8-bit channel.
// T411 fixed the shaders instead: `SOLID3D_FS`, `UI2D_FS` and the particle
// point sprite all multiply in `texture2D(uAlphaTexture, uv).r`, and
// `COpenGLES2Driver::bindAlphaCompanionUnit()` binds the companion from the
// unit-0 texture in one place. So every entry here is back to ETC1 + an ETC1
// companion, and `NoPackEntryNeedsTheRgba8888Payload` asserts the pack holds
// no in-band payload at all. If a future consumer genuinely cannot take a
// second sampler, put it in kInBandAlpha with the reason.

#include <gtest/gtest.h>

#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_format.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

#ifndef WILLEQ_SOURCE_DIR
#define WILLEQ_SOURCE_DIR "."
#endif

const std::string kRoot = std::string(WILLEQ_SOURCE_DIR) + "/";
const char* kWilleqEqe1 = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";

struct Entry {
    std::string file;   // basename under data/textures/
    std::string pack;   // willeq.eqe1 entry name (lowercased stem)
    std::string why;    // the consumer and its blend
};

// ── The weather set ─────────────────────────────────────────────────────────
// StormCloudLayer sets the dome node to EMT_TRANSPARENT_ALPHA_CHANNEL, which
// COpenGLES2Driver::applyMaterialRenderState() maps to
// glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) + SOLID3D_FS -- one
// sampler, alpha read from unit 0. RainOverlay and SnowOverlay draw through
// driver->draw2DImage(..., useAlphaChannelOfTexture=true), which is the same
// blend with the UI2D program: also one sampler. Every one of these three
// generators writes a CONSTANT RGB and puts the whole shape in alpha, so a
// 24-bit file is a solid rectangle of that constant colour.
std::vector<Entry> weatherSet() {
    std::vector<Entry> v;
    for (int i = 0; i < 32; ++i) {
        std::string stem = "storm_cloud_blend_" + std::to_string(i);
        v.push_back({stem + ".bmp", stem,
                     "StormCloudLayer dome, EMT_TRANSPARENT_ALPHA_CHANNEL"});
    }
    for (int i = 1; i <= 10; ++i) {
        char stem[32];
        snprintf(stem, sizeof(stem), "rain_intensity_%02d", i);
        v.push_back({std::string(stem) + ".bmp", stem,
                     "RainOverlay foreground layer, draw2DImage alpha blend"});
    }
    v.push_back({"rain_layer_mid.bmp", "rain_layer_mid",
                 "RainOverlay mid layer, draw2DImage alpha blend"});
    v.push_back({"rain_layer_far.bmp", "rain_layer_far",
                 "RainOverlay far layer, draw2DImage alpha blend"});
    for (int i = 1; i <= 10; ++i) {
        char stem[32];
        snprintf(stem, sizeof(stem), "snow_intensity_%02d", i);
        v.push_back({std::string(stem) + ".bmp", stem,
                     "SnowOverlay foreground layer, draw2DImage alpha blend"});
    }
    v.push_back({"snow_layer_mid.bmp", "snow_layer_mid",
                 "SnowOverlay mid layer, draw2DImage alpha blend"});
    v.push_back({"snow_layer_far.bmp", "snow_layer_far",
                 "SnowOverlay far layer, draw2DImage alpha blend"});
    return v;
}

// ── The software cursor ─────────────────────────────────────────────────────
// IrrlichtRenderer::createSoftwareCursor() loads it (DRM/KMS only -- there is
// no hardware cursor there) and FrameComponent::renderFrame() draws it with
// draw2DImage(..., useAlphaChannelOfTexture=true). generateSoftwareCursor()
// clears the 8x10 image to SColor(0,0,0,0) and draws a black-outlined white
// arrow over it: two alpha levels, 0 and 255. 24-bit made the transparent
// surround opaque black.
std::vector<Entry> cursorSet() {
    return {{"software_cursor.bmp", "software_cursor",
             "IrrlichtRenderer software cursor, draw2DImage alpha blend"}};
}

// ── The billboard cutouts ───────────────────────────────────────────────────
// BoidsManager (fish/birds) and TumbleweedManager both draw camera-facing
// quads with EMT_TRANSPARENT_ALPHA_CHANNEL, which the GLES2 driver maps to
// glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) plus SOLID3D_FS -- one
// sampler. Both textures are shapes on a transparent ground.
std::vector<Entry> billboardSet() {
    return {{"creature_atlas.bmp", "creature_atlas",
             "BoidsManager creature billboards, EMT_TRANSPARENT_ALPHA_CHANNEL"},
            {"tumbleweed.bmp", "tumbleweed",
             "TumbleweedManager mesh buffer, EMT_TRANSPARENT_ALPHA_CHANNEL"}};
}

// ── The particle atlas ──────────────────────────────────────────────────────
// ParticleManager uploads it and UnifiedParticleRenderer draws it as a point
// sprite through its OWN raw-GL program (POINT_SPRITE_FS), not through the
// driver's material path -- so it binds texture unit 1 itself, from the
// companion ParticleManager reads out of the cache (T411).
std::vector<Entry> particleSet() {
    return {{"particle_atlas.bmp", "particle_atlas",
             "UnifiedParticleRenderer point sprite, alpha blend"}};
}

// ── Entries that must stay in-band RGBA8888, and why ────────────────────────
// Down from 60 to 1 at T411. An entry here costs ~8x its ETC1 size in the
// shared pack AND in every zone's mapped bytes (a 256x256 with mips:
// 43,704 -> 349,524), so it needs a MEASURED reason.
//
// `ui_atlas` is not one of the 60 and its reason is not "single sampler" --
// UI2D_FS samples the companion now. It is MIP SELECTION. UIRenderer::
// drawRect() maps the atlas's 4x4 `white` sprite onto destination rects one
// pixel tall (a 32x1 gem border), which minifies 4:1 in v and selects mip
// level 2 of texture unit 1. The companion's chain is ETC1-encoded per level,
// and at level 2 the `white` sprite is one bright texel alone in a 4x4 block,
// which ETC1 cannot represent: it decodes to 217 instead of 255 (measured by
// decoding ui_atlas_alpha level 2 out of the pack), dimming every 1px UI rect
// by 15%. `UIAtlasPixelTest.SpellGemReadyBorderIsGreen` read 172 against a
// wanted 200 with the companion; it is green with the alpha in band, because
// unit 0 carries no mip filter at all (T275). The 3D alpha-test and
// alpha-blend paths never minify a 4x4 sprite that hard, which is why the
// other 60 are fine on companions.
//
// `file` is empty: ui_atlas is generated by tools/ui_atlas_builder into the
// pack staging directory, not shipped under data/textures/, so only `pack`
// and `why` are used here.
const std::vector<Entry> kInBandAlpha = {
    {"", "ui_atlas",
     "static UI drawRect: 1px-tall destination rects select companion mip "
     "level 2, where ETC1 decodes the isolated white texel as 217/255"},
};

// Every shipped BMP must appear in exactly one of the lists in this file:
// the four converted sets, the one file T400 already converted, or the
// documented 24-bit exceptions. A new generator with no census entry is the
// state B266 exists to end.
std::vector<std::string> allCensusedFiles() {
    std::vector<std::string> v;
    for (const Entry& e : particleSet()) v.push_back(e.file);  // T400, board B225
    for (const Entry& e : weatherSet()) v.push_back(e.file);
    for (const Entry& e : cursorSet()) v.push_back(e.file);
    for (const Entry& e : billboardSet()) v.push_back(e.file);
    return v;
}

// ── The files that stay 24-bit, and why ─────────────────────────────────────
struct NeverAlpha {
    std::string file;
    std::string why;
};

const std::vector<NeverAlpha> kNeverAlpha = {
    {"spell_particle_fallback.bmp",
     "board B322 (T413, MEASURED): this file is 32x32 24-bit and ALL 3,072 of "
     "its pixel bytes are 255 -- solid white, with no falloff in any channel. "
     "generateSpellParticleFallback() computes a cosine falloff and writes it "
     "into ALPHA only (SColor(alpha, 255, 255, 255)); the 24-bit BMP write "
     "discards that channel, so the shape is destroyed at generation time, not "
     "at blend time. That refutes both of B286's proposed code-side fixes: "
     "there is no alpha for a shader to premultiply and none for an alpha-blend "
     "material to use, so neither switching the material nor changing "
     "SOLID3D_FS can produce a pixel that is not full white. The fix is the "
     "GENERATOR putting the falloff in RGB (correct for the additive blend this "
     "is drawn with, and what the official client's Glowflare DDS does), which "
     "moves data/textures/, data/textures.sha256, the shared willeq.eqe1 and "
     "data/willeq.eqe1.sha256 together -- and PackReproducible --check compares "
     "against the INSTALLED pack in every worktree, so it belongs to a unit "
     "that owns the pack exclusively. Until then the entry stays 24-bit and "
     "honest: there is nothing in its alpha to carry."},
    {"detail_atlas.bmp",
     "board B265: this is the one BMP no build reproduces (a 16-bit software "
     "driver round trip plus code drift in the twelve footprint tiles). "
     "Rewriting it in 32 bpp means regenerating its pixels, which moves a "
     "texture every outdoor gate zone draws. It is RECIPE-exempt in "
     "scripts/build_textures.sh for exactly that reason."},
    {"rain_streaks.bmp",
     "no consumer: RainOverlay loads rain_intensity_01..10 plus "
     "rain_layer_mid/far and never names this file."},
    {"snow_flakes.bmp",
     "no consumer: SnowOverlay loads snow_intensity_01..10 plus "
     "snow_layer_mid/far and never names this file."},
};

// ── BMP reading, deliberately the runtime's arithmetic ──────────────────────

struct BmpHeader {
    uint32_t dataOffset = 0;
    uint32_t dibHeaderSize = 0;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t bitsPerPixel = 0;
    uint32_t compression = 0;
};

std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> data(static_cast<size_t>(n));
    if (!f.read(data.data(), n)) return {};
    return data;
}

bool parseBmpHeader(const std::vector<char>& data, BmpHeader& out) {
    if (data.size() < 54) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
    if (p[0] != 'B' || p[1] != 'M') return false;
    std::memcpy(&out.dataOffset, p + 10, 4);
    std::memcpy(&out.dibHeaderSize, p + 14, 4);
    std::memcpy(&out.width, p + 18, 4);
    std::memcpy(&out.height, p + 22, 4);
    std::memcpy(&out.bitsPerPixel, p + 28, 2);
    std::memcpy(&out.compression, p + 30, 4);
    return true;
}

// Same arithmetic as ConstrainedTextureCache::decodeBMP(), alpha=255
// substitution for 24 bpp included, so what this test reads is what the
// runtime reads.
bool decodeBmpToRgba(const std::vector<char>& data, std::vector<uint8_t>& rgba,
                     int& width, int& height) {
    BmpHeader h;
    if (!parseBmpHeader(data, h)) return false;
    if (h.width <= 0 || h.height == 0) return false;
    if (h.bitsPerPixel != 24 && h.bitsPerPixel != 32) return false;

    const bool topDown = h.height < 0;
    const int bmpHeight = topDown ? -h.height : h.height;
    width = h.width;
    height = bmpHeight;

    const int bytesPerPixel = h.bitsPerPixel / 8;
    const int rowSize = ((width * bytesPerPixel + 3) / 4) * 4;
    if (h.dataOffset + static_cast<size_t>(rowSize) * height > data.size()) return false;

    rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    const uint8_t* pixelData = reinterpret_cast<const uint8_t*>(data.data()) + h.dataOffset;

    for (int y = 0; y < height; ++y) {
        const int srcY = topDown ? y : (height - 1 - y);
        const uint8_t* row = pixelData + static_cast<size_t>(srcY) * rowSize;
        for (int x = 0; x < width; ++x) {
            const size_t dst = (static_cast<size_t>(y) * width + x) * 4;
            if (h.bitsPerPixel == 24) {
                rgba[dst + 0] = row[x * 3 + 2];
                rgba[dst + 1] = row[x * 3 + 1];
                rgba[dst + 2] = row[x * 3 + 0];
                rgba[dst + 3] = 255;
            } else {
                rgba[dst + 0] = row[x * 4 + 2];
                rgba[dst + 1] = row[x * 4 + 1];
                rgba[dst + 2] = row[x * 4 + 0];
                rgba[dst + 3] = row[x * 4 + 3];
            }
        }
    }
    return true;
}

std::string sourceText(const std::string& relPath) {
    std::vector<char> raw = readFile(kRoot + relPath);
    return std::string(raw.begin(), raw.end());
}

void expectFormat32(const std::vector<Entry>& set) {
    for (const Entry& e : set) {
        const std::string path = kRoot + "data/textures/" + e.file;
        std::vector<char> data = readFile(path);
        ASSERT_FALSE(data.empty()) << "cannot read " << path;
        BmpHeader h;
        ASSERT_TRUE(parseBmpHeader(data, h)) << "not a BMP: " << path;
        EXPECT_EQ(h.bitsPerPixel, 32)
            << e.file << " is " << h.bitsPerPixel << " bpp, so it has no alpha "
               "channel at all and decodes to A=255 everywhere. Its consumer ("
            << e.why << ") samples alpha from a single texture unit, so the "
               "texture draws as an opaque rectangle (board B266). The generator "
               "draws real alpha -- see `generate_textures --report`; write the "
               "file through tools/bmp32_writer.h by giving its recipe "
               "BmpFormat::Bmp32 in tools/generate_textures.cpp.";
    }
}

void expectNonTrivialAlpha(const std::vector<Entry>& set) {
    for (const Entry& e : set) {
        const std::string path = kRoot + "data/textures/" + e.file;
        std::vector<char> data = readFile(path);
        ASSERT_FALSE(data.empty()) << "cannot read " << path;
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        ASSERT_TRUE(decodeBmpToRgba(data, rgba, w, h)) << "failed to decode " << path;

        std::set<int> levels;
        int minA = 255, maxA = 0;
        for (size_t i = 3; i < rgba.size(); i += 4) {
            const int a = rgba[i];
            if (levels.size() < 64) levels.insert(a);
            minA = std::min(minA, a);
            maxA = std::max(maxA, a);
        }
        EXPECT_GE(levels.size(), 2u)
            << e.file << " has a single alpha level (" << minA << ") -- its shape "
               "is gone (board B266)";
        EXPECT_LT(minA, 255)
            << e.file << " never falls below alpha 255, so it has no transparent "
               "region at all and draws as a filled rectangle (board B266)";
        EXPECT_GT(maxA, 0) << e.file << " is fully transparent everywhere";
    }
}

void expectPackedWithCompanion(const std::vector<Entry>& set) {
    std::ifstream probe(kWilleqEqe1, std::ios::binary);
    if (!probe.good()) {
        GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;
    }
    probe.close();

    EQT::Graphics::EQE1Loader loader;
    ASSERT_TRUE(loader.open(kWilleqEqe1)) << "failed to open " << kWilleqEqe1;

    for (const Entry& e : set) {
        const auto* entry = loader.lookup(e.pack);
        ASSERT_NE(entry, nullptr) << "willeq.eqe1 has no '" << e.pack << "' entry";
        EXPECT_TRUE(entry->hasAlpha) << e.pack << " reports no alpha";
        EXPECT_EQ(entry->payloadFormat, EQT::Graphics::EQE1_PAYLOAD_ETC1)
            << e.pack << " is payload format " << static_cast<int>(entry->payloadFormat)
            << ", not ETC1. Its consumer (" << e.why << ") samples the ETC1 `"
            << e.pack << "_alpha` companion on texture unit 1 since T411, so the "
               "in-band RGBA8888 payload is ~8x the bytes for the same pixels "
               "(board B287). Rebuild the pack with scripts/build_willeq_pack.sh, "
               "whose --rgba8888-texture list holds only the kInBandAlpha entries.";
        EXPECT_NE(entry->alphaData, nullptr)
            << e.pack << ": ETC1 payload with hasAlpha but no companion payload -- "
               "its alpha would read 1.0 everywhere (board B261)";
        EXPECT_NE(loader.lookup(e.pack + "_alpha"), nullptr)
            << e.pack << ": no '" << e.pack << "_alpha' entry in the pack";
    }
}

}  // namespace

// ── The shipped files ───────────────────────────────────────────────────────

TEST(TextureAlphaCensus, WeatherTexturesAre32Bit) {
    expectFormat32(weatherSet());
}

TEST(TextureAlphaCensus, WeatherTexturesHaveANonTrivialAlphaHistogram) {
    expectNonTrivialAlpha(weatherSet());
}

TEST(TextureAlphaCensus, WeatherPackEntriesCarryAlphaInACompanion) {
    expectPackedWithCompanion(weatherSet());
}

TEST(TextureAlphaCensus, SoftwareCursorIs32Bit) {
    expectFormat32(cursorSet());
}

TEST(TextureAlphaCensus, SoftwareCursorHasANonTrivialAlphaHistogram) {
    expectNonTrivialAlpha(cursorSet());
}

TEST(TextureAlphaCensus, SoftwareCursorPackEntryCarriesAlphaInACompanion) {
    expectPackedWithCompanion(cursorSet());
}

TEST(TextureAlphaCensus, BillboardCutoutsAre32Bit) {
    expectFormat32(billboardSet());
}

TEST(TextureAlphaCensus, BillboardCutoutsHaveANonTrivialAlphaHistogram) {
    expectNonTrivialAlpha(billboardSet());
}

TEST(TextureAlphaCensus, BillboardPackEntriesCarryAlphaInACompanion) {
    expectPackedWithCompanion(billboardSet());
}

TEST(TextureAlphaCensus, ParticleAtlasIs32Bit) {
    expectFormat32(particleSet());
}

TEST(TextureAlphaCensus, ParticleAtlasHasANonTrivialAlphaHistogram) {
    expectNonTrivialAlpha(particleSet());
}

TEST(TextureAlphaCensus, ParticleAtlasPackEntryCarriesAlphaInACompanion) {
    expectPackedWithCompanion(particleSet());
}

// T411 (board B287): the whole point of giving the blend shaders a companion
// sampler is that the pack does not have to carry a 32-bit payload for a
// one-channel image. If this ever fails, something reintroduced an
// --rgba8888-texture override -- it needs an entry in kInBandAlpha naming the
// consumer whose shader cannot take a second sampler.
TEST(TextureAlphaCensus, NoPackEntryNeedsTheRgba8888Payload) {
    std::ifstream probe(kWilleqEqe1, std::ios::binary);
    if (!probe.good()) {
        GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;
    }
    probe.close();

    EQT::Graphics::EQE1Loader loader;
    ASSERT_TRUE(loader.open(kWilleqEqe1)) << "failed to open " << kWilleqEqe1;

    std::set<std::string> allowed;
    for (const Entry& e : kInBandAlpha) allowed.insert(e.pack);

    std::vector<std::string> offenders;
    for (const std::string& name : loader.getTextureNames()) {
        const auto* entry = loader.lookup(name);
        if (!entry) continue;
        if (entry->payloadFormat != EQT::Graphics::EQE1_PAYLOAD_ETC1 &&
            allowed.find(name) == allowed.end()) {
            offenders.push_back(name);
        }
    }
    for (const Entry& e : kInBandAlpha) {
        const auto* entry = loader.lookup(e.pack);
        ASSERT_NE(entry, nullptr) << "willeq.eqe1 has no '" << e.pack << "' entry";
        EXPECT_EQ(entry->payloadFormat, EQT::Graphics::EQE1_PAYLOAD_RGBA8888)
            << e.pack << " is listed in kInBandAlpha (" << e.why
            << ") but its pack payload is not RGBA8888 -- scripts/build_willeq_pack.sh "
               "must pass --rgba8888-texture for it";
        EXPECT_EQ(loader.lookup(e.pack + "_alpha"), nullptr)
            << e.pack << ": an in-band entry must not also carry an ETC1 companion";
    }

    EXPECT_TRUE(offenders.empty())
        << offenders.size() << " willeq.eqe1 entries carry a non-ETC1 payload with no "
           "kInBandAlpha justification (first: " << (offenders.empty() ? "" : offenders[0])
        << "). A 256x256 RGBA8888 entry with mips is 349,528 bytes against 43,688 for "
           "ETC1 + companion, and it lands on the Orange Pi's texture budget (board "
           "B287).";
}

// ── The files that stay 24-bit ──────────────────────────────────────────────

TEST(TextureAlphaCensus, TheDocumentedExceptionsStay24Bit) {
    for (const NeverAlpha& n : kNeverAlpha) {
        const std::string path = kRoot + "data/textures/" + n.file;
        std::vector<char> data = readFile(path);
        ASSERT_FALSE(data.empty()) << "cannot read " << path;
        BmpHeader h;
        ASSERT_TRUE(parseBmpHeader(data, h)) << "not a BMP: " << path;
        EXPECT_EQ(h.bitsPerPixel, 24)
            << n.file << " became 32-bit. That may well be right, but it is not "
               "free -- it grows the shared willeq.eqe1 and needs a pack payload "
               "override to be visible at all. The recorded reason it was left "
               "alone is:\n  " << n.why;
    }
}

// ── The consumers' blend modes ──────────────────────────────────────────────
//
// A source-level pin on purpose: every one of these materials is configured
// inside a method that needs a live IVideoDriver, and the point is not to
// re-run the setter but to fail loudly if the BLEND changes, because the
// texture format above is derived from it.

TEST(TextureAlphaCensus, StormCloudLayerBlendsWithAlpha) {
    const std::string src = sourceText("src/client/graphics/environment/storm_cloud_layer.cpp");
    ASSERT_FALSE(src.empty());
    EXPECT_NE(src.find("setMaterialType(irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL)"),
              std::string::npos)
        << "the storm cloud dome no longer uses EMT_TRANSPARENT_ALPHA_CHANNEL. The "
           "32 storm_cloud_blend_* BMPs are constant-RGB images whose whole shape "
           "is alpha; if this became EMT_TRANSPARENT_ADD_COLOR (GL_ONE, GL_ONE) "
           "their alpha would stop being sampled and the files would need to carry "
           "the shape in RGB instead.";
    EXPECT_EQ(src.find("EMT_TRANSPARENT_ADD_COLOR"), std::string::npos);
}

TEST(TextureAlphaCensus, TheSoftwareCursorIsDrawnWithItsTextureAlpha) {
    const std::string src = sourceText("src/client/graphics/frame/renderer_frame_render.cpp");
    ASSERT_FALSE(src.empty());
    EXPECT_NE(src.find("draw2DImage(owner_.softwareCursorTexture_, destPos, srcRect,"),
              std::string::npos)
        << "the software cursor is no longer drawn through draw2DImage; its BMP "
           "carries the arrow's shape entirely in alpha.";
    // The literal `true` on the next line is useAlphaChannelOfTexture.
    const size_t at = src.find("draw2DImage(owner_.softwareCursorTexture_");
    if (at != std::string::npos) {
        EXPECT_NE(src.find("SColor(255, 255, 255, 255), true);", at), std::string::npos)
            << "the software cursor draw dropped useAlphaChannelOfTexture=true, so "
               "its transparent surround would draw as an opaque rectangle.";
    }
}

TEST(TextureAlphaCensus, RainAndSnowOverlaysDrawWithTheTextureAlpha) {
    for (const char* tu : {"src/client/graphics/environment/rain_overlay.cpp",
                           "src/client/graphics/environment/snow_overlay.cpp"}) {
        const std::string src = sourceText(tu);
        ASSERT_FALSE(src.empty()) << tu;
        // The trailing `true` is draw2DImage's useAlphaChannelOfTexture, which
        // is what selects glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
        EXPECT_NE(src.find("draw2DImage(layerTexture, destRect, srcRect, nullptr, colors, true)"),
                  std::string::npos)
            << tu << " no longer draws its layers with useAlphaChannelOfTexture=true. "
                     "The 24 rain/snow BMPs put their streaks and flakes entirely in "
                     "alpha; without that flag they draw as opaque sheets.";
    }
}

// ── Nothing is missing from the census ──────────────────────────────────────

TEST(TextureAlphaCensus, EveryShippedBmpIsAccountedFor) {
    // The pinned set is the authority on what ships (scripts/build_textures.sh
    // asserts data/textures/ holds exactly it).
    std::vector<char> raw = readFile(kRoot + "data/textures.sha256");
    ASSERT_FALSE(raw.empty()) << "cannot read data/textures.sha256";

    std::set<std::string> pinned;
    std::istringstream in(std::string(raw.begin(), raw.end()));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t sp = line.find_last_of(' ');
        if (sp == std::string::npos) continue;
        pinned.insert(line.substr(sp + 1));
    }
    ASSERT_EQ(pinned.size(), 64u) << "data/textures.sha256 does not pin 64 files";

    std::set<std::string> censused;
    for (const std::string& f : allCensusedFiles()) censused.insert(f);
    for (const NeverAlpha& n : kNeverAlpha) censused.insert(n.file);

    for (const std::string& f : pinned) {
        EXPECT_EQ(censused.count(f), 1u)
            << f << " ships but appears in no list in this file. Decide which it "
                    "is: does its consumer sample alpha from a single texture unit "
                    "(then it is 32-bit with an rgba8888 pack override), or is it "
                    "provably never sampled (then it belongs in kNeverAlpha with "
                    "the reason)? See generate_textures --report.";
    }
    for (const std::string& f : censused) {
        EXPECT_EQ(pinned.count(f), 1u)
            << f << " is censused here but is not a shipped file any more.";
    }
}
