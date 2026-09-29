# B917 — a zone change that lands during a zone load (issue #236) and the exit code of a FATAL (B918, issue #237)

Branch `task/B917-B918-midload-zone-change`, from fable `b1132c3f`. Evidence of
the Orange Pi failures: `refactor/B854/RENDERED_SWEEP.md` finding 1, logs under
`/mnt/projects/fable/review/B854-rendered/sweep-full/zones/`.

## Root cause (B917)

Application kept ONE `eqt::ZoneLoadSnapshot` member shared by three parties,
and two bare flags to hand it around:

1. **Game thread** (`Application::gameThreadLoop`, `src/client/application.cpp`,
   fable `b1132c3f` lines 982-991): on every `ConsumeZoneLoadRequest()` it
   overwrote `m_zoneLoadSnapshot` and set `m_zoneLoadReady`.
2. **Loading thread** (`loadZoneGraphicsOnThread`,
   `src/client/application_zone_loading.cpp:170`): read that member through
   `const auto& snap = m_zoneLoadSnapshot;` for the whole load, without the lock.
3. **Game thread again** (`application.cpp:995-1013`): on
   `m_graphicsCompleteReady` it finalised the load (`SetZoneBspTree` +
   `OnGraphicsComplete()`), then CLEARED the snapshot (P04).

The sequence when the server moves the character during the first load
(charasis on the Pi: killed 7 s in, sent to the qeynos bind point):

| step | thread | what happened |
|---|---|---|
| 1 | game | snapshot `charasis`, ready flag; render starts loading thread #1 |
| 2 | game | death, zone handshake to qeynos, snapshot `qeynos` written **over the one loader #1 is reading** |
| 3 | loader #1 | finishes charasis, logs `complete for zone 'qeynos'` (reading the overwritten member) |
| 4 | render | `checkLoadingComplete` publishes charasis's BSP tree labelled `qeynos` (T260's label read from the overwritten member) |
| 5 | game | finalises it: **`Ready!` for qeynos with no qeynos graphics built**, then **clears the snapshot** |
| 6 | render | the ready flag from step 2 is still set: starts loader #2 **from the member step 5 is clearing** |
| 7 | loader #2 | reads `zoneName` = `""` (`.s3d`) or reads the name before the clear and `eqClientPath` = `""` after it (`qeynos.s3d`) → `[FATAL] Failed to load zone S3D archive` → quit |

So the defect is not path resolution: the second load's snapshot is emptied
under it by the game thread finalising the FIRST load as if it were the second.
`review/B854-rendered/.../growthplane` shows the `.s3d` interleaving and
`.../charasis` the `qeynos.s3d` one.

**User-reachable: yes.** Nothing here is rig-specific. Any server-directed zone
change whose new zone-in completes while the previous zone's graphics are still
loading triggers it: dying while zoning in (the charasis case: aggro at the
zone-in point), a GM `#zone`/`#summon`, a kick for level or flag, an
evacuation. On the Pi a load takes 12-20 s, a handshake 2-4 s. On a fast x86
host the load usually wins the race and the old code merely reported the
abandoned zone `Ready!` mid-handshake (`runs/prefix-run1`).

## Fix

`eqt::ZoneLoadHandoff` (`include/client/zone_load_handoff.h`,
`src/client/zone_load_handoff.cpp`) replaces the shared member and both flags:

- the game thread **posts** a snapshot and gets a generation number;
- the render thread **takes** it (the slot empties; the loading thread reads
  `m_activeLoadSnapshot`, a private copy nothing else writes, released after
  the join);
- the render thread **publishes** a completion with its generation, the zone it
  BUILT and the BSP tree;
- the game thread **finalises only the load it is waiting for**: a completion
  whose generation is not the latest post, or that arrives after the game left
  the GRAPHICS_* phases, is dropped. The pending destination snapshot is still
  in the slot, so the render thread starts it next frame.

The superseded load is allowed to finish rather than cancelled — the loader has
no cancellation points and a finished-then-discarded load is the simplest
correct design. No threading-model change: the loading thread still owns the
GL context; game code still reaches the renderer only through the bridge.

## B918

