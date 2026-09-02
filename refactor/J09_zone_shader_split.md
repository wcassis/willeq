# J09 — Batch J Wave 3: split zone_shader.cpp (2,561 → 1 TU + 3 private headers, largest ~1,010)

**Slot: assigned at dispatch. Branch `task/J09-zone-shader-split` cut from
`fable`. x86 only. No Pi hardware. No eqemu.**

Wave 3 of `refactor/J_file_size_reclamation.md`, running alongside **J08** and
**J10**. Read, before editing anything: **`refactor/J_file_size_reclamation.md`
§Process per unit AND all six correction sections** (eighteen numbered lessons
from J01–J07 and J11; newer text wins over older), then
`refactor/J05_wld_loader_split.md` **including its Review** (the batch's only
other unit that creates a private header, and the one whose CMake exposure most
resembles this one) and `refactor/J11_irrlicht_renderer_split.md` §Review, plus
`refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## ⚠ This unit is STRUCTURALLY THE ODDEST IN THE BATCH. Read this section twice.

`zone_shader.cpp` is not shaped like the other eleven. **1,362 of its 2,561
lines are GLSL source held in `static const char*` raw string literals**, laid
out as two arms of a single `#ifdef EQT_HAS_GLES2` / `#else` / `#endif` that
**declare the SAME EIGHT IDENTIFIERS** — `VERTEX_SHADER_SRC`,
`FRAGMENT_SHADER_SOLID_SRC`, `ATLAS_VERTEX_SHADER_SRC`,
`ATLAS_FRAGMENT_SHADER_SOLID_SRC`, `WIND_VERTEX_SHADER_SRC` and friends are each
defined **once in each arm**, with different GLSL bodies (GLSL ES 1.00 vs GLSL
1.20).

**Two hard consequences:**

1. **They are `static`, and they must stay `static`.** Two TUs cannot both see
   them (internal linkage), and de-`static`-ing them would put eight
   identically-named symbols in the link — a genuine ODR/multiple-definition
   break, not a paperwork problem. **They therefore CANNOT move to a .cpp file.**
2. **They move to HEADERS, one per arm, each arm keeping its own `#ifdef`,
   included by exactly one TU.** This is the T166 / J05 `wld_internal.h`
   precedent applied to data rather than to a helper: a private internal header
   living **beside the source**, not under `include/`.

⚠ **Do not "simplify" the two arms into one.** They are two different GLSL
dialects for two different drivers. A unit that merges them is not a move.

⚠ **Do not de-`static` anything, do not add `inline`, do not introduce a
`detail` namespace.** Correction 18 is a rule, not an option: a promoted symbol
keeps its EXISTING namespace. Everything here is already in `EQT::Graphics` and
must stay there — the headers open `namespace EQT { namespace Graphics {`
themselves so that every reference in the .cpp is textually untouched.

## Measured at `f215651a` (2026-09-02, `fable` tip after J07 merged)

`src/client/graphics/zone_shader.cpp` — **2,561 lines**, matching the survey.
`include/client/graphics/zone_shader.h` — 301 lines, **not touched by this unit.**

Verified properties:

- **Preamble is lines 1-13**: includes **1-9** (with a `#ifdef EQT_HAS_GLES2` /
  `#elif defined(EQT_HAS_DRM)` / `#endif` group at 3-8), blank 10,
  `namespace EQT {` **11**, `namespace Graphics {` **12**, blank 13.
  **Lines 2560-2561 are the two namespace closes.** Every new header replicates
  the namespace open/close; only the callbacks header needs the include block.
- **Twelve `// ====` banner triples**, at 14, 455, 515, 644, 1020, 1134, 1268,
  1377, 1408, 2089, 2443, 2494. §Process's banner rule and J03's cutting rule
  agree at every boundary this plan uses.
- **28 file-local `static`s**: 26 shader-source `const char*` (18 in the GLES2
  arm, 8 in the desktop arm) and 2 helpers, `floatToHalf` (1382) and
  `fillAttenLUT` (1395), both themselves inside a `#ifdef EQT_HAS_GLES2` at
  1380-1406 and both called only from `ZoneShaderManager`'s constructor (2399)
  and `beginFrame()` (2431). **The helpers stay in the .cpp** — they have no
  cross-TU consumer.
- **Zero anonymous namespaces. Zero block comments** (`grep -n '/\*'` → empty).
- **Four CMake add-sites**, one of them `gles2_zone_benchmark`. See §CMake.
- **Zero functional readers in `tests/` or `scripts/`.** See §Territory.

## ✅ THE SURVEY'S "NO `friend` NEEDED" CLAIM HOLDS — verified, not read

The survey asserted from reading that the three shader-callback classes reach
`ZoneShaderManager` only through public accessors. **Measured at `f215651a`:
TRUE.**

- `include/client/graphics/zone_shader.h` contains **no `friend` declaration at
  all** (`grep -n friend` → empty). Whatever the callbacks use today is already
  public, or the file would not compile.
- The three classes touch `owner_` through exactly **27 distinct members**:
  `ambientColor` `attenLutTexture` `fireGlowLightColor` `fireGlowLightIntensity`
  `fireGlowLightPos` `fireGlowLightRadius` `fogColor` `fogEnd` `fogStart`
  `frameId` `getAtlasPageTexture` `lightAtten` `lightColor` `lightPos`
  `lutScale` `numFireGlowLights` `numPointLights` `playerLightAtten`
  `playerLightColor` `playerLightPos` `shaderDebug` `sunColor` `sunDir`
  `tintColor` `unifiedLighting` `windParams` `windTime`.
  **Every one is a public accessor**, declared between `public:` at
  `zone_shader.h:16` and `private:` at `zone_shader.h:219`. There is **not one
  `owner_->m_…` raw-member access** in 681 lines of callback code.
- The two constants they qualify, `ZoneShaderManager::MAX_POINT_LIGHTS` and
  `::MAX_FIRE_GLOW_LIGHTS`, are `static constexpr int` at `zone_shader.h:17-18`,
  also public.

