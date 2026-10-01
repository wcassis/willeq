// B964: impostor decision logic -- see include/client/graphics/impostor/impostor_policy.h.

#include "client/graphics/impostor/impostor_policy.h"

#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {
namespace impostor {

// ---------------------------------------------------------------------------
// CellTable
// ---------------------------------------------------------------------------

CellTable::Alloc CellTable::acquire(uint64_t key, uint32_t frame, CellRef& out) {
    auto it = cells_.find(key);
    if (it != cells_.end()) {
        it->second.lastUsed = frame;
        out = it->second.ref;
        return Alloc::Existing;
    }
    if (!freeList_.empty()) {
        out = freeList_.back();
        freeList_.pop_back();
        cells_[key] = Entry{out, frame};
        return Alloc::NewCell;
    }
    if (pages_ >= maxPages_) {
        out = CellRef{};
        return Alloc::Full;
    }
    const int page = pages_++;
    // Cells of the new page, highest first so pop_back hands out cell 1, 2, ...
    for (int c = kCellsPerPage - 1; c >= 1; --c) freeList_.push_back(CellRef{page, c});
    out = CellRef{page, 0};
    cells_[key] = Entry{out, frame};
    return Alloc::NewPage;
}

void CellTable::release(uint64_t key) {
    auto it = cells_.find(key);
    if (it == cells_.end()) return;
    freeList_.push_back(it->second.ref);
    cells_.erase(it);
}

bool CellTable::lruUnique(uint64_t& keyOut) const {
    bool found = false;
    uint32_t best = 0;
    for (const auto& [k, e] : cells_) {
        if (isSharedKey(k)) continue;
        // Ties broken by key so the choice is deterministic.
        if (!found || e.lastUsed < best || (e.lastUsed == best && k < keyOut)) {
            best = e.lastUsed;
            keyOut = k;
            found = true;
        }
    }
    return found;
}

void CellTable::touch(uint64_t key, uint32_t frame) {
    auto it = cells_.find(key);
    if (it != cells_.end()) it->second.lastUsed = frame;
}

bool CellTable::find(uint64_t key, CellRef& out) const {
    auto it = cells_.find(key);
    if (it == cells_.end()) return false;
    out = it->second.ref;
    return true;
}

void CellTable::clear() {
    cells_.clear();
    freeList_.clear();
    pages_ = 0;
}

int CellTable::uniqueCount() const {
    int n = 0;
    for (const auto& [k, e] : cells_)
        if (!isSharedKey(k)) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// TierPolicy
// ---------------------------------------------------------------------------

void TierPolicy::reset() {
    limit_ = -1;
    framesSinceAdjust_ = 0;
    recentImpostor_.clear();
}

void TierPolicy::adjustLimit(GovernorState state, float avgFrameMs, float targetFrameMs) {
    if (limit_ < 0) limit_ = cfg_.maxFull;
    if (++framesSinceAdjust_ < TierPolicyConfig::kAdjustEveryFrames) return;
    framesSinceAdjust_ = 0;
    const bool over = state == GovernorState::Red || avgFrameMs > targetFrameMs;
    const bool roomy = state == GovernorState::Green &&
                       avgFrameMs < targetFrameMs * TierPolicyConfig::kSafetyMargin;
    if (over) limit_ -= TierPolicyConfig::kStep;
    else if (roomy) limit_ += TierPolicyConfig::kStep;
    limit_ = std::max(0, std::min(limit_, cfg_.maxFull));
}

void TierPolicy::plan(std::vector<TierCandidate>& cands, GovernorState state, float avgFrameMs,
                      float targetFrameMs, std::vector<Tier>& out) {
    adjustLimit(state, avgFrameMs, targetFrameMs);

    // Hysteresis timers count down once per planned frame.
    for (auto it = recentImpostor_.begin(); it != recentImpostor_.end();) {
        if (--it->second <= 0) it = recentImpostor_.erase(it);
        else ++it;
    }

    std::sort(cands.begin(), cands.end(), [](const TierCandidate& a, const TierCandidate& b) {
        if (a.pinned != b.pinned) return a.pinned;
        if (a.distSq != b.distSq) return a.distSq < b.distSq;
        return a.spawnId < b.spawnId;
    });

    const float tierSq = cfg_.tierDistance * cfg_.tierDistance;
    const int limit = fullLimit();
    out.assign(cands.size(), Tier::Full);
    int fullIdx = 0;
    for (size_t i = 0; i < cands.size(); ++i) {
        const TierCandidate& c = cands[i];
        if (c.pinned) {
            ++fullIdx;
            continue;
        }
        const bool beyondTier = cfg_.tierDistance > 0.0f && c.distSq > tierSq;
        const bool overLimit = fullIdx >= limit;
        const bool nearBoundary = fullIdx >= limit - TierPolicyConfig::kBoundaryBand;
        const bool sticky = nearBoundary && recentImpostor_.count(c.spawnId) != 0;
        if (beyondTier || overLimit || sticky) {
            out[i] = Tier::Impostor;
            recentImpostor_[c.spawnId] = TierPolicyConfig::kHysteresisFrames;
        } else {
            ++fullIdx;
        }
    }
}

// ---------------------------------------------------------------------------
// View angle and scheduling
// ---------------------------------------------------------------------------

float relativeViewAngleDeg(float camX, float camZ, float entX, float entZ, float entityYawDeg) {
    const float dx = entX - camX, dz = entZ - camZ;
    float dirDeg = (dx == 0.0f && dz == 0.0f) ? 0.0f : std::atan2(dx, dz) * 57.29577951f;
    float rel = std::fmod(dirDeg - entityYawDeg, 360.0f);
    if (rel < 0.0f) rel += 360.0f;
    return rel;
}

float angleDeltaDeg(float a, float b) {
    float d = std::fmod(std::fabs(a - b), 360.0f);
    return d > 180.0f ? 360.0f - d : d;
}

UnitBudget unitBudgetFor(GovernorState state, float targetFrameMs) {
    UnitBudget b;
    switch (state) {
        case GovernorState::Green:
            b.maxUnits = 3;
            b.maxMs = 0.5f * targetFrameMs;
            break;
        case GovernorState::Yellow:
        case GovernorState::Red:
        default:
            b.maxUnits = 2;
            b.maxMs = 0.25f * targetFrameMs;
            break;
    }
    return b;
}

}  // namespace impostor
}  // namespace Graphics
}  // namespace EQT
