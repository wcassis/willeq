# J27 — Batch J Wave 6: split `src/client/eq_merchant.cpp` (1,932 → 3 TUs, largest ~702)

**Slot: assigned at dispatch. Branch `task/J27-eq-merchant-split` cut from `fable`.
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

`src/client/eq_merchant.cpp` — **1,932 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **1,932** |
| preamble | **1-34** — the 8-line F05 file-header comment, the include block, the P28 comment and a `WITH_AUDIO` group at 26-28. **Zero non-boilerplate lines.** |
| namespace | **none** — zero `namespace` tokens. Each TU ends with its last method's closing brace. |
| method definitions | **54** |
| largest method | **219** `EverQuest::SetupTradeManagerCallbacks` (678-896); 2nd **105** `ZoneProcessGMTraining` (1621-1725) |
| `// ====` banners | **4**, at 1590/1592 and 1800/1802 — two banner pairs, both inside range 1346-1932 |
| conditional preprocessor directives | 10 — 26/28 `WITH_AUDIO` (preamble); **1155/1160** (inside `ZoneProcessShopRequest`, range 678-1345); **1511/1516**, **1549/1554**, **1581/1586** (all inside range 1346-1932). **No cut lands inside any of them.** |
| file-scope `static` / anonymous namespaces | two `static constexpr` at **1820-1822** — see §Correction 21. **Zero anonymous namespaces, zero `static` functions.** |
| multi-line comment regions | **zero multi-line regions.** `grep -n "/\*"` returns six lines (141, 1082, 1087, 1092, 1103, 1596) and **every one is a single-line `/*…*/`** — correction 33's exact case. Comment depth is 0 on every line. |

⚠ **Correction 33: "no block comments" must mean "no MULTI-LINE comment
regions".** Single-line `/* … */` argument comments are irrelevant; what the
cutting rule needs is **comment depth 0 at every cut**. Compute it, do not
assert it, and beware the trap `eq_movement.cpp:18` set for the orchestrator's
own scanner: a `//` line containing the token `*/` makes a naive depth counter
go negative. Strip `//` tails before counting:

```bash
python3 - <<'EOF'
import re
L=open('src/client/eq_merchant.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {678,1346}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-34 | preamble: the F05 header comment, the include block, the P28 note, the `WITH_AUDIO` group | copied to each new TU, with 1-8 adapted | 34 |
| **35-677** | `ZoneProcessCharInventory` (35-…) / `ZoneProcessMoveItem` (56-95) / `ZoneProcessDeleteItem` / `ScribeSpellFromScroll` (253-326) / `ZoneProcessReadBook` (494-540) / `ZoneProcessTradePartnerItem` (582-676) — inventory movement, book/scroll and the incoming trade-partner packets | **stays** `eq_merchant.cpp` | **677** |
| **678-1345** | `SetupTradeManagerCallbacks` (**678-896, 219 lines, moved WHOLE**) / `ZoneProcessLootItemToUI` (898-1000) / `ZoneProcessLootedItemToInventory` (1002-1078) / `ZoneProcessShopRequest` (1106-1166, **with the `WITH_AUDIO` pair at 1155/1160 inside it**) / `ZoneProcessShopPlayerBuy` (1168-1219) / `ZoneProcessShopPlayerSell` (1221-1288) and the shop-end tail through 1345 — trade callbacks, looting and shopping | `eq_merchant_trade.cpp` | **702** |
| **1346-1932** | `ZoneProcessVendorItemToUI` (1346-1397) / the bank cluster (banner **1590/1592**) / `ZoneProcessGMTraining` (1621-1725) / the tradeskill-recipe cluster (banner **1800/1802**) with its two `static constexpr` at **1820-1822** and their readers at **1827, 1835** — vendor UI, bank, trainer and recipes | `eq_merchant_vendor_bank.cpp` | **621** |

**Result: 3 TUs** (`eq_merchant.cpp` stays and counts). Largest **~702**.

Arithmetic that must hold: **`34 + 643 + 668 + 587 = 1932`**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundaries fall where they do: **35-677** is the item-movement and
book/scroll side — what happens to an item already in your inventory.
**678-1345** is the *transaction* side: the `TradeManager` callback wiring, loot
transfer and shop buy/sell. **1346-1932** is the standing-institution side:
vendor UI, bank, GM trainer and tradeskill recipes, and it is where both banner
pairs and both `static constexpr` live.

⚠ **`SetupTradeManagerCallbacks` is 219 lines and is NOT to be decomposed.** It
lands in a 702-line TU with 798 lines of headroom.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 678 | `676: }` (end of `ZoneProcessTradePartnerItem`), `677` blank | `678: void EverQuest::SetupTradeManagerCallbacks()` |
| 1346 | `1344: }`, `1345` blank | `1346: void EverQuest::ZoneProcessVendorItemToUI(const EQ::Net::Packet &p)` |

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/eq_merchant.cpp
```

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `eq_merchant.cpp`:**