**So the unit does not change shape on that axis.** ⚠ **Re-run the check at your
tip and paste it** — one new `owner_->` line added by a T unit between now and
dispatch would change the answer:

```bash
sed -n '1409,2088p' src/client/graphics/zone_shader.cpp \
  | grep -o 'owner_->[A-Za-z_]*' | sort -u
grep -n 'friend' include/client/graphics/zone_shader.h
awk 'NR>=1 && NR<=301 && /public:|private:|protected:/ {print NR": "$0}' \
    include/client/graphics/zone_shader.h
```

### But the survey's TU count does NOT survive measurement — and here is why

The survey's row reads "3 TUs + 2 hdrs". **That is not achievable, and the
reason is worth stating so nobody re-litigates it at review time:** the three
callback classes are **defined entirely inline, in-class**, and
`ZoneShaderManager`'s constructor instantiates them **18 times**
(`new ShaderCallback(this)` ×7, `new AtlasShaderCallback(this, …)` ×4,
`new WindShaderCallback(this)` ×7, at lines 2122-2373). A separate `.cpp` for
them would therefore need a header declaring them anyway — and since they are
100% inline, that header would contain the whole implementation and the `.cpp`
would be **empty**. So:

**J09 produces ONE TU and THREE private headers.** That is the honest shape.
It also clears the ceiling with more room than the survey's plan did.

## The split

All three headers live **beside the source**, in `src/client/graphics/`, not
under `include/` — they are private, single-includer internal headers (the T166
`wld_internal.h` / J05 precedent). Each carries a comment saying so.

| Range | Content | Destination | ≈ lines |
|---|---|---|---|
| 1-13 | preamble: includes 1-9, blank, `namespace EQT {` 11, `namespace Graphics {` 12, blank | **stays** (and is copied into the callbacks header) | 13 |
| **14-1017** | banner "GLES2 shader variants (GLSL ES 1.00)" (14-16) + its 5-line explanation (17-21) + `#ifdef EQT_HAS_GLES2` (22) + **18 shader sources** (27, 120, 151, 190, 223, 260, 355, 386, 420, 465, 521, 654, 738, 825, 843, 866, 885, 906) with their banners at 455, 515, 644 | **`zone_shader_sources_gles2.h`** | ~1,010 |
| **1018** | `#else // !EQT_HAS_GLES2 — Desktop GL / GLSL 1.20` | **becomes `#ifndef EQT_HAS_GLES2`** in the second header | 1 |
| **1019-1374** | **8 desktop-GL shader sources** (1025, 1100, 1117, 1154, 1230, 1247, 1271) with their banners at 1020, 1134, 1268 | **`zone_shader_sources_gl.h`** | ~365 |
| **1375** | `#endif // EQT_HAS_GLES2` | closes **both** headers (one copy each) | 1 |
| **1376-1407** | banner "FP16 attenuation LUT helpers (GLES2)" (1377-1379) + `#ifdef EQT_HAS_GLES2` (1380) + `floatToHalf` (1382-1391) + `fillAttenLUT` (1395-1404) + `#endif` (1406) | **stays** `zone_shader.cpp` | 32 |
| **1408-2088** | banner "Shader callbacks" (1408-1410) + `class ShaderCallback` (1413-1614) + `class AtlasShaderCallback` (1618-1850) + `class WindShaderCallback` (1854-2087) | **`zone_shader_callbacks.h`** | ~700 |
| **2089-2559** | banner "ZoneShaderManager constructor" (2089-2091) + ctor (2093-2411) + dtor (2413-2420) + `beginFrame` (2422-2441) + banner "Active material getters" (2443-2445) + 9 getters (2447-2492) + banner "Uniform setters" (2494-2496) + 11 setters (2498-2558) | **stays** `zone_shader.cpp` | 471 |
| 2560-2561 | `} // namespace Graphics` / `} // namespace EQT` | stays, **and is replicated in each header** | 2 |

**Result: 1 TU + 3 private headers.**

| File | ≈ lines |
|---|---|
| `src/client/graphics/zone_shader.cpp` (stays) | **~525** |
| `src/client/graphics/zone_shader_sources_gles2.h` | **~1,010** |
| `src/client/graphics/zone_shader_sources_gl.h` | **~365** |
| `src/client/graphics/zone_shader_callbacks.h` | **~700** |

Arithmetic that must hold: `13 + 1004 + 1 + 356 + 1 + 32 + 681 + 471 + 2 = 2561`.
If your own numbers do not sum to the file's line count, you have a gap or an
overlap — find it before you build.

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low, J07 ran exactly 3 high). Cluster membership has
been right every time; the arithmetic has not.

### Exact header shapes

**`zone_shader_sources_gles2.h`** — needs **no `#include` at all** (the content
is nothing but string literals):

```
#pragma once
// GLES2 (GLSL ES 1.00) zone shader sources, split out of zone_shader.cpp by
// Batch J unit J09. PRIVATE internal header: these are file-local `static`
// definitions and this file must be included by zone_shader.cpp and NOTHING
// ELSE. See refactor/J09_zone_shader_split.md.

namespace EQT {
namespace Graphics {

<lines 14-1017 VERBATIM, including the `#ifdef EQT_HAS_GLES2` at 22>

#endif // EQT_HAS_GLES2

} // namespace Graphics
} // namespace EQT
```

**`zone_shader_sources_gl.h`** — same shape, but line 1018's `#else` is replaced
by a fresh `#ifndef EQT_HAS_GLES2` and a fresh `#endif` closes it:

```
#pragma once
// Desktop GL (GLSL 1.20) zone shader sources ... PRIVATE internal header ...

namespace EQT {
namespace Graphics {

#ifndef EQT_HAS_GLES2   // was the `#else` arm of zone_shader.cpp:1018

<lines 1019-1374 VERBATIM>

