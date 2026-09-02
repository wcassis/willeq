# J17 — Batch J Wave 7: lift the free types out of `include/client/graphics/irrlicht_renderer.h` (2,520 → ~1,910 + 3 headers)

**Slot: assigned at dispatch. Branch `task/J17-irrlicht-renderer-header-split` cut from `fable`.
x86 only. No Pi hardware. No eqemu.**

Wave 7 of `refactor/J_file_size_reclamation.md`. Read, before editing anything:
**`refactor/J_file_size_reclamation.md` §Process per unit AND every correction
section (eighty numbered lessons from thirty-odd executed units; newer text wins
over older)**, then **`refactor/J11_irrlicht_renderer_split.md` §Review** — same
entity's `.cpp`, and the unit that established correction 9 (a verbatim preamble
carrying non-boilerplate content costs verifier lines) — plus
**`refactor/J28_detail_texture_atlas_split.md` §Review** and
**`refactor/J23_eq_movement_split.md` §Review**. Then
`refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## ⚠⚠ READ THIS FIRST: THIS UNIT DOES NOT REACH 1,500 AND IS NOT EXPECTED TO

**`irrlicht_renderer.h` will land at roughly 1,910 lines and that is the
correct, accepted outcome of this unit.** It is not a shortfall, it is not a
failure, and it is not something to fix by cutting more.

A class definition **cannot span files**. `class IrrlichtRenderer` alone is
**1,799 lines** (717-2515). Once every free-standing type is lifted out, what
remains is the class, its include block and the namespace it lives in — nothing
mechanical can make that smaller. Reaching 1,500 would require pimpl'ing the
private data block or extracting owned components, which turns the risk from
"a compile error" into "a subtle lifetime bug".

**Will has ruled** (recorded in `refactor/J_file_size_reclamation.md`
§Header-rule): **component extraction is approved as a separate programme with
no deadline, and the move-only header wins come first.** This unit is one of
those wins. **Its purpose is to leave `irrlicht_renderer.h` containing ONLY the
class**, which is the clean starting condition for that later programme.

**Do not treat the ~1,910-line residue as a failure. Do not decompose
`IrrlichtRenderer`. Do not "compress code to fit" — that is exactly the harm
T115 recorded and T166 exists to prevent.**
`check_file_sizes.sh` will emit a **WARN** on `irrlicht_renderer.h`.
**Report it; it does not block acceptance.** Say so plainly in your Review so
the next reader does not re-open the question.

## MOVE-ONLY. No behaviour changes, no interface changes.

The enums, structs, forward declarations and `class RendererEventReceiver` move
**verbatim**. No member is added, removed, reordered or re-signatured. No
drive-by fixes. No deletion of dead content: a J unit **records** it and
**moves it verbatim**.

## Measured at `09c99b0b` (2026-09-02, `fable` tip after T325 merged)

`include/client/graphics/irrlicht_renderer.h` — **2,520 lines** (the batch
survey's figure, unchanged). ⚠ **Re-verify at your own branch tip before you
cut** (correction 9) and report the drift in your Review rather than silently
adjusting.

| property | measured value |
|---|---|
| total lines | **2,520** |
| include guard | `EQT_GRAPHICS_IRRLICHT_RENDERER_H`, `#ifndef`/`#define` at **1-2**, `#endif` at **2520** |
| namespace | `EQT` opened at **101**, `Graphics` at **102**; closed at **2517** / **2518** |
| preamble | **1-103** — guard, ~50 `#include`s (4-51, 73, 93-94, 99), **three conditional include blocks** (`#ifdef WITH_RDP` 53-56, `#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)` 58-60), a mixed run of forward declarations (76-97), the namespace opens at 101-102, blank at 103 |
| ⚠ non-boilerplate preamble content | **lines 62-70** — a 6-line `// P52:` comment and **three `extern void eqtGPUPhase*(…)` declarations** at 68, 69, 70. **This is correction 9's case and it governs this unit's verifier prediction.** |
| free-type region | **104-716** — **613 lines**, the whole subject of this unit |
| `class IrrlichtRenderer` | **717-2515** (leading comment at 717, `class` at 718, `};` at 2515) — **1,799 lines** |
| `// ====` banner sections | ⚠ **ZERO.** `grep -cE '^// ={4,}'` returns 0. **There is no banner rule to apply here** — J03's cutting rule (correction 11) decides every cut on its own. |
| conditional preprocessor directives in 104-716 | ⚠ **ZERO.** `awk` over the region returns nothing. All fourteen directives are in the preamble (1, 2, 53, 55, 56, 58, 60) or inside the class (1067, 1069, 1071, 1326, 1365, 1493, 1504, 1627, 1629, 1994, 2007, 2138, 2140, 2142, 2493, 2514) or the trailing `#endif` (2520). |
| file-scope constants | ⚠ **ZERO.** The narrow grep returns only `namespace` lines. |
| namespace-scope free functions | **three** — the `eqtGPUPhase*` externs at 68-70, **all in the preamble, none in 104-716.** |
| includers | **60** files `#include` this header |
| text-scanning readers | ⚠ **ZERO.** See §The guards — this is the good news, and it must still be proven. |

