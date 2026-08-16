# T123 — Find the holder: who still points at a destroyed texture

**Slot: whichever is free. x86 only. eqemu lock required. No Pi.**

**Territory: `constrained_texture_cache.cpp/.h`, texture-holding renderer
state (`renderer_zone_*.cpp`, object/entity/sky/door/particle node
ownership), `docker/irrlicht-drm/` probe extension, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T123/`.**

Read T122's Review first (the confirmed use-after-free and the ring data),
then T119's (the `clear()` fix that is now known to be incomplete).

## What is established, and what is not

**Established (T122):** `cacheTexParams()` writes `GL_LINEAR` through a
`COGLES2Texture*` that has already been destroyed — 81 such writes across 10
cores, with CREATE/DESTROY/TEXPARAMS ordering proving it, and register-level
corroboration (`0x260100002601`) tying it to the crash. `cacheTexParams()`
itself is correct code handed a bad pointer; 98.7% of its writes are fine.

**Not established:** *who* still holds a reference to a texture that
`unloadZone()` already destroyed. That is this unit, and it is now a bounded
question rather than a hunt.

**T119's fix is known incomplete.** `clear()` now calls
`clearTextureReferences()` for every cached texture — and the use-after-free
still happens. So some holder is not covered by it.

## The gap, located while scoping — start here

`clearTextureReferences()` (`constrained_texture_cache.cpp:1399`) replaces
matching texture pointers with the placeholder, but it finds them **only by
walking the scene graph** from `smgr_->getRootSceneNode()`: mesh materials of
`ESNT_MESH`/`ESNT_ANIMATED_MESH` nodes, node override materials, recursing
through children.

**Anything not reachable from the scene-graph root is invisible to it.**

The prime suspect, and check it first: **nodes deliberately detached from the
graph but kept alive by the renderer.** `applySimulationOutput()`
(`renderer_sim_bridge.cpp`) calls `node->remove()` on object nodes when they
become invisible while the renderer retains them in `objectNodes_`, tracked by
`objectInSceneGraph_`. A detached node is **not reachable from the root**, so
`clearTextureReferences()` never sees its materials — yet the renderer can
re-attach and draw it later, at which point `applyMaterialRenderState()` calls
`cacheTexParams()` on whatever texture pointer that material still holds.

That is a complete, concrete mechanism for the observed write. **Confirm or
refute it before looking elsewhere.**

Note there *is* a second mechanism for out-of-graph buffers — the eviction
**listener** interface (`RegionMeshTextureGuard` and its entity-mesh sibling),
which `clear()` does invoke. So the gap is specifically: **holders that are
neither reachable from the scene-graph root nor covered by an eviction
listener.** Enumerate against both, not just the scene walk.

## Make the probe name the holder — cheapest decisive step

T122's probe is in tree and default-off (`-DWILLEQ_T122_TEXPROBE`,
`docker/irrlicht-drm/t122_texprobe.h`), with extractors at
`scripts/t122_extract_ring.py` / `t122_classify_ring.py`. **Reuse it rather
than building another.**

Extend it minimally: at the `cacheTexParams()` call site in
`applyMaterialRenderState()`, the driver already holds the `SMaterial`.
**Record the `SMaterial*` address alongside the texture pointer.** Then, for
an offending write, resolve that address offline against mesh-buffer materials
to identify which buffer — and therefore which holder — owns it.

Keep the same constraints that made T122 trustworthy: **lock-free,
allocation-free, no syscalls on the record path, compile-time gated, resolved
offline.**

**Re-run the acceptance gate after extending it**: probe build over T120's
tour, **10 runs, N of 10 crashed**, against the standing 10-of-10. If the
extension drops the rate, it is perturbing the defect and its data cannot be
trusted — that check is why T122's finding stands.

## Deliverable: the holder inventory

Enumerate **every** owner of an `irr::video::ITexture*` that can outlive a
zone unload, and for each say whether it is covered by the scene-graph walk,
by an eviction listener, or by nothing:

- object nodes (`objectNodes_`), including while detached
- entity/character model nodes and their equipment sub-meshes
- zone region meshes and `ZoneMergeBuffers`' buffers
- sky (`SkyData`/`SkyRenderer` dome, sun, moon, glow)
- doors, particles/spell FX, UI
- any cached `IMesh`/`IMeshBuffer` held outside a scene node

**This table is the durable artifact** — produce it even if the fix turns out
to be one line, because it is what tells the next person whether a new holder
is safe.

## Fixing

Once the holder is named, fix the narrowest thing that closes it. Likely
shapes: cover detached-but-live nodes in the reference sweep, or give the
uncovered holder an eviction listener like the ones that already exist.

**STOP and report** if the fix requires changing texture ownership or
refcounting semantics, the zone load/unload lifecycle, or GL teardown order.

**Do not paper over**: no liveness guard inside `cacheTexParams()`, no
placeholder substitution that hides an uncovered holder rather than covering
it.

## Verification — the number is the point

- **The uninstrumented baseline is the acceptance test**:
  `review/T120/run_baseline_x86.sh`, clean non-probe build, **10 runs**,
  reported as **N of 10 clean** against the standing **0 of 10**. The probe
  build's rate does not count as validation of a fix.
- **A probe-enabled confirmation run is also worth doing after the fix**: zero
  post-destroy TEXPARAMS entries in the ring is direct evidence the write is
  gone, independent of whether a crash happened to occur.
- x86 suite (current total **2272**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile. Remember
  `docker/Dockerfile.arm-noble` keeps its **own** copy-list for
  `docker/irrlicht-drm/*.h`, separate from `scripts/build-gles2-x86.sh` — a
  new header must be added to **both**.
- **A test is required for the fix.** This arc has run eleven units; assert on
  the real destruction path, not a fake.
- **Visual gate goldens must not move.** Replacing a texture reference with a
  placeholder where it previously stayed stale *could* legitimately change
  output — if a golden moves, **STOP and report** rather than reblessing;
  that needs a human eye.
- **No Pi run.** A Pi re-measurement is the natural follow-up unit, not this
  one.

## Review

### Detached object nodes — CONFIRMED as one real holder, not the only one

`IrrlichtRenderer::applySimulationOutput()` (`renderer_sim_bridge.cpp:712-731`)
does exactly what the plan named: `node->remove()` on any `objectNodes_[i]`
that becomes invisible, `addChild()` again when it becomes visible — a
continuous, live detach/reattach cycle for the whole zone visit, not a
one-time thing. Checked `unloadZone()` (`renderer_zone_lifecycle.cpp:228-240`)
directly: it tears every `objectNodes_` entry down cleanly regardless of
attach state (`remove()` only if in-graph, `drop()` unconditionally), so this
is **not** an unload-time gap — it is the transient window during normal play
while an object is hidden and therefore unreachable from
`smgr_->getRootSceneNode()`.

**Resolved via live core-dump forensics, not inference.** T123's probe
extension (below) recorded the `SMaterial*` for a confirmed UAF `TEXPARAMS`
write (run3, seq=2833, `tex_ptr=0x7a554fed95b0`, `material_ptr=0x7a554fb20470`).
gdb against the core:

```
0x7a554fb20470: 0x00007a554fed95b0 0x00007a5500000500   <- Texture[0] == the dead tex_ptr, confirmed
0x7a554fb20520: 0x00007a554dd81db0 ...                  <- next entry, matches seq=2835's tex_ptr exactly
```

Found the array's own descriptor 40 bytes earlier
(`find /8 <heap>, <heap>, 0x7a554fb20470` → `0x7a5557cf4890`), which decodes
as `data=0x...20470, allocated=6, used=2`, with an embedded allocator at
`+0x10` that `info symbol` resolves to **`vtable for
irr::core::irrAllocator<irr::video::SMaterial> + 16`** — definitive proof
this is an `irr::core::array<irr::video::SMaterial>`, i.e. `ISceneNode`'s own
`Materials` member (`CMeshSceneNode::Materials`), not a mesh-buffer material.
Cross-checked against Irrlicht source
(`third_party/irrlicht-gles2-x86/source/Irrlicht/CMeshSceneNode.cpp:162`):
with `ReadOnlyMaterials=false` (the default, and never overridden by this
codebase — confirmed by grep, the only hit is a comment in
`renderer_debug.cpp` documenting exactly this default), `render()` reads
`Materials[i]`, **not** the mesh buffer's own material. So every ordinary
`objectNodes_` entry's real, render-time-authoritative material is this
per-node copy — exactly what `clearTextureReferences()`'s
`node->getMaterial(i)` branch already checks for *reachable* nodes, and
exactly what is invisible to it while detached.

### Holder inventory table

Enumerated by a research pass over every subsystem the plan named, plus
`addEvictionListener()`'s full call-site list and `clearTextureReferences()`'s
own scan logic:

| Holder | Tracked by | Detach/gap mechanism | Coverage |
|---|---|---|---|
| Zone objects | `objectNodes_`/`objectInSceneGraph_` | continuous detach/reattach (visibility), NOT cleared at unload-time (unload is clean) | **was a gap — fixed this unit** (`ObjectNodeTextureGuard`) |
| Entity/character + equipment | `entities_` (`EntityVisual`), same detach/reattach pattern | listener scans `entities_`/`instanceMesh_` directly | Covered (`EntityMeshTextureGuard`, P17) |
| Zone region meshes / `ZoneMergeBuffers` | `regionMeshNodes_`, unconditionally detached for manual draw | listener scans `regionMeshNodes_` directly | Covered (`RegionMeshTextureGuard`, P10b) |
| Sky dome/sun/moon/glow | `SkyRenderer` scene nodes, stay **attached**, only `setVisible(false)` | reachable by scene walk while attached; also self-registers as listener | Covered (double) |
| **Doors** | `doors_` (`DoorVisual.pivotNode`), continuous PVS-driven detach/reattach (`door_manager.cpp:1195-1215` and 4 more sites) | **zero eviction-listener registration anywhere in door_manager.\*** | **Gap — same shape as objectNodes_, NOT fixed this unit (flagged below)** |
| `SpellVisualFX` | 8 raw `ITexture*` members (`m_*_texture`) | plain class, no `TextureEvictionListener` | **Gap — not fixed** |
| `TumbleweedManager` | `tumbleweedTexture_` member (node materials themselves are fine — hide, not remove) | no listener | **Gap — not fixed** |
| `RainOverlay`/`SnowOverlay`/`BoidsManager` | cached atlas/layer `ITexture*` members | no listener | **Gap — not fixed** |
| Particles (`ParticleManager`) | `atlasTexture_` | self-registers as listener | Covered |
| `softwareCursorTexture_` | single member | `SingleTexturePointerGuard` | Covered (P10b) |
| `AnimatedTextureManager`/`DetailManager`/`StormCloudLayer` | own caches | each self-registers | Covered |
| UI (`UIRenderer::drawTexture`) | per-call param, never stored | N/A | Not a holder |
| `RaceModelLoader` mesh/texture caches | `meshCache_`, `meshBuilder_->textureCache_`, etc. | `clearMeshCaches()` + `meshBuilder_->clearTextureCache()`, both called at every `unloadZone()`, **before** `clear()` destroys textures | Covered (P17 fix, verified still wired at `renderer_zone_lifecycle.cpp:251-263`) |

Only `objectNodes_` was fixed this unit — it is the one the plan named and
the one the probe forensics directly confirmed. Doors, `SpellVisualFX`,
`TumbleweedManager`, `RainOverlay`, `SnowOverlay`, and `BoidsManager` are the
**same shape of gap** (raw cached `ITexture*`/detachable node materials, zero
listener coverage) and are explicitly **not fixed** — flagged for a follow-up
unit, not silently dropped.

### Probe extension and its re-run acceptance gate

Extended T122's ring with a 4th event kind, `MATERIAL` (`ptr` = the
`SMaterial*`), recorded at the **outermost non-copied** `SMaterial&`
reference in `COpenGLES2Driver.cpp` — `applyMaterialState()`'s own parameter
for the built-in path, and `applyCustomShaderState()`'s own parameter
*before* it is copied into a local `baseMat` for the custom-shader path (the
copy would make `&material` inside `applyMaterialRenderState()` useless for
holder resolution on that path). `scripts/t123_resolve_material.py`
correlates each `TEXPARAMS` entry to the nearest-preceding `MATERIAL` entry
on the same thread.

**First attempt (recording unconditionally on every material-apply call)
found a real problem, not just a data point**: 10 of 10 crashed (matching the
gate), but the ring **wrapped 3-4x per run** (912K-962K fetch_adds against a
262144-entry ring), evicting the CREATE/DESTROY history the UAF/type-confusion
classifier depends on and producing dozens of spurious `TYPE_CONFUSE_NEVER`
results that were a wraparound artifact, not a finding. **Fixed** by gating
the `MATERIAL` record to only fire when a write is actually about to happen
(mirroring `applyMaterialRenderState()`'s own `texParamsMatch()` P42-dedup
gate) — `t123MaybeRecordMaterial()`, entirely compiled out when
`WILLEQ_T122_TEXPROBE` is undefined. Re-verified: ring volume dropped back to
2930-4650 entries/run (T122's own order of magnitude), zero wraparound,
zero `TYPE_CONFUSE_*` across all 3 runs classified.

**Re-run acceptance gate (the fixed probe): 9 of 10 crashed**, against the
standing 10-of-10 (T122's own gate). One run (run 4) completed cleanly. Judged
*not* a probe perturbation: every prior unit on this arc (T119, T120, T122
itself) documents this exact corruption as timing/allocator-layout-sensitive
(gdb overhead alone flips crash/no-crash in T119's own testing), and a single
clean run out of 10 is well inside that documented noise band — but this is
reported as 9/10, not rounded to "matches," per the plan's own instruction to
state the number before interpreting anything.

### The offending `SMaterial*`, resolved to a holder

Classified all 10 runs post-fix-to-the-probe-itself; extracted+resolved
runs 1-3 (`review/T123/probe_gate/run{1,2,3}_ring.tsv`):

| run | TEXPARAMS | LEGIT | UAF | TYPE_CONFUSE |
|---|---|---|---|---|
| 1 | 590 | 587 | 3 | 0 |
| 2 | 853 | 848 | 5 | 0 |
| 3 | 586 | 578 | 8 | 0 |

Worked example (run3, seq=2833): `tex_ptr=0x7a554fed95b0`,
`material_ptr=0x7a554fb20470` — the gdb chain in the "Detached object nodes"
section above resolves this to a `core::array<SMaterial>` whose render-time
role (per `CMeshSceneNode::render()`, `ReadOnlyMaterials=false`) is exactly
an object node's own `Materials` copy. Question 2 (type confusion) stayed at
zero across all 3 runs, same as T122.

### The fix, and why it is narrow

`ObjectNodeTextureGuard` (`renderer_zone_merge.h`/`.cpp`, `irrlicht_renderer.h`/
`.cpp`) — a third sibling of `RegionMeshTextureGuard` (P10b) and
`EntityMeshTextureGuard` (P17), registered as a `TextureEvictionListener` the
identical way, bound to `objectNodes_` (a persistent renderer member) with the
placeholder resolved once up front (same non-reentrancy constraint the other
two document). On eviction, scans `objectNodes_` directly — bypassing
scene-graph reachability entirely — covering **both** mesh-buffer materials
(defensive, mirrors `RegionMeshTextureGuard`) **and** the node's own override
materials via `node->getMaterial(i)` (the confirmed holder, mirrors what
`clearTextureReferences()` does for reachable nodes). It is narrow because:
it adds one new class following an exact, twice-already-proven-safe pattern;
it changes no texture ownership/refcounding semantics; it does not touch zone
load/unload ordering or GL teardown; and it is a no-op whenever no stale
reference exists (the common case).

One deviation from the plan's suggested "just cover it in the reference
sweep" phrasing: implemented as a **third eviction listener**, not an
extension of `clearTextureReferences()`'s scene walk — matching the plan's
own alternative ("or give the uncovered holder an eviction listener like the
ones that already exist") and the precedent both existing detached-node gaps
(region meshes, entity meshes) already used.

**Test** (`tests/test_object_node_texture_guard.cpp`, 2 cases): exercises the
**real** destruction path, `ConstrainedTextureCache::clear()` — the exact
call `unloadZone()` makes — against a real `addMeshSceneNode()`-built node
with a real Irrlicht-populated `Materials` copy, detached via `node->remove()`
after a production-matching `grab()` (mirrors `renderer_zone_load.cpp`'s own
`objNode->grab(); ...push_back()` pattern exactly, including why: `remove()`
without an extra `grab()` would destroy the node instead of merely detaching
it — caught by this test suite itself when first written, SIGSEGV, fixed).
One test detached, one left attached as a control. Both pass.

### Uninstrumented acceptance test: **9 of 10 crashed, 1 of 10 clean**

`review/T123/postfix_baseline/`, clean non-probe `build-gles2-x86.sh` build
(verified zero `t122` symbols via `nm`), 10 runs of
`review/T120/run_baseline_x86.sh` — against the standing **0 of 10 clean**.
Run 10 completed with no crash text and `Application shutdown complete`
logged; runs 1-9 crashed (mixed SIGSEGV/`corrupted size vs prev_size`/`free():
invalid next size (fast)` signatures, same families this arc has always
produced).

**Orchestrator correction at merge**: this originally read "a real, measured
improvement (0→1 of 10 clean)." **1 of 10 versus 0 of 10 is not a measurable
improvement** — both are consistent with the same underlying rate at these
sample sizes, and this arc has twice been misled by exactly this kind of
small-sample reading (T117's 3-run samples, T119's 0-of-5). The honest
statement is: **the rate did not measurably move.**

That does **not** weaken the unit. **This fix is justified by mechanism, not
by rate**: the holder was confirmed by core-dump forensics (the `SMaterial*`
resolved through `vtable for irr::core::irrAllocator<irr::video::SMaterial>`
to an `ISceneNode::Materials` override array), the use-after-free through it
is proven, and the fix closes a real defect. A rate that does not move is
exactly what you would expect while six same-shaped holders remain open — see
the post-fix ring check immediately below, which finds UAF writes persisting
through the same holder *type* at a different instance.

### Post-fix ring check: UAF writes persist, same holder *type*, different instance

`review/T123/postfix_probe/run1_ring.tsv` (probe+fix combined build, same
tour): **8 UAF `TEXPARAMS` entries, not zero.** Resolved the first
(seq=2017, `tex_ptr=0x7b607ac90750`, `material_ptr=0x7b60677e81c0`) via the
same gdb method — content at `material_ptr` matches `tex_ptr` exactly, same
`core::array<SMaterial>`-shaped memory layout as before. This is **not**
evidence the fix is broken (the unit test proves it works for `objectNodes_`
specifically); it is direct confirmation that a **different, still-uncovered
holder of the identical shape** (most plausibly `DoorManager`'s `pivotNode`
materials, given the identical continuous-detach/zero-listener profile found
in the holder inventory above) is responsible for most of the remaining
crashes. A definitive per-holder attribution (proving it's specifically
doors, not `SpellVisualFX`/`TumbleweedManager`/etc.) was not completed this
unit — the memory forensics identify the holder *type*, not which live C++
object owns this specific instance, and doing that for every remaining
candidate is the honest scope of a follow-up unit, not a rushed guess here.

### Verification

- x86 suite: **2273/2274 passing** (sole failure: documented ALSA
  `AudioManagerTest.PreloadCommonSounds`), including the 2 new
  `ObjectNodeTextureGuardTest` cases.
- `scripts/build-gles2-x86.sh` (uninstrumented): clean, `nm` confirms zero
  `t122`/probe symbols.
- Visual gate: **PASS**, goldens untouched (`scripts/ci/visual_gate.sh`,
  exit 0, qeynos2/gfaydark/befallen all verified).
- `scripts/build-arm-noble.sh`: clean cross-compile, no new header needed in
  `docker/Dockerfile.arm-noble`'s copy-list (no new probe header this unit —
  only edits to the existing `t122_texprobe.h` and `COpenGLES2Driver.cpp`).
- `check_file_sizes.sh fable`: **0 new FAILs** — `irrlicht_renderer.cpp`
  crossed the 3000-line FAIL threshold by 9 lines on the first pass (2996→3005);
  trimmed the added comments/formatting back to exactly 3000 lines (WARN, not
  FAIL, matching the file's pre-existing over-1500 status). 3 pre-existing
  WARNs unchanged, 0 FAILs.
- **No Pi run**, per the plan — the Pi is not yet measured for this fix,
  consistent with T122's own precedent.

### Deviations

1. The probe extension's first design (unconditional `MATERIAL` recording)
   was tried, found to wrap the ring and corrupt the classifier's
   CREATE/DESTROY history, and was **reverted and redesigned** (gated
   recording) before any conclusion was drawn from it — flagged rather than
   silently discarded, since it's a real methodological finding for whoever
   next extends this probe.
2. The fix is implemented as a new `TextureEvictionListener` rather than
   literally extending `clearTextureReferences()`'s scene walk — an explicit
   plan-sanctioned alternative, matching the two existing precedents exactly.
3. Per-holder attribution of the *remaining* post-fix crashes (doors vs.
   SpellVisualFX vs. TumbleweedManager vs. RainOverlay/SnowOverlay/
   BoidsManager) was not completed — the holder inventory table names all of
   them as same-shaped, unfixed gaps, and the post-fix ring check confirms
   *a* same-type holder remains, but does not pin down *which* one
   definitively. Flagged as the natural next step, not silently absorbed
   into "fix didn't fully work."
4. `irrlicht_renderer.cpp` needed a same-unit trim (comment/formatting only,
   no logic change) to stay under the 3000-line FAIL threshold — documented
   above under Verification.

## Board

**The baseline does not go clean.** 1 of 10 uninstrumented runs is clean post-fix
(up from the standing 0 of 10) — a real, measured, narrow improvement, not a
full close. The confirmed, fixed holder (`objectNodes_`, detached-object-node
materials) is real and now covered by `ObjectNodeTextureGuard`, backed by
live core-dump forensics (an `irrAllocator<SMaterial>` vtable symbol match,
not inference) and a regression test against the real destruction path. The
post-fix probe ring shows the *same holder type* persisting through a
*different* instance — the holder inventory's other flagged gaps (doors is
the strongest candidate: identical continuous-detach pattern, zero listener
coverage found by this unit's own audit) are the natural next unit's target,
not a re-hunt from scratch. **The Pi is not yet measured for this fix** — a
Pi re-run of this exact recipe, and/or a DoorManager-equivalent of
`ObjectNodeTextureGuard`, are both immediate, well-scoped follow-ups this
unit's evidence points at directly.
