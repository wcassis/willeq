# macOS: the headless dev platform (B1039, #359)

A Mac builds and runs the **headless** client (`EQT_GRAPHICS=OFF`, the
`--no-graphics` binary) and its whole test suite. Proven on the M4 mac mini
(`m4mini.goldfarm.local`, Apple M4, macOS 15.7, Xcode clang, Homebrew under
`/opt/homebrew`). B1031 (#351) took the census; B1038 (#358) fixed the half
that was broken on Linux too; B1039 fixed the Darwin half.
`refactor/B1039/README.md` has the RED/GREEN numbers.

## What is NOT supported

- **The renderer.** No Irrlicht/GL build: the configure line below is
  `-DEQT_GRAPHICS=OFF`. A windowed renderer (ANGLE) is a separate later item.
- **The visual gate and every golden.** `visual_gate` and
  `zone_content_sweep_smoke` skip in a headless tree on any host.
- **Memory-ledger numbers.** The ledger's allocator hooks wrap glibc's
  `__libc_*` entry points, which macOS does not have, so on Darwin the ledger
  always compiles to its OFF shape (`include/client/core/memory_ledger.h`):
  no symbol is replaced, every category reads 0, `enabled()` is false, and
  `RssAnon` (read from `/proc/self/status`) reads 0. `test_memory_ledger`
  is not built there. A `malloc_zone` backend is not written. Budgets and
  memory findings come from Linux and the Pi only.
- **The EGL/GLES2 probe tools** (`tools/hwcaps/`, `test_gl_points`): built
  only where EGL and GLESv2 are found. **The Lima BO census shim**
  (`scripts/pi/lima_bo_census.c`) is Linux-only.
- **Live rigs** (`scripts/eqemu/`, `scripts/b964/crowd_run.sh`,
  `scripts/pi_rendered_zone_sweep.sh`): they use GNU `timeout`, `/proc` and
  `nproc`. Their hygiene tests run against inert stubs and pass.

## One-time setup

    xcode-select --install          # or a full Xcode
    brew install cmake ninja fmt glm jsoncpp cereal bash openssl@3

**Homebrew's `bash` must come first on `PATH`** when running ctest: the rig
wrappers that `CrowdObservationControls` drives need bash >= 4, and macOS's
`/bin/bash` is 3.2 (that test says so by name if it finds 3.2).
`FileSizeCeiling`'s script runs under either.

**The NAS.** The assets live on the `projects` SMB share. On the mini,
`~/bin/mount_projects.sh` (idempotent) mounts it at `~/projects`, so the
trees are `~/projects/EverQuestP1999` and `~/projects/EverQuest_ETC1`.
**Turn off the SMB directory cache before mounting**, or large directories
list wrong: with the default, `ls ~/projects/EverQuestP1999` returned the
first 1,000 names eight times over and no `snd*.pfs`, so every PFS-sound
test failed.

    printf '[default]\ndir_cache_off=yes\n' > ~/Library/Preferences/nsmb.conf
    umount ~/projects; ~/bin/mount_projects.sh
    ls ~/projects/EverQuestP1999 | sort | uniq -d | head   # must print nothing

## Build

From a checkout (or an rsynced tree) on the Mac:

    ln -sfn ~/projects/EverQuestP1999 data/eq_client
    ln -sfn ~/projects/EverQuest_ETC1 data/eqe1_client
    PATH=/opt/homebrew/bin:$PATH cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON \
        -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3
    PATH=/opt/homebrew/bin:$PATH cmake --build build -j8

The memory ledger option stays at its default (ON); the configure prints
`Memory ledger: ON requested; macOS has no glibc allocator entry points --
compiled in its OFF shape`, plus one `NOT FOUND`/`not built` STATUS line each
for EGL/GLESv2 and the Lima shim.

## Run

    ./build/bin/willeq --no-graphics --help
    ./build/bin/willeq --no-graphics -c <config.json>

## Test

    export PATH=/opt/homebrew/bin:$PATH
    export EQ_CLIENT_PATH=$HOME/projects/EverQuestP1999
    export WILLEQ_EQE1_PATH=$HOME/projects/EverQuest_ETC1
    ctest --test-dir build -LE hygiene -j8 --output-on-failure
    ctest --test-dir build -L hygiene

`EQ_CLIENT_PATH` and `WILLEQ_EQE1_PATH` point the asset tests at the NAS
mount (their default is the Linux `/mnt/projects/...` path). Without them,
tests that look only there SKIP, and `test_eqe1_loader` FAILS. The skips
of a correct run are the same kinds as Linux's headless tree: absent
`data/captures/` replays, the no-renderer gate tests, and the cases that
skip by design (`refactor/B1039/README.md` names them).

## What differs on Darwin in the code

Every difference is a `__linux__`/`__APPLE__` guard or a CMake condition;
the Linux build compiles the same code it did before.

- The memory ledger: the OFF shape (above); `[memarena]`'s mallinfo2 line
  is Linux-only, like its only caller.
- Thread names: the T504 names go through `eqt::setCurrentThreadName`
  (`include/common/thread_name.h`), the one-argument `pthread_setname_np`
  on macOS. The tree has the 6 thread names set this way.
- The UDP receive buffer: no `SO_RCVBUFFORCE`; the `SO_RCVBUF` fallback
  runs and the INFO line says `this platform has no SO_RCVBUFFORCE`.
- The shipped-data resolver finds the binary's directory with
  `_NSGetExecutablePath` (there is no `/proc/self/exe`).
- `test_headless_shutdown` uses `pipe()` + `FD_CLOEXEC` (no `pipe2`).
