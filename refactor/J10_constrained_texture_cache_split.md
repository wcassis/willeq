# J10 — Batch J Wave 3: split constrained_texture_cache.cpp (2,211 → 4 TUs, largest ~812)

**Slot: assigned at dispatch. Branch `task/J10-constrained-texture-cache-split`
cut from `fable`. x86 only. No Pi hardware. No eqemu.**

Wave 3 of `refactor/J_file_size_reclamation.md`, running alongside **J08** and
**J09**. Read, before editing anything: **`refactor/J_file_size_reclamation.md`
§Process per unit AND all six correction sections** (eighteen numbered lessons
from J01–J07 and J11; newer text wins over older), then
`refactor/J07_eq_player_info_split.md` **including its Review** (the cleanest
worked example) and `refactor/J05_wld_loader_split.md` §Review (the unit whose
CMake exposure most resembles this one — J05 had nineteen add-sites; **this one
has twelve**), plus `refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## ⚠ THE ONE THING THIS UNIT MUST NOT GET WRONG: the `#if 0` region

`constrained_texture_cache.cpp` carries a **disabled 299-line region**, lines
**822-1120**:

```
 818| // ---------------------------------------------------------------------------
 819| // T04: Upload queue removed. Use getOrLoad() + uploadTexture() instead.
 820| // ---------------------------------------------------------------------------
 822| #if 0  // T04: Removed — replaced by getOrLoad() + uploadTexture()
 ...
1120| #endif  // T04: End of removed upload queue block
```

Inside it are eight method definitions that **do not compile and are not
linked**: `queueDecoded` (823), `queueDecodedARGB` (830), `processUploadQueue`
(847), `drainDecodedQueue` (975), `processOneUpload` (987),
`getStagedUploadCount` (1106), **`isPending` (1111)** and **`hasPendingWork`
(1116)**.

⚠ **The last two are SECOND DEFINITIONS.** The live `isPending` and
`hasPendingWork` are at **1122** and **1127**, immediately after the `#endif`.
A reader — or a reviewer, or a linter, or an executor "tidying while they are in
there" — who sees two definitions of the same method in one file will be tempted
to delete one. **Deleting the wrong one silently removes live behaviour;
deleting either one is outside a move-only commit's contract.**

**Three rules, all absolute:**

1. **The whole region 818-1120 travels intact, in ONE TU, as one contiguous
   verbatim range.** Never split it. Never cut inside it.
2. **Nothing inside it is deleted, uncommented, reformatted or reindented.**
3. **Neither `isPending` nor `hasPendingWork` is touched, in either copy.**

⚠ **The region also contains FOUR nested conditional pairs** — `886/906`
(`EQT_HAS_GLES2`), `934/950` (`EQT_HAS_DRM`), `1016/1035` (`EQT_HAS_GLES2`),
`1063/1084` (`EQT_HAS_DRM`) — so a cut inside it would leave an unbalanced
`#if`/`#endif` in *both* halves, which is J02's failure mode with the
preprocessor rather than a block comment. The rule above already prevents this;
the nesting is stated so you can see the cost of breaking it.

**Reviewer note, to be quoted in the merge commit:** this region is
intentionally preserved. It is the batch's seventh dead-content site and belongs
to the batch's pending dead-content decision (see
`refactor/J_file_size_reclamation.md` §The dead-content decision) — **not to
this unit.**

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No change to
`include/client/graphics/constrained_texture_cache.h`. **No method
decomposition.** No drive-by fixes.

## Measured at `f215651a` (2026-09-02, `fable` tip after J07 merged)

`src/client/graphics/constrained_texture_cache.cpp` — **2,211 lines**, matching
the survey. Every range below was read out of the tree at that commit.
**Re-verify against your own branch tip before you cut** and report drift in
your Review rather than silently adjusting.

**Preamble is lines 1-42**, and ⚠ **it is NOT pure boilerplate — this is
correction 9's case and it will cost you verifier exception lines**:

