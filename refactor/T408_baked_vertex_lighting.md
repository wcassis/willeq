# T408 — the zone's baked vertex lighting (B110, approved by Will 2026-09-06 with a pixel re-bless)

Class A, the slot that frees; eqemu lock for the oracle capture (atomic;
must be ABSENT). Read B110, T367's §Review (`review/T367/` — the lamp-spot
day/night table: oracle 17.1x vs fable 47.5x; the baked-colour census:
all 3,830 region meshes carry `colorCount == vertexCount`, mean 44.2/255),
`WldLoader::parseFragment36` (`// Skip colors`), `ZoneGeometry::vertices`,
the three mesh builders (`entity_mesh_build.cpp`, `entity_build_step.cpp`,
the atlased builder), and `zone_shader_sources_gles2.h`
(`vColor = baseLighting * uTintColor * aColor`).

1. Load the colours: `parseFragment36` reads `colorCount` RGBA entries into
   `ZoneGeometry` (a field per vertex); the three builders write them into
   the vertex colour instead of white. Test: qeynos2's loaded vertex colours
   reproduce T367's census (count, mean byte) — pre-fix failing (all white).
2. The lighting equation is the oracle's: EQ's model is that the baked
   colour IS the light (a lamp's pool is in the vertex colours), and the
   day/night ambient scales the whole scene on top. Read the oracle's
   day/night ratios (T367: no-lamp 40.2x, lamp 17.1x) and solve the
   composition (`baked * ambient`, `baked + ambient`, or a clamp) by live A/B
   against the oracle at BOTH spots at 11:00 and 22:00 — the composition
   that reproduces both ratios within the noise floor wins; a formula
   picked by argument is the smell. Weather pinned (T396 log lines),
   heading pinned (T405's rig).
3. The pixel goldens WILL move. Run the gate; write the contact sheet
   (`scripts/ci/visual_gate.py --bless` produces it) into `review/T408/`
   with before/after per zone; Will approved the re-bless FOR THIS UNIT —
   bless in the unit's last commit, with the contact sheet path in the
   commit message, and note the memory baselines if they move too.
4. Tests: the vertex-colour load pin; the composition constant(s) pinned
   with the probe that produced them; T367's `test_zone_light_culling`
   still green.
5. Strike B110; annotate B067's residual note.

## Proof

Build (all trees), `ctest -j6`, the live A/B table at two spots × two
times × two clients, gate re-blessed with the sheet, ARM, file sizes.
Restore server values; `down.sh`; delete the lock.

STOP: no composition reproduces both ratios (report the nearest reach and
the two frames; do not bless a compromise).
