# Surface Map System Expansion Plan

**Date:** 2026-01-20
**Status:** In Progress (Phases 1-4 Complete, Phase 6 Running)
**Scope:** Classic, Kunark, Velious zones

## Overview

Expand the detail system's surface map creation and texture atlas to support all ground texture types across the original game, Kunark, and Velious expansions.

## Current State Analysis

### What Works

1. **Surface Type Detection** - The classifier already handles:
   - `*grass*` → SurfaceType::Grass
   - `*dirt*`, `*mud*`, `*ground*` → SurfaceType::Dirt
   - `*sand*` → SurfaceType::Sand
   - `*snow*`, `*ice*` → SurfaceType::Snow
   - `*water*`, `*lava*` → excluded
   - `*stone*`, `*rock*`, `*floor*`, `*tile*` → SurfaceType::Stone
   - `*brick*`, `*wall*` → SurfaceType::Brick
   - `*wood*` → SurfaceType::Wood

2. **Surface Types Defined** (in detail_types.h):
   - Unknown, Grass, Dirt, Stone, Brick, Wood, Sand, Snow, Water, Lava

3. **Detail Objects Defined** (in detail_manager.cpp):
   | Object | Allowed Surfaces | Notes |
   |--------|------------------|-------|
   | Grass | Grass, Dirt | Green grass blades |
   | TallGrass | Grass | Taller grass |
   | Flower | Grass | Small flowers |
   | Rock | All except Water/Lava | Small stones |
   | Debris | All except Water/Lava | Twigs, leaves |
   | Mushroom | Grass, Dirt | Small mushrooms |

### What's Missing

1. **Surface Types Not Defined:**
   - Jungle (Kunark tropical areas)
   - Swamp (Innothule, Feerrott, Kunark swamps)
   - Rock (natural rocky terrain - distinct from Stone floors)

2. **Detail Objects Not Defined:**
   | Surface | Missing Objects |
   |---------|-----------------|
   | Snow | Ice crystals, snowdrifts, frozen twigs, dead grass |
   | Sand | Shells, driftwood, beach grass, small crabs |
   | Jungle | Ferns, tropical flowers, palm fronds, vines |
   | Swamp | Cattails, lily pads, swamp mushrooms, reeds |
   | Lava | Charred rocks, ash piles, embers (if we want any) |

3. **Atlas Content:**
   - No snow/ice textures
   - No jungle/tropical textures
   - No swamp/wetland textures
   - No beach/sand textures

4. **Texture Classification Gaps:**
   - Kunark prefixes (kur*, cab*, dro*) not handled
   - Velious prefixes (gdr*, vel*, thu*) not handled
   - Jungle textures misclassified as Grass
   - Some snow textures may be missed

## Texture Counts by Expansion

| Expansion | Zones | Unique Textures | Ground Textures |
|-----------|-------|-----------------|-----------------|
| Classic Antonica | 35 | ~1200 | ~150 |
| Classic Faydwer | 13 | ~450 | ~80 |
| Classic Odus | 7 | ~250 | ~40 |
| Classic Planes | 3 | ~50 | ~10 |
| Classic Dungeons | 7 | ~400 | ~60 |
| Kunark | 27 | ~1000 | ~200 |
| Velious | 18 | ~900 | ~180 |
| **Total** | 110 | ~4250 | ~720 |

## Implementation Plan

### Phase 1: Add Missing Surface Types ✅ COMPLETE

**Status:** Completed 2026-01-21

**Files modified:**
- `include/client/graphics/detail/detail_types.h`
- `include/client/graphics/detail/surface_map.h`

**Changes made:**

In `detail_types.h`:
- Added `Jungle = 1 << 9` (Kunark tropical vegetation)
- Added `Swamp = 1 << 10` (Wetlands, marshes)
- Added `Rock = 1 << 11` (Natural rocky terrain)
- Updated `Natural` composite to include Jungle, Swamp, Rock
- Added `Tropical = Jungle | Swamp` composite
- Added `Cold = Snow` composite

