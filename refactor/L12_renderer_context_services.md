# L12 — Batch L close-out: census the transition handle, retire what is cheap, board the residue

The renderer's K13. **Runs ALONE after L11.** Method: K13's, verbatim
(`refactor/K13_context_services.md`), applied to `IrrlichtRenderer`:

1. Measure `owner_.` across `src/client/graphics/*/` by target group:
   sibling-component accessors (→ `ctx_.<Family>()` pointers set after
   construction), the 39-member "below-threshold but shared" set L04/L07
   reached via `owner_` (→ promote into `RendererContext` those used by
   ≥ 3 components NOW that the components exist — re-measure breadth per
   component, not per TU family), stayed-behind methods (list each; the
   frequent ones become context services or move to the component that
   owns their state), private statics/nested types (K13 ii axis 2).
2. Remove `friend class` lines only where zero qualified-private uses
   remain; remove the `IrrlichtRenderer&` ctor parameter only where zero
   `owner_.` uses remain (`RdpComponent` qualifies today).
3. Directory move: the remaining flat `renderer_*.cpp` (`renderer_init`,
   `irrlicht_renderer.cpp`, `renderer_entities`, `renderer_entity_load_gate`,
   `renderer_event_receiver`) into `src/client/graphics/renderer/`; text
   readers repointed (correction 27's full list; `tools/hwcaps/*` comment
   citations swept — L07 deviation 1).
4. The `irrlicht_renderer.h` allowlist entry set to the measured count;
   the residue table boarded (one entry, ranked clusters, one reason each).
5. `docs/module_map.md` regenerated for `src/client/graphics/`; `CLAUDE.md`
   §"Core Components" and `src/client/graphics/CLAUDE.md` updated to name
   the components.

Also: move `measureSection()`/`sectionStart_` into `FrameComponent` (L11 hand-off, 35 sites, one substitution pass); `frameStart_` (write-only) deleted or boarded; `renderer_entity_load_gate.cpp`'s two single-family methods folded into `ZoneLoadComponent` as a pure `git mv` (L10 deviation 1).

STOP: the residue after step 1 exceeds 300; any thread/sync change.

Territory: everything under `src/client/graphics/` and
`include/client/graphics/` (call sites and the moves above), the renderer
tests' call sites, `tools/hwcaps/` comment citations, allowlist,
`docs/module_map.md`, `CLAUDE.md`, `src/client/graphics/CLAUDE.md`,
`refactor/BOARD.md`. Visual gate + ARM NOT skippable.

After L12: **Batch G** — the eight 546–880-line methods
(`DispatchZonePacket`, `updatePlayerMovement`, `registerBuiltinCommands`,
`Application::initialize`, `ProcessBridgeIntents`, `processFrameRender`,
`loadModelFromGlobalChrWithVariantsForAnimation`, `createDefaultConfig`
(a data table — leave)), each now inside a component with a bounded
header, decomposed one per unit with the same move-only proof.

## Review

Executed ALONE in `wt/slot2` on `task/L12-renderer-closeout`, branched from
`fable` at **`1c9afba7`**. Every verification step passed, no golden moved, and
both moved body sets are multiset-identical.

**THE FIRST STOP CONDITION FIRED AND IS REPORTED, NOT WORKED AROUND.** The
plan's promotion rule is "members reached via `owner_` by >= 3 components";
applied literally, the residue after step 1 is **1,065**, against a STOP
threshold of 300. Per the STOP's own diagnosis ("the plan's grouping was
wrong") the measurement is boarded as **B050** with every remaining use in a
ranked table and one reason each, and the threshold is left as a ruling for
the orchestrator rather than widened on this executor's authority. Everything
else in the unit -- the sibling pointers, the promotions the rule DOES reach,
the two hand-offs, the directory move, the friend/handle census, the docs --
was executed in full. **No thread or sync change**; the second STOP did not
fire, and the two EQE1 mutexes were deliberately left alone because of it
(deviation 3).

### The headline: `owner_.` 1,796 -> 1,065

