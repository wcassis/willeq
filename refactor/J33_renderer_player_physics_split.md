# J33 — Batch J Wave 6: split `src/client/graphics/renderer_player_physics.cpp` (1,546 → 2 TUs, largest ~827)

**Slot: assigned at dispatch. Branch `task/J33-renderer-player-physics-split` cut from `fable`.
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
`renderer_player_physics.cpp` is **T312 territory** and must not be boarded until
T312 merges. **T312 merged at `c9b02f7e` and is an ancestor of `fable`'s tip
`824adb28`** — verified with `git merge-base --is-ancestor c9b02f7e HEAD`, not
read off a note. There is no live hold on this file.

## ⚠ THE ONE THING THIS UNIT MUST NOT GET WRONG: the file's own NAME is an assertion

**`tests/test_first_person_camera_single_formula.cpp` names
`"renderer_player_physics.cpp"` as a STRING LITERAL** — `kFloorSecondReader`,
at that test's `:145` region — and allows it, and **only** it besides
`renderer_camera.cpp`, to read `firstPersonMinLookPitchDegrees`.

**`firstPersonMinLookPitchDegrees` is read exactly once in this file, at line
135, inside `updatePlayerMovement` (19-824).**

⚠ **Therefore `updatePlayerMovement` MUST STAY IN `renderer_player_physics.cpp`.**
If it moved into a differently-named TU, the sweep would find the tunable read
from a file that is neither `renderer_camera.cpp` nor `kFloorSecondReader`, and
`TunablesAreReferencedFromOneTUOnly` would fail with
*"only renderer_camera.cpp and renderer_player_physics.cpp may apply the
down-pitch floor"*. **This plan's single cut is chosen to satisfy that
constraint** — the 806-line method stays put and the collision helpers move out.

⚠ **This guard names no file that either standard grep would surface**: it is a
recursive directory sweep plus a filename comparison. `grep -rn
'renderer_player_physics' tests/ scripts/` **does** find it (the string literal
is the filename), but `grep -hn 'kRoot / "src…' tests/*.cpp` does **not**.
**Both greps, every unit, every time.**

**Also confirm, and paste:** this file mentions `CameraMode::FirstPerson` (at 134
and 731) but **never writes `camera_->setPosition(` or `camera_->setTarget(`** —
so it is not a Part 1 offender today, and neither new TU may become one.

## Measured at `824adb28` (2026-09-02, `fable` tip after J13 merged)