In `surface_map.h`:
- Added `Jungle = 10`, `Swamp = 11`, `Rock = 12` to RawSurfaceType enum
- Added backwards-compatibility note for binary map files
- Updated `convertRawType()` to handle new types

**Tasks:**
- [x] Add Jungle, Swamp, Rock to SurfaceType enum
- [x] Add to RawSurfaceType in surface_map.h
- [x] Update convertRawType() mapping
- [x] Update composite masks
- [x] Build verified - no compile errors

### Phase 2: Expand Texture Classifier ✅ COMPLETE

**Status:** Completed 2026-01-21

**Files modified:**
- `src/client/graphics/detail/detail_manager.cpp` (runtime classifier)
- `tools/generate_surface_map.cpp` (map generation classifier)

**Changes made to both classifiers:**

1. **Added SurfaceType enum values to generate_surface_map.cpp:**
   - Jungle = 10, Swamp = 11, Rock = 12 (matching RawSurfaceType)
   - Updated surfaceTypeName() for new types

2. **Added biome-specific detection (before generic grass):**
   - Swamp: swamp, marsh, bog, muck, slime, sludge
   - Jungle: jungle, fern, palm, tropical, ej*, sbjung*
   - Firiona tropical: fir* prefix (excluding "fire")

3. **Expanded Snow/Ice patterns for Velious:**
   - snow, ice, frost, frozen, icsnow
   - Velious prefixes: gdr* (Great Divide), vel* (Velketor), wice*, thu* (Thurgadin)

4. **Added natural Rock detection:**
   - rock, cliff, boulder, mountain, crag
   - Excludes floor/tile indicators (those stay as Stone)

5. **Added Kunark zone prefix handling:**
   - bw* (Burning Woods) → Dirt (volcanic ash)
   - dread*/drd* (Dreadlands) → Rock
   - fob*/bone (Field of Bone) → Dirt

6. **Added startsWith() helper for prefix matching**

7. **Added agua* to water exclusions**

**Tasks:**
- [x] Add Jungle pattern detection (both files)
- [x] Add Swamp pattern detection (both files)
- [x] Add natural Rock pattern detection (both files)
- [x] Expand Snow/Ice patterns for Velious prefixes (both files)
- [x] Add Kunark prefix handling (both files)
- [x] Update `tools/generate_surface_map.cpp` with all new patterns
- [x] Rebuild `generate_surface_map` tool after classifier updates
- [x] Build verified - no compile errors

### Phase 3: Create Detail Object Definitions ✅ COMPLETE

**Status:** Completed 2026-01-21

**Note:** The detail system uses procedural quad geometry (CrossedQuads, FlatGround, SingleQuad orientations), not external mesh files. Detail objects are defined as `DetailType` structs in `detail_manager.cpp` with atlas UV coordinates.

**Files modified:**
- `include/client/graphics/detail/detail_texture_atlas.h` - Expanded atlas and added tile enums
- `src/client/graphics/detail/detail_manager.cpp` - Added DetailType definitions

**Changes made:**

1. **Expanded Atlas (detail_texture_atlas.h):**
   - Changed ATLAS_SIZE from 256 to 512 (8x8 grid = 64 tiles)
   - Added new AtlasTile enum values for each biome:
     - Row 1 (Snow): IceCrystal, Snowdrift, DeadGrass, SnowRock, Icicle
     - Row 2 (Sand): Shell, BeachGrass, Pebbles, Driftwood, Cactus
     - Row 3 (Jungle): Fern, TropicalFlower, JungleGrass, Vine, Bamboo
     - Row 4 (Swamp): Cattail, SwampMushroom, Reed, LilyPad, SwampGrass

2. **Added Snow biome DetailTypes (SurfaceType::Snow):**
   | Type | Category | Size | Density | Wind |
   |------|----------|------|---------|------|
   | ice_crystal | Rocks | 0.6-1.4 | 0.20 | 0.0 |
   | snowdrift | Debris | 1.0-2.5 | 0.35 | 0.0 |
   | dead_grass | Grass | 1.2-2.4 | 0.25 | 0.5 |
   | snow_rock | Rocks | 0.8-1.8 | 0.15 | 0.0 |