Comment- and string-filtered census over `src/client/graphics/<family>/` +
`include/client/graphics/<family>/` for the **eleven** extracted components,
before at `1c9afba7` and after in the working tree.

| target group | before | after | became |
|---|---|---|---|
| sibling-component accessors: `Frame` 26, `Diagnostics` 24, `ZoneStream` 23, `Overlays` 21, `ZoneLifecycle` 13, `Eqe1` 11, `PlayerCamera` 10, `SimBridge` 6, `Rdp` 1, plus 4 raw `owner_.<family>_->` reaches | **139** | 0 | `ctx_.<Family>()` -- eleven raw, late-bound pointers on `RendererContext`, set by `SetComponents()` |
| the shared set at >= 3 components, 48 members: `zoneMeshNode_` 58, `fallbackMeshNode_` 35, `regionWorldTriangles_` 25, `deferredObjects_` 24, `objectTextureRebuildQueue_` 21, `zonePlaceholderNode_` 20, `doorTextureRebuildQueue_` 19, `playerHeading_` 18, `regionNeighbors_` 17, `objectRegions_` 15, `zoneReady_` 14, `eqe1Loaders_` 13, `currentVision_` 13, `pendingGlobalEQE1Archives_` 12, `weatherSystem_` 12, ... down to 3 | **549** | 0 | `ctx_.<same name without the trailing underscore>` -- promoted into `RendererContext` as references, same type, same object |
| the frame-phase stopwatch: `measureSection()` 35, `sectionStart_` 7 | **42** | 0 | `FrameComponent`'s own method and member (L11's hand-off) |
| the T109 load gate: `ensureEntityPrepWorkerStarted` 1, `deferEntityForLoadGate` 1 | **2** | 0 | `ZoneLoadComponent`'s own methods (L10 deviation 1) |
| `frameStart_` (write-only tree-wide) | **1** | 0 | deleted |
| **residue** | **1,063** | **1,065** | boarded as **B050** |
| **total** | **1,796** | **1,065** | **-731 (40.7%)** |

The residue rises by 2 rather than staying flat because the load-gate TU moved
INTO `zone_load/` and brought two `owner_.backgroundThreadPool_` reaches with
it -- they were not `owner_.` uses before, since the bodies were
`IrrlichtRenderer`'s own.

Per component, before -> after, with the two-axis friend verdict:

| component | `owner_.` before | after | axis 1 (private reaches) | axis 2 (`IrrlichtRenderer::` private names) | `friend` line | `IrrlichtRenderer&` ctor param |
|---|---|---|---|---|---|---|
| **rdp** | 0 | **0** | **0** | **0** | never had one | **REMOVED** |
| zone_stream | 62 | 19 | 19 | 0 | kept | kept |
| **slash** | 80 | 37 | **0** | **0** (`IrrlichtRenderer::CameraMode` x4 resolves through the PUBLIC using-alias) | **REMOVED** | kept -- 37 public-method reaches |
| eqe1 | 95 | 49 | 45 | 0 | kept | kept |
| sim | 107 | 59 | 57 | 0 | kept | kept |
| zone_load | 237 | 72 | 70 | 2 | kept | kept |
| overlays | 96 | 73 | 46 | 0 | kept | kept |
| player | 133 | 84 | 58 | 1 | kept | kept |
| zone_lifecycle | 255 | 136 | 121 | 5 | kept | kept |
| diagnostics | 254 | 190 | 163 | 1 | kept | kept |
| frame | 477 | 346 | 330 | 1 | kept | kept |

`SlashCommandsComponent` is the two-axis rule's real payoff: it has 37
`owner_.` uses and STILL loses its friend line, because every one of them is a
PUBLIC renderer method. K13's one-axis rule ("zero `owner_.m_` remain") would
have reached the same verdict here, but only by accident -- the axis-2 check is
what proves the `CameraMode` uses are safe. `RdpComponent` is the renderer's
`GmComponent`: zero on both axes, so the transition handle itself went.

### `RendererContext` after L12

