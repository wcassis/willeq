# T205 — FIXED: `ConstrainedTextureCache::clear()` destroyed the UI atlas

The EQE1 crash that blocked T193–T204 is a use-after-free of the UI atlas
texture, destroyed by the zone-unload cache wipe.

## Who freed it — named by watchpoint

Broke in `UIAtlas::load` after `texture_` was assigned, watched its vtable slot,
and continued. It fired on the loading thread, the vtable changing from
`COGLES2Texture`'s to a *construction vtable* — a destructor running:

```
#0  ITexture::~ITexture
#1  COGLES2Texture::~COGLES2Texture
#3  IReferenceCounted::drop
#4  CNullDriver::removeTexture
#5  COpenGLES2Driver::removeTexture
#6  ConstrainedTextureCache::clear          <-- here
#7  IrrlichtRenderer::unloadZone
#8  Application::loadZoneGraphicsOnThread
```

`unloadZone()` wipes the whole texture cache on every zone load. The UI atlas
is in that cache (it comes from `willeq.eqe1`) but is **not a zone asset** —
`UIAtlas` loads it once and `UIRenderer` keeps a **raw pointer** to it forever.
So the next frame drew freed memory, with the texture's vtable reading `0x50`.

**This is the same hazard the T119 comment already documents** a few lines
above, for scene nodes that survive `clear()`. The UI atlas is another owner
that survives it.

## The fix

`clear()` now honours `protectedTextures_`: protected entries are skipped in the
destroy loop and re-inserted afterwards, with their bytes counted back into
`currentUsage_`. `protectTexture("ui_atlas")` is called by `UIAtlas::load`.

**Why T202's pinning appeared to do nothing:** it guarded only
`evictUntilAvailable`, the LRU path. The killer was `clear()`, a different path
entirely — and eviction never even ran, which is why the log showed zero
eviction lines. The instinct was right and the guard was in the wrong function.

## Verification — by EXIT STATUS, not log contents

| preset | tree | exit | Ready |
|---|---|---|---|
| none | live | **0** | 1 |
| orangepi | regen | **0** | 1 |
| none | regen | **0** | 1 |

Previously these were `139` (SIGSEGV) with no `Ready`. The log now carries
`Constrained cache: preserved 1 protected texture(s) across zone unload`.

## What this unblocks

EQE1 was unusable on every platform because of this. With it fixed:

- **T199** (spawn-driven archive selection: 15 races → 3 archives, `fishe0301`
  FATAL gone) is no longer blocked and can be re-verified and merged.
- The Pi's remaining EQE1 issue is the separate, real one T195 measured —
  32-bit address-space exhaustion from the composite atlas, already addressed by
  removing the atlas from the `orangepi` preset (T198).
- **T193's Pi/x86 EQE1 comparison should be re-run**, since it was taken while
  this crash was live.
