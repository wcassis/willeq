# J23 — Batch J Wave 6: split `src/client/eq_movement.cpp` (2,139 → 3 TUs, largest ~908)

**Slot: assigned at dispatch. Branch `task/J23-eq-movement-split` cut from `fable`.
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

`src/client/eq_movement.cpp` — **2,139 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **2,139** |
| preamble | **1-81** — includes, the `#ifndef M_PI` pair, and **eight file-scope `const float` constants at 70-80** |
| namespace | **none** — `grep -n "^namespace" src/client/eq_movement.cpp` returns nothing. Each TU ends with the last method's closing brace; there is no namespace close to replicate. |
| method definitions | **43** |
| largest method | ****499** `EverQuest::UpdateMovement` (509-1007); 2nd **229** `SendPositionUpdate` (1010-1238)** |
| `// ====` banners | **zero** `// ====` banners |
| conditional preprocessor directives | 10 — 43/45 `EQT_HAS_NAVMESH`, 53/55 `WITH_AUDIO`, 66/68 `#ifndef M_PI` (all preamble); **1341/1349** `#if defined(EQT_HAS_NAVMESH)`; **1898/1903** `WITH_AUDIO` |
| file-scope `static` / anonymous namespaces | **eight `const float` at 70-80** — see §Correction 21. Zero anonymous namespaces, zero `static` functions. |
| multi-line comment regions | **zero.** ⚠ But `:18` is a `//` comment containing the token `*/` **twice** — a naive depth counter goes to -2 and every cut looks illegal. Strip `//` tails. |

⚠ **Correction 33: "no block comments" must mean "no MULTI-LINE comment
regions".** Single-line `/* … */` argument comments are irrelevant; what the
cutting rule needs is **comment depth 0 at every cut**. Compute it, do not
assert it, and beware the trap `eq_movement.cpp:18` set for the orchestrator's
own scanner: a `//` line containing the token `*/` makes a naive depth counter
go negative. Strip `//` tails before counting:

```bash
python3 - <<'EOF'
import re
L=open('src/client/eq_movement.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {509,1304}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-69, 81 | preamble: includes + `#ifndef M_PI`. **Lines 70-80 leave the preamble entirely** — see §Correction 21 | copied to each TU, with 70-80 replaced by one `#include` | 71 |
| **82-508** | the T88 FRAME CONTRACT comment (82-96) + `Move` (97-126) / `MoveTo*` / `MoveToEntityWithinRange` (128-201) / `StopMovement` / `UpdateCombatMovement` (305-428) and the rest of the command-side movement entry points — the leading comment travels with `Move` | **stays** `eq_movement.cpp` | **498** |
| **509-1303** | `UpdateMovement` (509-1007, **499 lines, moved WHOLE and VERBATIM**) + `SendPositionUpdate` (1010-1238) + the position-arrival helpers through 1303 — the per-tick movement pump | `eq_movement_update.cpp` | **866** |
| **1304-2139** | the `// Pathfinding implementation` comment (1304) + `LoadPathfinder` (1305-1360) / `FindPath` (1362-1428) — **with the `#if defined(EQT_HAS_NAVMESH)` pair at 1341/1349 wholly inside `LoadPathfinder`** — then `SetPositionState` (1537-1592) / `SendMovementHistory` (1612-1686) / `Jump` (1688-1729) / `UpdateJump` (1731-1776) / `UpdateWaterState` (1778-1884, **with the `WITH_AUDIO` pair at 1898/1903 just after it**) / `UpdateKeyboardMovement` (2081-2139) | `eq_movement_state.cpp` | **908** |

**Result: 3 TUs** (`eq_movement.cpp` stays and counts). Largest **~908**.

Arithmetic that must hold: **`81 + 427 + 795 + 836 = 2139`, and after the constants move to the header each TU is `71 + range + its file-header comment`**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundaries fall where they do: **82-508** is the *commanded* movement
API (Move / MoveTo / follow / combat approach) that other TUs and the command
layer call into. **509-1303** is the per-tick pump — `UpdateMovement` and the
position update it emits, which is one causal chain. **1304-2139** is
everything movement *depends on or derives*: pathfinding, position state,
movement history, jump, water, and the keyboard path.

