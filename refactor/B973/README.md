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

## COMPLETED 2026-10-03 (hermes-ext lane, MR !72 un-drafted)

Final head `06586cf4` (the hermes-ext-lane commit on top of the hand-off).
CI pipeline **475** on that head: green — hygiene 55 s, build 355 s,
test (full suite) 164 s, gate 422 s, build-arm success (the Pi build
`06586cf4...` under `~/ci-arm/`, Mesa pin cc4d9beb). Local: `ctest -R
EntityBuildScheduler` 7/7, `ctest -L hygiene` 22/22.

### REMAINS, resolved

1. **Local tests** — `ctest -R EntityBuildScheduler` 7/7, `ctest -L hygiene`
   22/22 (this host, Release, `-j3`); the full suite is CI's `test` job
   (pipeline 475, green).
2. **Pi re-run of `HEAD`** (qeynos2 1280x720, `06586cf4`'s `willeq-nollvm`,
   Mesa 25.2.8 pin-verified, VM lane `EQEMU_HOST=172.16.200.99`):

   | metric | legacy200 | new200 (B973) |
   |---|---|---|
   | crowd NPCs built (of 200) | **1** | **180** (173 by the end of the build phase) |
   | visible backlog drained after the last spawn | never | **0.4 s** |
   | frames with a build unit / traced frames | 30 / 2,108 | 602 / 710 |
   | build work per frame p50 / p95 / max ms | 0.00 / 0.00 / 377.71 | 6.61 / 19.18 / 473.05 |
   | frame ms p50, spawn phase | 40.3 | 46.7 |
   | build phase | 100.0 | 83.3 |
   | hold phase | 66.7 | 110.0 |
   | `EntityBuild: ... NodeSetup ->` lines | 1 (Crowd022: the B966 frame) | **0** |

   **Verification bar met: 180 >= 180 of 200 built, backlog drained within
   seconds of the last spawn, legacy never.**
   **B966 confirmed on the Pi:** in the new run every crowd entity leaves
   `Placeholder -> MeshFinalize` in the create unit (173 of them) and there
   are zero separate `NodeSetup` steps — the node is never drawn with its raw
   materials. The legacy run shows the split (`Crowd022: Placeholder ->
   MeshFinalize` then `NodeSetup -> EquipAttach`/`Built`).
   **The 60-344 ms upload units are settled (REMAINS 2's question):** all 26
   upload units >= 20 ms in the new run logged `lock_wait=0.00 ms`
   (`EntityBuild: upload unit 1 tex in 56.51 ms (lock_wait=0.00 ms
   upload=56.51 ms, 16 pending before)` is the worst) — the cost is the GPU
   upload itself, NOT contention on `ConstrainedTextureCache::mutex_` with the
   prep worker, so the `findCompressedEntry()` lock-scope change is NOT
   confirmed as the fix direction (noted on #301/B982). The run's per-frame
   max (473.05 ms) is a CREATE unit: the first build of a non-crowd race
   (`a_grass_snake003`, `create=472745us tex_loads=0`) — the same
   first-build-of-a-race class as crowd1's 24.9 ms max and x86's 135 ms.
3. **x86 re-run of `HEAD`** (llvmpipe, counts/drain only — NOT performance
   evidence; `~/kb-evidence/b973-entity-build/x86/crowd3/`): new200
   **200 built, drained 3.0 s** after the last spawn; legacy200 192 built,
   last one 119 s after the last spawn (the legacy path DOES drain on x86 —
   the governor is frequently Green here; the Pi is the evidence).
4. **The bound** (measured, Pi): at 1 spawn / 0.25 s the build keeps pace —
   the visible backlog drains **0.4 s** after the last spawn (180 built by
   move, the other 20 are out of the visible set at tour end). Per-frame build
   work is bounded by the state's slice PLUS the first unit, whose cost is
   unbounded by design: this run's max first unit is the 473 ms
   first-race-arcade create (above); upload units max 56.5 ms.
5. **Docs / board / issues:**
   - `refactor/BOARD.md`: B973 and B966 struck (FIXED, this README / the
     NodeSetup fold).
   - New issues filed (max `B###:` title at filing was B981 = #300):
     **#301 (B982)** upload-unit stall class (the 60-344 ms units; this run's
     finding posted as a note — it is the upload itself, plus the
     first-race-arcade create cost); **#302 (B983)** the decoded-texture
     pending-upload queue is unbounded while builds starve (legacy Pi:
     14 -> 325 pending); **#303 (B984)** a fully built crowd costs more to draw
     than placeholders (Pi hold p50 110 vs 67 ms in this pair — same sign as
     crowd1's 100 vs 83); **#304 (B985)** the sweep rig's dev-host/NFS
     coupling (this unit's Pi leg, below).
   - `src/client/graphics/CLAUDE.md`'s governor paragraph is NOT in this MR:
     the write-guard blocks agent-instruction-file writes in this lane (the
     same guard B978 hit); the paragraph text is on the kanban card's thread
     for a follow-up pass.
6. **File sizes + gate:** `check_file_sizes.sh -C . --all` clean (1,337
   files, 0 warn, 0 fail; the `COpenGLES2Driver.cpp` 2,502-line WARN is
   pre-existing at `origin/fable`, untouched by this branch). The CI gate on
   this head (pipeline 475, job 2269) is green with **no golden moved**
   (`git diff origin/fable...HEAD -- tests/goldens/` is empty: the benchmark
   builds no EntityRenderer).
7. **Final state:** `git merge origin/fable` re-run before the final push; MR
   !72 un-drafted with `Closes #291` + `Closes #284`; #291 and #284 labeled
   `status::review`.

### Lane notes (hermes-ext)

- `scripts/b973/pi_crowd.sh` gained the `--allow-vm-lane` passthrough
  (commit `06586cf4`, in scope per the card; the VM lane refuses without it).
- The shared `pi_rendered_zone_sweep.sh` rig is dev-host-coupled: its runner
  invocation runs `python3 $REPO/scripts/pi/rendered_zone_run.py` ON THE PI,
  and `$REPO` (the checkout), `$CFG`, `$TOUR` and `$dir` (the `--out` path)
  are local-to-the-launcher paths that exist only on the dev host's NFS.
  From hermes-ext (no NFS) the runner therefore never started ("no
  summary.json (runner never finished)"). This unit's Pi leg was run through
  the repo's own pieces with the paths staged on the Pi (runner + tours +
  config in `/tmp/b973keep`, run dir `/tmp/prs`): same client binary, same
  tour, same cgroup, same Mesa. The rig fix for the non-NFS lane is B985
  (issue #304) — it is NOT in this MR (out of scope: the card authorized
  only the wrapper's one-line passthrough).
- All eqemu runs restored BYTE-IDENTICALLY with the world down and ran
  `down.sh`; the Pi and eqemu locks are released (`pi_lock_show` /
  `eqemu_lock_show` empty after the last run).

