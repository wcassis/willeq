#include "client/graphics/texture_generators.h"
#include <cmath>
#include <random>

namespace EQT {
namespace Graphics {

// ============================================================
// Particle Atlas Generation
// ============================================================

irr::video::IImage* generateParticleAtlas(irr::video::IVideoDriver* driver) {
    if (!driver) return nullptr;

    const int tileSize = 16;
    const int atlasColumns = 4;
    const int atlasRows = 4;
    const int atlasWidth = tileSize * atlasColumns;
    const int atlasHeight = tileSize * atlasRows;

    irr::video::IImage* img = driver->createImage(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(atlasWidth, atlasHeight));

    if (!img) return nullptr;

    img->fill(irr::video::SColor(0, 0, 0, 0));

    auto drawCircle = [&](int tileX, int tileY, float softness, irr::video::SColor color) {
        int baseX = tileX * tileSize;
        int baseY = tileY * tileSize;
        float centerX = tileSize / 2.0f;
        float centerY = tileSize / 2.0f;
        float radius = tileSize / 2.0f - 1.0f;

        for (int y = 0; y < tileSize; ++y) {
            for (int x = 0; x < tileSize; ++x) {
                float dx = x - centerX + 0.5f;
                float dy = y - centerY + 0.5f;
                float dist = std::sqrt(dx * dx + dy * dy);
                float alpha = 0.0f;

                if (dist < radius) {
                    float edgeDist = radius - dist;
                    alpha = std::min(1.0f, edgeDist / (radius * softness));
                    alpha = alpha * alpha;
                }

                irr::video::SColor pixelColor = color;
                pixelColor.setAlpha(static_cast<irr::u32>(alpha * color.getAlpha()));
                img->setPixel(baseX + x, baseY + y, pixelColor);
            }
        }
    };

    auto drawStar = [&](int tileX, int tileY, int points, irr::video::SColor color) {
        int baseX = tileX * tileSize;
        int baseY = tileY * tileSize;
        float centerX = tileSize / 2.0f;
        float centerY = tileSize / 2.0f;

        for (int y = 0; y < tileSize; ++y) {
            for (int x = 0; x < tileSize; ++x) {
                float dx = x - centerX + 0.5f;
                float dy = y - centerY + 0.5f;
                float dist = std::sqrt(dx * dx + dy * dy);
                float angle = std::atan2(dy, dx);

                float starRadius = 3.0f + 3.0f * std::pow(std::cos(angle * points), 2.0f);
                float alpha = 0.0f;

                if (dist < starRadius) {
                    alpha = 1.0f - dist / starRadius;
                    alpha = std::pow(alpha, 0.5f);
                }

                irr::video::SColor pixelColor = color;
                pixelColor.setAlpha(static_cast<irr::u32>(alpha * color.getAlpha()));
                img->setPixel(baseX + x, baseY + y, pixelColor);
            }
        }
    };

    // Tile 0: Soft circle (dust)
    drawCircle(0, 0, 0.8f, irr::video::SColor(200, 255, 255, 255));
    // Tile 1: Star shape (firefly)
    drawStar(1, 0, 4, irr::video::SColor(255, 200, 255, 100));
    // Tile 2: Wispy cloud (mist)
    drawCircle(2, 0, 0.9f, irr::video::SColor(100, 255, 255, 255));
    // Tile 3: Spore shape (pollen)
    drawCircle(3, 0, 0.5f, irr::video::SColor(220, 255, 255, 150));
    // Tile 4: Grain shape (sand)
    drawCircle(0, 1, 0.3f, irr::video::SColor(200, 220, 180, 120));
    // Tile 5: Leaf shape
    drawCircle(1, 1, 0.4f, irr::video::SColor(200, 100, 180, 80));
    // Tile 6: Snowflake
    drawStar(2, 1, 6, irr::video::SColor(255, 255, 255, 255));
    // Tile 7: Ember
    drawCircle(3, 1, 0.6f, irr::video::SColor(255, 255, 150, 50));

    // Tile 8: Foam spray
    drawCircle(0, 2, 0.7f, irr::video::SColor(230, 255, 255, 255));
    // Tile 9: Water droplet
    drawCircle(1, 2, 0.4f, irr::video::SColor(200, 200, 230, 255));

    // Tile 10: Ripple ring
    auto drawRing = [&](int tileX, int tileY, irr::video::SColor color) {
        int baseX = tileX * tileSize;
        int baseY = tileY * tileSize;
        float centerX = tileSize / 2.0f;
        float centerY = tileSize / 2.0f;
        float outerRadius = tileSize / 2.0f - 1.0f;
        float innerRadius = outerRadius * 0.7f;
        float ringWidth = outerRadius - innerRadius;

        for (int y = 0; y < tileSize; ++y) {
            for (int x = 0; x < tileSize; ++x) {
                float dx = x - centerX + 0.5f;
                float dy = y - centerY + 0.5f;
                float dist = std::sqrt(dx * dx + dy * dy);

                float alpha = 0.0f;
                if (dist >= innerRadius && dist <= outerRadius) {
                    float ringPos = (dist - innerRadius) / ringWidth;
                    alpha = 1.0f - std::abs(ringPos * 2.0f - 1.0f);
                    alpha = std::pow(alpha, 0.7f);
                }

                irr::video::SColor pixelColor = color;
                pixelColor.setAlpha(static_cast<irr::u32>(alpha * color.getAlpha()));
                img->setPixel(baseX + x, baseY + y, pixelColor);
            }
        }
    };
    drawRing(2, 2, irr::video::SColor(200, 220, 240, 255));

    // Tile 11: Snow patch
    auto drawSnowPatch = [&](int tileX, int tileY, irr::video::SColor color) {
        int baseX = tileX * tileSize;
        int baseY = tileY * tileSize;
        float centerX = tileSize / 2.0f;
        float centerY = tileSize / 2.0f;
        float baseRadius = tileSize / 2.0f - 2.0f;

        auto pseudoNoise = [](int x, int y, int seed) -> float {
            int n = x + y * 57 + seed;
            n = (n << 13) ^ n;
            return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f) * 0.5f + 0.5f;
        };

        for (int y = 0; y < tileSize; ++y) {
            for (int x = 0; x < tileSize; ++x) {
                float dx = x - centerX + 0.5f;
                float dy = y - centerY + 0.5f;
                float dist = std::sqrt(dx * dx + dy * dy);

                float noise = pseudoNoise(x, y, 42) * 2.0f;
                float radius = baseRadius + noise;

                float alpha = 0.0f;
                if (dist < radius) {
                    float edgeDist = radius - dist;
                    alpha = std::min(1.0f, edgeDist / (radius * 0.4f));
                    alpha *= 0.7f + 0.3f * pseudoNoise(x * 2, y * 2, 123);
                }

                irr::video::SColor pixelColor = color;
                pixelColor.setAlpha(static_cast<irr::u32>(alpha * color.getAlpha()));
                img->setPixel(baseX + x, baseY + y, pixelColor);
            }
        }
    };
    drawSnowPatch(3, 2, irr::video::SColor(220, 245, 250, 255));

