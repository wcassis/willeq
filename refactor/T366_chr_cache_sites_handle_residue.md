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
