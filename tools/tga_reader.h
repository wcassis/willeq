// tools/tga_reader.h — minimal uncompressed/RLE TGA decoder for offline tools.
//
// T398 Leg B (board B230a): lifted out of tools/dump_icon_sheet.cpp, which
// held the ONLY TGA decoder in the tree and was linked by nothing else. The
// official client's UI art (uifiles/default/*.tga) is 32-bit uncompressed
// type-2 TGA, and `ui_atlas_builder` needs to read a rectangle out of it.
//
// Header-only on purpose: these are offline tools, not the client. No
// logging dependency — a failure returns false and fills `error`, so each
// caller reports in its own voice.
//
// Output is always tightly-packed top-down RGBA8, whatever the file's own
// origin bit and channel count say.

#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace EQT {
namespace ToolTGA {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4, top-down

    bool valid() const {
        return width > 0 && height > 0 &&
               rgba.size() == static_cast<size_t>(width) * height * 4;
    }

    // Copies a w x h rectangle out of the image. Returns false if the
    // rectangle is not fully inside the image (never clamps — a silently
    // truncated sprite is worse than a hard failure).
    bool crop(int x, int y, int w, int h, std::vector<uint8_t>& out) const {
        if (!valid() || w <= 0 || h <= 0) return false;
        if (x < 0 || y < 0 || x + w > width || y + h > height) return false;
        out.resize(static_cast<size_t>(w) * h * 4);
        for (int row = 0; row < h; ++row) {
            std::memcpy(out.data() + static_cast<size_t>(row) * w * 4,
                        rgba.data() + (static_cast<size_t>(y + row) * width + x) * 4,
                        static_cast<size_t>(w) * 4);
        }
        return true;
    }
};

// Decodes a 24- or 32-bit TGA, uncompressed (type 2) or RLE (type 10).
inline bool load(const std::string& path, Image& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "cannot open " + path;
        return false;
    }

    uint8_t header[18] = {};
    file.read(reinterpret_cast<char*>(header), 18);
    if (!file.good()) {
        error = "short header in " + path;
        return false;
    }

    const uint8_t idLength = header[0];
    if (idLength > 0) file.seekg(idLength, std::ios::cur);

    const uint8_t imageType = header[2];
    const int width  = header[12] | (header[13] << 8);
    const int height = header[14] | (header[15] << 8);
    const uint8_t bitsPerPixel = header[16];
    const uint8_t descriptor = header[17];

    if (width <= 0 || height <= 0) {
        error = "invalid dimensions in " + path;
        return false;
    }
    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        error = "unsupported bit depth " + std::to_string((int)bitsPerPixel) +
                " in " + path;
        return false;
    }
    if (imageType != 2 && imageType != 10) {
        error = "unsupported TGA image type " + std::to_string((int)imageType) +
                " in " + path;
        return false;
    }

    const int bytesPerPixel = bitsPerPixel / 8;
    const bool isRLE = (imageType == 10);
    const bool topOrigin = (descriptor & 0x20) != 0;
    const int pixelCount = width * height;

    std::vector<uint8_t> pixels(static_cast<size_t>(pixelCount) * 4);

    auto storePixel = [&](int index, const uint8_t* bgra) {
        const size_t idx = static_cast<size_t>(index) * 4;
        pixels[idx + 0] = bgra[2];  // TGA is BGR(A)
        pixels[idx + 1] = bgra[1];
        pixels[idx + 2] = bgra[0];
        pixels[idx + 3] = (bytesPerPixel == 4) ? bgra[3] : 255;
    };

    int currentPixel = 0;
    if (isRLE) {
        while (currentPixel < pixelCount && file.good()) {
            uint8_t packetHeader = 0;
            file.read(reinterpret_cast<char*>(&packetHeader), 1);
            if (!file.good()) break;
            const int count = (packetHeader & 0x7F) + 1;
            const bool runLength = (packetHeader & 0x80) != 0;
            if (runLength) {
                uint8_t pixel[4] = {0, 0, 0, 255};
                file.read(reinterpret_cast<char*>(pixel), bytesPerPixel);
                for (int i = 0; i < count && currentPixel < pixelCount; ++i)
                    storePixel(currentPixel++, pixel);
            } else {
                for (int i = 0; i < count && currentPixel < pixelCount; ++i) {
                    uint8_t pixel[4] = {0, 0, 0, 255};
                    file.read(reinterpret_cast<char*>(pixel), bytesPerPixel);
                    storePixel(currentPixel++, pixel);
                }
            }
        }
    } else {
        for (; currentPixel < pixelCount && file.good(); ) {
            uint8_t pixel[4] = {0, 0, 0, 255};
            file.read(reinterpret_cast<char*>(pixel), bytesPerPixel);
            if (!file.good()) break;
            storePixel(currentPixel++, pixel);
        }
    }

    if (currentPixel != pixelCount) {
        error = "truncated pixel data in " + path + " (" +
                std::to_string(currentPixel) + " of " +
                std::to_string(pixelCount) + " pixels)";
        return false;
    }

    // Normalise to top-down.
    if (!topOrigin) {
        std::vector<uint8_t> flipped(pixels.size());
        for (int y = 0; y < height; ++y) {
            std::memcpy(flipped.data() + static_cast<size_t>(y) * width * 4,
                        pixels.data() + static_cast<size_t>(height - 1 - y) * width * 4,
                        static_cast<size_t>(width) * 4);
        }
        pixels.swap(flipped);
    }

    out.width = width;
    out.height = height;
    out.rgba = std::move(pixels);
    error.clear();
    return true;
}

}  // namespace ToolTGA
}  // namespace EQT
