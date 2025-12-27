#include "client/graphics/eq/dds_decoder.h"
#include <cstring>
#include <algorithm>

namespace EQT {
namespace Graphics {

// FourCC codes
constexpr uint32_t FOURCC_DXT1 = 0x31545844; // "DXT1"
constexpr uint32_t FOURCC_DXT3 = 0x33545844; // "DXT3"
constexpr uint32_t FOURCC_DXT5 = 0x35545844; // "DXT5"

bool DDSDecoder::isDDS(const std::vector<char>& data) {
    if (data.size() < 4) return false;
    return data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ';
}

DecodedImage DDSDecoder::decode(const std::vector<char>& data) {
    DecodedImage result;

    if (data.size() < sizeof(DDSHeader)) {
        return result;
    }

    const DDSHeader* header = reinterpret_cast<const DDSHeader*>(data.data());

    // Verify magic
    if (header->magic != 0x20534444) {  // "DDS "
        return result;
    }

    result.width = header->width;
    result.height = header->height;

    // Check compression format
    uint32_t fourCC = header->pixelFormat.fourCC;
    const uint8_t* pixelData = reinterpret_cast<const uint8_t*>(data.data()) + 128;
    size_t pixelDataSize = data.size() - 128;

    result.pixels.resize(result.width * result.height * 4);

    bool success = false;
    if (fourCC == FOURCC_DXT1) {
        success = decodeDXT1(pixelData, pixelDataSize, result.width, result.height, result.pixels);
    } else if (fourCC == FOURCC_DXT3) {
        success = decodeDXT3(pixelData, pixelDataSize, result.width, result.height, result.pixels);
    } else if (fourCC == FOURCC_DXT5) {
        success = decodeDXT5(pixelData, pixelDataSize, result.width, result.height, result.pixels);
    }

    if (!success) {
        result.width = 0;
        result.height = 0;
        result.pixels.clear();
    }

    return result;
}

void DDSDecoder::rgb565ToRGB888(uint16_t color, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = ((color >> 11) & 0x1F) * 255 / 31;
    g = ((color >> 5) & 0x3F) * 255 / 63;
    b = (color & 0x1F) * 255 / 31;
}

bool DDSDecoder::decodeDXT1(const uint8_t* data, size_t dataSize,
                           uint32_t width, uint32_t height,
                           std::vector<uint8_t>& outPixels) {
    // DXT1 blocks are 8 bytes each, covering 4x4 pixels
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    size_t requiredSize = blocksX * blocksY * 8;

    if (dataSize < requiredSize) {
        return false;
    }

    const uint8_t* block = data;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            decodeDXT1Block(block, outPixels.data(), bx * 4, by * 4, width, height);
            block += 8;
        }
    }

    return true;
}

void DDSDecoder::decodeDXT1Block(const uint8_t* block, uint8_t* outPixels,
                                 int blockX, int blockY, int width, int height) {
    // Read color endpoints
    uint16_t c0 = block[0] | (block[1] << 8);
    uint16_t c1 = block[2] | (block[3] << 8);

    // Decode colors
    uint8_t colors[4][4];  // [index][r,g,b,a]

    rgb565ToRGB888(c0, colors[0][0], colors[0][1], colors[0][2]);
    colors[0][3] = 255;

    rgb565ToRGB888(c1, colors[1][0], colors[1][1], colors[1][2]);
    colors[1][3] = 255;

    if (c0 > c1) {
        // Four color block: color2 = 2/3*c0 + 1/3*c1, color3 = 1/3*c0 + 2/3*c1
        for (int i = 0; i < 3; ++i) {
            colors[2][i] = (2 * colors[0][i] + colors[1][i]) / 3;
            colors[3][i] = (colors[0][i] + 2 * colors[1][i]) / 3;
        }
        colors[2][3] = 255;
        colors[3][3] = 255;
    } else {
        // Three color block + transparent: color2 = 1/2*c0 + 1/2*c1, color3 = transparent
        for (int i = 0; i < 3; ++i) {
            colors[2][i] = (colors[0][i] + colors[1][i]) / 2;
            colors[3][i] = 0;
        }
        colors[2][3] = 255;
        colors[3][3] = 0;  // Transparent
    }

    // Decode pixel indices
    uint32_t indices = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);

    for (int py = 0; py < 4; ++py) {
        int y = blockY + py;
        if (y >= height) continue;

        for (int px = 0; px < 4; ++px) {
            int x = blockX + px;
            if (x >= width) continue;

            int idx = (indices >> ((py * 4 + px) * 2)) & 0x3;
            int pixelOffset = (y * width + x) * 4;

            outPixels[pixelOffset + 0] = colors[idx][0];  // R
            outPixels[pixelOffset + 1] = colors[idx][1];  // G
            outPixels[pixelOffset + 2] = colors[idx][2];  // B
            outPixels[pixelOffset + 3] = colors[idx][3];  // A
        }
    }
}

