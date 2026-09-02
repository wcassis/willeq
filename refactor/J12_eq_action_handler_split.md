# J12 — Batch J Wave 4: split eq_action_handler.cpp (2,248 → 3 TUs, largest ~779)

**Slot: assigned at dispatch. Branch `task/J12-eq-action-handler-split` cut from
`fable`. x86 only. No Pi hardware. No eqemu.**

Wave 4 of `refactor/J_file_size_reclamation.md`, alongside **J13**. Read, before
editing anything: **`refactor/J_file_size_reclamation.md` §Process per unit AND
all eight correction sections (twenty-four numbered lessons from J01–J11;
newer text wins over older)**, then `refactor/J07_eq_player_info_split.md`
**including its Review** — J07 is this unit's twin (118 small methods, no
statics, no preprocessor, no hazards) and the closest worked example you will
find — and `refactor/J09_zone_shader_split.md` §Review for how a Review is
expected to read. Then `refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## This is the LOWEST-RISK unit remaining in Batch J. The bar is identical anyway.

Measured, not assumed, at `136a319b`:

- **Zero file-scope `static`s.** `grep -n '^\s*static ' src/client/eq_action_handler.cpp`
  returns **nothing**. There is therefore **no correction-21 exposure at all**
  (J08's plan defect: a file-local helper assigned to a different range from its
  sole consumer). Nothing needs promoting; **no internal header is required.**
- **Zero anonymous namespaces.** The only `namespace` token in the file is
  `namespace eqt {` at line 21.
- **Zero preprocessor directives in the body.**
  `awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/' ` returns **nothing** —
  the J02 unterminated-conditional trap and the J10 nested-`#ifdef` trap both
  cannot fire here.
- **Zero block comments.** `grep -n '/\*'` returns **nothing** — the J02
  cut-inside-a-`/*` trap cannot fire either.
- **Zero functional readers in `tests/` or `scripts/`.** See §Territory. This is
  correction 10's check and it came back clean **only because it was run**.
- **The largest method in the file is 55 lines** (`raidGroup`, 1868-1922). The
  preamble is 22 lines, so a TU's floor is **77**. Every partition of this file
  clears 1,500 by a wide margin; the three chosen below are picked for
  coherence, not to make the arithmetic work.

**Consequence: this unit's verifier run is predicted to exit 0** (see
§Acceptance for the derivation). J06 and J07 are the two precedents; both were
clean, and both still paid the full bar. So will you.

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. **No change to
`include/client/eq_action_handler.h`.** No method decomposition. No drive-by
fixes. No reordering within a cluster. No banner rewording.

## Measured at `136a319b` (2026-09-02, `fable` tip after J08 merged)

`src/client/eq_action_handler.cpp` — **2,248 lines**. ⚠ **The unit table's
2,202 is STALE** (correction 9: nineteen units merged on 2026-09-02 and the
survey predates all of them). **Re-verify against your own branch tip before you
cut** and report the drift in your Review rather than silently adjusting.

**174 method definitions**, not the 173 on record. All are
`EqActionHandler::` members and **every one has its full signature on a single
line starting at column 0** — verified: `grep -n 'EqActionHandler::' | grep -v
'^[0-9]*:[A-Za-z_]'` returns nothing, so there is no multi-line signature to
mis-slice.

**Preamble is lines 1-22, and it is 100% boilerplate:**

| lines | content | boilerplate to `verify_move_only.py`? |
|---|---|---|
| 1-19 | the include block (`client/eq_action_handler.h`, `client/eq.h`, combat, pet_constants, spell/skill/inventory/trade/world_object/game_state, logging, name_utils, performance_metrics, `<fmt/format.h>`, `<cmath>`, `<algorithm>`, `<vector>`) | yes (`#include`) |
| 20 | blank | dropped by the normalizer |
| 21 | `namespace eqt {` | yes (`namespace\s+[\w:]*\s*\{?$`) |
| 22 | blank | dropped |

Body is **23-2246**. Line **2247** is blank; line **2248** is
`} // namespace eqt` — also boilerplate (`\}\s*(//.*)?$`).

