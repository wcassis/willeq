# J25 — Batch J Wave 6: split `src/client/action/action_dispatcher.cpp` (2,081 → 3 TUs, largest ~729)

**Slot: assigned at dispatch. Branch `task/J25-action-dispatcher-split` cut from `fable`.
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

## ⚠ THE ONE THING THIS UNIT MUST NOT GET WRONG: `tests/test_reachability_sweep.cpp`

**`tests/test_reachability_sweep.cpp` opens `src/client/action/action_dispatcher.cpp`
and uses it as text at TWO sites, and both break when methods move out.** This is
correction 10 (J11's) and correction 27 (J13's), and it is **not optional and not
scope creep** — it is part of this unit's territory.

⚠ **The batch plan's §Residual row records ONE site (`:356`). That is stale: there
are TWO, at `:357` and `:394`, and the second is the invisible kind.**

| site | code | what happens after the split if you do nothing |
|---|---|---|
| `:357` | `auto lines = splitLines(readFile(kRoot / "src/client/action/action_dispatcher.cpp"));` in `TEST(ReachabilitySweep, UnreachableHandlerMethodsAreExactlyTheBlockedList)` — parses **every `ActionDispatcher::` definition** in the file to build the `methods` set, then the `m_handler->…` and dispatcher→dispatcher call graphs | ⚠ **The `methods` set loses every method that moved out.** `kBlockedHandlerMethods` is `{}` (empty), so the test asserts that *no* `IActionHandler` pure virtual is unreached — with two thirds of the dispatcher invisible, a large set of handler methods appears orphaned and the test **fails loudly with a long list of names**. Easy to misdiagnose as "the split deleted the handlers". |
| `:394` | `if (f.rel == "src/client/action/action_dispatcher.cpp") continue;` inside the **external-caller** sweep over `src/` and `include/` | ⚠ **Silently INVERTS the test's meaning.** The exclusion exists so the dispatcher's own internal `checkGMMode(...)`-style calls are not counted as *external* callers. After the split the new TUs are no longer excluded, so their internal calls are read as external ones, **every method they call becomes "reachable", and a genuinely unreachable method is quietly declared reached.** This is a `readFile`-invisible **string-compare exclusion** — exactly J13's third site, the one a `readFile` grep cannot find. |

**The prescribed repoint — exactly four edits, no more:**

1. `:357` — read **all three** TUs and concatenate, so the `methods` set and both
   call graphs are computed over the whole dispatcher:
   ```cpp
   auto lines = splitLines(readFile(kRoot / "src/client/action/action_dispatcher.cpp") +
                           readFile(kRoot / "src/client/action/action_dispatcher_social.cpp") +
                           readFile(kRoot / "src/client/action/action_dispatcher_guild_misc.cpp"));
   ```
   ⚠ **Concatenate, do not replace.** Replacing the path with one new TU would
   make the test assert over a third of the dispatcher and pass while guarding
   almost nothing.
2. `:394` — **ADD** two exclusions and **KEEP** the existing one:
   ```cpp
   if (f.rel == "src/client/action/action_dispatcher.cpp") continue;
   if (f.rel == "src/client/action/action_dispatcher_social.cpp") continue;
   if (f.rel == "src/client/action/action_dispatcher_guild_misc.cpp") continue;
   ```
   Keeping all three makes the guard **exactly as strict as it is today** — the
   content previously excluded is still excluded, wherever it now lives.
   **Do not replace the existing line**; replacing it would newly expose the
   surviving `action_dispatcher.cpp` to the producer sweep, which is a behaviour
   change wearing a repoint's clothes.

**Then measure whether the guard is load-bearing at all, rather than asserting
it** — this is what J13's Review did and it is the model:

```bash
ctest --test-dir build -R ReachabilitySweep --output-on-failure   # BEFORE the repoint
ctest --test-dir build -R ReachabilitySweep --output-on-failure   # AFTER
```
**Run it immediately after the cut and before the full suite.** A failure naming
a long list of handler methods means `:357` is wrong or missing; a failure from
the reachable-set comparison means `:394` is.

⚠ **This unit's verifier prediction is derived from these edits.** See
§Acceptance.

## Measured at `824adb28` (2026-09-02, `fable` tip after J13 merged)