`eq_merchant.cpp` has **zero anonymous namespaces, zero `static` functions**, and
exactly **two file-scope symbols**:

| symbol | definition | readers | range holding the readers |
|---|---|---|---|
| `kTradeskillFavoritesMaxRecipes` | **1820** | **1822, 1835** | **1346-1932** ✅ |
| `kTradeskillFavoritesBodySize` | **1821-1822** | **1827** | **1346-1932** ✅ |

**Both definitions and all three readers fall inside the single range
1346-1932**, which this plan assigns wholly to `eq_merchant_vendor_bank.cpp`.
Correction 21 is satisfied by construction: **no internal header is needed and
nothing is promoted.**

Re-derive it yourself; do not cite this table:

```bash
grep -n '^static \|^const \|^constexpr \|^namespace' src/client/eq_merchant.cpp
grep -n 'kTradeskillFavorites' src/client/eq_merchant.cpp
grep -rn 'kTradeskillFavorites' src/ include/ tests/ | grep -v eq_merchant.cpp
```
(the third must return nothing — these are internal-linkage constants)

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `eq_merchant.cpp`'s preamble, lines 1-34, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

The preamble is **100% boilerplate** and contains no definition, no `extern` and
no non-`inline` file-scope symbol, so the copy is unconditional and total:
**lines 1-34 verbatim into each new TU**, including the `WITH_AUDIO` group at
26-28 and the P28 comment. Only lines 1-8 (the F05 header) are adapted — see
§File-header comments.

**There is no namespace wrapper.** Each new TU ends with its last method's
closing brace and nothing else.

## File-header comments

`eq_merchant.cpp` **already has** an 8-line F05 header describing the whole
file's contents — it enumerates the trade senders, the loot/vendor/bank/trade/
tradeskill/trainer `Setup*Callbacks`, GM training and the Phase 4 recipe
handlers. **After the split that text is actively wrong**: it would describe
three files' worth of content in the one file that no longer holds most of it.

**Each of the three TUs gets its own adapted version** — same shape, naming that
TU's clusters, citing `refactor/J27_eq_merchant_split.md` alongside the existing
`refactor/F05_merchant.md` reference. **The stays file's header must be updated
too.** **Change no other comment**, and in particular do not touch the P28 block.

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
grep -rn 'eq_merchant\.cpp' CMakeLists.txt tests/ scripts/
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
| `CMakeLists.txt:287` | `SET(CLIENT_CORE_SOURCES` (opens at `:265`) | ⚠ **YES — add both new TUs here** |