3. **Added Sand biome DetailTypes (SurfaceType::Sand):**
   | Type | Category | Size | Density | Wind |
   |------|----------|------|---------|------|
   | shell | Debris | 0.4-1.0 | 0.15 | 0.0 |
   | beach_grass | Grass | 1.0-2.2 | 0.20 | 0.8 |
   | pebbles | Rocks | 0.5-1.2 | 0.18 | 0.0 |
   | driftwood | Debris | 0.8-2.0 | 0.08 | 0.0 |
   | cactus | Plants | 1.0-2.5 | 0.05 | 0.0 |

4. **Added Jungle biome DetailTypes (SurfaceType::Jungle):**
   | Type | Category | Size | Density | Wind |
   |------|----------|------|---------|------|
   | fern | Plants | 1.5-3.5 | 0.40 | 0.6 |
   | tropical_flower | Plants | 1.2-2.8 | 0.25 | 0.5 |
   | jungle_grass | Grass | 1.5-3.0 | 0.50 | 0.8 |
   | vine | Plants | 1.0-2.5 | 0.15 | 0.3 |
   | bamboo | Plants | 2.0-4.0 | 0.10 | 0.4 |

5. **Added Swamp biome DetailTypes (SurfaceType::Swamp):**
   | Type | Category | Size | Density | Wind |
   |------|----------|------|---------|------|
   | cattail | Plants | 2.0-4.5 | 0.30 | 0.5 |
   | swamp_mushroom | Mushrooms | 0.8-2.0 | 0.20 | 0.0 |
   | reed | Grass | 1.5-3.0 | 0.40 | 0.6 |
   | lily_pad | Plants | 1.0-2.5 | 0.15 | 0.0 |
   | swamp_grass | Grass | 1.2-2.5 | 0.35 | 0.7 |

**Tasks:**
- [x] Design detail object specifications for each biome
- [x] Add AtlasTile enum values for all new objects
- [x] Add DetailType definitions with proper surface constraints
- [x] Configure density, scale, wind response for each biome
- [x] Build verified - no compile errors

### Phase 4: Create Atlas Textures ✅ COMPLETE

**Status:** Completed 2026-01-21

**Note:** The atlas is procedurally generated at runtime, not loaded from PNG files. All texture sprites are drawn programmatically via the `DetailTextureAtlas` class.

**Files modified:**
- `include/client/graphics/detail/detail_texture_atlas.h` - Added 20 new drawing function declarations
- `src/client/graphics/detail/detail_texture_atlas.cpp` - Implemented all drawing functions

**Changes made:**

1. **Expanded atlas from 256x256 to 512x512** (8x8 grid = 64 tiles)
   - Added `getTilePosition()` helper to calculate tile positions from enum

2. **Added helper functions:**
   - `fillCircle()` - Draw filled circles
   - `fillEllipse()` - Draw filled ellipses

3. **Implemented Snow biome sprites (5 functions):**
   - `drawIceCrystal()` - Angular ice shards, light blue gradient
   - `drawSnowdrift()` - Domed snow piles, white with blue shadows
   - `drawDeadGrass()` - Brown/tan dead grass blades
   - `drawSnowRock()` - Gray rock with white snow cap
   - `drawIcicle()` - Hanging tapered ice formations

4. **Implemented Sand biome sprites (5 functions):**
   - `drawShell()` - Cream/pink seashells with spiral lines
   - `drawBeachGrass()` - Tan/yellow sparse dune grass
   - `drawPebbles()` - Scattered gray/brown small stones
   - `drawDriftwood()` - Weathered wood pieces with grain
   - `drawCactus()` - Green cacti with spines and arms

5. **Implemented Jungle biome sprites (5 functions):**
   - `drawFern()` - Dark green fronds with leaflets
   - `drawTropicalFlower()` - Colorful star-shaped flowers (red/orange/yellow)
   - `drawJungleGrass()` - Dense bright green grass with drooping tips
   - `drawVine()` - Curving vines with small leaves
   - `drawBamboo()` - Segmented stalks with node rings and leaves