## The split — three new headers, not two

⚠ **The dispatch sketch said two headers. Three is correct, and here is why:**
`class RendererEventReceiver` (361-555) sits **in the middle** of the free-type
region. A two-header shape would force `renderer_types.h` to hold two
*discontiguous* blocks (104-359 and 556-716), which costs the byte-identity
proof its simplest form for no benefit. Three contiguous tiles give a linear
dependency chain and a trivial re-concatenation proof. **The residue in
`irrlicht_renderer.h` is identical either way.**

| Range | Content | Target header | ≈ body lines |
|---|---|---|---|
| **1-103** | preamble (see §Header shape for what is copied and what is not) | **stays** verbatim in `irrlicht_renderer.h` | 103 |
| **104-359** | `// Forward declarations` (104) + `SkyRenderer`/`SkyLoader`/`SkyConfig`/`WeatherEffectsController` (105-108), `enum class RendererAction : uint8_t` (111-138), `struct RendererEvent` (140-144), `enum class ZoneLoadStep : uint8_t` (148-164), `struct DeferredObject` (167-173), `struct PendingZoneComputations` (176-233), `enum class VisionType` (237-241), `struct TargetInfo` (244-262), `struct ObjectLight` (265-274), `struct VertexAnimatedMesh` (277-292), `struct PlayerMovementState` (295-330), `struct PlayerModeConfig` (333-340), `struct RendererConfig` (343-358) | **new** `include/client/graphics/renderer_types.h` | **256** |
| **360-555** | the `// Event receiver for keyboard/mouse input` comment (360) and `class RendererEventReceiver : public irr::IEventReceiver` (361-555) | **new** `include/client/graphics/renderer_event_receiver.h` | **196** |
| **556-716** | blank (556), `struct SortedRegionEntry` (557-562), the `ZoneMergeBuffers` / `RegionMeshTextureGuard` / `SingleTexturePointerGuard` / `EntityMeshTextureGuard` / `ObjectNodeTextureGuard` / `DoorTextureGuard` / `ForwardingTextureGuard` forward declarations (566-589), `struct ZoneDrawEntry` (592-598), `struct MemoryReportInput` (601-656), `struct MemoryReportBreakdown` (667-715) | **new** `include/client/graphics/renderer_draw_types.h` | **161** |
| **717-2520** | the class comment, `class IrrlichtRenderer`, both namespace closes, `#endif` | **stays** in `irrlicht_renderer.h` | 1,804 |

**Arithmetic that must hold:** `103 + 256 + 196 + 161 + 1804 = 2520`. **If your
own numbers do not sum to the file's line count, you have a gap or an overlap —
find it before you build.**

⚠ **Range 556-716 BEGINS with the blank separator line** that preceded
`SortedRegionEntry`. That is deliberate: it makes the three ranges tile
`104-716` exactly with no gap, so the byte-identity proof is trivial. **Do not
"tidy" the leading blank away.**

**Dependency order is a straight chain and must be respected:**
`renderer_types.h` ← `renderer_event_receiver.h`.
`RendererEventReceiver` holds `std::vector<RendererEvent>` queues and
`drainActions()`/`drainBridgeActions()`/`pushBridgeAction()` take or return
`RendererEvent` (measured: 8 references in 361-555).
**`renderer_draw_types.h` is independent of both** — verified: 556-716
references **none** of `RendererEvent`, `RendererAction`, `TargetInfo`,
`ObjectLight`, `VertexAnimatedMesh`, `PlayerMovementState`, `RendererConfig`,
`VisionType`, `DeferredObject`, `ZoneLoadStep` or `PendingZoneComputations`.
⚠ **The only cross-references in 104-716 to `IrrlichtRenderer` itself are six
COMMENT lines** (naming `IrrlichtRenderer::updatePlayerMovement`,
`::processFrameInput`, `::processFrame`, `::processChatInput`,
`::pushBridgeAction`). **No code dependency in either direction. Re-verify and
paste that.**