**Twenty-seven `// ==========` banners**, listed per cluster in §The split. ⚠
**Unlike every previous unit in this batch, the banner rule and J03's cutting
rule DISAGREE at one place in this file** — see §HAZARD.

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-22 | preamble (see table above) | copied to each new TU | 22 |
| **23-777** | ctor / dtor / `getCombatManager` (23-32); the 11-line T95 enum-bounds comment (33-43) + `isValidMovementMode` / `isValidPositionState` / `isValidPetCommand` (45-108); then the banners **Movement** (109), **Combat** (175), **Combat (T102)** (300), **Interaction** (562), **Chat** (662), **Group** (689), **Character State** (723), **Inventory** (733-777, ending with `scribeItem` at 774-776) — **68 methods** | **stays** `eq_action_handler.cpp` | **~779** |
| **778-1318** | the banners **Spellbook** (778), **Trade** (885), **Pet** (1032), **Tradeskill** (1064-1318) — **21 methods**, including the file's larger bodies: `memorizeSpellByName` (799-840), `requestTrade` (906-951), `acceptTrade` (953-995), `findNearestWorldObject` (1084-1131), `setAppearance` (1233-1284), `setInspectMessage` (1292-1317). ⚠ **The file's own grouping quirk: `setAppearance` and `setInspectMessage` sit under the Tradeskill banner. Leave them there — this unit does not re-group.** | `eq_action_handler_spells_trade.cpp` | ~565 |
| **1319-2246** | the banners **Extended Movement** (1319), **Extended Character State** (1361), **Extended Combat** (1371), **Entity Query** (1394), **Utility** (1404), **Social (T102)** (1410), **Guild (T132)** (1454), **Utility (T135)** (1518), **Raid (T179)** (1577), **Raid leader-only ops (T181)** (1691), **Merchant (T136)** (1954), **Movement / Utility / Skills (T140)** (1998), **Social / Find (T150)** (2117), **GM (T148)** (2198-2246) — **85 methods** | `eq_action_handler_extended.cpp` | ~952 |
| 2247-2248 | blank + `} // namespace eqt` | replicated in each TU | 2 |

**Result: 3 TUs** (`eq_action_handler.cpp` stays and counts, as J01's and J07's
did). Largest **~952**, floor 77.

Arithmetic that must hold: `22 + 755 + 541 + 928 + 2 = 2248`.
If your own numbers do not sum to the file's line count, you have a gap or an
overlap — **find it before you build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** — J03 ran
1-6 high, J11 ran 20-130 LOW, J07 ran exactly 3 high, J06 ran uniformly exactly
1 low. Cluster membership has been right every time; the arithmetic has not.
The three counts above are derived from the ranges in this table plus the
22-line preamble plus the 2-line tail and nothing else. Add your new file-header
comment lines on top (§File-header comments) and say so explicitly.

Why the boundaries fall where they do: the file is `IActionHandler` grouped by
domain banner, and the two cuts fall on the two places where the domain grouping
itself changes character — **Spellbook** opens the spell/trade/pet/tradeskill
block that carries the file's only non-trivial method bodies, and **Extended
Movement** opens everything the file grew after T102 (the extended action
families, guild, raid, merchant, find, GM). Nothing crosses a cut: no method in
one cluster calls a file-local helper in another, because **there are no
file-local helpers.**

## Every cut boundary, with its neighbours — verify each at your tip

The rule is J03's (correction 11, which **replaces** the banner rule outright):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** Checked at `136a319b`:

| Cut | line-2 | line-1 (blank) | line (first of next range) |
|---|---|---|---|
| 778 | `776: }` (end of `scribeItem`) | `777` | `778: // ========== Spellbook ==========` |
| 1319 | `1317: }` (end of `setInspectMessage`, 1292-1317) | `1318` | `1319: // ========== Extended Movement ==========` |
| 2247 | `2246: }` (end of `gmLastName`) | `2247` | `2248: } // namespace eqt` |

Comment depth at both interior cuts is **0** — the file has no block comments at
all, so this is trivially true, but **run the check and paste it** rather than
citing this sentence.

Both interior cuts start at a `// ==========` banner. **Take the banner with the
code below it** (§Process's banner rule, which survives as the tie-breaker for
where a leading comment belongs).

## ⚠ HAZARD — the ONE place the two cutting rules disagree. Do not cut there.

**Line 1691, the `// ========== Raid leader-only ops (T181) ==========` banner,
has NO blank line before it.** Line 1690 is `}` (the close of `raidList`) and
1691 is the banner, directly adjacent:

```
1689|    }
1690| }
1691| // ========== Raid leader-only ops (T181) ==========
```

That banner also opens a **23-line prose block** (1691-1713) explaining which
raid commands eqemu's `Handle_OP_RaidCommand` does and does not gate — the most
load-bearing comment in the file.

**J03's rule refuses this boundary** (no blank line) and the banner rule would
accept it. **The rule that wins is J03's: 1691 is NOT a legal cut point.** This
plan does not cut there, and **you must not "improve the balance" by moving a
cut onto it.** If you find yourself wanting a fourth TU, STOP and report — a
better balance is not worth introducing the batch's first cut that only one of
its two rules allows.

This is the only such site: the other twenty-six banners are all preceded by a
blank line. Confirm that with

```bash
awk '/^\/\/ ={4,}/{ if (prev != "") print NR": NO BLANK BEFORE: "$0 } {prev=$0}' \
    src/client/eq_action_handler.cpp
```

and paste it. **Expect exactly one hit: 1691.**

## Includes

