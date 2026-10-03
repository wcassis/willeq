# B949 — the texture cache's LRU follows DRAW order, not load order

Date: 2026-10-03. Branch `task/B949-texture-lru-draw-order` from `gitlab/fable`
@ `d3375b65`, merged `origin/fable` @ `55c9ac0c` (B967 rebless), GitLab #267
(class B, lane offline). Found by B944 (`refactor/B944/README.md`).

Will's rules this unit works to: memory first, frame rate second (2026-09-29);
performance is secondary. No golden, budget, ceiling or gate assertion moved
(no ruling quotes one, so none was blessed).

## The defect

`ConstrainedTextureCache`'s LRU list (`lruOrder_`) was touched on LOOKUP
(`getOrLoad`/`uploadTexture`/`getTexture`/`touch`) and, since B944, on restore
— never on DRAW. So under the ceiling the eviction order was load order: a
texture loaded first (the floor, the sky — the ones drawn every frame) sat at
the front and was evicted first, then B944's restore brought it back on its
very next bind, evicting the next-oldest, which may also have been on screen.
Correct pixels either way (B944's net), but it can thrash. B944 measured **85
evictions / 21 restores** for the gate's three static qeynos2 poses at a 20 MiB
ceiling.

## The fix — a cheap per-frame "used" stamp on bind

Per the issue: mark use on bind, evict the least-recently-DRAWN.

- **`COpenGLES2Driver::FrameCounter`** (`docker/irrlicht-drm/COpenGLES2Driver.{h,cpp}`):
  a static `std::atomic<u32>`, bumped once in `beginScene()` — once per
  rendered frame, on the GL thread, before any of that frame's binds. u32
  wraps after ~136 days at 60 fps; the ordering only ever compares stamps from
  nearby frames, so the wrap is tolerated.
- **`COGLES2Texture::markDrawn()`** (`COGLES2Texture.{h,cpp}`), called from
  `getOpenGLTextureName()` — the one accessor every bind path in the driver
  goes through (`getDriverTextureHandle()` and `lock()` route through it too).
  It stamps the texture with the current frame number; one relaxed atomic
  load/compare/store per bind, skipped when the frame was already stamped (a
  texture bound five times in one frame writes once).
- **`ConstrainedTextureCache::endFrame()`** (`constrained_texture_cache.cpp`):
  called once per rendered frame, on the GL thread, AFTER that frame's
  binds/draws and before any further upload/eviction. It reorders `lruOrder_`
  into least-recently-DRAWN order (ascending stamp; front = the eviction
  candidate). Eviction (`evictUntilAvailable`) already falls off the front.
  Why a separate call and not a `touch` on bind: the driver's bind path is on
  the GL thread and holds no lock; reordering the list there would take the
  cache's mutex on every bind of every texture. The bind only records a cheap
  per-frame stamp, and this one per-frame call does the single reorder. For a
  non-GLES2 driver the stamps do not exist, so `endFrame()` is a no-op and the
  list stays in load order (the pre-B949 behaviour; the EDT_NULL unit-test
  driver only).
- **The reorder moves the EXISTING list nodes, it never rebuilds the node
  set.** `std::list` has no in-place stable sort, so `endFrame()` copies the
  names to a vector, `std::stable_sort`s them by stamp, and moves the nodes
  into that order with `splice()`. A spliced node keeps its address, so every
  `CachedTexture::lruIterator` into `lruOrder_` stays valid. `evictTexture()`
  / `releaseStorage()` `erase()` through those iterators — and a first draft
  used `lruOrder_.assign()`, which rebuilds the node set and dangles every
  `lruIterator`; a gate run on that draft spun forever in `evictUntilAvailable()`
  on the corrupted list. The splice form is what the numbers below are from.
- **Stability.** Equal stamps (two textures drawn in the same frame, or
  several never drawn) keep their old relative order — load/touch order. So a
  never-drawn texture sits exactly where load order put it relative to its
  equal-stamped peers, and the first `endFrame()` of a zone is the identity
  permutation (every stamp is 0). A freshly RESTORED entry was `push_back`ed by
  `restoreEvicted()`, which is consistent: it was just drawn (its bind restored
  it).
- **Call sites.** `Application::render()` (`application_frame.cpp`) calls
  `m_constrainedCache->endFrame()` once per frame, on the render thread, after
  `processFrame()` returned true (a cache-only op, no GL calls). The
  benchmark's frame loop (`gles2_zone_benchmark.cpp`) and its static-pose loop
  (`zone_bench_modes.cpp`) call it after their own `endScene()`, so the gate's
  deterministic three-pose run exercises the same reorder.

## Measured — the gate's qeynos2 golden invocation at a 20 MiB ceiling

Same invocation both sides (`--frames 60 --warmup 5 --res 800 600`, the
three `--camera-pose`s, the manifest's production args, `--occluder-budget 16`,
`--texture-memory-bytes 20971520` — B944's test knob; the preset ceiling is
untouched). x86 surfaceless llvmpipe, Mesa 25.2.8.

| side | entries | resident | evictions | restores | restore failures | pinned | used / limit |
|---|---|---|---|---|---|---|---|
| before (`d3375b65`, pre-B949) | 126 | 62 | **85** | **21** | 0 | 0 | 20,884,116 / 20,971,520 |
| after (this branch, merged) | 126 | 62 | **84** | **20** | 0 | 0 | 20,884,116 / 20,971,520 |

The `[texture-cache]` line is the benchmark's stdout at the end of the run.
Used and limit are byte-identical (the ceiling is enforced either way, and the
resident set is the same 62). The count dropped by exactly one eviction/restore
pair: the texture the old load-order LRU would have evicted and immediately
restored on the next frame now stays resident because it is on screen, so one
of B944's 21 restores no longer happens.

**No golden moved.** Frames f0/f1/f2 are byte-identical to the committed
qeynos2 goldens (`cmp` clean, 0 differing pixels) both before and after. The
full 5-zone `visual_gate.py` run on this branch (merged fable tip, the
B967-reblessed goldens): **0 pixel mismatch on all 15 frames of all 5
zones**; the single gate failure is `iceclad: p95 process-CPU ms 10.563
exceeds ceiling 9.530` — a CPU-timing metric run on this host while the gate
tree and the 20 MiB benchmarks were building concurrently (the gate's own
header documents these false trips with zero image mismatches: the p95 is
taken over only three measured frames). CI's gate job re-takes it on a clean
runner. The gate's own invocation at the PRESET ceiling is untouched by this
change (nothing evicts at the preset ceiling, so the reorder is a no-op on
which texture is resident — the pixels are load-order-independent).

The one remaining thrash (84/20) is structural, not an LRU-order defect: at
20 MiB the working set (62 resident of 126) is larger than the ceiling, so
something must evict every frame regardless of order; the fix only changes
WHICH texture goes — now the off-screen one, not the floor.

## Repeatable test

`tests/test_texture_lru_draw_order.cpp` (ctest, real EDT_OGLES2 surfaceless
device, self-skips without EGL; linked against `willeq_core`):

- `EveryFrameDrawnTextureSurvivesLoadPressure` — a texture loaded FIRST (worst
  position in load order) and bound every frame survives nine rounds of
  eviction pressure from loads, while the ceiling is enforced by evicting the
  off-screen loads; restore count stays 0. Pre-B949 this failed at round 2 (a
  was the oldest-LOADED resident and the first load evicted it).
- `WithoutDrawsEvictionStaysInLoadOrder` — the control: the same sequence with
  no draws. Every stamp stays 0, `endFrame()`'s stable sort keeps load order,
  and the oldest-LOADED texture is evicted by the first load that presses the
  ceiling — the pre-B949 behaviour, and what makes the first test
  discriminating.
- `SameFrameTieKeepsLoadOrder` — two textures drawn in the same frame carry
  the same stamp; the tie keeps load order, so the oldest-loaded of the pair
  is the eviction candidate.
- `RestoredTextureIsProtectedOnTheNextLoad` — an evicted texture that comes
  back on a frame is restored by its own bind and its fresh stamp protects it
  from the next load; the off-screen texture goes instead.

All four pass (also after the B967 merge). The two B944 nets
(`test_texture_eviction_residency`, `test_texture_eviction_character_restore`)
still pass.

## Files

| file | change |
|---|---|
| `docker/irrlicht-drm/COpenGLES2Driver.{h,cpp}` | `FrameCounter` (static atomic, bumped in `beginScene()`); `gles2TextureGetDrawFrameStamp()` |
| `docker/irrlicht-drm/COGLES2Texture.{h,cpp}` | `markDrawn()` (called from `getOpenGLTextureName()`), `getDrawFrameStamp()`, `DrawFrameStamp_` |
| `include/client/graphics/constrained_texture_cache.h` | `endFrame()` declaration + the class-comment B949 block; `lruOrder_` comment |
| `src/client/graphics/constrained_texture_cache.cpp` | `endFrame()` body (stable sort + splice); the `gles2TextureGetDrawFrameStamp` extern |
| `src/client/application_frame.cpp` | `render()` calls `endFrame()` once per frame after `processFrame()` |
| `tools/hwcaps/gles2_zone_benchmark.cpp` | frame loop calls `endFrame()` after `endScene()` |
| `tools/hwcaps/zone_bench_modes.cpp` | static-pose loop calls `endFrame()` after `endScene()` |
| `tests/test_texture_lru_draw_order.cpp` | the repeatable test (4 cases) |
| `tests/cmake/renderer_guards.cmake` | registers `test_texture_lru_draw_order` |

## Not verified / out of scope

- **The client's live behaviour under the ceiling** is not re-measured here
  (lane offline; the Pi lane is separate). The unit test drives the same
  driver + reorder the client uses; the client's `endFrame()` call is in
  `Application::render()`, on the render thread, after the frame's binds — the
  same ordering the test and the benchmark use.
- **The Pi (Lima) GPU** is not exercised; the driver change is a per-bind
  atomic stamp + a per-frame O(n log n) reorder, with no new GL calls. The
  32 MB target and the ledger budget are unchanged (no budget blessed).
- **The one-count reduction (85/21 → 84/20)** is measured on the x86
  llvmpipe gate invocation; it is a property of the LRU order at this
  ceiling, not a general bound. A different ceiling or asset mix changes the
  count; the invariant is "evict the least-recently-drawn, restore the
  on-screen one as late as possible."

## Evidence paths (raw logs; not committed)

- `~/kb-evidence/b949-lru-draw-order/before/bench_20mib.log` — pre-B949 run
  (`d3375b65`), the `[texture-cache]` line and the f0/f1/f2 PNGs.
- `~/kb-evidence/b949-lru-draw-order/after/bench_20mib_merged.log` — this
  branch (merged), the same. The `f_f0/1/2.png` next to each are the frames,
  `cmp`-clean against `tests/goldens/qeynos2_f*.png`.
- The unit test is repeatable: `cmake --build build -j3 --target
  test_texture_lru_draw_order && ./build/bin/test_texture_lru_draw_order`.