| lines | content | boilerplate? |
|---|---|---|
| 1-15 | the include block, with a `#ifdef EQT_HAS_GLES2` group at 7-10 | yes |
| 16 | blank | — |
| 17-21 | the 5-line P03 comment explaining why the EQE1 upload path is widened to any GLES2 build | yes (comment) |
| 22 | `#if defined(EQT_HAS_DRM) \|\| defined(EQT_HAS_GLES2)` | yes |
| 23 | `#define EQT_EQE1_UPLOAD_AVAILABLE 1` | yes |
| 24-28 | `#include <GL/gl.h>`, `<GL/glext.h>`, a 2-line comment, `#include <EGL/egl.h>` | yes |
| **29** | `typedef void (*PFNGLGENERATEMIPMAPPROC_)(GLenum target);` | ⚠ **NO** |
| **30-32** | `typedef void (*PFNGLCOMPRESSEDTEXIMAGE2DPROC_)(…);` (3 lines) | ⚠ **NO** |
| 33-34 | blank + a comment about `gles2WrapTexture` | yes |
| **35-37** | `extern void* gles2WrapTexture(…);` (3 lines) | ⚠ **NO** |
| 38 | `#endif` | yes |
| 39 | blank | — |
| 40-41 | `namespace EQT {` / `namespace Graphics {` | yes |
| 42 | blank | — |

**Seven substantive preamble lines (29, 30, 31, 32, 35, 36, 37) × 3 new TUs =
21 substantive ADDED lines.** ⚠ **The verifier will therefore exit 1 by
construction**, exactly as J11's did (70 lines, from 12 `extern` GLES2
declarations plus a stub, × 5 TUs). **This is EXPECTED, not a failure.** Paste
the full output, enumerate all 21, confirm they are textually identical copies
of lines 29-32/35-37, and request orchestrator sign-off in the Review.
**Do NOT contort code to force a green exit** — do not hoist them into a header,
do not trim per-TU include sets. J04 correctly refused to and was signed off.

Line **2209** is blank; **2210-2211** are `} // namespace Graphics` /
`} // namespace EQT`. Body is **43-2208**.

Other verified properties:

- **One file-local `static`**: `eqe1ArchiveStem` (1807), called only from
  `recordEQE1Request` (1815). ⚠ **Confirm that single-consumer claim with your
  own grep** — both are inside range 1537-2209 below, so nothing needs
  promoting and **no internal header is required.** If the grep says otherwise,
  **STOP and report.**
- **Zero anonymous namespaces. Zero block comments** (`grep -n '/\*'` → empty) —
  so the J02 comment-nesting trap cannot fire; the preprocessor is the whole
  hazard here.
- **Zero `// ====` banners.** §Process's banner rule cannot decide the cuts:
  **use J03's rule** (correction 11) — *cut at a blank line whose predecessor
  closes a method, and verify comment depth 0 at the cut.* The file does use
  `// ---------` three-line section rules; three of the four cuts land on one.
- **TWELVE CMake add-sites**, one of them `gles2_zone_benchmark`. See §CMake.
- **Zero functional readers in `tests/` or `scripts/`.** See §Territory.

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-42 | preamble (see table above) | copied to each new TU | 42 |
| **43-808** | the P46d cache-key comment (43-55) + `cacheKey` (56) / `splitCacheKey` (66); ctor (79) / `setDriver` (84) / dtor (94) / `touchInternal` (108) / `getPlaceholderTextureInternal` (117); **`getOrLoad` (155-273)** and **`uploadTexture` (275-437)** — the live load/upload path — + `uploadAllPending` (439-477); then `touch` (479) through `setMeshBuilder` (804-807): accessors, `registerTexture`, alpha/ETC1 queries, `clear`, the ten statistics getters, `dumpLedger`, freeze/unfreeze, and the six setters | **stays** `constrained_texture_cache.cpp` | **~812** |
| **809-1131** | the "Placeholder texture" section rule (809-811) + `getPlaceholderTexture` (813-816); the "T04: Upload queue removed" section rule (818-820); ⚠ **the entire `#if 0` region 822-1120 VERBATIM AND INTACT**; the **live** `isPending` (1122-1125) and **live** `hasPendingWork` (1127-1130) | `constrained_texture_cache_upload_queue.cpp` | ~373 |
| **1132-1536** | the "Eviction listeners" section rule (1132-1134) + `addEvictionListener` (1136) / `removeEvictionListener` (1143) / `evictUntilAvailable` (1154) / `evictTexture` (1192) / `calculateTextureSize` (1231); then the decode/resample family: `processTextureData` (1253) / `downsampleToMaxSize` (1285) / `convertTo16Bit` (1330) / `detectAlpha` (1362) / `decodeBMP` (1372-1469) / `clearTextureReferences` (1472-1535) | `constrained_texture_cache_eviction.cpp` | ~455 |
| **1537-2209** | `probeCompressedTextureSupport` (1537) / `tryCompressedUpload` (1557-1699); the "EQE1 flat pack support" section rule (1701-1703) + `addEQE1Loader` (1705) / `clearEQE1Loaders` (1712) / `snapshotEQE1Loaders` (1720) / `getEQE1RawData` (1725) / `findCompressedEntry` (1751) / `static eqe1ArchiveStem` (1807) / `recordEQE1Request` (1815) / `noteEQE1LateOpen` (1828) / `dumpEQE1RequestLog` (1833); **`uploadCompressedETC1` (1851-2075)** and **`uploadCompressedV2` (2077-2208)** | `constrained_texture_cache_compressed.cpp` | ~723 |
| 2210-2211 | `} // namespace Graphics` / `} // namespace EQT` | replicated in each TU | 2 |