**Each new TU carries `eq_action_handler.cpp`'s full preamble, lines 1-21,
verbatim** — the whole 19-line include block and `namespace eqt {`. **Not "the
includes it actually needs":** §Process is explicit, and trimming per-TU include
sets is a second, riskier change wearing a move-only commit's clothes. It also
removes a judgement call from the executor.

Each new TU ends with `} // namespace eqt`, copied from line 2248.

This unit relocates nothing into an existing file, so **correction 20 (a
destination file gets a narrow, enumerated include list instead) does not
apply** — every destination here is a file this unit creates.

## File-header comments — the one thing you may adapt

`eq_action_handler.cpp` has **no file-header comment today** — line 1 is
`#include`. Each new TU gets a **4-6 line header** placed *above* the copied
include block, naming its clusters and citing
`refactor/J12_eq_action_handler_split.md`. The stays file may gain one too.

Comment lines are allowlisted boilerplate for `verify_move_only.py`, so this is
permitted — but they **are** visible in the line-multiset check, so **enumerate
every header line you wrote in your Review**, exactly as J07 did (its §3 is the
model). **Change no other comment**, and in particular **do not touch the T95
block at 33-43 or the T181 block at 1691-1713.**

## CMake — grep, never trust a line number

```bash
grep -rn 'eq_action_handler\.cpp' CMakeLists.txt tests/CMakeLists.txt tests/cmake/
```

At `136a319b` that is **ONE site**:

| file:line | target |
|---|---|
| `CMakeLists.txt:402` | `SET(CLIENT_CORE_SOURCES` (opens at `:265`) |

**Add both new TUs beside the original at that one site.** `tests/cmake/` and
`tests/CMakeLists.txt` contain **zero** references to this source file — the
sixteen `test_eq_action_handler_*` targets link `willeq_core` and include only
the *header*.

⚠ **`tests/CMakeLists.txt` no longer holds test targets** — J20 split it into 18
per-domain fragments under `tests/cmake/` (top-level file is now 135 lines).
**Any test-target edit goes in `tests/cmake/<domain>.cmake`.** This unit needs
none.

⚠ **CMake commands in this project are UPPERCASE** — `grep -n 'set('` and
`grep -n 'add_executable'` both return nothing, silently, which reads exactly
like "no sites here". **Grep for the filename.** ⚠ **Add-site counts have ranged
from 1 to 19 across this batch** (J01 1, J02 1, J04 5, J05 19, J08 2, J09 4,
J10 12) and the shape of the file gives no hint. **Report the count you actually
found**, and if it is not 1, say so loudly.

`CMakeLists.txt` is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours. Do not "fix" it.**

## Acceptance (the Batch J bar — every item, reported by EXIT CODE)

- **build clean**: `cmake --build build -j24`, classified by **EXIT CODE**,
  never by grepping the log. `grep ' error '` **never** matches GCC output,
  which prints `error:`. Capture `BUILD_EXIT=$?` immediately.
- ⚠ **A long build can end TWO different ways and only one invalidates
  binaries** (correction 23): it can be killed at the 600 s tool ceiling with
  **exit 143 (SIGTERM)**, or it can be **backgrounded by the tool and later
  report its own exit code** (J08's `build-arm-noble.sh` did this — ~11 min,
  exit 0, binaries fine). **After a real exit 143, and only then, scan every
  produced binary for valid ELF magic before trusting the rebuild:**
  ```bash
  for f in build/bin/*; do
    [ -f "$f" ] || continue
    [ "$(head -c 4 "$f" | od -An -tx1 | tr -d ' ')" = "7f454c46" ] || echo "BAD $f"
  done
  ```
  then `rm` every hit and rebuild once. J07 hit this at scale: a timed-out build
  left **twelve binaries truncated mid-link**, the re-invocation **exited 0**,
  and `ctest` then died at *discovery*, reporting one bad binary per full suite
  run. **Do not run this scan reflexively after every slow build; do not skip it
  after a real 143.**
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`.
  - ⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known **LOAD-DEPENDENT FLAKE**, not a regression, and is **not** in
    `test_baseline.txt`. **Do not re-diagnose it.** If it fails in a wave run,
    re-run it alone before reporting anything.
  - ⚠ `ctest` **exit 8 during `GoogleTestAddTests.cmake` discovery is not
    necessarily the truncated-binary trap** (correction 16): check the binary
    with `--gtest_list_tests` before concluding a mid-link truncation.
  - ⚠ `test_irrlicht_camera_command` has 8 parameterized test NAMES embedding an
    ASLR-randomised pointer; they change on every relink (J20's Review). If you
    diff `ctest -N`, redact `72-byte object <...>` first.
- ⚠ **the verifier — COMMIT FIRST, THEN VERIFY:**
  ```bash
  git -C /mnt/projects/fable/wt/<your slot> commit ...      # FIRST
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **There are THREE distinct ways to get a confident-looking wrong answer out of
  this script, and two of them share a signature:**
  1. **Omitting `--repo`.** It defaults to `.`, and an executor agent's shell
     cwd is **not** its slot (this project forbids `cd` and agent threads reset
     cwd between calls). Omitting it silently verifies
     `/mnt/projects/fable/main` and prints a loud FAIL full of some unrelated
     merged unit's lines. J02 hit exactly this.
  2. **The wrong base rev.** `fable..HEAD` is **WRONG**: units merge during a
     wave, so `fable` moves under you and `fable..HEAD` reports the *other*
     units' merged work, in reverse, as substantive removals. J01 saw 533
     spurious removals this way. Use the fork point.
     ⚠ **Failures 1 and 2 look IDENTICAL** — another unit's work appearing as
     removals — and have different causes. An executor who "fixes" the base and
     re-runs without `--repo` is still verifying the wrong tree. **Check both
     before changing a single line of code.**
  3. ⚠ **Running on an UNCOMMITTED tree.** The script prints
     **`No C++ files changed` and EXITS 0.** J06 hit this. It looks like a
     definitive pass and is not one. **Commit first, then verify.**
  Record `$BASE` in the Review and paste the full output.
