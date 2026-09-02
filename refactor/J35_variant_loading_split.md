# J35 — Batch J Wave 6: split `src/client/graphics/eq/variant_loading.cpp` (1,607 → 3 TUs, largest ~833)

**Slot: assigned at dispatch. Branch `task/J35-variant-loading-split` cut from `fable`.
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

## ⚠ READ THIS FIRST: the batch plan calls this file the batch's hardest case. **It is not.**

`refactor/J_file_size_reclamation.md` §Residual says:

> ⚠ **the batch's worst floor: 2 methods = 1,211 of 1,607 lines.** A 2-way split
> works (~850 / ~780) but leaves no headroom.

and its scheduling note calls this file **"the strongest candidate in the tree for
a Batch-G decision taken *before* its Batch-F split rather than after."**

**Measured at `824adb28`, that conclusion does not survive arithmetic. There is
no Batch-G decision owed here, and this unit is a plain Batch-F relocation.**

The premise of the worry is that the two dominant methods must end up in the same
TU. **They must not, and this plan puts them in different ones:**

| | lines | 815-line method | 396-line method |
|---|---|---|---|
| `variant_loading.cpp` (stays) | **383** | — | — |
| `variant_loading_animation.cpp` | **833** | ✅ here | — |
| `variant_loading_preload.cpp` | **425** | — | ✅ here |

**The largest TU is 833 lines — 667 below the ceiling.** The floor for any TU is
`preamble + largest method` = `15 + 815` = **830**, and 830 < 1,500 with 670 to
spare. **A file is decomposition-first for the purpose of the ceiling only when
its floor EXCEEDS the ceiling**, which is the test the batch plan's own
§Headline finding sets — and this file passes it comfortably.

**What the §Residual note actually measured was a TWO-way split** (`~850 / ~780`),
which does leave the larger TU tight. **A three-way split at the two legal cut
points 382 and 1198 does not.** The cut at 1198 sits between the two dominant
methods, and it is a legal boundary today.

**Recommendation to the orchestrator, with the numbers to put it to Will if he
wants it anyway:** *no decision is needed.* Should Will still want the 815-line
`loadModelFromGlobalChrWithVariantsForAnimation` decomposed, that is **Batch G,
separately, after this unit**, and the case for it is readability, not the
ceiling. §Optional of the batch plan lists it as one of eight such methods
alongside `DispatchZonePacket` (880), `updatePlayerMovement` (806) and
`registerBuiltinCommands` (768) — **all four have now been relocated whole by a
Batch-F unit and flagged**, which is the precedent this unit follows.

⚠ **The same question was asked of `renderer_player_physics.cpp` (806 of 1,546)
and answered the same way — see `refactor/J33_renderer_player_physics_split.md`.**

**This plan is written LAST in Wave 6 for that reason, and its answer is: split
it mechanically, three ways, and flag both methods.**

## Measured at `824adb28` (2026-09-02, `fable` tip after J13 merged)

