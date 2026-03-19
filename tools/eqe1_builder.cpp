// eqe1_builder - Offline tool to convert EQ S3D archives to EQE1 flat pack files.
//
// Reads S3D archives, decodes BMP/DDS textures to RGBA, compresses to ETC1 via Etc2Comp
// with full mipmap chains (down to 4x4), and writes .eqe1 flat pack files.
// Non-texture data (.wld files) is embedded as raw data entries.
//
// Usage:
//   ./eqe1_builder --eq-path /path/to/EverQuestP1999 --archive qeynos2.s3d --output /path/to/output/
//   ./eqe1_builder --eq-path /path/to/EverQuestP1999 --all --output /path/to/output/
//
// Design: docs/plans/full_etc1_textures.md

#include "client/graphics/eqe1_format.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/dds_decoder.h"
#include <Etc.h>
#include <EtcImage.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace EQT::Graphics;

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Strip file extension (e.g., "foo.bmp" -> "foo")
static std::string stripExtension(const std::string& name) {
    auto dot = name.rfind('.');
    if (dot != std::string::npos) return name.substr(0, dot);
    return name;
}

// ── BMP decoder ──────────────────────────────────────────────────────────────
// Handles 8-bit paletted, 24-bit, and 32-bit uncompressed BMPs.
// Output: RGBA pixels, bottom-up → top-down.

static bool decodeBMP(const std::vector<char>& data,
                      std::vector<uint8_t>& outRGBA,
                      int& outWidth, int& outHeight) {
    if (data.size() < 54) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());

    if (d[0] != 'B' || d[1] != 'M') return false;

    uint32_t dataOffset = *reinterpret_cast<const uint32_t*>(d + 10);
    int32_t width  = *reinterpret_cast<const int32_t*>(d + 18);
    int32_t height = *reinterpret_cast<const int32_t*>(d + 22);
    uint16_t bpp   = *reinterpret_cast<const uint16_t*>(d + 28);
    uint32_t compression = *reinterpret_cast<const uint32_t*>(d + 30);

    if (width <= 0 || width > 4096) return false;
    bool bottomUp = height > 0;
    if (height < 0) height = -height;
    if (height <= 0 || height > 4096) return false;

    outWidth = width;
    outHeight = height;
    outRGBA.resize(width * height * 4);

    if (bpp == 8 && compression == 0) {
        // Read colorsUsed from DIB header (offset 46). 0 means 256.
        uint32_t colorsUsed = *reinterpret_cast<const uint32_t*>(d + 46);
        if (colorsUsed == 0) colorsUsed = 256;

        uint32_t dibHeaderSize = *reinterpret_cast<const uint32_t*>(d + 14);
        const uint8_t* palette = d + 14 + dibHeaderSize;
        size_t paletteSize = colorsUsed * 4;
        if (14 + dibHeaderSize + paletteSize > data.size()) return false;

        const uint8_t* pixels = d + dataOffset;
        int rowStride = (width + 3) & ~3;

        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                uint8_t idx = pixels[srcY * rowStride + x];
                if (idx >= colorsUsed) idx = 0;  // safety clamp
                int outIdx = (y * width + x) * 4;
                outRGBA[outIdx + 0] = palette[idx * 4 + 2]; // R
                outRGBA[outIdx + 1] = palette[idx * 4 + 1]; // G
                outRGBA[outIdx + 2] = palette[idx * 4 + 0]; // B
                outRGBA[outIdx + 3] = 255;
            }
        }
        return true;
    } else if (bpp == 24 && compression == 0) {
        const uint8_t* pixels = d + dataOffset;
        int rowStride = (width * 3 + 3) & ~3;

        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                int srcIdx = srcY * rowStride + x * 3;
                int outIdx = (y * width + x) * 4;
                outRGBA[outIdx + 0] = pixels[srcIdx + 2]; // R
                outRGBA[outIdx + 1] = pixels[srcIdx + 1]; // G
                outRGBA[outIdx + 2] = pixels[srcIdx + 0]; // B
                outRGBA[outIdx + 3] = 255;
            }
        }
        return true;
    } else if (bpp == 32 && compression == 0) {
        const uint8_t* pixels = d + dataOffset;
        int rowStride = width * 4;

        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                int srcIdx = srcY * rowStride + x * 4;
                int outIdx = (y * width + x) * 4;
                outRGBA[outIdx + 0] = pixels[srcIdx + 2]; // R
                outRGBA[outIdx + 1] = pixels[srcIdx + 1]; // G
                outRGBA[outIdx + 2] = pixels[srcIdx + 0]; // B
                outRGBA[outIdx + 3] = pixels[srcIdx + 3]; // A
            }
        }
        return true;
    }

    return false;
}

