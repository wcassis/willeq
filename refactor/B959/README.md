# B959 -- the GLES2 shader-binary cache is no longer written relative to the CWD

GitLab #277 (class B). Branch `task/B959-shader-cache-path`. Found by B958.

## The defect

`COpenGLES2Driver::initDriver()` called
`shaderManager_.enableBinaryCache("config/shader_cache", ...)`, so
`COGLES2ShaderManager` wrote `<hash>.bin` under a path relative to the current
directory. Scripts `cd` to the repo root (on NFS for Pi runs), so B952's runs
left 46 binaries inside a checkout; from a CWD without `config/` the
non-recursive `mkdir` failed and every save was dropped silently. It broke
CLAUDE.md's rule that generated caches live outside the checkout (T402/B248).

## Writers and readers (every one, grepped over `docker/irrlicht-drm`, `src`, `tools`)

| site | role |
|---|---|
| `docker/irrlicht-drm/COpenGLES2Driver.cpp` `initDriver()` | the only place a directory was named (now: the injected one) |
| `COGLES2ShaderManager::enableBinaryCache` (`COGLES2Shaders.cpp`) | stores the directory, `mkdir`s it |
| `COGLES2ShaderManager::saveBinaryToCache` | WRITER: `glGetProgramBinaryOES` -> `<dir>/<hash>.bin`, called from `buildProgram()` |
| `COGLES2ShaderManager::loadCachedBinary` | READER: `<dir>/<hash>.bin` -> `glProgramBinaryOES`; a miss or a rejected blob falls back to compiling |
| `tools/hwcaps/gles2_alpha_test_probe.cpp` | only mentions the cache in a comment (it uses `glGetProgramBinaryOES` on its own raw programs, no file) |

No other file calls `glGetProgramBinary`/`glProgramBinary`/`enableBinaryCache`.

## The fix and the shape

* `eqt::ResolveShaderCacheDirectory()` (`data_paths.h/.cpp`) returns
  `<first writable cache candidate>/shader_cache`, via
  `ResolveCachePathForWrite()` (`$WILLEQ_CACHE_DIR`, `$WILLEQ_DATA_ROOT/cache`,
  `<exe dir>/cache`, `cache/`), "" if nothing is writable.
* The driver gets a free function `eqtSetShaderBinaryCacheDir(const char*)`
  (defined in `COpenGLES2Driver.cpp`). Unset or empty = cache OFF.
* `include/client/graphics/gles2_shader_cache.h` gives hosts one call,
  `eqt::ConfigureGles2ShaderCache()`, made right before `createDeviceEx()` in
  the client (`renderer_init.cpp`) and every tool that creates the GLES2
  driver: `gles2_zone_benchmark` (`zone_bench_loader.cpp`),
  `gles2_alpha_test_probe`, `gles2_composite_audit`, `held_item_candidate_search`,
  `p50_mask_plumbing_probe`, `ui_inventory_capture` (3 sites), `ui_panel_capture`,
  `model_viewer`. `gles2_alpha_test_probe` now links `data_paths.cpp`.

Why a setter and not an env var or including `data_paths.h` in the driver: the
driver is an Irrlicht patch linked into libIrrlicht, below the client, and must
not include client headers. The existing host-to-driver channel is a plain
free function with plain types (`eqtEnableGPUSyncProbe`, `gpu_sync_probe.h`).
An env var read by the driver would need its own copy of the four-candidate
walk. The safe default matters most: a host that forgets the call loses the
cache, it can never write into a checkout (and a census test catches the
forgetting).

Read side: the driver reads from the same directory it writes. A read-only
staged cache directory (the `ResolveCachePathForRead` case where nothing is
writable) is NOT served: `ResolveShaderCacheDirectory()` returns "" and the
cache is off. A cache miss is never an error.

## Regression nets

* `tests/test_data_path_routing.cpp`: `ShaderCacheIsNotWrittenRelativeToTheCwd`
  (no code under `src`, `include`, `tools`, `docker/irrlicht-drm` names a
  `shader_cache` path except the resolver) and
  `EveryGles2DeviceCreatorInjectsTheShaderCacheDir` (every file that selects
  `EDT_OGLES2` and creates a device calls `ConfigureGles2ShaderCache`).
* `tests/test_shader_cache_path.cpp`: a real headless GLES2 device run from a
  scratch CWD that has a `config/` directory, `WILLEQ_CACHE_DIR` = a temp dir:
  binaries land in `<temp>/shader_cache`, nothing (no `.bin`, no `shader_cache`
  directory) under the CWD; and with no directory injected, nothing is written
  at all.

## Cleanup

Untracked `config/shader_cache` directories removed from `main` and 20
`wt/*` worktrees (`git ls-files` showed 0 tracked files in each; the files were
ignored via `.gitignore`, which is why `git status` never showed them): 595
`.bin` files in 21 directories. `.gitignore`'s `config/shader_cache/` line was
removed so a regression would show up as untracked files.

## Where the cache lands now

* Client / tools run with no env: `<exe dir>/cache/shader_cache`.
* The visual gate (`scripts/ci/visual_gate.sh`, `build-gles2/bin/gles2_zone_benchmark`)
  sets no `WILLEQ_CACHE_DIR`, so on the runner it lands in the warm build
  tree's `build-gles2/bin/cache/shader_cache` -- outside the source checkout,
  persisting between pipelines like the tree itself. A cold build therefore
  starts with a cold shader cache.
* Pi rigs that set `WILLEQ_CACHE_DIR` (`pi_rendered_zone_sweep.sh`,
  `scenario_zone_sweep.sh`) get `<that>/shader_cache`.

## Not verified

* A Pi run: nothing was run on the Orange Pi / Lima, so the cache directory
  resolution on the Pi (exe dir on NFS vs `WILLEQ_CACHE_DIR`) is reasoned, not
  measured.
* The Mesa-build-collision half of #277 (two Mesa builds with one version
  string sharing a cache key) is unchanged and unmeasured.
* The cache key is unchanged; whether a cache written by an older build under
  `config/shader_cache` would have been valid is moot (those files were deleted).