⚠ **`UpdateMovement` is 499 lines and is NOT to be decomposed.** §Optional of
the batch plan is explicit. It lands in an 866-line TU with 634 lines of
headroom — no Batch-G decision is needed here, and taking one would put this
unit out of scope. Precedent: J02 gave `DispatchZonePacket` (880 lines) its own
~965-line TU and flagged it; J13 did the same with `ProcessBridgeIntents` (580).

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 509 | `507: }` (end of `UpdateCombatMovement`'s successor), `508` blank | `509: void EverQuest::UpdateMovement()` |
| 1304 | `1302: }`, `1303` blank | `1304: // Pathfinding implementation` (the leading comment for `LoadPathfinder` — it travels with the method) |

⚠ **This file separates methods with TWO blank lines, not one** (e.g. 1007 `}`,
1008 blank, 1009 blank, 1010 the next signature). The rule still decides both
cuts unambiguously: the trailing blank(s) stay with the preceding range and the
new range starts at the first non-blank line. **Say which convention you used.**

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/eq_movement.cpp
```

## ⚠ HAZARD 1 — the eight movement constants cannot stay in a copied preamble

`DEFAULT_RUN_SPEED`, `DEFAULT_WALK_SPEED`, `WALK_SPEED_THRESHOLD`,
`POSITION_UPDATE_INTERVAL_MS`, `FOLLOW_CLOSE_DISTANCE`, `FOLLOW_FAR_DISTANCE`,
`FOLLOW_MIN_SPEED_MULT`, `FOLLOW_MAX_SPEED_MULT` are `const float` at namespace
scope in a file with **no namespace** — **internal linkage**, so they are not
link-visible from a second TU. Their consumers straddle **two** of this plan's
three ranges (measured, §Correction 21). **There is no cut that keeps all eight
file-local with all their consumers**: the earliest consumer is at 891, inside
`UpdateMovement`, and the latest is at 2111, so a single TU holding all
consumers would be 1,631 body lines + preamble = **1,727** — over the ceiling.

**Two options. This plan prescribes (a).**

**(a) A sanctioned internal header** — `include/client/eq_movement_internal.h`,
§Process step 4, precedent `src/client/graphics/eq/wld_internal.h` (J05) and
`src/client/action/command_processor_internal.h` (J04). **Exactly 17 lines:**

```
 1  // eq_movement_internal.h -- the movement/following constants shared by the
 2  // three eq_movement*.cpp TUs. Batch-J sanctioned internal header (J23,
 3  // refactor/J_file_size_reclamation.md Process step 4). These are `const float`
 4  // at namespace scope: internal linkage, one copy per including TU, no ODR
 5  // issue and no link-visible symbol. Do not add to this header "for symmetry"
 6  // -- correction 10 requires each symbol be measured and signed off.
 7  #pragma once
 8  (blank)
 9-19  VERBATIM eq_movement.cpp:70-80  (two comments, eight constants, one blank)
```
— that is **7 written lines + 11 moved lines + 1 blank = 19**. Count it exactly
in your Review; the number above is what this plan prescribes, so if you write a
shorter header, say so and re-derive the multiset arithmetic.

**All three TUs `#include "client/eq_movement_internal.h"`** at the point lines
70-80 occupied. Range **82-508 does not read any of the eight** — including the
header there anyway keeps the three preambles identical and removes a judgement
call; a namespace-scope `const` that nothing reads produces no warning in C++.
**Say that you checked, and paste the grep.**

**(b) Copy the eleven lines into all three preambles.** Legal (internal
linkage), zero new files, and exactly what J10/J11 did with their non-boilerplate
preamble lines — but it creates **three copies of eight tuning constants**, and
this project's dominant defect class is *duplicated-then-drifted* (T286 found 107
duplicate state stores, nine already drifting). **Rejected for that reason.**
Verifier cost is also higher: 8 distinct lines × +2 = **16 added substantive
lines** versus **0** for (a).

**(c) Partition the eight constants by consumer.** The orchestrator's audit shows
each individual constant is read from exactly **one** of the two ranges —
`DEFAULT_RUN_SPEED` / `DEFAULT_WALK_SPEED` / `WALK_SPEED_THRESHOLD` only from
1304-2139, the other five only from 509-1303 — so lines 71-73 could go to one TU
and 74/77-80 to the other, with no header at all and zero verifier lines.
**Rejected**, for two reasons: it splits two comment-grouped blocks
(`// Movement constants` 70-74, `// Following constants` 76-80) across two files
so neither comment describes its contents any more, and it makes the *next*
reader of one constant from the other TU a silent compile error rather than a
lookup. **(a) is one file, one source of truth, and zero verifier lines too.**

**Correction 18 is a rule, not an option: keep a promoted helper in its EXISTING
namespace.** These constants are at global scope today; the header keeps them at
global scope. **Do not wrap them in a `detail` namespace** — J04 paid 8 exception
lines for 12 call sites by doing that; J05 paid 2 for 114 by not.

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `eq_movement.cpp`:**

`grep -rn` across `src/ include/ tests/` for each of the eight constants returns
**their definition and their readers, all inside `src/client/eq_movement.cpp`,
and nothing else.** Measured readers, mapped onto this plan's ranges:

| symbol | definition | readers | range(s) holding the readers |
|---|---|---|---|
| `DEFAULT_RUN_SPEED` | 71 | 1519 | **1304-2139** |
| `DEFAULT_WALK_SPEED` | 72 | 1522, 1525, 2111 | **1304-2139** |
| `WALK_SPEED_THRESHOLD` | 73 | 1753, 2109 | **1304-2139** |
| `POSITION_UPDATE_INTERVAL_MS` | 74 | 1003 | **509-1303** |
| `FOLLOW_CLOSE_DISTANCE` | 77 | 891, 895 | **509-1303** |
| `FOLLOW_FAR_DISTANCE` | 78 | 889, 895 | **509-1303** |
| `FOLLOW_MIN_SPEED_MULT` | 79 | 892, 896 | **509-1303** |
| `FOLLOW_MAX_SPEED_MULT` | 80 | 890, 896 | **509-1303** |

**Two different ranges consume them, so no assignment of the definitions to a
single range works** — hence the internal header (HAZARD 1). With the header,
every consumer's TU has a declaration in scope by construction.

Everything else this file declares is either an `EverQuest` member (external
linkage, declared in `include/client/eq.h`) or function-local. **Zero anonymous
namespaces, zero `static` functions** — confirm with:

```bash
grep -n '^static \|^namespace' src/client/eq_movement.cpp
```

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `eq_movement.cpp`'s preamble, lines 1-81, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

⚠ **Deviation, enumerated and justified (the correction-20 discipline):
lines 70-80 do NOT travel in the copied preamble.** They move once, into
`include/client/eq_movement_internal.h`, and every TU gets
`#include "client/eq_movement_internal.h"` in their place. That is the whole
deviation; lines 1-69 and 81 are copied verbatim, including the
`EQT_HAS_NAVMESH` and `WITH_AUDIO` groups and the `#ifndef M_PI` pair.

**There is no namespace wrapper.** Each new TU ends with its last method's
closing brace and nothing else. Correction 30's double-blank tail issue does not
arise here — but this file's **two-blank-line** method separator does; declare
which TU ends on one blank and which on two.

## File-header comments

`eq_movement.cpp` has a file-header comment block at **1-30** describing the
whole file's contents (it enumerates the keyboard-movement family at `:18`).
**Each new TU gets its own adapted version** — same shape, naming that TU's
clusters, citing `refactor/J23_eq_movement_split.md`. **The stays file's header
must be updated too**: its current text will be actively wrong once two thirds of
the methods it lists have moved.

