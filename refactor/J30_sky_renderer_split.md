# J30 — Batch J Wave 6: split `src/client/graphics/sky_renderer.cpp` (1,770 → 3 TUs, largest ~690)

**Slot: assigned at dispatch. Branch `task/J30-sky-renderer-split` cut from `fable`.
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

`src/client/graphics/sky_renderer.cpp` — **1,770 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **1,770** |
| preamble | **1-19** — six `#include` lines, then an `#ifdef EQT_HAS_GLES2` group (10-16) containing ⚠ **a 3-physical-line `extern void* gles2WrapTexture(...)` declaration at 13-15**, then `namespace EQT {` / `namespace Graphics {` at 18-19 |
| namespace | `EQT` / `Graphics` opened at **18-19**, closed at **1769-1770**. **Both closes replicate into each new TU.** |
| method definitions | **49** |
| largest method | **144** `SkyRenderer::calculateSkyColors` (1168-1311); 2nd **124** `createCustomSkyDome` (564-687) |
| `// ====` banners | **2**, at 1640/1642 — one banner pair, immediately before the trailing `#ifdef EQT_HAS_GLES2` region |
| conditional preprocessor directives | 4 — **10/16** `EQT_HAS_GLES2` (preamble, containing the `extern`); ⚠ **1644 / 1767** `EQT_HAS_GLES2` — **a 124-line region spanning `beginStripUpload` (1646-1699) and the strip-upload family whole**. **No cut may land between 1644 and 1767.** |
| file-scope `static` / anonymous namespaces | **zero of both.** The one non-boilerplate preamble item is the `extern` declaration at 13-15 — a **declaration**, safe to duplicate (contrast J26, whose line 17 was a definition). |
| multi-line comment regions | **zero multi-line regions.** `grep -n "/\*"` returns one line (48) and it is a single-line `/*…*/`. Comment depth is 0 on every line. |

⚠ **Correction 33: "no block comments" must mean "no MULTI-LINE comment
regions".** Single-line `/* … */` argument comments are irrelevant; what the
cutting rule needs is **comment depth 0 at every cut**. Compute it, do not
assert it, and beware the trap `eq_movement.cpp:18` set for the orchestrator's
own scanner: a `//` line containing the token `*/` makes a naive depth counter
go negative. Strip `//` tails before counting:

```bash
python3 - <<'EOF'
import re
L=open('src/client/graphics/sky_renderer.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {689,1168}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-19 | preamble: six includes, the `EQT_HAS_GLES2` group with the `extern` at 13-15, two namespace opens | copied to each new TU | 19 |
| **20-688** | ctor / `initialize` / `precomputeDomeMesh` (187-251) / `createSkyDomeFromPrecomputed` (253-330) / `createSkyDomeFromWldGeometry` (332-409) / `createSkyDome` (515-562) / `createCustomSkyDome` (564-687) — sky-dome construction and geometry | **stays** `sky_renderer.cpp` | **690** |
| **689-1167** | `updateDomeVertexAlpha` (689-737) / `createCelestialBodies` (785-874) / `updateCelestialPositions` (892-961) / the cloud-layer and fog accessors through `getRecommendedFogColor` (1164-1166) — celestial bodies, cloud layers and per-frame dome updates | `sky_renderer_celestial.cpp` | **500** |
| **1168-1770** | `calculateSkyColors` (**1168-1311, 144 lines — the file's largest, moved WHOLE**) / `updateSkyLayerColors` (1348-1412) / `getSpecialSkyColors` (1503-1599) / the **1640/1642 banner** and ⚠ **the whole `#ifdef EQT_HAS_GLES2` region 1644-1767** (`beginStripUpload` 1646-1699 and the strip-upload family) — colour computation and the GLES2 strip-upload path | `sky_renderer_colors.cpp` | **622** |

**Result: 3 TUs** (`sky_renderer.cpp` stays and counts). Largest **~690**.

