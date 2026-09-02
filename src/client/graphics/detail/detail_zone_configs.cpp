// Per-zone default detail (foliage) configuration table.
//
// J22: DetailManager::createDefaultConfig() was lifted verbatim out of
// detail_manager.cpp, which was 1,754 lines. The method is a ~548-line
// per-zone data table and accounts for most of that overage; it remains an
// unchanged DetailManager member with an unchanged signature.

#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_texture_atlas.h"

#include <string>

namespace EQT {
namespace Graphics {
namespace Detail {

ZoneDetailConfig DetailManager::createDefaultConfig(const std::string& zoneName) const {
    ZoneDetailConfig config;
    config.zoneName = zoneName;
    config.isOutdoor = true;
    config.chunkSize = 50.0f;
    config.viewDistance = 150.0f;
    config.densityMultiplier = 1.0f;

    // Create detail types with proper UV coordinates for the atlas
    // Atlas is 256x256 with 64x64 tiles (4x4 grid)
    // Row 0: GrassShort, GrassTall, Flower1, Flower2
    // Row 1: Rock1, Rock2, Debris, Mushroom

    // Grass - only on natural grass/dirt surfaces
    DetailType grass;
    grass.name = "grass";
    grass.category = DetailCategory::Grass;
    grass.orientation = DetailOrientation::CrossedQuads;
    grass.minSize = 1.0f;
    grass.maxSize = 2.4f;
    grass.baseDensity = 1.0f;
    grass.maxSlope = 0.5f;
    grass.windResponse = 1.0f;
    grass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                            static_cast<uint32_t>(SurfaceType::Dirt);
    grass.testColor = irr::video::SColor(255, 0, 255, 0);  // Bright green
    // GrassShort: tile (0,0)
    getTileUV(AtlasTile::GrassShort, grass.u0, grass.v0, grass.u1, grass.v1);
    config.detailTypes.push_back(grass);

    // Tall grass - only on grass surfaces
    DetailType tallGrass;
    tallGrass.name = "tall_grass";
    tallGrass.category = DetailCategory::Grass;
    tallGrass.orientation = DetailOrientation::CrossedQuads;
    tallGrass.minSize = 2.0f;
    tallGrass.maxSize = 4.0f;
    tallGrass.baseDensity = 0.3f;
    tallGrass.maxSlope = 0.4f;
    tallGrass.windResponse = 1.0f;
    tallGrass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass);
    tallGrass.testColor = irr::video::SColor(255, 0, 200, 0);  // Green
    // GrassTall: tile (1,0)
    getTileUV(AtlasTile::GrassTall, tallGrass.u0, tallGrass.v0, tallGrass.u1, tallGrass.v1);
    config.detailTypes.push_back(tallGrass);

    // ==========================================
    // Additional grass varieties (Row 7)
    // These add visual variety with dead blades, different shapes
    // ==========================================

    // Mixed grass (short) - has ~20% dead/dying blades intermixed
    DetailType grassMixed1;
    grassMixed1.name = "grass_mixed_short";
    grassMixed1.category = DetailCategory::Grass;
    grassMixed1.orientation = DetailOrientation::CrossedQuads;
    grassMixed1.minSize = 1.0f;
    grassMixed1.maxSize = 2.2f;
    grassMixed1.baseDensity = 0.4f;
    grassMixed1.maxSlope = 0.5f;
    grassMixed1.windResponse = 0.9f;
    grassMixed1.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                                   static_cast<uint32_t>(SurfaceType::Dirt);
    grassMixed1.testColor = irr::video::SColor(255, 100, 180, 60);
    getTileUV(AtlasTile::GrassMixed1, grassMixed1.u0, grassMixed1.v0, grassMixed1.u1, grassMixed1.v1);
    config.detailTypes.push_back(grassMixed1);

