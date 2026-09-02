# J15 — Batch J Wave 7: split `include/client/action/action_dispatcher.h` (1,581 → 3 headers, largest ~1,048)

**Slot: assigned at dispatch. Branch `task/J15-action-dispatcher-header-split` cut from `fable`.
x86 only. No Pi hardware. No eqemu.**

Wave 7 of `refactor/J_file_size_reclamation.md`. Read, before editing anything:
**`refactor/J_file_size_reclamation.md` §Process per unit AND every correction
section (eighty numbered lessons from thirty-odd executed units; newer text wins
over older)**, then **`refactor/J25_action_dispatcher_split.md` including its
Review** — same entity, and **the batch's best handling of a text-scanning test
guard**, which is this unit's one real hazard — and
`refactor/J28_detail_texture_atlas_split.md` §Review. Then
`refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## MOVE-ONLY. No behaviour changes, no interface changes.

The three entities move **verbatim**. No method is added, removed, reordered or
re-signatured. No drive-by fixes. No deletion of dead content: a J unit
**records** it and **moves it verbatim**.

## Serialisation — read this before you start

**J25 (`src/client/action/action_dispatcher.cpp` → three TUs) is MERGED.** This
unit is its header counterpart and the batch plan gates it "after J04 and J12",
both of which are also merged. **Before cutting, prove nothing else is in
flight against this entity:**

```bash
git -C /mnt/projects/fable/main branch -a | grep -i 'dispatch\|action'
git -C /mnt/projects/fable/main log --oneline -20 -- \
    include/client/action/action_dispatcher.h \
    src/client/action/action_dispatcher.cpp
ls /mnt/projects/fable/wt          # what the other slots are holding
```
**If any branch or slot names `action_dispatcher`, STOP and report** — this is
the one Wave-7 unit with a live collision risk, and J12's correction 31 records
what a concurrent edit does to a split file: git offers the entire pre-split
file as one side of the conflict, and taking either side wholesale silently
reverts a completed unit.

## Measured at `09c99b0b` (2026-09-02, `fable` tip after T325 merged)

`include/client/action/action_dispatcher.h` — **1,581 lines** (the batch
survey's figure, unchanged). ⚠ **Re-verify at your own branch tip before you
cut** (correction 9) and report the drift in your Review rather than silently
adjusting.

| property | measured value |
|---|---|
| total lines | **1,581** |
| include guard | **`#pragma once` at line 1** — no `#ifndef` guard |
| includes | **seven**, at 3-9: `<cstdint>`, `<string>`, `<functional>`, `<optional>`, `<vector>`, `<glm/glm.hpp>`, `"client/hotbar/hotbar_slot_type.h"` |
| namespaces | `eqt` opened at **11**, `action` at **18**; closed at **1580** (`} // namespace action`) and **1581** (`} // namespace eqt`). A nested `namespace state { class GameState; }` forward declaration occupies **14-16** and closes itself. |
| preamble | **1-19** — guard, blank, seven includes, blank, `namespace eqt {`, blank, `// Forward declarations`, the three-line `state` block, blank, `namespace action {`, blank |
| top-level entities | **three**: `enum class ChatChannel` (23-33), `struct AppearanceChange` (52-65), `struct ActionResult` (70-81), `class IActionHandler` (89-554), `class ActionDispatcher` (573-1578) — i.e. three free types, one interface, one class |
| conditional preprocessor directives | ⚠ **ZERO in 20-1578.** `awk '/^[ \t]*#(if\|ifdef\|ifndef\|else\|elif\|endif\|define)/'` over the body returns **nothing at all**. The only directive in the file is `#pragma once`. |
| file-scope `static` / `const` / `constexpr` | ⚠ **ZERO.** The narrow grep returns only the three `namespace` opens at 11, 14, 18. |
| namespace-scope free functions | ⚠ **ZERO.** The widened grep returns **no output, exit 1** — see §Correction 21 for the mandatory positive control. |
| multi-line comment regions | many — every entity carries a `/** … */` doc block. **Comment depth at every cut must be COMPUTED, not asserted.** |
| includers | **23** files `#include` this header |
| text-scanning readers | ⚠ **ONE test, at TWO sites.** See §The guard. |

## The split

| Range | Content | Target header | ≈ body lines |
|---|---|---|---|
| **1-19** | preamble: `#pragma once`, seven includes, `namespace eqt {`, the `state::GameState` forward declaration, `namespace action {` | **copied verbatim into each new header**, and kept verbatim in the stays file | 19 |
| **20-81** | the three free types: `ChatChannel` doc block + enum, `AppearanceChange` doc block + struct, `ActionResult` doc block + struct | **new** `include/client/action/action_types.h` | **62** |
| **82-554** | the blank, the `IActionHandler` doc block (83-88) and `class IActionHandler` (89-554) | **new** `include/client/action/action_handler.h` | **473** |
| **555-1578** | the blank, the `ActionDispatcher` doc block (556-572) and `class ActionDispatcher` (573-1578) | **stays** `action_dispatcher.h` | **1,024** |
| **1579-1581** | blank, `} // namespace action`, `} // namespace eqt` | **replicated verbatim into each new header** | 3 |

**Arithmetic that must hold:** `19 + 62 + 473 + 1024 + 3 = 1581`. **If your own
numbers do not sum to the file's line count, you have a gap or an overlap — find
it before you build.**

**Projected finished sizes** — ⚠ correction 71: the "≈ body lines" column is
RANGE LENGTH, not finished file size, and correction 58 says **compute, never
quote**. Each finished file is `(your prescribed header comment block) +
19 + (its new #include lines) + (its range) + 3`:

| file | expression | ≈ |
|---|---|---|
| `action_types.h` | `H + 19 + 0 + 62 + 3` | **~84 + H** |
| `action_handler.h` | `H + 19 + 1 + 473 + 3` | **~496 + H** |
| `action_dispatcher.h` | `H + 19 + 2 + 1024 + 3` | **~1,048 + H** |

**All three clear 1,500 comfortably**, which is the whole point of this unit
and the thing that distinguishes it from J16/J17.

Dependency order is a straight chain and must be respected:
**`action_types.h` ← `action_handler.h` ← `action_dispatcher.h`.**
`IActionHandler`'s methods take `ChatChannel`, `AppearanceChange` and return
`ActionResult`; `ActionDispatcher` needs all four. So `action_handler.h`
includes `action_types.h`, and `action_dispatcher.h` includes **both**
(explicitly — do not rely on the transitive path through `action_handler.h`).

## The umbrella property — 23 includers must not change

`action_dispatcher.h` keeps its name, its `#pragma once`, its full seven-line
include block and its namespace structure, and gains two `#include` lines.
**Every one of the 23 files that includes it sees exactly the declarations it
saw before, in the same order.** No includer is edited.

```bash
grep -rlE '#include[[:space:]]*[<"][^">]*action_dispatcher\.h[">]' src include tests tools benchmarks scripts | wc -l
```
Expect **23**. ⚠ **Re-count at your tip and report the number** — do not quote
this one (correction 67: include counts are a third derived quantity that keeps
slipping). **The count must be the same before and after your change**, and
`git diff --stat` must not name any of those 23 files.

## Header shape — every new header, exactly this

```
// <one line naming what this header holds>
// Extracted from action_dispatcher.h by
// refactor/J15_action_dispatcher_header_split.md. Move-only: nothing was
// edited.

<lines 1-19 of the original, VERBATIM>
#include "client/action/action_types.h"    // action_handler.h only

<the verbatim range>

<lines 1579-1581 of the original, VERBATIM>
```

⚠ The new `#include` line goes **after** the original's seven includes but it
must sit **before** `namespace eqt {` — i.e. it is inserted into the copied
preamble at the end of the include block, not after the namespace opens.
**State exactly where you put it.**

⚠ **Correction 30 and 48: replicating "blank + namespace closes" produces a
double blank line in any file whose body already ends on a blank, and the
artefact has also appeared inverted (no blank after a namespace open).** It is
cosmetic, invisible to both proofs and the verifier — **declare it; do not
hand-fix it**, because fixing it breaks the byte proof.

## ⚠⚠ THE GUARD — this unit's one real hazard, and it is NOT optional

