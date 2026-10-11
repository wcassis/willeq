// verify_eqe1 - Validates .eqe1 flat pack files for structural integrity.
//
// Checks header magic/version, index entry consistency, data alignment,
// mipmap chain sizes, and alpha companion entries. Accepts v1/v2/v3 packs
// (v3 = B910's name table, filed as B100, for entry names longer than 31 chars); a v1/v2
// entry whose name is not null-terminated within the 32-byte field is a
// truncated name and a FAIL (rebuild the pack; the builder now emits v3
// for such names).
//
// Usage:
//   ./verify_eqe1 <file.eqe1>
//   ./verify_eqe1 --all <directory>

#include "client/graphics/eqe1_format.h"

#include <algorithm>
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
    // B930: the header version the file declared (0 when the header itself is
    // unreadable). Printed on every line, and checked against --require-v3.
    uint16_t version = 0;
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

    // B930: record the version now — it is printed on every line even when the
    // version check below fails (a future v4 still gets a readable report).
    result.version = header->version;

    if (header->version != EQE1_VERSION && header->version != EQE1_VERSION_V2 &&
        header->version != EQE1_VERSION_V3) {
        fprintf(stderr, "  ERROR: Bad version: %u (expected %u, %u or %u)\n",
                header->version, EQE1_VERSION, EQE1_VERSION_V2, EQE1_VERSION_V3);
        result.errors++;
        return result;
    }

    const uint32_t stride = indexEntryStride(header->version);
    uint16_t numEntries = header->numEntries;
    size_t indexEnd = sizeof(EQE1Header) + numEntries * stride;

    if (fileSize < indexEnd) {
        fprintf(stderr, "  ERROR: File too small for index (%zu bytes, need %zu)\n",
                fileSize, indexEnd);
        result.errors++;
        return result;
    }

    // Validate each index entry. NOTE: for v3 the index entries are
    // 56-byte-strided (the 48-byte entry plus the 8-byte name tail), so the
    // array below is only valid as a v1/v2 view; always address entries
    // through entryAt(i), which strides by the version-dependent size.
    const auto* entriesBase = data.data() + sizeof(EQE1Header);
    auto entryAt = [&](uint16_t i) -> const EQE1IndexEntry& {
        return *reinterpret_cast<const EQE1IndexEntry*>(
            entriesBase + static_cast<size_t>(i) * stride);
    };

    // v3: the name table sits between the index and the data section; its end
    // is the data section's start — the smallest dataOffset over the
    // data-carrying entries. A pack with no data-carrying entries has no
    // data section and therefore no room for a table.
    uint32_t v3DataStart = 0;
    if (header->version == EQE1_VERSION_V3) {
        v3DataStart = static_cast<uint32_t>(fileSize);
        for (uint16_t i = 0; i < numEntries; ++i) {
            const auto& e = entryAt(i);
            if (e.dataSize != 0) {
                v3DataStart = std::min(v3DataStart, e.dataOffset);
            }
        }
    }

    // Read one entry's effective name. v3 entries either carry it inline
    // (zero tail) or in the table; v1/v2 entries must be null-terminated
    // within the 32-byte field (a truncated name is a build defect — B910,
    // filed as B100).
    // On a bad reference the error is counted here and an empty name
    // returned so the loop can keep checking the other entries.
    auto entryName = [&](uint16_t i) -> std::string {
        const auto& e = entryAt(i);
        if (header->version == EQE1_VERSION_V3) {
            const auto* tail =
                reinterpret_cast<const EQE1IndexEntryNameTail*>(
                    reinterpret_cast<const uint8_t*>(&e) + sizeof(EQE1IndexEntry));
            if (tail->nameOffset == 0 && tail->nameLength == 0) {
                return e.name;  // inline; NUL-padded, so std::string stops there
            }
            if (e.name[0] != '\0') {
                fprintf(stderr, "  ERROR: Entry %u has BOTH a name-table reference "
                                "and an inline name\n", i);
                result.errors++;
            }
            if (tail->nameLength == 0 ||
                static_cast<size_t>(tail->nameOffset) >= fileSize ||
                static_cast<size_t>(tail->nameOffset) + tail->nameLength > fileSize ||
                tail->nameOffset < indexEnd ||
                (v3DataStart != 0 &&
                 (tail->nameOffset > v3DataStart ||
                  static_cast<size_t>(tail->nameOffset) + tail->nameLength > v3DataStart))) {
                fprintf(stderr, "  ERROR: Entry %u name-table reference out of bounds "
                                "(offset=%u, length=%u)\n",
                        i, tail->nameOffset, tail->nameLength);
                result.errors++;
                return std::string();
            }
            return std::string(reinterpret_cast<const char*>(
                                   data.data() + tail->nameOffset),
                               tail->nameLength);
        }
        for (uint32_t j = 0; j < EQE1_NAME_SIZE; ++j) {
            if (e.name[j] == '\0') return e.name;
        }
        fprintf(stderr, "  ERROR: Entry %u name not null-terminated within the "
                        "%u-byte field (truncated at 31 chars? rebuild as v3)\n",
                i, EQE1_NAME_SIZE);
        result.errors++;
        return std::string();
    };

    // Collect all entry names for alpha companion check
    std::set<std::string> allNames;
    for (uint16_t i = 0; i < numEntries; ++i) {
        allNames.insert(entryName(i));
    }

    for (uint16_t i = 0; i < numEntries; ++i) {
        const auto& e = entryAt(i);

        // Extract name (table-reference / truncation errors are counted
        // inside entryName(); the name is empty in that case)
        std::string name = entryName(i);
        char nameBuf[EQE1_NAME_SIZE + 1] = {};
        std::snprintf(nameBuf, sizeof(nameBuf), "%.32s", name.c_str());

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

            bool isV2Payload = (e.payloadFormat != EQE1_PAYLOAD_ETC1);

            // Check data size matches mip chain (compressed ETC1 vs uncompressed v2 payload)
            uint32_t expectedSize = isV2Payload
                ? payloadMipChainSize(e.width, e.height, e.mipLevels,
                                       static_cast<EQE1PayloadFormat>(e.payloadFormat))
                : etc1MipChainSize(e.width, e.height, e.mipLevels);
            if (e.dataSize != expectedSize) {
                fprintf(stderr, "  ERROR: Entry %u '%s' dataSize=%u (expected %u for %ux%u, %u mips, payloadFormat=%u)\n",
                        i, nameBuf, e.dataSize, expectedSize, e.width, e.height, e.mipLevels, e.payloadFormat);
                result.errors++;
            }

            // Check alpha companion — only the ETC1 payload path builds one.
            // v2 payloads (RGBA4444/RGBA8888) carry alpha in-band; hasAlpha=1
            // with no companion entry is expected and correct for them.
            if (e.hasAlpha) {
                if (!isV2Payload) {
                    std::string alphaName = name + "_alpha";
                    if (allNames.find(alphaName) == allNames.end()) {
                        fprintf(stderr, "  ERROR: Entry %u '%s' hasAlpha=1 but no '%s' companion entry\n",
                                i, nameBuf, alphaName.c_str());
                        result.errors++;
                    }
                }
                result.alphaCount++;
            }

            // Check entryType vs hasAlpha consistency — only meaningful for
            // the ETC1 companion model; v2 payloads always use the
            // no-companion entryType regardless of hasAlpha.
            if (!isV2Payload) {
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
            }

            result.textureCount++;
            result.totalDataSize += e.dataSize;

            if (verbose) {
                static const char* kPayloadNames[] = {"etc1", "rgb565", "rgba4444", "rgba8888"};
                const char* payloadName = (e.payloadFormat < 4) ? kPayloadNames[e.payloadFormat] : "?";
                printf("    %s %ux%u %u mips %u bytes%s [%s]\n",
                       nameBuf, e.width, e.height, e.mipLevels, e.dataSize,
                       e.hasAlpha ? " +alpha" : "", payloadName);
            }
        }
    }

    return result;
}