`src/client/action/action_dispatcher.cpp` — **2,081 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **2,081** |
| preamble | **1-8** — six `#include` lines then `namespace eqt {` (7) / `namespace action {` (8) |
| namespace | `namespace eqt { namespace action {` opened at **7-8**, closed at **2080-2081** (`} // namespace action` / `} // namespace eqt`). **Both closes replicate into each new TU.** |
| method definitions | **183** |
| largest method | **68** `ActionDispatcher::interactNearest` (539-606); 2nd **24** `activateHotbarCommandSlot` (847-870). **The lowest floor in the whole batch** — `8 + 68 = 76` |
| `// ====` banners | **29**, at 49, 169, 260, 292, 484, 661, 736, 795, 822, 872, 931, 965, 1167, 1265, 1283, 1301, 1327, 1345, 1355, **1409**, 1498, 1569, 1635, 1678, 1697, 1716, 1734, 1860, 1903 |
| conditional preprocessor directives | **zero.** No `#if`, `#ifdef`, `#else` or `#endif` anywhere in the file. |
| file-scope `static` / anonymous namespaces | **zero of both.** The only `namespace` tokens are the file's own four. |
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
L=open('src/client/action/action_dispatcher.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {690,1409}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-8 | preamble: six includes + the two namespace opens | copied to each new TU | 8 |
| **9-689** | the movement / camera / target / door / spell-gem / inventory / merchant clusters — banners at 49, 169, 260, 292, 484, 661 — including `interactNearest` (539-606, the file's largest method at **68** lines) and `clickNearestDoor` | **stays** `action_dispatcher.cpp` | **691** |
| **690-1408** | the tell / chat / group / raid / pet / hotbar / spellbook / recipe clusters — banners at 736, 795, 822, 872, 931, 965, 1167, 1265, 1283, 1301, 1327, 1345, 1355 — including `replyToLastTell` (690-707), `activateHotbarCommandSlot` (847-870), `forgetSpell` (874-890), `recipesFavorite` (1220-1241) | `action_dispatcher_social.cpp` | **729** |
| **1409-2081** | the `// ========== Guild Actions (T132) ==========` banner (1409) onward — guild, who, LFG, GM, skill and appearance clusters (banners 1498, 1569, 1635, 1678, 1697, 1716, 1734, 1860, 1903), including `trainSkill` (1834-1850) and `checkGMMode` (1911-1927) | `action_dispatcher_guild_misc.cpp` | **681** |

**Result: 3 TUs** (`action_dispatcher.cpp` stays and counts). Largest **~729**.

Arithmetic that must hold: **`689 + 719 + 671 = 2079`, plus the two namespace closes at 2080-2081 = **2,081****. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundaries fall where they do: this file is 183 near-uniform forwarding
methods grouped by 29 banners into feature clusters, and both cuts fall on a
banner boundary between clusters. **9-689** is the world-interaction half
(movement, camera, targeting, doors, casting, items). **690-1408** is
communication and party state. **1409-2081** is guild, GM and character
progression.

⚠ **Cut 1409 is a banner start** — correction 11 makes J03's rule (blank line,
predecessor closes a method, comment depth 0) the deciding rule, and the banner
rule survives only as a tie-breaker for **where a leading comment belongs**. It
belongs with the cluster it introduces, so 1409-1411 travel into
`action_dispatcher_guild_misc.cpp`. **1409 also satisfies J03's rule** (1407 `}`,
1408 blank), so the two rules agree here.

⚠ **Every method in this file is an `ActionDispatcher` member declared in
`include/client/action/action_dispatcher.h`** — external linkage. A method in one
TU calling a method in another is a normal link. **Nothing crosses a cut that
needs promoting.**

