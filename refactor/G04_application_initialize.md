# G04 — `Application::initialize`: one sequence of subsystem steps becomes one `bool` step per subsystem

Batch: `refactor/G_oversize_methods.md` (G01 corrections 1–12). Two-wide
with G05 (disjoint: `src/client/application.cpp` vs `src/client/graphics/frame/`).
Measured at `810d699f`: `Application::initialize` in `src/client/application.cpp`
(~569 lines), sequential init with early `return false`s. Re-measure
mechanically: list every early return and the step boundaries the
existing banners/comments mark.

## Shape

1. Each subsystem block becomes `bool Application::initX()` (config,
   logging, database, audio, renderer/loading screen, game client, bridge
   wiring, RDP, threads…) — verbatim slice; its early `return false`
   stays inside the step; `initialize()` becomes the ordered list
   `if (!initX()) return false;`. Locals that cross steps become private
   members or are passed explicitly (record each; STOP if a lifetime
   changes).
2. Order is load-bearing (the bridge must exist before the game client;
   the renderer before `InitGraphics`) — same discipline as G03: no call
   crosses a cut; an order test pins the step sequence by parsing the TU
   (G03 correction 9: measure what the container preserves — here there
   is no container, the TU IS the order), golden extracted BEFORE the
   change, size asserted, faults planted in code (swap two step calls;
   drop one).
3. Proof: line multiset; the driving tests (`test_zoning_graphics_integration`,
   `test_headless_shutdown`, whatever names `Application::initialize` or
   constructs an `Application` — grep) pass with assertions untouched.

Predicted: `initialize` 569 → ≈ 20–30 lines; `application.h` gains ≈ 10
private declarations.

STOP: a cross-step local whose lifetime must change; a driving test's
assertion; an early-return whose semantics differ once wrapped (e.g. a
partial-init cleanup that ran after the return point — read the
shutdown path first).

Territory: `src/client/application.cpp`, `include/client/application.h`,
the new test + golden + cmake, `docs/module_map.md`. No graphics files.

## Review

Executed 2026-09-04 in `wt/slot1`, branch `task/G04-application-initialize`
off `fable` at `e00e17bb`. Structural, move-only, zero behaviour change.
**No STOP condition hit.**

### Re-measurement at the tip (correction 1: mechanical, not the plan's numbers)

The plan measured 569 lines at `810d699f`. At `e00e17bb`
`Application::initialize` spans **lines 105-691 of a 949-line
`src/client/application.cpp`** — signature line 105, closing brace 691, body
**585 lines**. A census of every column-4 statement in the body found
**ten early `return false;`** (S01 config validation, S03 required files,
S04 spell system, EverQuest-client `catch`, `createMode` failure,
`IGameMode::initialize` failure, S02 preset `validate`, E06 eqe1 path,
S02 `validateResolution`, `InitGraphics` failure) — matching the plan's
"ten early returns" exactly. Body composition: 316 statement lines,
269 blank/comment/preprocessor/brace-only lines.

### The cut: 15 steps at the file's own banners, two of them nested

Cut points are the leading comment banners already in the file; no step was
invented and none reordered. `initialize()` keeps only `m_config = config;`,
the two bookend `LOG_INFO`s and `return true;`.

