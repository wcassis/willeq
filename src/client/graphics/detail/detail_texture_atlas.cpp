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

    // Save atlas to file for debugging
    if (driver->writeImageToFile(image, "detail_atlas_debug.png")) {
        LOG_INFO(MOD_GRAPHICS, "DetailTextureAtlas: Saved debug image to detail_atlas_debug.png");
    }

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

// ============================================================
// Snow biome (Velious) - Row 1
// ============================================================

void DetailTextureAtlas::drawIceCrystal(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(11111);
    std::uniform_int_distribution<int> xDist(10, TILE_SIZE - 11);

    irr::video::SColor iceLight(255, 200, 230, 255);   // Light blue-white
    irr::video::SColor iceMid(255, 150, 200, 245);     // Medium blue
    irr::video::SColor iceDark(255, 100, 160, 220);    // Darker blue

    // Draw several ice crystal shards
    for (int i = 0; i < 6; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 5;
        int height = 15 + rng() % 25;

        // Angular crystal shape - main spike
        for (int h = 0; h < height; ++h) {
            int width = std::max(1, 4 - h / 8);
            float t = static_cast<float>(h) / height;
            irr::video::SColor c = (t < 0.3f) ? iceDark : (t < 0.7f) ? iceMid : iceLight;

            for (int w = -width / 2; w <= width / 2; ++w) {
                setPixel(image, baseX + w, baseY - h, c);
            }
        }

        // Add small facets/highlights
        if (height > 20) {
            setPixel(image, baseX + 1, baseY - height / 2, iceLight);
            setPixel(image, baseX - 1, baseY - height / 3, iceLight);
        }
    }
}

void DetailTextureAtlas::drawSnowdrift(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(22222);

    irr::video::SColor snowWhite(255, 250, 252, 255);
    irr::video::SColor snowLight(255, 235, 240, 250);
    irr::video::SColor snowShade(255, 210, 220, 235);

    // Draw mounded snow piles
    for (int i = 0; i < 4; ++i) {
        int centerX = startX + 10 + (i * 14) + (rng() % 6);
        int centerY = startY + TILE_SIZE - 8;
        int width = 10 + rng() % 8;
        int height = 6 + rng() % 6;

        // Dome shape
        for (int dy = -height; dy <= 0; ++dy) {
            float yRatio = static_cast<float>(-dy) / height;
            int rowWidth = static_cast<int>(width * std::sqrt(1.0f - yRatio * yRatio));

            for (int dx = -rowWidth; dx <= rowWidth; ++dx) {
                irr::video::SColor c = snowWhite;
                if (dy > -height / 3) c = snowShade;
                else if (dx > rowWidth / 2) c = snowLight;
                setPixel(image, centerX + dx, centerY + dy, c);
            }
        }
    }
}

void DetailTextureAtlas::drawDeadGrass(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(33333);
    std::uniform_int_distribution<int> xDist(4, TILE_SIZE - 5);
    std::uniform_int_distribution<int> heightDist(15, 35);
    std::uniform_int_distribution<int> swayDist(-4, 4);

    irr::video::SColor brownLight(255, 160, 140, 100);
    irr::video::SColor brownMid(255, 130, 110, 75);
    irr::video::SColor brownDark(255, 100, 80, 55);

    // Draw dead/frozen grass blades
    for (int i = 0; i < 14; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        irr::video::SColor color = (i % 3 == 0) ? brownLight : (i % 3 == 1) ? brownMid : brownDark;
        drawLine(image, baseX, baseY, topX, baseY - height, color);

        // Some blades are bent/broken
        if (rng() % 3 == 0 && height > 20) {
            int midY = baseY - height / 2;
            int endX = topX + (rng() % 8) - 4;
            drawLine(image, topX, baseY - height, endX, midY, color);
        }
    }
}

void DetailTextureAtlas::drawSnowRock(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(44444);

    int centerX = startX + TILE_SIZE / 2;
    int centerY = startY + TILE_SIZE - 14;

    irr::video::SColor rockGray(255, 110, 115, 120);
    irr::video::SColor rockDark(255, 80, 85, 90);
    irr::video::SColor snowCap(255, 245, 248, 255);
    irr::video::SColor snowShade(255, 220, 225, 235);

    // Draw rock body
    for (int dy = -8; dy <= 8; ++dy) {
        for (int dx = -12; dx <= 12; ++dx) {
            float dist = std::sqrt(dx * dx * 0.8f + dy * dy * 1.4f);
            float noise = (rng() % 100) / 100.0f * 2.5f;
            if (dist + noise < 11) {
                irr::video::SColor c = (dy < 0) ? rockGray : rockDark;
                setPixel(image, centerX + dx, centerY + dy, c);
            }
        }
    }

    // Snow cap on top
    for (int dy = -10; dy <= -3; ++dy) {
        for (int dx = -10; dx <= 10; ++dx) {
            float dist = std::sqrt(dx * dx + (dy + 6) * (dy + 6) * 2.0f);
            if (dist < 9) {
                irr::video::SColor c = (dx < 0) ? snowCap : snowShade;
                setPixel(image, centerX + dx, centerY + dy, c);
            }
        }
    }
}

void DetailTextureAtlas::drawIcicle(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(55555);
    std::uniform_int_distribution<int> xDist(8, TILE_SIZE - 9);

    irr::video::SColor iceTop(255, 220, 240, 255);
    irr::video::SColor iceMid(255, 180, 210, 250);
    irr::video::SColor iceTip(255, 140, 180, 240);

    // Draw hanging icicles
    for (int i = 0; i < 7; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + 5;
        int length = 20 + rng() % 30;

        // Tapered icicle shape
        for (int h = 0; h < length; ++h) {
            float t = static_cast<float>(h) / length;
            int width = std::max(1, static_cast<int>(3 * (1.0f - t)));

            irr::video::SColor c = (t < 0.3f) ? iceTop : (t < 0.7f) ? iceMid : iceTip;

            for (int w = -width; w <= width; ++w) {
                setPixel(image, baseX + w, baseY + h, c);
            }
        }
    }
}

// ============================================================
// Sand biome (Ro deserts, beaches) - Row 2
// ============================================================

