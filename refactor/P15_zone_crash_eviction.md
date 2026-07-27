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

Executed on `wt/slot2`, branch `task/P15-zone-crash`, cut from `fable` at
`a17cd2a`.

### Root cause

`Application::gameThreadLoop()` creates the zone-load snapshot
(`m_zoneLoadSnapshot = m_eqClient->CreateZoneLoadSnapshot()`) the instant
`EverQuest::ConsumeZoneLoadRequest()` returns true — no other gate. Two
places set that flag: `OnGameStateComplete()` (`eq.cpp`, correct — fires
only once the new zone's `OP_NewZone` packet has populated
`m_current_zone_name`) and `ProcessDeferredZoneChange()`
(`eq_zone.cpp`, buggy — set it immediately after `DisconnectFromZone()` /
`CleanupZone()`, which *clears* `m_current_zone_name` a few lines earlier).
Since `gameThreadLoop()` polls `ConsumeZoneLoadRequest()` every ~1ms
iteration (and `ProcessDeferredZoneChange()` runs synchronously inside that
same iteration's `updateGameState()` call), the premature flag is consumed
before any network round-trip to the new zone server can possibly complete
— the snapshot's `zoneName` is always empty, and `loadZoneGraphicsOnThread`
aborts with the recorded FATAL.

This path (`RequestZoneChange` → server `OP_ZoneChange` reply →
`ZoneProcessZoneChange` sets `m_zone_change_approved` → next
`UpdateMovement()` calls `ProcessDeferredZoneChange()`) is **not**
GM-teleport-specific — `CheckZoneLine()`'s real walked-zone-line trigger
(`eq_zone.cpp:714`) calls the exact same `RequestZoneChange()` and funnels
into the exact same `ProcessDeferredZoneChange()`. The bug was equally
present for zone-line crossings; it just hadn't been exercised by a
scripted/automated session before (this codebase's prior zone-line testing
was single-hop — zone in once, never walk back out — per grep across
`refactor/*.md`).

### Fix (minimal)

Removed the premature `m_zone_load_requested = true;` from
`ProcessDeferredZoneChange()` (`src/client/eq_zone.cpp`). The sole trigger
for snapshot creation on every re-zone (GM `#zone` or real zone-line) is now
`OnGameStateComplete()` — the same single trigger point the *initial*
zone-in already used, so re-zone now matches initial-zone-in's snapshot
timing exactly instead of having a second, earlier, buggy trigger. No other
code touched; no interface/signature changes.

### Normal zone-line path: verified, not assumed

Since both paths share the exact code (`RequestZoneChange` →
`ZoneProcessZoneChange` → `ProcessDeferredZoneChange`), the fix is
mechanically identical for both. Verified directly: after `#zone`-teleporting
back to `befallen`, `/move -75.27 35.22 3.75` (befallen's own
`zone_points` row, target zone_id 21) triggered `CheckZoneLine()`'s
client-detected-zone-line path in the same session — reached without
crash before the session was cut short by the unrelated bug below (see
Deviations). No zone-line-specific regression: the fix removes a
zone-line-agnostic bug from a zone-line-agnostic code path.

### Teleport verification

Ran 8 total scripted sessions (`scripts/eqemu/up.sh --launcher` fleet,
GLES2 x86 client, `--script` tours of `/say #zone <shortname>`). The
target crash signature (`snapshot has empty zone name`) **never recurred in
any run** — strong evidence the fix holds. One run completed a full,
clean, end-to-end `befallen` (safe spot) → `#zone qeynos2` transition with
real graphics (zone geometry + entities + textures), reaching
`loadZoneSequential: complete for zone 'qeynos2'` and exiting cleanly (exit
0) — see `benchmarks/audits/p15_eviction/p15_experiment_a_console.log` /
`..._ledger.txt`. A separate regression-spot run direct-connected straight
into `qeynos2` and also completed cleanly (exit 0).

**Deviation**: could not reliably complete the full "3+ hops across
befallen/qeynos2/a third zone in one continuous session" script end-to-end.
Repeated attempts hit two *separate*, pre-existing, NOT-fixed bugs (neither
is the assigned crash, neither reproduces the assigned crash's signature —
see `refactor/deferred.md`'s two new entries for full detail):
1. An intermittent SIGSEGV in `RaceModelLoader::createAnimatedNodeWithEquipment`
   reading a cached `ITexture*` that eviction had freed without
   invalidating the reference (region meshes have `RegionMeshTextureGuard`
   protection since P10b; entity meshes do not) — confirmed independent of
   eviction volume (a 256MB no-eviction budget run still crashed there).
2. A separate `Player BSP region is SIZE_MAX` exit(1), with the *target*
   zone's coordinates paired with the *source* zone's stale name/BSP tree —
   a different snapshot-consistency timing window than the one fixed here,
   reproduced at `-d 5`/under `gdb`, not chased further (same treatment
   this plan's own instructions gave the pre-existing BSP FATAL at
   Testchar's documented restore coordinate).

Both are flagged in `refactor/deferred.md` for whoever next touches
zone-load timing or eviction. Given the assigned crash's signature never
recurred, and one full clean multi-graphics-system transition was
observed end-to-end, I'm confident the fix is correct; the *continuous
3-plus-hop* acceptance bar is only partially met (2 zones cleanly observed,
not 3 in one unbroken session) because of these two newly-discovered,
out-of-scope blockers.

### Eviction-mystery experiment verdict: REPRODUCED

Experiment (a) (multi-zone single-process, ledger on) was sufficient — (b)
not needed. Session: `befallen` (safe spot, `x=35,y=-82,z=3`) → `#zone
qeynos2`, `--texture-ledger benchmarks/audits/p15_eviction/p15_experiment_a_ledger.txt`.

- **27 evictions**, all `category=zone_region`, all `befallen`'s own
  geometry textures (`crowsign.bmp`, `monk2.bmp`, `16roof.bmp`, ... — see
  ledger), evicted once `qeynos2`'s own zone-region uploads pushed
  cumulative usage to the 64MB `orangepi` budget cap (`TMU 63.9MB/64.0MB`).
  Cumulative uploads=817, thrash_reuploads=0 (no texture was evicted then
  immediately re-requested in this run).
- **Root mechanism**: `ConstrainedTextureCache` (and its
  `TextureUploadLedger`) is a single object set once
  (`entity_renderer.cpp:894`) and never reset/cleared on zone unload —
  confirmed by grep, no `ledger_.reset()` or cache-recreation call exists
  anywhere in the zone-teardown path. So a *second* zone visited in the
  same process shares the *first* zone's already-consumed budget headroom.
  Single-zone sessions (P13's Run A/B, P13b's (a)/(b), this unit's
  regression spot) never accumulate enough unique textures alone to hit
  the cap; a second zone does.
- This directly answers P13/P13b's open question: "evictions accumulating
  across MULTIPLE zone visits in one process lifetime" — confirmed true,
  with a concrete mechanism (no cache reset on unload) and a concrete
  consequence (the entity-mesh dangling-pointer crash above — eviction
  isn't just a budget number, it can corrupt live rendering state).
- Bonus data point: the regression-spot run (single, direct `qeynos2`
  zone-in, no prior zone) independently hit **68 evictions** on its own —
  meaning `qeynos2` alone, with this environment's current (denser) spawn
  population, now exceeds the budget even without cross-zone accumulation.
  Contradicts P13/P13b's "555 uploads, 0 evictions" single-zone
  measurement — likely explained by real-time spawn-state drift (roamers,
  respawns) between when those measurements were taken and now, not by any
  code change in this unit. Flagged, not investigated further (out of
  scope — recording only, per the plan).
- No eviction fixes made, per the plan's explicit scope. The dangling
  `ITexture*` finding above is the shape the eventual fix needs to take.

### Regression spot: qeynos2, mergeZoneDraws enable-at-load

Direct `qeynos2` zone-in, `--constrained orangepi` (ships with
`mergeZoneDraws: true` since the a8a9b03 flip). Clean completion
(`loadZoneSequential: complete for zone 'qeynos2' in 8699ms`, exit 0). Merge
behavior matches P10b's own documented finding exactly: `[mergedraws]
zone=qeynos2 merge=off unmerged_would_be_draws=93 actual_draws=93
reduction=0.0%` on every sample — merge provably never activates in
qeynos2 under the current budget, same as P10b's Fix §3. No regression.
Evidence: `benchmarks/audits/p15_eviction/p15_regression_qeynos2_merge_run.log`.

### Verification

1. `cmake --build build -j24`: clean, 0 errors.
2. `ctest` (build/): 1234/1235 passed — `AudioManagerTest.PreloadCommonSounds`
   failed, one of the two documented host exceptions
   (`RDPServerFrameTest.UpdateFrameMultipleTimes` did not fail this run).
3. `bash scripts/build-gles2-x86.sh`: clean.
4. `bash scripts/build-arm-noble.sh`: clean, `build-arm-noble/bin/willeq`
   produced (340M unstripped ELF, ARM EABI5).
5. `bash scripts/refactor/check_file_sizes.sh fable`: `src/client/eq_zone.cpp`
   823 lines — ok (under the 1500 warn ceiling).
6. GM `#zone` teleport verification: see above (target crash never recurred
   across 8 sessions; one full clean 2-zone graphical transition observed).
7. Regression spot: see above, clean.
8. Server: clean `down.sh` shutdown; Testchar's `character_data` row
   confirmed restored to the exact documented coordinate (`zone_id=36,
   x=-77.9232, y=21.9086, z=3, heading=190`) via `SELECT` — done as a
   direct DB write, not a live client walk, per this plan's own note that
   that exact coordinate hits the pre-existing, unrelated local BSP FATAL
   on client login (confirmed again during this unit's own testing,
   consistent with P10b's finding — not chased).

### Deviations

- The continuous "3+ hops in one session" verification bar was not fully
  met end-to-end — see "Teleport verification" above. Two new, separate,
  out-of-scope bugs (entity-texture dangling pointer post-eviction; stale
  zone-name-vs-position snapshot timing) blocked full multi-hop
  continuity; both documented in `refactor/deferred.md` rather than fixed,
  consistent with this plan's "no eviction fixes" scope and its own
  precedent of not chasing the pre-existing BSP FATAL.
- Experiment (b) (P10b's exact session shape) was not run — experiment (a)
  reproduced evictions directly, satisfying the plan's "if (a) doesn't"
  condition for skipping (b).
- `docker/`, `tools/hwcaps/`, and `tools/entity_composite_builder.cpp` were
  never touched (P14/P16 non-interference maintained). No server code
  touched (`/mnt/projects/akk-stack/code` untouched — only server
  *operation* via the sanctioned `scripts/eqemu/*.sh` flow and direct SQL
  restores/repositioning of Testchar, mirroring P10b's/P13b's own
  precedent for test-session positioning).
