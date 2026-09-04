# T330 — Four debug hotkeys have never done anything: add the four switch arms (B043)

Class A, found by L04. Small. Runs in slot3 alongside T328 (slot1, eqemu
lock) and T329 (slot2, `irrlicht_bridge.cpp`). Territory is disjoint from
both.

## Measured at `347259db`

- `RendererEventReceiver::OnEvent` queues `RendererAction::ToggleWireframe`,
  `ToggleHUD`, `ToggleNameTags` (`src/client/graphics/renderer_event_receiver.cpp`);
  `ToggleOldModels` is in the same enum (`include/client/graphics/renderer_types.h`).
- `IrrlichtRenderer::processCommonInput()` (`src/client/graphics/renderer_frame_input.cpp`)
  has NO arm for any of the four → `default: break;`.
- Implementations exist and are live: `DiagnosticsComponent::toggleWireframe`,
  `toggleHUD`, `toggleNameTags`, `toggleOldModels`, reachable as
  `diagnostics_->toggleX()` inside `IrrlichtRenderer`.
- The second route (`GraphicalMode::toggleWireframe/toggleHUD/toggleNameTags`
  → `output::IOutputRenderer*` empty defaults) is ALSO dead because
  `IrrlichtRenderer` does not implement that interface. Out of scope here;
  leave it, note it.

## Steps

1. Four arms in `processCommonInput`'s switch, mirroring the
   `ToggleZoneLights` / `CycleObjectLights` arms next to them; one line
   each (`diagnostics_->toggleWireframe(); break;` …). If `ToggleOldModels`
   is not queued by the event receiver, check `hotkey_manager.cpp` for
   whether the binding exists; add the arm anyway (the enum value is live).
2. Test: `tests/test_debug_hotkey_arms.cpp` — the existing frame-input
   tests show how to push a `RendererAction` through the receiver queue and
   drive `processCommonInput` (grep `tests/` for `ToggleZoneLights`); assert
   each of the four flips its observable state (`ctx_.wireframeMode`,
   `isUsingOldModels()`, the HUD/name-tag flags — read the diagnostics
   header for the getters). ⚠ `tests/test_ui_hide_command.cpp` and
   `tests/test_static_ui_scroll.cpp` read `renderer_frame_input.cpp` AS
   TEXT — run them; do not restructure the switch.
3. BOARD: strike B043 with the commit; the `GraphicalMode` route stays
   open as a one-line note under B043's strike (class B, "interface not
   implemented").

## Territory

`src/client/graphics/renderer_frame_input.cpp` (four arms), the new test +
its cmake (`tests/cmake/renderer_guards.cmake` or wherever the frame-input
tests live — grep), `refactor/BOARD.md`. A file under `src/client/graphics`
changes: visual gate + ARM standalone are NOT skippable.

## Verification

Build, full ctest -j6 (ALSA quirk only), `build-gles2-x86.sh`, visual gate
standalone (goldens untouched), `build-arm-noble.sh`, `check_file_sizes.sh --all`.

## Review

Done as planned; no deviations from the contract's territory.

### The four arms

`src/client/graphics/renderer_frame_input.cpp`, in `processCommonInput`'s
switch, immediately after the `ToggleZoneLights` / `CycleObjectLights` pair and
mirroring their one-line form:

    case RA::ToggleWireframe: diagnostics_->toggleWireframe(); break;
    case RA::ToggleHUD:       diagnostics_->toggleHUD();       break;
    case RA::ToggleNameTags:  diagnostics_->toggleNameTags();  break;
    case RA::ToggleOldModels: diagnostics_->toggleOldModels(); break;

(each written on one line in the file, with a four-line comment above naming
T330/B043). The switch was not restructured -- the two suites that read this TU
as text (`test_ui_hide_command.cpp`, `test_static_ui_scroll.cpp`) both pass
unchanged, including `ScrollSourceInvariants.NothingElseConsumesTheWheelAccumulator`,
which counts occurrences in this file.

Step 1's contingency did not apply: the event receiver already queues all four.
`renderer_event_receiver.cpp:181-186` has `case HA::ToggleOldModels:
actionQueue_.push_back({RA::ToggleOldModels});` alongside the other three, and
`hotkey_manager.cpp:111-116` binds all four names. The missing piece really was
only the consumer.

### The test -- tests/test_debug_hotkey_arms.cpp (6 cases, new)

