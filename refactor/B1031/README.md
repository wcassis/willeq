# B1031 — macOS dev-platform probe: headless build error census on the M4 mini (issue #351)

**Class B. Card 1 of prove-then-fix (`docs/local_model_cards.md`): PROVE only.
No source, CMake or test change in this unit; card 2 is the fix.**

Scope (Will, 2026-10-12): the DEV PLATFORM — the headless client and the test
suite on a Mac. No visual gate, no budgets, no goldens. A windowed renderer
(ANGLE) is a later item cut from these findings.

Evidence lives outside the checkout in `/mnt/projects/fable/review/B1031/`
(index: that directory's `README.md`). Every number below is quoted from a
file there and names the command that produced it.

## Step 0 — host inventory

`uname -a; sw_vers; sysctl -n hw.memsize hw.ncpu machdep.cpu.brand_string;
xcodebuild -version; /usr/bin/clang --version | head -1` over ssh as `willeq`
(`review/B1031/host_inventory.txt`):

| item | value |
|---|---|
| host | `m4mini.goldfarm.local` (Darwin 24.6.0, xnu-11417.140.69.708.3~1, arm64) |
| OS | macOS 15.7.4 (24G517) |
| CPU | Apple M4, `hw.ncpu` 10 |
| memory | `hw.memsize` 17179869184 (16 GiB) |
| Xcode | 26.2 (17C52) |
| compiler | Apple clang 17.0.0 (clang-1700.6.3.2), `/usr/bin/clang` |

The mini also runs Ollama; the build ran under `nice -n 10` at `-j 8`.

## Step 1 — dependencies

**Finding 0 (environment, not code): `brew install` cannot run as `willeq`.**
`/opt/homebrew/bin/brew install cmake ninja fmt glm jsoncpp zlib` exited 1
with `Error: /opt/homebrew/Cellar is not writable` (`review/B1031/brew_install.log`):
the Homebrew prefix is owned by `will` (`drwxrwxr-x will admin`) and `willeq` is
not in `admin`. Fixing that needs Will (run the install as `will`, or
`chown`), so this probe installed **the exact versions Homebrew would have
installed** (`brew info --json=v2`, read-only) into a user prefix
`~/willeq-deps` with `review/B1031/deps.sh` (log `review/B1031/deps.log`,
exit 0):

| formula | version | how |
|---|---|---|
| cmake | 4.4.4 | Kitware `cmake-4.4.4-macos-universal.tar.gz` |
| ninja | 1.13.2 | `ninja-mac.zip` release binary |
| fmt | 12.2.0 | source, shared, `~/willeq-deps` |
| glm | 1.0.3 | headers only |
| jsoncpp | 1.9.8 | source, shared, `~/willeq-deps` |
| zlib | 1.2.12 | the macOS SDK's `libz.tbd` (Homebrew's `zlib` is keg-only, so CMake finds the SDK copy either way) |
| openssl@3 | 3.6.1 | already in Homebrew (`brew list --versions openssl@3`) |

Every configure/build below adds `-DCMAKE_PREFIX_PATH=$HOME/willeq-deps` and
puts `~/willeq-deps/bin` first on `PATH`; nothing else differs from the issue's
recipe. There is no `pkg-config` on the mini, so `jsoncpp` was found by the
`FIND_PATH`/`FIND_LIBRARY` fallback in `CMakeLists.txt`.

## Step 2 — configure

The tree was rsynced from this worktree at fable `bfafca12` (excluding
`build/`, `.git/` and the two asset links) to `~willeq/willeq-probe/`, the
assets linked as `data/eq_client -> ~/projects/EverQuestP1999` and
`data/eqe1_client -> ~/projects/EverQuest_ETC1` (SMB share, `~/bin/mount_projects.sh`).

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEQT_GRAPHICS=OFF \
  -DEQT_BUILD_TESTS=ON -DEQT_MEMORY_LEDGER=OFF \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 -DCMAKE_PREFIX_PATH=$HOME/willeq-deps
