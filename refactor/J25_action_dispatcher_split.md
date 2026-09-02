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

## Review (executor, slot1, 2026-09-02)

Branch `task/J25-action-dispatcher-split`, cut from `fable` at
**`0c95f7df4cf59481d6be267f181052c026a49a0f`** ("Batch J Wave 6: plan the last
thirteen files (J23-J35)…"). That is also `$BASE` for the verifier — the plan's
survey commit `824adb28` is an ancestor.

### 1. Line count and range drift

`src/client/action/action_dispatcher.cpp` measured **2,081** at my tip —
**exactly** the plan's number. **Zero range drift**: both cut boundaries, all 29
banner line numbers, the namespace lines and the preamble length reproduced
without adjustment.

### 2. §Measured re-verified (own greps, pasted)

```
$ wc -l src/client/action/action_dispatcher.cpp
2081

$ grep -n 'namespace' src/client/action/action_dispatcher.cpp
7:namespace eqt {
8:namespace action {
2080:} // namespace action
2081:} // namespace eqt

$ grep -n '^static \|^namespace\s*{\|^const \|^constexpr ' src/client/action/action_dispatcher.cpp
(no output, exit 1)

$ awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' src/client/action/action_dispatcher.cpp
(no output — zero conditional preprocessor directives)

$ grep -n '/\*' src/client/action/action_dispatcher.cpp
(no output, exit 1 — zero comment-open tokens of any kind, so zero multi-line
 comment regions; correction 33 satisfied trivially)

$ grep -cE '^// ={4,}' src/client/action/action_dispatcher.cpp
29
$ grep -nE '^// ={4,}' | cut -d: -f1
49 169 260 292 484 661 736 795 822 872 931 965 1167 1265 1283 1301 1327 1345
1355 1409 1498 1569 1635 1678 1697 1716 1734 1860 1903
```

Every banner line number matches the plan.

⚠ **TWO CORRECTIONS TO §Measured — neither changes the shape of the unit, but
both are plan errors the next Wave-6 executor should not inherit.**

**(a) The preamble is FIVE `#include` lines, not six, and there is NO file-header
comment block.** §Measured says "preamble 1-8 — six `#include` lines then
`namespace eqt {`", and §File-header comments says "`action_dispatcher.cpp` has a
file-header comment block at **1-5** … the stays file's header must be updated
because it currently describes the whole dispatcher." Both are wrong. Lines 1-8
at `0c95f7df` are:

```
1: #include "client/action/action_dispatcher.h"
2: #include "client/state/game_state.h"
3: #include "client/pet_constants.h"
4: #include <cmath>
5: #include <limits>
6: (blank)
7: namespace eqt {
8: namespace action {
```

Five includes, one blank, two namespace opens. **The file has no header comment
at all.** The prescribed 1-8 verbatim copy is unaffected — the range is right,
only its description was wrong. What changed is the header work: instead of
*adapting* an existing header there was none to adapt, so all three TUs
(including the stays file) received a **newly written** 13-line header. Every
line is enumerated in §6 below.

**(b) The method count is 184 definition lines, not 183.** `grep -c
'ActionDispatcher::'` returns **184**; of those, 182 match
`^[A-Za-z_].*ActionDispatcher::[A-Za-z_]` and the two that do not are the
constructor (`ActionDispatcher::ActionDispatcher(...)`, line 10 — the token
appears at column 0 so there is no preceding character for `.*` to consume) and
the destructor (`ActionDispatcher::~ActionDispatcher() = default;`, line 14).
Any future count of this file should say which pattern it used; the three
plausible patterns give 182 / 183 / 184.

### 3. Correction 21, re-derived (not cited)

The grep above returns **nothing, exit 1**: zero file-scope `static`, zero
anonymous namespaces, zero file-scope `const`/`constexpr`. The only `namespace`
tokens in the file are its own four (7, 8, 2080, 2081). Every symbol the file
defines is an `ActionDispatcher` member declared in
`include/client/action/action_dispatcher.h` — external linkage, so a call across
a cut is an ordinary link, and there is no file-local helper that could be
separated from a consumer.

**Therefore correction 21 has no exposure here: there is no (helper range,
consumer range) pair to check.** Re-derived from my own greps, not from the
plan's audit sentence.

### 4. Comment depth at every cut (computed)

```
$ python3 <the //-stripping counter from the plan>
depth after line 689 = 0
depth after line 690 = 0
depth after line 1408 = 0
depth after line 1409 = 0
final depth 0
```

Depth is 0 everywhere (the file contains no `/*` at all), so no cut lands inside
a comment region.

### 5. Preprocessor dump

The `awk` dump above returns **nothing**: the file contains no `#if`, `#ifdef`,
`#ifndef`, `#else`, `#elif` or `#endif`. No cut can land inside a conditional
region.

### Cut boundaries, verified at my tip

| Cut | preceding lines | first line of next range |
|---|---|---|
| 690 | `688: }` (end of `sendTell`), `689` blank | `690: ActionResult ActionDispatcher::replyToLastTell(const std::string& message) {` |
| 1409 | `1407: }` (end of `requestWho`), `1408` blank | `1409: // ========== Guild Actions (T132) ==========` |

Pasted context:

```
685: (blank)
686:     m_handler->sendTell(target, message);
687:     return ActionResult::Success();
688: }
689: (blank)
690: ActionResult ActionDispatcher::replyToLastTell(const std::string& message) {

1404: (blank)
1405:     m_handler->requestWho(name, lvlLow, lvlHigh);
1406:     return ActionResult::Success();
1407: }
1408: (blank)
1409: // ========== Guild Actions (T132) ==========
1410: //
1411: // Net-new: EqActionHandler wraps the pre-existing EverQuest::SendGuild*()
```

Both cuts satisfy J03's rule (blank line, predecessor closes a method, comment
depth 0), and at 1409 the banner rule agrees — the banner travels with the guild
cluster it introduces.

