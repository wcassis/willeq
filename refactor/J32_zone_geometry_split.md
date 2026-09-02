# J32 — Batch J Wave 6: split `src/client/graphics/eq/zone_geometry.cpp` (1,637 → 2 TUs, largest ~994)

**Slot: assigned at dispatch. Branch `task/J32-zone-geometry-split` cut from `fable`.
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

`src/client/graphics/eq/zone_geometry.cpp` — **1,637 lines**. ⚠ **Re-verify at your own branch tip
before you cut** (correction 9: every Wave-1..5 unit found its survey number
stale, four Wave-6 files GREW between surveys) and report the drift in your
Review rather than silently adjusting.

| property | measured value |
|---|---|
| total lines | **1,637** |
| preamble | **1-34** — fourteen `#include` lines with an `#ifdef EQT_HAS_TEXTURE_ATLAS` pair at 6/8, then the banner-wrapped **UV and Coordinate System Notes** comment block (16-33). **Zero non-boilerplate lines.** |
| namespace | `EQT` / `Graphics` opened at **35-36**, closed at **1636-1637**. **Both closes replicate into the new TU.** |
| method definitions | **14** |
| largest method | **499** `ZoneMeshBuilder::buildAtlasedMesh` (1061-1559); 2nd **259** `buildTexturedMesh` (303-561); 3rd **198** `buildCompositeMesh` (720-917) |
| `// ====` banners | **13**, at 16, 18, 33 (preamble), then pairs at 563/565, 716/718, **919/921**, 1057/1059, **1562/1564** |
| conditional preprocessor directives | 4 — **6/8** `EQT_HAS_TEXTURE_ATLAS` (preamble); ⚠ **923 / 1560** `EQT_HAS_TEXTURE_ATLAS` — **a 638-line region containing the whole tessellation helper block AND `buildAtlasedMesh`**. **This region is the split.** |
| file-scope `static` / anonymous namespaces | ⚠ **two `struct`s (`TessVertex` 926, `TessTriangle` 933) and four `static` free functions (`lerpVertex` 937, `clipPolygon` 954, `fanTriangulate` 988, `splitTriangleToUVCells` 1004)** — all inside the 923-1560 conditional. **Zero anonymous namespaces.** See §Correction 21. |
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
L=open('src/client/graphics/eq/zone_geometry.cpp').read().split('\n')
d=0
for i,ln in enumerate(L,1):
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
    if i in {919,1562}: print('depth before line',i,'=',d)
EOF
```

## The split

| Range | Content | Target TU | ≈ lines |
|---|---|---|---|
| 1-34 | preamble: fourteen includes (with the `EQT_HAS_TEXTURE_ATLAS` pair at 6/8) and the UV-notes comment block | copied to the new TU | 34 |
| **35-918** | the two namespace opens (35-36), ctor (38-41), `buildMesh` (94-172) / `buildColoredMesh` (174-253) / `loadTextureFromBMP` (255-301) / `buildTexturedMesh` (303-561) / banner 563/565 + `buildTexturedMeshFromUploaded` (567-714) / banner 716/718 + `buildCompositeMesh` (720-917) — every non-atlased mesh builder | **stays** `zone_geometry.cpp` (part 1 of 2) | **918** |
| **919-1561** | ⚠ the **Pre-Tessellation** banner (919-921) + **the whole `#ifdef EQT_HAS_TEXTURE_ATLAS` region 923-1560**: `TessVertex` (926) / `TessTriangle` (933) / `lerpVertex` (937) / `clipPolygon` (954) / `fanTriangulate` (988) / `splitTriangleToUVCells` (1004) / banner 1057/1059 + `buildAtlasedMesh` (**1061-1559, 499 lines, moved WHOLE and VERBATIM**) / `#endif // EQT_HAS_TEXTURE_ATLAS` (1560) + the blank at 1561 | `zone_geometry_atlased.cpp` | **681** |
| **1562-1637** | the **Lazy Texture Loading** banner (1562/1564) + `registerLazyTexture` (1566-1578) / `getOrLoadTexture` (1580-1605) / `registerUploadedTexture` (1612-1624) / `clearTextureCache` (1630-1634) + the two namespace closes | **stays** `zone_geometry.cpp` (part 2 of 2) | (76, included in the 918 above → **994** total) |

