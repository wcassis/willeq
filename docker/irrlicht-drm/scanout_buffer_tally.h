#pragma once
// B961 (GitLab #279): the client-side source for the scanout framebuffers.
//
// The device's GBM surface allocates its colour buffers on the DISPLAY
// device (CMA on the Orange Pi's sun4i-drm; the Lima render driver imports
// them by PRIME). The rig already reads them as root from
// /sys/kernel/debug/dri/display-engine/framebuffer (scripts/pi/
// proc_mem_snapshot.py `gpu.scanout_fb_bytes`, 2 x 3,686,400 B = 7.03 MiB at
// 1280x720), but the client's own ledger never saw them -- no per-process
// non-root source exists on this kernel. This class IS the client-side
// source: the device knows every gbm_bo it has locked, and a bo's backing
// store is stride x height. The set is tiny and bounded (the GBM pool is
// three buffers; the tracker can hold all three at once after a flip-event
// loss) -- one fixed table, a spin-locked pair of published totals, a C
// export the client core reads at its lifecycle points.
//
// Zero cost on the surfaceless/x11 paths: no bo is ever registered, the
// publish exports a zero tally, and the ledger row is 0.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace eqt_drm {

// A fixed-width CAS spinlock. The contending parties are the render thread
// (registration/teardown) and the gbm destroy callback (same thread today);
// every critical section is a handful of word writes, so a plain CAS loop is
// the whole implementation.
class SpinLock {
public:
    void lock() {
        for (;;) {
            if (!flag_.test_and_set(std::memory_order_acquire))
                return;
            std::atomic_thread_fence(std::memory_order_acquire);
        }
    }
    void unlock() { flag_.clear(std::memory_order_release); }
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// A RAII guard for SpinLock (public: the device's TU names it).
class SpinLockScope {
public:
    explicit SpinLockScope(SpinLock& l) : l_(l) { l_.lock(); }
    ~SpinLockScope() { l_.unlock(); }
    SpinLockScope(const SpinLockScope&) = delete;
    SpinLockScope& operator=(const SpinLockScope&) = delete;
private:
    SpinLock& l_;
};

class ScanoutBufferTally {
public:
    static constexpr size_t kSlots = 8;   // the GBM pool is three; headroom

    // A bo the device has locked for the surface. Registered exactly once
    // (its first lock), removed exactly once (its destroy callback).
    struct Entry {
        std::atomic<bool> inUse{false};
        std::atomic<uintptr_t> bo{0};
        std::atomic<uint64_t> bytes{0};
    };

    void registerBo(void* bo, uint32_t stride, uint32_t height) {
        if (!bo) return;
        const uint64_t b = static_cast<uint64_t>(stride) * height;
        uintptr_t key = reinterpret_cast<uintptr_t>(bo);
        SpinLockScope _l(lock_);
        for (Entry& e : entries_) {
            if (e.bo.load(std::memory_order_relaxed) == key) {
                if (e.bytes.load(std::memory_order_relaxed) != b) {
                    e.bytes.store(b, std::memory_order_relaxed);
                    recount();
                }
                return;
            }
            if (!e.inUse.load(std::memory_order_relaxed)) {
                e.bytes.store(b, std::memory_order_relaxed);
                e.bo.store(key, std::memory_order_relaxed);
                e.inUse.store(true, std::memory_order_release);
                recount();
                return;
            }
        }
        // Table full: a fourth distinct buffer beyond the GBM pool is not
        // possible in practice; drop the sample, keep the old tally.
    }

    // The gbm_bo_set_user_data callback: called once when the bo dies (and
    // again with the PREVIOUS data when user data is re-set, which the
    // device does not do). Removes only if this entry still is `bo`.
    void destroyCallback(void* bo, void* data) {
        (void)data;
        if (!bo) return;
        const uintptr_t key = reinterpret_cast<uintptr_t>(bo);
        SpinLockScope _l(lock_);
        for (Entry& e : entries_) {
            if (e.bo.load(std::memory_order_relaxed) == key) {
                e.inUse.store(false, std::memory_order_release);
                e.bo.store(0, std::memory_order_relaxed);
                e.bytes.store(0, std::memory_order_relaxed);
                recount();
                return;
            }
        }
    }

    void clear() {
        SpinLockScope _l(lock_);
        for (Entry& e : entries_) {
            e.inUse.store(false, std::memory_order_release);
            e.bo.store(0, std::memory_order_relaxed);
            e.bytes.store(0, std::memory_order_relaxed);
        }
        recount();
    }

    // The published pair; a ledger point on another thread reads these.
    uint32_t bufs() const {
        return bufCount_.load(std::memory_order_acquire);
    }
    uint64_t bytes() const {
        return totalBytes_.load(std::memory_order_acquire);
    }

private:
    void recount() {
        uint64_t total = 0;
        uint32_t count = 0;
        for (const Entry& e : entries_) {
            if (e.inUse.load(std::memory_order_relaxed)) {
                total += e.bytes.load(std::memory_order_relaxed);
                ++count;
            }
        }
        totalBytes_.store(total, std::memory_order_release);
        bufCount_.store(count, std::memory_order_release);
    }

    Entry entries_[kSlots]{};
    SpinLock lock_;
    std::atomic<uint32_t> bufCount_{0};
    std::atomic<uint64_t> totalBytes_{0};
};

} // namespace eqt_drm
