# T203 — Line-numbered backtrace: the UI atlas draw path may never have worked

Rebuilt with `CMAKE_BUILD_TYPE=RelWithDebInfo` and reproduced under gdb.

## The stack

```
#0  COpenGLES2Driver::draw2DImage(ITexture const*, rect<int> const&, ...)  [no symbols]
#1  UIRenderer::endFrame()      src/client/graphics/ui/ui_renderer.cpp:131
#2  IrrlichtRenderer::processFrameRender
#3  IrrlichtRenderer::processFrame
#4  Application::render
#5  Application::mainLoop        src/client/application.cpp:884
#6  Application::run             src/client/application.cpp:659
#7  main                         src/client/main.cpp:160
```

## Every argument at the fault is sane

```
tex      = 0x55555876d920
destRect = (4,4) -> (164,70)
srcRect  = (0,160) -> (64,224)      a normal 64x64 atlas sprite
color    = 0xFFFFFFFF
clipRect = nullptr, colors = nullptr, useAlphaChannelOfTexture = true
```

Neither the quad nor the texture pointer is corrupt.

## Ruled out by reading the driver source

- **null `clipRect`** — `setScissorFromClip` (line 625) guards with `if (clip)`.
- **null `colors`** — `SColor tint = colors ? colors[0] : SColor(...)`.
- **the `static_cast<const COGLES2Texture*>`** — valid; `wrapExternalTexture`
  really constructs a `COGLES2Texture` (`COpenGLES2Driver.cpp:2028`).
- **LRU eviction** — never runs (T202: zero eviction lines in the log).

## The reframing

**The crash requires the UI atlas to EXIST**, and EQE1 is the only way it
currently can. `UIAtlas::load` fetches `ui_atlas` from the constrained cache,
which is EQE1-only; without `--eqe1-path` the load fails, `atlas_` stays null,
and `endFrame`'s `if (atlas_ && !atlasQuads_.empty())` skips the draw entirely.

So EQE1 is not corrupting anything — **it is simply the only configuration in
which this code path runs at all.** Which raises the real possibility that
`UIRenderer::endFrame`'s atlas draw has **never worked**, and that every "EQE1
crashes" observation in this arc is that latent bug being reached for the first
time.

## The blocker on going further

`#0` has no symbols: the patched Irrlicht in `third_party/irrlicht-gles2-x86`
is built separately by `scripts/build-gles2-x86.sh --irrlicht-only`, so
`RelWithDebInfo` did not reach it.

**Next: rebuild the patched Irrlicht with `-g`** and re-run this repro to get
the faulting line inside `draw2DImage`. Everything on our side of the boundary
is now accounted for.

## Build-tree note

`build/` is now `RelWithDebInfo` (it previously had no `CMAKE_BUILD_TYPE`).
Keep while chasing this; reset if timings matter.

---

## T204 — Rebuilt Irrlicht with `-g`: the texture is DANGLING

`scripts/build-gles2-x86.sh` only passes `-g` under `--t122-texprobe` (which
also injects a behaviour-changing define), so the library was rebuilt directly:

```
make -C third_party/irrlicht-gles2-x86/source/Irrlicht NDEBUG=1 -j20 \
  CXX=g++ CC=gcc AR=ar CXXFLAGS="-g -fno-omit-frame-pointer" \
  CXXINCS="-I../../include -Izlib -Ijpeglib -Ilibpng -I/usr/include/libdrm" staticlib
cp ../../lib/Linux/libIrrlicht.a ../../lib/libIrrlicht.a
```

then relinked `willeq`.

**`p *texture` at the fault:**

```
$1 = {<irr::IReferenceCounted> = <invalid address>,
      _vptr.ITexture = 0x50,
      NamedPath = {Path = {array = 0x61 <error: Cannot access memory...>
```

**The texture is dangling.** The pointer *looks* plausible
(`tex = 0x55555876d920`, which is why T203 read it as sane) but dereferences to
garbage — a vtable of `0x50`. The driver frame's `vertices`/`indices` locals are
likewise uninitialised, consistent with faulting on first use of the object.

## What this settles, and what it re-opens

- **Not bad quad data** (T203 — destRect/srcRect are correct).
- **Not a type error** (the `static_cast` is valid).
- **Not LRU eviction** (T202 — never runs; confirmed by zero eviction lines).
- **It IS a use-after-free of the UI atlas `ITexture`.**

So T202's instinct — "UIAtlas holds a raw pointer to something the cache can
destroy" — was the right *shape* and the wrong *mechanism*. Something frees this
texture that is not `evictUntilAvailable`, which is why pinning against eviction
changed nothing.

**Next: find who destroys it.** Candidates in order:
`ConstrainedTextureCache`'s own teardown/clear paths; `unloadZone`'s
`clearEQE1Loaders()` + cache clear; and anything reached from
`releaseTextureDataAfterUpload` (`renderer_zone_stream.cpp:951`) — though that
one releases model-loader *raw bytes*, not `ITexture` objects, so it is the
weakest of the three. A watchpoint on the texture address, or a breakpoint on
the cache's destroy path, should name it in one run.

**`protectTexture()` from T202 is probably still wanted** — but it must be
consulted by whichever path actually frees this, not only by the LRU scan.
