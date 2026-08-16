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

(executor fills: whether detached object nodes are confirmed as the holder;
the full holder inventory table; the probe extension and its re-run acceptance
gate N of 10; the offending `SMaterial*` resolved to a holder, with entries
quoted; the fix and why it is narrow; **uninstrumented N of 10 clean**; the
post-fix ring check; deviations)

## Board

If the baseline goes clean, say so — and say explicitly that **the Pi is not
yet measured**, because that is the next unit and this arc has been burned
before by inferring one platform from another. If the baseline improves but
does not reach clean, report the rate and say what remains.
