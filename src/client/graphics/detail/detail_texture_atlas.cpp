// detail_texture_atlas.cpp -- atlas assembly plus the shared drawing
// primitives every tile painter calls, and the desert-biome row.
// The snow, swamp and cactus rows live in detail_texture_atlas_flora.cpp;
// the footprint and grass rows in detail_texture_atlas_grass.cpp.
// Split per refactor/J28_detail_texture_atlas_split.md -- move-only.
#include "client/graphics/detail/detail_texture_atlas.h"
#include "common/logging.h"
#include <random>
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Detail {

void DetailTextureAtlas::getTilePosition(AtlasTile tile, int& startX, int& startY) {
    int idx = static_cast<int>(tile);
    int col = idx % TILES_PER_ROW;
    int row = idx / TILES_PER_ROW;
    startX = col * TILE_SIZE;
    startY = row * TILE_SIZE;
}

irr::video::ITexture* DetailTextureAtlas::createAtlas(irr::video::IVideoDriver* driver) {
    if (!driver) {
        return nullptr;
    }

    // Create image with alpha channel
    irr::video::IImage* image = driver->createImage(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(ATLAS_SIZE, ATLAS_SIZE));

    if (!image) {
        LOG_ERROR(MOD_GRAPHICS, "DetailTextureAtlas: Failed to create image");
        return nullptr;
    }

    // Fill with transparent black
    image->fill(irr::video::SColor(0, 0, 0, 0));

    int x, y;

    // === Row 0: Temperate biome ===
    getTilePosition(AtlasTile::GrassShort, x, y);
    drawGrassShort(image, x, y);
    getTilePosition(AtlasTile::GrassTall, x, y);
    drawGrassTall(image, x, y);
    getTilePosition(AtlasTile::Flower1, x, y);
    drawFlower1(image, x, y);
    getTilePosition(AtlasTile::Flower2, x, y);
    drawFlower2(image, x, y);
    getTilePosition(AtlasTile::Rock1, x, y);
    drawRock1(image, x, y);
    getTilePosition(AtlasTile::Rock2, x, y);
    drawRock2(image, x, y);
    getTilePosition(AtlasTile::Debris, x, y);
    drawDebris(image, x, y);
    getTilePosition(AtlasTile::Mushroom, x, y);
    drawMushroom(image, x, y);

    // === Row 1: Snow biome (Velious) ===
    getTilePosition(AtlasTile::IceCrystal, x, y);
    drawIceCrystal(image, x, y);
    getTilePosition(AtlasTile::Snowdrift, x, y);
    drawSnowdrift(image, x, y);
    getTilePosition(AtlasTile::DeadGrass, x, y);
    drawDeadGrass(image, x, y);
    getTilePosition(AtlasTile::SnowRock, x, y);
    drawSnowRock(image, x, y);
    getTilePosition(AtlasTile::Icicle, x, y);
    drawIcicle(image, x, y);

    // === Row 2: Sand biome (Ro deserts, beaches) ===
    getTilePosition(AtlasTile::Shell, x, y);
    drawShell(image, x, y);
    getTilePosition(AtlasTile::BeachGrass, x, y);
    drawBeachGrass(image, x, y);
    getTilePosition(AtlasTile::Pebbles, x, y);
    drawPebbles(image, x, y);
    getTilePosition(AtlasTile::Driftwood, x, y);
    drawDriftwood(image, x, y);
    getTilePosition(AtlasTile::Cactus, x, y);
    drawCactus(image, x, y);

    // === Row 3: Jungle biome (Kunark) ===
    getTilePosition(AtlasTile::Fern, x, y);
    drawFern(image, x, y);
    getTilePosition(AtlasTile::TropicalFlower, x, y);
    drawTropicalFlower(image, x, y);
    getTilePosition(AtlasTile::JungleGrass, x, y);
    drawJungleGrass(image, x, y);
    getTilePosition(AtlasTile::Vine, x, y);
    drawVine(image, x, y);
    getTilePosition(AtlasTile::Bamboo, x, y);
    drawBamboo(image, x, y);

    // === Row 4: Swamp biome (Innothule, Kunark marshes) ===
    getTilePosition(AtlasTile::Cattail, x, y);
    drawCattail(image, x, y);
    getTilePosition(AtlasTile::SwampMushroom, x, y);
    drawSwampMushroom(image, x, y);
    getTilePosition(AtlasTile::Reed, x, y);
    drawReed(image, x, y);
    getTilePosition(AtlasTile::LilyPad, x, y);
    drawLilyPad(image, x, y);
    getTilePosition(AtlasTile::SwampGrass, x, y);
    drawSwampGrass(image, x, y);

    // === Row 5: Footprints - biome-specific colors ===
    // Grass - dark green-brown (muddy grass)
    getTilePosition(AtlasTile::FootprintLeftGrass, x, y);
    drawFootprintLeft(image, x, y, 40, 35, 25);
    getTilePosition(AtlasTile::FootprintRightGrass, x, y);
    drawFootprintRight(image, x, y, 40, 35, 25);

    // Dirt - dark brown (earth)
    getTilePosition(AtlasTile::FootprintLeftDirt, x, y);
    drawFootprintLeft(image, x, y, 50, 40, 30);
    getTilePosition(AtlasTile::FootprintRightDirt, x, y);
    drawFootprintRight(image, x, y, 50, 40, 30);

    // Sand - tan impression
    getTilePosition(AtlasTile::FootprintLeftSand, x, y);
    drawFootprintLeft(image, x, y, 140, 120, 80);
    getTilePosition(AtlasTile::FootprintRightSand, x, y);
    drawFootprintRight(image, x, y, 140, 120, 80);

    // Snow - light blue-gray (compressed snow shadow)
    getTilePosition(AtlasTile::FootprintLeftSnow, x, y);
    drawFootprintLeft(image, x, y, 180, 190, 210);
    getTilePosition(AtlasTile::FootprintRightSnow, x, y);
    drawFootprintRight(image, x, y, 180, 190, 210);

    // === Row 6: More footprints ===
    // Swamp - dark muddy green
    getTilePosition(AtlasTile::FootprintLeftSwamp, x, y);
    drawFootprintLeft(image, x, y, 35, 45, 30);
    getTilePosition(AtlasTile::FootprintRightSwamp, x, y);
    drawFootprintRight(image, x, y, 35, 45, 30);

    // Jungle - dark humid soil
    getTilePosition(AtlasTile::FootprintLeftJungle, x, y);
    drawFootprintLeft(image, x, y, 45, 40, 35);
    getTilePosition(AtlasTile::FootprintRightJungle, x, y);
    drawFootprintRight(image, x, y, 45, 40, 35);

    // === Row 7: Additional grass varieties ===
    getTilePosition(AtlasTile::GrassMixed1, x, y);
    drawGrassMixed1(image, x, y);
    getTilePosition(AtlasTile::GrassMixed2, x, y);
    drawGrassMixed2(image, x, y);
    getTilePosition(AtlasTile::GrassClump, x, y);
    drawGrassClump(image, x, y);
    getTilePosition(AtlasTile::GrassWispy, x, y);
    drawGrassWispy(image, x, y);
    getTilePosition(AtlasTile::GrassBroad, x, y);
    drawGrassBroad(image, x, y);
    getTilePosition(AtlasTile::GrassCurved, x, y);
    drawGrassCurved(image, x, y);
    getTilePosition(AtlasTile::GrassSeedHead, x, y);
    drawGrassSeedHead(image, x, y);
    getTilePosition(AtlasTile::GrassBroken, x, y);
    drawGrassBroken(image, x, y);

    // Create texture from image
    irr::video::ITexture* texture = driver->addTexture("detail_atlas", image);
    image->drop();

    if (texture) {
        LOG_INFO(MOD_GRAPHICS, "DetailTextureAtlas: Created {}x{} atlas texture", ATLAS_SIZE, ATLAS_SIZE);
    }

    return texture;
}

void DetailTextureAtlas::setPixel(irr::video::IImage* image, int x, int y, irr::video::SColor color) {
    if (x >= 0 && x < static_cast<int>(image->getDimension().Width) &&
        y >= 0 && y < static_cast<int>(image->getDimension().Height)) {
        image->setPixel(x, y, color);
    }
}

void DetailTextureAtlas::fillRect(irr::video::IImage* image, int x, int y, int w, int h, irr::video::SColor color) {
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            setPixel(image, px, py, color);
        }
    }
}