### 6. Every file-header comment line written, enumerated

All three TUs carry the **same 13-line header** except for line 1's cluster
list. Lines 2-13 are byte-identical across the three files:

```
 1  // ActionDispatcher -- <per-file cluster list, see below>
 2  //
 3  // Split out of src/client/action/action_dispatcher.cpp by
 4  // refactor/J25_action_dispatcher_split.md (move-only, 2026-09-02). The
 5  // dispatcher's method definitions now live in three TUs:
 6  // action_dispatcher.cpp, action_dispatcher_social.cpp and
 7  // action_dispatcher_guild_misc.cpp; all are declared in
 8  // include/client/action/action_dispatcher.h.
 9  //
10  // WARNING: tests/test_reachability_sweep.cpp reads THIS FILE AS TEXT (it
11  // concatenates all three TUs to parse every ActionDispatcher:: definition,
12  // and excludes all three by path from its external-caller sweep). Moving a
13  // method into or out of this file requires updating that test.
```

Line 1 per file:

- `action_dispatcher.cpp`: `// ActionDispatcher -- movement, camera, targeting, doors, casting and item clusters.`
- `action_dispatcher_social.cpp`: `// ActionDispatcher -- tell, chat, group, raid, pet, hotbar, spellbook and recipe clusters.`
- `action_dispatcher_guild_misc.cpp`: `// ActionDispatcher -- guild, /who, LFG, GM, skill and appearance clusters.`

Header is followed by one blank line, then the verbatim preamble (lines 15-22 of
each produced file), then the body from line 23. **No other comment in any of
the three files was changed.** The stays file received a header where it
previously had none — deviation (a) above.