**Result: 4 TUs** (`constrained_texture_cache.cpp` stays and counts, as J01's
did). Largest ~812.

Arithmetic that must hold: `42 + 766 + 323 + 405 + 673 + 2 = 2211`.
If your own numbers do not sum to the file's line count, you have a gap or an
overlap — find it before you build.

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low, J07 ran exactly 3 high). Cluster membership has
been right every time; the arithmetic has not.

Why the boundaries fall where they do: the split follows the file's own four
concerns — **live load/upload + public surface** (stays), **the quarantined T04
upload queue** (isolated deliberately, so 299 lines of disabled code stop
costing every reader of this file their context), **eviction and CPU-side image
decode**, and **GPU compressed upload + EQE1**. The middle TU being the smallest
is a feature, not an imbalance.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is J03's: **a blank line whose predecessor closes a method, comment
depth 0 at the cut.** Checked at `f215651a`:

| Cut | line-1 | line (blank) | line (first of next range) |
|---|---|---|---|
| 809 | `807: }` (end of `setMeshBuilder`) | `808` | `809: // ---------------------------------------------------------------------------` |
| 1132 | `1130: }` (end of the **live** `isPending`) | `1131` | `1132: // ---------------------------------------------------------------------------` |
| 1537 | `1535: }` (end of `clearTextureReferences`) | `1536` | `1537: void ConstrainedTextureCache::probeCompressedTextureSupport() {` |
| 2210 | `2208: }` (end of `uploadCompressedV2`) | `2209` | `2210: } // namespace Graphics` |

⚠ **Note the 1132 cut carefully.** It sits **two lines after the `#endif` that
closes the `#if 0` region** and **immediately after the live `isPending`**.
`hasPendingWork` — the live one, at 1127-1130 — is the method whose closing
brace is line 1130. Get this one line wrong and you either split the disabled
region or orphan a live method. **Paste lines 1118-1136 in your Review.**

Three of the four cuts start at a `// ------` section rule. **Take the rule with
the code below it** — §Process's banner rule generalises to "the leading section
comment travels with its cluster".

## ⚠ HAZARD 1 — the preprocessor. 14 regions; one is the `#if 0`.

**Run this and paste it:**

```bash
awk '/^[ \t]*(#if|#ifdef|#ifndef|#else|#elif|#endif)/ {print NR": "$0}' \
    src/client/graphics/constrained_texture_cache.cpp
```

At `f215651a`:

| pair | guard | where |
|---|---|---|
| 7 / 10 | `EQT_HAS_GLES2` | **preamble** |
| 22 / 38 | `EQT_HAS_DRM \|\| EQT_HAS_GLES2` | **preamble** (defines `EQT_EQE1_UPLOAD_AVAILABLE`) |
| 364 / 385 | `EQT_HAS_DRM` | 43-808 (`uploadTexture`) |
| **822 / 1120** | ⚠ **`#if 0` — the T04 region** | 809-1131 |
| 886 / 906 | `EQT_HAS_GLES2` | **nested inside the `#if 0`** |
| 934 / 950 | `EQT_HAS_DRM` | **nested inside the `#if 0`** |
| 1016 / 1035 | `EQT_HAS_GLES2` | **nested inside the `#if 0`** |
| 1063 / 1084 | `EQT_HAS_DRM` | **nested inside the `#if 0`** |
| 1543 / 1552 / 1554 | `EQT_HAS_DRM` / `#else` | 1537-2209 (`probeCompressedTextureSupport`) |
| 1559 / 1694 / 1698 | `EQT_HAS_DRM` / `#else` | 1537-2209 (`tryCompressedUpload`) |
| 1859 / 2067 / 2074 | `EQT_EQE1_UPLOAD_AVAILABLE` / `#else` | 1537-2209 (`uploadCompressedETC1`) |
| 2083 / 2201 / 2207 | `EQT_EQE1_UPLOAD_AVAILABLE` / `#else` | 1537-2209 (`uploadCompressedV2`) |

