#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/zone_biome.h"
#include "client/graphics/environment/emitters/dust_mote_emitter.h"
#include "client/graphics/environment/emitters/pollen_emitter.h"
#include "client/graphics/environment/emitters/firefly_emitter.h"
#include "client/graphics/environment/emitters/mist_emitter.h"
#include "client/graphics/environment/emitters/sand_dust_emitter.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace EQT {
namespace Graphics {
namespace Environment {

ParticleManager::ParticleManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver)
    : smgr_(smgr)
    , driver_(driver)
{
    // Initialize all types as enabled
    for (size_t i = 0; i < static_cast<size_t>(ParticleType::Count); ++i) {
        typeEnabled_[i] = true;
    }

    // Set default budget based on medium quality
    budget_ = ParticleBudget::fromQuality(EffectQuality::Medium);
}

ParticleManager::~ParticleManager() {
    clearEmitters();
}

bool ParticleManager::init(const std::string& eqClientPath) {
    if (initialized_) {
        return true;
    }

    // Try to load particle atlas texture, or create a default one
    std::string atlasPath = eqClientPath + "/data/textures/particle_atlas.png";
    if (!loadParticleAtlas(atlasPath)) {
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Creating procedural particle atlas");
        createDefaultAtlas();
    }

    // Set up particle material
    particleMaterial_.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
    particleMaterial_.setTexture(0, atlasTexture_);
    particleMaterial_.Lighting = false;
    particleMaterial_.ZWriteEnable = false;
    particleMaterial_.BackfaceCulling = false;
    particleMaterial_.FogEnable = false;

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "ParticleManager initialized");
    return true;
}

void ParticleManager::update(float deltaTime) {
    if (!enabled_ || !initialized_ || quality_ == EffectQuality::Off) {
        return;
    }

    // Update all emitters
    for (auto& emitter : emitters_) {
        if (emitter && isTypeEnabled(emitter->getType())) {
            emitter->update(deltaTime, envState_);
        }
    }
}

void ParticleManager::render() {
    if (!enabled_ || !initialized_ || quality_ == EffectQuality::Off || !driver_) {
        return;
    }

    // Get camera info for billboard orientation
    irr::scene::ICameraSceneNode* camera = smgr_->getActiveCamera();
    if (!camera) {
        return;
    }

    irr::core::vector3df cameraPos = camera->getAbsolutePosition();
    irr::core::vector3df cameraUp = camera->getUpVector();

    // Set up rendering state
    driver_->setMaterial(particleMaterial_);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    // Render all particles from all emitters
    for (const auto& emitter : emitters_) {
        if (!emitter || !isTypeEnabled(emitter->getType())) {
            continue;
        }

        for (const Particle& p : emitter->getParticles()) {
            if (p.isAlive()) {
                // Distance culling
                glm::vec3 diff = p.position - envState_.playerPosition;
                float distSq = glm::dot(diff, diff);
                if (distSq > budget_.cullDistance * budget_.cullDistance) {
                    continue;
                }

                renderBillboard(p, cameraPos, cameraUp);
            }
        }
    }
}

void ParticleManager::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Entering zone '{}' with biome {}",
             zoneName, static_cast<int>(biome));

    currentZoneName_ = zoneName;
    currentBiome_ = biome;

    // Clear existing emitters and set up new ones for this biome
    clearEmitters();
    setupEmittersForBiome(biome);

    // Notify all emitters of zone entry
    for (auto& emitter : emitters_) {
        if (emitter) {
            emitter->onZoneEnter(zoneName, biome);
        }
    }
}

void ParticleManager::onZoneLeave() {
    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Leaving zone '{}'", currentZoneName_);

    // Notify all emitters
    for (auto& emitter : emitters_) {
        if (emitter) {
            emitter->onZoneLeave();
        }
    }

    currentZoneName_.clear();
    currentBiome_ = ZoneBiome::Unknown;
}

void ParticleManager::setQuality(EffectQuality quality) {
    quality_ = quality;
    budget_ = ParticleBudget::fromQuality(quality);

    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Quality set to {}, budget={}",
              static_cast<int>(quality), budget_.maxTotal);
}

