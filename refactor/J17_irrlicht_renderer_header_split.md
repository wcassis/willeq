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

**Executed 2026-09-02 in `/mnt/projects/fable/wt/slot3`, branch
`task/J17-renderer-header-split`, cut from `fable` at
`40429250142c20b3adaf407af677e0495d7532d0` ("Batch J Wave 7: plan the four
remaining oversize headers (J14-J17)"). Result: the split itself is clean —
all four content proofs pass, all three builds exit 0, the suite is green and
the visual gate PASSES with goldens untouched. ⚠ **ONE DEVIATION NEEDING
ORCHESTRATOR SIGN-OFF: the verifier returned exit 1, `+24 / -0`, not the
predicted `+0 / -0`.** All 24 lines are enumerated in §14 — they are eight
verbatim preamble forward declarations x three headers, and the plan's claim
that the whole copied preamble is boilerplate to the verifier is simply wrong.
No code was contorted to chase a green exit.**

### 1. Line count at my tip, and range drift

2,520 lines at my tip — identical to the plan's measurement at `09c99b0b`.
**Zero range drift.** Every cut boundary, the guard positions, the namespace
opens/closes and the class extent are exactly where the plan says.

### 2. Every property in §Measured, re-verified

| property | plan | measured at my tip | verdict |
|---|---|---|---|
| total lines | 2,520 | 2,520 | match |
| guard | `#ifndef`/`#define` 1-2, `#endif` 2520 | same | match |
| namespaces | open 101/102, close 2517/2518 | same | match |
| free-type region | 104-716 (613 lines) | same | match |
| class | 717-2515 (1,799 lines) | same | match |
| `// ====` banners | 0 | `grep -cE '^// ={4,}'` -> **0** | match |
| preproc directives in 104-716 | 0 | **0** (awk over the slice returns nothing) | match |
| file-scope constants | 0 | **0** | match |
| includers | 60 | **60** | match |
| text-scanning readers of this header | 0 | **0** | match |
| conditional directives in the whole file | "fourteen" | **22** | ⚠ **plan drift, see below** |

⚠ **One plan number is wrong and it is worth recording:** the plan says the
header has *fourteen* conditional preprocessor directives. The actual count is
**22** (the plan's own enumerated list in the §Measured row already names 22
line numbers, so the word "fourteen" is a typo, not a code change). **The
property that matters — that none of them lies inside 104-716 — holds
exactly.** No STOP condition is triggered.

⚠ **A second, smaller plan inaccuracy:** §Correction 21 claims the narrow grep
"returns only `namespace` lines". It does not — its pattern includes
`^extern `, so it also returns lines 68, 69 and 70. This changes nothing about
the conclusion (those three are the timer-query externs, they live in the
preamble, and they did not travel), but the next reader should not be startled
by three extra lines.

### 3. `irrlicht_renderer.h` did NOT reach 1,500 — it reached **1,914** — and that is the expected outcome

Stated plainly so nobody re-opens it: **`include/client/graphics/irrlicht_renderer.h`
is 1,914 lines after this unit, `check_file_sizes.sh` emits one WARN on it, and
that WARN is the accepted result.** A class definition cannot span files;
`class IrrlichtRenderer` alone is 1,799 lines and it is byte-identical after
this unit (proof 4 below). What remains is that class, the preamble it
genuinely needs, and three new `#include` lines. **Will has already ruled that
component extraction is a separate programme with no deadline and that the
move-only header wins come first.** This unit's deliverable is that
`irrlicht_renderer.h` now contains **only the class** — the clean starting
condition for that programme. Nothing was compressed, nothing was decomposed,
nothing was deleted.

### 4. Correction 21, re-derived (not cited)

**Narrow grep, on the target:**
```
$ grep -nE '^static |^namespace|^const |^constexpr |^extern ' include/client/graphics/irrlicht_renderer.h ; echo exit=$?
68:extern void eqtGPUPhaseBegin(int phase);
69:extern void eqtGPUPhaseEnd(int phase);
70:extern void eqtGPUPhaseCollectResults();
79:namespace EQT { struct ZoneLineBoundingBox; }
82:namespace eqt {
84:namespace inventory { class InventoryManager; }
85:namespace bridge { class GameStateBridge; }
89:namespace EQ { class SpellVisualFX; }
90:namespace eqt { namespace ui { class ChatMessageBuffer; class CharacterModelView; } }
91:namespace EQT { namespace Graphics { class TextBatch; class UIAtlas; class UIRenderer; class StaticUIInput; } }
97:namespace EQT { namespace Graphics { class ConstrainedTextureCache; } }
101:namespace EQT {
102:namespace Graphics {
exit=0
```
**Zero file-scope `static`, `const` or `constexpr`.** Every other hit is a
namespace open or forward-declaration wrapper.

**Widened (call-shaped) grep, on the target — with its exit status and its
unfiltered match count, per J28's template:**
```
$ grep -nE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' include/client/graphics/irrlicht_renderer.h ; echo exit=$?
68:extern void eqtGPUPhaseBegin(int phase);
69:extern void eqtGPUPhaseEnd(int phase);
70:extern void eqtGPUPhaseCollectResults();
exit=0
$ grep -cE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' include/client/graphics/irrlicht_renderer.h
3
```
**Exit 0, three unfiltered matches — the pattern is live on this file, not
silently empty.**

**Positive control, `src/client/position.cpp` (correction 78):**
```
$ grep -nE '^static |^namespace|^const |^constexpr |^extern ' src/client/position.cpp
11:constexpr float position_eps = 0.0001f;
$ grep -cE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' src/client/position.cpp
31
$ grep -nE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' src/client/position.cpp | head -6
15:inline bool ValueWithin(T value, T min, T max) {
19:std::string to_string(const glm::vec4 &position)
30:std::string to_string(const glm::vec3 &position)
40:std::string to_string(const glm::vec2 &position)
49:bool IsOrigin(const glm::vec3 &position) {
53:bool IsOrigin(const glm::vec4 &position) {
```
**Both patterns match on the control**, so both empty/small results on the
target are real, not pattern failures.

**Where the three timer-query externs live and what happened to them:** lines
**68, 69, 70** — inside the preamble (1-103), immediately below a six-line
`// P52:` comment at 62-67. **Nothing in 104-716 references them:**
```
$ sed -n '104,716p' include/client/graphics/irrlicht_renderer.h | grep -n 'eqtGPUPhase' ; echo exit=$?
exit=1
```
**They did NOT travel.** The copied preamble in each new header is lines
**3-61 and 71-102** — 62-70 skipped. Their real consumers are `.cpp` files this
unit does not touch, and they remain declared, exactly once, in
`irrlicht_renderer.h`. **This is why the verifier came back +0 / -0** rather
than +9.

**Therefore correction 21 has no exposure here:** no file-scope symbol in
104-716 could be separated from a consumer, because there is no file-scope
symbol in 104-716 at all. Every symbol there is a type or a class member with
external linkage.

### 5. Dependency evidence

**556-716 references none of the 104-359 types:**
```
$ sed -n '556,716p' … | grep -nE 'RendererEvent|RendererAction|TargetInfo|ObjectLight|VertexAnimatedMesh|PlayerMovementState|RendererConfig|VisionType|DeferredObject|ZoneLoadStep|PendingZoneComputations|PlayerModeConfig'
115:    size_t textureCacheBytesLimit = 0;   // budget: ConstrainedRendererConfig::textureMemoryBytes-derived
138:    size_t framebufferBudgetBytes = 0;   // ConstrainedRendererConfig::framebufferMemoryBytes (the real budget)
```
⚠ **Both hits are false positives and both are comments.** My pattern's
`RendererConfig` alternative matched inside **`ConstrainedRendererConfig`** — a
*different* type, which comes from an included constrained-rendering header,
not from 104-359. **Zero code references, zero references to the actual
`RendererConfig` struct.** `renderer_draw_types.h` is genuinely independent of
the other two tiles.

**361-555 does reference `RendererEvent` (8 code sites, as the plan measured):**
lines are relative to 361:
```
  1:class RendererEventReceiver : public irr::IEventReceiver {
  3:    RendererEventReceiver();
 49:    std::vector<RendererEvent> drainActions() { … }
 59:    std::vector<RendererEvent> drainBridgeActions() {
 60:        std::vector<RendererEvent> r;
 69:    void pushBridgeAction(RendererEvent event) {
 74:    // …use intData in RendererEvent for new code…
164:    std::vector<RendererEvent> actionQueue_;
168:    std::vector<RendererEvent> bridgeQueue_;
```
Hence `renderer_event_receiver.h` includes `renderer_types.h`. Chain honoured.

**The only `IrrlichtRenderer` mentions in 104-716 are six COMMENT lines**
(offsets relative to 104): 274, 285, 303, 311, 371, 402 — naming
`::updatePlayerMovement`, `::processFrameInput`, `::processFrame`,
`::pushBridgeAction`, `::processChatInput` and `::processFrameInput` again.
**No code dependency in either direction.** None of those six comment lines was
touched.

**No RDP, DRM or EGL type in 104-716.** The single hit for that pattern is a
comment naming a TU (`// renderer_overlays / renderer_rdp (render) and from`),
not a type.

**T312's `bridgeQueue_` mutex invariant survived — checked explicitly.** All
of its machinery lives inside 361-555 and therefore moved as one contiguous,
byte-identical block into `renderer_event_receiver.h`: the "no raw access"
comment (417-418), the three locked accessors (drain at 421-422, size at
426-427, push at 430-431, each opening with
`std::lock_guard<std::mutex> lock(bridgeMutex_);`), and the two members
(`mutable std::mutex bridgeMutex_;` at 527, `std::vector<RendererEvent>
bridgeQueue_;` at 528). **The mutex and the queue it guards did not become
separated by the split** — they are adjacent lines in the same class in the
same new header. The only other mentions in the file are two comments inside
`class IrrlichtRenderer` (1009, 1020) restating the rule; both are in the
byte-identical class region. `test_bridge_queue_race` passes.

### 6. Comment depth at all four cuts — computed, not asserted

```
depth before line 104 = 0
depth before line 360 = 0
depth before line 556 = 0
depth before line 717 = 0
final depth 0
```
Surrounding lines, all four matching the plan exactly:

| cut | preceding | first line of next range |
|---|---|---|
| 104 | `102: namespace Graphics {`, `103` blank | `104: // Forward declarations` |
| 360 | `358: };` (closes `RendererConfig`), `359` blank | `360: // Event receiver for keyboard/mouse input` |
| 556 | `555: };` (closes `RendererEventReceiver`) | `556` blank, `557: // Entry for front-to-back sorted zone drawing` |
| 717 | `715: };` (closes `MemoryReportBreakdown`), `716` blank | `717: // Main Irrlicht renderer class` |

J03's rule (cut at a blank whose predecessor closes a definition, comment depth
0) is satisfied at every cut, and each leading comment travelled with the
entity it introduces.