bool DDSDecoder::decodeDXT3(const uint8_t* data, size_t dataSize,
                           uint32_t width, uint32_t height,
                           std::vector<uint8_t>& outPixels) {
    // DXT3 blocks are 16 bytes each (8 bytes alpha + 8 bytes color)
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    size_t requiredSize = blocksX * blocksY * 16;

    if (dataSize < requiredSize) {
        return false;
    }

    const uint8_t* block = data;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            decodeDXT3Block(block, outPixels.data(), bx * 4, by * 4, width, height);
            block += 16;
        }
    }

    return true;
}

void DDSDecoder::decodeDXT3Block(const uint8_t* block, uint8_t* outPixels,
                                 int blockX, int blockY, int width, int height) {
    // First 8 bytes: explicit alpha (4 bits per pixel)
    const uint8_t* alphaData = block;

    // Next 8 bytes: DXT1-style color block
    const uint8_t* colorBlock = block + 8;

    // Read color endpoints
    uint16_t c0 = colorBlock[0] | (colorBlock[1] << 8);
    uint16_t c1 = colorBlock[2] | (colorBlock[3] << 8);

    uint8_t colors[4][3];
    rgb565ToRGB888(c0, colors[0][0], colors[0][1], colors[0][2]);
    rgb565ToRGB888(c1, colors[1][0], colors[1][1], colors[1][2]);

    // Always 4-color mode for DXT3
    for (int i = 0; i < 3; ++i) {
        colors[2][i] = (2 * colors[0][i] + colors[1][i]) / 3;
        colors[3][i] = (colors[0][i] + 2 * colors[1][i]) / 3;
    }

    // Decode color indices
    uint32_t indices = colorBlock[4] | (colorBlock[5] << 8) |
                       (colorBlock[6] << 16) | (colorBlock[7] << 24);

    for (int py = 0; py < 4; ++py) {
        int y = blockY + py;
        if (y >= height) continue;

        for (int px = 0; px < 4; ++px) {
            int x = blockX + px;
            if (x >= width) continue;

            int pixelIdx = py * 4 + px;
            int colorIdx = (indices >> (pixelIdx * 2)) & 0x3;
            int pixelOffset = (y * width + x) * 4;

            outPixels[pixelOffset + 0] = colors[colorIdx][0];
            outPixels[pixelOffset + 1] = colors[colorIdx][1];
            outPixels[pixelOffset + 2] = colors[colorIdx][2];

            // Extract 4-bit alpha
            int alphaByteIdx = pixelIdx / 2;
            int alphaNibble = (pixelIdx % 2 == 0) ?
                              (alphaData[alphaByteIdx] & 0x0F) :
                              (alphaData[alphaByteIdx] >> 4);
            outPixels[pixelOffset + 3] = alphaNibble * 17;  // Scale 0-15 to 0-255
        }
    }
}