void ParticleManager::setDensity(float density) {
    userDensity_ = glm::clamp(density, 0.0f, 1.0f);

    // Update all emitters with new density
    float effectiveDensity = userDensity_ * budget_.densityMult;
    for (auto& emitter : emitters_) {
        if (emitter) {
            emitter->setDensityMultiplier(effectiveDensity);
        }
    }
}

void ParticleManager::setTypeEnabled(ParticleType type, bool enabled) {
    if (type < ParticleType::Count) {
        typeEnabled_[static_cast<size_t>(type)] = enabled;

        // Update emitters of this type
        for (auto& emitter : emitters_) {
            if (emitter && emitter->getType() == type) {
                emitter->setEnabled(enabled);
            }
        }
    }
}

bool ParticleManager::isTypeEnabled(ParticleType type) const {
    if (type < ParticleType::Count) {
        return typeEnabled_[static_cast<size_t>(type)];
    }
    return false;
}

void ParticleManager::setTimeOfDay(float hour) {
    envState_.timeOfDay = std::fmod(hour, 24.0f);
    if (envState_.timeOfDay < 0.0f) {
        envState_.timeOfDay += 24.0f;
    }
}

void ParticleManager::setWeather(WeatherType weather) {
    envState_.weather = weather;
}

void ParticleManager::setWind(const glm::vec3& direction, float strength) {
    envState_.windDirection = glm::length(direction) > 0.0f ?
        glm::normalize(direction) : glm::vec3(1.0f, 0.0f, 0.0f);
    envState_.windStrength = glm::clamp(strength, 0.0f, 1.0f);
}

void ParticleManager::setPlayerPosition(const glm::vec3& pos, float heading) {
    envState_.playerPosition = pos;
    envState_.playerHeading = heading;
}

int ParticleManager::getTotalActiveParticles() const {
    int total = 0;
    for (const auto& emitter : emitters_) {
        if (emitter) {
            total += emitter->getActiveCount();
        }
    }
    return total;
}

std::string ParticleManager::getDebugInfo() const {
    std::ostringstream ss;
    ss << "Particles: " << getTotalActiveParticles() << "/" << budget_.maxTotal;
    ss << " | Quality: ";
    switch (quality_) {
        case EffectQuality::Off: ss << "Off"; break;
        case EffectQuality::Low: ss << "Low"; break;
        case EffectQuality::Medium: ss << "Med"; break;
        case EffectQuality::High: ss << "High"; break;
    }
    ss << " | Biome: " << static_cast<int>(currentBiome_);
    ss << " | Time: " << static_cast<int>(envState_.timeOfDay) << ":00";
    return ss.str();
}

void ParticleManager::setupEmittersForBiome(ZoneBiome biome) {
    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Setting up emitters for biome {}",
              static_cast<int>(biome));

    // Calculate effective density
    float effectiveDensity = userDensity_ * budget_.densityMult;

    // Check zone properties for water, etc.
    const auto& detector = ZoneBiomeDetector::instance();
    bool zoneHasWater = detector.hasWater(currentZoneName_);

    // Helper to add an emitter with common setup
    auto addEmitter = [&](std::unique_ptr<ParticleEmitter> emitter) {
        if (emitter) {
            emitter->setDensityMultiplier(effectiveDensity);
            emitter->setEnabled(isTypeEnabled(emitter->getType()));
            emitters_.push_back(std::move(emitter));
        }
    };

    // Set up emitters based on biome
    switch (biome) {
        case ZoneBiome::Forest:
            // Forests: pollen (day), fireflies (night), dust motes
            addEmitter(std::make_unique<PollenEmitter>());
            addEmitter(std::make_unique<FireflyEmitter>());
            addEmitter(std::make_unique<DustMoteEmitter>());
            if (zoneHasWater) {
                addEmitter(std::make_unique<MistEmitter>());
            }
            break;

        case ZoneBiome::Swamp:
            // Swamps: heavy mist, fireflies, pollen
            addEmitter(std::make_unique<MistEmitter>());
            addEmitter(std::make_unique<FireflyEmitter>());
            addEmitter(std::make_unique<PollenEmitter>());
            break;

        case ZoneBiome::Desert:
            // Deserts: blowing sand, dust motes
            addEmitter(std::make_unique<SandDustEmitter>());
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        case ZoneBiome::Plains:
            // Plains: pollen, dust motes, occasional mist near water
            addEmitter(std::make_unique<PollenEmitter>());
            addEmitter(std::make_unique<DustMoteEmitter>());
            if (zoneHasWater) {
                addEmitter(std::make_unique<MistEmitter>());
                addEmitter(std::make_unique<FireflyEmitter>());
            }
            break;

        case ZoneBiome::Ocean:
            // Ocean/coastal: mist, dust motes
            addEmitter(std::make_unique<MistEmitter>());
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        case ZoneBiome::Dungeon:
            // Dungeons: dust motes (primary atmospheric effect)
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        case ZoneBiome::Cave:
            // Caves: dust motes, mist in some areas
            addEmitter(std::make_unique<DustMoteEmitter>());
            addEmitter(std::make_unique<MistEmitter>());
            break;

        case ZoneBiome::Urban:
            // Cities: dust motes
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        case ZoneBiome::Snow:
            // Snow zones: dust motes for now (snowflake emitter future)
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        case ZoneBiome::Volcanic:
            // Volcanic zones: dust motes (ember emitter future)
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        default:
            // Unknown biome: just dust motes
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;
    }

    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Created {} emitters", emitters_.size());
}