**None of the four interior cut points lands inside a conditional.** Verify that
is still true at your tip.

⚠ **`EQT_EQE1_UPLOAD_AVAILABLE` is `#define`d in the preamble at line 23, inside
the `22/38` guard, and is read at 1859 and 2083 — both in
`constrained_texture_cache_compressed.cpp`.** The verbatim-preamble rule already
carries the `#define` into every new TU, so this works — **but it is exactly the
kind of thing a per-TU include trim would break.** Another reason §Process
forbids trimming.

## Includes

**Each new TU carries `constrained_texture_cache.cpp`'s full preamble, lines
1-41, verbatim** — the include block, the P03 comment, the
`#if defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)` region **with its `#define`,
its two `typedef`s and its `extern void* gles2WrapTexture` declaration**, and
the two namespace opens. **Not "the includes it actually needs":** §Process is
explicit, trimming per-TU include sets is a second and riskier change wearing a
move-only commit's clothes, and here it would additionally break
`EQT_EQE1_UPLOAD_AVAILABLE`.

Each new TU ends with the two namespace closes, `} // namespace Graphics` and
`} // namespace EQT`, copied from 2210-2211.

⚠ **This is what makes the verifier exit 1** (see §Measured). Predicted: **21
substantive added lines, 0 removed.**

## File-header comments — the one thing you may adapt

`constrained_texture_cache.cpp` has **no file-header comment today** — line 1 is
`#include`. Each new TU gets a **4-6 line header** placed *above* the copied
include block, naming its clusters and citing
`refactor/J10_constrained_texture_cache_split.md`. The stays file may gain one
too. Comment lines are allowlisted boilerplate for `verify_move_only.py`, so
this is permitted — but they **are** visible in the line-multiset check, so
**enumerate every header line you wrote in your Review**, exactly as J07 did.
**Change no other comment**, and in particular change nothing inside the `#if 0`
region.

The `constrained_texture_cache_upload_queue.cpp` header must say, in plain
words, that **~299 of its ~373 lines are a deliberately disabled `#if 0` region
preserved verbatim by J10, and that the live `isPending`/`hasPendingWork` are
the copies AFTER the `#endif`.** That sentence is the whole point of giving this
cluster its own TU.

## CMake — grep, never trust a line number

```bash
grep -n 'constrained_texture_cache\.cpp' CMakeLists.txt tests/CMakeLists.txt
```

At `f215651a` that is **TWELVE sites — nine in the root file, three in
`tests/CMakeLists.txt`**:

| file:line | target |
|---|---|
| `CMakeLists.txt:521` | `SET(GRAPHICS_SOURCES` (opens at `:432`, inside `IF(EQT_GRAPHICS)` at `:431`) |
| `CMakeLists.txt:804` | `ADD_EXECUTABLE(model_viewer` (`:788`) |
| `CMakeLists.txt:940` | `ADD_EXECUTABLE(s3d_dump` (`:925`) |
| `CMakeLists.txt:997` | `ADD_EXECUTABLE(entity_composite_builder` (`:982`) |
| `CMakeLists.txt:1179` | `ADD_EXECUTABLE(generate_surface_map` (`:1164`) |
| `CMakeLists.txt:1237` | `ADD_EXECUTABLE(zone_line_editor` (`:1222`) |
| `CMakeLists.txt:1645` | ⚠ **`ADD_EXECUTABLE(gles2_zone_benchmark` (`:1621`)** |
| `CMakeLists.txt:1766` | `ADD_EXECUTABLE(gles2_shader_diagnostic` (`:1748`) |
| `CMakeLists.txt:1849` | `ADD_EXECUTABLE(gles2_composite_audit` (`:1831`) |
| `tests/CMakeLists.txt:1271` | `ADD_EXECUTABLE(test_graphics` (`:1245`) |
| `tests/CMakeLists.txt:1348` | `ADD_EXECUTABLE(test_asset_loading` (`:1321`) |
| `tests/CMakeLists.txt:1442` | `ADD_EXECUTABLE(test_door_loading` (`:1423`) |