    // Mixed grass (tall) - has ~20% dead/dying blades intermixed
    DetailType grassMixed2;
    grassMixed2.name = "grass_mixed_tall";
    grassMixed2.category = DetailCategory::Grass;
    grassMixed2.orientation = DetailOrientation::CrossedQuads;
    grassMixed2.minSize = 2.0f;
    grassMixed2.maxSize = 3.8f;
    grassMixed2.baseDensity = 0.25f;
    grassMixed2.maxSlope = 0.45f;
    grassMixed2.windResponse = 1.0f;
    grassMixed2.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass);
    grassMixed2.testColor = irr::video::SColor(255, 80, 170, 50);
    getTileUV(AtlasTile::GrassMixed2, grassMixed2.u0, grassMixed2.v0, grassMixed2.u1, grassMixed2.v1);
    config.detailTypes.push_back(grassMixed2);

    // Grass clump - dense cluster radiating from center, varied heights
    DetailType grassClump;
    grassClump.name = "grass_clump";
    grassClump.category = DetailCategory::Grass;
    grassClump.orientation = DetailOrientation::CrossedQuads;
    grassClump.minSize = 1.8f;
    grassClump.maxSize = 3.5f;
    grassClump.baseDensity = 0.2f;
    grassClump.maxSlope = 0.4f;
    grassClump.windResponse = 0.85f;
    grassClump.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                                  static_cast<uint32_t>(SurfaceType::Dirt);
    grassClump.testColor = irr::video::SColor(255, 90, 175, 55);
    getTileUV(AtlasTile::GrassClump, grassClump.u0, grassClump.v0, grassClump.u1, grassClump.v1);
    config.detailTypes.push_back(grassClump);

    // Wispy grass - thin, delicate single-pixel blades
    DetailType grassWispy;
    grassWispy.name = "grass_wispy";
    grassWispy.category = DetailCategory::Grass;
    grassWispy.orientation = DetailOrientation::CrossedQuads;
    grassWispy.minSize = 1.2f;
    grassWispy.maxSize = 2.8f;
    grassWispy.baseDensity = 0.35f;
    grassWispy.maxSlope = 0.55f;
    grassWispy.windResponse = 1.2f;  // More responsive - delicate
    grassWispy.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass);
    grassWispy.testColor = irr::video::SColor(255, 115, 200, 70);
    getTileUV(AtlasTile::GrassWispy, grassWispy.u0, grassWispy.v0, grassWispy.u1, grassWispy.v1);
    config.detailTypes.push_back(grassWispy);

    // Broad grass - wide, flat leaf blades (like ornamental grass)
    DetailType grassBroad;
    grassBroad.name = "grass_broad";
    grassBroad.category = DetailCategory::Grass;
    grassBroad.orientation = DetailOrientation::CrossedQuads;
    grassBroad.minSize = 1.5f;
    grassBroad.maxSize = 3.2f;
    grassBroad.baseDensity = 0.15f;
    grassBroad.maxSlope = 0.4f;
    grassBroad.windResponse = 0.7f;  // Less responsive - sturdy
    grassBroad.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                                  static_cast<uint32_t>(SurfaceType::Dirt);
    grassBroad.testColor = irr::video::SColor(255, 75, 155, 45);
    getTileUV(AtlasTile::GrassBroad, grassBroad.u0, grassBroad.v0, grassBroad.u1, grassBroad.v1);
    config.detailTypes.push_back(grassBroad);

