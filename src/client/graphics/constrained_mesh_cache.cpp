#include "client/graphics/constrained_mesh_cache.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

ConstrainedMeshCache::ConstrainedMeshCache(size_t budgetBytes)
    : budgetBytes_(budgetBytes) {
}

ConstrainedMeshCache::~ConstrainedMeshCache() {
    // Note: caller is responsible for removing scene nodes before destroying cache
    clear();
}

void ConstrainedMeshCache::registerRegion(size_t regionIdx) {
    if (cache_.find(regionIdx) != cache_.end()) return;

    CachedRegionMesh entry;
    entry.node = nullptr;
    entry.sizeBytes = 0;
    entry.loaded = false;
    entry.wasEverLoaded = false;

    // Add to front of LRU (unloaded regions have lowest priority)
    lruOrder_.push_front(regionIdx);
    entry.lruIterator = lruOrder_.begin();
    cache_[regionIdx] = entry;
}

void ConstrainedMeshCache::onLoaded(size_t regionIdx, irr::scene::IMeshSceneNode* node, size_t sizeBytes) {
    auto it = cache_.find(regionIdx);
    if (it == cache_.end()) return;

    auto& entry = it->second;

    // Track rebuilds (loaded more than once = was evicted and rebuilt)
    if (entry.wasEverLoaded) {
        rebuildCount_++;
    }

    entry.node = node;
    entry.sizeBytes = sizeBytes;
    entry.loaded = true;
    entry.wasEverLoaded = true;
    currentUsage_ += sizeBytes;

    // Move to back of LRU (most recently used)
    lruOrder_.erase(entry.lruIterator);
    lruOrder_.push_back(regionIdx);
    entry.lruIterator = std::prev(lruOrder_.end());

    cacheHits_++;
}

void ConstrainedMeshCache::touch(size_t regionIdx) {
    auto it = cache_.find(regionIdx);
    if (it == cache_.end() || !it->second.loaded) return;

    // Move to back of LRU
    lruOrder_.erase(it->second.lruIterator);
    lruOrder_.push_back(regionIdx);
    it->second.lruIterator = std::prev(lruOrder_.end());

    cacheHits_++;
}

bool ConstrainedMeshCache::isLoaded(size_t regionIdx) const {
    auto it = cache_.find(regionIdx);
    return it != cache_.end() && it->second.loaded;
}

irr::scene::IMeshSceneNode* ConstrainedMeshCache::getNode(size_t regionIdx) const {
    auto it = cache_.find(regionIdx);
    if (it != cache_.end() && it->second.loaded) {
        return it->second.node;
    }
    return nullptr;
}

std::vector<size_t> ConstrainedMeshCache::evictUntilAvailable(size_t bytesNeeded,
    const std::unordered_set<size_t>& protectedRegions) {
    std::vector<size_t> result;
    if (frozen_) return result;

    size_t scanCount = 0;
    size_t maxScans = lruOrder_.size();

    while (currentUsage_ + bytesNeeded > budgetBytes_ && scanCount < maxScans) {
        size_t candidate = lruOrder_.front();

        auto cacheIt = cache_.find(candidate);
        if (cacheIt == cache_.end()) {
            // Shouldn't happen, but be safe
            lruOrder_.pop_front();
            scanCount++;
            continue;
        }

        // Skip protected or unloaded regions
        if (protectedRegions.count(candidate) > 0 || !cacheIt->second.loaded) {
            // Move to back of LRU and try next
            lruOrder_.pop_front();
            lruOrder_.push_back(candidate);
            cacheIt->second.lruIterator = std::prev(lruOrder_.end());
            scanCount++;
            continue;
        }

        // Evict this region
        LOG_DEBUG(MOD_GRAPHICS, "MeshCache: evicting region {} ({} bytes)",
            candidate, cacheIt->second.sizeBytes);

        currentUsage_ -= cacheIt->second.sizeBytes;
        cacheIt->second.loaded = false;
        cacheIt->second.node = nullptr;  // Caller handles node->remove()
        cacheIt->second.sizeBytes = 0;

        // Move to front of LRU as unloaded placeholder
        lruOrder_.pop_front();
        lruOrder_.push_front(candidate);
        cacheIt->second.lruIterator = lruOrder_.begin();

        evictionCount_++;
        result.push_back(candidate);
        scanCount = 0;  // Reset scan since list changed
    }

    return result;
}

void ConstrainedMeshCache::getUnloadedRegions(std::vector<size_t>& out) const {
    out.clear();
    for (const auto& [regionIdx, entry] : cache_) {
        if (!entry.loaded) {
            out.push_back(regionIdx);
        }
    }
}

void ConstrainedMeshCache::clear() {
    cache_.clear();
    lruOrder_.clear();
    currentUsage_ = 0;
}

size_t ConstrainedMeshCache::getLoadedCount() const {
    size_t count = 0;
    for (const auto& [idx, entry] : cache_) {
        if (entry.loaded) count++;
    }
    return count;
}

size_t ConstrainedMeshCache::getEvictedCount() const {
    size_t count = 0;
    for (const auto& [idx, entry] : cache_) {
        if (!entry.loaded && entry.wasEverLoaded) count++;
    }
    return count;
}

float ConstrainedMeshCache::getHitRate() const {
    size_t total = cacheHits_ + cacheMisses_;
    if (total == 0) return 0.0f;
    return (static_cast<float>(cacheHits_) / total) * 100.0f;
}

void ConstrainedMeshCache::resetStatistics() {
    cacheHits_ = 0;
    cacheMisses_ = 0;
    evictionCount_ = 0;
    rebuildCount_ = 0;
}

size_t ConstrainedMeshCache::estimateMeshSize(irr::scene::IMeshSceneNode* node) {
    if (!node || !node->getMesh()) return 0;

    size_t total = 0;
    auto* mesh = node->getMesh();
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        auto* buf = mesh->getMeshBuffer(i);
        if (buf) {
            total += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
            total += buf->getIndexCount() * sizeof(uint16_t);
        }
    }
    return total;
}

} // namespace Graphics
} // namespace EQT
