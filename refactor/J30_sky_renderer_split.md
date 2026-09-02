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
