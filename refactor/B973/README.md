# B973 -- post-load entity builds starve under a non-Green governor

GitLab #291 (class A, lane pi). Branch `task/B973-entity-build-starvation`.
Found by B964 (#282, `refactor/B964/README.md`). Raw evidence:
`/mnt/projects/fable/review/B973/` (x86 runs `x86/crowd1`, `x86/crowd2`; Pi runs `pi/crowd1`).

## STATE AT HAND-OFF 2026-10-01

Hand-off ordered by the coordinator mid-unit. Everything below is committed on
the branch (WIP commits); nothing is merged.

### DONE (with evidence)

1. **Mechanism reproduced and traced on the Pi** (ARM build `7a8041d9`,
   `ci-arm/7a8041d9.../bin/willeq-nollvm`, qeynos2 1280x720, 200 GM-spawned
   player-race NPCs after zone-in, no zone hop; `review/B973/pi/crowd1/qeynos2/{legacy200,new200}/`).
   Under `WILLEQ_ENTITY_BUILD_LEGACY=1` (the pre-B973 rule): **1 of 200 crowd
   NPCs built in the whole run**; 2,810 of 2,921 traced frames Red; the
   `[entity-build]` windows show `units=0`, `unbuilt_visible` 127-128,
   `awaiting_prep` 105-117 (prep RESULTS were never distributed: the poll was
   Green-gated too), `pending_uploads` growing 14 -> 325 (decoded textures
   piling up un-uploaded). Mechanism, as traced:
   - post-load work (prep-result distribution AND every build step) ran only
     on Green frames, one unit per Green frame;
   - a crowd entering the visible set re-arms the progressive load (T111),
     whose entity path was GREEN-gated as well;
   - the governor watchdog (`hasPendingWork` = progressive active) resets the
     history every 90 non-Green frames, giving ONE Green frame; that frame's
     single unit was consumed by `pollAndDistributePrepResults()` (a new prep
     result each time), so the build step itself almost never ran (B964's
     legacy Pi log: one build every ~4 s, `Placeholder -> NodeSetup` 120-550 ms);
   - with the crowd in view the frame stays Red (~50-83 ms here), so no Green
     frame comes and nothing improves.
2. **Two further starvation defects found and fixed in the same path:**
   - the nearest not-ready Placeholder was chosen and returned "no work",
     blocking every ready entity behind it (selection now skips it);
   - a race/gender in the loader's NEGATIVE cache (e.g. Crowd race 130 g0)
     looped forever: prep "completes" in 0 ms, `buildEntityMesh()` deferred
     it to the prep worker again (400+ deferrals per entity in one x86 run,
     each taking the nearest-first slot). `entity_mesh_build.cpp` no longer
     defers a known-missing model.
3. **The heavy unit, decomposed** (new `EntityBuild:` line fields
   `upload= create= tex_loads=`): the create unit called
   `uploadPendingEntityTextures()` = upload EVERY pending texture: x86 up to
   68 textures / 485 ms in one step. Texture uploads are now their own units,
   ONE texture each (`ConstrainedTextureCache::uploadPending(1)`), run before
   creates. After that, per-unit costs on the Pi (`new200` run): create
   (incl. B966's folded NodeSetup) p50 3.89 / p90 6.61 / max 24.9 ms;
   finalize p50 0.05 / max 0.26 ms; equip 0.16 / 113.9 ms (2 samples).
   x86: create p50 0.70 / p90 4.79 / max 135 ms (first build of a race).
4. **The fix** (`include/client/graphics/entity_build_scheduler.h`,
   `src/client/graphics/entity_build_scheduler.cpp`,
   `src/client/graphics/frame/renderer_frame_entity_build.cpp`, edits in
   `renderer_frame.cpp`, `renderer_zone_stream.cpp`, `entity_build_step.cpp`):
   per frame the first unit ALWAYS runs when work is pending (every governor
   state); further units while work-so-far + the kind's measured average fits
   the state's slice (Green 20 % / Yellow 12 % / Red 6 % of the target frame
   = 6.7 / 4.0 / 2.0 ms at 30 fps). Prep queue/dispatch/distribution run every
   frame (data moves, not units). Priority: player > in-progress > the
   player's target > nearest. Loading-screen path unchanged. The re-armed
   progressive load without the loading screen uses the same slice.
   `WILLEQ_ENTITY_BUILD_LEGACY=1` = the old rule (A/B).
   **B966 (#284) fixed here:** the create unit runs NodeSetup in the same unit,
   so a node is never drawn with its raw materials.
5. **Before/after, Pi** (qeynos2 1280x720, same binary, legacy vs new):

   | metric | legacy | new (B973) |
   |---|---|---|
   | crowd NPCs built (of 200) | **1** | **182** (173 by the end of the build phase) |
   | visible backlog drained after the last spawn | never | 0.7 s |
   | frames with a build unit / traced frames | 43 / 2,921 | 628 / 723 |
   | build work per frame p50 / p95 / max ms | - | 6.50 / 20.84 / 344.5 |
   | frame ms p50 (max window p95), spawn phase | 66.1 (100.4) | 38.0 (195.8) |
   | build phase | 50.0 (88.7) | 39.1 (147.8) |
   | hold phase (after the walk) | 83.4 (90.8) | 100.1 (101.3) |

   The 344 ms max (and 60-290 ms frames at f=81..224) are single
   `upload` units early in the spawn phase while the prep worker parsed an
   archive (389 ms preload of race 128 at the same timestamp): suspected
   contention on `ConstrainedTextureCache::mutex_` (shared with the worker's
   `getOrLoad()`); the build at `HEAD` adds `lock_wait=` / `upload=` to an
   INFO line for any upload unit >= 20 ms to confirm -- NOT yet run on the Pi.
   Hold phase is heavier with the crowd BUILT (137 impostors + name tags) than
   as placeholders: that is the crowd's own draw cost (B964/B975), not build work.
6. **x86** (llvmpipe, loaded host -- not performance evidence;
   `review/B973/x86/crowd1/qeynos2/legacy200`, `crowd2/qeynos2/new200`):
   legacy 125 built / backlog never drained; new 194 built, drained 3.3 s after
   the last spawn; build work per frame p50 3.05 / p95 8.75 / max 135 ms.
7. **Tests:** `tests/test_entity_build_scheduler.cpp` (9 cases: legacy starves
   in Red; >= 1 unit/frame in every state; work after the first unit within
   the slice; per-kind predictor; 200-entity crowd in permanent Red built in
   <= 400 frames; the window line; the real staged build under a Red scheduler
   reaches Built and the create unit leaves MeshFinalize (B966)). Passing.
   Full suite at commit `7a8041d9`: 5148/5151, the 3 reds were hygiene
   (module_map, ZeroCallers, WriteOnlyFields) and are fixed at `HEAD`
   (not yet re-run).
8. **Rigs:** `scripts/b973/make_tour.py` (post-load crowd, no hop),
   `scripts/b973/crowd_run.sh` (x86), `scripts/b973/pi_crowd.sh` (Pi),
   `scripts/b973/build_table.py` (folds `client.log`; `--units` per-unit table).

### REMAINS (numbered checklist)

1. Wait for the local build started at hand-off, then re-run the three
   hygiene tests and the suite:
   `cmake --build /mnt/projects/fable/wt/audit-greenstarve/build -j24` (background, exit file),
   `ctest --test-dir /mnt/projects/fable/wt/audit-greenstarve/build -L hygiene`,
   `ctest --test-dir /mnt/projects/fable/wt/audit-greenstarve/build -j6 --output-on-failure`.
2. ARM build of `HEAD` was triggered (`build-arm` job 2088). When
   `/mnt/projects/fable/ci-arm/<HEAD sha>/bin/willeq-nollvm` exists, re-run the
   Pi (take nothing by hand -- the rig takes eqemu then Pi lock, owner b973;
   the Pi lock was held by **b975** at hand-off, wait for it):
   `bash scripts/b973/pi_crowd.sh --pi-dir /mnt/projects/fable/ci-arm/<sha> --variants new200 --out /mnt/projects/fable/review/B973/pi/crowd2`
   then `python3 scripts/b973/build_table.py .../zones/qeynos2/client.log` and
   `grep "upload unit" .../client.log` to settle the 60-344 ms upload units
   (lock_wait vs upload). If lock wait dominates: the fix direction is to move
   `findCompressedEntry()`'s late archive open out from under `mutex_`
   (constrained_texture_cache.cpp `getOrLoad`); file it as a new B issue if
   not fixed here. Optionally 800x600 and gfaydark for a second zone.
3. Re-run x86 `bash scripts/b973/crowd_run.sh --zones qeynos2 --variants new200,legacy200 --out /mnt/projects/fable/review/B973/x86/crowd3` on `HEAD`
   for the final x86 table (legacy accounting in the progressive path was
   added after crowd1).
4. State the bound in this README: time to all-visible-built (Pi: drained 0.7 s
   after the last spawn at 1 spawn/0.25 s; i.e. the build keeps pace with the
   spawns) and per-frame build work (slice + at most one unit; one unit's cost
   is unbounded by design: max create 24.9 ms, max upload 344 ms -- see 2).
5. Docs: `src/client/graphics/CLAUDE.md` -- add a governor paragraph (the
   governor decides HOW MUCH, the scheduler guarantees progress; slices;
   legacy switch; trace env); `refactor/BOARD.md` -- strike B973 (FIXED, this
   README) and B966 (#284, fixed by the NodeSetup fold); board/file new issues
   (B numbers from the current max `B###:` title; B976 = #294 last known):
   (a) upload-unit stalls (if confirmed), (b) decoded textures pile up
   un-uploaded while builds are starved (legacy pending_uploads -> 325: memory),
   (c) built crowd costs more to draw than placeholders (hold 100 vs 83 ms).
6. `bash scripts/refactor/check_file_sizes.sh fable` from the worktree; local
   visual gate (`bash scripts/build-gles2-x86.sh`, then the gate) -- no golden
   is expected to move (the benchmark builds no EntityRenderer).
7. Merge `gitlab/fable` again before the final push; replace the WIP commits'
   message if wanted; MR body `Closes #291` (and #284); #291 -> `status::review`.

### Locks / stacks touched

- eqemu LOCAL lane: taken/released by every rig run (owner b973); all runs
  restored byte-identically with the world down and ran `down.sh`. Free at hand-off.
- Pi lock: taken/released by the two sweep runs (owner b973). At hand-off it is
  held by **b975** (another unit) -- not touched.
- No builds on the Inspur or the Pi.