static void printUsage(const char* argv0) {
    printf("Usage: %s <file.eqe1>\n"
           "       %s --all <directory>\n"
           "       %s --verbose <file.eqe1>\n"
           "       %s --require-v3 <file.eqe1 | directory>\n"
           "\nValidates .eqe1 flat pack files (v1/v2/v3) for structural integrity.\n"
           "Every line prints the pack's header version (v1, v2 or v3).\n"
           "--require-v3 additionally FAILs (exit 1) on any pack whose version\n"
           "is below v3: a v1/v2 pack can only store 31-char names, so a zone\n"
           "with a longer name in it is stale and must be rebuilt (the client\n"
           "FATALs loudly on the first miss — see B930, #248). Auditing a\n"
           "host's pack directory:\n"
           "  for f in <dir>/*.eqe1; do %s --require-v3 \"$f\"; done\n"
           "Exit code: 0 = OK, 1 = errors found.\n",
           argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    bool allMode = false;
    bool verbose = false;
    bool requireV3 = false;
    std::string target;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--all" && i + 1 < argc) {
            allMode = true;
            target = argv[++i];
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--require-v3") {
            requireV3 = true;
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

        // B930: the version is part of every line — an audit of a host's pack
        // directory reads it at a glance. A file whose header could not even
        // be read (bad magic, truncated) has version 0 and stays FAILED.
        // --require-v3: a structurally-clean v1/v2 pack is reported for the
        // audit and the run exits non-zero. The version alone cannot prove
        // the pack STALE — a v1/v2 pack is legitimate when every name it
        // holds fits 31 chars (willeq.eqe1 is one) — but it is the only
        // format that CAN truncate a >31-char name, and that is the defect
        // that kills the client (B930, #248). The line therefore says
        // "STALE: below v3" in the card's wording: it marks the candidates
        // for the rebuild, and the client-side FATAL (which names the exact
        // pack + version + truncated name) is the ground truth for which of
        // them actually needs it.
        if (requireV3 && result.version != 0 && result.version < EQE1_VERSION_V3) {
            printf("STALE: format v%u below v3 (a v1/v2 pack cannot hold >31-char "
                   "names; rebuild with tools/convert_all_to_eqe1.sh if this "
                   "zone's textures need one)\n",
                   result.version);
            result.errors++;
            totalErrors++;
        }

        if (result.errors == 0) {
            printf("OK (v%u, %d textures, %d alpha, %d raw, %u KB data)\n",
                   result.version, result.textureCount, result.alphaCount,
                   result.rawDataCount, result.totalDataSize / 1024);
            passedFiles++;
        } else {
            printf("FAIL (v%u, %d errors, %d warnings)\n",
                   result.version, result.errors, result.warnings);
        }
    }

    if (allMode) {
        printf("\n=== Summary ===\n");
        printf("  Files: %d/%d passed\n", passedFiles, totalFiles);
        printf("  Errors: %d, Warnings: %d\n", totalErrors, totalWarnings);
    }

    return totalErrors > 0 ? 1 : 0;
}
