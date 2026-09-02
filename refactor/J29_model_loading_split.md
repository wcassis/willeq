# J29 — Batch J Wave 6: split `src/client/graphics/eq/model_loading.cpp` (1,793 → 3 TUs, largest ~683)

**Slot: assigned at dispatch. Branch `task/J29-model-loading-split` cut from `fable`.
x86 only. No Pi hardware. No eqemu.**

Wave 6 of `refactor/J_file_size_reclamation.md`. Read, before editing anything:
**`refactor/J_file_size_reclamation.md` §Process per unit AND all eleven
correction sections (thirty-three numbered lessons from seventeen executed
units; newer text wins over older)**, then
**`refactor/J13_eq_bridge_split.md` including its Review** — the batch's
best-instrumented unit and the model for how a text-scanning test guard is
handled — and `refactor/J12_eq_action_handler_split.md` §Review. Then
`refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## MOVE-ONLY. No behaviour changes, no interface changes.

Method definitions move **verbatim**. **No header changes.** **No method
decomposition** — §Optional of the batch plan is explicit that a unit which
decomposes a dominant method is out of scope. No drive-by fixes. No reordering.
No deletion of dead content: a J unit **records** it and **moves it verbatim**.

## Measured at `824adb28` (2026-09-02, `fable` tip after J13 merged)

`src/client/graphics/eq/model_loading.cpp` — **1,793 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **1,793** |
| preamble | **1-17** — a 2-line file-header comment, ten `#include` lines, then `namespace EQT {` / `namespace Graphics {` at 16-17. **Zero non-boilerplate lines.** |
| namespace | `EQT` / `Graphics` opened at **16-17**, closed at **1792-1793**. **Both closes replicate into each new TU.** ⚠ There is also `namespace fs = std::filesystem;` at **1246** — that is a **method-local alias inside `searchZoneChrFilesForModel`**, not a namespace open; it travels with its method. |
| method definitions | **17** |
| largest method | **326** `RaceModelLoader::loadModelFromCachedChr` (867-1192); 2nd **302** `loadModelFromZoneChr` (564-865); 3rd **268** `searchZoneChrFilesForModel` (1228-1495) |
| `// ====` banners | **zero** |
| conditional preprocessor directives | **zero.** No conditional compilation anywhere in the file. |
| file-scope `static` / anonymous namespaces | **zero of both.** |
| multi-line comment regions | **zero** — `grep -n "/\*"` returns nothing at all, so comment depth is 0 on every line. |

⚠ **Correction 33: "no block comments" must mean "no MULTI-LINE comment
regions".** Single-line `/* … */` argument comments are irrelevant; what the
cutting rule needs is **comment depth 0 at every cut**. Compute it, do not
assert it, and beware the trap `eq_movement.cpp:18` set for the orchestrator's
own scanner: a `//` line containing the token `*/` makes a naive depth counter
go negative. Strip `//` tails before counting:

```bash
python3 - <<'EOF'
import re
L=open('src/client/graphics/eq/model_loading.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {564,1228}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-17 | preamble: the 2-line header comment, ten includes, two namespace opens | copied to each new TU, with 1-2 adapted | 17 |
| **18-563** | `loadGlobalModels` (18-…) / `loadNumberedGlobalModels` (51-88) / `getArmorTexture` (101-137) / `loadZoneModels` (139-201) / `loadModelFromS3D` (203-278) / `loadModelFromGlobalChr` (280-416) / `loadModelFromNumberedGlobal` (418-562) — archive discovery and the global/S3D load paths | **stays** `model_loading.cpp` | **565** |
| **564-1227** | `loadModelFromZoneChr` (**564-865, 302 lines**) / `loadModelFromCachedChr` (**867-1192, 326 lines — the file's largest, moved WHOLE and VERBATIM**) / `getVariantModelData`-adjacent tail through 1227 — the two big `.chr` load paths | `model_loading_chr.cpp` | **683** |
| **1228-1793** | `searchZoneChrFilesForModel` (**1228-1495, 268 lines**, containing the `namespace fs = std::filesystem;` alias at 1246) / `removeMeshRef` (1502-1596) / `releaseRawTextureData` (1598-1659) / `getMemoryStats` (1661-1790) — zone-file search and the memory/lifetime accounting | `model_loading_search.cpp` | **583** |

**Result: 3 TUs** (`model_loading.cpp` stays and counts). Largest **~683**.

Arithmetic that must hold: **`17 + 546 + 664 + 566 = 1793` (the third range carries the original's two namespace closes)**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundaries fall where they do: only **17 methods** in 1,793 lines, and
three of them are 326, 302 and 268 — **the file splits three ways naturally, one
big sibling per TU.** **18-563** is discovery and the global/S3D paths.
**564-1227** is the two `.chr` loaders. **1228-1793** is search plus the memory
accounting that owns the loaded meshes' lifetime.

⚠ **None of the three big methods is to be decomposed.** §Optional of the batch
plan is explicit. The floor here is `17 + 326 = 343`, and the largest TU lands at
683 — **817 lines of headroom.** No Batch-G decision is needed or wanted.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 564 | `562: }` (end of `loadModelFromNumberedGlobal`), `563` blank | `564: bool RaceModelLoader::loadModelFromZoneChr(const std::string& zoneName, uint16_t raceId, uint8_t gender) {` |
| 1228 | `1226: }`, `1227` blank | `1228: bool RaceModelLoader::searchZoneChrFilesForModel(uint16_t raceId, uint8_t gender) {` |

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/graphics/eq/model_loading.cpp
```

## ⚠ HAZARD 1 — `variant_loading.cpp` defines more `RaceModelLoader` methods and is J35's file

`RaceModelLoader` is already split across at least
`src/client/graphics/eq/model_loading.cpp` and
`src/client/graphics/eq/variant_loading.cpp`. **J35 splits the latter in this
same wave.** The two file territories are disjoint and the class relationship
creates no build dependency — every method is a class member with external
linkage — **but the five shared root CMake add-sites are the same five lines**
(`GRAPHICS_SOURCES`, `model_viewer`, `gles2_zone_benchmark`,
`gles2_shader_diagnostic`, `gles2_composite_audit`) and **two shared test
targets** (`test_graphics`, `test_asset_loading`).

⚠ **J29 and J35 will conflict in CMake if run concurrently.** They are textually
adjacent inserts into the same source lists. **Merge them one at a time and
rebase** — §Process already requires this, and correction 31 records what happens
when two units touch one file at once.

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `model_loading.cpp`:**

`model_loading.cpp` has **zero file-scope `static` symbols, zero anonymous
namespaces, and zero file-scope constants.** Every symbol it defines is a
`RaceModelLoader` member with external linkage, declared in
`include/client/graphics/eq/race_model_loader.h`.

**Therefore correction 21 has NO exposure in this file.** The one thing that
*looks* like a namespace-scope entity — `namespace fs = std::filesystem;` at
**1246** — is **indented, inside `searchZoneChrFilesForModel`'s body**, so it is a
block-scope alias and travels with its method into range 1228-1793. Confirm both
with your own greps and paste them:

```bash
grep -n '^static \|^namespace\s*{\|^const \|^constexpr ' \
    src/client/graphics/eq/model_loading.cpp
grep -n 'namespace' src/client/graphics/eq/model_loading.cpp
```
(the first: no output, exit 1. The second: five lines — 16, 17, **1246
(indented, method-local)**, 1792, 1793.)

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `model_loading.cpp`'s preamble, lines 1-17, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

There is no conditional compilation and nothing non-boilerplate in the preamble,
so the copy is unconditional and total: **lines 1-17 verbatim into each new TU**,
with lines 1-2 adapted per §File-header comments.

**Each new TU ends with the two namespace closes, replicated verbatim:**
```
} // namespace Graphics
} // namespace EQT
```
⚠ **Correction 30**: replicating "blank + closes" double-spaces any TU whose body
already ends on a blank. Declare it; do not hand-fix it.

## File-header comments

`model_loading.cpp` has a **2-line** header comment
(`// RaceModelLoader model loading methods - split from race_model_loader.cpp` /
`// These methods remain part of the RaceModelLoader class`). **Each of the three
TUs gets its own adapted version** — same shape, naming that TU's clusters and
citing `refactor/J29_model_loading_split.md`. **The stays file's must be updated
too.**

`model_loading_chr.cpp`'s header must record that **`loadModelFromCachedChr` is
326 lines and `loadModelFromZoneChr` is 302, both recorded Batch-G candidates
J29 deliberately did not touch.**

Comment lines are allowlisted boilerplate for `verify_move_only.py`, so header
edits are permitted — but they **are** visible in the line-multiset check, so
**enumerate every header line you wrote or changed in your Review**, exactly as
J07 did. **Change no other comment.**

⚠ **Correction 30: the tail convention double-spaces some TUs.** Replicating
"blank + namespace close" produces a double blank line in any TU whose body
already ends on a blank. It is cosmetic and invisible to both proofs and the
verifier — **declare it, do not hand-fix it.**

## CMake — grep, never trust a line number

```bash
grep -rn 'model_loading\.cpp' CMakeLists.txt tests/ scripts/
```

⚠ **NEVER name `tests/CMakeLists.txt` in a grep (correction 26).** J20 split it
into 18 per-domain fragments under `tests/cmake/`; the top-level file is now 135
lines and holds no test targets. A plan that greps `CMakeLists.txt
tests/CMakeLists.txt` **finds a subset of the add-sites and looks complete** —
J10 would have found nine of twelve that way. **`grep -rn` across `tests/`.**

⚠ **CMake commands in this project are UPPERCASE** (correction 12) —
`grep -n 'set('` and `grep -n 'add_executable('` both return nothing, silently,
which reads exactly like "no sites here". **Grep for the filename.**

⚠ **Add-site counts have ranged from 1 to 19 across this batch** (J01 1, J02 1,
J04 5, J05 19, J08 2, J09 4, J10 12, J12 1, J13 1). **Report the count you
actually found**, and if it differs from the SEVEN below, say so loudly.

At `824adb28` this file has **SEVEN real add-site(s)**:

| file:line | variable / target | edit? |
|---|---|---|
| `CMakeLists.txt:458` | `SET(GRAPHICS_SOURCES` (opens at `:439`) | ⚠ **YES** |
| `CMakeLists.txt:826` | `ADD_EXECUTABLE(model_viewer` (`:798`) | ⚠ **YES** |
| `CMakeLists.txt:1688` | ⚠ `ADD_EXECUTABLE(gles2_zone_benchmark` (`:1646`) — **the visual-gate binary** | ⚠ **YES** |
| `CMakeLists.txt:1812` | `ADD_EXECUTABLE(gles2_shader_diagnostic` (`:1776`) | ⚠ **YES** |
| `CMakeLists.txt:1898` | `ADD_EXECUTABLE(gles2_composite_audit` (`:1862`) | ⚠ **YES** |
| `tests/cmake/graphics.cmake:29` | `ADD_EXECUTABLE(test_graphics` (`:20`) | ⚠ **YES** |
| `tests/cmake/graphics.cmake:114` | `ADD_EXECUTABLE(test_asset_loading` (`:99`) | ⚠ **YES** |
| `tests/cmake/ui_rendering.cmake:43` | a comment mentioning `test_race_model_loading.cpp` | no |

⚠ **Two of the seven are TEST targets in `tests/cmake/graphics.cmake`**, a
fragment that did not exist before J20 (correction 26). A plan that greps
`CMakeLists.txt tests/CMakeLists.txt` finds **five of seven** and looks complete.

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'model_loading' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

At `824adb28` the first grep returns **comment-only hits and zero functional
readers**, and the second — the exhaustive list of source paths any test opens as
text — **does not contain this file**. **No repoint is needed.** (J11 was the
cautionary case: four tests read `irrlicht_renderer.cpp` as text; three would
have failed and one would have **silently asserted nothing**. J25 and J31 in this
same wave both have that problem. You do not.)

⚠ **Re-run both at your tip and paste them.** If a text reader has appeared, add
it to territory and repoint it — that is correction 10, not scope creep.

⚠ **`tests/test_first_person_camera_single_formula.cpp` sweeps EVERY `.cpp` and
`.h` under `src/client/graphics` RECURSIVELY** (`kGraphicsDir`,
`fs::recursive_directory_iterator`) **and keys two of its three assertions on
FILENAME.** Any TU this unit creates under that tree enters the sweep
automatically — **this is a third kind of text-scanning guard, one that names no
file and therefore appears in NEITHER of the two standard greps.** The rules a
new TU must not break:

- **Part 1** — a file that both mentions `CameraMode::FirstPerson` **and** writes
  `camera_->setPosition(` / `camera_->setTarget(` is an offender unless its
  filename is `renderer_camera.cpp` (`kHelperTU`).
- **Part 2** — each first-person tunable
  (`kFirstPersonPitchDownDegrees`, `kFirstPersonHorizontalFovDegrees`,
  `firstPersonMaxDownPitchDegrees`, `firstPersonMinLookPitchDegrees`,
  `firstPersonVerticalFovRadians`, `computeFirstPersonCamera`) may be read from
  **at most one** TU under that tree, except `firstPersonMinLookPitchDegrees`
  which may be read from two — `renderer_camera.cpp` and
  **`renderer_player_physics.cpp`, named as a string literal (`kFloorSecondReader`)**.

**Check your file before cutting and paste the result:**

```bash
grep -n 'CameraMode::FirstPerson\|camera_->setPosition(\|camera_->setTarget(\|kFirstPerson\|firstPersonM\|firstPersonV\|computeFirstPersonCamera' \
    src/client/graphics/eq/model_loading.cpp
```

At `824adb28` **`model_loading.cpp` matches none of those tokens**, so it is
invisible to both parts of the sweep and the new TUs will be too. **Confirm at
your tip and paste the (empty) grep** — this guard names no file, so neither
standard grep would warn you.

## Acceptance (the Batch J bar — every item, classified by EXIT CODE)

- **build clean**: `cmake --build build -j24`. Classify by **EXIT CODE**, never
  by grepping the log — `grep ' error '` **never** matches GCC, which prints
  `error:`. Capture `BUILD_EXIT=$?` **immediately**.
- ⚠ **A long build can end two ways and they are not the same** (correction 23):
  killed at the 600 s tool ceiling with **exit 143 (SIGTERM)**, or
  **backgrounded by the tool** and later reporting its own exit code (J08's ARM
  build did the latter — ~11 min, exit 0, binaries fine). **Only a real 143
  invalidates binaries.**
- ⚠ **After a real exit 143, scan every produced binary for valid ELF MAGIC, not
  size** (corrections 27 and 32). J10's truncated files were 50-71 MB with
  all-zero ELF headers; the orchestrator's was 377 MB. **No size heuristic
  catches these.** And the scan's outcome is **not predictable from the exit
  code**: a killed *link* makes `gmake` delete its own partial output (J13 — the
  target was *absent*, and the scan correctly found nothing), while a killed
  *compile* can leave a truncated file behind (J07 twelve, J10 three, J12 two).
  **Absent and truncated are different failures with the same trigger. Run the
  scan either way; it costs seconds.**
  ```bash
  for f in build/bin/*; do
    [ -f "$f" ] || continue
    readelf -h "$f" >/dev/null 2>&1 || echo "BAD $f"
  done
  ```
  (correction 29: `readelf -h` is stronger than the `head -c 4 | od` recipe and
  equally cheap.) `rm` every hit, then rebuild once.
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`.
  - ⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known **LOAD-DEPENDENT FLAKE**, not a regression, and is **not** in
    `test_baseline.txt`. **Do not re-diagnose it** — re-run it alone before
    reporting anything.
  - ⚠ `ctest` **exit 8 during `GoogleTestAddTests.cmake` discovery is not
    necessarily the truncated-binary trap** (correction 16): check the binary
    with `--gtest_list_tests` before concluding a mid-link truncation.
  - ⚠ `test_irrlicht_camera_command` has 8 parameterized test NAMES embedding an
    ASLR-randomised pointer; they change on every relink. If you diff
    `ctest -N`, redact `72-byte object <...>` first.

- ⚠ **the verifier — COMMIT FIRST, THEN VERIFY:**
  ```bash
  git -C /mnt/projects/fable/wt/<your slot> commit ...      # FIRST
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **There are THREE distinct ways to get a confident-looking wrong answer, and
  two of them share a signature:**
  1. **Omitting `--repo`.** It defaults to `.`, and an executor agent's shell
     cwd is **not** its slot (this project forbids `cd`; agent threads reset cwd
     between calls). Omitting it silently verifies `/mnt/projects/fable/main`
     and prints a loud FAIL full of some unrelated merged unit's lines. J02 hit
     exactly this.
  2. **The wrong base rev.** `fable..HEAD` is **WRONG** — units merge during a
     wave, so `fable` moves under you and `fable..HEAD` reports the *other*
     units' merged work, in reverse, as substantive removals. J01 saw 533
     spurious removals this way. **Use the fork point.**
     ⚠ **Failures 1 and 2 look IDENTICAL** and have different causes. An
     executor who "fixes" the base and re-runs without `--repo` is still
     verifying the wrong tree. **Check both before changing a line of code.**
  3. ⚠ **Running on an UNCOMMITTED tree.** The script prints
     **`No C++ files changed` and EXITS 0.** J06 hit this. It looks like a
     definitive pass and is not one.
  Record `$BASE` in the Review and paste the full output.
- ⚠ **Expected verifier result for J29: **EXIT 0, `+0/-0` substantive.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied preamble (1-17) is **100% boilerplate**: two `//` lines, ten
    `#include`, two `namespace … {`, and blanks the normalizer drops.
  - The replicated namespace closes are bare `}` with a `//` tail — boilerplate.
  - Header-comment adaptations cost zero (`//`).
  - No non-boilerplate preamble content, no internal header, no test repoint, no
    preamble deviation.
  - **Total: ADDED 0, REMOVED 0 → exit 0.**

  Paste the full output and enumerate every flagged line. **Any line beyond the
  enumerated set is a real finding — chase it.** **NEVER contort code to force a
  green exit** (J04 correctly refused to). If the verifier exits 1 with exactly
  the predicted set, that is the §Process step-4 / correction-5 case: **expected,
  not a failure** — request orchestrator sign-off in the Review.
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines and allowlists a bare `}` and every
  comment (correction 23), so a range duplicated into two TUs, or dropped from
  both, is invisible to it. **This check is not optional.**
  ```bash
  cat <the 3 TU bodies, each stripped of its preamble and namespace close> \
      | sort > /tmp/after.txt
  git show "$BASE:src/client/graphics/eq/model_loading.cpp" | sed -n '18,1791p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Strip the two namespace closes from each TU before comparing**, and note
  that the original's own closes (1792-1793) are inside range 1228-1793 — so the
  `before` side must run to **1791**, not 1793. Getting this off by two lines
  produces a diff that reads exactly like a dropped range: **when a proof fails,
  suspect the proof first** (correction 20).
  ⚠ **Additionally and separately, prove `loadModelFromCachedChr` survived byte
  for byte** — 326 lines, 18% of the file, and the thing a reviewer looks at
  first:
  ```bash
  diff <(git show "$BASE:src/client/graphics/eq/model_loading.cpp" | sed -n '867,1192p') \
       <(sed -n '<its new range>p' src/client/graphics/eq/model_loading_chr.cpp)
  ```
  **326 lines, exit 0. Paste it.**
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`18-563, 564-1227, 1228-1791`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/graphics/eq/model_loading.cpp" | sed -n '18,1791p'`.
  **1,774 lines. Expect `cmp` exit 0.**
  ⚠ **Declare count variables `local` in your proof helpers.** More generally
  (correction 20): **when a proof fails, suspect the proof first.** J05's
  non-`local` `n` produced a 247-line shortfall that read exactly like a dropped
  range; J09's one proof failure was range arithmetic against the produced
  files' numbering; J08's failed **exactly at its relocation seam**. **Three
  units, three proof bugs, zero real defects.**

- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report** rather than re-blessing.
  - ⚠ **A gate FAIL is not necessarily a PIXEL fail — check WHICH metric
    failed** (correction 15). J05's first run failed on `befallen` **p95 CPU
    11.684 vs a 10.948 ceiling with ZERO image mismatches**; the re-run measured
    4.676. That is a 2.5x swing on a 2x ceiling under wave load: **contention,
    not your change. Re-run.**
  - ⚠ **A failure at the RENDER step is a different animal**: it means a
    `gles2_zone_benchmark` CMake add-site was missed. ⚠ **This unit DOES touch `gles2_zone_benchmark`** (the binary `scripts/ci/visual_gate.sh` runs). A render-step gate failure means you missed that add-site — fix the add-site, do not re-run past it. Contrast the CPU-p95 failure above, which is contention (correction 15).
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ Correction 14: the wave-stretch figures on record are **worst cases
  observed, not forecasts.** J06's `ctest -j6` ran in 120.7 s — faster than
  J01's solo 3.0 min on a larger suite — and its ARM build completed first
  invocation. **Do not report a clean run as an anomaly.**
- `scripts/refactor/check_file_sizes.sh --files` on **every changed AND created
  file**: `src/client/graphics/eq/model_loading.cpp`, `src/client/graphics/eq/model_loading_chr.cpp`, `src/client/graphics/eq/model_loading_search.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **0**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **1,793** this plan measured,
   and any range drift.
2. **Every property in §Measured, re-verified with your own greps and pasted.**
   If any has changed since `824adb28`, **STOP and report**.
3. ⚠ **Correction 21, explicitly.** Paste the grep, name the range this plan
   assigns each file-local symbol and the range it assigns each consumer, and
   confirm they are the same range. **Do not cite §Correction 21 — re-derive it.**
4. **Comment depth at every cut**, computed with the `//`-stripping counter
   above, not asserted.
5. **The preprocessor dump**, pasted, confirming no cut lands inside a
   conditional region.
6. **Every file-header comment line you wrote**, enumerated.
7. **How many CMake add-sites you actually found**, against the SEVEN
   this plan names, and the output of both `tests/`/`scripts/` greps.
8. **Both content proofs**, pasted, and the verifier's exit code plus `$BASE`.
   State whether the result matched the prediction; if it did not, enumerate
   every flagged line before concluding anything.
9. **Final line counts of every file**, method counts per TU
   (this plan predicts 7 / 3 / 7 of the 17), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. **The seven CMake add-sites, each confirmed** — five root, two in
    `tests/cmake/graphics.cmake`. ⚠ **One of them is `gles2_zone_benchmark`**:
    if you miss it the visual gate fails at the RENDER step, which reads exactly
    like a behaviour change (correction 15 disambiguates: a CPU-p95 failure is
    contention, a render failure is a missed add-site).
12. **The `test_first_person_camera_single_formula.cpp` check, pasted** — the
    empty grep proving neither new TU enters that recursive sweep's offender set.

## Territory

`src/client/graphics/eq/model_loading.cpp`, the two new TUs beside it
(`…_chr.cpp`, `…_search.cpp`), `CMakeLists.txt` (the five root sites),
`tests/cmake/graphics.cmake` (the two test sites), and this plan file.
**Nothing else** — not `include/client/graphics/eq/race_model_loader.h`, and in
particular **NOT `src/client/graphics/eq/variant_loading.cpp`**, which is
**J35's** file even though it defines methods of the same class.

⚠ **SERIALISATION with J35**: disjoint file territories, but **five shared root
CMake lines and two shared test targets**. Merge one at a time and rebase; do not
resolve a CMake conflict by taking one side wholesale (correction 31).

## STOP conditions

- A file-scope symbol, an anonymous namespace, or a multi-line comment region
  has appeared in a place §Measured says there is none.
- Any cut boundary does not match §Every cut boundary at your tip and the drift
  is more than a few lines of uniform offset.
- The verifier reports any substantive line beyond the predicted set.
- Either content proof fails after you have satisfied yourself the proof itself
  is correct.
- The visual gate fails on an **image mismatch** (as opposed to the CPU p95
  metric).
- You find yourself wanting to decompose a method, delete dead content, trim an
  include block, or add a TU this plan does not name.