| | count |
|---|---|
| core members (L01, referenced by >= 5 TU families) | 54 |
| promoted shared members (L12, reached by >= 3 components) | **48** |
| sibling-component pointers + accessors + `SetComponents()` | **11** |
| header lines | 137 -> **297** |

Two guarded members carry their `#ifdef` into both the struct and the ctor
initialiser list, the shape K01 correction 1 established for
`gpuUploadThread_`: `pendingVBOUploads` (`EQT_HAS_GLES2`, and it is in the
core, not this set) and `icosphereProgram` (`EQT_HAS_DRM`) -- the latter is
compiled ONLY by `build-arm-noble.sh` (batch correction 9), which ran and
exits 0 with 132 icosphere symbols in the ARM binary.

The pointers are RAW and LATE-BOUND for K13 deviation 9's reason, which
reproduces exactly: `ctx_` is brace-initialised in `IrrlichtRenderer`'s ctor
initialiser list, before any component exists, so a reference member could not
be bound; and `RendererContext` must stay an AGGREGATE, so the pointers are
public with default member initialisers and only `SetComponents()` writes
them. `ctx_.SetComponents(...)` is the line immediately after the eleventh
`make_unique`; nothing above it may reach a sibling.

### The two hand-offs, and the third thing L11 left

1. **`measureSection()`/`sectionStart_` -> `FrameComponent`, one substitution
   pass, exactly as L11's Review predicted.** 35 `owner_.measureSection()` ->
   `measureSection()` and 7 `owner_.sectionStart_` -> `sectionStart_`; the
   declaration and the member moved into `frame_component.h`; the body in
   `frame/renderer_frame.cpp` changed ONLY its qualifier
   (`IrrlichtRenderer::` -> `FrameComponent::`) because `sectionStart_` kept
   its spelling. Multiset proof below: 6 code lines in, 6 out, zero added,
   zero removed. L04's source comment predicting this move is kept as the
   record and annotated.
2. **`frameStart_` DELETED.** L11 deviation 3 measured it write-only; L12
   re-measured tree-wide across `src/`, `include/`, `tests/`, `tools/` and
   `scripts/` and confirms **exactly one reference**, the write in
   `processFrame()`. The other tree hits are `FrameBudgetGovernor`'s and
   `tools/hwcaps/zone_bench_metrics.h`'s own same-named members (K10 (t),
   fourth sighting). K12 correction (ff) was checked and does not apply -- it
   governs zero-caller METHODS that publish a `GameEventType`; a data member
   publishes nothing. Member and write both gone.
3. **`renderer_entity_load_gate.cpp`'s two bodies folded into
   `ZoneLoadComponent`** and the TU `git mv`'d into `zone_load/` (L10
   deviation 1, closed). Four substitutions (`config_` -> `ctx_.config`,
   `entityPrepWorker_` -> `ctx_.entityPrepWorker`, `entityRenderer_` ->
   `ctx_.entityRenderer`, `backgroundThreadPool_` ->
   `owner_.backgroundThreadPool_`); the two call sites in
   `renderer_zone_load_entities.cpp` dropped their `owner_.`; the two
   declarations left `irrlicht_renderer.h`. Multiset proof: 30 in, 30 out.

### The directory move

`src/client/graphics/renderer/` now holds the class's own four TUs:
`irrlicht_renderer.cpp`, `renderer_init.cpp`, `renderer_entities.cpp`,
`renderer_event_receiver.cpp`. All four are `git mv`, basenames unchanged.
`src/client/graphics/` no longer has a single flat `renderer_*.cpp`.

Text readers repointed (correction 27's full sweep -- `docs/`,
`refactor/BOARD.md` and every `CLAUDE.md`, not only `tests/`):

