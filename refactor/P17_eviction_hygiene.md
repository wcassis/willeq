# P17 — Eviction hygiene: zone-unload texture release + entity-node guard + snapshot race

Pre-authorized batch (Will, 2026-07-27). Slot: wt/slot1, branch
task/P17-eviction-hygiene. OWNS THE EQEMU SERVER. Protocol:
refactor/EXECUTOR_PROTOCOL.md. Parallel: P18 (slot2, benchmark/
animation files), P19 (slot3, Pi/benchmark runs) — stay out of
tools/hwcaps code.

## Motivation (P15, 2026-07-27)

Three related production defects from the P15 findings:
1. **Cross-zone texture accumulation (the solved eviction mystery)**:
   ConstrainedTextureCache is never reset across zone unloads —
   befallen→qeynos2 = 27 evictions; long sessions worse (P10b's 71).
   The 1999-honest fix: release the OUTGOING zone's textures at zone
   unload (zone_region/object categories; entity/composite textures
   for races not present in the new zone are fair game too — design
   the retention rule, justify it).
2. **Entity-node dangling-ITexture* post-eviction**: P10b's
   RegionMeshTextureGuard protects region nodes only; entity mesh
   materials have the identical hazard (P15 hit it intermittently).
   Extend the guard (or a sibling) to entity nodes/instance meshes.
3. **Stale-zone-name-vs-position snapshot race** (P15's second new
   bug — its Review has the evidence trail).

## Design

1. Read P15/P13/P10b reviews + the ledger harness. Implement the
   unload-release (hook zone teardown; category-aware; the
   TextureUploadLedger should also reset/segment per zone so future
   audits stay per-zone-readable).
2. Entity guard: same eviction-listener pattern as
   RegionMeshTextureGuard, over the entity renderer's node/instance
   bookkeeping (EQAnimatedMeshSceneNode materials_ + instanceMesh_
   buffers).
3. Snapshot race: root-cause from P15's evidence, minimal fix,
   normal-path-untouched proof (the P15 precedent).
4. Verification: the P15 multi-hop session goal now achievable — 4+
   hops (befallen↔qeynos2↔third zone) in one process, ledger on:
   near-zero evictions expected post-release (quantify), no dangling
   crashes, no snapshot race; plus a mergeZoneDraws regression spot
   (merge should now ACTIVATE in qeynos2 once single-zone footprint
   stays under budget — check and report the activation change);
   standard bar.

## Out of scope
- Budget resizing; eviction-policy redesign; foliage/pop-in (next
  server tenants).

## Review
(executor fills in)
