# T366 — the four remaining chr-cache bypass sites (B095); the last EverQuest& residue (B038)

Class B, slot3. Two legs, one commit each (Leg B may be several commits,
one per cluster). Read the B095 and B038 board text and T364's / T341's
§Review.

## Leg A — B095

`loadModelFromZoneChr` (animation-source insert) and
`searchZoneChrFilesForModel` (two negative-cache inserts + one inline
open-coding of the store helper's push/evict) still write
`otherChrCaches_[key]` directly. Route all four through
`storeOtherChrCacheLocked()`; delete the inline copy. Extend
`tests/test_chr_cache_lru.cpp` to drive both functions (real archives,
EDT_NULL, T364's harness), pre-fix failing on the accounting seam
`getChrCacheAccountingForTest()`. Report the measured effect (how many
entries were untracked per path before). Strike B095.

## Leg B — B038: make the handle go to zero

T341 left four clusters and stated that (1) was a design question. The
ruling (orchestrator, 2026-09-05): the transition handle is retired
entirely.
1. `EverQuest::m_test_bypass_send_guard` (5 `owner_.` uses: movement 3,
   group 2; set by `Test_SetBypassSendGuardForSeam`): the guard bypass
   belongs to the thing that sends — move the flag into `PacketSender`
   (`ctx_.net`), expose the same test setter there, delete the EverQuest
   member and its setter; the T88 tests that use the seam switch to the
   new setter. Pre/post: the T88 tests stay green, and a planted removal of
   the bypass fails them.
2. 22 `&owner_` identity keys in `MovementComponent`'s function-static
   `std::map<EverQuest*, …>` throttle tables: key on `this` (one
   MovementComponent per EverQuest — state the invariant in a comment and
   pin it if a test can construct two). The maps' key type changes to
   `const MovementComponent*`.
3. `std::bind(&EverQuest::ZoneOnStatusChangeReconnectDisabled, &owner_, …)`
   x2 in `ZoneComponent`: the function is zone territory — move it into
   `ZoneComponent` (K mechanism: substitutions, callers found by the
   compiler) and drop zone's `friend` line.
4. `EQ::castRefusalMessage(owner_, result)` x2 in `BridgeComponent`: read
   what the free function takes from `EverQuest&`; change its parameter to
   the narrowest thing it needs (`ClientContext&` or the strings service),
   fix its callers, drop bridge's handle.
5. When no component names `EverQuest&` any more: delete the `Owner&`
   constructor parameter from the components that still carry it, the
   remaining `friend` lines (group, movement, zone), and update
   `refactor/K_component_extraction.md` §CLOSE-OUT + `CLAUDE.md` with the
   final census (a comment-filtered grep for `owner_` under `src/client`
   must return zero outside the renderer families, where `owner_` is the
   RendererContext pattern — say which). Strike B038.

## Proof

Build, `ctest -j6`, the new/changed tests with pre-fix/post-fix runs,
the order goldens for every TU touched (G06 movement, G01 zone opcodes,
the bridge intent table), file sizes; Leg A touches `src/client/graphics`
→ visual gate standalone + ARM.

STOP: `castRefusalMessage` needs something only `EverQuest` has (then
report what, and do the other three clusters).

## Review

Branch `task/T366-chr-cache-sites-handle-residue`, base `fable` tip `5e059c25`.
Leg A `a2b83e28`, Leg B `7ac4efe6`; the board strikes, doc updates and this
Review are the branch tip. No eqemu lock taken — nothing here needs a server.
No STOP hit: `castRefusalMessage` reads only context members.

### Leg A — B095

**The four sites.** `loadModelFromZoneChr`'s animation-source insert
(`model_loading_chr.cpp`) and `searchZoneChrFilesForModel`'s two negative-cache
inserts plus the third that open-coded the helper's push/evict
(`model_loading_search.cpp`) all call `storeOtherChrCacheLocked()` now. The
inline LRU block is deleted; the positive insert re-finds through the iterator
because the store may evict. **There is exactly one `otherChrCaches_[key]` left
in the tree and it is the helper's own line** — measured, not asserted.

**The test.** `ChrCacheScanTest` in `tests/test_chr_cache_lru.cpp`. Both target
functions are private and both sit on the NEW-models branch of
`ensureModelDataLoaded()`, so the fixture drives them through that public entry
point. `searchZoneChrFilesForModel` scans the whole client directory for
`*_chr.s3d` — 378 archives in the real tree — so the fixture builds a **private
client tree** of symlinks holding five archives, plus two engineered to take the
two negative branches: `zzbroken_chr.s3d` (64 bytes of junk, `loadZone()` fails)
and `zznochars_chr.s3d` (a symlink to the zone archive `arena.s3d`). Race 76
(PUM in `beholder_chr.s3d`, animation source LIM, whose own archive is
`arena_chr.s3d`) reaches the first site; race 330 (FRM, which lives only in
`globalpcfroglok_chr.s3d` and is therefore in no archive in the tree) makes the
scan run to completion.

**Measured effect, per path, cap 2:**

| case | pre-fix | post-fix |
|---|---|---|
| `ZoneChrAnimationSourceInsertIsRegisteredInTheLruOrder` | **FAIL** — `untracked=1` of `entries=1`, `lruOrder=0` | PASS |
| `ScannedChrFilesAreAllRegisteredInTheLruOrder` | **FAIL** — `untracked=1` of `entries=3`, `lruOrder=2` | PASS |
| `ScannedChrFilesRespectTheCap` | **FAIL** — `entries=3` against a cap of 2; `entries(3) != lruOrder(2)` | PASS |

**This was NOT the total bypass B065's path turned out to be.** B065 measured
6 of 6 untracked and an inert cap; here the animation-source insert is total
(its one entry is its only entry) but the directory scan's inline accounting
really did track every positive insert — only the negative-cache entries
escaped. That is still fatal to the cap, because an untracked entry can never be
evicted: three entries stood against a cap of two and the eviction loop had no
candidate to take it lower.

**Surprise, and a new board item (B105).** The first pre-fix run reported
`untracked=7 of 7, lruOrder=0` — every archive in the private tree failing to
cache a single character. The cause was not the fix under test:
`S3DLoader::loadCharacters()` derives the character-archive path by searching
the **whole path** for the first `_chr` and truncating there, so the fixture's
scratch directory `/tmp/willeq_chrscan_<pid>/` collapsed every archive to
`/tmp/willeq_chr.s3d` and every load silently returned zero characters.
Renaming the directory to `willeq_scantree_<pid>` fixed it outright. Boarded as
**B105**: it is a silent total failure for any EQ client path with `_chr` in a
directory component, and nothing in the client's output diagnoses it.

### Leg B — B038, the handle to zero

| cluster | ruling | result |
|---|---|---|
| 1. `m_test_bypass_send_guard`, 5 `owner_.` uses | moved to `eqt::PacketSender` | guards read `ctx_.net.TestBypassSendGuard()`; **70 test call sites** retargeted to `eq.Net().Test_SetBypassSendGuardForSeam(true)`; the `EverQuest` member, its setter and its doc block are gone |
| 2. 22 `&owner_` identity keys in movement | key on `this` | five function-static tables, key type `const MovementComponent*`; the one-to-one invariant is stated in `movement_update.cpp`. K06/K07 correction (n) is reversed |
| 3. 2 `std::bind(&EverQuest::ZoneOnStatusChangeReconnectDisabled, &owner_, …)` | the function is zone territory | moved into `eqt::zone::ZoneComponent`; one substitution in the body (`m_zone_connection` -> `ctx_.zone_connection`); deleted from `eq.h` and `everquest.cpp`, callers found by the compiler |
| 4. 2 `EQ::castRefusalMessage(owner_, result)` | narrow the parameter | takes `eqt::ClientContext&`. It reads exactly three things — `GetSpellManager()`, `GetInventoryManager()`, `Strings()` — and all three are context members, so no STOP. Bridge passes `ctx_`; the action layer passes `m_eq.Context()` |
| 5. the handles and friends | delete | `Owner&` ctor parameter and `EverQuest& owner_` member removed from group, movement, zone and bridge (the last four); the three surviving `friend` lines removed |

**Final census (comment-filtered, code lines only, T366 tip).**

| scope | `owner_` |
|---|---|
| `src/client/**` + `include/client/**`, excluding `graphics/` | **0** |
| `graphics/` — Batch L's `IrrlichtRenderer& owner_` (slash 39, zone_lifecycle 31, sim 20, zone_load 16, diagnostics 13, frame 11, player 9, overlays 9, eqe1 9, zone_stream 6) | 163 |
| `graphics/zone_shader_callbacks.h` — `ZoneShaderManager* owner_`, a shader-callback back-pointer, neither programme's handle | 178 |

`EverQuest&` outside `graphics/` survives in exactly one place and it is not a
component: `EqActionHandler::m_eq` (`include/client/eq_action_handler.h`), the
action layer's own handle on the client. `friend class` in `eq.h` is down to
`CombatManager` and `TradeManager`.

**Planted-fault proof (cluster 1).** `TestBypassSendGuard()` forced to
`return false`, the four dependent targets rebuilt:

| test | with the bypass removed | restored |
|---|---|---|
| `test_position_frame_roundtrip` | **23 cases FAILED** | 13 tests PASSED |
| `test_group_accept_gm_hideme` | **2 cases FAILED** (`AcceptNamesTheInviterEvenThoughFollowClearsThePendingName`, `DeclineStillConsumesThePendingInvite`) | 8 tests PASSED |
| `test_setheading_conversion` | 8 PASSED (does not exercise a guarded send) | 8 PASSED |
| `test_group_component` | 5 PASSED (same) | 5 PASSED |

Both the movement guard and the group guard are proven live and proven reached
through the new home. Done with a file copy + restore, never `git stash`.

### Goldens

Run standalone on the Leg B tip; **none needed re-blessing** — the statement-text
goldens do not quote any line this branch changed.

| golden | result |
|---|---|
| `test_movement_order` (G06) | 4 PASSED |
| `test_zone_dispatch_table` (G01) | 6 PASSED |
| `test_bridge_intent_table` | 6 PASSED |
| `test_autorun_arm` | 4 PASSED |
| `test_reachability_sweep` | 5 PASSED |

`git status tests/goldens` is clean.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0.** One intermediate build hit exit 143 at the 600 s ceiling (the `race_model_loader.h` comment edit invalidates the whole graphics tree). Post-143 sweep: 0 `build*/lib/st??????` temps, both archives intact (`libwilleq_core.a` 321 members, `libEtcLib.a` 15), **5 non-ELF truncated binaries** deleted, plus 19 more targets the log named as `Deleting file`/`Terminated` deleted by hand — a glob cannot match a target the killed link removed. Rebuild EXIT 0, re-scan `bad=0`, 271 binaries present |
| `ctest --test-dir build -j6 --output-on-failure` | **3,904 of 3,905 passed** (exit 8). Sole failure `AudioManagerTest.PreloadCommonSounds`, the waived ALSA host quirk. No load artefacts (`visual_gate`, `HeadlessShutdownTest`, discovery) |
| `scripts/build-gles2-x86.sh` | EXIT 0 |
| `scripts/ci/visual_gate.sh` | **PASS** (qeynos2 p95 4.315 ms / ceiling 10.108; gfaydark 5.328 / 10.672; befallen 7.787 / 10.948). Goldens untouched |
| `scripts/build-arm-noble.sh` | EXIT 0 |
| `scripts/refactor/check_file_sizes.sh fable` | **43 checked, 43 ok, 0 warn, 0 fail** |

### Board and docs

- **B095 struck** with `a2b83e28` and the per-path measurement.
- **B038 struck** with `7ac4efe6` and the final census.
- **B105 added** (B) — **closed by T369**: `S3DLoader::loadCharacters` matches `_chr` against the
  whole path.
- `refactor/K_component_extraction.md` §CLOSE-OUT gains "The handle reached
  ZERO", with the four-cluster table and the census.
- `CLAUDE.md` "Friend class pattern" rewritten: `CombatManager` and
  `TradeManager` are the only two, no component is a friend or holds a handle,
  and the `graphics/` `owner_` uses are named as the two other patterns they are.

### Deviations

None. Both legs executed as planned.