**Result: 2 TUs** (`zone_geometry.cpp` stays and counts). Largest **~994**.

Arithmetic that must hold: **`918 + 643 + 76 = 1637`, and `zone_geometry.cpp` ends at `918 + 76 = 994` while `zone_geometry_atlased.cpp` is `34 + 643 + 2 = 679` plus its file-header comment**. If your own numbers do not sum to
the file's line count, you have a gap or an overlap — **find it before you
build.**

⚠ **Correction 12: line estimates are unreliable in BOTH directions** (J03 ran
1-6 high, J11 ran 20-130 low). The counts above are `preamble + range
(+ namespace close)` and nothing else. **Your new file-header comment lines land
on top of that** (correction 19: a predicted number must be derived from what
the plan itself prescribes) — say so explicitly in your Review.

Why the boundary falls where it does: **the entire atlased path is already
fenced off by one `#ifdef EQT_HAS_TEXTURE_ATLAS` region, 923-1560, and that
region alone is 638 lines — 39% of the file.** Lifting it clears the ceiling in
one cut, and every one of the file's file-local helpers is inside it together
with its only consumer. Nothing else in the file needs to move.

⚠ **THIS UNIT'S `stays` FILE IS DISCONTIGUOUS.** `zone_geometry.cpp` keeps
**1-918 and 1562-1637**; the middle comes out. That is unusual for this batch
(J08 was the only other unit whose proofs had to account for a seam) and it
matters for the re-concatenation proof — see §Acceptance. **It is deliberate:**
the lazy-texture tail at 1562-1637 is outside the conditional and belongs with
the non-atlased builders, and moving it into the atlas TU would put unconditional
code inside a file whose entire reason for existing is the conditional region.

⚠ **`buildAtlasedMesh` is 499 lines and is NOT to be decomposed.** It lands in a
681-line TU with 819 lines of headroom. **Two TUs are enough** — a third would
buy nothing and would have to cut inside the conditional.

## Every cut boundary, with its neighbours — verify each at your tip

The rule is **J03's, which replaces the banner rule outright** (correction 11):
**cut at a blank line whose predecessor closes a method, and verify comment
depth 0 at the cut.** The banner rule survives only as a tie-breaker for where a
leading `// ====` comment belongs — **a banner travels with the cluster it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 919 | `917: }` (end of `buildCompositeMesh`), `918` blank | `919: // ============================================================================` — the *Pre-Tessellation: Split triangles at integer UV boundaries* banner, followed by `923: #ifdef EQT_HAS_TEXTURE_ATLAS` |
| 1562 | ⚠ **not a method close** — `1559: }` (end of `buildAtlasedMesh`), then `1560: #endif // EQT_HAS_TEXTURE_ATLAS`, then `1561` blank | `1562: // ============================================================================` — the *Performance: Lazy Texture Loading (Phase 3)* banner |

⚠ **The second boundary is the only one in Wave 6 whose predecessor is an
`#endif` rather than a `}`.** J03's rule assumes the predecessor closes a method;
here the method close is at 1559 and the `#endif` that closes the conditional is
at 1560. **The boundary is still unambiguous** — take 1560 and the blank at 1561
into the atlas TU so the `#ifdef`/`#endif` pair stays matched, and resume the
stays file at the banner on 1562. **Say explicitly in your Review that you put
1560-1561 in the atlas TU**; putting the `#endif` on the wrong side leaves an
unterminated `#ifdef` in one file and an unopened `#endif` in the other, and
**both halves would still compile with `EQT_HAS_TEXTURE_ATLAS` off**.

**Paste the surrounding lines for every cut in your Review.**

⚠ **Do not cut inside a conditional.** J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in the
other; the same mistake with an `#ifdef` **still compiles in the default
configuration** and breaks only in a config the acceptance bar exercises.
Run and paste:

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}' \
    src/client/graphics/eq/zone_geometry.cpp