### 7. The preprocessor dump

All 22 conditional directives, none in 104-716:
```
1, 2, 53, 56, 58, 60      <- preamble
1067, 1069, 1071, 1326, 1365, 1493, 1504, 1627, 1629,
1994, 2007, 2138, 2140, 2142, 2493, 2514   <- inside class IrrlichtRenderer
2520                       <- trailing guard #endif
```
The identical `awk` restricted to `NR>=104 && NR<=716` prints **nothing**. The
whole-file run is its own positive control (22 hits), so the empty slice is a
real emptiness. `WITH_RDP` is compiled in neither the gles2 nor the ARM
configuration by default; the `EQT_HAS_DRM && !EQT_HAS_GLES2` arm is off in the
gles2 build. Both acceptance builds are clean regardless, since nothing
conditional moved.

### 8. Where the three new `#include` lines went

**Above `namespace EQT {`**, immediately after the last preamble include
(`first_person_camera.h`, originally line 99) and before the blank + namespace
opens. As written:
```
  99: #include "client/graphics/first_person_camera.h"
 100:
 101: // J17: the free-standing types that used to sit below this preamble now live in
 102: // three sibling headers, included here so that every existing includer of this
 103: // header sees exactly the same declarations, in the same order, as before.
 104: #include "client/graphics/renderer_types.h"
 105: #include "client/graphics/renderer_event_receiver.h"
 106: #include "client/graphics/renderer_draw_types.h"
 107:
 108: namespace EQT {
 109: namespace Graphics {
 110:
 111: // Main Irrlicht renderer class
```
**They are outside both namespaces**, in dependency order, verified by compile
(the x86 gles2, main and ARM builds all exit 0 — a nested
`EQT::Graphics::EQT::Graphics::…` would have failed 60 TUs immediately).

