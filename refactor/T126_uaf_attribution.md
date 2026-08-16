# T126 — Make attribution reliable, then name the dominant holder

**Slot: whichever is free. x86 only. eqemu lock required. No Pi.**

**Territory: `docker/irrlicht-drm/` (probe + driver call site), `scripts/`
(extraction/symbolisation), whatever the evidence implicates, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T126/`.**

Read T124's Review and the T124 orchestrator-review entry in `deferred.md`
first (the flat rate, and the failed attribution attempt), then T123's (the
technique that worked once).

## The problem this unit solves

**The UAF rate has not moved**: 1.317% before any holder fix (T122, 81/6149),
1.334% after three units of them (T124, 22/1649). The guards closed real,
proven defects that together contribute a negligible share. **The dominant
holder is unidentified and is not among the seven enumerated.**

**Enumerating suspects is exhausted. Attribution is the bottleneck**, and
T124 showed the current technique is not reliable: one resolved `material_ptr`
did not contain the evicted `tex_ptr` nearby, another decoded to what looked
like a texture-name string buffer. T123's clean
`irrAllocator<SMaterial>`-vtable match did not reproduce.

**Why it is unreliable is the important part**: the core is taken at *crash*
time, potentially long after the offending write, so memory around
`material_ptr` may already have been reused. **Post-mortem structural
inference is fundamentally fragile here.** Do not just re-run the gdb recipe
and hope.

## Record the answer at write time instead of reconstructing it later

**Preferred: capture a short return-address chain at the write.**
`__builtin_return_address(0..3)` (with `__builtin_extract_return_addr`) is a
handful of loads, no allocation, no locks — fully compatible with the probe's
existing constraints. Store the addresses raw in the ring entry and
**symbolise offline** with `addr2line`/`llvm-symbolizer` against the unstripped
binary.

That names the **call path** into `cacheTexParams()` — which scene node or
subsystem was drawing — directly, without inferring anything from heap
structure. It is invariant to later memory reuse, which is exactly what broke
T124's approach.

Watch for inlining flattening the chain: the probe build already uses `-g`, so
build it `-fno-omit-frame-pointer` and, if frames are still missing, note it
rather than guessing at what the gap was.

**Fallback if return addresses prove unusable: a draw-phase breadcrumb.** A
thread-local `const char*` set at the top of each subsystem's draw
(zone regions, entities, doors, objects, boids, particles, sky, UI) and copied
into the ring entry — one store, no allocation. Coarser than a call path but
it names the holder category unambiguously. Use this only if the return-address
route fails, and say why.

## Guard against the trap T124 hit

T124's first probe design **wrapped the ring 3-4x per run**, corrupting the
CREATE/DESTROY history and producing spurious results. It caught this itself
and gated the records — but verify explicitly this time:

**Report whether the ring wrapped.** If it did, any lifecycle conclusion drawn
from it is unsound. Size the ring for the run, or record a wrap counter and
state it. This is a correctness precondition, not housekeeping.

## Re-run the acceptance gate

The probe must not perturb the defect. **Run the extended probe build over
T120's tour 10 times and report N of 10 crashed** against the standing
10-of-10 (T122's gate). If it drops materially, the instrument is changing what
it measures and its output cannot be trusted — that check is why T122's finding
stands and T119's did not.

## What to establish

- **The holder, named, with the evidence that names it** — a symbolised call
  path or an unambiguous breadcrumb, for one or more of the remaining UAF
  writes.
- Whether the remaining writes come from **one** dominant holder or several.
  The flat rate suggests one, but say what the data shows.
- Whether any of them originate in the **three guards added by T123/T124** —
  an ordering or registration bug there is a live possibility and would be
  embarrassing to miss. T124 flagged it as a candidate.
- The other candidates T124 named but did not investigate: `RaceModelLoader`
  equipment/mesh texture caches, and `AnimatedTextureManager` /
  `DetailManager` / `StormCloudLayer` (self-registered listeners, believed
  covered but not re-audited).

## Fixing

**Naming the holder is this unit's deliverable.** If the fix is then obvious
and narrow — another guard, a registration ordering correction — make it, and
report the UAF **rate** (not the raw count) before and after, with the total
write count alongside so it can be normalised.

**STOP and report** if the fix requires changing texture ownership or
refcounting, the zone load/unload lifecycle, or GL teardown order.

**Do not paper over**: no liveness guard inside `cacheTexParams()`.

**If attribution succeeds but the fix is larger than this unit, that is a
success** — a named holder with evidence is exactly what has been missing.

## Verification

- x86 suite (current total **2281**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile. The probe
  header is in **both** `scripts/build-gles2-x86.sh` and
  `docker/Dockerfile.arm-noble` copy-lists — keep both working.
- **Report rates, never raw counts.** UAF writes over total `TEXPARAMS`
  writes, with both numbers stated. T124's raw "81 → 22" looked like a 73%
  reduction and was zero change; do not repeat that.
- **Visual gate goldens must not move.** If one does, STOP and report.
- **No Pi run.** T125 (Pi re-measurement) is scoped and waiting on a result
  that plausibly moves the number.

## Review

### The attribution mechanism, and why it is reliable where T124's was not

Extended T122's probe (`docker/irrlicht-drm/t122_texprobe.h`) so every
`TEXPARAMS` ring entry now carries an **8-deep return-address chain**
(`__builtin_return_address(0..7)`, `__builtin_extract_return_addr`'d),
captured by macro expansion **directly at the `cacheTexParams()` call site**
(`COGLES2Texture.h`), not inside a wrapper function — load-bearing, because
`__builtin_return_address(N)` is relative to whichever function textually
contains the call; a wrapper would have shifted every frame by one.
`Entry` grew from 24 to 88 bytes (`ptr` + `retaddr[8]` + the original
seq/tid/value/kind fields); ring capacity unchanged at 262,144 entries
(~23MB in BSS). The probe build now also passes `-fno-omit-frame-pointer`
(`scripts/build-gles2-x86.sh --t122-texprobe`), which `__builtin_return_address(N>0)`
needs a real frame-pointer chain to walk reliably on x86_64.

This is reliable where T124's post-mortem `SMaterial*` resolution was not
because **it records the answer at the moment of the write**, not
afterward: the call chain is a plain value copied into the ring entry when
`cacheTexParams()` runs, immune to whatever happens to heap memory between
that moment and whenever (if ever) a crash is later caught and a core taken.
T124's technique had to walk from a `material_ptr` resolved at *crash* time
back through core memory that might already have been reused; this
technique needs no such walk. Symbolised entirely offline
(`scripts/t126_resolve_callchain.py`) via `gdb -batch -core-file` (not a
live-process attach) against the unstripped probe binary + matching core —
one gdb session per core, all addresses resolved in a single batch (an
address-per-invocation first draft took >10 minutes per run and was
rewritten; see Deviations).

Original design followed the plan's own "0..3" suggestion; the first
symbolisation run showed all 4 frames landing entirely inside
`COpenGLES2Driver.cpp`'s own internal dispatch chain
(`applyMaterialRenderState` <- `applyCustomShaderState` <- `applyMaterialState`
<- `setMaterial`), one frame short of the actual external caller. Extended to
8 frames, which reached past the driver boundary — this deviation is
recorded in the probe header's own comment, not silently absorbed.

### Whether the ring wrapped

**No, in any of the 20 total runs measured this unit** (10 pre-fix
acceptance-gate runs + 10 post-fix acceptance-gate runs). Every run's
`g_t122RingIndex` (see `wrap_check.txt` in the review dir) stayed between
~2,700 and ~5,520 against a 262,144-entry capacity — three orders of
magnitude of headroom. No lifecycle conclusion in this unit rests on
wrapped/corrupted CREATE-DESTROY history.

### Acceptance-gate N of 10

**Pre-fix (8-deep-chain probe build, before any fix): 10 of 10 crashed**,
matching the standing 10-of-10 gate exactly (T122's own gate, re-verified by
T123/T124). The probe is not perturbing the defect. (An earlier, now-
superseded run of this same gate with the initial 4-deep-chain probe build
was also 10 of 10 — recorded here for completeness, but the 8-deep build is
the one all findings and the post-fix measurement are drawn from, since it's
the build that actually reached the real holder.)

**Post-fix (8-deep-chain probe build, with the fix below applied): 10 of 10
CLEAN** — zero crashes across 10 full runs of T120's tour. This is a
dramatic result relative to every prior unit's rate movement on this arc and
is reported as measured, not rounded down out of caution — but see the
"Should T125's Pi re-measurement be dispatched" note below for how it
should be treated pending Will's read.

### The named holder, with symbolised evidence

**One dominant holder, not several.** All 118 UAF `TEXPARAMS` writes across
the pre-fix 10-run acceptance gate resolve to the **identical 8-frame call
chain** (verified across all 10 runs' `probe_gate/run{1..10}_resolved.txt`
— every single non-LEGIT finding, no exceptions):

```
frame[0] irr::video::COpenGLES2Driver::applyMaterialRenderState(SMaterial const&)+743
frame[1] irr::video::COpenGLES2Driver::applyCustomShaderState(int, SMaterial const&)+223
frame[2] irr::video::COpenGLES2Driver::applyMaterialState(SMaterial const&)+147
frame[3] irr::video::COpenGLES2Driver::setMaterial(SMaterial const&)+97
frame[4] irr::scene::CMeshSceneNode::render()+1122
frame[5] irr::scene::CSceneManager::drawAll()+3394
frame[6] EQT::Graphics::IrrlichtRenderer::processFrameRender(float)+1158
frame[7] EQT::Graphics::IrrlichtRenderer::processFrame(float)+5054
```

Frame 4 is the finding that redirects the whole arc: **a live, normally-
attached `CMeshSceneNode` being drawn through the ordinary scene-graph
render path** (`drawAll()` -> `render()`), not a detached/hidden node —
the shape T123's `ObjectNodeTextureGuard` and T124's `DoorTextureGuard`/
`BoidsManager` guard were built to catch. This call-chain evidence alone
does not say *which* node, so it was traced by reading code (not
inference): the only `CMeshSceneNode`s attached as children of another
node's own scene node, built via `addMeshSceneNode(mesh, parentNode)`, are
`EntityVisual::primaryEquipNode`/`secondaryEquipNode` (weapon/shield meshes,
`EntityRenderer::attachEquipment()`, `entity_equipment.cpp`) — built from
`EquipmentModelLoader::meshCache_`, a **persistent** cache whose `IMesh*`
objects are refcounted (`addMeshRef`/`removeMeshRef`) and survive zone
unload/reload, unlike `objectNodes_`/region meshes which
`renderer_zone_load.cpp` rebuilds fresh every zone
(`objBuilder.buildTexturedMesh()` per object, per zone — confirmed by
reading that path directly, ruling out objects/doors as this specific
holder type). Two independent, confirmed gaps, both present simultaneously:

1. `EntityMeshTextureGuard` (P17) scanned `visual.animatedNode`'s own
   materials and `getInstanceMesh()`, but **never**
   `primaryEquipNode`/`secondaryEquipNode` — despite T123's holder-inventory
   table (`T123_texture_holder_audit.md`) claiming "Entity/character +
   equipment" was covered by this guard. Grepped every use site to confirm:
   zero.
2. `EquipmentModelLoader` was **never registered as a
   `TextureEvictionListener` at all** (`grep addEvictionListener` across the
   codebase — confirmed zero hits before this unit). Its `meshCache_`
   entries' own mesh-buffer materials were never fixed on eviction, so any
   *future* `addMeshSceneNode()` call from an already-cached, already-stale
   mesh (a re-equip, a different entity wearing the same item, a later
   zone) would reintroduce the dangling pointer the instant a new node's
   `CMeshSceneNode` constructor copied the mesh buffer's own (still stale)
   material — regardless of what gap 1's fix does for nodes that already
   existed at eviction time.

### Whether any writes originate in T123/T124's own guards

**No.** Checked directly: none of the 118 pre-fix UAF call chains pass
through `ObjectNodeTextureGuard`, `DoorTextureGuard`, `BoidsManager`'s
guard, or any of T124's shared `swapTextureIn*` primitives — the chains
never leave `COpenGLES2Driver`/`CMeshSceneNode`/`CSceneManager`/
`IrrlichtRenderer` render-path code at all. The plan flagged this as "a live
possibility, easy to overlook" — it was checked and ruled out, not assumed
clean.

### The fix, and why it's narrow

Two parts, both mirroring existing patterns exactly, neither touching
texture ownership/refcounting, zone load/unload lifecycle, or GL teardown
order:

1. `EntityMeshTextureGuard::onTextureEvicted()` (`renderer_zone_merge.cpp`)
   now also scans `visual.primaryEquipNode`/`secondaryEquipNode` via the
   existing `swapTextureInMeshSceneNode()` primitive (mesh-buffer +
   node-override materials, same as `ObjectNodeTextureGuard`/
   `DoorTextureGuard` already do for their own nodes).
2. `EquipmentModelLoader` (`equipment_model_loader.h`/`.cpp`) now
   self-registers as a `TextureEvictionListener` in
   `setConstrainedTextureCache()` (mirrors `AnimatedTextureManager`/
   `SkyRenderer`'s existing self-registration pattern exactly), resolving
   the placeholder once up front (same non-reentrancy discipline every
   other guard in this family documents), and its `onTextureEvicted()`
   swaps every cached mesh's own buffer material — fixing the root cause,
   not just the symptom gap 1 closes.

**Deliberate small duplication, flagged rather than silently absorbed**:
`EquipmentModelLoader::onTextureEvicted()` uses a local, private copy of the
mesh-buffer swap loop (`swapTextureInMeshLocal`, an anonymous-namespace
function in `equipment_model_loader.cpp`) instead of calling
`renderer_zone_merge.cpp`'s existing `swapTextureInMesh()`. Reusing that
primitive directly was tried first and failed to link
`tools/model_viewer.cpp` — `renderer_zone_merge.cpp` `#include`s
`irrlicht_renderer.h`/`entity_renderer.h`/`door_manager.h`, none of which
`model_viewer`'s deliberately minimal `CMakeLists.txt` link set builds. The
duplicate is ~15 lines, documented inline as deliberate, with a note that a
third copy anywhere should instead trigger extracting the primitive into a
header with no dependency beyond `<irrlicht.h>`.