`src/client/graphics/eq/variant_loading.cpp` — **1,607 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **1,607** |
| preamble | **1-15** — a 2-line file-header comment, nine `#include` lines, then `namespace EQT {` / `namespace Graphics {` at 14-15. **Zero non-boilerplate lines.** |
| namespace | `EQT` / `Graphics` opened at **14-15**, closed at **1606-1607**. **Both closes replicate into each new TU.** |
| method definitions | **6** |
| largest method | ⚠ **815** `RaceModelLoader::loadModelFromGlobalChrWithVariantsForAnimation` (382-1196); 2nd ⚠ **396** `preloadVariantModel` (1209-1604); 3rd **165** `getAnimatedMeshWithAppearance` (216-380). **Two methods are 1,211 of 1,607 lines — 75% of the file.** |
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
L=open('src/client/graphics/eq/variant_loading.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {382,1198}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-15 | preamble: the 2-line header comment, nine includes, two namespace opens | copied to each new TU, with 1-2 adapted | 15 |
| **16-381** | `getMeshForRaceWithAppearance` (17-75) / `loadModelFromGlobalChrWithVariants` (77-214) / `getAnimatedMeshWithAppearance` (216-380) — the three entry points that resolve a race+appearance to a mesh | **stays** `variant_loading.cpp` | **383** |
| **382-1197** | ⚠ `loadModelFromGlobalChrWithVariantsForAnimation` (**382-1196, 815 lines — the second-largest method in the tree after `DispatchZonePacket`, moved WHOLE and VERBATIM**) | `variant_loading_animation.cpp` | **833** |
| **1198-1607** | `getVariantModelData` (1198-1207) / ⚠ `preloadVariantModel` (**1209-1604, 396 lines, moved WHOLE and VERBATIM**) + the two namespace closes | `variant_loading_preload.cpp` | **425** |

**Result: 3 TUs** (`variant_loading.cpp` stays and counts). Largest **~833**.

Arithmetic that must hold: **`381 + 816 + 410 = 1607` (the third range carries the original's two namespace closes)**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundaries fall where they do: this file has only **six methods**, and
**both cuts fall on a plain method boundary between two of them.** The split is
forced by the arithmetic rather than chosen by topic — but the topics line up
anyway: **16-381** resolves a race and appearance to a mesh, **382-1197** is the
one giant animation-aware variant loader, and **1198-1607** is the variant cache
and its preloader.

⚠ **NEITHER dominant method is to be decomposed** (see the box at the top of this
plan and §Optional of the batch plan). `loadModelFromGlobalChrWithVariantsForAnimation`
lands in an 833-line TU with **667 lines of headroom**; `preloadVariantModel`
lands in a 425-line TU with **1,075**. **A unit that decomposes either is out of
scope.** Precedent: J02 (`DispatchZonePacket` 880 → its own ~965-line TU), J13
(`ProcessBridgeIntents` 580 → its own ~700-line TU), J33
(`updatePlayerMovement` 806 → stays in an 827-line TU). **All flagged, none
touched.**

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 382 | `380: }` (end of `getAnimatedMeshWithAppearance`), `381` blank | `382: bool RaceModelLoader::loadModelFromGlobalChrWithVariantsForAnimation(uint16_t raceId, uint8_t gender,` (a two-line signature continuing on 383) |
| 1198 | `1196: }` (end of the 815-line method), `1197` blank | `1198: std::shared_ptr<RaceModelData> RaceModelLoader::getVariantModelData(uint16_t raceId, uint8_t gender,` (a two-line signature continuing on 1199) |

⚠ **Both cut lines are the first of a TWO-LINE signature** (382-383 and
1198-1199). That is fine — the rule is about the boundary, not the signature —
but **paste 378-384 and 1194-1200 in your Review** so it is on the record that
neither signature was split. This is the most likely way to get this particular
file wrong.

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/graphics/eq/variant_loading.cpp
```

## ⚠ HAZARD 1 — the alternative two-way split, and why this plan rejects it

**A two-way split at 382 is legal and simpler**: `variant_loading.cpp` 383 lines
+ `variant_loading_animation.cpp` `15 + 1226 = 1,241`. It clears the ceiling.

**This plan rejects it**, because 1,241 leaves **259 lines of headroom in a file
whose two methods have grown before**, and the batch plan's whole §Residual
warning about this file is that *the next feature unit to touch it re-crosses the
line*. T166 exists because `renderer_zone_load.cpp` had to be re-split for
exactly that reason. **The three-way split costs one extra file and buys 408
extra lines of headroom in the largest TU.**

**Do not take the two-way option.** If you believe you must, that is a STOP
condition: report it and let the orchestrator decide.

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `variant_loading.cpp`:**

`variant_loading.cpp` has **zero file-scope `static` symbols, zero anonymous
namespaces, and zero file-scope constants.** Every symbol it defines is a
`RaceModelLoader` member with external linkage, declared in
`include/client/graphics/eq/race_model_loader.h` — including
`makeVariantCacheKey`, which `getVariantModelData` calls at 1200 and which is
**declared in that header, not here** (`grep` it and confirm).

**Therefore correction 21 has NO exposure in this file** — there is no file-local
helper that could be separated from its consumer. Confirm with your own greps and
paste them:

```bash
grep -n '^static \|^namespace\s*{\|^const \|^constexpr \|^extern ' \
    src/client/graphics/eq/variant_loading.cpp
grep -n 'namespace' src/client/graphics/eq/variant_loading.cpp
grep -rn 'makeVariantCacheKey' include/ | head
```
(the first: no output, exit 1. The second: exactly four lines — 14, 15, 1606,
1607. The third: the declaration in `race_model_loader.h`.)

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `variant_loading.cpp`'s preamble, lines 1-15, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

There is no conditional compilation and nothing non-boilerplate in the preamble,
so the copy is unconditional and total: **lines 1-15 verbatim into each new TU**,
with lines 1-2 adapted per §File-header comments.

**Each new TU ends with the two namespace closes, replicated verbatim:**
```
} // namespace Graphics
} // namespace EQT
```
⚠ **Correction 30**: replicating "blank + closes" double-spaces any TU whose body
already ends on a blank. Declare it; do not hand-fix it.

## File-header comments

`variant_loading.cpp` has a **2-line** header comment
(`// RaceModelLoader variant loading methods - split from race_model_loader.cpp` /
`// These methods remain part of the RaceModelLoader class`). **Each of the three
TUs gets its own adapted version** — same shape, naming that TU's methods and
citing `refactor/J35_variant_loading_split.md`. **The stays file's must be updated
too.**