void DetailTextureAtlas::drawShell(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(66666);
    std::uniform_int_distribution<int> xDist(8, TILE_SIZE - 12);
    std::uniform_int_distribution<int> yDist(TILE_SIZE - 20, TILE_SIZE - 8);

    irr::video::SColor shellWhite(255, 255, 250, 240);
    irr::video::SColor shellPink(255, 255, 220, 210);
    irr::video::SColor shellTan(255, 230, 210, 180);

    // Draw several seashells
    for (int i = 0; i < 5; ++i) {
        int cx = startX + xDist(rng);
        int cy = startY + yDist(rng);
        int rx = 4 + rng() % 4;
        int ry = 3 + rng() % 3;

        irr::video::SColor baseColor = (i % 3 == 0) ? shellWhite : (i % 3 == 1) ? shellPink : shellTan;

        // Shell body (elliptical)
        fillEllipse(image, cx, cy, rx, ry, baseColor);

        // Spiral lines
        for (int r = 1; r < rx; r += 2) {
            irr::video::SColor lineColor(255, baseColor.getRed() - 30, baseColor.getGreen() - 30, baseColor.getBlue() - 20);
            for (float a = 0; a < 3.14f; a += 0.3f) {
                int px = cx + static_cast<int>(r * std::cos(a));
                int py = cy - static_cast<int>((r * 0.6f) * std::sin(a));
                setPixel(image, px, py, lineColor);
            }
        }
    }
}

void DetailTextureAtlas::drawBeachGrass(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(77777);
    std::uniform_int_distribution<int> xDist(5, TILE_SIZE - 6);
    std::uniform_int_distribution<int> heightDist(20, 40);
    std::uniform_int_distribution<int> swayDist(-5, 5);

    irr::video::SColor grassTan(255, 190, 175, 120);
    irr::video::SColor grassYellow(255, 210, 195, 130);
    irr::video::SColor grassBrown(255, 160, 140, 95);

    // Draw sparse dune grass
    for (int i = 0; i < 10; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        irr::video::SColor color = (i % 3 == 0) ? grassTan : (i % 3 == 1) ? grassYellow : grassBrown;
        drawLine(image, baseX, baseY, topX, baseY - height, color);
        drawLine(image, baseX + 1, baseY, topX + 1, baseY - height + 2, color);
    }
}

void DetailTextureAtlas::drawPebbles(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(88888);
    std::uniform_int_distribution<int> xDist(5, TILE_SIZE - 6);
    std::uniform_int_distribution<int> yDist(TILE_SIZE - 18, TILE_SIZE - 4);

    irr::video::SColor pebbleGray(255, 160, 155, 150);
    irr::video::SColor pebbleBrown(255, 150, 130, 110);
    irr::video::SColor pebbleTan(255, 180, 170, 150);
    irr::video::SColor pebbleDark(255, 120, 115, 110);

    // Draw scattered pebbles
    for (int i = 0; i < 15; ++i) {
        int cx = startX + xDist(rng);
        int cy = startY + yDist(rng);
        int radius = 2 + rng() % 3;

        irr::video::SColor baseColor;
        switch (i % 4) {
            case 0: baseColor = pebbleGray; break;
            case 1: baseColor = pebbleBrown; break;
            case 2: baseColor = pebbleTan; break;
            default: baseColor = pebbleDark; break;
        }

        fillCircle(image, cx, cy, radius, baseColor);

        // Highlight
        if (radius > 2) {
            irr::video::SColor highlight(255,
                std::min(255u, baseColor.getRed() + 40),
                std::min(255u, baseColor.getGreen() + 40),
                std::min(255u, baseColor.getBlue() + 40));
            setPixel(image, cx - 1, cy - 1, highlight);
        }
    }
}

void DetailTextureAtlas::drawDriftwood(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(99999);
    std::uniform_int_distribution<int> xDist(8, TILE_SIZE - 20);
    std::uniform_int_distribution<int> yDist(TILE_SIZE - 15, TILE_SIZE - 5);

    irr::video::SColor woodLight(255, 180, 165, 140);
    irr::video::SColor woodMid(255, 150, 135, 110);
    irr::video::SColor woodDark(255, 120, 105, 85);

    // Draw several pieces of driftwood
    for (int i = 0; i < 4; ++i) {
        int x = startX + xDist(rng);
        int y = startY + yDist(rng);
        int len = 12 + rng() % 15;
        int thickness = 2 + rng() % 2;

        float angle = (rng() % 60 - 30) * 3.14159f / 180.0f;
        int endX = x + static_cast<int>(len * std::cos(angle));
        int endY = y + static_cast<int>(len * std::sin(angle) * 0.3f);

        // Draw wood piece with thickness
        for (int t = -thickness; t <= thickness; ++t) {
            irr::video::SColor c = (t == 0) ? woodLight : (t > 0) ? woodMid : woodDark;
            drawLine(image, x, y + t, endX, endY + t, c);
        }

        // Wood grain lines
        for (int g = 2; g < len - 2; g += 3) {
            int gx = x + static_cast<int>(g * std::cos(angle));
            int gy = y + static_cast<int>(g * std::sin(angle) * 0.3f);
            setPixel(image, gx, gy, woodDark);
        }
    }
}

void DetailTextureAtlas::drawCactus(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(10101);
    std::uniform_int_distribution<int> xDist(12, TILE_SIZE - 13);

    irr::video::SColor cactusGreen(255, 80, 130, 60);
    irr::video::SColor cactusLight(255, 100, 150, 75);
    irr::video::SColor cactusDark(255, 60, 100, 45);
    irr::video::SColor spine(255, 220, 210, 180);

    // Draw small cacti
    for (int i = 0; i < 3; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 3;
        int height = 25 + rng() % 20;
        int width = 5 + rng() % 3;

        // Main body
        for (int h = 0; h < height; ++h) {
            int w = width;
            if (h < 3 || h > height - 3) w = width - 1;

            for (int dx = -w; dx <= w; ++dx) {
                irr::video::SColor c = (dx < 0) ? cactusDark : (dx > 0) ? cactusLight : cactusGreen;
                setPixel(image, baseX + dx, baseY - h, c);
            }
        }

        // Spines
        for (int h = 5; h < height - 3; h += 4) {
            setPixel(image, baseX - width - 1, baseY - h, spine);
            setPixel(image, baseX + width + 1, baseY - h, spine);
            setPixel(image, baseX - width - 2, baseY - h - 1, spine);
            setPixel(image, baseX + width + 2, baseY - h - 1, spine);
        }

        // Arms (on taller cacti)
        if (height > 35 && i == 0) {
            int armY = baseY - height / 2;
            for (int a = 0; a < 10; ++a) {
                setPixel(image, baseX - width - a, armY + a / 2, cactusGreen);
                setPixel(image, baseX - width - a, armY + a / 2 - 1, cactusLight);
            }
            for (int ah = 0; ah < 8; ++ah) {
                setPixel(image, baseX - width - 9, armY + 4 - ah, cactusGreen);
            }
        }
    }
}

// ============================================================
// Jungle biome (Kunark) - Row 3
// ============================================================

