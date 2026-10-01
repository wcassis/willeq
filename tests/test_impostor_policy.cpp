// B964 (GitLab #282): the impostor port's decision logic, device-free.
//
// Pins Will's blessed numbers (128^2, 20 MiB -> 4 atlas pages = 256 unique
// cells), the cell table's allocate / reuse / LRU-demotion rules, the tier
// policy (distance floor, governor-driven full-model limit, pinned entities,
// boundary hysteresis), the view-angle refresh and the per-frame unit budget,
// and the [memledger-owner] row format the rigs parse.

#include <gtest/gtest.h>

#include <vector>

#include "client/core/memory_owner_rows.h"
#include "client/graphics/impostor/impostor_policy.h"

using namespace EQT::Graphics::impostor;

namespace {
constexpr size_t kBlessedBudget = 20u * 1024u * 1024u;
}

TEST(ImpostorBudget, BlessedSizesAndTheCapCheck) {
    EXPECT_EQ(kCellPx, 128);
    EXPECT_EQ(kCellBytes, 65536u);              // 4 s^2
    EXPECT_EQ(kScratchBytes, 98304u);           // 6 s^2: RGBA8 + DEPTH16
    EXPECT_EQ(kCellsPerPage, 64);
    EXPECT_EQ(kPageBytes, 4u * 1024u * 1024u);
    // The blessed per-impostor rule (n + 1) * 4 s^2 + 6 s^2 <= B at 20 MiB:
    // 318 fit, the 319th does not.
    EXPECT_TRUE(blessedUniqueFits(317, kBlessedBudget));
    EXPECT_FALSE(blessedUniqueFits(318, kBlessedBudget));
    // With the page as the allocation unit: 4 pages + the scratch fit, 5 do not.
    EXPECT_EQ(maxPagesForBudget(kBlessedBudget), 4);
    EXPECT_LE(rowBytes(4, true), kBlessedBudget);
    EXPECT_GT(rowBytes(5, true), kBlessedBudget);
    EXPECT_EQ(rowBytes(4, true), 4u * kPageBytes + kScratchBytes);
    // B952's measurement scenario: 200 unique impostors fit.
    EXPECT_GE(maxPagesForBudget(kBlessedBudget) * kCellsPerPage, 200);
    EXPECT_EQ(maxPagesForBudget(0), 0);
    EXPECT_EQ(maxPagesForBudget(kScratchBytes + kPageBytes - 1), 0);
}

TEST(ImpostorKeys, UniqueAndSharedNeverCollide) {
    EXPECT_FALSE(isSharedKey(uniqueKey(65535)));
    const uint64_t a = sharedKey(1, 0, 0), b = sharedKey(1, 1, 0), c = sharedKey(1, 0, 2);
    EXPECT_TRUE(isSharedKey(a));
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, uniqueKey(1));
}

TEST(ImpostorCellTable, AllocatesPagesLazilyAndReusesFreedCells) {
    CellTable t(kBlessedBudget);
    CellRef r;
    EXPECT_EQ(t.acquire(uniqueKey(1), 1, r), CellTable::Alloc::NewPage);
    EXPECT_EQ(r.page, 0);
    EXPECT_EQ(r.cell, 0);
    EXPECT_EQ(t.acquire(uniqueKey(1), 2, r), CellTable::Alloc::Existing);
    EXPECT_EQ(t.acquire(uniqueKey(2), 2, r), CellTable::Alloc::NewCell);
    EXPECT_EQ(r.cell, 1);
    t.release(uniqueKey(1));
    EXPECT_EQ(t.acquire(uniqueKey(3), 3, r), CellTable::Alloc::NewCell);
    EXPECT_EQ(r.page, 0);
    EXPECT_EQ(r.cell, 0);  // the freed cell, not a new one
    EXPECT_EQ(t.pages(), 1);
}

TEST(ImpostorCellTable, CapRefusesThe257thCell) {
    CellTable t(kBlessedBudget);
    CellRef r;
    for (uint16_t i = 0; i < 256; ++i) ASSERT_NE(t.acquire(uniqueKey(i), i, r), CellTable::Alloc::Full) << i;
    EXPECT_EQ(t.pages(), 4);
    EXPECT_EQ(t.cellsUsed(), 256);
    EXPECT_EQ(t.acquire(uniqueKey(999), 999, r), CellTable::Alloc::Full);
    EXPECT_EQ(t.acquire(sharedKey(1, 0, 0), 999, r), CellTable::Alloc::Full);
}

