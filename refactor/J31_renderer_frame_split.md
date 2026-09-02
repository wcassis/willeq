# J31 — Batch J Wave 6: split `src/client/graphics/renderer_frame.cpp` (1,752 → 3 TUs, largest ~730)

**Slot: assigned at dispatch. Branch `task/J31-renderer-frame-split` cut from `fable`.
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

## ⚠ T312 IS MERGED — this unit is UNBLOCKED, and that was re-checked, not assumed

The batch plan's §Residual and its in-flight collision matrix both say
`renderer_frame.cpp` is **T312 territory** and must not be boarded until T312
merges. **T312 merged at `c9b02f7e` and is an ancestor of `fable`'s tip
`824adb28`** — verified with
`git merge-base --is-ancestor c9b02f7e HEAD`, not read off a note. **T313 (which
touched `graphics/ui/` and one hunk in `irrlicht_renderer.h`) has also merged
(`f0046f7c`).** There is no live hold on this file.

## ⚠ THE ONE THING THIS UNIT MUST NOT GET WRONG: three tests read this file as text

**Three assertions in two test files open `src/client/graphics/renderer_frame.cpp`
and scan it as text, and all three break when the methods they look for move
out.** This is correction 10 (J11's), and it is **not optional and not scope
creep** — it is part of this unit's territory.

⚠ **The batch plan's §Residual row names the wrong files.** It records
`test_reachability_sweep.cpp:156` and `test_ui_hide_command.cpp:698,706`. The
truth at `824adb28` is **`test_static_ui_scroll.cpp:698` and `:706`, plus
`test_ui_hide_command.cpp:156`** — `test_reachability_sweep.cpp` does **not**
read this file at all. **Re-derive the list yourself; do not trust either
record.**

| site | what it asserts | which new TU holds the text |
|---|---|---|
| `tests/test_static_ui_scroll.cpp:698` — `TEST(ScrollSourceInvariants, NothingElseConsumesTheWheelAccumulator)` | counts `getMouseWheelDelta()` and requires **exactly 2** occurrences | the two occurrences are at **546** and **549** → **`renderer_frame_input.cpp`** |
| `tests/test_static_ui_scroll.cpp:706` — `TEST(ScrollSourceInvariants, TheFrameRoutesTheWheelIntoTheStaticUI)` | requires `staticUIInput_->processWheel(` and `staticUIInput_->processScrollKey(` | **546** and **583** → **`renderer_frame_input.cpp`** |
| `tests/test_ui_hide_command.cpp:156` — `TEST(UIHideSourceInvariants, StaticUiBlockIsGatedOnAllUIHidden)` | requires the literal `if (newUIEnabled_ && uiRenderer_ && !allUIHidden_)` | **1677** → **`renderer_frame_render.cpp`** |

⚠ **All three would FAIL LOUDLY, and the first one is the dangerous shape**: it
asserts a *count of exactly 2*, so after the split it reads **0** from a file
that no longer contains the accumulator — a failure that says "the wheel is
consumed by nothing" when in fact nothing changed. **An executor will misdiagnose
it as a behaviour change.**

**The prescribed repoint — exactly four changed lines, no more:**

1. `test_static_ui_scroll.cpp:698` — path → `src/client/graphics/renderer_frame_input.cpp`.
2. `test_static_ui_scroll.cpp:706` — path → `src/client/graphics/renderer_frame_input.cpp`.
   ⚠ **These two lines are textually IDENTICAL**
   (`    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");`),
   so they are **one distinct line appearing twice** — which is exactly how the
   verifier's `Counter` arithmetic will report them. See §Acceptance.
3. `test_static_ui_scroll.cpp:702` — the **failure-message string**
   `"renderer_frame.cpp";` → `"renderer_frame_input.cpp";`. It is a diagnostic,
   not an assertion; **prefer updating it** (a wrong filename in a failure
   message is exactly the doc rot J21 exists to clean up) and **count it in the
   prediction**.
4. `test_ui_hide_command.cpp:156` — path → `src/client/graphics/renderer_frame_render.cpp`.
   This line differs from the other two (`src` rather than `frame` as the
   variable name), so it is **a separate distinct line**.

**Do not weaken any assertion, do not merge the two tests, and do not add a
fallback that reads both files** — each assertion is about one specific TU and
should stay that way.

⚠ **Run these three tests alone, immediately after the cut, before the full
suite:**
```bash
ctest --test-dir build -R 'ScrollSourceInvariants|UIHideSourceInvariants' --output-on-failure
```

## Measured at `824adb28` (2026-09-02, `fable` tip after J13 merged)

`src/client/graphics/renderer_frame.cpp` — **1,752 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **1,752** |
| preamble | **1-30** — eighteen `#include` lines, an `#ifdef __linux__` group (19-21), an `#ifdef EQT_HAS_GLES2` group (23-27) containing ⚠ **a one-line `extern void gles2Draw3DLinesBatch(...)` at 26**, then `namespace EQT {` / `namespace Graphics {` at 29-30 |
| namespace | `EQT` / `Graphics` opened at **29-30**, closed at **1751-1752**. **Both closes replicate into each new TU.** |
| method definitions | **9** |
| largest method | **546** `IrrlichtRenderer::processFrameRender` (1204-1749); 2nd **472** `processFrame` (32-503); 3rd **211** `processPlayerInput` (606-816) |
| `// ====` banners | **4**, at 505, 993, 1024, 1203 — single-line `// ===== Phase N: … =====` markers, not the three-line pairs other files use |
| conditional preprocessor directives | 23 — **19/21** `__linux__` and **23/27** `EQT_HAS_GLES2` (preamble); **83/90** and **121/123** inside `processFrame` (range 31-504); **1326/1340** `__linux__`, **1345/1347/1349** `EQT_HAS_GLES2` + `#else`, **1397/1411**, **1415/1425** `EQT_HAS_DRM`, **1551/1607**, **1612/1633**, **1706/1708** `WITH_RDP` — ⚠ **all nine body regions after 1203 are inside `processFrameRender`**, which moves whole. **No cut lands inside any of them.** |
| file-scope `static` / anonymous namespaces | **zero of both.** The one non-boilerplate preamble item is the `extern` at 26 — a **declaration**, safe to duplicate. |
| multi-line comment regions | **zero multi-line regions.** `grep -n "/\*"` returns five lines (981, 1364, 1704, 1737, 1739) and **every one is a single-line `/*…*/`** — correction 33's case. Comment depth is 0 on every line. |

⚠ **Correction 33: "no block comments" must mean "no MULTI-LINE comment
regions".** Single-line `/* … */` argument comments are irrelevant; what the
cutting rule needs is **comment depth 0 at every cut**. Compute it, do not
assert it, and beware the trap `eq_movement.cpp:18` set for the orchestrator's
own scanner: a `//` line containing the token `*/` makes a naive depth counter
go negative. Strip `//` tails before counting:

```bash
python3 - <<'EOF'
import re
L=open('src/client/graphics/renderer_frame.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {505,1203}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-30 | preamble: eighteen includes, the two conditional groups (with the `extern` at 26), two namespace opens | copied to each new TU | 30 |
| **31-504** | `processFrame` (**32-503, 472 lines, moved WHOLE**) — the frame orchestrator that calls the four phases | **stays** `renderer_frame.cpp` | **506** |
| **505-1202** | the `// ===== Phase 1: Input =====` banner (505) + `processFrameInput` (506-570) / `processCommonInput` (572-604) / `processPlayerInput` (606-816) / `processInputDeltas` (818-845) / `processChatInput` (847-991) / the Phase 2 banner (993) + `processFrameVisibility` (994-1022) / the Phase 3 banner (1024) + `processFrameSimulation` (1025-1201) — ⚠ **holds `getMouseWheelDelta()` ×2 at 546/549 and both `staticUIInput_->` calls at 546/583** | `renderer_frame_input.cpp` | **730** |
| **1203-1752** | the `// ===== Phase 4: Render =====` banner (1203) + `processFrameRender` (**1204-1749, 546 lines, moved WHOLE and VERBATIM**), with **all nine of its conditional regions inside it** — ⚠ **holds `if (newUIEnabled_ && uiRenderer_ && !allUIHidden_)` at 1677** | `renderer_frame_render.cpp` | **580** |

**Result: 3 TUs** (`renderer_frame.cpp` stays and counts). Largest **~730**.

Arithmetic that must hold: **`30 + 474 + 698 + 550 = 1752` (the third range carries the original's two namespace closes)**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundaries fall where they do: this file has only **nine methods** and
they are already labelled by four `// ===== Phase N =====` banners. The cuts fall
on two of those banners. **31-504** is the orchestrator alone. **505-1202** is
Phases 1-3 (input, visibility, simulation). **1203-1752** is Phase 4 (render)
alone, which is where every remaining conditional lives.

⚠ **`processFrameRender` (546) and `processFrame` (472) are both §Optional
Batch-G candidates and NEITHER is to be decomposed.** They land in TUs of 580 and
506 — **920 and 994 lines of headroom respectively.** No Batch-G decision is
needed. Precedent: J02 (`DispatchZonePacket` 880), J13 (`ProcessBridgeIntents`
580) — both flagged, neither touched.

⚠ **Cuts 505 and 1203 are single-line banners, not the three-line pairs other
files use.** The banner is line 505 and the method signature is line 506 — the
banner travels with its cluster, as always. Both cuts also satisfy J03's rule
independently (503 `}` / 504 blank; 1201 `}` / 1202 blank), so **the two rules
agree at both boundaries.**

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 505 | `503: }` (end of `processFrame`), `504` blank | `505: // ===== Phase 1: Input =====`, then `506: void IrrlichtRenderer::processFrameInput(float deltaTime) {` |
| 1203 | `1201: }` (end of `processFrameSimulation`), `1202` blank | `1203: // ===== Phase 4: Render =====`, then `1204: bool IrrlichtRenderer::processFrameRender(float deltaTime) {` |

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/graphics/renderer_frame.cpp
```

## ⚠ HAZARD 1 — the `extern gles2Draw3DLinesBatch` declaration is the split's only verifier cost

Preamble line **26** is
`extern void gles2Draw3DLinesBatch(void* driver, const float* vertices, unsigned int lineCount);`,
inside the preamble's `#ifdef EQT_HAS_GLES2` group.
`verify_move_only.py`'s `BOILERPLATE` allowlist does **not** cover `extern`.

**Copying the preamble verbatim into two new TUs adds 1 distinct line × 2 extra
copies = 2 substantive ADDED lines, 0 removed.** Correction 24's case, and it is
**expected, not a failure**.

⚠ **Measured: the ONLY caller of `gles2Draw3DLinesBatch` in this file is in `renderer_frame_render.cpp` (range 1203-1752).** The other two TUs will carry a declaration they never use — that is correct and intended, and it is not a reason to trim.

⚠ **Do NOT trim the `extern` out of the TU that does not call it.** §Process is
explicit; trimming is a second, riskier change wearing a move-only commit's
clothes.

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `renderer_frame.cpp`:**

`renderer_frame.cpp` has **zero file-scope `static` symbols, zero anonymous
namespaces, and zero file-scope constants.** Every symbol it defines is an
`IrrlichtRenderer` member with external linkage, declared in
`include/client/graphics/irrlicht_renderer.h`. The single file-scope
*declaration* — `extern void gles2Draw3DLinesBatch` at 26 — declares a symbol
defined in `COpenGLES2Driver.cpp`, not here.

**Therefore correction 21 has NO exposure in this file.** In particular,
`processFrame` calling `processFrameInput` / `processFrameVisibility` /
`processFrameSimulation` / `processFrameRender` across three TUs is an ordinary
link — those are class members, not file statics. Confirm with your own greps and
paste them:

```bash
grep -n '^static \|^namespace\s*{\|^const \|^constexpr \|^extern ' \
    src/client/graphics/renderer_frame.cpp
grep -n 'namespace' src/client/graphics/renderer_frame.cpp
```
(the first: only line 26. The second: exactly four lines — 29, 30, 1751, 1752.)

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `renderer_frame.cpp`'s preamble, lines 1-30, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

**Lines 1-30 verbatim into each new TU**, including the `__linux__` group at
19-21, the `EQT_HAS_GLES2` group at 23-27 **and the `extern` at 26** — see
HAZARD 1 for what that costs. **Do not trim it.**

**Each new TU ends with the two namespace closes, replicated verbatim:**
```
} // namespace Graphics
} // namespace EQT
```
⚠ **Correction 30**: replicating "blank + closes" double-spaces any TU whose body
already ends on a blank. Declare it; do not hand-fix it.

## File-header comments

`renderer_frame.cpp` has **no file-header comment block** — it opens straight
into `#include`. **Each of the three TUs gets a short one** (five to eight lines)
naming its phases and citing `refactor/J31_renderer_frame_split.md`.

⚠ **`renderer_frame_input.cpp`'s and `renderer_frame_render.cpp`'s headers must
each record, in plain words, that `tests/test_static_ui_scroll.cpp` /
`tests/test_ui_hide_command.cpp` scan THIS file as text and for what** — so the
next person to move the wheel accumulator or the `allUIHidden_` gate knows there
is a text-scanning test pointed at it. That sentence is the cheapest defence
against the exact defect this unit had to repair, and J13's Review is the
precedent for writing it.

`renderer_frame_render.cpp`'s header must also record that **`processFrameRender`
is 546 lines and is a recorded Batch-G candidate J31 deliberately did not touch**,
and that **nine `#ifdef` regions live wholly inside it.**

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
grep -rn 'renderer_frame\.cpp' CMakeLists.txt tests/ scripts/
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
| `CMakeLists.txt:489` | `SET(GRAPHICS_SOURCES` (opens at `:439`) | ⚠ **YES — add both new TUs here** |

**No `tests/cmake/` fragment names this file** — `test_static_ui_scroll` and
`test_ui_hide_command` read it as *text* at runtime via `WILLEQ_REPO_ROOT`; they
do not compile it. **This unit changes two test sources but adds no target and no
source to any target, so it needs no `tests/cmake/` edit. Confirm that.**
**This unit does NOT touch `gles2_zone_benchmark`.**

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'renderer_frame' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

⚠ **This file HAS text readers — see the box at the top of this plan.** The two
greps return:

- `grep -rn 'renderer_frame' tests/ scripts/` — **three functional readers**
  (`test_static_ui_scroll.cpp:698`, `:706`, `test_ui_hide_command.cpp:156`), one
  **diagnostic string** (`test_static_ui_scroll.cpp:702`), and comment-only hits
  in `test_read_and_reset_ownership.cpp:14,98`, `test_bridge_queue_race.cpp:8`
  and `test_ui_hide_command.cpp:18`. ⚠ **Record whether those four comments go
  stale; do not fix them — that is J21's successor pass** (correction 24).
- `grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u` — returns
  `readFile(kRoot / "src/client/graphics/renderer_frame.cpp")` twice (once as
  `frame`, once as `src`).

⚠ **The two greps are not interchangeable, and neither one finds the THIRD kind
of guard.**

⚠ **`tests/test_first_person_camera_single_formula.cpp` sweeps EVERY `.cpp` and
`.h` under `src/client/graphics` RECURSIVELY** (`kGraphicsDir`,
`fs::recursive_directory_iterator`) **and keys two of its three assertions on
FILENAME.** Any TU this unit creates under that tree enters the sweep
automatically — **this is a third kind of text-scanning guard, one that names no
file and therefore appears in NEITHER of the two standard greps.**

- **Part 1** — a file that both mentions `CameraMode::FirstPerson` **and** writes
  `camera_->setPosition(` / `camera_->setTarget(` is an offender unless its
  filename is `renderer_camera.cpp` (`kHelperTU`).
- **Part 2** — each first-person tunable may be read from **at most one** TU
  under that tree, except `firstPersonMinLookPitchDegrees`, which may be read
  from `renderer_camera.cpp` and **`renderer_player_physics.cpp`, named as a
  string literal (`kFloorSecondReader`)**.

At `824adb28` **`renderer_frame.cpp` matches none of the first-person tokens** —
`grep -n 'CameraMode::FirstPerson\|camera_->setPosition(\|camera_->setTarget(\|kFirstPerson\|firstPersonM\|firstPersonV\|computeFirstPersonCamera' src/client/graphics/renderer_frame.cpp`
returns nothing — so it is invisible to both parts of that sweep and the new TUs
will be too. **Confirm at your tip and paste the (empty) grep.**

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
  - ⚠ **`ScrollSourceInvariants` and `UIHideSourceInvariants` are the tests this
    unit is most likely to break, and they are also the tests that prove the
    repoint worked.** Run them alone, before the full suite, immediately after
    the cut:
    `ctest --test-dir build -R 'ScrollSourceInvariants|UIHideSourceInvariants' --output-on-failure`.
    A `NothingElseConsumesTheWheelAccumulator` failure reporting **0** where 2 was
    expected means you repointed `:698` wrongly or not at all — **it is not a
    behaviour change.**
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
- ⚠ **Expected verifier result for J31: **EXIT 1, with exactly 6 substantive ADDED lines and 4 substantive REMOVED lines — 2 from the duplicated `extern`, the other 4 in the two test files, none in the split itself.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - Everything in the preamble except line 26 is boilerplate: `#include`,
    `#ifdef`, `#endif`, and blanks the normalizer drops.
  - **`extern` is NOT in the allowlist.** Line 26 is 1 distinct line with count 1
    before and 3 after → **`+2x` = 2 ADDED**, 0 removed.
  - The replicated namespace closes are bare `}` with a `//` tail — boilerplate;
    the `// ===== Phase N =====` banners are comments — boilerplate.
  - Header-comment lines are `//` — boilerplate.
  - **`test_static_ui_scroll.cpp:698` and `:706` are textually IDENTICAL**, so the
    old line is **one distinct line with count 2** → **`-2x`** for it and
    **`+2x`** for its replacement.
  - **`test_ui_hide_command.cpp:156`** is a *different* distinct line (`src`, not
    `frame`) → **`-1x` / `+1x`**.
  - **`test_static_ui_scroll.cpp:702`**, the diagnostic string → **`-1x` / `+1x`**.
  - **Total: ADDED 2 + 2 + 1 + 1 = 6; REMOVED 2 + 1 + 1 = 4.**

  ⚠ **If you decline to update the `:702` diagnostic string, the prediction
  becomes ADDED 5 / REMOVED 3. Say which you did** — J09's plan predicted a
  number one of its own sections invalidated, and a unit trusting the count over
  the pasted diff would have reported a phantom anomaly (correction 19).

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
  git show "$BASE:src/client/graphics/renderer_frame.cpp" | sed -n '31,1750p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Run this over the THREE `.cpp` bodies only. `test_static_ui_scroll.cpp` and
  `test_ui_hide_command.cpp` are NOT part of the moved content** and must not be
  concatenated into either side — their four edits are accounted for by the
  verifier, above.
  ⚠ **Strip the two namespace closes from each TU before comparing**, and note
  that the original's own closes (1751-1752) are inside range 1203-1752 — so the
  `before` side must run to **1750**, not 1752 (correction 20: suspect the proof
  first).
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`31-504, 505-1202, 1203-1750`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/graphics/renderer_frame.cpp" | sed -n '31,1750p'`.
  **1,720 lines. Expect `cmp` exit 0.**
  ⚠ **Declare count variables `local` in your proof helpers.** More generally
  (correction 20): **when a proof fails, suspect the proof first.** J05's
  non-`local` `n` produced a 247-line shortfall that read exactly like a dropped
  range; J09's one proof failure was range arithmetic against the produced
  files' numbering; J08's failed **exactly at its relocation seam**. **Three
  units, three proof bugs, zero real defects.**
  ⚠ **Additionally and separately, prove `processFrameRender` survived byte for
  byte** — 546 lines, 31% of the file, nine conditional regions, and the thing a
  reviewer looks at first:
  ```bash
  diff <(git show "$BASE:src/client/graphics/renderer_frame.cpp" | sed -n '1204,1749p') \
       <(sed -n '<its new range>p' src/client/graphics/renderer_frame_render.cpp)
  ```
  **546 lines, exit 0. Paste it.**
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
  file**: `src/client/graphics/renderer_frame.cpp`, `src/client/graphics/renderer_frame_input.cpp`, `src/client/graphics/renderer_frame_render.cpp`, `tests/test_static_ui_scroll.cpp`, `tests/test_ui_hide_command.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **4**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **1,752** this plan measured,
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
   (this plan predicts 1 / 7 / 1 of the 9), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. ⚠ **The text-scan repoint, in full**: the before/after of all four edited
    lines, the `ctest -R 'ScrollSourceInvariants|UIHideSourceInvariants'` result,
    and a note on whether you updated the `:702` diagnostic string (which changes
    the verifier prediction).
12. ⚠ **The correction to the record.** The batch plan's §Residual row names
    `test_reachability_sweep.cpp:156` and `test_ui_hide_command.cpp:698,706` for
    this file. **Both are wrong**: `test_reachability_sweep.cpp` does not read
    this file, and 698/706 are in `test_static_ui_scroll.cpp`. **State the correct
    list in your Review so the batch plan can be fixed.**
13. **Confirmation that T312 and T313 are ancestors of your base**, by
    `git merge-base --is-ancestor`, not by reading a note.
14. **The `test_first_person_camera_single_formula.cpp` check, pasted.**

## Territory

`src/client/graphics/renderer_frame.cpp`, the two new TUs beside it
(`…_input.cpp`, `…_render.cpp`), **`tests/test_static_ui_scroll.cpp`** and
**`tests/test_ui_hide_command.cpp`** (the four enumerated edits — in territory,
not optional), `CMakeLists.txt` (the one `GRAPHICS_SOURCES` site), and this plan
file. **Nothing else** — not `include/client/graphics/irrlicht_renderer.h` (which
is **J17's** file), not any other `renderer_*.cpp`, not
`src/client/graphics/renderer_event_receiver.cpp` (which those same two tests also
read, at `test_static_ui_scroll.cpp:690` and `test_ui_hide_command.cpp:146` —
**leave both alone**), not `tests/cmake/*.cmake`, not `docs/`.

⚠ **SERIALISATION with J33** (`renderer_player_physics.cpp`): disjoint files and
disjoint CMake lines, but both are `IrrlichtRenderer` TUs in
`SET(GRAPHICS_SOURCES` and both insert lines into it. **Merge one at a time and
rebase.**

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
- A fourth text reader of this file has appeared.
- Any cut boundary has drifted inside one of the nine conditional regions after
  line 1203.

---

## Review (executed 2026-09-02, slot2, branch `task/J31-renderer-frame-split`)

**Base / tip.** Branch cut from `fable` at **`0c95f7df`** ("Batch J Wave 6: plan
the last thirteen files (J23-J35)…"), which is `$BASE` =
`git merge-base fable HEAD` = `0c95f7df4cf59481d6be267f181052c026a49a0f`.
Split commit **`13ac54ca`**. **PASS on every bar item.** The verifier exits 1
with **exactly** the predicted 6-added / 4-removed set — the §Process step-4 /
correction-5 case, requesting orchestrator sign-off.

**One finding worth the plan's attention, found during the cut and fixed before
the build: the file-header warning the plan prescribes can itself satisfy the
guard it warns about.** See §11.

### 1. Line count at my tip — zero drift

`src/client/graphics/renderer_frame.cpp` = **1,752 lines**, exactly the plan's
measurement at `824adb28`.
`git diff 824adb28 HEAD -- src/client/graphics/renderer_frame.cpp
tests/test_static_ui_scroll.cpp tests/test_ui_hide_command.cpp` is **empty** —
all three inputs are byte-identical to the survey commit. Every cut boundary,
every preprocessor line number, every method line number and the single CMake
add-site reproduced **exactly**. Correction 9's "every survey number is stale"
warning did not fire here.

### 2. §Measured re-verified at my tip — every property holds

```
$ wc -l src/client/graphics/renderer_frame.cpp
1752

$ grep -n '^[A-Za-z_].*IrrlichtRenderer::' src/client/graphics/renderer_frame.cpp
32:bool IrrlichtRenderer::processFrame(float deltaTime) {
506:void IrrlichtRenderer::processFrameInput(float deltaTime) {
572:void IrrlichtRenderer::processCommonInput(const std::vector<RendererEvent>& actions) {
606:void IrrlichtRenderer::processPlayerInput(const std::vector<RendererEvent>& actions) {
818:void IrrlichtRenderer::processInputDeltas(float deltaTime) {
847:void IrrlichtRenderer::processChatInput() {
994:void IrrlichtRenderer::processFrameVisibility() {
1025:void IrrlichtRenderer::processFrameSimulation(float deltaTime) {
1204:bool IrrlichtRenderer::processFrameRender(float deltaTime) {

$ grep -nE '^// ={4,}' src/client/graphics/renderer_frame.cpp
505:// ===== Phase 1: Input =====
993:// ===== Phase 2: Visibility =====
1024:// ===== Phase 3: Simulation =====
1203:// ===== Phase 4: Render =====
$ grep -cE '^// ={4,}' src/client/graphics/renderer_frame.cpp
4
```

**9 methods; largest `processFrameRender` 546 (1204-1749); 2nd `processFrame`
472 (32-503); 3rd `processPlayerInput` 211 (606-816).** 4 banners, single-line.
Preamble 1-30 exactly as described: 18 includes, `__linux__` 19-21,
`EQT_HAS_GLES2` 23-27 with the `extern` at **26**, namespace opens at 29-30.
Namespace closes at **1751-1752**. Zero file-scope statics, zero anonymous
namespaces, zero multi-line comment regions.

### 3. Correction 21, re-derived (not cited)

```
$ grep -n '^static \|^namespace\s*{\|^const \|^constexpr \|^extern ' src/client/graphics/renderer_frame.cpp
26:extern void gles2Draw3DLinesBatch(void* driver, const float* vertices, unsigned int lineCount);

$ grep -n 'namespace' src/client/graphics/renderer_frame.cpp
29:namespace EQT {
30:namespace Graphics {
1751:} // namespace Graphics
1752:} // namespace EQT
```

Exactly as the plan states: **one file-scope item, and it is a declaration, not
a definition.** I located the definition rather than trusting the plan:

```
$ grep -rn 'void gles2Draw3DLinesBatch' src/ docker/
...
docker/irrlicht-drm/COpenGLES2Driver.cpp:2214:void gles2Draw3DLinesBatch(void* driver, const float* vertices, unsigned int lineCount)
```

Six other graphics TUs (`irrlicht_renderer.cpp:77`, `renderer_hud_env.cpp:77`,
`renderer_camera.cpp:77`, `renderer_init.cpp:77`, `renderer_slash_commands.cpp:77`,
`renderer_event_receiver.cpp:77`) already carry the identical declaration, so
duplicating it a further two times is established practice in this tree, not a
novelty. ⚠ **The in-file comment at 24-25 says "Declared in
irrlicht_renderer.cpp"; the definition is actually in
`docker/irrlicht-drm/COpenGLES2Driver.cpp:2214`** (the plan says the same). That
comment is pre-existing and slightly wrong; it moved verbatim into all three
TUs. **Recorded, not fixed** — move-only.

**Correction-21 mapping.** The only file-scope symbol is a declaration of an
external symbol, so there is no definition/consumer range to align. Its sole
in-file caller is inside `processFrameRender`, i.e. range 1203-1750
(`renderer_frame_render.cpp`), which carries the declaration; the other two TUs
carry an unused declaration, which is correct and was **not** trimmed. **Zero
correction-21 exposure — same conclusion as the plan's audit, reached
independently.**

Cross-TU calls (`processFrame` → `processFrameInput` / `processFrameVisibility` /
`processFrameSimulation` / `processFrameRender`) are `IrrlichtRenderer` members
with external linkage declared in `include/client/graphics/irrlicht_renderer.h` —
ordinary links. The build confirms it.

### 4. Comment depth at every cut — computed, not asserted

Using the plan's `//`-stripping counter:

```
depth after line 504 = 0
depth after line 505 = 0
depth after line 1202 = 0
depth after line 1203 = 0
final depth 0
```

Comment depth is **0** at both cuts and at end of file. `grep -n "/\*"` returns
five lines (981, 1364, 1704, 1737, 1739), every one a single-line `/*…*/` —
correction 33's case, and it holds.

### 5. Preprocessor dump — no cut lands inside a conditional

```
19: #ifdef __linux__          1326: #ifdef __linux__
21: #endif                    1340: #endif
23: #ifdef EQT_HAS_GLES2      1345: #ifdef EQT_HAS_GLES2
27: #endif                    1347: #else
83: #ifdef EQT_HAS_GLES2      1349: #endif
90: #endif                    1397: #ifdef EQT_HAS_GLES2
121: #ifdef EQT_HAS_GLES2     1411: #endif
123: #endif                   1415: #ifdef EQT_HAS_DRM
                              1425: #endif
                              1551: #ifdef EQT_HAS_GLES2
                              1607: #endif
                              1612: #ifdef EQT_HAS_GLES2
                              1633: #endif
                              1706: #ifdef WITH_RDP
                              1708: #endif
```

**23 directives**, exactly as measured. Two regions in the preamble (copied
whole into every TU); two inside `processFrame` (83/90, 121/123 — inside the
*stays* range); **nine regions, all after 1203, all wholly inside
`processFrameRender`**, which moves whole. **Neither cut (505, 1203) falls
between an `#if`/`#endif` pair.**

### Cut boundaries, pasted

```
Cut 505:                              Cut 1203:
  502:     return true;                 1199:     updateHUD();
  503: }                                1200:     frameTimings_.hudUpdate = measureSection();
  504: (blank)                          1201: }
  505: // ===== Phase 1: Input =====    1202: (blank)
  506: void IrrlichtRenderer::          1203: // ===== Phase 4: Render =====
       processFrameInput(float …) {     1204: bool IrrlichtRenderer::
                                              processFrameRender(float …) {
```

Both satisfy J03's rule (blank line whose predecessor closes a method) *and* the
banner tie-breaker, which agree at both boundaries.

### 6. Every file-header comment line written, enumerated

No other comment in the tree was changed. All three headers are new; the
original had none.

**`renderer_frame.cpp` — 7 lines added at the top:**
```
1  // renderer_frame.cpp - IrrlichtRenderer's per-frame orchestrator.
2  //
3  // Holds processFrame() alone, which sequences the four frame phases.
4  // Phase 1 (input), Phase 2 (visibility) and Phase 3 (simulation) live in
5  // renderer_frame_input.cpp; Phase 4 (render) lives in
6  // renderer_frame_render.cpp.
7  // Split per refactor/J31_renderer_frame_split.md (move-only).
```

**`renderer_frame_input.cpp` — 17 lines:**
```
 1  // renderer_frame_input.cpp - IrrlichtRenderer frame phases 1-3.
 2  //
 3  // Phase 1 input (processFrameInput, processCommonInput, processPlayerInput,
 4  // processInputDeltas, processChatInput), Phase 2 visibility
 5  // (processFrameVisibility), Phase 3 simulation (processFrameSimulation).
 6  // The orchestrator processFrame() stays in renderer_frame.cpp; Phase 4 is in
 7  // renderer_frame_render.cpp.
 8  //
 9  // WARNING: tests/test_static_ui_scroll.cpp reads THIS FILE as text. It counts
10  // the reads of the event receiver's mouse-wheel accumulator and requires
11  // exactly two, and it requires that the static UI input object's wheel and
12  // scroll-key routing calls both appear here. Moving the wheel accumulator or
13  // either routing call out of this TU fails those assertions with a message
14  // that wrongly reads as a behaviour change. The literal tokens are spelled
15  // out in that test, deliberately not here: a comment naming them would
16  // satisfy the guard on its own.
17  // Split per refactor/J31_renderer_frame_split.md (move-only).
```

**`renderer_frame_render.cpp` — 17 lines:**
```
 1  // renderer_frame_render.cpp - IrrlichtRenderer frame phase 4 (render).
 2  //
 3  // Holds processFrameRender() alone. Phases 1-3 live in
 4  // renderer_frame_input.cpp; the orchestrator processFrame() stays in
 5  // renderer_frame.cpp.
 6  //
 7  // processFrameRender is 546 lines and is a recorded Batch-G decomposition
 8  // candidate that J31 deliberately did NOT touch (move-only). All nine of this
 9  // file's conditional regions (__linux__, EQT_HAS_GLES2, EQT_HAS_DRM,
10  // WITH_RDP) live wholly inside it.
11  //
12  // WARNING: tests/test_ui_hide_command.cpp reads THIS FILE as text and
13  // requires that the static-UI draw block's guard, which ands the new-UI flag
14  // and the UI renderer against the negated all-UI-hidden flag, appear here
15  // verbatim. The literal condition is spelled out in that test, deliberately
16  // not here: a comment naming it would satisfy the guard on its own.
17  // Split per refactor/J31_renderer_frame_split.md (move-only).
```

⚠ **Correction 30 did not fire.** Each body range already ends on the original's
blank line (504, 1202, 1750), and I appended the two closes **directly** with no
extra blank, so no TU is double-spaced and no hand-fix was needed. The render TU
reproduces the original's exact 1749-1752 tail byte for byte.

### 7. CMake add-sites — **ONE**, as the plan says; the dispatch's "12" is wrong

```
$ find . \( -name 'CMakeLists.txt' -o -name '*.cmake' \) -not -path '*/third_party/*' \
       -not -path '*/build*' -exec grep -Hn -i 'renderer_frame' {} +
./CMakeLists.txt:489:        src/client/graphics/renderer_frame.cpp
```

Searched **all 23** CMake files in the tree (the top-level, three toolchain
files, `tests/CMakeLists.txt` and all 18 `tests/cmake/*.cmake` fragments) —
enumerated by `find`, so correction 26's trap (naming `tests/CMakeLists.txt` and
missing the fragments) could not apply, and `-i` covers the UPPERCASE-command
trap. **Exactly one site**, `SET(GRAPHICS_SOURCES` at `CMakeLists.txt:489`; both
new TUs added there, +2 lines.

⚠ **Correction to the dispatch, not to the plan.** My dispatch brief stated
"This file has 12 CMake add-sites — the most in Wave 6". **That is false and it
belongs to J32**: correction 39 of the batch plan reads "J31 1, **J32 12**". The
plan file's §CMake correctly says ONE. I measured ONE. **The 12 was a
transcription error in the dispatch; the batch plan and this plan are both
right.** Flagging it because a dispatch that tells an executor to expect twelve
sites invites them to keep hunting past a correct answer, or worse, to widen
territory to find them.

**No `tests/cmake/` fragment names this file** — confirmed by the same
exhaustive `find`. This unit adds no target and no source to any target, so no
fragment edit was needed. `gles2_zone_benchmark` untouched.

### 8. Both content proofs, plus the standalone `processFrameRender` diff

`$BASE = 0c95f7df4cf59481d6be267f181052c026a49a0f`.

**Byte-identical re-concatenation** (ranges `31-504`, `505-1202`, `1203-1750`,
in original order, taken from the *produced* files' body ranges 38-511 / 48-745 /
48-595):
```
$ cmp orig_body.txt reconcat.txt ; echo $?
0
$ wc -l reconcat.txt
1720
```
**1,720 lines, `cmp` exit 0.**

**Line-multiset check** (three `.cpp` bodies only, each stripped of header,
preamble and namespace closes; `before` runs to **1750**, not 1752, since the
original's own closes are inside the third range):
```
$ wc -l before.txt after.txt
1720 before.txt
1720 after.txt
$ diff before.txt after.txt ; echo $?
0            -> MULTISET-OK
```
The two test files were **not** concatenated into either side.

**`processFrameRender` byte for byte:**
```
$ diff <(git show "$BASE:src/client/graphics/renderer_frame.cpp" | sed -n '1204,1749p') \
       <(sed -n '49,594p' src/client/graphics/renderer_frame_render.cpp) ; echo $?
0
```
**546 lines, exit 0.** First and last lines of the new range confirm the
alignment: `49` is `bool IrrlichtRenderer::processFrameRender(float deltaTime) {`
and `594` is `}`.

No proof failed, so correction 20's "suspect the proof first" was not needed —
worth recording that the batch's three-for-three proof-bug streak has now broken.

**Verifier — EXIT 1 with EXACTLY the predicted set:**
```
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/slot2
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt

Files verified: 5
Boilerplate: +99 / -0 lines (allowed)

SUBSTANTIVE ADDED lines (6):
  +1x  "renderer_frame_input.cpp";
  +2x  const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame_input.cpp");
  +1x  const std::string src = readFile(kRoot / "src/client/graphics/renderer_frame_render.cpp");
  +2x  extern void gles2Draw3DLinesBatch(void* driver, const float* vertices, unsigned int lineCount);

SUBSTANTIVE REMOVED lines (4):
  -1x  "renderer_frame.cpp";
  -2x  const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");
  -1x  const std::string src = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");

FAIL: substantive line changes detected. …
VERIFIER EXIT=1
```

**ADDED 6 / REMOVED 4 — the prediction held exactly, line for line, including
the `+2x`/`-2x` `Counter` arithmetic on the two textually identical
`test_static_ui_scroll.cpp` lines and the `+2x` on the duplicated `extern`.**
I **did** update the `:702` diagnostic string, so the 6/4 branch of the
prediction applies rather than the 5/3 branch. **Nothing beyond the enumerated
set was flagged; nothing to chase. Requesting orchestrator sign-off** per
§Process step 4 / correction 5.

### 9. Final line counts, method counts, timings

| file | lines | methods | note |
|---|---|---|---|
| `src/client/graphics/renderer_frame.cpp` | **513** | **1** | `processFrame` |
| `src/client/graphics/renderer_frame_input.cpp` | **747** | **7** | phases 1-3 |
| `src/client/graphics/renderer_frame_render.cpp` | **597** | **1** | `processFrameRender` |
| total | 1,857 | **9** | vs 1,752 in one TU |

**Method counts are 1 / 7 / 1 = 9, exactly as predicted.** Largest TU **747**,
comfortably under the plan's ~730 target and 753 under the warn ceiling.

**The plan's estimates were right to the line, once the headers are added back**
(correction 12 / 19): predicted `506 / 730 / 580` bodies plus my `7 / 17 / 17`
header lines = **513 / 747 / 597**. Exact. The plan predicted 14-line-equivalent
headers loosely as "five to eight lines"; I wrote 7 / 17 / 17 because the two
warning paragraphs the plan **requires** (§File-header comments) do not fit in
eight lines. **That is the one place the plan's own prescriptions are internally
tight** — "five to eight lines" and "must record, in plain words, that
tests/… scan THIS file as text and for what" cannot both be met. I honoured the
content requirement over the length suggestion. Worth relaxing to "five to
eight, or as long as the required warning needs" in the template.

Banner distribution after the split: `renderer_frame.cpp` **0**,
`renderer_frame_input.cpp` **3** (Phases 1, 2, 3), `renderer_frame_render.cpp`
**1** (Phase 4) — 4 total, conserved (`grep -cE`, per correction 28).

**Wall clock ≈ 75 min**, of which the mechanical cut + CMake was **under 4 min**.
Timings:

| step | result | wall |
|---|---|---|
| `cmake --build build -j24` | **exit 0** | ~11.5 min (backgrounded, not a 143) |
| guard tests alone | 5/5 pass | 23 s (after one re-run, see below) |
| `ctest -j6` full | **3506/3507** | ~2.4 min |
| `scripts/build-gles2-x86.sh` | **exit 0** | ~12 min |
| `scripts/build-arm-noble.sh` | **exit 0** | ~11 min (run concurrently with gles2) |
| visual gate | **PASS** | ~80 s |
| `check_file_sizes.sh --files` | 5 ok, **0 warn 0 fail** | s |

**Slowest step: `build-gles2-x86.sh` (~12 min).** Wave concurrency (slot1 J25,
slot3 J33) did stretch the tail — the first `cmake --build` invocation ran past
the 540 s `wait_for` deadline and needed a second poll — but **no step failed on
contention and nothing needed a re-run for load**, so correction 14 applies: this
is a clean run, not an anomaly.

**No 143 anywhere; every build reported its own exit code.** I ran the ELF scan
over all of `build/bin` regardless (correction 32 — "run it either way; it costs
seconds"): **0 bad binaries.**

**One discovery flake, correctly classified.** The first
`ctest -R 'ScrollSourceInvariants|UIHideSourceInvariants'` died with **exit 8** in
`GoogleTestAddTests.cmake` naming
`test_eq_action_handler_enum_guards` — a binary **this unit does not touch**.
Per correction 16 I checked the binary before concluding anything:
`readelf -h` exit 0 and `--gtest_list_tests` exit 0, i.e. **intact**. It was
cold-cache/wave contention (the J05/J07 class), and the identical re-run passed.
**Not truncation, not a test failure.** Worth noting the plan's warning is
accurate and load-bearing: the naive read of "exit 8 in discovery" would have
sent me hunting a mid-link truncation in an unrelated binary.

**`ctest -j6` full suite: 3507 tests, 3506 passed, 1 failed —
`AudioManagerTest.PreloadCommonSounds`, the documented ALSA host quirk in
`refactor/test_baseline.txt`. Zero new failures.**
`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` did **not**
flake this run.

**Visual gate: `=== VISUAL GATE: PASS ===`, goldens untouched** —
`git status --porcelain` after the gate listed only this unit's four modified and
two new files, no golden. All three zones passed on both CPU p95 and image
comparison (`qeynos2`, `gfaydark` p95 9.345 vs ceiling 10.672, `befallen` p95
9.508 vs ceiling 10.948 — closer to the ceiling than J05's re-run measured, which
is the wave load, but under it on the first attempt with **zero image
mismatches**). **The frame loop's behaviour is unchanged.**

### 10. Findings only, not fixes

- **`docs/module_map.md:91`** reads
  `| renderer_frame.cpp | 1,752 | processFrame + input/visibility/simulation/render sub-passes |`.
  That row is now wrong in both its size and its content column, and **two new
  rows are owed** for `renderer_frame_input.cpp` and `renderer_frame_render.cpp`.
  **Recorded for J21's successor pass; not acted on.**
- **Four comment-only mentions of `renderer_frame.cpp` now name the wrong TU**
  (correction 24 — I checked each **at `$BASE`**, and all four were accurate
  before this split, so unlike J06/J07/J08's cases this rot **is** newly created
  by J31 and should be attributed to it):
  - `tests/test_read_and_reset_ownership.cpp:14` and `:98` — both cite
    "renderer_frame.cpp's processFrameInput()", now in
    **`renderer_frame_input.cpp`**.
  - `tests/test_bridge_queue_race.cpp:8` — cites "renderer_frame.cpp's" for
    render-thread hotkey decoding, now in **`renderer_frame_input.cpp`**.
  - `tests/test_ui_hide_command.cpp:18` — cites `(renderer_frame.cpp)` for the
    static UI block, now in **`renderer_frame_render.cpp`**.
  All four are **comments in files outside this unit's four prescribed edits**;
  fixing them is a second change in a move-only commit. **Recorded, not fixed.**
- The pre-existing comment at `renderer_frame.cpp:24-25` ("Declared in
  irrlicht_renderer.cpp") misnames the definition site, which is
  `docker/irrlicht-drm/COpenGLES2Driver.cpp:2214`. It moved verbatim into all
  three TUs, so the tree now has **three** copies of a slightly wrong comment
  where it had one (and six other graphics TUs already carry the same text).
  **Recorded, not fixed** — and a candidate for J21.
- **No dead content** was found or removed.

### 11. The text-scan repoint, in full — and the header-comment trap

**Four lines changed, exactly as prescribed, no more.**

| file:line | before | after |
|---|---|---|
| `test_static_ui_scroll.cpp:698` | `    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");` | `…/renderer_frame_input.cpp");` |
| `test_static_ui_scroll.cpp:702` | `                        "renderer_frame.cpp";` | `                        "renderer_frame_input.cpp";` |
| `test_static_ui_scroll.cpp:706` | `    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");` | `…/renderer_frame_input.cpp");` |
| `test_ui_hide_command.cpp:156` | `    const std::string src = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");` | `…/renderer_frame_render.cpp");` |

**I DID update the `:702` diagnostic string**, so the verifier prediction is the
**6/4** branch, which is what it reported. No assertion was weakened, no test
merged, no both-files fallback added. `renderer_event_receiver.cpp`'s two
readers (`test_static_ui_scroll.cpp:690`, `test_ui_hide_command.cpp:146`) were
left untouched, as territory requires.

The tokens landed where the plan predicted, confirmed in the produced files by
substring count (not `grep -c`, which counts lines):

```
getMouseWheelDelta()                  in renderer_frame_input.cpp  = 2
staticUIInput_->processWheel(         in renderer_frame_input.cpp  = 1
staticUIInput_->processScrollKey(     in renderer_frame_input.cpp  = 1
if (newUIEnabled_ && uiRenderer_ && !allUIHidden_)
                                      in renderer_frame_render.cpp = 1
```
and **zero** occurrences of any of them in the other TUs.

```
$ ctest --test-dir build -R 'ScrollSourceInvariants|UIHideSourceInvariants' --output-on-failure
1/5 Test #2623: UIHideSourceInvariants.KeyEventSwitchQueuesTheRendererAction .... Passed 0.04 sec
2/5 Test #2624: UIHideSourceInvariants.StaticUiBlockIsGatedOnAllUIHidden ........ Passed 0.02 sec
3/5 Test #2722: ScrollSourceInvariants.OnEventAccumulatesTheMouseWheel .......... Passed 0.03 sec
4/5 Test #2723: ScrollSourceInvariants.NothingElseConsumesTheWheelAccumulator ... Passed 0.04 sec
5/5 Test #2724: ScrollSourceInvariants.TheFrameRoutesTheWheelIntoTheStaticUI .... Passed 0.03 sec

100% tests passed, 0 tests failed out of 5
```

#### ⚠ NEW HAZARD — the prescribed file-header warning can satisfy the guard it warns about

**My first draft of the two headers followed §File-header comments literally and
named the scanned tokens verbatim.** That is wrong in two distinct ways, and one
of them is silent:

1. **Loud:** `renderer_frame_input.cpp`'s header contained the literal
   `getMouseWheelDelta()`. `NothingElseConsumesTheWheelAccumulator` counts
   **substring occurrences of the whole file**, comments included, so the count
   became **3** and the test would have failed asserting "expected exactly the UI
   read and the no-UI drain" — the *same* misleading message the plan warns about,
   arriving from the opposite direction. I caught this before building.
2. **Silent, and worse:** the same headers contained the literal
   `staticUIInput_->processWheel(`, `staticUIInput_->processScrollKey(` and
   `if (newUIEnabled_ && uiRenderer_ && !allUIHidden_)`. Those three assertions
   are `find(...) != npos`. **A comment naming the token satisfies the assertion
   on its own** — so if someone later deleted the real `allUIHidden_` gate, the
   guard would still pass, on the strength of the header comment describing it.
   **The defence J31 was told to write would have disarmed the guard it was
   defending.** This is exactly the J11 shape ("a guard left pointing at the
   wrong file can pass while asserting nothing"), reached by a new route: a guard
   pointing at the *right* file, satisfied by prose.

**Resolution:** both headers describe the scanned tokens **in words, never as
literals**, and say so explicitly ("The literal tokens are spelled out in that
test, deliberately not here: a comment naming them would satisfy the guard on its
own"). The counts above confirm 2 / 1 / 1 / 1 with the headers in place.

**Template change recommended (correction 40):** §File-header comments currently
says a new TU's header "must record, in plain words, that <test> scans THIS file
as text and for what". Add: **"and must NOT contain any literal token that test
searches for — describe it. A `find != npos` guard is satisfied by a comment, so
naming the token in the header silently disarms the guard; a counting guard is
broken loudly by it."** Every future Wave-6+ unit whose file has a text reader
inherits this, and the two failure modes look nothing alike.

### 12. The correction to the record — batch plan §Residual is wrong for this file

**The batch plan's §Residual row for `renderer_frame.cpp` names
`test_reachability_sweep.cpp:156` and `test_ui_hide_command.cpp:698,706`.
Both citations are wrong**, as this plan says and as I re-derived independently
at my tip:

```
$ grep -rn 'renderer_frame' tests/ scripts/
tests/test_bridge_queue_race.cpp:8://     the RENDER thread (OnEvent's decoded hotkeys; renderer_frame.cpp's
tests/test_static_ui_scroll.cpp:698:    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");
tests/test_static_ui_scroll.cpp:702:                        "renderer_frame.cpp";
tests/test_static_ui_scroll.cpp:706:    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");
tests/test_read_and_reset_ownership.cpp:14://   * renderer_frame.cpp's processFrameInput() (RENDER thread) -- the real
tests/test_read_and_reset_ownership.cpp:98:        // The render thread's consumer (renderer_frame.cpp processFrameInput).
tests/test_ui_hide_command.cpp:18: *      (renderer_frame.cpp). The STATIC UI block right below them -- chat,
tests/test_ui_hide_command.cpp:156:    const std::string src = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");
```
```
$ grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
… 156:    const std::string src = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");
… 698:    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");
… 706:    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");
```

**`test_reachability_sweep.cpp` does not read this file at all** — it appears
nowhere in either grep. **The correct list, for §Residual:**

| site | kind |
|---|---|
| `tests/test_static_ui_scroll.cpp:698` | functional (`readFile`, count == 2) |
| `tests/test_static_ui_scroll.cpp:702` | diagnostic string |
| `tests/test_static_ui_scroll.cpp:706` | functional (`readFile`, two `find != npos`) |
| `tests/test_ui_hide_command.cpp:156` | functional (`readFile`, one `find != npos`) |
| `tests/test_bridge_queue_race.cpp:8`, `tests/test_read_and_reset_ownership.cpp:14`, `:98`, `tests/test_ui_hide_command.cpp:18` | comment-only |

**Three functional readers, one diagnostic, four comments. No fourth functional
reader appeared**, so that STOP condition did not fire. **Both standard greps
were needed and neither alone was sufficient** — the first finds the diagnostic
string at `:702`, which the second cannot see.

### 13. T312 and T313 are ancestors of my base — verified, not read off a note

```
$ git merge-base --is-ancestor c9b02f7e HEAD ; echo $?
0
$ git merge-base --is-ancestor f0046f7c HEAD ; echo $?
0
```
Both exit 0. **No live hold on this file.**

### 14. The `test_first_person_camera_single_formula.cpp` check — empty, as predicted

```
$ grep -n 'CameraMode::FirstPerson\|camera_->setPosition(\|camera_->setTarget(\|kFirstPerson\|firstPersonM\|firstPersonV\|computeFirstPersonCamera' src/client/graphics/renderer_frame.cpp
(no output, exit 1)
```

`renderer_frame.cpp` matches **none** of the first-person tokens, so it is
invisible to both parts of that recursive sweep, and the two new TUs — which
carry only content from this file — are too. The sweep runs over
`src/client/graphics` recursively and did pick up both new files; **it passed**
in the full suite. Neither new filename collides with `kHelperTU`
(`renderer_camera.cpp`) or `kFloorSecondReader`
(`renderer_player_physics.cpp`, J33's file).

### Deviations

**One, and it is a plan-content conflict rather than a departure:** the file
headers are 7 / 17 / 17 lines, not the "five to eight" §File-header comments
suggests, because the warning text that same section **requires** does not fit in
eight lines. Content requirement honoured over length suggestion; see §9.

**No other deviation.** Nothing was decomposed, no include block trimmed, no dead
content removed, no comment other than the three new headers changed, no file
outside territory touched.