- ⚠ **Expected verifier result for J12: EXIT 0, PASS, with boilerplate
  `+N / -0` and ZERO substantive lines in either direction.** Derived from what
  this plan prescribes and nothing else (correction 19): the preamble this plan
  tells you to copy is lines 1-21, **every line of which the `BOILERPLATE`
  regex matches** (`#include` ×19, `namespace eqt {`); the tail is
  `} // namespace eqt`, matched by `\}\s*(//.*)?$`; the new file-header comments
  are matched by `//`; blank lines are dropped by the normalizer before
  comparison. **Nothing else in this unit adds or removes a line.** So `N` =
  (19 includes + 1 namespace open + 1 namespace close + your header comment
  lines) × 2 new TUs, plus the stays file's header if you add one.
  **Any substantive line at all is a real finding — chase it, and NEVER contort
  code to force a green exit** (J04 correctly refused to).
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares normalized line *counts* but allowlists a bare `}` and every comment,
  so a wholly dropped block's braces are invisible to it (correction 23). This
  check is not optional.
  ```bash
  # after: the three TU bodies, each stripped of its new header comment, of the
  # copied 21-line preamble, and of the 1-line namespace close.
  cat <the three stripped bodies> | sort > /tmp/after.txt
  git show "$BASE:src/client/eq_action_handler.cpp" \
    | sed -n '23,2246p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  This unit creates no cross-TU header and relocates nothing into an existing
  file, so **neither correction 7 nor correction 20 applies** — the diff should
  be **empty, with no exceptions at all.**
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  three extracted ranges **in original order** (`23-777, 778-1318, 1319-2246`)
  and confirm the result is byte-identical to
  `git show "$BASE:src/client/eq_action_handler.cpp" | sed -n '23,2246p'`.
  **2,224 lines.** Expect `cmp` exit 0.
  ⚠ **Declare count variables `local` in your proof helpers.** More generally
  (correction 20): **when a proof fails, suspect the proof first.** J05's
  non-`local` `n` produced a 247-line shortfall that read exactly like a dropped
  range; J09's one proof failure was range arithmetic against the produced
  files' numbering; J08's failed exactly at its relocation seam. **Three units,
  three proof bugs, zero real defects.**
- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report** rather than re-blessing.
  - ⚠ **A gate FAIL is not necessarily a PIXEL fail — check WHICH metric
    failed** (correction 15). J05's first run failed on `befallen` **p95 CPU
    11.684 vs a 10.948 ceiling with ZERO image mismatches**; the re-run measured
    4.676. That is a 2.5x swing on a 2x ceiling under wave load: **contention,
    not your change. Re-run.**
  - ⚠ **A failure at the RENDER step is a different animal**: it means a
    `gles2_zone_benchmark` CMake add-site was missed. **This unit has no
    `gles2_zone_benchmark` site** (its only site is `CLIENT_CORE_SOURCES`), so a
    render-step failure here would be a genuine surprise — report it, do not
    re-run past it.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ Correction 14: the wave-stretch figures on record are **worst cases
  observed, not forecasts.** J06's `ctest -j6` ran in 120.7 s — faster than
  J01's solo 3.0 min on a larger suite — and its ARM build completed first
  invocation. Do not report a clean run as an anomaly.
- `scripts/refactor/check_file_sizes.sh --files` on **every changed AND created
  file**: `src/client/eq_action_handler.cpp`,
  `src/client/eq_action_handler_spells_trade.cpp`,
  `src/client/eq_action_handler_extended.cpp` → **0 warn, 0 fail**. The root
  `CMakeLists.txt` WARN is pre-existing and grandfathered; it is not yours.
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **2,248** this plan measured, and any
   range drift. The unit table's 2,202 is stale — say which number you found.
2. **The four zero-hazard properties, re-verified with your own greps and
   pasted:** zero file-scope statics, zero anonymous namespaces, zero
   preprocessor directives, zero block comments. If any has changed since
   `136a319b`, **STOP and report** — the unit changes shape.
3. ⚠ **Correction 21, explicitly.** The batch's first build-step plan defect was
   a file-local helper assigned to a range different from its sole consumer's.
   **This file has no file-local helpers at all**, so there is nothing to check
   — but **say that you checked, and paste the grep**, rather than citing this
   sentence.
4. **The blank-line-before-banner check**, pasted, confirming **exactly one**
   hit (line 1691) and that you did not cut there.
5. **Every file-header comment line you wrote**, enumerated — the unit's only
   non-move content.
6. **How many CMake add-sites you actually found**, against the ONE this plan
   names.
7. **Both content proofs**, pasted, and the verifier's exit code plus `$BASE`.
   State whether the verifier exited 0 as predicted; if it did not, enumerate
   every flagged line before concluding anything.
8. **Final line counts of all three files**, method counts per TU (this plan
   predicts 68 / 21 / 85 = 174), wall clock, slowest step, and whether
   concurrency with J13 stretched the verification tail.
9. **Findings only, not fixes.** ⚠ `docs/module_map.md:24` records this file as
   **2,205 lines** and as one TU; it needs a re-count and two new rows. That is
   **comment/doc rot, outside a move-only commit's scope** — fold it into the
   `docs/module_map.md` hygiene pass that J02, J04, J06, J07, J09 and J11
   already owe (**J21**). **Record. Do not act.**
   ⚠ **And do NOT attribute any stale `tests/` line-number citation to this
   split** (correction 24). Three units in a row wasted effort here: J06, J07
   and J08 each checked their file's citations **at their base commit** and
   found every one already wrong before the split. **They are pre-existing doc
   rot owned by J21, not this unit's problem.** If you find one, record the line
   and move on.

## Territory

`src/client/eq_action_handler.cpp`, the two new TUs beside it
(`src/client/eq_action_handler_spells_trade.cpp`,
`src/client/eq_action_handler_extended.cpp`), `CMakeLists.txt` (the one
`CLIENT_CORE_SOURCES` site), and this file. **Nothing else** — **not
`include/client/eq_action_handler.h`** (this unit changes no interface and
promotes nothing), not `src/client/eq.cpp` or any other `eq_*.cpp`, not
`src/client/action/`, not any `tests/*.cpp`, not `tests/cmake/*.cmake`, not
`docs/`.

**Correction 10 pre-check, already run for you and to be re-run at your tip:**

```bash
grep -rn 'eq_action_handler' tests/ scripts/
```

At `136a319b` this returns **~40 hits and ZERO functional readers of this file
as text.** Every hit is either `#include "client/eq_action_handler.h"` (the
*header*, unaffected — sixteen test files) or a comment in one test naming
another test file (`test_eq_action_handler_trade.cpp` etc.). ⚠ **No test opens
`src/client/eq_action_handler.cpp` and scans it.** Confirmed against the
exhaustive list of source paths any test reads as text:

```bash
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

— **`eq_action_handler.cpp` does not appear.** (J11 was the cautionary case:
four tests read `irrlicht_renderer.cpp` as text and had to be repointed;
**three would have failed and one would have silently asserted nothing.** J13,
running beside you, has exactly that problem with `eq_bridge.cpp`. You do not.)

**Re-run both greps at your tip and paste them.** If a text reader has appeared,
add it to territory and repoint it — that is not scope creep, it is correction
10, and it is not optional.

## STOP conditions

- A file-scope `static`, an anonymous namespace, a preprocessor directive or a
  block comment has appeared in the body since `136a319b`.
- Any cut boundary does not match §Every cut boundary at your tip and the drift
  is more than a few lines of uniform offset.
- The verifier reports **any** substantive line.
- Either content proof fails after you have satisfied yourself the proof itself
  is correct.
- The visual gate fails on an **image mismatch** (as opposed to the CPU p95
  metric).
- You find yourself wanting to cut at line 1691, or to add a fourth TU.

---

## Review (executor, slot3, 2026-09-02)

**Branch `task/J12-action-handler-split`, cut from `fable` at
`beaffdf404b3ffbcecab1235e2fe4fe1402f5b54`. Commit `9c36a801`.
`$BASE = beaffdf404b3ffbcecab1235e2fe4fe1402f5b54` (the fork point equals the
dispatch tip — no other unit merged during this run).**

**Result: the whole bar passed, and the exit-0 verifier prediction held exactly,
down to the predicted line count. Zero range drift. No STOP condition fired.**

### 1. Line count and range drift

`wc -l src/client/eq_action_handler.cpp` = **2,248** at my tip — the plan's
re-measured figure, not the unit table's stale 2,202. **Zero drift**: all 26
banner line numbers, the preamble (1-22), the body (23-2246), the tail
(2247 blank + 2248 `} // namespace eqt`) and all three cut boundaries
reproduced exactly as the plan states. Nothing had to be moved.

The plan's arithmetic `22 + 755 + 541 + 928 + 2 = 2248` held on the nose:

```
wc: preamble=22 a=755 b=541 c=928
```

### 2. The four zero-hazard properties, re-verified at my tip

```
$ grep -n '^[[:space:]]*static ' src/client/eq_action_handler.cpp
(no output, exit 1)

$ grep -n 'namespace' src/client/eq_action_handler.cpp
21:namespace eqt {
2248:} // namespace eqt

$ awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/{print NR": "$0}' src/client/eq_action_handler.cpp
(no output)

$ grep -n '/\*' src/client/eq_action_handler.cpp
(no output)
```

Zero file-scope statics, zero anonymous namespaces (the only two `namespace`
tokens are the file's own open/close), zero preprocessor directives, zero block
comments. **None has changed since `136a319b`.** Also re-confirmed **174**
method definitions and that none has a multi-line signature:

```
$ grep -n 'EqActionHandler::' src/client/eq_action_handler.cpp | grep -v '^[0-9]*:[A-Za-z_]'
(no output)
```

Comment depth at every cut, computed rather than asserted:

```
$ awk 'BEGIN{d=0} { n=gsub(/\/\*/,"&"); m=gsub(/\*\//,"&"); d+=n-m;
        if(NR==22||NR==777||NR==1318||NR==2246) print "depth after line "NR" = "d }' \
      src/client/eq_action_handler.cpp