    // Tile 12: Rain streak
    auto drawRainStreak = [&](int tileX, int tileY, irr::video::SColor color) {
        int baseX = tileX * tileSize;
        int baseY = tileY * tileSize;
        float centerX = tileSize / 2.0f;

        for (int y = 0; y < tileSize; ++y) {
            for (int x = 0; x < tileSize; ++x) {
                float dx = std::abs(x - centerX + 0.5f);

                float width = 1.5f;
                float yFactor = static_cast<float>(y) / tileSize;
                float localWidth = width * (0.5f + yFactor * 0.5f);

                float alpha = 0.0f;
                if (dx < localWidth) {
                    alpha = 1.0f - (dx / localWidth);
                    alpha = std::pow(alpha, 0.5f);
                    alpha *= 0.3f + 0.7f * yFactor;
                }

                irr::video::SColor pixelColor = color;
                pixelColor.setAlpha(static_cast<irr::u32>(alpha * color.getAlpha()));
                img->setPixel(baseX + x, baseY + y, pixelColor);
            }
        }
    };
    drawRainStreak(0, 3, irr::video::SColor(200, 200, 220, 255));

    return img;
}

// ============================================================
// Creature/Boids Atlas Generation
// ============================================================

irr::video::IImage* generateCreatureAtlas(irr::video::IVideoDriver* driver) {
    if (!driver) return nullptr;

    const int tileSize = 16;
    const int atlasColumns = 4;
    const int atlasRows = 4;
    const int atlasWidth = tileSize * atlasColumns;
    const int atlasHeight = tileSize * atlasRows;

    irr::video::IImage* img = driver->createImage(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(atlasWidth, atlasHeight));

    if (!img) return nullptr;

    img->fill(irr::video::SColor(0, 0, 0, 0));

    auto drawBird = [&](int tileX, int tileY, bool wingsUp, irr::video::SColor bodyColor) {
        int baseX = tileX * tileSize;
        int baseY = tileY * tileSize;
        int cx = baseX + tileSize / 2;
        int cy = baseY + tileSize / 2;

        // Body (oval)
        for (int y = -2; y <= 2; ++y) {
            for (int x = -4; x <= 4; ++x) {
                if (x * x / 16.0f + y * y / 4.0f <= 1.0f) {
                    img->setPixel(cx + x, cy + y, bodyColor);
                }
            }
        }

        // Wings
        int wingY = wingsUp ? -3 : 2;
        for (int x = -6; x <= 6; ++x) {
            int wx = cx + x;
            int wy = cy + wingY;
            if (std::abs(x) > 2) {
                img->setPixel(wx, wy, bodyColor);
                if (wingsUp) {
                    img->setPixel(wx, wy + 1, bodyColor);
                } else {
                    img->setPixel(wx, wy - 1, bodyColor);
                }
            }
        }
    };

    auto drawBat = [&](int tileX, int tileY, bool wingsUp, irr::video::SColor bodyColor) {
        int baseX = tileX * tileSize;
        int baseY = tileY * tileSize;
        int cx = baseX + tileSize / 2;
        int cy = baseY + tileSize / 2;

        // Body (small)
        for (int y = -1; y <= 1; ++y) {
            for (int x = -2; x <= 2; ++x) {
                if (std::abs(x) + std::abs(y) <= 2) {
                    img->setPixel(cx + x, cy + y, bodyColor);
                }
            }
        }

        // Wings (angular)
        int wingY = wingsUp ? -2 : 2;
        for (int x = -6; x <= 6; ++x) {
            int absX = std::abs(x);
            if (absX > 2) {
                int wy = cy + wingY * (wingsUp ? (absX - 2) / 4 : 1);
                img->setPixel(cx + x, wy, bodyColor);
                img->setPixel(cx + x, wy + (wingsUp ? 1 : -1), bodyColor);
            }
        }
    };

    auto drawButterfly = [&](int tileX, int tileY, bool wingsUp, irr::video::SColor color) {
        int baseX = tileX * tileSize;
        int baseY = tileY * tileSize;
        int cx = baseX + tileSize / 2;
        int cy = baseY + tileSize / 2;

        // Body (thin line)
        for (int y = -3; y <= 3; ++y) {
            img->setPixel(cx, cy + y, irr::video::SColor(255, 80, 60, 40));
        }

        // Wings (circles on each side)
        int wingOffset = wingsUp ? 3 : 5;
        for (int side = -1; side <= 1; side += 2) {
            int wcx = cx + side * wingOffset;
            for (int dy = -3; dy <= 3; ++dy) {
                for (int dx = -3; dx <= 3; ++dx) {
                    if (dx * dx + dy * dy <= 9) {
                        img->setPixel(wcx + dx, cy + dy, color);
                    }
                }
            }
        }
    };

    auto drawFirefly = [&](int tileX, int tileY, float glow, irr::video::SColor color) {
        int baseX = tileX * tileSize;
        int baseY = tileY * tileSize;

        float radius = 4.0f + glow * 2.0f;
        for (int y = 0; y < tileSize; ++y) {
            for (int x = 0; x < tileSize; ++x) {
                float dx = x - tileSize / 2.0f + 0.5f;
                float dy = y - tileSize / 2.0f + 0.5f;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < radius) {
                    float alpha = 1.0f - (dist / radius);
                    alpha = alpha * alpha;
                    irr::video::SColor c = color;
                    c.setAlpha(static_cast<irr::u32>(alpha * 255));
                    img->setPixel(baseX + x, baseY + y, c);
                }
            }
        }
    };