void ParticleManager::clearEmitters() {
    emitters_.clear();
}

bool ParticleManager::loadParticleAtlas(const std::string& path) {
    if (!driver_) {
        return false;
    }

    atlasTexture_ = driver_->getTexture(path.c_str());
    return atlasTexture_ != nullptr;
}

void ParticleManager::createDefaultAtlas() {
    if (!driver_) {
        return;
    }

    // Create a procedural particle atlas (64x32 with 4x2 tiles of 16x16 each)
    const int tileSize = 16;
    const int atlasWidth = tileSize * ParticleAtlas::AtlasColumns;
    const int atlasHeight = tileSize * ParticleAtlas::AtlasRows;

    // Create image
    irr::video::IImage* img = driver_->createImage(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(atlasWidth, atlasHeight));

    if (!img) {
        LOG_ERROR(MOD_GRAPHICS, "ParticleManager: Failed to create atlas image");
        return;
    }

    // Clear to transparent
    img->fill(irr::video::SColor(0, 0, 0, 0));

    // Draw each tile
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
                    // Soft edge falloff
                    float edgeDist = radius - dist;
                    alpha = std::min(1.0f, edgeDist / (radius * softness));
                    alpha = alpha * alpha;  // Quadratic falloff for softer look
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

                // Create star shape
                float starRadius = 3.0f + 3.0f * std::pow(std::cos(angle * points), 2.0f);
                float alpha = 0.0f;

                if (dist < starRadius) {
                    alpha = 1.0f - dist / starRadius;
                    alpha = std::pow(alpha, 0.5f);  // Softer falloff
                }

                irr::video::SColor pixelColor = color;
                pixelColor.setAlpha(static_cast<irr::u32>(alpha * color.getAlpha()));
                img->setPixel(baseX + x, baseY + y, pixelColor);
            }
        }
    };

    // Tile 0: Soft circle (dust) - white, very soft edges
    drawCircle(0, 0, 0.8f, irr::video::SColor(200, 255, 255, 255));

    // Tile 1: Star shape (firefly) - yellow-green glow
    drawStar(1, 0, 4, irr::video::SColor(255, 200, 255, 100));

    // Tile 2: Wispy cloud (mist) - white, very soft, large
    drawCircle(2, 0, 0.9f, irr::video::SColor(100, 255, 255, 255));

    // Tile 3: Spore shape (pollen) - yellow-green, medium soft
    drawCircle(3, 0, 0.5f, irr::video::SColor(220, 255, 255, 150));

    // Tile 4: Grain shape (sand) - tan/brown
    drawCircle(0, 1, 0.3f, irr::video::SColor(200, 220, 180, 120));

    // Tile 5: Leaf shape - green (simplified as oval)
    drawCircle(1, 1, 0.4f, irr::video::SColor(200, 100, 180, 80));

    // Tile 6: Snowflake - white, crisp
    drawStar(2, 1, 6, irr::video::SColor(255, 255, 255, 255));

    // Tile 7: Ember - orange-red glow
    drawCircle(3, 1, 0.6f, irr::video::SColor(255, 255, 150, 50));

    // Create texture from image
    atlasTexture_ = driver_->addTexture("particle_atlas", img);
    img->drop();

    if (atlasTexture_) {
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Created procedural particle atlas");
    } else {
        LOG_ERROR(MOD_GRAPHICS, "ParticleManager: Failed to create atlas texture");
    }
}

