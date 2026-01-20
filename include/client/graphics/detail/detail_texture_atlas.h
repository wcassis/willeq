#ifndef EQT_GRAPHICS_DETAIL_TEXTURE_ATLAS_H
#define EQT_GRAPHICS_DETAIL_TEXTURE_ATLAS_H

#include <irrlicht.h>
#include <string>

namespace EQT {
namespace Graphics {
namespace Detail {

// Atlas layout constants
constexpr int ATLAS_SIZE = 256;        // 256x256 atlas
constexpr int TILE_SIZE = 64;          // 64x64 per tile
constexpr int TILES_PER_ROW = 4;       // 4 tiles per row

// Tile indices in atlas
enum class AtlasTile {
    GrassShort = 0,    // Row 0, Col 0
    GrassTall = 1,     // Row 0, Col 1
    Flower1 = 2,       // Row 0, Col 2
    Flower2 = 3,       // Row 0, Col 3
    Rock1 = 4,         // Row 1, Col 0
    Rock2 = 5,         // Row 1, Col 1
    Debris = 6,        // Row 1, Col 2
    Mushroom = 7       // Row 1, Col 3
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
    // Draw individual tile types
    void drawGrassShort(irr::video::IImage* image, int startX, int startY);
    void drawGrassTall(irr::video::IImage* image, int startX, int startY);
    void drawFlower1(irr::video::IImage* image, int startX, int startY);
    void drawFlower2(irr::video::IImage* image, int startX, int startY);
    void drawRock1(irr::video::IImage* image, int startX, int startY);
    void drawRock2(irr::video::IImage* image, int startX, int startY);
    void drawDebris(irr::video::IImage* image, int startX, int startY);
    void drawMushroom(irr::video::IImage* image, int startX, int startY);

    // Helper to set a pixel with bounds checking
    void setPixel(irr::video::IImage* image, int x, int y, irr::video::SColor color);

    // Draw a filled rectangle
    void fillRect(irr::video::IImage* image, int x, int y, int w, int h, irr::video::SColor color);

    // Draw a line
    void drawLine(irr::video::IImage* image, int x0, int y0, int x1, int y1, irr::video::SColor color);
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_TEXTURE_ATLAS_H
