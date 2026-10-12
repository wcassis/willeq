#ifdef WITH_AUDIO

#include "client/audio/sound_buffer_cache.h"
#include "client/audio/sound_buffer.h"
#include "common/logging.h"

#include <utility>

namespace EQT {
namespace Audio {

SoundBufferCache::SoundBufferCache() = default;

std::shared_ptr<SoundBuffer> SoundBufferCache::getOrLoad(const std::string& filename) {
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        auto it = entries_.find(filename);
        if (it != entries_.end()) {
            // A hit is a touch: the sound just served becomes the most recent.
            lruOrder_.remove(it->first);
            lruOrder_.push_front(it->first);
            return it->second;
        }
    }

    // Decode WITHOUT the cache lock: the loader opens the snd*.pfs archives
    // (its own lock) and may scan them (a first fetch is ~0.4 s of I/O), and
    // it must never call back into this cache. The decode is charged to the
    // loader's `au.sound_buffers` owner by the caller.
    if (!source_) {
        return nullptr;
    }
    std::shared_ptr<SoundBuffer> buffer = source_(filename);
    if (!buffer || !buffer->isValid()) {
        LOG_DEBUG(MOD_AUDIO, "Sound buffer cache: no decoded PCM for '{}'", filename);
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        // Lost a race: another fetch inserted it while we decoded. Drop ours
        // (the caller still holds its own reference) and serve the resident
        // copy.
        auto it = entries_.find(filename);
        if (it != entries_.end()) {
            lruOrder_.remove(it->first);
            lruOrder_.push_front(it->first);
            return it->second;
        }
        entries_[filename] = buffer;
        lruOrder_.push_front(filename);
        sizeBytes_ += buffer->getMemorySize();
        // The entry just inserted sits at the FRONT of the LRU, and
        // enforceBudgetLocked() walks from the BACK -- so this call can
        // never evict what it just asked for.
        enforceBudgetLocked();
        return buffer;
    }
}

void SoundBufferCache::clear() {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    entries_.clear();
    lruOrder_.clear();
    sizeBytes_ = 0;
}

void SoundBufferCache::setMaxBytes(size_t maxBytes) {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    maxBytes_ = maxBytes;
    // Applying a lower ceiling at runtime must take effect now, not at the
    // next fetch -- setMemoryConstraints() is called precisely because memory
    // is already tight.
    enforceBudgetLocked();
}

void SoundBufferCache::rebalance() {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    enforceBudgetLocked();
}

size_t SoundBufferCache::getSizeBytes() const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    return sizeBytes_;
}

std::shared_ptr<SoundBuffer> SoundBufferCache::peek(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    auto it = entries_.find(filename);
    return it == entries_.end() ? nullptr : it->second;
}

size_t SoundBufferCache::getMaxBytes() const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    return maxBytes_;
}

size_t SoundBufferCache::getEntryCount() const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    return entries_.size();
}

void SoundBufferCache::enforceBudgetLocked() {
    if (maxBytes_ <= 0) return;  // unlimited

    // Evict from the LRU end (oldest) toward the front (most recent) until
    // the total is at or under the ceiling. Two rules shape the walk:
    //
    //   * An entry the in-use pin names is SKIPPED (a live channel reads its
    //     PCM; evicting it is a use-after-free). The cache stays over budget
    //     for as long as the pins hold -- the next rebalance/insertion tries
    //     again when they release.
    //   * The FRONT entry (the freshest -- the one a caller just inserted or
    //     re-fetched) is a FLOOR: the walk never evicts it. That is the
    //     T412/B1015 rule that the sound a caller just asked for is never
    //     the entry its own request evicts (honouring a cap smaller than a
    //     single sound by throwing away the sound just requested was the
    //     T412 defect); a single entry over budget is logged, not dropped.
    //
    // When the freshest entry alone exceeds the ceiling (or only it plus
    // pinned entries remain), the cache stays over budget and a WARN is
    // logged: over budget is better than a use-after-free, and the cap is
    // honoured the moment a smaller/fresher population fits.
    while (sizeBytes_ > maxBytes_) {
        if (lruOrder_.size() <= 1) {
            LOG_WARN(MOD_AUDIO,
                     "Sound buffer cache over budget ({} > {} bytes) -- only the "
                     "freshest entry remains",
                     sizeBytes_, maxBytes_);
            break;
        }
        bool evicted = false;
        // Walk the LRU from the BACK (oldest) toward the FRONT (most recent):
        // `--it` (the front is the first, so the loop stops at begin()). The
        // victim is the oldest evictable entry; pinned and the freshest are
        // skipped.
        for (auto it = std::prev(lruOrder_.end()); it != lruOrder_.begin(); --it) {
            auto entryIt = entries_.find(*it);
            if (entryIt == entries_.end()) {
                // Stale LRU name (evicted out from under the list) -- drop it.
                lruOrder_.erase(it);
                evicted = true;
                break;
            }
            if (inUse_ && inUse_(*entryIt->second)) {
                continue;  // pinned: a live channel reads this PCM; the loop's --it goes on
            }
            LOG_DEBUG(MOD_AUDIO, "Sound buffer cache: evicting '{}' ({} bytes, {} -> {} of {})",
                      *it, entryIt->second->getMemorySize(), sizeBytes_,
                      sizeBytes_ - entryIt->second->getMemorySize(), maxBytes_);
            sizeBytes_ -= entryIt->second->getMemorySize();
            entries_.erase(entryIt);
            lruOrder_.erase(it);
            evicted = true;
            break;
        }
        if (!evicted) {
            // Every evictable entry is pinned, or only the freshest entry
            // remains. Over budget is better than a use-after-free; the cap
            // is re-checked on the next insertion / rebalance.
            LOG_WARN(MOD_AUDIO,
                     "Sound buffer cache over budget ({} > {} bytes) -- every "
                     "evictable sound is playing or is the freshest entry",
                     sizeBytes_, maxBytes_);
            break;
        }
    }
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