| step | start | end | lines | the banner it was cut at | early `return false` |
|---|---|---|---|---|---|
| `initialize` | 105 | 130 | 26 | the ordered list | 15 (one per step) |
| `initFpCamOverrides` | 147 | 162 | 16 | `T310 Leg A: apply --fpcam overrides` | 0 |
| `initConfigValidation` | 164 | 199 | 36 | `S01: Mandatory config validation` | 1 |
| `initFrameDumpDirectory` | 201 | 218 | 18 | `T64: video-capture evidence tool (--dump-frames-dir)` | 0 |
| `initRequiredFileValidation` | 220 | 249 | 30 | `S03: Global file validation` | 1 |
| `initSignalHandlers` | 251 | 263 | 13 | `Register signal handlers` | 0 |
| `initEverQuestClient` | 265 | 343 | 79 | `T227` + `Create EverQuest client` | 1 (the `catch`) |
| `initSpellSystem` | 345 | 357 | 13 | `S04: Initialize spell system at startup` | 1 |
| `initActionLayer` | 359 | 376 | 18 | `Create action handler adapter` / `... dispatcher` / `... command processor` | 0 |
| `initScriptedCommandTour` | 378 | 388 | 11 | `H03: Scripted command tour (--script <file>)` | 0 |
| `initGameMode` | 390 | 423 | 34 | `Create game mode` | 2 |
| `initInputBridge` | 425 | 468 | 44 | `Create input action bridge` (+ `Connect input handler`, `T29 unified dispatch`) | 0 |
| `initCommandOutputRouting` | 470 | 483 | 14 | `Connect command processor output to renderer` (+ the T241 residue note) | 0 |
| `initGraphics` | 485 | 495 | 11 | `Initialize graphics if graphical mode` — the `#ifdef` + guard + the two calls below | 0 |
| ⤷ `initConstrainedRendererConfig` | 497 | 646 | 150 | `Build constrained config: preset -> JSON overrides -> CLI overrides` | 3 |
| ⤷ `initRendererAndGraphics` | 648 | 755 | 108 | `D20b5: Application creates renderer + bridge, passes to EverQuest` | 1 |
| `initConsoleBridge` | 757 | 769 | 13 | `D25: Attach console bridge in headless mode` | 0 |
| `initLoginConnection` | 771 | 782 | 12 | `Start login connection AFTER graphics init` | 0 |

Sum of the early-return column below `initialize`: **10**, every one of them,
each still inside the step whose banner it belonged to.

- **`initialize`: 587 lines -> 26** (signature, `m_config = config;`, two
  `LOG_INFO`s, a 2-line G04 note, 15 calls, `return true;`, `}` and blanks).
  Predicted 20-30: hit.
- `src/client/application.cpp`: 949 -> **1040** (+91, every line attributed
  below). `include/client/application.h`: 600 -> **628** (+28: 17
  declarations, a 9-line comment block, blanks). Predicted "≈ 10 private
  declarations" — 17, because the banners in the file number 15 and the
  graphics banner splits in two (below). `check_file_sizes.sh --all`: 862 ok,
  0 warn, 0 fail.

**Deviations from the plan's shape.** (a) The plan listed "config, logging,
database, audio, renderer/loading screen, game client, bridge wiring, RDP,
threads" — those are not the file's banners. Logging, database, audio and RDP
have **no banner of their own**: logging is configured before `initialize()`
runs, `OpenDatabase` and the eight `SetAudio*` calls are inside the
`Create EverQuest client` `try` block, and the RDP bring-up is nested four
levels deep inside the `InitGraphics` success arm. Following correction 11
("use the banners that are there"), each stayed inside the step whose banner
encloses it. (b) `initGraphics` is the one step split further: at 255 lines it
is the whole `#ifdef EQT_HAS_GRAPHICS` block, and it carries **two** banners
(`Build constrained config…` and `D20b5: Application creates renderer +
bridge…`) with a natural brace boundary between them, so it became a
three-line wrapper over two sub-steps. The wrapper keeps the `#ifdef` and the
`config.graphicsEnabled && operatingMode == GraphicalInteractive` guard, so
the two sub-steps are unreachable in an `EQT_GRAPHICS=OFF` build exactly as
their code was before.

### Cross-step locals (the batch's main STOP condition)

Enumerated mechanically: every declaration in the pre-change body, with the
line of its last use.

| local | declared | last use | crosses a cut? | disposition |
|---|---|---|---|---|
| `errors` | 126 | 152 | no (inside the S01 bare scope) | unchanged |
| `missing` | 177 | 193 | no | unchanged |
| `checkFile` (lambda) | 178 | 187 | no | unchanged |
| `dbPath` | 245 | 246 | no (its own bare scope) | unchanged |
| `modeConfig` | 327 | 350 | no | unchanged |
| `rendererType` | 336 | 343 | no | unchanged |
| `inputHandler` | 369 | 387 | no | unchanged |
| `renderer` | 401 | 403 | no | unchanged |
| `builtConfig` | 419 | 558 | no — its bare scope **closes at 560**, before the renderer banner | unchanged |
| `customMemorySpec` | 420 | 424 | no | unchanged (see below) |
| `preset` | 434 | 439 | no | unchanged |
| `eqRenderer` | 590 | 646 | no | unchanged |
| `eventReceiver` | 608 | 609 | no | unchanged |