`src/client/graphics/renderer_player_physics.cpp` — **1,546 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **1,546** |
| preamble | **1-15** — twelve `#include` lines, then `namespace EQT {` / `namespace Graphics {` at 14-15. **Zero non-boilerplate lines.** |
| namespace | `EQT` / `Graphics` opened at **14-15**, closed at **1545-1546**. **Both closes replicate into the new TU.** |
| method definitions | **18** |
| largest method | **806** `IrrlichtRenderer::updatePlayerMovement` (19-824) — **52% of the file**; 2nd **142** `findGroundZBspFiltered` (1351-1492) |
| `// ====` banners | **zero** `// ====` banners. There is one `// --- Player Mode Movement Implementation ---` rule at 17. |
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
L=open('src/client/graphics/renderer_player_physics.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {826}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-15 | preamble: twelve includes + the two namespace opens | copied to the new TU | 15 |
| **16-825** | the `// --- Player Mode Movement Implementation ---` rule (17) + `updatePlayerMovement` (**19-824, 806 lines, moved NOWHERE — it stays**), which is the sole reader of `firstPersonMinLookPitchDegrees` (135) and mentions `CameraMode::FirstPerson` (134, 731) | **stays** `renderer_player_physics.cpp` | **827** |
| **826-1546** | `checkMovementCollision` (826-873) / `findGroundZ` (875-964) / `setupMinimalZoneCollision` (968-1029) / `addDoorToCollision` (1050-1099) / `addObjectToCollision` (1101-1120) / `extractWorldTriangles` (1148-1166) / `rayIntersectTriangles` (1168-1210) / `rayTestDoorsAndObjects` (1212-1255) / `checkCollisionBspFiltered` (1257-1349) / `findGroundZBspFiltered` (1351-1492) / `drawCollisionDebugLines` (1508-1537) + the two namespace closes | `renderer_player_collision.cpp` | **736** |

**Result: 2 TUs** (`renderer_player_physics.cpp` stays and counts). Largest **~827**.

Arithmetic that must hold: **`825 + 721 = 1546`; `renderer_player_physics.cpp` becomes `825 + 2 replicated closes = 827` and the new TU is `15 + 721 = 736`, plus each file's header comment**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundary falls where it does: **line 826 is the only cut this unit may
take.** `updatePlayerMovement` runs 19-824 and must stay (see the box above), and
everything after it is the collision/raycast/ground-height family — a coherent
cluster that shares no file-local state with the mover. **One cut, two TUs, and
the ceiling is cleared with 673 lines of headroom in the larger file.**

⚠ **`updatePlayerMovement` is 806 lines and is NOT to be decomposed.** §Optional
of the batch plan names it as one of the eight dominant methods.

⚠ **And it does NOT need a Batch-G decision before this Batch-F split** — the
orchestrator checked. The relevant number is **not** "806 of 827 lines is 97% of
its TU"; it is **the TU's distance from the 1,500 ceiling, which is 673 lines.**
A method dominating a small TU is a readability observation, not a ceiling
problem. Contrast the case the batch plan worried about — a method dominating a
TU that is already near 1,500 — which does not arise here and, as J35 documents,
does not arise for `variant_loading.cpp` either. **Do not decompose. Do not open
the question.**

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 826 | `824: }` (end of `updatePlayerMovement`), `825` blank | `826: bool IrrlichtRenderer::checkMovementCollision(float fromX, float fromY, float fromZ,` (a two-line signature continuing on 827) |

⚠ **The cut line is the first of a TWO-LINE signature** (826-827). That is fine —
the rule is about the boundary, not the signature — but **paste 822-830 in your
Review** so it is on the record that the signature was not split.

⚠ **The file's tail has THREE blank lines before the namespace closes** (1542,
1543, 1544, then 1545-1546). They travel with the collision TU. Correction 30's
double-spacing note applies in reverse here: **do not tidy them.**

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/graphics/renderer_player_physics.cpp
```

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `renderer_player_physics.cpp`:**

`renderer_player_physics.cpp` has **zero file-scope `static` symbols, zero
anonymous namespaces, and zero file-scope constants.** Every symbol it defines is
an `IrrlichtRenderer` member with external linkage, declared in
`include/client/graphics/irrlicht_renderer.h`.

**Therefore correction 21 has NO exposure in this file.** In particular
`updatePlayerMovement` calling `checkMovementCollision` / `findGroundZ` across
the cut is an ordinary link — those are class members, not file statics. Confirm
with your own greps and paste them:

```bash
grep -n '^static \|^namespace\s*{\|^const \|^constexpr \|^extern ' \
    src/client/graphics/renderer_player_physics.cpp
grep -n 'namespace' src/client/graphics/renderer_player_physics.cpp
```
(the first: no output, exit 1. The second: exactly four lines — 14, 15, 1545,
1546.)

⚠ **The constraint that DOES bind this unit is not correction 21 but the
filename-keyed test guard** — see the box at the top of this plan. Check that
too, and paste it.

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `renderer_player_physics.cpp`'s preamble, lines 1-15, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

There is no conditional compilation and nothing non-boilerplate in the preamble,
so the copy is unconditional and total: **lines 1-15 verbatim into the new TU.**

**`renderer_player_collision.cpp` carries the original's own namespace closes**
(1545-1546, which are inside its range), and **`renderer_player_physics.cpp` gains
two replicated ones** at its new end:
```
} // namespace Graphics
} // namespace EQT
```

## File-header comments

`renderer_player_physics.cpp` has **no file-header comment block** — it opens
straight into `#include`, and the `// --- Player Mode Movement Implementation ---`
rule at 17 is a section marker that travels with `updatePlayerMovement`. **Both
TUs get a short header** (five to eight lines) citing
`refactor/J33_renderer_player_physics_split.md`.

⚠ **`renderer_player_physics.cpp`'s header must record, in plain words, that
`tests/test_first_person_camera_single_formula.cpp` names THIS FILE as a string
literal (`kFloorSecondReader`) and that `updatePlayerMovement` must not be moved
out of a file with this name** — that sentence is the whole reason the cut falls
where it does, and without it the next unit will move the method and get a test
failure it cannot explain. It must also record that **`updatePlayerMovement` is
806 lines and is a recorded Batch-G candidate J33 deliberately did not touch, with
673 lines of ceiling headroom.**

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
grep -rn 'renderer_player_physics\.cpp' CMakeLists.txt tests/ scripts/
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
| `CMakeLists.txt:483` | `SET(GRAPHICS_SOURCES` (opens at `:439`) | ⚠ **YES — add the new TU here** |