`eq_movement_update.cpp`'s header must say in plain words that
**`UpdateMovement` is 499 lines and is a recorded Batch-G decomposition
candidate that J23 deliberately did not touch.**

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
grep -rn 'eq_movement\.cpp' CMakeLists.txt tests/ scripts/
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
| `CMakeLists.txt:297` | `SET(CLIENT_CORE_SOURCES` (opens at `:265`) | ⚠ **YES — add both new TUs here** |

**No `tests/cmake/` fragment names this file.** ⚠ **This unit does NOT touch
`gles2_zone_benchmark`.** The internal header is a header: **CMake needs no entry
for it.**

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'eq_movement' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

At ``824adb28`` the first grep returns **comment-only hits and zero functional
readers**, and the second — the exhaustive list of source paths any test opens as
text — **does not contain `src/client/eq_movement.cpp`**. **No repoint is
needed.** (J11 was the cautionary case: four tests read `irrlicht_renderer.cpp`
as text, three would have failed and one would have **silently asserted
nothing**. J25 and J31, running in this same wave, both have that problem. You
do not.)

⚠ **Re-run both at your tip and paste them.** If a text reader has appeared, add
it to territory and repoint it — that is correction 10, not scope creep.

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
- ⚠ **Expected verifier result for J23: **EXIT 0, `+0/-0` substantive.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied preamble (1-69, 81) is **100% boilerplate**: `#include`,
    `#ifndef`, `#define`, `#endif`, `//`, and blanks the normalizer drops.
  - The eleven constant lines (70-80) are **moved, not added** — they leave
    `eq_movement.cpp` and appear once in `eq_movement_internal.h`, so each
    distinct line's count is 1 before and 1 after. **Net zero.**
  - The header's own 7 written lines are 6 `//` comments + 1 `#pragma once` —
    **all boilerplate.**
  - The three `#include "client/eq_movement_internal.h"` lines are boilerplate.
  - Header-comment adaptations cost zero (`//`).
  - There is no namespace open/close to replicate.
  - **Total: ADDED 0, REMOVED 0 → exit 0.**

  ⚠ If you take HAZARD 1 option (b) instead, the prediction changes to **exit 1
  with 16 added substantive lines** (8 distinct constants × `+2x`). **Say which
  you did and re-derive.**

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
  git show "$BASE:src/client/eq_movement.cpp" | sed -n '82,2139p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **The header's 11 moved lines must be counted** (correction 7): a naive
  "concatenate the three .cpp bodies" **silently under-counts by exactly the
  header's contribution and still looks clean.** Either re-insert lines 70-80 at
  their original offset before comparing (what J04 did), or state the header's
  line contribution explicitly and diff the preamble separately. **Do both sides
  of the comparison over lines 70-2139, not 82-2139, so the constants are inside
  the proof.**
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`82-508, 509-1303, 1304-2139`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/eq_movement.cpp" | sed -n '82,2139p'`.
  **2,058 lines. Expect `cmp` exit 0.**
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
  file**: `src/client/eq_movement.cpp`, `src/client/eq_movement_update.cpp`, `src/client/eq_movement_state.cpp`, `include/client/eq_movement_internal.h` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **0**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **2,139** this plan measured,
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
   (this plan predicts roughly 14 / 6 / 23 of the 43), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. **The internal header's exact line count**, against the **19** this plan
    prescribes, and the enumerated exception list (§Process step 4 requires the
    contents of an internal header be an **individually signed-off list**, not a
    set added "for symmetry" — correction 10).

## Territory

`src/client/eq_movement.cpp`, the two new TUs beside it
(`src/client/eq_movement_update.cpp`, `src/client/eq_movement_state.cpp`), the
new `include/client/eq_movement_internal.h`, `CMakeLists.txt` (the one
`CLIENT_CORE_SOURCES` site), and this plan file. **Nothing else** — not
`include/client/eq.h` (this unit changes no interface), not any other
`eq_*.cpp`, not any `tests/` file, not `tests/cmake/*.cmake`, not `docs/`.

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
- Any of the eight constants has acquired a reader outside
  `src/client/eq_movement.cpp`.

---

## Review (executor, slot3, 2026-09-02)

**Branch `task/J23-eq-movement-split`, cut from `fable` at `2498c3a5`
("Batch J: fold J33's corrections"). `$BASE = 2498c3a5354a7df86d9126e31a3acb787af1664c`
(`git merge-base fable HEAD`, unchanged throughout — no wave merge moved it).**

### 1. Line count at my tip, and range drift

`wc -l src/client/eq_movement.cpp` = **2,139** — exactly what the plan measured
at `824adb28`. **Zero drift.** Every range, cut boundary and preprocessor line
number below reproduced exactly. This is the second Wave-6 unit (after J31/J33)
to find its survey stable, and it holds despite the plan's own warning that
T311 Leg B and T322 had touched this file recently — both landed before
`824adb28`.

### 2. Every property in §Measured, re-verified

| property | plan | measured at `2498c3a5` | |
|---|---|---|---|
| total lines | 2,139 | **2,139** | ✅ |
| preamble | 1-81, constants at 70-80 | **1-81, constants at 70-80** | ✅ |
| namespace | none | `grep -n '^namespace'` returns **nothing** | ✅ |
| method definitions | 43 | **43** (`grep -c '^[A-Za-z_].*EverQuest::'`) | ✅ |
| `// ====` banners | 0 | **0** (`grep -cE '^// ={4,}'`, per correction 28) | ✅ |
| file-scope `static` / anon ns | none | `grep -n '^static \|^namespace'` returns **nothing** | ✅ |
| multi-line comment regions | zero | depth 0 everywhere, final depth 0 | ✅ |

```
$ grep -n '^static \|^namespace' src/client/eq_movement.cpp
(no output)
$ grep -cE '^// ={4,}' src/client/eq_movement.cpp
0
$ grep -c '^[A-Za-z_].*EverQuest::' src/client/eq_movement.cpp
43
```