void DetailTextureAtlas::drawFern(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(20202);
    std::uniform_int_distribution<int> xDist(15, TILE_SIZE - 16);

    irr::video::SColor fernDark(255, 40, 100, 35);
    irr::video::SColor fernMid(255, 55, 130, 45);
    irr::video::SColor fernLight(255, 75, 160, 60);

    // Draw fern fronds
    for (int f = 0; f < 4; ++f) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 3;
        int length = 30 + rng() % 20;
        float baseAngle = -1.57f + (f - 1.5f) * 0.4f; // Spread out

        // Main stem
        for (int i = 0; i < length; ++i) {
            float t = static_cast<float>(i) / length;
            float angle = baseAngle + t * 0.3f; // Slight curve
            int px = baseX + static_cast<int>(i * std::sin(angle) * 0.5f);
            int py = baseY - i;

            irr::video::SColor stemColor = (t < 0.5f) ? fernDark : fernMid;
            setPixel(image, px, py, stemColor);

            // Leaflets along the stem
            if (i > 5 && i % 3 == 0) {
                int leafLen = static_cast<int>((1.0f - t) * 8);
                for (int l = 1; l <= leafLen; ++l) {
                    setPixel(image, px - l, py + l / 2, fernLight);
                    setPixel(image, px + l, py + l / 2, fernMid);
                }
            }
        }
    }
}

void DetailTextureAtlas::drawTropicalFlower(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(30303);
    std::uniform_int_distribution<int> xDist(12, TILE_SIZE - 13);

    irr::video::SColor stemGreen(255, 50, 120, 40);
    irr::video::SColor petalRed(255, 220, 60, 80);
    irr::video::SColor petalOrange(255, 255, 140, 50);
    irr::video::SColor petalYellow(255, 255, 220, 60);
    irr::video::SColor center(255, 255, 200, 100);

    // Draw tropical flowers
    for (int i = 0; i < 4; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int stemHeight = 20 + rng() % 15;

        // Curved stem
        for (int h = 0; h < stemHeight; ++h) {
            int curve = static_cast<int>(std::sin(h * 0.1f) * 2);
            setPixel(image, baseX + curve, baseY - h, stemGreen);
        }

        // Flower head
        int flowerY = baseY - stemHeight;
        irr::video::SColor petalColor = (i % 3 == 0) ? petalRed : (i % 3 == 1) ? petalOrange : petalYellow;

        // Petals (star-like pattern)
        for (int p = 0; p < 5; ++p) {
            float angle = p * 1.257f; // 72 degrees
            for (int r = 2; r <= 6; ++r) {
                int px = baseX + static_cast<int>(r * std::cos(angle));
                int py = flowerY + static_cast<int>(r * std::sin(angle));
                setPixel(image, px, py, petalColor);
                setPixel(image, px + 1, py, petalColor);
            }
        }

        // Center
        fillCircle(image, baseX, flowerY, 2, center);
    }
}

void DetailTextureAtlas::drawJungleGrass(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(40404);
    std::uniform_int_distribution<int> xDist(3, TILE_SIZE - 4);
    std::uniform_int_distribution<int> heightDist(25, 50);
    std::uniform_int_distribution<int> swayDist(-6, 6);

    irr::video::SColor grassDark(255, 40, 120, 30);
    irr::video::SColor grassMid(255, 60, 150, 45);
    irr::video::SColor grassLight(255, 85, 180, 60);

    // Draw dense tropical grass
    for (int i = 0; i < 16; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        irr::video::SColor color = (i % 3 == 0) ? grassDark : (i % 3 == 1) ? grassMid : grassLight;

        // Thicker blades
        drawLine(image, baseX, baseY, topX, baseY - height, color);
        drawLine(image, baseX + 1, baseY, topX + 1, baseY - height, color);

        // Drooping tip
        if (height > 35) {
            int tipX = topX + (rng() % 6) - 3;
            int tipY = baseY - height + 5;
            drawLine(image, topX, baseY - height, tipX, tipY, color);
        }
    }
}

void DetailTextureAtlas::drawVine(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(50505);
    std::uniform_int_distribution<int> xDist(8, TILE_SIZE - 9);

    irr::video::SColor vineDark(255, 45, 90, 35);
    irr::video::SColor vineMid(255, 60, 110, 45);
    irr::video::SColor vineLight(255, 80, 130, 55);
    irr::video::SColor leaf(255, 50, 120, 40);

    // Draw curving vines
    for (int v = 0; v < 5; ++v) {
        int startPosX = startX + xDist(rng);
        int y = startY + TILE_SIZE - 5;
        int length = 25 + rng() % 20;

        float x = static_cast<float>(startPosX);
        float dx = (rng() % 3 - 1) * 0.3f;

        for (int i = 0; i < length; ++i) {
            x += dx + std::sin(i * 0.2f) * 0.5f;
            irr::video::SColor c = (i % 3 == 0) ? vineDark : (i % 3 == 1) ? vineMid : vineLight;
            setPixel(image, static_cast<int>(x), y - i, c);

            // Small leaves along vine
            if (i % 6 == 3) {
                int side = (i / 6) % 2 == 0 ? -1 : 1;
                for (int l = 1; l <= 3; ++l) {
                    setPixel(image, static_cast<int>(x) + side * l, y - i - l / 2, leaf);
                }
            }
        }
    }
}

void DetailTextureAtlas::drawBamboo(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(60606);
    std::uniform_int_distribution<int> xDist(10, TILE_SIZE - 11);

    irr::video::SColor bambooLight(255, 160, 180, 90);
    irr::video::SColor bambooMid(255, 130, 155, 70);
    irr::video::SColor bambooDark(255, 100, 125, 55);
    irr::video::SColor bambooNode(255, 90, 110, 50);
    irr::video::SColor leafGreen(255, 70, 140, 50);

    // Draw bamboo stalks
    for (int b = 0; b < 4; ++b) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = 35 + rng() % 20;
        int width = 3;

        // Main stalk with segments
        for (int h = 0; h < height; ++h) {
            for (int w = -width; w <= width; ++w) {
                irr::video::SColor c = (w < 0) ? bambooDark : (w > 0) ? bambooLight : bambooMid;
                setPixel(image, baseX + w, baseY - h, c);
            }

            // Node rings
            if (h % 12 == 0 && h > 0) {
                for (int w = -width - 1; w <= width + 1; ++w) {
                    setPixel(image, baseX + w, baseY - h, bambooNode);
                    setPixel(image, baseX + w, baseY - h - 1, bambooNode);
                }
            }
        }

        // Leaves at top
        int leafY = baseY - height + 3;
        for (int l = 0; l < 3; ++l) {
            int lx = baseX + (l - 1) * 4;
            for (int i = 0; i < 8; ++i) {
                setPixel(image, lx + i, leafY - std::abs(i - 4), leafGreen);
                setPixel(image, lx - i, leafY - 5 - std::abs(i - 4), leafGreen);
            }
        }
    }
}

// ============================================================
// Swamp biome (Innothule, Kunark marshes) - Row 4
// ============================================================