**Add all three new TUs beside the original at EVERY ONE of the twelve sites.**
`tests/CMakeLists.txt:2100` names `test_constrained_texture_cache.cpp` — that is
the *test* source, a different file; **do not touch it.**

⚠ **`gles2_zone_benchmark` is the binary `scripts/ci/visual_gate.sh` runs**, so a
missed add-site there surfaces as a **visual gate failure at the RENDER step**,
which an executor will misdiagnose as a behaviour change. Miss one of the other
eight and you get a link error naming an undefined
`ConstrainedTextureCache::…` — read the symbol, it tells you which TU is
missing.

⚠ **CMake commands in this project are UPPERCASE** — `grep -n 'set('` and
`grep -n 'add_executable'` both return nothing, silently, which reads exactly
like "no sites here". **Grep for the filename.** ⚠ **Add-site counts have ranged
from 1 to 19 across this batch** and the shape of the file gives no hint;
**report the count you actually found.**

⚠ **`tests/CMakeLists.txt` is ~2,850 lines — within ~150 of the 3,000 hard cap —
and this unit adds ~9 lines to it.** That WARN is **pre-existing and not yours**
(§Process grandfathers it via J20). **Do not "fix" it. Do report the new line
count**, because J20's urgency is set by it.

## Acceptance (the Batch J bar — every item, reported by EXIT CODE)

- **build clean**: `cmake --build build -j24`, classified by **EXIT CODE**,
  never by grepping the log. `grep ' error '` **never** matches GCC output,
  which prints `error:`. Capture `BUILD_EXIT=$?` immediately.
- ⚠ **After ANY exit-143 (SIGTERM) build, scan every produced binary for valid
  ELF magic BEFORE trusting the rebuild.** J07 hit this at scale: the timed-out
  build left **twelve binaries truncated mid-link**, the re-invocation **exited
  0**, and `ctest` then died at *discovery* — reporting only ONE bad binary per
  full suite run. Find them all at once:
  ```bash
  for f in build/bin/*; do
    [ -f "$f" ] || continue
    [ "$(head -c 4 "$f" | od -An -tx1 | tr -d ' ')" = "7f454c46" ] || echo "BAD $f"
  done
  ```
  then `rm` every hit and rebuild once. ⚠ **This unit touches twelve targets,
  nine of them tools and three of them test binaries — it is the Wave-3 unit
  most likely to trip the 600 s ceiling.** This is now also in
  `refactor/EXECUTOR_PROTOCOL.md`.
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`.
  - ⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known **LOAD-DEPENDENT FLAKE**, not a regression, and is **not** in
    `test_baseline.txt`. **Do not re-diagnose it.** If it fails in a wave run,
    re-run it alone before reporting anything.
  - ⚠ `ctest` **exit 8 during `GoogleTestAddTests.cmake` discovery is not
    necessarily the truncated-binary trap** (J05's correction 16): check the
    binary with `--gtest_list_tests` before concluding a mid-link truncation.
- ⚠ **the verifier — COMMIT FIRST, THEN VERIFY:**
  ```bash
  git -C /mnt/projects/fable/wt/<your slot> commit ...      # FIRST
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **There are THREE distinct ways to get a confident-looking wrong answer from
  this script, and two of them share a signature:**
  1. **Omitting `--repo`.** It defaults to `.`, and an executor agent's shell
     cwd is **not** its slot (this project forbids `cd` and agent threads reset
     cwd between calls). Omitting it silently verifies
     `/mnt/projects/fable/main` and prints a loud FAIL full of some unrelated
     merged unit's lines. J02 hit exactly this.
  2. **The wrong base rev.** `fable..HEAD` is **WRONG**: three units merge
     during a wave, so `fable` moves under you and `fable..HEAD` reports the
     *other* units' merged work, in reverse, as substantive removals. J01 saw
     533 spurious removals this way. Use the fork point.
     ⚠ **Failures 1 and 2 look IDENTICAL** — another unit's work appearing as
     removals — and have different causes. An executor who "fixes" the base and
     re-runs without `--repo` is still verifying the wrong tree. **Check both
     before changing a single line of code.**
  3. ⚠ **Running on an UNCOMMITTED tree.** The script prints
     **`No C++ files changed` and EXITS 0.** J06 hit this. It looks like a
     definitive pass and is not one. **Commit first, then verify.**
  Record `$BASE` in the Review and paste the full output.
