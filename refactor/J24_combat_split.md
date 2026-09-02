# J24 — Batch J Wave 6: split `src/client/combat.cpp` (2,119 → 3 TUs, largest ~809)

**Slot: assigned at dispatch. Branch `task/J24-combat-split` cut from `fable`.
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

`src/client/combat.cpp` — **2,119 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **2,119** |
| preamble | **1-49** — 14 include lines, then **`#pragma pack(1)` (17), three packet structs (18-47), `#pragma pack()` (48)** |
| namespace | **none** — zero `namespace` tokens in the file. Each TU ends with its last method's closing brace. |
| method definitions | **77** |
| largest method | **192** `CombatManager::FindNextHuntTarget` (1360-1551); 2nd **108** `ListHuntTargets` (1798-1905) |
| `// ====` banners | **zero** |
| conditional preprocessor directives | 6 — 7/9 `WITH_AUDIO` (preamble); **509/513** and **545/549** `WITH_AUDIO`, both inside `EnableAutoAttack` / `DisableAutoAttack`, both inside range 50-789 |
| file-scope `static` / anonymous namespaces | **zero `static`, zero anonymous namespaces.** Three file-local `struct`s at 18-47 — see §Correction 21. |
| multi-line comment regions | **zero** — `grep -n "/\*"` returns nothing at all. |

⚠ **Correction 33: "no block comments" must mean "no MULTI-LINE comment
regions".** Single-line `/* … */` argument comments are irrelevant; what the
cutting rule needs is **comment depth 0 at every cut**. Compute it, do not
assert it, and beware the trap `eq_movement.cpp:18` set for the orchestrator's
own scanner: a `//` line containing the token `*/` makes a naive depth counter
go negative. Strip `//` tails before counting:

```bash
python3 - <<'EOF'
import re
L=open('src/client/combat.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {790,1360}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-14, 49 | the include block (with the `WITH_AUDIO` pair at 7/9) and the trailing blank | copied to each TU | 15 |
| **15-48** | ⚠ `// Packet structures based on Titanium client` + **`#pragma pack(1)`** + `ClientTarget_Struct` / `Consider_Struct` / `Action_Struct` + **`#pragma pack()`** | **stays in `combat.cpp` ONLY** — see §Correction 21 | 34 |
| **50-789** | ctor (50-110) / `Update` (112-187) / `SetTarget` (203-243, **the only reader of `ClientTarget_Struct`, at 220-221**) / `ClearTarget` / `CycleTargets` (384-469) / `EnableAutoAttack` (479-519) / `DisableAutoAttack` / `SendTargetPacket` (**second `ClientTarget_Struct` reader, 261-262**) / `GetNearbyHostiles` (711-788) — targeting and auto-attack | **stays** `combat.cpp` | **789** |
| **790-1359** | `GetNearbyAllies` (790-…) / `UpdateAutoHunting` (970-1023) / `ProcessCombatRound` (1171-1265) / the range and suitability helpers through 1359 — the per-round combat pump | `combat_rounds.cpp` | **619** |
| **1360-2119** | `FindNextHuntTarget` (1360-1551, **192 lines, moved WHOLE**) / `IsTargetSuitableForHunt` (1579-1632) / `ProcessConsiderResponse` (1694-1765) / `ListHuntTargets` (1798-1905) / `TickAwaitingItems` (2042-2081) and the looting tail — auto-hunt and consider | `combat_hunting.cpp` | **809** |

**Result: 3 TUs** (`combat.cpp` stays and counts). Largest **~809**.

Arithmetic that must hold: **`49 + 740 + 570 + 760 = 2119`; with the struct block staying behind, the two new TUs are `15 + range`**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundaries fall where they do: **50-789** is targeting and auto-attack —
the state a `/target` or `/attack` sets and the packets it sends. **790-1359** is
the per-round pump that runs while attacking. **1360-2119** is auto-hunting:
target discovery, `/consider` response handling, the hunt list, and the looting
that follows a kill.

⚠ **`CombatManager` is a `friend` of `EverQuest`** (`include/client/eq.h`).
That is a class-level relationship, **not a file-level one** — friendship
follows the class, so every TU that defines a `CombatManager` member keeps the
same access. **No `friend` declaration changes, and no header change of any
kind.** Verify by building; do not "fix" anything here.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 790 | `788: }` (end of `GetNearbyHostiles`), `789` blank | `790: std::vector<CombatTarget> CombatManager::GetNearbyAllies(float range) {` |
| 1360 | `1358: }`, `1359` blank | `1360: void CombatManager::FindNextHuntTarget() {` |

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/combat.cpp
```

## ⚠ HAZARD 1 — `#pragma pack(1)` must not be split from `#pragma pack()`

Lines 17 and 48 are a matched **global** pack directive pair (not
`push`/`pop`). **They travel together or not at all.** This plan keeps the whole
block 15-48 in `combat.cpp` and gives the two new TUs **no pack directive**,
which is correct because neither reads any of the three structs. ⚠ **A
half-copied pair would silently change the layout of every struct declared after
it in that TU** — the T145/T147 failure mode the batch plan names for J14.
**Grep both TUs for `pragma pack` after the cut and paste the (empty) result.**

## ⚠ HAZARD 2 — two of the three structs are dead — record, do not delete