**No `tests/cmake/` fragment names this file** — the first-person sweep reads the
graphics tree as *text* at runtime; it does not compile it. **This unit adds no
target and no source to any test target, so it needs no `tests/cmake/` edit.
Confirm that.** **This unit does NOT touch `gles2_zone_benchmark`.**

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'renderer_player_physics' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

At `824adb28` the first grep returns **comment-only hits and zero functional
readers**, and the second — the exhaustive list of source paths any test opens as
text — **does not contain this file**. **No `readFile` repoint is needed.**

⚠ **Re-run both at your tip and paste them.**

⚠ **BUT the filename guard IS real** — `grep -rn 'renderer_player_physics' tests/
scripts/` finds `tests/test_first_person_camera_single_formula.cpp`'s
`kFloorSecondReader` string literal and its explanatory T300 comment. **No edit
is needed there, because this plan keeps the method in the file the test names.**
**Paste the grep and state explicitly that no edit was required and why.**

⚠ **`tests/test_first_person_camera_single_formula.cpp` sweeps EVERY `.cpp` and
`.h` under `src/client/graphics` RECURSIVELY** (`kGraphicsDir`,
`fs::recursive_directory_iterator`) **and keys two of its three assertions on
FILENAME.** Any TU this unit creates under that tree enters the sweep
automatically — **a third kind of text-scanning guard, one that names no file and
therefore appears in NEITHER standard grep.**

- **Part 1** — a file that both mentions `CameraMode::FirstPerson` **and** writes
  `camera_->setPosition(` / `camera_->setTarget(` is an offender unless its
  filename is `renderer_camera.cpp` (`kHelperTU`).
- **Part 2** — each first-person tunable may be read from **at most one** TU
  under that tree, except `firstPersonMinLookPitchDegrees`, which may be read
  from `renderer_camera.cpp` and **`renderer_player_physics.cpp`, named as a
  string literal (`kFloorSecondReader`)**.

```bash
grep -n 'CameraMode::FirstPerson\|camera_->setPosition(\|camera_->setTarget(\|kFirstPerson\|firstPersonM\|firstPersonV\|computeFirstPersonCamera' \
    src/client/graphics/renderer_player_physics.cpp
```

At `824adb28` this file matches `CameraMode::FirstPerson` (134, 731),
`computeFirstPersonCamera` (125, in a `//` comment — **stripped by the test's own
`stripLineComment`, so it does not count**) and `firstPersonMinLookPitchDegrees`
(135). It matches **neither** `camera_->setPosition(` **nor**
`camera_->setTarget(`, so **Part 1 is satisfied today and both TUs must keep it
that way.** Part 2 is satisfied because 134/135 stay in the file the test names.

