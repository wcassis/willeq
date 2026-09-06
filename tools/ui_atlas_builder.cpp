/*
 * ui_atlas_builder — generates ui_atlas.bmp + ui_atlas.json
 *
 * U02: Pre-generates the UI chrome sprite atlas. Run offline, output goes
 * into the --atlas-path directory alongside zone atlases.
 *
 * T398 (board B218/B230a): it is no longer purely procedural. With
 * --uifiles-path it also lifts the four coin decals the official client
 * blits into its IW_Money buttons (A_PlatinumCoin / A_GoldCoin /
 * A_SilverCoin / A_CopperCoin) out of the oracle's own art: EQUI_Animations.xml
 * puts all four in ONE texture, window_pieces01.tga, as a contiguous 72x18
 * strip at (90,140), each animation 18x18. fable draws them in
 * InventoryLayout::coinDecalRect(), replacing T393's placeholder letter.
 *
 * Usage:
 *   ui_atlas_builder --output <atlas-path-dir> [--uifiles-path <dir>]
 *   # Creates: <atlas-path-dir>/ui_atlas.bmp + <atlas-path-dir>/ui_atlas.json
 *   # --uifiles-path is the official client's uifiles/default directory. If
 *   # given, window_pieces01.tga MUST be readable there -- a missing coin
 *   # sprite is a hard error, not a degraded atlas (Will's all-or-nothing
 *   # rule for EQE1 assets). Omitting it emits the pre-T398 atlas.
 */

#include "tga_reader.h"
#include "bmp32_writer.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <algorithm>

static constexpr int ATLAS_W = 256;
static constexpr int ATLAS_H = 256;

struct Color { uint8_t r, g, b, a; };
struct SpriteEntry { std::string name; int x, y, w, h; };

static std::vector<uint8_t> pixels(ATLAS_W * ATLAS_H * 4, 0);
static std::vector<SpriteEntry> sprites;

static void setPixel(int x, int y, Color c) {
    if (x < 0 || x >= ATLAS_W || y < 0 || y >= ATLAS_H) return;
    int idx = (y * ATLAS_W + x) * 4;
    pixels[idx + 0] = c.r;
    pixels[idx + 1] = c.g;
    pixels[idx + 2] = c.b;
    pixels[idx + 3] = c.a;
}

static void fillRect(int x, int y, int w, int h, Color c) {
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px)
            setPixel(px, py, c);
}

static void fillBorder(int x, int y, int w, int h, Color c, int t = 1) {
    for (int i = 0; i < t; ++i) {
        for (int px = x + i; px < x + w - i; ++px) { setPixel(px, y + i, c); setPixel(px, y + h - 1 - i, c); }
        for (int py = y + i; py < y + h - i; ++py) { setPixel(x + i, py, c); setPixel(x + w - 1 - i, py, c); }
    }
}

static void fillGradientV(int x, int y, int w, int h, Color top, Color bot) {
    for (int py = 0; py < h; ++py) {
        float t = (h > 1) ? (float)py / (float)(h - 1) : 0.0f;
        Color c = {
            (uint8_t)(top.r * (1 - t) + bot.r * t),
            (uint8_t)(top.g * (1 - t) + bot.g * t),
            (uint8_t)(top.b * (1 - t) + bot.b * t),
            (uint8_t)(top.a * (1 - t) + bot.a * t)
        };
        for (int px = x; px < x + w; ++px) setPixel(px, py + y, c);
    }
}

// Copies a tightly-packed top-down RGBA block into the atlas.
static void blitRGBA(int x, int y, int w, int h, const std::vector<uint8_t>& src) {
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            size_t s = (static_cast<size_t>(py) * w + px) * 4;
            setPixel(x + px, y + py, {src[s], src[s + 1], src[s + 2], src[s + 3]});
        }
    }
}

static void reg(const std::string& name, int x, int y, int w, int h) {
    sprites.push_back({name, x, y, w, h});
}

static void generate() {
    // Row 0 (y=0): Slot sprites 32x32 each
    reg("slot_background",      0, 0, 32, 32);  fillRect(0, 0, 32, 32, {20, 20, 25, 200});
    reg("slot_border_normal",  32, 0, 32, 32);  fillBorder(32, 0, 32, 32, {80, 80, 80, 255});
    reg("slot_border_hover",   64, 0, 32, 32);  fillBorder(64, 0, 32, 32, {255, 215, 0, 255});

    // T424 (board B351): row 0's fourth slot sprite (`slot_border_selected`)
    // and the whole of row 1 (`button_normal` / `button_pressed` /
    // `button_disabled`) are GONE, along with `panel_border` and
    // `scroll_indicator` below. T416's census found all six with no consumer
    // anywhere in src/, tools/ or tests/, and none of them is oracle art:
    // every one is a gradient or a 1px border invented for the WindowManager
    // UI deleted in 0f25cb57. Wiring them would INVENT a look (a 64x24
    // gradient stretched onto a button of another size; a 32x32 one-pixel
    // border stretched across a 400px panel), which is board B350's open
    // question and starts from window_pieces01.tga, not from here. The
    // surviving roster is pinned by tests/test_ui_sprite_roster.cpp.

    // Row 2 (y=64): Bar fills 128x16 each
    reg("bar_hp",         0, 64, 128, 16);  fillGradientV(0, 64, 128, 16, {40, 200, 40, 255}, {20, 140, 20, 255});
    reg("bar_mana",       0, 80, 128, 16);  fillGradientV(0, 80, 128, 16, {40, 80, 220, 255}, {20, 50, 160, 255});
    reg("bar_stamina",    0, 96, 128, 16);  fillGradientV(0, 96, 128, 16, {220, 200, 40, 255}, {160, 140, 20, 255});
    reg("bar_xp",         0, 112, 128, 16); fillGradientV(0, 112, 128, 16, {160, 40, 220, 255}, {100, 20, 160, 255});
    reg("bar_casting",    0, 128, 128, 16); fillGradientV(0, 128, 128, 16, {80, 180, 255, 255}, {40, 120, 200, 255});
    reg("bar_background", 0, 144, 128, 16); fillRect(0, 144, 128, 16, {15, 15, 20, 200});

    // Row 3 (y=160): Panels and misc
    reg("panel_background", 0, 160, 64, 64);  fillRect(0, 160, 64, 64, {10, 10, 15, 180});

    // White pixel (4x4 for safe UV sampling)
    reg("white", 252, 252, 4, 4);  fillRect(252, 252, 4, 4, {255, 255, 255, 255});
}