void DetailTextureAtlas::drawLine(irr::video::IImage* image, int x0, int y0, int x1, int y1, irr::video::SColor color) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        setPixel(image, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

void DetailTextureAtlas::drawGrassShort(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> xDist(4, TILE_SIZE - 5);
    std::uniform_int_distribution<int> heightDist(20, 35);
    std::uniform_int_distribution<int> greenDist(220, 255);
    std::uniform_int_distribution<int> swayDist(-3, 3);

    // Draw several grass blades
    for (int i = 0; i < 12; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);
        int green = greenDist(rng);

        // Draw blade with slight width - light bright green
        irr::video::SColor color(255, 120, green, 50);
        irr::video::SColor colorDark(255, 90, green - 20, 35);

        drawLine(image, baseX, baseY, topX, baseY - height, color);
        drawLine(image, baseX - 1, baseY, topX - 1, baseY - height + 2, colorDark);
    }
}

void DetailTextureAtlas::drawGrassTall(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(23456);
    std::uniform_int_distribution<int> xDist(6, TILE_SIZE - 7);
    std::uniform_int_distribution<int> heightDist(35, 55);
    std::uniform_int_distribution<int> greenDist(160, 240);
    std::uniform_int_distribution<int> swayDist(-5, 5);

    // Draw taller grass blades
    for (int i = 0; i < 10; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);
        int green = greenDist(rng);

        // Brighter green
        irr::video::SColor color(255, 70, green, 25);
        irr::video::SColor colorDark(255, 50, green - 30, 15);

        // Thicker blades
        drawLine(image, baseX, baseY, topX, baseY - height, color);
        drawLine(image, baseX + 1, baseY, topX + 1, baseY - height, color);
        drawLine(image, baseX - 1, baseY, topX - 1, baseY - height + 3, colorDark);
    }
}

