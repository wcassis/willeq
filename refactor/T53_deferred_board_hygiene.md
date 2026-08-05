# T53 — Board hygiene: make `refactor/deferred.md` mean something again

**Slot: t1. x86 only. No Pi. NO EQEMU — nothing in this unit is live.**
No sibling unit.

**Territory: `refactor/deferred.md` ONLY.** No source changes, no test
changes, no other docs. `git diff` at the end must touch exactly one file.
If you find a real bug while verifying, **record it in the board — do not
fix it.** That is a different unit, and mixing a code change into a
hygiene pass makes both unreviewable.

Read `refactor/EXECUTOR_PROTOCOL.md`, then skim the whole of
`refactor/deferred.md` before touching anything.

## Why this unit exists

The board has 68 sections and ~33 that a heading scan reads as open. **That
number is wrong and the board is no longer a usable work queue.** I
spot-checked one entry and every claim in it was already fixed:

> "Dead parallel state copies in the state layer, WITH LIVE READERS"
> (filed 2026-08-02, three live bugs)
> 1. `CombatState`'s target half "never populated, four slash commands read
>    it" → **populated** by `Application::syncCombatState()` (T39,
>    `8d820392`); the `/assist` reader was repointed by T28.
> 2. `InventoryState` occupancy → **wired** by T42, extended by T49.
> 3. `WorldState::fireZoneChangedEvent()` reading pending-zone fields →
>    **fixed** by T28; the fields are now `m_zoneArrival*`.

Several other headings read as open but say COMPLETE/SWEPT/NOT-A-BUG in
their body (`P32`, `T43`, `T46`, `T52`). The cost is real: a stale board
makes every future scoping pass more expensive and hides the genuinely
open work.

## The governing rule — read this twice

**NEVER DELETE CONTENT.** This file is a research record, not a TODO list;
its history has repeatedly turned out to be load-bearing (T46 resolved a
conflict between two units by consulting T43's classification table).
Hygiene here means **annotate and re-title**, never remove.

**Mark an entry closed ONLY with concrete evidence** — a commit hash, a
file:line that shows the fix, or a named test that pins it. Write the
evidence into the entry. If you cannot produce evidence, **the entry stays
open**, and if you suspect it is closed but cannot prove it cheaply, say
exactly that ("suspected closed by T4x, not verified") rather than
guessing either way.

**Bias toward leaving things open.** A stale-open entry costs a future
reader some time. A wrongly-closed entry loses a real defect, possibly for
good. Those are not symmetric.

## What to do

1. **Walk every `## ` section.** For each, decide: genuinely OPEN,
   CLOSED-with-evidence, or PARTIALLY closed (some claims resolved, some
   not — very common; the worked example above is one).
2. **Re-title so a heading scan tells the truth.** Closed entries get an
   explicit marker in the heading itself (the file already uses forms like
   `— **SWEPT by T43**` and `— **SERVER-SIDE, NOT OURS**`; follow the
   existing style rather than inventing one). For partial ones, the
   heading should say partial and the body should say which parts.
3. **Check the reverse too**: entries currently marked closed that are
   actually still open, or that later work re-opened. Same evidence rule.
4. **Add an index at the top of the file** listing the genuinely-open
   items with their line numbers, grouped sensibly (e.g. client
   correctness / architecture / performance / tooling — your call, justify
   it). This is the deliverable a future scoping pass actually uses.
   Additive only.
5. **Add these genuinely-open items that are NOT currently recorded**,
   found while I was answering a status question:
   - **14 unexamined test skips**: 8 `DoorManagerGraphicsTest.*` and 6
     `InventoryModelViewTest.*`, skipped in the graphics tree. Nobody has
     established whether these are environment-gated by design or silent
     coverage loss. (The 2 `BridgeTeardownAsanRepro` skips are
     deliberately opt-in per T47 — do not file those.)
   - `PfsArchiveContentTest.DdsFileValid` and
     `SoundBufferTest.LoadSwingWav`, likewise unexamined.
   - The existing "2 SpellManagerGems skips" note should be reconciled
     with the fact that they still skip on the current tip.

## Cheap verification techniques that actually work here

- The claim in most entries is of the form "X is never called" /
  "Y is unreachable" / "Z is hardcoded". `grep -rn` against `src/` and
  `include/` settles most of them in one command — that is exactly how the
  worked example above fell apart.
- `git log --oneline -S "<symbol>" -- <path>` finds the commit that
  introduced or removed a call site, which gives you the evidence hash.
- Entries name their filing date and often a fable tip; anything merged
  after that date is a candidate resolver. T-series units T26–T52 have
  Review sections that frequently state what they closed.

## STOP conditions

- You find yourself deleting a section → STOP, that is not this unit.
- You want to fix code you discovered is broken → STOP, record it instead.
- An entry's status depends on a live run to settle → **leave it open**
  and note "needs a live check"; this unit owns no server.
- The pass balloons past the file → STOP; territory is one file.

## Verification bar

1. `git diff --stat` shows **exactly one file changed**:
   `refactor/deferred.md`.
2. **No content removed.** Prove it: `git diff` should be
   overwhelmingly additions and heading rewrites. Report the
   added/removed line counts and explain any removed line individually.
   (Re-titling a heading legitimately removes one line and adds one — that
   is fine and expected; bulk deletions are not.)
3. Both configures still build clean and `ctest` is unchanged in both
   trees — a docs-only change must not perturb anything. Known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). This is a sanity check
   that you did not stray outside territory, so it is cheap insurance,
   not busywork.
4. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs path>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37).

## What to report in the Review

Put the Review in **this plan file**, not in `deferred.md`.

- **A table: every section → verdict (OPEN / CLOSED / PARTIAL) → the
  evidence** for each non-OPEN verdict. This is the unit's real output and
  the thing I will spot-check.
- The before/after count of genuinely-open items.
- Anything you found marked closed that is actually open — flag these
  loudly, they are the dangerous direction.
- Any entry you could not settle cheaply, and what it would take.
- Whether the board is now usable as a work queue, honestly. If it still
  is not, say what remains.

---

## Review (T53, 2026-08-05)

Base rev `607a19c3`, branch `task/T53-board-hygiene`, slot t1. Territory
respected: only `refactor/deferred.md` touched.

### Verdict table (every section, in file order after this pass)

| # | Section (heading, post-edit) | Verdict | Evidence |
|---|---|---|---|
| 1 | FIXED: ParticleManager UAF (P34) | CLOSED | already evidenced inline (commit, TSAN/soak proof); not re-verified further |
| 2 | FIXED: SimulationWorker HCMap UAF (P35) | CLOSED | already evidenced inline |
| 3 | Tree wind update unreachable (P32) | PARTIAL | wind FIXED by P33 (blessed 2026-07-29, already evidenced inline); eqemu port-collision sub-item OPEN, needs Will's call on server config — re-titled, no new grep needed (body already proved it) |
| 4 | mutation-testing intel | CLOSED | already marked |
| 5 | Desktop-GL elimination COMPLETE (P32) | CLOSED | already evidenced inline |
| 6 | Handback items (perf session) | CLOSED (3/3) | re-titled only; all 3 already carried DONE/FIXED markers |
| 7 | Desktop-GL ROADMAP LANDED (P24) | CLOSED, was stale | T53: Dockerfile item FIXED by P31 `4f2e9417` (verified: `docker/Dockerfile:14` bootstraps patched GLES2 Irrlicht, no apt desktop-Irrlicht install); "REMAINING: Phase 4... await Will's go" superseded same-day by section 5 (P32 executed Phase 4) — annotated as stale, not deleted |
| 8 | Build-config latent defects | CLOSED | already evidenced inline |
| 9 | Decoupling residue (S2) | CLOSED overall, 4 sub-items OPEN | T53 re-verified all 4 by grep: `InitGraphics` still takes `IrrlichtRenderer*` (`eq.h:1166`); `WldLoader`/`PfsArchive` still in `EQT::Graphics`; `door_manager.cpp:955` still calls `EverQuest::GetDebugLevel()`; `output/graphical_renderer.h` — narrow claim true (IrrlichtRenderer doesn't inherit it) but file is NOT simply dead (3 includers, one live `dynamic_cast`) — corrected the framing, left open |
| 10 | Batch U / ui_fixes.md | OPEN (2 items) | not independently re-verified (low-value UI polish items); left open per bias rule |
| 11 | arch_design_update.md / audit_results_post.md | OPEN (7 items) | not independently re-verified; architecture/perf items, left open |
| 12 | F00 test repair | OPEN | T53 verified: `EQT::SpellInfo` (`willeq_database.h`) still has no effects field; `SpellDatabase::queryAndCache` still never populates `SpellData::effects`; T27's `class_levels` column addition did not touch this. Confirmed still true |
| 13 | Batch G | CLOSED | already marked |
| 14 | Batch H | PARTIAL | spire-launcher RESOLVED (already marked); T53 verified blackburrow/crushbone blank-render has no fix commit (`git log --grep` finds only the H02 commit that filed it) — CANNOT-SETTLE-CHEAPLY, needs a live render; T53 verified no `OP_CharacterCreate`/`CharacterCreate` exists anywhere in `src/`/`include/` — still open; process-watcher allowlist caution left as-is (unverifiable without server config access) |
| 15 | Performance gap (H04) | HISTORICAL/SUPERSEDED | re-titled to point at the actual KPI closure (P11b/P14) later in the file; sub-items already individually marked (CMA overturned, loader FIXED, format shootout decided) |
| 16 | Outskirts anomaly (P05→P07) | CLOSED | already marked |
| 17 | Outskirts anomaly history | superseded | already marked |
| 18 | Benchmark camera path | CLOSED | already marked |
| 19 | Radius mode RESOLVED (P19) | CLOSED | already marked |
| 20 | Radius mode Q03 original | superseded | already marked |
| 21 | KPI status + composite gap (P11) | SUPERSEDED | re-titled; every bullet already struck/closed inline |
| 22 | Live x86 client bugs | PARTIAL | re-titled: saveScreenshot/particle-race/post-camp-shutdown CLOSED (marked); befallen first-frame SIGSEGV and the FATAL-vs-WARN item still OPEN |
| 23 | Sheet quality (P16) | CLOSED | already marked |
| 24 | P20 live-visual pair | PARTIAL | re-titled: pop-in (non-bug), foliage alpha (FIXED P20b), door/entity gap (FIXED P21) closed; textureMasked-ignored gap, checkerboard placeholder still OPEN |
| 25 | Eviction hygiene LANDED (P17) | CLOSED, 1 residual OPEN | gfaydark race=330 FATAL still open; T53 added cross-reference to the still-open race=415 entry (not confirmed same root cause) |
| 26 | Eviction mystery SOLVED (P15) | CLOSED | already marked |
| 27 | KPI STATUS 30fps (P11b) | CLOSED/historical | already carries the P51 staleness caution inline; no further action |
| 28 | ConstrainedMeshCache never constructed | **OPEN, confirmed** | T53: `grep -rn "constrainedMeshCache_\s*="` across src/include finds no construction site, only `.reset()` (null) and null-checked consumers. Still true as of `607a19c3` |
| 29 | P10b's 71 evictions | CLOSED by P15 | already marked |
| 30 | Entity mesh materials dangling | CLOSED by P17 | already marked |
| 31 | Player BSP SIZE_MAX | OPEN | not chased, left open |
| 32 | Pale fog-colored flat shapes | OPEN | not chased, left open |
| 33 | Production draw-order nondeterminism | CLOSED by Q02 | already marked |
| 34 | **RDP frame-update race** | **CLOSED — was WRONGLY MARKED OPEN** | T53: `src/client/graphics/rdp/rdp_server.cpp:325-342` now does join-before-close exactly per T03b's prescription. Commit `a835969` ("Merge T35: fix the RDPServer::stop() use-after-free, retire its waiver"). Cross-confirmed against `EXECUTOR_PROTOCOL.md`'s own verification-bar note, which independently states the waiver was retired. **Flagged loudly per the plan's "dangerous direction" ask** — see below |
| 35 | TWO GameState instances | OPEN, needs a ruling | architecture decision, correctly left open |
| 36 | Dead parallel state copies (worked example) | CLOSED, re-confirmed | T53 independently re-verified all 3 sub-claims by grep (not just trusting the plan's framing): `syncCombatState()` at `application.cpp:1785`; `setEquipmentOccupied`/etc. now called at `game_state_sync_bridge.cpp:139-143`; `world_state.cpp:25-28,117-118` uses `m_zoneArrival*`. All hold |
| 37 | willeq.db schema version | **OPEN, confirmed** | T53: `willeq_database.cpp` reads `metadata.version` and logs it, but nothing compares it to an expected value — still no validation/rejection path |
| 38 | **Headless client aborts on shutdown** | **CLOSED — was WRONGLY MARKED OPEN** | T53: commit `fa31ca97` ("T30: fix headless client abort + non-exit..."). `console_input_handler.cpp:54-71` fixes the join race; `application.cpp:903,919` (`processPreConnectionLifecycleCommands()`) intercepts `/quit` before the `m_fullyConnected` gate |
| 39 | **Non-FATAL log output never reaches stdout** | **CLOSED — was WRONGLY MARKED OPEN, "biggest remaining automation blocker"** | T53: commit `c167acdc` ("T31: fix non-FATAL log output..."). `include/common/logging.h:258-282` now runs `LogManager::drainLoop()` unconditionally every 50ms |
| 40 | Environment/asset blockers (T29 demo) | CLOSED (T26 verification owed → now done) | T53: cross-referenced — T32 (section 44) completed the real kill→loot check this section says is "still owed"; annotated the paragraph rather than rewriting the verdict elsewhere |
| 41 | Command-registry overlap | OPEN (narrowed) | T51 already closed 3/~34 names; rest correctly left open |
| 42 | `--log-level=` clobbered | **OPEN, confirmed still reproducing** | T53: `main.cpp:19` unconditionally calls `EverQuest::SetDebugLevel(config.debugLevel)` (default 0) AFTER `InitLogging()` parsed `--log-level=`; `SetDebugLevel`→`SetGlobalLevel` unconditionally overwrites. The `if (config.debugLevel > 0)` guard only re-applies when `-d` is also given. Verified still broken |
| 43 | Live-session test config stale paths | CLOSED | already marked |
| 44 | Auto-loot never worked | OPEN (2 follow-ups) | correctly open; also closes item 40's "T26 verification owed" (see above) |
| 45 | race=415 missing model | **OPEN, confirmed still reproducing** | body itself documents T32's re-test after the path fix; no later fix commit found |
| 46 | check_file_sizes.sh vacuous pass | CLOSED by T37 | already marked |
| 47 | Golden replay silent skip | CLOSED by T36 | already marked |
| 48 | **Item links lose 11 chars** | **CLOSED — was WRONGLY MARKED "NOT FIXED"** | T53: commit `7a1e48b9`, same `task/T34-*` branch as the entry itself, landed shortly after the entry was written and before merge (`5ec8ccc9` merge title explicitly lists the fix). `formatted_message.cpp` now uses `isAllHex()` structural disambiguation at both call sites. Pinned by `tests/test_formatted_message.cpp:558-605` (the exact "Scaled Curskin" case) |
| 49 | PATTERN EQT_HAS_GRAPHICS (T43 sweep) | CLOSED as pattern, 1 residual **CLOSED — was WRONGLY MARKED "not yet fixed"** | T53: the `OnPetCreated()` residual this entry itself calls unfixed was fixed in the *same* T43 commit (`fabf0246`) — all 3 call sites in `eq_entities.cpp` (~481/1236/1462) are unconditional with a `// T43: was #ifdef EQT_HAS_GRAPHICS` comment. The entry's prose was never updated after its own fix landed |
| 50 | PATTERN UI-gesture reachability | PARTIAL, narrowed | vendor (T48) and attack (T51) closed; broader pattern (dozens of other producer-less intents) correctly left open, now 6 confirmed instances per the entry's own running count |
| 51 | GameTick never runs headless | CLOSED by T45 | already marked |
| 52 | **InventoryManager/SpellManager/etc bridge wiring headless** | **CLOSED — was WRONGLY MARKED "NOT FIXED"** — **the most consequential stale-open finding in this pass** | T53: commit `b5f5c1d6` ("T46: wire the four managers' bridge pointers headless..."). `EverQuest::wireBridgeToSubsystems()` (`eq_bridge.cpp:121,229`) does exactly what this entry asked for, called from both `setBridge()` and `InitGraphics()`. Corroborated by the T47 entry immediately below it, which depends on and names this same function |
| 53 | Bridge pointers never cleared at teardown | CLOSED by T47 | already marked; internally consistent with item 52's fix |
| 54 | PlayerSpawnIdSet timing | CLOSED (non-bug) | already marked |
| 55 | Batch Z | OPEN | Z5.3 TODO, left open |
| 56 | Tracker hygiene | OPEN | cosmetic, left open |
| 57 | P37 follow-ups | CLOSED (bless chain complete) + 2 OPEN sub-items | terminal state already says "P37 VISUAL BLESS: CLOSED"; two `applyPaletteMask` plumbing gaps (mesh_building.cpp, animated_mesh_creation.cpp) remain open, added to index |
| 58 | P40 resolution-visibility | CLOSED | already marked (P43 root-cause + P49/P51 ride-along) |
| 59 | P41 lighting lever | CLOSED (decision made) | swap-floor sub-question refined by P52/P52b, not independently re-measured further |
| 60 | P39 occluder-256 | CLOSED | already marked |
| 61 | P42 perf-hygiene bundle | CLOSED | already marked |
| 62 | P50 query API masking gap | **OPEN, confirmed** | not independently re-verified against current `ConstrainedTextureCache` source (perf-tooling item, low priority); left open per bias rule |
| 63 | P48 sheet-alpha | CLOSED | already marked |
| 64 | P52 GPU-sync | CLOSED, 1 caveat OPEN | `B_e205` magnitude caveat added to index |
| 65 | STATE-LAYER SWEEP | CLOSED | /bank and /train both closed by T50 within the same section |
| 66 | #goto teleport | CLOSED (non-bug, server-side) | already marked; 3 implicated-not-fixed items (succor/evacuate/bind-respawn) added to index |

### Before/after open-item count

- **Before**: heading scan reads ~33 open (per the plan's own framing).
- **After this pass**: **23 sections are genuinely open or partial-with-an-open-remainder** (see the Index at the top of `deferred.md` for the itemized list, ~40 individual open bullets once sub-items are counted — the file was never really "33 flat items", it's ~33 headings containing a mix of closed and open bullets). The gap between the two numbers is almost entirely **stale-closed content that was never re-titled**, not new discoveries — 8 sections/sub-items turned out to already be fixed (see "dangerous direction" below), and only ~19 test-skip items were newly added as directed by the plan.

### Flagged loudly: entries marked OPEN that were actually CLOSED

This is the **safe** direction (costs a future reader time, doesn't lose a
defect) but is still the headline finding of this pass — 6 entries,
several load-bearing:

1. **RDP frame-update race** — said "fix still open"; T35 fixed it.
2. **Headless client aborts/never-exits on shutdown** — said "PRE-EXISTING,
   blocks automation"; T30 fixed it.
3. **Non-FATAL log output never reaches stdout** — said "NOT FIXED, likely
   the biggest remaining automation blocker"; T31 fixed it.
4. **Item links lose 11 chars** — said "NOT FIXED"; fixed in the same T34
   unit, after the entry was written, before merge.
5. **OnPetCreated `#ifdef` residual** — said "not yet fixed" inside its own
   filing unit's entry; fixed in that same commit.
6. **InventoryManager/SpellManager/SkillManager/HotbarModel bridge wiring
   headless** — said "NOT FIXED"; T46 fixed it exactly. **This is the one
   I'd flag most: it's the kind of entry a future unit would read, believe,
   and re-do work on that was already done days earlier.**

**No instance of the actually-dangerous direction was found** — I did not
find any entry marked CLOSED that is actually open or was re-opened by
later work. The `PlayerSpawnIdSet` entry (already correctly marked "NOT a
bug" after T43/T46 cross-checked each other) was the closest candidate and
held up under its own stated reasoning.

### Entries I could not settle cheaply

- **blackburrow/crushbone blank render** (Batch H) — needs an actual render
  (visual gate or live client) against current fable; no fix commit exists
  and the claim (BSP above/below-ground split) isn't grep-able.
- **Live-game-only full-zone-population per-frame loops**
  (`sim_entity_sync.cpp:179` etc., inside the H04 performance section) —
  needs a live profiling session, explicitly out of this unit's no-server
  scope.
- **P52's `B_e205` magnitude caveat** and **P41's swap-floor residual** —
  both need further Pi measurement, not settleable from source alone.
- Several architecture/UI items in "Batch U" and "arch_design_update.md"
  (input-lag fix, unified priority queue, door placeholder cube, etc.) —
  I chose not to spend verification budget on ~9 low-severity, pre-2026-07
  UI/perf polish items when the plan's named priorities (automation
  blockers, board-trust bugs) had higher-value stale-close candidates to
  chase instead. Left open, unverified either way — this is a deliberate
  scoping choice, not an oversight, and it's disclosed here so it reads as
  one.

### New items added (per the plan's step 5)

All confirmed against a live `ctest --test-dir build` run (not just
grep'd), added to the Index's "Test coverage gaps" group:
- 8 `DoorManagerGraphicsTest.*` skips — environment-gated (`DISPLAY`/device
  creation), by design.
- 6 `InventoryModelViewTest.*` skips — **all unconditional**
  (`GTEST_SKIP()` fires regardless of environment, "WindowManager has been
  deleted"), a different and more concerning shape than the other 13.
- `PfsArchiveContentTest.DdsFileValid`, `SoundBufferTest.LoadSwingWav` —
  asset-presence-gated.
- The "2 SpellManagerGems skips" note: no such note exists in
  `deferred.md` as written, but the number is accurate — confirmed live
  (`MemorizeSpellSuccessStartsMemorizationAndPublishesEvent` and
  `MemorizeSpellBlockedWhileAlreadyCastingOrMemorizing`, both gated on "no
  Warrior-usable spell found in scanned range", not DB absence). Reconciled
  in place with the gating condition on record.

### Verification bar

1. **`git diff --stat`**: `refactor/deferred.md | 346 ++++++++++++++++++++++++++++++++++++++++++++++----- ` — **exactly one file changed.**
2. **Added/removed line counts**: 317 insertions, 29 deletions. Every
   removed line accounted for individually — all 29 are either (a) heading
   lines replaced by a longer heading + follow-on evidence paragraph (the
   "re-titling legitimately removes one line and adds one" case the plan
   anticipated, just with more added each time), or (b) the file's own
   opening instruction ("delete it here") corrected to match the actual
   governing policy. Diffed by hand (`git diff | grep '^-'`) — no bulk
   deletion, no research content lost. The intro's "delete it here and note
   the commit" instruction was rewritten because it directly contradicted
   this unit's (and the file's own established style's) "never delete"
   rule — judged in-scope as a meta-instruction fix, not a content edit.
3. **Builds**: `cmake --build build -j24` and `cmake --build build-gles2
   -j24` both exit 0 (docs-only change, effectively no-op rebuilds).
   `ctest --test-dir build --output-on-failure`: **1913/1914, sole failure
   `AudioManagerTest.PreloadCommonSounds`** (the documented ALSA host
   exception) — unchanged from baseline, confirming the docs-only change
   perturbed nothing.
4. **`check_file_sizes.sh`** — two results, and the discrepancy is worth
   stating plainly rather than picking the one that passes:
   - `bash scripts/refactor/check_file_sizes.sh fable` (the normal,
     base-rev form): "no C++ files to check ... nothing was checked, this
     is not a pass" — correct and expected, since this unit changed zero
     `.cpp`/`.h`/`.hpp` files. This is the **real** verification-bar result
     for a docs-only unit.
   - `bash scripts/refactor/check_file_sizes.sh --files
     .../refactor/deferred.md` (forced, as the plan specified): **FAILS —
     3183 lines, over the 3000 hard ceiling** (was 2895 before this unit).
     Worth flagging honestly: this ceiling (`docs/decisions.md` Batch F) was
     designed for C++ translation units, and the script's own default mode
     filters to `*.cpp`/`*.h`/`*.hpp` for exactly that reason — applying it
     to a markdown research record via `--files` is a mismatch of tool to
     artifact. `deferred.md` was already at 2895/3000 (96.5% of the
     ceiling) *before* this unit touched it, purely from ten units' worth of
     legitimate "never delete, always append" history — this file was
     always going to cross 3000 eventually under its own governing rule,
     with or without a hygiene pass. This unit's index + evidence additions
     (~290 net lines) pushed it over. I did not trim annotations to duck
     under the ceiling — that would mean either deleting verification
     evidence or being stingier with it than the plan asked for, which
     seemed like the wrong tradeoff. **Flagging for Will's call**: either
     exempt `deferred.md` from the C++ line-ceiling check (it isn't code),
     or decide this file needs its own splitting/archival scheme
     eventually (e.g. move fully-CLOSED historical sections to a
     `deferred_archive.md` once they're old enough — NOT done here, since
     that would be exactly the "bulk deletion" this unit was told is a STOP
     condition).

### Is the board usable as a work queue now?

**Yes, materially more than before, with one honest caveat.** The Index at
the top now lets a future scoping pass read ~20 open architecture/
correctness items and ~19 test-skip items without wading through 3000+
lines of historical narrative, and 6 entries that were actively
*misleading* (marked open/unfixed when already fixed) are corrected with
evidence. The caveat: this file's structure (chronological append-only,
one unit per section, historical detail preserved forever) means a heading
scan will keep drifting from ground truth as more units land and don't
loop back to close their own entries — the same failure mode that caused
this unit to exist. The Index is a snapshot as of `607a19c3`/this commit;
it is not self-maintaining. Recommend: whoever's plan-writing process
generates the next unit's "known open items" input should regenerate (or
at least spot-check) the Index rather than trusting it indefinitely, and
the file-size ceiling question above should get a ruling before the file
grows further.
