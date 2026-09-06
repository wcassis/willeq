# T427 — the rain overlay tilts (B330); the detail atlas matches its generator (B265)

Class B ×2, slot2, no server. Read B330 (T414 deleted the inert tilt keys;
honouring them needs a rotated-quad draw — `RainOverlay::render()` tiles
axis-aligned `draw2DImage` rects), B265 (T403: `detail_atlas.bmp` is the
one BMP its generator does not reproduce — 29,509 px differ: an
A1R5G5B5 round-trip signature (every byte a multiple of 8) plus real
drift in the twelve footprint tiles where the generator now draws
per-biome colours), T403's §Review, T418's oracle rain videos
(`review/T418/oracle/` — does the official rain tilt with wind? measure
the streak angle in the frames), `tools/generate_textures.cpp`
(`detail_atlas`), `DetailTextureAtlas::createAtlasImage()`, and the
detail consumers (DetailManager) — the gate's outdoor zones DRAW this
atlas.

## Leg A — B330

1. Oracle first: from T418's rain frames, the streak angle vs vertical
   (and whether it changes with intensity). If the oracle's rain is
   vertical, the tilt keys were fiction — strike B330 as "oracle draws
   vertical rain" and stop the leg. If it tilts: implement a rotated
   quad path for the overlay (the 2D driver has `draw2DImage` with a
   rotation? read Irrlicht's; else draw the tiles as two triangles via
   the 3D material path the particle renderer uses), restore the two keys
   with the MEASURED angle, test pre-fix failing (the streak texture's
   principal axis in a rendered frame equals the configured angle).

## Leg B — B265

1. Separate the two causes: regenerate at full colour depth and compare
   to the shipped file — which pixels differ by the A1R5G5B5 quantisation
   only, which by the footprint drift. The generator is the truth for the
   shipped asset (the tree's rule since T403), so the shipped file must
   match it — BUT the gate's outdoor zones draw it, so the pixel goldens
   WILL move if the regenerated atlas differs visibly. Do the comparison,
   regenerate into a temp path, run the gate against it (`--eqe1-path`
   with a temp pack built from a temp textures dir — do NOT install), and
   report whether any golden moves and by how much (contact sheet). If
   none moves: ship it (cascade in one commit; install atomically with no
   ctest/gate/willeq running; report the mapped-bytes delta). If one
   moves: STOP with the contact sheet — that is a re-bless for Will.

## Proof

Build (background), `ctest -j6`, pre-fix/post-fix, `TexturesReproducible`
/`PackReproducible` green, visual gate standalone + ARM, file sizes.

STOP: Leg B moves a golden (report; do not install).