void DetailTextureAtlas::drawCattail(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(70707);
    std::uniform_int_distribution<int> xDist(10, TILE_SIZE - 11);

    irr::video::SColor stemGreen(255, 70, 100, 50);
    irr::video::SColor stemDark(255, 50, 80, 40);
    irr::video::SColor headBrown(255, 100, 70, 45);
    irr::video::SColor headDark(255, 80, 55, 35);

    // Draw cattails
    for (int c = 0; c < 5; ++c) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = 40 + rng() % 15;

        // Long thin stem
        for (int h = 0; h < height; ++h) {
            irr::video::SColor sc = (h % 2 == 0) ? stemGreen : stemDark;
            setPixel(image, baseX, baseY - h, sc);
            if (h < height - 15) {
                setPixel(image, baseX + 1, baseY - h, stemDark);
            }
        }

        // Brown fuzzy head
        int headY = baseY - height + 8;
        int headHeight = 10 + rng() % 5;
        for (int hh = 0; hh < headHeight; ++hh) {
            int headWidth = 3 - std::abs(hh - headHeight / 2) / 3;
            for (int hw = -headWidth; hw <= headWidth; ++hw) {
                irr::video::SColor hc = (hw == 0) ? headBrown : headDark;
                setPixel(image, baseX + hw, headY - hh, hc);
            }
        }

        // Spike at top
        for (int s = 0; s < 5; ++s) {
            setPixel(image, baseX, headY - headHeight - s, stemGreen);
        }
    }
}

void DetailTextureAtlas::drawSwampMushroom(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(80808);
    std::uniform_int_distribution<int> xDist(10, TILE_SIZE - 11);

    irr::video::SColor stemPale(255, 180, 170, 150);
    irr::video::SColor stemDark(255, 140, 130, 110);
    irr::video::SColor capBrown(255, 120, 90, 70);
    irr::video::SColor capLight(255, 150, 115, 90);
    irr::video::SColor capDark(255, 90, 65, 50);
    irr::video::SColor spots(255, 200, 180, 150);

    // Draw swamp mushrooms
    for (int m = 0; m < 4; ++m) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 3;
        int stemHeight = 10 + rng() % 8;
        int capRadius = 6 + rng() % 4;

        // Stem
        for (int h = 0; h < stemHeight; ++h) {
            int sw = 2 + (h < 3 ? 1 : 0);
            for (int w = -sw; w <= sw; ++w) {
                irr::video::SColor sc = (w == 0) ? stemPale : stemDark;
                setPixel(image, baseX + w, baseY - h, sc);
            }
        }

        // Cap
        int capY = baseY - stemHeight;
        for (int dy = -capRadius / 2; dy <= capRadius / 2; ++dy) {
            for (int dx = -capRadius; dx <= capRadius; ++dx) {
                float dist = std::sqrt(dx * dx + dy * dy * 3.0f);
                if (dist < capRadius) {
                    irr::video::SColor cc = (dy < 0) ? capLight : (dx < 0) ? capBrown : capDark;
                    setPixel(image, baseX + dx, capY + dy, cc);
                }
            }
        }

        // Pale spots
        if (capRadius > 5) {
            setPixel(image, baseX - 2, capY - 1, spots);
            setPixel(image, baseX + 3, capY, spots);
            setPixel(image, baseX, capY - 2, spots);
        }
    }
}

void DetailTextureAtlas::drawReed(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(90909);
    std::uniform_int_distribution<int> xDist(4, TILE_SIZE - 5);
    std::uniform_int_distribution<int> heightDist(30, 50);
    std::uniform_int_distribution<int> swayDist(-4, 4);

    irr::video::SColor reedGreen(255, 80, 110, 55);
    irr::video::SColor reedDark(255, 60, 85, 40);
    irr::video::SColor reedLight(255, 100, 130, 70);

    // Draw marsh reeds
    for (int r = 0; r < 12; ++r) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        irr::video::SColor color = (r % 3 == 0) ? reedDark : (r % 3 == 1) ? reedGreen : reedLight;

        drawLine(image, baseX, baseY, topX, baseY - height, color);

        // Some reeds have a second blade
        if (r % 3 == 0) {
            int topX2 = topX + swayDist(rng);
            drawLine(image, baseX + 1, baseY, topX2, baseY - height + 5, reedDark);
        }
    }
}

void DetailTextureAtlas::drawLilyPad(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(11110);
    std::uniform_int_distribution<int> xDist(12, TILE_SIZE - 13);
    std::uniform_int_distribution<int> yDist(TILE_SIZE - 20, TILE_SIZE - 8);

    irr::video::SColor padGreen(255, 60, 120, 50);
    irr::video::SColor padLight(255, 80, 145, 65);
    irr::video::SColor padDark(255, 45, 95, 38);
    irr::video::SColor vein(255, 50, 100, 42);

    // Draw lily pads (viewed from above, flat on ground)
    for (int p = 0; p < 4; ++p) {
        int cx = startX + xDist(rng);
        int cy = startY + yDist(rng);
        int radius = 6 + rng() % 4;

        // Circular pad with notch
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                // V-shaped notch
                bool inNotch = (dy < 0 && std::abs(dx) < -dy / 2);

                if (dist <= radius && !inNotch) {
                    irr::video::SColor c = (dy < 0) ? padLight : (dx < 0) ? padGreen : padDark;
                    setPixel(image, cx + dx, cy + dy, c);
                }
            }
        }

        // Radial veins
        for (float angle = 0.5f; angle < 6.0f; angle += 0.8f) {
            for (int r = 2; r < radius - 1; ++r) {
                int vx = cx + static_cast<int>(r * std::cos(angle));
                int vy = cy + static_cast<int>(r * std::sin(angle));
                setPixel(image, vx, vy, vein);
            }
        }
    }
}

void DetailTextureAtlas::drawSwampGrass(irr::video::IImage* image, int startX, int startY) {
    std::mt19937 rng(12121);
    std::uniform_int_distribution<int> xDist(4, TILE_SIZE - 5);
    std::uniform_int_distribution<int> heightDist(18, 38);
    std::uniform_int_distribution<int> swayDist(-5, 5);

    irr::video::SColor grassOlive(255, 90, 105, 55);
    irr::video::SColor grassMud(255, 75, 85, 45);
    irr::video::SColor grassYellow(255, 110, 115, 60);

    // Draw soggy swamp grass
    for (int g = 0; g < 14; ++g) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        irr::video::SColor color = (g % 3 == 0) ? grassOlive : (g % 3 == 1) ? grassMud : grassYellow;

        drawLine(image, baseX, baseY, topX, baseY - height, color);

        // Some blades droop more (wet/heavy)
        if (g % 4 == 0 && height > 25) {
            int droopX = topX + swayDist(rng) * 2;
            int droopY = baseY - height + 8;
            drawLine(image, topX, baseY - height, droopX, droopY, color);
        }
    }
}