TEST(ImpostorCellTable, LruUniqueSkipsSharedKeysAndPicksTheOldest) {
    CellTable t(kBlessedBudget);
    CellRef r;
    t.acquire(sharedKey(1, 0, 0), 1, r);
    t.acquire(uniqueKey(10), 5, r);
    t.acquire(uniqueKey(11), 3, r);
    t.acquire(uniqueKey(12), 9, r);
    uint64_t k = 0;
    ASSERT_TRUE(t.lruUnique(k));
    EXPECT_EQ(k, uniqueKey(11));
    t.touch(uniqueKey(11), 20);
    ASSERT_TRUE(t.lruUnique(k));
    EXPECT_EQ(k, uniqueKey(10));
    EXPECT_EQ(t.uniqueCount(), 3);
    EXPECT_EQ(t.sharedCount(), 1);
}

namespace {
std::vector<TierCandidate> line(int n, float spacing, float start = 5.0f) {
    std::vector<TierCandidate> v;
    for (int i = 0; i < n; ++i) {
        TierCandidate c;
        c.spawnId = static_cast<uint16_t>(100 + i);
        const float d = start + spacing * i;
        c.distSq = d * d;
        v.push_back(c);
    }
    return v;
}
int count(const std::vector<Tier>& t, Tier x) {
    int n = 0;
    for (Tier y : t) n += (y == x);
    return n;
}
}  // namespace

TEST(ImpostorTierPolicy, BeyondTheTierDistanceIsAlwaysAnImpostor) {
    TierPolicy p;
    TierPolicyConfig c;
    c.tierDistance = 80.0f;
    c.maxFull = 40;
    p.configure(c);
    auto cands = line(10, 20.0f);  // 5, 25, ..., 185
    std::vector<Tier> out;
    p.plan(cands, GovernorState::Green, 10.0f, 33.3f, out);
    for (size_t i = 0; i < cands.size(); ++i) {
        const bool beyond = cands[i].distSq > 80.0f * 80.0f;
        EXPECT_EQ(out[i], beyond ? Tier::Impostor : Tier::Full) << i;
    }
}

TEST(ImpostorTierPolicy, RedNarrowsGreenWidensTwoPerStep) {
    TierPolicy p;
    TierPolicyConfig c;
    c.tierDistance = 1000.0f;
    c.maxFull = 40;
    p.configure(c);
    auto cands = line(60, 1.0f);
    std::vector<Tier> out;
    // Red for 5 frames: one adjustment step of -2.
    for (int f = 0; f < TierPolicyConfig::kAdjustEveryFrames; ++f)
        p.plan(cands, GovernorState::Red, 50.0f, 33.3f, out);
    EXPECT_EQ(p.fullLimit(), 38);
    for (int f = 0; f < 10 * TierPolicyConfig::kAdjustEveryFrames; ++f)
        p.plan(cands, GovernorState::Red, 50.0f, 33.3f, out);
    EXPECT_EQ(p.fullLimit(), 18);
    EXPECT_LE(count(out, Tier::Full), 18);
    // Yellow, within budget: held.
    for (int f = 0; f < 4 * TierPolicyConfig::kAdjustEveryFrames; ++f)
        p.plan(cands, GovernorState::Yellow, 30.0f, 33.3f, out);
    EXPECT_EQ(p.fullLimit(), 18);
    // Green with room under 0.85 x budget: widens, never past maxFull.
    for (int f = 0; f < 100 * TierPolicyConfig::kAdjustEveryFrames; ++f)
        p.plan(cands, GovernorState::Green, 10.0f, 33.3f, out);
    EXPECT_EQ(p.fullLimit(), 40);
    // Green but above the 0.85 margin: held.
    for (int f = 0; f < TierPolicyConfig::kAdjustEveryFrames; ++f)
        p.plan(cands, GovernorState::Red, 50.0f, 33.3f, out);
    const int held = p.fullLimit();
    for (int f = 0; f < 4 * TierPolicyConfig::kAdjustEveryFrames; ++f)
        p.plan(cands, GovernorState::Green, 30.0f, 33.3f, out);
    EXPECT_EQ(p.fullLimit(), held);
}

