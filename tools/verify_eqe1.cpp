// verify_eqe1 - Validates .eqe1 flat pack files for structural integrity.
//
// Checks header magic/version, index entry consistency, data alignment,
// mipmap chain sizes, and alpha companion entries.
//
// Usage:
//   ./verify_eqe1 <file.eqe1>
//   ./verify_eqe1 --all <directory>

#include "client/graphics/eqe1_format.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace EQT::Graphics;

struct VerifyResult {
    int errors = 0;
    int warnings = 0;
    int textureCount = 0;
    int alphaCount = 0;
    int rawDataCount = 0;
    uint32_t totalDataSize = 0;
};

static VerifyResult verifyFile(const std::string& filepath, bool verbose) {
    VerifyResult result;

    std::ifstream in(filepath, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        fprintf(stderr, "  ERROR: Cannot open file\n");
        result.errors++;
        return result;
    }

    size_t fileSize = static_cast<size_t>(in.tellg());
    in.seekg(0);

    // Read entire file
    std::vector<uint8_t> data(fileSize);
    in.read(reinterpret_cast<char*>(data.data()), fileSize);
    in.close();

    // Validate header
    if (fileSize < sizeof(EQE1Header)) {
        fprintf(stderr, "  ERROR: File too small for header (%zu bytes)\n", fileSize);
        result.errors++;
        return result;
    }

    const auto* header = reinterpret_cast<const EQE1Header*>(data.data());

    if (header->magic != EQE1_MAGIC) {
        fprintf(stderr, "  ERROR: Bad magic: 0x%08X (expected 0x%08X)\n",
                header->magic, EQE1_MAGIC);
        result.errors++;
        return result;
    }

    if (header->version != EQE1_VERSION) {
        fprintf(stderr, "  ERROR: Bad version: %u (expected %u)\n",
                header->version, EQE1_VERSION);
        result.errors++;
        return result;
    }

    uint16_t numEntries = header->numEntries;
    size_t indexEnd = sizeof(EQE1Header) + numEntries * EQE1_INDEX_ENTRY_SIZE;

    if (fileSize < indexEnd) {
        fprintf(stderr, "  ERROR: File too small for index (%zu bytes, need %zu)\n",
                fileSize, indexEnd);
        result.errors++;
        return result;
    }

    // Validate each index entry
    const auto* entries = reinterpret_cast<const EQE1IndexEntry*>(
        data.data() + sizeof(EQE1Header));

    // Collect all entry names for alpha companion check
    std::set<std::string> allNames;
    for (uint16_t i = 0; i < numEntries; ++i) {
        // Extract null-terminated name
        char nameBuf[EQE1_NAME_SIZE + 1] = {};
        std::memcpy(nameBuf, entries[i].name, EQE1_NAME_SIZE);
        allNames.insert(std::string(nameBuf));
    }

    for (uint16_t i = 0; i < numEntries; ++i) {
        const auto& e = entries[i];

        // Extract name
        char nameBuf[EQE1_NAME_SIZE + 1] = {};
        std::memcpy(nameBuf, e.name, EQE1_NAME_SIZE);
        std::string name(nameBuf);

        // Check null-termination within 32 bytes
        bool nullTerminated = false;
        for (uint32_t j = 0; j < EQE1_NAME_SIZE; ++j) {
            if (e.name[j] == '\0') { nullTerminated = true; break; }
        }
        if (!nullTerminated) {
            fprintf(stderr, "  ERROR: Entry %u name not null-terminated\n", i);
            result.errors++;
        }

        // Check entry type
        if (e.entryType != EQE1_TYPE_ETC1_RGB &&
            e.entryType != EQE1_TYPE_ETC1_RGBA &&
            e.entryType != EQE1_TYPE_RAW_DATA) {
            fprintf(stderr, "  ERROR: Entry %u '%s' invalid entryType %u\n",
                    i, nameBuf, e.entryType);
            result.errors++;
        }

        // Check data bounds
        if (static_cast<size_t>(e.dataOffset) + e.dataSize > fileSize) {
            fprintf(stderr, "  ERROR: Entry %u '%s' data exceeds file (offset=%u, size=%u, fileSize=%zu)\n",
                    i, nameBuf, e.dataOffset, e.dataSize, fileSize);
            result.errors++;
        }

        // Check 16-byte alignment
        if (e.dataOffset % EQE1_DATA_ALIGNMENT != 0) {
            fprintf(stderr, "  ERROR: Entry %u '%s' dataOffset %u not 16-byte aligned\n",
                    i, nameBuf, e.dataOffset);
            result.errors++;
        }

        if (e.entryType == EQE1_TYPE_RAW_DATA) {
            // Raw data: width/height/mipLevels should be 0
            if (e.width != 0 || e.height != 0 || e.mipLevels != 0) {
                fprintf(stderr, "  WARNING: Entry %u '%s' RAW_DATA has non-zero dimensions (%ux%u, %u mips)\n",
                        i, nameBuf, e.width, e.height, e.mipLevels);
                result.warnings++;
            }
            result.rawDataCount++;
        } else {
            // Texture entry
            if (e.width == 0 || e.height == 0) {
                fprintf(stderr, "  ERROR: Entry %u '%s' texture has zero dimensions (%ux%u)\n",
                        i, nameBuf, e.width, e.height);
                result.errors++;
                continue;
            }

            // Check mipLevels
            uint8_t expectedMips = computeMipLevels(e.width, e.height);
            if (e.mipLevels != expectedMips) {
                fprintf(stderr, "  ERROR: Entry %u '%s' mipLevels=%u (expected %u for %ux%u)\n",
                        i, nameBuf, e.mipLevels, expectedMips, e.width, e.height);
                result.errors++;
            }

            // Check data size matches mip chain
            uint32_t expectedSize = etc1MipChainSize(e.width, e.height, e.mipLevels);
            if (e.dataSize != expectedSize) {
                fprintf(stderr, "  ERROR: Entry %u '%s' dataSize=%u (expected %u for %ux%u, %u mips)\n",
                        i, nameBuf, e.dataSize, expectedSize, e.width, e.height, e.mipLevels);
                result.errors++;
            }

            // Check alpha companion
            if (e.hasAlpha) {
                std::string alphaName = name + "_alpha";
                if (allNames.find(alphaName) == allNames.end()) {
                    fprintf(stderr, "  ERROR: Entry %u '%s' hasAlpha=1 but no '%s' companion entry\n",
                            i, nameBuf, alphaName.c_str());
                    result.errors++;
                }
                result.alphaCount++;
            }

            // Check entryType vs hasAlpha consistency
            if (e.hasAlpha && e.entryType != EQE1_TYPE_ETC1_RGBA) {
                fprintf(stderr, "  ERROR: Entry %u '%s' hasAlpha=1 but entryType=%u (expected %u)\n",
                        i, nameBuf, e.entryType, EQE1_TYPE_ETC1_RGBA);
                result.errors++;
            }
            if (!e.hasAlpha && e.entryType == EQE1_TYPE_ETC1_RGBA) {
                fprintf(stderr, "  ERROR: Entry %u '%s' hasAlpha=0 but entryType=%u\n",
                        i, nameBuf, e.entryType);
                result.errors++;
            }

            result.textureCount++;
            result.totalDataSize += e.dataSize;

            if (verbose) {
                printf("    %s %ux%u %u mips %u bytes%s\n",
                       nameBuf, e.width, e.height, e.mipLevels, e.dataSize,
                       e.hasAlpha ? " +alpha" : "");
            }
        }
    }

    return result;
}