```

## ⚠ HAZARD 1 — ⚠ the 923-1560 conditional must not be cut, and it defines the same identifiers the stays file must not see

`#ifdef EQT_HAS_TEXTURE_ATLAS` at **923** and `#endif // EQT_HAS_TEXTURE_ATLAS`
at **1560** fence a 638-line region. Everything inside is compiled only when the
atlas feature is on. **A cut anywhere between 923 and 1560 would leave an
unterminated `#ifdef` in one file and an unopened `#endif` in the other — and
both halves would still compile in the default configuration**, because
`EQT_HAS_TEXTURE_ATLAS` is on there. The break surfaces only in the config that
turns it off. This is J02's block-comment failure mode wearing preprocessor
clothes.

**The plan's single interior cut at 919 is outside the region (before it), and
the second boundary at 1562 is after the `#endif`.** Verify both at your tip and
paste the preprocessor dump.

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a helper CAN stay file-local. It does NOT
prove the plan's ranges KEEP it that way.** J08's plan assigned the exp helpers
to the *stays* range and their sole consumer to a *different* TU; the function
would have been undeclared and the three statics readerless. **It surfaced only
at the BUILD step.**

**The orchestrator audited this plan against correction 21 before committing it.
Result for `zone_geometry.cpp`:**

`zone_geometry.cpp` has **zero anonymous namespaces** and **six file-local
entities**, all of them inside the 923-1560 conditional. Every one of them was
mapped onto this plan's ranges:

| symbol | definition | readers | range holding the readers |
|---|---|---|---|
| `struct TessVertex` | **926-932** | 934, 937, 954, 957, 988, 1004-1006, 1020, 1026, 1039, **1156-1158** | all inside **919-1561** ✅ |
| `struct TessTriangle` | **933-935** | 988, 991, 1004, 1050, 1080 | all inside **919-1561** ✅ |
| `static lerpVertex` | **937-952** | **980** | **919-1561** ✅ |
| `static clipPolygon` | **954-986** | **1028, 1029, 1041, 1042** | **919-1561** ✅ |
| `static fanTriangulate` | **988-1002** | **1052** | **919-1561** ✅ |
| `static splitTriangleToUVCells` | **1004-1055** | **1159** — ⚠ **inside `buildAtlasedMesh`** | **919-1561** ✅ |

**Every definition and every reader is inside the single range this plan assigns
wholly to `zone_geometry_atlased.cpp`.** The one cross-method consumer —
`splitTriangleToUVCells` called from `buildAtlasedMesh` at 1159 — travels with
its helper by construction, because the `#ifdef` region contains both.

**Correction 21 is satisfied: no internal header is needed and nothing is
promoted.** ⚠ Note that this is *only* true because the plan keeps the whole
conditional together. **A three-way split that put the tess helpers and
`buildAtlasedMesh` in different TUs would be exactly J08's defect** — four
`static` functions with no declaration in scope, failing at the BUILD step.

Re-derive it yourself; do not cite this table:

```bash
grep -n '^static \|^struct \|^namespace\s*{\|^const \|^constexpr ' \
    src/client/graphics/eq/zone_geometry.cpp
for s in TessVertex TessTriangle lerpVertex clipPolygon fanTriangulate splitTriangleToUVCells; do
  echo "-- $s"; grep -n "\b$s\b" src/client/graphics/eq/zone_geometry.cpp
done
grep -rn 'lerpVertex\|clipPolygon\|fanTriangulate\|splitTriangleToUVCells\|TessVertex\|TessTriangle' \
    src/ include/ tests/ | grep -v zone_geometry.cpp
```
(the last must return nothing — these are file-local)

**Re-run the greps at your tip and paste them.** Do not cite this section — if
a file-scope symbol has appeared or a consumer has moved since `824adb28`,
**STOP and report**; the unit changes shape.

## Includes and the preamble

**Each new TU carries `zone_geometry.cpp`'s preamble, lines 1-34, verbatim**
— the whole include block including every conditional group. **Not "the includes
it actually needs":** §Process is explicit, and trimming per-TU include sets is a
second, riskier change wearing a move-only commit's clothes.

