# J28 — Batch J Wave 6: split `src/client/graphics/detail/detail_texture_atlas.cpp` (1,826 → 3 TUs, largest ~738)

**Slot: assigned at dispatch. Branch `task/J28-detail-texture-atlas-split` cut from `fable`.
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

`src/client/graphics/detail/detail_texture_atlas.cpp` — **1,826 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **1,826** |
| preamble | **1-8** — four `#include` lines, a blank, then `namespace EQT {` / `namespace Graphics {` / `namespace Detail {` at 6-8. **Zero non-boilerplate lines.** |
| namespace | `EQT` / `Graphics` / `Detail` opened at **6-8**, closed at **1824-1826**. **All three closes replicate into each new TU.** |
| method definitions | **45** |
| largest method | **152** `DetailTextureAtlas::createAtlas` (18-169); 2nd **84** `drawFootprintLeft` (1193-1276) |
| `// ====` banners | **12**, in six pairs, at 462/464, 620/622, 801/803, 995/997, 1189/1191, 1362/1365 — each is a `// ====` / title / `// ====` biome section header |
| conditional preprocessor directives | **zero.** No conditional compilation anywhere in the file. |
| file-scope `static` / anonymous namespaces | **zero of both.** The only `namespace` tokens are the file's own six. |
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
L=open('src/client/graphics/detail/detail_texture_atlas.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {462,1189}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-8 | preamble: four includes + the three namespace opens | copied to each new TU | 8 |
| **9-461** | `getTilePosition` (9-16) / `createAtlas` (**18-169, 152 lines, moved WHOLE**) / `drawLine` / `drawRect` and the primitive helpers / the desert-biome draws through 461 — the atlas assembly and drawing primitives | **stays** `detail_texture_atlas.cpp` | **464** |
| **462-1188** | the **Snow biome (Velious) - Row 1** banner (462-464) onward: `drawIceCrystal` (466-…) and the snow, swamp and cactus biome draws — banners at 620/622, 801/803, 995/997 — through `drawSwampMushroom` (1041-1087) and the tail at 1188 | `detail_texture_atlas_flora.cpp` | **738** |
| **1189-1826** | the **Footprints … Row 5** banner (1189-1191) onward: `drawFootprintLeft` (1193-1276) / `drawFootprintRight` (1278-1360) / the **Row 6 grass** banner (1362/1365) and every `drawGrass*` — `drawGrassMixed1` (1367-1413) / `drawGrassMixed2` (1415-1461) / `drawGrassClump` (1463-1514) / `drawGrassBroad` (1555-1614) / `drawGrassCurved` (1616-1685) / `drawGrassSeedHead` (1687-1749) / `drawGrassBroken` (1751-1822) | `detail_texture_atlas_grass.cpp` | **646** |

**Result: 3 TUs** (`detail_texture_atlas.cpp` stays and counts). Largest **~738**.

Arithmetic that must hold: **`8 + 453 + 727 + 638 = 1826` (the third range carries the original's three namespace closes)**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundaries fall where they do: this file is 45 procedural-drawing
methods organised into six banner-delimited biome/row sections, and **both cuts
fall exactly on a banner start**. **9-461** is the atlas machinery plus the
drawing primitives every other method calls. **462-1188** is the flora biomes.
**1189-1826** is footprints and the grass row — the largest, most uniform block.

⚠ **Cut 462 and cut 1189 are banner starts.** Correction 11 makes J03's rule the
deciding one and leaves the banner rule as a tie-breaker for **where a leading
comment belongs**: with the cluster it introduces. Both cuts also satisfy J03's
rule independently (460 `}` / 461 blank; 1187 `}` / 1188 blank), so **the two
rules agree at both boundaries** — say so in your Review, and say so if they
disagree anywhere at your tip.

**This is the lowest-risk unit in Wave 6**: zero preprocessor, zero statics, zero
block comments, zero text readers, one CMake site. It is Wave 6's control sample
for what the bar alone costs — the role J07 played in Wave 2.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 462 | `460: }` (end of the preceding draw method), `461` blank | `462: // ============================================================` — the *Snow biome (Velious) - Row 1* banner |
| 1189 | `1187: }`, `1188` blank | `1189: // ============================================================` — the *Footprints (for different surface types) - Row 5* banner |

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/graphics/detail/detail_texture_atlas.cpp
```

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `detail_texture_atlas.cpp`:**

`detail_texture_atlas.cpp` has **zero file-scope `static` symbols, zero
anonymous namespaces, and zero file-scope constants.** Every symbol it defines is
a `DetailTextureAtlas` member with external linkage, declared in
`include/client/graphics/detail/detail_texture_atlas.h`.

**Therefore correction 21 has NO exposure in this file** — there is no
file-local helper that could be separated from its consumer. Note in particular
that the drawing primitives (`drawLine`, `drawRect`, …) that every biome method
calls are **class members, not file statics**, so the cross-TU calls this split
creates are ordinary links. Confirm with your own greps and paste them:

```bash
grep -n '^static \|^namespace\s*{\|^const \|^constexpr ' \
    src/client/graphics/detail/detail_texture_atlas.cpp
grep -n 'namespace' src/client/graphics/detail/detail_texture_atlas.cpp
```
(the first: no output, exit 1. The second: exactly six lines — 6, 7, 8, 1824,
1825, 1826.)

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `detail_texture_atlas.cpp`'s preamble, lines 1-8, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

There is no conditional compilation and nothing non-boilerplate in the preamble,
so the copy is unconditional and total: **lines 1-8 verbatim into each new TU.**

**Each new TU ends with the three namespace closes, replicated verbatim:**
```
} // namespace Detail
} // namespace Graphics
} // namespace EQT
```
⚠ **Correction 30**: replicating "blank + closes" double-spaces any TU whose body
already ends on a blank. Declare it; do not hand-fix it.

## File-header comments

`detail_texture_atlas.cpp` has **no file-header comment block** — it opens
straight into `#include`. **Each of the three TUs gets a short one** (four to six
lines) naming its biome/row sections and citing
`refactor/J28_detail_texture_atlas_split.md`. That is the unit's only non-move
content; enumerate every line of it.

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
grep -rn 'detail_texture_atlas\.cpp' CMakeLists.txt tests/ scripts/
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
| `CMakeLists.txt:553` | `SET(GRAPHICS_SOURCES` (opens at `:439`) | ⚠ **YES — add both new TUs here** |

