# J34 — Batch J Wave 6: split `src/client/graphics/spell_visual_fx.cpp` (1,517 → 2 TUs, largest ~841)

**Slot: assigned at dispatch. Branch `task/J34-spell-visual-fx-split` cut from `fable`.
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

`src/client/graphics/spell_visual_fx.cpp` — **1,517 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **1,517** |
| preamble | **1-19** — ⚠ **a 3-line `/* … */` block comment (1-3)**, thirteen `#include` lines with an `#ifdef EQT_HAS_GLES2` group at 12-15, then `namespace EQ {` at 19. **Zero non-boilerplate lines.** |
| namespace | **a single namespace, `EQ`** (not `EQT::Graphics` — this file is the exception in the graphics tree), opened at **19**, closed at **1517** (`} // namespace EQ`). **One close replicates into the new TU.** |
| method definitions | **41** |
| largest method | **125** `SpellVisualFX::createCastGlow` (353-477); 2nd **90** `loadParticleTextures` (102-191) |
| `// ====` banners | **14**, in seven pairs, at 334/336, **841/843**, 939/941, 968/970, 1056/1058, 1307/1309, 1400/1402 |
| conditional preprocessor directives | 26 — **12/15** and **27/31** `EQT_HAS_GLES2` / `#ifndef EQT_HAS_GLES2` (preamble and constructor); then twelve more pairs at 355/417, 481/518, 563/582, 605/624, 639/648, 727/736, 784/795, 814/825 (all in range 20-840) and 847/854, 874/879, 901/908 (all in range 841-1517). **Every pair is inside a single method. No cut lands inside any of them.** |
| file-scope `static` / anonymous namespaces | **zero of both.** |
| multi-line comment regions | ⚠ **ONE multi-line region: lines 1-3**, the `/* WillEQ Spell Visual Effects Implementation */` file header. It is in the **preamble**, nowhere near a cut, and comment depth returns to 0 at line 3. **This is correction 33's case stated correctly** — the file has a real multi-line comment region, and it is irrelevant to the cut because **depth at the cut is 0**. |

⚠ **Correction 33: "no block comments" must mean "no MULTI-LINE comment
regions".** Single-line `/* … */` argument comments are irrelevant; what the
cutting rule needs is **comment depth 0 at every cut**. Compute it, do not
assert it, and beware the trap `eq_movement.cpp:18` set for the orchestrator's
own scanner: a `//` line containing the token `*/` makes a naive depth counter
go negative. Strip `//` tails before counting:

```bash
python3 - <<'EOF'
import re
L=open('src/client/graphics/spell_visual_fx.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {841}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-19 | preamble: the 3-line block comment, thirteen includes with the `EQT_HAS_GLES2` group, the single namespace open | copied to the new TU, with 1-3 adapted | 19 |
| **20-840** | ctor (21-…, with the `#ifndef EQT_HAS_GLES2` pair at 27/31) / `loadParticleTextures` (102-191) / `update` (284-332) / the **334/336 banner** + `createCastGlow` (**353-477, 125 lines — the file's largest**) / `createProjectile` (479-559) / `createImpact` (561-601) / `createSpellComplete` (637-723) / `createBuffAura` (725-779) and the effect-creation tail through 840 — **effect creation, eight `EQT_HAS_GLES2` pairs, all inside methods** | **stays** `spell_visual_fx.cpp` | **841** |
| **841-1517** | the **Effect Removal** banner (841-843) + `removeCastGlow` (845-…) and the removal family / the 939/941, 968/970 banners + `getSpellColor` (984-1025) / the **1056/1058** banner + `createGlowNode` (1060-1134) / `createImpactNode` (1152-1197) / `createAuraNode` (1199-1245) / the 1307/1309 banner / the **1400/1402** banner + `applyWorkerResults` (1411-1494) / `swapEvictedTexture` and the namespace close at 1517 — **removal, colour lookup, the scene-node builders and the worker-result path** | `spell_visual_fx_nodes.cpp` | **696** |