There is no non-boilerplate preamble content, so the copy is unconditional and
total: **lines 1-34 verbatim into `zone_geometry_atlased.cpp`**, including the
`#ifdef EQT_HAS_TEXTURE_ATLAS` include pair at 6/8 and the UV-notes comment block
at 16-33. `zone_geometry.cpp`'s own preamble is untouched.

**`zone_geometry_atlased.cpp` ends with the two namespace closes, replicated
verbatim:**
```
} // namespace Graphics
} // namespace EQT
```
⚠ **and they come AFTER the `#endif // EQT_HAS_TEXTURE_ATLAS`** that the TU
carries at its body's end. `zone_geometry.cpp` keeps the original's closes at
what are now its last two lines.

## File-header comments

`zone_geometry.cpp` has **no file-header comment block** — it opens straight into
`#include`, and its banner-wrapped UV notes at 16-33 are *documentation of the
coordinate convention*, not a file header. **Do not touch that block**; copy it
verbatim.

**`zone_geometry_atlased.cpp` gets a short header** (five to eight lines) that
says: it holds the whole `EQT_HAS_TEXTURE_ATLAS` region lifted out of
`zone_geometry.cpp`; **the region must not be split** because six file-local
entities and their only consumers live inside it; and **`buildAtlasedMesh` is 499
lines and is a recorded Batch-G candidate J32 deliberately did not touch.**
**`zone_geometry.cpp` gets a matching two-line note** pointing at the new TU.

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
grep -rn 'zone_geometry\.cpp' CMakeLists.txt tests/ scripts/
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
actually found**, and if it differs from the TWELVE below, say so loudly.

At `824adb28` this file has **TWELVE real add-site(s)**:

| file:line | variable / target | edit? |
|---|---|---|
| `CMakeLists.txt:451` | `SET(GRAPHICS_SOURCES` (opens at `:439`) | ⚠ **YES** |
| `CMakeLists.txt:811` | `ADD_EXECUTABLE(model_viewer` (`:798`) | ⚠ **YES** |
| `CMakeLists.txt:950` | `ADD_EXECUTABLE(s3d_dump` (`:938`) | ⚠ **YES** |
| `CMakeLists.txt:1010` | `ADD_EXECUTABLE(entity_composite_builder` (`:998`) | ⚠ **YES** |
| `CMakeLists.txt:1195` | `ADD_EXECUTABLE(generate_surface_map` (`:1183`) | ⚠ **YES** |
| `CMakeLists.txt:1256` | `ADD_EXECUTABLE(zone_line_editor` (`:1244`) | ⚠ **YES** |
| `CMakeLists.txt:1667` | ⚠ `ADD_EXECUTABLE(gles2_zone_benchmark` (`:1646`) — **the visual-gate binary** | ⚠ **YES** |
| `CMakeLists.txt:1791` | `ADD_EXECUTABLE(gles2_shader_diagnostic` (`:1776`) | ⚠ **YES** |
| `CMakeLists.txt:1877` | `ADD_EXECUTABLE(gles2_composite_audit` (`:1862`) | ⚠ **YES** |
| `tests/cmake/graphics.cmake:33` | `ADD_EXECUTABLE(test_graphics` (`:20`) | ⚠ **YES** |
| `tests/cmake/graphics.cmake:110` | `ADD_EXECUTABLE(test_asset_loading` (`:99`) | ⚠ **YES** |
| `tests/cmake/zone_integration.cmake:58` | `ADD_EXECUTABLE(test_door_loading` (`:47`) | ⚠ **YES** |

⚠ **TWELVE add-sites — the joint-highest in Wave 6, and three of them are in
`tests/cmake/` fragments that did not exist before J20** (correction 26). A plan
that greps `CMakeLists.txt tests/CMakeLists.txt` finds **nine of twelve and looks
complete** — losing 25% of the add-sites that way produces a link error at best
and a **silently unbuilt test target** at worst. This is J10's exact trap.

`CMakeLists.txt` itself is a pre-existing WARN (over 2,000 lines) that §Process
grandfathers by name. **Not yours.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the first finds a *string-compare exclusion* that a `readFile` grep cannot see —
J13's third site was exactly that, and it would have silently let a guard pass
while guarding nothing.

