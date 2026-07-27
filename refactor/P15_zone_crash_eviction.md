# P15 — Fix the #zone-teleport crash, then run the eviction-mystery experiments it blocked

Approved by Will 2026-07-27 ("go ahead with all three").
Slot: wt/slot2, branch task/P15-zone-crash, cut from fable tip.
OWNS THE EQEMU SERVER (P14 owns the Pi; P16 needs neither).

## Motivation

P13b's cross-zone eviction experiment was blocked by a NEW production
crash: GM #zone teleport → `loadZoneGraphicsOnThread: snapshot has
empty zone name` (recorded in deferred.md). Fixing it (a) removes a
real zoning-robustness bug, (b) unblocks the last open question from
the P13 texture audit: what actually caused P10b's 71-eviction
scenario (controlled single-zone runs upload only ~16MB / 0 evictions).

## Design

1. Root-cause the crash: reproduce via GM #zone in the x86 client
   (sanctioned eqemu flow); trace how the zone-change snapshot gets an
   empty zone name in the teleport path vs the normal zone-line path
   (ZoneLoadSnapshot creation, game-thread → loading-thread handoff,
   D21b mutex handoff rules in CLAUDE.md). Fix minimally; the normal
   zone-line path must be provably untouched (it presumably works —
   verify, don't assume).
2. Verify: #zone teleports across 3+ zones in one session (befallen ↔
   qeynos2 ↔ one more), plus one normal zone-line crossing if
   achievable in-session; no crash, zones render.
3. THEN the bounded eviction experiments (ledger on, 2 MAX):
   a. multi-zone single-process session (befallen → qeynos2 → befallen
      → qeynos2): cumulative ledger + eviction trace — does cross-zone
      texture accumulation reproduce evictions?
   b. if (a) doesn't: replicate P10b's exact session shape (its Review
      documents the repro configs).
   Record verdict + traces; NO eviction fixes here beyond recording
   (if reproduced, the fix is its own unit).
4. Full standard bar; the P10b regression matrix spot (one qeynos2
   enable-at-load run) to confirm the zone-load changes didn't disturb
   the merge path.

## Verification
- Standard bar; sanctioned server flow, clean shutdown, configs +
  Testchar restored; ledgers → benchmarks/audits/p15_eviction/.

## Out of scope
- Eviction FIXES; the Pi; foliage/pop-in (next server tenants).

## Review
(executor fills in)
