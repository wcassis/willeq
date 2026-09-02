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

---

## Review (executed 2026-09-02, slot1, branch `task/J32-zone-geometry-split`)

**Base / fork point: `2498c3a5354a7df86d9126e31a3acb787af1664c`** ("Batch J: fold
J33's corrections…"), the `fable` tip at dispatch. All measurements below are at
that tip.

### 1. Line count and drift

**1,637 lines — EXACTLY what §Measured recorded at `824adb28`. Zero drift**, in
line count and in every range. Every line number in §The split, §Every cut
boundary and §Correction 21 was correct as written. This is the first Wave-6 file
this executor has seen with no stale numbers at all (contrast correction 9).

### 2. §Measured re-verified

| property | plan | measured at my tip | ✓ |
|---|---|---|---|
| total lines | 1,637 | **1,637** | ✓ |
| namespace opens / closes | 35-36 / 1636-1637 | **35,36 / 1636,1637** | ✓ |
| method definitions | 14 | **14** `ZoneMeshBuilder::` | ✓ |
| banners `^// ={4,}` | 13 | **13** at 16,18,33,563,565,716,718,919,921,1057,1059,1562,1564 | ✓ |
| preprocessor directives | 4 | **4** | ✓ |
| multi-line comment regions | zero | **zero** (`grep -n '/\*'` exit 1, total `/*` count 0) | ✓ |

Preprocessor dump at my tip (pasted, §Do not cut inside a conditional):

```
6: #ifdef EQT_HAS_TEXTURE_ATLAS
8: #endif
923: #ifdef EQT_HAS_TEXTURE_ATLAS
1560: #endif // EQT_HAS_TEXTURE_ATLAS
```

**The region boundaries are exactly 923 and 1560.** Neither cut (919, 1562) lands
inside it: 919 precedes the `#ifdef`, 1562 follows the `#endif`.

⚠ **One §Measured omission, not a defect but worth recording.** §Measured's
file-scope grep pattern (`^static |^struct |…`) cannot see the file's **three
namespace-scope free functions** — `heightToColor` (43), `normalToColor` (67),
`estimateMeshSizeBytes` (79) — because their definitions begin `irr::video::…` /
`size_t`. They are **not** file-local: all three are **declared in
`include/client/graphics/eq/zone_geometry.h`** (lines 202, 203, 211) and
`estimateMeshSizeBytes` has an external consumer in
`src/client/graphics/renderer_zone_draw.cpp:1142`. **`heightToColor` is called at
line 1531, inside `buildAtlasedMesh`** — i.e. across this unit's cut. That is
safe precisely because it is header-declared, and the new TU carries the header
include; no promotion, no internal header. Had it been `static`, this split would
have been J08's defect. **Plans should grep for namespace-scope free functions,
not only `^static`.**

### 3. Correction 21 — re-derived, not cited

`grep -n '^static \|^struct \|^namespace\s*{\|^const \|^constexpr '`:

```
926:struct TessVertex {
933:struct TessTriangle {
937:static TessVertex lerpVertex(const TessVertex& a, const TessVertex& b, float t) {
954:static std::vector<TessVertex> clipPolygon(const std::vector<TessVertex>& poly,
988:static void fanTriangulate(const std::vector<TessVertex>& poly, std::vector<TessTriangle>& out) {
1004:static std::vector<TessTriangle> splitTriangleToUVCells(const TessVertex& v0,
```

Zero anonymous namespaces. Per-symbol readers, from my own greps:

| symbol | def | every reader | range holding defn | range holding readers | same? |
|---|---|---|---|---|---|
| `TessVertex` | 926 | 934, 937, 954, 957, 988, 1004, 1005, 1006, 1020, 1026, 1039, 1156, 1157, 1158 | 919-1561 | 919-1561 | ✅ |
| `TessTriangle` | 933 | 988, 991, 1004, 1050, 1080 | 919-1561 | 919-1561 | ✅ |
| `lerpVertex` | 937 | 980 | 919-1561 | 919-1561 | ✅ |
| `clipPolygon` | 954 | 1028, 1029, 1041, 1042 | 919-1561 | 919-1561 | ✅ |
| `fanTriangulate` | 988 | 1052 | 919-1561 | 919-1561 | ✅ |
| `splitTriangleToUVCells` | 1004 | 1159 (inside `buildAtlasedMesh`) | 919-1561 | 919-1561 | ✅ |

External sweep over `src/ include/ tests/` excluding `zone_geometry.cpp`
returned **nothing** (grep exit 1) — all six are genuinely file-local.

**Confirming the two-not-three call, independently.** The six entities span
926-1055 and their only consumer chain terminates in `buildAtlasedMesh`
(1061-1559) at line 1159. Both sit inside the single `#ifdef` at 923-1560.
**A third TU could only be cut inside that region**, which would (a) orphan four
`static` functions from their sole consumer — J08's defect exactly — and (b)
break the `#ifdef`/`#endif` pair. There is no third legal cut. **Two TUs is not a
conservative choice here; it is the only one.** The unsplit region is 638 lines,
and the resulting 696-line TU has 804 lines of headroom, so nothing is bought by
trying.

### 4. Comment depth at every cut — computed, not asserted

Using the `//`-stripping counter from §Correction 33:

```
depth after line 918  = 0
depth after line 919  = 0
depth after line 1561 = 0
depth after line 1562 = 0
```

Total `/*` occurrences in the file: **0**. Depth is 0 everywhere by construction.

### 5. Cut boundaries at my tip, with neighbours

Cut **919** (915-925):
```
915:              mesh->getMeshBufferCount() > 0 ? mesh->getMeshBuffer(0)->getVertexCount() : 0);
916:    return mesh;
917: }
918:
919: // ============================================================================
920: // Pre-Tessellation: Split triangles at integer UV boundaries
921: // ============================================================================
922:
923: #ifdef EQT_HAS_TEXTURE_ATLAS
```
Blank at 918 whose predecessor 917 closes `buildCompositeMesh`. J03's rule
satisfied exactly.

Cut **1562** (1556-1566):
```
1556:             totalBuffers, pageBuckets.size(), fallbackTriangles.size());
1557:
1558:    return mesh;
1559: }
1560: #endif // EQT_HAS_TEXTURE_ATLAS
1561:
1562: // ============================================================================
1563: // Performance: Lazy Texture Loading (Phase 3)
1564: // ============================================================================
```
As the plan warned, the predecessor is an `#endif`, not a `}`.

### 6. ⚠ Item 12 — where the `#endif` went

**`#endif // EQT_HAS_TEXTURE_ATLAS` (original 1560) and the blank at 1561 are
both in `zone_geometry_atlased.cpp`**, at its lines 693 and 694. Verified by the
per-TU preprocessor dumps:

```
zone_geometry_atlased.cpp:   20: #ifdef EQT_HAS_TEXTURE_ATLAS
                             22: #endif
                             56: #ifdef EQT_HAS_TEXTURE_ATLAS
                            693: #endif // EQT_HAS_TEXTURE_ATLAS
zone_geometry.cpp:            8: #ifdef EQT_HAS_TEXTURE_ATLAS
                             10: #endif
```

**Both files are balanced** — 2 pairs in the atlas TU, 1 pair in the stays TU,
and no unmatched directive in either.

⚠ **`grep -c 'EQT_HAS_TEXTURE_ATLAS'` is NOT a matched-pair check and the plan's
item 12 should not ask for it.** It returns **3** for the atlas TU and **1** for
the stays TU, because the bare `#endif` at original line 8 carries no trailing
comment. The counts are *asymmetric by construction* and would read as an
unbalanced fence to anyone taking them at face value. **The `awk` preprocessor
dump above is the correct check** and is what this Review certifies on.

### 7. File-header comment lines written — every one, enumerated

`zone_geometry_atlased.cpp` lines 1-14 (then a blank at 15):
```
 1: // zone_geometry_atlased.cpp -- the texture-atlas half of ZoneMeshBuilder.
 2: //
 3: // J32 (Batch J, file-size reclamation) lifted this out of zone_geometry.cpp
 4: // verbatim. It holds the whole feature-conditional region that the atlas macro
 5: // fences off in the original file, and that region MUST NOT be split further:
 6: // six file-local entities (two tessellation structs and four static helpers)
 7: // live inside it together with their only consumer, so any cut between them
 8: // leaves undeclared statics -- and, because the conditional is on in the
 9: // default configuration, a mismatched fence would still compile here and break
10: // only where the feature is off.
11: //
12: // buildAtlasedMesh is 499 lines. It is a recorded Batch-G decomposition
13: // candidate that J32 deliberately did not touch: this is a move-only commit.
```

`zone_geometry.cpp` lines 1-2:
```
1: // Part 1 of 2: the non-atlased ZoneMeshBuilder path. The texture-atlas
2: // conditional region and buildAtlasedMesh live in zone_geometry_atlased.cpp (J32).
```

**Sixteen new lines total (14 + 2), all `//` or blank. No other comment in either
file was changed.**

**Correction 40/42 compliance:** neither header spells out a token any
text-scanning test greps for. The header describes the atlas macro *in words*
("the atlas macro", "the feature-conditional region") rather than writing the
macro name, so the per-TU directive counts above stay a faithful picture of the
fence structure. No first-person token appears in either file — see §14.

⚠ **Correction 30 did NOT fire.** The atlas TU's body already ends on the
original's blank line 1561, so appending the two namespace closes produced no
double blank. Same outcome J31 reported.

### 8. Both content proofs, plus the region proof

All three run against `$BASE = 2498c3a5…`.

**(a) Byte-identical, seam-aware, three-segment re-concatenation** — assembled as
`zone_geometry.cpp[37..920]` + `zone_geometry_atlased.cpp[52..694]` +
`zone_geometry.cpp[921..996]`, i.e. original `35-918`, `919-1561`, `1562-1637`
**in that order, third segment from the stays TU's tail**:
```
1603 orig_body.txt
1603 reconcat.txt
cmp -> BYTE_PROOF_EXIT=0
```
**1,603 lines, `cmp` exit 0**, exactly as the plan predicted. The naive
`cat stays atlased` order was never attempted; the discontiguity was handled up
front, so J08's seam failure did not recur.

**(b) Line multiset** — `sort` of the same two sides:
```
diff before.txt after.txt -> MULTISET_EXIT=0
```

**(c) The whole conditional region, byte for byte** — original 923-1560 against
`zone_geometry_atlased.cpp` 56-693:
```
diff -> IFDEF_PROOF_EXIT=0
```
**638 lines, exit 0.** The 499-line `buildAtlasedMesh` and all six file-local
entities survived verbatim.

### 9. Verifier

```
BASE=2498c3a5354a7df86d9126e31a3acb787af1664c
python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" \
    --repo /mnt/projects/fable/wt/slot1
```
Run **after** committing, **with** `--repo`, **from the fork point**. Result
pasted in §Verifier output below. **Prediction was EXIT 0, +0/-0 substantive —
and it held exactly.** The derivation was sound: the copied preamble is 100%
`#include`/`#ifdef`/`#endif`/`//`/blank, there is no `extern`, `typedef` or
file-scope definition in it, and correction 36's hazard (an out-of-class static
member definition in the preamble, `daybreak_connection.cpp`'s shape) does not
apply here — grepped and absent.

### 10. Final counts

| file | lines | `ZoneMeshBuilder::` methods | other defs |
|---|---|---|---|
| `src/client/graphics/eq/zone_geometry.cpp` | **996** | **13** | 3 namespace-scope free functions (`heightToColor`, `normalToColor`, `estimateMeshSizeBytes`) |
| `src/client/graphics/eq/zone_geometry_atlased.cpp` | **696** | **1** (`buildAtlasedMesh`) | 2 structs + 4 `static` free functions |
| total | 1,692 | 14 | — |

`check_file_sizes.sh --files`: **2 ok, 0 warn, 0 fail.**

**Predicted-vs-actual, and two record errors in the plan:**

- **Line counts land exactly on the plan's arithmetic plus the header lines it
  prescribes.** 994 + 2 = **996**; 681 + 15 (14 comment + 1 blank) = **696**.
  ⚠ Note the plan's arithmetic line says `34 + 643 + 2 = 679` for the atlas TU
  while its own §The split table says **681** — the 679 figure **omits the two
  namespace OPEN lines**, which the TU obviously needs. 681 is right; the
  sentence is wrong. Trivial, but it is the kind of off-by-two that correction 20
  says will be blamed on a dropped range.
- ⚠ **§Review-must-answer item 9 predicts a 12 / 2 method split. The correct
  answer is 13 / 1.** Only `buildAtlasedMesh` is inside 919-1561; the other
  thirteen (`ZoneMeshBuilder` ctor, `buildMesh`, `buildColoredMesh`,
  `loadTextureFromBMP`, `buildTexturedMesh`, `buildTexturedMeshFromUploaded`,
  `buildCompositeMesh`, `registerLazyTexture`, `getOrLoadTexture`, `hasTexture`,
  `registerUploadedTexture`, `setConstrainedTextureCache`, `clearTextureCache`)
  all stay. `hasTexture` (1607) and `setConstrainedTextureCache` (1626) sit in
  the 1562-1637 tail, which is presumably where the miscount came from — they are
  not listed in §The split's tail row either.

Banners: 9 (stays) + 7 (atlas) = **16** = the original 13 + the 3 replicated with
the UV-notes preamble block. `grep -cE`, per correction 28.

### 11. CMake — TWELVE add-sites, all confirmed

⚠ **The dispatch brief said SEVEN. It is TWELVE**, exactly as this plan and
correction 39 say. Seven is J29's/J35's figure — the same mis-transcription
class as J31's brief (which was handed J32's twelve). **Count them yourself; the
brief is not the record.**

