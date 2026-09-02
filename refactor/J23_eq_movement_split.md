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
