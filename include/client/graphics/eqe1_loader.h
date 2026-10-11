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

    // B930: the version this pack's header declares (0 if not open). v1/v2
    // packs cap entry names at 31 characters; a name longer than that can
    // only be stored by a v3 pack's name table. The all-or-nothing miss path
    // uses this to name the pack that truncated a >31-char texture name
    // (soldunga's gnome_stronghold_floor_transition, #248) instead of a
    // texture-named FATAL with no cause.
    uint16_t getFormatVersion() const { return formatVersion_; }

    // B930: does this pack hold the 31-character truncation of `name` as one
    // of its OWN entries? Only meaningful for a v1/v2 pack holding a name
    // longer than the 31-char inline field (a v3 pack cannot truncate): a
    // yes is proof this pack was built by the pre-B910 builder, which
    // silently strncpy'd long names to 31 — the all-or-nothing FATAL then
    // names THIS pack as stale instead of naming the missing texture. An
    // entry whose genuine name IS the 31-char prefix (a different texture)
    // is the same string and indistinguishable; the message stays true
    // either way (the pack does carry the prefix), and the fix is the same.
    bool holdsTruncatedName(const std::string& name) const;

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
    // B930: the header version this file declared at open() (0 == not open).
    uint16_t formatVersion_ = 0;
    mutable uint32_t lookupHits_ = 0;  // T207, see getLookupHits()
    bool isOpen_ = false;
};

// B930: the pure half of the all-or-nothing miss diagnosis, split from
// ConstrainedTextureCache::getOrLoad()'s FATAL site so it is testable
// without a GL context (ConstrainedTextureCache needs irr::video device
// headers the standalone loader test does not link).
//
// The miss in question: a texture name LONGER than the 31-char inline field
// (EQE1_INLINE_NAME_LIMIT) that no open pack holds in full. A v3 pack cannot
// miss a name it was told to store (its name table is full-length; B910), so
// the only way the client gets here with a >31-char name is a v1/v2 pack that
// the pre-B910 builder truncated: it holds the 31-char prefix as a genuine
// entry key and the full name misses. That is the stale-pack signature —
// soldunga.eqe1 v1 holding gnome_stronghold_floor_transiti (31) while the
// client asks for gnome_stronghold_floor_transition (33), #248.
//
// `loaders` is the snapshot of every open archive (the same one the FATAL
// site takes). The answer names the offending pack and its version. An empty
// result means the miss is the plain asset-gap kind the old FATAL already
// describes, or every open pack is new enough (v3) that a truncation is
// impossible and the old message stays correct.
struct StaleEQE1PackDiagnosis {
    bool stalePack = false;      // a v1/v2 pack holds the 31-char prefix
    std::string packPath;        // that pack's file path
    uint16_t packVersion = 0;    // its header version (1 or 2)
    std::string truncatedName;   // the 31-char key the pack actually holds
};

StaleEQE1PackDiagnosis diagnoseStaleEQE1Pack(const std::vector<const EQE1Loader*>& loaders,
                                             const std::string& name);

}} // namespace EQT::Graphics
