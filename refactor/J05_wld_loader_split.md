# J05 — Batch J Wave 2: split wld_loader.cpp (2,908 → 7 TUs + 1 internal header, largest ~748)

**Slot: assigned at dispatch. Branch `task/J05-wld-loader-split` cut from
`fable`. x86 only. No Pi hardware. No eqemu.**

Wave 2 of `refactor/J_file_size_reclamation.md`, running alongside **J06** and
**J07**. Read, before editing anything: **`refactor/J_file_size_reclamation.md`
§Process per unit AND its two correction sections — "⚠ TEMPLATE CORRECTIONS
FROM J02" and "⚠ FURTHER CORRECTIONS FROM J04"** (eight hard-won fixes; they
win over anything older), then `refactor/J04_command_processor_split.md`
**including its Review** — J04 is the closest analogue to this unit because it
is the only other one that had to create an internal header, and its Review
records exactly what the verifier did about it. Also
`refactor/EXECUTOR_PROTOCOL.md` and `src/client/graphics/CLAUDE.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No change to
`include/client/graphics/eq/wld_loader.h`. **No method decomposition** — not
even `parseFragment36` (337 lines) or `parseFragment2C` (287 lines), both of
which are the obvious targets and both of which are explicitly out of scope
(`J_file_size_reclamation.md` §Optional). No drive-by fixes. No deletion of the
dead `#if 0` region (Hazard 3).

## ⚠ READ THIS FIRST — this unit CANNOT exit the verifier with 0

`read_val<T>` is a file-local `static inline` function template used **114
times**, in **every fragment parser in the file**, spread continuously from
line ~314 to line ~2447. There is no partition of this file into seven TUs in
which it stays file-local. It must be promoted to a private internal header, and
that promotion changes one signature line.