#endif // !EQT_HAS_GLES2

} // namespace Graphics
} // namespace EQT
```

⚠ **The `#else` → `#ifndef` swap is semantically identical and costs the
verifier NOTHING**: `verify_move_only.py`'s `BOILERPLATE` allowlist explicitly
covers `#ifdef`, `#ifndef`, `#if`, `#else`, `#elif`, `#endif`, `#pragma`,
`#include`, `#define`, comments, `namespace … {` and `}`. **Confirm that by
reading `scripts/refactor/verify_move_only.py:25-28` yourself before you
believe it.**

**`zone_shader_callbacks.h`** — this one **does** need the includes, because the
classes use `irr::video::IShaderConstantSetCallBack`, `glUniform*` and
`LOG_*`:

```
#pragma once
// The three ZoneShaderManager shader-constant callbacks, split out of
// zone_shader.cpp by Batch J unit J09. PRIVATE internal header ...

<lines 1-9 of zone_shader.cpp, the include block, VERBATIM>

namespace EQT {
namespace Graphics {

<lines 1408-2088 VERBATIM>

} // namespace Graphics
} // namespace EQT
```

**`zone_shader.cpp` after the cut**: its own include block (1-9) **unchanged**,
then three new `#include "client/graphics/zone_shader_sources_gles2.h"` /
`…_gl.h` / `…_callbacks.h` lines, then the namespace opens, then ranges
1376-1407 and 2089-2559, then the namespace closes.

⚠ **The include ORDER is load-bearing.** The sources headers must precede the
constructor (which names 25 of the 26 literals, at 2109-2376) and the callbacks
header must precede it too (18 `new` sites, 2122-2373). Put all three in the
include block, `_gles2` then `_gl` then `_callbacks`. Follow the J05 precedent
for the include path form: `wld_fragments_*.cpp` write
`#include "client/graphics/eq/wld_internal.h"` — i.e. the **project-relative
path from `src/`**, not a bare filename. Check that the graphics source
directory is on the include path the same way (`grep -n 'INCLUDE_DIRECTORIES'
CMakeLists.txt`) and match whatever `wld_internal.h` does.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is J03's (correction 11): **cut at a blank line whose predecessor
closes a method (or a literal), and verify comment depth 0 at the cut.**
`zone_shader.cpp` has **no block comments at all**, so comment depth is 0
everywhere — confirm with `grep -n '/\*' src/client/graphics/zone_shader.cpp`
returning empty. Checked at `f215651a`:

| Cut | line-1 | line | line+1 |
|---|---|---|---|
| 14 | `12: namespace Graphics {` | `13:` (blank) | `14: // ====…` |
| 1018 | `1016: )";` | `1017:` (blank) | `1018: #else // !EQT_HAS_GLES2 — Desktop GL / GLSL 1.20` |
| 1375 | `1373: )";` | `1374:` (blank) | `1375: #endif // EQT_HAS_GLES2` |
| 1376 | `1375: #endif // EQT_HAS_GLES2` | `1376:` (blank) | `1377: // ====…` |
| 1408 | `1406: #endif // EQT_HAS_GLES2` | `1407:` (blank) | `1408: // ====…` |
| 2089 | `2087: };` | `2088:` (blank) | `2089: // ====…` |
| 2560 | `2558: }` | `2559:` (blank) | `2560: } // namespace Graphics` |

## ⚠ HAZARD 1 — the preprocessor. 21 regions; three cuts land ON one.

**Run this and paste it:**

```bash
awk '/^[ \t]*(#if|#ifdef|#ifndef|#else|#elif|#endif)/ {print NR": "$0}' \
    src/client/graphics/zone_shader.cpp
```

At `f215651a` it lists 37 lines. The ones that matter to a cut:

- **`22` `#ifdef EQT_HAS_GLES2` … `1018` `#else` … `1375` `#endif`** — the big
  two-arm block. **This is the only place in the batch where a unit
  deliberately splits a live `#ifdef/#else/#endif` across files.** Get it
  right: `#ifdef` travels with arm 1 into header A, `#endif` is written fresh at
  the end of BOTH headers, and `#else` is replaced by `#ifndef EQT_HAS_GLES2`
  at the top of header B.
- **`1380` … `1406` `#ifdef EQT_HAS_GLES2`** — wraps `floatToHalf` and
  `fillAttenLUT` **entirely**. Both stay in the .cpp; their only callers (2399,
  2431) are themselves inside `#ifdef EQT_HAS_GLES2` regions (2259-2410 and
  2427-2440), so the arrangement is already self-consistent. **Do not move
  either helper without moving that guard.**
- **`1423/1427`, `1429/1543/1591`, `1596/1613`, `1634/1644/1646/1656/1658/1664`,
  `1666/1770/1822`, `1833/1849`, `1870/2011/2061`, `2068/2086`** — all interior
  to the callbacks block (1408-2088) and all move together into the callbacks
  header untouched.
- **`2107/2112/2119`, `2259/2410`, `2414/2419`, `2427/2440`** — all interior to
  the stays range.

**No cut point other than 1018/1375 lands on or inside a conditional.** Verify
that is still true at your tip.

## ⚠ HAZARD 2 — GLSL text is C++ text to every tool you will use

The 1,362 relocated lines are shader source, and they contain lines that look
like C++ to `grep`, to `awk`, and to `verify_move_only.py`'s normalizer:
`void main() {`, `}`, `precision highp float;`, and dozens of `uniform`/
`varying` declarations. Two implications:

- ⚠ **A bare closing `}` matches `verify_move_only.py`'s BOILERPLATE regex**
  (`\}\s*(//.*)?$`). So GLSL closing braces are allowlisted and will not be
  reported if a whole shader body went missing — the surrounding lines would be,
  but **the multiset check and the byte-identical re-concatenation are what
  actually protect you here.** Run both; do not lean on the verifier alone.
- ⚠ **The two arms share many identical GLSL lines.** The multiset comparison
  handles that correctly (it counts occurrences) but a naive `sort -u` does
  **not**. Use `sort`, never `sort -u`.

## ⚠ HAZARD 3 — three private headers, one includer, and it must stay that way