void DetailTextureAtlas::drawFlower1(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(34567);
    std::uniform_int_distribution<int> xDist(10, TILE_SIZE - 11);

    // Draw a few flowers with stems
    for (int i = 0; i < 5; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int stemHeight = 25 + (rng() % 15);

        // Green stem
        irr::video::SColor stemColor(255, 50, 130, 40);
        drawLine(image, baseX, baseY, baseX, baseY - stemHeight, stemColor);

        // Flower petals (simple circle-ish)
        int flowerY = baseY - stemHeight;
        irr::video::SColor petalColor(255, 220, 80, 180);  // Pink/purple
        irr::video::SColor centerColor(255, 255, 220, 50); // Yellow center

        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                if (dx * dx + dy * dy <= 16) {
                    setPixel(image, baseX + dx, flowerY + dy, petalColor);
                }
            }
        }
        // Center
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                setPixel(image, baseX + dx, flowerY + dy, centerColor);
            }
        }
    }
}

void DetailTextureAtlas::drawFlower2(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(45678);
    std::uniform_int_distribution<int> xDist(10, TILE_SIZE - 11);

    // Draw different colored flowers
    for (int i = 0; i < 5; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int stemHeight = 20 + (rng() % 20);

        // Green stem
        irr::video::SColor stemColor(255, 60, 140, 50);
        drawLine(image, baseX, baseY, baseX, baseY - stemHeight, stemColor);

        // Flower petals - white/yellow
        int flowerY = baseY - stemHeight;
        irr::video::SColor petalColor(255, 255, 255, 200);  // White
        irr::video::SColor centerColor(255, 255, 200, 50);  // Yellow center

        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                if (dx * dx + dy * dy <= 9) {
                    setPixel(image, baseX + dx, flowerY + dy, petalColor);
                }
            }
        }
        setPixel(image, baseX, flowerY, centerColor);
        setPixel(image, baseX + 1, flowerY, centerColor);
        setPixel(image, baseX, flowerY + 1, centerColor);
    }
}

void DetailTextureAtlas::drawRock1(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(56789);

    // Draw a rocky shape
    int centerX = startX + TILE_SIZE / 2;
    int centerY = startY + TILE_SIZE - 15;

    irr::video::SColor rockColor(255, 120, 115, 105);
    irr::video::SColor rockLight(255, 150, 145, 135);
    irr::video::SColor rockDark(255, 80, 75, 70);

    // Irregular rock shape
    for (int dy = -12; dy <= 8; ++dy) {
        for (int dx = -15; dx <= 15; ++dx) {
            float dist = std::sqrt(dx * dx * 0.7f + dy * dy * 1.3f);
            float noise = (rng() % 100) / 100.0f * 3.0f;
            if (dist + noise < 14) {
                irr::video::SColor c = rockColor;
                if (dy < -3) c = rockLight;
                else if (dy > 3) c = rockDark;
                setPixel(image, centerX + dx, centerY + dy, c);
            }
        }
    }
}