```

**Configure SUCCEEDS on Darwin: exit 0** (`review/B1031/configure.log`). No
`CMake Error`; four `CMake Warning`s (`grep -c "CMake Warning" configure.log`),
none ours to fix here: CMP0167 at `CMakeLists.txt:83` (`FIND_PACKAGE(Boost)`
under CMake 4.x) and three deprecation warnings inside the fetched
googletest's `cmake_minimum_required`. The summary block reports OpenSSL
(Homebrew), zlib (SDK), fmt (package), GLM, jsoncpp found; Boost,
Recast/Detour, Irrlicht and FreeRDP absent, so waypoint/navmesh pathfinding,
graphics and RDP are off; audio ON (miniaudio). The CMake `IF(APPLE)` branches
were not exercised beyond that (graphics is off).

## Step 3 — keep-going build

**The issue's command has a flag error:** `cmake --build build -k 0 -j 8`
exits 1 with `Unknown argument -k` (`review/B1031/build_attempt1_badflag.log`);
`-k` is a ninja option and has to follow `--`. The build that was measured:

```
nice -n 10 cmake --build build -j 8 -- -k 0 > build.log 2>&1; echo $? > build.exit
```

**BUILD_EXIT=1** (`review/B1031/build.log`, ends `ninja: build stopped: cannot
make progress due to previous errors.`). `grep -c "error:" build.log` = **425**;
`grep -c "^FAILED:" build.log` = **269** failed edges; 0 linker failures
(`grep -c "linker command failed" build.log`), because every link that could
fail sits behind a failed compile.

**Finding 1 — `cereal` is an undeclared dependency, and it masks the census.**
216 of the 425 lines are one line, `include/common/net/packet.h:10: fatal error:
'cereal/cereal.hpp' file not found` (+1 from `daybreak_structs.h`): clang stops
a TU at its first fatal error, so every TU that includes `packet.h` reported
NOTHING else. `CMakeLists.txt` never looks for cereal and `CLAUDE.md`'s
Dependencies list does not name it; Linux hosts have it from
`libcereal-dev` (dev-u24: 1.3.2+dfsg) and the ARM images download v1.3.2
(`docker/Dockerfile.arm-noble:162`). Homebrew has a `cereal` formula.

So the census was taken TWICE:

- **pass 1** = the issue's recipe exactly (plus the user-prefix deps):
  `build/`, `build.log`.
- **pass 2** = the same configure into `build-pass2/` plus
  `"-DCMAKE_CXX_FLAGS=-isystem $HOME/willeq-cereal/include"` (cereal 1.3.2
  headers copied from dev-u24's `/usr/include/cereal`; GitHub returned 504 for
  the tarball), `review/B1031/configure_pass2.log` (exit 0),
  `review/B1031/build_pass2.log`. **BUILD_EXIT=1**; `grep -c "error:"
  build_pass2.log` = **238**; `grep -c "^FAILED:" build_pass2.log` = **61**.
  With `-k 0` every compile edge whose inputs exist was attempted, so pass 2 is
  the COMPLETE compile census of this configuration; links behind a failed
  `willeq_core` object were not attempted (see "Not verified").

Pass 2 is the one card 2 should plan from.

## Step 4 — census

`review/B1031/census.py <log> <out>` parses the ninja log: every line
containing `error:` (the lines `grep -c` counts; the script asserts the two
totals agree) is attributed to its `FAILED:` object and put in exactly one
KIND by the first matching rule (rules in the script). Full tables, including
every distinct `file:line` site per KIND and every TU with its first error:
`review/B1031/census_pass1.md` and `review/B1031/census_pass2.md` (+ `.json`
with every record).

### Per KIND

| KIND | pass 1 lines | pass 2 lines | pass 2 distinct sites |
|---|---:|---:|---:|
| glibc/malloc internals | 176 | 176 | 14 |
| /proc readers | 0 | 0 | 0 |
| pthread_setname_np | 3 | 5 | 4 |
| socket options | 3 | 3 | 1 |
| Linux headers | 0 | 0 | 0 |
| missing third-party dependency | 240 | 24 | 20 |
| CMake | 0 | 0 | 0 |
| other | 3 | 30 | 29 |
| **total** (`grep -c "error:"`) | **425** | **238** | **68** |

Missing-dependency lines by header: pass 1 cereal 217, EGL 21, `irrlicht.h` 2;
pass 2 EGL 21, `irrlicht.h` 3.

### Every pass-2 site, by KIND

- **glibc/malloc internals (176)**
  - `src/client/core/memory_ledger.cpp:1012-1020`: `no member named 'mi' in
    'eqt::memory::ArenaReport'`, 9 sites x 19 objects = 171 lines.
    `ArenaReport::mi` (`struct mallinfo2`) is declared under
    `#if defined(__linux__)` in `include/client/core/memory_ledger.h:561`, but
    `arenaToLogLine()` reads it unguarded. `memory_ledger.cpp` is compiled
    into `willeq_core` and 18 test executables, and `EQT_MEMORY_LEDGER=OFF`
    does not compile the TU out.
  - `scripts/pi/lima_bo_census.c:104,402,499,502,503`: `unknown type name
    'off64_t'` and the implicit-int lines it causes (5). This is the Pi's
    LD_PRELOAD shim over glibc's `mmap64`, built into
    `test_lima_bo_census_shim`.