| reader | what |
|---|---|
| `CMakeLists.txt` | 4 source paths + the load-gate path |
| `tests/test_ui_hide_command.cpp`, `tests/test_static_ui_scroll.cpp`, `tests/test_debug_hotkey_arms.cpp` | `readFile(kRoot / "src/client/graphics/renderer/renderer_event_receiver.cpp")` -- **the K11 (ee) trap: `readFile` on a missing path returns an empty buffer that PASSES**, so these three would have gone silently vacuous |
| `docs/module_map.md` | the four TU rows, the load-gate row, the `RendererContext` paragraph, the component table |
| `refactor/BOARD.md` | one citation |
| `CLAUDE.md`, `src/client/graphics/CLAUDE.md` | rewritten, see below |
| `README.md` | one prose path |
| `include/client/graphics/frame/frame_component.h` | one comment citation |

`tools/hwcaps/gles2_zone_benchmark.cpp` cites `renderer_entities.cpp` and
`renderer_sim_bridge.cpp` by BARE BASENAME in three comments (L07 deviation
1's class) -- basenames did not change, so those citations are still correct
and were left alone. `DocCitations` passes: **414 citations, 413 ok, 0 fail,
1 skip.**

### Text guards -- widened and RE-PROVED with planted breaks (correction 21)

`test_debug_hotkey_arms.cpp`'s `AllFourArmsExistInProcessCommonInput`
**FAILED on the first full ctest run** -- correction 21's third sighting, and
this time the guard caught the unit rather than the unit catching the guard.
It knew two spellings (`diagnostics_->` and `owner_.Diagnostics().`) and L12
created a third (`ctx_.Diagnostics().`). Widened to all three with the
"exactly one matches" assertion intact, then proved live:

| planted break | result |
|---|---|
| delete `case RA::ToggleWireframe: ctx_.Diagnostics().toggleWireframe(); break;` (the NEW spelling) | **FAILS** -- the detector sees component-style code |
| add a second spelling of the `ToggleHUD` arm alongside the first | **FAILS** -- the "exactly one" half still fires |
| perturb the receiver queue line in the MOVED `renderer_event_receiver.cpp` | **FAILS** -- the repointed path really is being read |
| all three reverted | **6/6 PASS**, and `git diff` on both files is back to the unit's own substitutions only |

The other two path-reading guards (`test_ui_hide_command`,
`test_static_ui_scroll`) were repointed and pass.
`test_first_person_camera_single_formula` needed NO change: its two spellings
are `camera_->` and `ctx_.camera->`, and `camera` was already a core context
member.

### Zero-code-residual multiset proof

Reverse the substitutions by name mapping on a copy, strip blank /
comment-only / preprocessor lines and trailing `//` comments, normalise
whitespace, sort, diff the multisets against `git show 1c9afba7:<old path>`.

| moved body set | old code lines | new code lines | ADDED | REMOVED |
|---|---|---|---|---|
| `FrameComponent::measureSection` (was `IrrlichtRenderer::measureSection`, same TU) | 6 | 6 | **0** | **0** |
| `ZoneLoadComponent::ensureEntityPrepWorkerStarted` + `::deferEntityForLoadGate` (was `src/client/graphics/renderer_entity_load_gate.cpp`) | 30 | 30 | **0** | **0** |

Everything else in this unit is a receiver rewrite, not a body move: the 549
promoted reaches, the 139 accessor reaches and the 42 stopwatch reaches are
`git diff --numstat`-symmetric per file (e.g.
`frame/renderer_frame_input.cpp` 42 added / 42 removed).

### The `irrlicht_renderer.h` numbers

| | lines |
|---|---|
| at dispatch (`fable`, `1c9afba7`) | **1,567** |
| predicted before any edit: -(3 frame-phase lines: `sectionStart_`, `frameStart_`, `measureSection()`) -(12 load-gate declaration + banner lines) -(1 `friend` line) +(3 replacement one-liners) | **~ -13** |
| **measured after** | **1,556 (-11)** |
| allowlist entry | lowered **1,567 -> 1,556** in the same commit |

The 2-line miss is the `#include <set>` that stayed and the load-gate banner
being 11 lines rather than 12. **The first draft of this unit LANDED AT +2**
-- batch correction 24 in action: the rationale for each removal was written
into the god header. Moving it into the component headers
(`frame_component.h`, `slash_commands_component.h`, `zone_load_component.h`,
`rdp_component.h`) turned +2 into -11 with no change to the code. Correction
40 ("write the component header first") is the same lesson stated earlier;
this unit re-learned it the expensive way.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0 on the FIRST attempt** and on every attempt after (4 builds total, one per edit wave). No compile error was produced by any part of this unit -- the promotion is type-preserving by construction, since every context member is a reference to the member of the same declared type |
| `ctest --test-dir build -j6 --output-on-failure` | **3,731 / 3,732 passed**, 1 failure = `AudioManagerTest.PreloadCommonSounds` (the ALSA host quirk). The FIRST run had 3 failures: the hotkey guard above and `DocCitations` (two `BOARD.md` citations), both fixed in-unit. `FileSizeCeiling`, `DocCitations`, all `ReachabilitySweep.*`, `DuplicateStateStores.*` and `visual_gate` all PASS |
| `bash scripts/build-gles2-x86.sh` | **exit 0** (run in full) |
| visual gate STANDALONE (`bash scripts/ci/visual_gate.sh`) | **=== VISUAL GATE: PASS ===**, and `git status --porcelain tests/goldens` is **EMPTY** -- no golden touched |
| `bash scripts/build-arm-noble.sh` | **exit 0**; `strings build-arm-noble/bin/willeq` finds **132** icosphere symbols, so the `EQT_HAS_DRM`-only `icosphereProgram` context member really is compiled (batch correction 9) |
| `bash scripts/refactor/check_file_sizes.sh --all` | **859 ok, 0 warn, 0 FAIL**; `irrlicht_renderer.h` ratchet lowered to 1,556 |

### Deviations and corrections

1. **The first STOP FIRED: residue 1,065 > 300.** Reported, not worked around
   -- see the headline above and **B050**. The arithmetic that decides it:
   promoting at `>= 3 components` reaches 48 members / 549 uses, but the
   coupling that dominates the renderer is TWO-component
   (`frameTimings_` 200 frame + zone_lifecycle, `frameTimingsAccum_` 135
   diagnostics + frame, `playerConfig_` 56 player + frame,
   `storedZoneEnvironment_` 37 zone_lifecycle + zone_load, ...): **854 of the
   1,065 residue uses are members shared by exactly two components.** A
   `>= 2` threshold would have taken the residue to ~211 and comfortably
   under the STOP, at the cost of a ~160-member `RendererContext`. That is a
   design call about what the context is FOR, not an executor's call, and
   both numbers are on the board so it can be made on evidence.
2. **The `eqe1/` component was missing from this unit's first census.** The
   plan's step 1 says "measure `owner_.` across `src/client/graphics/*/`";
   the executor enumerated the ten directories Batch L's unit table names and
   `Eqe1ArchiveComponent` (L02) is the eleventh, in `eqe1/`. Caught by the
   `ctx_.Eqe1()` accessor showing up in the target list with no matching
   family row. Re-measured (95 uses), the promotion re-derived, and four more
   members crossed the `>= 3` bar once it was counted (`eqe1Loaders_`,
   `pendingGlobalEQE1Archives_`, `eqe1LoadTimeCandidateCount_`,
   `eqe1CandidateRaces_`). **Every number in this Review is the eleven-family
   number.** Correction 49 below.
3. **The two EQE1 mutexes were NOT promoted, though both clear the `>= 3`
   bar.** `eqe1Mutex_` (6) and `eqe1CandidatesMutex_` (6) are `mutable
   std::mutex`. Binding a reference to the same mutex object changes no
   synchronisation -- but the plan's second STOP is "any thread/sync change",
   and the conservative reading of that in a purely structural unit is not to
   relocate the access path of a synchronisation primitive at all. The four
   EQE1 members beside them moved; these two are B050 cluster 4 with that
   reason. If the orchestrator wants them, it is four lines.
4. **Four members that clear the `>= 3` bar were excluded because their type
   is an `IrrlichtRenderer`-NESTED type** (`fireGlowLights_` 26 /
   `FireGlowLight`, `zoneLightData_` 22 / `ZoneLightData`, `renderPassTimer_`
   14 / `RenderPassTimer`, `doorCollisionData_` 7 / `DoorCollisionData`).
   `renderer_context.h` would have to name `IrrlichtRenderer::FireGlowLight`,
   which means including `irrlicht_renderer.h` and re-acquiring the ~60
   transitive includes L01 built the context to avoid (batch correction 2/4).
   The fix is correction 22's shape -- hoist each struct to
   `renderer_types.h`, leave a `using` alias in the class -- and it is NOT
   free for `ZoneLightData`: **two other structs of that name exist**
   (`SimulationZoneData::ZoneLightData` in `simulation_worker.h`, and one at
   namespace scope in `include/client/zone_object_data.h`), so a namespace-
   scope hoist needs an ambiguity check. B050 cluster 3.
5. **`SlashCommandsComponent` lost its `friend` line while keeping 37
   `owner_.` uses** -- the two-axis rule (K13 (ii)) working as intended, and
   the first time in either batch that a component with a live transition
   handle has been proved private-clean. The rationale is in
   `slash/slash_commands_component.h`, not in the god header (correction 24).
6. **`RdpComponent` lost the `IrrlichtRenderer&` transition handle**, as the
   plan predicted. Zero reaches on both axes -- the only one of eleven. Its
   ctor is now `explicit RdpComponent(RendererContext&)` and
   `renderer_init.cpp`'s construction line dropped `*this`.
7. **The unit's first header measurement was +2, not negative**, and the fix
   was moving comments, not code (see the numbers table). Recorded because
   the batch's ratchet is a per-wave property (correction 26) and a unit that
   deletes 16 lines of declaration can still GROW the header if it explains
   itself there.
8. **`irrlicht_bridge.cpp`, `application*.cpp` and `graphical_mode.cpp` are
   untouched.** Unlike L11 (its deviation 1), this unit needed no bend of the
   K11 rule: every `ctx_.<Family>()` rewrite is inside a component, and the
   directory move changed no include path (headers are included as
   `client/graphics/...`, and none of the four moved TUs is included by
   anything).

### Corrections to the Batch L template

**49. Enumerate the component directories from the TREE, not from the batch
doc's unit table.** L12's census walked the ten directories Batch L's unit
table names and silently omitted `eqe1/` -- `Eqe1ArchiveComponent` (L02) is a
component like any other, with 95 `owner_.` uses, and leaving it out changed
which members crossed the promotion threshold. The census script's family list
must come from `ls include/client/graphics/*/ | grep _component.h`, and any
unit measuring "across the components" should print the family list it found
and eyeball the count against the accessors on the god class.

**50. A promotion threshold expressed in COMPONENT COUNT has to be checked
against the distribution before it is written into a plan.** L12's `>= 3`
rule was chosen when the below-threshold set was believed to be ~39 members;
the measured distribution is 0 members at 5+ components, 9 at four, 39 at
three and **55 at two, carrying 480 uses plus the 374-use frame-timing blob**.
The rule reaches 30% of what is left. Any future "promote what >= N families
use" plan should carry the histogram, not just the threshold.

**51. Correction 21 fires on the CLOSE-OUT unit too, and it fires on the
spelling the close-out itself invents.** `owner_.Diagnostics().` -> 
`ctx_.Diagnostics().` broke a guard that had already been widened once, by
L11, for exactly this class of change. A text guard that matches a receiver
expression accumulates one accepted spelling per structural unit; widen it and
re-prove it with a planted break every time, and never delete the old
spellings (they are what makes "exactly one matches" a real assertion).

**52. `frameStart_`-shaped dead members are best deleted by the unit that
CENSUSES them, not the one that trips over them.** L11 found it write-only and
correctly declined to move dead state into a component; L12 re-measured and
deleted it in two lines. The rule that worked: a member with exactly one
tree-wide reference which is a WRITE, and which is not a `GameEventType`
publisher (K12 (ff) does not apply to data members at all), is deleted by the
next unit whose territory contains both the declaration and the write.
