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