// ============================================================
// Footprints (for different surface types) - Row 5
// ============================================================

void DetailTextureAtlas::drawFootprintLeft(irr::video::IImage* image, int startX, int startY, int r, int g, int b) {
    // Left foot shape with biome-specific color
    int cx = startX + TILE_SIZE / 2;
    int cy = startY + TILE_SIZE / 2;

    // Foot dimensions
    const int heelWidth = 14;
    const int ballWidth = 18;
    const int footLength = 28;
    const int archInset = 4;

    // Toe positions for left foot - big toe on inside (left)
    struct Toe { int offsetX; int offsetY; int radius; };
    Toe toes[5] = {
        {-7, -12, 5},   // Big toe (inside)
        {-2, -14, 4},   // Second toe
        {3, -13, 4},    // Middle toe
        {7, -11, 3},    // Fourth toe
        {10, -8, 3}     // Little toe (outside)
    };

    for (int dy = -footLength; dy <= footLength / 2 + 5; ++dy) {
        for (int dx = -ballWidth - 5; dx <= ballWidth + 5; ++dx) {
            float alpha = 0.0f;

            float yNorm = static_cast<float>(dy + footLength / 3) / footLength;
            yNorm = std::max(0.0f, std::min(1.0f, yNorm));
            float halfWidth = ballWidth * (1.0f - yNorm * 0.3f) - (heelWidth * yNorm * 0.2f);

            float archOffset = 0.0f;
            if (yNorm > 0.2f && yNorm < 0.8f) {
                float archT = (yNorm - 0.2f) / 0.6f;
                archOffset = archInset * std::sin(archT * 3.14159f);
            }

            float insideEdge = -halfWidth + archOffset;
            float outsideEdge = halfWidth;

            if (dy > -footLength / 3 && dy < footLength / 2) {
                if (dx >= insideEdge && dx <= outsideEdge) {
                    float distFromCenter = std::abs(dx) / halfWidth;
                    alpha = 1.0f - distFromCenter * 0.3f;
                    float edgeDist = std::min(dx - insideEdge, outsideEdge - dx);
                    if (edgeDist < 3.0f) alpha *= edgeDist / 3.0f;
                }
            }

            for (int t = 0; t < 5; ++t) {
                float tdx = dx - toes[t].offsetX;
                float tdy = dy - toes[t].offsetY;
                float toeDistSq = tdx * tdx + tdy * tdy;
                float toeRadiusSq = static_cast<float>(toes[t].radius * toes[t].radius);
                if (toeDistSq < toeRadiusSq * 1.5f) {
                    float toeDist = std::sqrt(toeDistSq) / toes[t].radius;
                    float toeAlpha = 1.0f - toeDist * 0.6f;
                    if (toeAlpha > 0) alpha = std::max(alpha, toeAlpha);
                }
            }

            if (dy < -footLength / 3 + 8 && dy > -footLength / 3 - 5) {
                float ballY = dy + footLength / 3;
                float ballDist = std::sqrt(dx * dx / (ballWidth * ballWidth * 0.8f) + ballY * ballY / 64.0f);
                if (ballDist < 1.0f) alpha = std::max(alpha, 1.0f - ballDist * 0.5f);
            }

            if (dy > footLength / 2 - 10) {
                float heelY = dy - footLength / 2 + 5;
                float heelRx = heelWidth * 0.8f;
                float heelDist = std::sqrt(dx * dx / (heelRx * heelRx) + heelY * heelY / 64.0f);
                if (heelDist < 1.0f) alpha = std::max(alpha, 1.0f - heelDist * 0.4f);
            }

            if (alpha > 0.01f) {
                alpha = std::min(1.0f, alpha);
                // Apply slight variation based on alpha for depth
                int shade_r = r + static_cast<int>((1.0f - alpha) * 20);
                int shade_g = g + static_cast<int>((1.0f - alpha) * 20);
                int shade_b = b + static_cast<int>((1.0f - alpha) * 20);
                irr::video::SColor color(static_cast<irr::u32>(alpha * 200), shade_r, shade_g, shade_b);
                setPixel(image, cx + dx, cy + dy, color);
            }
        }
    }
}

void DetailTextureAtlas::drawFootprintRight(irr::video::IImage* image, int startX, int startY, int r, int g, int b) {
    // Right foot shape with biome-specific color (mirrored from left)
    int cx = startX + TILE_SIZE / 2;
    int cy = startY + TILE_SIZE / 2;

    const int heelWidth = 14;
    const int ballWidth = 18;
    const int footLength = 28;
    const int archInset = 4;

    // Toe positions for right foot - big toe on inside (right)
    struct Toe { int offsetX; int offsetY; int radius; };
    Toe toes[5] = {
        {7, -12, 5},    // Big toe (inside - on right)
        {2, -14, 4},    // Second toe
        {-3, -13, 4},   // Middle toe
        {-7, -11, 3},   // Fourth toe
        {-10, -8, 3}    // Little toe (outside - on left)
    };

    for (int dy = -footLength; dy <= footLength / 2 + 5; ++dy) {
        for (int dx = -ballWidth - 5; dx <= ballWidth + 5; ++dx) {
            float alpha = 0.0f;

            float yNorm = static_cast<float>(dy + footLength / 3) / footLength;
            yNorm = std::max(0.0f, std::min(1.0f, yNorm));
            float halfWidth = ballWidth * (1.0f - yNorm * 0.3f) - (heelWidth * yNorm * 0.2f);

            float archOffset = 0.0f;
            if (yNorm > 0.2f && yNorm < 0.8f) {
                float archT = (yNorm - 0.2f) / 0.6f;
                archOffset = archInset * std::sin(archT * 3.14159f);
            }

            float insideEdge = -halfWidth;
            float outsideEdge = halfWidth - archOffset;

            if (dy > -footLength / 3 && dy < footLength / 2) {
                if (dx >= insideEdge && dx <= outsideEdge) {
                    float distFromCenter = std::abs(dx) / halfWidth;
                    alpha = 1.0f - distFromCenter * 0.3f;
                    float edgeDist = std::min(dx - insideEdge, outsideEdge - dx);
                    if (edgeDist < 3.0f) alpha *= edgeDist / 3.0f;
                }
            }

            for (int t = 0; t < 5; ++t) {
                float tdx = dx - toes[t].offsetX;
                float tdy = dy - toes[t].offsetY;
                float toeDistSq = tdx * tdx + tdy * tdy;
                float toeRadiusSq = static_cast<float>(toes[t].radius * toes[t].radius);
                if (toeDistSq < toeRadiusSq * 1.5f) {
                    float toeDist = std::sqrt(toeDistSq) / toes[t].radius;
                    float toeAlpha = 1.0f - toeDist * 0.6f;
                    if (toeAlpha > 0) alpha = std::max(alpha, toeAlpha);
                }
            }

            if (dy < -footLength / 3 + 8 && dy > -footLength / 3 - 5) {
                float ballY = dy + footLength / 3;
                float ballDist = std::sqrt(dx * dx / (ballWidth * ballWidth * 0.8f) + ballY * ballY / 64.0f);
                if (ballDist < 1.0f) alpha = std::max(alpha, 1.0f - ballDist * 0.5f);
            }

            if (dy > footLength / 2 - 10) {
                float heelY = dy - footLength / 2 + 5;
                float heelRx = heelWidth * 0.8f;
                float heelDist = std::sqrt(dx * dx / (heelRx * heelRx) + heelY * heelY / 64.0f);
                if (heelDist < 1.0f) alpha = std::max(alpha, 1.0f - heelDist * 0.4f);
            }

            if (alpha > 0.01f) {
                alpha = std::min(1.0f, alpha);
                // Apply slight variation based on alpha for depth
                int shade_r = r + static_cast<int>((1.0f - alpha) * 20);
                int shade_g = g + static_cast<int>((1.0f - alpha) * 20);
                int shade_b = b + static_cast<int>((1.0f - alpha) * 20);
                irr::video::SColor color(static_cast<irr::u32>(alpha * 200), shade_r, shade_g, shade_b);
                setPixel(image, cx + dx, cy + dy, color);
            }
        }
    }
}