**Result: 2 TUs** (`spell_visual_fx.cpp` stays and counts). Largest **~841**.

Arithmetic that must hold: **`19 + 821 + 677 = 1517`; `spell_visual_fx.cpp` becomes `840 + 1 replicated close = 841` and the new TU is `19 + 677 = 696`, plus each file's header comment**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundary falls where it does: **841 is a banner start** — the
`// ==== Effect Removal ====` pair — and it is the cleanest topic change in the
file: everything before it *creates* an effect, everything after it *removes*,
*colours*, or *builds the scene nodes* for one.

**This file is only 17 lines over the ceiling, so ONE cut is enough** — and one
cut still costs the full acceptance bar (~30-60 min), which is fixed per unit
regardless of file size. **Consider pairing this unit in a slot with J33** (the
other one-cut unit in Wave 6): the batch plan's scheduling note recommends
exactly that, and their file territories are disjoint.

⚠ **No method here is a §Optional Batch-G candidate.** The largest is 125 lines;
the floor is `19 + 125 = 144`. There is nothing to flag.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 841 | `839: }` (end of `createGroundCircle`), `840` blank | `841: // ============================================================================` — the *Effect Removal* banner, then `845: void SpellVisualFX::removeCastGlow(uint16_t caster_id)` |

⚠ **841 is between the `#endif` at 825 and the `#ifdef` at 847** — outside every
conditional region. **Verify that at your tip**; this file has twenty-six
directives and it is the one property most likely to have drifted.

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/graphics/spell_visual_fx.cpp
```

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `spell_visual_fx.cpp`:**

`spell_visual_fx.cpp` has **zero file-scope `static` symbols, zero anonymous
namespaces, and zero file-scope constants.** Every symbol it defines is a
`SpellVisualFX` member with external linkage, declared in
`include/client/graphics/spell_visual_fx.h`.

**Therefore correction 21 has NO exposure in this file** — the `createXNode`
helpers that the creation methods call across the cut are class members, not file
statics. Confirm with your own greps and paste them:

```bash
grep -n '^static \|^namespace\s*{\|^const \|^constexpr \|^extern ' \
    src/client/graphics/spell_visual_fx.cpp
grep -n 'namespace' src/client/graphics/spell_visual_fx.cpp
```
(the first: no output, exit 1. The second: exactly two lines — 19 and 1517.)

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `spell_visual_fx.cpp`'s preamble, lines 1-19, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

There is nothing non-boilerplate in the preamble (a `/* */` block comment and a
`#ifdef` group are both allowlisted), so the copy is unconditional and total:
**lines 1-19 verbatim into the new TU**, with 1-3 adapted per §File-header
comments. ⚠ **Copy the block comment as a complete `/* … */` region** — a
half-copied block comment is J02's exact failure mode.

⚠ **This file uses `namespace EQ`, NOT `namespace EQT { namespace Graphics {`** —
it is the exception in the graphics tree. **The new TU opens `namespace EQ {` and
ends with a single `} // namespace EQ`.** Do not "harmonise" it with its
neighbours; that would be a behaviour-neutral change wearing a move-only commit's
clothes, and it is out of scope.

## File-header comments

`spell_visual_fx.cpp` **already has** a 3-line `/* … */` header
(`WillEQ Spell Visual Effects Implementation`). **Both TUs get an adapted
version** — same `/* … */` shape, naming that TU's clusters and citing
`refactor/J34_spell_visual_fx_split.md`. **The stays file's must be updated too.**

⚠ **Keep the adapted headers as `/* … */` blocks, matching the original.** Do not
convert them to `//`; do not leave one file's block unterminated. Paste both
headers in full in your Review, and confirm comment depth returns to 0.

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
grep -rn 'spell_visual_fx\.cpp' CMakeLists.txt tests/ scripts/
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
actually found**, and if it differs from the TWO below, say so loudly.