TEST(ImpostorTierPolicy, PinnedStayFullAndComeFirst) {
    TierPolicy p;
    TierPolicyConfig c;
    c.tierDistance = 80.0f;
    c.maxFull = 2;
    p.configure(c);
    auto cands = line(6, 50.0f);       // 5, 55, 105 ...
    cands[5].pinned = true;            // the target, 255 units away
    std::vector<Tier> out;
    p.plan(cands, GovernorState::Green, 10.0f, 33.3f, out);
    ASSERT_TRUE(cands[0].pinned);      // sorted to the front
    EXPECT_EQ(out[0], Tier::Full);     // beyond the tier, still a model
    EXPECT_EQ(out[1], Tier::Full);     // nearest, inside the limit
    EXPECT_EQ(count(out, Tier::Full), 2);
}

TEST(ImpostorTierPolicy, BoundaryHysteresisKeepsARecentImpostor) {
    TierPolicy p;
    TierPolicyConfig c;
    c.tierDistance = 1000.0f;
    c.maxFull = 10;
    p.configure(c);
    auto cands = line(12, 1.0f);
    std::vector<Tier> out;
    // Force a narrower limit via Red so entity 100+9 becomes an impostor.
    for (int f = 0; f < TierPolicyConfig::kAdjustEveryFrames; ++f)
        p.plan(cands, GovernorState::Red, 50.0f, 33.3f, out);
    ASSERT_EQ(p.fullLimit(), 8);
    // Entity index 8 (spawn 108) is an impostor now.
    EXPECT_EQ(out[8], Tier::Impostor);
    // One Green step back to 10: 108 sits in the boundary band and was an
    // impostor within kHysteresisFrames, so it stays one.
    for (int f = 0; f < TierPolicyConfig::kAdjustEveryFrames; ++f)
        p.plan(cands, GovernorState::Green, 5.0f, 33.3f, out);
    ASSERT_EQ(p.fullLimit(), 10);
    EXPECT_EQ(out[8], Tier::Impostor);
    // Well past the hysteresis it recovers.
    for (int f = 0; f < 3 * TierPolicyConfig::kHysteresisFrames; ++f)
        p.plan(cands, GovernorState::Yellow, 30.0f, 33.3f, out);
    EXPECT_EQ(out[0], Tier::Full);
}

TEST(ImpostorViewAngle, RefreshPastThirtyDegreesOnly) {
    EXPECT_FLOAT_EQ(angleDeltaDeg(10.0f, 350.0f), 20.0f);
    EXPECT_FLOAT_EQ(angleDeltaDeg(0.0f, 180.0f), 180.0f);
    EXPECT_FALSE(needsViewRefresh(0.0f, 29.0f));
    EXPECT_TRUE(needsViewRefresh(0.0f, 31.0f));
    EXPECT_TRUE(needsViewRefresh(350.0f, 25.0f));
    // The camera walking a quarter turn round a still entity moves the angle 90.
    const float a = relativeViewAngleDeg(0.0f, -10.0f, 0.0f, 0.0f, 0.0f);
    const float b = relativeViewAngleDeg(-10.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(angleDeltaDeg(a, b), 90.0f, 1e-3f);
    // The entity turning in place moves it by the turn.
    const float c = relativeViewAngleDeg(0.0f, -10.0f, 0.0f, 0.0f, 45.0f);
    EXPECT_NEAR(angleDeltaDeg(a, c), 45.0f, 1e-3f);
}

TEST(ImpostorUnitBudget, AFewPerFrameNeverZero) {
    const UnitBudget g = unitBudgetFor(GovernorState::Green, 33.3f);
    const UnitBudget r = unitBudgetFor(GovernorState::Red, 33.3f);
    EXPECT_GE(g.maxUnits, r.maxUnits);
    EXPECT_GE(r.maxUnits, 1);  // creation is how the governor leaves Red
    EXPECT_LE(g.maxMs, 33.3f);
    EXPECT_LE(r.maxMs, 33.3f * 0.25f + 1e-3f);
}

TEST(ImpostorOwnerRow, LedgerLineShape) {
    eqt::memory::setOwnerRow("tex.impostor", "textures_driver", 16875520, "pages=4 cells=200");
    const std::string lines = eqt::memory::ownerRowsToLogLines("steady_60s");
    EXPECT_NE(lines.find("[memledger-owner] point=steady_60s category=textures_driver owner=tex.impostor "
                         "live=16875520 pages=4 cells=200"),
              std::string::npos)
        << lines;
    eqt::memory::clearOwnerRow("tex.impostor");
    EXPECT_EQ(eqt::memory::ownerRowsToLogLines("x").find("tex.impostor"), std::string::npos);
}