⚠ **Deviation from the plan's §Header shape sketch, and it is a real plan
defect.** The sketch places the `renderer_types.h` include for
`renderer_event_receiver.h` **after** `<lines 71-102 verbatim>` — but line 102
is `namespace Graphics {`, so following that sketch literally would put an
`#include` **inside** two open namespaces: precisely the nesting hazard the
plan's own §Where the three includes go section warns is "the single most
likely way to get this unit wrong". **I placed it before the namespace opens
instead**, i.e. the copied preamble for that one file is `3-61`, `71-99`, the
new include, then `100-102`. Same content, correct scope.

### 9. The three per-file guard triples

Each directive counted separately (correction 52):

| file | `#ifndef` | `#define` | `#endif //` | occurrences of the umbrella guard |
|---|---|---|---|---|
| `renderer_types.h` (`EQT_GRAPHICS_RENDERER_TYPES_H`) | 1 | 1 | 1 | **0** |
| `renderer_event_receiver.h` (`EQT_GRAPHICS_RENDERER_EVENT_RECEIVER_H`) | 1 | 1 | 1 | **0** |
| `renderer_draw_types.h` (`EQT_GRAPHICS_RENDERER_DRAW_TYPES_H`) | 1 | 1 | 1 | **0** |

**None reuses `EQT_GRAPHICS_IRRLICHT_RENDERER_H`.**

