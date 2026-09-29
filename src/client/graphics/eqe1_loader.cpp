#include "client/graphics/eqe1_loader.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Logging - use LOG macros when integrated into willeq, fprintf for standalone use
#ifdef EQT_HAS_LOGGING
#include "common/logging.h"
#define EQE1_LOG_INFO(fmt, ...)  LOG_INFO(MOD_GRAPHICS, fmt, ##__VA_ARGS__)
#define EQE1_LOG_ERROR(fmt, ...) LOG_ERROR(MOD_GRAPHICS, fmt, ##__VA_ARGS__)
#define EQE1_LOG_DEBUG(fmt, ...) LOG_DEBUG(MOD_GRAPHICS, fmt, ##__VA_ARGS__)
#else
#define EQE1_LOG_INFO(fmt, ...)  fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define EQE1_LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define EQE1_LOG_DEBUG(fmt, ...)
#endif

namespace EQT { namespace Graphics {

EQE1Loader::EQE1Loader() = default;
EQE1Loader::~EQE1Loader() { close(); }

EQE1Loader::EQE1Loader(EQE1Loader&& other) noexcept
    : filepath_(std::move(other.filepath_))
    , mapping_(other.mapping_)
    , mappingSize_(other.mappingSize_)
    , textureLookup_(std::move(other.textureLookup_))
    , rawDataLookup_(std::move(other.rawDataLookup_))
    , entryCount_(other.entryCount_)
    , lookupHits_(other.lookupHits_)
    , isOpen_(other.isOpen_)
{
    other.mapping_ = nullptr;
    other.mappingSize_ = 0;
    other.entryCount_ = 0;
    other.lookupHits_ = 0;
    other.isOpen_ = false;
}

EQE1Loader& EQE1Loader::operator=(EQE1Loader&& other) noexcept {
    if (this != &other) {
        close();
        filepath_ = std::move(other.filepath_);
        mapping_ = other.mapping_;
        mappingSize_ = other.mappingSize_;
        textureLookup_ = std::move(other.textureLookup_);
        rawDataLookup_ = std::move(other.rawDataLookup_);
        entryCount_ = other.entryCount_;
        lookupHits_ = other.lookupHits_;
        isOpen_ = other.isOpen_;
        other.mapping_ = nullptr;
        other.mappingSize_ = 0;
        other.entryCount_ = 0;
        other.lookupHits_ = 0;
        other.isOpen_ = false;
    }
    return *this;
}

bool EQE1Loader::open(const std::string& filepath) {
    close();

    // P04: mmap the file read-only instead of heap-loading it. Backing pages
    // come from the page cache (clean, reclaimable), so opened-but-mostly-
    // unreferenced archives no longer pin RSS the way the old heap vector did.
    int fd = ::open(filepath.c_str(), O_RDONLY);
    if (fd < 0) {
        EQE1_LOG_ERROR("EQE1Loader: cannot open '%s'", filepath.c_str());
        return false;
    }

    struct stat st{};
    if (fstat(fd, &st) != 0) {
        EQE1_LOG_ERROR("EQE1Loader: stat failed for '%s'", filepath.c_str());
        ::close(fd);
        return false;
    }

    size_t fileSize = static_cast<size_t>(st.st_size);

    if (fileSize < sizeof(EQE1Header)) {
        EQE1_LOG_ERROR("EQE1Loader: file too small (%zu bytes): '%s'", fileSize, filepath.c_str());
        ::close(fd);
        return false;
    }

    void* mapping = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    // The mapping keeps its own reference to the file; the fd can close now.
    ::close(fd);
    if (mapping == MAP_FAILED) {
        EQE1_LOG_ERROR("EQE1Loader: mmap failed for '%s': %s", filepath.c_str(), strerror(errno));
        return false;
    }

    // Index parse + subsequent texture uploads stream through the file mostly
    // in append order; hint the kernel so it can read ahead and drop pages
    // behind the cursor promptly once they've been consumed.
    madvise(mapping, fileSize, MADV_SEQUENTIAL);

    const auto* base = static_cast<const uint8_t*>(mapping);

    // Validate header
    const auto* header = reinterpret_cast<const EQE1Header*>(base);

    if (header->magic != EQE1_MAGIC) {
        EQE1_LOG_ERROR("EQE1Loader: bad magic 0x%08X in '%s'", header->magic, filepath.c_str());
        munmap(mapping, fileSize);
        return false;
    }

    if (header->version != EQE1_VERSION &&
        header->version != EQE1_VERSION_V2 &&
        header->version != EQE1_VERSION_V3) {
        EQE1_LOG_ERROR("EQE1Loader: unsupported version %u in '%s'", header->version, filepath.c_str());
        munmap(mapping, fileSize);
        return false;
    }

    const uint16_t numEntries = header->numEntries;
    const uint32_t stride = indexEntryStride(header->version);
    size_t indexEnd = sizeof(EQE1Header) + numEntries * stride;

    if (fileSize < indexEnd) {
        EQE1_LOG_ERROR("EQE1Loader: truncated index (%zu < %zu) in '%s'",
                       fileSize, indexEnd, filepath.c_str());
        munmap(mapping, fileSize);
        return false;
    }

    // v3: the name table sits between the index and the first data byte. The
    // builder writes it immediately after the index (contiguous, NUL-free),
    // so every table-resident reference lands in [indexEnd, dataStart).
    // dataStart is the smallest dataOffset over the data-carrying entries;
    // a pack with none has no data bytes, so the table may run to EOF.
    uint32_t dataStart = static_cast<uint32_t>(fileSize);
    if (header->version == EQE1_VERSION_V3) {
        const auto* v3 = reinterpret_cast<const EQE1IndexEntry*>(
            base + sizeof(EQE1Header));
        for (uint16_t i = 0; i < numEntries; ++i) {
            if (v3[i].dataSize != 0) {
                dataStart = std::min(dataStart, v3[i].dataOffset);
            }
        }
    }

    // Parse index entries — first pass: build texture and raw data maps
    const auto* entriesBase = base + sizeof(EQE1Header);

    int textureCount = 0;
    int alphaCount = 0;
    int rawCount = 0;

    for (uint16_t i = 0; i < numEntries; ++i) {
        const auto* entryAt = entriesBase + static_cast<size_t>(i) * stride;
        const auto& e = *reinterpret_cast<const EQE1IndexEntry*>(entryAt);
        const auto* tail = (header->version == EQE1_VERSION_V3)
            ? reinterpret_cast<const EQE1IndexEntryNameTail*>(entryAt + sizeof(EQE1IndexEntry))
            : nullptr;

        // Validate bounds
        if (static_cast<size_t>(e.dataOffset) + e.dataSize > fileSize) {
            EQE1_LOG_ERROR("EQE1Loader: entry %u data out of bounds in '%s'", i, filepath.c_str());
            munmap(mapping, fileSize);
            return false;
        }

        if (e.dataOffset % EQE1_DATA_ALIGNMENT != 0) {
            EQE1_LOG_ERROR("EQE1Loader: entry %u misaligned (offset=%u) in '%s'",
                           i, e.dataOffset, filepath.c_str());
            munmap(mapping, fileSize);
            return false;
        }

        // Extract name.
        // v3 entries with a non-zero name tail carry their name in the table
        // (the 32-byte inline field is zeroed). Every other entry — all of
        // v1/v2, and v3's short names — must be null-terminated within the
        // 32-byte field. Note the B910 defect (filed as B100; soldunga's
        // gnome_stronghold_floor_transition stored as
        // gnome_stronghold_floor_transiti) did NOT trip this check: the old
        // builder zero-initialized the entry before strncpy, so a truncated
        // name was null-PADDED and formally valid — that silence is why it
        // hid for years. This check is the secondary net for a genuinely
        // unterminated field (e.g. a hand-crafted or corrupted pack); the
        // primary fix is on the write side (the builder now emits v3 for
        // long names), pinned by testV1TruncationHistoricalMiss /
        // testV3NameTableRoundTrip in tests/test_eqe1_loader.cpp.
        std::string name;
        if (tail && !eqe1NameInline(header->version, tail)) {
            if (tail->nameLength == 0 ||
                static_cast<size_t>(tail->nameOffset) >= fileSize ||
                static_cast<size_t>(tail->nameOffset) + tail->nameLength > fileSize ||
                tail->nameOffset < indexEnd) {
                EQE1_LOG_ERROR("EQE1Loader: entry %u name-table reference out of bounds "
                               "(offset=%u, length=%u) in '%s'",
                               i, tail->nameOffset, tail->nameLength, filepath.c_str());
                munmap(mapping, fileSize);
                return false;
            }
            if (dataStart != 0 &&
                (tail->nameOffset > dataStart ||
                 static_cast<size_t>(tail->nameOffset) + tail->nameLength > dataStart)) {
                EQE1_LOG_ERROR("EQE1Loader: entry %u name-table reference escapes the "
                               "name table (offset=%u, length=%u) in '%s'",
                               i, tail->nameOffset, tail->nameLength, filepath.c_str());
                munmap(mapping, fileSize);
                return false;
            }
            name.assign(reinterpret_cast<const char*>(base + tail->nameOffset),
                        tail->nameLength);
        } else {
            if (e.name[EQE1_NAME_SIZE - 1] != '\0') {
                EQE1_LOG_ERROR("EQE1Loader: entry %u name is not null-terminated within "
                               "the %u-byte field (truncated name? pack needs a v3 "
                               "rebuild) in '%s'", i, EQE1_NAME_SIZE, filepath.c_str());
                munmap(mapping, fileSize);
                return false;
            }
            name = e.name;
        }

        if (e.entryType == EQE1_TYPE_RAW_DATA) {
            rawDataLookup_[name] = {base + e.dataOffset, e.dataSize};
            rawCount++;
        } else {
            TextureEntry tex;
            tex.data = base + e.dataOffset;
            tex.dataSize = e.dataSize;
            tex.width = e.width;
            tex.height = e.height;
            tex.mipLevels = e.mipLevels;
            tex.hasAlpha = (e.hasAlpha != 0);
            tex.alphaData = nullptr;
            tex.alphaDataSize = 0;
            tex.payloadFormat = e.payloadFormat;
            textureLookup_[name] = tex;
            textureCount++;
            if (tex.hasAlpha) alphaCount++;
        }
    }

    // Second pass: link alpha companions
    for (auto& [name, tex] : textureLookup_) {
        if (!tex.hasAlpha) continue;

        std::string alphaName = name + "_alpha";
        auto alphaIt = textureLookup_.find(alphaName);
        if (alphaIt != textureLookup_.end()) {
            tex.alphaData = alphaIt->second.data;
            tex.alphaDataSize = alphaIt->second.dataSize;
        }
    }

    filepath_ = filepath;
    mapping_ = mapping;
    mappingSize_ = fileSize;
    entryCount_ = numEntries;
    isOpen_ = true;

    EQE1_LOG_INFO("EQE1Loader: mmap'd '%s' (v%u, %d textures, %d alpha, %d raw, %zu bytes)",
                  filepath.c_str(), header->version, textureCount, alphaCount, rawCount, fileSize);

    return true;
}

void EQE1Loader::close() {
    textureLookup_.clear();
    rawDataLookup_.clear();
    if (mapping_) {
        munmap(mapping_, mappingSize_);
        mapping_ = nullptr;
        mappingSize_ = 0;
    }
    filepath_.clear();
    entryCount_ = 0;
    lookupHits_ = 0;
    isOpen_ = false;
}

const EQE1Loader::TextureEntry* EQE1Loader::lookup(const std::string& name) const {
    auto it = textureLookup_.find(name);
    if (it != textureLookup_.end()) {
        ++lookupHits_;  // T207
        return &it->second;
    }
    return nullptr;
}

bool EQE1Loader::getRawData(const std::string& name,
                             const uint8_t*& data, uint32_t& size) const {
    auto it = rawDataLookup_.find(name);
    if (it != rawDataLookup_.end()) {
        data = it->second.first;
        size = it->second.second;
        ++lookupHits_;  // T207
        return true;
    }
    return false;
}

std::vector<std::string> EQE1Loader::getTextureNames() const {
    std::vector<std::string> names;
    names.reserve(textureLookup_.size());
    for (const auto& [name, _] : textureLookup_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> EQE1Loader::getRawDataNames() const {
    std::vector<std::string> names;
    names.reserve(rawDataLookup_.size());
    for (const auto& [name, _] : rawDataLookup_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

}} // namespace EQT::Graphics