```bash
grep -rn 'zone_geometry' tests/ scripts/
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

At `824adb28` the first grep returns **comment-only hits and zero functional
readers**, and the second — the exhaustive list of source paths any test opens as
text — **does not contain this file**. **No repoint is needed.**

⚠ **Re-run both at your tip and paste them.** If a text reader has appeared, add
it to territory and repoint it — that is correction 10, not scope creep.

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
    src/client/graphics/eq/zone_geometry.cpp
```

At `824adb28` **`zone_geometry.cpp` matches none of those tokens**, so it is
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
- ⚠ **Expected verifier result for J32: **EXIT 0, `+0/-0` substantive.****

  **Derivation, from what this plan prescribes and nothing else** (correction 19
  — J09's plan contradicted itself by predicting a number one section later
  invalidated):

  - The copied preamble (1-34) is **100% boilerplate**: `#include`, `#ifdef`,
    `#endif`, `//` (the whole UV-notes block), and blanks the normalizer drops.
    **There is no `extern`, no `typedef` and no file-scope definition in it** —
    contrast J30 (6 lines) and J31 (2).
  - The replicated namespace closes are bare `}` with a `//` tail; the
    `#endif // EQT_HAS_TEXTURE_ATLAS` line **moves**, it is not duplicated, and
    `#endif` is allowlisted regardless.
  - Header-comment lines are `//` — boilerplate.
  - Only ONE new TU, so even a non-boilerplate preamble line would cost only
    `+1x`. There are none.
  - No internal header, no test repoint, no preamble deviation.
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
  git show "$BASE:src/client/graphics/eq/zone_geometry.cpp" | sed -n '35,1637p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **THE SEAM.** `zone_geometry.cpp`'s body is **discontiguous** — 35-918 then
  1562-1637 — so the `after` side must be assembled as
  `stays[35..918] + atlased[919..1561] + stays[1562..1637]`, **in that order**.
  A naive `cat stays atlased` produces a diff that fails **exactly at the seam**
  and reads like a dropped range. **That is J08's experience verbatim** (its
  first re-concatenation failed at its relocation seam, from its own offset
  arithmetic, not a dropped range): **when a proof fails, suspect the proof
  first** (correction 20).
  ⚠ Strip the two namespace closes from `zone_geometry_atlased.cpp` before
  comparing, and remember the original's closes at 1636-1637 are inside the third
  segment.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  extracted ranges **in original order** (`35-918, 919-1561, 1562-1637` — **in that order, with the third segment coming from `zone_geometry.cpp`'s tail, not the atlas TU**) and confirm the result is
  byte-identical to `git show "$BASE:src/client/graphics/eq/zone_geometry.cpp" | sed -n '35,1637p'`.
  **1,603 lines. Expect `cmp` exit 0.**
  ⚠ **Declare count variables `local` in your proof helpers.** More generally
  (correction 20): **when a proof fails, suspect the proof first.** J05's
  non-`local` `n` produced a 247-line shortfall that read exactly like a dropped
  range; J09's one proof failure was range arithmetic against the produced
  files' numbering; J08's failed **exactly at its relocation seam**. **Three
  units, three proof bugs, zero real defects.**
  ⚠ **Additionally and separately, prove the whole `#ifdef EQT_HAS_TEXTURE_ATLAS`
  region survived byte for byte** — 638 lines, 39% of the file, six file-local
  entities and the 499-line method:
  ```bash
  diff <(git show "$BASE:src/client/graphics/eq/zone_geometry.cpp" | sed -n '923,1560p') \
       <(sed -n '<its new range>p' src/client/graphics/eq/zone_geometry_atlased.cpp)
  ```
  **638 lines, exit 0. Paste it.**
- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report** rather than re-blessing.
  - ⚠ **A gate FAIL is not necessarily a PIXEL fail — check WHICH metric
    failed** (correction 15). J05's first run failed on `befallen` **p95 CPU
    11.684 vs a 10.948 ceiling with ZERO image mismatches**; the re-run measured
    4.676. That is a 2.5x swing on a 2x ceiling under wave load: **contention,
    not your change. Re-run.**
  - ⚠ **A failure at the RENDER step is a different animal**: it means a
    `gles2_zone_benchmark` CMake add-site was missed. ⚠ **This unit DOES touch `gles2_zone_benchmark`** (the binary `scripts/ci/visual_gate.sh` runs). A render-step gate failure means you missed that add-site — fix the add-site, do not re-run past it. Contrast the CPU-p95 failure above, which is contention (correction 15).
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ Correction 14: the wave-stretch figures on record are **worst cases
  observed, not forecasts.** J06's `ctest -j6` ran in 120.7 s — faster than
  J01's solo 3.0 min on a larger suite — and its ARM build completed first
  invocation. **Do not report a clean run as an anomaly.**
- `scripts/refactor/check_file_sizes.sh --files` on **every changed AND created
  file**: `src/client/graphics/eq/zone_geometry.cpp`, `src/client/graphics/eq/zone_geometry_atlased.cpp` → **0 warn, 0 fail.**
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'` (this file has **13**).
- **`chmod 644` every created file before `git add`.** A `sed >` pass produces
  mode 755 and the commit records a spurious `100644 => 100755` (J01).

## Review must answer

1. **Line count at your tip** against the **1,637** this plan measured,
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
7. **How many CMake add-sites you actually found**, against the TWELVE
   this plan names, and the output of both `tests/`/`scripts/` greps.
8. **Both content proofs**, pasted, and the verifier's exit code plus `$BASE`.
   State whether the result matched the prediction; if it did not, enumerate
   every flagged line before concluding anything.
9. **Final line counts of every file**, method counts per TU
   (this plan predicts 12 / 2 of the 14 methods, plus the four `static` free functions with the second), wall clock, slowest step, and whether wave concurrency
   stretched the verification tail.
10. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). J06, J07 and J08 each
    checked their file's citations **at their base commit** and found every one
    already wrong before the split. They are pre-existing doc rot owned by J21's
    successor pass. `docs/module_map.md` needs new rows for every TU this unit
    creates — **record it, do not act.**