⚠ **`variant_loading_animation.cpp`'s header must record, in plain words, that
`loadModelFromGlobalChrWithVariantsForAnimation` is 815 lines, that it is a
recorded §Optional Batch-G decomposition candidate, that J35 deliberately did not
touch it, and that the TU has 667 lines of ceiling headroom.** Same for
`preloadVariantModel` (396) in `variant_loading_preload.cpp`. **Those two
sentences are what stop the next reader from re-opening the question this plan's
opening box closes.**

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
grep -rn 'variant_loading\.cpp' CMakeLists.txt tests/ scripts/
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
| `CMakeLists.txt:461` | `SET(GRAPHICS_SOURCES` (opens at `:439`) | ⚠ **YES** |
| `CMakeLists.txt:829` | `ADD_EXECUTABLE(model_viewer` (`:798`) | ⚠ **YES** |
| `CMakeLists.txt:1690` | ⚠ `ADD_EXECUTABLE(gles2_zone_benchmark` (`:1646`) — **the visual-gate binary** | ⚠ **YES** |
| `CMakeLists.txt:1814` | `ADD_EXECUTABLE(gles2_shader_diagnostic` (`:1776`) | ⚠ **YES** |
| `CMakeLists.txt:1900` | `ADD_EXECUTABLE(gles2_composite_audit` (`:1862`) | ⚠ **YES** |
| `tests/cmake/graphics.cmake:32` | `ADD_EXECUTABLE(test_graphics` (`:20`) | ⚠ **YES** |
| `tests/cmake/graphics.cmake:117` | `ADD_EXECUTABLE(test_asset_loading` (`:99`) | ⚠ **YES** |

⚠ **Two of the seven are TEST targets in `tests/cmake/graphics.cmake`**, a
fragment that did not exist before J20 (correction 26). A plan that greps
`CMakeLists.txt tests/CMakeLists.txt` finds **five of seven** and looks complete.

⚠ **These are the SAME seven lines J29 (`model_loading.cpp`) edits**, five of
them adjacent inserts into the same source lists. See §Territory.

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'variant_loading' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

At `824adb28` the first grep returns **comment-only hits and zero functional
readers**, and the second — the exhaustive list of source paths any test opens as
text — **does not contain this file**. **No repoint is needed.**

⚠ **Re-run both at your tip and paste them.**

⚠ **`tests/test_first_person_camera_single_formula.cpp` sweeps EVERY `.cpp` and
`.h` under `src/client/graphics` RECURSIVELY** (`kGraphicsDir`,
`fs::recursive_directory_iterator`) **and keys two of its three assertions on
FILENAME** — a third kind of text-scanning guard, one that names no file and
therefore appears in NEITHER standard grep.

- **Part 1** — a file that both mentions `CameraMode::FirstPerson` **and** writes
  `camera_->setPosition(` / `camera_->setTarget(` is an offender unless it is
  `renderer_camera.cpp`.
- **Part 2** — each first-person tunable may be read from at most one TU under
  that tree (two for `firstPersonMinLookPitchDegrees`).

```bash
grep -n 'CameraMode::FirstPerson\|camera_->setPosition(\|camera_->setTarget(\|kFirstPerson\|firstPersonM\|firstPersonV\|computeFirstPersonCamera' \
    src/client/graphics/eq/variant_loading.cpp
```

