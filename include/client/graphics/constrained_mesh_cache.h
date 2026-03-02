#ifndef EQT_GRAPHICS_CONSTRAINED_MESH_CACHE_H
#define EQT_GRAPHICS_CONSTRAINED_MESH_CACHE_H

#include <irrlicht.h>
#include <map>
#include <list>
#include <vector>
#include <unordered_set>
#include <cstddef>

namespace EQT {
namespace Graphics {

// LRU mesh cache with memory budget enforcement for region meshes.
// Used in constrained rendering mode to lazily load/evict zone region meshes
// within a frame-time budget to maintain target FPS.
class ConstrainedMeshCache {
public:
    explicit ConstrainedMeshCache(size_t budgetBytes);
    ~ConstrainedMeshCache();

    // Register a region as known (starts unloaded). Called during zone load.
    void registerRegion(size_t regionIdx);

    // Record that a region mesh was just built. Adds to LRU + budget tracking.
    void onLoaded(size_t regionIdx, irr::scene::IMeshSceneNode* node, size_t sizeBytes);

    // Mark region as recently accessed (move to back of LRU).
    void touch(size_t regionIdx);

    // Mark a loaded region for rebuild (resets loaded state without eviction).
    // Subtracts old size from budget so onLoaded() won't double-count.
    void markForRebuild(size_t regionIdx);

    // Check if region mesh is currently loaded.
    bool isLoaded(size_t regionIdx) const;

    // Get scene node (nullptr if evicted/unloaded).
    irr::scene::IMeshSceneNode* getNode(size_t regionIdx) const;

    // Evict LRU meshes until bytesNeeded fits within budget.
    // protectedRegions are skipped (visible + buffer ring).
    // Returns evicted region indices (caller handles scene cleanup).
    std::vector<size_t> evictUntilAvailable(size_t bytesNeeded,
        const std::unordered_set<size_t>& protectedRegions);

    // Get all registered-but-not-loaded region indices.
    void getUnloadedRegions(std::vector<size_t>& out) const;

    // Clear all entries (zone unload).
    void clear();

    // Freeze/unfreeze (prevent eviction during transitions).
    void freeze() { frozen_ = true; }
    void unfreeze() { frozen_ = false; }
    bool isFrozen() const { return frozen_; }

    // Record a cache miss (visible region was not loaded)
    void cacheMiss() { cacheMisses_++; }

    // Stats
    size_t getCurrentUsage() const { return currentUsage_; }
    size_t getMemoryLimit() const { return budgetBytes_; }
    size_t getLoadedCount() const;
    size_t getEvictedCount() const;
    size_t getTotalCount() const { return cache_.size(); }
    size_t getEvictionCount() const { return evictionCount_; }
    size_t getRebuildCount() const { return rebuildCount_; }
    float getHitRate() const;
    void resetStatistics();

    // Estimate mesh memory from Irrlicht scene node
    static size_t estimateMeshSize(irr::scene::IMeshSceneNode* node);

private:
    struct CachedRegionMesh {
        irr::scene::IMeshSceneNode* node = nullptr;  // nullptr when evicted/unloaded
        size_t sizeBytes = 0;
        std::list<size_t>::iterator lruIterator;
        bool loaded = false;
        bool wasEverLoaded = false;  // true if loaded at least once (for rebuild count)
    };

    std::map<size_t, CachedRegionMesh> cache_;   // regionIdx -> entry
    std::list<size_t> lruOrder_;                  // front=oldest, back=newest
    size_t currentUsage_ = 0;
    size_t budgetBytes_;
    bool frozen_ = false;

    // Statistics
    size_t cacheHits_ = 0;
    size_t cacheMisses_ = 0;
    size_t evictionCount_ = 0;
    size_t rebuildCount_ = 0;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_CONSTRAINED_MESH_CACHE_H