⚠ **Run the sweep alone, immediately after the cut, before the full suite:**
```bash
ctest --test-dir build -R FirstPersonCameraSingleFormula --output-on-failure
```

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
  - ⚠ **`FirstPersonCameraSingleFormula` is the test this unit is most likely to
    break.** Run it alone, before the full suite, immediately after the cut. A
    `TunablesAreReferencedFromOneTUOnly` failure naming
    `firstPersonMinLookPitchDegrees` means `updatePlayerMovement` ended up in the
    wrong file — **it is not a behaviour change.**
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
- ⚠ **Expected verifier result for J33: **EXIT 0, `+0/-0` substantive.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied preamble (1-15) is **100% boilerplate**: twelve `#include`, two
    `namespace … {`, and a blank the normalizer drops. **No `extern`, no
    `typedef`, no file-scope definition** — contrast J30 (6 lines) and J31 (2).
  - The two replicated namespace closes are bare `}` with a `//` tail —
    boilerplate.
  - Header-comment lines are `//` — boilerplate.
  - Only ONE new TU, no internal header, no test repoint, no preamble deviation.
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
  cat <the 2 TU bodies, each stripped of its preamble and namespace close> \
      | sort > /tmp/after.txt
  git show "$BASE:src/client/graphics/renderer_player_physics.cpp" | sed -n '16,1544p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Strip the replicated namespace closes from `renderer_player_physics.cpp`
  before comparing.** The original's own closes (1545-1546) are inside range
  826-1546, so the `before` side must run to **1544**, not 1546. Getting this off
  by two lines produces a diff that reads exactly like a dropped range
  (correction 20: suspect the proof first).
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`16-825, 826-1544`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/graphics/renderer_player_physics.cpp" | sed -n '16,1544p'`.
  **1,529 lines. Expect `cmp` exit 0.**
  ⚠ **Declare count variables `local` in your proof helpers.** More generally
  (correction 20): **when a proof fails, suspect the proof first.** J05's
  non-`local` `n` produced a 247-line shortfall that read exactly like a dropped
  range; J09's one proof failure was range arithmetic against the produced
  files' numbering; J08's failed **exactly at its relocation seam**. **Three
  units, three proof bugs, zero real defects.**
  ⚠ **Additionally and separately, prove `updatePlayerMovement` survived byte for
  byte** — 806 lines, 52% of the file, and the method the whole cut is arranged
  around:
  ```bash
  diff <(git show "$BASE:src/client/graphics/renderer_player_physics.cpp" | sed -n '19,824p') \
       <(sed -n '19,824p' src/client/graphics/renderer_player_physics.cpp)
  ```
  ⚠ Its line numbers change only by the header comment you add above it, so
  **derive the second range from your own file**, do not assume 19-824.
  **806 lines, exit 0. Paste it.**
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
  file**: `src/client/graphics/renderer_player_physics.cpp`, `src/client/graphics/renderer_player_collision.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **0**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **1,546** this plan measured,
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
   (this plan predicts 1 / 17 of the 18), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. ⚠ **The first-person guard, in full**: the grep showing which tokens this
    file matches, the `ctest -R FirstPersonCameraSingleFormula` result, and an
    explicit statement that **`updatePlayerMovement` stayed in
    `renderer_player_physics.cpp`** and that **no test file was edited.**
12. **Your own assessment, in one paragraph, of whether the surviving 827-line TU
    with a 806-line method in it is acceptable** — the orchestrator's position is
    that it is (673 lines of ceiling headroom; a dominant method is a readability
    observation, not a ceiling problem) and that no Batch-G decision is owed here.
    **If you disagree, say so as a FINDING; do not act on it.**
13. **Confirmation that T312 is an ancestor of your base**, by
    `git merge-base --is-ancestor`, not by reading a note.

## Territory

`src/client/graphics/renderer_player_physics.cpp`, the one new TU beside it
(`src/client/graphics/renderer_player_collision.cpp`), `CMakeLists.txt` (the one
`GRAPHICS_SOURCES` site), and this plan file. **Nothing else** — **not
`tests/test_first_person_camera_single_formula.cpp`** (this plan is arranged so
that no edit is needed there; if you find yourself wanting to edit it, **STOP**),
not `include/client/graphics/irrlicht_renderer.h` (**J17's**), not
`src/client/graphics/renderer_camera.cpp`, not
`include/client/graphics/first_person_camera.h`, not any other `renderer_*.cpp`,
not `tests/cmake/*.cmake`, not `docs/`.

⚠ **SERIALISATION with J31** (`renderer_frame.cpp`): disjoint files, but both
insert into `SET(GRAPHICS_SOURCES`. **Merge one at a time and rebase.**

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
- `firstPersonMinLookPitchDegrees` is read anywhere in this file OUTSIDE
  `updatePlayerMovement` at your tip.
- You find yourself needing to edit
  `tests/test_first_person_camera_single_formula.cpp`.

## Review (executor, slot3, 2026-09-02)

**Branch `task/J33-player-physics-split`, base `0c95f7df4cf59481d6be267f181052c026a49a0f`**
(`fable` tip at checkout; the plan measured at `824adb28`).
`git diff --stat 824adb28 HEAD -- src/client/graphics/renderer_player_physics.cpp`
is **empty** — the file did not drift between the plan's survey and this tip.

**13. T312 ancestry (measured, not read):**
`git merge-base --is-ancestor c9b02f7e HEAD` → **exit 0.** T312 is an ancestor of
the base. No live hold on this file.

### 1. Line count at my tip

**1,546 lines — exactly the plan's number. Zero range drift.** Every cut boundary
in §Every cut boundary matched at my tip without offset.

### 2. §Measured re-verified (own greps, pasted)

```
$ grep -n '^[A-Za-z_].*IrrlichtRenderer::' src/client/graphics/renderer_player_physics.cpp
19, 826, 875, 968, 1031, 1038, 1050, 1101, 1122, 1134, 1148, 1168, 1212, 1257,
1351, 1496, 1508, 1539            -> 18 method definitions ✅

$ grep -n 'namespace' src/client/graphics/renderer_player_physics.cpp
14:namespace EQT {
15:namespace Graphics {
1545:} // namespace Graphics
1546:} // namespace EQT                      -> exactly four ✅

$ grep -n '/\*' src/client/graphics/renderer_player_physics.cpp
(no output, exit 1)                          -> zero comment regions ✅

$ grep -cE '^// ={4,}' src/client/graphics/renderer_player_physics.cpp
0                                            -> zero banners ✅ (correction 28's -E used)
```

Preamble 1-15 confirmed: 12 `#include`, blank at 13, `namespace EQT {` /
`namespace Graphics {` at 14-15. Zero non-boilerplate lines.

### 3. Correction 21, re-derived (not cited)

```
$ grep -n '^static \|^namespace\s*{\|^const \|^constexpr \|^extern ' \
      src/client/graphics/renderer_player_physics.cpp
(no output, exit 1)
```

**Zero file-scope `static` symbols, zero anonymous namespaces, zero file-scope
constants.** Every one of the 18 definitions is an `IrrlichtRenderer::` member
with external linkage declared in `include/client/graphics/irrlicht_renderer.h`,
so there is no file-local symbol whose consumer could land in the other range.
`updatePlayerMovement` (stays range) calling `checkMovementCollision` /
`findGroundZ` / `findGroundZBspFiltered` (moved range) is an ordinary
cross-TU member call resolved by the linker. **Correction 21 has no exposure
here, confirmed by measurement, and the build proves it.**

### 4. Comment depth at the cut (computed)

```
$ python3 <the //-stripping counter>
depth after line 825 = 0
depth after line 826 = 0
```

Depth 0 on both sides of the only cut, as expected from zero `/*` in the file.

Cut context (822-830), pasted so it is on record that the two-line signature at
826-827 was not split:

```
822:        }
823:    }
824: }
825:
826: bool IrrlichtRenderer::checkMovementCollision(float fromX, float fromY, float fromZ,
827:                                                float toX, float toY, float toZ) {
828:     if (!collisionMap_) {
829:         return true;  // No collision map = allow movement
830:     }
```

### 5. Preprocessor dump

```
$ awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
      src/client/graphics/renderer_player_physics.cpp
(no output)
```

**Zero conditional preprocessor directives in the whole file.** The cut cannot
land inside a conditional region.

### 6. File-header comment lines written (enumerated)

`renderer_player_physics.cpp`, 12 lines prepended (all `//`):

```
1  // renderer_player_physics.cpp -- IrrlichtRenderer player-mode movement.
2  // Split from the original 1,546-line TU by refactor/J33_renderer_player_physics_split.md
3  // (move-only); the collision/raycast/ground-height family moved to
4  // renderer_player_collision.cpp.
5  // DO NOT RENAME THIS FILE, and DO NOT MOVE updatePlayerMovement() OUT OF IT:
6  // tests/test_first_person_camera_single_formula.cpp names "renderer_player_physics.cpp"
7  // as a string literal and allows only this file and renderer_camera.cpp to read
8  // the first-person down-pitch floor accessor (deliberately not spelled out here:
9  // correction 40 -- a comment naming a scanned token can weaken the guard that
10 // scans for it). A rename or a move fails that test's tunable-reader assertion.
11 // updatePlayerMovement() is 806 lines -- a recorded Batch-G decomposition candidate
12 // that J33 deliberately did not touch; this TU has 673 lines of ceiling headroom.
```

⚠ **CORRECTION 40 APPLIED, and it landed on this unit mid-flight.** `a15c47ea`
merged onto `fable` while J33 was in its verification tail, carrying corrections
40 and 41. The header's first draft quoted the tunable's identifier verbatim, as
§File-header comments literally prescribes. **In this specific guard that is
harmless** — Part 2 calls `stripLineComment(line)` before searching, so a `//`
mention cannot count as a reader, and the four-test run was green both before
and after the rewording — **but correction 40's rule is general and I complied
rather than argue the special case.** The identifier is now described ("the
first-person down-pitch floor accessor") with an inline note saying why it is
not spelled out. The filename string IS still quoted, deliberately: the test
compares `path.filename().string()` against it, so it is not a text search and a
comment cannot satisfy it. **Cost: +1 header line (11 → 12), physics TU 838 →
839.** All three proofs and the guard were re-run after the edit; all clean.

**No other test scans `src/client/graphics` as text** —
`grep -rn 'kGraphicsDir\|graphicsSources' tests/*.cpp` returns hits in
`test_first_person_camera_single_formula.cpp` only — so this is the whole
exposure.

`renderer_player_collision.cpp`, 6 lines (all `//`):

```
1 // renderer_player_collision.cpp -- IrrlichtRenderer movement collision, raycasts,
2 // ground-height queries and collision debug lines.
3 // Split from renderer_player_physics.cpp by
4 // refactor/J33_renderer_player_physics_split.md (move-only, verbatim relocation).
5 // updatePlayerMovement() stays in renderer_player_physics.cpp: a filename-keyed
6 // guard in tests/test_first_person_camera_single_formula.cpp requires it.
```

**No other comment in either file was changed.**

### 7. CMake add-sites and the tests/scripts greps

```
$ grep -rn 'renderer_player_physics\.cpp' CMakeLists.txt tests/ scripts/
CMakeLists.txt:483:        src/client/graphics/renderer_player_physics.cpp
tests/test_bridge_queue_race.cpp:149            (comment)
tests/test_entity_collision_z_offset.cpp:9      (comment)
tests/test_first_person_camera_single_formula.cpp:7,101,142,235,557  (5 hits: 4 comments + the kFloorSecondReader string literal at :147)
tests/test_first_person_body_draw_rule.cpp:291  (comment)
```

**ONE CMake add-site, exactly as the plan predicted** — `CMakeLists.txt:483` in
`SET(GRAPHICS_SOURCES` — and the new TU was inserted immediately after it. **No
`tests/cmake/` fragment names this file; no test target changed; confirmed.**
`gles2_zone_benchmark` untouched.

Second grep (the exhaustive list of source paths any test opens as text):

```
$ grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
144: include/client/eq.h
146: src/client/graphics/renderer_event_receiver.cpp
151: include/client/state/<h>
156: src/client/graphics/renderer_frame.cpp
166: include/client/events/renderer_intents.h
221: src/client/eq_bridge_intents.cpp
259: src/client/eq_bridge_intents.cpp
271: include/client/state/event_bus.h
333: include/client/action/action_dispatcher.h
357: src/client/action/action_dispatcher.cpp
431: include/client/eq.h
49:  const fs::path kGraphicsDir = kRoot / "src" / "client" / "graphics";
652: src/client/help/renderer_command_catalog.cpp
690: src/client/graphics/renderer_event_receiver.cpp
698: src/client/graphics/renderer_frame.cpp
706: src/client/graphics/renderer_frame.cpp
```

**`renderer_player_physics.cpp` does not appear as a `readFile` target — no
repoint needed.** The `kGraphicsDir` line at `:49` is the recursive-sweep root
that makes the third, filename-keyed guard invisible to both greps; it is
handled in item 11.

### 8. Both content proofs, and the verifier

`BASE=0c95f7df4cf59481d6be267f181052c026a49a0f`

**Byte-identical re-concatenation** — physics body (lines 28-837 of the new
file = original 16-825, 810 lines) + collision body (lines 22-740 = original
826-1544, 719 lines) = **1,529 lines**, against
`git show "$BASE:...renderer_player_physics.cpp" | sed -n '16,1544p'`:

```
cmp before_body.txt after_concat.txt   -> CMP_EXIT=0
```

**Line-multiset check** on the same two sides, sorted:

```
diff before_sorted.txt after_sorted.txt -> MULTISET_EXIT=0   (MULTISET-OK)
```

**`updatePlayerMovement` byte-for-byte** (ranges derived from each file's own
numbering: original 19-824 vs new 31-836, 806 lines each):

```
diff <(git show "$BASE:..." | sed -n '19,824p') <(sed -n '30,835p' <new>)
-> exit 0, no output
```
(`sed -n '31p;836p'` on the new file prints
`void IrrlichtRenderer::updatePlayerMovement(float deltaTime) {` and `}`.)

**Verifier — committed first, then run with `--repo` and the fork point:**

```
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" \
      --repo /mnt/projects/fable/wt/slot3
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
  refactor/J33_renderer_player_physics_split.md

Files verified: 2
Boilerplate: +33 / -0 lines (allowed)

PASS: move-only (modulo boilerplate).
VERIFIER_EXIT=0
```

**Exit 0, `+0/-0` substantive — the prediction held exactly, and no
orchestrator sign-off is owed.** The 33 allowlisted boilerplate lines are, in
full: the **17 header-comment lines** enumerated in item 6 (12 + 6, all `//`),
the collision TU's copy of the preamble — **12 `#include` + 2 `namespace … {`**
= 14 distinct lines, the blank being dropped by the normalizer — and the **2
replicated namespace closes** (`} // namespace Graphics`, `} // namespace EQT`),
which are bare `}` with a `//` tail. 18 + 14 + 2 = **34** at the amended header; the pasted run above was taken at the 11-line draft (17 + 14 + 2 = **33**) and the extra line is one more `//`. Re-run after the reword: identical PASS, `+34 / -0`. Nothing outside that
set was flagged. The commit was made **before** the verifier ran, `--repo`
pointed at slot3, and `$BASE` is the fork point, so none of the three
false-answer traps applies.

### 9. Final line counts, method counts, timing

| file | lines | methods | derivation |
|---|---|---|---|
| `renderer_player_physics.cpp` | **839** | **1** | 12 header + 825 (orig 1-825) + 2 replicated closes |
| `renderer_player_collision.cpp` | **742** | **17** | 6 header + 15 preamble + 721 (orig 826-1546, incl. the original's own closes) |

**Method split 1 / 17 of 18 — exactly as predicted.** The plan's body figures
(827 and 736) were **exactly right**; the deltas (+12, +6) are precisely the
header-comment lines this plan itself prescribes, as correction 19 requires.
`check_file_sizes.sh --files` on both: **2 ok, 0 warn, 0 fail.**

Wall clock ≈ 2 h 15 m. **Slowest step by a wide margin: `build-gles2-x86.sh`,
which hit the 600 s tool ceiling TWICE (exit 143) before completing on the third
(detached) invocation.** `cmake --build build -j24` also hit 143 once. Wave
concurrency stretched the verification tail heavily; the cut plus CMake plus all
three proofs took **under 5 minutes** in total.

⚠ **Truncation trap fired again (5th unit in the batch).** The first `cmake
--build` exit-143 left **five zero-byte `build/bin/test_*`** files that
`readelf -h` rejected (`test_eq_commands_equip`, `test_hunt_target_body_type`,
`test_spell_body_type_targeting`, `test_spell_manager`, `test_train_skill`) —
all zero-length, i.e. gmake had *started* deleting them. 261 recently-touched
`.o` files all scanned clean. Deleted the five, rebuilt once, exit 0, second scan
clean. Both `build-gles2-x86.sh` 143s were killed *links* that gmake deleted
cleanly (correction 32's "absent" case): `lib/libwilleq_core.a` and
`bin/gles2_zone_benchmark`; both scans found nothing bad.

### Acceptance results

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** (after one exit-143 + truncation cleanup) |
| ELF scan `build/bin/*` (221 files) | clean |
| `ctest -R FirstPersonCameraSingleFormula` | **4/4 PASS**, run alone immediately after the cut |
| `ctest -j6` full suite | **3506/3507**, 138.1 s. Sole failure `AudioManagerTest.PreloadCommonSounds` — the documented ALSA host quirk in `refactor/test_baseline.txt`. **Zero new failures.** No `HeadlessShutdownTest` flake this run. |
| visual gate (in-suite test #3504) | **Passed**, 34.18 s |
| `scripts/ci/visual_gate.sh` (standalone) | **=== VISUAL GATE: PASS ===**, all three zones, **zero image mismatches**; p95 CPU qeynos2 5.293/10.108, gfaydark 5.934/10.672, befallen 4.856/10.948 |
| goldens | **untouched** — `git status --porcelain` after the gate shows only the two source files + the new TU |

⚠ **Correction 41 (which also merged mid-flight): the standalone gate here is
NOT vacuous.** The order run was `build-gles2-x86.sh` (exit 0) **then**
`scripts/ci/visual_gate.sh`. `build-gles2/bin/gles2_zone_benchmark` and
`build-gles2/bin/willeq` are both stamped **12:44**, after every source edit in
this unit (~11:55) and before the gate run. The in-suite `visual_gate` test
(#3504) that ran during `ctest -j6` **was** against the older tree and should be
discounted; the standalone re-run is the one that counts.
| `scripts/build-gles2-x86.sh` | **exit 0** (third invocation; two 600 s tool-ceiling kills) |
| `scripts/build-arm-noble.sh` | **exit 0**, first invocation, ~7 min; ARM ELF scan clean |
| `check_file_sizes.sh --files` (both files) | **2 ok, 0 warn, 0 fail** (839 / 742) |

### 10. Findings only, not fixes

- **`docs/module_map.md` needs a row for `src/client/graphics/renderer_player_collision.cpp`**
  (17 collision/raycast/ground-height members) and its `renderer_player_physics.cpp`
  row narrowed to `updatePlayerMovement` alone. **Recorded, not acted on.**
- Four `tests/` comments mention `renderer_player_physics.cpp`
  (`test_bridge_queue_race.cpp:149`, `test_entity_collision_z_offset.cpp:9`,
  `test_first_person_body_draw_rule.cpp:291`, and the five in
  `test_first_person_camera_single_formula.cpp`). **All remain accurate after
  this split** — every one refers to `updatePlayerMovement` or to the pitch
  accumulator, both of which stayed. Per correction 24, no stale citation is
  attributed to this unit.
- **Cosmetic, declared not fixed (correction 30, in reverse):**
  `renderer_player_collision.cpp` has **no blank line between
  `namespace Graphics {` and its first method** — original line 16 (blank) and
  17 (the `// --- Player Mode Movement Implementation ---` rule) belong to the
  *stays* range, so the collision TU's body starts at original 826. Adding a
  blank would have broken the multiset proof. **Left verbatim.** Conversely
  `renderer_player_physics.cpp`'s tail is `}` / blank / two closes, which is the
  normal shape, not a double blank. The original's three trailing blanks
  (1542-1544) travelled with the collision TU untidied, as instructed.
- **`kFirstPersonMaxUpPitchDegrees` is read at lines 118-119** (inside
  `updatePlayerMovement`), which §Measured's token list did not mention. It is
  **not** in the test's tunable list, so it constrains nothing — but a future
  plan for this file should list it, because it is a first-person tunable that
  the guard does *not* currently cover.

### 11. The first-person guard, in full

```
$ grep -n 'CameraMode::FirstPerson\|camera_->setPosition(\|camera_->setTarget(\|kFirstPerson\|firstPersonM\|firstPersonV\|computeFirstPersonCamera' \
      src/client/graphics/renderer_player_physics.cpp     # at BASE
118:        if (playerPitch_ > kFirstPersonMaxUpPitchDegrees) playerPitch_ = kFirstPersonMaxUpPitchDegrees;
119:        if (playerPitch_ < -kFirstPersonMaxUpPitchDegrees) playerPitch_ = -kFirstPersonMaxUpPitchDegrees;
125:        // is consumed. computeFirstPersonCamera() clamps the total pitch again
134:        if (cameraMode_ == CameraMode::FirstPerson) {
135:            const float minLook = firstPersonMinLookPitchDegrees();
731:    if (cameraMode_ == CameraMode::FirstPerson && (positionChanged || headingChanged || pitchChanged)) {
```

**Every one of these six lines is inside `updatePlayerMovement` (19-824) and
therefore stayed in `renderer_player_physics.cpp`.** The same grep against
`renderer_player_collision.cpp` returns **no output, exit 1** — the new TU
matches none of the tokens, so it is invisible to both Part 1 and Part 2 of the
sweep even though `fs::recursive_directory_iterator` does enumerate it.
`camera_->setPosition(` / `camera_->setTarget(` appear in **neither** file, so
Part 1 stays satisfied.

I verified the constraint myself before cutting rather than taking the plan's
word: `tests/test_first_person_camera_single_formula.cpp:147` declares
`const std::string kFloorSecondReader = "renderer_player_physics.cpp";` and the
`TunablesAreReferencedFromOneTUOnly` loop compares `path.filename().string()`
against it for `firstPersonMinLookPitchDegrees` only. **My reading matches the
plan's exactly.** Had `updatePlayerMovement` moved, `files` would have contained
a third filename and the `EXPECT_TRUE(f == kHelperTU || f == kFloorSecondReader)`
would have failed.

```
$ ctest --test-dir build -R FirstPersonCameraSingleFormula --output-on-failure
900 OnlyTheHelperTUDrivesTheCameraInFirstPerson ......... Passed 1.66 sec
901 TunablesAreReferencedFromOneTUOnly .................. Passed 0.92 sec
902 TheOneBodyAppliesBothTunablesAndMouseLookPitch ...... Passed 0.01 sec
903 DeadFirstPersonHelpersStayDeleted ................... Passed 1.99 sec
100% tests passed, 0 tests failed out of 4
```

**`updatePlayerMovement` stayed in `renderer_player_physics.cpp`. No test file
was edited.**

### 12. Is the surviving 827-line (838 with header) TU acceptable?

**Yes — I agree with the orchestrator, and I would not have decomposed it even
if the plan had left the question open.** The ceiling is a context-budget rule,
and the file now costs an agent roughly half what it did; 673 lines of headroom
is not a file under pressure. The objection worth stating is a different one and
it is not a Batch-J objection: `updatePlayerMovement` is 806 lines of
input-sampling, collision probing, pitch clamping, camera-mode branching and
position publishing in one body, and the *reason* it is hard to change is its
internal structure, not the size of the file around it. Decomposing it would be
a behaviour-risk change to the single hottest player-facing code path in the
client — the one the visual gate and the first-person guards exist to protect —
and doing it inside a commit whose contract is verbatim relocation would destroy
the only cheap proof available (byte-identical re-concatenation). **Record it as
a Batch-G candidate with the guard note attached; do not fold it into a split.**
