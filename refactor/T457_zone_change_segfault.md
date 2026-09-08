# T457 — the zone-change segfault with the inventory open (B482), the double world (B483), the airplane zone-in (B488)

Class A, eqemu lock (atomic; must be ABSENT), slot3. Read T449's
§Review and `review/T449/README.md` (its board items were numbered
B474–B481 on its branch and become B482–B489 at merge: the segfault is
B482, `up.sh` double world B483, `cast_lands` spell B484, the peer
B485, buff re-run B486, chat input B487, `#zone airplane` B488, no
pick-up gesture B489), the gdb backtrace: `CharacterModelView::
renderToTexture()` → `EQAnimatedMeshSceneNode::render()` →
`COpenGLES2Driver::bindAlphaCompanionUnit()` → `COGLES2Texture::
getOpenGLTextureName()` on a texture the zone unload destroyed; T432's
persistence reopens the paperdoll at login so a saved `Show=1` crashes
the next zone change. Read the zone unload order (`ZoneLifecycleComponent`,
`renderer_zone_lifecycle.cpp`), the paperdoll's `CharacterModelView`
(T393/T419), and T411's alpha companion binding.

1. B482: the fix belongs in the lifecycle — the paperdoll (and every
   other render-to-texture user: read the list) is torn down or
   detached BEFORE the zone's textures are destroyed, and rebuilt after
   zone-in, rather than a null check at the driver. Pre-fix failing
   test: a headless (EDT_NULL or the gate's GLES2 device if the
   companion path needs it) zone unload with the paperdoll open does
   not dereference a destroyed texture (ASan/UBSan build of the test
   binary if the tree has the option; otherwise a lifecycle-order pin
   in the G05 shape). Then LIVE: both directions with `Show=1`, `dmesg`
   clean, capture.
2. B483: `scripts/scenario.sh` / `up.sh` — the akk-stack container
   auto-starts the spire launcher, so the non-launcher path starts a
   second world. Make `up.sh` detect the running fleet and not start a
   second one (or use `--launcher` everywhere); document in
   EXECUTOR_PROTOCOL.md. Prove: one `world` process after `up.sh`.
