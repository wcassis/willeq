# T488 — `desertsky.bmp` never reaches the sky texture cache: nro's sky dome draws with no texture

Class A (visible in every desert zone), **offline (no lock)**, one slot.
Found by the 2026-09-14 audit: every Pi run in nro (tip and 09c5472f) logs
`loadSkyTexture: 'desertsky.bmp' not in cache — pre-decode path failed, skipping`
(`src/client/graphics/sky_renderer_celestial.cpp:227`;
`review/AUDIT-0914/pi/raw/R3_tip_atlas_1280_inventory/willeq.log:611`) and
the frames show a flat tinted dome (blue after T484, tan before). Read first:
T484 Review (sky tint, B780–B783), `include/client/graphics/eq/sky_loader.h`
(`Background` layer: "normalsky, desertsky"), the sky upload site in the
zone loader ("uploaded via constrained cache getOrLoad() + uploadTexture()
during Step 10"), `data/eqe1_texture_index.json` (`desertsky` IS in the pack,
entry 1191, with an `_alpha` companion).

## Tasks

1. Trace, from the zone's sky type to the cache insert: which code pre-decodes
   the Background layer's texture names for the zone's sky type, why
   `desertsky.bmp` is not among them for nro (name/extension mismatch —
   `.bmp` vs the pack's bare `desertsky` — sky type table, or a
   normal-sky-only list), and whether the same failure hits the other layers
   (clouds, sun, moon) or other sky types. Report the table of sky types ×
   layers × cached-or-not from a headless run over the gate zones + nro.
2. Fix at the root with a test that is red first (a headless WldLoader/sky
   config case: nro's sky type resolves every layer texture to a cache hit).
3. Before/after x86 stills of nro's sky at 11:00 (offline: the gate's
   `gles2_zone_benchmark` renders zones; nro may need adding to a capture
   list, not to the gate). `review/T488/` with a README.

## Proof

Standard verification bar; visual gate: **an image golden may move if a gate
zone is a desert-sky zone — STOP and report before/after, Will blesses.**
ARM; file sizes; board: new B item struck by this unit; B781 (tint table)
untouched unless the fix makes it moot.

File territory: `src/client/graphics/sky_*.cpp`, `sky_config.cpp`, the sky
loader/upload site in the zone loader, sky tests, `review/T488/`. **Not**
T484's `gles2_vertex_color.h` helper or the driver.

STOP: an image golden moves; the cause is the EQE1 pack lacking a texture
(then the fix is the builder + re-bless, which is pre-approved only for T279
— report).