**Projected finished sizes** — ⚠ correction 71: the table's column is RANGE
LENGTH, not finished file size, and correction 58 says **compute, never quote**.
Each finished file is `(your prescribed header comment block) + (its copied
preamble) + (its range) + (its namespace closes and #endif)`:

| file | expression | ≈ |
|---|---|---|
| `renderer_types.h` | `H + P + 256 + 4` | compute it |
| `renderer_event_receiver.h` | `H + P + 1 + 196 + 4` | compute it |
| `renderer_draw_types.h` | `H + P + 161 + 4` | compute it |
| `irrlicht_renderer.h` | `103 + 3 new #include lines + 1804` | **~1,910** — **over the soft limit, expected, see the box at the top** |

where `P` is the copied preamble defined in §Header shape. ⚠ **Correction 57:
state the preamble convention your numbers assume, and derive `P` from the
file you actually wrote rather than from this table.**

## Header shape — and the one enumerated exception in the preamble

**Each new header carries the original's preamble VERBATIM — §Process's rule,
which this unit follows — with EXACTLY ONE enumerated exception.**

```
// <one line naming what this header holds>
// Extracted from irrlicht_renderer.h by
// refactor/J17_irrlicht_renderer_header_split.md. Move-only: nothing was
// edited. See that plan for why the P52 timer-query declarations did not
// travel with this block.

#ifndef EQT_GRAPHICS_RENDERER_TYPES_H          // per-file guard, not the original's
#define EQT_GRAPHICS_RENDERER_TYPES_H

<lines 3-61 verbatim>
<lines 71-102 verbatim>          <-- note: 62-70 SKIPPED, see below
#include "client/graphics/renderer_types.h"   // renderer_event_receiver.h ONLY

<the verbatim range>

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RENDERER_TYPES_H
```

⚠ **THE EXCEPTION: lines 62-70 — the `// P52:` comment block and the three
`extern void eqtGPUPhase*` declarations — do NOT travel. They stay only in
`irrlicht_renderer.h`.**

**Why, stated so it is not a judgement call:** this is **correction 36's
hazard class** (a preamble can contain a declaration, and copying it verbatim
is a real change, not a copy) and **correction 9's cost** (J11's verbatim
preamble carried 12 `extern` GLES2 declarations and a `WindowManager` stub, and
paid **65 verifier lines** for it — 13 lines × 5 TUs). Here it is 3 lines × 3
headers = **+9**, for declarations that **nothing in 104-716 uses** —
**measured: `sed -n '104,716p' … | grep 'eqtGPUPhase'` returns nothing, exit
1.** Skipping them costs nothing, changes nothing, and buys a `+0/-0` verifier
result. **Re-run that grep at your tip and paste it before you skip them.**

Everything else in 3-102 is copied unchanged, **including the two conditional
include blocks (53-56, 58-60) and every forward declaration**. ⚠ **Do NOT trim
per-header include sets** — §Process is explicit, and trimming is a second,
riskier change wearing a move-only commit's clothes. The whole 3-61/71-102
block is `#include`, `#if…`, `namespace … {` and comments, i.e. **BOILERPLATE**
to the verifier, so copying it three times is free.

⚠ **Each new header gets its OWN include guard** (`EQT_GRAPHICS_RENDERER_TYPES_H`,
`EQT_GRAPHICS_RENDERER_EVENT_RECEIVER_H`, `EQT_GRAPHICS_RENDERER_DRAW_TYPES_H`).
It must **not** reuse `EQT_GRAPHICS_IRRLICHT_RENDERER_H` — that would make the
umbrella's own guard swallow the rest of the file. **Prove each guard's
`#ifndef`/`#define`/`#endif` triple with three separate counts** (correction 52:
`grep -c '<GUARD>'` is **not** a matched-pair check; count each directive
separately).

⚠ **Correction 30 and 48: replicating the namespace closes double-spaces any
file whose body already ends on a blank, and the artefact has also appeared
inverted (no blank after a namespace open).** Cosmetic, invisible to both
proofs and the verifier — **declare it; do not hand-fix it**, because fixing it
breaks the byte proof.

## Where the three includes go in `irrlicht_renderer.h` — this is load-bearing

**Put them ABOVE `namespace EQT {`** — after the last preamble include (line 99)
and before line 101 — in dependency order:

```
#include "client/graphics/renderer_types.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/renderer_draw_types.h"
```

⚠ **NOT at line 104, where the region was.** Lines 101-102 open
`namespace EQT { namespace Graphics {`, so line 104 is **inside** them, and each
new header opens those same namespaces itself. Including them there would nest
the types as `EQT::Graphics::EQT::Graphics::…`. **This is the single most likely
way to get this unit wrong.**