- **pthread_setname_np (5)**: `no matching function for call to
  'pthread_setname_np'` (Darwin takes 1 argument, the calling thread) at
  `src/client/graphics/background_thread_pool.cpp:53` (2 objects),
  `include/client/graphics/background_work_queue.h:163`,
  `src/client/application.cpp:937`, `src/client/application.cpp:1074`.
  The issue's other two sites (`loading_thread.cpp:149`,
  `simulation_worker.cpp:352`) are graphics TUs and are not compiled here.
- **socket options (3)**: `src/common/net/posix_udp_transport.cpp:70` `use of
  undeclared identifier 'SO_RCVBUFFORCE'` (3 objects). The `SO_RCVBUF`
  fallback already exists, so this only needs a guard.
- **missing third-party dependency (24)**: `'EGL/egl.h' file not found` in
  21 `tools/hwcaps/*`, `tools/gl_points/*` and `tools/test_gl_points.cpp`
  objects; `'irrlicht.h' file not found` via
  `include/client/input/hotkey_manager.h:3` (`tests/test_data_paths.cpp`,
  `tests/test_ranged_attack_wire.cpp`) and
  `include/client/graphics/detail/detail_types.h:4`
  (`tests/test_detail_view_distance.cpp`). These targets are registered
  whatever `EQT_GRAPHICS` says. The EGL tools compile on dev-u24 only because
  `/usr/include/EGL` happens to be installed there; the three `irrlicht.h`
  tests fail headless on Linux too (cross-check below).
- **other (30)**:
  - **headless-config rot, NOT Darwin (11)**: `src/client/application.cpp:1087,1088`
    `use of undeclared identifier 'm_renderer'`;
    `tests/test_zone_connection_flow_identity.cpp:353..454` `no member named
    'CreateZoneLoadSnapshot'` / `'SetZoneBspTree'` in `BridgeComponent` (9).
  - **void-pointer arithmetic (13)** `arithmetic on a pointer to void` (a GNU
    extension GCC accepts with a warning; clang rejects it in C++):
    `src/client/merchant/merchant.cpp:365,381,410,481,497`,
    `src/client/zone/zone_dispatch.cpp:353,367,396,418,433`,
    `src/client/entities/entities_component_world.cpp:411`,
    `tests/test_forage_fishing.cpp:584,600` — all `packet.Data() + n`.
  - **narrowing in a braced initializer (2)** (`-Wc++11-narrowing`, an error
    in clang): `src/client/inventory/inventory_cursor.cpp:247,295`
    (`int32_t` -> `uint32_t`).
  - **fmt 12 (2)**: `type_is_unformattable_for<eqt::inventory::EquipmentSlot>`
    from `src/client/combat.cpp:685` (a scoped enum passed to `fmt::format`),
    and `type_is_unformattable_for<std::__bit_reference<std::vector<bool>>>`
    from `src/client/graphics/eq/wld_fragments_mesh.cpp:298` (libc++'s
    `vector<bool>` proxy; libstdc++ + fmt 9.1 accept both).
  - **libc header placement (2)**: `tests/scoped_temp_dir.h:43` `no member
    named 'mkdtemp' in the global namespace` (Darwin declares it in
    `<unistd.h>`, glibc in `<stdlib.h>`).

No `/proc` reader, Linux-header or CMake error appeared: the `/proc` readers
(`gl_driver_census.cpp`, `load_mem_audit.cpp`, `gpu_buffer_census.cpp`, …) use
plain `std::ifstream` and COMPILE on Darwin; they fail at RUN time (file
absent), which this probe did not exercise.

### What linked

Executable targets in the graph: 246 at `bfafca12` (`ninja -C build -t targets
all | grep _EXECUTABLE_LINKER`, `review/B1031/exe_targets.txt`; 228 of them
`test_*`). `ls build/bin`: **pass 1 linked 88** (83 `test_*` + the 5 tools
`chase_rig_fit`, `dump_icon_sheet`, `item_icon_atlas_builder`,
`spell_sprite_atlas_builder`, `ui_atlas_builder`); **pass 2 linked 95** (90
`test_*`; cereal adds `test_daybreak_crc_capture`,
`test_daybreak_state_machine`, `test_eq_client`, `test_integration_network`,
`test_packet`, `test_titanium_opcodes`, `test_wire_layout_t145`). Lists:
`review/B1031/bin_build.txt`, `review/B1031/bin_build-pass2.txt`. **`willeq`
itself did not link in either pass**: it needs `willeq_core`, which has failing
objects (`memory_ledger.cpp`, `application.cpp`, `posix_udp_transport.cpp`,
`merchant.cpp`, `zone_dispatch.cpp`, …), and so does every test that links
`willeq_core`. No link step failed: every executable that was attempted linked.