    // Row 0: Bird, Bat
    drawBird(0, 0, true, irr::video::SColor(255, 180, 140, 100));
    drawBird(1, 0, false, irr::video::SColor(255, 180, 140, 100));
    drawBat(2, 0, true, irr::video::SColor(255, 80, 60, 90));
    drawBat(3, 0, false, irr::video::SColor(255, 80, 60, 90));

    // Row 1: Butterfly, Dragonfly
    drawButterfly(0, 1, true, irr::video::SColor(255, 255, 200, 50));
    drawButterfly(1, 1, false, irr::video::SColor(255, 255, 200, 50));
    drawBird(2, 1, true, irr::video::SColor(255, 80, 180, 220));
    drawBird(3, 1, false, irr::video::SColor(255, 80, 180, 220));

    // Row 2: Crow, Seagull
    drawBird(0, 2, true, irr::video::SColor(255, 40, 40, 45));
    drawBird(1, 2, false, irr::video::SColor(255, 40, 40, 45));
    drawBird(2, 2, true, irr::video::SColor(255, 240, 240, 245));
    drawBird(3, 2, false, irr::video::SColor(255, 240, 240, 245));

    // Row 3: Firefly glow states
    drawFirefly(0, 3, 0.5f, irr::video::SColor(255, 200, 255, 50));
    drawFirefly(1, 3, 1.0f, irr::video::SColor(255, 220, 255, 80));