**Not one local crosses a cut**, so no local became a member, none became a
parameter, and **no lifetime changed** — the unit's headline STOP condition is
discharged mechanically. The decisive one is `builtConfig`: it looked like it
would have to cross the constrained-config / renderer cut, and it does not,
because the pre-change code already wrapped it in its own `{ … }` block that
closes before the renderer banner. That is why the graphics split needed no
out-parameter.

**Pre-existing defect noted, not touched (move-only):** `customMemorySpec`
(line 420 pre-change, 523 post-change) is **write-only** — set to `true` in
one arm of the preset branch and never read. Dead since it was written.
Reported for the class-A list rather than fixed here.

Every step takes `const ApplicationConfig& config`; the six that do not read
it (`initFrameDumpDirectory`, `initSignalHandlers`, `initActionLayer`,
`initInputBridge`, `initCommandOutputRouting`, `initConsoleBridge`) leave the
parameter unnamed in the definition. `initFrameDumpDirectory` reads
`m_config.dumpFramesDir`, not `config` — as it did before.

### Partial-init cleanup: why wrapping the early returns is safe

Read `Application::shutdown()` (`src/client/application.cpp`) and
`src/client/application_frame.cpp` first, as the contract requires. `shutdown()`
is the only cleanup path, it is called from `~Application()`, and it is written
to be null-safe at every stage: it early-outs on `!m_running && !m_eqClient`,
then guards each teardown (`if (m_eqClient)`, `if (m_loadingThread && …)`,
`if (m_renderer)`, `if (m_gameMode)`) and `reset()`s unique_ptrs that may
already be empty. **No cleanup ran between an early `return false` and the
caller before this change, and none does now** — each early return still
returns `false` out of `initialize()` to the same caller, from the same point
in the same sequence, with the same members constructed. The only difference
is one extra `return false;` frame. `m_running` is set by
`initLoginConnection()`, i.e. after every early return, so a failed
`initialize()` leaves `m_running == false` exactly as before, and
`~Application()`'s `shutdown()` takes the early-out iff `m_eqClient` is also
null — unchanged.

### Proof (a): the line multiset

`git show fable:src/client/application.cpp` vs the new file, every line
`strip()`ed, `Counter` difference both ways:

```
REMOVED: 0 lines            <- nothing was dropped or reworded
ADDED:   91 lines
    17  }                                       (one per new step function)
    17  return true;                            (one per new step function)
    17  bool Application::initX(const ApplicationConfig& …) {
    15  if (!initX(config)) return false;        (initialize's ordered list)
     2  if (!initConstrainedRendererConfig/initRendererAndGraphics(config)) …
    18  the G04 banner above the step definitions + the 2-line note in initialize
     3  blank separators
     2  #ifdef EQT_HAS_GRAPHICS / #endif  (net: 3 emitted for the 3 graphics
        functions, 1 pre-existing pair consumed by the initGraphics wrapper)
```

Residual outside those buckets: **0 lines, both directions**. Every one of the
585 original body lines is present, verbatim, exactly once.

### Proof (b): the order golden and the planted faults

**Why a text parse and not a runtime observation** (correction 9 — check what
the container preserves first): *there is no container*. `initialize()` brings
up the whole client and cannot be run in a unit test at all — it opens
`data/willeq.db`, `stat`s the EQ client directory, installs process signal
handlers, creates an Irrlicht renderer and connects to a login server. The TU
**is** the order, so the test reads it.

`tests/goldens/application_init_order_g04.txt`: **316 entries**,
`<ordinal> <normalised source line>`, extracted mechanically from
`git show fable:src/client/application.cpp` at `e00e17bb` — from the
pre-change code, before a line of it was touched. Normalisation (mirrored
exactly in the test): strip whitespace; drop blank, `//`-comment,
preprocessor and brace-only lines.