11. **The twelve CMake add-sites, each confirmed** — nine root, two in
    `tests/cmake/graphics.cmake`, one in `tests/cmake/zone_integration.cmake`.
    ⚠ **One is `gles2_zone_benchmark`**: a miss there fails the visual gate at the
    RENDER step, which reads exactly like a behaviour change.
12. ⚠ **Explicit confirmation that `#endif // EQT_HAS_TEXTURE_ATLAS` (1560) and
    the blank at 1561 went into `zone_geometry_atlased.cpp`**, and that
    `grep -c 'EQT_HAS_TEXTURE_ATLAS' ` on each TU shows a **matched** pair.
13. **The seam-aware re-concatenation**, pasted, in the three-segment order.
14. **The `test_first_person_camera_single_formula.cpp` check, pasted.**

## Territory

`src/client/graphics/eq/zone_geometry.cpp`, the one new TU beside it
(`src/client/graphics/eq/zone_geometry_atlased.cpp`), `CMakeLists.txt` (the nine
root sites), `tests/cmake/graphics.cmake` and `tests/cmake/zone_integration.cmake`
(the three test sites), and this plan file. **Nothing else** — not
`include/client/graphics/eq/zone_geometry.h`, not `texture_atlas.h` or its `.cpp`,
not `src/client/graphics/eq/model_loading.cpp` or `variant_loading.cpp` (J29's and
J35's), not any `tests/*.cpp`, not `docs/`.

⚠ **SERIALISATION with J29 and J35**: all three insert into
`SET(GRAPHICS_SOURCES`, `model_viewer`, `gles2_zone_benchmark`,
`gles2_shader_diagnostic`, `gles2_composite_audit`, `test_graphics` and
`test_asset_loading` — **seven shared add-sites**. Disjoint source files, so they
can run concurrently, but **merge one at a time and rebase**, and never resolve a
CMake conflict by taking one side wholesale (correction 31).

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
- The `EQT_HAS_TEXTURE_ATLAS` region's boundaries are not exactly 923 and 1560 at
  your tip.
- Any of the six file-local entities has acquired a reader outside 919-1561.
