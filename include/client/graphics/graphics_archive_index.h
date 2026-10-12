#ifndef EQT_GRAPHICS_ARCHIVE_INDEX_H
#define EQT_GRAPHICS_ARCHIVE_INDEX_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

namespace EQT {
namespace Graphics {

class PfsArchive;

// Persistent archive index for O(1) race-to-archive lookups.
// Mirrors the audio system's PFS index caching pattern:
//   - First run: scans *_chr.s3d archives, extracts character names, maps race codes
//   - Subsequent runs: loads gfx_index_cache.json from the run-local cache
//     directory (T402, board B248 -- eqt::ResolveCachePathForRead; it used to
//     be a git-tracked config/gfx_index_cache.json), validated by file sizes
//
// The index is pure filename arithmetic: scanArchives() opens NO archive, so
// the lazy/eager choice the constrained preset carries
// (ConstrainedRendererConfig::lazyPfsLoading) never reaches it -- the
// buildIndex(eqPath, lazyMode, ...) parameter and the lazyMode_ member were
// stored and never read (B295, #125, found by T407's census). The parameter
// and member went with the finding; the one surviving call site
// (renderer_zone_load.cpp Step 5) builds the index the same way for every
// preset.
class GraphicsArchiveIndex {
public:
    GraphicsArchiveIndex() = default;
    ~GraphicsArchiveIndex() = default;

    // Build the index (tries cache first, falls back to scanning archives)
    // tickCallback: called between archives to pump network event loop
    // Returns true if index was built successfully
    bool buildIndex(const std::string& eqPath,
                    std::function<void()> tickCallback = nullptr);

    // Look up which archive contains a given race+gender
    // Returns archive path, or empty string if not found
    // Key is (raceId << 8 | gender)
    std::string getArchiveForRace(uint16_t raceId, uint8_t gender) const;

    // Cached open archive. Zero-caller (check_zero_callers.py's B494 row);
    // kept, like the cachedArchives_ member, until B494 takes it.
    PfsArchive* getCachedArchive(const std::string& archivePath);

    // Get the number of indexed race entries
    size_t getRaceEntryCount() const { return raceIndex_.size(); }

    // Get the number of indexed archives
    size_t getArchiveCount() const { return archiveSizes_.size(); }

private:
    bool loadCache();
    void saveCache();
    void scanArchives(std::function<void()> tickCallback);

    // Make composite key for race index
    static uint32_t makeKey(uint16_t raceId, uint8_t gender) {
        return (static_cast<uint32_t>(raceId) << 8) | gender;
    }

    std::string eqPath_;

    // Race index: (raceId << 8 | gender) -> archive filename (just the filename, not full path)
    std::unordered_map<uint32_t, std::string> raceIndex_;

    // Archive validation: filename -> file size
    std::map<std::string, uintmax_t> archiveSizes_;

    // Cached open archives (never populated by scanArchives(); B494's
    // getCachedArchive is the only accessor)
    std::map<std::string, std::unique_ptr<PfsArchive>> cachedArchives_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ARCHIVE_INDEX_H