3. B488: `#zone airplane` (Plane of Sky, min level 46): the client takes
   the arrival position and never completes zone-in. Trace with the
   zone-in state machine logs (T340's loader census) which step never
   fires; fix if fable's (sky has no BSP — T440: HCMap path), board if
   the server's. This unblocks B453's `/dumpscene` proof.
4. If time remains under the lock: the T449 items not reached, in
   order — B461 lift ride (walk on), T443's `/who` guild + ESC, B356/
   B227 duel rig, B460 oracle ESC. Strike or reopen each with evidence.

## Proof

Standard (background builds, `ctest -j6`, pre/post, G05/G06, gate +
ARM with host load, file sizes, doc nets). Server values restored with
the world DOWN; `/camp` cleanly; lock released. Media `review/T457/`.
New items B490+.

STOP: an image golden moves; the crash cannot be reproduced headless
(then fix by lifecycle order, prove live only, and say so).

## Review (PAUSED)

Branch `task/T457-zone-change-segfault`, base **`d2f35875`**, slot3.
Session 2026-09-07 23:00 - 2026-09-08 01:00, stopped on the orchestrator's
pause order partway through item 1's LIVE half. Media and logs:
`/mnt/projects/fable/review/T457/`.

### 1. B482 -- the crash. Root-caused and FIXED; headless pin both ways.

**The mechanism is a second scene graph that nothing walks.**
`ConstrainedTextureCache::clear()` -- the zone-unload hook -- already calls
`clearTextureReferences()` before every `driver_->removeTexture()`, precisely so
that no surviving node keeps a raw `ITexture*` to a deleted texture
(`SMaterial::setTexture()` does not grab; `removeTexture()` drops the sole
owning reference). That is T119's fix and it is correct. It cannot help the
paperdoll, because `clearTextureReferences()`
(`src/client/graphics/constrained_texture_cache_eviction.cpp:388`) scans from
`smgr_->getRootSceneNode()` -- the MAIN scene manager -- and
`CharacterModelView::init()` builds **its own** scene manager
(`smgr->createNewSceneManager(false)`) and parents `characterNode_` under THAT
root. The scan is structurally unable to reach the doll's materials. Same class
as P10b / T119 / the boids' out-of-graph nodes, one layer further out: not a
node hidden from the graph, a node in a graph nobody walks.

Two further facts make it fire every time rather than sometimes:
`updatePaperdollForFrame()` calls `setCharacter()` only when the player's
appearance CHANGED, and a zone change does not change it -- so the stale model
is never rebuilt; and `update()` marks the doll dirty every
`animationInterval_` because the `o01` idle loop is playing, so
`renderToTexture()` is guaranteed to redraw the dangling materials within a
frame or two of the new zone.

**The fix is lifecycle order.** `ZoneLifecycleComponent::unloadZone()` now calls
`ctx_.Overlays().onZoneUnload()` BEFORE the two owners that destroy what the
doll names -- `rml->clearMeshCaches()` and `ctx_.constrainedTextureCache->clear()`.
`OverlaysComponent::onZoneUnload()` releases the model
(`CharacterModelView::releaseCharacterModel()`, which shares
`removeModelNodes()` with `updateCharacterModel()`) and resets
`paperdollHasAppearance_`, which is what makes `updatePaperdollForFrame()`
rebuild on the first frame after zone-in that has a player entity. Nothing is
null-checked at the driver; the dangling pointer is never created.

The view object, its scene manager and its render target are deliberately kept.
The RTT is a driver texture, not a cache entry, so the unload does not destroy
it; only the MODEL is zone-scoped.

**The "every other render-to-texture user" list is EMPTY.** Grepping
`addRenderTargetTexture` / `setRenderTarget` across `src/`, `include/`,
`tests/`, `tools/` and `benchmarks/`: the paperdoll is the only one in the
client. The other five hits are `tools/hwcaps/gles2_composite_audit.cpp`,
`tools/hwcaps/gles2_shader_diagnostic.cpp` and
`tools/hwcaps/zone_bench_lod.cpp`. `createNewSceneManager` has exactly one
caller tree-wide, the same one. So the class of defect has a population of one
and it is now handled.

**The headless pin**, `tests/test_paperdoll_zone_unload_order.cpp`, three cases,
the G05 idiom (a text parse -- `unloadZone()` needs a live device, a GL context,
a loaded zone and two worker threads, so the TU is the record of the order).
It asserts the release is strictly before both owners; that `onZoneUnload()`
both drops the model and arms the rebuild; and that the shared teardown does NOT
clear `hasAppearance_`, which would make every rebuild a silent no-op.

| run | result | log |
|---|---|---|
| PRE-fix | **3 of 3 FAILED** | `review/T457/logs/order_pin_PRE_FIX_red.log` |
| POST-fix | **3 of 3 PASSED** | `review/T457/logs/order_pin_POST_FIX_green.log` |

Both runs used the SAME binary and the SAME test text: the pin reads the
production TUs at RUNTIME, so the red was re-taken after the fix was written by
exporting the five production hunks to a patch, `git apply -R`, re-running, and
`git apply` to restore (`git stash` is forbidden -- it is cross-slot shared
state). That closes the usual hole where the "before" run used an earlier
version of the test.

**One deviation, flagged.** The third case first asserted that
`releaseCharacterModel()`'s own body named `characterNode_` and the two weapon
nodes. It went red POST-fix, because the implementation factors that teardown
into `removeModelNodes()` so `updateCharacterModel()` can share it. The test was
changed to follow the delegation (and gained the negative above). That is a test
adjusted to a better implementation shape, not a threshold nudged to green; the
pre-fix red was then re-taken with the final text, as described.

### 2. B483 -- up.sh's double world. FIXED, proven, and T449's PREMISE CORRECTED.

`up.sh` now ADOPTS whatever is already running instead of starting a second
copy (`server_process_running` / `server_process_count` in
`scripts/eqemu/env.sh`), checking loginserver, world and the zone fleet
independently. Proven both ways in one session on a container that did not
exist beforehand (`docker ps -a` was empty):

| run | what it did | worlds after |
|---|---|---|
| `up.sh qeynos2` on a COLD container (`logs/up_sh_cold_container.log`) | started loginserver, world, zone qeynos2 | **1** |
| `up.sh qeynos2` again, fleet up (`logs/up_sh_adopts_running_fleet.log`) | `loginserver already running -- adopting`, `world already running -- adopting`, `zone fleet already running (27 processes) -- adopting`, exit **0** | **1** (same pid, age 248 s) |

Pre-fix, the second run would have started a second `bin/world` and a second
`bin/loginserver`.

**T449's diagnosis of WHY is refuted on this config, and this matters.** T449
recorded that the container auto-starts the launcher "and its world" ~45 s after
a bare `compose up`. Measured here on a cold container: the launcher IS
auto-started (`/tmp/spire-launcher eqemu-server:launcher start`), but what it
brings up is the **zone fleet only** -- 27 `bin/zone` processes and **no world
and no loginserver**. Those zones attached to `up.sh`'s world. Timing was also
much later than 45 s: `up.sh`'s world was started 66 s after container start and
the fleet's zones did not appear until ~128 s (host load average ~32, a GLES2
build running). So the duplication T449 hit was almost certainly its OWN earlier
fleet still being up -- which its README says outright for the discarded
`zone_in` run -- rather than the container racing it. The adoption check is
still the right fix, because that is exactly the case it now handles; the
"container starts a second world" premise is not.

