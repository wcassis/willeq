# T480 — hygiene: renderer headers that include unused upload/priority headers (B710), unused `AssetType` values (B711), the unused `PlayerState::setFollowDistance` (B684)

Class B, **no lock, no Pi**, slot3. Read T477's Review
(`refactor/T477_upload_leftovers_and_shared_tmp_names.md`) and T474's
(`refactor/T474_chase_camera_geometry.md`, B684).

## Items

- **B710:** three renderer headers include `gpu_upload_thread.h` and
  `work_priority.h` but use neither, and `irrlicht_renderer.h` includes
  `work_priority.h` without using it. Two zone TUs reach the priority key only
  transitively through those headers. The fix: move the include to the TUs
  that actually use each symbol, then remove it from the headers. Prove with
  the full build, tools included, that nothing else depended on the transitive
  include.
- **B711:** `AssetType::Door`, `EntityTexture` and `LightEffect` have no
  production user. T477 showed the `AssetType` priority key is compared
  in-process only: never persisted, serialised, used as a table index or
  switch-exhaustive. Re-prove that for these three, then delete them the way
  T477 deleted `ZoneTexture`, leaving explicit values with commented gaps.
- **B684:** `PlayerState::setFollowDistance` has no production caller. It was
  hidden by a name collision with the `CameraController` method T474 deleted.
  Prove zero callers per the protocol grep rule, delete it, and cascade the
  delete (e.g. its backing field, if that becomes write-only).

## Proof

Standard verification bar, builds strictly in sequence; `ZeroCallers` and
`WriteOnlyFields` rows; G-batch goldens for touched TUs; visual gate; ARM; file
sizes. Board B740–B749; strike B710, B711, B684.

File territory: the renderer headers B710 names, the two zone TUs, `work_priority.h`
`AssetType`, `tests/test_work_priority.cpp`, `PlayerState`, the zero-caller
script rows. **Not** `wld_loader*`, character, animated-mesh or object loaders
(T479, slot2). **Not** `camera_rig.h` or camera constants (T478, slot1).

STOP: an `AssetType` value turns out to be positionally significant (report).
Also STOP if an image golden moves.

## Review

Slot3, branch `task/T480-header-enum-leftovers`, cut from `fable` `621f544d`.
No lock, no Pi, no server contact. Every build and test run wrote its own exit
file under a unit-private scratch directory (`scratchpad/T480/`). Every wait
reported a non-zero elapsed time, and there was no exit 143. The slot's
`build/` is `RelWithDebInfo` and points at its own
`third_party/irrlicht-gles2-x86`.

**STOP conditions:** neither fired. No `AssetType` value is positionally
significant (below), and no image golden moved (`git status tests/goldens` is
empty).

### B710: includes moved and deleted

Census: each header name and each symbol (`GPUUploadThread`,
`UploadRequest*`, `UploadResult`, `WorkPriorityKey`, `AssetType`), grepped over
`src/ include/ tests/ tools/ benchmarks/ docker/`.

| file | before | after | why |
|---|---|---|---|
| `include/client/graphics/renderer_types.h` | `gpu_upload_thread.h`, `work_priority.h` | neither | names no symbol from either |
| `include/client/graphics/renderer_draw_types.h` | both | neither | same |
| `include/client/graphics/renderer_event_receiver.h` | both | neither | same |
| `include/client/graphics/irrlicht_renderer.h` | both | `gpu_upload_thread.h` only | `gpuUploadThread_` and `pendingGPUResults_` are live; nothing from `work_priority.h` |
| `src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp` | reached `WorkPriorityKey` / `AssetType::ZoneMesh` transitively | **direct `#include "client/graphics/work_priority.h"`** | line ~1097 |
| `src/client/graphics/zone_stream/renderer_zone_stream.cpp` | same | **direct include** | `ZoneMesh`, `Icon`, lines ~253-255 |

The other users of the upload types get `gpu_upload_thread.h` from
`irrlicht_renderer.h`, which keeps it. `renderer_gpu_upload.cpp` and
`renderer_slash_commands.cpp` use them, and so does `renderer_zone_stream.cpp`
(`UploadRequest`). `entity_prep_worker.cpp` already included
`work_priority.h` directly. Tests name the upload types only in regex strings.

**The proof is the compile, with all four removals in place at once.**
- **Step 1:** `build/` exited 0 with 0 `error:` lines and 985 compiles. It
  rebuilds the client, every test and every tool target, and the log shows
  `wld_dump`, `zone_line_editor`, `model_viewer` and `gles2_zone_benchmark`
  relinked.
- **Step 3:** `build-gles2` exited 0.
- **Step 5:** the ARM build exited 0.

No other include was needed.

### B711: `AssetType` values deleted

Census for `AssetType::(Door|EntityTexture|LightEffect)` and the bare names
`EntityTexture` and `LightEffect`, grepped over `src/ include/ tests/ tools/
benchmarks/ docker/ scripts/ docs/ tests/goldens/`: the only hits were
`tests/test_work_priority.cpp` and the enum itself.