void DetailTextureAtlas::drawRock2(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(67890);

    // Draw a smaller, rounder rock
    int centerX = startX + TILE_SIZE / 2;
    int centerY = startY + TILE_SIZE - 12;

    irr::video::SColor rockColor(255, 140, 130, 115);
    irr::video::SColor rockLight(255, 170, 160, 145);
    irr::video::SColor rockDark(255, 100, 90, 80);

    for (int dy = -10; dy <= 6; ++dy) {
        for (int dx = -12; dx <= 12; ++dx) {
            float dist = std::sqrt(dx * dx + dy * dy * 1.5f);
            float noise = (rng() % 100) / 100.0f * 2.0f;
            if (dist + noise < 11) {
                irr::video::SColor c = rockColor;
                if (dy < -2 && dx < 2) c = rockLight;
                else if (dy > 2) c = rockDark;
                setPixel(image, centerX + dx, centerY + dy, c);
            }
        }
    }
}

void DetailTextureAtlas::drawDebris(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(78901);
    std::uniform_int_distribution<int> xDist(5, TILE_SIZE - 6);
    std::uniform_int_distribution<int> yDist(TILE_SIZE - 20, TILE_SIZE - 5);

    // Draw scattered debris/twigs
    irr::video::SColor twigColor(255, 100, 80, 50);
    irr::video::SColor twigDark(255, 70, 55, 35);

    for (int i = 0; i < 8; ++i) {
        int x = startX + xDist(rng);
        int y = startY + yDist(rng);
        int len = 5 + rng() % 10;
        int angle = rng() % 360;
        float rad = angle * 3.14159f / 180.0f;
        int endX = x + static_cast<int>(std::cos(rad) * len);
        int endY = y + static_cast<int>(std::sin(rad) * len * 0.3f); // Flatten for ground

        drawLine(image, x, y, endX, endY, (i % 2 == 0) ? twigColor : twigDark);
    }

    // Add some leaf-like shapes
    irr::video::SColor leafColor(255, 90, 120, 50);
    for (int i = 0; i < 4; ++i) {
        int x = startX + xDist(rng);
        int y = startY + yDist(rng);
        fillRect(image, x, y, 4, 2, leafColor);
    }
}

void DetailTextureAtlas::drawMushroom(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(89012);
    std::uniform_int_distribution<int> xDist(12, TILE_SIZE - 13);

    // Draw a few mushrooms
    for (int i = 0; i < 4; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 3;
        int stemHeight = 8 + rng() % 8;
        int capRadius = 4 + rng() % 4;

        // Stem
        irr::video::SColor stemColor(255, 220, 210, 190);
        fillRect(image, baseX - 1, baseY - stemHeight, 3, stemHeight, stemColor);

        // Cap
        int capY = baseY - stemHeight - capRadius / 2;
        irr::video::SColor capColor(255, 180, 50, 40);  // Red-brown cap
        irr::video::SColor capLight(255, 220, 80, 60);

        for (int dy = -capRadius; dy <= capRadius / 2; ++dy) {
            for (int dx = -capRadius; dx <= capRadius; ++dx) {
                float dist = std::sqrt(dx * dx + dy * dy * 2.0f);
                if (dist < capRadius) {
                    irr::video::SColor c = (dy < 0) ? capLight : capColor;
                    setPixel(image, baseX + dx, capY + dy, c);
                }
            }
        }

        // White spots on cap
        irr::video::SColor spotColor(255, 255, 250, 240);
        if (capRadius > 4) {
            setPixel(image, baseX - 2, capY - 1, spotColor);
            setPixel(image, baseX + 2, capY, spotColor);
            setPixel(image, baseX, capY - 2, spotColor);
        }
    }
}

void DetailTextureAtlas::fillCircle(irr::video::IImage* image, int cx, int cy, int radius, irr::video::SColor color) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                setPixel(image, cx + dx, cy + dy, color);
            }
        }
    }
}

void DetailTextureAtlas::fillEllipse(irr::video::IImage* image, int cx, int cy, int rx, int ry, irr::video::SColor color) {
    for (int dy = -ry; dy <= ry; ++dy) {
        for (int dx = -rx; dx <= rx; ++dx) {
            float ex = static_cast<float>(dx) / rx;
            float ey = static_cast<float>(dy) / ry;
            if (ex * ex + ey * ey <= 1.0f) {
                setPixel(image, cx + dx, cy + dy, color);
            }
        }
    }
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