- ⚠ **Expected verifier result for J10: EXIT 1, with exactly 21 substantive
  ADDED lines and 0 removed** — the seven non-boilerplate preamble lines
  (`typedef` ×4 physical lines, `extern void* gles2WrapTexture` ×3 physical
  lines) × 3 new TUs. **This is the §Process step-4 / correction-9 case and it
  is expected, not a failure.** Paste the full output, enumerate all 21, confirm
  each is a byte-identical copy of an existing line, and request orchestrator
  sign-off in the Review. **Any twenty-second line is a real finding — chase
  it.** **NEVER contort code to force a green exit** (J04 correctly refused to).
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines, so it cannot see a range duplicated into
  two TUs or dropped from both. This check can, because it counts occurrences —
  and it is what actually guards the `#if 0` region.
  ```bash
  # after: the four TU bodies, each stripped of its new 4-6 line header, of the
  # copied 41-line preamble, and of the 2 namespace closes.
  cat <the four stripped bodies> | sort > /tmp/after.txt
  git show "$BASE:src/client/graphics/constrained_texture_cache.cpp" \
    | sed -n '43,2209p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  This unit creates no cross-TU header, so J04's correction 7 does not apply —
  the diff should be **empty, with no exceptions at all**.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  four extracted ranges **in original order** (`43-808, 809-1131, 1132-1536,
  1537-2209`) and confirm the result is byte-identical to
  `git show "$BASE:…/constrained_texture_cache.cpp" | sed -n '43,2209p'`.
  **2,167 lines.** Expect `cmp` exit 0 with **no reverted lines at all.**
  ⚠ **Additionally and separately, prove the `#if 0` region survived byte for
  byte:**
  ```bash
  diff <(git show "$BASE:src/client/graphics/constrained_texture_cache.cpp" | sed -n '818,1120p') \
       <(sed -n '<its new range>p' src/client/graphics/constrained_texture_cache_upload_queue.cpp)
  ```
  **303 lines, exit 0.** Paste it. This is the one proof a reviewer will look
  for first.
  ⚠ **Declare count variables `local` in your proof helpers** (correction 17). A
  non-local `n` in J05's `body()` helper truncated one range and produced a
  247-line shortfall **that read exactly like a dropped range.** When a proof
  fails, suspect the proof first.
- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report** rather than re-blessing.
  - ⚠ **A gate FAIL is not necessarily a PIXEL fail — check WHICH metric
    failed.** J05's first run failed on `befallen` **p95 CPU 11.684 vs a 10.948
    ceiling with ZERO image mismatches**; the re-run measured 4.676. That is a
    2.5x swing on a 2x ceiling under three-wide load: **wave contention, not
    your change. Re-run.**
  - ⚠ **A failure at the RENDER step is a different animal and, for THIS unit,
    the likely one: it means the `gles2_zone_benchmark` add-site at
    `CMakeLists.txt:1645` was missed.** Do not re-run past it; go check the
    twelve sites.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ `build-arm-noble.sh` can blow the 600 s Bash-tool ceiling under wave
  concurrency — SIGTERM, **exit 143**, typically mid-`gles2_zone_benchmark`.
  **Exit 143 means RE-INVOKE, not breakage** — and then run the ELF scan above.
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**. `CMakeLists.txt` (~2,004) and
  `tests/CMakeLists.txt` (~2,850) both WARN; **both are pre-existing and neither
  is yours** (§Process, and J20).
- ⚠ **`chmod 644` every created file before `git add`.** A scripted `sed >` pass
  produces mode 755 and records a spurious `100644 => 100755`; verify with
  `git ls-files -s` that all new files are `100644`.

⚠ **Timing figures anywhere in the batch plan are WORST CASES OBSERVED, NOT
FORECASTS** (correction 14). J06's `ctest -j6` ran in 120.7 s and its
`build-arm-noble.sh` finished first time. Treat the stretch figures as "may
happen".