**Positional significance, re-proven for these three:**
- No `static_cast<AssetType>` exists anywhere, no `switch` covers it, and
  nothing indexes a table with it. Its only producers are
  `WorkPriorityKey::make` and `WorkPriorityKey::makeNonSpatial`.
- The key's `.value` is only compared:
  - `GPUUploadThread` sorts with `a.priority < b.priority`
    (`gpu_upload_thread.cpp:179`, `:209`).
  - `BackgroundThreadPool` receives it through `queue_->submit(req, priority)`.
  - Nothing persists, serialises or logs-then-parses it.
- The only decode of bits 23-20 is `WorkPriorityKey.BitLayoutWithDistance`'s
  own `(value >> 20) & 0xF`. In `src/`, the only `>> 20` is
  `door_manager_mesh.cpp`'s colour hash.
- Every enumerator carries an explicit value, so deleting 2, 4 and 5 moves no
  surviving key.

| value | enumerator | verdict |
|---|---|---|
| 0 | `ZoneMesh` | live, kept |
| 1 | (gap, T477 `ZoneTexture`) | — |
| 2 | `Door` | **deleted**, commented gap |
| 3 | `EntityMesh` | live, kept |
| 4 | `EntityTexture` | **deleted**, commented gap |
| 5 | `LightEffect` | **deleted**, commented gap |
| 6 | `Icon` | live, kept |

**Test changes** in `tests/test_work_priority.cpp` (13 → 14 cases):
- `SameDepthAssetTypeOrdering` now chains `ZoneMesh < EntityMesh < Icon`.
- `EqualityAndInequality` and `SortVectorByPriority` use `EntityMesh` in place
  of `Door` / `EntityTexture`.
- `BitLayout` is `make(3, EntityMesh)` = `0x03300000`.
- **New:** `SurvivingAssetTypeValuesAreUnchanged` pins 0, 3 and 6. An ordering
  case alone would pass a renumbering, which is the thing a gap exists to
  prevent.
- Standalone: 14/14.
- **Deviation (flagged):** the new case was written against the edited header,
  so it has no red run of its own. It asserts values that did not change.

### B684: `PlayerState::setFollowDistance`

Census for `setFollowDistance`, grepped over `src/ include/ tests/ tools/
benchmarks/ docker/ scripts/`:
- the definition, `include/client/state/player_state.h:153`;
- the EXEMPT row in `scripts/refactor/check_zero_callers.py`;
- a comment in `include/client/graphics/camera_controller.h:30`, which names
  the *deleted* `CameraController` method, not this one.

No caller. The setter is **deleted**, and so is its EXEMPT row and the T474
comment above it. A row left behind would be a stale exemption.

**Cascade — a fixed point at once.** `followDistance()` and `m_followDistance`
are not unreached. The follow logic reads the getter in
`movement_update.cpp:201/205/214` and `movement_component.cpp:223/224`. The
field is read, so `WriteOnlyFields` does not apply, and it stays. It now holds
only its `10.0f` initializer, a fixed follow stop distance. That is live
behaviour, not dead state, so it is not boarded. B684's "the rest of that
unreached state" was wrong about the getter, and the strike says so.

### Goldens

No order or statement golden pins a TU this unit touched. Grepping
`tests/goldens/` and the golden tests for `renderer_zone_draw`,
`renderer_zone_stream`, `work_priority`, `player_state` and the four headers
returns nothing that pins code. `test_dead_renderer_members` slurps
`renderer_zone_draw.cpp` for member names, not includes. Every G-batch golden
test passed inside the suite, among them `MovementOrder.*` (4) and
`FrameRenderOrder.*` (4).

### Verification (builds strictly in sequence)

| step | result |
|---|---|
| 1 `cmake --build build -j24` | **PASS**, exit 0, 0 `error:` lines, tools included |
| 2 `ctest -j6` | **PASS**, 4793/4793, 338.54 s. `ZeroCallers` and `WriteOnlyFields` passed, as did `DocCounts`, `FileSizeCeiling` and `ReachabilitySweep.*`. Both scripts also exited 0 standalone before the build. |
| 3 `build-gles2-x86.sh` | **PASS**, exit 0, 0 `error:` lines (started after ctest finished) |
| 4 visual gate | **PASS** standalone on the fresh `build-gles2` (`visual_gate` 21.03 s); no golden moved. The in-suite pass (26.81 s) ran against the pre-T480 `build-gles2`, so the standalone run is the one that counts. |
| 5 `build-arm-noble.sh` | **PASS**, exit 0, 0 `error:` lines |
| 6 `check_file_sizes.sh fable` | **PASS**, 9 files ok, 0 warn, 0 fail |

### Board

Struck **B710**, **B711**, **B684**. Nothing new; B740–B749 unused.

### Deviations

1. The new `SurvivingAssetTypeValuesAreUnchanged` case has no red run (above).
2. `fable` moved during the unit (`621f544d` → `70ed7340`, T481's plan). The
   branch is not rebased.