⚠ **Cosmetic artefact, declared not hand-fixed (correction 30's mirror image).**
The plan warns that replicating "blank + namespace close" double-spaces a TU
whose body already ends on a blank. That did **not** happen — all three ranges
end on a blank line (689, 1408, 2079), so each TU's tail reproduces the
original's exact `}` / blank / `} // namespace action` shape. The artefact that
*did* appear is the opposite one at the **head**: the original's line 9 (blank)
belongs to the stays range, so `action_dispatcher_social.cpp` and
`action_dispatcher_guild_misc.cpp` have their first method definition
immediately after `namespace action {` with no blank line between. Purely
cosmetic; fixing it would break the byte-identical re-concatenation proof.

### 7. CMake add-sites: SIX found, exactly as the plan names

```
$ grep -rn 'action_dispatcher\.cpp' CMakeLists.txt tests/ scripts/
CMakeLists.txt:382:    src/client/action/action_dispatcher.cpp
tests/test_setheading_conversion.cpp:115:// RecordingActionHandler fake test_action_dispatcher.cpp uses) so this
tests/test_reachability_sweep.cpp:357:    auto lines = splitLines(readFile(kRoot / "src/client/action/action_dispatcher.cpp"));
tests/test_reachability_sweep.cpp:394:        if (f.rel == "src/client/action/action_dispatcher.cpp") continue;
tests/cmake/commands.cmake:8:# … plus action_dispatcher.cpp        (comment)
tests/cmake/commands.cmake:30:    test_action_dispatcher.cpp         (the test's own source)
tests/cmake/commands.cmake:31:    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
tests/cmake/commands.cmake:55:    …
tests/cmake/commands.cmake:81:    …
tests/cmake/commands.cmake:101:   …
tests/cmake/commands.cmake:233:  …
```

**Six real add-sites, at exactly the line numbers the plan predicted** — one
`SET(CLIENT_CORE_SOURCES` site at `CMakeLists.txt:382`, five `ADD_EXECUTABLE`
source lists in `tests/cmake/commands.cmake` (`test_action_dispatcher`,
`test_command_processor`, `test_appearance_command`, `test_merged_help`,
`test_input_action_bridge`). Both new TUs added at all six; `grep -rn` across
`tests/` was used, `tests/CMakeLists.txt` was never named (correction 26).
`scripts/` has zero hits. No `gles2_zone_benchmark` site, as predicted.

The comment at `tests/cmake/commands.cmake:8` and the one at `:150` still name
`action_dispatcher.cpp` only; they are now incomplete rather than wrong.
**Recorded, not fixed** (J21's successor pass).

Second grep (correction 10's other half):

```
$ grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
…
333:    auto lines = splitLines(readFile(kRoot / "include/client/action/action_dispatcher.h"));
357:    auto lines = splitLines(readFile(kRoot / "src/client/action/action_dispatcher.cpp"));
…
```

The header read at `:333` is the `IActionHandler` pure-virtual parse; this unit
does not touch the header, so that line stays. **This grep does NOT see `:394`**
— confirming the plan's warning that the two greps are not interchangeable.
No third reference to this file exists (STOP condition clear).

### 8. Both content proofs, and the verifier

`$BASE = 0c95f7df4cf59481d6be267f181052c026a49a0f`

**Proof A — byte-identical re-concatenation.** Bodies extracted from the
**produced files** (not scratch extracts), so the proof covers assembly:
`action_dispatcher.cpp:23-703`, `action_dispatcher_social.cpp:23-741`,
`action_dispatcher_guild_misc.cpp:23-693`, concatenated in original order,
against `git show "$BASE:src/client/action/action_dispatcher.cpp" | sed -n
'9,2079p'`:

```
2071 before_body.txt
2071 after_body.txt
cmp → PROOF1_CMP_EXIT=0
```

**2,071 lines, byte-identical, exit 0** — exactly the plan's expected count.

**Proof B — line multiset.** Same two files, `sort`ed:

```
diff before.sorted after.sorted → PROOF2_DIFF_EXIT=0   (MULTISET-OK)
```

Run over the three `.cpp` bodies only; `tests/test_reachability_sweep.cpp` was
correctly excluded from both sides. No cross-TU header, nothing relocated into
an existing file — neither correction 7 nor correction 20 applies.

**Verifier** (committed first, `--repo` given, fork-point base):
result recorded in §Verifier below.

### 9. Final counts

| file | lines | `ActionDispatcher::` definitions | banners |
|---|---|---|---|
| `src/client/action/action_dispatcher.cpp` | **705** | 67 (65 named + ctor + dtor) | 6 |
| `src/client/action/action_dispatcher_social.cpp` | **743** | 60 | 13 |
| `src/client/action/action_dispatcher_guild_misc.cpp` | **695** | 57 | 10 |
| total | 2,143 | 184 | **29** |

Banner total is preserved at 29 (`grep -cE`, per correction 28).

**Line counts vs the plan's estimate.** The plan projected 691 / 729 / 681 and
told the executor to add its own header lines on top; the delta is uniformly
**+14** (13 header lines + 1 blank) on each file, and the plan's three range
numbers were otherwise exact. `705 = 13 + 1 + 8 + 681 + 2`,
`743 = 13 + 1 + 8 + 719 + 2`, `695 = 13 + 1 + 8 + 671 + 2`.

**Method split is 67 / 60 / 57 against the plan's "roughly 60 / 63 / 60"** —
cluster membership was right (both cuts landed exactly where predicted), the
per-TU arithmetic was not, same as J13's transposition. All three TUs are well
under the 1,500 WARN line.

`check_file_sizes.sh --files` over all four changed/created files:
**4 ok, 0 warn, 0 fail.**

### 10. Findings, not fixes

- `docs/module_map.md` needs rows for `action_dispatcher_social.cpp` and
  `action_dispatcher_guild_misc.cpp`; `src/client/input/CLAUDE.md` describes
  action dispatch as one file. **Recorded, not acted on.**
- `tests/cmake/commands.cmake:8` and `:150` name `action_dispatcher.cpp` in
  prose and are now incomplete. **Recorded.**
- Pre-existing citation rot was **not** attributed to this split (correction 24):
  the only functional readers of this file's text are the two sweep sites, both
  repointed.
- No dead content found in this file: no `#if 0`, no commented-out method
  bodies, no retired-configuration arms. This is the first Wave-6 file with
  nothing to add to the dead-content decision.

### 11. The `test_reachability_sweep.cpp` repoint, in full

**Site `:357` — before:**
```cpp
    auto lines = splitLines(readFile(kRoot / "src/client/action/action_dispatcher.cpp"));
```
**after (3 physical lines, exactly as the plan prescribes — concatenate, not
replace):**
```cpp
    auto lines = splitLines(readFile(kRoot / "src/client/action/action_dispatcher.cpp") +
                            readFile(kRoot / "src/client/action/action_dispatcher_social.cpp") +
                            readFile(kRoot / "src/client/action/action_dispatcher_guild_misc.cpp"));
```

**Site `:394` — before:**
```cpp
        if (f.rel == "src/client/action/action_dispatcher.cpp") continue;
        if (f.rel == "include/client/action/action_dispatcher.h") continue;
```
**after (existing lines KEPT, two ADDED):**
```cpp
        if (f.rel == "src/client/action/action_dispatcher.cpp") continue;
        if (f.rel == "src/client/action/action_dispatcher_social.cpp") continue;
        if (f.rel == "src/client/action/action_dispatcher_guild_misc.cpp") continue;
        if (f.rel == "include/client/action/action_dispatcher.h") continue;
```

Four edits, no more. The header exclusion at `:395` was not touched.

#### `:357` is LOAD-BEARING — measured, not asserted

The split was built and the sweep run **before** the repoint:

```
$ ctest --test-dir build -R ReachabilitySweep --output-on-failure   # BEFORE
4/5 ReachabilitySweep.UnreachableHandlerMethodsAreExactlyTheBlockedList ***Failed 7.87s
  NEW unreachable IActionHandler methods (not on the documented list):
  acceptGroupInvite, acceptTrade, activateHotbarCommandSlot, … trainSkill,
  trainWithTarget, useItem, vendorList, vendorToggle, warp, yellForHelp
  (102 names)
80% tests passed, 1 tests failed out of 5
```

Exactly the failure the plan predicts for a missing `:357` repoint: a long list
of handler methods, which reads like "the split deleted the handlers". After the
repoint:

```
$ ctest --test-dir build -R ReachabilitySweep --output-on-failure   # AFTER
100% tests passed, 0 tests failed out of 5      (5/5, 13.18 s)
```

#### `:394` is DEFENSIVE, not load-bearing at this tip — measured (J13's model)

The exclusion protects the **external-caller** sweep, whose regex is

```
(?:m_dispatcher|dispatcher_|dispatcher|m_actionDispatcher|actionDispatcher_)(?:\.|->)(\w+)\s*\(
```

Counting matches of that pattern inside each of the three TUs:

```
action_dispatcher.cpp:            0
action_dispatcher_social.cpp:     0
action_dispatcher_guild_misc.cpp: 0
include/client/action/action_dispatcher.h: 0   (already excluded at :395)
```

**Zero in all three.** The dispatcher's internal calls are unqualified
(`checkGMMode(...)`, `checkZoneConnection(...)`) — they have no
`dispatcher->`/`dispatcher.` receiver, so the external regex never matched them
even before the split. **At this tip neither the old exclusion nor the two new
ones changes the computed `reachable` set** — the same conclusion J13 reached
about its own string-compare exclusion.

That is a reason to **keep all three**, not to drop any: the guard is cheap, it
encodes the intent ("the dispatcher's own text is never an external caller"),
and the moment someone writes `m_dispatcher->foo()` inside one of these TUs
(a plausible thing to do in a forwarding helper) it becomes load-bearing
immediately. Adding the two new lines rather than replacing the old one keeps
the guard **exactly as strict as it was**, which was the plan's requirement.

### 12. Six CMake add-sites, each confirmed

See §7. **Six found, six predicted, all six line numbers exact** — one root
(`CMakeLists.txt:382`, `CLIENT_CORE_SOURCES`) and five in
`tests/cmake/commands.cmake` (`:31`, `:55`, `:81`, `:101`, `:233`). No
discrepancy to report loudly.

### Acceptance results

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0** (backgrounded by the tool at the 600 s ceiling, then reported its own exit 0 — correction 23's second case, not a 143; ELF scan run anyway, clean) |
| `ctest -R ReachabilitySweep` before / after repoint | 4/5 FAIL (expected) → **5/5 PASS** |
| `ctest -j6` full suite | **3506/3507**, sole failure `AudioManagerTest.PreloadCommonSounds` (the documented ALSA host quirk in `refactor/test_baseline.txt`). 157.6 s. **Zero new failures.** No `HeadlessShutdownTest` flake, no discovery failure. |
| visual gate | **PASS** (ran inside the suite as test `visual_gate`, 47.90 s) |
| goldens | `git status --porcelain` shows only the 4 changed + 2 created files — **no golden touched** |
| `scripts/build-gles2-x86.sh` | see §Long-build notes |
| `scripts/build-arm-noble.sh` | see §Long-build notes |
| `check_file_sizes.sh --files` | **4 ok, 0 warn, 0 fail** |
| verifier | see §Verifier |

### Deviations

1. **§Measured's preamble description and §File-header comments' premise are
   both wrong** (five includes, no existing header comment). Range 1-8 is
   correct; only the description was. Consequence: the stays file gained a
   header it did not have, rather than having one updated. Fully enumerated in
   §6.
2. **Method count is 184 definition lines, not 183** (§2b) — a counting-pattern
   artefact, not drift.
3. **The head-blank cosmetic artefact** in the two new TUs (§6), declared not
   hand-fixed.

Nothing else deviates. No STOP condition was hit: no file-scope symbol appeared,
no comment region exists, both cuts matched exactly, both proofs passed, the
gate found no image mismatch, no third text reference appeared, and J15 was not
in flight.

### Verifier — the +5/−1 prediction HELD EXACTLY

Run **after** committing (`7b16e6fc`), with `--repo` and the fork point:

```
$ BASE=$(git -C /mnt/projects/fable/wt/slot1 merge-base fable HEAD)
$ echo $BASE
0c95f7df4cf59481d6be267f181052c026a49a0f
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/slot1

Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
  refactor/J25_action_dispatcher_split.md
  tests/cmake/commands.cmake

Files verified: 4
Boilerplate: +57 / -0 lines (allowed)

SUBSTANTIVE ADDED lines (5):
  +1x  auto lines = splitLines(readFile(kRoot / "src/client/action/action_dispatcher.cpp") +
  +1x  if (f.rel == "src/client/action/action_dispatcher_guild_misc.cpp") continue;
  +1x  if (f.rel == "src/client/action/action_dispatcher_social.cpp") continue;
  +1x  readFile(kRoot / "src/client/action/action_dispatcher_guild_misc.cpp"));
  +1x  readFile(kRoot / "src/client/action/action_dispatcher_social.cpp") +

SUBSTANTIVE REMOVED lines (1):
  -1x  auto lines = splitLines(readFile(kRoot / "src/client/action/action_dispatcher.cpp"));

FAIL: substantive line changes detected. If intentional …
VERIFIER_EXIT=1
```

**Exit 1, ADDED 5, REMOVED 1 — exactly the plan's prediction, and every one of
the six lines is in `tests/test_reachability_sweep.cpp`. Not one line in the
split itself.** Enumerated against the derivation:

| predicted | observed |
|---|---|
| `:357` old `readFile(...);` removed | `-1x auto lines = splitLines(readFile(… action_dispatcher.cpp"));` |
| `:357` replaced by 3 physical lines | `+1x auto lines = splitLines(readFile(… action_dispatcher.cpp") +`, `+1x readFile(… _social.cpp") +`, `+1x readFile(… _guild_misc.cpp"));` |
| `:394` two ADDED exclusions, none removed | `+1x if (f.rel == "… _social.cpp") continue;`, `+1x if (f.rel == "… _guild_misc.cpp") continue;` |

Nothing beyond the enumerated set, so there is nothing to chase. The `+57 / -0`
boilerplate is the 13-line header × 3 files + their blank separators + the two
copied 8-line preambles + the two replicated namespace-close pairs — all `//`,
`#include`, `namespace` and `}`, all inside the allowlist.

This is the §Process step-4 / correction-5 case: **expected, not a failure.**
**Requesting orchestrator sign-off** on these six lines at merge.

### Long-build notes (correction 23 / 27 / 32)

| build | outcome |
|---|---|
| `cmake --build build -j24` | **backgrounded by the tool** at the 600 s ceiling, then reported its own **exit 0** (166 s of further wall clock after the hand-off). Correction 23's second case — **not** a 143. ELF scan over `build/bin/*` run anyway: clean. |
| `scripts/build-gles2-x86.sh` (1st) | **real exit 143** at the 600 s ceiling, killed mid-link of `bin/willeq`. `gmake` printed `*** Deleting file 'bin/willeq'` and removed its own partial output — **J13's shape exactly** (correction 32): the target was **absent**, not truncated. ELF scan over the whole `build-gles2/bin` found **zero bad files**. |
| `scripts/build-gles2-x86.sh` (2nd) | **exit 0**, 460 s. `bin/willeq` (81 MB) and `bin/gles2_zone_benchmark` (15 MB) both relinked at 12:35 with valid ELF headers; ELF scan clean. |
| `scripts/build-arm-noble.sh` | **exit 0, first invocation**, 160 s. `build-arm-noble/bin/willeq` is `ELF 32-bit LSB pie executable, ARM, EABI5`. |

⚠ **One trap worth boarding for the next executor: the in-suite `visual_gate`
test does NOT necessarily exercise your change.** `ctest -j6` runs `visual_gate`
against the **`build-gles2/` tree**, which is built by
`scripts/build-gles2-x86.sh` — a *separate* step further down the acceptance
list. Running the suite before that script means the gate measures a
`gles2_zone_benchmark` binary that predates the split (mine was stamped 11:01,
my edits 11:55) and passes without proving anything. **The gate was therefore
re-run explicitly after the gles2 build**, and that is the run recorded above:

```
$ bash scripts/ci/visual_gate.sh
  [cpu] qeynos2:  p95 5.525 (baseline 5.054, ceiling 10.108)
  [cpu] gfaydark: p95 7.596 (baseline 5.336, ceiling 10.672)
  [cpu] befallen: p95 8.280 (baseline 5.474, ceiling 10.948)
=== VISUAL GATE: PASS ===
EXIT=0
```

Zero image mismatches, all three CPU p95s inside their ceilings, and
`git status --porcelain` afterwards lists only this unit's six source files plus
this plan — **no golden touched.** Suggest ordering `build-gles2-x86.sh` *before*
the full `ctest` in the acceptance list, or re-running the gate standalone as
this unit did.

### Wall clock

~55 min end to end. The cut plus the six CMake sites plus the four test edits
plus both proofs was **under 5 minutes**; everything else was the bar. Slowest
step by far: `scripts/build-gles2-x86.sh` at **~17 min across two invocations**
(one 600 s SIGTERM plus a 460 s clean rebuild). `ctest -j6` was **157.6 s** for
3,507 tests — no wave stretch worth reporting (correction 14: this is a clean
run, not an anomaly). The x86 `-j24` build and the ARM build were 166 s and
160 s of measured tail respectively.