**`tests/test_reachability_sweep.cpp` reads this header AS TEXT at two sites,
and the first one breaks silently-ish.** Correction 10 makes the repoint part of
this unit's **territory**, not scope creep. J11 is the cautionary case (four
tests read `irrlicht_renderer.cpp` as text; three would have failed and one
would have **silently asserted nothing**); J25's handling is the model.

**Site 1 — `tests/test_reachability_sweep.cpp:333`, inside `handlerPureVirtuals()`:**
```
auto lines = splitLines(readFile(kRoot / "include/client/action/action_dispatcher.h"));
```
It then scans forward from `class IActionHandler` collecting `= 0;` pure
virtuals. **After the split, `IActionHandler` is no longer in that file and the
function returns an empty set.** `kBlockedHandlerMethods` is `{}` at line 350,
so `expectSetsEqual(unreachable, kBlockedHandlerMethods, …)` would compare two
empty sets and **pass vacuously** — except that line 354's
`ASSERT_GT(virtuals.size(), 100u) << "IActionHandler parse failed"` catches it
**loudly** first. **That assertion is the only thing standing between this split
and a silently gutted guard. Do not remove or weaken it.**

**Do what J25 did — measure the failure before fixing it:**
1. Perform the split.
2. Build and run `ctest -R ReachabilitySweep` **before** repointing.
3. **Record the failure verbatim in your Review** — it is the proof that the
   guard was load-bearing and that your repoint restored it, not the proof that
   the split broke something.
4. Repoint line 333 to `include/client/action/action_handler.h`.
5. Re-run; expect the same pass it had at base.

**Site 2 — `tests/test_reachability_sweep.cpp:399`**, in the external-caller
scan (`collect({"src","include"}, {".cpp",".h"})` sweeps `include/`
recursively, so both new headers enter it automatically):
```
if (f.rel == "include/client/action/action_dispatcher.h") continue;
```
This excludes the declaration site from counting as a *caller*.
**ADD two lines beside it — do not replace it** (J25's rule: adding keeps the
guard exactly as strict, replacing risks loosening it):
```
if (f.rel == "include/client/action/action_types.h") continue;
if (f.rel == "include/client/action/action_handler.h") continue;
```
⚠ **Then MEASURE whether the exclusion is load-bearing at all rather than
asserting it** — J13's practice, which J25 called the batch's best. The regex
at `:392` matches `dispatcher.foo(` / `m_dispatcher->foo(` shapes. Run
`grep -cE '(m_dispatcher|dispatcher_|dispatcher|m_actionDispatcher|actionDispatcher_)(\.|->)\w+\(' `
against all three headers and report the counts. If they are zero, all three
exclusions are **defensive** — which is a reason to keep all three, not to drop
any.

**Run both greps at your tip and paste both** (correction 10; they are not
interchangeable — the second finds string-compare source-path scans a `readFile`
grep cannot see):
```bash
grep -rn 'action_dispatcher\|action_handler\|action_types' tests scripts
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```
⚠ **`grep -rn --include=<pat>` SILENTLY RETURNS NOTHING on this host** (ugrep
7.8.4, correction 55). Never use `--include`.

At `09c99b0b` the second grep's exhaustive list of source paths any test opens
as text contains `include/client/action/action_dispatcher.h` **and no other
file this unit touches**. ⚠ **Expect nothing new** (corrections 73/77/80) — a
hit is the surprise.

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a symbol CAN stay file-local. It does NOT
prove the plan's ranges KEEP it with its consumers.** J08's plan assigned three
statics to one range and their only consumer to another; it surfaced only at
the BUILD step.

**Run BOTH greps (correction 68 — the widened pattern is call-shaped and
CANNOT match `static constexpr size_t kFoo = 500;`, so it replaces the narrow
one for FREE FUNCTIONS and runs ALONGSIDE it; neither alone is sufficient), and
run each with a positive control before believing any empty result
(correction 60):**

```bash
grep -nE '^static |^namespace|^const |^constexpr |^extern ' include/client/action/action_dispatcher.h
grep -nE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' include/client/action/action_dispatcher.h
```

⚠ **Positive control: `src/client/position.cpp`** — correction 78. Narrow
returns `11:constexpr float position_eps`; widened returns ~30 genuine free
functions. **The controls named in corrections 51 and 61
(`zone_geometry.cpp`'s `heightToColor`, `combat.cpp`'s `IsCorpseEntity`) are
STALE — J32 and J24 split those files and both symbols are gone.**

**The orchestrator audited this plan against correction 21. Result:**
the narrow grep returns **only the three `namespace` opens (11, 14, 18)**, and
the widened grep returns **no output at all, exit 1**. **This header defines
zero file-scope constants and zero namespace-scope free functions.** Every
symbol it declares is a member of `IActionHandler` or `ActionDispatcher`, or one
of the three free types — all of which the dependency chain
`action_types.h ← action_handler.h ← action_dispatcher.h` keeps visible to
every consumer. **Correction 21 has NO exposure in this file.**

**Re-run both at your tip with the control and paste them.** Do not cite this
section — if a file-scope symbol has appeared since `09c99b0b`, **STOP and
report**; the unit changes shape.

## Every cut boundary — verify each at your tip

The rule is **J03's** (correction 11): **cut at a blank line whose predecessor
closes a definition, and verify comment depth 0 at the cut.** The doc block
introducing an entity **travels with that entity.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 20 | `19` blank (after `namespace action {` at 18) | `20: /**` — the `ChatChannel` doc block |
| 82 | `81: };` (close of `ActionResult`) | `82` blank, then `83: /**` — the `IActionHandler` doc block |
| 555 | `554: };` (close of `IActionHandler`) | `555` blank, then `556: /**` — the `ActionDispatcher` doc block |
| 1579 | `1578: };` (close of `ActionDispatcher`) | `1579` blank, `1580: } // namespace action` |

**Paste the surrounding lines for every cut in your Review.**

**Compute comment depth at every cut — do not assert it.** A `//` line
containing the token `*/` makes a naive counter go negative (the trap
`eq_movement.cpp:18` set for the orchestrator's own scanner):

```bash
python3 - <<'EOF'
L=open('include/client/action/action_dispatcher.h').read().split('\n')
cuts={20,82,555,1579}
d=0
for i,ln in enumerate(L,1):
    if i in cuts: print('depth before line',i,'=',d)
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
EOF
```
Expect **0** at all four. ⚠ **Correction 33: "zero block comments" must mean
"zero MULTI-LINE comment regions"; this file has plenty and that is fine —
what the rule needs is depth 0 at the cut.**

## The preprocessor dump — paste it, with a control

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif|define)/ {print NR": "$0}' \
    include/client/action/action_dispatcher.h
```
At `09c99b0b` this returns **nothing** — zero conditional compilation in the
whole file. ⚠ **Correction 60: an empty result is indistinguishable from a
broken pattern. Prove the pattern live** by running the identical `awk` against
`include/client/graphics/irrlicht_renderer.h` (fourteen directives) and pasting
that too.

## CMake — expect ZERO add-sites, and prove it

```bash
grep -rn 'action_dispatcher\|action_handler\|action_types' CMakeLists.txt cmake tests scripts benchmarks
```

⚠ **NEVER name `tests/CMakeLists.txt` in a grep (correction 26)** — J20 split it
into fragments under `tests/cmake/`; the top-level file holds no test targets.
⚠ **CMake commands here are UPPERCASE** (correction 12): `grep -n 'set('` and
`grep -n 'add_executable('` both return nothing, silently, which reads exactly
like "no sites here". **Grep for the filename.**
⚠ **Do not cite a CMake line number** — corrections 59 and 76 record five
consecutive units whose target names were right and whose line numbers were all
stale within a day. **Name the target; locate it yourself.**

**At `09c99b0b`, no CMake file names `action_dispatcher.h`** — this project
does not list headers in its source lists. The hits you will see are for the
**`.cpp`** files, in `tests/cmake/commands.cmake` (targets
`test_action_dispatcher` and one other, each naming
`action_dispatcher.cpp`, `action_dispatcher_social.cpp`,
`action_dispatcher_guild_misc.cpp`). **None of them needs an edit; this unit
creates no `.cpp`.** **The expected add-site count is ZERO.** ⚠ Add-site counts
in this batch have ranged 1 to 19 — **count your own and say so loudly if you
find any.**

## File-header comments

`action_dispatcher.h` opens with `#pragma once` and has **no file-header
comment block**. Each of the two new headers gets the short block prescribed in
§Header shape; `action_dispatcher.h` gains a matching one-line note. ⚠
**Correction 74: do not aim for a line count — carry the required content.**
That is the unit's only non-move content; **enumerate every line of it in your
Review**, exactly as J07 did. **Change no other comment.**