**No `tests/cmake/` fragment names this file, and no tool or benchmark target
does either.** **This unit does NOT touch `gles2_zone_benchmark`.** ⚠ That is
unusual for a `src/client/graphics/` file — J10 had twelve sites and J32
(`zone_geometry.cpp`, this same wave) has twelve. **Count your own; the shape of
the file gives no hint** (correction 11: `wld_loader.cpp` is a leaf parser and
had the most exposure in the tree).

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'detail_texture_atlas' tests/ scripts/
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

⚠ **`tests/test_first_person_camera_single_formula.cpp` sweeps EVERY `.cpp` and
`.h` under `src/client/graphics` recursively, and keys two of its three
assertions on FILENAME.** Any TU this unit creates under that tree enters the
sweep automatically. The two rules a new TU must not break:

- **Part 1** — a file that both mentions `CameraMode::FirstPerson` **and** writes
  `camera_->setPosition(` / `camera_->setTarget(` is an offender unless its
  filename is `renderer_camera.cpp`.
- **Part 2** — each first-person tunable
  (`kFirstPersonPitchDownDegrees`, `kFirstPersonHorizontalFovDegrees`,
  `firstPersonMaxDownPitchDegrees`, `firstPersonMinLookPitchDegrees`,
  `firstPersonVerticalFovRadians`, `computeFirstPersonCamera`) may be read from
  **at most one** TU, and `firstPersonMinLookPitchDegrees` from at most two —
  `renderer_camera.cpp` and **`renderer_player_physics.cpp`, named as a string
  literal**.

**Check your file before cutting and paste the result:**

```bash
grep -n 'CameraMode::FirstPerson\|camera_->setPosition(\|camera_->setTarget(\|kFirstPerson\|firstPerson\|computeFirstPersonCamera' \
    src/client/graphics/detail/detail_texture_atlas.cpp
```

At `824adb28` **`detail_texture_atlas.cpp` matches none of those tokens**, so it
is invisible to both parts of the sweep and the new TUs will be too.

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
- ⚠ **Expected verifier result for J28: **EXIT 0, `+0/-0` substantive.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied preamble (1-8) is **100% boilerplate**: four `#include` and three
    `namespace … {`.
  - The replicated namespace closes are bare `}` with a `//` tail — boilerplate.
  - Header-comment lines are `//` — boilerplate.
  - No non-boilerplate preamble content, no internal header, no test repoint, no
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
  cat <the 3 TU bodies, each stripped of its preamble and namespace close> \
      | sort > /tmp/after.txt
  git show "$BASE:src/client/graphics/detail/detail_texture_atlas.cpp" | sed -n '9,1823p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Strip the three namespace closes from each TU before comparing**, and note
  that the original's own closes (1824-1826) are inside range 1189-1826 — so the
  `before` side must run to **1823**, not 1826, and the `after` side must exclude
  every TU's replicated closes. Getting this off by three lines produces a diff
  that reads exactly like a dropped range: **when a proof fails, suspect the proof
  first** (correction 20).
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`9-461, 462-1188, 1189-1823`) and confirm the result is
  byte-identical to `git show "$BASE:src/client/graphics/detail/detail_texture_atlas.cpp" | sed -n '9,1823p'`.
  **1,815 lines. Expect `cmp` exit 0.**
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
  file**: `src/client/graphics/detail/detail_texture_atlas.cpp`, `src/client/graphics/detail/detail_texture_atlas_flora.cpp`, `src/client/graphics/detail/detail_texture_atlas_grass.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **12**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **1,826** this plan measured,
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
   (this plan predicts roughly 13 / 19 / 13 of the 45), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**

## Territory

`src/client/graphics/detail/detail_texture_atlas.cpp`, the two new TUs beside it
(`…_flora.cpp`, `…_grass.cpp`), `CMakeLists.txt` (the one `GRAPHICS_SOURCES`
site), and this plan file. **Nothing else** — not
`include/client/graphics/detail/detail_texture_atlas.h`, not
`src/client/graphics/detail/detail_manager.cpp` (**J22's file, in flight** — do
not touch it), not any `tests/` file, not `tests/cmake/*.cmake`, not `docs/`.

⚠ **J22 (`detail_manager.cpp`) is in flight in the same directory.** Different
file, no overlap — but **do not "fix" anything in `detail_manager.cpp`** and do
not add a `detail/` directory-wide edit of any kind.

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