⚠ **Correction 36 check (out-of-class static member definitions in the
preamble): NONE.** Lines 1-81 are a comment block, `#include`s, the `#ifndef
M_PI` pair, and eleven internal-linkage `const float` lines. Nothing with
external linkage is defined in the copied region, so the verbatim-preamble copy
carries no duplicate-symbol risk.

⚠ **Correction 35 / J33's filename-keyed sweep does NOT apply.**
`tests/test_first_person_camera_single_formula.cpp:49` sweeps
`kRoot / "src" / "client" / "graphics"` recursively. `src/client/eq_movement.cpp`
is not under that subtree, so neither `kHelperTU` nor `kFloorSecondReader`
constrains this unit's filenames. The prescribed first-person token grep also
returns nothing in this file.

### 3. Correction 21, re-derived (not cited)

`grep -rn '\b<sym>\b' src/ include/ tests/ scripts/` for each of the eight
constants. **Every hit is inside `src/client/eq_movement.cpp`; nothing else in
the tree names any of them.** Full output:

```
DEFAULT_RUN_SPEED           71 (def)   1519
DEFAULT_WALK_SPEED          72 (def)   1522, 1525, 2111
WALK_SPEED_THRESHOLD        73 (def)   1753, 2109
POSITION_UPDATE_INTERVAL_MS 74 (def)   1003
FOLLOW_CLOSE_DISTANCE       77 (def)   891, 895
FOLLOW_FAR_DISTANCE         78 (def)   889, 895
FOLLOW_MIN_SPEED_MULT       79 (def)   892, 896
FOLLOW_MAX_SPEED_MULT       80 (def)   890, 896
```

Mapping each reader onto the ranges **this plan assigns**:

| symbol | definition's range | readers | reader range(s) | same range? |
|---|---|---|---|---|
| `DEFAULT_RUN_SPEED` | preamble | 1519 | 1304-2139 | ✗ |
| `DEFAULT_WALK_SPEED` | preamble | 1522, 1525, 2111 | 1304-2139 | ✗ |
| `WALK_SPEED_THRESHOLD` | preamble | 1753, 2109 | 1304-2139 | ✗ |
| `POSITION_UPDATE_INTERVAL_MS` | preamble | 1003 | 509-1303 | ✗ |
| `FOLLOW_CLOSE_DISTANCE` | preamble | 891, 895 | 509-1303 | ✗ |
| `FOLLOW_FAR_DISTANCE` | preamble | 889, 895 | 509-1303 | ✗ |
| `FOLLOW_MIN_SPEED_MULT` | preamble | 892, 896 | 509-1303 | ✗ |
| `FOLLOW_MAX_SPEED_MULT` | preamble | 890, 896 | 509-1303 | ✗ |

**Two distinct ranges consume them — 1304-2139 takes three, 509-1303 takes
five — so no assignment of the eleven definition lines to a single range works.
Confirmed independently: this is J08's defect shape, and HAZARD 1 option (a)
(the internal header) is the resolution that makes every consumer's TU have a
declaration in scope by construction.**

⚠ **Range 82-508 reads none of the eight** — verified: the lowest reader line
in the whole file is **889**, well past 508. It includes the header anyway, per
the plan, so all three preambles are identical and no judgement call is left to
a future reader. A namespace-scope `const` with no reader produces no warning in
C++ and the build confirms it (`-Wall` clean across all four configurations
built below).

**No constant has acquired a reader outside `src/client/eq_movement.cpp`** — the
last STOP condition is not triggered.

### 4. Comment depth at every cut — computed, not asserted

Using the plan's `//`-stripping counter verbatim:

```
depth before line 82   = 0
depth before line 509  = 0
depth before line 1304 = 0
final depth = 0
```

The `:18` trap the plan warns about (a `//` line containing `*/` twice) is
real and the stripping counter handles it; a naive counter goes to -2.

### 5. The preprocessor dump — no cut lands inside a conditional

```
$ awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' src/client/eq_movement.cpp
43: #ifdef EQT_HAS_NAVMESH
45: #endif
53: #ifdef WITH_AUDIO
55: #endif
66: #ifndef M_PI
68: #endif
1341: #if defined(EQT_HAS_NAVMESH)
1349: #endif
1898: #ifdef WITH_AUDIO
1903: #endif
```

**All ten lines reproduce the plan exactly.** Three pairs are preamble (copied
into every TU). The two body pairs — 1341/1349 inside `LoadPathfinder`, and
1898/1903 inside `OnEnterWater` — both fall wholly inside **1304-2139**, so
neither cut (509, 1304) is inside a conditional region.

Post-split, each TU's directives are balanced within itself:

| TU | directive lines |
|---|---|
| `eq_movement.cpp` | 31/33, 41/43, 54/56 (preamble only) |
| `eq_movement_update.cpp` | 31/33, 41/43, 54/56 (preamble only) |
| `eq_movement_state.cpp` | 25/27, 35/37, 48/50 (preamble) + **91/99** (navmesh) + **648/653** (audio) |
| `eq_movement_internal.h` | none |

### 6. Cut boundaries, with neighbours, at my tip

```
cut 509                          cut 1304
504|   std::cout << "Movement…"   1299|   // Check if we're close enough…
505|   }                          1300|   float dist = CalculateDistance2D(…)
506|  }                           1301|   return dist < 2.0f;
507| }                            1302| }
508| (blank)                      1303| (blank)
509| void EverQuest::UpdateMove…  1304| // Pathfinding implementation
                                  1305| void EverQuest::LoadPathfinder(…)
```

Both cuts satisfy J03's rule (correction 11): **a blank line whose predecessor
closes a method, comment depth 0.** The leading comment at 1304 travels with
`LoadPathfinder`, per the surviving half of the banner rule.

