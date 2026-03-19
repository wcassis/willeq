/*
 * ui_atlas_builder — generates ui_atlas.bmp + ui_atlas.json
 *
 * U02: Pre-generates the UI chrome sprite atlas. Run offline, output goes
 * into the --atlas-path directory alongside zone atlases.
 *
 * Usage:
 *   ui_atlas_builder --output <atlas-path-dir>
 *   # Creates: <atlas-path-dir>/ui_atlas.bmp + <atlas-path-dir>/ui_atlas.json
 */

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

static void reg(const std::string& name, int x, int y, int w, int h) {
    sprites.push_back({name, x, y, w, h});
}

static void generate() {
    // Row 0 (y=0): Slot sprites 32x32 each
    reg("slot_background",      0, 0, 32, 32);  fillRect(0, 0, 32, 32, {20, 20, 25, 200});
    reg("slot_border_normal",  32, 0, 32, 32);  fillBorder(32, 0, 32, 32, {80, 80, 80, 255});
    reg("slot_border_hover",   64, 0, 32, 32);  fillBorder(64, 0, 32, 32, {255, 215, 0, 255});
    reg("slot_border_selected",96, 0, 32, 32);  fillBorder(96, 0, 32, 32, {255, 255, 255, 255});

    // Row 1 (y=32): Buttons 64x24 each
    reg("button_normal",   0, 32, 64, 24);
    fillGradientV(0, 32, 64, 24, {70, 70, 75, 255}, {40, 40, 45, 255});
    fillBorder(0, 32, 64, 24, {100, 100, 105, 255});

    reg("button_pressed",  64, 32, 64, 24);
    fillGradientV(64, 32, 64, 24, {30, 30, 35, 255}, {55, 55, 60, 255});
    fillBorder(64, 32, 64, 24, {80, 80, 85, 255});

    reg("button_disabled", 128, 32, 64, 24);
    fillRect(128, 32, 64, 24, {35, 35, 38, 255});
    fillBorder(128, 32, 64, 24, {50, 50, 55, 255});

    // Row 2 (y=64): Bar fills 128x16 each
    reg("bar_hp",         0, 64, 128, 16);  fillGradientV(0, 64, 128, 16, {40, 200, 40, 255}, {20, 140, 20, 255});
    reg("bar_mana",       0, 80, 128, 16);  fillGradientV(0, 80, 128, 16, {40, 80, 220, 255}, {20, 50, 160, 255});
    reg("bar_stamina",    0, 96, 128, 16);  fillGradientV(0, 96, 128, 16, {220, 200, 40, 255}, {160, 140, 20, 255});
    reg("bar_xp",         0, 112, 128, 16); fillGradientV(0, 112, 128, 16, {160, 40, 220, 255}, {100, 20, 160, 255});
    reg("bar_casting",    0, 128, 128, 16); fillGradientV(0, 128, 128, 16, {80, 180, 255, 255}, {40, 120, 200, 255});
    reg("bar_background", 0, 144, 128, 16); fillRect(0, 144, 128, 16, {15, 15, 20, 200});

    // Row 3 (y=160): Panels and misc
    reg("panel_background", 0, 160, 64, 64);  fillRect(0, 160, 64, 64, {10, 10, 15, 180});
    reg("panel_border",    64, 160, 32, 32);   fillBorder(64, 160, 32, 32, {60, 60, 70, 255});
    reg("scroll_indicator", 96, 160, 8, 16);   fillRect(96, 160, 8, 16, {120, 120, 130, 200});

    // White pixel (4x4 for safe UV sampling)
    reg("white", 252, 252, 4, 4);  fillRect(252, 252, 4, 4, {255, 255, 255, 255});
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

static bool writeBMP(const std::string& path) {
    // BMP with 32-bit RGBA (BITMAPV4HEADER for alpha channel support)
    const int w = ATLAS_W;
    const int h = ATLAS_H;
    const int rowBytes = w * 4;
    const int imageSize = rowBytes * h;
    const int headerSize = 14 + 108; // file header + BITMAPV4HEADER
    const int fileSize = headerSize + imageSize;

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    // BMP file header (14 bytes)
    uint8_t fileHeader[14] = {};
    fileHeader[0] = 'B'; fileHeader[1] = 'M';
    fileHeader[2] = fileSize & 0xFF;
    fileHeader[3] = (fileSize >> 8) & 0xFF;
    fileHeader[4] = (fileSize >> 16) & 0xFF;
    fileHeader[5] = (fileSize >> 24) & 0xFF;
    fileHeader[10] = headerSize & 0xFF;
    fileHeader[11] = (headerSize >> 8) & 0xFF;
    f.write(reinterpret_cast<char*>(fileHeader), 14);

    // BITMAPV4HEADER (108 bytes)
    uint8_t dibHeader[108] = {};
    auto write32 = [&](int offset, uint32_t val) {
        dibHeader[offset] = val & 0xFF;
        dibHeader[offset+1] = (val >> 8) & 0xFF;
        dibHeader[offset+2] = (val >> 16) & 0xFF;
        dibHeader[offset+3] = (val >> 24) & 0xFF;
    };
    write32(0, 108);             // header size
    write32(4, w);               // width
    write32(8, -h);              // height (negative = top-down)
    dibHeader[12] = 1; dibHeader[13] = 0;  // planes
    dibHeader[14] = 32; dibHeader[15] = 0; // bits per pixel
    write32(16, 3);              // compression = BI_BITFIELDS
    write32(20, imageSize);      // image size
    // Channel masks: R, G, B, A
    write32(40, 0x00FF0000);     // red mask
    write32(44, 0x0000FF00);     // green mask
    write32(48, 0x000000FF);     // blue mask
    write32(52, 0xFF000000);     // alpha mask
    f.write(reinterpret_cast<char*>(dibHeader), 108);

    // Pixel data: convert RGBA to BGRA (BMP byte order)
    std::vector<uint8_t> row(rowBytes);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int srcIdx = (y * w + x) * 4;
            int dstIdx = x * 4;
            row[dstIdx + 0] = pixels[srcIdx + 2]; // B
            row[dstIdx + 1] = pixels[srcIdx + 1]; // G
            row[dstIdx + 2] = pixels[srcIdx + 0]; // R
            row[dstIdx + 3] = pixels[srcIdx + 3]; // A
        }
        f.write(reinterpret_cast<char*>(row.data()), rowBytes);
    }

    return f.good();
}

int main(int argc, char* argv[]) {
    std::string outputDir = ".";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--output" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printf("Usage: ui_atlas_builder --output <dir>\n");
            printf("  Generates ui_atlas.bmp + ui_atlas.json in <dir>\n");
            return 0;
        }
    }

    generate();

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