static void printUsage(const char* argv0) {
    printf("Usage: %s <file.eqe1>\n"
           "       %s --all <directory>\n"
           "       %s --verbose <file.eqe1>\n"
           "\nValidates .eqe1 flat pack files for structural integrity.\n"
           "Exit code: 0 = OK, 1 = errors found.\n",
           argv0, argv0, argv0);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    bool allMode = false;
    bool verbose = false;
    std::string target;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--all" && i + 1 < argc) {
            allMode = true;
            target = argv[++i];
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (target.empty()) {
            target = arg;
        }
    }

    if (target.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    std::vector<std::string> files;
    if (allMode) {
        if (!fs::is_directory(target)) {
            fprintf(stderr, "Error: '%s' is not a directory\n", target.c_str());
            return 1;
        }
        for (const auto& entry : fs::directory_iterator(target)) {
            if (entry.is_regular_file() && entry.path().extension() == ".eqe1") {
                files.push_back(entry.path().string());
            }
        }
        std::sort(files.begin(), files.end());
    } else {
        files.push_back(target);
    }

    int totalErrors = 0;
    int totalWarnings = 0;
    int totalFiles = 0;
    int passedFiles = 0;

    for (const auto& file : files) {
        printf("%s: ", fs::path(file).filename().c_str());
        fflush(stdout);

        auto result = verifyFile(file, verbose);
        totalFiles++;
        totalErrors += result.errors;
        totalWarnings += result.warnings;

        if (result.errors == 0) {
            printf("OK (%d textures, %d alpha, %d raw, %u KB data)\n",
                   result.textureCount, result.alphaCount, result.rawDataCount,
                   result.totalDataSize / 1024);
            passedFiles++;
        } else {
            printf("FAIL (%d errors, %d warnings)\n", result.errors, result.warnings);
        }
    }

    if (allMode) {
        printf("\n=== Summary ===\n");
        printf("  Files: %d/%d passed\n", passedFiles, totalFiles);
        printf("  Errors: %d, Warnings: %d\n", totalErrors, totalWarnings);
    }

    return totalErrors > 0 ? 1 : 0;
}
