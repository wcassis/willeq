// s3d_index_builder — Build offline asset index from S3D/PFS archives
//
// Scans all S3D archives in an EQ client directory and writes a single
// binary index file mapping every asset name to its archive + offset + size.
// At runtime, this eliminates the need to open archives just to read their
// directories, saving 100-200ms per archive open on ARM.
//
// Usage:
//   ./s3d_index_builder --eq-path /path/to/EQ --output s3d_index.bin
//   ./s3d_index_builder --eq-path /path/to/EQ --output s3d_index.bin --verbose

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <zlib.h>

// Safe unaligned read (ARM-safe)
template<typename T>
static inline T read_val(const void* ptr) {
    T val;
    std::memcpy(&val, ptr, sizeof(T));
    return val;
}

// PFS CRC for filename matching (same as pfs.cpp)
static int32_t crc_table[256];
static bool crc_table_init = false;

static void initCrcTable() {
    if (crc_table_init) return;
    for (int i = 0; i < 256; ++i) {
        int32_t crc_accum = i << 24;
        for (int j = 0; j < 8; ++j) {
            if ((crc_accum & 0x80000000) != 0)
                crc_accum = (crc_accum << 1) ^ 0x04C11DB7;
            else
                crc_accum = crc_accum << 1;
        }
        crc_table[i] = crc_accum;
    }
    crc_table_init = true;
}

static int32_t pfsCrc(const std::string& s) {
    initCrcTable();
    std::vector<uint8_t> buf(s.length() + 1, 0);
    std::memcpy(buf.data(), s.c_str(), s.length());
    int32_t crc = 0;
    for (size_t i = 0; i < s.length() + 1; ++i) {
        int idx = ((crc >> 24) ^ buf[i]) & 0xFF;
        crc = (crc << 8) ^ crc_table[idx];
    }
    return crc;
}

// Inflate a single compressed block
static uint32_t inflateBlock(const char* data, uint32_t compLen, char* out, uint32_t outMax) {
    z_stream zs = {};
    zs.next_in = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data));
    zs.avail_in = compLen;
    zs.next_out = reinterpret_cast<unsigned char*>(out);
    zs.avail_out = outMax;
    if (inflateInit2(&zs, 15) != Z_OK) return 0;
    int ret = inflate(&zs, Z_FINISH);
    uint32_t out_total = static_cast<uint32_t>(zs.total_out);
    inflateEnd(&zs);
    return (ret == Z_STREAM_END) ? out_total : 0;
}

// Index entry for one file in one archive
struct IndexEntry {
    std::string filename;        // Lowercase asset name (e.g., "clkshk01.bmp")
    std::string archivePath;     // Relative path to S3D file (e.g., "qeynos2.s3d")
    uint32_t blockOffset;        // Byte offset of first compressed block in archive
    uint32_t compressedSize;     // Total compressed size (all blocks)
    uint32_t uncompressedSize;   // Total uncompressed size
};