Nine in the root `CMakeLists.txt` (line numbers drifted +2..+8 from the plan's,
targets identical — re-derived by scanning backwards for the enclosing
`ADD_EXECUTABLE`/`SET`):

| plan line | my line | target |
|---|---|---|
| 451 | **453** | `SET(GRAPHICS_SOURCES` (opens 441) |
| 811 | **817** | `ADD_EXECUTABLE(model_viewer` (804) |
| 950 | **956** | `ADD_EXECUTABLE(s3d_dump` (944) |
| 1010 | **1016** | `ADD_EXECUTABLE(entity_composite_builder` (1004) |
| 1195 | **1201** | `ADD_EXECUTABLE(generate_surface_map` (1189) |
| 1256 | **1262** | `ADD_EXECUTABLE(zone_line_editor` (1250) |
| 1667 | **1673** | ⚠ `ADD_EXECUTABLE(gles2_zone_benchmark` (1652) — the visual-gate binary |
| 1791 | **1797** | `ADD_EXECUTABLE(gles2_shader_diagnostic` (1782) |
| 1877 | **1883** | `ADD_EXECUTABLE(gles2_composite_audit` (1868) |

Three in `tests/cmake/` fragments (correction 26 — `tests/CMakeLists.txt` was
never named):

| file:line | target |
|---|---|
| `tests/cmake/graphics.cmake:33` | `ADD_EXECUTABLE(test_graphics` (20) |
| `tests/cmake/graphics.cmake:110` | `ADD_EXECUTABLE(test_asset_loading` (99) |
| `tests/cmake/zone_integration.cmake:58` | `ADD_EXECUTABLE(test_door_loading` (47) |