**No `tests/cmake/` fragment names this file.** **This unit does NOT touch
`gles2_zone_benchmark`.**

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'eq_merchant' tests/ scripts/
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
- ⚠ **Expected verifier result for J27: **EXIT 0, `+0/-0` substantive.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied preamble (1-34) is **100% boilerplate** — `#include`, `#ifdef`,
    `#endif`, `//`, and blanks the normalizer drops. **This is the difference
    from J10 (21 lines, two `typedef`s + an `extern`) and J11 (70 lines).**
  - Header-comment adaptations cost zero (`//`).
  - No namespace open/close to replicate; no internal header; no test repoint;
    no preamble deviation.
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
  cat <the 3 TU bodies, each stripped of its preamble and namespace close> \
      | sort > /tmp/after.txt
  git show "$BASE:src/client/eq_merchant.cpp" | sed -n '35,1932p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ This unit creates no cross-TU header and relocates nothing into an existing
  file, so **neither correction 7 nor correction 20 applies** and the diff should
  be **empty**.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`35-677, 678-1345, 1346-1932`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/eq_merchant.cpp" | sed -n '35,1932p'`.
  **1,898 lines. Expect `cmp` exit 0.**
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
  file**: `src/client/eq_merchant.cpp`, `src/client/eq_merchant_trade.cpp`, `src/client/eq_merchant_vendor_bank.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **4**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **1,932** this plan measured,
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
   (this plan predicts roughly 20 / 14 / 20 of the 54), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**

## Territory

`src/client/eq_merchant.cpp`, the two new TUs beside it
(`src/client/eq_merchant_trade.cpp`, `src/client/eq_merchant_vendor_bank.cpp`),
`CMakeLists.txt` (the one `CLIENT_CORE_SOURCES` site), and this plan file.
**Nothing else** — not `include/client/eq.h`, not `src/client/trade_manager.cpp`,
not any other `eq_*.cpp`, not any `tests/` file, not `tests/cmake/*.cmake`, not
`docs/`.

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

---

## Review (executed 2026-09-02, slot1, branch `task/J27-eq-merchant-split`)

**Branch tip at checkout / `$BASE`: `3c6c726cb274579fa9a7463818f60579b5f24f2a`**
("Batch J: fold J24's corrections -- the widened symbol grep has scored twice
running; make it the only pattern"). Not `824adb28` — the plan's survey commit —
but every §Measured property re-derived below matched it exactly.

**Result: 1,932 -> 677 / 702 / 621. Verifier exit 0, +0/-0 substantive,
prediction exact. Both content proofs exit 0. ZERO range drift on every line
number in this plan.**

### 1. Line count at my tip

`wc -l src/client/eq_merchant.cpp` = **1,932** — identical to the plan's survey
at `824adb28`. **No drift.** Correction 9's warning did not fire for this file;
it is the first Wave-6 file measured that did not move.

### 2. Every §Measured property, re-derived

| property | plan (`824adb28`) | measured at `3c6c726c` | drift |
|---|---|---|---|
| total lines | 1,932 | **1,932** | none |
| preamble | 1-34 | **1-34** (33 = `#include <vector>`, 34 blank, 35 = first definition) | none |
| namespace tokens | none | **none** (the widened grep below returns nothing) | none |
| method definitions | 54 | **54** | none |
| largest method | `SetupTradeManagerCallbacks` 678-896 | **678-896**, next definition at 898 | none |
| 2nd largest | `ZoneProcessGMTraining` 1621-1725 | **1621-1725**, next at 1727 | none |
| `// ====` banners | 4, at 1590/1592/1800/1802 | **4**, at **1590, 1592, 1800, 1802** | none |
| preprocessor directives | 10 | **10** (dump below) | none |
| file-scope statics | 2 `static constexpr` at 1820-1822 | **1820, 1821-1822** | none |
| multi-line comment regions | zero; six single-line `/*…*/` | **zero**; `/*` at **141, 1082, 1087, 1092, 1103, 1596**, all closed on their own line | none |

**No STOP condition fired.**

### 3. Correction 21 / 51 / 61 — re-derived, not cited

**The widened grep (correction 61) — every column-0 call-shaped line that is not
an `EverQuest::` method:**

```
$ grep -nE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' src/client/eq_merchant.cpp | grep -v 'EverQuest::'
(no output, exit 1)
```

**Positive control (correction 60/55 — proving the pattern matches before
believing the zero)** — the same pattern without the `EverQuest::` filter:

```
35:void EverQuest::ZoneProcessCharInventory(const EQ::Net::Packet &p)
56:void EverQuest::ZoneProcessMoveItem(const EQ::Net::Packet &p)
97:void EverQuest::ZoneProcessDeleteItem(const EQ::Net::Packet &p)
128:void EverQuest::SyncInventorySlotToGameState(int16_t slot, bool hasItem)
139:void EverQuest::SetupInventoryCallbacks()
```

So the pattern works and the file defines **zero namespace-scope free
functions** — J32/J24's shape has no exposure here.

The widened pattern cannot see a non-call declaration, so the narrow pattern is
still needed alongside it:

```
$ grep -n '^static \|^namespace\|^const \|^constexpr ' src/client/eq_merchant.cpp
1820:static constexpr size_t kTradeskillFavoritesMaxRecipes = 500;
1821:static constexpr size_t kTradeskillFavoritesBodySize =
```

**Definitions and readers, and the range this plan assigns each:**

```
$ grep -n 'kTradeskillFavorites' src/client/eq_merchant.cpp
1820:static constexpr size_t kTradeskillFavoritesMaxRecipes = 500;
1821:static constexpr size_t kTradeskillFavoritesBodySize =
1822:  sizeof(uint32_t) * 2 + sizeof(uint32_t) * kTradeskillFavoritesMaxRecipes;  // 2008
1827:  std::vector<uint8_t> body(kTradeskillFavoritesBodySize, 0);
1835:  size_t count = std::min(favorites.size(), kTradeskillFavoritesMaxRecipes);
```

| symbol | definition | line | assigned range | readers | reader lines | assigned range |
|---|---|---|---|---|---|---|
| `kTradeskillFavoritesMaxRecipes` | 1820 | 1820 | **1346-1932** | 2 | 1822, 1835 | **1346-1932** |
| `kTradeskillFavoritesBodySize` | 1821-1822 | 1821 | **1346-1932** | 1 | 1827 | **1346-1932** |

**Same range for every definition and every reader** — all of `1346-1932`, which
this plan assigns wholly to `eq_merchant_vendor_bank.cpp`. **No internal header
is needed and nothing is promoted.**

Tree-wide, outside the file:

```
$ grep -rn 'kTradeskillFavorites' src/ include/ tests/ | grep -v 'eq_merchant.cpp'
(no output, exit 1)
```

**Positive control for that grep** (same token, unfiltered, must match):
`src/client/eq_merchant.cpp:1820`, `:1822`, `:1835`. The zero is real — these are
internal-linkage constants with no external reader.

⚠ **Note for anyone re-running that grep AFTER this split:** `grep -v
'eq_merchant.cpp'` no longer excludes the symbols' new home, because
`eq_merchant_vendor_bank.cpp` does not contain the substring `eq_merchant.cpp`.
Post-split the correct filter is `grep -v 'eq_merchant'`. Not a defect — the
grep was prescribed against the pre-split tip and was run there.

### 4. Comment depth at every cut — computed, not asserted

The plan's `//`-stripping counter, extended to also report the final depth:

```
depth before line 35   = 0
depth before line 678  = 0
depth before line 1346 = 0
final depth = 0
```

All six `/*` occurrences (141, 1082, 1087, 1092, 1103, 1596) close on their own
line, so there is no multi-line comment region anywhere and **no cut can land
inside one**. Correction 33 satisfied by computation.

### 5. Preprocessor dump — no cut inside a conditional

```
$ awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' src/client/eq_merchant.cpp
26: #ifdef WITH_AUDIO
28: #endif
1155: #ifdef WITH_AUDIO
1160: #endif
1511: #ifdef WITH_AUDIO
1516: #endif
1549: #ifdef WITH_AUDIO
1554: #endif
1581: #ifdef WITH_AUDIO
1586: #endif
```

**Correction 60's positive control is satisfied intrinsically** — this file has
ten directives, so a non-empty result proves the `awk` pattern works. Region
assignment: 26/28 is preamble (copied whole into all three TUs); 1155/1160 lies
inside `678-1345`; 1511/1516, 1549/1554 and 1581/1586 all lie inside
`1346-1932`. **Neither cut (678, 1346) falls between an `#ifdef` and its
`#endif`.**

### Cut boundaries, with neighbours

```
--- cut at 678 ---
673:  }
674:
675:  return true;  // Handled as trade item
676: }
677:                                   <- blank
678: void EverQuest::SetupTradeManagerCallbacks()
679: {

--- cut at 1346 ---
1341:  }
1342:
1343:  m_vendor_items.clear();
1344: }
1345:                                  <- blank
1346: void EverQuest::ZoneProcessVendorItemToUI(const EQ::Net::Packet &p)
1347: {
```

Both are J03's rule exactly: **a blank line whose predecessor closes a method,
at comment depth 0.** Both match the plan's §Every cut boundary table to the
line.

### 6. File-header comment lines written — enumerated

The original 8-line F05 header (lines 1-8) was **replaced** in the stays file and
a new 8-line header written into each new TU. **Lines 9-34 — the blank, the
whole include block, the P28 comment and the `WITH_AUDIO` group — are copied
byte-for-byte, unmodified, into all three TUs.** No other comment in the file was
touched, and the P28 block is untouched.

Correction 62 check performed **before** writing: this plan's acceptance
prescribes `grep -cE '^// ={4,}'` expecting 4. **No header line contains `====`**
(asserted programmatically in the split script), so the banner count is not
perturbed by the headers.

**`src/client/eq_merchant.cpp` (replacing old 1-8):**
```
// EverQuest inventory item movement, scroll/book and trade-request cluster.
//
// Split out of eq.cpp (Batch F, unit F05), then split again (Batch J, unit
// J27): CharInventory/MoveItem/DeleteItem processing, the inventory
// callbacks, ScribeSpellFromScroll, the trade request/coins/accept/cancel
// senders, ReadBook and the incoming TradePartnerItem packet. Moved
// verbatim, no behavior change. See refactor/F05_merchant.md and
// refactor/J27_eq_merchant_split.md.
```

**`src/client/eq_merchant_trade.cpp` (new):**
```
// EverQuest trade-callback, loot and shop transaction cluster.
//
// Split out of eq_merchant.cpp (Batch J, unit J27), which was itself split
// out of eq.cpp (Batch F, unit F05): SetupTradeManagerCallbacks and the
// loot/vendor/bank/trade-window/tradeskill Setup*Callbacks, the loot
// transfer handlers, and the shop request/buy/sell/money/end handlers.
// Moved verbatim, no behavior change. See refactor/F05_merchant.md and
// refactor/J27_eq_merchant_split.md.
```

**`src/client/eq_merchant_vendor_bank.cpp` (new):**
```
// EverQuest vendor-window, bank, GM trainer and tradeskill-recipe cluster.
//
// Split out of eq_merchant.cpp (Batch J, unit J27), which was itself split
// out of eq.cpp (Batch F, unit F05): the vendor UI packet and the vendor
// open/buy/sell/close senders, the bank window, the trainer callbacks with
// GM training, and the Phase 4 tradeskill recipe senders/handlers. Moved
// verbatim, no behavior change. See refactor/F05_merchant.md and
// refactor/J27_eq_merchant_split.md.
```

**24 header lines written in total** (8 x 3), all `//`, all allowlisted
boilerplate. **8 old header lines removed** from the stays file.

Correction 57's shape, stated explicitly: **`8 (header) + 26 (preamble lines
9-34, verbatim) + body`**. That constant is **34**, and it is what produces
`34 + 643 = 677`, `34 + 668 = 702`, `34 + 587 = 621`. The plan's predicted
677/702/621 were derived on the same convention and came out **exact**.

### 7. CMake add-sites, and the tests/scripts greps

**ONE add-site, as the plan predicted — but the line numbers are stale, for the
FOURTH consecutive unit (correction 59).**

Derived with `find … -print0 | xargs -0 grep -Hn` (correction 55 — never
`grep -rn --include=`), across every `CMakeLists.txt` and `*.cmake` in the tree
with `build*` pruned:

```
/mnt/projects/fable/wt/slot1/CMakeLists.txt:289:    src/client/eq_merchant.cpp
```

**Positive control for that find/grep** (correction 55): the same command for
`eq_bridge` returns `CMakeLists.txt:274,275,276` plus two `tests/cmake/`
comment lines — the pattern demonstrably matches, so the single hit is real.

| | plan | measured | drift |
|---|---|---|---|
| add-site count | **1** | **1** | none |
| target / variable | `SET(CLIENT_CORE_SOURCES` | **`SET(CLIENT_CORE_SOURCES`** | **name correct** |
| `SET(` opener line | `:265` | **`:267`** | **+2 stale** |
| `eq_merchant.cpp` line | `:287` | **`:289`** | **+2 stale** |

Correction 59's pattern holds again: **target name right, both line numbers
wrong.** No `tests/cmake/` fragment names this file; `gles2_zone_benchmark` is
untouched.

**Both `tests/`/`scripts/` greps (correction 10) — run at my tip, pasted:**

```
$ grep -rn 'eq_merchant' tests/ scripts/
tests/test_eq_action_handler_trade.cpp:12:// no-op harmlessly in this configuration (verified by reading eq_merchant.cpp
tests/test_eq_commands_equip.cpp:4:// quantity) (eq_merchant.cpp, opcode HC_OP_MoveItem=0x420f) was fully
tests/test_command_processor_senders.cpp:100:// All four recipe senders in eq_merchant.cpp had ZERO production callers
tests/test_state_modules.cpp:1060:// EverQuest::SyncInventorySlotToGameState() in eq_merchant.cpp calls these
tests/test_combat_manager.cpp:720:// ZoneProcessLootItemToUI (eq_merchant.cpp) -- or, when a slot request is
tests/test_wire_layout_t145.cpp:156:// eq_merchant.cpp sends our 24-byte struct on HC_OP_ShopPlayerBuy and our
```

**Six hits, all `//` comments, zero functional readers.** The plan's
"comment-only hits and zero functional readers" claim is **correct for this
file** (unlike J35's, where it was wrong in the other direction).

```
$ grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
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

**`eq_merchant.cpp` does not appear** — no test opens it as text, at any of the
three guard shapes (`readFile`, string-compare exclusion, or path constant).
**No repoint needed. No `tests/` file touched.**

### 8. Both content proofs, and the verifier

`body(TU)` is defined **structurally** (J23/J35's method, correction 20): "from
the first line matching `^[A-Za-z_][…]*EverQuest::` to EOF". No offset constant
appears anywhere in the proof script, so no arithmetic slip can silently shorten
a range. The script re-derives each file's preamble length from that boundary
and prints it — all three came back **34**, independently confirming the
`8 + 26` convention actually landed.

```
BASE = 3c6c726cb274579fa9a7463818f60579b5f24f2a
original total lines = 1932
original body lines  = 1898
--- produced files ---
  src/client/eq_merchant.cpp                 total= 677  preamble=34  body= 643
  src/client/eq_merchant_trade.cpp           total= 702  preamble=34  body= 668
  src/client/eq_merchant_vendor_bank.cpp     total= 621  preamble=34  body= 587
PROOF 1 byte-identical re-concatenation: PASS
  concat bytes=70199 orig bytes=70199  concat lines=1898 orig lines=1898
PROOF 2 line-multiset: MULTISET-OK
```

**Proof 1** — the three extracted bodies concatenated in original order
(`35-677`, `678-1345`, `1346-1932`) are **byte-identical** to
`git show "$BASE:src/client/eq_merchant.cpp" | sed -n '35,1932p'`: 70,199 bytes
and 1,898 lines on both sides. The plan predicted 1,898 lines and `cmp` exit 0.
**Exact.**

**Proof 2** — line multiset over the same 1,898 lines: **identical**. Nothing
duplicated into two TUs, nothing dropped from both.

**Verifier** (committed FIRST, then run, with `--repo`, from the merge-base):

```
$ BASE=$(git merge-base fable HEAD)   # 3c6c726cb274579fa9a7463818f60579b5f24f2a
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" \
      --repo /mnt/projects/fable/wt/slot1
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
  refactor/J27_eq_merchant_split.md

Files verified: 3
Boilerplate: +65 / -7 lines (allowed)

PASS: move-only (modulo boilerplate).
VERIFIER_EXIT=0
```

**Prediction was EXIT 0, +0/-0 substantive. Result: matched exactly.** The
derivation held: the copied preamble (lines 9-34) is 100% `#include`, `#ifdef`,
`#endif`, `//` and blanks — every line the normalizer drops — so duplicating it
x3 costs nothing; the 24 new header lines and 8 removed ones are all `//`; and
with no namespace wrapper, no internal header, no promoted symbol and no test
repoint, there was nothing else to pay for. **No sign-off needed.** This is
correction 54's rule seen from the other side: nothing's text or count changed
in the normalized view.

All three verifier-avoidance traps were avoided deliberately: `--repo` passed
explicitly (an agent shell's cwd is not its slot), `merge-base` used rather than
`fable..HEAD`, and the run made **after** the commit (an uncommitted tree prints
"No C++ files changed" and exits 0).

### 9. Final counts, methods, and timing

| file | lines | headroom to 1,500 | methods |
|---|---|---|---|
| `src/client/eq_merchant.cpp` | **677** | 823 | **21** |
| `src/client/eq_merchant_trade.cpp` | **702** | 798 | **13** |
| `src/client/eq_merchant_vendor_bank.cpp` | **621** | 879 | **20** |
| total | 2,000 | | **54** |

Headroom **computed, not quoted** (correction 58). 1,932 -> 2,000 is +68: the
two extra 34-line preambles. Largest TU **702**, exactly as predicted.

**Method counts: predicted 20/14/20, actual 21/13/20** — correction 63's
"indicative only" holds for a sixth consecutive unit, though the miss is small
and self-cancelling. The counting pattern is stated, per correction 63:
`grep -cE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*EverQuest::[A-Za-z_0-9]+\('`, which
totals **54** across the file and matches §Measured. (This file defines no
constructor, so correction 63's constructor blind spot does not bite.)

**Acceptance, every item by exit code:**

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** |
| binary/archive scan (`readelf -h` + `ar t`, correction 56) | **221 binaries + 2 archives, 0 BAD** |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| `ctest -j6` | **EXIT=8**: 3,507 tests, **1 failure — `AudioManagerTest.PreloadCommonSounds`**, the documented ALSA host exception. **Zero new failures vs `refactor/test_baseline.txt`.** |
| `visual_gate` (in-suite) | **Passed, 22.00 s** |
| goldens | **untouched** — `git status --porcelain` shows only the 2 modified + 2 new files |
| `scripts/build-arm-noble.sh` | **EXIT=0** |
| `check_file_sizes.sh --files` (all 3) | **3 ok, 0 warn, 0 fail** |
| verifier | **exit 0, +0/-0** |
| both content proofs | **PASS / MULTISET-OK** |

**Gate ordering — correction 41's FIX, not the workaround.**
`scripts/build-gles2-x86.sh` was run **before** `ctest -j6`, and the stamps were
checked rather than assumed: `build-gles2/bin/gles2_zone_benchmark` is stamped
**15:23:59**, the split edits **14:53:22**. The in-suite gate therefore
exercised **this unit's own build**. No standalone re-run was needed.

`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` did **not**
fire. No `ctest` discovery failure, no exit-143 build, no truncation.

**Wall clock ~55 min end to end.** Slowest step by a wide margin: **`cmake
--build build -j24`, ~9.5 min** — it exceeded the 600 s tool ceiling and was
**backgrounded by the tool rather than killed** (correction 23's benign case:
its own exit file recorded `EXIT=0`, no SIGTERM). The binary scan was run anyway
and found nothing, per the plan's "it costs seconds". `ctest -j6` **244 s**;
gles2 and ARM builds both under the ceiling, first invocation. **Wave concurrency
did stretch the main build** but no step needed a retry, and correction 14
applies: this is not an anomaly to report as one.

### Deviations and cosmetic artefacts

- **Correction 30's tail artefact is present and NOT hand-fixed, as instructed.**
  Ranges `35-677` and `678-1345` each end on the blank line that separated the
  next method, so `eq_merchant.cpp` and `eq_merchant_trade.cpp` each end with a
  blank line after their last `}`. `eq_merchant_vendor_bank.cpp` does not (1932
  is the final `}`). Including those blanks is **required** by the
  byte-identical proof — removing them would break it. Cosmetic, invisible to
  both proofs and the verifier, declared rather than fixed.
- ⚠ **A FILE-MODE TRAP THE TEMPLATE DOES NOT COVER, caught and corrected before
  sign-off — the INVERSE of J01's.** `src/client/eq_merchant.cpp` was
  **`100755`** in the tree at base (a pre-existing anomaly: **3 of the 82 files
  under `src/client/` are 755**). The split script wrote all three files and
  `chmod 644`'d them, per the plan's "chmod 644 every created file" rule — which
  is right for the two **created** TUs and **wrong for the modified stays
  file**, where it recorded a spurious `100755 => 100644` in the first commit.
  Restoring `755` on the stays file and amending removed it; the two new TUs
  remain `644`. **The rule needs a second half: `chmod 644` every CREATED file,
  and PRESERVE the original mode on the file that stays.** Normalising the stays
  file to 644 would be a drive-by fix, however tempting — the 755 bit is
  pre-existing and belongs to whoever cleans up all three.
- **No other deviation.** Nothing decomposed, nothing deleted, no include list
  trimmed, no TU added beyond the two this plan names, no file touched outside
  §Territory.

### 10. Findings only, not fixes

- **`docs/module_map.md` needs two new rows** — `src/client/eq_merchant_trade.cpp`
  and `src/client/eq_merchant_vendor_bank.cpp` — and its existing
  `eq_merchant.cpp` row now describes a third of what it used to. **Recorded,
  not acted on** (out of territory).
- **`tests/` doc rot, split honestly per J31's distinction.** All six comment
  hits were checked line by line against the base commit:
  - **Accurate at base, now pointing at the wrong TU — attribute to J27, four
    sites:** `test_command_processor_senders.cpp:100` (the four recipe senders
    are now in `eq_merchant_vendor_bank.cpp`), `test_combat_manager.cpp:720`
    (`ZoneProcessLootItemToUI` is now in `eq_merchant_trade.cpp`),
    `test_wire_layout_t145.cpp:156` (`HC_OP_ShopPlayerBuy` is now in
    `eq_merchant_trade.cpp`), and `test_eq_action_handler_trade.cpp:12` (the
    trade-manager callbacks are now in `eq_merchant_trade.cpp`).
  - **Still accurate after the split, no action — two sites:**
    `test_state_modules.cpp:1060` (`SyncInventorySlotToGameState` stays at
    `eq_merchant.cpp:128` pre-split, and stays in the stays file) and
    `test_eq_commands_equip.cpp:4` (`HC_OP_MoveItem` / `ZoneProcessMoveItem`
    likewise stays).
  None of these is a functional guard — all six are prose comments, so nothing
  can fail. **Queue the four against J27, not against pre-existing rot.**
- **No dead content found** in this file — every one of the 54 definitions is a
  declared `EverQuest` member. No new dead-content site to add to the batch's
  list of eight.
- **No file-scope symbol, anonymous namespace or multi-line comment region
  appeared** anywhere §Measured said there was none. No STOP condition fired at
  any point.

### What the template still gets wrong

1. **Correction 59 is now FOUR consecutive units, not three.** Both root-CMake
   line numbers in this plan were `+2` stale while the target name and the count
   were right. The advice ("name targets, never cite CMake line numbers") is
   correct but plans keep citing them anyway — this plan's §CMake table has a
   `file:line` column that *invites* the stale number. **Drop the column;** keep
   the variable name and the count.
2. **The correction-21 grep needs BOTH patterns, and the template now implies
   one.** Correction 61 says the widened pattern should be "the ONLY pattern".
   That is right for *functions* and wrong as stated: the widened pattern is
   call-shaped (`…\(`) and **cannot match `static constexpr size_t kFoo = 500;`**
   — this file's only two file-scope symbols. Had I obeyed correction 61
   literally I would have concluded the file has zero file-scope symbols and
   never checked where the two constants' readers land. **The rule should be
   "the widened pattern REPLACES `^static |^namespace` for finding free
   functions, and is run ALONGSIDE it, not instead of it."**
3. **Correction 62's class fired again, in a shape the correction does not
   name — a grep whose *filter* stops working after the split.** This plan's
   §Correction 21 prescribes `grep -rn 'kTradeskillFavorites' … | grep -v
   eq_merchant.cpp` and asserts it "must return nothing". True at the tip, but
   the split creates `eq_merchant_vendor_bank.cpp`, which the `-v` filter does
   **not** exclude, so the same command run afterwards returns three hits that
   read exactly like a leaked internal-linkage constant. Correction 62 covers
   "a header mentioning a scanned token"; **this is the sibling case — an
   exclusion pattern invalidated by the unit's own new filenames.** Plans should
   filter on the stem (`eq_merchant`), not the full basename.
4. **§The split's range table has an arithmetic typo.** It states
   `34 + 643 + 668 + 587 = 1932` as the check that must hold — correct — but the
   table's own ≈lines column gives the stays range as **677**, which is
   `34 + 643`, i.e. the *finished file size*, not the *range length* the sum
   uses. The other two rows (702, 621) are likewise finished-file sizes. The
   sum and the column are measuring different things and happen to be labelled
   the same. Harmless here because both are right, but it is the same
   prose-vs-table shape as corrections 19/47/64. **State the column as
   `34 + range` explicitly, as §Includes' arithmetic sentence does.**
5. **Correction 60's positive control should be prescribed for the `awk`
   preprocessor dump generically, not only where the file has zero directives.**
   This file has ten, so the control is intrinsic — but the plan does not say
   that, and an executor with a file of ten directives has no cue to check the
   pattern at all. One sentence ("a non-empty dump is its own positive control;
   an empty one needs an external check") closes it.
6. **The `chmod 644` rule is half a rule.** See the mode trap under
   §Deviations: applied to the *stays* file it manufactures a spurious
   `100755 => 100644` in a commit that is supposed to change nothing but line
   locations. Every remaining J unit whose source file is one of the tree's
   stray 755s will hit this. **Amend the standing rule to: `chmod 644` every
   CREATED file; preserve the original mode on the file that stays.**
7. **What the plan got exactly right, worth keeping:** the `(header) + constant +
   body` size shape (correction 57) predicted all three files to the line; the
   structural `body(TU)` definition (correction 20/J23) again made the proofs
   unable to lie; and §Correction 21's pre-audit was correct on the substance
   even though its grep filter does not survive the split.