**Blank-line convention: the trailing blank stays with the preceding range and
each new range starts at its first non-blank line.** So `eq_movement.cpp`'s body
ends on a blank (old 508), `eq_movement_update.cpp`'s ends on a blank (old
1303), and `eq_movement_state.cpp`'s ends on `}` (old 2139).

⚠ **PLAN ERROR (record it).** §Every cut boundary says "This file separates
methods with TWO blank lines, not one". **Measured: the file has 141 single-blank
runs and exactly ONE double-blank run** (1008/1009, between `UpdateMovement` and
`SendPositionUpdate`). The convention is one blank; the double is a single
outlier, it sits mid-range in `eq_movement_update.cpp`, and it is untouched.
**Neither of my two cuts is at a double blank**, so the ambiguity the plan
raises never arises. **Correction 30's double-spaced-tail artefact did not fire**
(there is no namespace close to replicate).

### 7. CMake add-sites — **ONE**, matching the plan

```
$ find <slot> \( -name CMakeLists.txt -o -name '*.cmake' \) \
      -not -path '*/build*' -not -path '*third_party*' -exec grep -Hni eq_movement {} +
/mnt/projects/fable/wt/slot3/CMakeLists.txt:297:    src/client/eq_movement.cpp
```

`CMakeLists.txt:297`, inside `SET(CLIENT_CORE_SOURCES` (opens at `:265` —
confirmed, and confirmed UPPERCASE per correction 12). Both new TUs added
immediately below it. **No `tests/cmake/` fragment names this file**; searched
all 23 CMake files in the tree with `-i`, so neither correction 26 (never name
`tests/CMakeLists.txt`) nor the uppercase trap could hide a site. The internal
header needs no CMake entry. `gles2_zone_benchmark` is untouched.

