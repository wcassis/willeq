#ifndef EQT_GRAPHICS_DETAIL_TEXTURE_ATLAS_H
#define EQT_GRAPHICS_DETAIL_TEXTURE_ATLAS_H

#include <irrlicht.h>
#include <string>

namespace EQT {
namespace Graphics {
namespace Detail {

// Atlas layout constants
constexpr int ATLAS_SIZE = 512;        // 512x512 atlas (expanded for biome support)
constexpr int TILE_SIZE = 64;          // 64x64 per tile
constexpr int TILES_PER_ROW = 8;       // 8 tiles per row

// Tile indices in atlas (8x8 grid = 64 slots)
// NOTE: Indices match binary storage - do not reorder existing values!
enum class AtlasTile {
    // Row 0: Temperate grass/flowers (original)
    GrassShort = 0,    // Row 0, Col 0
    GrassTall = 1,     // Row 0, Col 1
    Flower1 = 2,       // Row 0, Col 2
    Flower2 = 3,       // Row 0, Col 3
    Rock1 = 4,         // Row 0, Col 4
    Rock2 = 5,         // Row 0, Col 5
    Debris = 6,        // Row 0, Col 6
    Mushroom = 7,      // Row 0, Col 7

    // Row 1: Snow biome (Velious)
    IceCrystal = 8,    // Row 1, Col 0 - Crystalline ice formations
    Snowdrift = 9,     // Row 1, Col 1 - Small snow piles
    DeadGrass = 10,    // Row 1, Col 2 - Brown/dead grass poking through snow
    SnowRock = 11,     // Row 1, Col 3 - Snow-covered rocks
    Icicle = 12,       // Row 1, Col 4 - Hanging icicle formations

    // Row 2: Sand biome (Ro deserts, beaches)
    Shell = 16,        // Row 2, Col 0 - Seashells
    BeachGrass = 17,   // Row 2, Col 1 - Sparse dune grass
    Pebbles = 18,      // Row 2, Col 2 - Small pebbles/stones
    Driftwood = 19,    // Row 2, Col 3 - Beach debris
    Cactus = 20,       // Row 2, Col 4 - Small desert cactus

    // Row 3: Jungle biome (Kunark tropical)
    Fern = 24,         // Row 3, Col 0 - Tropical fern
    TropicalFlower = 25, // Row 3, Col 1 - Colorful jungle flower
    JungleGrass = 26,  // Row 3, Col 2 - Dense tropical grass
    Vine = 27,         // Row 3, Col 3 - Ground vines
    Bamboo = 28,       // Row 3, Col 4 - Small bamboo shoots

    // Row 4: Swamp biome (Innothule, Kunark marshes)
    Cattail = 32,      // Row 4, Col 0 - Cattail reeds
    SwampMushroom = 33, // Row 4, Col 1 - Swamp fungus
    Reed = 34,         // Row 4, Col 2 - Marsh reeds
    LilyPad = 35,      // Row 4, Col 3 - Small lily pad (ground edge)
    SwampGrass = 36,   // Row 4, Col 4 - Soggy marsh grass

    // Row 5: Footprints - Grass/Dirt biome (dark brown-gray)
    FootprintLeftGrass = 40,    // Row 5, Col 0
    FootprintRightGrass = 41,   // Row 5, Col 1
    FootprintLeftDirt = 42,     // Row 5, Col 2
    FootprintRightDirt = 43,    // Row 5, Col 3
    FootprintLeftSand = 44,     // Row 5, Col 4 - Tan color
    FootprintRightSand = 45,    // Row 5, Col 5
    FootprintLeftSnow = 46,     // Row 5, Col 6 - Light blue-gray
    FootprintRightSnow = 47,    // Row 5, Col 7

    // Row 6: Footprints - Swamp/Jungle biome
    FootprintLeftSwamp = 48,    // Row 6, Col 0 - Dark muddy green
    FootprintRightSwamp = 49,   // Row 6, Col 1
    FootprintLeftJungle = 50,   // Row 6, Col 2 - Dark humid soil
    FootprintRightJungle = 51,  // Row 6, Col 3

    // Row 7: Additional grass varieties (non-desert biomes)
    GrassMixed1 = 56,           // Row 7, Col 0 - Short grass with dead blades mixed in
    GrassMixed2 = 57,           // Row 7, Col 1 - Tall grass with dead blades mixed in
    GrassClump = 58,            // Row 7, Col 2 - Dense clump with varied heights
    GrassWispy = 59,            // Row 7, Col 3 - Thin delicate blades
    GrassBroad = 60,            // Row 7, Col 4 - Wide flat leaf blades
    GrassCurved = 61,           // Row 7, Col 5 - Curved/wind-blown tips
    GrassSeedHead = 62,         // Row 7, Col 6 - Grass with seed heads
    GrassBroken = 63            // Row 7, Col 7 - Bent/broken blades
};

// Get UV coordinates for a tile
inline void getTileUV(AtlasTile tile, float& u0, float& v0, float& u1, float& v1) {
    int idx = static_cast<int>(tile);
    int col = idx % TILES_PER_ROW;
    int row = idx / TILES_PER_ROW;

    float tileU = static_cast<float>(TILE_SIZE) / ATLAS_SIZE;
    float tileV = static_cast<float>(TILE_SIZE) / ATLAS_SIZE;

    u0 = col * tileU;
    v0 = row * tileV;
    u1 = u0 + tileU;
    v1 = v0 + tileV;
}

// Generates a procedural texture atlas for detail objects
class DetailTextureAtlas {
public:
    DetailTextureAtlas() = default;
    ~DetailTextureAtlas() = default;