Declaration order is still preserved by the above-101 placement: everything in
3-99 is declared before them, and `class IrrlichtRenderer` after. **State
exactly where you put them, and verify with a compile.**

## The umbrella property — 60 includers must not change

`irrlicht_renderer.h` keeps its name, its guard, its entire preamble and
`class IrrlichtRenderer`, and gains three `#include` lines. **Every one of the
60 files that includes it sees exactly the declarations it saw before, in the
same order.** No includer is edited.

```bash
grep -rlE '#include[[:space:]]*[<"][^">]*irrlicht_renderer\.h[">]' src include tests tools benchmarks scripts | wc -l
```
Expect **60**. ⚠ **Re-count at your tip and report the number** — do not quote
it (correction 67: include counts are a derived quantity that has slipped for
four units). **The count must be the same before and after**, and
`git diff --stat` must not name any of those 60 files.

## The guards — zero text readers, and that must be PROVEN, not assumed

⚠ **`src/client/graphics/renderer_event_receiver.cpp` already exists** (J11
created it) and **two tests read it as text**
(`tests/test_reachability_sweep.cpp:690` and one other at `:146`).
**This unit's new header has a similar name and must not be confused with it.**
Those tests read the **`.cpp` under `src/`**, which this unit does not touch.
**Confirm that explicitly.**

**Run both greps at your tip and paste both** (correction 10; they are not
interchangeable — the second finds string-compare source-path scans a `readFile`
grep cannot see):
```bash
grep -rn 'irrlicht_renderer\|renderer_types\|renderer_draw_types\|renderer_event_receiver' tests scripts
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```
⚠ **`grep -rn --include=<pat>` SILENTLY RETURNS NOTHING on this host** (ugrep
7.8.4, correction 55). Never use `--include`.

At `09c99b0b`: **no test and no script reads
`include/client/graphics/irrlicht_renderer.h` as text.** The second grep's
exhaustive list of source paths any test opens contains
`src/client/graphics/renderer_event_receiver.cpp`,
`src/client/graphics/renderer_frame_render.cpp`,
`src/client/graphics/renderer_frame_input.cpp`, `include/client/eq.h`,
`include/client/action/action_dispatcher.h`,
`include/client/events/renderer_intents.h`, `include/client/state/event_bus.h`
— **and no `include/client/graphics/` path at all.** The `tests/` hits on
`irrlicht_renderer` are `#include`s and prose comments about the `.cpp`.
⚠ **Expect nothing** (corrections 73/77/80) — a hit is the surprise.

⚠ **`tests/test_first_person_camera_single_formula.cpp` sweeps recursively —
but only `kRoot / "src" / "client" / "graphics"`** (line 49). **This unit
creates files under `include/client/graphics/`, which that sweep does NOT
reach.** ⚠ **Verify that at your tip** — if the sweep root has widened to
`include/`, the two filename-keyed rules apply and your headers enter them:
- Part 1 — a file mentioning `CameraMode::FirstPerson` **and** writing
  `camera_->setPosition(` / `camera_->setTarget(` is an offender unless it is
  named `renderer_camera.cpp`.
- Part 2 — each first-person tunable may be read from at most one TU
  (`firstPersonMinLookPitchDegrees` from two: `renderer_camera.cpp` and
  `renderer_player_physics.cpp`, named as a string literal).

Check your ranges before cutting and paste the result:
```bash
sed -n '104,716p' include/client/graphics/irrlicht_renderer.h | \
  grep -nE 'CameraMode::FirstPerson|camera_->setPosition\(|camera_->setTarget\(|kFirstPerson|firstPerson|computeFirstPersonCamera'
```

⚠ **`tests/test_reachability_sweep.cpp`'s `collect({"src","include"},{".cpp",".h"})`
sweeps `include/` recursively, so your three new headers enter that sweep
automatically.** They contain no `dispatcher.foo(` or `Send*(` call shapes —
**prove it and paste the zeros:**
```bash
grep -cE '(m_dispatcher|dispatcher_|dispatcher|m_actionDispatcher|actionDispatcher_)(\.|->)\w+\(' include/client/graphics/renderer_*.h
grep -cE 'Send[A-Za-z0-9_]+\(' include/client/graphics/renderer_*.h
```

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a symbol CAN stay file-local. It does NOT
prove the plan's ranges KEEP it with its consumers.** J08's plan assigned three
statics to one range and their only consumer to another; it surfaced only at
the BUILD step.

**Run BOTH greps (correction 68 — the narrow one catches file-scope constants
the call-shaped widened one structurally cannot; neither alone is sufficient),
and run each with a positive control before believing any empty result
(correction 60):**