// Scan one PFS archive and extract directory entries
static bool scanArchive(const std::string& fullPath, const std::string& relativePath,
                         std::vector<IndexEntry>& entries, bool verbose) {
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file) return false;

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<size_t>(fileSize));
    if (!file.read(buffer.data(), fileSize)) return false;

    size_t idx = 0;

    // Read directory offset
    if (idx + 4 > buffer.size()) return false;
    uint32_t dir_offset = read_val<uint32_t>(&buffer[idx]);
    idx += 4;

    // Check magic
    if (idx + 4 > buffer.size()) return false;
    if (buffer[idx] != 'P' || buffer[idx+1] != 'F' ||
        buffer[idx+2] != 'S' || buffer[idx+3] != ' ') return false;

    // Read directory
    if (dir_offset + 4 > buffer.size()) return false;
    uint32_t dir_count = read_val<uint32_t>(&buffer[dir_offset]);

    struct DirEntry { int32_t crc; uint32_t offset; uint32_t size; };
    std::vector<DirEntry> dirEntries;
    std::map<int32_t, std::string> crcToName;

    for (uint32_t i = 0; i < dir_count; ++i) {
        size_t entryOff = dir_offset + 4 + (i * 12);
        if (entryOff + 12 > buffer.size()) return false;

        int32_t crc = read_val<int32_t>(&buffer[entryOff]);
        uint32_t offset = read_val<uint32_t>(&buffer[entryOff + 4]);
        uint32_t size = read_val<uint32_t>(&buffer[entryOff + 8]);

        // Filename table entry
        if (crc == 0x61580ac9) {
            // Decompress filename table
            // Walk blocks to find total compressed size
            uint32_t pos = offset;
            uint32_t inflateTotal = 0;
            std::vector<uint32_t> blockOffsets, blockCompSizes, blockDecompSizes;
            while (inflateTotal < size) {
                if (pos + 8 > buffer.size()) break;
                uint32_t deflLen = read_val<uint32_t>(&buffer[pos]);
                uint32_t inflLen = read_val<uint32_t>(&buffer[pos + 4]);
                blockOffsets.push_back(pos + 8);
                blockCompSizes.push_back(deflLen);
                blockDecompSizes.push_back(inflLen);
                inflateTotal += inflLen;
                pos += deflLen + 8;
            }

            // Decompress all blocks
            std::vector<char> fnBuf(size);
            uint32_t outPos = 0;
            for (size_t b = 0; b < blockOffsets.size(); ++b) {
                inflateBlock(&buffer[blockOffsets[b]], blockCompSizes[b],
                             &fnBuf[outPos], blockDecompSizes[b]);
                outPos += blockDecompSizes[b];
            }

            // Parse filename entries
            uint32_t fnPos = 0;
            if (fnPos + 4 > fnBuf.size()) continue;
            uint32_t fnCount = read_val<uint32_t>(&fnBuf[fnPos]);
            fnPos += 4;

            for (uint32_t j = 0; j < fnCount; ++j) {
                if (fnPos + 4 > fnBuf.size()) break;
                uint32_t fnLen = read_val<uint32_t>(&fnBuf[fnPos]);
                fnPos += 4;
                if (fnPos + fnLen > fnBuf.size()) break;

                std::string name(fnLen - 1, '\0');
                std::memcpy(&name[0], &fnBuf[fnPos], fnLen - 1);
                fnPos += fnLen;

                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                crcToName[pfsCrc(name)] = name;
            }
        } else {
            dirEntries.push_back({crc, offset, size});
        }
    }

    // Match CRCs to filenames and compute block chain sizes
    for (const auto& de : dirEntries) {
        auto nameIt = crcToName.find(de.crc);
        if (nameIt == crcToName.end()) continue;

        // Walk block chain to compute total compressed size
        uint32_t pos = de.offset;
        uint32_t inflateTotal = 0;
        while (inflateTotal < de.size) {
            if (pos + 8 > buffer.size()) break;
            uint32_t deflLen = read_val<uint32_t>(&buffer[pos]);
            uint32_t inflLen = read_val<uint32_t>(&buffer[pos + 4]);
            inflateTotal += inflLen;
            pos += deflLen + 8;
        }
        uint32_t totalCompressed = pos - de.offset;

        entries.push_back({nameIt->second, relativePath,
                          de.offset, totalCompressed, de.size});
    }

    if (verbose) {
        printf("  %s: %zu files indexed\n", relativePath.c_str(), entries.size());
    }
    return true;
}

// ============================================================================
// Index file format
// ============================================================================
//
// Header (16 bytes):
//   uint32_t magic = 0x58444E49 ("INDX")
//   uint32_t version = 1
//   uint32_t entryCount
//   uint32_t stringTableOffset
//
// Entries (20 bytes each):
//   uint32_t filenameOffset    (into string table)
//   uint32_t archivePathOffset (into string table)
//   uint32_t blockOffset       (in archive file)
//   uint32_t compressedSize
//   uint32_t uncompressedSize
//
// String table:
//   Null-terminated strings, packed sequentially