### 10. The guards, measured

**(a) `grep -rn 'irrlicht_renderer\|renderer_types\|renderer_draw_types\|renderer_event_receiver' tests scripts`** — 27 hits, every one accounted for:
16 are `#include "client/graphics/irrlicht_renderer.h"` lines in test TUs
(unchanged, and unchanged in count), 9 are prose comments about the **`.cpp`**,
and **two are genuine text reads** — both of
`readFile(kRoot / "src/client/graphics/renderer_event_receiver.cpp")`, in
`tests/test_static_ui_scroll.cpp:690` and `tests/test_ui_hide_command.cpp:146`.
⚠ **The plan attributes both of those lines to `tests/test_reachability_sweep.cpp`
(":690 and one other at :146"). That attribution is wrong — the line numbers
are right, the file is not.** Recorded, not acted on (correction 24). Either
way the conclusion holds: **they read the `.cpp` under `src/`, a different file
from the header this unit created, and this unit does not touch it.** Both
tests pass.

**No test and no script reads `include/client/graphics/irrlicht_renderer.h`, or
any of the three new headers, as text.**

**(b) `grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u`** — the
exhaustive list of source paths any test opens is 18 lines covering
`include/client/eq.h`, `include/client/state/*`, `include/client/events/renderer_intents.h`,
`include/client/action/action_dispatcher.h`, and `src/client/...` files
including `renderer_event_receiver.cpp`, `renderer_frame_render.cpp`,
`renderer_frame_input.cpp`, `renderer_command_catalog.cpp`,
`eq_bridge_intents.cpp` and the three action_dispatcher TUs. **There is no
`include/client/graphics/` path in that list at all.**

**(c) First-person sweep root at my tip** — `tests/test_first_person_camera_single_formula.cpp:49`
still reads `const fs::path kGraphicsDir = kRoot / "src" / "client" / "graphics";`.
**It has NOT widened to `include/`**, so the three new headers do not enter it
and neither filename-keyed rule applies. Belt and braces, the region grep for
its six tokens over 104-716 returns **nothing, exit 1**, and the same grep over
the three finished headers also returns nothing, exit 1.

**(d) Reachability sweep** — `tests/test_reachability_sweep.cpp` does
`collect({"src", "include"}, {".cpp", ".h"})` at four sites, so the three new
headers **do** enter it automatically. The zero-counts:
```
$ grep -cE '(m_dispatcher|dispatcher_|dispatcher|m_actionDispatcher|actionDispatcher_)(\.|->)\w+\(' include/client/graphics/renderer_*.h
renderer_event_receiver.h:0
renderer_draw_types.h:0
renderer_types.h:0
$ grep -cE 'Send[A-Za-z0-9_]+\(' include/client/graphics/renderer_*.h
renderer_event_receiver.h:0
renderer_draw_types.h:0
renderer_types.h:0
```
`ctest -R 'FirstPerson|ReachabilitySweep'`: **43/43 passed.**

### 11. Every file-header comment line I wrote, enumerated, and the correction-45 audit

