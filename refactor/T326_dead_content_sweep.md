# T326 — the dead-content sweep

**Slot:** wt/slot3  **Branch:** `task/T326`  **Base:** `bcbf2ddc` (Batch J: J34 merged)

Will ruled directly (2026-09-02): *"run the dead-content sweep"*. Batch J's
file-size reclamation found six large blocks of dead content while splitting;
none of them were in scope for the splitting units, which preserved them
verbatim. This unit removes them.

## The governing distinction (J07's, accepted by Will)

Commented-out **code** is dead weight and goes. **Prose explaining a
deliberate absence** is documentation and stays — deleting a tombstone
paragraph invites someone to rebuild what was deliberately removed.

## Site table

| # | site (at base `bcbf2ddc`) | lines | class | evidence it is dead |
|---|---|---|---|---|
| 1 | `src/client/eq_ucs.cpp` (whole file) | 320 | commented-out code | 79 lines of `#include` + one 241-line `/* … */` block; **zero live code**. The TU compiles nothing. Chat-server support is deliberately deferred (`docs/decisions.md` 2026-08-27) and T297 measured that 7 of 8 chat channels work cross-zone on a Titanium client without it. |
| 2 | `src/client/graphics/zone_shader_sources_gl.h` (whole file) | 370 | retired-config code | Entire body is inside `#ifndef EQT_HAS_GLES2`. Included only by `zone_shader.cpp`. `EQT_GLES2=OFF` with graphics was retired by P30/P32; no script in the acceptance bar configures it, so no supported build parses a single line of this file. |
| 3 | `constrained_texture_cache_upload_queue.cpp` `#if 0` region | 299 | obsolete implementation | Not a duplicate: it is T04's older upload queue, whose members (`decodedQueue_`, `decodedQueueMutex_`, `pendingDecodes_`, `stagedUploads_`) were deleted from the header by T04 — the header carries tombstone prose saying so. The block therefore cannot compile. Its `isPending` reads a member that no longer exists; the live `isPending` after the `#endif` reads a different one. |
| 4 | `src/client/graphics/eq/wld_loader.cpp` `#if 0` region | 278 | dead block | Lines 472–749, entirely **outside** the namespace close, in a fake `namespace removed`. D20f1 moved these implementations to `src/client/zone_bsp.cpp`. |
| 5 | `src/client/eq_entities.cpp` hex-dump block | 42 | commented-out code | Lines 62–103, a `/* … */` debugging scratchpad from the original spawn-offset investigation. Its one live-looking statement (`offset = 2;`) is already executed three lines above the block. |
| 6 | `src/client/combat.cpp` two packet structs | 26 | unreferenced code | File-local (`.cpp`-scope, unnamespaced) `Consider_Struct` and `Action_Struct`. Zero readers in this TU or its two J24 siblings. Every other tree-wide hit is a different symbol: the namespaced `EQT::` structs in `include/common/packet_structs.h`, and a separate file-local struct in `eq_combat_actions.cpp`. `ClientTarget_Struct` in the same block **is** read (4 sites) and stays, with its `#pragma pack` pair. |

Total: **1,336 lines** (site 6 took 27, not 26 — the two structs plus the blank line that separated them from the live one).

## KEEP — explicitly not touched

- J06's 22-line tombstone in the application zone-loading TU.
- J07's two tombstone paragraphs (the boat/T168 note, the T145 `OP_ENVDAMAGE` note).
- The T04 tombstone prose in `include/client/graphics/constrained_texture_cache.h`
  (lines 234, 517) — it is what makes site 3's deletion legible.
- The D20f1 one-liner in `wld_loader.cpp` above the deleted block.
- `src/client/ucs.cpp` — a different, live file.
- The `UCSClientOpcodes` enum and `m_ucs_*` members in `eq.h` — live, and covered by tests.

## Method notes

- On this host an empty grep is not evidence of absence. Every pattern used
  below was first shown to match somewhere before a zero was believed.
- Each deletion leaves a **one-line pointer naming the base commit**, so the
  content is recoverable with `git show <base>:<path>`. Pointers deliberately
  avoid quoting any symbol name, because a comment can satisfy a
  `find != npos` guard in a test and silently disarm it.

## Site 2, the subtle one — why the `#ifdef` is NOT simplified

`zone_shader.cpp` keeps its `#ifdef EQT_HAS_GLES2 / #else` structure unchanged;
only the `#include` of the deleted header goes. Collapsing the `#else` arms
would change preprocessor behaviour in the retired configuration (and touches
seven separate gates in that TU for no gain in any supported build), so per the
unit's instruction it was not done. The retired configuration now fails to
compile rather than building a desktop-GL shader set — which is the correct
outcome for a configuration P30's CMake gate already documents as unsupported.

## Verification bar

`scripts/build-gles2-x86.sh` → full `ctest -j6` (zero new failures vs
`refactor/test_baseline.txt`) → visual gate PASS with goldens untouched →
`scripts/build-arm-noble.sh` → `check_file_sizes.sh --files`.

## Review

### Result: all six sites removed. Nothing turned out to be reachable.

