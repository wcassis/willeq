# T92 — Who is handing the texture cache zero bytes? (plus the P50 query gap)

**Slot: slot2 or whichever is free. Headless first. Live leg only to
reproduce in `qeynos2` if the headless path cannot — eqemu lock normally.**

**Territory: `src/client/graphics/constrained_texture_cache.cpp` and its
callers, `tests/`, `refactor/`, `/mnt/projects/fable/review/T92/`.**

Read the P50 entry in `refactor/deferred.md` (search `P50 (2026-07-31)`),
then this plan in full.

## Part 1 — the real defect: empty buffers, not failed decodes

T85's `qeynos2` capture logged **30** of these at WARN:

```
Constrained cache: decode failed for 'hum_hs_def_0_0' (0 bytes)
Constrained cache: decode failed for 'qcm_hs_def_0_0' (0 bytes)
Constrained cache: decode failed for 'rat_hs_def_0_0' (0 bytes)
... 12 distinct names, mostly `*_hs_def_0_0` head textures, plus
    'ship_actordef_0_0'
```

The message is **misleading**. At `constrained_texture_cache.cpp:234-235`
the guard is `processTextureData(data, ...)` failing — but `data.size()` is
**0**. Nothing failed to decode; **the caller supplied an empty buffer.** The
defect is upstream of this function.

Establish:
- **Which caller** passes empty `data` (`getOrLoad(name, data)` has callers in
  `animated_texture_manager.cpp` and the entity/race/equipment loaders — find
  the one responsible for `*_hs_def_0_0` head textures).
- **Why** it is empty. Does the S3D/archive read return nothing, is the name
  wrong, is it a race with a background prep worker, or is the entry genuinely
  absent from the archive?
- Whether these names are even correct — `hum_hs_def_0_0`, `qcm_hs_def_0_0`
  and friends look like composite/variant keys. If a key is being constructed
  wrongly, the lookup would legitimately find nothing.

**Fix the message regardless of the cause.** A log that says "decode failed"
when the input was empty sent this investigation the wrong way once already;
distinguish empty-input from genuine decode failure at the call site.

## Part 2 — what this does and does not explain

**Do not assume this is the magenta/checkerboard defect.** These are character
head and actor textures, which would produce untextured NPCs, **not** magenta
walls and floors. State what the failure actually accounts for.

Separately: zone geometry logs its texture failures at **`LOG_DEBUG`**
(`zone_geometry.cpp:357`, `loadTextureFromBMP returned null`) while model
failures log at `LOG_WARN`. That inconsistency is why the wall magenta has
never been named. **Raise the zone-geometry texture failure to `WARN`** with
the texture name, then a single `qeynos2` run will say what is actually
failing on the walls. That is a small, clearly-scoped change and it is the
highest-value thing in this unit.

Also seen in the same capture, at DEBUG:
`buildTexturedMesh: texture 'crate1.bmp' has empty data` (also `crate2-4`,
`phook.bmp`, `pchain.bmp`) — same empty-buffer signature on the object path.
Note whether it shares a cause with Part 1.

## Part 3 — P50, small and separate

`ConstrainedTextureCache`'s by-name query methods — `hasAlpha(name)`,
`getTexture(name)`, `getAlphaTexture(name)`, `isETC1Texture(name)` — look up
`cache_` by plain name, but `uploadTexture()` stores masked variants under the
mangled key `cacheKey(name, true)` = `name + "\x01masked"`. A plain-name query
for a masked variant therefore always misses.

**P50's own entry records that this does not affect rendering correctness** —
`MaterialType` selection is driven by the WLD `textureMasked()` flag at
mesh-build time, never by these methods, and no known production caller
depends on them for a decision that matters. So this is API hygiene, not a
rendering fix. **Do not inflate it, and do not claim it fixes anything
visual.**

Take option (a) from the P50 entry: add an `applyPaletteMask` parameter to the
four query methods, mirroring the existing `getOrLoad()`/`uploadTexture()`
pattern. Update callers. If any caller turns out to *depend* on the current
missing behaviour, that is a finding — report it rather than papering over it.

## Tests

- A test that a masked-variant upload is findable through each of the four
  query methods with the new parameter, and still not found without it.
- A test covering the empty-buffer path once its cause is known — at minimum,
  that empty input is reported distinctly from a decode failure.
- **Prove the tests can fail**: break the behaviour temporarily, show red,
  restore, show green. Report that you did it.

## What you may not do

- Do not claim the Part 1 failures explain the magenta walls without evidence.
- Do not restructure the texture cache. If a fix needs that, **STOP and
  report** — it is its own unit.
- Do not remove `ConstrainedTextureCache`. It is live, widely used
  (`application.cpp`, `sky_renderer`, `door_manager`, `zone_shader`,
  `animated_texture_manager`, `weather_effects_controller`,
  `renderer_zone_merge`) and is **not** the `ConstrainedMeshCache` that T67
  removed.

## Verification

Full build and suite (expect 2070/2071 plus new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — **a golden change here is a real signal**, since this path feeds what
is drawn: say so and explain why.

Findings to `/mnt/projects/fable/review/T92/` with a README. Open any image
you produce and describe what is actually visible; describe, do not grade.

## Review

(executor fills: which caller supplies empty data and why; what the failures
actually account for; the zone-geometry log-level change and what a `qeynos2`
run then reveals; the P50 fix and any caller depending on the old behaviour;
proof the tests can fail; deviations)
