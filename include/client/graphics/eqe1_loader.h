#pragma once

#include "client/graphics/eqe1_format.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace EQT { namespace Graphics {

class EQE1Loader {
public:
    EQE1Loader();
    ~EQE1Loader();

    EQE1Loader(const EQE1Loader&) = delete;
    EQE1Loader& operator=(const EQE1Loader&) = delete;
    EQE1Loader(EQE1Loader&&) noexcept;
    EQE1Loader& operator=(EQE1Loader&&) noexcept;

    bool open(const std::string& filepath);
    void close();

    struct TextureEntry {
        const uint8_t* data;
        uint32_t dataSize;
        uint16_t width;
        uint16_t height;
        uint8_t  mipLevels;
        bool     hasAlpha;
        const uint8_t* alphaData;
        uint32_t alphaDataSize;
        uint8_t  payloadFormat = 0;  // EQE1PayloadFormat; 0 == EQE1_PAYLOAD_ETC1 (v1 default)
    };

    const TextureEntry* lookup(const std::string& name) const;
    bool getRawData(const std::string& name, const uint8_t*& data, uint32_t& size) const;

    std::vector<std::string> getTextureNames() const;
    std::vector<std::string> getRawDataNames() const;

    uint16_t getEntryCount() const { return entryCount_; }

    // T207: how many lookups this archive actually SERVED since it was opened.
    // The EQE1 lazy opener is a linear probe over globalArchives, so an archive
    // being open proves nothing about it being needed. A zero here is a proven
    // unnecessary open+mmap -- the metric the manifest/index fix has to drive
    // to zero. Counts successful lookup() and getRawData() calls only.
    uint32_t getLookupHits() const { return lookupHits_; }
    const std::string& getFilePath() const { return filepath_; }
    bool isOpen() const { return isOpen_; }

    // P04: bytes currently mmap'd for this archive (0 if not open). Used by the
    // load-mem-audit instrumentation and /pmem's accounted-vs-RSS breakdown to
    // name EQE1 mapped memory as a distinct suspect.
    size_t getMappedBytes() const { return isOpen_ ? mappingSize_ : 0; }

private:
    std::string filepath_;
    // P04: file is mmap'd read-only (MAP_PRIVATE) rather than heap-loaded. Entry
    // pointers below point into this mapping. Backing pages are page-cache
    // resident (reclaimable under memory pressure), unlike the old heap vector
    // which pinned every byte of every opened archive as anonymous memory.
    void* mapping_ = nullptr;
    size_t mappingSize_ = 0;
    std::unordered_map<std::string, TextureEntry> textureLookup_;
    std::unordered_map<std::string, std::pair<const uint8_t*, uint32_t>> rawDataLookup_;
    uint16_t entryCount_ = 0;
    mutable uint32_t lookupHits_ = 0;  // T207, see getLookupHits()
    bool isOpen_ = false;
};

}} // namespace EQT::Graphics