`tests/test_application_init_order.cpp` (registered in
`tests/cmake/core_utils.cmake`; no link deps, `WILLEQ_REPO_ROOT` is global
from `tests/CMakeLists.txt`) parses every `bool Application::<name>(` at
column 0, then **composes**: walk `initialize()`'s body in order; a line
`if (!initX(config)) return false;` naming a function defined in the TU
expands *recursively* to that function's body minus its trailing
`return true;`; every other line is emitted as itself. Four cases:

1. `GoldenIsPresentAndTheRightSize` — non-empty and **== 316**, in its own
   case (correction 5 / K11 ee).
2. `EveryStepIsCalledExactlyOnceAndNoneIsOrphaned` — no step called twice, no
   `init*()` defined in the TU left unreached.
3. `InitializeIsOnlyTheOrderedList` — `initialize()` carries nothing but step
   calls, `m_config = config;`, the two `LOG_INFO(MOD_MAIN,` lines and
   `return true;`, and fewer than 40 statements. This is the anti-regrowth
   clause: work creeping back into the driver would sit outside every step.
4. `ComposedStartupOrderMatchesThePreG04Golden` — position by position; the
   failure message prints the four statements around the divergence in both
   lists.

**Planted faults, all in the CODE, both directions. Six plants, six catches,
source and golden byte-identical afterwards (`diff` clean):**

| plant | outcome |
|---|---|
| **the call-order fault** (correction 12) — swap `initEverQuestClient` ⇄ `initSpellSystem` in the ordered list; no step body edited at all | **FAILS**: `startup order diverges at statement 62`. 1 failed, 3 passed |
| **drop a step call** — delete `if (!initConsoleBridge(config)) return false;` | **FAILS** ×2: `initConsoleBridge() is defined but never called from initialize()` and `startup statement count changed: 316 -> 309` |
| **delete a real statement** inside a step (`m_dispatcher->setActionHandler(m_actionHandler.get());`) | **FAILS**: `316 -> 315` |
| **add an invented statement** inside a step (`m_fullyConnected = false;` before `m_running.store(true);`) | **FAILS**: `316 -> 317` |
| **move a statement across a cut** — hoist `m_eqClient->LoginWorld().ConnectToLogin();` from `initLoginConnection` to the top of `initGraphics` (the exact regression the "login last" comment warns about; the count is unchanged, only the position moves) | **FAILS**: `startup order diverges at statement 160` |
| (the empty-file trap, for completeness) truncate the golden to 0 bytes | **FAILS** ×2 including case 1 — nothing passes vacuously |

### Text-reader check (correction 7)