⚠ **Correction 45/62: never quote, in a header comment, a token that any check
in this plan greps for.** The dangerous tokens here are `IActionHandler`,
`class ActionDispatcher`, `action_dispatcher`, `action_handler` and
`action_types` — a comment spelling any of them inflates this plan's own
`tests/`/`scripts/` grep, its CMake grep, or the reachability sweep's own
`readFile` scan. **Describe them in words.** ⚠ **Audit your own written headers
against every grep in this plan before you run one** — correction 62 records
this nearly firing on four units.

## Acceptance (the Batch J bar — every item, classified by EXIT CODE)

- ⚠ **`scripts/build-gles2-x86.sh` FIRST, BEFORE `ctest`** (corrections 41 and
  72). `scripts/ci/visual_gate.sh` runs against the **separate** `build-gles2/`
  tree; running `ctest` first tests whatever binary was there beforehand and
  **passes while proving nothing** — J25 measured a benchmark stamped 11:01
  against edits made at 11:55, and J30 confirmed it directly. **Either build
  gles2 first, or re-run the gate standalone afterwards, and say which you did
  and what the benchmark's timestamp was against your last edit's.**
- **build clean**: `cmake --build build -j24`. Classify by **EXIT CODE**, never
  by grepping the log — `grep ' error '` **never** matches GCC, which prints
  `error:`. Capture `BUILD_EXIT=$?` **immediately**.
- ⚠ **A long build can end three ways** (corrections 23 and 65): **exit 143**
  (SIGTERM at the 600 s tool ceiling); **backgrounded** by the tool and later
  reporting its own code; or **completed with no exit file recorded at all**.
  ⚠ **An absent exit file is NO RESULT, not a bad one** (J28, J34). Relaunch
  detached with a **unit-private exit file** and read the code from inside it.
- ⚠ **After a real exit 143, scan for valid ELF magic — not size**
  (corrections 27, 29, 32, 56). Truncated files have been 50-377 MB of zeroes;
  **no size heuristic catches them.** A killed *link* makes gmake delete its
  output; a killed *compile* can leave a truncated file; a killed **`ar`** is a
  third site (`libwilleq_core.a`, four instances) that `build/bin/*` alone is
  blind to. **Scan binaries AND `lib*.a`:**
  ```bash
  for f in build/bin/* build/*.a build/**/lib*.a; do
    [ -f "$f" ] || continue
    readelf -h "$f" >/dev/null 2>&1 || echo "BAD $f"
  done
  ```
  `rm` every hit, then rebuild once.
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`.
  - ⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known **LOAD-DEPENDENT FLAKE**, not a regression, and is **not** in
    `test_baseline.txt`. **Do not re-diagnose it** — re-run it alone first.
  - ⚠ `ctest` exit 8 during `GoogleTestAddTests.cmake` discovery is **not
    necessarily** the truncated-binary trap (correction 16): check with
    `--gtest_list_tests` first.
  - ⚠ `test_irrlicht_camera_command` has 8 parameterized test NAMES embedding an
    ASLR-randomised pointer; they change on every relink. Redact
    `72-byte object <...>` before diffing `ctest -N`.
  - **`ctest -R ReachabilitySweep` must be 5/5**, run **twice** — once before
    the repoint (expect the loud `IActionHandler parse failed`) and once after.
- ⚠ **the verifier — COMMIT FIRST, THEN VERIFY:**
  ```bash
  git -C /mnt/projects/fable/wt/<your slot> commit ...      # FIRST
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **THREE distinct ways to get a confident-looking wrong answer, two of which
  share a signature:**
  1. **Omitting `--repo`.** It defaults to `.`, and an executor agent's shell
     cwd is **not** its slot (this project forbids `cd`; agent threads reset cwd
     between calls). Omitting it silently verifies `/mnt/projects/fable/main`
     and prints a loud FAIL full of some unrelated merged unit's lines (J02).
  2. **The wrong base rev.** `fable..HEAD` is **WRONG** — units merge during a
     wave, so `fable` moves under you and reports the *other* units' merged
     work, in reverse, as substantive removals (J01: 533 spurious removals).
     **Use the fork point.**
     ⚠ **Failures 1 and 2 look IDENTICAL.** An executor who "fixes" the base and
     re-runs without `--repo` is still verifying the wrong tree. **Check both
     before changing a line of code.**
  3. ⚠ **Running on an UNCOMMITTED tree.** The script prints
     **`No C++ files changed` and EXITS 0** (J06). It looks like a definitive
     pass and is not one.
  Record `$BASE` in the Review and paste the full output.
