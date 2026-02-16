#ifndef EQT_GRAPHICS_DDS_DECODER_H
#define EQT_GRAPHICS_DDS_DECODER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace EQT {
namespace Graphics {

// DDS file header structures
struct DDSPixelFormat {
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

struct DDSHeader {
    uint32_t magic;          // "DDS "
    uint32_t size;           // Header size (124)
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DDSPixelFormat pixelFormat;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

// Decoded image data (RGBA)
struct DecodedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;  // RGBA format, row by row

    bool isValid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

// Compressed texture data (raw DXT blocks, not decompressed)
struct CompressedTextureData {
    uint32_t glFormat = 0;       // GL_COMPRESSED_RGB_S3TC_DXT1_EXT etc.
    uint32_t width = 0;
    uint32_t height = 0;
    const uint8_t* data = nullptr;  // Points into original DDS buffer (not owned)
    size_t dataSize = 0;

    bool isValid() const { return glFormat != 0 && width > 0 && height > 0 && data && dataSize > 0; }
};

// DDS decoder class
class DDSDecoder {
public:
    // Check if data is a DDS file
    static bool isDDS(const std::vector<char>& data);

    // Decode DDS data to RGBA pixels
    // Returns empty DecodedImage on failure
    static DecodedImage decode(const std::vector<char>& data);

    // Extract compressed DXT data without decompressing
    // Returns invalid CompressedTextureData if not a supported DXT format
    // The returned data pointer points into the original buffer — caller must keep it alive
    static CompressedTextureData extractCompressed(const std::vector<char>& data);

    // Calculate compressed data size in bytes for a given GL format and dimensions
    static size_t compressedSize(uint32_t glFormat, uint32_t width, uint32_t height);

private:
    // Decode DXT1 compressed data
    static bool decodeDXT1(const uint8_t* data, size_t dataSize,
                           uint32_t width, uint32_t height,
                           std::vector<uint8_t>& outPixels);

    // Decode DXT3 compressed data
    static bool decodeDXT3(const uint8_t* data, size_t dataSize,
                           uint32_t width, uint32_t height,
                           std::vector<uint8_t>& outPixels);

    // Decode DXT5 compressed data
    static bool decodeDXT5(const uint8_t* data, size_t dataSize,
                           uint32_t width, uint32_t height,
                           std::vector<uint8_t>& outPixels);

    // Helper to decode a single DXT1 block
    static void decodeDXT1Block(const uint8_t* block, uint8_t* outPixels,
                                int blockX, int blockY, int width, int height);

    // Helper to decode a single DXT3 block
    static void decodeDXT3Block(const uint8_t* block, uint8_t* outPixels,
                                int blockX, int blockY, int width, int height);

    // Helper to decode a single DXT5 block
    static void decodeDXT5Block(const uint8_t* block, uint8_t* outPixels,
                                int blockX, int blockY, int width, int height);

    // Convert 5:6:5 RGB to 8:8:8 RGB
    static void rgb565ToRGB888(uint16_t color, uint8_t& r, uint8_t& g, uint8_t& b);
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DDS_DECODER_H
