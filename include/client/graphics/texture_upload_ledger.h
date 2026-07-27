#pragma once

// P13 — zone-in texture upload audit instrumentation.
//
// Flag/env-gated, additive ledger of every GPU texture upload + eviction
// during a zone load, so a run can be reconstructed after the fact into the
// category tables the P13 plan asks for (zone regions / objects / entity
// per-part / composite sheet / sky / detail / UI / placeholder), plus
// eviction victim details and re-upload ("thrash") detection.
//
// OFF BY DEFAULT: ConstrainedTextureCache only calls into this when
// ConstrainedRendererConfig::textureUploadLedgerPath is non-empty (wired the
// same way as P04's --load-mem-audit). When disabled, TextureUploadLedger's
// record*() calls are simply never made — zero behavior change, verified by
// the off-by-default spot-render proof in the P13 review.
//
// One instance is owned per-ConstrainedTextureCache (i.e. per zone-load
// session); dumpToFile() is called once at the "zone-in complete" marker
// (end of loadZoneSequential).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace EQT { namespace Graphics {

// One row per actual driver texture creation (uploadTexture's RGBA path,
// uploadCompressedETC1, uploadCompressedV2, registerTexture, or the
// placeholder). "category" is a caller-supplied source tag (best-effort —
// call sites that were not updated to pass one record "uncategorized" rather
// than a fabricated guess).
struct TextureUploadRecord {
    uint32_t order = 0;          // monotonic upload sequence number this session
    std::string name;
    int width = 0;
    int height = 0;
    size_t bytes = 0;            // bytes as actually charged against the budget
    std::string format;          // "RGBA8888" | "ETC1" | "ETC1+alpha" | "RGB565" | "RGBA4444" | "external"
    std::string category;        // "zone_region" | "object" | "entity_perpart" | "composite_sheet" |
                                  // "sky" | "detail" | "ui" | "placeholder" | "uncategorized"
    std::string callsite;        // free-form, e.g. "loadZoneStep4_Regions"
    bool isReupload = false;     // true if this name was evicted earlier THIS session and is being re-uploaded (thrash)
};

// One row per eviction.
struct TextureEvictionRecord {
    uint32_t order = 0;          // monotonic eviction sequence number this session
    std::string name;
    size_t bytes = 0;
    std::string category;        // category recorded at upload time, if known
};

class TextureUploadLedger {
public:
    void reset();

    // Called from ConstrainedTextureCache's upload sites when enabled.
    void recordUpload(const std::string& name, int width, int height, size_t bytes,
                       const std::string& format, const std::string& category,
                       const std::string& callsite);

    // Called from evictTexture() when enabled.
    void recordEviction(const std::string& name, size_t bytes, const std::string& category);

    // Dump the full ledger (uploads + evictions, in session order) plus a
    // small summary header to a text file. Returns true on success.
    bool dumpToFile(const std::string& path, const std::string& runLabel) const;

    size_t uploadCount() const { return uploads_.size(); }
    size_t evictionCount() const { return evictions_.size(); }
    size_t thrashCount() const { return thrashCount_; }

    const std::vector<TextureUploadRecord>& uploads() const { return uploads_; }
    const std::vector<TextureEvictionRecord>& evictions() const { return evictions_; }

private:
    std::vector<TextureUploadRecord> uploads_;
    std::vector<TextureEvictionRecord> evictions_;
    // name -> category, populated on upload, consulted on eviction (so the
    // eviction record can carry the category even though evictTexture()
    // itself only has the CachedTexture, not the original call context).
    std::unordered_map<std::string, std::string> nameToCategory_;
    // Names ever evicted this session — used to flag a later same-name
    // upload as a thrash re-upload.
    std::unordered_set<std::string> everEvicted_;
    uint32_t uploadOrder_ = 0;
    uint32_t evictionOrder_ = 0;
    size_t thrashCount_ = 0;
};

}} // namespace EQT::Graphics