Registered in `tests/cmake/ui_rendering.cmake` next to `test_ui_hide_command`,
same gate (`EQT_GRAPHICS AND IRRLICHT_LIBRARY`), same `WILLEQ_REPO_ROOT`
definition.

Behavioural, on a device-less `IrrlichtRenderer` driven through the real
consumer (`processCommonInput`, `test_ui_hide_command.cpp`'s idiom):

| case | asserts |
|---|---|
| `ToggleWireframeFlipsWireframeMode` | `Context().wireframeMode` false -> true -> false |
| `ToggleNameTagsFlipsTheNameTagFlag` | `Context().config.constrainedConfig.nameTagsEnabled` true -> false -> true |
| `ToggleOldModelsFlipsTheModelEra` | `Diagnostics().isUsingOldModels()` true -> false -> true |
| `EachArmTouchesOnlyItsOwnState` | one action moves one flag; the other three are untouched |

`toggleNameTags` and `toggleOldModels` are guarded on `ctx_.entityRenderer`,
which only `init()` builds, so the fixture installs an
`EntityRenderer(nullptr, nullptr, nullptr)` through the public
`IrrlichtRenderer::Context()`. That is safe and was checked before relying on
it: `EntityRenderer`, `RaceModelLoader`, `EquipmentModelLoader` and
`ZoneMeshBuilder` constructors only store the three pointers, and
`setNameTagsVisible` / `setUseOldModels` / `clearCache` only walk containers
that are empty here.

Source-pinned rather than behavioural, and said so in the file:

- `AllFourArmsExistInProcessCommonInput` -- the HUD arm has no behavioural
  assertion available. `toggleHUD()` writes `IrrlichtRenderer::hudEnabled_`,
  which is private with no public reader, and adding a getter is outside this
  unit's territory. All four arm texts are pinned here; the HUD one is the only
  coverage that arm has.
- `TheReceiverStillQueuesAllFour` -- the other end, so a new arm cannot be
  silently orphaned by a change to the receiver.

No negative control was run (reverting the arms and watching the behavioural
cases fail) -- it costs a full relink of ~240 binaries against a 3 GB
`libwilleq_core.a`, which is what blew the 600 s ceiling on the first build.
The causal argument was verified by grep instead: across `src/`, `include/` and
`tests/`, the only references to `RendererAction::ToggleWireframe`,
`ToggleHUD`, `ToggleNameTags` and `ToggleOldModels` are the four receiver push
sites and the four new arms. Nothing else consumes these actions, so a flip
observed after `processCommonInput` can only have come from the arm.

### Left open (boarded under B043's strike)

The second route is still dead and untouched:
`GraphicalMode::toggleWireframe/toggleHUD/toggleNameTags`
(`src/client/mode/graphical_mode.cpp:138-145`) dispatch through an
`output::IOutputRenderer*`, and `IrrlichtRenderer` does not derive from that
interface, so they reach `output_renderer.h`'s empty virtual defaults. Class B,
"interface not implemented".

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 (see note) |
| `ctest -j6 --output-on-failure` | 3643 tests, 1 failed: `AudioManagerTest.PreloadCommonSounds` (the known ALSA quirk). The 6 new cases pass. |
| `scripts/build-gles2-x86.sh` | exit 0 |
| visual gate standalone | `=== VISUAL GATE: PASS ===`; `git status --porcelain` lists only this unit's three files, no golden |
| `scripts/build-arm-noble.sh` | exit 0 |
| `scripts/refactor/check_file_sizes.sh --all` | exit 0, 841 files, 841 ok, 0 warn, 0 fail |

Build note (the T293/J07 trap, handled): the first `cmake --build` hit the
600 s ceiling and was killed (exit 143), reporting 23 targets "Terminated"
mid-link. This was the ABSENCE case, not the corruption case -- make had
deleted all 23 partial outputs (`willeq` plus 22 test binaries), so a glob-based
integrity scan found 215 artifacts with bad=0 while 23 were simply gone. The
expected-artifact list was asserted present FIRST, then validated: after the
re-run (exit 0) all 23 were back, `build/lib/libwilleq_core.a` was present and
`ar t`-readable, and every one of the 242 binaries and archives passed an ELF
check that verifies the section-header table lies inside the file (not just the
magic, which a truncated binary keeps).