    return img;
}

// ============================================================
// Tumbleweed Texture Generation
// ============================================================

irr::video::IImage* generateTumbleweedTexture(irr::video::IVideoDriver* driver) {
    if (!driver) return nullptr;

    const int texSize = 64;
    irr::video::IImage* image = driver->createImage(
        irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(texSize, texSize));

    if (!image) return nullptr;

    irr::video::SColor branchColor(255, 160, 130, 80);
    irr::video::SColor branchDark(255, 120, 95, 55);
    irr::video::SColor transparent(0, 0, 0, 0);

    for (int y = 0; y < texSize; ++y) {
        for (int x = 0; x < texSize; ++x) {
            image->setPixel(x, y, transparent);
        }
    }

    const float centerX = texSize / 2.0f;
    const float centerY = texSize / 2.0f;
    const float maxRadius = texSize / 2.0f - 2.0f;

    // Use fixed seed for reproducibility
    std::mt19937 gen(42);
    std::uniform_real_distribution<float> angleJitter(-0.25f, 0.25f);
    std::uniform_real_distribution<float> lengthJitter(0.7f, 1.1f);
    std::uniform_real_distribution<float> waveJitter(0.1f, 0.25f);
    std::uniform_real_distribution<float> branchOffsetJitter(0.3f, 0.6f);
    std::uniform_int_distribution<int> colorChoice(0, 1);

    const int numSpokes = 11;
    for (int spoke = 0; spoke < numSpokes; ++spoke) {
        float baseAngle = (spoke / (float)numSpokes) * 6.28318f + angleJitter(gen);
        float spokeLength = maxRadius * lengthJitter(gen);
        float waveAmount = waveJitter(gen);
        irr::video::SColor color = (colorChoice(gen) == 0) ? branchColor : branchDark;

        float angle = baseAngle;
        float startRadius = 3.0f + angleJitter(gen) * 4.0f;
        for (float r = startRadius; r < spokeLength; r += 1.0f) {
            angle = baseAngle + std::sin(r * 0.3f + spoke) * waveAmount;

            int px = static_cast<int>(centerX + std::cos(angle) * r);
            int py = static_cast<int>(centerY + std::sin(angle) * r);

            if (px >= 0 && px < texSize && py >= 0 && py < texSize) {
                image->setPixel(px, py, color);
            }
        }

        float branchStart = spokeLength * branchOffsetJitter(gen);
        float branchAngle = baseAngle + 0.3f + angleJitter(gen);
        float branchLen = spokeLength * 0.3f * lengthJitter(gen);
        for (float r = 0; r < branchLen; r += 1.0f) {
            int px = static_cast<int>(centerX + std::cos(baseAngle) * branchStart + std::cos(branchAngle) * r);
            int py = static_cast<int>(centerY + std::sin(baseAngle) * branchStart + std::sin(branchAngle) * r);

            if (px >= 0 && px < texSize && py >= 0 && py < texSize) {
                image->setPixel(px, py, color);
            }
        }

        if (spoke % 3 == 0) {
            branchStart = spokeLength * branchOffsetJitter(gen);
            branchAngle = baseAngle - 0.4f + angleJitter(gen);
            branchLen = spokeLength * 0.25f * lengthJitter(gen);
            for (float r = 0; r < branchLen; r += 1.0f) {
                int px = static_cast<int>(centerX + std::cos(baseAngle) * branchStart + std::cos(branchAngle) * r);
                int py = static_cast<int>(centerY + std::sin(baseAngle) * branchStart + std::sin(branchAngle) * r);

                if (px >= 0 && px < texSize && py >= 0 && py < texSize) {
                    image->setPixel(px, py, color);
                }
            }
        }
    }

    // Irregular arcs
    for (int ring = 0; ring < 2; ++ring) {
        float baseRadius = maxRadius * (0.45f + ring * 0.35f);
        irr::video::SColor color = (ring == 0) ? branchDark : branchColor;

        for (int arc = 0; arc < 3; ++arc) {
            float startAngle = arc * 2.1f + angleJitter(gen);
            float arcLength = 0.5f + lengthJitter(gen) * 0.4f;

            for (float a = startAngle; a < startAngle + arcLength; a += 0.05f) {
                float radius = baseRadius + std::sin(a * 3.0f) * 2.0f;
                int px = static_cast<int>(centerX + std::cos(a) * radius);
                int py = static_cast<int>(centerY + std::sin(a) * radius);

                if (px >= 0 && px < texSize && py >= 0 && py < texSize) {
                    image->setPixel(px, py, color);
                }
            }
        }
    }

    return image;
}