All twelve edited (`grep -c zone_geometry_atlased`: 9 / 2 / 1). The
`gles2_zone_benchmark` site was hit and **the visual gate passed at the RENDER
step**, which is the observable proof it was not missed.

⚠ **A GREP TRAP WORTH BOARDING, NEW.** My first sweep was
`grep -rn -i 'zone_geometry' --include='CMakeLists.txt' --include='*.cmake' <repo>`
and it returned **only the three `tests/cmake/` hits — the root `CMakeLists.txt`
did not match, silently.** A direct `grep -n 'zone_geometry' <repo>/CMakeLists.txt`
returns all nine. This host's `grep` is `ugrep`-backed (see the standing
`\|`-is-literal note), and `-r` + `--include` on a directory argument evidently
does not consider the directory's own top-level `CMakeLists.txt` the way GNU grep
does. **Had I trusted that one command I would have found 3 of 12 and it would
have looked complete** — the same shape as correction 26's failure, from a
different cause. **Always grep the root `CMakeLists.txt` by NAME as well as
recursing.**

### 12. `tests/` and `scripts/` — both greps, pasted

```
$ grep -rn 'zone_geometry' tests/ scripts/
tests/test_door_loading.cpp:24:#include "client/graphics/eq/zone_geometry.h"
tests/test_palette_mask_plumbing.cpp:39:#include "client/graphics/eq/zone_geometry.h"
tests/test_inventory_model_view.cpp:116:        // cache for ..." — zone_geometry.cpp:273). This is a real
tests/test_wld_conversions.cpp:239:    //   - Y/Z swap: pos.Y = z, pos.Z = y (zone_geometry.cpp, the
tests/cmake/graphics.cmake:33, :110, tests/cmake/zone_integration.cmake:58
```
Two `#include` of the **header** (unaffected — no header change) and **two
comment-only citations**. Zero functional text readers. `scripts/` returns
nothing.