bool DDSDecoder::decodeDXT5(const uint8_t* data, size_t dataSize,
                           uint32_t width, uint32_t height,
                           std::vector<uint8_t>& outPixels) {
    // DXT5 blocks are 16 bytes each (8 bytes alpha + 8 bytes color)
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    size_t requiredSize = blocksX * blocksY * 16;

    if (dataSize < requiredSize) {
        return false;
    }

    const uint8_t* block = data;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            decodeDXT5Block(block, outPixels.data(), bx * 4, by * 4, width, height);
            block += 16;
        }
    }

    return true;
}

void DDSDecoder::decodeDXT5Block(const uint8_t* block, uint8_t* outPixels,
                                 int blockX, int blockY, int width, int height) {
    // First 8 bytes: interpolated alpha
    uint8_t alpha0 = block[0];
    uint8_t alpha1 = block[1];

    // Build alpha lookup table
    uint8_t alphas[8];
    alphas[0] = alpha0;
    alphas[1] = alpha1;

    if (alpha0 > alpha1) {
        // 8 alpha values
        alphas[2] = (6 * alpha0 + 1 * alpha1) / 7;
        alphas[3] = (5 * alpha0 + 2 * alpha1) / 7;
        alphas[4] = (4 * alpha0 + 3 * alpha1) / 7;
        alphas[5] = (3 * alpha0 + 4 * alpha1) / 7;
        alphas[6] = (2 * alpha0 + 5 * alpha1) / 7;
        alphas[7] = (1 * alpha0 + 6 * alpha1) / 7;
    } else {
        // 6 alpha values + 0 and 255
        alphas[2] = (4 * alpha0 + 1 * alpha1) / 5;
        alphas[3] = (3 * alpha0 + 2 * alpha1) / 5;
        alphas[4] = (2 * alpha0 + 3 * alpha1) / 5;
        alphas[5] = (1 * alpha0 + 4 * alpha1) / 5;
        alphas[6] = 0;
        alphas[7] = 255;
    }

    // Extract alpha indices (48 bits = 16 * 3 bits)
    uint64_t alphaIndices = 0;
    for (int i = 2; i < 8; ++i) {
        alphaIndices |= static_cast<uint64_t>(block[i]) << ((i - 2) * 8);
    }

    // Next 8 bytes: DXT1-style color block
    const uint8_t* colorBlock = block + 8;

    uint16_t c0 = colorBlock[0] | (colorBlock[1] << 8);
    uint16_t c1 = colorBlock[2] | (colorBlock[3] << 8);

    uint8_t colors[4][3];
    rgb565ToRGB888(c0, colors[0][0], colors[0][1], colors[0][2]);
    rgb565ToRGB888(c1, colors[1][0], colors[1][1], colors[1][2]);

    // Always 4-color mode for DXT5
    for (int i = 0; i < 3; ++i) {
        colors[2][i] = (2 * colors[0][i] + colors[1][i]) / 3;
        colors[3][i] = (colors[0][i] + 2 * colors[1][i]) / 3;
    }

    uint32_t colorIndices = colorBlock[4] | (colorBlock[5] << 8) |
                            (colorBlock[6] << 16) | (colorBlock[7] << 24);

    for (int py = 0; py < 4; ++py) {
        int y = blockY + py;
        if (y >= height) continue;

        for (int px = 0; px < 4; ++px) {
            int x = blockX + px;
            if (x >= width) continue;

            int pixelIdx = py * 4 + px;
            int colorIdx = (colorIndices >> (pixelIdx * 2)) & 0x3;
            int alphaIdx = (alphaIndices >> (pixelIdx * 3)) & 0x7;

            int pixelOffset = (y * width + x) * 4;

            outPixels[pixelOffset + 0] = colors[colorIdx][0];
            outPixels[pixelOffset + 1] = colors[colorIdx][1];
            outPixels[pixelOffset + 2] = colors[colorIdx][2];
            outPixels[pixelOffset + 3] = alphas[alphaIdx];
        }
    }
}

} // namespace Graphics
} // namespace EQT