```bash
grep -nE '^static |^namespace|^const |^constexpr |^extern ' include/client/graphics/irrlicht_renderer.h
grep -nE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' include/client/graphics/irrlicht_renderer.h
```

⚠ **Positive control: `src/client/position.cpp`** — correction 78. Narrow
returns `11:constexpr float position_eps`; widened returns ~30 genuine free
functions. **The controls named in corrections 51 and 61
(`zone_geometry.cpp`'s `heightToColor`, `combat.cpp`'s `IsCorpseEntity`) are
STALE — J32 and J24 split those files and both symbols are gone.**

**The orchestrator audited this plan against correction 21. Result:**
- The narrow grep returns **only `namespace` lines** (79, 82, 84, 85, 89, 90,
  91, 97, 101, 102). **Zero file-scope `static`, `const` or `constexpr`.**
- The widened grep returns **exactly three lines — 68, 69, 70**, the
  `eqtGPUPhase*` externs. **All three are in the preamble (1-103), all three
  stay in `irrlicht_renderer.h`, and NOTHING in 104-716 references them**
  (measured: the region grep returns nothing, exit 1). Their real consumers are
  `.cpp` files this unit does not touch.
- **Every other symbol in 104-716 is a type or a class member with external
  linkage.** The cross-header uses this split creates —
  `RendererEventReceiver`'s `RendererEvent` queues, and
  `class IrrlichtRenderer`'s use of all three tiles — are resolved by the
  dependency chain and by `irrlicht_renderer.h` including all three headers
  ahead of the class.

**Therefore correction 21 has NO exposure in this file.** **Re-run both greps
at your tip with the control and paste them.** Do not cite this section — if a
file-scope symbol has appeared or moved since `09c99b0b`, **STOP and report**;
the unit changes shape.

## Every cut boundary — verify each at your tip

⚠ **This header has ZERO `// ====` banners**, so J03's rule (correction 11) is
the only rule: **cut at a blank line whose predecessor closes a definition, and
verify comment depth 0 at the cut.** A leading comment **travels with the entity
it introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 104 | `102: namespace Graphics {`, `103` blank | `104: // Forward declarations` |
| 360 | `358: };` (close of `RendererConfig`), `359` blank | `360: // Event receiver for keyboard/mouse input` |
| 556 | `555: };` (close of `RendererEventReceiver`) | `556` blank, `557: // Entry for front-to-back sorted zone drawing` |
| 717 | `715: };` (close of `MemoryReportBreakdown`), `716` blank | `717: // Main Irrlicht renderer class` |

**Paste the surrounding lines for every cut in your Review.**

**Compute comment depth at every cut — do not assert it.** A `//` line
containing the token `*/` makes a naive counter go negative (the trap
`eq_movement.cpp:18` set for the orchestrator's own scanner):

```bash
python3 - <<'EOF'
L=open('include/client/graphics/irrlicht_renderer.h').read().split('\n')
cuts={104,360,556,717}
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
"zero MULTI-LINE comment regions"** — what the rule needs is depth 0 at the cut,
which the counter above measures.

## The preprocessor dump — this is the one place a mis-cut is invisible

⚠ **Correction 2: a range that cuts inside an `#ifdef` STILL COMPILES in the
default configuration and breaks only in a config the acceptance bar
exercises.** This header has **fourteen** conditional directives.
**None is in 104-716 — prove that.**

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif|define)/ {print NR": "$0}' \
    include/client/graphics/irrlicht_renderer.h
