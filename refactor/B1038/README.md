# B1038 (#358): the headless build (EQT_GRAPHICS=OFF) compiles again on Linux

Source: B1031 (#351), the macOS probe's Linux cross-check
(`review/B1031/linux_crosscheck_pass2.tsv`). Evidence for this unit:
`review/B1038/` (outside the checkout).

## RED (fable `b47fd97d`, dev-u24, GCC)

    cmake -S . -B build-headless -DCMAKE_BUILD_TYPE=Release -DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON
    cmake --build build-headless -j4 -- -k

(The generator is Unix Makefiles here, so `-k`, not ninja's `-k 0`.)
`review/B1038/red_configure.log` (exit 0), `review/B1038/red_build.log`:
**BUILD_EXIT=2** after 1,470 s, `grep -c "error:"` = **1**:
`src/client/application.cpp:1087:21: error: 'm_renderer' was not declared in this scope`.

`make -k` does not cross a failed target, so every target linking
`willeq_core` was never attempted. To get the complete census, the 124 object
files the build left unbuilt (`review/B1038/red_unbuilt_objects.txt`, every
`*.cpp.o` rule in the tree's `build.make` files with no object on disk) were
compiled one at a time with B1031's `linux_crosscheck.sh`
(`review/B1038/red_crosscheck.tsv`). **Exactly 5 fail, 119 compile:**

| Object | errors | first error |
| --- | --- | --- |
| `willeq_core` `src/client/application.cpp` | 1 | `m_renderer` not declared (line 1087) |
| `test_data_paths` `tests/test_data_paths.cpp` | 1 | `hotkey_manager.h:3: irrlicht.h: No such file` |
| `test_detail_view_distance` `tests/test_detail_view_distance.cpp` | 1 | `detail_types.h:4: irrlicht.h: No such file` |
| `test_ranged_attack_wire` `tests/test_ranged_attack_wire.cpp` | 1 | `hotkey_manager.h:3: irrlicht.h: No such file` |
| `test_zone_connection_flow` `tests/test_zone_connection_flow_identity.cpp` | 9 | `BridgeComponent` has no member `CreateZoneLoadSnapshot` (line 353) |

These are the issue's 5 TUs; there are no others.

**cereal.** `grep -rn "cereal/" include src`:

    include/common/net/daybreak_structs.h:3:#include <cereal/cereal.hpp>
    include/common/net/packet.h:10:#include <cereal/cereal.hpp>
    include/common/net/packet.h:11:#include <cereal/archives/binary.hpp>

`grep -n -i cereal CMakeLists.txt` is empty (there is no `cmake/*.cmake`; only
`cmake/toolchains/`). dev-u24 has it from `libcereal-dev`
(`dpkg -S /usr/include/cereal/cereal.hpp`); the ARM image copies v1.3.2 into
its sysroot's `/usr/include` (`docker/Dockerfile.arm-noble`, next to GLM).

**RED behind the RED: links and run-time.** `make -k` stopped at
`willeq_core`, so nothing that links it had ever been tried headless. With
the 5 compile fixes in, the cold build (`review/B1038/green_build.log` +
`green_build_k.log`, `-k`) failed to LINK 8 test executables, every one on a
symbol from a `GRAPHICS_SOURCES` TU (`CMakeLists.txt`'s graphics list):

| Test executable | Undefined (defining renderer TU) | Failing cases / total |
| --- | --- | --- |
| `test_config_key_reachability` | `ParseDisplaySettings`, `ParseEffectQuality` (`display_settings_loader.cpp`) | 4 / 5 |
| `test_data_paths` | `ConstrainedRendererConfig::*` (`constrained_renderer_config.cpp`) | 1 / 17 |
| `test_door_motion` | `DoorTravels`, `LiftCycle::*`, `PlayerRidesLift`, ... (`door_motion.cpp`) | 11 / 16 |
| `test_ground_sentinels` | `ShouldLandJump`, `ShouldClampSwimToFloor`, `ClassifyBspGroundRay` (`ground_query.cpp`) | 11 / 17 |
| `test_levitation` | `ClampLevitationFeet` (`ground_query.cpp`) | 2 / 15 |
| `test_pose_log_flags` | `cameraModeName` (`camera_rig.cpp`, via inline `pose_log_format.h`) | the row-format/writer half |
| `test_texture_alpha_census` | `EQE1Loader::*` (`eqe1_loader.cpp`) | 6 / 20 (via a helper) |
| `test_ui_sprite_roster` | `EQE1Loader::*` | 1 / 5 |

and once linked, `ctest -LE hygiene` failed 5 cases, all asserting
graphics-only behaviour (`review/B1038/` first ctest run, not kept):
`PoseLogFlags.KnobsBeforePoseLogAreNotClobbered`,
`PoseLogFlags.AbsentIntervalDefaultsToEveryFrame`,
`PoseLogPreflight.UnwritablePathIsRefusedWithTheReason` (a no-graphics build
refuses `--pose-log`: "--pose-log needs the renderer ... or a build without
graphics"), and `SelfRelocationHeading.AServerRelocationReachesTheCameraInTheInternalConvention`,
`SelfRelocationHeading.TheEntityRowStillCarriesTheServerHeadingVerbatim`
(`BridgeComponent::OnSpawnMovedGraphics` is called only under
`EQT_HAS_GRAPHICS`, `src/client/entities/entities_component_updates.cpp`).

## The fix (guards, registration conditions, CMake find logic only)

- `src/client/application.cpp`: the steady_60s source-character census
  (the renderer's) is `#ifdef EQT_HAS_GRAPHICS`, the mechanism the file
  already uses everywhere else; `pollSteadyState()` still runs headless.
- A test case that exercises a renderer TU or a graphics-only path is
  `#ifdef EQT_HAS_GRAPHICS` (the idiom `test_data_paths.cpp` already used for
  its GLES2 cases), and the file's headless cases keep running:
  `test_zone_connection_flow_identity.cpp` (the T260 `ZoneLoadSnapshot`
  cases), `test_data_paths.cpp` (hotkey + constrained-preset cases),
  `test_ranged_attack_wire.cpp` (the `RangedAttackKey` key leg; the five wire
  cases are game logic and run headless), `test_config_key_reachability.cpp`,
  `test_ground_sentinels.cpp` (the B435/T440 half; `FindBestZT437` is HCMap,
  game-side), `test_levitation.cpp` (section 3), `test_pose_log_flags.cpp`
  (row format, writer, the 3 cases above), `test_self_relocation_heading.cpp`
  (the 2 cases above).
- A test whose EVERY case is a renderer module's is registered only under
  `IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)` (the guard the fragments already
  use): `test_detail_view_distance` (DetailManager; `detail_types.h` includes
  `irrlicht.h`), `test_door_motion`, `test_texture_alpha_census`,
  `test_ui_sprite_roster`. Testing a renderer TU in a tree whose product
  never links it would be coverage of nothing headless; the graphics `test`
  job still runs them.
- **cereal**: `FIND_PATH(CEREAL_INCLUDE_DIR cereal/cereal.hpp)` beside GLM,
  added to the global `INCLUDE_DIRECTORIES` (the header is reached through
  `common/net/packet.h`, i.e. by `willeq_core` and nearly every test and
  tool, which is why the GLM-style global list is the right place), with
  `FATAL_ERROR` naming `libcereal-dev` (Debian/Ubuntu) and `cereal`
  (Homebrew) and `-DCEREAL_INCLUDE_DIR`. Proven by a standalone configure of
  the same block with `/usr/include` ignored
  (`review/B1038/cereal_missing_configure.log`: exit 1, the message). The ARM
  image's sysroot copy is found the same way GLM's is. **Not vendored**:
  adding `third_party/cereal` (header-only, BSD-3, ~1 MB) is the alternative
  and is Will's call.

In the graphics configuration every guard is true and every registration
condition holds, so the graphics build compiles the same sources.

## GREEN (dev-u24)

- Headless, cold tree (`rm -rf build-headless`, same cmake line):
  `review/B1038/green_build_final.exit` **BUILD_EXIT=0**, `grep -c "error:"`
  = 0, `ls build-headless/bin | wc -l` = **243** (242 executables + the
  `cache/` directory tests create; `check_build_tree_orphans.py
  build-headless`: 242 files, PASS). Wall time of the cold tree at `-j4`:
  1,534 + 352 + 32 s over the three passes that built it (~1,920 s).
- `ctest --test-dir build-headless -LE hygiene --output-on-failure -j4`:
  **100% tests passed, 0 tests failed out of 3533**, 48 s
  (`review/B1038/green_ctest.log`); the same with `WILLEQ_REQUIRE_ASSETS=1`
  (the CI job's setting): identical, 0 failed
  (`green_ctest_require_assets.log`). 45 skipped, none an asset root:
  `AssetRequirementTest.AbsentPathSkips`/`AbsentPathWithOtherEnvValueSkips`
  (skip by design), 32 real-capture replay cases (`PcapExtractorTest.*` x10,
  `PcapDaybreakHarnessTest.*` x3, `PcapStepThroughTest.*` x4,
  `DaybreakConnectionPcapTest.*` x5, `ZoneEntitiesRealCapture.FullZoneStreamReplay_PopulatesEntityTable`,
  `ZoneChatTest.RealCapture_ChannelMessagesReplayAndPublish`,
  `LoginWorldCaptureReplay.RealSessionReplaysLoginAndWorldStreamsWithoutCrash`,
  `LoginWorldAccessors.*_RealCapture_*` x7 -- "no capture at
  data/captures/session.pcap|.sleq": this worktree has no `data/captures/`),
  `BridgeTeardownAsanRepro.*` x2, `SpellManagerGems.MemorizeSpell*` x2 and
  `EqActionHandlerSpellPet.*` x3 ("no Warrior-usable spell found in scanned
  range"),
  `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`,
  `SoundBufferTest.LoadSwingWav`, `visual_gate`, `zone_content_sweep_smoke`.
- `ctest --test-dir build-headless -L hygiene`: **26/26 passed**.
- Graphics sanity (the CLAUDE.md tree, `-DEQT_GLES2_IRRLICHT_DIR=<main>/third_party/irrlicht-gles2-x86`),
  ONLY the touched targets at `-j4` (`review/B1038/gfx_build.exit`:
  BUILD_EXIT=0, 977 s): `willeq` + the 12 touched test executables. Each
  test binary run directly (`review/B1038/gfx_tests.txt`), all rc=0 with
  the SAME case count as before the unit -- `test_zone_connection_flow` 35,
  `test_data_paths` 17, `test_ranged_attack_wire` 8,
  `test_detail_view_distance` 4, `test_config_key_reachability` 5,
  `test_door_motion` 16, `test_ground_sentinels` 17, `test_levitation` 15,
  `test_pose_log_flags` 23, `test_self_relocation_heading` 3,
  `test_texture_alpha_census` 20, `test_ui_sprite_roster` 5 -- so the
  graphics build compiles and registers exactly what it did. CI runs the rest.

## CI net

`build-headless` (`.gitlab-ci.yml`, `build` stage; `docs/ci.md` "The headless
build"): the shared `willeq_build_tree` with `WILLEQ_HEADLESS=1` (OFF
configure, no Irrlicht bootstrap -- the only two lines that differ, so the
function is NOT duplicated) into `build-headless/ref/$CI_COMMIT_REF_SLUG`
(job-level `BUILD_TREE`), then `ctest -LE hygiene` in the same job under
`WILLEQ_REQUIRE_ASSETS=1`. No B1026 verify is needed: build and suite are one
job on one host, and the host+sha claim is still written for the prune and
any future consumer. `GIT_CLEAN_FLAGS` keeps `build-headless`;
`prune-build-trees` gets `--root build-headless/ref` (and
`prune_ref_build_trees.py` accepts it as a `--root`, never as a
`--legacy-root`). Pinned by `CiBuildTreeStamp` (new group 4, red on the
pre-B1038 yml: "no `build-headless` job") and two new `PruneRefBuildTrees`
cases. `glab ci lint`: valid.

## Not verified / left open

- The runner-side cost of the new job (`-j8`, cold and warm): only the
  dev-u24 `-j4` number above. The first pipeline's job log is the measurement.
- The macOS half of B1031's 61 TUs (Darwin-only failures) is untouched; this
  unit is Linux only.
- `test` now waits for `build-headless` too (stage ordering); a red headless
  build skips `test`/`gate` the way a red `build` does. If that coupling is
  unwanted, `test` could take `needs: [build]` -- not done (it would change
  an existing job).
- The headless cases compiled out are compiled out, not re-homed: the
  renderer TUs they test are still covered by the graphics `test` job.
- `__LINE__`-bearing log calls below each new `#ifdef` in the touched TUs
  shift by the added lines; no other graphics-build change.
- Vendoring cereal into `third_party/` is not done (Will's call).