**This unit pairs with J15** (`include/client/action/action_dispatcher.h`,
1,581 lines, three independent entities). Same entity, and J15 is already gated
on J04+J12. **They must not run concurrently** — see §Territory.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 690 | `688: }` (end of `sendTell`), `689` blank | `690: ActionResult ActionDispatcher::replyToLastTell(const std::string& message) {` |
| 1409 | `1407: }` (end of `requestWho`), `1408` blank | `1409: // ========== Guild Actions (T132) ==========` — the banner introducing the guild cluster, followed by its multi-line `//` explanation |

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/action/action_dispatcher.cpp
```

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `action_dispatcher.cpp`:**

`src/client/action/action_dispatcher.cpp` has **zero file-scope `static`
symbols, zero anonymous namespaces, and zero file-scope constants.** Every symbol
it defines is an `ActionDispatcher` member with external linkage, declared in
`include/client/action/action_dispatcher.h`.

**Therefore correction 21 has NO exposure in this file** — there is no file-local
helper that could be separated from its consumer. Confirm with your own grep and
paste it rather than citing this sentence:

```bash
grep -n '^static \|^namespace\s*{\|^const \|^constexpr ' src/client/action/action_dispatcher.cpp
```
(expected: no output, exit 1)

```bash
grep -n 'namespace' src/client/action/action_dispatcher.cpp
```
(expected: exactly four lines — 7, 8, 2080, 2081)

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `action_dispatcher.cpp`'s preamble, lines 1-8, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

There is no conditional compilation and nothing non-boilerplate in the preamble,
so the copy is unconditional and total: **lines 1-8 verbatim into each new TU.**

**Each new TU ends with the two namespace closes, replicated verbatim:**
```
} // namespace action
} // namespace eqt
```
⚠ **Correction 30**: replicating "blank + closes" double-spaces any TU whose body
already ends on a blank. Declare it; do not hand-fix it.

## File-header comments

`action_dispatcher.cpp` has a file-header comment block at **1-5**. **Each new TU
gets its own adapted version** naming that TU's clusters and citing
`refactor/J25_action_dispatcher_split.md`, and **the stays file's header must be
updated** because it currently describes the whole dispatcher.

⚠ **Each new TU's header must record that `tests/test_reachability_sweep.cpp`
reads THIS file as text** — so the next person to move a method knows there is a
text-scanning test pointed at it. That sentence is the cheapest defence against
the exact defect this unit had to repair.

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
grep -rn 'action_dispatcher\.cpp' CMakeLists.txt tests/ scripts/
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
actually found**, and if it differs from the SIX below, say so loudly.

At `824adb28` this file has **SIX real add-site(s)**:

| file:line | variable / target | edit? |
|---|---|---|
| `CMakeLists.txt:382` | `SET(CLIENT_CORE_SOURCES` (opens at `:265`) | ⚠ **YES** |
| `tests/cmake/commands.cmake:31` | `ADD_EXECUTABLE(test_action_dispatcher` (`:29`) | ⚠ **YES** |
| `tests/cmake/commands.cmake:55` | `ADD_EXECUTABLE(test_command_processor` (`:49`) | ⚠ **YES** |
| `tests/cmake/commands.cmake:81` | `ADD_EXECUTABLE(test_appearance_command` (`:79`) | ⚠ **YES** |
| `tests/cmake/commands.cmake:101` | `ADD_EXECUTABLE(test_merged_help` (`:99`) | ⚠ **YES** |
| `tests/cmake/commands.cmake:233` | `ADD_EXECUTABLE(test_input_action_bridge` (`:231`) | ⚠ **YES** |
| `tests/cmake/commands.cmake:8` | a comment mentioning `action_dispatcher.cpp` | no — record if it goes stale |
| `tests/cmake/commands.cmake:30` | `test_action_dispatcher.cpp`, the test's own source | no |

⚠ **Five of the six real add-sites are TEST targets in `tests/cmake/commands.cmake`
— a fragment that did not exist before J20.** A plan that greps
`CMakeLists.txt tests/CMakeLists.txt` finds **one of six** and looks complete
(correction 26). **A missed test add-site is a link error at best and a silently
unbuilt test target at worst.** Add **both** new TUs at **all six** sites.

**This unit does NOT touch `gles2_zone_benchmark`.**

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'action_dispatcher' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

⚠ **This file HAS text readers — see the box at the top of this plan.** The two
greps return:

- `grep -rn 'action_dispatcher' tests/ scripts/` — many hits, of which the
  functional ones are **`tests/test_reachability_sweep.cpp:357`** (a `readFile`)
  and **`tests/test_reachability_sweep.cpp:394`** (a **string-compare
  exclusion**). Everything else is `#include` of the *header* or a comment.
- `grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u` — returns
  `readFile(kRoot / "src/client/action/action_dispatcher.cpp")` **and** the
  header line `readFile(kRoot / "include/client/action/action_dispatcher.h")`
  (the latter is the `IActionHandler` pure-virtual parse — **this unit does not
  touch the header, so that line stays**).

⚠ **The two greps are not interchangeable.** The second one **cannot see the
`:394` exclusion** — it is a string compare, not a `readFile`. **Run both.**

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
  - ⚠ **`test_reachability_sweep` is the test this unit is most likely to break,
    and it is also the test that proves the repoint worked.** Run it alone,
    before the full suite, immediately after the cut:
    `ctest --test-dir build -R ReachabilitySweep --output-on-failure`. Paste the
    result. Then **measure whether the `:394` exclusion is load-bearing at this
    tip** rather than asserting it — J13's Review is the model.
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
- ⚠ **Expected verifier result for J25: **EXIT 1, with exactly 5 substantive ADDED lines and 1 substantive REMOVED line — every one of them in `tests/test_reachability_sweep.cpp`, none in the split itself.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied preamble (1-8) is **100% boilerplate**: six `#include` and two
    `namespace … {`.
  - The replicated namespace closes are bare `}` with a `//` tail — boilerplate.
  - Header-comment adaptations cost zero (`//`).
  - There is no non-boilerplate preamble content (contrast J10's 21 and J11's 70)
    and no internal header.
  - `:357`: the single old `readFile(... "action_dispatcher.cpp"));` line is
    **replaced by three lines** (the `readFile` + two concatenated ones as
    written above) → **`-1x`, `+3x`**.
  - `:394`: **two ADDED exclusion lines, none removed** → **`+2x`**.
  - **Total: ADDED 5, REMOVED 1.**

  ⚠ If you format the `:357` change differently — say, onto two physical lines
  instead of three — **the prediction changes and you must re-derive it from what
  you actually wrote** (correction 19). State the derivation, not just the count.

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
  git show "$BASE:src/client/action/action_dispatcher.cpp" | sed -n '9,2079p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Run this over the THREE `.cpp` bodies only.
  `tests/test_reachability_sweep.cpp` is NOT part of the moved content** and must
  not be concatenated into either side — its edits are accounted for by the
  verifier, above. This unit creates no cross-TU header and relocates nothing into
  an existing file, so **neither correction 7 nor correction 20 applies** and the
  diff should be **empty**.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`9-689, 690-1408, 1409-2079`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/action/action_dispatcher.cpp" | sed -n '9,2079p'`.
  **2,071 lines. Expect `cmp` exit 0.**
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
  file**: `src/client/action/action_dispatcher.cpp`, `src/client/action/action_dispatcher_social.cpp`, `src/client/action/action_dispatcher_guild_misc.cpp`, `tests/test_reachability_sweep.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **29**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **2,081** this plan measured,
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
7. **How many CMake add-sites you actually found**, against the SIX
   this plan names, and the output of both `tests/`/`scripts/` greps.
8. **Both content proofs**, pasted, and the verifier's exit code plus `$BASE`.
   State whether the result matched the prediction; if it did not, enumerate
   every flagged line before concluding anything.
9. **Final line counts of every file**, method counts per TU
   (this plan predicts roughly 60 / 63 / 60 of the 183), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. ⚠ **The `test_reachability_sweep.cpp` repoint, in full**: the before/after
    of both sites, the `ctest -R ReachabilitySweep` result, and your **measurement**
    of whether the `:394` exclusion is load-bearing at this tip (J13's Review is
    the model — it proved both exclusions were defensive rather than asserting it).
12. **The six CMake add-sites, each confirmed** — one root, five in
    `tests/cmake/commands.cmake`. If you found a different number, say so loudly.

## Territory

`src/client/action/action_dispatcher.cpp`, the two new TUs beside it
(`src/client/action/action_dispatcher_social.cpp`,
`src/client/action/action_dispatcher_guild_misc.cpp`),
**`tests/test_reachability_sweep.cpp`** (the four enumerated edits at `:357` and
`:394` — in territory, not optional), `CMakeLists.txt` (the one
`CLIENT_CORE_SOURCES` site), `tests/cmake/commands.cmake` (the five test
add-sites), and this plan file. **Nothing else** — in particular **NOT
`include/client/action/action_dispatcher.h`**, which is **J15's** file: this unit
changes no interface and promotes nothing.

⚠ **SERIALISATION: this unit and J15 must not run concurrently.** They are the
same entity and J15 restructures the header this file's every method is declared
in. Correction 31 is the precedent: J21 edited two comments in
`eq_action_handler.cpp` while J12 was splitting it, and git offered the entire
pre-split file as one side of an alternatives-shaped conflict — taking either
side wholesale would have silently reverted a completed unit.

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
- `tests/test_reachability_sweep.cpp` has grown a third reference to this file.
- J15 is in flight in another slot.
