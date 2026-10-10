# B946 — the [gldriver] line: which GL driver the client actually loaded (GitLab #264)

Branch `task/kb-b946-gl-driver-log` (code half: `103155c6`, docs half: the commit
on top of it). MR !127 into `fable`.

## The problem

A `willeq-nollvm` binary (B935, #253) selects the shipped LLVM-less Mesa prefix
by an absolute DT_RPATH. If that directory is absent on the machine it runs
on, `ld.so` falls through to the default search path and maps the SYSTEM Mesa
— libLLVM included, ~30 MiB of RssFile more — with no error at all. Before
this, only the Pi rigs caught it (`mesa_mismatch`, exit 67, from their maps
snapshots, `docs/ci.md` "The shipped Mesa prefix"); a hand run, or any other
launcher, got a silent fallback, and the client logged neither the driver
library it mapped nor the GL renderer string.

## What the client now does

At the first GL init — `IrrlichtRenderer::initLoadingScreen()` →
`glDriverInitLog()` (`src/client/graphics/renderer/renderer_init.cpp`) — the
client logs ONE INFO line:

```
[gldriver] renderer='<GL_RENDERER>' gallium=<path or none> llvm=<path or none>
```

`renderer` is everything `GL_RENDERER` says, single-quoted (spaces allowed);
`gallium` and `llvm` are the mapped libgallium/libLLVM paths from the
process's own `/proc/self/maps` — the same file the `[memmap]` census already
attributes (T491) — or the word `none`. `GL_RENDERER` comes back through
`gles2GetGlRenderer()` (the driver stores it where the context is guaranteed
current; `COpenGLES2Driver.cpp:2522`), because the driver's GL headers are
private to the patched Irrlicht build. The one-place choice is x86/ARM-shared:
`initLoadingScreen()` runs once per process on both, the same
`COpenGLES2Driver.cpp` is compiled into both trees, and the client cannot call
`glGetString` itself.

**The nollvm decision** (issue acceptance 2): the binary identifies itself in
its own dynamic section. `glPrefixExpected()` reads `/proc/self/exe` and
`parseElfRpath()` walks the ELF for the DT_RPATH/DT_RUNPATH string
(fixture-tested over synthetic ELF bytes, no GL). The publish step
(`scripts/pi/mesa_nollvm_select.sh`) is the one place that knows the prefix —
it writes `<prefix>/lib` into the rpath with `patchelf --force-rpath` — so no
build-time define is needed: a fresh build of the same commit behaves like a
deployed one, and a plain `bin/willeq` (no `mesa-nollvm-*` rpath entry) never
fires this. If the baked-in prefix's lib directory does not exist at start,
the client logs an ERROR naming the missing directory and the fallback it
will take — the default search path, i.e. the system Mesa — and **continues**:

```
[gldriver] nollvm Mesa prefix missing: /home/user/mesa-nollvm-25.2.8/lib does not exist; falling back to the default library search path (the system Mesa -- libLLVM included). Deploy it with scripts/pi/deploy_mesa_prefix.sh (or run bin/willeq / WILLEQ_MESA=system for the opt-out). The client continues: a hand run on a fresh Pi must still work, and the Pi rigs already fail such a run (mesa_mismatch, exit 67).
```

It must not refuse: a hand run on a fresh Pi still has to work, and the rigs
already fail such a run.

## The rig's half

`scripts/pi/rendered_zone_summarize.py` (the sweep's summarizer) reads the
line back from the run's `client.log` (`mesa_driver_field()`, first line wins)
and appends a `mesa_driver` column **at the END** of the TSV header (its value
is `renderer:gallium:llvm`, or empty for a log without the line). The
maps-snapshot proof (`mesa_proof`, `mesa_mismatch`/exit 67) keeps its rules and
stays the PRIMARY evidence; the column is the additive, independent read —
visible even when the snapshots are missing. `scripts/pi_rendered_zone_sweep.sh`'s
result row reports `driver=` from it.

## Decisions

- **rpath self-detection, not a build-time define.** The publish step already
  knows the prefix and writes it into DT_RPATH; re-reading it from
  `/proc/self/exe` needs no new publish step and no CMake define.
- **ERROR-and-continue** on a missing nollvm prefix (see above).
- **Append-at-end column**, matching the B962/B978/B1006 TSV-column precedent;
  the maps proof stays primary.
- **One /proc shape.** `scanMapsText()` parses the PATHNAME column of
  `/proc/self/maps` whitespace-agnostically (six fields, pathname last). Two
  regressions are pinned by fixtures in `tests/test_gl_driver_census.cpp`:
  the first draft skipped only four columns, then searched for `' '`
  literally, which matched nothing on a tab-padded line and reported
  `none none` for every process.

## Evidence

- **Red (on `origin/fable`):** a probe that includes
  `client/core/gl_driver_census.h` fails to compile (`fatal error: … No such
  file or directory`), and the summarizer module has no `mesa_driver*`
  attribute. Repro output:
  `/home/user/kb-evidence/b946-gl-driver-log/docs-commit/redgreen_probe.txt`.
- **Green (branch tip):** `test_gl_driver_census` 16/16 and
  `test_rendered_zone_gl_driver.py` 6/6 (the probe round-trips a real-format
  line through `parseGlDriverLine`/`makeGlDriverLine` and
  `mesa_driver_field()` returns `renderer:gallium:llvm`). Pipeline 754 at
  `103155c6` fully green (hygiene 01m18s / build 28m58s / test 03m50s / gate
  08m37s) [measured].
- **One x86 run's real line** (llvmpipe, surfaceless EGL; review round 1,
  `103155c6`):
  `/home/user/kb-evidence/b946-gl-driver-log/review-round1/x86_run_gldriver.log:63`
  [measured]:
  `[2026-10-10 19:53:27.245] [INFO ] [GRAPHICS] [gldriver] renderer='llvmpipe (LLVM 20.1.2, 256 bits)' gallium=/usr/lib/x86_64-linux-gnu/libgallium-25.2.8-0ubuntu0.24.04.4.so llvm=/usr/lib/x86_64-linux-gnu/libLLVM.so.20.1`
  — matches `makeGlDriverLine()` byte-for-byte (modulo the log prefix), and
  both paths are the system Mesa's, as expected on this x86 box (no
  `mesa-nollvm-*` rpath: the ERROR variant does not fire here).
- **The ERROR variant** is pinned by `MesaPrefixMissingDecidesOnTheDirectory`
  and `NollvmPrefixFromRpath` (synthetic rpath strings + a nonexistent dir;
  no GL, no real nollvm binary) [measured as a fixture, not exercised on a
  live nollvm binary].

## Not verified / left open

- **The ARM/Pi half is unverified by design.** The card excludes a Pi run
  (the Pi is not part of this card); a real `willeq-nollvm` process logging
  the INFO line, a real missing-prefix ERROR on the Pi, and the sweep's
  `driver=` column from a live run remain to be observed by the next Pi rig
  pass (`scripts/pi_rendered_zone_sweep.sh`) — the format itself is what the
  unit tests and the x86 line pin.
- `gles2GetGlRenderer()` returns `""` before the first `initDriver()`, so the
  line degrades to `renderer=''` rather than a stale string — the caller
  (`initLoadingScreen`) guarantees a context exists, so this is belt-and-
  braces, not a path anyone has exercised.
