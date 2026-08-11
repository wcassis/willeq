# T67 — Remove ConstrainedMeshCache from production

**Slot: t1. x86 only. No Pi. NO EQEMU.**

**Territory: `include/client/graphics/constrained_mesh_cache.h`,
`src/client/graphics/constrained_mesh_cache.cpp`,
`include/client/graphics/irrlicht_renderer.h`, the nine renderer TUs that
reference the pointer, `include/client/graphics/constrained_renderer_config.h`
+ `src/client/graphics/constrained_renderer_config.cpp` (the
`meshMemoryBytes` field), `src/client/graphics/CLAUDE.md`, and tests.**

Do not touch the GLES2 driver, the bone/equipment path, or the command
registries. `refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `refactor/deferred.md`'s entry
**"ConstrainedMeshCache is never constructed (P10 finding, 2026-07-26)"**.

## The decision (Will, 2026-08-11)

Remove it from production, keep the code findable for reference.

`ConstrainedMeshCache` is a complete, working 385-line LRU mesh cache —
budget, eviction with protected regions, freeze/unfreeze, hit-rate and
eviction stats. **It has never been constructed.** The only statement in
the codebase that touches the `unique_ptr` as an lvalue is
`renderer_zone_lifecycle.cpp:177`'s `constrainedMeshCache_.reset()`, and
that sits inside `if (constrainedMeshCache_)`, so it never runs either.
The pointer is null for the entire process lifetime and every one of the
~40 call sites is a read behind a null guard.

Supporting facts, verified during scoping:
- There is **no way to turn it on**. `meshMemoryBytes` is computed per
  preset (24MB for `orangepi`, `constrained_renderer_config.cpp:306`) and
  consumed by nothing.
- The eviction pressure it exists to relieve **has never been reproduced**.
  P10b's "71 evictions" is the only such observation ever recorded; P13 and
  P13b then measured **zero** evictions across four independent
  configurations.
- It has been independently written **twice** — once on the now-deleted
  `mem` branch, once as `d442db12` (2026-02-18) — and wired **zero** times.

**"Keep a reference" does not mean an archive tag here.** Unlike the `mem`
branch (unmerged, whose commits would eventually be garbage-collected),
this code is in `fable`'s history and is permanently recoverable via
`git log --follow -- <path>`. The reference is a **pointer in the docs**:
record the last commit that contained the implementation and its file
paths, so a future reader can retrieve it in one command instead of
knowing to go looking.

## Step 1 — the one part that is NOT dead

**`ConstrainedMeshCache::estimateMeshSize()` is a static function and it
genuinely runs**, at `renderer_zone_stream.cpp:282` and
`renderer_zone_draw.cpp:1155`. The class is not link-dead.

Deal with this deliberately before deleting anything: keep that
calculation, relocated somewhere sensible (a free function in a mesh or
renderer utility header is the obvious shape), with its behaviour
unchanged. **Do not delete it and do not reimplement it from memory** —
move it verbatim and say where it went.

If you find any other static or otherwise-live member during the sweep,
treat it the same way and report it.

## Step 2 — establish what is genuinely dead, file by file

Roughly nine TUs reference `constrainedMeshCache_`:
`renderer_zone_stream.cpp` (the bulk), `renderer_zone_draw.cpp`,
`renderer_zone_lifecycle.cpp`, `renderer_zone_load.cpp`,
`renderer_diagnostics.cpp`, `renderer_sim_bridge.cpp`,
`renderer_player_physics.cpp`, `renderer_frame.cpp`, `renderer_debug.cpp`.

`renderer_zone_stream.cpp` is *largely* unreachable, but **not
necessarily entirely** — e.g. `:851` guards on
`constrainedMeshCache_ && !textureRebuildQueue_.empty()`, and the texture
rebuild queue may well be live on its own. **Do not delete whole files or
whole functions on the assumption that the guard implies death.** For each
site, determine whether removing the cache leaves live code that must be
preserved, dead code that goes with it, or a branch that now needs
simplifying rather than deleting.

Report the per-file accounting in the Review. This is the part of the unit
where a mistake is expensive and invisible.

## Step 3 — remove the config field

`meshMemoryBytes` (`constrained_renderer_config.h:209`) is a derived limit
consumed by nothing once the cache is gone. Remove it, including the
per-preset assignments (`constrained_renderer_config.cpp:212, 232, 254,
306`), the `calculateMemoryLimits()` default
(`meshMemoryBytes = totalMemoryBudgetBytes / 5`, `:104-105`), the
validation that rejects zero when a total budget is set (`:72-73`), and
the JSON preset keys (`mesh_memory_mb` / `meshMemoryBytes`, `:527-529`).

**Check whether any committed preset JSON in `config/` sets those keys.**
If so, decide deliberately whether removing the field silently ignores an
existing key or errors on it, and say which you chose. A config key that
becomes silently meaningless is its own small trap.

Note Will's standing decision from the same conversation: **per-subsystem
budgets that are defined and enforced are the model** — no global
coordinator is wanted. Removing an unenforced, unconsumed budget is
consistent with that, not a retreat from it.

## Step 4 — the docs, which are actively misleading

`src/client/graphics/CLAUDE.md:121-124` describes the mesh cache as
shipping behaviour — the 24MB budget, on-demand region loading, eviction —
and then makes a consequential claim about lifecycle:

> Texture pixel data (`releaseTexturePixelData()`) is released only when
> the mesh cache is NOT active (all meshes pre-built). When the mesh cache
> IS active, texture pixel data must be retained for progressive region
> mesh rebuilds.

Since the cache is never active, that second branch has never executed.
Anyone reasoning about memory lifecycle from this document — including a
future Claude — is being told about a mechanism that does not run.

Correct it to describe what actually ships: regions are built eagerly at
zone load, and texture pixel data is always released. Add the reference
pointer (commit sha + paths) so the removed implementation stays findable.

## STOP conditions

- `estimateMeshSize()` (or any other live member) turning out to be
  entangled such that preserving it means keeping the class — report
  rather than improvising.
- Any call site where removing the cache would change rendering behaviour.
  There should be none; if you find one, the premise is wrong and I want
  to know before you proceed.
- The visual gate moving. This is dead-code removal — **rendering must be
  byte-identical**, and the gate is the proof.

## Verification

Full protocol bar. The visual gate matters more than usual here: it is the
evidence that removing ~40 guarded branches changed nothing. State the
line-count delta and the per-file accounting from Step 2.

## Review

(executor fills: per-file accounting of what was removed vs preserved;
where `estimateMeshSize()` went; any other live member found; the config
key decision; the docs correction; the reference pointer recorded;
line-count delta; deviations)

Board (`refactor/deferred.md`, append, never delete): re-title the
"ConstrainedMeshCache is never constructed" entry as resolved-by-removal,
recording Will's decision and the reference pointer. Note explicitly that
this closes the item **without** answering whether progressive region-mesh
streaming is ever wanted — if Pi memory pressure is later measured, the
implementation is one `git show` away, and the entry should say so.
