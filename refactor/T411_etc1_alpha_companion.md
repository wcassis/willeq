# T411 — alpha through the pack's own companion channel, not RGBA8888 (B287, B261)

Class B (the Pi's GPU texture budget: 56 of 59 textures at 256² cost
349,528 B as RGBA8888 vs 43,688 B as ETC1+mips; the pack grew +606% and
every zone maps +17.2 MB), slot1, no server. Read B287, B261, T400's and
T406's §Reviews (the consumers and their single-sampler shaders:
`SOLID3D_FS`, `UI2D_FS`, the particle shader), the EQE1 format's
`<name>_alpha` companion (`tools/eqe1_builder`, the loader in
`constrained_texture_cache`), and the alpha-TEST shaders that already
sample the companion on unit 1.

1. Give the three alpha-BLEND shaders a companion sampler on unit 1
   (`uAlphaTex`, `hasAlpha` uniform): `texColor.a = texture2D(uAlphaTex,
   uv).r` when the entry carries a companion. Bind the companion at the
   same sites the alpha-test path binds it (read how; one code path, not
   a second). Materials for storm clouds, rain/snow overlays, cursor,
   billboards, particle atlas switch to it.
2. Drop the `--rgba8888-texture` overrides for every entry whose consumer
   now samples the companion; keep only the ones that provably cannot
   (say which). Rebuild the pack via the script (atomic; copy the old pack
   under `review/T411/`); the pack should return close to 2.8 MB (+ the
   companions, which are ETC1 too — measure). Update the sha256 pins in
   the same commit.
3. Tests: per consumer, a headless render with the companion bound yields
   the same alpha as T406's RGBA8888 path (pre-fix: the companion is
   ignored → opaque); the pack census asserts companions present and no
   RGBA8888 payload except the documented exceptions.
4. Visual gate standalone: no image golden should move (alpha identical
   by construction — if one moves, the companion is not equivalent: STOP
   with the contact sheet). `eqe1MappedBytes` WILL drop; report the new
   three numbers for a B240-style bless (Will blessed the up-move; the
   down-move is put to him the same way — do not bless).
5. GPU texture memory on the Pi: the P13 ledger shape — report the
   qeynos2 upload total before/after (the coverage run's
   `gpuTextureBytes` if the gate emits it).
6. Strike B287, B261.

## Proof

Build (all trees), `ctest -j6`, pre-fix/post-fix, gate standalone + ARM,
file sizes.

STOP: a consumer's shader cannot take a second sampler on the DRM/Pi
path (unit count); report it.