**`verify_move_only.py` exits 1 by construction when a unit does this** — its
`BOILERPLATE` allowlist covers `#include`, `#pragma`, `namespace`, `}` and
comments, but **not `inline`, not `using`, not `template`.** J04 hit this
identically and correctly refused to work around it (J04 Review, §"`verify_move_only.py`
— ⚠ NOT clean, and it CANNOT be under this plan").

So, for this unit:

- **Expect exit 1. That is the pass condition, not a failure.**
- **Paste the verifier's full output verbatim in your Review.**
- **Enumerate every single flagged line** and say what moved it.
- **Ask for orchestrator sign-off explicitly**, in those words.
- **Do NOT contort the code to force a green exit** — no putting `inline` on
  its own line, no re-wrapping the template, no rewriting call sites. An
  auditable two-line exception beats a verifier-shaped hack. J04 got this right;
  match it.

If your diff shows **more** than the lines predicted in Hazard 1 below, that is
a real finding — stop and report before proceeding.

## ⚠ AND READ THIS SECOND — a green test suite proves LESS here than anywhere else in the batch

`wld_loader.cpp` has **the worst mutation-test score in the repository.** It has
the most covered lines of any file (952), and the benchmark session measured it
killing **1 of 8** injected faults; `refactor/T10_wld_kill_rate.md` re-measured
independently at **0 of 8** and strengthened it to **4 of 8** (see also
`refactor/T22_kill_rate_sweep.md` §"Why this unit"). Half the injected faults in
this file still survive a full green suite.

**Consequence for J05: `ctest -j6` passing is necessary and nowhere near
sufficient.** The weight of proof here sits on the two mechanical proofs — the
**byte-identical re-concatenation** and the **line multiset** — because those
two are the only checks in the bar that would actually catch a dropped or
duplicated range in this file. Run both carefully, and state in your Review that
you understand the suite is not the net here.

## The split

**Measured at `ed97d701` (2026-09-02, `fable` tip after J01/J02/J04 merged):
2,908 lines.** Every range below was read out of the tree at that commit, and
every cut boundary was checked against comment and `#if` nesting (see Hazard 3).
**Re-verify every range against your own branch tip before you cut** and report
drift in your Review rather than silently adjusting.

**There are NO `// ====` banner comments anywhere in this file** — `grep -n
'^\s*//\s*=\{3,\}'` returns nothing. §Process's "take ranges from the leading
banner comment" rule therefore maps here to: **every range starts at the blank
line's successor, i.e. the function's own leading `//` comment if it has one at
file scope, otherwise its signature line.** All six interior cut points (471,
613, 1080, 1567, 1894, 2138) sit on a function signature line immediately
preceded by a blank line, verified. Keep the blank line with the *preceding*
cluster.

**Preamble is lines 1-14**: 10 `#include`s, a blank, `namespace EQT {`,
`namespace Graphics {`, a blank. The close is `} // namespace Graphics` /
`} // namespace EQT` at **2628-2629**. Both replicate into every new TU.

| Range | Cluster | Target | ≈ lines |
|---|---|---|---|
| 15-22 | `// Safe unaligned read helper…` comment + `template<typename T> static inline T read_val(const void*)` | **`src/client/graphics/eq/wld_internal.h`** (NEW — Hazard 1) | 8 moved |
| 23-470 **+ 2623-2908** | `HASH_KEY`, `decodeStringHash`, `getMemoryUsage`, `logMemoryBreakdown`, `releasePostLoadData`, `parseFromArchive`, `parseFromOpenArchive`, `parseWldBuffer` (the top-level entry points and the fragment dispatcher) — **plus** the trailing `// BspTree implementation` comments, the namespace close, and the whole `#if 0` region (Hazard 3) | **stays** `wld_loader.cpp` | **~748** |
| 471-612 | `parseFragment03` (bitmap name), `04` (texture), `05` (texture ref), `30` (material), `31` (material list) | `wld_fragments_material.cpp` | ~158 |
| 613-1079 | `parseFragment36` (mesh, 337 lines), `getCombinedGeometry`, `getGeometryForRegion`, `hasPvsData` | `wld_fragments_mesh.cpp` | ~483 |
| 1080-1566 | `parseFragment14` (actor def), `15` (actor instance), `2C` (alternate mesh, 287 lines), `2D` (mesh reference) | `wld_fragments_actor.cpp` | ~503 |
| 1567-1893 | `parseFragment10` (skeleton track def), `11`, `12` (track def), `13` (track ref) | `wld_fragments_skeleton.cpp` | ~343 |
| 1894-2137 | `parseFragment1B` (light def), `1C`, `28` (point light), `2A` (ambient), `35` (global ambient), `2F` (animated-vertex ref) | `wld_fragments_light.cpp` | ~260 |
| 2138-2622 | `parseFragment37` (DMTRACKDEF), the file-local `static decodeRlePvs`, `parseFragment21` (BSP tree), `22` (BSP region), `29` (region flag), `decodeZoneLineString` | `wld_fragments_bsp.cpp` | ~502 |

**Result: 7 TUs + 1 header.** Every one under 1,500; largest is the surviving
`wld_loader.cpp` at ~748 (of which 286 lines are the dead `#if 0` block).

Arithmetic that must hold: `14 + 8 + 448 + 142 + 467 + 487 + 327 + 244 + 485 + 286 = 2908`.
If your own numbers do not sum to the file's line count, you have a gap or an
overlap — find it before you build.

**The six TU names are deliberate**: `tests/test_wld*` is already split along
exactly these lines (`test_wld_fragments_mesh.cpp`,
`test_wld_fragments_skeleton.cpp`, `test_wld_fragments_light.cpp`,
`test_wld_fragments_bsp.cpp`). Keep the names as given.

## ⚠ HAZARD 1 — `read_val<T>`: the internal header, and exactly what it costs

`src/client/graphics/eq/wld_loader.cpp:15-22`:

```cpp
// Safe unaligned read helper - uses memcpy to avoid bus errors on ARM
template<typename T>
static inline T read_val(const void* ptr) {
    T val;
    std::memcpy(&val, ptr, sizeof(T));
    return val;
}
```

114 call sites, distributed across every cluster in the table (measured by
200-line bucket: 3 / 9 / 9 / 11 / 16 / 2 / 13 / 12 / 13 / 21 / 5). All seven TUs
use it.

**Resolution — the §Process step 4 sanctioned Batch-F exception** (the live
precedent is J04's `src/client/action/command_processor_internal.h`, which is
the only internal header that currently exists in this tree):

1. Create **`src/client/graphics/eq/wld_internal.h`**, `#pragma once`, with
   `#include <cstring>` (for `std::memcpy`) and the two namespace opens
   **`namespace EQT { namespace Graphics {`** — *the same namespace the function
   lives in today*.
2. Move lines 15-22 into it **verbatim except for dropping `static`**:
   `template<typename T> inline T read_val(const void* ptr) {`. `static` gives
   internal linkage; in a header that would silently give each TU its own copy.
   `inline` on a function template is redundant but harmless and is what makes
   the intent legible.
3. **Do NOT put it in a `detail` namespace, and do NOT add any `using`
   declaration.** J04 used `namespace detail` + a `using` per consuming TU
   because its helpers were non-template free functions; here, keeping
   `read_val` in `EQT::Graphics` means **all 114 call sites are textually
   untouched and zero `using` lines are added.** That is strictly better and it
   halves the verifier's exception list. Take it.
4. Include it from **all seven** TUs, as
   `#include "client/graphics/eq/wld_internal.h"` — `${CMAKE_SOURCE_DIR}/src` is
   on the include path (`CMakeLists.txt:193-203`), which is how J04's header
   resolves.
5. **Declare this in your Review as the sanctioned exception**, naming the
   symbol, all seven consuming TUs, and why duplicating a template into seven
   files was rejected.

**Predicted verifier exception: exactly 1 added line and 1 removed line.**

```
+  inline T read_val(const void* ptr) {
-  static inline T read_val(const void* ptr) {
```

Anything beyond that is drift you must explain.

**`HASH_KEY` and `decodeStringHash` do NOT need promoting — the survey text is
overbroad on this point and you should not act on it.** Verified at `ed97d701`:

- `void decodeStringHash(char*, size_t)` is **already declared in the public
  header** at `include/client/graphics/eq/wld_loader.h:720`. It has external
  linkage today. It needs nothing.
- `static const uint8_t HASH_KEY[]` (`:24`) has **exactly one reader in the
  entire tree**: `wld_loader.cpp:28`, inside `decodeStringHash` itself.

So both travel together, as one contiguous block, into the stays-file, and
neither goes into `wld_internal.h`. Do not put them there "for symmetry" — every
symbol in that header is an exception the orchestrator has to sign off on
individually.

## ⚠ HAZARD 2 — `decodeRlePvs` is a second file-local static, and it does NOT need promoting

`static std::vector<bool> decodeRlePvs(const char*, int16_t, uint32_t)` at
**`:2225-2285`**. Sole caller: **`:2404`**, inside `parseFragment22`. Both are
inside the `2138-2622` cluster, so it travels to `wld_fragments_bsp.cpp` for
free and stays `static`.

**Confirm this yourself before cutting** (`grep -n 'decodeRlePvs'` → two hits,
2225 and 2404). If a caller has appeared elsewhere at your tip, that is a second
promotion and a second sign-off request — say so, do not quietly add it.

Those two plus `read_val` and `HASH_KEY` are the file's **only** file-scope
statics; there are **no anonymous namespaces** in this file (`grep -n
'^namespace {'` → empty). The two `static int` counters at `:2796` and `:2869`
are function-local and inside the dead `#if 0` block.

## ⚠ HAZARD 3 — 286 lines of dead `#if 0` code sit OUTSIDE the namespace close

This is the boundary that will break your build if you take a naive range.

```
2622   }                         <- end of decodeZoneLineString
2623   (blank)
2624   // BspTree implementation
2625   (blank)
2626   // D20f1: BspTree method implementations moved to src/client/zone_bsp.cpp
2627   (blank)
2628   } // namespace Graphics
2629   } // namespace EQT
2630   (blank)
2631   #if 0  // D20f1: removed — implementations now in src/client/zone_bsp.cpp
2632   namespace removed {
...
2906   } // removed
2907   (blank, or #endif's predecessor)
2908   #endif // D20f1 removed code
```

Three rules, all mandatory:

1. **The `#if 0 … #endif` region 2631-2908 is a single indivisible block** and
   it lives **after** the namespace closes. It is not inside `EQT::Graphics`.
2. **It stays in `wld_loader.cpp`, untouched, exactly where it is.** Do not move
   it into `wld_fragments_bsp.cpp` even though it is BspTree code. Leaving it
   means the stays-file's entire tail (2623-2908) is byte-for-byte unchanged,
   which is the lowest-risk option available and makes your re-concatenation
   proof trivially checkable at that end.
3. **Do not delete it, and do not "clean it up".** Deleting 286 lines of dead
   code is not move-only. See §Findings below — record it, do not act.

This is the J02 lesson applied preemptively: J02's plan cut a range *inside* a
`/* */` block and would not have compiled. **Before you cut, run the boundary
check on every one of your six interior cut points and paste the result:**

```bash
awk 'NR>=1 && NR<=2908 && /^\s*(#if|#ifdef|#ifndef|#else|#elif|#endif)/ {print NR": "$0}' \
    src/client/graphics/eq/wld_loader.cpp
```

At `ed97d701` this file has **no preprocessor conditionals at all** below the
includes except the 2631/2908 pair — so every interior cut is safe. Verify that
is still true at your tip. Also confirm `grep -n '/\*' wld_loader.cpp` is still
empty (it is at `ed97d701` — no block comments anywhere).

## Includes

**Each of the seven TUs carries `wld_loader.cpp`'s full 10-include block
(lines 1-10), verbatim**, plus `namespace EQT {` / `namespace Graphics {` and
the matching two-line close. **Not "the includes it actually needs"** — §Process
is explicit, and trimming per-TU include sets is a second, riskier change
wearing a move-only commit's clothes. Add `#include
"client/graphics/eq/wld_internal.h"` to all seven, after the existing block.

## CMake — ⚠ NINETEEN add-sites. This is the real work of J05.

**This is by far the most CMake-exposed file in Batch J** — J04 had five, J06
and J07 have one each. `wld_loader.cpp` is compiled directly into fourteen tool
and benchmark targets and four test targets, not merely linked via
`willeq_core`. **Missing one produces undefined-symbol link errors in a single
target, which is easy to misread as a bad split.**

Find them yourself — do not trust these line numbers, they rot within hours:

```bash
grep -n 'graphics/eq/wld_loader\.cpp' CMakeLists.txt tests/CMakeLists.txt
```

At `ed97d701` that is **15 sites in `CMakeLists.txt` and 4 in
`tests/CMakeLists.txt`**:

| file | line | context |
|---|---|---|
| `CMakeLists.txt` | 319 | `SET(CLIENT_CORE_SOURCES` (opens at `:265`) — the one that matters for `willeq_core` |
| `CMakeLists.txt` | 769 | `ADD_EXECUTABLE(model_viewer` |
| `CMakeLists.txt` | 899 | `ADD_EXECUTABLE(s3d_dump` |
| `CMakeLists.txt` | 950 | `ADD_EXECUTABLE(entity_composite_builder` |
| `CMakeLists.txt` | 1024 | `ADD_EXECUTABLE(wld_dump` |
| `CMakeLists.txt` | 1041 | `ADD_EXECUTABLE(bsp_region_finder` |
| `CMakeLists.txt` | 1057 | `ADD_EXECUTABLE(zone_region_classifier` |
| `CMakeLists.txt` | 1079 | `ADD_EXECUTABLE(region_poly_audit` |
| `CMakeLists.txt` | 1102 | `ADD_EXECUTABLE(generate_surface_map` |
| `CMakeLists.txt` | 1154 | `ADD_EXECUTABLE(zone_line_editor` |
| `CMakeLists.txt` | 1251 | `ADD_EXECUTABLE(equipment_index_builder` |
| `CMakeLists.txt` | 1360 | `ADD_EXECUTABLE(atlas_uv_audit` |
| `CMakeLists.txt` | 1545 | **`ADD_EXECUTABLE(gles2_zone_benchmark`** ⚠ |
| `CMakeLists.txt` | 1660 | `ADD_EXECUTABLE(gles2_shader_diagnostic` |
| `CMakeLists.txt` | 1737 | `ADD_EXECUTABLE(gles2_composite_audit` |
| `CMakeLists.txt` | 427 | **a COMMENT only** — `# EQ file loaders (pfs.cpp/wld_loader.cpp moved to CLIENT_CORE_SOURCES — P28…)`. Do not edit it. |
| `tests/CMakeLists.txt` | 1203 | `ADD_EXECUTABLE(test_wld` |
| `tests/CMakeLists.txt` | 1237 | `ADD_EXECUTABLE(test_graphics` |
| `tests/CMakeLists.txt` | 1296 | `ADD_EXECUTABLE(test_asset_loading` |
| `tests/CMakeLists.txt` | 1392 | `ADD_EXECUTABLE(test_door_loading` |

⚠ **`gles2_zone_benchmark` (`CMakeLists.txt:1545`) is the binary
`scripts/ci/visual_gate.sh` runs.** If you miss that one site, the incremental
`cmake --build build` may still succeed and the **visual gate is what will
fail** — you would then be debugging a "behaviour change" that is actually a
link error. Check it first.

⚠ **The CMake commands in this project are UPPERCASE** (`ADD_EXECUTABLE`,
`SET`, `IF`). `grep -n 'add_executable'` returns nothing. Grep for the
**filename**, as instructed above, not for the command.

At every one of the 19 real sites, `wld_loader.cpp` sits in a group with
`pfs.cpp` / `zone_bsp.cpp` / `s3d_loader.cpp`. **Add the six new TUs to that
same group at every site.** Many of these targets are inside
`IF(EQT_GRAPHICS …)` guards — add inside the guard, beside the original.

`tests/CMakeLists.txt` is **2,778 lines** and every J unit grows it; a
`check_file_sizes.sh` WARN there is pre-existing and not yours (see §Process
and the J20 unit now boarded in the batch plan).

## Acceptance (the Batch J bar — every item, reported by EXIT CODE)

- **build clean**: `cmake --build build -j24`, classified by **EXIT CODE**,
  never by grepping the log. `grep ' error '` **never** matches GCC output,
  which prints `error:`. Capture `BUILD_EXIT=$?` immediately.
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`. **`test_wld`,
  `test_graphics`, `test_asset_loading` and `test_door_loading` are this unit's
  real net** — confirm all four *built and ran*, not merely that the suite total
  held. (And re-read §"a green suite proves less here" — they are a weak net.)
  - ⚠ **`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known LOAD-DEPENDENT FLAKE**, not a regression: a 5 s subprocess-timing test
    that captures empty output under multi-slot contention and passes 5/5 in
    isolation on the same binary. It is **not** in `test_baseline.txt`. **Do not
    re-diagnose it. If it fails in a wave run, re-run it alone before reporting
    anything.**
- ⚠ **the verifier, with `--repo`:**
  ```bash
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **Both parts of that line are load-bearing and each has its own failure mode
  that looks identical to the other's:**
  - **`--repo` is mandatory.** It defaults to `.`, and an executor agent's shell
    cwd is **not** its slot (this project forbids `cd` and agent threads reset
    cwd between calls). Omitting it silently verifies `/mnt/projects/fable/main`
    and prints a loud FAIL full of some unrelated merged unit's lines. J02 hit
    exactly this.
  - **`fable..HEAD` is WRONG.** Three units merge during a wave, so `fable`
    moves under you; `fable..HEAD` reports the *other* units' merged work, in
    reverse, as substantive removals. J01 saw 533 spurious removals this way.
  - ⚠ **These two failures have the SAME SIGNATURE — another unit's work
    appearing as removals — and different causes.** An executor who "fixes" the
    base and re-runs without `--repo` is still verifying the wrong tree. If the
    verifier flags files your unit never touched, check **both** before changing
    a single line of code.
  - Record `$BASE` in the Review, and see §"this unit CANNOT exit with 0": **the
    expected result is exit 1 with exactly the two `read_val` signature lines.**
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines, so it cannot see a range duplicated into
  two TUs or dropped from both. This check can, because it counts occurrences.
  ```bash
  # `after`: the seven TU bodies (each stripped of the 14-line preamble and the
  # 2-line namespace close) PLUS wld_internal.h's 8 moved lines.
  cat <the seven stripped bodies> src/client/graphics/eq/wld_internal.h.moved-part | sort > /tmp/after.txt
  git show "$BASE:src/client/graphics/eq/wld_loader.cpp" | sed -n '15,2908p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **The header convention (J04 correction 7): `wld_internal.h`'s 8 moved lines
  MUST be counted into the `after` multiset.** A naive "cat the seven .cpp
  bodies" **silently under-counts by 8 and still looks clean.** State the
  header's exact line contribution in your Review. The only permitted delta is
  the 1-added/1-removed `read_val` signature line of Hazard 1 — enumerate it.
- **byte-identical re-concatenation** — stronger and also cheap, and given this
  file's mutation score it is the proof that carries the weight. Re-insert
  `wld_internal.h`'s moved lines at their original offset (15-22), revert the
  one `static` keyword, concatenate the extracted ranges **in original order**
  (`15-22, 23-470, 471-612, 613-1079, 1080-1566, 1567-1893, 1894-2137,
  2138-2622, 2623-2908`) and confirm the result is byte-identical to
  `git show "$BASE:src/client/graphics/eq/wld_loader.cpp" | sed -n '15,2908p'`.
  Paste the result. **2,894 lines.**
- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report** rather than re-blessing. (And see the `gles2_zone_benchmark`
  CMake warning above: a gate failure here is more likely a missed add-site than
  a behaviour change.)
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ **`build-arm-noble.sh` routinely blows the 600 s Bash-tool ceiling under
  wave concurrency** — SIGTERM, **exit 143**, typically mid-`gles2_zone_benchmark`.
  **Exit 143 means RE-INVOKE, not breakage.** The incremental re-run finishes.
  Expect to call it twice; J04 did.
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**. `CMakeLists.txt` (1,909) and
  `tests/CMakeLists.txt` (2,778) both WARN; **both are pre-existing and neither
  is yours** (§Process, and J20).
- ⚠ **`chmod 644` every created file before `git add`.** A scripted `sed >` pass
  produces mode 755 and records a spurious `100644 => 100755`; verify with
  `git ls-files -s` that all eight new files are `100644`.

## Review must answer

1. **The internal header, as the sanctioned §Process step-4 exception:** the
   symbol, all seven consuming TUs, the verifier's full output pasted verbatim,
   every flagged line enumerated, and an explicit request for orchestrator
   sign-off. Confirm you did **not** contort the code to force a green exit.
2. **Was the survey's internal-header claim right?** It asserted `read_val<T>`
   *and* `HASH_KEY`/`decodeStringHash`. This plan says only `read_val<T>`
   qualifies. Confirm or refute at your tip, with the grep output.
3. **The preprocessor/comment boundary check** on all six interior cut points,
   pasted — the J02 lesson. Any range you had to move, and why.
4. **How many CMake add-sites you actually found**, per file, against the 19
   this plan names. Name any target that had gained or lost the file.
5. **Both content proofs**, pasted: multiset (with the header's line
   contribution stated) and byte-identical re-concatenation.
6. Line-range drift vs this plan, final line counts of all seven TUs and the
   header, the verifier base rev, wall clock and slowest step, and whether
   concurrency with J06/J07 stretched the verification tail.
7. **Findings only, not fixes:** the 286-line dead `#if 0` region (J02's
   `eq_ucs.cpp` has the same shape — 320 lines containing zero code — and the
   batch owes a decision on both); `parseFragment36` at 337 lines and
   `parseFragment2C` at 287 as Batch-G candidates; and `resolveTextureName`,
   which `refactor/T10_wld_kill_rate.md:281` records as dead code. **Record all
   of these. Act on none of them.**

## Territory

`src/client/graphics/eq/wld_loader.cpp`, the six new
`src/client/graphics/eq/wld_fragments_*.cpp` TUs and
`src/client/graphics/eq/wld_internal.h` beside them, `CMakeLists.txt`,
`tests/CMakeLists.txt`, this file. **Nothing else** — not
`include/client/graphics/eq/wld_loader.h`, not `pfs.cpp`, not `zone_bsp.cpp`,
not `s3d_loader.cpp`, not `zone_geometry.cpp`, not any `tests/test_wld*.cpp`,
not `docs/module_map.md`, not `src/client/graphics/CLAUDE.md` (both will need
entries for the six new TUs — that is a flagged follow-up, not this unit).

Concurrent at dispatch: **J06** (`src/client/application.cpp` + new
`application_*.cpp`, `CMakeLists.txt`) and **J07** (`src/client/eq_player_info.cpp`
+ new `eq_player_*.cpp`, `CMakeLists.txt`). No source overlap with either.
⚠ **`CMakeLists.txt` is shared with both** and `tests/CMakeLists.txt` with any
in-flight T unit — which is why **merges are serialized one at a time with a
rebase.**

---

## Review (slot2, 2026-09-02, branch `task/J05-wld-loader-split`)

**Done exactly as specified. Move-only. ZERO line-range drift** — all six
interior cut points, the preamble boundary, the namespace close and the `#if 0`
region were byte-correct at my tip. Every range in the cluster table applied
verbatim.

Branch cut from `fable` at **`6e3a5b48`** ("T322 plan: match the official
client's zone-transition connection lifecycle so the group survives"), not the
`ed97d701` the plan measured at. `wld_loader.cpp` was **2,908 lines** at my tip,
unchanged, so no re-measurement was needed.

**I understand the suite is not the net here.** `wld_loader.cpp` has the worst
mutation score in the repo (952 covered lines, 1-of-8 / 4-of-8 kill rate). The
weight of this unit's proof sits on §5's two mechanical proofs, not on
`ctest`. Both are pasted below and both are clean; the byte-identical
re-concatenation in particular reproduces the base file exactly.

### Result — final line counts

| file | lines | original range |
|---|---|---|
| `wld_loader.cpp` | **749** | 23-470 + 2623-2908 |
| `wld_fragments_material.cpp` | **159** | 471-612 |
| `wld_fragments_mesh.cpp` | **484** | 613-1079 |
| `wld_fragments_actor.cpp` | **504** | 1080-1566 |
| `wld_fragments_skeleton.cpp` | **344** | 1567-1893 |
| `wld_fragments_light.cpp` | **261** | 1894-2137 |
| `wld_fragments_bsp.cpp` | **502** | 2138-2622 |
| `wld_internal.h` | **18** | 15-22 (8 moved lines) |

2,908 → **3,021** across 7 TUs + 1 header. The +113 is six extra copies of the
15-line preamble, six namespace closes, the header's own 10 lines of
guard/include/namespace scaffolding, and the trailing blank each close carries.
Every file is well under the 1,500 soft limit; the largest is the surviving
`wld_loader.cpp` at 749, of which 286 lines are the dead `#if 0` block.
`CMakeLists.txt` 1,914→2,004, `tests/CMakeLists.txt` 2,826→2,850.

### 1. The internal header — the sanctioned §Process step-4 exception

**Symbol: exactly one.** `template<typename T> inline T read_val(const void*)`,
moved verbatim from `wld_loader.cpp:15-22` into the new
**`src/client/graphics/eq/wld_internal.h`** (`#pragma once`, `#include
<cstring>`, `namespace EQT { namespace Graphics {`). **The only edit to the
moved text is dropping `static` from the signature line.**

**Consuming TUs — all seven**, each carrying `#include
"client/graphics/eq/wld_internal.h"` appended to the verbatim 10-line include
block: `wld_loader.cpp`, `wld_fragments_material.cpp`, `wld_fragments_mesh.cpp`,
`wld_fragments_actor.cpp`, `wld_fragments_skeleton.cpp`,
`wld_fragments_light.cpp`, `wld_fragments_bsp.cpp`.

**The plan's shape recommendation (correction 10) is correct and was taken.**
Keeping `read_val` in `EQT::Graphics` rather than J04's `namespace detail` +
`using` means **all 114 call sites are textually untouched, zero `using` lines
were added, and the exception list is one line instead of J04's eight.**

**Why duplicating the template into seven files was rejected:** seven
independent `static` definitions of an unaligned-read helper is a
silently-divergent-by-construction hazard the compiler cannot diagnose (internal
linkage makes each copy legal), on a function whose entire purpose is to be
bus-error-safe on ARM — exactly the kind of helper where one copy getting a
"fix" and six not is a platform-specific corruption bug. It is also 7×8 = 56
lines of pure duplication for zero benefit.

**Verifier — full output, verbatim.** Base rev recorded:
`BASE=6e3a5b48640611b8e0c0f3805d54b2e88e2655f5` (`git merge-base fable HEAD`),
run with `--repo /mnt/projects/fable/wt/slot2`:

```
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
  tests/CMakeLists.txt

Files verified: 8
Boilerplate: +97 / -0 lines (allowed)

SUBSTANTIVE ADDED lines (1):
  +1x  inline T read_val(const void* ptr) {

SUBSTANTIVE REMOVED lines (1):
  -1x  static inline T read_val(const void* ptr) {

FAIL: substantive line changes detected. If intentional (e.g. de-static-ing a
helper), document in the unit review section for orchestrator sign-off.
```

`VERIFY_EXIT=1`, **exactly the 1-added / 1-removed pair the plan predicted, and
nothing else.** Boilerplate +97/−0 is the six replicated preambles, seven
`#include "wld_internal.h"` lines, the namespace closes and the header's guard.

**Every flagged line, enumerated:**

| line | direction | what moved it |
|---|---|---|
| `inline T read_val(const void* ptr) {` | added | the signature as it now reads in `wld_internal.h:10`. `static` gives internal linkage; left in a header it would give each of the seven TUs its own copy of the template, which is the bug the promotion exists to avoid. `inline` is redundant on a function template but is what makes the intent legible at the definition site. |
| `static inline T read_val(const void* ptr) {` | removed | the same line as it read at `wld_loader.cpp:17` before the move. |

**These two lines are the same line.** No other line in the 3,021 lines of moved
code differs from the original by a single byte — proven independently in §5.

**⚠ Orchestrator sign-off is explicitly requested for this one-line
de-static-ing.** Please record it in the merge commit.

**Confirmed: I did NOT contort the code to force a green exit.** No re-wrapping
of the template, no putting `inline` on its own line, no `detail` namespace, no
`using` declarations, no rewritten call sites.

### 2. Was the survey's internal-header claim right? — REFUTED, the plan is right

The survey claimed `read_val<T>` **and** `HASH_KEY`/`decodeStringHash`. Only
`read_val<T>` qualifies. Measured at `6e3a5b48`:

```
$ grep -c 'read_val' src/client/graphics/eq/wld_loader.cpp
115                          # 1 definition + 114 call sites

$ grep -n 'decodeStringHash' include/client/graphics/eq/wld_loader.h
720:void decodeStringHash(char* str, size_t len);

$ grep -rn 'HASH_KEY' src include
src/client/graphics/eq/wld_loader.cpp:17:static const uint8_t HASH_KEY[] = { 0x95, ... };
src/client/graphics/eq/wld_loader.cpp:21:        str[i] ^= HASH_KEY[i % 8];
```

`decodeStringHash` is **already declared in the public header** and has external
linkage today — it needs nothing. `HASH_KEY` has **exactly one reader in the
whole tree**, at `:21`, inside `decodeStringHash` itself. Both travelled
together as one contiguous block into the stays-file and **neither went into
`wld_internal.h`.** (Line numbers are the post-split file; they were `:24`/`:28`
pre-split.)

**Hazard 2 confirmed unchanged:** `grep -n 'decodeRlePvs'` → exactly two hits,
`2225` (the `static` definition) and `2404` (its sole caller, inside
`parseFragment22`). Both inside the 2138-2622 cluster, so it travelled to
`wld_fragments_bsp.cpp` for free and **stays `static`. No second promotion, no
second sign-off request.**

### 3. The preprocessor / comment boundary check — the J02 lesson

```
$ awk 'NR>=1 && NR<=2908 && /^\s*(#if|#ifdef|#ifndef|#else|#elif|#endif)/ {print NR": "$0}' \
      src/client/graphics/eq/wld_loader.cpp
2631: #if 0  // D20f1: removed — implementations now in src/client/zone_bsp.cpp
2908: #endif // D20f1 removed code

$ grep -n '/\*' src/client/graphics/eq/wld_loader.cpp
(no output)

$ grep -n '^namespace {' src/client/graphics/eq/wld_loader.cpp
(no output)
```

**No block comments anywhere in the file, no anonymous namespaces, and the only
preprocessor conditionals are the 2631/2908 pair** — so comment depth and `#if`
depth are both **0 at every one of the six interior cut points**, verified
individually. Each of the six also matches the plan's stated shape exactly — a
`WldLoader::parseFragmentXX` signature line whose predecessor is a blank line
whose own predecessor is a `}` closing the previous method:

```
468:     return result;                 |  1077:     return false;
469: }                                  |  1078: }
470:                                    |  1079:
471: void WldLoader::parseFragment03(   |  1080: void WldLoader::parseFragment14(

610:     lastBrushSetFragIndex_ = ...    |  1564:     modelRefs_[fragIndex] = modelRef;
611: }                                  |  1565: }
612:                                    |  1566:
613: void WldLoader::parseFragment36(   |  1567: void WldLoader::parseFragment10(

1891:     }                             |  2135:     }
1892: }                                 |  2136: }
1893:                                   |  2137:
1894: void WldLoader::parseFragment1B(  |  2138: void WldLoader::parseFragment37(
```

**No range had to be moved.** The blank line was kept with the preceding
cluster in every case, as the plan directs.

**Hazard 3 confirmed and honoured.** The tail is exactly as the plan drew it:
`decodeZoneLineString` closes at 2622, the two `// BspTree` comments and the
namespace closes run 2623-2629, and the indivisible `#if 0 … #endif` block runs
2631-2908 **outside** `EQT::Graphics`. **All of 2623-2908 stayed in
`wld_loader.cpp` byte-for-byte untouched** — nothing was moved into
`wld_fragments_bsp.cpp`, nothing deleted, nothing cleaned up.

### 4. CMake add-sites — 19 found, 19 expected, exact match

```
$ grep -n 'graphics/eq/wld_loader\.cpp' CMakeLists.txt tests/CMakeLists.txt | wc -l
19
```

**15 in `CMakeLists.txt`, 4 in `tests/CMakeLists.txt`.** Every target the plan
named was present and **no target had gained or lost the file** since
`ed97d701`. Line numbers had all shifted by +4/+5/+16 (J02/J04/J06/J07 churn),
which is exactly why the plan says to grep rather than trust them. Post-split
site counts, all six new TUs added inside the same group and inside any
`IF(EQT_GRAPHICS …)` guard:

| file | sites | verification |
|---|---|---|
| `CMakeLists.txt` | 15 | `grep -c 'wld_fragments_'` → **90** = 15 × 6 ✅ |
| `tests/CMakeLists.txt` | 4 | `grep -c 'wld_fragments_'` → **28** = 4 × 6 + 4 pre-existing `test_wld_fragments_*.cpp` test-source lines ✅ |

The `CMakeLists.txt:427` comment (`# EQ file loaders (pfs.cpp/wld_loader.cpp
moved to CLIENT_CORE_SOURCES — P28…)`) does not match the path-qualified grep
and **was not edited**, as instructed.

⚠ **`gles2_zone_benchmark` was checked first**, as the plan warns. It is the
visual-gate binary and it links `wld_loader.cpp` directly; the gate passed on a
clean `build-gles2` tree (§below), which is positive evidence that site landed.
**The plan's warning about this is well-placed and should stay in the
template** — see §7.

### 5. Both content proofs

**Multiset — with the header's contribution stated explicitly.** The `after`
multiset is the seven TU bodies (each stripped of its 15-line preamble and
2-line namespace close) **plus `wld_internal.h`'s 8 moved lines** (`sed -n
'8,15p' wld_internal.h` — the leading `//` comment through the closing `}` and
its trailing blank; **the header contributes exactly 8 lines, and omitting them
would under-count silently, J04 correction 7**). `before` is `git show
"$BASE:…/wld_loader.cpp" | sed -n '15,2908p'`.

```
$ diff <(sort before.txt) <(sort after.txt)
1833a1834
> inline T read_val(const void* ptr) {
2533d2533
< static inline T read_val(const void* ptr) {
```

**The only delta is the 1-added / 1-removed Hazard-1 signature line.** Every
other one of the 2,894 lines occurs the same number of times before and after —
nothing dropped, nothing duplicated.

**Byte-identical re-concatenation** — the stronger proof, and the one that
carries the weight in this file. `wld_internal.h`'s 8 moved lines re-inserted at
their original offset with `static` restored, then the extracted ranges
concatenated in original order (`15-22, 23-470, 471-612, 613-1079, 1080-1566,
1567-1893, 1894-2137, 2138-2622, 2623-2908`):

```
before lines: 2894  reconcat lines: 2894
$ cmp before.txt reconcat.txt ; echo $?
0
$ diff <(sort before.txt) <(sort reconcat.txt) ; echo $?
0
```

**2,894 lines, `cmp` exit 0 — byte-for-byte identical to the base file.** With
the one signature keyword reverted, the split is a pure partition: the seven TUs
plus the header reconstruct `wld_loader.cpp` exactly.

⚠ **One process note worth recording.** My first proof run reported a 247-line
shortfall. That was a **bug in the proof harness, not in the split** — a shell
helper function used an unlocalised loop variable `n` that clobbered the caller's
line count, truncating the `#if 0` tail from 286 lines to 39. **A failing
proof's first suspect should be the proof**; the failure signature (a
contiguous tail of one range missing, with every other range intact) is
diagnostic. Fixed with `local`; both proofs then passed first time. Worth a
template line: **declare loop/count variables `local` in any proof helper.**

### 6. Acceptance bar — every item, by exit code

| step | command | result |
|---|---|---|
| build | `cmake --build build -j24` | **`BUILD_EXIT=0`** |
| suite | `ctest --test-dir build -j6 --output-on-failure` | **3,498 / 3,499**, sole failure `AudioManagerTest.PreloadCommonSounds` (the known ALSA host exception). 518.85 s |
| verifier | `verify_move_only.py "$BASE..HEAD" --repo <slot2>` | **exit 1 by construction** — the two `read_val` lines, §1 |
| multiset | see §5 | **1 added / 1 removed, both the same signature line** |
| byte-identical | see §5 | **`cmp` exit 0, 2,894 lines** |
| visual gate | `scripts/ci/visual_gate.sh` | **PASS**, `git status --porcelain` **empty** — goldens untouched |
| gles2 | `scripts/build-gles2-x86.sh` | **exit 0** |
| ARM | `scripts/build-arm-noble.sh` | **exit 0, first invocation** |
| sizes | `check_file_sizes.sh --files <11 files>` | **9 ok, 2 warn, 0 fail** |

**`test_wld`, `test_graphics`, `test_asset_loading` and `test_door_loading` — this
unit's real net — all four built and RAN**, confirmed by running each binary
directly rather than by watching the suite total:

```
test_wld           EXIT=0  103 tests from 11 suites ran, 103 PASSED (248.5 s)
test_graphics      EXIT=0   63 tests from 10 suites ran,  63 PASSED (4.5 s)
test_asset_loading EXIT=0   14 tests from  7 suites ran,  14 PASSED (70.6 s)
test_door_loading  EXIT=0   (graphics subset skipped headless, as at baseline)
```

`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` **did not
fail** in this wave run. The two WARNs are `CMakeLists.txt` (2,004) and
`tests/CMakeLists.txt` (2,850), **both pre-existing and neither mine** — though
see §7, both grew again.

**Two steps needed a second invocation, neither for a reason in this unit:**

1. **First `ctest` run died at exit 8 in test DISCOVERY, not in a test** —
   `GoogleTestAddTests.cmake` timed out running `bin/test_read_book
   --gtest_list_tests` under three-slot contention. This is the T293 shape but
   **not** the T293 cause: the binary was intact (`--gtest_list_tests` exit 0 on
   demand, 72 MB, correct mtime), the build had already exited 0, and the clean
   re-run enumerated all 3,499 tests. **A ctest exit 8 with a
   `GoogleTestAddTests.cmake` "Process terminated due to timeout" is a
   contention artifact of discovery, distinct from a truncated binary — check
   the binary before assuming a bad link.**
2. **First visual gate run FAILED, on CPU time, not on pixels** — `befallen p95
   process-CPU 11.684 ms` against a 10.948 ms ceiling (baseline 5.474 × 2.0),
   while `gfaydark` in the same run was 5.183 against 5.336. **Zero image
   comparisons failed and `git status` was empty**, so this was never a golden
   or a rendering question. The clean re-run: qeynos2 4.505, gfaydark 5.345,
   **befallen 4.676** — a 2.5× swing on the same binary, i.e. wave contention on
   a 2× ceiling. Reported as a template hazard in §7.

**Wall clock ≈ 62 min end to end.** The cut, the CMake pass and both proofs took
**under 6 minutes combined**; everything else was the bar. **Slowest single step:
`ctest -j6` at 518.85 s (8.6 min) — and it had to be run twice**, so ~19 min of
the hour was the suite alone. `test_wld` by itself is 248 s of that. Concurrency
with J06/J07 **stretched the verification tail materially**: it cost one extra
`ctest` (discovery timeout) and one extra visual gate (CPU ceiling), ~14 min of
pure re-run. The ARM build, which the plan warned would need two invocations,
finished inside the ceiling on the first — the wave was evidently lighter at
that moment than during the suite.

### 7. Findings only — recorded, NOT acted on

1. **The dead `#if 0` region** — `wld_loader.cpp:472-749` post-split,
   `2631-2908` pre-split, `namespace removed { … }` holding the pre-D20f1
   `BspTree` implementations that now live in `src/client/zone_bsp.cpp`.
   ⚠ **It is 278 lines, not the 286 this plan and the batch plan both state**;
   286 is the length of the whole `2623-2908` tail range, which also contains
   the two `// BspTree` comments and the two namespace-close lines. The dead
   block proper is `2908 - 2631 + 1 = 278`. It is **37% of the surviving
   stays-file**; without it `wld_loader.cpp` would be 471 lines. **J02's `eq_ucs.cpp` has the identical shape** (320 lines, zero
   live code) and the batch owes one decision covering both. Untouched here —
   deleting 286 lines is not move-only.
2. **`parseFragment36` (337 lines) and `parseFragment2C` (287 lines)** are now
   the two largest methods in the tree's WLD path and dominate
   `wld_fragments_mesh.cpp` (337 of 484) and `wld_fragments_actor.cpp` (287 of
   504) respectively. **Batch-G decomposition candidates**, and given this
   file's mutation score they are where the surviving faults most plausibly
   live. Explicitly out of scope per §Optional; not decomposed.
3. ⚠ **`resolveTextureName` no longer exists.** `refactor/T10_wld_kill_rate.md:281`
   records it as dead code in this file; `grep -rn 'resolveTextureName' src
   include tests` at `6e3a5b48` returns **nothing at all**. It was removed at
   some point between T10 and now. **This is a correction to the T10 record, not
   a finding for Batch G** — whoever closes T10's leftovers should strike that
   line rather than go looking for it.
4. **`tests/CMakeLists.txt` is now 2,850** (was 2,778 at J04, 2,762 before it) —
   **within 150 lines of the 3,000 hard cap**, and J05 alone added 24. Correction
   8 boarded a split for this; on the current slope **the next two or three J
   units will trip a genuine FAIL through no fault of their own.** `CMakeLists.txt`
   crossed 2,000 in the same wave (1,914 → 2,004). Both are grandfathered by
   name today; the grandfathering is what is hiding the slope.

### Template corrections this unit contributes

**13. A visual-gate FAIL is not necessarily a pixel FAIL — read which check
failed.** J05's first gate run failed on `befallen`'s p95 process-CPU ceiling
(11.684 vs 10.948) with **zero image mismatches and a clean `git status`**. The
same zone measured 4.676 on an immediate re-run — a 2.5× swing from wave
contention against a ceiling set at only 2× baseline. **Under a three-wide wave,
re-run the gate once before treating a CPU-ceiling failure as a finding**, and
distinguish it in the report from a golden mismatch. (This interacts badly with
the plan's own — correct, and separately valuable — warning that a missed
`gles2_zone_benchmark` CMake site surfaces as a gate failure: **a link error
would fail the gate at the render step, not at the CPU-ceiling step**, so the
failing line tells you which of the two you have. That disambiguation belongs in
the template.)

**14. A ctest exit 8 in test DISCOVERY is not the T293 truncated-binary bug.**
`GoogleTestAddTests.cmake … Process terminated due to timeout` under multi-slot
contention looks alarming and resolves on a re-run. Distinguish by running the
named binary's `--gtest_list_tests` on demand: exit 0 means the binary is fine
and the discovery step merely lost a race. T293's genuine truncation fails
differently (a shell-parse error).

**15. Declare loop and count variables `local` in proof helper functions.** A
non-local `n` in a two-line `body()` helper silently truncated one range of the
re-concatenation proof and produced a 247-line shortfall that looked exactly
like a dropped range. **When a mechanical proof fails, suspect the proof first**
— the failure signature (one contiguous tail missing, every other range intact)
distinguishes a harness bug from a real gap.

**16. Correction 10's "keep the symbol in its existing namespace" is now
confirmed in practice and should be promoted to a rule.** J04 needed
`namespace detail` + a `using` per TU and paid 8 exception lines for 12 call
sites; J05 kept `read_val` in `EQT::Graphics` and paid **2 exception lines for
114 call sites, with zero call sites rewritten.** For any future unit whose
promoted symbol already lives in a namespace shared by every consumer, this is
strictly better and should be the default, not an option.