⚠ **NEW TRAP, worth folding into the template — `grep -rn --include=` IS
SILENTLY EMPTY ON THIS HOST.** `grep` here is **ugrep 7.8.4**, and
`grep -rn --include=CMakeLists.txt --include=*.cmake -i eq_movement <dir>`
returns **nothing, exit 1** on a tree where the site demonstrably exists — while
`grep -rn -i eq_movement <explicit file list>` finds it and
`find … -exec grep -Hni … {} +` finds it. This is the **fifth** member of the
silently-empty-grep family the batch has catalogued (after uppercase CMake,
`grep -c '{4,}'`, `grep ' error '`, and ugrep's literal `\|`). **A unit that
counted its add-sites with `--include=` would report ZERO sites and look
thorough.** Use `find … -exec grep` or an explicit file list.

### 8. `tests/` and `scripts/` — both greps, run at my tip

```
$ grep -rn 'eq_movement' tests/ scripts/
tests/test_setheading_conversion.cpp:8            // …eq_movement.cpp's
tests/test_eq_action_handler_enum_guards.cpp:20   // …eq_movement.cpp's no-op guard).
tests/test_eq_action_handler_transforms_real.cpp:603  // both setters (eq_movement.cpp);
tests/test_position_frame_roundtrip.cpp:12,30,63,206,383  // (5 comment hits)
tests/test_zone_position_convention.cpp:295       // (eq_movement.cpp) is gated on…
```
**Nine hits, ALL comments, ZERO functional readers.** (The plan said "comment-only
hits"; the count is nine.)

```
$ grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```
returns 17 distinct source paths — `eq.h`, `renderer_event_receiver.cpp`,
`renderer_frame_render.cpp`, `renderer_intents.h`, `eq_bridge_intents.cpp` ×2,
`event_bus.h`, `action_dispatcher{,_social,_guild_misc}.cpp`,
`action_dispatcher.h`, `renderer_command_catalog.cpp`,
`renderer_frame_input.cpp` ×2, plus `kGraphicsDir`. **`src/client/eq_movement.cpp`
is not among them.** **No repoint needed; no `tests/` file was edited.**

### 9. File-header comment lines — every one enumerated

The pre-split file-header comment block is **lines 1-38** (not the "1-30" §File-header
comments claims — a plan error; line 30 is mid-way through the
`StartUpdateLoop`/`StopUpdateLoop` NOTE, and the block runs to `:38`). All 38
lines were replaced with per-TU headers. Line 39 (blank) onward is verbatim.

- **`eq_movement.cpp` — 26 header lines.** Names the commanded-movement cluster
  (Move/MoveTo/MoveToEntity/MoveToEntityWithinRange, Follow/StopFollow, both
  `StartCombatMovement` overloads, `UpdateCombatMovement`, Face/FaceEntity,
  `Test_SnapToMovementTargetForSeam`, `StopMovement`); cites J23 and F08; names
  both sibling TUs and what each holds; says the eight constants moved to the
  internal header. **Carries forward, condensed into two lines, the pre-split
  header's four still-true NOTEs** (FindEntityByName/ListEntities,
  SafeQueueZonePacket, StartUpdateLoop/StopUpdateLoop, QueuePacket all left in
  `eq.cpp` by F08) and keeps the G03 `StartCombatMovement(uint16_t)` NOTE
  verbatim. The old header's *enumeration of the whole file's contents* — which
  is what would have gone actively wrong — is gone.
- **`eq_movement_update.cpp` — 26 header lines.** Names `UpdateMovement`,
  `SendPositionUpdate`, and the four geometry/arrival helpers; cites J23 and
  F08; names the siblings; **states in plain words that `UpdateMovement` is 499
  lines and is a recorded Batch-G decomposition candidate J23 deliberately did
  not touch**, with both reasons (a verbatim-relocation commit's byte proof, and
  ~646 lines of headroom under the warn ceiling), pointing at
  `refactor/J_file_size_reclamation.md` §Optional. Says this TU reads five of
  the eight constants.
- **`eq_movement_state.cpp` — 20 header lines.** Names all six clusters
  (pathfinding, movement mode / position state, jump + history, water state,
  speed, keyboard control); cites J23 and F08; names both siblings; says this TU
  reads three of the eight constants.
- **`eq_movement_internal.h` — 7 written lines**, verbatim as §HAZARD 1
  prescribes (6 `//` + `#pragma once`).

**No other comment in the file was changed.** All header text is `//` and
therefore allowlisted boilerplate for the verifier.

⚠ **Correction 40 has no exposure here** and I did not have to reword anything:
**no test scans this file as text at all** (§8), so no header line can satisfy a
`find != npos` guard or perturb a counting guard. Nothing was quoted defensively
because there is nothing to defend.

### 10. The internal header — 19 lines, signed-off contents

`include/client/eq_movement_internal.h`, mode 644, **exactly 19 lines**, matching
the plan's prescription:

| lines | content |
|---|---|
| 1-6 | the six `//` lines §HAZARD 1 prescribes, verbatim |
| 7 | `#pragma once` |
| 8 | blank |
| 9-19 | **`eq_movement.cpp:70-80` VERBATIM** — asserted programmatically in the proof script (`hdr[8:] == orig[69:80]`), not eyeballed |

**7 written + 1 blank + 11 moved = 19.** ✅

**Enumerated exception list (correction 10 — each measured, not added for
symmetry).** Every one of the eight is promoted for the same measured reason,
and no ninth symbol was added:

| symbol | why it is in this header |
|---|---|
| `DEFAULT_RUN_SPEED` | read at 1519 (state TU) — outside the definition's TU |
| `DEFAULT_WALK_SPEED` | read at 1522/1525/2111 (state TU) |
| `WALK_SPEED_THRESHOLD` | read at 1753/2109 (state TU) |
| `POSITION_UPDATE_INTERVAL_MS` | read at 1003 (update TU) |
| `FOLLOW_CLOSE_DISTANCE` | read at 891/895 (update TU) |
| `FOLLOW_FAR_DISTANCE` | read at 889/895 (update TU) |
| `FOLLOW_MIN_SPEED_MULT` | read at 892/896 (update TU) |
| `FOLLOW_MAX_SPEED_MULT` | read at 890/896 (update TU) |

The two grouping comments (`// Movement constants`, `// Following constants`)
and the blank between the groups travel too — that is the whole 70-80 block and
the reason option (c) was rejected.

⚠ **Correction 18 honoured: the constants stay at GLOBAL scope**, exactly where
they are today. No `detail` namespace, no `using` declarations, **zero call sites
textually changed**. This is J05's shape (2 exception lines for 114 sites), not
J04's (8 lines for 12 sites) — and here it is cheaper still, because `const
float` initialisers are boilerplate-adjacent in a way `static inline` was not.

### 11. Both content proofs — pasted, with the header counted explicitly

**How the header was counted, in both proofs.** The naive recipe (concatenate
the three .cpp bodies) under-counts by exactly 11 lines and still looks clean
(correction 7). I used **J04's method**: re-insert the header's 11 moved lines
**at their original offset** and run both comparisons over **lines 70-2139**
(2,070 lines), not 82-2139. The reconstruction is literally

`hdr[8:19]` (the 11 moved lines) `+ orig[81]` (the blank) `+ body(stays) +
body(update) + body(state)`

where `body(f)` is defined structurally — everything two lines after that TU's
`#include "client/eq_movement_internal.h"` — rather than by an offset constant,
so no arithmetic of mine can shorten a range (corrections 17/20: suspect the
proof first; count variables are not hand-rolled at all here).

```
BASE = 2498c3a5354a7df86d9126e31a3acb787af1664c
original lines = 2139
bodies: 427 795 836   sum 2058
header total lines = 19 | written = 7 | blank = 1 | moved constant lines = 11
header moved block == original 70-80 VERBATIM: OK
recon lines = 2070   target lines = 2070
PROOF1 cmp  exit = 0 -> BYTE-IDENTICAL
PROOF2 diff exit = 0 -> MULTISET-OK
```

Arithmetic that must hold, and does: **81 + 427 + 795 + 836 = 2,139**, and
**11 (header) + 1 (blank 81) + 2,058 (bodies) = 2,070 = lines 70-2139.**

### 12. The verifier

```
$ git -C /mnt/projects/fable/wt/slot3 commit …          # FIRST (correction 13)
$ BASE=$(git -C … merge-base fable HEAD)                # = 2498c3a5, the fork point
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" \
      --repo /mnt/projects/fable/wt/slot3                # --repo, not the default '.'
```

All three of the batch's confident-looking-wrong-answer traps were avoided
explicitly: committed first (not `No C++ files changed`/exit 0), fork point (not
`fable..HEAD`), and `--repo <slot>` (not `main/`).

```
BASE=2498c3a5354a7df86d9126e31a3acb787af1664c
VERIFY_EXIT=0

Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
  refactor/J23_eq_movement_split.md

Files verified: 4
Boilerplate: +127 / -29 lines (allowed)

PASS: move-only (modulo boilerplate).
```

**Result: EXIT 0, `+0/-0` substantive — the plan's prediction held EXACTLY, and
there is NOTHING to enumerate and NOTHING to sign off.** The full flagged set is
empty; every line the diff touches is `#include`, `#pragma`, `//`, `}` or blank.

⚠ **THE DISPATCH BRIEF WAS WRONG ON THIS POINT, AND THE PLAN WAS RIGHT.** The
brief stated that "the verifier will exit 1 by construction -- promoting anything
trips it", generalising from correction 5 (J04's `inline`/`using`) and correction
9 (J11's `extern` preamble). **That generalisation does not hold for a pure
RELOCATION into a header.** §Expected verifier result derived the correct answer
from the plan's own prescribed output and it is worth restating, because it is a
genuinely useful distinction the batch had not yet isolated:

- The verifier compares **distinct normalized lines as a multiset across the
  whole changeset**, not per file. The eleven constant lines are **removed from
  `eq_movement.cpp` and added to `eq_movement_internal.h` unchanged**, so each
  distinct line's count is 1 before and 1 after. **They cancel.**
- J04 and J05 exited 1 because promotion **rewrote the lines** (`static inline
  read_val` -> `inline read_val`; `namespace detail` + `using`). A changed line
  is a removal plus an addition and cannot cancel.
- J11 exited 1 because its non-boilerplate preamble lines were **duplicated**
  into five TUs — 1 before, 5 after.

**The rule, sharper than correction 5/9: a promoted symbol costs verifier lines
only if its text CHANGES or its COUNT changes. Move eleven lines verbatim into a
header included by every TU that used to see them, keep them in their existing
namespace, and the cost is zero.** Correction 18's "keep it in its existing
namespace" is what buys this — it is the difference between J05's 2 lines and
J04's 8, and here it is the difference between some number and none at all.
**A future plan prescribing an internal header should predict exit 0 unless it
also prescribes a text change.**

Both the `--repo` trap and the wrong-base trap are ruled out positively rather
than by assumption: the verifier names **exactly** this unit's 4 C++ files and 2
non-C++ files and nothing else, which is what a correct base + correct repo looks
like. A wrong repo would have listed `main/`'s merged units; a wrong base would
have listed slot1's or slot2's.

### 13. Final numbers

| file | lines | methods | note |
|---|---|---|---|
| `src/client/eq_movement.cpp` | **486** | **13** | commanded movement API |
| `src/client/eq_movement_update.cpp` | **854** | **6** | per-tick pump; holds the 499-line `UpdateMovement` |
| `src/client/eq_movement_state.cpp` | **889** | **24** | pathfinding / state / jump / water / keyboard |
| `include/client/eq_movement_internal.h` | **19** | — | the eight constants |
| **total** | **2,248** | **43** | 3 TUs, largest **889** |

`check_file_sizes.sh --files` over all four: **4 ok, 0 warn, 0 fail.**

**Method counts: 13 / 6 / 24, against the plan's "roughly 14 / 6 / 23".** The
one-off in each direction is `Test_SnapToMovementTargetForSeam` (:480) and the
`LoadPathfinder`/`SetMovementMode` boundary — cluster membership was right, the
count was approximate, as §Optional's "roughly" allowed.

⚠ **Predicted vs actual sizes, and WHY they differ — this is correction 12/19
territory and the plan flagged it.** §The split predicts 498 / 866 / 908. Those
numbers are `71 (preamble) + range`, where 71 assumes the **38-line pre-split
header rides along verbatim**. §File-header comments then tells each TU to write
its **own** header, which the plan explicitly says "lands on top of that". The
derivable identity is:

**TU lines = (own header) + 31 (lines 39-69) + 1 (the `#include`) + 1 (blank 81) + body**

- `eq_movement.cpp` = 26 + 33 + 427 = **486** (plan 498; my header is 12 shorter than 38)
- `eq_movement_update.cpp` = 26 + 33 + 795 = **854** (plan 866; same 12)
- `eq_movement_state.cpp` = 20 + 33 + 836 = **889** (plan 908; my header is 18 shorter, and the plan's 908 also over-counts the range by ~1)

**Every TU came in slightly under the plan, uniformly, for one reason: my
adapted headers are shorter than the 38-line block the estimate carried.** Body
ranges are exact to the line. Largest TU **889**, 611 lines under the warn
ceiling.

### 14. Acceptance — every item, by exit code

| step | result |
|---|---|
| `cmake --build build -j24` | first invocation **exit 143** (10 min tool ceiling); re-invocation **exit 0** |
| ELF scan after the 143 | **221 binaries scanned, 0 BAD** |
| `scripts/build-gles2-x86.sh` | first **exit 143**; re-invocation **exit 0**; 40 binaries, 0 BAD |
| `ctest -j6` | **3506/3507 passed**; sole failure `AudioManagerTest.PreloadCommonSounds` — the documented ALSA host quirk, the only entry in `refactor/test_baseline.txt`. **Zero new failures.** ctest exits 8 because of it. |
| visual gate (in-suite #3504) | **Passed, 43.62 s** |
| goldens | `git status --porcelain` shows only my 5 intended paths — **untouched** |
| `scripts/build-arm-noble.sh` | **exit 0, first invocation**; 18 binaries, 0 BAD |
| `check_file_sizes.sh --files` | **0 warn, 0 fail** |
| `verify_move_only.py "$BASE..HEAD" --repo <slot>` | **exit 0, PASS, +0/-0 substantive** (§12) |

⚠ **GATE ORDERING (correction 41): I used the FIX, not the workaround. I ran
`scripts/build-gles2-x86.sh` BEFORE `ctest -j6`**, so the in-suite `visual_gate`
ran against a `gles2_zone_benchmark` **stamped 13:45** against edits made at
~13:15. **The in-suite run is therefore the one that counts, and no standalone
re-run was needed.** (J25 and J33 both had to re-run standalone; building first
is strictly cheaper — it costs nothing, since `build-gles2-x86.sh` is on the bar
anyway, and it removes a whole class of vacuous pass.)

⚠ **The truncation trap fired a SIXTH and SEVENTH time — both the J13
absent-not-truncated shape, and one of them is a NEW kill site.**
- The x86 `build/` 143 was killed **archiving `lib/libwilleq_core.a`**, and the
  log's last line is `gmake[2]: *** Deleting file 'lib/libwilleq_core.a'`. **The
  batch's record so far only has killed *compiles* (J07/J10/J12) and killed
  *links* (J13/J33). A killed `ar` is a third site, and it behaves like a link:
  gmake deletes its own partial archive.** Worth recording, because a truncated
  static library would not be caught by a `build/bin/*` ELF scan at all —
  `readelf -h` on an archive is not what the scan checks. Here gmake cleaned up
  and the rebuild was clean, but the scan as written would not have found it.
- The `build-gles2-x86.sh` 143 was killed linking `bin/willeq`; gmake deleted it
  cleanly. **0 BAD both times.**
- Both re-invocations exited 0 and both post-scans were clean, so the J07/J10/J12
  "clean exit 0 over truncated binaries" trap did **not** fire here. **The scan is
  what distinguishes the two outcomes and it cost seconds; keep it mandatory.**

### 15. Findings only (not fixed)

1. **`docs/module_map.md` needs two new rows** — `eq_movement_update.cpp` and
   `eq_movement_state.cpp` — and its `eq_movement.cpp` row is now wrong (2,139 →
   486, and the file no longer holds `UpdateMovement`). **Recorded, not acted on**
   (territory, and correction 24's queue).
2. **Nine `tests/` comment citations of `eq_movement.cpp` are now imprecise.**
   Per correction 24 I checked them **at my base commit**: none of the nine cites
   a **line number** — they all name the file, or a method in it, in prose. So
   unlike J06/J07/J08 there is no pre-existing line-number rot here, and unlike
   J31 I have **not newly invalidated** any line citation. What I *have* done is
   make five of them name the wrong TU: the three
   `SendPositionUpdate`/`UpdateMovement` references
   (`test_position_frame_roundtrip.cpp:12,206,383`) now point at
   `eq_movement_update.cpp`, and `test_zone_position_convention.cpp:295` and
   `test_setheading_conversion.cpp:8` point at the update/state TUs respectively.
   **Attribute those five to J23 in the doc-hygiene queue.** The remaining four
   are still correct or still ambiguous-either-way.
3. **`UpdateMovement` is 499 lines** and is now 58% of its 854-line TU. Batch-G
   candidate, **flagged in the TU's own file header so the question is not
   re-opened by the next reader**, exactly as J33 did for `updatePlayerMovement`.
   The §Optional test — `preamble + largest method` = 33 + 499 = **532** against a
   1,500 ceiling — says clearly there is **no ceiling problem** and therefore no
   Batch-G decision owed here either (correction 34's arithmetic, applied to this
   file). `SendPositionUpdate` at 229 is a distant second.
4. **No dead-content site found in this file** — no `#if 0`, no commented-out
   method bodies, no retired-configuration arm. The eight-site dead-content
   decision gains nothing from J23.

### 16. Plan errors to fold into the template

1. ⚠ **The two-blank-line claim is wrong** (§Every cut boundary). One blank is
   the convention: **141 single-blank runs, exactly one double** (1008/1009).
   Neither cut is affected, but a unit trusting the claim would look for a
   convention that isn't there.
2. ⚠ **The file-header comment block is 1-38, not 1-30** (§File-header comments).
   A unit that replaced only 1-30 would leave eight orphaned NOTE lines above its
   new header.
3. ⚠ **The predicted TU sizes (498/866/908) are `71 + range`, which silently
   assumes the 38-line pre-split header rides along** — while §File-header
   comments tells you to write your own. The two sections are consistent only if
   you read the correction-12 caveat, which the plan does include. **Better shape
   for future plans: state the preamble as `(your header) + 33 + body` and let the
   executor fill in the first term.**
4. ⚠ **NEW, and the most dangerous: `grep -rn --include=…` silently finds
   nothing on this host** (ugrep 7.8.4). See §7. Fifth member of the
   silently-empty-grep family. **Add it to the batch's grep-trap list.**
5. **The `tests/`/`scripts/` grep returns nine hits, not an unstated number** —
   all comments, zero functional readers, as predicted.
6. **Method-count prediction 14/6/23 is actually 13/6/24.** Minor; cluster
   membership was exact, which is the batch's consistent pattern (correction 12).
7. ⚠ **The dispatch brief's "the verifier will exit 1 by construction" is wrong
   for a verbatim promotion, and the plan's own §Expected verifier result was
   right.** See §12 for the derivation. A brief that tells an executor to expect
   a failing exit code invites them to stop chasing an anomaly that is really
   there; here the correct expectation (exit 0, +0/-0) was also the observed one,
   so nothing was missed, but the reasoning should be corrected in the template
   rather than left as folklore. **Promotion costs verifier lines only when the
   promoted text CHANGES or its COUNT changes.**

### 17. Wall clock

**~75 min end to end.** The cut itself, the CMake edit and **both proofs took
under 4 minutes**; the survey and re-derivation ~12; everything else was the bar.
**Slowest step: the x86 `build/` build — two invocations, ~10 min (killed at the
ceiling) + ~6, so ~16 min for one build.** `build-gles2-x86.sh` cost the same
shape (~10 killed + ~4). `ctest -j6` was **265.8 s**, and `build-arm-noble.sh`
finished on its **first** invocation.

**Did wave concurrency stretch the tail? Yes — and the cost was entirely in the
two exit-143s, ~14 min of pure re-run.** Both are correction 6/14's shape: not
breakage, re-invoke. `ctest -j6` at 4.4 min against J02's three-wide 6.8 min and
J06's solo-beating 2.0 min sits mid-range, so correction 14 holds: the stretch
figures are "may happen", not "will". Slot1 (J32) and slot2 (J26) were both
active throughout.

### 18. Deviations

**One, and it is the one the plan enumerates and justifies (§Includes and the
preamble, the correction-20 discipline):** lines **70-80 do not travel in the
copied preamble**. They move once, into `include/client/eq_movement_internal.h`,
and all three TUs carry `#include "client/eq_movement_internal.h"` at the point
those eleven lines occupied — line 69's blank, then the include, then line 81's
blank, then the body. Lines 39-69 and 81 are copied **verbatim** into all three
TUs, including the `EQT_HAS_NAVMESH` and `WITH_AUDIO` groups and the `#ifndef
M_PI` pair.

**Nothing else deviates.** No method decomposed, no dead content deleted, no
include block trimmed, no reordering, no interface change, no `tests/` file
touched, no `include/client/eq.h` touched, no TU added beyond the two named.
HAZARD 1 resolved as **option (a)**, as prescribed. All four created/changed
files are mode 644.
