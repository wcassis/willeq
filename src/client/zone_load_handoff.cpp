// B917 (issue #236): the zone-load handoff between the game thread and the
// render thread. See include/client/zone_load_handoff.h for why it exists.

#include "client/zone_load_handoff.h"

#include "client/eq_types.h"   // Entity: ZoneLoadSnapshot holds std::vector<Entity>
#include "client/zone_bsp.h"   // BspTree: Completion holds a shared_ptr to one
#include "common/logging.h"

#include <string>
#include <utility>

namespace eqt {

uint64_t ZoneLoadHandoff::post(ZoneLoadSnapshot snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = std::move(snapshot);
    ++latestPosted_;
    // Set under the mutex, so a take() that runs between the two writes of a
    // concurrent post() can never see the flag without the snapshot.
    hasPending_.store(true, std::memory_order_release);
    return latestPosted_;
}

uint64_t ZoneLoadHandoff::latestPosted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latestPosted_;
}

bool ZoneLoadHandoff::take(ZoneLoadSnapshot& out, uint64_t& generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasPending_.load(std::memory_order_acquire)) return false;
    out = std::move(pending_);
    pending_ = ZoneLoadSnapshot{};  // a moved-from snapshot is valid but unspecified
    generation = latestPosted_;
    hasPending_.store(false, std::memory_order_release);
    return true;
}

void ZoneLoadHandoff::complete(Completion completion) {
    std::lock_guard<std::mutex> lock(mutex_);
    completion_ = std::move(completion);
    hasCompletion_.store(true, std::memory_order_release);
}

std::optional<ZoneLoadHandoff::Completion> ZoneLoadHandoff::takeCompletion() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasCompletion_.load(std::memory_order_acquire)) return std::nullopt;
    std::optional<Completion> out = std::move(completion_);
    completion_.reset();
    hasCompletion_.store(false, std::memory_order_release);
    return out;
}

ZoneLoadHandoff::Verdict ZoneLoadHandoff::classify(const Completion& completion,
                                                   uint64_t latestPosted,
                                                   bool gameAwaitingGraphics) {
    if (completion.generation != latestPosted) return Verdict::Stale;
    if (!gameAwaitingGraphics) return Verdict::Stale;
    return Verdict::Apply;
}

std::optional<ZoneLoadHandoff::Completion>
ZoneLoadHandoff::takeApplicableCompletion(bool gameAwaitingGraphics) {
    std::optional<Completion> completion = takeCompletion();
    if (!completion) return std::nullopt;
    const uint64_t latest = latestPosted();
    if (classify(*completion, latest, gameAwaitingGraphics) == Verdict::Stale) {
        LOG_INFO(MOD_MAIN,
                 "ZoneLoadHandoff: discarding superseded zone load #{} ('{}') -- latest request #{}, "
                 "game awaiting graphics={} (B917: a zone change landed during the load)",
                 completion->generation, completion->zoneName, latest, gameAwaitingGraphics);
        return std::nullopt;
    }
    return completion;
}

bool ZoneLoadHandoff::finishLoad(uint64_t generation, bool failed, std::string zoneName,
                                 std::shared_ptr<EQT::Graphics::BspTree> bspTree,
                                 std::string failureReason) {
    if (failed) {
        if (failureReason.empty()) {
            failureReason = "zone load #" + std::to_string(generation) + " ('" + zoneName + "') failed";
        }
        recordLoaderFailure(std::move(failureReason));
        return false;
    }
    Completion c;
    c.generation = generation;
    c.zoneName = std::move(zoneName);
    c.bspTree = std::move(bspTree);
    complete(std::move(c));
    return true;
}

void ZoneLoadHandoff::recordLoaderFailure(std::string reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    failureReason_ = std::move(reason);
    loaderFailed_.store(true, std::memory_order_release);
}

std::string ZoneLoadHandoff::loaderFailureReason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failureReason_;
}

}  // namespace eqt