// ── Alpha detection ──────────────────────────────────────────────────────────

static bool detectAlpha(const uint8_t* rgba, int w, int h) {
    for (int i = 0; i < w * h; ++i) {
        if (rgba[i * 4 + 3] < 255) return true;
    }
    return false;
}

// ── ETC1 encoding via Etc2Comp ───────────────────────────────────────────────

static std::vector<uint8_t> encodeETC1_RGB(const uint8_t* rgba, int width, int height) {
    std::vector<float> floatPixels(width * height * 4);
    for (int i = 0; i < width * height * 4; ++i) {
        floatPixels[i] = rgba[i] / 255.0f;
    }

    unsigned char* encodingBits = nullptr;
    unsigned int encodingBitsBytes = 0;
    unsigned int extendedWidth = 0, extendedHeight = 0;
    int encodingTimeMs = 0;

    Etc::Encode(floatPixels.data(),
                static_cast<unsigned int>(width),
                static_cast<unsigned int>(height),
                Etc::Image::Format::ETC1,
                Etc::ErrorMetric::RGBA,
                ETCCOMP_DEFAULT_EFFORT_LEVEL,
                1, 1024,
                &encodingBits, &encodingBitsBytes,
                &extendedWidth, &extendedHeight,
                &encodingTimeMs);

    std::vector<uint8_t> result;
    if (encodingBits && encodingBitsBytes > 0) {
        result.assign(encodingBits, encodingBits + encodingBitsBytes);
        delete[] encodingBits;
    }
    return result;
}

static std::vector<uint8_t> encodeETC1_Alpha(const uint8_t* rgba, int width, int height) {
    std::vector<float> floatPixels(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        float a = rgba[i * 4 + 3] / 255.0f;
        floatPixels[i * 4 + 0] = a;
        floatPixels[i * 4 + 1] = a;
        floatPixels[i * 4 + 2] = a;
        floatPixels[i * 4 + 3] = 1.0f;
    }

    unsigned char* encodingBits = nullptr;
    unsigned int encodingBitsBytes = 0;
    unsigned int extendedWidth = 0, extendedHeight = 0;
    int encodingTimeMs = 0;

    Etc::Encode(floatPixels.data(),
                static_cast<unsigned int>(width),
                static_cast<unsigned int>(height),
                Etc::Image::Format::ETC1,
                Etc::ErrorMetric::RGBA,
                ETCCOMP_DEFAULT_EFFORT_LEVEL,
                1, 1024,
                &encodingBits, &encodingBitsBytes,
                &extendedWidth, &extendedHeight,
                &encodingTimeMs);

    std::vector<uint8_t> result;
    if (encodingBits && encodingBitsBytes > 0) {
        result.assign(encodingBits, encodingBits + encodingBitsBytes);
        delete[] encodingBits;
    }
    return result;
}

// ── Mipmap generation ────────────────────────────────────────────────────────