static bool writeIndex(const std::string& outputPath,
                        const std::vector<IndexEntry>& entries) {
    // Build string table
    std::vector<char> stringTable;
    std::map<std::string, uint32_t> stringOffsets;

    auto addString = [&](const std::string& s) -> uint32_t {
        auto it = stringOffsets.find(s);
        if (it != stringOffsets.end()) return it->second;
        uint32_t offset = static_cast<uint32_t>(stringTable.size());
        stringTable.insert(stringTable.end(), s.begin(), s.end());
        stringTable.push_back('\0');
        stringOffsets[s] = offset;
        return offset;
    };

    // Pre-populate string table
    struct PackedEntry {
        uint32_t filenameOff, archiveOff, blockOffset, compSize, uncompSize;
    };
    std::vector<PackedEntry> packed;
    packed.reserve(entries.size());

    for (const auto& e : entries) {
        uint32_t fnOff = addString(e.filename);
        uint32_t arOff = addString(e.archivePath);
        packed.push_back({fnOff, arOff, e.blockOffset, e.compressedSize, e.uncompressedSize});
    }

    // Write
    FILE* f = fopen(outputPath.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open output file: %s\n", outputPath.c_str());
        return false;
    }

    uint32_t magic = 0x58444E49;  // "INDX"
    uint32_t version = 1;
    uint32_t entryCount = static_cast<uint32_t>(packed.size());
    uint32_t stringTableOffset = 16 + entryCount * 20;

    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);
    fwrite(&entryCount, 4, 1, f);
    fwrite(&stringTableOffset, 4, 1, f);

    for (const auto& pe : packed) {
        fwrite(&pe.filenameOff, 4, 1, f);
        fwrite(&pe.archiveOff, 4, 1, f);
        fwrite(&pe.blockOffset, 4, 1, f);
        fwrite(&pe.compSize, 4, 1, f);
        fwrite(&pe.uncompSize, 4, 1, f);
    }

    fwrite(stringTable.data(), 1, stringTable.size(), f);
    fclose(f);
    return true;
}

// ============================================================================
// JSON I/O
// ============================================================================

static bool writeJson(const std::string& path, const std::vector<IndexEntry>& entries) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open JSON output: %s\n", path.c_str());
        return false;
    }

    // Compute stats for header
    uint32_t maxUncomp = 0;
    for (const auto& e : entries)
        if (e.uncompressedSize > maxUncomp) maxUncomp = e.uncompressedSize;

    // Group by archive for readability
    std::map<std::string, std::vector<const IndexEntry*>> byArchive;
    for (const auto& e : entries)
        byArchive[e.archivePath].push_back(&e);

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"entry_count\": %zu,\n", entries.size());
    fprintf(f, "  \"archive_count\": %zu,\n", byArchive.size());
    fprintf(f, "  \"max_uncompressed_size\": %u,\n", maxUncomp);
    fprintf(f, "  \"archives\": {\n");

    size_t archiveIdx = 0;
    for (const auto& [archive, archiveEntries] : byArchive) {
        fprintf(f, "    \"%s\": [\n", archive.c_str());
        for (size_t i = 0; i < archiveEntries.size(); ++i) {
            const auto* e = archiveEntries[i];
            fprintf(f, "      {\"name\": \"%s\", \"offset\": %u, \"compressed\": %u, \"uncompressed\": %u}%s\n",
                    e->filename.c_str(), e->blockOffset,
                    e->compressedSize, e->uncompressedSize,
                    (i + 1 < archiveEntries.size()) ? "," : "");
        }
        fprintf(f, "    ]%s\n", (archiveIdx + 1 < byArchive.size()) ? "," : "");
        archiveIdx++;
    }

    fprintf(f, "  }\n");
    fprintf(f, "}\n");
    fclose(f);
    return true;
}