depth after line 22 = 0
depth after line 777 = 0
depth after line 1318 = 0
depth after line 2246 = 0
```

### 3. Correction 21, checked rather than cited

There are no file-local helpers of any kind, so no helper can be separated from
its consumer. The grep that shows it — every column-0 declaration in the file
other than the namespace open is an `EqActionHandler::` member:

```
$ grep -nE '^[A-Za-z_~]' src/client/eq_action_handler.cpp | grep -v 'EqActionHandler::'
21:namespace eqt {
```

That is the whole list. Combined with the zero-statics grep above, correction 21
has **no exposure in this unit**, as the Wave-4 planning pass predicted.

### 4. The blank-line-before-banner check

```
$ awk '/^\/\/ ={4,}/{ if (prev != "") print NR": NO BLANK BEFORE: "$0 } {prev=$0}' \
      src/client/eq_action_handler.cpp
1691: NO BLANK BEFORE: // ========== Raid leader-only ops (T181) ==========
```

**Exactly one hit, line 1691, as predicted. I did not cut there.** The cuts are
at 778 and 1319, both of which satisfy J03's rule (blank line at 777 / 1318,
predecessor `}` closing `scribeItem` / `setInspectMessage`) *and* the banner
rule. 1691 and its 23-line T181 prose block ride into
`eq_action_handler_extended.cpp` untouched, in the middle of that TU.

**No other place in this file has the two rules disagreeing** — the awk above is
exhaustive over the banners, and the two rules are only in tension at a banner.
I did not want a fourth TU: the three-way split already clears the ceiling with
the largest TU at 957.

⚠ Worth adding to the template: `grep -c '^// ={4,}' <file>` returns **0**, not
26, because `{4,}` is not an interval in POSIX BRE. The awk form in the plan is
correct; anyone cross-checking it with plain `grep -c` will conclude the banners
have vanished. Use `grep -cE`.

### 5. Every file-header comment line written (the unit's only non-move content)

**Fifteen lines total, 5 per file**, all placed above line 1 of the copied
preamble. Nothing else in any of the three files is new, and no existing comment
was altered — the T95 block and the T181 block are byte-identical to their
originals (proven by §7).

`src/client/eq_action_handler.cpp` (5):

```
// eq_action_handler.cpp -- EqActionHandler: the core IActionHandler domains.
// Movement, Combat, Combat (T102), Interaction, Chat, Group, Character
// State and Inventory. The spell/trade/pet/tradeskill domains live in
// eq_action_handler_spells_trade.cpp; the post-T102 extended families in
// eq_action_handler_extended.cpp. Split by refactor/J12_eq_action_handler_split.md.
```

`src/client/eq_action_handler_spells_trade.cpp` (5):

```
// eq_action_handler_spells_trade.cpp -- EqActionHandler: the Spellbook,
// Trade, Pet and Tradeskill domains, moved verbatim out of
// eq_action_handler.cpp. Note the file's own grouping quirk: setAppearance
// and setInspectMessage sit under the Tradeskill banner and stay there.
// Split by refactor/J12_eq_action_handler_split.md.
```

`src/client/eq_action_handler_extended.cpp` (5):

```
// eq_action_handler_extended.cpp -- EqActionHandler: the extended action
// families the file grew after T102 -- Extended Movement / Character State /
// Combat, Entity Query, Utility, Social, Guild, Raid, Merchant, Find and GM.
// Moved verbatim out of eq_action_handler.cpp.
// Split by refactor/J12_eq_action_handler_split.md.
```

### 6. CMake add-sites: ONE, exactly as the plan named

```
$ grep -rn 'eq_action_handler\.cpp' CMakeLists.txt tests/CMakeLists.txt tests/cmake/
CMakeLists.txt:402:    src/client/eq_action_handler.cpp
```

Both new TUs added immediately beside it inside `CLIENT_CORE_SOURCES`. **Zero
sites in `tests/CMakeLists.txt` and zero in `tests/cmake/`** — the sixteen
`test_eq_action_handler_*` targets link `willeq_core` and include only the
header, confirmed by the grep above returning nothing under `tests/`.

**Correction 10 pre-check, both greps, re-run at my tip:**

```
$ grep -rn 'eq_action_handler' tests/ scripts/          # 109 hits
```

All 109 are either `#include "client/eq_action_handler.h"` (the *header*) or
comments in one test naming another test file. **Zero functional readers.** And
the exhaustive text-reader enumeration:

```
$ grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

returns 16 paths — `eq.h`, `renderer_event_receiver.cpp`, `renderer_frame.cpp`
(×3), `renderer_intents.h`, `eq_bridge.cpp` (×2), `event_bus.h`,
`action_dispatcher.h`, `action_dispatcher.cpp`,
`renderer_command_catalog.cpp`, plus a `src/client/graphics` directory scan —
and **`eq_action_handler.cpp` is not among them.** No repoint needed; territory
unchanged.

### 7. Both content proofs, and the verifier

**Proof A — byte-identical re-concatenation, run against the PRODUCED files**
(not the scratch extracts, so it also proves the assembly step). Each produced
file stripped of its 5-line header, its 22-line copied preamble and its trailing
`} // namespace eqt` (plus, for `_extended`, the one blank line before it):

```
$ sed -n '28,782p' src/client/eq_action_handler.cpp              > s1   # 755
$ sed -n '28,568p' src/client/eq_action_handler_spells_trade.cpp > s2   # 541
$ sed -n '28,955p' src/client/eq_action_handler_extended.cpp     > s3   # 928
$ cat s1 s2 s3 > recat
$ git show "$BASE:src/client/eq_action_handler.cpp" | sed -n '23,2246p' > orig_body
$ wc -l orig_body recat
2224 orig_body
2224 recat
$ cmp orig_body recat ; echo $?
0
```