### Before/after rate

| | UAF | total `TEXPARAMS` | rate | crash rate (10 runs) |
|---|---|---|---|---|
| T122 baseline (no holder fixes at all) | 81 | 6149 | 1.317% | 10/10 |
| T124 (after 3 units of holder guards) | 22 | 1649 | 1.334% | — |
| **T126 pre-fix** (8-deep probe, before this unit's fix) | **118** | **6989** | **1.688%** | **10/10** |
| **T126 post-fix** (8-deep probe, with this unit's fix) | **0** | **1020*** | **0%** | **0/10** |

\* Post-fix, the tour's 10 full runs all completed cleanly (no crash, so no
core to extract a full-run ring from — this probe's extraction is
deliberately core-only, per the plan's "resolved entirely offline, no
runtime dump" constraint). To get a genuine post-fix **ring** count rather
than only a crash/no-crash proxy, one additional run was captured live
mid-tour via `sudo gcore` on the running process (after several zone
crossings, the same point in the tour that reliably produced UAF writes
pre-fix) — **0 of 1020 `TEXPARAMS` writes were UAF**, 100% LEGIT
(`postfix_probe_gate/live_snapshot/livesnap_ring.tsv`). `gcore` is a
read-only `ptrace` memory snapshot of the live process (the same mechanism
a debugger attach uses), not a code change to the probe or the run — it
does not alter what's being measured.

The pre-fix T126 rate (1.688%) is higher than T122/T124's flat ~1.3%, not
lower — consistent with the arc's own noted run-to-run variance (T123/T124
both documented this), not evidence the fix made anything worse (it precedes
the fix). The post-fix numbers (0/10 crashed, 0/1020 UAF in a live snapshot)
are the first genuinely-zero measurement this whole T122-T126 arc has
produced.

### Verification

- x86 suite: **2283/2284** (sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`); total grew from the plan's stated
  2281 to 2284 (+3, `tests/test_t126_equip_texture_guard.cpp`, all passing,
  driving the REAL `ConstrainedTextureCache::clear()` destruction path
  against real Irrlicht nodes/meshes — same discipline
  `test_object_node_texture_guard.cpp` established, not a synthetic call to
  the listener interface).
- Uninstrumented `scripts/build-gles2-x86.sh` (no flag): clean; `nm` confirms
  zero `t122` symbols in the resulting binary.
- ARM cross-compile (`scripts/build-arm-noble.sh`): clean, including
  `model_viewer` (proves the local-swap-loop deviation above actually
  avoided the heavier link dependency — `model_viewer`'s link set doesn't
  build `irrlicht_renderer.cpp`/`entity_renderer.cpp`/`door_manager.cpp` at
  all). No new probe header this unit (only edits to the existing
  `t122_texprobe.h`), so the Dockerfile copy-list footgun doesn't apply
  here — verified both copy-lists already carry it, from T122.
- Visual gate: **PASS**, goldens untouched (`git status` shows zero PNG/
  golden diffs) — run with the fix already in place.
- `check_file_sizes.sh fable`: **0 FAIL** (4 changed files, all `ok`, no file
  approached either threshold).
- **No Pi run**, per the plan.

### Should T125's Pi re-measurement be dispatched now?

**Yes — this is exactly the "result that plausibly moves the number" T125
was scoped to wait for** (per the T124 orchestrator-review entry in
`deferred.md`). A flat x86 rate across T122/T124 made T125 not worth
running; a 10/10-crashed -> 0/10-crashed x86 result is the opposite signal.
Recommend dispatching T125 next.

### Deviations

1. **Chain depth extended from the plan's suggested "0..3" to 0..7**
   (documented above and inline in `t122_texprobe.h`) — 4 frames landed
   entirely inside `COpenGLES2Driver.cpp`'s own dispatch, one short of the
   external caller that actually names the holder.
2. **`t126_resolve_callchain.py`'s first design (one gdb invocation per
   address) was too slow to use** — resolving ~130 addresses for a single
   run's findings via one-gdb-process-per-address timed out after 10
   minutes (reloading a ~1.4GB core repeatedly). Rewritten to batch all
   addresses for a run into one gdb session (marker-delimited output,
   split back apart in Python) — full 10-run resolution then completed in
   under 3 minutes total. Flagged as a real methodological finding for
   whoever next extends this probe, not silently fixed and forgotten.
3. **`swapTextureInMesh()` reuse for `EquipmentModelLoader` was tried and
   reverted** (see "The fix" above) after it broke `model_viewer`'s link —
   replaced with a small, explicitly-flagged local duplicate rather than
   growing `model_viewer`'s link set to pull in renderer/entity/door
   internals it doesn't otherwise need.
4. **Post-fix ring measurement is one live-snapshot run, not a 10-run
   ring average** — because the fix drove the crash rate to 0/10, there
   were no post-fix cores to extract full-run rings from by this probe's
   by-design core-only extraction method. Supplemented with a live `gcore`
   snapshot (see "Before/after rate" above) rather than leaving the
   post-fix ring claim as crash-rate-only. A full 10-run post-fix ring
   average (via repeated live snapshots) was not attempted — flagged as
   available follow-up, not required to support this unit's finding.
5. **`gtest_discover_tests` intermittently timed out** during two of this
   unit's several full builds (`test_zone_entities`,
   `test_eq_action_handler_transforms`, `test_position_frame_roundtrip`) —
   a re-run of `cmake --build` (no source changes) succeeded cleanly both
   times; judged host-load-related (concurrent eqemu containers + parallel
   compile), consistent with T124's own documented instance of this same
   class of transient failure, not a defect in this unit's changes.
6. Two `sudo` uses this unit (retargeting `core_pattern` for the standard
   core-capture workaround, and `gcore` for the live post-fix snapshot,
   which needs elevated `ptrace` under this host's default
   `yama/ptrace_scope`) — both restored/verified before session end
   (`core_pattern` restored to its exact prior value; the live-snapshot core
   itself deleted after extraction, matching every other run's cores in
   this unit which were deleted after extraction to keep the review
   directory a reasonable size — evidence retained as ring TSVs and
   symbolised `.txt` files, not raw cores).

## Board

**Named the holder, with symbolised write-time evidence: `EntityVisual::
primaryEquipNode`/`secondaryEquipNode` (weapon/shield meshes) built from
`EquipmentModelLoader::meshCache_`, a persistent cross-zone mesh cache with
zero eviction-listener coverage.** One dominant holder, not several — all
118 pre-fix UAF writes resolved to the identical 8-frame call chain. None
originated in T123/T124's own guards. Fixed in two parts (entity-visual
guard extended to cover the two equip-node pointers; `EquipmentModelLoader`
self-registers and fixes its own cached meshes' materials). **Rate:
1.688% (118/6989) pre-fix -> 0% (0/1020, live-snapshot-measured) post-fix;
crash rate 10/10 -> 0/10** — the first genuinely zero measurement this
five-unit arc has produced. **T125's Pi re-measurement is now worth
dispatching.**