## Review must answer

1. **Line count at your tip** against the 2,211 this plan measured, and any
   range drift.
2. ⚠ **The `#if 0` proof**, pasted: the 303-line `diff` showing lines 818-1120
   arrived in `constrained_texture_cache_upload_queue.cpp` byte for byte, and an
   explicit statement that **both** `isPending`/`hasPendingWork` pairs still
   exist — the disabled ones at their new offsets, the live ones after the
   `#endif`. Quote the four signature lines.
3. **The preprocessor boundary check** on all four interior cut points, pasted,
   plus lines 1118-1136 quoted (the tightest boundary in the unit), plus
   confirmation that `grep -n '/\*'` is still empty.
4. **The `eqe1ArchiveStem` single-consumer grep**, pasted. If it has grown a
   second caller, that is a finding and the unit's shape changes.
5. **Every rewritten or created file-header comment line**, enumerated as the
   unit's only non-move change beyond the 21 predicted preamble copies.
6. **How many CMake add-sites you actually found**, per file, against the twelve
   this plan names — and the new `tests/CMakeLists.txt` line count.
7. **Both content proofs**, pasted. State the verifier's exit code, `$BASE`, and
   the enumerated exception list (predicted: 21 lines).
8. Final line counts of all four TUs, wall clock and slowest step, and whether
   concurrency with J08/J09 stretched the verification tail.
9. **Findings only, not fixes:**
   - the **299-line `#if 0` region** — the batch's seventh dead-content site,
     and the largest single one after `eq_ucs.cpp`. It is **13.5% of this
     file**. Record it against the batch's pending dead-content decision. Note
     which kind it is: **commented-out code, not an explanatory tombstone** —
     the T04 comment above it is the documentation; the 299 lines below are the
     debt.
   - the **19 `tests/` files that `#include
     "client/graphics/constrained_texture_cache.h"`** — all header includes,
     **zero read the .cpp as text**, so no repoint is needed. Confirm that held
     at your tip.
   - `docs/future/entity_texture_atlas.md:240` names
     `constrained_texture_cache.cpp::queueDecodedARGB()` as a thing to change —
     ⚠ **that method is INSIDE the `#if 0` region and has not existed since
     T04.** A prospective doc that is planning against dead code. Record it for
     the board; it is not this unit's to fix.
   - anything else that looks unreachable. **Record. Do not act.**

## Territory

`src/client/graphics/constrained_texture_cache.cpp`, the three new
`src/client/graphics/constrained_texture_cache_*.cpp` TUs beside it,
`CMakeLists.txt`, `tests/CMakeLists.txt`, and this file. **Nothing else** — not
`include/client/graphics/constrained_texture_cache.h`, not
`tests/test_constrained_texture_cache.cpp`, not any other `tests/*.cpp`, not
`src/client/graphics/eqe1_loader.*`, not `docs/`.

**Correction 10 pre-check, already run for you and to be re-run at your tip:**

```bash
grep -rn 'constrained_texture_cache' tests/ scripts/
```

At `f215651a` this returns **19 `#include` lines for the header plus 3 prose
comments naming `test_constrained_texture_cache.cpp`, and nothing from
`scripts/` — ZERO functional readers of the .cpp as text.** So unlike J11 —
where four tests read `irrlicht_renderer.cpp` as *text*, three of which would
have failed and one of which would have **silently asserted nothing** — **no
repoint is needed.** ⚠ **Re-run it anyway before cutting** and report what you
find; this is a standing pre-cut step, not an optional one.

⚠ **Naming:** the three new TUs are
`constrained_texture_cache_upload_queue.cpp`,
`constrained_texture_cache_eviction.cpp` and
`constrained_texture_cache_compressed.cpp`. Check nothing by those names exists
(`ls src/client/graphics/constrained_*`) before creating them.

Concurrent at dispatch: **J08** (`src/client/eq_entities.cpp` + new
`eq_entities_*.cpp` + `src/client/eq_group.cpp`, `CMakeLists.txt`) and **J09**
(`src/client/graphics/zone_shader.cpp` + three new private headers). No source
overlap with either. ⚠ **`CMakeLists.txt` is shared with both, and this unit is
the only one of the three that also edits `tests/CMakeLists.txt`** — which is
why **merges are serialized one at a time with a rebase.**