// Box-filter downsample RGBA by 2x, clamping minimum dimension to 4 (ETC1 block size).
static std::vector<uint8_t> downsample2x(const uint8_t* src, int srcW, int srcH,
                                          int& dstW, int& dstH) {
    dstW = std::max(4, srcW / 2);
    dstH = std::max(4, srcH / 2);
    std::vector<uint8_t> dst(dstW * dstH * 4);

    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            int sx = std::min(x * 2, srcW - 1);
            int sy = std::min(y * 2, srcH - 1);
            int sx1 = std::min(sx + 1, srcW - 1);
            int sy1 = std::min(sy + 1, srcH - 1);

            for (int c = 0; c < 4; ++c) {
                int sum = src[(sy * srcW + sx) * 4 + c]
                        + src[(sy * srcW + sx1) * 4 + c]
                        + src[(sy1 * srcW + sx) * 4 + c]
                        + src[(sy1 * srcW + sx1) * 4 + c];
                dst[(y * dstW + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
    return dst;
}

// Generate full ETC1 mipmap chain. Returns concatenated ETC1 data for all levels.
static std::vector<uint8_t> generateMipChain(const uint8_t* rgba, int width, int height,
                                              bool isAlpha, uint8_t& outMipLevels) {
    outMipLevels = computeMipLevels(static_cast<uint16_t>(width),
                                     static_cast<uint16_t>(height));
    std::vector<uint8_t> result;

    // Level 0
    auto level0 = isAlpha ? encodeETC1_Alpha(rgba, width, height)
                          : encodeETC1_RGB(rgba, width, height);
    result.insert(result.end(), level0.begin(), level0.end());

    // Subsequent levels
    std::vector<uint8_t> current(rgba, rgba + width * height * 4);
    int w = width, h = height;

    for (uint8_t level = 1; level < outMipLevels; ++level) {
        int dw, dh;
        auto downsampled = downsample2x(current.data(), w, h, dw, dh);
        w = dw;
        h = dh;

        auto encoded = isAlpha ? encodeETC1_Alpha(downsampled.data(), w, h)
                               : encodeETC1_RGB(downsampled.data(), w, h);
        result.insert(result.end(), encoded.begin(), encoded.end());

        current = std::move(downsampled);
    }

    return result;
}

// ── Archive processing ───────────────────────────────────────────────────────

struct TextureEntry {
    std::string name;       // lowercase, no extension
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
    std::vector<uint8_t> rgbETC1;      // full mip chain
    std::vector<uint8_t> alphaETC1;    // full mip chain (empty if opaque)
    uint8_t mipLevels = 0;
};

struct RawDataEntry {
    std::string name;       // lowercase, with extension (e.g., "zone.wld")
    std::vector<char> data;
};

static bool processArchive(const std::string& eqPath,
                            const std::string& archiveName,
                            const std::string& outputDir,
                            bool verbose) {
    std::string archivePath = eqPath + "/" + archiveName;
    PfsArchive archive;
    if (!archive.open(archivePath)) {
        std::cerr << "  Could not open: " << archivePath << std::endl;
        return false;
    }

    // Extract texture filenames (.bmp and .dds)
    std::vector<std::string> bmpFiles;
    archive.getFilenames(".bmp", bmpFiles);
    std::vector<std::string> ddsFiles;
    archive.getFilenames(".dds", ddsFiles);
    bmpFiles.insert(bmpFiles.end(), ddsFiles.begin(), ddsFiles.end());

    // Extract non-texture filenames
    std::vector<std::string> wldFiles;
    archive.getFilenames(".wld", wldFiles);

    std::cout << "  Files: " << bmpFiles.size() << " textures, "
              << wldFiles.size() << " wld" << std::endl;

    // Decode and encode all textures
    std::vector<TextureEntry> textures;
    int alphaCount = 0;
    int failCount = 0;

    for (const auto& filename : bmpFiles) {
        std::vector<char> fileData;
        if (!archive.get(filename, fileData) || fileData.empty()) {
            failCount++;
            continue;
        }

        TextureEntry tex;
        tex.name = stripExtension(toLower(filename));

        // Decode BMP or DDS-as-BMP
        std::vector<uint8_t> rgba;
        if (DDSDecoder::isDDS(fileData)) {
            auto decoded = DDSDecoder::decode(fileData);
            if (!decoded.isValid()) {
                if (verbose) std::cerr << "    SKIP (DDS decode failed): " << filename << std::endl;
                failCount++;
                continue;
            }
            tex.width = decoded.width;
            tex.height = decoded.height;
            rgba = std::move(decoded.pixels);
        } else {
            if (!decodeBMP(fileData, rgba, tex.width, tex.height)) {
                if (verbose) std::cerr << "    SKIP (BMP decode failed): " << filename << std::endl;
                failCount++;
                continue;
            }
        }

        tex.hasAlpha = detectAlpha(rgba.data(), tex.width, tex.height);
        if (tex.hasAlpha) alphaCount++;

        // Encode RGB mip chain
        tex.rgbETC1 = generateMipChain(rgba.data(), tex.width, tex.height,
                                        false, tex.mipLevels);

        // Encode alpha mip chain if needed
        if (tex.hasAlpha) {
            uint8_t alphaMips = 0;
            tex.alphaETC1 = generateMipChain(rgba.data(), tex.width, tex.height,
                                              true, alphaMips);
        }

        if (verbose) {
            std::cout << "    " << tex.name << " " << tex.width << "x" << tex.height
                      << " " << (int)tex.mipLevels << " mips"
                      << " " << tex.rgbETC1.size() << " bytes"
                      << (tex.hasAlpha ? " +alpha" : "") << std::endl;
        }

        textures.push_back(std::move(tex));
    }

    // Collect raw data entries
    std::vector<RawDataEntry> rawEntries;
    for (const auto& filename : wldFiles) {
        RawDataEntry raw;
        raw.name = toLower(filename);
        if (!archive.get(filename, raw.data) || raw.data.empty()) continue;
        rawEntries.push_back(std::move(raw));
    }

    // Calculate total entries (textures + alpha companions + raw data)
    uint16_t numEntries = 0;
    for (const auto& tex : textures) {
        numEntries++;  // RGB entry
        if (tex.hasAlpha) numEntries++;  // alpha companion entry
    }
    numEntries += static_cast<uint16_t>(rawEntries.size());

    // Compute data offsets
    uint32_t indexEnd = sizeof(EQE1Header) + numEntries * EQE1_INDEX_ENTRY_SIZE;
    uint32_t dataOffset = alignTo16(indexEnd);

    // Build index entries and track data positions
    struct EntryInfo {
        EQE1IndexEntry index;
        const uint8_t* data;
        uint32_t dataSize;
    };
    std::vector<EntryInfo> entries;
    entries.reserve(numEntries);

    for (const auto& tex : textures) {
        // RGB entry
        EQE1IndexEntry idx = {};
        std::strncpy(idx.name, tex.name.c_str(), EQE1_NAME_SIZE - 1);
        idx.width = static_cast<uint16_t>(tex.width);
        idx.height = static_cast<uint16_t>(tex.height);
        idx.mipLevels = tex.mipLevels;
        idx.hasAlpha = tex.hasAlpha ? 1 : 0;
        idx.entryType = tex.hasAlpha ? EQE1_TYPE_ETC1_RGBA : EQE1_TYPE_ETC1_RGB;
        idx.dataOffset = dataOffset;
        idx.dataSize = static_cast<uint32_t>(tex.rgbETC1.size());
        idx.padding = 0;
        entries.push_back({idx, tex.rgbETC1.data(), idx.dataSize});
        dataOffset = alignTo16(dataOffset + idx.dataSize);

        // Alpha companion entry
        if (tex.hasAlpha) {
            EQE1IndexEntry alphaIdx = {};
            std::string alphaName = tex.name + "_alpha";
            std::strncpy(alphaIdx.name, alphaName.c_str(), EQE1_NAME_SIZE - 1);
            alphaIdx.width = static_cast<uint16_t>(tex.width);
            alphaIdx.height = static_cast<uint16_t>(tex.height);
            alphaIdx.mipLevels = tex.mipLevels;
            alphaIdx.hasAlpha = 0;
            alphaIdx.entryType = EQE1_TYPE_ETC1_RGB;  // alpha stored as grayscale RGB
            alphaIdx.dataOffset = dataOffset;
            alphaIdx.dataSize = static_cast<uint32_t>(tex.alphaETC1.size());
            alphaIdx.padding = 0;
            entries.push_back({alphaIdx, tex.alphaETC1.data(), alphaIdx.dataSize});
            dataOffset = alignTo16(dataOffset + alphaIdx.dataSize);
        }
    }

    // Raw data entries
    for (const auto& raw : rawEntries) {
        EQE1IndexEntry idx = {};
        std::strncpy(idx.name, raw.name.c_str(), EQE1_NAME_SIZE - 1);
        idx.width = 0;
        idx.height = 0;
        idx.mipLevels = 0;
        idx.hasAlpha = 0;
        idx.entryType = EQE1_TYPE_RAW_DATA;
        idx.dataOffset = dataOffset;
        idx.dataSize = static_cast<uint32_t>(raw.data.size());
        idx.padding = 0;
        entries.push_back({idx, reinterpret_cast<const uint8_t*>(raw.data.data()), idx.dataSize});
        dataOffset = alignTo16(dataOffset + idx.dataSize);
    }

    assert(entries.size() == numEntries);

    // Write output file
    std::string baseName = toLower(archiveName);
    // Strip .s3d extension
    if (baseName.size() > 4 && baseName.substr(baseName.size() - 4) == ".s3d") {
        baseName = baseName.substr(0, baseName.size() - 4);
    }
    std::string outputPath = outputDir + "/" + baseName + ".eqe1";

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "  Cannot open output: " << outputPath << std::endl;
        return false;
    }

    // Write header
    EQE1Header header = {};
    header.magic = EQE1_MAGIC;
    header.version = EQE1_VERSION;
    header.numEntries = numEntries;
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write index entries
    for (const auto& entry : entries) {
        out.write(reinterpret_cast<const char*>(&entry.index), sizeof(EQE1IndexEntry));
    }

    // Write data with 16-byte alignment padding
    for (const auto& entry : entries) {
        // Pad to entry's dataOffset
        auto currentPos = static_cast<uint32_t>(out.tellp());
        if (currentPos < entry.index.dataOffset) {
            uint32_t padSize = entry.index.dataOffset - currentPos;
            std::vector<uint8_t> padding(padSize, 0);
            out.write(reinterpret_cast<const char*>(padding.data()), padSize);
        }
        out.write(reinterpret_cast<const char*>(entry.data), entry.dataSize);
    }

    out.close();

    size_t fileSize = fs::file_size(outputPath);
    std::cout << "  Written: " << outputPath << " ("
              << fileSize / 1024 << " KB, "
              << textures.size() << " textures"
              << (alphaCount > 0 ? ", " + std::to_string(alphaCount) + " alpha" : "")
              << ", " << rawEntries.size() << " wld"
              << (failCount > 0 ? ", " + std::to_string(failCount) + " skipped" : "")
              << ")" << std::endl;

    return true;
}

// ── Entry point ──────────────────────────────────────────────────────────────

static void printUsage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " --eq-path <path> --archive <name> --output <dir>\n"
              << "       " << argv0 << " --eq-path <path> --all --output <dir>\n"
              << "\nOptions:\n"
              << "  --eq-path <path>     Path to EverQuest client directory\n"
              << "  --archive <name>     Process single archive (e.g., qeynos2.s3d)\n"
              << "  --all                Process all .s3d files\n"
              << "  --output <dir>       Output directory for .eqe1 files\n"
              << "  --verbose            Print per-texture stats\n"
              << std::endl;
}

int main(int argc, char** argv) {
    std::string eqPath;
    std::string archiveName;
    std::string outputDir;
    bool allArchives = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--eq-path" && i + 1 < argc) {
            eqPath = argv[++i];
        } else if (arg == "--archive" && i + 1 < argc) {
            archiveName = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--all") {
            allArchives = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (eqPath.empty()) {
        std::cerr << "Error: --eq-path is required\n";
        printUsage(argv[0]);
        return 1;
    }
    if (!fs::exists(eqPath) || !fs::is_directory(eqPath)) {
        std::cerr << "Error: EQ path does not exist or is not a directory: " << eqPath << std::endl;
        return 1;
    }
    if (!allArchives && archiveName.empty()) {
        std::cerr << "Error: --archive or --all is required\n";
        printUsage(argv[0]);
        return 1;
    }
    if (outputDir.empty()) {
        std::cerr << "Error: --output is required\n";
        printUsage(argv[0]);
        return 1;
    }

    fs::create_directories(outputDir);

    // Collect archives to process
    std::vector<std::string> archives;
    if (allArchives) {
        for (const auto& entry : fs::directory_iterator(eqPath)) {
            if (!entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            std::string lower = toLower(name);
            if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".s3d") {
                archives.push_back(name);
            }
        }
        std::sort(archives.begin(), archives.end());
        std::cout << "Processing " << archives.size() << " S3D archives..." << std::endl;
    } else {
        archives.push_back(archiveName);
    }

    int success = 0, failed = 0;
    size_t totalOutputSize = 0;
    int totalTextures = 0;

    for (size_t i = 0; i < archives.size(); ++i) {
        std::cout << "\n[" << (i + 1) << "/" << archives.size() << "] " << archives[i] << std::endl;

        if (processArchive(eqPath, archives[i], outputDir, verbose)) {
            success++;

            // Accumulate stats
            std::string baseName = toLower(archives[i]);
            if (baseName.size() > 4) baseName = baseName.substr(0, baseName.size() - 4);
            std::string outPath = outputDir + "/" + baseName + ".eqe1";
            if (fs::exists(outPath)) {
                totalOutputSize += fs::file_size(outPath);
            }
        } else {
            failed++;
        }
    }

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "  Archives: " << success << " succeeded, " << failed << " failed" << std::endl;
    std::cout << "  Total output: " << totalOutputSize / (1024 * 1024) << " MB" << std::endl;

    return failed > 0 ? 1 : 0;
}