`eqt::scenario::ProcessExitCode()` (`scenario_runner.h`) decides main()'s code:
0 for a clean `/q` / `/camp` / SIGTERM, **4 (`kClientFatalExit`)** for a run that
ended on a FATAL; `--scenario` stays inside 0..3. Two FATAL-then-ordinary-quit
families existed and both now record the FATAL: the loading thread
(`ZoneLoadHandoff::finishLoad`) and `ZoneComponent::LoadZoneSources()` (six
sites, `eqt::RecordFatalExit()`, `include/client/fatal_exit.h` — found live
here, `runs/b918-before-relic-sources-fatal`). `loadZoneGraphicsOnThread`'s two
early FATAL returns now set `quitRequested` (they used to let the game finalise
an unloaded zone).

## Tests (`tests/test_zone_load_handoff.cpp`)

Offline, no server, no device; the destination-load case opens the real
charasis/qeynos archives with the real `S3DLoader` (asset-gated).

| test | RED without the fix |
|---|---|
| `AZoneChangeDuringALoadEndsLoadedInTheDestination` | `classify()` reverted to always-Apply: "the superseded charasis load was finalised as the zone the game is entering", 2 ready vs 1 |
| `ALoadFinishingWhileTheGameIsReconnectingIsNotReady` | same revert: `{charasis, qeynos}` vs `{qeynos}` |
| `ALoaderFailureExitsWithTheFatalCode` | `ProcessExitCode` reverted to "0 outside --scenario": 0 vs 4 |
| `ALoadZoneSourcesFailureExitsWithTheFatalCode` | the six `RecordFatalExit` calls commented out: "the FATAL was not recorded -- main() returns 0" |
| `ProcessExitCode.CleanQuitIsZeroAndFatalIsNonZeroInEveryMode` | same revert as above: 0 vs 4 |

`AChangeBeforeTheLoadStartsLoadsOnlyTheDestination` and
`AnOrdinaryZoneInIsFinalisedExactlyOnce` pin the unchanged paths. A clean
headless `/quit` exiting 0 at process level stays pinned by
`test_headless_shutdown`.

## Live (local eqemu lane, rendered x86, surfaceless EGL, `--script`)

Rig: `scripts/b917_midload_zone_change.sh` — seeds Testchar at charasis's safe
point with the world down, and the script's first command, `/say #zone qeynos`,
is dispatched at "fully connected", before the graphics load starts. Unpinned,
charasis loads in ~1.2 s on this 40-core host, faster than the ~1.6 s handshake,
so the race does not open; `--cpus 0` (one CPU) slows the CPU-bound load
towards the Pi's while the network-bound handshake is unchanged.

| run (`runs/`) | binary | result |
|---|---|---|
| `prefix-run1` | pre-fix, all CPUs | race closed; charasis finalised `Ready!` mid-handshake (`SetZoneBspTree: rejecting tree built for 'charasis' (current zone '')`), qeynos then loads; exit 0 |
| `prefix-run2-cpu1` | pre-fix, 1 CPU | **REPRODUCED**: `complete for zone 'qeynos'` on the charasis load, `Instant scene ready for zone ''`, `[FATAL] Failed to load zone S3D archive: .s3d`, **exit 0** |
| `prefix-run3-cpu1` | pre-fix, 1 CPU | **REPRODUCED again**, same lines, exit 0 |
| `fixed-run1-cpu1` | fixed, 1 CPU | `discarding superseded zone load #1 ('charasis') -- latest request #2`, qeynos load #2 completes, `Ready!`, `/loc` in qeynos, clean `/q`, **exit 0** |
| `fixed-run2-cpu1` | fixed, 1 CPU | same, exit 0 |
| `fixed-run3-allcpus` | fixed, all CPUs | load #1 finishes during the handshake: discarded (`game awaiting graphics=false`), no premature `Ready!`; qeynos loads; exit 0 |
| `b918-before-relic-sources-fatal` | loader fix only | seeded into relic (no `relic.s3d` client asset): `LoadZoneSources: Failed to open zone archive`, **exit 0** |
| `b918-after-relic-sources-fatal` | final | same FATAL, `FATAL exit=4 reason=LoadZoneSources failed for zone 'relic'`, **exit 4** |

Frames of the rendered destination (qeynos, 800x600) are under
`/mnt/projects/fable/review/B917/fixed-run*/frames/` (not committed). Every run
restored the character byte-identically with the world down and brought the
lane down.

**Not verified live:** the loading-thread FATAL returning 4 (the fix removes the
only live trigger known; offline test only), the death-to-bind variant of the
move (the GM `#zone` is the same server-directed move, deterministic), and the
Orange Pi itself.