void ParticleManager::renderBillboard(const Particle& p,
                                       const irr::core::vector3df& cameraPos,
                                       const irr::core::vector3df& cameraUp)
{
    // Convert particle position to Irrlicht coordinates (EQ uses Z-up, Irrlicht uses Y-up)
    irr::core::vector3df pos(p.position.x, p.position.z, p.position.y);

    // Calculate billboard orientation
    irr::core::vector3df toCamera = cameraPos - pos;
    toCamera.normalize();

    irr::core::vector3df right = cameraUp.crossProduct(toCamera);
    right.normalize();

    irr::core::vector3df up = toCamera.crossProduct(right);
    up.normalize();

    // Apply rotation
    if (std::abs(p.rotation) > 0.001f) {
        float cosR = std::cos(p.rotation);
        float sinR = std::sin(p.rotation);
        irr::core::vector3df newRight = right * cosR + up * sinR;
        irr::core::vector3df newUp = up * cosR - right * sinR;
        right = newRight;
        up = newUp;
    }

    // Calculate quad vertices
    float halfSize = p.size * 0.5f;
    irr::core::vector3df v0 = pos + (-right + up) * halfSize;
    irr::core::vector3df v1 = pos + (right + up) * halfSize;
    irr::core::vector3df v2 = pos + (right - up) * halfSize;
    irr::core::vector3df v3 = pos + (-right - up) * halfSize;

    // Get UV coordinates for this particle's texture tile
    float u0, v0Uv, u1, v1Uv;
    getAtlasUVs(p.textureIndex, u0, v0Uv, u1, v1Uv);

    // Calculate final color with alpha
    irr::u32 alpha = static_cast<irr::u32>(p.alpha * p.color.a * 255.0f);
    irr::u32 r = static_cast<irr::u32>(p.color.r * 255.0f);
    irr::u32 g = static_cast<irr::u32>(p.color.g * 255.0f);
    irr::u32 b = static_cast<irr::u32>(p.color.b * 255.0f);
    irr::video::SColor color(alpha, r, g, b);

    // Create vertices
    irr::video::S3DVertex vertices[4];
    vertices[0] = irr::video::S3DVertex(v0, toCamera, color, irr::core::vector2df(u0, v0Uv));
    vertices[1] = irr::video::S3DVertex(v1, toCamera, color, irr::core::vector2df(u1, v0Uv));
    vertices[2] = irr::video::S3DVertex(v2, toCamera, color, irr::core::vector2df(u1, v1Uv));
    vertices[3] = irr::video::S3DVertex(v3, toCamera, color, irr::core::vector2df(u0, v1Uv));

    // Create indices for two triangles
    irr::u16 indices[6] = {0, 1, 2, 0, 2, 3};

    // Draw the quad
    driver_->drawVertexPrimitiveList(vertices, 4, indices, 2,
                                      irr::video::EVT_STANDARD,
                                      irr::scene::EPT_TRIANGLES,
                                      irr::video::EIT_16BIT);
}

void ParticleManager::getAtlasUVs(uint8_t tileIndex, float& u0, float& v0, float& u1, float& v1) const {
    // Calculate tile position in atlas
    int col = tileIndex % ParticleAtlas::AtlasColumns;
    int row = tileIndex / ParticleAtlas::AtlasColumns;

    float tileWidth = 1.0f / ParticleAtlas::AtlasColumns;
    float tileHeight = 1.0f / ParticleAtlas::AtlasRows;

    u0 = col * tileWidth;
    v0 = row * tileHeight;
    u1 = u0 + tileWidth;
    v1 = v0 + tileHeight;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