// ============================================================
// Additional grass varieties (Row 7)
// All non-desert grass has 75-100% living, 0-25% dead/dying (yellow-brown)
// ============================================================

void DetailTextureAtlas::drawGrassMixed1(irr::video::IImage* image, int startX, int startY) {
    // Short grass with ~20% dead blades mixed in
    std::mt19937 rng(70001);
    std::uniform_int_distribution<int> xDist(4, TILE_SIZE - 5);
    std::uniform_int_distribution<int> heightDist(15, 32);
    std::uniform_int_distribution<int> swayDist(-3, 3);
    std::uniform_int_distribution<int> deadChance(0, 99);

    // Living grass colors - varied greens
    std::vector<irr::video::SColor> livingColors = {
        irr::video::SColor(255, 100, 180, 45),   // Medium green
        irr::video::SColor(255, 85, 165, 40),    // Slightly darker
        irr::video::SColor(255, 110, 195, 55),   // Lighter green
        irr::video::SColor(255, 95, 175, 60),    // Blue-green tint
        irr::video::SColor(255, 120, 185, 50),   // Yellow-green tint
    };

    // Dead/dying grass colors - yellow-brown
    std::vector<irr::video::SColor> deadColors = {
        irr::video::SColor(255, 180, 160, 90),   // Light yellow-brown
        irr::video::SColor(255, 165, 145, 80),   // Medium yellow-brown
        irr::video::SColor(255, 150, 130, 70),   // Darker yellow-brown
        irr::video::SColor(255, 175, 155, 95),   // Tan
    };

    for (int i = 0; i < 14; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        // 20% chance of dead blade
        bool isDead = deadChance(rng) < 20;
        irr::video::SColor color = isDead
            ? deadColors[rng() % deadColors.size()]
            : livingColors[rng() % livingColors.size()];

        // Slightly darker shade for depth
        irr::video::SColor colorDark(255,
            static_cast<irr::u32>(color.getRed() * 0.75f),
            static_cast<irr::u32>(color.getGreen() * 0.75f),
            static_cast<irr::u32>(color.getBlue() * 0.75f));

        drawLine(image, baseX, baseY, topX, baseY - height, color);
        drawLine(image, baseX - 1, baseY, topX - 1, baseY - height + 2, colorDark);
    }
}

void DetailTextureAtlas::drawGrassMixed2(irr::video::IImage* image, int startX, int startY) {
    // Tall grass with ~20% dead blades mixed in
    std::mt19937 rng(70002);
    std::uniform_int_distribution<int> xDist(5, TILE_SIZE - 6);
    std::uniform_int_distribution<int> heightDist(35, 55);
    std::uniform_int_distribution<int> swayDist(-5, 5);
    std::uniform_int_distribution<int> deadChance(0, 99);

    // Living grass colors
    std::vector<irr::video::SColor> livingColors = {
        irr::video::SColor(255, 70, 160, 35),    // Dark rich green
        irr::video::SColor(255, 80, 175, 40),    // Medium green
        irr::video::SColor(255, 90, 185, 50),    // Lighter green
        irr::video::SColor(255, 75, 170, 55),    // Blue-green
        irr::video::SColor(255, 95, 180, 45),    // Standard green
    };

    // Dead/dying grass colors
    std::vector<irr::video::SColor> deadColors = {
        irr::video::SColor(255, 175, 155, 85),
        irr::video::SColor(255, 160, 140, 75),
        irr::video::SColor(255, 185, 165, 95),
        irr::video::SColor(255, 170, 150, 80),
    };

    for (int i = 0; i < 11; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        bool isDead = deadChance(rng) < 20;
        irr::video::SColor color = isDead
            ? deadColors[rng() % deadColors.size()]
            : livingColors[rng() % livingColors.size()];

        irr::video::SColor colorDark(255,
            static_cast<irr::u32>(color.getRed() * 0.7f),
            static_cast<irr::u32>(color.getGreen() * 0.7f),
            static_cast<irr::u32>(color.getBlue() * 0.7f));

        // Thicker blades for tall grass
        drawLine(image, baseX, baseY, topX, baseY - height, color);
        drawLine(image, baseX + 1, baseY, topX + 1, baseY - height, color);
        drawLine(image, baseX - 1, baseY, topX - 1, baseY - height + 3, colorDark);
    }
}

