#pragma once

#ifdef WITH_AUDIO

#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace EQT {
namespace Audio {

class SoundBuffer;

// B1015 (#336): the ONE decoded-PCM cache of SFX PCM.
//
// Before B1015, `AudioManager::bufferCache_` and `SfxManager::cache_` were
// TWO filename-keyed LRUs with two caps (`setMemoryConstraints` set both).
// They evicted different victims -- the sfx LRU skipped in-use entries, the
// buffer cache did not -- so three defects stood on fable:
//
//   1. a bufferCache_ eviction resurrected a second decoded copy on the next
//      play (SfxManager::preload() was a no-op for a filename it had);
//   2. the cap bound a PROJECTION of the resident PCM: the MR's own cap case
//      (cap 21,919) held 11,072 B in the buffer cache and 21,920 B in the
//      sfx cache -- 32,992 B = 11,072 + 21,920 of PCM live under 21,919;
//   3. pre-existing since T412: when every older sfx entry was playing, the
//      sfx LRU evicted the entry it had JUST preloaded, so play() returned -1
//      and the requested sound silently never played.
//
// B1015 removes the second LRU: this class is the one population of decoded
// SFX PCM under the one cap (the preset's `soundCache`), with in-use
// pinning in it, and with the one rule that makes the cap honest:
//
//   * ONE cap bounds the TOTAL resident decoded PCM (getSizeBytes()). There
//     is no second cache outside it to hold a copy of the same PCM.
//   * An entry an ACTIVE mixer channel points into is never evicted (the
//     channel holds a raw `float*` into the buffer's vector -- the pin IS
//     the safety, not a shared_ptr).
//   * The entry being inserted (or just touched) is at the FRONT of the LRU
//     when eviction runs, and eviction walks from the BACK -- the entry a
//     caller just asked for is the last thing that can go, and never goes
//     while anything older is evictable (or the cache holds only it).
//
// The decoded PCM is charged ONCE, by the loader, to the owner that decoded
// it (`au.sound_buffers` in the loader's StartupScope) -- CLAUDE.md's memory
// accounting: a block shared by several owners is reported once, with its
// owner named. This class never allocates PCM.
//
// Threading: all calls are game-thread (fetch / evict / set cap / clear);
// the audio thread never touches this cache. The in-use pin is a callback
// into the mixer's channel table, which the audio thread only reads from.
class SoundBufferCache {
public:
    // The loader: decodes `filename` (files or the snd*.pfs archives) into
    // a FRESH SoundBuffer, or returns nullptr when it cannot be found. It
    // runs WITHOUT the cache lock (a first fetch can scan the archives,
    // ~0.4 s of I/O, and the loader keeps its own lock), so it must not call
    // back into the cache or the manager.
    using Source = std::function<std::shared_ptr<SoundBuffer>(const std::string&)>;
    // The in-use pin: true for a buffer an ACTIVE mixer channel is reading
    // right now. Called with entries_ held; the mixer's channel table is
    // written by the game thread only (allocation / free), so no second
    // lock is needed (the audio thread only reads an already-published
    // channel -- the same rule SfxManager::isInUseLocked() always relied on).
    using Pin = std::function<bool(const SoundBuffer&)>;

    SoundBufferCache();

    // Not copyable
    SoundBufferCache(const SoundBufferCache&) = delete;
    SoundBufferCache& operator=(const SoundBufferCache&) = delete;

    void setSource(Source source) { source_ = std::move(source); }
    void setPin(Pin pin) { inUse_ = std::move(pin); }

    // Fetch the decoded buffer for `filename`: a hit is returned as-is
    // (touched to the front); a miss decodes through the source, inserts,
    // and enforces the cap before returning. Returns nullptr when there is
    // no source or the sound cannot be found.
    std::shared_ptr<SoundBuffer> getOrLoad(const std::string& filename);

    // Residency check WITHOUT loading: returns the resident buffer for
    // `filename`, or nullptr when it is not in the cache. It does not decode
    // and does not touch the LRU, so the tests can observe what eviction
    // left behind without triggering a re-load. (getOrLoad re-loads on a
    // miss, so it cannot be used to tell "evicted" from "resident".)
    std::shared_ptr<SoundBuffer> peek(const std::string& filename) const;

    // Drop every entry (shutdown). Used only after the mixer is stopped, so
    // nothing is pinned.
    void clear();

    // The cap: 0 = unlimited. Applying a lower ceiling evicts immediately,
    // so lowering it at runtime takes effect at once (setMemoryConstraints
    // is called precisely because memory is already tight).
    void setMaxBytes(size_t maxBytes);

    // Re-run the cap check now, without changing the ceiling. The in-use pin
    // may have released entries since the last check (a channel finished
    // playing), so a cache that was lawfully over budget while its sounds
    // were playing can now reclaim them. No-op when the cap is 0 (unlimited)
    // or the cache is already at or under the cap.
    void rebalance();

    // The cap's accounting row: the TOTAL decoded PCM resident here.
    size_t getSizeBytes() const;
    size_t getMaxBytes() const;
    // The number of distinct sounds resident (for the debug and the tests).
    size_t getEntryCount() const;

private:
    // All callers require bufferMutex_ to be held. Walk the LRU from the
    // BACK (oldest) and evict until under the cap:
    //   * the FRONT entry (the most recently touched -- the one a caller
    //     just inserted or re-fetched) is never evicted;
    //   * an entry the in-use pin names is skipped;
    //   * an LRU name with no map entry is dropped.
    // When nothing left is evictable (only pinned entries, or only the front
    // entry), the cache stays over budget and a WARN is logged: over budget
    // is better than a use-after-free, and the next insertion tries again.
    void enforceBudgetLocked();

    mutable std::mutex bufferMutex_;
    std::unordered_map<std::string, std::shared_ptr<SoundBuffer>> entries_;
    // Most-recently-used at the front.
    std::list<std::string> lruOrder_;
    size_t maxBytes_ = 0;    // 0 = unlimited
    size_t sizeBytes_ = 0;   // the cap's accounting row
    Source source_;
    Pin inUse_;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
