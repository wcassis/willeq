# T457 — the zone-change segfault with the inventory open (B482), the double world (B483), the airplane zone-in (B488)

Class A, eqemu lock (atomic; must be ABSENT), slot3. Read T449's
§Review and `review/T449/README.md` (its board items were numbered
B474–B481 on its branch and become B482–B489 at merge: the segfault is
B482, `up.sh` double world B483, `cast_lands` spell B484, the peer
B485, buff re-run B486, chat input B487, `#zone airplane` B488, no
pick-up gesture B489), the gdb backtrace: `CharacterModelView::
renderToTexture()` → `EQAnimatedMeshSceneNode::render()` →
`COpenGLES2Driver::bindAlphaCompanionUnit()` → `COGLES2Texture::
getOpenGLTextureName()` on a texture the zone unload destroyed; T432's
persistence reopens the paperdoll at login so a saved `Show=1` crashes
the next zone change. Read the zone unload order (`ZoneLifecycleComponent`,
`renderer_zone_lifecycle.cpp`), the paperdoll's `CharacterModelView`
(T393/T419), and T411's alpha companion binding.

1. B482: the fix belongs in the lifecycle — the paperdoll (and every
   other render-to-texture user: read the list) is torn down or
   detached BEFORE the zone's textures are destroyed, and rebuilt after
   zone-in, rather than a null check at the driver. Pre-fix failing
   test: a headless (EDT_NULL or the gate's GLES2 device if the
   companion path needs it) zone unload with the paperdoll open does
   not dereference a destroyed texture (ASan/UBSan build of the test
   binary if the tree has the option; otherwise a lifecycle-order pin
   in the G05 shape). Then LIVE: both directions with `Show=1`, `dmesg`
   clean, capture.
2. B483: `scripts/scenario.sh` / `up.sh` — the akk-stack container
   auto-starts the spire launcher, so the non-launcher path starts a
   second world. Make `up.sh` detect the running fleet and not start a
   second one (or use `--launcher` everywhere); document in
   EXECUTOR_PROTOCOL.md. Prove: one `world` process after `up.sh`.
3. B488: `#zone airplane` (Plane of Sky, min level 46): the client takes
   the arrival position and never completes zone-in. Trace with the
   zone-in state machine logs (T340's loader census) which step never
   fires; fix if fable's (sky has no BSP — T440: HCMap path), board if
   the server's. This unblocks B453's `/dumpscene` proof.
4. If time remains under the lock: the T449 items not reached, in
   order — B461 lift ride (walk on), T443's `/who` guild + ESC, B356/
   B227 duel rig, B460 oracle ESC. Strike or reopen each with evidence.

## Proof

Standard (background builds, `ctest -j6`, pre/post, G05/G06, gate +
ARM with host load, file sizes, doc nets). Server values restored with
the world DOWN; `/camp` cleanly; lock released. Media `review/T457/`.
New items B490+.

STOP: an image golden moves; the crash cannot be reproduced headless
(then fix by lifecycle order, prove live only, and say so).