### ctest (WILLEQ_REQUIRE_ASSETS unset)

`ctest --test-dir build -N | tail -1` on the mini printed `Total Tests: 1900`
for pass 1 and `Total Tests: 2018` for pass 2 — the count depends on which
gtest binaries linked (gtest discovery runs at build time; an unbuilt binary
registers one `*_NOT_BUILT` placeholder), so it is a property of the build,
not of the source. Logs: `review/B1031/ctest_N_*.log`.

| run | tests | passed | skipped | not run (binary absent) | failed |
|---|---:|---:|---:|---:|---:|
| `ctest -L hygiene` (pass 1 = pass 2) | 26 | 23 | 1 | 0 | 2 |
| `ctest -LE hygiene` pass 1 | 1874 | 1680 | 50 | 144 | 0 |
| `ctest -LE hygiene` pass 2 | 1992 | 1805 | 50 | 137 | 0 |

(The brief's `ctest -E hygiene` matches test NAMES, not the label, so it
re-ran the hygiene tests; that run is kept as `ctest_rest_*.log` and the
table uses `-LE hygiene`, `ctest_nonhygiene_*.log`.)

**Every test that ran on Darwin and is not a hygiene test PASSED.** The two
hygiene failures are both **macOS `/bin/bash` 3.2**, not the client:

- `FileSizeCeiling`: `scripts/refactor/check_file_sizes.sh` uses `mapfile` and
  `declare -A` (bash 4+): `mapfile: command not found`, `declare: -A: invalid
  option`.
- `CrowdObservationControls`: `scripts/b964/pi_crowd.sh: line 118:
  EXTRA_ENV[@]: unbound variable` (`"${arr[@]}"` of an empty array under
  `set -u` is an error before bash 4.4).

Skipped: `MemoryCapHuntScript` (hygiene; SKIP 125 when `systemd-run --user
--scope` is unusable). The 50 non-hygiene SKIPs (identical in both passes):
`GameStateIdentity.*` (11), `PcapExtractorTest.*` (10), `HCMapRealFixtures.*`
(6), `PcapStepThroughTest.*` (4), `ItemIconAtlas.*` (4),
`HCMapRaySetGolden.*` (4), `PcapDaybreakHarnessTest.*` (3),
`SpellSpriteAtlasFile.*` (2), `AssetRequirementTest.*` (2, by design: they
test the skip), `visual_gate`, `zone_content_sweep_smoke`,
`Eqe1TextureIndexShipped.CoversEveryPackInTheTree`,
`Eqe1ArchiveConfigTest.EveryGlobalArchiveExistsAsEqe1Pack`. Their individual
skip reasons were not inspected (see "Not verified").

### Linux cross-check: which failures are Darwin's

`review/B1031/linux_crosscheck.sh` compiled each of the 61 objects that failed
in pass 2, one at a time, with GCC 13.3 + fmt 9.1 in this worktree's
`EQT_GRAPHICS=OFF` tree on dev-u24 (`build/hygiene`, the step-6 configure;
result `review/B1031/linux_crosscheck_pass2.tsv`, one row per object: exit
code, `error:` count, first error). **56 compile on Linux; 5 fail there too**:

| object | Linux error |
|---|---|
| `willeq_core` `src/client/application.cpp` | `m_renderer` was not declared (1087) |
| `test_zone_connection_flow` `tests/test_zone_connection_flow_identity.cpp` | `BridgeComponent` has no member `CreateZoneLoadSnapshot` (9 lines) |
| `test_data_paths` `tests/test_data_paths.cpp` | `irrlicht.h: No such file or directory` (via `hotkey_manager.h:3`) |
| `test_ranged_attack_wire` `tests/test_ranged_attack_wire.cpp` | same |
| `test_detail_view_distance` `tests/test_detail_view_distance.cpp` | `irrlicht.h` via `detail_types.h:4` |

**Finding 2 — the headless (`EQT_GRAPHICS=OFF`) build is broken on Linux as
well**, independent of macOS: `willeq_core` itself fails, so no headless
`willeq` links on ANY host at `bfafca12`. CI configures `EQT_GRAPHICS=OFF` only for the
`hygiene` stage, which compiles nothing (`.gitlab-ci.yml:142`), and builds only
`EQT_GRAPHICS=ON` (`.gitlab-ci.yml:178`), which is why nothing noticed. These 14 Mac error lines
(`application.cpp:1087,1088`, the 9 flow-identity lines, the 3 `irrlicht.h`
lines) are the headless share of the census; the other 224 pass-2 lines are
Darwin/clang/libc++/fmt-12 specific.