- ⚠ **Expected verifier result for J15: EXIT 1, `+3 / -1` substantive — the
  test repoint, and nothing else.**

  **Derivation, from what this plan prescribes and nothing else** (correction
  19 — a predicted diff size must come from the plan's own prescribed output):
  - Every moved line keeps its **exact text** and its **count of 1**, so it
    cancels (correction 54: a promoted line costs verifier lines only if its
    TEXT or its COUNT changes).
  - The two copied preambles and the two replicated tails are `#pragma`,
    `#include`, `namespace … {`, `}` and comments — **all BOILERPLATE**;
    `namespace` and `}` change count 1 → 3, which is free for boilerplate.
  - The new header comment blocks are `//` — boilerplate.
  - The two new `#include` lines — boilerplate.
  - **`tests/test_reachability_sweep.cpp:333`**: one line's path string
    changes → **−1 removed, +1 added**, substantive.
  - **`tests/test_reachability_sweep.cpp:399`**: **+2 added**, substantive
    (two new `if (f.rel == …) continue;` lines).
  - **Total: ADDED 3, REMOVED 1 → exit 1.**

  This is the §Process step-4 / correction-5 case: **expected, not a failure.**
  Paste the full output, enumerate every flagged line, and request orchestrator
  sign-off in the Review. **Any line beyond the enumerated set is a real
  finding — chase it.** **NEVER contort code to force a green exit** (J04
  correctly refused to).
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines and allowlists a bare `}` and every
  comment, so a range duplicated into two files, or dropped from both, is
  invisible to it. **Not optional.**
  ```bash
  cat <the 3 bodies> | sort > /tmp/after.txt
  git show "$BASE:include/client/action/action_dispatcher.h" | sed -n '20,1578p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Define `body(file)` STRUCTURALLY** — between the file's own
  `namespace action {` line and its own `} // namespace action` line —
  **never by an offset constant.** J23, J27, J28, J30 and J35 all did this, and
  it is why none of them produced J08's alarming false alarm. ⚠ **Declare count
  variables `local` in your proof helpers** (correction 17): J05's non-`local`
  `n` produced a 247-line shortfall that read exactly like a dropped range.
  **When a proof fails, suspect the proof first** (correction 20) — three
  units, three proof bugs, zero real defects.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  three bodies **in original order** (`20-81`, `82-554`, `555-1578`) and confirm
  the result is byte-identical to
  `git show "$BASE:include/client/action/action_dispatcher.h" | sed -n '20,1578p'`.
  **1,559 lines. Expect `cmp` exit 0.** Extract the bodies **from the produced
  files**, not from scratch `sed` on the original, so the proof covers assembly
  as well as content (J12's rule).
- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report.**
  - ⚠ **A gate FAIL is not necessarily a PIXEL fail — check WHICH metric
    failed** (correction 15). J05's first run failed on `befallen` p95 CPU
    11.684 vs a 10.948 ceiling with **zero image mismatches**; the re-run
    measured 4.676. Contention, not your change. **Re-run.**
  - ⚠ A failure at the **RENDER** step means a `gles2_zone_benchmark` CMake
    add-site was missed. **This unit has no CMake site at all**, so a
    render-step failure would be a genuine surprise — report it, do not re-run
    past it.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ Correction 14: the wave-stretch figures on record are **worst cases
  observed, not forecasts.** J06's `ctest -j6` ran in 120.7 s — faster than
  J01's solo 3.0 min on a larger suite. **Do not report a clean run as an
  anomaly.**
- `scripts/refactor/check_file_sizes.sh --files` on **every changed AND created
  file** — `include/client/action/action_dispatcher.h`,
  `include/client/action/action_handler.h`,
  `include/client/action/action_types.h`,
  `tests/test_reachability_sweep.cpp` → **0 warn, 0 fail.** ⚠ **Compute the
  totals and paste them** rather than quoting §The split (correction 58).
- ⚠ **`chmod 644` every CREATED file before `git add`; PRESERVE the original
  mode on `action_dispatcher.h`** (correction 69 and J01's inverse). A `sed >`
  pass produces 755 and records a spurious `100644 => 100755`; chmod-ing the
  *stays* file records the opposite. ⚠ **3 of 82 `src/client` files are `100755`
  at base** — check this header's mode with `git ls-files -s` **before** you
  touch it, and confirm `git diff --raw` shows no mode change.
- **`git diff --stat` must name exactly four files:** the three headers and
  `tests/test_reachability_sweep.cpp`. Nothing under `src/`, `scripts/`,
  `docs/`, `CMakeLists.txt` or `tests/cmake/`.

## Review must answer

1. **Line count at your tip** against the **1,581** this plan measured, and any
   range drift.
2. **Every property in §Measured, re-verified with your own greps and pasted.**
   If any has changed since `09c99b0b`, **STOP and report**.
3. **The serialisation check** of §Serialisation, pasted — what the other slots
   were holding when you started.
4. ⚠ **Correction 21, explicitly.** Paste **both** greps **and both positive
   controls**, print the widened grep's **exit status** and a **count of its
   unfiltered matches** to prove it was live (J28's is the template), then state
   the derivation in your own words. **Do not cite §Correction 21 — re-derive
   it.**
5. **Comment depth at all four cuts**, computed with the `//`-stripping counter,
   not asserted, plus the surrounding lines for each.
6. **The preprocessor dump**, pasted, with its positive control.
7. ⚠ **The guard, in full:** the `ctest -R ReachabilitySweep` failure **before**
   the repoint, pasted verbatim; the repointed line 333; the two added
   exclusions at `:399`; the measurement of whether those exclusions are
   load-bearing; and the passing re-run.
8. **Every file-header comment line you wrote**, enumerated, plus your
   correction-45 audit of them against this plan's own greps.
9. **The includer count**, before and after — expect it unchanged and expect
   `git diff --stat` to name none of those files.
10. **How many CMake add-sites you actually found**, against the **ZERO** this
    plan predicts, and the output of both `tests/`/`scripts/` greps.
11. **Both content proofs**, pasted, and the verifier's exit code plus `$BASE`.
    State whether the result matched the `+3/-1` prediction; if not, enumerate
    every flagged line before concluding anything.
12. **Final line counts of every file**, computed not quoted; wall clock;
    slowest step; and whether wave concurrency stretched the verification tail.
    ⚠ **Do not report per-file method counts** — derived counts have slipped for
    seven consecutive units while ranges were exact (corrections 66, 75, 79).
13. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). `docs/module_map.md`
    needs rows for the two new headers — **record it, do not act.**

## Territory

`include/client/action/action_dispatcher.h`, the two new headers beside it
(`action_types.h`, `action_handler.h`),
**`tests/test_reachability_sweep.cpp` (lines 333 and 399 ONLY)**, and this plan
file. **Nothing else** — not `src/client/action/*.cpp` (J25's files, merged —
do not touch them), not any other `tests/` file, not `tests/cmake/*.cmake`, not
`CMakeLists.txt`, not `docs/`.

⚠ **`tests/test_reachability_sweep.cpp` is shared with J16** (which reads
`include/client/eq.h` at `:435` and excludes it at `:450`). **If J16 is running
in another slot at the same time, the two units WILL conflict in that file** —
correction 31's shape. **Serialise: do not run J15 and J16 concurrently.**

## STOP conditions

- `ctest -R ReachabilitySweep` does not return to its base-commit result after
  the repoint.
- Any cut boundary does not match §Every cut boundary at your tip and the drift
  is more than a few lines of uniform offset.
- The verifier reports any substantive line beyond the predicted `+3/-1`.
- Either content proof fails after you have satisfied yourself the proof itself
  is correct.
- The includer count changes, or `git diff --stat` names a file outside
  §Territory.
- The visual gate fails on an **image mismatch** (as opposed to the CPU p95
  metric).
- A branch or slot is found holding `action_dispatcher`.
- You find yourself wanting to change a method signature, delete dead content,
  trim the include block, or add a header this plan does not name.

---

## Review (to be written by the executor)

**Executor: slot1, branch `task/J15-action-dispatcher-header`, base tip
`9904f88f48e4b6f7bbb9ec03417a9012d7c80b8b` ("Merge branch 'task/T327' into fable").**
⚠ The dispatch named the branch `task/J15-action-dispatcher-header`, one token
shorter than the `-split` suffix in this plan's header line. Deviation recorded;
the branch actually used is the dispatched one.
⚠ The dispatch also named `refactor/J28_detail_atlas_split.md` and
`refactor/J34_spell_visual_fx_split.md`; the on-disk names are
`J28_detail_texture_atlas_split.md` (as this plan cites) and there is no
`J34_spell_visual_fx_split.md`. Non-blocking.

### 1. Line count at my tip

**1,581 — identical to the plan's measurement at `09c99b0b`.** No range drift:
every one of the four cut boundaries matched the plan line-for-line (§5 below).
Base tip moved from `09c99b0b` to `9904f88f` (T327 merged) but did not touch
this header.

### 2. §Measured, re-verified

| property | plan | measured at my tip |
|---|---|---|
| total lines | 1,581 | **1,581** |
| include guard | `#pragma once` at line 1 | **confirmed** |
| includes | seven at 3-9 | **confirmed** (`<cstdint>`, `<string>`, `<functional>`, `<optional>`, `<vector>`, `<glm/glm.hpp>`, `"client/hotbar/hotbar_slot_type.h"`) |
| namespaces | `eqt` 11, nested `state` 14-16, `action` 18, closes 1580/1581 | **confirmed** |
| preamble | 1-19 | **confirmed** |
| conditional preprocessor directives in 20-1578 | zero | **zero** (§6) |
| file-scope static/const/constexpr | zero | **zero** (§4) |
| namespace-scope free functions | zero | **zero, exit 1** (§4) |
| includers | 23 | **23** (§9) |
| text-scanning readers | one test, two sites | **confirmed**: `tests/test_reachability_sweep.cpp:333` and `:399`, nothing else |
| file mode | — | `100644` at base (`git ls-files -s`), `100644` after (`git diff --raw` shows no mode change) |

Nothing changed since `09c99b0b`. No STOP condition triggered.

### 3. Serialisation check

```
$ git -C main branch -a | grep -i 'dispatch\|action'
  task/J12-action-handler-split
+ task/J15-action-dispatcher-header
  task/J25-action-dispatcher-split
  task/T141-equip-extraction
  task/T99-action-unification

$ git -C main branch --merged fable | grep -i 'dispatch\|action'
  task/J12-action-handler-split
+ task/J15-action-dispatcher-header
  task/J25-action-dispatcher-split
  task/T141-equip-extraction
  task/T99-action-unification
```
**Every branch naming this entity is already MERGED into `fable`** — J12 and J25
included. No live work against it.

Slots when I started:
```
slot1  task/J15-action-dispatcher-header        (mine, clean)
slot2  task/J14-packet-structs-split            M include/common/packet_structs.h
                                                ?? include/common/packets/
slot3  task/J17-renderer-header-split           M include/client/graphics/irrlicht_renderer.h
                                                ?? include/client/graphics/renderer_{draw_types,event_receiver,types}.h
t1     task/T85-join-baseline-capture           (clean)
t2     detached                                 D data/willeq.db-{shm,wal}
```
**No slot holds `action_dispatcher`.** No collision. J16 (`eq.h`) was not
running — nothing in any slot touches `tests/test_reachability_sweep.cpp`.

### 4. Correction 21, re-derived (both greps, both positive controls)

**Narrow — target:**
```
$ grep -nE '^static |^namespace|^const |^constexpr |^extern ' include/client/action/action_dispatcher.h
11:namespace eqt {
14:namespace state {
18:namespace action {
NARROW_EXIT=0
```
**Narrow — positive control `src/client/position.cpp`** (proves the pattern is live):
```
11:constexpr float position_eps = 0.0001f;
CTL_EXIT=0
```

**Widened (call-shaped) — target:**
```
$ grep -nE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' include/client/action/action_dispatcher.h
WIDE_EXIT=1
$ grep -cE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' include/client/action/action_dispatcher.h
0
COUNT_EXIT=1
```
Unfiltered match count: **0**, exit status **1**.

**Widened — positive control `src/client/position.cpp`:** count **31**, first eight:
```
15:inline bool ValueWithin(T value, T min, T max) {
19:std::string to_string(const glm::vec4 &position)
30:std::string to_string(const glm::vec3 &position)
40:std::string to_string(const glm::vec2 &position)
49:bool IsOrigin(const glm::vec3 &position) {
53:bool IsOrigin(const glm::vec4 &position) {
60:float DistanceSquared(const glm::vec2& point1, const glm::vec2& point2) {
68:float Distance(const glm::vec2& point1, const glm::vec2& point2) {
```

**Derivation in my own words.** The narrow grep finds only the three
`namespace` opens: this header declares **no file-scope `static`, `const`,
`constexpr` or `extern` object at all**, so there is no constant that could be
stranded away from a consumer. The widened grep is live (31 hits on the
control) and returns **nothing** here: there is **no namespace-scope free
function** either. Every name the file declares is therefore a member of one of
the two classes or one of the three free types. The two classes each move as a
single indivisible unit, and the three free types land together in
`action_types.h`, which sits at the bottom of the include chain
`action_types.h <- action_handler.h <- action_dispatcher.h`. Consequently every
consumer of every symbol still sees its declaration through that chain, and
**correction 21's failure mode — a symbol separated from its only consumer by a
range boundary — has no way to occur in this file.** No file-scope symbol has
appeared since `09c99b0b`.

### 5. Comment depth at all four cuts (computed, not asserted)

```
depth before line 20 = 0
depth before line 82 = 0
depth before line 555 = 0
depth before line 1579 = 0
final depth 0
```
(the `//`-stripping counter from §Every cut boundary, run verbatim.)

Surrounding lines:
```
   18: namespace action {
   19: 
   20: /**
   21:  * ChatChannel - Types of chat channels.

   80:     }
   81: };
   82: 
   83: /**

  553:     virtual void gmLastName(const std::string& charName, const std::string& lastName) = 0;
  554: };
  555: 
  556: /**

 1577:     float calculateHeadingTo(float x, float y) const;
 1578: };
 1579: 
 1580: } // namespace action
 1581: } // namespace eqt
```
Every boundary is exactly as the plan tabulated. Range arithmetic:
`9 (lines 1-9) + 10 (lines 10-19) + 62 + 473 + 1024 + 3 = 1581`, checked by
`wc -l` on the six extracted pieces.

### 6. Preprocessor dump, with positive control

Target — **nothing at all**:
```
$ awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif|define)/ {print NR": "$0}' include/client/action/action_dispatcher.h
(no output)
```
Positive control `include/client/graphics/irrlicht_renderer.h`, identical `awk`
— **24 lines**, proving the pattern is live:
```
1: #ifndef EQT_GRAPHICS_IRRLICHT_RENDERER_H
2: #define EQT_GRAPHICS_IRRLICHT_RENDERER_H
53: #ifdef WITH_RDP
56: #endif
58: #if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
60: #endif
1067: #ifdef EQT_HAS_GLES2
1069: #else
1071: #endif
1326: #ifdef WITH_RDP
1365: #endif // WITH_RDP
1493: #ifdef EQT_HAS_GLES2
1504: #endif
1627: #if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
1629: #endif
1994: #ifdef EQT_HAS_DRM
2007: #endif
2138: #ifdef EQT_HAS_GLES2
2140: #else
2142: #endif
2493: #ifdef WITH_RDP
2514: #endif // WITH_RDP
2520: #endif // EQT_GRAPHICS_IRRLICHT_RENDERER_H
```

### 7. THE GUARD — in full

**7a. The failure before the repoint** (split done, build exit 0, sweep run
against the un-repointed test), verbatim:
```
    Start 898: ReachabilitySweep.UnreachableHandlerMethodsAreExactlyTheBlockedList
4/5 Test #898: ReachabilitySweep.UnreachableHandlerMethodsAreExactlyTheBlockedList ...***Failed    0.01 sec
[ RUN      ] ReachabilitySweep.UnreachableHandlerMethodsAreExactlyTheBlockedList
/mnt/projects/fable/wt/slot1/tests/test_reachability_sweep.cpp:354: Failure
Expected: (virtuals.size()) > (100u), actual: 0 vs 100
IActionHandler parse failed

[  FAILED  ] ReachabilitySweep.UnreachableHandlerMethodsAreExactlyTheBlockedList (3 ms)
...
80% tests passed, 1 tests failed out of 5
	898 - ReachabilitySweep.UnreachableHandlerMethodsAreExactlyTheBlockedList (Failed)
CTEST_EXIT=8
```
**`actual: 0 vs 100`** — the parse collapsed to the empty set exactly as the
plan predicted, and line 354's `ASSERT_GT` caught it loudly rather than letting
the empty-vs-empty comparison pass vacuously. The other four sweep tests passed
throughout. Note the timing tell as well: the failing run took **0.01 s** where
the passing run takes **~23 s** — the test did no work at all.

**7b. The repointed line.** `tests/test_reachability_sweep.cpp:333`, inside
`handlerPureVirtuals()`, the only line changed there:
```
-    auto lines = splitLines(readFile(kRoot / "include/client/action/action_dispatcher.h"));
+    auto lines = splitLines(readFile(kRoot / "include/client/action/action_handler.h"));
```

**7c. The two ADDED exclusions** — the existing line was kept, not replaced.
After the edit, `:396-401` read:
```
        if (f.rel == "src/client/action/action_dispatcher.cpp") continue;
        if (f.rel == "src/client/action/action_dispatcher_social.cpp") continue;
        if (f.rel == "src/client/action/action_dispatcher_guild_misc.cpp") continue;
        if (f.rel == "include/client/action/action_dispatcher.h") continue;
        if (f.rel == "include/client/action/action_types.h") continue;      <- added
        if (f.rel == "include/client/action/action_handler.h") continue;    <- added
```

**7d. Are the exclusions load-bearing? MEASURED, not asserted.** The `:392`
external-caller regex matches `dispatcher.foo(` / `m_dispatcher->foo(` shapes:
```
$ grep -cE '(m_dispatcher|dispatcher_|dispatcher|m_actionDispatcher|actionDispatcher_)(\.|->)\w+\(' <each header>
action_dispatcher.h: 0
action_handler.h:    0
action_types.h:      0
```
Positive control (files that must match): `src/client/application.cpp`,
`src/client/action/command_processor_bank.cpp`,
`src/client/action/input_action_bridge.cpp`.
**All three exclusions are DEFENSIVE, zero of them currently suppress a hit.**
Per J25's rule that is a reason to keep all three (a future doc example or
inline body in any of the three headers would otherwise silently count itself
as an external caller), not a reason to drop any.

**7e. Does the guard still count what it means to count?** This is the check
the dispatch asked for beyond "it passes". I re-implemented
`handlerPureVirtuals()`'s exact algorithm (same regex, same
`class IActionHandler` start marker, same `};`-prefix break) in Python and ran
it over both the base file and the produced one:
```
base include/client/action/action_dispatcher.h virtuals: 160
new  include/client/action/action_handler.h    virtuals: 160
identical sets: True
symmetric difference: []
new include/client/action/action_dispatcher.h virtuals: 0
```
**The guard parses the identical 160-name set before and after.** Not merely
">100" — the *same* set, element for element. The third line also confirms the
repoint is genuinely required: the surviving `action_dispatcher.h`
`#include`s `action_handler.h`, but `readFile` is a **text** read and does not
follow includes, so scanning the old path yields 0 forever.

**7f. The passing re-run** (after repoint, rebuild exit 0):
```
1/5 ReachabilitySweep.EveryIntentHasAConsumerArm ...................... Passed   0.02 sec
2/5 ReachabilitySweep.ProducerlessIntentsAreExactlyTheBlockedList ..... Passed   1.90 sec
3/5 ReachabilitySweep.EveryEventIsPublishedAndConsumed ................ Passed   2.95 sec
4/5 ReachabilitySweep.UnreachableHandlerMethodsAreExactlyTheBlockedList Passed  23.61 sec
5/5 ReachabilitySweep.EverySenderHasACaller ........................... Passed  20.77 sec
100% tests passed, 0 tests failed out of 5
CTEST_EXIT=0
```
**5/5, same as base.** `ASSERT_GT(virtuals.size(), 100u)` was neither removed
nor weakened.

**For J16's benefit: the ONLY lines of `tests/test_reachability_sweep.cpp` this
unit touched are line 333 (a one-line path change, in place) and an insertion of
two lines immediately after line 399.** Nothing else in the file was edited.
`git diff --numstat` reports `3	1	tests/test_reachability_sweep.cpp` — three
added, one removed (the repointed line counts as one of each), a **net +2**.
The file goes **463 -> 465** lines. **Lines 435 and 450, J16's two sites, move
down by exactly 2 to 437 and 452**; line 333 and everything above 400 keeps its
number.

### 8. Every file-header comment line I wrote, enumerated

`include/client/action/action_types.h`, lines 1-5 (line 5 blank):
```
1  // Free chat-channel, appearance-change and result value types used across the
2  // action layer.
3  // Extracted from the action layer's original combined header by refactor unit
4  // J15 (its plan lives in refactor/). Move-only: nothing was edited.
5  (blank)
```
`include/client/action/action_handler.h`, lines 1-5 (line 5 blank):
```
1  // The action layer's pure-virtual callback interface, which the dispatcher
2  // calls into.
3  // Extracted from the action layer's original combined header by refactor unit
4  // J15 (its plan lives in refactor/). Move-only: nothing was edited.
5  (blank)
```
`include/client/action/action_dispatcher.h`, lines 1-5 (line 5 blank) — the
"matching note", placed above `#pragma once`:
```
1  // The action layer's dispatcher class. Its free value types and its
2  // pure-virtual callback interface now live in the two sibling headers included
3  // below, moved there by refactor unit J15 (its plan lives in refactor/).
4  // Move-only: nothing was edited.
5  (blank)
```

**Correction-45 audit of those twelve comment lines against every grep in this
plan.** The dangerous tokens are `IActionHandler`, `class ActionDispatcher`,
`action_dispatcher`, `action_handler` and `action_types`.

⚠ **Deviation, deliberate and declared: I did NOT use §Header shape's literal
wording.** §Header shape prescribes `Extracted from action_dispatcher.h by
refactor/J15_action_dispatcher_header_split.md`, which spells `action_dispatcher`
**twice** and directly contradicts §File-header comments' own correction-45
instruction to describe these names in words. I followed the correction-45
instruction and referred to the unit as "J15" and to the origin as "the action
layer's original combined header". The required content — what the header holds,
where it came from, which unit moved it, and that it is move-only — is all
carried (correction 74: content, not line count).

Audited grep by grep:
- `grep -rn 'action_dispatcher\|action_handler\|action_types' tests scripts` — my
  comments live in `include/`, outside this grep's scope, **and** contain none of
  the three tokens anyway. Zero inflation.
- `grep -rn 'action_dispatcher\|action_handler\|action_types' CMakeLists.txt cmake
  tests scripts benchmarks` — same: out of scope and token-free.
- `readFile` scan at `:333` over `action_handler.h`, looking for
  `class IActionHandler` — my comment does **not** contain that substring (it
  says "pure-virtual callback interface"), so the scanner's `in` flag is not
  tripped early. Verified empirically in §7e: the parse yields exactly the same
  160 names as the base file.
- external-caller regex at `:392` — requires `dispatcher` immediately followed by
  `.` or `->`, an identifier and `(`. My comments contain the bare word
  "dispatcher" twice with no such punctuation, and all three files are excluded
  at `:399` regardless. Measured at 0 in §7d.
- includer grep `#include ... action_dispatcher.h` — my comments contain no
  `#include` line and no such filename.

The **only** intentional exception is the two real `#include` lines I added to
`action_dispatcher.h`, which are code, not comment, and are required.

**No other comment in any of the three files was changed** — the byte-identical
re-concatenation in §11 proves it.

### 9. Includer count, before and after

```
$ grep -rlE '#include[[:space:]]*[<"][^">]*action_dispatcher\.h[">]' src include tests tools benchmarks scripts | wc -l
23      (before)
23      (after)
```
**23 both sides, as the plan predicted.** `git diff --stat` names none of them:
```
 include/client/action/action_dispatcher.h | 542 +-----------------------------
 tests/test_reachability_sweep.cpp         |   4 +-
 2 files changed, 10 insertions(+), 536 deletions(-)
```
plus the two untracked new headers. **Exactly four files, all inside §Territory.**
Nothing under `src/`, `scripts/`, `docs/`, `CMakeLists.txt` or `tests/cmake/`.

`git diff --raw` confirms **no mode change**:
```
:100644 100644 1a98d64e 00000000 M	include/client/action/action_dispatcher.h
:100644 100644 e988e5f8 00000000 M	tests/test_reachability_sweep.cpp
```
Both created headers were `chmod 644` before `git add`.

### 10. CMake add-sites: ZERO found, ZERO predicted

```
$ grep -rn 'action_dispatcher\|action_handler\|action_types' CMakeLists.txt cmake benchmarks
CMakeLists.txt:391:    src/client/action/action_dispatcher.cpp
CMakeLists.txt:392:    src/client/action/action_dispatcher_social.cpp
CMakeLists.txt:393:    src/client/action/action_dispatcher_guild_misc.cpp
CMakeLists.txt:415:    src/client/eq_action_handler.cpp
CMakeLists.txt:416:    src/client/eq_action_handler_spells_trade.cpp
CMakeLists.txt:417:    src/client/eq_action_handler_extended.cpp
benchmarks/audits/T34/run_verification_primary.sh:10,11:  (comments citing .cpp line numbers)
```
Every hit is a **`.cpp`** — under `src/client/action/` (J25's merged split) or
the unrelated `src/client/eq_action_handler*.cpp` family. Searching for the
header filenames specifically (`action_dispatcher\.h|action_handler\.h|action_types\.h`)
across `CMakeLists.txt cmake tests scripts benchmarks` returns only `#include`
lines in `tests/*.cpp` and the two sweep sites — **no CMake file anywhere names
any header.** This project does not list headers in its source lists.
**Add-sites found: 0. Predicted: 0. Match.** This unit creates no `.cpp`, so
`tests/cmake/commands.cmake`'s `test_action_dispatcher` target and the four
other targets naming the three dispatcher `.cpp` files are untouched and
unaffected.

**Both text-scanning greps, pasted.** First (`tests scripts`) — the only two
lines that matter, out of ~90 hits that are all `#include`s or comment
references to unrelated `eq_action_handler*` tests:
```
tests/test_reachability_sweep.cpp:333:    auto lines = splitLines(readFile(kRoot / "include/client/action/action_dispatcher.h"));
tests/test_reachability_sweep.cpp:399:        if (f.rel == "include/client/action/action_dispatcher.h") continue;
tests/test_reachability_sweep.cpp:357-359:  (readFile of the three .cpp files -- J25's, untouched)
tests/test_reachability_sweep.cpp:396-398:  (their exclusions -- untouched)
```
Second (`kRoot / "src` / `kRoot / "include` — the exhaustive list of source
paths any test opens as text):
```
144:    const std::set<std::string> eqMembers = membersOf(kRoot / "include/client/eq.h");
146:    const std::string src = readFile(kRoot / "src/client/graphics/renderer_event_receiver.cpp");  // J11
151:        const fs::path p = kRoot / "include/client/state" / h;
156:    const std::string src = readFile(kRoot / "src/client/graphics/renderer_frame_render.cpp");
166:    auto lines = splitLines(readFile(kRoot / "include/client/events/renderer_intents.h"));
221:    std::string bridge = readFile(kRoot / "src/client/eq_bridge_intents.cpp");
259:    std::string bridge = readFile(kRoot / "src/client/eq_bridge_intents.cpp");
271:    auto lines = splitLines(readFile(kRoot / "include/client/state/event_bus.h"));
333:    auto lines = splitLines(readFile(kRoot / "include/client/action/action_dispatcher.h"));
357:    auto lines = splitLines(readFile(kRoot / "src/client/action/action_dispatcher.cpp") +
358:                            readFile(kRoot / "src/client/action/action_dispatcher_social.cpp") +
359:                            readFile(kRoot / "src/client/action/action_dispatcher_guild_misc.cpp"));
435:    auto lines = splitLines(readFile(kRoot / "include/client/eq.h"));
49:const fs::path kGraphicsDir = kRoot / "src" / "client" / "graphics";
652:    const std::string src = readFile(kRoot / "src/client/help/renderer_command_catalog.cpp");
690:    const std::string src = readFile(kRoot / "src/client/graphics/renderer_event_receiver.cpp");  // J11
698:    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame_input.cpp");
706:    const std::string frame = readFile(kRoot / "src/client/graphics/renderer_frame_input.cpp");
```
**`include/client/action/action_dispatcher.h` at `:333` is the only entry this
unit touches. Nothing new turned up** — as corrections 73/77/80 expect.
(Line `435` is J16's `eq.h` site: still present, untouched, now at 437.)

### 11. Both content proofs

⚠ **First attempt of the multiset proof FAILED with a 6-line surplus of blank
lines, and correction 20 was right: the proof was wrong, not the split.** My
first `body()` took everything strictly between each file's `namespace action {`
and `} // namespace action`. That interval also contains the **replicated
preamble blank (original line 19)** and the **replicated tail blank (original
line 1579)** — two per file, three files, **six** surplus blanks against a base
body defined as `20,1578`. Corrected `body()` (still structural — it drops
exactly the one replicated preamble blank and the one replicated tail blank,
both of which are boilerplate copied into every file by design, never an offset
constant):

```
body(f) = lines (ns_open+2) .. (ns_close-2)
          where ns_open  = line matching ^namespace action {$
                ns_close = line matching ^} // namespace action$
```
Located structurally per file (all helper variables `local`, correction 17):
```
action_types:      ns_open=23  ns_close=88    -> 62 lines
action_handler:    ns_open=24  ns_close=500   -> 473 lines
action_dispatcher: ns_open=25  ns_close=1052  -> 1024 lines
                                            total 1559
git show $BASE:...action_dispatcher.h | sed -n '20,1578p'  ->  1559 lines
```

**Proof A — byte-identical re-concatenation** (bodies taken from the PRODUCED
files, in original order 20-81 / 82-554 / 555-1578, per J12's rule so the proof
covers assembly as well as content):
```
$ cmp before_cat.txt after2.txt
CMP_EXIT=0
```
**1,559 lines, `cmp` exit 0. Byte-identical.**

**Proof B — line multiset:**
```
$ diff <(sort before_cat.txt) <(sort after2.txt)
MULTISET_EXIT=0
```
**No difference.** Nothing duplicated into two files, nothing dropped from both.

**Blank-line artefact (corrections 30/48), declared and NOT hand-fixed.**
`action_handler.h` and `action_dispatcher.h` each carry a **double blank line**
between their `namespace action {` and the first doc block, because the copied
preamble ends on original line 19 (blank) and their ranges begin on original
lines 82 / 555 (also blank). `action_types.h` has no such artefact (its range
starts at line 20, `/**`). This is cosmetic, invisible to both proofs and to the
verifier, and fixing it would break the byte proof.

### 12. Verifier

`$BASE = 9904f88f48e4b6f7bbb9ec03417a9012d7c80b8b`
(`git merge-base fable HEAD`, run in the slot; `--repo` pointed at
`/mnt/projects/fable/wt/slot1`; run **after** committing so it had a real diff
rather than J06's `No C++ files changed` exit-0 lie).

```
$ BASE=$(git merge-base fable HEAD)      # 9904f88f48e4b6f7bbb9ec03417a9012d7c80b8b
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/slot1

Non-C++ files changed (not verified, review separately):
  refactor/J15_action_dispatcher_header_split.md

Files verified: 4
Boilerplate: +45 / -0 lines (allowed)

SUBSTANTIVE ADDED lines (5):
  +1x  auto lines = splitLines(readFile(kRoot / "include/client/action/action_handler.h"));
  +2x  class GameState;
  +1x  if (f.rel == "include/client/action/action_handler.h") continue;
  +1x  if (f.rel == "include/client/action/action_types.h") continue;

SUBSTANTIVE REMOVED lines (1):
  -1x  auto lines = splitLines(readFile(kRoot / "include/client/action/action_dispatcher.h"));

FAIL: substantive line changes detected. If intentional (e.g. de-static-ing a helper), document in the unit review section for orchestrator sign-off.
VERIFY_EXIT=1
```

**Result: exit 1, `+5 / -1`. The plan predicted `+3 / -1`. The prediction MISSED
by two, and the two are `+2x class GameState;`.**

⚠ Both of the three failure modes that produce a confident-looking wrong answer
were checked **before** interpreting anything: `--repo` points at
`/mnt/projects/fable/wt/slot1` (not the default `.`, which would have verified
`main/`), the base is the **fork point** from `git merge-base` (not `fable..HEAD`),
and the run is **after** the commit — `Files verified: 4` proves it was not J06's
`No C++ files changed` exit-0 case.

**Every flagged line, enumerated:**

| line | count | accounted for by |
|---|---|---|
| `auto lines = splitLines(readFile(... action_handler.h));` | **+1** | §7b, the repoint — predicted |
| `auto lines = splitLines(readFile(... action_dispatcher.h));` | **−1** | §7b, the repoint — predicted |
| `if (f.rel == "include/client/action/action_types.h") continue;` | **+1** | §7c — predicted |
| `if (f.rel == "include/client/action/action_handler.h") continue;` | **+1** | §7c — predicted |
| `class GameState;` | **+2** | **NOT predicted** — see below |

**Why `class GameState;` costs +2, and why it is not a finding.** §Header shape
prescribes that **lines 1-19 of the original be copied VERBATIM into each new
header**. Original line 15 is `class GameState;`, the body of the nested
`namespace state { … }` forward declaration at 14-16. Copying the preamble twice
therefore takes that line's count from **1 to 3**, and correction 54 says a
promoted line costs verifier lines exactly when its **count** changes. The plan's
derivation asserted that the whole copied preamble is boilerplate
("`#pragma`, `#include`, `namespace … {`, `}` and comments — all BOILERPLATE"),
but the preamble contains a **sixth** kind of line the derivation overlooked:
a class forward declaration. `class GameState;` is a real C++ declaration, so
`verify_move_only.py` does not allowlist it the way it allowlists `namespace eqt {`
and a bare `}` — hence `+2`, one per new header.

**This is entirely the plan's own prescribed output, not a deviation from it.**
It is arithmetic the plan's §Expected-verifier-result derivation got wrong, not
a line I introduced: the alternative — omitting the forward declaration from the
copied preambles — would make each new header non-self-contained and would
violate "copied verbatim into each new header". The other four flagged lines are
exactly the four the plan predicted, in exactly the predicted direction.
**There is no line beyond the enumerated set.**

⚠ **Requesting orchestrator sign-off on the `+2x class GameState;` pair**, per
§Process step 4 / correction 5, and noting for the batch that **the corrected
general rule is: a copied preamble costs `+(N-1)` verifier lines for every
non-boilerplate declaration it contains** — for J15 that is one line and two
copies. Any future J unit whose preamble carries a forward declaration will hit
the same surcharge, and its plan should predict it. **No code was contorted to
force a green exit** (correction: J04's rule).

**Cross-check against the independent proofs.** The verifier's `+2` is not
evidence of content drift: §11's byte-identical re-concatenation (`cmp` exit 0
over 1,559 lines) and line multiset (`diff` clean) both compare the **bodies**,
20-1578, which exclude the preamble entirely — so they are silent on this line by
construction, and they prove the moved content itself is untouched. The two facts
are consistent: the body is byte-identical, and the preamble was deliberately
triplicated exactly as prescribed.

### 13. Acceptance bar

| step | result |
|---|---|
| `cmake --build build -j24` (pre-repoint, exit file read) | **exit 0** |
| `ctest -R ReachabilitySweep` (pre-repoint) | **exit 8, 4/5 — the predicted loud `IActionHandler parse failed`** (§7a) |
| `cmake --build build -j24` (post-repoint, exit file read) | **exit 0**, relinked `test_reachability_sweep` |
| `ctest -R ReachabilitySweep` (post-repoint) | **exit 0, 5/5** (§7f) |
| `scripts/build-gles2-x86.sh` — **run BEFORE `ctest`** (correction 41) | **exit 0** |
| full `ctest -j6` | **3496/3498**, exit 8 — see below |
| visual gate | **PASS** (standalone re-run), goldens untouched |
| `scripts/build-arm-noble.sh` | **exit 0** (481 s) |
| `check_file_sizes.sh --files` (4 files) | **4 ok, 0 warn, 0 fail** |
| `git status --porcelain` after the gate | exactly the 4 territory files, **no golden modified** |

No build ended in exit 143; no truncation scan was needed (all three builds were
launched detached with unit-private exit files under a J15 subdirectory —
correction on the shared-scratchpad trap — and each `wait_for.sh` reported a
plausible non-zero elapsed time: 180 s + 540 s, 10 s, 540 s + 15 s, 481 s; no
0-second MET).

**`ctest -j6`: 2 failures out of 3498, ZERO of them new.**
1. `AudioManagerTest.PreloadCommonSounds` — **the documented ALSA host quirk**,
   the sole entry in `refactor/test_baseline.txt`.
2. `visual_gate` — **a CPU p95 metric failure with ZERO image mismatches**
   (correction 15's exact signature). In-suite:
   ```
   [cpu] qeynos2:  p95 4.290  (baseline 5.054, ceiling 10.108)   ok
   [cpu] gfaydark: p95 8.284  (baseline 5.336, ceiling 10.672)   ok
   [cpu] befallen: p95 25.594 (baseline 5.474, ceiling 10.948)   FAIL
   === VISUAL GATE: FAIL ===
     - befallen: p95 process-CPU ms 25.594 exceeds ceiling 10.948 (baseline 5.474 x2.0)
   ```
   No `mismatch` line anywhere in the block. **Standalone re-run, exit 0:**
   ```
   [cpu] qeynos2:  p95 4.458  (baseline 5.054, ceiling 10.108)
   [cpu] gfaydark: p95 7.346  (baseline 5.336, ceiling 10.672)
   [cpu] befallen: p95 5.691  (baseline 5.474, ceiling 10.948)
   === VISUAL GATE: PASS ===
   ```
   befallen 25.594 -> **5.691**, essentially its baseline. **Contention from the
   three-slot wave (slot2 J14 and slot3 J17 were both building concurrently),
   not this change.** There was no RENDER-step failure, which is what a missed
   CMake add-site would have produced — consistent with this unit having zero
   CMake sites.

   Correction 41 satisfied **both ways**: `build-gles2-x86.sh` ran **before** the
   suite, and the gate was additionally re-run standalone afterwards.
   `build-gles2/bin/gles2_zone_benchmark` is stamped **21:23:27**; my last source
   edit (`tests/test_reachability_sweep.cpp`) is stamped **21:12:18**. The gate
   binary is 11 minutes NEWER than the last edit — it tested my tree, not a stale
   one.

`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` did not fire.

### 14. Final line counts (computed, not quoted)

```
$ wc -l include/client/action/action_types.h include/client/action/action_handler.h \
        include/client/action/action_dispatcher.h tests/test_reachability_sweep.cpp
     89 include/client/action/action_types.h
    501 include/client/action/action_handler.h
   1053 include/client/action/action_dispatcher.h
    465 tests/test_reachability_sweep.cpp
```
Against the plan's projections (`H` = 5, the four-line comment block plus its
trailing blank):

| file | plan expression | plan ≈ | actual | delta |
|---|---|---|---|---|
| `action_types.h` | `H + 19 + 0 + 62 + 3` | 84 + H = 89 | **89** | **0** |
| `action_handler.h` | `H + 19 + 1 + 473 + 3` | 496 + H = 501 | **501** | **0** |
| `action_dispatcher.h` | `H + 19 + 2 + 1024 + 3` | 1048 + H = 1053 | **1053** | **0** |

**Every projection hit exactly.** `1,581 -> 1,053` on the surviving header, a
**528-line reduction**; the largest of the three is **1,053**, clearing the
1,500 ceiling by 447 lines. `tests/test_reachability_sweep.cpp` 463 -> 465.

**Where the new `#include`s went** (as §Header shape demands be stated
explicitly): both were inserted at **the end of the copied seven-line include
block, after `#include "client/hotbar/hotbar_slot_type.h"` and before the blank
line preceding `namespace eqt {`** — i.e. into the preamble, not after the
namespace opens. `action_handler.h` gained one
(`#include "client/action/action_types.h"`); `action_dispatcher.h` gained two,
`action_types.h` then `action_handler.h`, both explicit rather than relying on
the transitive path, as the plan requires. `action_types.h` gained none.

**Wall clock ~1 h 15 m.** Slowest step: `scripts/build-arm-noble.sh` at **481 s**,
narrowly ahead of the first full `cmake --build -j24` at ~**480 s** (a cold-ish
tree: the split header forced a rebuild of all 23 includers and everything
downstream of them). `ctest -j6` took **172.6 s** for 3,498 tests — well inside
correction 14's "do not report a clean run as an anomaly" band.

**Did wave concurrency stretch the verification tail? YES, and it cost one
re-run.** slot2 (J14) and slot3 (J17) were building throughout. The only
concrete casualty was the in-suite visual gate's `befallen` p95 (25.594 vs a
10.948 ceiling — a 4.5x inflation over its 5.474 baseline), which measured
**5.691** the moment it was re-run alone. Nothing else in the tail was affected.

### 15. Findings (recorded, NOT acted on)

1. **`docs/module_map.md` needs rows for the two new headers**
   (`include/client/action/action_types.h`,
   `include/client/action/action_handler.h`). Out of §Territory — recorded only.
2. **`tests/test_reachability_sweep.cpp` line numbers shifted by +2 from line
   400 onward.** J16's two sites move from `:435`/`:450` to **`:437`/`:452`**.
   J16 must re-locate them at its own tip rather than trusting the numbers in
   its plan. Per correction 24, **no stale `tests/` line-number citation
   elsewhere in the repo is attributed to this split** — I did not go looking
   for or fix any.
3. **§Header shape's literal wording contradicts §File-header comments'
   correction-45 rule** in this very plan: the prescribed block spells
   `action_dispatcher` twice. Future plans in this batch should write the
   prescribed block in correction-45-safe language rather than leaving the
   executor to reconcile the two sections. Recorded as a template defect.
4. **All three `:399`-family header exclusions are currently defensive**
   (measured 0 hits each, §7d). Worth keeping; worth knowing they assert nothing
   today.
5. `refactor/test_baseline.txt` still carries only the ALSA entry and is
   accurate; no update needed.

### 16. STOP conditions — none triggered

- Sweep returned to its base-commit 5/5 after the repoint. ✔
- All four cut boundaries matched at my tip, zero drift. ✔
- Verifier reported no substantive line **beyond** the enumerated set; the
  one unpredicted pair (`+2x class GameState;`) is the plan's own prescribed
  verbatim preamble copy, derived in §12. **Orchestrator sign-off requested**,
  not a STOP. ✔
- Both content proofs pass (after fixing my own first proof). ✔
- Includer count unchanged at 23; `git diff --stat` names only territory files. ✔
- Visual gate: metric failure only, zero image mismatches; PASS on re-run. ✔
- No branch or slot holds `action_dispatcher`. ✔
- No signature changed, nothing deleted, no include trimmed, no unlisted header
  added. ✔