void DetailTextureAtlas::drawGrassClump(irr::video::IImage* image, int startX, int startY) {
    // Dense grass clump with highly varied heights, emanating from a central point
    std::mt19937 rng(70003);
    std::uniform_int_distribution<int> heightDist(12, 52);
    std::uniform_int_distribution<int> deadChance(0, 99);

    // Center of the clump
    int centerX = startX + TILE_SIZE / 2;
    int baseY = startY + TILE_SIZE - 2;

    std::vector<irr::video::SColor> livingColors = {
        irr::video::SColor(255, 80, 170, 40),
        irr::video::SColor(255, 95, 185, 50),
        irr::video::SColor(255, 70, 155, 35),
        irr::video::SColor(255, 105, 190, 55),
        irr::video::SColor(255, 85, 175, 45),
    };

    std::vector<irr::video::SColor> deadColors = {
        irr::video::SColor(255, 170, 150, 85),
        irr::video::SColor(255, 180, 160, 90),
        irr::video::SColor(255, 160, 140, 75),
    };

    // Draw 18 blades radiating from center
    for (int i = 0; i < 18; ++i) {
        // Spread angle from -60 to +60 degrees
        float angle = -1.05f + (i / 17.0f) * 2.1f;
        int height = heightDist(rng);

        // Base position varies slightly around center
        int bx = centerX + (rng() % 7) - 3;
        int topX = bx + static_cast<int>(std::sin(angle) * height * 0.4f);
        int topY = baseY - height;

        bool isDead = deadChance(rng) < 15;  // 15% dead in clump
        irr::video::SColor color = isDead
            ? deadColors[rng() % deadColors.size()]
            : livingColors[rng() % livingColors.size()];

        drawLine(image, bx, baseY, topX, topY, color);

        // Add second line for thickness on taller blades
        if (height > 30) {
            irr::video::SColor colorDark(255,
                static_cast<irr::u32>(color.getRed() * 0.8f),
                static_cast<irr::u32>(color.getGreen() * 0.8f),
                static_cast<irr::u32>(color.getBlue() * 0.8f));
            drawLine(image, bx + 1, baseY, topX + 1, topY + 2, colorDark);
        }
    }
}

void DetailTextureAtlas::drawGrassWispy(irr::video::IImage* image, int startX, int startY) {
    // Thin, delicate single-pixel-wide grass blades
    std::mt19937 rng(70004);
    std::uniform_int_distribution<int> xDist(3, TILE_SIZE - 4);
    std::uniform_int_distribution<int> heightDist(20, 48);
    std::uniform_int_distribution<int> swayDist(-6, 6);
    std::uniform_int_distribution<int> deadChance(0, 99);

    std::vector<irr::video::SColor> livingColors = {
        irr::video::SColor(255, 115, 200, 60),   // Light bright green
        irr::video::SColor(255, 100, 190, 55),   // Medium light green
        irr::video::SColor(255, 125, 210, 70),   // Very light green
        irr::video::SColor(255, 110, 195, 65),   // Yellow-green
        irr::video::SColor(255, 105, 185, 50),   // Standard light
    };

    std::vector<irr::video::SColor> deadColors = {
        irr::video::SColor(255, 195, 180, 110),  // Light straw
        irr::video::SColor(255, 185, 170, 100),
        irr::video::SColor(255, 200, 185, 115),
    };

    // Many thin blades
    for (int i = 0; i < 20; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        bool isDead = deadChance(rng) < 18;
        irr::video::SColor color = isDead
            ? deadColors[rng() % deadColors.size()]
            : livingColors[rng() % livingColors.size()];

        // Single pixel wide - delicate look
        drawLine(image, baseX, baseY, topX, baseY - height, color);
    }
}

void DetailTextureAtlas::drawGrassBroad(irr::video::IImage* image, int startX, int startY) {
    // Wide, flat leaf grass blades (like ornamental grass)
    std::mt19937 rng(70005);
    std::uniform_int_distribution<int> xDist(8, TILE_SIZE - 12);
    std::uniform_int_distribution<int> heightDist(28, 50);
    std::uniform_int_distribution<int> swayDist(-4, 4);
    std::uniform_int_distribution<int> deadChance(0, 99);

    std::vector<irr::video::SColor> livingColors = {
        irr::video::SColor(255, 75, 155, 40),    // Dark green
        irr::video::SColor(255, 85, 165, 45),    // Medium green
        irr::video::SColor(255, 70, 150, 35),    // Forest green
        irr::video::SColor(255, 90, 170, 50),    // Lighter
    };

    std::vector<irr::video::SColor> deadColors = {
        irr::video::SColor(255, 165, 145, 80),
        irr::video::SColor(255, 175, 155, 85),
        irr::video::SColor(255, 155, 135, 70),
    };

    // Fewer but wider blades
    for (int i = 0; i < 8; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        bool isDead = deadChance(rng) < 20;
        irr::video::SColor color = isDead
            ? deadColors[rng() % deadColors.size()]
            : livingColors[rng() % livingColors.size()];

        // Create highlight and shadow for width
        irr::video::SColor colorLight(255,
            std::min(255u, static_cast<irr::u32>(color.getRed() * 1.2f)),
            std::min(255u, static_cast<irr::u32>(color.getGreen() * 1.15f)),
            std::min(255u, static_cast<irr::u32>(color.getBlue() * 1.1f)));
        irr::video::SColor colorDark(255,
            static_cast<irr::u32>(color.getRed() * 0.7f),
            static_cast<irr::u32>(color.getGreen() * 0.7f),
            static_cast<irr::u32>(color.getBlue() * 0.7f));

        // Wide blade - 4 pixels wide tapering to 2 at top
        for (int h = 0; h < height; ++h) {
            float t = static_cast<float>(h) / height;
            int y = baseY - h;
            float xProgress = static_cast<float>(h) / height;
            int x = baseX + static_cast<int>(xProgress * (topX - baseX));

            // Width tapers from 4 to 2
            int halfWidth = static_cast<int>(2.0f * (1.0f - t * 0.5f));

            for (int w = -halfWidth; w <= halfWidth; ++w) {
                irr::video::SColor c = (w < 0) ? colorDark : (w > 0) ? colorLight : color;
                setPixel(image, x + w, y, c);
            }
        }
    }
}

void DetailTextureAtlas::drawGrassCurved(irr::video::IImage* image, int startX, int startY) {
    // Grass with curved/wind-blown tips
    std::mt19937 rng(70006);
    std::uniform_int_distribution<int> xDist(5, TILE_SIZE - 8);
    std::uniform_int_distribution<int> heightDist(30, 52);
    std::uniform_int_distribution<int> deadChance(0, 99);

    std::vector<irr::video::SColor> livingColors = {
        irr::video::SColor(255, 90, 175, 45),
        irr::video::SColor(255, 80, 165, 40),
        irr::video::SColor(255, 100, 185, 55),
        irr::video::SColor(255, 85, 170, 50),
    };

    std::vector<irr::video::SColor> deadColors = {
        irr::video::SColor(255, 175, 155, 85),
        irr::video::SColor(255, 185, 165, 95),
        irr::video::SColor(255, 165, 145, 75),
    };

    for (int i = 0; i < 12; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);

        bool isDead = deadChance(rng) < 18;
        irr::video::SColor color = isDead
            ? deadColors[rng() % deadColors.size()]
            : livingColors[rng() % livingColors.size()];

        // Curve direction - mostly to one side for wind effect
        float curveDir = (rng() % 100 < 70) ? 1.0f : -1.0f;
        float curveStrength = 0.3f + (rng() % 100) / 200.0f;

        // Draw curved blade using quadratic bezier-like curve
        int prevX = baseX;
        int prevY = baseY;
        for (int h = 1; h <= height; ++h) {
            float t = static_cast<float>(h) / height;
            // Curve increases toward the top (t^2 for acceleration)
            float curve = curveDir * curveStrength * height * t * t;
            int x = baseX + static_cast<int>(curve);
            int y = baseY - h;

            drawLine(image, prevX, prevY, x, y, color);
            prevX = x;
            prevY = y;
        }

        // Second line for thickness
        if (height > 35) {
            irr::video::SColor colorDark(255,
                static_cast<irr::u32>(color.getRed() * 0.8f),
                static_cast<irr::u32>(color.getGreen() * 0.8f),
                static_cast<irr::u32>(color.getBlue() * 0.8f));

            prevX = baseX + 1;
            prevY = baseY;
            for (int h = 1; h <= height - 2; ++h) {
                float t = static_cast<float>(h) / height;
                float curve = curveDir * curveStrength * height * t * t;
                int x = baseX + 1 + static_cast<int>(curve);
                int y = baseY - h;
                drawLine(image, prevX, prevY, x, y, colorDark);
                prevX = x;
                prevY = y;
            }
        }
    }
}