**2,224 lines, `cmp` exit 0.** (Correction 17/20 heeded — the extraction ran in
a single non-recursive shell with no reused counters, and I ran the proof twice:
once from the scratch ranges and once from the produced files, both exit 0.)

**Proof B — line multiset:**

```
$ cat s1 s2 s3 | sort > /tmp/after.txt
$ git show "$BASE:src/client/eq_action_handler.cpp" | sed -n '23,2246p' | sort > /tmp/before.txt
$ diff /tmp/before.txt /tmp/after.txt ; echo $?
0
MULTISET-OK
```

**Empty diff, no exceptions at all** — as the plan predicted, since this unit
creates no cross-TU header (correction 7 N/A) and relocates nothing into an
existing file (correction 20 N/A).

**Verifier — committed FIRST, then run, with `--repo`:**

```
$ BASE=$(git -C /mnt/projects/fable/wt/slot3 merge-base fable HEAD)
$ echo $BASE
beaffdf404b3ffbcecab1235e2fe4fe1402f5b54
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/slot3
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt

Files verified: 3
Boilerplate: +57 / -0 lines (allowed)

PASS: move-only (modulo boilerplate).
$ echo $?
0
```

**EXIT 0, PASS, +57 / -0 boilerplate, ZERO substantive lines in either
direction — the prediction held.** And the *number* is derivable from the plan
exactly as §Acceptance says (correction 19): (19 includes + 1 `namespace eqt {`
+ 1 `} // namespace eqt` + 5 header lines) × 2 new TUs = 52, **plus the stays
file's 5 header lines = 57.** No sign-off exception is needed; this is the third
clean-exit-0 unit of the batch after J06 and J08.

### 8. Final counts, method counts, wall clock

| file | lines | methods | check_file_sizes |
|---|---|---|---|
| `src/client/eq_action_handler.cpp` | **783** | 68 | ok |
| `src/client/eq_action_handler_spells_trade.cpp` | **569** | 21 | ok |
| `src/client/eq_action_handler_extended.cpp` | **957** | 85 | ok |
| total | 2,309 | **174** | **3 ok, 0 warn, 0 fail** |

**The method-count prediction 68 / 21 / 85 = 174 was exact.** The line counts
run 4 high against the plan's ~779 / ~565 / ~952 — **uniformly and entirely
explained by the 5 header lines minus 1**, because I used each body's own
trailing blank line as the separator before `} // namespace eqt` rather than
adding a second one. That is correction 12's usual off-by-a-small-constant, in
the predictable direction for once.

