# L06 — `SlashCommandsComponent` (family: `renderer_slash_commands.cpp`, one 593-line body `processSlashCommand`)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–23 apply).
Wave 2, two-wide with L07 (disjoint TUs; both edit `irrlicht_renderer.h`,
`renderer_init.cpp`, the allowlist and `module_map.md` at their own lines).
Measured at `1ea4a59c`.

One body, `processSlashCommand`, moves WHOLE (Batch G decides its
decomposition). It is a dispatcher: it reaches `diagnostics_` (13),
`playerCamera_` (4), `zoneShader_`, `eventReceiver_`, `bridge_` and the UI
panel state members `spellbookState_` (10), `skillsPopupState_` (5),
`inventoryState_`, `lootState_`, `uiLayout_`, `newUIEnabled_`,
`eq_commands_`. The panel-state members are the FRAME family's (L11 — the
24 UI members) — reach them as `owner_.X_` and record each; do not move
them. Sibling components are `owner_.Diagnostics().X()` etc. (K06/K07 r).
`getSimWorkerDebugInfo` is L07's — if L07 has merged by your tip, write
`owner_.SimBridge().getSimWorkerDebugInfo()`; else `owner_.getSimWorkerDebugInfo()`.

Callers: `processSlashCommand` is called from the frame family
(`renderer_frame_input.cpp` — measure) and possibly the bridge; the caller
retargets to `slash_->processSlashCommand(...)`. Zero forwarders expected
unless a bridge/application caller exists — name it or there is none
(correction 23).

Text readers: `tests/cmake/commands.cmake` defines
`RENDERER_COMMAND_SOURCE=…/renderer_slash_commands.cpp` — repoint;
`tests/test_merged_help.cpp:359` and `tests/test_player_camera_component.cpp:58`
name it in comments — verify; `test_merged_help` may READ the TU for the
help table (read it before moving).

Predicted header delta: −1 declaration −banner +(scaffolding 4..6) ≈ **−2
or worse** — this unit is not about the header; it is about the 593-line
body leaving the god class. Report the measured number; a small positive
delta is acceptable here ONLY if the body moved (say so, and ratchet to
the measured count — the allowlist may not go up; if it would, STOP and
report the arithmetic).

Territory: `include/client/graphics/slash/slash_commands_component.h`
(new), `src/client/graphics/slash/` (git mv), `irrlicht_renderer.h`,
`renderer_init.cpp` (ctor line), root `CMakeLists.txt`, `tests/cmake/commands.cmake`
(the define), `tests/test_slash_commands_component.cpp` + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in the caller(s). Visual gate +
ARM NOT skippable.

## Review

Executed in `wt/slot1` on `task/L06-slash-commands-component`, branched from
`fable` at `d2de5ca5`. Every verification step passed. **The build was clean on
the FIRST compile, zero errors at any point.** L07 had NOT merged at this tip
(no `SimBridge()` on `IrrlichtRenderer`), so `getSimWorkerDebugInfo` is
`owner_.getSimWorkerDebugInfo()` as the plan's second branch prescribed.

### Headline

`processSlashCommand` (593 lines) moved WHOLE into
`EQT::Graphics::SlashCommandsComponent`. **Residual: zero moved body lines
modified, zero added beyond one `#include`.** Suite 3,658 / 3,659, the ALSA
quirk the only failure. Visual gate PASS with no golden touched.

**⚠ DEVIATION REQUIRING RATIFICATION: `irrlicht_renderer.h` GREW, 1,659 ->
1,666 (+7), and the allowlist entry was raised to match.** The plan's two rules
for this case conflict — "a small positive delta is acceptable here ONLY if the
body moved (say so, and ratchet to the measured count)" versus "the allowlist
may not go up; if it would, STOP" — and the arithmetic below shows no legal way
to land at <= 0. The unit was completed rather than STOPped so the orchestrator
has a mergeable branch to accept or reject; reverting is one line of
`scripts/refactor/file_size_allowlist.txt` plus this commit.

### Measured vs the plan's lead