At `824adb28` this file has **TWO real add-site(s)**:

| file:line | variable / target | edit? |
|---|---|---|
| `CMakeLists.txt:546` | `SET(GRAPHICS_SOURCES` (opens at `:439`) | ⚠ **YES** |
| `CMakeLists.txt:842` | `ADD_EXECUTABLE(model_viewer` (`:798`) | ⚠ **YES** |

**No `tests/cmake/` fragment names this file.** **This unit does NOT touch
`gles2_zone_benchmark`.** ⚠ Two sites, not one — **`model_viewer` compiles this
file directly**, and a plan that stopped at `GRAPHICS_SOURCES` would produce a
link error in a tool target nothing in `ctest` builds.

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'spell_visual_fx' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

At `824adb28` the first grep returns **comment-only hits and zero functional
readers**, and the second — the exhaustive list of source paths any test opens as
text — **does not contain this file**. **No `readFile` repoint is needed.**

⚠ **Re-run both at your tip and paste them.**

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
    src/client/graphics/spell_visual_fx.cpp
```

At `824adb28` **`spell_visual_fx.cpp` matches none of those tokens**, so it is
invisible to both parts of the sweep and the new TU will be too. **Confirm at
your tip and paste the (empty) grep.**

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
- ⚠ **Expected verifier result for J34: **EXIT 0, `+0/-0` substantive.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied preamble (1-19) is **100% boilerplate**: the `/*` / ` *` / ` */`
    lines are comments, thirteen `#include`, two conditional directives, one
    `namespace … {`, and blanks the normalizer drops.
  - The replicated namespace close is a bare `}` with a `//` tail — boilerplate.
  - Header-comment adaptations cost zero (comments are allowlisted **including**
    `/* */` forms).
  - Only ONE new TU, no `extern`, no internal header, no test repoint, no
    preamble deviation.
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
  git show "$BASE:src/client/graphics/spell_visual_fx.cpp" | sed -n '20,1516p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Strip the single namespace close from each TU before comparing.** The
  original's own close (1517) is inside range 841-1517, so the `before` side must
  run to **1516**, not 1517.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`20-840, 841-1516`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/graphics/spell_visual_fx.cpp" | sed -n '20,1516p'`.
  **1,497 lines. Expect `cmp` exit 0.**
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
  file**: `src/client/graphics/spell_visual_fx.cpp`, `src/client/graphics/spell_visual_fx_nodes.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **14**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **1,517** this plan measured,
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
7. **How many CMake add-sites you actually found**, against the TWO
   this plan names, and the output of both `tests/`/`scripts/` greps.
8. **Both content proofs**, pasted, and the verifier's exit code plus `$BASE`.
   State whether the result matched the prediction; if it did not, enumerate
   every flagged line before concluding anything.
9. **Final line counts of every file**, method counts per TU
   (this plan predicts roughly 17 / 24 of the 41), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. **Both `/* … */` file headers, pasted in full**, with the comment-depth
    computation showing depth returns to 0 in each file.
12. **The two CMake add-sites, both confirmed** — `GRAPHICS_SOURCES` and
    `model_viewer`. If you found only one, you missed `model_viewer`.

## Territory

`src/client/graphics/spell_visual_fx.cpp`, the one new TU beside it
(`src/client/graphics/spell_visual_fx_nodes.cpp`), `CMakeLists.txt` (the two
sites), and this plan file. **Nothing else** — not
`include/client/graphics/spell_visual_fx.h`, not
`src/client/graphics/environment/particle_manager.cpp`, not
`src/client/spell/`, not `config/spell_effects.json`, not any `tests/` file, not
`tests/cmake/*.cmake`, not `docs/`.

⚠ **SERIALISATION**: this unit inserts into `SET(GRAPHICS_SOURCES` and
`model_viewer`, which J28, J29, J30, J31, J32, J33 and J35 also touch in one
combination or another. Disjoint source files, so concurrent execution is fine —
**merge one at a time and rebase.**

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