Full acceptance bar, every item by exit code:

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 143 first invocation**, exit 0 on re-invoke — see below |
| ELF scan after the 143 | **2 truncated binaries found**, deleted and rebuilt; second full `readelf -h` sweep 0/221 bad |
| `ctest -j6` | exit 8 — **3506/3507**, sole failure `AudioManagerTest.PreloadCommonSounds` (the documented ALSA host quirk). Zero new failures vs `refactor/test_baseline.txt`. No `HeadlessShutdownTest` flake this run. |
| `verify_move_only.py` | **exit 0, PASS** |
| re-concatenation `cmp` | **exit 0** (2,224 lines) |
| multiset `diff` | **exit 0** |
| `scripts/build-gles2-x86.sh` | exit 0, first invocation |
| `scripts/ci/visual_gate.sh` | **PASS**, first invocation. qeynos2/gfaydark/befallen p95 CPU 9.624 / 7.549 all under ceiling, zero image mismatches. |
| goldens untouched | `git status --porcelain` **empty** after the gate |
| `scripts/build-arm-noble.sh` | **exit 143 first invocation**, exit 0 on re-invoke; ARM ELF sweep 0/18 bad |
| `check_file_sizes.sh --files` (all 3) | **3 ok, 0 warn, 0 fail** |

**Wall clock ~55 min.** Slowest step by far: the x86 build (10 min ceiling hit +
~4 min resume + a third pass for the two truncated binaries ≈ 16 min), then ARM
(10 min + ~5 min), then `ctest -j6` at **275 s**. **The cut plus CMake plus both
proofs took under 4 minutes** — the ninth consecutive unit where the mechanical
work is free and the bar is the whole cost.

**Concurrency with J10 (slot1) and J21 (slot2) did stretch the tail**: both
600 s tool-ceiling hits are correction 6's and correction 23's wave behaviour,
not breakage. J21 is docs-only so it contributed little; J10 is a compile-heavy
unit in the same tree.

⚠ **The exit-143 trap fired for real, and correction 23's warning is right to be
emphatic.** After the 143 the re-invocation exited 0 with a completely clean log,
and **two binaries were still truncated behind it** —
`test_group_accept_gm_hideme` and `test_zone_position_convention`, both ~21 MB
of zeroes with `head -c 4` = `00000000` instead of `7f454c46`. Had I trusted the
exit 0, `ctest` would have died at discovery reporting one of them per full
suite run, exactly as J07 recorded. **The scan cost 3 seconds and saved two
275-second suite runs.** Worth noting that `readelf -h` is a slightly stronger
one-liner than the `head -c 4 | od` recipe in the plan and just as cheap; I ran
both.

### 9. Findings only, not fixes

- **`docs/module_map.md` needs two new rows and a re-count.** It records this
  file as **2,205 lines** and as one TU. Correct figures: `eq_action_handler.cpp`
  **783**, `eq_action_handler_spells_trade.cpp` **569**,
  `eq_action_handler_extended.cpp` **957**. **Recorded, not acted on** — J21's.
  (Note the module map has been rewritten since this plan was written and now
  reads "every split translation unit"; it is J21's territory either way and I
  did not open it for editing.)
- **No stale `tests/` line-number citation is attributable to this split**, and I
  did not go hunting for pre-existing ones (correction 24 — three units have
  already burned effort there). The 109 `tests/`+`scripts/` mentions of
  `eq_action_handler` are all header includes or cross-references to *other test
  files by name*; **none cites a line number in the source I split**, so this
  unit invalidates nothing. That is a genuinely cleaner position than J06/J07/J08
  found themselves in, and it is a property of this file, not of my care.
- **The dead-content ledger gains nothing here.** This file has no commented-out
  methods, no `#if 0`, and no retired-configuration gating — the first Wave-4
  file with a clean bill on all three.
- **One template nit, filed above in §4:** the plan's banner-count sanity check
  needs `grep -cE`, not `grep -c`, or it silently reports zero banners.

### 10. Deviations

**One, cosmetic and declared:** the plan's tail convention (§The split row
`2247-2248`, "blank + `} // namespace eqt`, replicated in each TU") would have
produced a double blank line in `eq_action_handler.cpp` and
`eq_action_handler_spells_trade.cpp`, whose bodies already end on the original's
blank line (777 / 1318). I used each body's own trailing blank as the separator
and appended only `} // namespace eqt`; `eq_action_handler_extended.cpp`, whose
body ends at `}` on 2246, got the explicit blank line. **All three files
therefore end `}` / blank / `} // namespace eqt`, identically.** This changes no
moved line and is invisible to both proofs (which strip the tail) and to the
verifier (`}` and blank lines are both boilerplate); it is the +4-vs-+5 line
delta noted in §8.

**No other deviation.** The branch name is `task/J12-action-handler-split` as
named at dispatch, not the `task/J12-eq-action-handler-split` in this plan's
header line.