| | plan (at `1ea4a59c`) | measured (at `d2de5ca5`) |
|---|---|---|
| bodies | 1 (593 lines) | **1, 593 lines, moved whole** |
| single-family members | 0 | **0** |
| `diagnostics_` reaches | 13 | **13**, all `owner_.Diagnostics().X()` |
| `playerCamera_` reaches | 4 | **4**, all `owner_.PlayerCamera().X()` |
| forwarders | 0 unless a bridge caller exists | **1** — `src/client/bridge/irrlicht_bridge.cpp:742` |
| header delta | "−2 or worse" | **+7** |

Two plan details did not survive re-measurement: the member list names
`eq_commands_`, which exists in this TU only inside a comment
("eq_commands_movement.cpp") and is not a member at all; and it omits
`chatPanelState_`, which the `/scroll` arm passes by reference. Both were
handled by measurement, not by the list.

### The `owner_` reaches into FRAME-family state — 7 members, 23 uses

The plan's instruction was to reach these as `owner_.X_` and record each; none
moved (L11 owns them, and every one is shared 10-20 ways outside this TU).

| frame-family member | uses in the moved body |
|---|---|
| `spellbookState_` | 10 |
| `skillsPopupState_` | 5 |
| `newUIEnabled_` | 3 |
| `inventoryState_` | 2 |
| `uiLayout_` | 1 |
| `chatPanelState_` | 1 |
| `lootState_` | 1 |
| **total** | **23** |

Full route census over the moved body: **80 `owner_.` uses** = 23 frame-family
members + 13 `Diagnostics()` + 4 `PlayerCamera()` + **40 calls to
`IrrlichtRenderer` methods that stayed behind**; and **13 `ctx_.` uses** =
`zoneShader` 7, `eventReceiver` 4, `bridge` 2. **Zero members moved: the slash
family owns no state whatsoever.** That is the whole reason the header delta is
positive — every other L unit paid its scaffolding out of deleted member and
accessor lines, and this family has none to delete.

### Predicted vs measured header delta, with the arithmetic

Predicted before starting, from the actual constructs the mechanism requires:

| construct | lines |
|---|---|
| `#include "client/graphics/slash/slash_commands_component.h"` | +1 |
| `friend class SlashCommandsComponent;` | +1 |
| `std::unique_ptr<SlashCommandsComponent> slash_;` | +1 |
| blank separator + 2-line note on the member block (every other component block has one) | +3 |
| one extra comment line on the forwarder naming its caller | +1 |
| declaration -> inline forwarder definition | 0 (same line) |
| declaration deleted, banner deleted | **0** |
| **predicted** | **+7** |
| **measured** | **+7** (1,659 -> 1,666) |