static bool readJson(const std::string& path, std::vector<IndexEntry>& entries) {
    // Minimal JSON parser — just enough for our format
    std::ifstream file(path);
    if (!file) {
        fprintf(stderr, "ERROR: cannot open JSON input: %s\n", path.c_str());
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    entries.clear();
    std::string currentArchive;

    // Simple state machine parser
    size_t pos = 0;
    auto skipWS = [&]() { while (pos < content.size() && isspace(content[pos])) pos++; };
    auto expect = [&](char c) -> bool {
        skipWS();
        if (pos < content.size() && content[pos] == c) { pos++; return true; }
        return false;
    };
    auto readString = [&]() -> std::string {
        skipWS();
        if (pos >= content.size() || content[pos] != '"') return "";
        pos++;
        std::string result;
        while (pos < content.size() && content[pos] != '"') {
            if (content[pos] == '\\' && pos + 1 < content.size()) { pos++; }
            result += content[pos++];
        }
        if (pos < content.size()) pos++; // skip closing quote
        return result;
    };
    auto readUint = [&]() -> uint32_t {
        skipWS();
        uint32_t val = 0;
        while (pos < content.size() && isdigit(content[pos]))
            val = val * 10 + (content[pos++] - '0');
        return val;
    };

    // Find "archives" key
    size_t archivesPos = content.find("\"archives\"");
    if (archivesPos == std::string::npos) {
        fprintf(stderr, "ERROR: no 'archives' key found in JSON\n");
        return false;
    }
    pos = archivesPos + 10; // skip "archives"
    expect(':');
    expect('{');

    // Parse archive groups
    while (pos < content.size()) {
        skipWS();
        if (content[pos] == '}') break;

        // Archive name
        currentArchive = readString();
        if (currentArchive.empty()) break;
        expect(':');
        expect('[');

        // Entries array
        while (pos < content.size()) {
            skipWS();
            if (content[pos] == ']') { pos++; break; }
            if (content[pos] == ',') { pos++; continue; }

            expect('{');
            IndexEntry entry;
            entry.archivePath = currentArchive;

            // Parse key-value pairs
            while (pos < content.size() && content[pos] != '}') {
                skipWS();
                if (content[pos] == ',') { pos++; continue; }
                std::string key = readString();
                expect(':');

                if (key == "name") entry.filename = readString();
                else if (key == "offset") entry.blockOffset = readUint();
                else if (key == "compressed") entry.compressedSize = readUint();
                else if (key == "uncompressed") entry.uncompressedSize = readUint();
                else { // skip unknown value
                    skipWS();
                    if (content[pos] == '"') readString();
                    else readUint();
                }
            }
            expect('}');

            if (!entry.filename.empty())
                entries.push_back(entry);
        }

        skipWS();
        if (content[pos] == ',') pos++;
    }

    return true;
}

// ============================================================================
// Main
// ============================================================================

static void printUsage(const char* prog) {
    printf("Usage:\n");
    printf("  %s --scan --eq-path /path/to/EQ [-o s3d_index.json] [-v]\n", prog);
    printf("    Scan all S3D archives and write JSON index\n\n");
    printf("  %s --build -i s3d_index.json [-o s3d_index.bin]\n", prog);
    printf("    Read JSON index and write binary index\n\n");
    printf("  %s --scan --build --eq-path /path/to/EQ [-o s3d_index] [-v]\n", prog);
    printf("    Scan + build in one step (writes .json and .bin)\n");
}

int main(int argc, char** argv) {
    std::string eqPath;
    std::string outputPath;
    std::string inputJson;
    bool verbose = false;
    bool doScan = false;
    bool doBuild = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--eq-path") == 0 && i + 1 < argc) eqPath = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) outputPath = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) outputPath = argv[++i];
        else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) inputJson = argv[++i];
        else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) inputJson = argv[++i];
        else if (strcmp(argv[i], "--scan") == 0) doScan = true;
        else if (strcmp(argv[i], "--build") == 0) doBuild = true;
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) verbose = true;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]); return 0;
        }
    }

    if (!doScan && !doBuild) {
        fprintf(stderr, "ERROR: specify --scan, --build, or both\n");
        printUsage(argv[0]);
        return 1;
    }

    std::vector<IndexEntry> allEntries;

    // ── Scan phase: read S3D archives → entries ──
    if (doScan) {
        if (eqPath.empty()) {
            fprintf(stderr, "ERROR: --eq-path is required for --scan\n");
            return 1;
        }

        printf("Scanning S3D archives in: %s\n", eqPath.c_str());

        std::vector<std::string> archiveFiles;
        std::string cmd = "ls " + eqPath + "/*.s3d 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char line[1024];
            while (fgets(line, sizeof(line), pipe)) {
                std::string path(line);
                while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
                    path.pop_back();
                if (!path.empty()) archiveFiles.push_back(path);
            }
            pclose(pipe);
        }

        printf("Found %zu S3D archives\n", archiveFiles.size());

        int scanned = 0, failed = 0;
        for (const auto& fullPath : archiveFiles) {
            std::string relative = fullPath;
            size_t lastSlash = relative.rfind('/');
            if (lastSlash != std::string::npos)
                relative = relative.substr(lastSlash + 1);

            if (scanArchive(fullPath, relative, allEntries, verbose))
                scanned++;
            else {
                fprintf(stderr, "  WARNING: failed to scan %s\n", fullPath.c_str());
                failed++;
            }
        }

        // Stats
        uint32_t maxUncomp = 0;
        uint64_t totalUncomp = 0;
        for (const auto& e : allEntries) {
            if (e.uncompressedSize > maxUncomp) maxUncomp = e.uncompressedSize;
            totalUncomp += e.uncompressedSize;
        }

        printf("\nScanned %d archives (%d failed), %zu entries\n", scanned, failed, allEntries.size());
        printf("Max single entry: %.1f KB\n", maxUncomp / 1024.0f);
        printf("Total uncompressed: %.1f MB\n", totalUncomp / (1024.0 * 1024.0));
        printf("Runtime decompress buffer: %.1f KB\n", maxUncomp / 1024.0f);

        // Write JSON
        std::string jsonPath = outputPath.empty() ? "s3d_index.json" : outputPath + ".json";
        if (doBuild && !outputPath.empty()) jsonPath = outputPath + ".json";
        else if (!doBuild && !outputPath.empty()) jsonPath = outputPath;

        if (writeJson(jsonPath, allEntries)) {
            std::ifstream check(jsonPath, std::ios::binary | std::ios::ate);
            printf("JSON written: %s (%.1f KB)\n", jsonPath.c_str(),
                   check ? check.tellg() / 1024.0f : 0.0f);
        }
    }

    // ── Build phase: JSON → binary ──
    if (doBuild) {
        if (!doScan) {
            // Load from JSON file
            std::string jsonPath = inputJson.empty() ? "s3d_index.json" : inputJson;
            printf("Reading JSON index: %s\n", jsonPath.c_str());
            if (!readJson(jsonPath, allEntries)) {
                fprintf(stderr, "ERROR: failed to read JSON index\n");
                return 1;
            }
            printf("Loaded %zu entries\n", allEntries.size());
        }

        std::string binPath = outputPath.empty() ? "s3d_index.bin" : outputPath + ".bin";
        if (!doScan && !outputPath.empty()) binPath = outputPath;

        if (writeIndex(binPath, allEntries)) {
            std::ifstream check(binPath, std::ios::binary | std::ios::ate);
            printf("Binary index written: %s (%.1f KB, %zu entries)\n",
                   binPath.c_str(), check ? check.tellg() / 1024.0f : 0.0f,
                   allEntries.size());
        }
    }

    return 0;
}