// ============================================================
// Storm Cloud Frame Generation (Perlin noise)
// ============================================================

namespace {

float noise2D(float x, float y, int seed) {
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;

    int n = xi + yi * 57 + seed;
    n = (n << 13) ^ n;
    return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

float smoothNoise2D(float x, float y, int seed) {
    float corners = (noise2D(x - 1, y - 1, seed) + noise2D(x + 1, y - 1, seed) +
                     noise2D(x - 1, y + 1, seed) + noise2D(x + 1, y + 1, seed)) / 16.0f;
    float sides = (noise2D(x - 1, y, seed) + noise2D(x + 1, y, seed) +
                   noise2D(x, y - 1, seed) + noise2D(x, y + 1, seed)) / 8.0f;
    float center = noise2D(x, y, seed) / 4.0f;

    return corners + sides + center;
}

float interpolatedNoise2D(float x, float y, int seed) {
    int intX = static_cast<int>(std::floor(x));
    int intY = static_cast<int>(std::floor(y));
    float fracX = x - intX;
    float fracY = y - intY;

    float fx = (1.0f - std::cos(fracX * 3.14159f)) * 0.5f;
    float fy = (1.0f - std::cos(fracY * 3.14159f)) * 0.5f;

    float v1 = smoothNoise2D(static_cast<float>(intX), static_cast<float>(intY), seed);
    float v2 = smoothNoise2D(static_cast<float>(intX + 1), static_cast<float>(intY), seed);
    float v3 = smoothNoise2D(static_cast<float>(intX), static_cast<float>(intY + 1), seed);
    float v4 = smoothNoise2D(static_cast<float>(intX + 1), static_cast<float>(intY + 1), seed);

    float i1 = v1 * (1.0f - fx) + v2 * fx;
    float i2 = v3 * (1.0f - fx) + v4 * fx;

    return i1 * (1.0f - fy) + i2 * fy;
}

float seamlessPerlinNoise2D(float x, float y, int seed, int octaves, float persistence) {
    const float PI2 = 6.28318530718f;

    float angleX = x * PI2;
    float angleY = y * PI2;

    float nx = std::cos(angleX);
    float ny = std::sin(angleX);
    float nz = std::cos(angleY);
    float nw = std::sin(angleY);

    float total = 0.0f;
    float frequency = 2.0f;
    float amplitude = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        float fx = nx * frequency;
        float fy = ny * frequency;
        float fz = nz * frequency;
        float fw = nw * frequency;

        float n1 = interpolatedNoise2D(fx + fz, fy + fw, seed + i * 1000);
        float n2 = interpolatedNoise2D(fx - fz, fy - fw, seed + i * 1000 + 500);
        float n3 = interpolatedNoise2D(fx + fw, fy + fz, seed + i * 1000 + 250);

        float n = (n1 + n2 + n3) / 3.0f;

        total += n * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }

    return (total / maxValue + 1.0f) * 0.5f;
}

} // anonymous namespace

irr::video::IImage* generateCloudFrame(irr::video::IVideoDriver* driver,
                                        int seed,
                                        int size,
                                        int octaves,
                                        float persistence,
                                        float cloudColorR,
                                        float cloudColorG,
                                        float cloudColorB) {
    if (!driver) return nullptr;

    irr::video::IImage* image = driver->createImage(irr::video::ECF_A8R8G8B8,
                                                      irr::core::dimension2d<irr::u32>(size, size));
    if (!image) return nullptr;

    uint8_t r = static_cast<uint8_t>(cloudColorR * 255.0f);
    uint8_t g = static_cast<uint8_t>(cloudColorG * 255.0f);
    uint8_t b = static_cast<uint8_t>(cloudColorB * 255.0f);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float nx = static_cast<float>(x) / size;
            float ny = static_cast<float>(y) / size;

            float noise = seamlessPerlinNoise2D(nx, ny, seed, octaves, persistence);

            noise = std::max(0.0f, noise - 0.35f) * 1.6f;
            noise = std::min(1.0f, noise);

            uint8_t alpha = static_cast<uint8_t>(noise * 255.0f);

            image->setPixel(x, y, irr::video::SColor(alpha, r, g, b));
        }
    }

    return image;
}

} // namespace Graphics
} // namespace EQT