// ── Coin decals from the official client's own art (T398) ───────────────────
// The four coin animations live in ONE 256x256 texture as a contiguous
// 72x18 strip; EQUI_Animations.xml gives each an 18x18 cell.
static constexpr int COIN_SRC_X = 90;
static constexpr int COIN_SRC_Y = 140;
static constexpr int COIN_SIZE  = 18;

// Row 4 of the atlas (y=224) was empty. 20 px pitch keeps each 18 px tile
// inside its own 4x4 ETC1 block column, so no coin bleeds into its
// neighbour when the atlas is compressed for the pack.
static constexpr int COIN_DST_Y     = 224;
static constexpr int COIN_DST_PITCH = 20;

static const char* const kCoinSpriteNames[4] = {
    "coin_platinum",  // A_PlatinumCoin
    "coin_gold",      // A_GoldCoin
    "coin_silver",    // A_SilverCoin
    "coin_copper",    // A_CopperCoin
};

static bool generateCoins(const std::string& uifilesPath) {
    const std::string tgaPath = uifilesPath + "/window_pieces01.tga";

    EQT::ToolTGA::Image img;
    std::string error;
    if (!EQT::ToolTGA::load(tgaPath, img, error)) {
        fprintf(stderr, "Error: coin decals unavailable: %s\n", error.c_str());
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        std::vector<uint8_t> tile;
        if (!img.crop(COIN_SRC_X + i * COIN_SIZE, COIN_SRC_Y,
                      COIN_SIZE, COIN_SIZE, tile)) {
            fprintf(stderr, "Error: coin cell %d is outside %s (%dx%d)\n",
                    i, tgaPath.c_str(), img.width, img.height);
            return false;
        }
        const int dstX = i * COIN_DST_PITCH;
        blitRGBA(dstX, COIN_DST_Y, COIN_SIZE, COIN_SIZE, tile);
        reg(kCoinSpriteNames[i], dstX, COIN_DST_Y, COIN_SIZE, COIN_SIZE);
    }

    printf("Lifted 4 coin decals from %s (%d,%d %dx%d)\n",
           tgaPath.c_str(), COIN_SRC_X, COIN_SRC_Y, COIN_SIZE * 4, COIN_SIZE);
    return true;
}

static bool writeJson(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "{\n  \"width\": " << ATLAS_W << ",\n  \"height\": " << ATLAS_H << ",\n  \"sprites\": {\n";
    for (size_t i = 0; i < sprites.size(); ++i) {
        auto& s = sprites[i];
        f << "    \"" << s.name << "\": { \"x\": " << s.x << ", \"y\": " << s.y
          << ", \"w\": " << s.w << ", \"h\": " << s.h << " }";
        if (i + 1 < sprites.size()) f << ",";
        f << "\n";
    }
    f << "  }\n}\n";
    return f.good();
}

// T400: the 32-bit-with-alpha BMP encoder moved to tools/bmp32_writer.h so
// particle_atlas_builder cannot drift from it (board B225). Byte-for-byte
// the same output -- PackReproducible proves it.
static bool writeBMP(const std::string& path) {
    return willeq_tools::writeBMP32(path, pixels.data(), ATLAS_W, ATLAS_H);
}

int main(int argc, char* argv[]) {
    std::string outputDir = ".";
    std::string uifilesPath;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--output" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (std::string(argv[i]) == "--uifiles-path" && i + 1 < argc) {
            uifilesPath = argv[++i];
        } else if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printf("Usage: ui_atlas_builder --output <dir> [--uifiles-path <dir>]\n");
            printf("  Generates ui_atlas.bmp + ui_atlas.json in <dir>\n");
            printf("  --uifiles-path  the official client's uifiles/default\n");
            printf("                  directory; adds the four coin decals\n");
            printf("                  lifted from window_pieces01.tga.\n");
            return 0;
        }
    }

    generate();

    // All-or-nothing: if the caller asked for the oracle's coins and they
    // cannot be read, fail loudly rather than write an atlas whose coin
    // sprites are missing -- UIAtlas::mapSprite would silently drop the
    // names and drawSprite would emit degenerate quads.
    if (!uifilesPath.empty() && !generateCoins(uifilesPath)) {
        return 1;
    }

    std::string bmpPath = outputDir + "/ui_atlas.bmp";
    std::string jsonPath = outputDir + "/ui_atlas.json";

    if (!writeBMP(bmpPath)) {
        fprintf(stderr, "Error: BMP write failed: %s\n", bmpPath.c_str());
        return 1;
    }
    printf("Wrote %s (%dx%d)\n", bmpPath.c_str(), ATLAS_W, ATLAS_H);

    if (!writeJson(jsonPath)) {
        fprintf(stderr, "Error: Failed to write %s\n", jsonPath.c_str());
        return 1;
    }
    printf("Wrote %s (%zu sprites)\n", jsonPath.c_str(), sprites.size());

    return 0;
}