`static` at namespace scope in a header means **a private copy per including
TU**. Today that is exactly one TU and the arrangement is free. If a future unit
includes one of these headers a second time, it silently duplicates 1,000 lines
of string data into another binary. **Each header carries the "PRIVATE — include
from zone_shader.cpp only" comment shown above**, and the Review states that the
includer count is one. Confirm with:

```bash
grep -rn 'zone_shader_sources_gles2.h\|zone_shader_sources_gl.h\|zone_shader_callbacks.h' src/ include/ tests/
```

## CMake — grep, never trust a line number

```bash
grep -n 'zone_shader\.cpp' CMakeLists.txt tests/CMakeLists.txt
```

At `f215651a` that is **exactly four sites, all in the root file, zero in
`tests/CMakeLists.txt`**:

| line | target |
|---|---|
| 485 | `SET(GRAPHICS_SOURCES` (opens at `:432`, inside `IF(EQT_GRAPHICS)` at `:431`) |
| 1656 | ⚠ **`ADD_EXECUTABLE(gles2_zone_benchmark` (opens at `:1621`)** |
| 1777 | `ADD_EXECUTABLE(gles2_shader_diagnostic` (opens at `:1748`) |
| 1860 | `ADD_EXECUTABLE(gles2_composite_audit` (opens at `:1831`) |

**This unit creates no new .cpp, so there is NOTHING TO ADD at any of the four
sites.** They are listed so you can confirm that — and so you know that if the
build breaks, it is an include-path problem, not a missing source.

⚠ **`gles2_zone_benchmark` is the binary `scripts/ci/visual_gate.sh` runs.**
A build problem in it surfaces as a **visual gate failure at the RENDER step**,
which an executor will misdiagnose as a behaviour change. See §Acceptance.

⚠ **CMake commands in this project are UPPERCASE** — `grep -n 'set('` and
`grep -n 'add_executable'` both return nothing, silently, which reads exactly
like "no sites here". **Grep for the filename.** ⚠ **Add-site counts have ranged
from 1 to 19 across this batch**; report the count you actually found.

⚠ **Headers are not listed in `CMakeLists.txt` in this project** — verify that
(`grep -c '\.h$' CMakeLists.txt`) rather than assuming, and if the project does
list some headers, add the three new ones the same way.

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
  then `rm` every hit and rebuild once. This is now also in
  `refactor/EXECUTOR_PROTOCOL.md`.
- ⚠ **Build BOTH arms.** This unit is the only one in the batch whose two halves
  are selected by a preprocessor switch, so a single configuration exercises
  only one of the two headers. `scripts/build-gles2-x86.sh` and
  `scripts/build-arm-noble.sh` both define `EQT_HAS_GLES2`; **state explicitly
  in your Review which configuration, if any, compiles
  `zone_shader_sources_gl.h`'s arm**, and if none does, say so — that is a
  finding (the desktop-GL arm may be dead since P30/P32 removed desktop GL) and
  belongs on the board, **not a licence to delete it in this unit.**
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
- **Expected verifier result for J09: exit 0.** Every line this unit adds that
  did not exist before is `#pragma once`, a comment, a `namespace … {`, a `}`,
  an `#include`, an `#ifndef` or an `#endif` — **all of them in the
  `BOILERPLATE` allowlist.** The `#else` → `#ifndef` swap and the duplicated
  include block cost nothing. ⚠ **But correction 9 is general: the verifier
  exits 1 by construction wherever a copied preamble carries anything beyond
  includes/pragmas/namespaces/comments** — J11 got 70 such lines from 12
  `extern` declarations plus a one-line stub. If yours exits 1, **paste the full
  output, enumerate every flagged line, and request orchestrator sign-off in the
  Review. NEVER contort code to force a green exit** (J04 correctly refused to).
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines, so it cannot see a range duplicated into
  two TUs or dropped from both. This check can, because it counts occurrences —
  and given HAZARD 2 (identical GLSL lines in both arms) it is the check that
  actually matters here.
  ```bash
  # before: the whole pre-split body
  git show "$BASE:src/client/graphics/zone_shader.cpp" | sed -n '14,2559p' | sort > /tmp/before.txt
  # after: the three headers' bodies (each stripped of #pragma once, its new
  # comment, its namespace opens/closes, and — for the callbacks header — the
  # copied 9-line include block) + zone_shader.cpp's surviving body, stripped of
  # its three new #include lines.
  cat <the four stripped bodies> | sort > /tmp/after.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Correction 7, generalised: state each header's line contribution
  explicitly.** A naive "concatenate the .cpp bodies" under-counts by the
  headers' 2,041 moved lines and **still looks clean**. ⚠ **`sort`, never
  `sort -u`.**
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate,
  **in original order**: header A's arm (was 14-1017), the line that replaced
  1018, header B's arm (was 1019-1374), the `#endif` that replaced 1375, the
  .cpp's 1376-1407, the callbacks header's 1408-2088, and the .cpp's 2089-2559.
  Everything except the two transformed preprocessor lines must be
  byte-identical to `git show "$BASE:…/zone_shader.cpp" | sed -n '14,2559p'`.
  **2,546 lines; expect exactly 2 differing lines** (`#else …` → `#ifndef
  EQT_HAS_GLES2`, and one extra `#endif`). **Paste the `diff`, and enumerate
  those two lines as the unit's only non-verbatim change.**
  ⚠ **Declare count variables `local` in your proof helpers** (correction 17). A
  non-local `n` in J05's `body()` helper truncated one range and produced a
  247-line shortfall **that read exactly like a dropped range.** When a proof
  fails, suspect the proof first.
- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report** rather than re-blessing.
  - ⚠ **A gate FAIL is not necessarily a PIXEL fail — check WHICH metric
    failed.** J05's first run failed on `befallen` **p95 CPU 11.684 vs a 10.948
    ceiling with ZERO image mismatches**; the re-run measured 4.676. That is
    wave contention, not your change: **re-run.**
  - ⚠ **A failure at the RENDER step is different and, for THIS unit, is the
    one to take seriously.** `gles2_zone_benchmark` compiles `zone_shader.cpp`,
    and this unit changes what that TU includes. **A render-step failure here
    means the shader sources did not reach the compiler — an include-path or
    include-order problem — not a CPU flake.** Do not re-run past it.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ `build-arm-noble.sh` can blow the 600 s Bash-tool ceiling under wave
  concurrency — SIGTERM, **exit 143**, typically mid-`gles2_zone_benchmark`.
  **Exit 143 means RE-INVOKE, not breakage** — and then run the ELF scan above.
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**, **including all three new headers**. ⚠ The
  largest, `zone_shader_sources_gles2.h` at ~1,010, is the number to watch: if
  your real count lands over 1,500, split the GLES2 arm at its own internal
  banner (455 "Debug fragment shader", 515 "Wind vertex shader" or 644
  "Lightweight shader variants") into a fourth header rather than shaving lines.
- ⚠ **`chmod 644` every created file before `git add`.** A scripted `sed >` pass
  produces mode 755 and records a spurious `100644 => 100755`; verify with
  `git ls-files -s` that all new files are `100644`.

⚠ **Timing figures anywhere in the batch plan are WORST CASES OBSERVED, NOT
FORECASTS** (correction 14). J06's `ctest -j6` ran in 120.7 s and its
`build-arm-noble.sh` finished first time. Treat the stretch figures as "may
happen".

## Review must answer

1. **Line count at your tip** against the 2,561 this plan measured, and any
   range drift.
2. ⚠ **The `friend` question, re-verified with the three greps in §"THE
   SURVEY'S NO-`friend` CLAIM HOLDS", pasted.** If a raw-member access has
   appeared since `f215651a`, **STOP and report** — the unit changes shape.
3. **The preprocessor boundary check**, pasted, and confirmation that
   `grep -n '/\*'` is still empty (no block comments).
4. **Exactly how you rendered the `#ifdef`/`#else`/`#endif` across the two
   headers**, with the two transformed lines quoted.
5. **The includer count for each of the three private headers** (must be 1) and
   the include path form you used, against the `wld_internal.h` precedent.
6. **Which build configuration, if any, compiles the desktop-GL arm.** If none
   does, say so plainly — it is a finding for the board.
7. **How many CMake add-sites you actually found**, against the four this plan
   names, and confirmation that none needed editing.
8. **Both content proofs**, pasted, with each header's line contribution
   explicitly counted. State the verifier's exit code and `$BASE`.
9. Final line counts of all four files, wall clock and slowest step, and whether
   concurrency with J08/J10 stretched the verification tail.
10. **Findings only, not fixes:** anything that looks unreachable; the
    `docs/future/shader_permutation_handoff.md` references at `:72-73`, `:404-407`
    (which name `ShaderCallback` as living "in zone_shader.cpp" — now the
    callbacks header) and `docs/reference/shader_overview.md:134`. Comment-only
    doc rot, outside a move-only commit's scope; fold into the
    `docs/module_map.md` hygiene pass J02/J04/J06/J07 already owe. **Record. Do
    not act.**

## Territory

`src/client/graphics/zone_shader.cpp`, the three new private headers beside it
(`zone_shader_sources_gles2.h`, `zone_shader_sources_gl.h`,
`zone_shader_callbacks.h`), `CMakeLists.txt` **only if a site actually needs
editing**, and this file. **Nothing else** — **not
`include/client/graphics/zone_shader.h`** (this unit changes no interface, adds
no `friend`, promotes nothing), not any other `src/client/graphics/*.cpp`, not
any `tests/*.cpp`, not `tests/CMakeLists.txt`, not `docs/`.

**Correction 10 pre-check, already run for you and to be re-run at your tip:**

```bash
grep -rn 'zone_shader' tests/ scripts/
```

At `f215651a` this returns **nothing from `tests/` and nothing from `scripts/`**
— **zero readers of any kind, functional or comment.** (The only mentions in the
repo outside `src/`/`include/` are in `docs/future/shader_permutation_handoff.md`
and `docs/reference/shader_overview.md`, both prose.) So unlike J11 — where four
tests read `irrlicht_renderer.cpp` as *text*, three of which would have failed
and one of which would have **silently asserted nothing** — **no repoint is
needed.** ⚠ **Re-run it anyway before cutting** and report what you find; this
is a standing pre-cut step, not an optional one.

Concurrent at dispatch: **J08** (`src/client/eq_entities.cpp` + new
`eq_entities_*.cpp` + `src/client/eq_group.cpp`, `CMakeLists.txt`) and **J10**
(`src/client/graphics/constrained_texture_cache.cpp` + new TUs,
`CMakeLists.txt`, `tests/CMakeLists.txt`). No source overlap with either.
⚠ **`CMakeLists.txt` is shared with both** — which is why **merges are
serialized one at a time with a rebase.**

---

## Review (executed 2026-09-02, slot2, branch `task/J09-zone-shader-split`)