void DetailTextureAtlas::drawGrassSeedHead(irr::video::IImage* image, int startX, int startY) {
    // Grass with seed heads at the top (like wheat or tall fescue)
    std::mt19937 rng(70007);
    std::uniform_int_distribution<int> xDist(8, TILE_SIZE - 10);
    std::uniform_int_distribution<int> heightDist(35, 55);
    std::uniform_int_distribution<int> swayDist(-4, 4);
    std::uniform_int_distribution<int> deadChance(0, 99);

    std::vector<irr::video::SColor> livingColors = {
        irr::video::SColor(255, 85, 165, 45),
        irr::video::SColor(255, 75, 155, 40),
        irr::video::SColor(255, 95, 175, 50),
    };

    std::vector<irr::video::SColor> deadColors = {
        irr::video::SColor(255, 180, 160, 90),
        irr::video::SColor(255, 170, 150, 85),
    };

    // Seed head colors - golden/tan
    irr::video::SColor seedLight(255, 210, 195, 130);
    irr::video::SColor seedMid(255, 190, 175, 110);
    irr::video::SColor seedDark(255, 170, 155, 95);

    for (int i = 0; i < 9; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        bool isDead = deadChance(rng) < 22;  // Slightly more dead for seeding grass
        irr::video::SColor stemColor = isDead
            ? deadColors[rng() % deadColors.size()]
            : livingColors[rng() % livingColors.size()];

        // Draw stem
        int stemHeight = height - 12;  // Leave room for seed head
        drawLine(image, baseX, baseY, topX, baseY - stemHeight, stemColor);

        // Draw seed head at top - elongated oval with small seeds
        int seedY = baseY - stemHeight;
        int seedHeight = 10 + rng() % 4;

        for (int sh = 0; sh < seedHeight; ++sh) {
            float t = static_cast<float>(sh) / seedHeight;
            // Tapered shape - wider in middle
            int width = static_cast<int>(2.5f * std::sin(t * 3.14159f));
            width = std::max(1, width);

            int sy = seedY - sh;
            for (int sw = -width; sw <= width; ++sw) {
                irr::video::SColor sc = (sw < 0) ? seedDark : (sw > 0) ? seedLight : seedMid;
                setPixel(image, topX + sw, sy, sc);
            }
        }

        // Add small seed details
        for (int s = 2; s < seedHeight - 2; s += 2) {
            setPixel(image, topX - 1, seedY - s, seedDark);
            setPixel(image, topX + 1, seedY - s - 1, seedLight);
        }
    }
}

void DetailTextureAtlas::drawGrassBroken(irr::video::IImage* image, int startX, int startY) {
    // Grass with bent/broken blades (trampled or weather-damaged)
    std::mt19937 rng(70008);
    std::uniform_int_distribution<int> xDist(4, TILE_SIZE - 8);
    std::uniform_int_distribution<int> heightDist(25, 45);
    std::uniform_int_distribution<int> swayDist(-3, 3);
    std::uniform_int_distribution<int> deadChance(0, 99);
    std::uniform_int_distribution<int> breakChance(0, 99);

    std::vector<irr::video::SColor> livingColors = {
        irr::video::SColor(255, 90, 170, 45),
        irr::video::SColor(255, 80, 160, 40),
        irr::video::SColor(255, 100, 180, 55),
        irr::video::SColor(255, 85, 165, 50),
    };

    std::vector<irr::video::SColor> deadColors = {
        irr::video::SColor(255, 175, 155, 85),
        irr::video::SColor(255, 165, 145, 75),
        irr::video::SColor(255, 185, 165, 95),
    };

    for (int i = 0; i < 14; ++i) {
        int baseX = startX + xDist(rng);
        int baseY = startY + TILE_SIZE - 2;
        int height = heightDist(rng);
        int topX = baseX + swayDist(rng);

        bool isDead = deadChance(rng) < 25;  // More dead in damaged grass
        irr::video::SColor color = isDead
            ? deadColors[rng() % deadColors.size()]
            : livingColors[rng() % livingColors.size()];

        // 40% chance of broken blade
        bool isBroken = breakChance(rng) < 40;

        if (isBroken && height > 20) {
            // Draw lower portion straight up
            int breakPoint = height / 2 + (rng() % (height / 4));
            int midX = baseX + (topX - baseX) * breakPoint / height;
            int midY = baseY - breakPoint;

            drawLine(image, baseX, baseY, midX, midY, color);

            // Upper portion bends sharply to the side
            int bendDir = (rng() % 2 == 0) ? 1 : -1;
            int bendAmount = 8 + rng() % 10;
            int endX = midX + bendDir * bendAmount;
            int endY = midY - (height - breakPoint) / 2;  // Doesn't go as high

            // Slightly faded color for broken part
            irr::video::SColor brokenColor(255,
                static_cast<irr::u32>(color.getRed() * 0.9f),
                static_cast<irr::u32>(color.getGreen() * 0.85f),
                static_cast<irr::u32>(color.getBlue() * 0.9f));

            drawLine(image, midX, midY, endX, endY, brokenColor);
        } else {
            // Normal blade
            drawLine(image, baseX, baseY, topX, baseY - height, color);

            // Add second line for some thickness
            if (height > 30 && rng() % 2 == 0) {
                irr::video::SColor colorDark(255,
                    static_cast<irr::u32>(color.getRed() * 0.8f),
                    static_cast<irr::u32>(color.getGreen() * 0.8f),
                    static_cast<irr::u32>(color.getBlue() * 0.8f));
                drawLine(image, baseX + 1, baseY, topX + 1, baseY - height + 2, colorDark);
            }
        }
    }
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