The last row is the one that makes this unit different from L01-L05. **Nothing
leaves this header.** `processSlashCommand`'s declaration cannot be deleted
(the bridge calls it), so it became a one-line inline forwarder in place, and
its banner is still needed. There are no member declarations to delete (zero
single-family members) and no inline accessors to take (K04 correction f found
none: every accessor the body calls is read by a test or by another family, and
the six that are read ONLY by this body -- `isManualZoneDrawEnabled`,
`isMergeZoneDrawsEnabled`, `isPlayerLightEnabled`, `isObjectLightsEnabled`,
`isDirectionalLightEnabled`, `isFrameTimingEnabled` -- carry an explicit L04
decision in the header itself: "The readable state accessors stay here -- they
read members other families share." Moving them would overturn the immediately
preceding unit's recorded ruling to buy 6 lines, and was not done).

The absolute floor, writing no comments at all and no blank line, is **+3**
(include + friend + member). L05 correction 23 ("prose about a component
belongs in the component's header") was applied as hard as it goes: all 44
lines of rationale live in `slash_commands_component.h`; the god header carries
2 comment lines total.

**Proposed correction 26 for the batch (below) records the general rule this
unit discovered.**

### The forwarder: 1, with a named real caller

| forwarder | real caller(s) on an `IrrlichtRenderer` receiver |
|---|---|
| `processSlashCommand` | `src/client/bridge/irrlicht_bridge.cpp:742` (`renderer_->processSlashCommand(d.command)`) — the production path, the game thread's `RendererCommand` bridge queue — plus `tests/test_irrlicht_camera_command.cpp` (7 sites), `tests/test_renderer_command_echo.cpp` (3), `tests/test_first_person_tunables.cpp` (5), `tests/test_ui_hide_command.cpp` (4), `tests/test_inventory_toggle.cpp` (2), `tests/test_static_ui_scroll.cpp` (2) |

This is correction 20/25's test applied: the caller is real, it is the bridge,
and the K11 rule forbids teaching `IrrlichtBridge` that a component exists. **No
`SlashCommands()` accessor was added** — nothing outside needs one, the new
component test drives the forwarder, and an accessor is a header line this unit
cannot afford.

### The text-scanning guard (L05 correction 21) — widened AND re-proved

`tests/test_merged_help.cpp`'s
`RendererCommandCatalogTest.MatchesRendererIfChainExactly` is exactly the shape
L05 warned about. It reads the TU through the `RENDERER_COMMAND_SOURCE`
compile definition and then **locates the body by owner name**:
`src.find("IrrlichtRenderer::processSlashCommand(")`, with the chain's end
found by `src.find("\nvoid IrrlichtRenderer::")`. Both spellings changed under
extraction. Read before moving, as the dispatch required.

`tests/test_merged_help.cpp`, +23/-3:

- The matcher tries BOTH owner spellings and **asserts exactly one definition**
  (`definitionsFound == 1`) — two would be the duplication the guard exists to
  stop, and one-of-two-found is what a silent vacuous pass would look like.
- The end-of-chain scan takes the EARLIEST of `\nvoid IrrlichtRenderer::` and
  `\nvoid SlashCommandsComponent::`.
- `tests/cmake/commands.cmake` repointed to
  `src/client/graphics/slash/renderer_slash_commands.cpp`.

**Proof by deliberate break, both halves, before committing:**

```
--- probe 1: a new /l06probe arm planted in the moved TU ---------------------
in if-chain but NOT in catalog: /l06probe
  fromChain.size()   Which is: 74
  fromCatalog.size() Which is: 73
[  FAILED  ] RendererCommandCatalogTest.MatchesRendererIfChainExactly

--- probe 2: a second, IrrlichtRenderer-spelled definition planted -----------
tests/test_merged_help.cpp:370: Failure
  definitionsFound   Which is: 2
  1
expected exactly one processSlashCommand() definition in
  .../src/client/graphics/slash/renderer_slash_commands.cpp, found 2
[  FAILED  ] RendererCommandCatalogTest.MatchesRendererIfChainExactly

--- both probes deleted ------------------------------------------------------
[==========] 24 tests from 3 test suites ran. [  PASSED  ] 24 tests.
```

Probe 1 proves the widened matcher still finds and PARSES the body under the
component spelling (74 literals, one more than the catalog); probe 2 proves the
new single-definition assert has teeth. Before this unit, probe 1 would have
failed with `ASSERT_NE(fnStart, npos)` instead — loudly, not vacuously — but
the drift-guard value would have been gone.

### Text readers of the moved TU (K03 c / K11 ee)

Greped `tests/`, `tests/cmake/`, `scripts/`, `tools/`, `docs/`, `refactor/` and
`CMakeLists.txt` for the basename. **The basename did not change; only the
directory moved,** so every comment reference stayed correct. Four files needed
a path edit and one of them is the only parser:

| file | edit |
|---|---|
| `tests/cmake/commands.cmake:126` | `RENDERER_COMMAND_SOURCE` repointed (the parser above) |
| `CMakeLists.txt:496` | source list repointed |
| `include/client/graphics/ui/CLAUDE.md:120` | path + owner name (`DocCitations` caught it) |
| `refactor/BOARD.md:128` (B013) | path (`DocCitations` caught it) |

`tests/test_merged_help.cpp:359` and `tests/test_player_camera_component.cpp:58`
name the basename in COMMENTS only, as the plan said — verified, both still
accurate, untouched. `scripts/` and `tools/` have no reader at all.

**`DocCitations` is a fifth text reader class this batch had not hit: a doc
citation of a MOVED SOURCE PATH.** It failed the first full suite run with two
`file does not exist` lines and named both. Cheap, but it means the K03 (c)
grep must cover `docs/`, `refactor/` and every `CLAUDE.md`, not just `tests/`.

### §5: nothing deleted, nothing zero-caller

The family is one public method with a live production caller, so §5 never
fires. K12 (ff) checked: the moved body contains **no `pushEvent` of its own**
outside the `chat()` lambda, which publishes `GameEventType::SystemMessage` and
moved with it — no event was stranded, and `ReachabilitySweep.EveryEventIs
PublishedAndConsumed` passes unchanged.

`friend class SlashCommandsComponent;` is required on **axis 1 only** of K13
(ii): ten private `owner_.X_` members. The component names no `IrrlichtRenderer`
private static and no private nested type — `CameraMode` is the L05 hoist in
`renderer_types.h` and resolves unqualified inside `namespace EQT::Graphics`,
`PopupType` is a free enum in `client/graphics/ui/static_panels.h`. **So the
body's `CameraMode mode;` needed no substitution at all**, which is L05
correction 22 paying off one unit later exactly as predicted.

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, by name mapping
(`SlashCommandsComponent::` -> `IrrlichtRenderer::`; `ctx_.zoneShader` ->
`zoneShader_`, `ctx_.eventReceiver` -> `eventReceiver_`, `ctx_.bridge` ->
`bridge_`; `owner_.Diagnostics().` -> `diagnostics_->`; `owner_.PlayerCamera().`
-> `playerCamera_->`; `owner_.<member>_` -> `<member>_`; `owner_.<Method>(` ->
`<Method>(` for the 30 renderer methods, longest-name-first per K04 (i)), then
trailing `//` comments stripped outside string literals (K08 w), blank lines
dropped, both sides compared as multisets.

`renderer_slash_commands.cpp` -> `slash/renderer_slash_commands.cpp`:
**461 old code lines vs 462 new.**

```
removed (0):
added (1):
  #include "client/graphics/slash/slash_commands_component.h"
```

**Zero moved body lines modified.** The substitutions were applied to CODE
LINES ONLY with string literals protected (K01 correction 4 / L05 correction
25): this TU's comments quote `eq_commands_movement.cpp`, `bridge_` and
`zoneShader_` in historical notes, and a whole-file regex would have falsified
them. The TU keeps `#include "client/graphics/irrlicht_renderer.h"` (it needs
the complete type for `owner_.`), so L01 correction 4's transitive-include
rediscovery did not bite, for the fourth handle-using component in a row.

### The new test

`tests/test_slash_commands_component.cpp`, **6 tests, all pass**, registered in
`tests/cmake/renderer_guards.cmake` next to `test_player_camera_component`
(same `IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)` guard, same bare never-`Init()`'d
`IrrlichtRenderer` idiom). Every assertion is a MUTATION (L01 correction 6),
and each of the four routes the moved body now uses is pinned separately:

- `NewUiCommandWritesTheRenderersOwnMemberThroughOwner`,
  `NewInvCommandReachesTheInventoryPopupState` — the `owner_.member_` route
  into frame-family UI state.
- `RenderDistCommandWritesTheContextsMember` — `owner_.Method()` observed
  through `Context()`, and a `Context()` write read back off the renderer:
  one storage location, both directions.
- `HideUiCommandReachesTheSameDiagnosticsComponent` — interleaves
  `processSlashCommand("/hideui")` with `renderer.Diagnostics().toggleAllUI()`
  and proves they hit the same bool, i.e. `owner_.Diagnostics()` is the same
  instance the accessor returns.
- `CameraCommandReachesTheSamePlayerCameraComponent` — the same for
  `owner_.PlayerCamera()`, and it is also `cycleCameraMode()`'s only test
  route, since L05 measured that method as having no forwarder.
- `UnknownCommandIsInertAndDisturbsNothing` — the fall-through arm.

**One deviation inside the test, recorded and fixed before commit.** The first
draft asserted `/renderdist 777` stored 777; it stores **500**, because
`setRenderDistance()` caps the effective distance by
`config_.constrainedConfig.clipDistance`, whose default is 500. The body is
verbatim from before this unit (the residual proves it) — the test's
expectation was wrong, not the code. Changed to 300, with the reason in a
comment so the next reader does not "fix" it back.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0, FIRST invocation, zero `error:` in the log.** No 600 s timeout, so no truncation scan needed; `build/bin/willeq`, `build/bin/test_slash_commands_component` and `build/lib/libwilleq_core.a` all present and valid |
| `ctest --test-dir build -j6 --output-on-failure` | **3,658 / 3,659 passed**, 17 skipped, **1 failure: `AudioManagerTest.PreloadCommonSounds`** — the only permitted one. (The first run had 3: the new test's 777/500 expectation and `DocCitations`' two moved-path citations, both fixed above.) |
| the two named gates | `test_merged_help` **24/24** and `test_renderer_command_echo` pass **unchanged**. Re-run together with the hygiene gates and every command/UI test: **96/96 passed** — `FileSizeCeiling`, `DocCitations`, `ReachabilitySweep` (all four), `DuplicateStateStores`, `CameraCommandRoundTrip`, `UiHide`, `StaticUIScroll`, `InventoryToggle`, `FirstPerson*` included |
| `test_slash_commands_component` (new) | **6 tests, all pass** |
| `scripts/build-gles2-x86.sh` | **exit 0**; `build-gles2/bin/willeq` and `gles2_zone_benchmark` both valid x86-64 ELF |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`), 3 zones + 3 memory-coverage runs, every CPU p95 under ceiling (qeynos2/gfaydark/befallen). `git status --porcelain` shows **no golden path** — nothing re-blessed |
| `scripts/build-arm-noble.sh` | **exit 0**, `build-arm-noble/bin/willeq` valid 32-bit ARM ELF |
| `scripts/refactor/check_file_sizes.sh --all` | **846 checked, 846 ok, 0 warn, 0 FAIL**, with the `irrlicht_renderer.h` allowlist entry ratcheted 1,659 -> 1,666 (the deviation above) |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,659 | **1,666 (+7)** |
| `src/client/graphics/renderer_slash_commands.cpp` -> `src/client/graphics/slash/renderer_slash_commands.cpp` | 593 | 594 |
| `include/client/graphics/slash/slash_commands_component.h` | — | 72 (new) |
| `tests/test_slash_commands_component.cpp` | — | 129 (new) |
| `tests/test_merged_help.cpp` | 386 | 406 |
| `src/client/graphics/renderer_init.cpp` | 758 | 760 |

### Corrections to the Batch L template

**26. A family that owns ZERO members cannot pay for its own scaffolding, and
   its header delta is positive by construction.** Every L unit before this one
   funded its `+include/+friend/+member/+accessor` out of deleted declarations,
   deleted member lines and moved inline accessors. L06 has none of those:
   one method whose declaration must STAY (a bridge caller), zero
   single-family members, zero movable inline accessors. The floor is +3 and
   the honest figure with the batch's own comment convention is +7. **The
   ratchet rule "the allowlist may not go up" is therefore not universal — it
   is a property of families that own state.** A future L plan must decide the
   allowlist question BEFORE dispatch for any family whose measured
   single-family-member count is 0 (by the batch table: `zone_draw` and
   `gpu_upload`, both 0, are the next two), rather than leaving the executor to
   arbitrate a contradiction mid-unit.

**27. `DocCitations` is a text reader of source PATHS and must be in the K03
   (c) sweep.** Two citations of the old flat path (`include/client/graphics/
   ui/CLAUDE.md`, `refactor/BOARD.md`) failed the first suite run. The K03 (c)
   grep as written says "grep `tests/` for the moved TU's filename"; for a
   Batch L directory move it must cover `docs/`, `refactor/` and every
   `CLAUDE.md` as well. Cheap to fix, but it costs a full suite run to find.

**28. Keeping the BASENAME is worth more than keeping the path, and this is now
   twice.** L05 kept three basenames and needed zero edits to a whole `Part 2`
   of its guard; L06 kept one and every prose reference in nine files stayed
   correct — only the four machine-read paths moved. Directory-only moves
   should be the default shape for the rest of the batch.

**29. L05 correction 22's `CameraMode` hoist paid off in the very next unit.**
   `renderer_slash_commands.cpp` names `CameraMode` in the moved body and
   `EQT::Graphics::IrrlichtRenderer::CameraMode` in its file-scope helpers.
   Because L05 hoisted the enum to `renderer_types.h` and left a class-scope
   `using` alias, the component needed **zero** edits for either: the
   unqualified name resolves in `namespace EQT::Graphics` and the qualified one
   still resolves through the alias. A "qualify it as
   `IrrlichtRenderer::CameraMode`" answer would have forced this header to
   include the god header.