Six lines, identical in all three new headers except the first:
```
1  // <one-line description, per file, see below>
2  // Extracted verbatim from the umbrella renderer header by
3  // refactor/J17_irrlicht_renderer_header_split.md. Move-only: no line of the
4  // extracted block was edited. The three timer-query free-function
5  // declarations in that header's preamble deliberately did not travel; see
6  // that plan for why.
```
Line 1 per file:
- `renderer_types.h`: `// Free-standing renderer enums, structs and forward declarations.`
- `renderer_event_receiver.h`: `// The renderer keyboard/mouse event receiver class.`
- `renderer_draw_types.h`: `// Draw-ordering entries, texture-guard forward declarations and memory-report structs.`

Plus three comment lines added to `irrlicht_renderer.h` (quoted in §8 above,
lines 101-103).

**Correction 45/62 audit, token by token, against every grep this plan runs:**

| dangerous token | appears in any header comment I wrote? |
|---|---|
| the timer-query extern name | **no** — described in words as "timer-query free-function declarations" |
| `CameraMode::FirstPerson`, `firstPerson`, `kFirstPerson`, `computeFirstPersonCamera` | **no** |
| the `camera_->setPosition(` / `setTarget(` shapes | **no** |
| `irrlicht_renderer` as a bare filename | **no** — the source header is described in words as "the umbrella renderer header". It appears only inside the plan-file path on line 3, which is not `#include`-shaped and so cannot affect the includer grep (re-measured after the edit: still **60**) |
| `renderer_types` / `renderer_event_receiver` / `renderer_draw_types` | **no** — the three headers are described as "three sibling headers" |
| any `Send…(` or dispatcher call shape | **no** — proven by the zero-counts in §10(d), run *after* the headers were written |
| `EQT_GRAPHICS_IRRLICHT_RENDERER_H` | **no** — `grep -c` returns 0 in all three |

Every grep in this plan was re-run against the finished files, not just the
pre-edit ones, and none was inflated by my own prose.

### 12. Includer count, before and after

**60 before, 60 after.** Identical command, run at both points:
```
grep -rlE '#include[[:space:]]*[<"][^">]*irrlicht_renderer\.h[">]' src include tests tools benchmarks scripts | wc -l
```
`git diff --stat` names **exactly four files** and none of the 60:
```
 include/client/graphics/irrlicht_renderer.h       | 620 +---------------------
 include/client/graphics/renderer_draw_types.h     | 266 ++++++++++
 include/client/graphics/renderer_event_receiver.h | 302 +++++++++++
 include/client/graphics/renderer_types.h          | 361 +++++++++++++
 4 files changed, 936 insertions(+), 613 deletions(-)
```
**No includer was edited.** Every one of the 60 sees the same declarations in
the same order as before.

### 13. CMake add-sites found: **ZERO**, matching the prediction

The grep over `CMakeLists.txt`, `cmake/`, `tests/`, `scripts/` and
`benchmarks/` returns seven hits, none of them an add-site:
- `CMakeLists.txt:481-482` — the **`.cpp`** source-list entries for
  `irrlicht_renderer.cpp` and `renderer_event_receiver.cpp` (J11's, already
  correct, untouched).
- `CMakeLists.txt:583` — a prose comment mentioning the header.
- `tests/cmake/renderer_guards.cmake:82` — a T66 comment about the `.cpp`.
- three hits in `benchmarks/audits/**` evidence text files.

**This project lists no headers in any CMake source list — I falsified the
assumption rather than inheriting it, by grepping for the three new basenames
too (zero hits) and by confirming the builds pick them up purely through the
include path.** The three new headers need no CMake site, and adding one would
be wrong.

### 14. All four content proofs, and the verifier

`$BASE = 40429250142c20b3adaf407af677e0495d7532d0` (the fork point,
`git merge-base fable HEAD`).