At `824adb28` **`variant_loading.cpp` matches none of those tokens**, so it is
invisible to both parts of the sweep and the new TUs will be too. **Confirm at
your tip and paste the (empty) grep.**

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
- ⚠ **Expected verifier result for J35: **EXIT 0, `+0/-0` substantive.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied preamble (1-15) is **100% boilerplate**: two `//` lines, nine
    `#include`, two `namespace … {`, and blanks the normalizer drops. **No
    `extern`, no `typedef`, no file-scope definition** — contrast J30 (6 lines)
    and J31 (2).
  - The replicated namespace closes are bare `}` with a `//` tail — boilerplate.
  - Header-comment adaptations cost zero (`//`).
  - No internal header, no test repoint, no preamble deviation.
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
  git show "$BASE:src/client/graphics/eq/variant_loading.cpp" | sed -n '16,1605p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Strip the two namespace closes from each TU before comparing**, and note
  that the original's own closes (1606-1607) are inside range 1198-1607 — so the
  `before` side must run to **1605**, not 1607 (correction 20: suspect the proof
  first).
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`16-381, 382-1197, 1198-1605`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/graphics/eq/variant_loading.cpp" | sed -n '16,1605p'`.
  **1,590 lines. Expect `cmp` exit 0.**
  ⚠ **Declare count variables `local` in your proof helpers.** More generally
  (correction 20): **when a proof fails, suspect the proof first.** J05's
  non-`local` `n` produced a 247-line shortfall that read exactly like a dropped
  range; J09's one proof failure was range arithmetic against the produced
  files' numbering; J08's failed **exactly at its relocation seam**. **Three
  units, three proof bugs, zero real defects.**
  ⚠ **Additionally and separately, prove BOTH dominant methods survived byte for
  byte.** Together they are 75% of the file and they are the first thing a
  reviewer will look at:
  ```bash
  diff <(git show "$BASE:src/client/graphics/eq/variant_loading.cpp" | sed -n '382,1196p') \
       <(sed -n '<its new range>p' src/client/graphics/eq/variant_loading_animation.cpp)   # 815 lines
  diff <(git show "$BASE:src/client/graphics/eq/variant_loading.cpp" | sed -n '1209,1604p') \
       <(sed -n '<its new range>p' src/client/graphics/eq/variant_loading_preload.cpp)     # 396 lines
  ```
  **Both exit 0. Paste both.**
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
  file**: `src/client/graphics/eq/variant_loading.cpp`, `src/client/graphics/eq/variant_loading_animation.cpp`, `src/client/graphics/eq/variant_loading_preload.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **0**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **1,607** this plan measured,
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
   (this plan predicts 3 / 1 / 2 of the 6), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. ⚠ **Your own arithmetic on the Batch-G question, stated explicitly.** This
    plan claims the floor is `15 + 815 = 830` against a 1,500 ceiling, that the
    largest TU lands at **833** with **667 lines of headroom**, and that
    **no Batch-G decision is owed before this Batch-F split** — contradicting
    `refactor/J_file_size_reclamation.md` §Residual, which calls this "the batch's
    worst floor" and "the strongest candidate for a Batch-G decision taken
    *before* its Batch-F split". **Re-derive the numbers at your tip and say
    whether you agree.** If your measurement contradicts this plan, **STOP and
    report** — the batch plan's §Residual row and scheduling note both need
    correcting either way, and that correction is a Review finding, not an edit
    you make.
12. **The seven CMake add-sites, each confirmed** — five root, two in
    `tests/cmake/graphics.cmake`. ⚠ **One is `gles2_zone_benchmark`**: a miss
    there fails the visual gate at the RENDER step.
13. **Both two-line signatures at the cuts, pasted**, proving neither was split.

## Territory

`src/client/graphics/eq/variant_loading.cpp`, the two new TUs beside it
(`…_animation.cpp`, `…_preload.cpp`), `CMakeLists.txt` (the five root sites),
`tests/cmake/graphics.cmake` (the two test sites), and this plan file. **Nothing
else** — not `include/client/graphics/eq/race_model_loader.h`, and in particular
**NOT `src/client/graphics/eq/model_loading.cpp`**, which is **J29's** file even
though it defines methods of the same class.

⚠ **SERIALISATION with J29 and J32**: disjoint source files, but J29 edits **all
seven** of this unit's CMake lines and J32 edits five of them. **Merge one at a
time and rebase**, and never resolve a CMake conflict by taking one side
wholesale — correction 31 records what that costs (J21 vs J12 offered the entire
pre-split file as one side of an alternatives-shaped conflict, and taking either
side would have silently reverted a completed unit).

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
- Either dominant method has grown enough that `15 + largest` exceeds 1,500 at
  your tip — in which case this file genuinely IS a Batch-G case and the unit's
  shape changes. **Report; do not improvise a decomposition.**
- You conclude that a two-way split is preferable.