```
At `09c99b0b` every hit is `< 104` or `> 716`. ⚠ **Correction 60: prove the
pattern is live** — this file *is* its own positive control here (fourteen
hits), so the risk runs the other way: **confirm the region slice
`awk 'NR>=104 && NR<=716 && …'` is genuinely empty by running the identical
`awk` over the whole file and showing every hit lies outside.**

The acceptance bar exercises the conditional configs: `build-gles2-x86.sh`
(`EQT_HAS_GLES2` — note lines 58-60 are `EQT_HAS_DRM && !EQT_HAS_GLES2`, so
that arm is *off* in the gles2 build) and `build-arm-noble.sh`. **Both must be
clean, and `WITH_RDP` is compiled in neither by default — say so.**

## CMake — expect ZERO add-sites, and prove it

```bash
grep -rn 'irrlicht_renderer\|renderer_types\|renderer_draw_types\|renderer_event_receiver' CMakeLists.txt cmake tests scripts benchmarks
```

⚠ **NEVER name `tests/CMakeLists.txt` in a grep (correction 26)** — J20 split it
into fragments under `tests/cmake/`.
⚠ **CMake commands here are UPPERCASE** (correction 12): `grep -n 'set('` and
`grep -n 'add_executable('` both return nothing, silently.
⚠ **Do not cite a CMake line number** — corrections 59 and 76 record five
consecutive units whose target names were right and whose line numbers were all
stale within a day. **Name the target; locate it yourself.**

**At `09c99b0b`, no CMake file names `irrlicht_renderer.h`** — this project does
not list headers in its source lists. The only hit is a **comment** in the root
`CMakeLists.txt` ("tree_identifier directly, see irrlicht_renderer.h"), which
needs no edit. Hits on `renderer_event_receiver.cpp` and the other renderer
`.cpp`s belong to J11 and are already correct. **The expected add-site count is
ZERO, and the three new headers need none either.** ⚠ Add-site counts in this
batch have ranged 1 to 19, and this directory has produced twelve-site units
(J10, J32) — **count your own and say so loudly if you find any.**

## File-header comments

`irrlicht_renderer.h` opens straight into its guard and has **no file-header
comment block**. Each of the three new headers gets the short block prescribed
in §Header shape; `irrlicht_renderer.h` gains a matching one-line note above
the three new includes. ⚠ **Correction 74: do not aim for a line count — carry
the required content.** That is the unit's only non-move content; **enumerate
every line of it in your Review**, exactly as J07 did. **Change no other
comment**, and in particular do not touch the six comment lines inside 104-716
that name `IrrlichtRenderer` methods.

⚠ **Correction 45/62: never quote, in a header comment, a token that any check
in this plan greps for.** The dangerous tokens here are `eqtGPUPhase`,
`CameraMode::FirstPerson`, `firstPerson`, `camera_->setPosition(`,
`irrlicht_renderer`, `renderer_types`, `renderer_event_receiver`,
`renderer_draw_types` and any `Send…(` shape. ⚠ **J30 hit exactly this in this
same directory**: the natural header warning for its colours TU would have
quoted the guard macro and `gles2WrapTexture`, inflating its own item-11 grep.
**Describe them in words**, and ⚠ **audit your own written headers against
every grep in this plan before you run one** — correction 62 records this
nearly firing on four units.

## Acceptance (the Batch J bar — every item, classified by EXIT CODE)

- ⚠ **`scripts/build-gles2-x86.sh` FIRST, BEFORE `ctest`** (corrections 41 and
  72). `scripts/ci/visual_gate.sh` runs against the **separate** `build-gles2/`
  tree; running `ctest` first tests whatever binary was there beforehand and
  **passes while proving nothing** — J25 measured a benchmark stamped 11:01
  against edits made at 11:55, and J30 confirmed it directly (14:24 vs 14:52).
  ⚠ **This unit touches the renderer's own header, so a vacuous gate here would
  be the worst possible false green in the batch.** **Build gles2 first, and
  report the benchmark's timestamp against your last edit's.**
- **build clean**: `cmake --build build -j24`. Classify by **EXIT CODE**, never
  by grepping the log — `grep ' error '` **never** matches GCC, which prints
  `error:`. Capture `BUILD_EXIT=$?` **immediately**.
- ⚠ **A long build can end three ways** (corrections 23 and 65): **exit 143**
  (SIGTERM at the 600 s tool ceiling); **backgrounded** by the tool and later
  reporting its own code; or **completed with no exit file recorded at all**.
  ⚠ **An absent exit file is NO RESULT, not a bad one** (J28, J34). Relaunch
  detached with a **unit-private exit file** and read the code from inside it.
  ⚠ **60 includers, most of them the heaviest TUs in the tree — this unit
  triggers close to a full renderer rebuild. Expect at least one 143.**
- ⚠ **After a real exit 143, scan for valid ELF magic — not size**
  (corrections 27, 29, 32, 56). Truncated files have been 50-377 MB of zeroes;
  **no size heuristic catches them.** A killed *link* makes gmake delete its
  output; a killed *compile* can leave a truncated file; a killed **`ar`** is a
  third site (`libwilleq_core.a`, four instances) that `build/bin/*` alone is
  blind to. **Scan binaries AND `lib*.a`:**
  ```bash
  for f in build/bin/* build-gles2/bin/* build/*.a build/**/lib*.a build-gles2/**/lib*.a; do
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
    necessarily** the truncated-binary trap (correction 16).
  - ⚠ `test_irrlicht_camera_command` has 8 parameterized test NAMES embedding an
    ASLR-randomised pointer; redact `72-byte object <...>` before diffing
    `ctest -N`.
  - **`ctest -R FirstPerson` and `ctest -R ReachabilitySweep` must be green** —
    see §The guards.
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
     ⚠ **Failures 1 and 2 look IDENTICAL.** **Check both before changing a line
     of code.**
  3. ⚠ **Running on an UNCOMMITTED tree.** The script prints
     **`No C++ files changed` and EXITS 0** (J06). It looks like a definitive
     pass and is not one.
  Record `$BASE` in the Review and paste the full output.
- ⚠ **Expected verifier result for J17: EXIT 0, `+0 / -0` substantive —
  BECAUSE lines 62-70 do not travel.**

  **Derivation, from what this plan prescribes and nothing else** (correction
  19 — a predicted diff size must come from the plan's own prescribed output):
  - Every moved line keeps its **exact text** and its **count of 1**, so it
    cancels (correction 54).
  - The three copied preambles are `#include`, `#if…`, `#ifndef`, `#define`,
    `namespace … {`, `}` and comments — **all BOILERPLATE**; the new guards and
    header comments likewise; the three new `#include` lines in
    `irrlicht_renderer.h` likewise. `namespace` and `}` change count 1 → 4:
    free for boilerplate.
  - ⚠ **The ONLY non-boilerplate preamble content is 68-70, and this plan
    excludes it from the copy.** If you copy it anyway, the prediction becomes
    **EXIT 1, `+9 / -0`** (3 lines × 3 headers) — J30's `+6` with one more
    header. **Either outcome is acceptable and neither is a defect; what is NOT
    acceptable is not knowing which you produced.** State which you did and
    why.
  - **Total, as prescribed: ADDED 0, REMOVED 0 → exit 0.**

  Paste the full output and enumerate every flagged line. **Any line beyond the
  enumerated set is a real finding — chase it.** **NEVER contort code to force a
  green exit** (J04 correctly refused to).
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines and allowlists a bare `}` and every
  comment, so a range duplicated into two headers, or dropped from both, is
  invisible to it. **Not optional.**
  ```bash
  cat <the 3 bodies> | sort > /tmp/after.txt
  git show "$BASE:include/client/graphics/irrlicht_renderer.h" | sed -n '104,716p' | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Define `body(header)` STRUCTURALLY** — between the file's own
  `namespace Graphics {` line and its own `} // namespace Graphics` line —
  **never by an offset constant.** J23, J27, J28, J30 and J35 all did this, and
  it is why none of them produced J08's alarming false alarm. ⚠ **Declare count
  variables `local` in your proof helpers** (correction 17): J05's non-`local`
  `n` produced a 247-line shortfall that read exactly like a dropped range.
  **When a proof fails, suspect the proof first** (correction 20) — three
  units, three proof bugs, zero real defects.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  three bodies **in original order** (`104-359`, `360-555`, `556-716`) and
  confirm the result is byte-identical to
  `git show "$BASE:include/client/graphics/irrlicht_renderer.h" | sed -n '104,716p'`.
  **613 lines. Expect `cmp` exit 0.** Extract the bodies **from the produced
  files**, not from scratch `sed` on the original, so the proof covers assembly
  as well as content (J12's rule).
- **a fourth proof, cheap and worth it here:**
  `git show "$BASE:…irrlicht_renderer.h" | sed -n '717,2515p'` against the class
  region at your tip — **byte-identical, 1,799 lines.** This is what proves the
  unit touched nothing inside the class. (J35's per-method byte diffs are the
  precedent — the strongest evidence any unit has produced that a large block
  survived relocation intact.)
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
  observed, not forecasts.** **Do not report a clean run as an anomaly.**
- `scripts/refactor/check_file_sizes.sh --files` on **every changed AND created
  file** — `include/client/graphics/irrlicht_renderer.h`,
  `renderer_types.h`, `renderer_event_receiver.h`, `renderer_draw_types.h`.
  **Expected: 0 fail, and ONE WARN — `irrlicht_renderer.h`, at ~1,910.**
  ⚠ **That WARN is the accepted outcome of this unit (see the box at the top).
  It does NOT block acceptance.** The three created files must be 0 warn, 0
  fail. **Compute every total and paste them** rather than quoting §The split
  (correction 58).
- ⚠ **`chmod 644` every CREATED file before `git add`; PRESERVE the original
  mode on `irrlicht_renderer.h`** (correction 69 and J01's inverse). A `sed >`
  pass produces 755 and records a spurious `100644 => 100755`; chmod-ing the
  *stays* file records the opposite. **Check its mode with `git ls-files -s`
  BEFORE you touch it** and confirm `git diff --raw` shows no mode change.
- **`git diff --stat` must name exactly four files** — the header and the three
  new ones. Nothing under `src/`, `tests/`, `scripts/`, `docs/`,
  `CMakeLists.txt` or `tests/cmake/`.

## Review must answer

1. **Line count at your tip** against the **2,520** this plan measured, and any
   range drift.
2. **Every property in §Measured, re-verified with your own greps and pasted.**
   If any has changed since `09c99b0b`, **STOP and report**.
3. ⚠ **A plain statement that `irrlicht_renderer.h` did not reach 1,500, the
   number it did reach, and why that is the expected outcome** — so the next
   reader does not re-open a decision Will has already made.
4. ⚠ **Correction 21, explicitly.** Paste **both** greps **and both positive
   controls**, print the widened grep's **exit status** and a **count of its
   unfiltered matches** to prove it was live (J28's is the template), then state
   where the three `eqtGPUPhase*` externs live, that nothing in 104-716 uses
   them, and that they did not travel. **Do not cite §Correction 21 — re-derive
   it.**
5. **The dependency evidence:** that 556-716 references none of the 104-359
   types, that 361-555 references `RendererEvent`, and that the only
   `IrrlichtRenderer` mentions in 104-716 are six comments. Paste the greps.
6. **Comment depth at all four cuts**, computed with the `//`-stripping counter,
   not asserted, plus the surrounding lines for each.
7. **The preprocessor dump**, pasted, showing all fourteen directives lie
   outside 104-716.
8. ⚠ **Where you put the three new `#include` lines**, and confirmation they
   are **above** `namespace EQT {` at line 101, not inside it.
9. **The three per-file guard triples**, each directive counted separately, and
   confirmation none reuses `EQT_GRAPHICS_IRRLICHT_RENDERER_H`.
10. **The guards, measured:** both `tests/`/`scripts/` greps; the first-person
    sweep's root at your tip; the region grep for its six tokens; and the
    dispatcher/`Send*` zero-counts on the three new headers.
11. **Every file-header comment line you wrote**, enumerated, plus your
    correction-45 audit of them against every grep in this plan — J30 hit
    exactly that trap in this directory.
12. **The includer count**, before and after — expect **60** unchanged, and
    expect `git diff --stat` to name none of those files.
13. **How many CMake add-sites you actually found**, against the **ZERO** this
    plan predicts.
14. **All four content proofs**, pasted, and the verifier's exit code plus
    `$BASE`. State whether the result matched the `+0/-0` prediction and
    whether lines 62-70 travelled; if it did not match, enumerate every flagged
    line before concluding anything.
15. **Final line counts of every file**, computed not quoted, with `P` (your
    copied-preamble length) stated explicitly (correction 57); wall clock;
    slowest step; and whether wave concurrency stretched the verification tail.
    ⚠ **Do not report per-header member counts as a headline** — derived counts
    have slipped for seven consecutive units while ranges were exact
    (corrections 66, 75, 79).
16. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24). `docs/module_map.md`
    needs rows for the three new headers — **record it, do not act.**

## Territory

`include/client/graphics/irrlicht_renderer.h`, the three new headers beside it
(`renderer_types.h`, `renderer_event_receiver.h`, `renderer_draw_types.h`), and
this plan file. **Nothing else** — not `src/client/graphics/*.cpp` (J11's,
J31's and J33's files, all merged — do not touch them), not
`src/client/graphics/renderer_event_receiver.cpp` (**a different file with a
confusingly similar name**), not any `tests/` file, not `tests/cmake/*.cmake`,
not `CMakeLists.txt`, not `docs/`.

⚠ **This unit shares no file with J14, J15 or J16** and can run concurrently
with any of them. It is the only Wave-7 unit with that property.

## STOP conditions

- Any cut boundary does not match §Every cut boundary at your tip and the drift
  is more than a few lines of uniform offset.
- A conditional preprocessor directive is found inside 104-716.
- Anything in 104-716 is found to reference `eqtGPUPhase*`, an RDP type, or a
  DRM/EGL type.
- The verifier reports any substantive line beyond the predicted set (or, if
  62-70 travelled, beyond the nine enumerated `extern` lines).
- Any of the four content proofs fails after you have satisfied yourself the
  proof itself is correct — in particular, if the `class IrrlichtRenderer`
  region is not byte-identical, **stop immediately**: this unit must not have
  touched it.
- The includer count changes, or `git diff --stat` names a file outside
  §Territory.
- The visual gate fails on an **image mismatch** (as opposed to the CPU p95
  metric).
- You find yourself wanting to decompose `IrrlichtRenderer`, pimpl anything,
  "compress code to fit", delete dead content, trim an include block, or add a
  header this plan does not name.

---

## Review (to be written by the executor)