| # | site | lines removed | outcome |
|---|---|---|---|
| 1 | `src/client/eq_ucs.cpp` | 320 | file deleted; CMake add-site removed; `docs/module_map.md` row dropped |
| 2 | `src/client/graphics/zone_shader_sources_gl.h` | 370 | file deleted; `#include` in `zone_shader.cpp` replaced by a pointer; `src/client/graphics/CLAUDE.md` and `docs/module_map.md` updated |
| 3 | `constrained_texture_cache_upload_queue.cpp` | 299 | `#if 0` region removed; file 373 → 78 lines; its header comment rewritten (it described the region as most of the file) |
| 4 | `src/client/graphics/eq/wld_loader.cpp` | 278 | `#if 0` region removed; file 749 → 475 lines |
| 5 | `src/client/eq_entities.cpp` | 42 | comment block removed |
| 6 | `src/client/combat.cpp` | 27 | two file-local structs removed; `ClientTarget_Struct` and the `#pragma pack` pair kept |

**1,336 lines of dead content removed**, plus 20 lines of pointer comments added back.

### Evidence, per site

1. Read the whole file: 79 `#include` lines, then a single unterminated-in-J02
   `/* … */` spanning the rest. No live statement anywhere. Nothing outside the
   plan/doc set references the file; the only build reference was one
   `CMakeLists.txt` line. Independently, `src/client/ucs.cpp` (413 lines) is the
   live chat-server file and is untouched, and the opcode enum and `m_ucs_*`
   members in `eq.h` are live and test-covered — all of that stays.
2. Every line of the header sits inside one `#ifndef EQT_HAS_GLES2`. The only
   includer is `zone_shader.cpp`. `CMakeLists.txt` defines `EQT_HAS_GLES2`
   whenever `EQT_GLES2` is on, and the surviving configuration space is
   `EQT_DRM OR EQT_GLES2` (fatal error otherwise, P30); the one remaining
   combination that would reach the deleted arm — ARM board with
   `EQT_DRM=ON EQT_GLES2=OFF` — is documented in `CMakeLists.txt` itself as
   retired by P32, and `scripts/build-arm-noble.sh` has no flag to select it.
   So neither `build-gles2-x86.sh`, the `build/` dev tree, nor
   `build-arm-noble.sh` compiled a single line of it. Confirmed by all three
   builds passing after deletion.
3. The disabled block references `decodedQueue_`, `decodedQueueMutex_`,
   `pendingDecodes_` and `stagedUploads_`; grepping the header for each finds
   them **only inside T04's own tombstone comments** saying they were removed.
   The block is therefore not merely disabled but uncompilable, which settles
   it as an obsolete implementation rather than a redundant copy — exactly as
   the dispatch predicted. Its `isPending` reads `pendingDecodes_`; the live
   one after the `#endif` reads `decodedEntries_`.
4. The block lives after `} // namespace EQT`, inside a fabricated
   `namespace removed`, with `_REMOVED`-suffixed method names. D20f1's own
   one-line note above the namespace close names the file the real
   implementations moved to. That note is prose and was kept.
5. The block is a `/* … */` whose only executable-looking line (`offset = 2;`)
   duplicates an assignment made three lines earlier in live code, so even
   re-enabling it would be a no-op.
6. The two structs are file-local (no namespace, defined in a `.cpp`), so only
   `combat.cpp` could read them, and it does not; nor do its two J24 siblings
   `combat_rounds.cpp` / `combat_hunting.cpp`. The 15 tree-wide grep hits
   resolve to three *different* things: the namespaced `EQT::` structs in
   `include/common/packet_structs.h` (live, size-asserted, test-covered), a
   separate file-local struct in `eq_combat_actions.cpp`, and prose in tests
   and headers. `ClientTarget_Struct`, in the same `#pragma pack` block, has
   four live readers and stays.

### Grep discipline

Every "no readers" conclusion here rests on a pattern that was first shown to
match somewhere else in the same run — the header tombstones for site 3, the
`EQT::`-namespaced hits for site 6, the `#if 0` pair for sites 3 and 4. No
conclusion rests on an unaccompanied empty result.

### Pointer comments

Each deletion site carries a short comment naming the base commit, so the
content is one `git show bcbf2ddc:<path>` away. The comments deliberately name
no symbol — a comment can satisfy a `find(...) != npos` guard in a test and
silently disarm it.

### Deviation

None to the scope. One judgement call, taken as the plan section above
describes: `zone_shader.cpp`'s `#ifdef`/`#else` gates were **not** collapsed.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 dev tree) | EXIT=0. First attempt hit the 600 s ceiling (exit 143); after the re-run every binary in `build/bin` and every `lib*.a` was scanned for ELF/archive validity and non-trivial size — 0 bad, so the T293/J07 truncation trap did not fire. |
| `bash scripts/build-gles2-x86.sh` | EXIT=0 (511 s) |
| `ctest --test-dir build -j6 --output-on-failure` | **3506 / 3507 passed.** Sole failure `AudioManagerTest.PreloadCommonSounds` — the documented ALSA host quirk in `refactor/test_baseline.txt`. **Zero new failures.** |
| visual gate | PASS — `visual_gate` ran inside the suite (test #3504, 37.76 s). `git status` shows no golden modified. |
| `bash scripts/build-arm-noble.sh` | EXIT=0 (440 s). This is the load-bearing one for site 2, and it is clean. |
| `check_file_sizes.sh --files` | 5 ok, 0 warn, 0 fail |