6. **Implemented Swamp biome sprites (5 functions):**
   - `drawCattail()` - Tall reeds with brown fuzzy heads
   - `drawSwampMushroom()` - Brown mushrooms with pale spots
   - `drawReed()` - Olive/green marsh reeds
   - `drawLilyPad()` - Circular pads with V-notch and radial veins
   - `drawSwampGrass()` - Soggy olive/muddy grass with drooping blades

**Color Palettes Used:**

| Biome | Primary Colors | Accent Colors |
|-------|----------------|---------------|
| Snow | White (#FAFCFF), Light Blue (#C8E6FF) | Brown (#A08C64), Gray (#6E7378) |
| Sand | Tan (#BEAf78), Beige (#D6D2AA) | Cream (#FFF0DC), Wood Brown (#967856) |
| Jungle | Dark Green (#286420), Bright Green (#55B43C) | Red (#DC3C50), Orange (#FF8C32), Yellow (#FFDC3C) |
| Swamp | Olive (#5A6937), Brown-Green (#4B552D) | Purple (#966496), Brown (#644628) |

**Tasks:**
- [x] Add drawing function declarations to header
- [x] Implement Snow biome drawing functions
- [x] Implement Sand biome drawing functions
- [x] Implement Jungle biome drawing functions
- [x] Implement Swamp biome drawing functions
- [x] Update createAtlas() to call all new functions
- [x] Build verified - no compile errors

### Phase 5: Register New Detail Object Types ✅ MERGED INTO PHASE 3

**Status:** Completed as part of Phase 3

This phase was merged into Phase 3. All DetailType registrations are now in `createDefaultConfig()` in `detail_manager.cpp`, with proper atlas tile references, surface constraints, and properties.

See Phase 3 for the complete list of registered detail types.

### Phase 6: Generate Surface Maps for All Zones 🔄 IN PROGRESS

**Status:** Batch generation running (started 2026-01-21)

**Existing tool:** `build/bin/generate_surface_map`
**New batch script:** `tools/generate_all_surface_maps.sh` ✅ Created

**Zone Lists (110 total zones):**

**Classic Antonica (35 zones):**
```
qeynos qeynos2 qcat qey2hh1 northqeynos southqeynos
sro nro oasis highkeep highpass blackburrow
eastkarana northkarana southkarana westkarana lakerathe
lavastorm nektulos commons ecommons
freportw freportn freporte
innothule feerrott cazicthule
befallen everfrost permafrost
grobb halas rivervale misty arena
```

**Classic Faydwer (13 zones):**
```
gfaydark lfaydark crushbone
felwithea felwitheb kaladima kaladimb
akanon steamfont butcher kedge mistmoore unrest
```

**Classic Odus (7 zones):**
```
erudnext erudnint erudsxing paineel tox kerraridge stonebrunt
```

**Classic Planes (3 zones):**
```
airplane fearplane hateplane
```

**Classic Dungeons (7 zones):**
```
soldunga soldungb najena beholder guktop gukbottom runnyeye
```

**Kunark (27 zones):**
```
burningwood cabeast cabwest charasis chardok chardokb
citymist dalnir dreadlands droga emeraldjungle
fieldofbone firiona frontiermtns kaesora karnor kurn
lakeofillomen nurga overthere sebilis skyfire
swampofnohope timorous trakanon veeshan warslikswood
```

**Velious (18 zones):**
```
thurgadina thurgadinb kael wakening skyshrine
cobaltscar crystal eastwastes greatdivide iceclad
necropolis sirens sleeper templeveeshan velketor
westwastes frozenshadow growthplane
```

**Batch Generation Script:**

```bash
#!/bin/bash
# tools/generate_all_surface_maps.sh
# Generate surface maps for all Classic, Kunark, and Velious zones

EQ_PATH="${1:-/home/user/projects/claude/EverQuestP1999}"
OUTPUT_DIR="${2:-data/detail/zones}"
TOOL="./build/bin/generate_surface_map"

mkdir -p "$OUTPUT_DIR"

# All zones by expansion
CLASSIC_ANTONICA="qeynos qeynos2 qcat qey2hh1 northqeynos southqeynos sro nro oasis highkeep highpass blackburrow eastkarana northkarana southkarana westkarana lakerathe lavastorm nektulos commons ecommons freportw freportn freporte innothule feerrott cazicthule befallen everfrost permafrost grobb halas rivervale misty arena"

CLASSIC_FAYDWER="gfaydark lfaydark crushbone felwithea felwitheb kaladima kaladimb akanon steamfont butcher kedge mistmoore unrest"

CLASSIC_ODUS="erudnext erudnint erudsxing paineel tox kerraridge stonebrunt"

CLASSIC_PLANES="airplane fearplane hateplane"

CLASSIC_DUNGEONS="soldunga soldungb najena beholder guktop gukbottom runnyeye"

KUNARK="burningwood cabeast cabwest charasis chardok chardokb citymist dalnir dreadlands droga emeraldjungle fieldofbone firiona frontiermtns kaesora karnor kurn lakeofillomen nurga overthere sebilis skyfire swampofnohope timorous trakanon veeshan warslikswood"

VELIOUS="thurgadina thurgadinb kael wakening skyshrine cobaltscar crystal eastwastes greatdivide iceclad necropolis sirens sleeper templeveeshan velketor westwastes frozenshadow growthplane"

ALL_ZONES="$CLASSIC_ANTONICA $CLASSIC_FAYDWER $CLASSIC_ODUS $CLASSIC_PLANES $CLASSIC_DUNGEONS $KUNARK $VELIOUS"

total=0
success=0
failed=0

for zone in $ALL_ZONES; do
    ((total++))
    s3d="$EQ_PATH/${zone}.s3d"
    output="$OUTPUT_DIR/${zone}_surface.map"

    if [[ -f "$s3d" ]]; then
        echo "[$total] Generating: $zone"
        if "$TOOL" "$s3d" "$output" 2>/dev/null; then
            ((success++))
        else
            echo "  FAILED: $zone"
            ((failed++))
        fi
    else
        echo "  SKIPPED (no S3D): $zone"
        ((failed++))
    fi
done

echo ""
echo "=== Generation Complete ==="
echo "Total: $total | Success: $success | Failed: $failed"
```

**Prerequisites:** Phase 1 (surface types) and Phase 2 (classifier patterns) MUST be completed first.
The generation tool must have updated texture classification before generating maps.

**Tasks:**
- [x] Verify `generate_surface_map.cpp` has all new SurfaceTypes from Phase 1
- [x] Verify `generate_surface_map.cpp` has all classifier patterns from Phase 2
- [x] Rebuild `generate_surface_map` tool
- [x] Create `tools/generate_all_surface_maps.sh` batch script
- [ ] Generate maps for all 35 Classic Antonica zones (in progress)
- [ ] Generate maps for all 13 Classic Faydwer zones
- [ ] Generate maps for all 7 Classic Odus zones
- [ ] Generate maps for all 3 Classic Planes zones
- [ ] Generate maps for all 7 Classic Dungeons zones
- [ ] Generate maps for all 27 Kunark zones
- [ ] Generate maps for all 18 Velious zones
- [ ] Validate all 110 maps generated successfully
- [ ] Spot-check coverage (no Unknown surfaces in outdoor areas)

**Notes on batch generation:**
- Running as background task (ID: b258b18)
- Large outdoor zones like qey2hh1 (36M cells) take significantly longer than city zones
- Output stored in `data/detail/zones/<zone>_surface.map`
- Estimated completion: several hours (110 zones × 3-5 min average)

### Phase 7: Zone Override Files

**Directory:** `data/config/zones/<zonename>/`

**Create overrides for edge cases:**

```json
// fearplane/surface_overrides.json
{
    "zone": "fearplane",
    "description": "Plane of Fear - demonic terrain",
    "texture_mappings": {
        "feardirt.bmp": "Rock",
        "fearground.bmp": "Rock",
        "fearblood.bmp": "Swamp"
    },
    "default_outdoor_surface": "Rock",
    "detail_density_multiplier": 0.5
}
```

**Zones needing overrides:**
- [ ] fearplane - demonic terrain
- [ ] hateplane - dark stone
- [ ] airplane - clouds (no detail?)
- [ ] kedge - underwater (no detail)
- [ ] sleeper - crystal/ice variants
- [ ] veeshan - dragon-themed

**Tasks:**
- [ ] Create override template
- [ ] Create overrides for Planes
- [ ] Create overrides for unique dungeons
- [ ] Test override loading

### Phase 8: Testing and Validation

**Test zones by biome:**

| Biome | Test Zones |
|-------|------------|
| Temperate Grass | qeynos2, gfaydark, commons |
| Snow/Ice | greatdivide, eastwastes, velketor |
| Jungle | emeraldjungle, firiona, warslikswood |
| Swamp | innothule, feerrott, swampofnohope |
| Sand/Beach | oasis, timorous, butcher |
| Desert | nro, sro |
| Lava | lavastorm, soldunga |

**Tasks:**
- [ ] Test each biome type visually
- [ ] Verify no detail on water/lava
- [ ] Verify no detail on indoor floors
- [ ] Check performance with full atlas
- [ ] Adjust densities per biome

## File Summary

### New Files
```
data/detail/atlas/snow_atlas.png
data/detail/atlas/sand_atlas.png
data/detail/atlas/jungle_atlas.png
data/detail/atlas/swamp_atlas.png
data/detail/meshes/ice_crystal.obj (or procedural)
data/detail/meshes/fern.obj
data/detail/meshes/cattail.obj
... (additional meshes)
data/config/zones/fearplane/surface_overrides.json
data/config/zones/hateplane/surface_overrides.json
... (additional overrides)
```

### Modified Files
```
include/client/graphics/detail/detail_types.h      # Add surface types
include/client/graphics/detail/surface_map.h       # Add raw types
src/client/graphics/detail/detail_manager.cpp      # Classifier + object types
tools/generate_surface_maps.sh                     # Update for new types
```

### Generated Files (per zone)
```
data/detail/zones/<zonename>_surface.map
```

## Success Criteria

1. All 110 zones have valid surface maps with no Unknown outdoor surfaces
2. Snow zones spawn ice/snow detail objects
3. Jungle zones spawn ferns/tropical detail objects
4. Swamp zones spawn cattails/mushroom detail objects
5. Beach/sand zones spawn shells/beach grass
6. No detail spawns on water, lava, or indoor floors
7. Performance: <1ms per surface lookup, <5ms per chunk generation
8. Visual quality matches biome expectations

## Open Questions

1. Should Plane of Sky have cloud detail? (probably not)
2. Detail in dungeons? (probably not, except natural caves)
3. Underwater zones like Kedge? (definitely not)
4. Memory budget for 4 additional atlas textures?
5. Should we procedurally generate meshes or hand-model them?

## Future Enhancements

### Config-Driven Detail Objects

**User Request:** Move detail object definitions from hardcoded C++ to JSON config files.

**Proposed Implementation:**
- Create `config/detail_types.json` with all DetailType definitions
- Load at runtime instead of hardcoding in `createDefaultConfig()`
- Allows tuning densities, sizes, colors without recompilation
- Enables per-zone overrides via `config/zones/<zone>/detail_types.json`

**Benefits:**
- Runtime customization without rebuilding
- Easier iteration on visual appearance
- Zone-specific detail configurations
- User-configurable detail preferences

**Config Schema (proposed):**
```json
{
  "atlas_size": 512,
  "tile_size": 64,
  "detail_types": [
    {
      "name": "grass",
      "category": "Grass",
      "orientation": "CrossedQuads",
      "atlas_tile": "GrassShort",
      "min_size": 1.0,
      "max_size": 2.4,
      "base_density": 1.0,
      "max_slope": 0.5,
      "wind_response": 1.0,
      "allowed_surfaces": ["Grass", "Dirt"],
      "test_color": [0, 255, 0, 255]
    }
  ]
}
```

**Status:** Deferred to after Phase 8 completion.

---

**Next Step:** Phase 6 - Generate surface maps for all 110 zones using the batch script.