    // Create the atlas texture
    // Returns nullptr on failure
    irr::video::ITexture* createAtlas(irr::video::IVideoDriver* driver);

private:
    // Helper to get tile position from AtlasTile enum
    void getTilePosition(AtlasTile tile, int& startX, int& startY);

    // Draw individual tile types - Temperate (Row 0)
    void drawGrassShort(irr::video::IImage* image, int startX, int startY);
    void drawGrassTall(irr::video::IImage* image, int startX, int startY);
    void drawFlower1(irr::video::IImage* image, int startX, int startY);
    void drawFlower2(irr::video::IImage* image, int startX, int startY);
    void drawRock1(irr::video::IImage* image, int startX, int startY);
    void drawRock2(irr::video::IImage* image, int startX, int startY);
    void drawDebris(irr::video::IImage* image, int startX, int startY);
    void drawMushroom(irr::video::IImage* image, int startX, int startY);

    // Snow biome (Row 1)
    void drawIceCrystal(irr::video::IImage* image, int startX, int startY);
    void drawSnowdrift(irr::video::IImage* image, int startX, int startY);
    void drawDeadGrass(irr::video::IImage* image, int startX, int startY);
    void drawSnowRock(irr::video::IImage* image, int startX, int startY);
    void drawIcicle(irr::video::IImage* image, int startX, int startY);

    // Sand biome (Row 2)
    void drawShell(irr::video::IImage* image, int startX, int startY);
    void drawBeachGrass(irr::video::IImage* image, int startX, int startY);
    void drawPebbles(irr::video::IImage* image, int startX, int startY);
    void drawDriftwood(irr::video::IImage* image, int startX, int startY);
    void drawCactus(irr::video::IImage* image, int startX, int startY);

    // Jungle biome (Row 3)
    void drawFern(irr::video::IImage* image, int startX, int startY);
    void drawTropicalFlower(irr::video::IImage* image, int startX, int startY);
    void drawJungleGrass(irr::video::IImage* image, int startX, int startY);
    void drawVine(irr::video::IImage* image, int startX, int startY);
    void drawBamboo(irr::video::IImage* image, int startX, int startY);

    // Swamp biome (Row 4)
    void drawCattail(irr::video::IImage* image, int startX, int startY);
    void drawSwampMushroom(irr::video::IImage* image, int startX, int startY);
    void drawReed(irr::video::IImage* image, int startX, int startY);
    void drawLilyPad(irr::video::IImage* image, int startX, int startY);
    void drawSwampGrass(irr::video::IImage* image, int startX, int startY);

    // Footprints - biome-specific colors
    void drawFootprintLeft(irr::video::IImage* image, int startX, int startY, int r, int g, int b);
    void drawFootprintRight(irr::video::IImage* image, int startX, int startY, int r, int g, int b);

    // Additional grass varieties (Row 7)
    void drawGrassMixed1(irr::video::IImage* image, int startX, int startY);
    void drawGrassMixed2(irr::video::IImage* image, int startX, int startY);
    void drawGrassClump(irr::video::IImage* image, int startX, int startY);
    void drawGrassWispy(irr::video::IImage* image, int startX, int startY);
    void drawGrassBroad(irr::video::IImage* image, int startX, int startY);
    void drawGrassCurved(irr::video::IImage* image, int startX, int startY);
    void drawGrassSeedHead(irr::video::IImage* image, int startX, int startY);
    void drawGrassBroken(irr::video::IImage* image, int startX, int startY);

    // Helper to set a pixel with bounds checking
    void setPixel(irr::video::IImage* image, int x, int y, irr::video::SColor color);

    // Draw a filled rectangle
    void fillRect(irr::video::IImage* image, int x, int y, int w, int h, irr::video::SColor color);

    // Draw a line
    void drawLine(irr::video::IImage* image, int x0, int y0, int x1, int y1, irr::video::SColor color);

    // Draw a filled circle
    void fillCircle(irr::video::IImage* image, int cx, int cy, int radius, irr::video::SColor color);

    // Draw ellipse
    void fillEllipse(irr::video::IImage* image, int cx, int cy, int rx, int ry, irr::video::SColor color);
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_TEXTURE_ATLAS_H
