# B1039 (#359): macOS dev platform, card 2 -- the headless client and suite build and run on the M4 mini

Source: B1031 (#351), the card-1 census (`review/B1031/census_pass2.md`,
`review/B1031/failed_objects_pass2.txt`). Builds on B1038 (#358), which fixed
the half of that census that was broken on Linux too. Scope (Will
2026-10-12): a DEV platform -- headless client + tests. No renderer, no gate,
no budgets, no goldens. Evidence: `review/B1039/` (outside the checkout).
The recipe a user follows is `docs/macos.md`.

Host: `m4mini.goldfarm.local`, Apple M4 (10 cores, 16 GiB), macOS 15.7.4,
Xcode clang, Homebrew `cmake ninja fmt glm jsoncpp cereal bash` +
`openssl@3` under `/opt/homebrew` (Homebrew was used throughout, NOT card 1's
`~/willeq-deps` user-prefix builds). Configure line, every run:

    PATH=/opt/homebrew/bin:$PATH cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3

The memory ledger option was left at its default, ON, for RED and GREEN
alike (no `-DEQT_MEMORY_LEDGER=OFF` fallback was needed).

## RED (branch base `806b8b79` = B1038's head, Mac, cold tree)

`cmake --build build -j8 -- -k 0`: **BUILD_EXIT=1**, `grep -c "error:"`
= **281**, 57 failed edges (`review/B1039/red_build.log`, `red_build.exit`).
B1031's 238 was ledger OFF and before B1038; B1038 removed its 14 Linux-too
lines and the ledger ON adds 57 (the `#error` for a non-glibc libc in 19
objects and `_SC_PAGESIZE` in the hooks), so 238 - 14 + 57 = 281.

| KIND | error lines | where |
| --- | ---: | --- |
| glibc/malloc internals | 233 | `memory_ledger.cpp` in 19 objects (171 `r.mi.*` mallinfo2 fields, 38 `_SC_PAGESIZE`, 19 the glibc `#error`); `scripts/pi/lima_bo_census.c` 5 |
| void* pointer arithmetic | 13 | `merchant.cpp` 5, `zone_dispatch.cpp` 5, `entities_component_world.cpp` 1, `test_forage_fishing.cpp` 2 |
| `pthread_setname_np` (2-argument form) | 5 | `application.cpp` 2, `background_thread_pool.cpp` 2 objects, `background_work_queue.h` 1 |
| `SO_RCVBUFFORCE` | 3 | `posix_udp_transport.cpp` in 3 objects |
| clang / fmt 12 strictness | 6 | `inventory_cursor.cpp` 2 (narrowing), `scoped_temp_dir.h` 2 objects (`mkdtemp`), `combat.cpp` (unscoped enum), `wld_fragments_mesh.cpp` (`vector<bool>` reference) |
| EGL headers | 21 | `tools/hwcaps/*`, `tools/gl_points/*`, `tools/test_gl_points.cpp` |
| **total** | **281** | |

**RED behind the RED.** `-k 0` does not cross a failed target, so three more
compile errors and the run-time half only appeared once the above were fixed:

- `tests/test_memory_ledger.cpp`: `<malloc.h>` not found (the ledger's own
  suite; meaningless with the ledger off -- it asserts `enabled()` first).
- `tests/test_headless_shutdown.cpp`: `::pipe2` x2.
- First `ctest -LE hygiene` (`review/B1039/mac_ctest_run1.log`): **23 failed
  of 3501** -- 4 `CwdElsewhere.*` (the data resolver walked up from
  `/proc/self/exe`), 17 PFS-sound cases + `AudioManagerTest.PreloadCommonSounds`
  (environment: the SMB mount listed only the first 1,000 directory entries,
  eight times over, so no `snd*.pfs` was found -- fixed on the mini with
  `dir_cache_off=yes` in `~/Library/Preferences/nsmb.conf`, `docs/macos.md`),
  and `test_eqe1_loader` (hard-coded `/mnt/projects` ETC1 tree).
- `ctest -L hygiene` under macOS `/bin/bash` 3.2 (B1031's run): 2 failed,
  `FileSizeCeiling` (`mapfile`, `declare -A`) and `CrowdObservationControls`
  (empty arrays under `set -u` in bash 3.2; with Homebrew bash still 5 of 8
  red: `crowd_run.sh` needs GNU `timeout`, which macOS lacks).

## The fix, per KIND (guards, CMake conditions, casts; Linux compiles the same code)

1. **glibc/malloc internals.** `include/client/core/memory_ledger.h`: under
   `__APPLE__` `EQT_MEMORY_LEDGER` is forced to 0 -- the existing OFF shape
   (no symbol replaced, empty `MemoryScope`, `enabled()` false);
   `CMakeLists.txt` passes 0 there too with a STATUS line, so no TU can
   disagree about the layout. `memory_ledger.cpp`'s `arenaToLogLine`
   (mallinfo2 fields; its only caller `recordArena` is Linux-only) is
   `__linux__`. `tests/cmake/core_utils.cmake`: `test_lima_bo_census_shim`
   only on Linux, `test_memory_ledger` only off Apple.
2. **void\*** -- `static_cast<uint8_t*>` / `<const uint8_t*>` at the 13 sites.
3. **Thread names** -- `include/common/thread_name.h`,
   `eqt::setCurrentThreadName(const char*)` (1-argument Darwin form,
   2-argument Linux form) at all six T504 call sites (`application.cpp` x2,
   `background_thread_pool.cpp`, `loading_thread.cpp`,
   `simulation_worker.cpp`, `background_work_queue.h`).
4. **`SO_RCVBUFFORCE`** -- `#ifdef SO_RCVBUFFORCE` around the forced
   attempt in `posix_udp_transport.cpp`; Linux's log text is unchanged,
   elsewhere it reads `SO_RCVBUF; this platform has no SO_RCVBUFFORCE`.
5. **clang / fmt 12** -- `static_cast<int>` for `SLOT_RANGE` in two log
   lines, `static_cast<bool>` for the `vector<bool>` element,
   `static_cast<uint32_t>` in two `MoveItemIntent` initialisers,
   `#include <unistd.h>` in `tests/scoped_temp_dir.h`.
6. **EGL tools** -- `FIND_PATH(EGL/egl.h)` + `FIND_LIBRARY(EGL/GLESv2)`; the
   `tools/hwcaps` probe block and `test_gl_points` are built only when all
   three are found (STATUS line otherwise). B1038 already registers the
   irrlicht-including tests only under `EQT_GRAPHICS` -- not duplicated.
7. **Test scripts** -- `scripts/refactor/check_file_sizes.sh` is bash-3.2
   safe (read loops, parallel indexed arrays, BSD `wc` padding); its `--all`
   output on this tree is byte-identical to fable's script under bash 5, and
   the allowlist ok/FAIL paths were checked in a sandbox under both bash 5
   and `/bin/bash` 3.2.57. `CrowdObservationControls` was NOT made bash-3.2
   safe (the wrappers it drives use `mapfile`/empty arrays throughout --
   not trivial): it now fails once, naming the requirement (bash >= 4 on
   PATH, `docs/macos.md`), and writes a pass-through `timeout` stub into its
   sandbox only when PATH has no `timeout` (Linux runs the real one).

Found after the census:

- `tests/test_headless_shutdown.cpp`: `pipe2` off Linux is `pipe` +
  `FD_CLOEXEC`.
- `src/client/core/data_paths.cpp` `ExecutableDirectory()`:
  `_NSGetExecutablePath` + `realpath` on Darwin.
- `tests/test_eqe1_loader.cpp`: `WILLEQ_EQE1_PATH` override (the name
  `test_eqe1_texture_index.cpp` already reads), else `/mnt/projects` as
  before, else (Mac) the checkout's `data/eqe1_client` link.
- `CMakeLists.txt`: `-ffp-contract=off` on Darwin. Apple clang fuses
  `a*b+c` on arm64 by default; with the server maps pointed in, every
  `HCMapRaySetGolden` zone differed from the x86 goldens (4 of 4, one a
  flipped CheckLOS verdict) and with it off `test_hc_map` is 24/24.

## GREEN (Mac)

At `3e5a80e2`, cold tree (`rm -rf build`): **BUILD_EXIT=0**,
`grep -c "error:"` = **0** (`review/B1039/green_build.log`);
`ls build/bin | wc -l` = **227** (the six non-test binaries: `willeq`,
`chase_rig_fit`, `dump_icon_sheet`, `item_icon_atlas_builder`,
`spell_sprite_atlas_builder`, `ui_atlas_builder`). The last commit
(`test_eqe1_loader`'s fallback) rebuilt incrementally, exit 0.
`./build/bin/willeq --no-graphics --help` exits 0 (`green_help.log`).

`ctest --test-dir build -LE hygiene -j8 --output-on-failure`:

- with the recipe's environment (Homebrew PATH, `EQ_CLIENT_PATH`,
  `WILLEQ_EQE1_PATH` at the NAS mount): **100% tests passed out of 3501**,
  90 skipped (`green_ctest.log`);
- with no environment at all: **100% tests passed out of 3501**, 91 skipped
  (`green_ctest_noenv.log`; the extra skip is
  `Eqe1TextureIndexShipped.CoversEveryPackInTheTree`, which looks only at
  `/mnt/projects` unless told).

`ctest --test-dir build -L hygiene` with Homebrew bash first on PATH:
**26/26**; with `/bin/bash` 3.2 first: 25/26, `CrowdObservationControls`
failing with its named-requirement message (`green_hygiene*.log`).

Every SKIP of the 90, classified:

| class | cases |
| --- | --- |
| Same as Linux's headless tree (B1038's 45) | 32 real-capture replays with no `data/captures/` (`PcapExtractorTest` x10, `LoginWorldAccessors` x7, `DaybreakConnectionPcapTest` x5, `PcapStepThroughTest` x4, `PcapDaybreakHarnessTest` x3, `ZoneEntitiesRealCapture`, `ZoneChatTest`, `LoginWorldCaptureReplay`); `AssetRequirementTest` x2 (by design); `BridgeTeardownAsanRepro` x2; `SpellManagerGems` x2 + `EqActionHandlerSpellPet` x3 (no Warrior-usable spell); `SessionReplayGolden`; `SoundBufferTest.LoadSwingWav`; `visual_gate`; `zone_content_sweep_smoke` |
| Ledger OFF on Darwin (by design, "ledger off in this build") -- 16 | `SoundFontRelease` x7, `MusicPlayerSoundFontLifetime` x3, `SfxSinglePcmCache` x2, `AudioStartupPeak` x2, `AudioLedgerAttribution` x2 |
| Asset at a hard-coded `/mnt/projects` path -- 29 | `GameStateIdentity` x11 + `HCMapRealFixtures` x6 + `HCMapRaySetGolden` x4 (server maps; `WILLEQ_MAP_FIXTURES` overrides, see below), `ItemIconAtlas` x4, `SpellSpriteAtlasFile` x2, `Eqe1ArchiveConfigTest` x1, `CharCreateData` x1 (no override) |

## Linux stays identical (dev-u24)

Filled in by the hand-back of this unit (`review/B1039/linux_*`): the
headless tree (`build-headless`, `EQT_GRAPHICS=OFF`) must match B1038's
3533/3533 + 26/26, and the graphics tree builds `willeq` + the touched
test targets.

## Not verified / left open

- **A `malloc_zone` ledger backend** for Darwin: not written; the ledger is
  its OFF shape there, so no memory number comes from a Mac.
- **`RssAnon` on Darwin** reads 0 (`/proc/self/status` does not exist);
  `task_info` `phys_footprint` is the candidate. Not done.
- **`GameStateIdentity` on arm64/Apple libm**: with `WILLEQ_MAP_FIXTURES`
  pointed at the NAS maps, all 11 zones FAIL their pinned triangle/query
  digests even with `-ffp-contract=off` -- `HCMap`'s placeable rotation goes
  through `sin`/`cos`, and Apple's libm is not glibc's bit for bit. Default
  runs skip them (no `/mnt/projects` on a Mac). Not a defect in the client;
  a platform property, recorded in `docs/macos.md`.
- **Runtime behaviour beyond the suite**: the headless client was not run
  against an eqemu server from the Mac (no live leg in this card's scope).
- The 29 hard-coded-path skips could be made overridable (an env var per
  test family); not done.
- The SMB `dir_cache_off` setting is a host configuration on the mini
  (`~/Library/Preferences/nsmb.conf`), not something the repo can enforce.
- 1,178 compiler warnings on the Mac build (clang is louder than GCC);
  not triaged.
