#pragma once

// B917 (issue #236): the game-thread <-> render-thread handoff of a zone load.
//
// Before this, Application kept ONE eqt::ZoneLoadSnapshot member that three
// parties shared: the game thread wrote it on every zone-load request, the
// loading thread read it by reference for the whole of a load, and the game
// thread CLEARED it (P04) when it finalised a load. A server-directed zone
// change that landed while the first load was still running (a death that
// sends the player to the bind point, a kick for level, a GM summon)
// therefore
//   1. overwrote the snapshot under the running loader's feet (a data race;
//      the charasis run logged "complete for zone 'qeynos'" for a charasis
//      load),
//   2. finalised the NEW zone when the OLD load finished (the game thread
//      could not tell which load had completed: "Ready!" for a zone whose
//      graphics had not been built, with the old zone's BSP tree labelled as
//      the new one's), and
//   3. cleared the snapshot while the render thread was starting the
//      follow-on load from it, which then read an empty zone name (".s3d")
//      or an empty client path ("qeynos.s3d") and died on a FATAL.
//
// The fix is to make ownership explicit. The game thread POSTS a snapshot
// into a pending slot and gets a generation number; the render thread TAKES
// it (the slot is emptied, the loading thread then reads its own private
// copy that nothing else writes); the render thread COMPLETES a load with its
// generation; the game thread CLASSIFIES the completion and finalises only
// the load it is actually waiting for. A superseded load is simply finished
// and discarded -- the pending snapshot for the destination zone is still in
// the slot, so the render thread starts it on its next frame.
//
// Everything here is device-free, so tests/test_zone_load_handoff.cpp drives
// the whole mid-load sequence with real threads and no server.

#include "client/zone_load_snapshot.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace EQT { namespace Graphics { struct BspTree; } }

namespace eqt {

class ZoneLoadHandoff {
public:
    /** What the loading thread built, handed back to the game thread. */
    struct Completion {
        uint64_t generation = 0;   // the posted load this is the result of
        std::string zoneName;      // the zone the loader actually built
        std::shared_ptr<EQT::Graphics::BspTree> bspTree;
    };

    enum class Verdict {
        Apply,  // the load the game is waiting for: finalise it
        Stale,  // superseded by a later request, or the game has since left
                // the zone: discard it and do NOT report the zone ready
    };

    // ---- game thread -------------------------------------------------------

    /** Posts a zone-load request. Returns its generation (1, 2, ...). */
    uint64_t post(ZoneLoadSnapshot snapshot);

    /** The generation of the most recent post() (0 before the first). */
    uint64_t latestPosted() const;

    /** Takes the completion the render thread published, if any. */
    std::optional<Completion> takeCompletion();

    /**
     * Whether a completion should be finalised. `gameAwaitingGraphics` is
     * true while the game is in the graphics-loading phases it entered when it
     * posted the request (EverQuest::GetLoadingPhase() in GRAPHICS_LOADING_ZONE
     * .. GRAPHICS_FINALIZING); a load that finishes after the game has begun
     * leaving that zone is stale even when no later request exists yet.
     */
    static Verdict classify(const Completion& completion, uint64_t latestPosted,
                            bool gameAwaitingGraphics);

    /**
     * What Application's game thread calls once per loop iteration: takes the
     * published completion, if any, and returns it only when classify() says
     * Apply. A Stale completion is logged and dropped here -- the zone it
     * built is not the one the game is entering, so it must not be reported
     * ready (and its BSP tree must not be installed).
     */
    std::optional<Completion> takeApplicableCompletion(bool gameAwaitingGraphics);

    // ---- render thread -----------------------------------------------------

    /** True when a posted snapshot is waiting to be started. */
    bool hasPending() const { return hasPending_.load(std::memory_order_acquire); }

    /**
     * Moves the pending snapshot out (the slot becomes empty) together with
     * its generation. Returns false if nothing was pending.
     */
    bool take(ZoneLoadSnapshot& out, uint64_t& generation);

    /** Publishes a SUCCESSFUL load's result for the game thread. */
    void complete(Completion completion);

    /**
     * What Application's render thread calls after joining a loading thread.
     * A failed load is recorded as the B918 fatal (and nothing is published:
     * the process is going down); a successful one is published with its
     * generation and the zone it built. Returns false for a failed load.
     */
    bool finishLoad(uint64_t generation, bool failed, std::string zoneName,
                    std::shared_ptr<EQT::Graphics::BspTree> bspTree,
                    std::string failureReason = {});

    /** True once a published completion is waiting for the game thread. */
    bool hasCompletion() const { return hasCompletion_.load(std::memory_order_acquire); }

    // ---- B918: the loader-failure record -----------------------------------

    /** The loading thread failed (FATAL): the process must exit non-zero. */
    void recordLoaderFailure(std::string reason);
    bool loaderFailed() const { return loaderFailed_.load(std::memory_order_acquire); }
    std::string loaderFailureReason() const;

private:
    mutable std::mutex mutex_;
    ZoneLoadSnapshot pending_;                 // guarded by mutex_
    uint64_t latestPosted_ = 0;                // guarded by mutex_
    std::optional<Completion> completion_;     // guarded by mutex_
    std::string failureReason_;                // guarded by mutex_
    std::atomic<bool> hasPending_{false};
    std::atomic<bool> hasCompletion_{false};
    std::atomic<bool> loaderFailed_{false};
};

}  // namespace eqt