    // Curved grass - wind-blown appearance with curved tips
    DetailType grassCurved;
    grassCurved.name = "grass_curved";
    grassCurved.category = DetailCategory::Grass;
    grassCurved.orientation = DetailOrientation::CrossedQuads;
    grassCurved.minSize = 1.6f;
    grassCurved.maxSize = 3.4f;
    grassCurved.baseDensity = 0.3f;
    grassCurved.maxSlope = 0.45f;
    grassCurved.windResponse = 1.1f;
    grassCurved.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass);
    grassCurved.testColor = irr::video::SColor(255, 90, 175, 50);
    getTileUV(AtlasTile::GrassCurved, grassCurved.u0, grassCurved.v0, grassCurved.u1, grassCurved.v1);
    config.detailTypes.push_back(grassCurved);

    // Seed head grass - grass with seed heads at top (wheat-like)
    DetailType grassSeedHead;
    grassSeedHead.name = "grass_seed_head";
    grassSeedHead.category = DetailCategory::Grass;
    grassSeedHead.orientation = DetailOrientation::CrossedQuads;
    grassSeedHead.minSize = 2.2f;
    grassSeedHead.maxSize = 4.2f;
    grassSeedHead.baseDensity = 0.18f;
    grassSeedHead.maxSlope = 0.4f;
    grassSeedHead.windResponse = 0.9f;
    grassSeedHead.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                                     static_cast<uint32_t>(SurfaceType::Dirt);
    grassSeedHead.testColor = irr::video::SColor(255, 180, 165, 100);  // Golden seed heads
    getTileUV(AtlasTile::GrassSeedHead, grassSeedHead.u0, grassSeedHead.v0, grassSeedHead.u1, grassSeedHead.v1);
    config.detailTypes.push_back(grassSeedHead);

    // Broken grass - bent/broken blades (trampled or weather-damaged)
    DetailType grassBroken;
    grassBroken.name = "grass_broken";
    grassBroken.category = DetailCategory::Grass;
    grassBroken.orientation = DetailOrientation::CrossedQuads;
    grassBroken.minSize = 1.2f;
    grassBroken.maxSize = 2.6f;
    grassBroken.baseDensity = 0.12f;  // Less common
    grassBroken.maxSlope = 0.5f;
    grassBroken.windResponse = 0.6f;  // Less responsive - damaged
    grassBroken.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                                   static_cast<uint32_t>(SurfaceType::Dirt);
    grassBroken.testColor = irr::video::SColor(255, 95, 165, 55);
    getTileUV(AtlasTile::GrassBroken, grassBroken.u0, grassBroken.v0, grassBroken.u1, grassBroken.v1);
    config.detailTypes.push_back(grassBroken);

    // Flowers - only on grass surfaces
    DetailType flower;
    flower.name = "flower";
    flower.category = DetailCategory::Plants;
    flower.orientation = DetailOrientation::CrossedQuads;
    flower.minSize = 1.6f;
    flower.maxSize = 3.2f;
    flower.baseDensity = 0.25f;  // Balanced density
    flower.maxSlope = 0.35f;
    flower.windResponse = 0.7f;
    flower.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass);
    flower.testColor = irr::video::SColor(255, 255, 0, 255);  // Magenta
    // Flower1: tile (2,0)
    getTileUV(AtlasTile::Flower1, flower.u0, flower.v0, flower.u1, flower.v1);
    config.detailTypes.push_back(flower);

    // Rocks - allowed on natural AND hard surfaces
    DetailType rock;
    rock.name = "rock";
    rock.category = DetailCategory::Rocks;
    rock.orientation = DetailOrientation::CrossedQuads;  // Use crossed quads for visibility
    rock.minSize = 1.0f;
    rock.maxSize = 2.0f;
    rock.baseDensity = 0.15f;  // Balanced density
    rock.maxSlope = 0.7f;
    rock.windResponse = 0.0f;
    rock.allowedSurfaces = static_cast<uint32_t>(SurfaceType::All) &
                           ~static_cast<uint32_t>(SurfaceType::Water) &
                           ~static_cast<uint32_t>(SurfaceType::Lava);  // Anywhere except water/lava
    rock.testColor = irr::video::SColor(255, 150, 150, 150);  // Gray
    // Rock1: tile (0,1)
    getTileUV(AtlasTile::Rock1, rock.u0, rock.v0, rock.u1, rock.v1);
    config.detailTypes.push_back(rock);

    // Debris - allowed on natural AND hard surfaces (twigs, leaves, etc)
    DetailType debris;
    debris.name = "debris";
    debris.category = DetailCategory::Debris;
    debris.orientation = DetailOrientation::CrossedQuads;
    debris.minSize = 0.8f;
    debris.maxSize = 1.6f;
    debris.baseDensity = 0.12f;  // Balanced density
    debris.maxSlope = 0.6f;
    debris.windResponse = 0.0f;
    debris.allowedSurfaces = static_cast<uint32_t>(SurfaceType::All) &
                             ~static_cast<uint32_t>(SurfaceType::Water) &
                             ~static_cast<uint32_t>(SurfaceType::Lava);  // Anywhere except water/lava
    debris.testColor = irr::video::SColor(255, 200, 150, 100);  // Light brown
    // Debris: tile (2,1)
    getTileUV(AtlasTile::Debris, debris.u0, debris.v0, debris.u1, debris.v1);
    config.detailTypes.push_back(debris);

    // Mushrooms - only on natural grass/dirt surfaces
    DetailType mushroom;
    mushroom.name = "mushroom";
    mushroom.category = DetailCategory::Mushrooms;
    mushroom.orientation = DetailOrientation::CrossedQuads;
    mushroom.minSize = 0.8f;
    mushroom.maxSize = 1.6f;
    mushroom.baseDensity = 0.08f;  // Balanced density
    mushroom.maxSlope = 0.4f;
    mushroom.windResponse = 0.0f;
    mushroom.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                               static_cast<uint32_t>(SurfaceType::Dirt);
    mushroom.testColor = irr::video::SColor(255, 255, 50, 50);  // Red
    // Mushroom: tile (3,1)
    getTileUV(AtlasTile::Mushroom, mushroom.u0, mushroom.v0, mushroom.u1, mushroom.v1);
    config.detailTypes.push_back(mushroom);

    // ==========================================
    // Snow biome (Velious) - SurfaceType::Snow
    // ==========================================

    // Ice crystals - small crystalline ice formations
    DetailType iceCrystal;
    iceCrystal.name = "ice_crystal";
    iceCrystal.category = DetailCategory::Rocks;  // Crystal formations are rock-like
    iceCrystal.orientation = DetailOrientation::CrossedQuads;
    iceCrystal.minSize = 0.6f;
    iceCrystal.maxSize = 1.4f;
    iceCrystal.baseDensity = 0.2f;
    iceCrystal.maxSlope = 0.5f;
    iceCrystal.windResponse = 0.0f;  // Ice doesn't blow in wind
    iceCrystal.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Snow);
    iceCrystal.testColor = irr::video::SColor(255, 180, 220, 255);  // Light blue
    getTileUV(AtlasTile::IceCrystal, iceCrystal.u0, iceCrystal.v0, iceCrystal.u1, iceCrystal.v1);
    config.detailTypes.push_back(iceCrystal);

    // Snowdrifts - small piles of snow
    DetailType snowdrift;
    snowdrift.name = "snowdrift";
    snowdrift.category = DetailCategory::Debris;  // Ground cover like debris
    snowdrift.orientation = DetailOrientation::CrossedQuads;
    snowdrift.minSize = 1.0f;
    snowdrift.maxSize = 2.5f;
    snowdrift.baseDensity = 0.35f;
    snowdrift.maxSlope = 0.4f;
    snowdrift.windResponse = 0.0f;
    snowdrift.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Snow);
    snowdrift.testColor = irr::video::SColor(255, 240, 245, 255);  // White-blue
    getTileUV(AtlasTile::Snowdrift, snowdrift.u0, snowdrift.v0, snowdrift.u1, snowdrift.v1);
    config.detailTypes.push_back(snowdrift);

    // Dead grass - brown/dead grass poking through snow
    DetailType deadGrass;
    deadGrass.name = "dead_grass";
    deadGrass.category = DetailCategory::Grass;
    deadGrass.orientation = DetailOrientation::CrossedQuads;
    deadGrass.minSize = 1.2f;
    deadGrass.maxSize = 2.4f;
    deadGrass.baseDensity = 0.25f;
    deadGrass.maxSlope = 0.45f;
    deadGrass.windResponse = 0.5f;  // Less wind response than healthy grass
    deadGrass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Snow);
    deadGrass.testColor = irr::video::SColor(255, 139, 119, 101);  // Brown
    getTileUV(AtlasTile::DeadGrass, deadGrass.u0, deadGrass.v0, deadGrass.u1, deadGrass.v1);
    config.detailTypes.push_back(deadGrass);

    // Snow-covered rocks
    DetailType snowRock;
    snowRock.name = "snow_rock";
    snowRock.category = DetailCategory::Rocks;
    snowRock.orientation = DetailOrientation::CrossedQuads;
    snowRock.minSize = 0.8f;
    snowRock.maxSize = 1.8f;
    snowRock.baseDensity = 0.15f;
    snowRock.maxSlope = 0.6f;
    snowRock.windResponse = 0.0f;
    snowRock.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Snow);
    snowRock.testColor = irr::video::SColor(255, 200, 200, 210);  // Grayish white
    getTileUV(AtlasTile::SnowRock, snowRock.u0, snowRock.v0, snowRock.u1, snowRock.v1);
    config.detailTypes.push_back(snowRock);

    // ==========================================
    // Sand biome (Ro deserts, beaches) - SurfaceType::Sand
    // ==========================================

    // Seashells - scattered on beaches
    DetailType shell;
    shell.name = "shell";
    shell.category = DetailCategory::Debris;
    shell.orientation = DetailOrientation::CrossedQuads;
    shell.minSize = 0.4f;
    shell.maxSize = 1.0f;
    shell.baseDensity = 0.4f;
    shell.maxSlope = 0.3f;
    shell.windResponse = 0.0f;
    shell.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Sand);
    shell.testColor = irr::video::SColor(255, 245, 235, 220);  // Cream/shell color
    getTileUV(AtlasTile::Shell, shell.u0, shell.v0, shell.u1, shell.v1);
    config.detailTypes.push_back(shell);

    // Beach grass - sparse dune grass
    DetailType beachGrass;
    beachGrass.name = "beach_grass";
    beachGrass.category = DetailCategory::Grass;
    beachGrass.orientation = DetailOrientation::CrossedQuads;
    beachGrass.minSize = 1.0f;
    beachGrass.maxSize = 2.2f;
    beachGrass.baseDensity = 0.5f;  // Sparse but visible in desert/beach
    beachGrass.maxSlope = 0.4f;
    beachGrass.windResponse = 0.8f;
    beachGrass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Sand);
    beachGrass.testColor = irr::video::SColor(255, 180, 180, 120);  // Tan/yellow
    getTileUV(AtlasTile::BeachGrass, beachGrass.u0, beachGrass.v0, beachGrass.u1, beachGrass.v1);
    config.detailTypes.push_back(beachGrass);

    // Pebbles - small stones on sand
    DetailType pebbles;
    pebbles.name = "pebbles";
    pebbles.category = DetailCategory::Rocks;
    pebbles.orientation = DetailOrientation::CrossedQuads;
    pebbles.minSize = 0.5f;
    pebbles.maxSize = 1.2f;
    pebbles.baseDensity = 0.4f;
    pebbles.maxSlope = 0.5f;
    pebbles.windResponse = 0.0f;
    pebbles.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Sand);
    pebbles.testColor = irr::video::SColor(255, 160, 150, 140);  // Gray-brown
    getTileUV(AtlasTile::Pebbles, pebbles.u0, pebbles.v0, pebbles.u1, pebbles.v1);
    config.detailTypes.push_back(pebbles);

    // Driftwood - beach debris
    DetailType driftwood;
    driftwood.name = "driftwood";
    driftwood.category = DetailCategory::Debris;
    driftwood.orientation = DetailOrientation::CrossedQuads;
    driftwood.minSize = 0.8f;
    driftwood.maxSize = 2.0f;
    driftwood.baseDensity = 0.3f;  // Sparse but visible
    driftwood.maxSlope = 0.35f;
    driftwood.windResponse = 0.0f;
    driftwood.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Sand);
    driftwood.testColor = irr::video::SColor(255, 139, 119, 101);  // Brown wood
    getTileUV(AtlasTile::Driftwood, driftwood.u0, driftwood.v0, driftwood.u1, driftwood.v1);
    config.detailTypes.push_back(driftwood);

    // Small cactus - desert vegetation
    DetailType cactus;
    cactus.name = "cactus";
    cactus.category = DetailCategory::Plants;
    cactus.orientation = DetailOrientation::CrossedQuads;
    cactus.minSize = 1.0f;
    cactus.maxSize = 2.5f;
    cactus.baseDensity = 0.25f;  // Sparse but visible
    cactus.maxSlope = 0.3f;
    cactus.windResponse = 0.0f;  // Cacti don't sway
    cactus.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Sand);
    cactus.testColor = irr::video::SColor(255, 100, 140, 80);  // Desert green
    getTileUV(AtlasTile::Cactus, cactus.u0, cactus.v0, cactus.u1, cactus.v1);
    config.detailTypes.push_back(cactus);

    // ==========================================
    // Jungle biome (Kunark tropical) - SurfaceType::Jungle
    // ==========================================

    // Tropical fern - large leafy fern
    DetailType fern;
    fern.name = "fern";
    fern.category = DetailCategory::Plants;
    fern.orientation = DetailOrientation::CrossedQuads;
    fern.minSize = 1.5f;
    fern.maxSize = 3.5f;
    fern.baseDensity = 0.4f;  // Dense tropical vegetation
    fern.maxSlope = 0.4f;
    fern.windResponse = 0.6f;
    fern.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Jungle);
    fern.testColor = irr::video::SColor(255, 60, 140, 60);  // Dark green
    getTileUV(AtlasTile::Fern, fern.u0, fern.v0, fern.u1, fern.v1);
    config.detailTypes.push_back(fern);

    // Tropical flower - colorful jungle flower
    DetailType tropicalFlower;
    tropicalFlower.name = "tropical_flower";
    tropicalFlower.category = DetailCategory::Plants;
    tropicalFlower.orientation = DetailOrientation::CrossedQuads;
    tropicalFlower.minSize = 1.2f;
    tropicalFlower.maxSize = 2.8f;
    tropicalFlower.baseDensity = 0.25f;
    tropicalFlower.maxSlope = 0.35f;
    tropicalFlower.windResponse = 0.5f;
    tropicalFlower.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Jungle);
    tropicalFlower.testColor = irr::video::SColor(255, 255, 100, 150);  // Pink/red
    getTileUV(AtlasTile::TropicalFlower, tropicalFlower.u0, tropicalFlower.v0, tropicalFlower.u1, tropicalFlower.v1);
    config.detailTypes.push_back(tropicalFlower);

    // Dense tropical grass
    DetailType jungleGrass;
    jungleGrass.name = "jungle_grass";
    jungleGrass.category = DetailCategory::Grass;
    jungleGrass.orientation = DetailOrientation::CrossedQuads;
    jungleGrass.minSize = 1.5f;
    jungleGrass.maxSize = 3.0f;
    jungleGrass.baseDensity = 0.5f;  // Very dense
    jungleGrass.maxSlope = 0.45f;
    jungleGrass.windResponse = 0.8f;
    jungleGrass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Jungle);
    jungleGrass.testColor = irr::video::SColor(255, 80, 160, 40);  // Bright jungle green
    getTileUV(AtlasTile::JungleGrass, jungleGrass.u0, jungleGrass.v0, jungleGrass.u1, jungleGrass.v1);
    config.detailTypes.push_back(jungleGrass);

    // Ground vines
    DetailType vine;
    vine.name = "vine";
    vine.category = DetailCategory::Plants;
    vine.orientation = DetailOrientation::CrossedQuads;
    vine.minSize = 1.0f;
    vine.maxSize = 2.5f;
    vine.baseDensity = 0.15f;
    vine.maxSlope = 0.5f;
    vine.windResponse = 0.3f;
    vine.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Jungle);
    vine.testColor = irr::video::SColor(255, 50, 120, 50);  // Dark green
    getTileUV(AtlasTile::Vine, vine.u0, vine.v0, vine.u1, vine.v1);
    config.detailTypes.push_back(vine);

    // Small bamboo shoots
    DetailType bamboo;
    bamboo.name = "bamboo";
    bamboo.category = DetailCategory::Plants;
    bamboo.orientation = DetailOrientation::CrossedQuads;
    bamboo.minSize = 2.0f;
    bamboo.maxSize = 4.0f;
    bamboo.baseDensity = 0.1f;
    bamboo.maxSlope = 0.3f;
    bamboo.windResponse = 0.4f;
    bamboo.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Jungle);
    bamboo.testColor = irr::video::SColor(255, 140, 180, 80);  // Light green-yellow
    getTileUV(AtlasTile::Bamboo, bamboo.u0, bamboo.v0, bamboo.u1, bamboo.v1);
    config.detailTypes.push_back(bamboo);

    // ==========================================
    // Swamp biome (Innothule, Kunark marshes) - SurfaceType::Swamp
    // ==========================================

    // Cattails - tall marsh reeds with brown tops
    DetailType cattail;
    cattail.name = "cattail";
    cattail.category = DetailCategory::Plants;
    cattail.orientation = DetailOrientation::CrossedQuads;
    cattail.minSize = 2.0f;
    cattail.maxSize = 4.5f;
    cattail.baseDensity = 0.3f;
    cattail.maxSlope = 0.3f;
    cattail.windResponse = 0.5f;
    cattail.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Swamp);
    cattail.testColor = irr::video::SColor(255, 100, 140, 80);  // Marsh green
    getTileUV(AtlasTile::Cattail, cattail.u0, cattail.v0, cattail.u1, cattail.v1);
    config.detailTypes.push_back(cattail);

    // Swamp mushrooms - fungi growing in wet areas
    DetailType swampMushroom;
    swampMushroom.name = "swamp_mushroom";
    swampMushroom.category = DetailCategory::Mushrooms;
    swampMushroom.orientation = DetailOrientation::CrossedQuads;
    swampMushroom.minSize = 0.8f;
    swampMushroom.maxSize = 2.0f;
    swampMushroom.baseDensity = 0.2f;
    swampMushroom.maxSlope = 0.4f;
    swampMushroom.windResponse = 0.0f;
    swampMushroom.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Swamp);
    swampMushroom.testColor = irr::video::SColor(255, 160, 120, 100);  // Brownish
    getTileUV(AtlasTile::SwampMushroom, swampMushroom.u0, swampMushroom.v0, swampMushroom.u1, swampMushroom.v1);
    config.detailTypes.push_back(swampMushroom);

    // Marsh reeds - shorter than cattails
    DetailType reed;
    reed.name = "reed";
    reed.category = DetailCategory::Grass;
    reed.orientation = DetailOrientation::CrossedQuads;
    reed.minSize = 1.5f;
    reed.maxSize = 3.0f;
    reed.baseDensity = 0.4f;
    reed.maxSlope = 0.35f;
    reed.windResponse = 0.6f;
    reed.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Swamp);
    reed.testColor = irr::video::SColor(255, 90, 130, 70);  // Dark swamp green
    getTileUV(AtlasTile::Reed, reed.u0, reed.v0, reed.u1, reed.v1);
    config.detailTypes.push_back(reed);

    // Lily pads at water edges (for ground near swamp water)
    DetailType lilyPad;
    lilyPad.name = "lily_pad";
    lilyPad.category = DetailCategory::Plants;
    lilyPad.orientation = DetailOrientation::FlatGround;  // Flat on ground
    lilyPad.minSize = 1.0f;
    lilyPad.maxSize = 2.5f;
    lilyPad.baseDensity = 0.15f;
    lilyPad.maxSlope = 0.15f;  // Only on very flat ground
    lilyPad.windResponse = 0.0f;
    lilyPad.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Swamp);
    lilyPad.testColor = irr::video::SColor(255, 60, 120, 60);  // Green
    getTileUV(AtlasTile::LilyPad, lilyPad.u0, lilyPad.v0, lilyPad.u1, lilyPad.v1);
    config.detailTypes.push_back(lilyPad);

    // Soggy marsh grass
    DetailType swampGrass;
    swampGrass.name = "swamp_grass";
    swampGrass.category = DetailCategory::Grass;
    swampGrass.orientation = DetailOrientation::CrossedQuads;
    swampGrass.minSize = 1.2f;
    swampGrass.maxSize = 2.5f;
    swampGrass.baseDensity = 0.35f;
    swampGrass.maxSlope = 0.4f;
    swampGrass.windResponse = 0.7f;
    swampGrass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Swamp);
    swampGrass.testColor = irr::video::SColor(255, 80, 110, 60);  // Dull swamp green
    getTileUV(AtlasTile::SwampGrass, swampGrass.u0, swampGrass.v0, swampGrass.u1, swampGrass.v1);
    config.detailTypes.push_back(swampGrass);

    return config;
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