```
$ grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```
17 lines, listing `eq.h`, `renderer_event_receiver.cpp`,
`renderer_frame_render.cpp`, `renderer_intents.h`, `eq_bridge_intents.cpp`,
`event_bus.h`, `action_dispatcher.h`, the three `action_dispatcher*.cpp`,
`renderer_command_catalog.cpp`, `renderer_frame_input.cpp`, plus `kGraphicsDir`.
**`zone_geometry.cpp` is not among them. No repoint needed** — the plan's record
was right in both directions.

⚠ **Doc rot, recorded not acted on (correction 24, correction 10).** The two
comment citations were checked **at my base commit**:
- `tests/test_inventory_model_view.cpp:116` cites `zone_geometry.cpp:273` for a
  texture-cache log string. **Line 273 at the base is inside
  `loadTextureFromBMP` and is not that string** — already stale before this
  split, pre-existing rot, J21's successor pass.
- `tests/test_wld_conversions.cpp:239` cites the file with no line number and
  refers to the Y/Z swap in the mesh builders, which **stayed** in
  `zone_geometry.cpp`. **Still accurate after the split.**

**No citation was made wrong by J32.** `docs/module_map.md` needs one new row for
`zone_geometry_atlased.cpp` — **recorded, not acted on** (out of territory).