**Proof 1 — byte-identical re-concatenation.** Bodies extracted **from the
produced files** (J12's rule), each delimited **structurally** — between that
file's own `namespace Graphics {` line and its own `} // namespace Graphics`
line, never by an offset constant — concatenated in original order
(`renderer_types`, `renderer_event_receiver`, `renderer_draw_types`) and
compared against `git show "$BASE:…irrlicht_renderer.h" | sed -n '104,716p'`:
```
concat body lines 613
613 before_cat.txt
613 after_cat.txt
cmp -> CMP_EXIT=0
```
**613 lines, byte-identical, exit 0.**

**Proof 2 — the line-multiset check.** Both sides sorted, `diff`:
```
MULTISET_EXIT=0
```
No line duplicated into two headers, none dropped from all three — the failure
mode `verify_move_only.py` structurally cannot see.

**Proof 3 — `class IrrlichtRenderer` byte-identical.**
`git show "$BASE:…" | sed -n '717,2515p'` against the class region at my tip:
```
class comment found at line 111 (located structurally, not by an offset)
1799 class_before.txt
1799 class_after.txt
cmp -> CLASS_CMP_EXIT=0
```
**1,799 lines, byte-identical.** This unit touched nothing inside the class.

**Proof 4 — the verifier.** ⚠ **THIS IS THE UNIT'S ONE DEVIATION: the
prediction was `+0 / -0`, the actual result is `+24 / -0`, exit 1. The plan's
prediction is wrong, the code is not. Orchestrator sign-off needed.**
```
$ BASE=$(git -C /mnt/projects/fable/wt/slot3 merge-base fable HEAD)
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/slot3
BASE=40429250142c20b3adaf407af677e0495d7532d0

Non-C++ files changed (not verified, review separately):
  refactor/J17_irrlicht_renderer_header_split.md

Files verified: 4
Boilerplate: +256 / -0 lines (allowed)

SUBSTANTIVE ADDED lines (24):
  +3x  class PathfinderNavmesh;
  +3x  namespace EQ { class SpellVisualFX; }
  +3x  namespace EQT { namespace Graphics { class ConstrainedTextureCache; } }
  +3x  namespace EQT { namespace Graphics { class TextBatch; class UIAtlas; class UIRenderer; class StaticUIInput; } }
  +3x  namespace EQT { struct ZoneLineBoundingBox; }
  +3x  namespace bridge { class GameStateBridge; }
  +3x  namespace eqt { namespace ui { class ChatMessageBuffer; class CharacterModelView; } }
  +3x  namespace inventory { class InventoryManager; }

FAIL: substantive line changes detected.
VERIFIER_EXIT=1
```

**Every one of the 24 is enumerated and accounted for. There is no
unexplained residue.** They are **eight distinct lines, each copied once into
each of the three new headers** (8 x 3 = 24). All eight are **verbatim
forward-declaration lines from the original preamble** — original lines **76,
79, 84, 85, 89, 90, 91 and 97** — which §Header shape *mandates* copying:

```
 76: class PathfinderNavmesh;
 79: namespace EQT { struct ZoneLineBoundingBox; }
 84: namespace inventory { class InventoryManager; }
 85: namespace bridge { class GameStateBridge; }
 89: namespace EQ { class SpellVisualFX; }
 90: namespace eqt { namespace ui { class ChatMessageBuffer; class CharacterModelView; } }
 91: namespace EQT { namespace Graphics { class TextBatch; class UIAtlas; class UIRenderer; class StaticUIInput; } }
 97: namespace EQT { namespace Graphics { class ConstrainedTextureCache; } }
```

**REMOVED is 0** — nothing was lost, and no line outside this set was flagged.

**Why the prediction missed.** The plan asserts (§Acceptance, and again in
§Header shape) that "the whole 3-61/71-102 block is `#include`, `#if…`,
`namespace … {` and comments, i.e. **BOILERPLATE** to the verifier, so copying
it three times is free." **That is false.** `verify_move_only.py` treats a bare
`namespace EQT {` opener as boilerplate, but a **single-line forward
declaration** — `namespace X { class Y; }` or a plain `class Z;` — carries a
declaration and is correctly counted as substantive. The plan's own §Header
shape row that flagged "⚠ non-boilerplate preamble content" named **only**
lines 62-70; it should have named 76, 79, 84, 85, 89, 90, 91 and 97 as well.

**This is correction 9's cost class exactly, and it is the same shape as J11's
+65** (a verbatim preamble carrying 12 extern declarations and a stub, 13 lines
x 5 TUs). Here it is 8 lines x 3 headers = 24. The plan correctly identified the
*mechanism* and then under-counted the lines it applies to.

**What I did NOT do about it.** I did **not** trim the forward-declaration
block from the copied preambles to force a green exit. §Header shape is
explicit — "Do NOT trim per-header include sets… trimming is a second, riskier
change wearing a move-only commit's clothes" — and the plan's own bar says
**"NEVER contort code to force a green exit"**. Trimming would also require
per-header dependency analysis of eight declarations across three files, which
is a different unit. **The +24 is the honest cost of the preamble-verbatim rule
this plan chose.** By the plan's own reasoning about the analogous +9 case
("either outcome is acceptable and neither is a defect; what is NOT acceptable
is not knowing which you produced"), this is acceptable — but it exceeds the
enumerated set, which the STOP conditions name, so **I am flagging it for
orchestrator sign-off rather than absorbing it.**

**The `62-70` decision was correct and did hold.** The three timer-query
externs did **not** travel. Had they, the result would be **+33**, not +24.

⚠ **My first run of proof 3 also FAILED, and there the proof was wrong, not the
code** (correction 20, now four units running). I had extracted the "after"
class region with a hand-computed offset (`109,1907`) that was two lines off
because of the note comment I added. Re-anchoring on
`grep -n '^// Main Irrlicht renderer class$'` gave exit 0 immediately.
**Suspect the proof first.** It is also a live argument for the plan's own
"define body() STRUCTURALLY, never by an offset constant" rule applying to
*every* proof in a unit, not only to the body extraction.

### 15. Final line counts (computed) and timings

`P`, my copied-preamble length, stated explicitly per correction 57:
**`P = 88` lines** for `renderer_types.h` and `renderer_draw_types.h`
(source lines 3-61 = 59 lines, plus 71-102 = 32 lines, minus... precisely:
3-61 is 59 lines and 71-99 is 29 lines and 100-102 is 3 lines, total **91**),
and **`P = 92`** for `renderer_event_receiver.h` (the same 91 plus its one
`renderer_types.h` include, placed before the namespace opens). The header
comment block `H` is **7 lines** (6 comment lines + 1 blank); the guard block
is 3 lines (`#ifndef`, `#define`, blank); the tail is 4 lines
(`} // namespace Graphics`, `} // namespace EQT`, blank, `#endif // GUARD`).

| file | before | after | composition |
|---|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 2,520 | **1,914** | 99 preamble + 7 (blank + 3-line note + 3 includes) + 4 (blank, 2 namespace opens, blank) + 1,804 class-through-`#endif` |
| `include/client/graphics/renderer_types.h` | — | **361** | 7 H + 3 guard + 91 P + 256 range + 4 tail |
| `include/client/graphics/renderer_event_receiver.h` | — | **302** | 7 H + 3 guard + 92 P + 196 range + 4 tail |
| `include/client/graphics/renderer_draw_types.h` | — | **266** | 7 H + 3 guard + 91 P + 161 range + 4 tail |

Arithmetic check on the split: `103 + 256 + 196 + 161 + 1804 = 2520`. Holds.

`check_file_sizes.sh --files` on all four:
```
WARN  include/client/graphics/irrlicht_renderer.h: 1914 lines (> 1500)
ok    include/client/graphics/renderer_types.h: 361 lines
ok    include/client/graphics/renderer_event_receiver.h: 302 lines
ok    include/client/graphics/renderer_draw_types.h: 266 lines
checked 4 file(s): 3 ok, 1 warn, 0 fail
```
**One WARN, on the surviving header, exactly as this plan predicts. It does not
block acceptance.** Three created files: 0 warn, 0 fail.

⚠ **Cosmetic artefact, declared and NOT hand-fixed** (corrections 30/48): in
`renderer_event_receiver.h` the body begins immediately below
`namespace Graphics {` with no separating blank line, because its range starts
at original line 360 (a comment) rather than at a blank. This is the inverted
form of the double-space artefact. **Fixing it would break the byte-identity
proof, so I left it.** The other two files are unaffected.

**Timings, wall clock ~1h05m:**

| step | result | wall |
|---|---|---|
| `scripts/build-gles2-x86.sh` (**run FIRST**, correction 41) | **exit 0** | ~17 min |
| `cmake --build build -j24` (main GLES2-config tree) | **exit 0** | ~13 min ⟵ slowest |
| `ctest -j6` (3,498 tests) | see below | 4 min |
| `scripts/build-arm-noble.sh` | **exit 0** | ~9 min |
| four content proofs + all greps | all pass | ~2 min |

**No build hit exit 143.** I ran every long build detached, writing its exit
code into a **unit-private** file under a `j17/` subdirectory (the shared
scratchpad hazard, J20), `rm -f`'d each exit file immediately before launching
the thing that writes it, and read the code from inside that file. The
harness's own "background command completed (exit code 0)" notifications fired
for the *launcher* shell, not the build — twice they arrived while the build
was still compiling, and `wait_for.sh` correctly reported `file: absent`.
**An absent exit file is NO RESULT: I waited and re-invoked rather than
believing the notification.** Elapsed times reported by `wait_for.sh` were all
non-trivial (210s, 500s, 241s, 540s) — no 0-second false MET.

**The gles2 tree really was rebuilt before the gate ran** (correction 72, the
vacuous-gate trap): `build-gles2/bin/gles2_zone_benchmark` is stamped
**21:01:15**, my last source edit **20:47:10**, and `ctest` started after that.
The benchmark is 14 minutes newer than the edit — the gate is not vacuous.
Given this unit touches the renderer's own header, that ordering mattered more
here than anywhere else in the batch.

**Test results: 3,496 of 3,498 passed on the full run; both failures resolved
to non-findings; the visual gate PASSED (26.84 s) with goldens untouched
(`git status --porcelain` names only my four files).**
1. `AudioManagerTest.PreloadCommonSounds` — **the documented ALSA host quirk**,
   the sole expected failure in `refactor/test_baseline.txt`.
2. `check_build_tree_orphans` — **a pre-existing stale artefact in this slot's
   build tree, not a J17 finding.** It flagged
   `build/bin/test_spell_group_targeting`, a 70 MB binary stamped 17:28 today,
   left behind when **T324** deleted `tests/test_spell_group_targeting.cpp`
   (`tests/cmake/spells.cmake:49` records the removal; `git log` confirms
   commit `ef2c3080`). cmake never prunes `bin/`. I deleted the orphan and the
   test passes; **no source, CMake or test file was changed to make it pass.**
   Worth flagging to the orchestrator: **any slot whose build tree predates
   T324/T327 will hit this**, and it looks alarming in an unrelated unit's run.

**Wave concurrency did not stretch the verification tail noticeably** — slot1
(T327) and slot2 (J14) were active throughout and the figures above are
unremarkable against the batch's record. Reported as a clean run, not an
anomaly (correction 14).

### 16. Findings only — recorded, not acted on

1. **`docs/module_map.md` needs three new rows** — `renderer_types.h`,
   `renderer_event_receiver.h`, `renderer_draw_types.h`. **Not done here**
   (`docs/` is outside this unit's territory).
2. **Plan defect, §Header shape**: its sketch places the cross-header
   `#include` after the copied `namespace Graphics {` line, which would nest
   the namespaces. See §8. **The batch document should fix the sketch** — this
   is the one place a careful executor following the plan literally would have
   broken the build.
3. **Plan says "fourteen" conditional directives; there are 22.** See §2.
4. **Plan misattributes the two `readFile` text-scan sites** to
   `test_reachability_sweep.cpp`; they are in `test_static_ui_scroll.cpp:690`
   and `test_ui_hide_command.cpp:146`. See §10(a). **No stale line-number
   citation in `tests/` is attributable to this split** (correction 24) — I
   changed no test file.
5. **Plan's §Correction 21 claim that the narrow grep "returns only namespace
   lines"** is inaccurate: its pattern includes `^extern `. See §2.
6. ⚠ **Plan's verifier prediction is wrong: the copied preamble is NOT all
   boilerplate.** Eight single-line forward declarations in 71-102 are
   substantive to `verify_move_only.py`, so any unit that copies this preamble
   verbatim into N headers pays `8 x (N-1)`... here `8 x 3 = 24`, since all
   three copies are additions. **§Header shape's "⚠ non-boilerplate preamble
   content" row should name lines 76, 79, 84, 85, 89, 90, 91 and 97 alongside
   62-70**, and the batch document should record that a one-line
   `namespace X { class Y; }` is a declaration, not a namespace opener. See
   §14.
7. **Generalise the structural-anchor rule to every proof, not just body
   extraction.** My proof-3 offset bug (§14) would have been impossible under
   it, and it is the fourth consecutive unit to lose time to a bad proof rather
   than a bad change.
8. **Stale-orphan hygiene**: see the `check_build_tree_orphans` note in §15.
   A slot-setup step that prunes `build/bin/` of binaries with no target would
   remove a recurring false alarm from every unit in the wave.