Arithmetic that must hold: **`19 + 669 + 479 + 603 = 1770` (the third range carries the original's two namespace closes)**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundaries fall where they do: **20-688** builds the dome —
precompute, WLD geometry, the custom dome. **689-1167** is what moves and shades
per frame: celestial bodies, cloud layers, dome alpha. **1168-1770** is colour
derivation plus the GLES2 upload path that consumes it, and it is deliberately
the range that swallows the whole trailing `#ifdef EQT_HAS_GLES2` region.

⚠ **The 1644-1767 conditional is 124 lines and spans whole methods.** A cut
inside it would leave an unopened `#endif` in one file and an unterminated
`#ifdef` in the other — **and it would still compile in the default
configuration**, because `EQT_HAS_GLES2` is on in every tree the bar builds. The
break would surface only in a config the acceptance bar exercises. This is J02's
block-comment failure mode wearing preprocessor clothes. **Do not cut inside a
conditional.**

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 689 | `687: }` (end of `createCustomSkyDome`), `688` blank | `689: void SkyRenderer::updateDomeVertexAlpha() {` |
| 1168 | `1166: }` (end of `getRecommendedFogColor`), `1167` blank | `1168: SkyColorSet SkyRenderer::calculateSkyColors(float timeOfDay) const {` |

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/graphics/sky_renderer.cpp
```

## ⚠ HAZARD 1 — the `extern gles2WrapTexture` declaration is the unit's only verifier cost, and it is computable in advance

Preamble lines **13-15** are one logical `extern void* gles2WrapTexture(...)`
declaration spread over **three physical lines**, inside the preamble's
`#ifdef EQT_HAS_GLES2` region. `verify_move_only.py`'s `BOILERPLATE` allowlist
covers `#include`, `#pragma`, `#ifndef`, `#ifdef`, `#if`, `#define`, `#endif`,
`#else`, `#elif`, any comment, `namespace … {` and a bare `}` — **it does not
cover `extern`.**

**Copying the preamble verbatim into two new TUs therefore adds 3 distinct lines
× 2 extra copies = 6 substantive ADDED lines, 0 removed.** This is correction
24's case (J10 predicted 21 and hit 21 exactly) and correction 9's
(J11's 70 lines from the same shape). **It is expected, not a failure.**

⚠ **Measured: the ONLY caller of `gles2WrapTexture` in this file is in `sky_renderer_colors.cpp` (range 1168-1770).** The other two TUs will carry a declaration they never use — that is correct and intended, and it is not a reason to trim.

⚠ **Do NOT "fix" it by trimming the extern out of the TUs that do not call
`gles2WrapTexture`.** §Process is explicit that each new TU carries the
original's full include block verbatim; trimming is a second, riskier change
wearing a move-only commit's clothes, and it removes a judgement call from the
executor for a reason. **Copy it, predict the six lines, and get the sign-off.**

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `sky_renderer.cpp`:**

`sky_renderer.cpp` has **zero file-scope `static` symbols, zero anonymous
namespaces, and zero file-scope constants.** Every symbol it defines is a
`SkyRenderer` member with external linkage, declared in
`include/client/graphics/sky_renderer.h`. The single file-scope *declaration* —
`extern void* gles2WrapTexture` at 13-15 — declares a symbol **defined in
`COpenGLES2Driver.cpp`**, not here, so there is no helper that could be separated
from its consumer.

**Therefore correction 21 has NO exposure in this file.** Confirm with your own
greps and paste them:

```bash
grep -n '^static \|^namespace\s*{\|^const \|^constexpr \|^extern ' \
    src/client/graphics/sky_renderer.cpp
grep -n 'namespace' src/client/graphics/sky_renderer.cpp
grep -rn 'gles2WrapTexture' src/ | grep -v sky_renderer
```
(the first: only line 13. The second: exactly four lines — 18, 19, 1769, 1770.
The third shows the definition lives in the GLES2 driver.)

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `sky_renderer.cpp`'s preamble, lines 1-19, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

**Lines 1-19 verbatim into each new TU**, including the `#ifdef EQT_HAS_GLES2`
group at 10-16 **and the `extern` at 13-15** — see HAZARD 1 for why that is
deliberate and what it costs. **Do not trim it.**

**Each new TU ends with the two namespace closes, replicated verbatim:**
```
} // namespace Graphics
} // namespace EQT
```
⚠ **In `sky_renderer_colors.cpp` those closes come AFTER the
`#endif // EQT_HAS_GLES2` at 1767** — exactly as in the original (1767 `#endif`,
1768 blank, 1769-1770 closes). **Preserve that order.**

## File-header comments

`sky_renderer.cpp` has **no file-header comment block** — it opens straight into
`#include`. **Each of the three TUs gets a short one** (four to six lines) naming
its clusters and citing `refactor/J30_sky_renderer_split.md`. That is the unit's
only non-move content besides the six duplicated `extern` lines; enumerate every
line of it.

`sky_renderer_colors.cpp`'s header must record that it **owns the trailing
`#ifdef EQT_HAS_GLES2` region (originally 1644-1767) whole**, so the next person
to move a method out of it knows the region must not be split.

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
grep -rn 'sky_renderer\.cpp' CMakeLists.txt tests/ scripts/
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
actually found**, and if it differs from the ONE below, say so loudly.

At `824adb28` this file has **ONE real add-site(s)**:

| file:line | variable / target | edit? |
|---|---|---|
| `CMakeLists.txt:522` | `SET(GRAPHICS_SOURCES` (opens at `:439`) | ⚠ **YES — add both new TUs here** |

**No `tests/cmake/` fragment names this file, and no tool or benchmark target
does either.** **This unit does NOT touch `gles2_zone_benchmark`.** ⚠ Count your
own anyway — **add-site counts have ranged from 1 to 19 in this batch and the
shape of the file gives no hint.**

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'sky_renderer' tests/ scripts/
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
    src/client/graphics/sky_renderer.cpp
```

At `824adb28` **`sky_renderer.cpp` matches none of those tokens**, so it is
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
- ⚠ **Expected verifier result for J30: **EXIT 1, with exactly 6 substantive ADDED lines and 0 REMOVED — the three `extern gles2WrapTexture` physical lines, each duplicated into two new TUs.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - Everything in the preamble except 13-15 is boilerplate: `#include`,
    `#ifdef`, `#endif`, `//`, `namespace … {`, and blanks the normalizer drops.
  - **`extern` is NOT in the allowlist.** Lines 13-15 are 3 distinct lines with
    count 1 before and count **3** after (original + two new TUs) → **`+2x` each
    = 6 ADDED**, and none removed.
  - The replicated namespace closes are bare `}` with a `//` tail — boilerplate.
  - Header-comment lines are `//` — boilerplate.
  - No internal header, no test repoint, no preamble deviation.
  - **Total: ADDED 6, REMOVED 0.**

  **This is the §Process step-4 / correction-5 case: expected, not a failure.**
  Enumerate all six, confirm each is one of the three `extern` physical lines,
  prove the three lines are byte-identical in all three TUs, and request
  orchestrator sign-off in the Review.

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
  git show "$BASE:src/client/graphics/sky_renderer.cpp" | sed -n '20,1768p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Strip the two namespace closes from each TU before comparing**, and note
  that the original's own closes (1769-1770) are inside range 1168-1770 — so the
  `before` side must run to **1768**, not 1770. Getting this off by two lines
  produces a diff that reads exactly like a dropped range (correction 20).
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`20-688, 689-1167, 1168-1768`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/graphics/sky_renderer.cpp" | sed -n '20,1768p'`.
  **1,749 lines. Expect `cmp` exit 0.**
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
    `gles2_zone_benchmark` CMake add-site was missed. **This unit has no `gles2_zone_benchmark` site**, so a render-step failure here would be a genuine surprise — report it, do not re-run past it.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ Correction 14: the wave-stretch figures on record are **worst cases
  observed, not forecasts.** J06's `ctest -j6` ran in 120.7 s — faster than
  J01's solo 3.0 min on a larger suite — and its ARM build completed first
  invocation. **Do not report a clean run as an anomaly.**
- `scripts/refactor/check_file_sizes.sh --files` on **every changed AND created
  file**: `src/client/graphics/sky_renderer.cpp`, `src/client/graphics/sky_renderer_celestial.cpp`, `src/client/graphics/sky_renderer_colors.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **2**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **1,770** this plan measured,
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
7. **How many CMake add-sites you actually found**, against the ONE
   this plan names, and the output of both `tests/`/`scripts/` greps.
8. **Both content proofs**, pasted, and the verifier's exit code plus `$BASE`.
   State whether the result matched the prediction; if it did not, enumerate
   every flagged line before concluding anything.
9. **Final line counts of every file**, method counts per TU
   (this plan predicts roughly 18 / 17 / 14 of the 49), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. **The six predicted `extern` lines, enumerated and confirmed byte-identical
    across all three TUs** (an `md5sum` of `sed -n '13,15p'` on each is the
    cheapest proof), plus your request for orchestrator sign-off.
12. **The `test_first_person_camera_single_formula.cpp` check, pasted** — the
    empty grep proving neither new TU enters that recursive sweep's offender set.

## Territory

`src/client/graphics/sky_renderer.cpp`, the two new TUs beside it
(`…_celestial.cpp`, `…_colors.cpp`), `CMakeLists.txt` (the one `GRAPHICS_SOURCES`
site), and this plan file. **Nothing else** — not
`include/client/graphics/sky_renderer.h`, not `sky_config.h`, not
`src/client/graphics/eq/sky_loader.cpp`, not any `tests/` file, not
`tests/cmake/*.cmake`, not `docs/`.

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
- Any cut boundary has drifted inside the 1644-1767 `EQT_HAS_GLES2` region.

## Review (executor, slot3, 2026-09-02)

**Branch `task/J30-sky-renderer-split`, cut from `fable` at
`3cf86bd114aa06e38d22f1e90068de61e58a52c9` ("Batch J: J35 closes the
variant_loading Batch-G question -- it was an artefact of assuming a two-way
split"). `$BASE = 3cf86bd114aa06e38d22f1e90068de61e58a52c9`
(`git merge-base fable HEAD`), unchanged throughout the run.**

### 1. Line count at my tip, and range drift

`wc -l src/client/graphics/sky_renderer.cpp` = **1,770** — exactly what the plan
measured at `824adb28`. **Zero drift in the source file.** Both cut boundaries,
the preamble extent, the four preprocessor directive lines, the two banner
lines, the two namespace opens and the two namespace closes all reproduced to
the line. Correction 9 did not fire here.

⚠ **Drift DID occur in `CMakeLists.txt`** — see item 7. The single add-site moved
`522 -> 534` and its enclosing `SET(GRAPHICS_SOURCES` moved `439 -> 445`.
**Fourth consecutive unit where the CMake target name was right and the line
number was stale** (correction 59).

### 2. Every property in Measured, re-verified with my own greps

| property | plan | measured at `3cf86bd1` | |
|---|---|---|---|
| total lines | 1,770 | **1,770** | ok |
| preamble | 1-19, 6 includes + GLES2 group 10-16 with a 3-line `extern` at 13-15 + opens 18-19 | **identical** | ok |
| namespace | opened 18-19, closed 1769-1770 | **exactly those four lines** | ok |
| method definitions | 49 | **47** | see below |
| largest method | 144, `calculateSkyColors` 1168-1311 | **1168-1311, 144** | ok |
| 2nd largest | 124, `createCustomSkyDome` 564-687 | **564-687, 124** | ok |
| `// ====` banners | 2, at 1640/1642 | **2, at 1640 and 1642** (`grep -cE`) | ok |
| conditional directives | 4: 10/16 and 1644/1767 | **exactly those four** | ok |
| file-scope `static` / anon ns | 0 / 0 | **0 / 0** | ok |
| multi-line comment regions | 0; one single-line `/*` at 48 | **`grep -n '/\*'` returns line 48 only** | ok |

⚠ **Record correction (correction 46, and this is now the fifth unit where a
derived count slipped while the ranges held exactly): the method count is 47,
not 49.** My counting pattern, stated as correction 46 requires:
`grep -c '^[A-Za-z_].*SkyRenderer::'` — qualified name at column 0. It returns
**47** for the pre-split file and 21 + 14 + 12 = 47 afterwards. `grep -c '^{'`
returns **1** (the constructor's initializer-list body brace at line 26), so
that alternative pattern is useless in this file. I could not reproduce 49 under
any pattern. **No STOP condition: this is a derived count, not a range.**

```
$ awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' src/client/graphics/sky_renderer.cpp
10: #ifdef EQT_HAS_GLES2
16: #endif
1644: #ifdef EQT_HAS_GLES2
1767: #endif // EQT_HAS_GLES2

$ grep -nE '^// ={4,}' src/client/graphics/sky_renderer.cpp
1640:// ============================================================================
1642:// ============================================================================

$ grep -n '/\*' src/client/graphics/sky_renderer.cpp
48:void SkyRenderer::onTextureEvicted(const std::string& name, irr::video::ITexture* /*texture*/) {

$ grep -n 'namespace' src/client/graphics/sky_renderer.cpp
18:namespace EQT {
19:namespace Graphics {
1769:} // namespace Graphics
1770:} // namespace EQT

$ grep -n '^static \|^namespace\s*{\|^const \|^constexpr \|^extern ' src/client/graphics/sky_renderer.cpp
13:extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,

$ grep -n 'gles2WrapTexture' src/client/graphics/sky_renderer.cpp
13:extern void* gles2WrapTexture(...)          <- the declaration
1742:        gles2WrapTexture(driver_, stripState_.textureName.c_str(),   <- the sole call
```

**Line 1742 is inside range 1168-1770**, so the only caller lands in
`sky_renderer_colors.cpp`, exactly as the plan predicts. The symbol is defined in
`COpenGLES2Driver.cpp`, not here.

⚠ **EVERY EMPTY OR NEAR-EMPTY GREP IS PROVEN, NOT ASSUMED** (the dispatch
brief's five silent-nothing modes, plus corrections 55 and 60). Each pattern was
run against a file where it must match before I believed a zero:

| pattern | positive control | hits there |
|---|---|---|
| `^static ` | `src/client/graphics/eq/wld_loader.cpp` | 1 |
| `^namespace {` | `src/client/raid_update.cpp` | 1 |
| `^const ` | `src/client/trade_manager.cpp` | 2 |
| `^constexpr ` | `src/client/position.cpp` | 1 |
| `^extern ` | matched in the target file itself (line 13) | 1 |
| first-person token set | `src/client/graphics/renderer_camera.cpp` | 20 |
| `awk` directive dump | matched in the target file itself | 4 |
| `grep -cE '^// ={4,}'` | correction 28's `-E` form used; plain `-c` would return 0 | 2 |
| CMake filename sweep | same sweep for `variant_loading` | 21 hits in 2 files |
| `grep -rn <name> tests/ scripts/` | same grep for `variant_loading` | 3+ hits |

⚠ **Correction 36 check (out-of-class static member definition in the copied
preamble): NONE.** Preamble 1-19 is 6 `#include`s, an `#ifdef`/`#endif` pair, a
`//` comment, a 3-physical-line `extern` **declaration**, two `namespace … {`
and two blanks. Nothing with external linkage is *defined* there, so copying it
verbatim into two more TUs carries no duplicate-symbol risk. (Confirmed by the
link: `willeq_core` archives cleanly and every binary links.)

**No Measured property has changed since `824adb28` except the method count,
which is a counting-pattern artefact rather than a change in the file. No STOP
condition fired.**

### 3. Correction 21 — re-derived, not cited, and widened per correction 51

**The file defines ZERO file-scope symbols of any kind.** The standard pattern
would have been enough here, but correction 51 exists because it misses
namespace-scope free functions (J32's `heightToColor` was called across its
cut), so I ran the widened form — **every column-0 line that is not a comment, a
preprocessor directive, a blank or a `}`**:

```
$ grep -n '^[^ \t}]' src/client/graphics/sky_renderer.cpp | grep -v '^[0-9]*:[/#]'
13:extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
18:namespace EQT {
19:namespace Graphics {
21:SkyRenderer::SkyRenderer(...)
26:{
29:SkyRenderer::~SkyRenderer() {
... 45 further lines, EVERY ONE of the form `<type> SkyRenderer::<method>(` ...
1763:bool SkyRenderer::isStripActive() const {
```

That is **52 lines total: the `extern` declaration, the two `namespace … {`
opens, the constructor's column-0 `{` at line 26, and 47 `SkyRenderer::` member
definitions. Nothing else.** So there is **no namespace-scope free function, no
file-scope constant, no anonymous namespace, and no `static` at any scope** —
`grep -n 'static '` (unanchored) returns nothing at all, so there is not even a
function-local static.

**Mapping each file-scope entity to the range that assigns it and the range that
consumes it:**

| entity | kind | defined where | this plan's range for the definition | consumers | consumers' range |
|---|---|---|---|---|---|
| `gles2WrapTexture` | **declaration only**; defined in `COpenGLES2Driver.cpp` | preamble 13-15 | **copied verbatim into all three TUs** | line 1742 only | 1168-1770 (`…_colors.cpp`) |
| all 47 `SkyRenderer::` members | class members, **external linkage**, declared in `include/client/graphics/sky_renderer.h` | 20-1768 | across all three ranges | each other, and callers outside this file | every TU includes the header via the copied preamble |

**The only symbol whose definition/consumer split could matter is
`gles2WrapTexture`, and the definition is not in this file at all — it is
resolved by the linker from the GLES2 driver.** Its declaration is present in
every TU because the preamble is copied verbatim, so no TU can be missing it.
Every other symbol is a class member visible through a header all three TUs
include. **Correction 21 therefore has no exposure here: there is no helper that
could be separated from its consumer, because there is no file-local helper.**

### 4. Comment depth at every cut — computed, not asserted

Run with the plan's `//`-stripping counter (which is what defends against the
`eq_movement.cpp:18` trap where a `//` line containing `*/` drives a naive
counter negative):

```
depth before line 689  = 0
depth before line 1168 = 0
final depth = 0
```

The file's only `/*` is the single-line `/*texture*/` parameter comment at line
48, well inside the stays range. **Comment depth is 0 at both cuts and 0 at the
end of the file**, so correction 33's distinction (multi-line *regions*, not
single-line `/* … */`) is satisfied and neither cut can land inside a comment.

### 5. The preprocessor dump — no cut lands inside a conditional

```
$ awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' src/client/graphics/sky_renderer.cpp
10: #ifdef EQT_HAS_GLES2
16: #endif
1644: #ifdef EQT_HAS_GLES2
1767: #endif // EQT_HAS_GLES2
```

**Correction 60's positive control is intrinsic here** — this file HAS
directives, so a non-empty result proves the pattern works; no external control
was needed (unlike J35, whose file had zero).

Two conditional regions, both matched pairs. **Cut 689 and cut 1168 are both
outside both regions**: 689 and 1168 are each greater than 16 and less than
1644. **The 124-line trailing region 1644-1767 falls entirely inside range
1168-1770** and travels whole into `sky_renderer_colors.cpp`, where it now
occupies **lines 512-635 (124 lines, unchanged)**. Post-split directive dumps:

```
sky_renderer.cpp            17: #ifdef EQT_HAS_GLES2   23: #endif
sky_renderer_celestial.cpp  18: #ifdef EQT_HAS_GLES2   24: #endif
sky_renderer_colors.cpp     26: #ifdef EQT_HAS_GLES2   32: #endif
                           512: #ifdef EQT_HAS_GLES2  635: #endif // EQT_HAS_GLES2
```

**Every fence is matched in every TU. No unopened `#endif`, no unterminated
`#ifdef`.** Correction 52's rule honoured: I used the directive dump, not
`grep -c '<GUARD>'` (which would report 3 vs 1 in the colors TU because the
preamble's `#endif` carries no trailing comment).

**Both cut boundaries, pasted with their neighbours:**

```
cut 689                                        cut 1168
685|     LOG_INFO(MOD_GRAPHICS, "Created…      1164| irr::video::SColor SkyRenderer::
686|              vertCount, indexCount / 3);      |   getRecommendedFogColor() const {
687| }        <- closes createCustomSkyDome    1165|     return currentSkyColors_.fog;
688| (blank)                                   1166| }   <- closes getRecommendedFogColor
689| void SkyRenderer::updateDomeVertexAlpha() 1167| (blank)
     { <- first line of the next range         1168| SkyColorSet SkyRenderer::
                                                   |   calculateSkyColors(float timeOfDay)
                                                   |   const {   <- first line of next range
```

Both satisfy J03's rule (correction 11): **a blank line whose predecessor closes
a method, comment depth 0.** Both first lines are complete one-line signatures,
so no signature was split. **The trailing blank stays with the preceding range**
(688 with the stays body, 1167 with the celestial body), which reproduces the
original's own shape.

### 6. Every file-header comment line I wrote, enumerated

`sky_renderer.cpp` had **no file-header comment block** — it opened straight
into `#include` at line 1. So **no existing comment was replaced, changed or
removed anywhere**; all header content is pure addition on top of a verbatim
preamble. This is proven, not asserted: PROOF1 (item 8) is a byte-identical
re-concatenation of everything from line 20 down, and the preamble 1-19 is
byte-identical in all three TUs (item 11).

- **`sky_renderer.cpp` — 7 header lines.** Line 1 names the TU and its subject
  (sky dome construction and geometry); lines 2-4 name its clusters
  (constructor, teardown, initialization and sky-type selection; the dome mesh
  precompute; dome creation from precomputed and from WLD geometry; the custom
  dome builder); lines 5-7 record that it was split move-only from the original
  1,770-line file by `refactor/J30_sky_renderer_split.md` and name both
  companion TUs.
- **`sky_renderer_celestial.cpp` — 8 header lines.** Lines 1-2 name the TU and
  its subject (celestial bodies, cloud layers, per-frame dome updates); lines
  3-5 name its clusters (dome vertex alpha, mesh construction from zone
  geometry, celestial body creation / positioning / sizing, sky visibility, sky
  node teardown, the current-colour and fog accessors); lines 6-8 the same
  move-only provenance line and both companions.
- **`sky_renderer_colors.cpp` — 16 header lines.** Lines 1-2 name the TU and its
  subject (sky colour derivation and the GLES2 strip upload path); lines 3-5
  name its clusters (colour computation and interpolation, sky-layer and sun-glow
  colours, category selection, per-category special colours, cloud scrolling, the
  incremental strip-upload family); lines 6-8 provenance and companions; line 9 a
  bare `//` separator; **lines 10-16 are the region warning this plan requires** —
  that the TU owns the file's trailing conditional-compilation region (the one
  guarded on the GLES2 feature macro, originally lines 1644-1767) **whole**, that
  it must stay whole because the region spans complete method definitions, and
  that a cut inside it would leave one file with an unmatched closing directive
  and the other with an unterminated opening one **while both halves still
  compiled in the default configuration** — which is what makes it dangerous
  rather than merely wrong.

⚠ **Correction 45 (the widened correction 40) honoured, and it had real exposure
in the colors header.** The natural way to write that warning is to quote the
guard macro. I did not. Nothing in any of the three headers quotes a token that
any check greps for:

- **The guard macro is never spelled out** — described as "the GLES2 feature
  macro". A header naming it would inflate any `grep -c` fence check (correction
  52's exact failure mode) and would show up in any future audit of that macro's
  use sites.
- **`gles2WrapTexture` is never named** — this plan's own item-11 check
  (`grep -rn 'gles2WrapTexture' src/`) would otherwise return three extra hits
  from comments. **This is correction 45's precise shape: a plan's own
  verification step defeated by a comment the same plan prescribed.**
- **No `// ====` sequence.** `grep -cE '^// ={4,}'` still reads **0 / 0 / 2** —
  the 2 being the original's banner pair at old-1640/1642, now in the colors TU
  and unchanged.
- **No first-person token.** These files live under `src/client/graphics`, so
  they enter `tests/test_first_person_camera_single_formula.cpp`'s recursive
  sweep automatically. The token grep returns **0 in all three TUs** (item 12).

Every header line begins `//`, so all 31 are allowlisted boilerplate for the
verifier.

⚠ **Correction 30 declared, not fixed.** The original's line 20 is blank
(between `namespace Graphics {` at 19 and the constructor at 21), and that blank
belongs to the stays range. The two **new** TUs therefore begin their body
immediately after `namespace Graphics {` with no blank between — J25's
**inverted** form of correction 30, not the double-spaced tail J12 saw. On the
tail side correction 30 did **not** fire: the original's own line 1768 is blank
and rides with the colors body, so appending the two closes reproduces the
original's exact 1767/1768/1769/1770 shape, and the stays and celestial bodies
each end on their range's own trailing blank (688 and 1167). **Cosmetic,
invisible to both proofs and the verifier; hand-fixing it would insert a line
that is in neither the before nor the after multiset.**

### 7. CMake add-sites — **I found ONE**, matching this plan's count

Derived, not trusted (correction 49 — the brief explicitly told me to derive it).
The sweep enumerated **all 23** `CMakeLists.txt` and `*.cmake` files in the tree
outside `build*` and `third_party` with
`find … -name 'CMakeLists.txt' -o -name '*.cmake'` and then
`xargs … grep -Hni`. **I did not use `grep -rn --include=`** (correction 55, the
fifth silent-nothing mode), **did not recurse into a directory hoping to reach
its own `CMakeLists.txt`** (correction 50), **never named
`tests/CMakeLists.txt`** (correction 26), and **greped for the filename, not for
lowercase CMake commands** (correction 12). The sweep was proven live by running
the identical pipeline for `variant_loading`, which returned 21 hits across two
files.

| file:line (at my tip) | plan said | enclosing target | edited |
|---|---|---|---|
| `CMakeLists.txt:534` | `:522` | `SET(GRAPHICS_SOURCES` at **`:445`** (plan said `:439`) | **yes — both new TUs inserted at 535/536** |

**One found, one edited, +2 lines. The count matched exactly; both line numbers
were stale.** Fourth consecutive unit with that shape (correction 59). No
`tests/cmake/` fragment names this file, no tool or benchmark target does, and
**this unit does not touch `gles2_zone_benchmark`** — confirmed, since the sweep
returned exactly one hit across all 23 CMake files.

`CMakeLists.txt` remains the pre-existing WARN that Process grandfathers by
name; my 2 added lines are not mine to fix.

### 8. `tests/` and `scripts/`, both greps, and both content proofs

```
$ grep -rn 'sky_renderer' tests/ scripts/
(no output, exit 1)
```

⚠ **A small correction to this plan's record.** The `tests/` and `scripts/`
section predicts "comment-only hits and zero functional readers". At my tip
there are **no hits of any kind** — not one comment, not one CMake entry, not
one string. The conclusion (no repoint needed) is right; the description of the
hits is not. **J30 creates zero `tests/` doc rot**, the same clean case J35 hit.

```
$ grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```
returns 17 distinct entries — `include/client/eq.h` ×2,
`renderer_event_receiver.cpp` ×2, `renderer_frame_render.cpp`,
`include/client/state/…`, `renderer_intents.h`, `eq_bridge_intents.cpp` ×2,
`event_bus.h`, `action_dispatcher.h`, `action_dispatcher{,_social,_guild_misc}.cpp`,
`renderer_command_catalog.cpp`, `renderer_frame_input.cpp` ×2, plus the
`kGraphicsDir` root at `:49`. **No `sky_renderer` path is among them**, so no
test opens this file as text and there is no string-compare exclusion naming it
(J13's invisible shape). **No test file was edited.**

**Both content proofs, plus a third.** `body(TU)` is defined **structurally**
(everything strictly between that TU's `namespace Graphics {` line and its
`} // namespace Graphics` line, **both asserted unique by count before use**)
rather than by an offset constant — J23's and J35's method, and the defence
against the arithmetic bug that produced J05's and J08's false alarms. Count
variables are function-local (correction 20's `local` lesson).

```
BASE=3cf86bd114aa06e38d22f1e90068de61e58a52c9

sky_renderer.cpp:            body lines 27-695  = 669
sky_renderer_celestial.cpp:  body lines 28-506  = 479
sky_renderer_colors.cpp:     body lines 36-636  = 601

before = git show "$BASE:src/client/graphics/sky_renderer.cpp" | sed -n '20,1768p'  = 1749 lines
recon  = cat body(stays) body(celestial) body(colors)                               = 1749 lines

$ cmp before recon
PROOF1 byte-identical re-concatenation: exit 0

$ diff <(sort before) <(sort recon)
PROOF2 line-multiset: exit 0
```

Arithmetic that must hold, and does: **19 + 669 + 479 + 603 = 1,770**, this
plan's own form with the third range carrying the two namespace closes; and
equivalently **19 (preamble) + 669 + 479 + 601 + 2 (closes) = 1,770**. The
`before` side runs to **1768, not 1770**, because the original's own closes are
inside the third range — correction 20's off-by-two, which reads exactly like a
dropped range if you get it wrong.

**PROOF3 — the preamble is byte-identical in all three TUs.** `md5sum` of the
three `extern` physical lines in each produced TU, located by finding each TU's
`#include "client/graphics/sky_renderer.h"` line and taking the following 12th
to 14th lines (so the proof does not depend on my header lengths):

```
f17e6999250cbab4d2222f8baf5f74c6  sky_renderer.cpp            (preamble starts at line  8)
f17e6999250cbab4d2222f8baf5f74c6  sky_renderer_celestial.cpp  (preamble starts at line  9)
f17e6999250cbab4d2222f8baf5f74c6  sky_renderer_colors.cpp     (preamble starts at line 17)
f17e6999250cbab4d2222f8baf5f74c6  $BASE:sky_renderer.cpp lines 13-15
```

**All four identical.** See item 11.

### The verifier

Committed **FIRST**, then verified. All three of the batch's
confident-looking-wrong-answer traps ruled out positively: `--repo` given
explicitly (an executor's cwd is not its slot, and omitting it verifies
`main/`); the **fork point** used, not `fable..HEAD`; and the tree was
committed, so the run cannot be the `No C++ files changed` / exit-0 non-answer.
The verifier names exactly this unit's 3 C++ files and 2 non-C++ files and
nothing else — a wrong repo would list `main/`'s merged units and a wrong base
would list slot1's or slot2's.

```
BASE=3cf86bd114aa06e38d22f1e90068de61e58a52c9

Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
  refactor/J30_sky_renderer_split.md

Files verified: 3
Boilerplate: +63 / -0 lines (allowed)

SUBSTANTIVE ADDED lines (6):
  +2x  extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
  +2x  unsigned int gpuBytes = 0);
  +2x  unsigned int width, unsigned int height,

FAIL: substantive line changes detected. If intentional (e.g. de-static-ing a
helper), document in the unit review section for orchestrator sign-off.
VERIFY_EXIT=1
```

The `+63 / -0` boilerplate figure is derivable, and worth stating so the next
unit can predict its own: **31 newly written header lines** (7 + 8 + 16) plus
the **32 preamble and namespace-close lines whose normalized multiset COUNT
rises from 1 to 3** while their text is unchanged — correction 54's rule seen
from the counter's side: a boilerplate line registers in that figure when its
count changes, even though it costs the substantive verdict nothing. Nothing was
removed, because this file had no pre-existing header comment to replace
(contrast J35's `-1`).

**Result: EXIT 1 with exactly 6 substantive ADDED and 0 REMOVED — the
prediction held EXACTLY.** See item 11 for the enumeration and the sign-off
request.

### 9. Final line counts, method counts, wall clock

| file | lines | methods | note |
|---|---|---|---|
| `src/client/graphics/sky_renderer.cpp` | **697** | **21** | sky dome construction and geometry |
| `src/client/graphics/sky_renderer_celestial.cpp` | **508** | **14** | celestial bodies, cloud layers, per-frame dome updates |
| `src/client/graphics/sky_renderer_colors.cpp` | **638** | **12** | colour derivation + the whole GLES2 strip-upload region |
| **total** | **1,843** | **47** | 3 TUs, largest **697**, **803 lines of headroom** |

**Predicted vs actual, stated in correction 57's shape** so the reconciliation is
mechanical rather than a puzzle. The derivable identity is:

**TU lines = (my header) + 19 (preamble 1-19) + body + 2 (namespace closes)**

- `sky_renderer.cpp` = 7 + 19 + 669 + 2 = **697** (plan ~690)
- `sky_renderer_celestial.cpp` = 8 + 19 + 479 + 2 = **508** (plan ~500)
- `sky_renderer_colors.cpp` = 16 + 19 + 601 + 2 = **638** (plan ~622)

**Every TU came in exactly `(my header)` lines over the plan's figure — 7, 8 and
16, nothing else differs.** The plan's 690/500/622 are `preamble + range
(+ closes)` and carry no file-header comment at all, which the plan states
explicitly ("your new file-header comment lines land on top of that"). **Body
ranges are exact to the line; zero range drift.** Largest TU 697 against the
1,500-line warn ceiling — computed, not quoted (correction 58): headroom is
**803**.

⚠ **Method split 21 / 14 / 12, against the plan's predicted "roughly 18 / 17 /
14 of the 49".** The total is 47, not 49 (item 2), and the per-TU distribution
differs by +3 / -3 / -2. **The ranges held exactly and the derived counts did
not — the sixth unit running with that shape** (J13, J25, J26, J32, J33, now
J30). Counting pattern stated: `^[A-Za-z_].*SkyRenderer::`.

**Wall clock: ~95 min end to end.** Contract reading (the protocol, all sixty
corrections, the two named Reviews) and the survey ~30 min; **the cut, the CMake
edit and all three proofs took under 4 minutes**; the rest was the bar.
**Slowest step: the x86 `build/` build — two invocations, 10 min (killed at the
ceiling) + ~7 min.** `ctest -j6` needed two invocations for an unrelated reason
(below). `build-gles2-x86.sh` also needed two (~10 min killed + ~6 min).
`build-arm-noble.sh` exited **0 first invocation**.

**Did wave concurrency stretch the tail? Yes, and the whole cost was retries,
not breakage** — two exit-143s and one ctest discovery flake, ~20 min of pure
re-running, with slot1 (J24, `combat.cpp`) and slot2 (J29, `model_loading.cpp`)
active throughout. Correction 14's framing holds: these are worst cases
observed, and nothing here was a defect.

### Acceptance — every item, classified by EXIT CODE

| step | result |
|---|---|
| `cmake --build build -j24` | first invocation **exit 143** at the 600 s ceiling; **re-invocation exit 0** |
| ELF + archive scan after that 143 | **197 binaries + 2 archives, 0 BAD**; after the clean build **221 binaries, 0 BAD** |
| `scripts/build-gles2-x86.sh` (**run BEFORE `ctest`**, correction 41) | first invocation **exit 143**; **re-invocation exit 0**; 40 binaries + archives, **0 BAD** |
| `ctest -j6` (**NOT -j24**) | first invocation **exit 8 at DISCOVERY**, see below; **re-run: 3506/3507 passed** |
| sole ctest failure | `AudioManagerTest.PreloadCommonSounds` — the documented ALSA host quirk and the only entry in `refactor/test_baseline.txt`. **Zero new failures.** ctest exits 8 because of it. |
| visual gate (in-suite #3504) | **Passed, 21.90 s** |
| goldens | `git status --porcelain` lists only my 4 intended paths — **untouched, no re-bless** |
| `scripts/build-arm-noble.sh` | **exit 0, first invocation**; 18 binaries + archives, 0 BAD |
| `check_file_sizes.sh --files` × 3 | **3 ok, 0 warn, 0 fail** (697 / 508 / 638) |
| `verify_move_only.py "$BASE..HEAD" --repo <slot>` | **exit 1, `+6/-0` substantive — prediction exact**; sign-off requested (item 11) |

⚠ **GATE ORDERING (correction 41): I used THE FIX, not the workaround.**
`scripts/build-gles2-x86.sh` ran **before** `ctest -j6`, so the in-suite
`visual_gate` ran against a `gles2_zone_benchmark` **stamped 15:26** against
edits made at **14:52**. **The in-suite run is the one that counts and no
standalone re-run was needed.** Before my rebuild, `build-gles2/bin/` held a
benchmark stamped **14:24** — i.e. a tree that predated my edits — which is
exactly the vacuous pass correction 41 describes, and I would have shipped it
had the bar's written order been followed.

⚠ **The visual gate passed on both the CPU metric and the image comparison** —
no re-bless, no golden touched, and no image mismatch. This file renders the
sky, so that was the live risk in this unit; the split is behaviour-neutral.

⚠ **The truncation trap fired twice, and neither was a truncation.**
- The `build/` 143 killed *compiles* and *links* of test binaries; gmake deleted
  its partial outputs and the scan found 0 BAD among 197 binaries and both
  archives.
- The `build-gles2/` 143 was **killed archiving `lib/libwilleq_core.a`**
  (`gmake[3]: *** Deleting file 'lib/libwilleq_core.a'` is the log's last line) —
  **correction 56's `ar` site, now its third recorded instance after J23 and
  J35.** `readelf -h` on `build-gles2/bin/*` alone would have been blind to it;
  the archive was already gone, so widening the scan with `ar t` is what turns
  "I checked" into a real answer.

⚠ **The first `ctest` exit 8 was correction 16's case, not a truncation.** It
died in `GoogleTestAddTests.cmake` with "Process terminated due to timeout"
while listing `test_eq_action_handler_movement_utility`. I checked the binary
before concluding anything: `readelf -h` exit 0, and
`--gtest_list_tests` **exit 0 listing 17 cases in ~2 s**. Cold-cache contention
under wave load (the J05/J07 shape). The re-run listed it without complaint.
`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` did not
appear in either run.

### 11. The six predicted `extern` lines, enumerated — and the sign-off request

**The verifier's flagged set is exactly the predicted set, and nothing else.**
The three physical lines are, from the original's 13-15:

1. `extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,`
2. `                                unsigned int width, unsigned int height,`
3. `                                unsigned int gpuBytes = 0);`

Each has **normalized count 1 before** (one copy, in `sky_renderer.cpp`) and
**count 3 after** (the original plus two new TUs), so each costs **+2**, and
**3 × 2 = 6 substantive ADDED, 0 REMOVED.** The derivation this plan gives is
correct in every clause: `extern` is not in `verify_move_only.py`'s
`BOILERPLATE` allowlist, while `#include`, `#pragma`, `#ifdef`, `#endif`, any
comment, `namespace … {` and a bare `}` all are — which is why the replicated
namespace closes, the six includes, the conditional group's directives, the
comment at line 12 and all 31 of my header lines cost the substantive verdict
nothing (they appear in the `+55 / -0` boilerplate figure instead).

**Byte-identity across all three TUs is proven, not eyeballed** (PROOF3, item 8):
the `md5sum` of the three lines is `f17e6999250cbab4d2222f8baf5f74c6` in each
produced TU **and** in `$BASE`'s original. The three lines are therefore
byte-identical everywhere they appear, which is what makes the duplication a
declaration copy rather than a divergence.

**This is correction 54's rule seen from the other side:** a duplicated line
costs the verifier because its **count** changes, exactly as a promoted symbol
that keeps its text and count costs nothing. The 6 is intrinsic to copying the
preamble verbatim into two TUs, which the plan requires.

⚠ **I did NOT trim the `extern` from the two TUs that never call
`gles2WrapTexture`**, as the plan directs. `sky_renderer_celestial.cpp` and
`sky_renderer.cpp` carry a declaration they do not use; the sole call site is
line 1742 of the original, now in `sky_renderer_colors.cpp`. An unused
declaration is harmless — no ODR issue, no link cost, no warning at either
`-Wall` level the tree builds with (both builds are clean).

**Orchestrator sign-off requested for the 6 substantive ADDED lines as the
Process step-4 / correction-5 case: expected, enumerated, byte-identical, and
exactly the number this plan predicted before the unit ran.**

### 12. The `test_first_person_camera_single_formula.cpp` check

```
$ grep -n 'CameraMode::FirstPerson\|camera_->setPosition(\|camera_->setTarget(\|kFirstPerson\|firstPersonM\|firstPersonV\|computeFirstPersonCamera' \
      src/client/graphics/sky_renderer.cpp
(no output, exit 1 — at $BASE, and 0 hits in all three produced TUs)
```

Proven non-vacuous: the identical pattern returns **20 hits** in
`src/client/graphics/renderer_camera.cpp`.

All three TUs live under `src/client/graphics`, so all three **enter that test's
recursive sweep automatically** — this is the guard that names no file and so
appears in neither standard grep. They are nonetheless invisible to both parts
of it:

- **Part 1** (the offender check) needs a file that both mentions the
  first-person camera mode **and** moves the camera. None of the three does
  either, so none is an offender and `renderer_camera.cpp`'s exemption is not
  needed.
- **Part 2** (the at-most-N-readers count per tunable) counts reads of six
  first-person tunables. All three TUs read **zero** of them, so every tunable's
  reader count is unchanged and the two-reader exemption for
  `renderer_player_physics.cpp` is untouched.

Per correction 42 this particular guard is a *loud* shape rather than a silent
one — it strips line comments first and asserts at-most-N, so a comment could
only add a false positive — but I kept the blanket rule anyway (item 6) because
it is free and always safe.

### 10. Findings only, not fixes

1. **`docs/module_map.md` needs two new rows** — `sky_renderer_celestial.cpp`
   and `sky_renderer_colors.cpp` — and its `sky_renderer.cpp` row (if any) is now
   wrong: 1,770 -> 697, and the file no longer holds 26 of its 47 methods.
   **Recorded, not acted on** (correction 24's queue; `docs/` is outside my
   territory).
2. **No `tests/` or `scripts/` citation of this file exists at all**, stale or
   otherwise — the grep returns nothing. **J30 creates zero `tests/` doc rot**
   (contrast J23's five and J31's four). This plan's "comment-only hits"
   description is wrong in the harmless direction; see item 8.
3. **The method count is 47, not 49, under every pattern I could construct**
   (item 2), and the per-TU prediction 18/17/14 is 21/14/12. **Derived counts,
   not ranges** — the ranges were exact.
4. **All CMake line numbers in this plan are stale** (`:522` -> `:534`, opener
   `:439` -> `:445`) while the target name and the count of one were right.
   Fourth consecutive unit; correction 59's "name targets, never cite line
   numbers" is the right instruction and the dispatch brief gave it.
5. **No dead-content site in this file** — no `#if 0`, no commented-out method
   body, no retired-configuration arm. The batch's seven-site dead-content
   decision gains nothing from J30.
6. **The trailing conditional region is flagged in `sky_renderer_colors.cpp`'s
   own header** (in words, never quoting the macro) so the next person to move a
   method out of that TU is warned that the region must not be split.

### Plan errors and template notes

1. ⚠ **The method count (49) does not reproduce; it is 47.** Correction 46 asked
   plans to state their counting pattern and this one did not. Sixth unit with a
   slipped derived count.
2. ⚠ **Both CMake line numbers are stale**, count correct — correction 59's
   fourth consecutive instance.
3. **The `tests/`/`scripts/` section predicts "comment-only hits"; there are no
   hits at all.** A unit reconciling the two sections finds a phantom
   discrepancy, exactly as J35 reported for its own plan. **This is now twice —
   the template's boilerplate for that section should say "expect nothing" rather
   than describing hits the surveyor did not verify.**
4. ⚠ **The predicted TU sizes (690 / 500 / 622) assume a zero-line file header
   while the plan simultaneously prescribes a 4-6 line one.** Correction 57's
   `(your header) + <constant> + body` shape would remove the reconciliation;
   here the constant is **19 + 2 = 21**. The plan does flag this in prose, which
   is why it cost nothing — but the table would be better.
5. **The prescribed header length ("four to six lines") cannot hold for the
   colors TU**, which also has to carry the region warning. Mine is 16. Same
   observation J31 made about its own plan; **content should win and the wording
   should say so.**
6. **Correction 60's positive control was free here** because the file has
   directives — worth noting that the control is only needed when the dump comes
   back empty.
7. **Correction 30 fired in J25's inverted form only** (no blank after the
   namespace open in the two new TUs); the tail is clean because the original's
   1768 is blank. Declared, not fixed.

### Deviations

**None.** No method decomposed, no dead content deleted, no include block
trimmed, no reordering, no interface change, no header in `include/` touched, no
`tests/` file touched, no TU added beyond the two this plan names, and the
`extern` was not trimmed from the TUs that do not use it. The preamble (lines
1-19) was copied verbatim into both new TUs with **nothing replaced** — this
file had no pre-existing header comment, so all header content is pure addition.
All three files are mode **644** (`chmod 644` before `git add`; verified in the
commit, no `100644 => 100755`).