`grep` for `application.cpp` **and** `Application::initialize` across
`tests/`, `scripts/`, `tools/`, `docs/`: **no text parse and no line-number
assertion**. Five prose comments (`test_bridge_teardown.cpp`,
`test_constrained_renderer.cpp` — which names the *sequence*
`fromPreset` then `loadJsonOverrides`, still adjacent inside
`initConstrainedRendererConfig`; `test_first_person_tunables.cpp`,
`test_command_output_routing.cpp`, `test_diagnostics_component.cpp`,
`test_reply_to_tell_key.cpp`), plus `docs/module_map.md` (updated). No test
constructs an `Application` and asserts on init ordering, and **not one
assertion in any file was edited**. `docs/future/ui_fixes.md` carries two
`~line 462 / ~line 438` pointers into this file — **already stale before G04**
(both numbers land inside the old `initialize()` while the text describes
`mainLoop`'s sleep), so they are left as the approximate pointers they always
were; `DocCitations` passes.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0**. First attempt hit the 600 s Bash ceiling (exit 143). Post-143 audit per correction 8: `ar t` listed **321** members in `libwilleq_core.a` and 15 in `libEtcLib.a` (both intact this time), but **21 zero-byte test binaries** were left in `build/bin` by the kill. Deleted them and rebuilt: exit 0, `ar t` = 321, **253** files in `build/bin` |
| `ctest -j6 --output-on-failure` | **3745/3746 passed**; sole failure is the known host quirk `AudioManagerTest.PreloadCommonSounds`. 3746 tests (was 3742 — the 4 new order cases) |
| the binary still initialises | **`HeadlessShutdownTest.PipedQuitWithNoServerExitsCleanFiveTimesInARow`** and **`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied`** both **Passed**. These are the real proof: each `popen()`s the built `build/bin/willeq` with piped stdin and `--no-graphics`, so the full rebuilt `initialize()` runs end to end (config validation → EverQuest client → spell system → action layer → game mode → input bridge → console bridge → login connect) and exits 0 — five consecutive spawns in the first case |
| every `Application`-constructing test, assertions untouched | `test_video_capture_flags`, `test_first_person_tunables`, `test_headless_command_refusal`, `test_command_output_routing`, `test_headless_shutdown`, `test_bridge_teardown` — **all green, 21 cases, zero assertion edits** |
| `check_file_sizes.sh --all` | **checked 862 files: 862 ok, 0 warn, 0 fail** |
| hygiene re-run after the `docs/module_map.md` edit | `FileSizeCeiling` + `DocCitations` **2/2 passed** |
| line multiset | 0 removed, 91 added, every line attributed; residual **0** |
| planted faults | 5 plants in the code + the empty-golden trap, **6/6 caught**; source and golden restored byte-identical |
| visual gate / ARM | **skipped with evidence**: `git diff --name-only` + untracked lists exactly `docs/module_map.md`, `include/client/application.h`, `src/client/application.cpp`, `tests/cmake/core_utils.cmake`, `tests/goldens/application_init_order_g04.txt`, `tests/test_application_init_order.cpp`. **Zero files under `src/client/graphics`**, no shader, no asset, no renderer TU |

### Corrections to the Batch G template

- **(13) Before designing the split, find where the method already closes a
  bare `{ … }` scope — that is a free cut with no cross-step local.** G04's
  one genuinely dangerous local (`builtConfig`, 140 lines of construction
  feeding the texture cache and `SetConstrainedConfig`) looked like it would
  force an out-parameter across the graphics cut, and did not, purely because
  the pre-change code already scoped it. Enumerating **declaration line vs
  last-use line for every local** is one pass and settles the batch's main
  STOP condition mechanically (the G03 analogue of correction 10, for a method
  that *does* have locals).
- **(14) A "subsystem" step list dictated by the plan will not match the
  file's banners — take the banners.** G04's plan named logging, database,
  audio and RDP as steps; none of the four has a banner, and all four live
  *inside* other steps (audio and the DB open are inside the EverQuest-client
  `try`; RDP is four levels deep inside the `InitGraphics` success arm).
  Hoisting them would have moved code across cuts, which is exactly what the
  golden forbids.
- **(15) One banner-cut step may still be oversized; split it at its OWN
  second banner and keep the guard in the wrapper.** `initGraphics` was 255
  lines and carried two banners; it became an 11-line wrapper (the `#ifdef`
  plus the `graphicsEnabled && GraphicalInteractive` guard) over two
  sub-steps. Because the order test **expands step calls recursively**, the
  nested pair is pinned by the same golden with no special case.
- **(16) Add an anti-regrowth case to the order test, not just an
  order-comparison case.** `InitializeIsOnlyTheOrderedList` whitelists the
  four statement shapes the driver may contain; without it, new work appended
  to `initialize()` would sit *outside* every step and the composed order
  would still match the golden for the region the golden covers only because
  the appended lines are compared too — but a statement inserted *between*
  two step calls is caught by the golden, whereas one appended after the last
  call before `return true;` is the shape worth naming explicitly.
- **(17) The post-143 hazard is not only truncated archives (correction 8): a
  killed build also leaves ZERO-BYTE TEST BINARIES in `build/bin`.** G04's
  kill left both archives intact (321 / 15 members) and **21 zero-byte
  executables**; ctest would have run them as instant failures. `ar t` alone
  would have declared the tree healthy. Sweep `find build/bin build/lib -size
  0 -type f -delete` as well as checking archive member counts.
