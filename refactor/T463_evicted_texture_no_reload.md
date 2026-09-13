# T463 — renderer textures evicted at zone unload and never reloaded (B522, B523, B524)

Class A (unconfirmed → confirm first), **no lock, no Pi**, slot2. Same defect
shape as B520 (the DRM software cursor, T460 — read its Review in
`refactor/T460_cursor_eviction_and_heading_units.md` and its test
`tests/test_software_cursor_eviction.cpp`, the model for this unit).

## The three candidates (T460's census, unconfirmed)

- **B522** `StormCloudLayer::cloudFrames_` — nulled by an eviction listener,
  never reloaded.
- **B523** `TumbleweedManager::tumbleweedTexture_` — no eviction listener, no
  protection, no reload: possibly a DANGLING pointer after
  `ConstrainedTextureCache::clear()` `removeTexture()`s it (use-after-free,
  the B482 class), not just a missing texture.
- **B524** `BoidsManager` `creature_atlas` — falls back to the placeholder
  after the first zone unload.

Known-good siblings for contrast: `ParticleManager` and `DetailManager`
reload in `onZoneEnter`.

## Tasks

1. For each: trace load → cache → zone unload (`clear()`) / LRU eviction →
   next zone's use. Confirm or refute with a pre-fix failing test driving the
   real `ConstrainedTextureCache` on an EDT_NULL device (T460 fixture shape):
   after `clear()` (and under budget pressure), the owner still draws a valid,
   live texture. For B523 prove whether the pointer dangles (the texture is
   removed from the driver while the member still holds it).
2. Fix per item, choosing with a written reason: reload in the owner's
   zone-enter hook (the `ParticleManager`/`DetailManager` idiom — preferred
   for zone-scoped effects, keeps them evictable) or `protectTexture()` (only
   for small process-lifetime assets). Every holder of a cache texture must
   either be protected, or be notified AND reload; add a guard where one is
   missing.
3. Close the class: a census test (source scan or registry) that fails when a
   renderer member holds a cache-loaded `ITexture*` with neither protection
   nor an eviction listener + reload path — or, if a generic net is not
   cheap, say so and pin each site.

## Proof

Standard verification bar (EXECUTOR_PROTOCOL): background builds with exit
files, `ctest -j6` (re-run load artefacts standalone —
`HeadlessShutdownTest` is one), G-batch goldens for every TU touched, visual
gate (no golden moves), ARM build, file sizes. Board B550–B559; strike
B522–B524 with evidence.

File territory: `src/client/graphics/environment/` (storm clouds,
tumbleweeds, boids), their headers, the texture cache only if a generic hook
is needed, new tests. **Not** `src/client/graphics/ui/` / frame timing
(T462, slot3), **not** camera/hotkeys/slash/player (T461, slot1).

STOP: a fix requires changing zone lifecycle order in
`renderer_zone_lifecycle.cpp` beyond adding a reload call (report the needed
change); an image golden moves.

## Review

(executor fills)