### 13. `test_first_person_camera_single_formula.cpp` — pasted

```
$ grep -n 'CameraMode::FirstPerson\|camera_->setPosition(\|camera_->setTarget(\|kFirstPerson\|firstPersonM\|firstPersonV\|computeFirstPersonCamera' \
      src/client/graphics/eq/zone_geometry.cpp
(no output, exit 1)
$ ... zone_geometry_atlased.cpp
(no output, exit 1)
```

`zone_geometry_atlased.cpp` enters the recursive `kGraphicsDir` sweep
automatically (it is under `src/client/graphics/`), and **matches none of the
sweep's tokens**, so it is invisible to both Part 1 (the `FirstPerson` +
`camera_->set*` offender check) and Part 2 (the at-most-one-reader tunable
count). **No test file was edited.** Confirmed empirically: `ctest` ran the
suite green.

### 14. Acceptance results

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0** (`BUILD_EXIT` captured to a unit-private exit file, read back as `0`). ~19 min under three-slot wave load. **No exit 143**, so no truncation risk — the `readelf -h` scan over `build/bin/*` was run anyway and reported **zero BAD**. |
| `scripts/build-gles2-x86.sh` | **EXIT 0**, ~11 min. **Run BEFORE `ctest`** — see below. |
| `ctest --test-dir build -j6` | **3506 / 3507 passed.** Sole failure `AudioManagerTest.PreloadCommonSounds` — the documented ALSA host quirk, and the only expected failure. `refactor/test_baseline.txt` lists no other. **Zero new failures.** 17 skips, all pre-existing. |
| **visual gate** | **PASS** (in-suite `visual_gate`, test #3504, 40.42 s). |
| goldens | `git status --porcelain` after the whole bar lists exactly **5 paths**: the two source files and the three CMake files. **No golden touched.** |
| `scripts/build-arm-noble.sh` | **EXIT 0**, ~9 min. |
| `check_file_sizes.sh --files` | **2 ok, 0 warn, 0 fail.** |

⚠ **CORRECTION 41 — WHICH ORDERING I USED. I fixed the ORDER: I ran
`scripts/build-gles2-x86.sh` to completion (exit 0) BEFORE launching `ctest`,
rather than re-running the gate standalone afterwards.** Evidence the gate was
not vacuous: `build-gles2/bin/gles2_zone_benchmark` was stamped **13:40** by that
build, and `ctest` was launched **after** its exit file appeared. **The in-suite
gate run therefore exercised a binary built from this unit's split**, including
the `gles2_zone_benchmark` add-site. No standalone re-run was needed.

The gate passing at the **render** step is the specific evidence that the
`CMakeLists.txt:1673` add-site was not missed — the failure mode the plan warned
reads like a behaviour change. No CPU-p95 near-miss was observed either, despite
the wave.

**Wall clock ~55 min**, slowest step `cmake --build -j24` at ~19 min. **Wave
concurrency stretched the tail substantially** — J01's solo figures were ~4 min
for the main build and ~4 min for gles2; both roughly tripled here, and `ctest
-j6` took 226 s against J01's 180 s. **This matches §Scheduling's warning and is
not an anomaly** (correction 14).

### 15. Deviations

**None.** The plan was executed exactly: two TUs, move-only, no header change, no
decomposition, no include trimming, no reordering, no deletion. The only content
added anywhere is the sixteen enumerated comment lines and the twelve CMake
lines.

### 16. Findings for the template

1. ⚠ **NEW GREP TRAP (§11):** `grep -rn --include='CMakeLists.txt' <dir>` **does
   not match `<dir>/CMakeLists.txt` on this host**. Found 3 of 12 add-sites and
   looked complete. Same failure shape as correction 26, different cause. **Grep
   the root file by name.**
2. ⚠ **`grep -c '<MACRO>'` is not a matched-pair check (§6).** Item 12 asks for
   one and it necessarily reports asymmetric counts here (3 vs 1) because one
   `#endif` carries no trailing comment. Replace with the `awk` directive dump.
3. ⚠ **§Measured's file-scope grep misses namespace-scope free functions (§2).**
   Three of them exist in this file, one is called across the cut, and it is only
   safe because the header declares it. A `static` in that position would have
   been J08's defect and **the audit pattern would not have seen it.**
4. **Item 9's 12/2 method prediction is wrong; it is 13/1 (§10)**, and §The
   split's tail row omits `hasTexture` and `setConstrainedTextureCache`.
5. **§The split's `34 + 643 + 2 = 679` omits the two namespace opens (§10);**
   the table's 681 is correct.
6. **The dispatch brief's add-site count was wrong for the second unit running
   (§11)** — J31 got J32's twelve, J32 got J29's seven. **The plan file, not the
   brief, is the record.**
7. **The plan's two-not-three reasoning is correct and independently confirmed
   (§3):** there is no legal third cut. Recorded so it is not re-litigated.
8. `docs/module_map.md` needs a row for `zone_geometry_atlased.cpp`. **Recorded,
   not acted on** — out of territory.
9. `buildAtlasedMesh` (499 lines) remains a Batch-G candidate, flagged in the new
   TU's header. **696-line TU, 804 lines of headroom — no ceiling problem**,
   same disposition as correction 34's for J33.

### Verifier output (run after committing `27885052`, with `--repo`, from the fork point)

```
BASE=2498c3a5354a7df86d9126e31a3acb787af1664c
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" \
      --repo /mnt/projects/fable/wt/slot1

Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
  refactor/J32_zone_geometry_split.md
  tests/cmake/graphics.cmake
  tests/cmake/zone_integration.cmake

Files verified: 2
Boilerplate: +51 / -0 lines (allowed)

PASS: move-only (modulo boilerplate).
VERIFIER_EXIT=0
```

**EXIT 0, ZERO substantive lines added or removed — the prediction held exactly,
and there is nothing to enumerate or sign off.** The 51 allowed boilerplate
additions are the replicated preamble and structure, all `#include` / `#ifdef` /
`#endif` / `//` / `namespace` / bare `}`: 14 includes with the conditional pair,
the 18-line UV-notes comment block, the two namespace opens, the two namespace
closes, and the 16 new comment lines enumerated in §7. **Nothing beyond the
predicted set appeared, so no §Process step-4 / correction-5 sign-off is owed.**

New file mode is **100644** (`chmod 644` applied before `git add`; the commit
records `create mode 100644`) — J01's spurious mode change did not recur.