`Consider_Struct` (22-31) and `Action_Struct` (33-47) are **defined and never
referenced anywhere in the tree** — `grep -rn` over `src/ include/ tests/`
returns their definitions and nothing else. That is **26 lines of dead
declaration**, a new instance of the batch's dead-content class (seven sites so
far). **Record it in your Review as dead-content site #8 and move it verbatim.**
A J unit never deletes and never tidies; the standing recommendation covering
all sites is Will's to accept.

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `combat.cpp`:**

`combat.cpp` has **zero `static` symbols and zero anonymous namespaces**. Its
only file-local entities are the three packet structs at 18-47.

| symbol | definition | readers | range holding the readers |
|---|---|---|---|
| `ClientTarget_Struct` | 18-20 | **220, 221, 261, 262** | **50-789** |
| `Consider_Struct` | 22-31 | **none in the whole tree** | — |
| `Action_Struct` | 33-47 | **none in the whole tree** | — |

**All four readers are inside range 50-789, which is the range that STAYS in
`combat.cpp` — the same range that holds the definitions.** Correction 21 is
satisfied by construction, and the two new TUs need neither the structs nor the
pack directives.

Re-derive it yourself; do not cite this table:

```bash
grep -n '^static \|^namespace' src/client/combat.cpp
grep -n 'ClientTarget_Struct\|Consider_Struct\|Action_Struct' src/client/combat.cpp
grep -rn 'Consider_Struct\|Action_Struct' src/ include/ tests/
```

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `combat.cpp`'s preamble, lines 1-49, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

⚠ **Deviation, enumerated and justified (correction 20's discipline): the two
new TUs carry preamble lines 1-14 and 49 ONLY.** They do **not** receive
15-48 — the pack directives and the three structs — because neither reads any of
them and a half-copied `#pragma pack` pair is a silent layout hazard (HAZARD 1).
Lines 1-14 (including the `WITH_AUDIO` pair at 7/9) are copied **verbatim**.
`combat.cpp` itself is untouched in its preamble.

**There is no namespace wrapper.** Each new TU ends with its last method's
closing brace and nothing else.

## File-header comments

`combat.cpp` has **no file-header comment block** — it opens straight into
`#include`. **Each new TU gets a short header comment** (four to six lines) that
names its clusters and cites `refactor/J24_combat_split.md`, and **`combat.cpp`
gains one too**, naming the three TUs. That is the unit's only non-move content;
enumerate every line of it.

`combat_hunting.cpp`'s header must record that **`FindNextHuntTarget` is 192
lines and was moved whole.**

Comment lines are boilerplate to the verifier, but they are visible in the
line-multiset check.

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
grep -rn 'combat\.cpp' CMakeLists.txt tests/ scripts/
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
| `CMakeLists.txt:300` | `SET(CLIENT_CORE_SOURCES` (opens at `:265`) | ⚠ **YES — add both new TUs here** |
| `tests/cmake/commands.cmake:278` | `test_combat.cpp` — a **test source file** whose name merely contains `combat` | no |

⚠ **The `tests/cmake/commands.cmake:278` hit is a false positive** — it is the
test's own source, not this production file. Do not add anything there. **This
unit does NOT touch `gles2_zone_benchmark`.**

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'combat' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

At `824adb28` the first grep returns **comment-only hits and zero functional
readers**, and the second — the exhaustive list of source paths any test opens as
text — **does not contain this file**. **No repoint is needed.** (J11 was the
cautionary case: four tests read `irrlicht_renderer.cpp` as text; three would
have failed and one would have **silently asserted nothing**.)

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
- ⚠ **Expected verifier result for J24: **EXIT 0, `+0/-0` substantive.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied preamble (1-14, 49) is **100% boilerplate**: `#include`,
    `#ifdef`, `#endif`, and a blank the normalizer drops.
  - Lines 15-48 are **not copied at all**, so they contribute nothing.
  - Header-comment lines are `//` — boilerplate.
  - No namespace open/close to replicate; no internal header; no test repoint.
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
  git show "$BASE:src/client/combat.cpp" | sed -n '50,2119p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ This unit creates no cross-TU header and relocates nothing into an existing
  file, so **neither correction 7 nor correction 20 applies** and the diff should
  be **empty**. Run it over the three `.cpp` **bodies only** (lines 50 onward);
  the preamble asymmetry is accounted for above and must not be concatenated in.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`50-789, 790-1359, 1360-2119`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/combat.cpp" | sed -n '50,2119p'`.
  **2,070 lines. Expect `cmp` exit 0.**
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
  file**: `src/client/combat.cpp`, `src/client/combat_rounds.cpp`, `src/client/combat_hunting.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **0**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **2,119** this plan measured,
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
   (this plan predicts roughly 30 / 20 / 27 of the 77), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. **`grep -n 'pragma pack'` on all three TUs, pasted** — `combat.cpp` must
    show exactly the two lines it has today and the new TUs must show none.
12. **The dead-content finding**, stated as site #8: `Consider_Struct` and
    `Action_Struct`, 26 lines, zero readers in the tree. **Recorded, moved
    verbatim, not deleted.**

## Territory

`src/client/combat.cpp`, the two new TUs beside it
(`src/client/combat_rounds.cpp`, `src/client/combat_hunting.cpp`),
`CMakeLists.txt` (the one `CLIENT_CORE_SOURCES` site), and this plan file.
**Nothing else** — not `include/client/combat.h`, not `include/client/eq.h` (the
`friend` declaration is untouched), not any `tests/` file, not
`tests/cmake/*.cmake`, not `docs/`.

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
