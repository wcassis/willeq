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

---

## Review (T408, slot2, branch `task/T408-baked-vertex-lighting`, base `45d30d57`)

Full evidence, every frame and every log: **`/mnt/projects/fable/review/T408/`**
(start at its `README.md`; the sheet for Will is
`review/T408/contact/T408_composition_contact_sheet.png`).

### Verdict against the plan

| step | result |
|---|---|
| 1. load the colours | **DONE** — `parseFragment36` reads the array; census pinned, pre-fix failing |
| 2. solve the composition by live A/B | **STOP FIRED** — no composition reproduces the oracle's two ratios, and "off" is the best fit in the family |
| 3. re-bless the pixel goldens | **NOT USED** — no pixel golden moved; three memory baselines did, and were blessed |
| 4. tests | **DONE** — 3 cases, 2 of them failing pre-fix |
| 5. strike B110 / annotate B067 | **DONE** — B110 answered with two corrections, B067 and B280 annotated, B325/B326 raised |

### The STOP, and the number that fires it

The plan's STOP is *"no composition reproduces both ratios"*. It fired, with a
mechanism rather than a shrug.

Measured at two spots × two hours × two clients, weather and heading pinned, on
a **geometry-only** crop (`560x110+200+350` — T361/T405's `700x420+40+40` world
crop is about half **sky** at both spots, and no zone-lighting term touches the
sky), on the invariant

    L = (day/night)_lamp-free / (day/night)_lamp

which divides out each spot's composition and is immune to a **uniform** error
in a client's night ambient:

| candidate | L | vs oracle **2.281** |
|---|---|---|
| **off (committed)** | **2.476** | **+8.5%** |
| additive gain 0.25 / 0.50 / 1.00 | 2.826 / 3.096 / 3.622 | +24% / +36% / +59% |
| multiplicative | 0.655 | −71% |
| max gain 0.50 / 1.00 | 3.461 / 4.683 | +52% / +105% |

L is monotone in gain above the oracle's value, so **no gain improves the fit**.
The reason is the residual's shape: fable's night needs **×2.27** at the
lamp-free spot and **×2.09** at the lamp spot — the same factor, a **uniform**
deficit (**B280**). A baked term is lamp-shaped by construction (×1.03 in the
open, ×2.48 by a lamp at gain 1.0) and cannot supply it. The baked-alpha sky
mask was measured too and is null (+0.1% / −0.2% by day).

Oracle target from two independent sessions agreeing to **0.5%**; fable's night
frames reproduce across sessions at **0.045%** and **2.63%** RMSE (T386's bar
2.7%). One block — `lamp3`'s day — moved composition mid-block and is reported
discarded, not hidden.

### Two corrections to the record

1. **T367's census was 2×.** Through the production loader qeynos2 has **1,915**
   region meshes and **36,218** coloured vertices, not 3,830 / 72,436. 1,915 is
   the number `wld_loader.h`'s own `ResolvedMaterialList` comment has carried
   since batch J. The distribution is otherwise exactly as T367 described.
2. **B110's premise is refuted, not its observation.** The baked colours are
   real, they carry the oracle's lamp chromaticity (R≈G, B half), and fable was
   throwing them away. They are simply not the term that separates the two
   clients at these spots.

### Deviations, flagged

* **The plan named the wrong builders.** It says "the three mesh builders
  (`entity_mesh_build.cpp`, `entity_build_step.cpp`, the atlased builder)".
  Those are the **entity** builders and must not get the zone's baked light;
  character WLD meshes carry **no** colour array at all (measured: the memory
  baselines moved by exactly one empty `std::vector` header per geometry). The
  zone builders are in `zone_geometry.cpp` / `zone_geometry_atlased.cpp`, and
  **three of the four have zero callers** — boarded as **B326**. Only the
  reached one, `buildTexturedMesh`, was wired.
* **A runtime probe was added** (`/bakedlight`) rather than one build per
  candidate. T405 spent six builds and six sessions solving four constants;
  this swept eight candidates × two hours × two spots in four sessions.
* **The first probe pass was wrong and was re-run.** With the colours loaded,
  "mode 0" silently became the *multiplicative* composition, because the
  pre-T408 equation already multiplies by `aColor`. Mode 0 now replaces a
  baked-carrying mesh's `aColor` with white and its alpha with 1.0, which is
  what makes it byte-for-byte the pre-T408 equation; that neutrality is pinned.
* **The colours ship with no consumer but the probe** — deliberate, measured,
  and boarded as **B325** with both ways out named.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest -j6` | **4,235 of 4,237**; the two reds are the protocol's documented `-j6` load artefacts (`HeadlessShutdownTest`, `visual_gate`) and both pass standalone |
| `visual_gate` standalone | **PASS**, **no image golden moved**; three `characterModelsBytes` baselines blessed (+6,144 / +6,288 / +6,216 = 24 bytes per character geometry) |
| `build-gles2-x86.sh` | **exit 0** |
| `build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh fable` | **47 checked, 47 ok** |

### Tests

| test | pre-fix | post-fix |
|---|---|---|
| `BakedVertexLightingTest.Qeynos2RegionMeshesCarryTheirBakedColours` | **FAIL** (0 meshes with colours) | **PASS** |
| `BakedLightCompositionTest.EveryZoneVertexShaderCarriesTheBakedUniformsAndIsNeutralWhenOff` | **FAIL** (0 of 6 on every clause) | **PASS** |
| `BakedLightCompositionTest.TheCommittedCompositionIsTheOneTheLiveAbSolved` | n/a | **PASS** |

### Server

Snapshot taken on arrival, restored with the eqemu-server container **stopped**
and only mariadb up, verified by a full `diff` of the before/after dumps (not a
spot-check): `character_data` all 18 rows **IDENTICAL**, `zone` qeynos2
weather columns **IDENTICAL**, `eqtime` **IDENTICAL**, `character_corpses`
**IDENTICAL**. `down.sh` exit 0; `/mnt/projects/fable/.eqemu-lock` deleted.
