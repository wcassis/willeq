#pragma once

// B964 (GitLab #282): impostor billboards, the production port -- the pure
// decision logic, no GL and no Irrlicht, so tests/test_impostor_policy.cpp
// drives every rule without a device. The GL half is impostor_cache.h.
//
// Will's rulings (2026-09-30): "I agree with the proposed imposter
// implementation. proceed" (the design in refactor/B952/README.md, "Production
// port"); "Your recommended impostor budget sounds fine" -> 128^2 impostors and
// a 20 MiB `tex.impostor` row.
//
// Storage: impostors live in ATLAS PAGES of kPagePx^2 RGBA8, each holding
// kCellsPerPage cells of kCellPx^2 (B952 measured ~0.12 ms per billboard DRAW
// on the Pi, so billboards are batched one draw per page; the page is the
// texture storage, so the row counts pages). One scratch render target
// (kCellPx^2 RGBA8 + DEPTH16) is shared by every capture.
//
// The cap check is the blessed rule `(n + 1) * 4 s^2 + 6 s^2 <= budget` with
// the allocation unit a page: a page may be allocated only while
// `(pages + 1) * kPageBytes + kScratchBytes <= budget`. At 20 MiB that is 4
// pages = 256 unique cells (the per-cell formula alone would allow 318; the
// page rounding is the price of batching).

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace EQT {
namespace Graphics {
namespace impostor {

constexpr int kCellPx = 128;
constexpr int kPagePx = 1024;
constexpr int kCellsPerRow = kPagePx / kCellPx;
constexpr int kCellsPerPage = kCellsPerRow * kCellsPerRow;
constexpr size_t kCellBytes = static_cast<size_t>(kCellPx) * kCellPx * 4;
constexpr size_t kPageBytes = static_cast<size_t>(kPagePx) * kPagePx * 4;
constexpr size_t kScratchBytes = static_cast<size_t>(kCellPx) * kCellPx * (4 + 2);  // RGBA8 + DEPTH16

// The blessed per-impostor formula, kept for the record and the tests.
inline bool blessedUniqueFits(size_t uniqueNow, size_t budgetBytes) {
    return (uniqueNow + 1) * kCellBytes + kScratchBytes <= budgetBytes;
}

// How many pages the budget admits (the scratch is always paid first).
inline int maxPagesForBudget(size_t budgetBytes) {
    if (budgetBytes <= kScratchBytes) return 0;
    return static_cast<int>((budgetBytes - kScratchBytes) / kPageBytes);
}

// The row: what `tex.impostor` reports (logical bytes).
inline size_t rowBytes(int pages, bool scratchAllocated) {
    return static_cast<size_t>(pages) * kPageBytes + (scratchAllocated ? kScratchBytes : 0);
}

// Keys. A unique key is the spawn id; a shared key folds race, gender and the
// body texture variant (B952's shared mode) and carries a high tag bit.
constexpr uint64_t kSharedTag = 1ull << 40;
inline uint64_t uniqueKey(uint16_t spawnId) { return spawnId; }
inline uint64_t sharedKey(uint16_t raceId, uint8_t gender, uint8_t textureVariant) {
    return kSharedTag | (static_cast<uint64_t>(raceId) << 16) | (static_cast<uint64_t>(gender) << 8) |
           textureVariant;
}
inline bool isSharedKey(uint64_t key) { return (key & kSharedTag) != 0; }

struct CellRef {
    int page = -1;
    int cell = -1;
    bool valid() const { return page >= 0 && cell >= 0; }
};

// Cell bookkeeping: which key holds which cell, pages in use, LRU stamps.
// Pages are only ever appended (a freed cell is reused before a new page is
// asked for) and are all released together by clear().
class CellTable {
public:
    explicit CellTable(size_t budgetBytes = 0) : maxPages_(maxPagesForBudget(budgetBytes)) {}

    enum class Alloc { Existing, NewCell, NewPage, Full };

    // Find or allocate a cell for `key`. NewPage means the caller must create
    // page `out.page` before using the cell. Full = no free cell and the cap
    // forbids another page.
    Alloc acquire(uint64_t key, uint32_t frame, CellRef& out);
    // Free the key's cell (the cell goes on the free list; pages stay).
    void release(uint64_t key);
    // The least-recently-used UNIQUE key (by touch frame), or false if none.
    bool lruUnique(uint64_t& keyOut) const;
    void touch(uint64_t key, uint32_t frame);
    bool find(uint64_t key, CellRef& out) const;
    void clear();

    int pages() const { return pages_; }
    int maxPages() const { return maxPages_; }
    int maxCells() const { return maxPages_ * kCellsPerPage; }
    int cellsUsed() const { return static_cast<int>(cells_.size()); }
    int freeCells() const { return pages_ * kCellsPerPage - cellsUsed(); }
    int uniqueCount() const;
    int sharedCount() const { return cellsUsed() - uniqueCount(); }

private:
    struct Entry {
        CellRef ref;
        uint32_t lastUsed = 0;
    };
    int maxPages_ = 0;
    int pages_ = 0;
    std::unordered_map<uint64_t, Entry> cells_;
    std::vector<CellRef> freeList_;
};

// ---------------------------------------------------------------------------
// The tier policy. Per frame, over the visible candidates:
//   - beyond the tier distance an entity WANTS its impostor (the floor);
//   - inside it, the first `fullLimit` candidates (pinned first, then nearest)
//     stay full models, the rest want impostors;
//   - an entity within kBoundaryBand of the limit that was an impostor in the
//     last kHysteresisFrames frames stays one (B952's boundary hysteresis);
//   - pinned entities (target, group members) are always full models.
// The full-model limit follows the production FrameBudgetGovernor (the
// benchmark's private predictor is gone): every kAdjustEveryFrames frames it
// moves by at most kStep -- down when the governor is Red or its rolling
// average is over the target, up when it is Green and the average is under
// kSafetyMargin x the target, held otherwise. It never exceeds `maxFull`
// (the preset's maxVisibleEntities).
// ---------------------------------------------------------------------------

enum class Tier : uint8_t { Full = 0, Impostor = 1 };

// Mirrors FrameBudgetGovernor's BudgetState without including it (0/1/2).
enum class GovernorState : uint8_t { Green = 0, Yellow = 1, Red = 2 };

struct TierCandidate {
    uint16_t spawnId = 0;
    float distSq = 0.0f;
    bool pinned = false;
};

struct TierPolicyConfig {
    float tierDistance = 80.0f;
    int maxFull = 40;
    static constexpr int kStep = 2;
    static constexpr int kAdjustEveryFrames = 5;
    static constexpr int kHysteresisFrames = 10;
    static constexpr int kBoundaryBand = 3;
    static constexpr float kSafetyMargin = 0.85f;
};

class TierPolicy {
public:
    TierPolicy() = default;
    void configure(const TierPolicyConfig& c) { cfg_ = c; if (limit_ > cfg_.maxFull) limit_ = cfg_.maxFull; }
    void reset();

    // `cands` is sorted in place (pinned first, then by distance); `out` gets
    // one desired tier per candidate in that order.
    void plan(std::vector<TierCandidate>& cands, GovernorState state, float avgFrameMs,
              float targetFrameMs, std::vector<Tier>& out);

    int fullLimit() const { return limit_ < 0 ? cfg_.maxFull : limit_; }
    const TierPolicyConfig& config() const { return cfg_; }

private:
    void adjustLimit(GovernorState state, float avgFrameMs, float targetFrameMs);
    TierPolicyConfig cfg_;
    int limit_ = -1;
    int framesSinceAdjust_ = 0;
    std::unordered_map<uint16_t, int> recentImpostor_;  // spawn -> frames left
};

// ---------------------------------------------------------------------------
// Refresh by view angle. The capture looks along the horizontal
// camera->entity direction; what the viewer sees of the model is that
// direction relative to the model's own heading. When it has turned more than
// kViewAngleRefreshDeg since the capture (the entity turned, or the camera
// walked round it), the impostor is re-captured. 30 degrees = 12 views per
// turn; chosen, not measured (refactor/B964/README.md, "Not verified").
// ---------------------------------------------------------------------------
constexpr float kViewAngleRefreshDeg = 30.0f;

// Relative view angle in degrees [0, 360): direction from the camera to the
// entity (horizontal, any consistent axes) minus the entity's heading.
float relativeViewAngleDeg(float camX, float camZ, float entX, float entZ, float entityYawDeg);
// Smallest absolute difference of two angles in degrees, in [0, 180].
float angleDeltaDeg(float a, float b);
inline bool needsViewRefresh(float capturedDeg, float currentDeg) {
    return angleDeltaDeg(capturedDeg, currentDeg) > kViewAngleRefreshDeg;
}

// ---------------------------------------------------------------------------
// Scheduling: creation and refresh are units run before beginScene(), a few
// per frame, bounded both by count and by the time already spent this frame
// on them. The time cap is a fraction of the governor's frame budget, so no
// frame carries more than a quarter (Yellow/Red) or half (Green) of a frame
// budget of capture work -- B952 measured 7 ms per capture on the Pi, so the
// Pi does 1-2 a frame. At least one unit always runs: an impostor is how the
// governor gets out of Red, so starving creation in Red would lock it there.
// ---------------------------------------------------------------------------
struct UnitBudget {
    int maxUnits = 1;
    float maxMs = 8.0f;
};
UnitBudget unitBudgetFor(GovernorState state, float targetFrameMs);

}  // namespace impostor
}  // namespace Graphics
}  // namespace EQT