**Base / tip.** Branch cut from `fable` at **`8b0dfcfb`** ("Protocol: the session
scratchpad is shared across slots…"), which is `$BASE` = `git merge-base fable
HEAD` = `8b0dfcfb871bf6ddcac131034487736cde99def6`. Split commit
**`af5144b9`**. **PASS on every bar item; zero deviations from the plan's
shape; zero re-runs.**

### 1. Line count at my tip — no drift

`src/client/graphics/zone_shader.cpp` = **2,561** lines at `8b0dfcfb`, exactly
the plan's measurement at `f215651a`. `include/client/graphics/zone_shader.h` =
**301**, untouched. **Every one of the plan's 37 preprocessor line numbers, 12
banner triples, 7 cut boundaries and 4 CMake sites reproduced verbatim at my
tip.** This is the first Wave-3 unit with zero range drift (contrast correction
19, where `eq_entities.cpp` had grown 50 lines under J08).

### 2. The `friend` question — RE-VERIFIED, the claim holds

```
$ sed -n '1409,2088p' src/client/graphics/zone_shader.cpp | grep -o 'owner_->[A-Za-z_]*' | sort -u
owner_->ambientColor          owner_->lightPos
owner_->attenLutTexture       owner_->lutScale
owner_->fireGlowLightColor    owner_->numFireGlowLights
owner_->fireGlowLightIntensity owner_->numPointLights
owner_->fireGlowLightPos      owner_->playerLightAtten
owner_->fireGlowLightRadius   owner_->playerLightColor
owner_->fogColor              owner_->playerLightPos
owner_->fogEnd                owner_->shaderDebug
owner_->fogStart              owner_->sunColor
owner_->frameId               owner_->sunDir
owner_->getAtlasPageTexture   owner_->tintColor
owner_->lightAtten            owner_->unifiedLighting
owner_->lightColor            owner_->windParams
                              owner_->windTime
   → 27 distinct members, matching the plan's list exactly.

$ ... | grep -n 'owner_->m_'        → no output (exit 1)   # ZERO raw member accesses
$ grep -n 'friend' include/client/graphics/zone_shader.h
                                    → no output (exit 1)
$ awk 'NR<=301 && /public:|private:|protected:/ {print NR": "$0}' include/client/graphics/zone_shader.h
16: public:
219: private:
```

All 27 members sit in the single `public:` region (`:16`–`:218`). **No `friend`
added; `zone_shader.h` not touched at all.** The prediction held.

### 3. Preprocessor boundary check + comment depth

`grep -n '/\*' src/client/graphics/zone_shader.cpp` → **no output (exit 1)**:
zero block comments, so comment depth is 0 at every line and every cut is
trivially safe on that axis.

The `awk` preprocessor listing returned **exactly the 37 lines the plan names,
at the same line numbers**: the big two-arm block at `22`/`1018`/`1375`; the LUT
helpers' guard at `1380`/`1406`; 20 lines interior to the callbacks block
(1423–2086); 8 interior to the stays range (2107–2440); and the preamble group
at `3`/`6`/`8`. **No cut point other than 1018/1375 lands on or inside a
conditional** — confirmed. All seven cut boundaries reproduced their documented
`line-1 / line / line+1` triples exactly.

### 4. How the `#ifdef` / `#else` / `#endif` was rendered across the two headers

Original `22`…`1018`…`1375` became:

| Original | Became | Where |
|---|---|---|
| `22 #ifdef EQT_HAS_GLES2` | unchanged, verbatim | `zone_shader_sources_gles2.h:31` |
| — | **added** `#endif // EQT_HAS_GLES2` | `zone_shader_sources_gles2.h:1014` |
| `1018 #else // !EQT_HAS_GLES2 — Desktop GL / GLSL 1.20` | **replaced by** ``#ifndef EQT_HAS_GLES2   // was the `#else` arm of zone_shader.cpp:1018`` | `zone_shader_sources_gl.h:10` |
| `1375 #endif // EQT_HAS_GLES2` | **comment retitled**: `#endif // !EQT_HAS_GLES2` | `zone_shader_sources_gl.h:367` |

Both headers are included unconditionally by `zone_shader.cpp`; exactly one arm
survives preprocessing in any configuration, as before. **The two arms were not
merged, no literal was de-`static`ed, no `inline` added, no `detail` namespace
introduced; both headers open `namespace EQT { namespace Graphics {` themselves,
so every reference in the .cpp is textually untouched.**

⚠ **Correction to the plan's own arithmetic (minor).** §Acceptance predicts
"exactly 2 differing lines"; the actual re-concatenation diff is **3 changed
lines across 2 hunks**, because the plan's own header-shape template
(§"Exact header shapes") specifies `#endif // !EQT_HAS_GLES2` for header B's
closer — a **comment-text** change to line 1375 that the prediction did not
count. The three are enumerated in §8 below. **The `!` was kept**: with an
`#ifndef` above it, `#endif // EQT_HAS_GLES2` would be actively misleading.

### 5. Includer counts and include-path form

```
$ grep -rn 'zone_shader_sources_gles2.h\|zone_shader_sources_gl.h\|zone_shader_callbacks.h' src/ include/ tests/
src/client/graphics/zone_shader.cpp:10:#include "client/graphics/zone_shader_sources_gles2.h"
src/client/graphics/zone_shader.cpp:11:#include "client/graphics/zone_shader_sources_gl.h"
src/client/graphics/zone_shader.cpp:12:#include "client/graphics/zone_shader_callbacks.h"
```

**One includer each, and all three are that same TU.** Path form is the
project-relative-from-`src/` form, matching the `wld_internal.h` precedent
(`#include "client/graphics/eq/wld_internal.h"` in the four `wld_*` TUs);
`${CMAKE_SOURCE_DIR}/src` is on the global `INCLUDE_DIRECTORIES` list
(`CMakeLists.txt:195`), so no CMake change was needed to make the form work.
Each header carries the "PRIVATE … include from zone_shader.cpp only" comment.

Include **order** is as mandated — `_gles2`, `_gl`, `_callbacks`, all inside the
include block ahead of `namespace EQT {`, so the 26 literals and the three
classes are all declared before the constructor's 18 `new` sites.

### 6. ⚠ FINDING — **no build configuration in the tree compiles the desktop-GL arm**

`zone_shader_sources_gl.h`'s body is guarded by `#ifndef EQT_HAS_GLES2`.
`EQT_HAS_GLES2` is defined at `CMakeLists.txt:707` under `IF(EQT_GLES2)`.
All three configurations this unit built — `build/` (the GLES2 dev tree),
`build-gles2/` (`scripts/build-gles2-x86.sh`) and `build-arm-noble/`
(`scripts/build-arm-noble.sh`) — set `EQT_GLES2=ON`. **So the arm is compiled
by none of them.**

The only configuration that *would* compile it is `EQT_GRAPHICS=ON EQT_DRM=ON
EQT_GLES2=OFF`: `CMakeLists.txt:129` FATAL_ERRORs unless one of `EQT_DRM` or
`EQT_GLES2` is set, so `EQT_DRM` alone is the sole remaining door. **P32 retired
exactly that configuration** — `CMakeLists.txt:701-706` says the
`EQT_GLES2=OFF` "Desktop GL mode via Lima" branch was removed and
`build-arm-noble.sh` no longer offers `--no-gles2`. It is consistent with the
`.cpp` preamble, whose `#elif defined(EQT_HAS_DRM) → #include <GL/gl.h>` at
lines 6-7 pairs the desktop arm with `EQT_HAS_DRM`.

**So: 356 lines of GLSL 1.20 that no reachable configuration compiles.** This is
the batch's **seventh dead-content site** and belongs on the board with the other
six (`eq_ucs.cpp`'s 320 commented-out lines, `wld_loader.cpp`'s 278-line `#if 0`,
J06's tombstone block, …). ⚠ **Recorded, NOT acted on** — the plan is explicit
that this is "not a licence to delete it in this unit."

Because nothing in the bar compiles it, I added a **standalone syntax probe** so
the arm is not shipped unparsed:

```
$ printf '#include "client/graphics/zone_shader_sources_gl.h"\nint main(){return 0;}\n' > glarm_probe.cpp
$ g++ -fsyntax-only -Wall -UEQT_HAS_GLES2 -Isrc -Iinclude glarm_probe.cpp ; echo $?
0
$ # parity check on the other arm
$ g++ -fsyntax-only -DEQT_HAS_GLES2 -Isrc -Iinclude gles2_probe.cpp ; echo $?
0
```

**Both arms parse clean.** (Probe files live in the scratchpad, not the repo.)

### 7. CMake — **4 sites found, 4 predicted, 0 edited**

```
$ grep -n 'zone_shader\.cpp' CMakeLists.txt tests/CMakeLists.txt tests/cmake/*.cmake
CMakeLists.txt:485      SET(GRAPHICS_SOURCES
CMakeLists.txt:1656     ADD_EXECUTABLE(gles2_zone_benchmark      ← the visual-gate binary
CMakeLists.txt:1777     ADD_EXECUTABLE(gles2_shader_diagnostic
CMakeLists.txt:1860     ADD_EXECUTABLE(gles2_composite_audit
```

**Exactly four, all in the root file, zero in `tests/CMakeLists.txt` and zero in
the new `tests/cmake/*.cmake` fragments** (J20's split). This unit creates no
`.cpp`, so **nothing was added at any site and `CMakeLists.txt` is unmodified**.
`grep -c '\.h$' CMakeLists.txt` → **0**: the project lists no headers in CMake,
so the three new headers need no add-site either. Confirmed by the fact that the
build picked them up with no configure change.

### 8. The two content proofs

Original body = lines **14–2559** = **2,546** lines.
Reassembled body = **2,547** lines. Contributions, stated explicitly per
correction 7:

| Source | Header/TU lines | Original range | Count |
|---|---|---|---|
| `zone_shader_sources_gles2.h` | 10–1013 | 14–1017 | 1,004 |
| `zone_shader_sources_gles2.h` | 1014 | *(new `#endif`)* | 1 |
| `zone_shader_sources_gl.h` | 10 | *(replaces 1018)* | 1 |
| `zone_shader_sources_gl.h` | 11–366 | 1019–1374 | 356 |
| `zone_shader_sources_gl.h` | 367 | *(replaces 1375)* | 1 |
| `zone_shader.cpp` | 16–47 | 1376–1407 | 32 |
| `zone_shader_callbacks.h` | 20–700 | 1408–2088 | 681 |
| `zone_shader.cpp` | 48–518 | 2089–2559 | 471 |
| | | **total** | **2,547** |

**Proof A — byte-identical re-concatenation, in original order:**

```
=== line counts: before 2546 / after 2547 ===
=== RECONCAT DIFF ===
1005c1005,1006
< #else // !EQT_HAS_GLES2 — Desktop GL / GLSL 1.20
---
> #endif // EQT_HAS_GLES2
> #ifndef EQT_HAS_GLES2   // was the `#else` arm of zone_shader.cpp:1018
1362c1363
< #endif // EQT_HAS_GLES2
---
> #endif // !EQT_HAS_GLES2
```

**Two hunks, three changed lines, and they are the unit's ONLY non-verbatim
change.** All 2,544 other lines — including every one of the 1,362 GLSL lines
and all 681 lines of callback code — are byte-identical.

**Proof B — line multiset (`sort`, never `sort -u`):**

```
=== MULTISET DIFF ===
723d722
< #else // !EQT_HAS_GLES2 — Desktop GL / GLSL 1.20
736a736
> #endif // !EQT_HAS_GLES2
1287a1288
> #ifndef EQT_HAS_GLES2   // was the `#else` arm of zone_shader.cpp:1018
```

**The same three lines, and nothing else** — the multiset is otherwise exactly
preserved, so no GLSL line was duplicated into two files or dropped from both.
This is the check that mattered: HAZARD 2 is real (the two arms share many
identical `uniform`/`varying`/`}` lines, and `verify_move_only.py` allowlists a
bare `}`), and only a counting comparison can see through it.

⚠ **Correction 17 bit, exactly as warned — and the proof was wrong before the
code was.** My first `body()` helper mis-derived header B's offsets (it emitted
its `#endif` twice and swept up a trailing blank), producing a 2,548-line result
and a 4-line diff that read like a stray inserted line. **Suspect the proof
first**: `local`-scoping was already correct, but the *range arithmetic* is the
other half of that lesson and the plan does not say so. **Recommend generalising
correction 17: derive proof ranges from the produced files' own structure, and
treat any proof failure whose diff is pure whitespace or pure preprocessor as a
proof bug until shown otherwise.**

**Verifier — exit 0, as predicted (correction 23 holds):**

```
$ BASE=$(git -C /mnt/projects/fable/wt/slot2 merge-base fable HEAD)   # 8b0dfcfb…
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/slot2

Files verified: 4
Boilerplate: +41 / -1 lines (allowed)

PASS: move-only (modulo boilerplate).
VERIFIER_EXIT=0
```

Run **after** committing (`af5144b9`), **with `--repo`**, **against the fork
point** — all three of the documented traps avoided. **+41/-1**: the 41 are
3 `#pragma once`, 13 comment lines, 6 `namespace … {`, 6 `}` closes, 12
`#include` (9 copied into the callbacks header + 3 new in the .cpp), plus the
new `#endif`/`#ifndef`/`#endif`; the −1 is the original `#else`. **This is the
first unit in the batch to pass the verifier while splitting a live conditional
across a file boundary, and the first since J06 to need no sign-off exception.**

### 9. Final line counts, timings, wave effects

| File | Plan estimate | **Actual** | Δ |
|---|---|---|---|
| `src/client/graphics/zone_shader.cpp` | ~525 | **520** | −5 |
| `src/client/graphics/zone_shader_sources_gles2.h` | ~1,010 | **1,017** | +7 |
| `src/client/graphics/zone_shader_sources_gl.h` | ~365 | **370** | +5 |
| `src/client/graphics/zone_shader_callbacks.h` | ~700 | **702** | +2 |
| | | 2,561 → **2,609** | +48 boilerplate |

`check_file_sizes.sh --files` on all four: **4 ok, 0 warn, 0 fail.** The largest
(1,017) is comfortably under the 1,500 WARN line, so the contingency fourth
header was not needed. Correction 12 holds again — cluster membership exact,
arithmetic off by −5…+7.

| Step | Result | Time |
|---|---|---|
| `cmake --build build -j24` | **exit 0**, first invocation, no SIGTERM | ~4 min |
| `ctest -j6` | **3,498 / 3,499**; sole failure `AudioManagerTest.PreloadCommonSounds` (the documented ALSA host quirk) | **399.4 s** |
| `scripts/build-gles2-x86.sh` | **exit 0** | ~3 min |
| `scripts/ci/visual_gate.sh` | **PASS, first run**, `git status --porcelain` empty | ~1 min |
| `scripts/build-arm-noble.sh` | **exit 0, FIRST invocation** — no 143 | ~5 min |
| `check_file_sizes.sh --files` | 4 ok, 0 warn, 0 fail | instant |

**No exit-143 build, so the ELF-magic truncation scan was not triggered.** No
`HeadlessShutdownTest` flake. **Zero re-runs of anything.** Total ~35 min wall
clock, of which the cut plus both proofs was **under 6 minutes** — the bar is
still the whole cost.

**Visual gate detail** (goldens untouched, zero image mismatches, all three
zones well under ceiling): qeynos2 p95 CPU **3.884** (ceiling 10.108), gfaydark
**4.915** (10.672), befallen **6.838** (10.948). Notably the gate did **not**
flake despite J08 and J10 running concurrently — correction 14's "may happen,
not will" is right again. **Concurrency with J08/J10 cost this unit nothing
measurable**; `ctest -j6` at 399 s is between J02's three-wide 6.8 min and
J06's solo-beating 120.7 s.

**⚠ The load-bearing gate result for THIS unit is that the gate passed at the
RENDER step at all.** `gles2_zone_benchmark` compiles `zone_shader.cpp`, and
this unit changed what that TU includes; a render-step pass is the direct
evidence that all 26 shader sources and all three callback classes reached the
compiler in the right order.

### 10. Findings only — recorded, not acted on

1. **The desktop-GL arm is unreachable** — §6 above. 356 lines of GLSL 1.20 that
   no configuration in the tree compiles, kept because P32 retired the door
   rather than the code. **Batch dead-content site #7.**
2. **Doc rot the split invalidates** (comment-only, outside a move-only commit):
   - `docs/future/shader_permutation_handoff.md:73` — "Zone shader
     implementation: `src/client/graphics/zone_shader.cpp`" is now four files.
   - `docs/future/shader_permutation_handoff.md:405` — the change-table row for
     `zone_shader.cpp` describes work now split across the .cpp and
     `zone_shader_sources_*.h`.
   - `docs/future/shader_permutation_handoff.md:407` — **``| `ShaderCallback`
     (in zone_shader.cpp) |``** names the wrong file; it is now
     `zone_shader_callbacks.h`. Also `:299`/`:313` discuss `ShaderCallback`
     without a path.
   - `docs/reference/shader_overview.md:134` — **"Files:
     `include/client/graphics/zone_shader.h` + `src/client/graphics/zone_shader.cpp`"**
     now under-describes the module by three headers.
   **Fold into the `docs/module_map.md` hygiene pass J02/J04/J06/J07 already
   owe** — that pass now has five units' worth of entries.
3. **`docs/module_map.md` has no `zone_shader` entry at all**, so nothing there
   went stale — but the new four-file shape is worth adding in the same pass.
4. **No unreachable code found inside the moved ranges.** All 26 shader sources
   are named by the constructor, all three callback classes are instantiated
   (7 + 4 + 7 = 18 times), and both LUT helpers have live callers at 2399/2431.
5. **`tests/`+`scripts/` pre-check re-run at my tip: still ZERO hits for
   `zone_shader` of any kind** — no functional readers, no comment mentions, no
   line-number citations to rot. Unique in the batch so far, and the reason this
   unit needed no territory expansion (contrast correction 10 / J11).

### What the template still gets wrong

- **§Acceptance's "expect exactly 2 differing lines" contradicts §"Exact header
  shapes"**, which prescribes the `!EQT_HAS_GLES2` comment retitle that makes it
  three. Harmless here because the plan also demanded the diff be pasted and
  enumerated — but a unit that trusted the count over the paste would have
  reported a spurious anomaly. **Where a plan predicts a diff size, it must
  derive the number from its own prescribed output, not from the semantic
  intent.**
- **Correction 17 is stated too narrowly.** "Declare count variables `local`"
  names one instance of a general rule; the failure that actually occurred here
  was **range arithmetic against the produced file's line numbering**, with no
  variable scoping involved. See §8.
- **The plan's "1 TU + 3 private headers" call was right and worth the space it
  took.** Re-deriving why the survey's "3 TUs + 2 hdrs" is impossible would have
  cost real time; having the `static`-linkage and 100%-inline arguments written
  out meant the shape was never in question. **Wave-4 plans should keep doing
  this wherever they overrule a survey row.**

