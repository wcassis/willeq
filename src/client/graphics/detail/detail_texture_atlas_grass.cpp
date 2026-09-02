// detail_texture_atlas_grass.cpp -- DetailTextureAtlas tile painters for the
// footprint row and the grass row.
// Atlas assembly and the shared drawing primitives stay in
// detail_texture_atlas.cpp; the flora biome rows are in the _flora TU.
// Split per refactor/J28_detail_texture_atlas_split.md -- move-only.
#include "client/graphics/detail/detail_texture_atlas.h"
#include "common/logging.h"
#include <random>
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Detail {
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