**A separate, pre-existing up.sh bug found while proving this, NOT fixed:**
the cold run **exited 1 on a zone that booted fine**. `up.sh` polls the zone log
for `Zone bootup type`, and this eqemu build prints `Bootup Booting [qeynos2]`;
the string has evidently never matched, so `up.sh` times out after 60 s and
returns failure on success. `scripts/scenario.sh` maps an `up.sh` failure to
operator code 65, so this can present as "the rig is broken" when the server is
up. Boarded as **B490** (see below). Left unfixed because the pause order
arrived; it is a one-line pattern change and should be done with a cold-container
re-run to prove it.

`refactor/EXECUTOR_PROTOCOL.md` gained the trap under the eqemu section.

### 3. What is NOT done

- **B482's LIVE A/B was not run.** The rig is staged and the binary it needs is
  built (`build-gles2` finished, exit 0), the server was up, and the lock was
  held -- the pause arrived first. This is the unit's main outstanding debt.
  Next step, concretely: copy
  `review/T449/data/UI_Testchar_WillEQ.inventory_open_crash.ini` (the file whose
  single differing byte is `[InventoryWindow] Show=1`) into the client's
  `<cache dir>/ui/`, launch with `review/T457/scripts/t457_fable_rig.sh`,
  `/say #set time 11 0`, capture, `#zone qeynos`, capture, `#zone qeynos2`,
  capture, then `dmesg | tail` for a `general protection fault` and `/camp`.
  T449's control run is the negative half and does not need repeating.
- **B488, the airplane zone-in, was not started.**
- **Item 4 (B461 lift, T443 `/who` guild + ESC, B356/B227 duel rig, B460 oracle
  ESC) was not started.**
- **The visual gate and the ARM build were not run.** `build-gles2` is built but
  `scripts/ci/visual_gate.sh` was never invoked, and
  `scripts/build-arm-noble.sh` was not run. Both are owed before merge, along
  with the merge-step golden runs for every TU this branch touches.

### 4. What IS verified

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest -j6` | **4694 of 4695**, then **4695 of 4695**. The single red was `check_build_tree_orphans` on `s3d_index_builder`, a binary T452 deleted and this slot inherited; deleted it and the test passed standalone. Full log `review/T457/logs/ctest_full.log`. Host load average at launch 5.7. |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| `check_file_sizes.sh fable` | 11 files checked, **11 ok, 0 warn, 0 fail** |
| visual gate | **NOT RUN** |
| `build-arm-noble.sh` | **NOT RUN** |

### 5. Server state

**No server value was changed and nothing needs restoring.** No client was ever
logged in, no SQL write was issued, no GM command was sent, no character was
moved and no quest fixture was installed. The only server operations were two
`up.sh` runs and `down.sh`. Testchar's row was not read or written by this unit,
so whatever pose it holds is the one T449 left (T311's warning applies to the
next unit: SELECT it, do not assume).

### 6. New board items

- **B490** (class A, process): `scripts/eqemu/up.sh` polls the zone log for
  `Zone bootup type`, which this eqemu build never prints (it prints
  `Bootup Booting [<zone>]`), so a cold `up.sh` times out after 60 s and exits 1
  on a zone that booted correctly. `scripts/scenario.sh` turns that into
  operator code 65. Evidence: `review/T457/logs/up_sh_cold_container.log`.

B490 is the only number this unit allocated; T455/T456 allocate from B476+ and
B479+, so there is no collision with them.
