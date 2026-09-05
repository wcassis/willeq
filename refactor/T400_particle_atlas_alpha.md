# T400 — the particle atlas carries its alpha (B225)

Class A, slot1, no server. Read the B225 board text and T389's §Review
(sprites/ and `particle_atlas_8x.png`), `tools/` particle atlas generator
(find it: grep `particle_atlas`), the loader in
`src/client/graphics/environment/` (`ParticleAtlas`), and the GLES2
particle shader/material (blend mode).

1. Measure: the shipped `data/textures/particle_atlas.bmp` header (bit
   depth, alpha), the generator's output format, and what the loader
   does with a 24-bit BMP (alpha defaults to 255 → opaque squares). One
   test that decodes the shipped file and asserts every tile has
   non-trivial alpha (pre-fix failing).
2. Fix at the generator: write a 32-bit BMP (or TGA/PNG if the loader
   path prefers — read what `data/textures/*.bmp` loads go through after
   T399's data-path routing; do not add a new image decoder) with the
   shape in alpha AND premultiplied/neutral RGB as the material's blend
   expects (read the material: additive vs alpha-blend — say which and
   why the RGB must be what it is). Regenerate the shipped file with the
   generator (the atlas pack B230/T398 may also embed it — coordinate:
   if T398 has merged, use its rebuild script; if not, note that the pack
   must be rebuilt when it does).
3. Test: the loaded tile's alpha channel matches the generator's shape
   function; the material's blend mode is pinned.
4. Visual: the gate's zones cast no spells, so no golden moves — if one
   does (a particle drawn in a gate zone: weather? torches?), STOP with
   the contact sheet, never bless. Re-take T389's fable impact frame for
   spell 54 offline if the headless capture rig allows (T389's
   `scripts/`); else owed to the next lock unit.
5. Strike B225; annotate B226 (the style sizes are a separate question —
   do NOT retune them here; T389 measured the oracle's impact as
   hundreds of sprites for 2–3 s, and that match is its own unit after
   the atlas draws).

## Proof

Build (all trees), `ctest -j6` (no expected failures), pre-fix/post-fix,
visual gate standalone + ARM, file sizes.

STOP: the material is additive and the shapes were designed for it
(then the "opaque square" is a different defect — report the frames).
