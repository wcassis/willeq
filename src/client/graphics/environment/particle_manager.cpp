#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/environment_config.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/zone_biome.h"
#include "client/graphics/environment/emitters/dust_mote_emitter.h"
#include "client/graphics/environment/emitters/pollen_emitter.h"
#include "client/graphics/environment/emitters/firefly_emitter.h"
#include "client/graphics/environment/emitters/mist_emitter.h"
#include "client/graphics/environment/emitters/sand_dust_emitter.h"
#include "client/graphics/environment/emitters/ember_emitter.h"
#include "client/graphics/environment/emitters/smoke_emitter.h"
#ifdef EQT_HAS_GLES2
#include "client/graphics/environment/unified_particle_renderer.h"
#include <GLES2/gl2.h>
#endif
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <random>

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

    // Load environment effects config
    EnvironmentEffectsConfig::instance().load("config/environment_effects.json");

    // Load spell effects config
    SpellEffectsConfig::instance().load("config/spell_effects.json");

    // Load particle atlas texture from project's data directory
    std::string atlasPath = "data/textures/particle_atlas.png";
    if (!loadParticleAtlas(atlasPath)) {
        LOG_WARN(MOD_GRAPHICS, "ParticleManager: Atlas not found at {}, disabling particles. Run generate_textures tool.", atlasPath);
        enabled_ = false;
        return true;
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

    // Render external emitters (e.g., weather effects)
    for (const auto* emitter : externalEmitters_) {
        if (!emitter || !emitter->isEnabled()) {
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

    // Clear unified fire emitters
    clearUnifiedEmitters();

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

void ParticleManager::reloadSettings() {
    // Reload the config file
    if (!EnvironmentEffectsConfig::instance().reload()) {
        LOG_WARN(MOD_GRAPHICS, "ParticleManager: Failed to reload config");
        return;
    }

    // Update all emitters with new settings
    for (auto& emitter : emitters_) {
        if (emitter) {
            emitter->reloadSettings();
        }
    }

    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Reloaded settings for {} emitters", emitters_.size());
}

void ParticleManager::setSurfaceMap(const Detail::SurfaceMap* surfaceMap) {
    surfaceMap_ = surfaceMap;

    // Propagate to all emitters that need terrain data
    for (auto& emitter : emitters_) {
        if (emitter) {
            emitter->setSurfaceMap(surfaceMap);
        }
    }

    if (surfaceMap) {
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Set surface map, propagated to {} emitters",
                  emitters_.size());
    }
}

void ParticleManager::registerExternalEmitter(ParticleEmitter* emitter) {
    if (!emitter) return;

    // Check if already registered
    auto it = std::find(externalEmitters_.begin(), externalEmitters_.end(), emitter);
    if (it == externalEmitters_.end()) {
        externalEmitters_.push_back(emitter);
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Registered external emitter");
    }
}

void ParticleManager::unregisterExternalEmitter(ParticleEmitter* emitter) {
    if (!emitter) return;

    auto it = std::find(externalEmitters_.begin(), externalEmitters_.end(), emitter);
    if (it != externalEmitters_.end()) {
        externalEmitters_.erase(it);
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Unregistered external emitter");
    }
}

void ParticleManager::setFireSources(const std::vector<glm::vec3>& positions) {
    for (auto& emitter : emitters_) {
        if (emitter->getType() == ParticleType::Ember) {
            static_cast<EmberEmitter*>(emitter.get())->setFireSources(positions);
        } else if (emitter->getType() == ParticleType::Smoke) {
            static_cast<SmokeEmitter*>(emitter.get())->setFireSources(positions);
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Set {} fire sources for ember/smoke emitters", positions.size());
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

    // Fire emitters (embers + smoke) - added to all biomes, activated when fire sources are set
    addEmitter(std::make_unique<EmberEmitter>());
    addEmitter(std::make_unique<SmokeEmitter>());

    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Created {} emitters", emitters_.size());
}

void ParticleManager::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;

    if (!enabled) {
        clearEmitters();
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Disabled, emitters released");
    } else if (!currentZoneName_.empty()) {
        setupEmittersForBiome(currentBiome_);
        for (auto& emitter : emitters_) {
            if (emitter) emitter->onZoneEnter(currentZoneName_, currentBiome_);
        }
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Re-enabled, {} emitters recreated", emitters_.size());
    }
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

void ParticleManager::renderBillboard(const Particle& p,
                                       const irr::core::vector3df& cameraPos,
                                       const irr::core::vector3df& cameraUp)
{
    // Convert particle position to Irrlicht coordinates (EQ uses Z-up, Irrlicht uses Y-up)
    irr::core::vector3df pos(p.position.x, p.position.z, p.position.y);

    irr::core::vector3df right, up;
    irr::core::vector3df toCamera = cameraPos - pos;
    toCamera.normalize();

    if (p.velocityAligned && glm::length(p.velocity) > 0.1f) {
        // Velocity-aligned billboard (for rain streaks, etc.)
        // Convert velocity to Irrlicht coordinates
        irr::core::vector3df vel(p.velocity.x, p.velocity.z, p.velocity.y);
        vel.normalize();

        // Up direction is the velocity direction (particles fall downward)
        up = vel;

        // Right is perpendicular to both velocity and camera direction
        right = up.crossProduct(toCamera);
        right.normalize();

        // Ensure right is perpendicular
        if (right.getLength() < 0.001f) {
            // Velocity is pointing at camera, use world up
            right = irr::core::vector3df(1, 0, 0);
        }
    } else {
        // Standard camera-facing billboard
        right = cameraUp.crossProduct(toCamera);
        right.normalize();

        up = toCamera.crossProduct(right);
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
    }

    // Calculate quad vertices with stretch support
    float halfWidth = p.size * 0.5f;
    float halfHeight = p.size * p.stretch * 0.5f;
    irr::core::vector3df v0 = pos + (-right * halfWidth + up * halfHeight);
    irr::core::vector3df v1 = pos + (right * halfWidth + up * halfHeight);
    irr::core::vector3df v2 = pos + (right * halfWidth - up * halfHeight);
    irr::core::vector3df v3 = pos + (-right * halfWidth - up * halfHeight);

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

// =============================================================================
// Unified Particle System (GLES2 point sprites)
// =============================================================================

// Thread-local RNG for particle randomization
static std::mt19937& getParticleRNG() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng;
}

static float randomFloat(float minVal, float maxVal) {
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(getParticleRNG());
}

static int randomInt(int minVal, int maxVal) {
    std::uniform_int_distribution<int> dist(minVal, maxVal);
    return dist(getParticleRNG());
}

bool ParticleManager::initUnifiedRenderer(int poolSize) {
#ifdef EQT_HAS_GLES2
    if (unifiedRendererInitialized_) return true;

    // Determine pool size: explicit param > config > default 1024
    int actualPoolSize = poolSize;
    if (actualPoolSize <= 0) {
        actualPoolSize = SpellEffectsConfig::instance().getGlobal().maxParticles;
    }
    if (actualPoolSize <= 0) {
        actualPoolSize = 1024;
    }

    // Allocate the fixed particle pool
    unifiedPool_.resize(actualPoolSize);
    freeList_.resize(actualPoolSize);
    for (int i = 0; i < actualPoolSize; ++i) {
        freeList_[i] = static_cast<uint16_t>(i);
        unifiedPool_[i].setAlive(false);
    }
    unifiedActiveCount_ = 0;
    unifiedRenderBuf_.reserve(actualPoolSize);

    // Create and init the GLES2 renderer (self-contained, uses raw GL calls)
    unifiedRenderer_ = std::make_unique<UnifiedParticleRenderer>();
    if (!unifiedRenderer_->init()) {
        LOG_WARN(MOD_GRAPHICS, "ParticleManager: Failed to init unified particle renderer");
        unifiedRenderer_.reset();
        return false;
    }

    unifiedRendererInitialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Unified particle system initialized (pool: {})", actualPoolSize);
    return true;
#else
    return false;
#endif
}

int ParticleManager::allocateUnifiedParticle() {
    if (freeList_.empty()) return -1;
    int idx = freeList_.back();
    freeList_.pop_back();
    unifiedActiveCount_++;
    return idx;
}

void ParticleManager::freeUnifiedParticle(int index) {
    if (index < 0 || index >= static_cast<int>(unifiedPool_.size())) return;
    unifiedPool_[index].setAlive(false);
    freeList_.push_back(static_cast<uint16_t>(index));
    unifiedActiveCount_--;
}

void ParticleManager::updateUnified(float deltaTime, const glm::vec3& cameraPos) {
    if (!unifiedRendererInitialized_) return;
    if (deltaTime <= 0.0f || deltaTime > 1.0f) return;  // Safety clamp

    // Get camera frustum for emitter culling
    irr::scene::ICameraSceneNode* camera = smgr_ ? smgr_->getActiveCamera() : nullptr;
    const irr::scene::SViewFrustum* frustum = camera ? camera->getViewFrustum() : nullptr;

    // Periodic debug: log update stats
    static int updateLogCounter = 0;
    int emittersActive = 0, emittersCulled = 0, totalSpawned = 0;

    // Update emitters: spawn new particles
    for (auto& [id, emitter] : unifiedEmitters_) {
        if (!emitter.active) continue;

        // Gate fire emitters by unifiedFireEnabled_; weather emitters always run
        bool isWeather = (emitter.config.motionType == MotionType::CAMERA_RELATIVE);
        if (!isWeather && !unifiedFireEnabled_) continue;

        emittersActive++;

        // Check emitter lifetime
        if (emitter.config.emitterLifetime > 0.0f) {
            emitter.emitterAge += deltaTime;
            if (emitter.emitterAge >= emitter.config.emitterLifetime) {
                emitter.active = false;
                continue;
            }
        }

        // Ramp transition alpha for weather emitters
        if (isWeather && emitter.transitionAlpha < 1.0f) {
            emitter.transitionAlpha += emitter.transitionRate * deltaTime;
            if (emitter.transitionAlpha > 1.0f) emitter.transitionAlpha = 1.0f;
        }

        if (isWeather) {
            // === CAMERA_RELATIVE: target-count spawning ===
            // Move emitter to track camera
            emitter.position = cameraPos;

            // Count alive particles for this emitter
            int aliveCount = 0;
            for (const auto& p : unifiedPool_) {
                if (p.isAlive() && p.emitterID == emitter.emitterID) {
                    aliveCount++;
                }
            }

            // Spawn deficit to reach target count (modulated by transition alpha)
            int effectiveTarget = static_cast<int>(emitter.config.targetCount * emitter.transitionAlpha);
            int deficit = effectiveTarget - aliveCount;
            for (int s = 0; s < deficit; ++s) {
                spawnWeatherParticle(emitter.config, emitter.emitterID, cameraPos, emitter.transitionAlpha);
                totalSpawned++;
            }
        } else {
            // === Non-weather spawning (fire, spell effects) ===

            // Frustum cull emitters: skip spawning if outside view
            if (frustum) {
                irr::core::aabbox3df emitterBox(
                    emitter.position.x - 2.0f, emitter.position.y - 2.0f, emitter.position.z - 2.0f,
                    emitter.position.x + 2.0f, emitter.position.y + 8.0f, emitter.position.z + 2.0f);
                if (!frustum->getBoundingBox().intersectsWithBox(emitterBox)) {
                    emittersCulled++;
                    continue;
                }
            }

            // Resolve dynamic direction for spray emitters
            const glm::vec3* dirPtr = nullptr;
            if (emitter.useDynamicDirection) {
                // Update direction from callback each frame
                if (entityDirCallback_ && emitter.attachEntityID != 0) {
                    entityDirCallback_(emitter.attachEntityID, emitter.dynamicDirection);
                }
                dirPtr = &emitter.dynamicDirection;
            }

            // BURST / RADIAL_EXPAND: one-shot spawn, all at once
            if (emitter.config.burstCount > 0 && !emitter.isBurstSpawned) {
                for (int s = 0; s < emitter.config.burstCount; ++s) {
                    spawnSpellParticle(emitter.config, emitter.emitterID,
                                       emitter.position + emitter.attachOffset, dirPtr);
                    totalSpawned++;
                }
                emitter.isBurstSpawned = true;
            }

            // Spawn-rate spawning (LINEAR fire, ORBITAL spell effects)
            if (emitter.config.spawnRate > 0.0f) {
                emitter.spawnAccumulator += emitter.config.spawnRate * deltaTime;
                int toSpawn = static_cast<int>(emitter.spawnAccumulator);
                emitter.spawnAccumulator -= static_cast<float>(toSpawn);

                for (int s = 0; s < toSpawn; ++s) {
                    spawnSpellParticle(emitter.config, emitter.emitterID,
                                       emitter.position + emitter.attachOffset, dirPtr);
                    totalSpawned++;
                }
            }
        }
    }

    // Update all alive particles
    for (auto& p : unifiedPool_) {
        if (!p.isAlive()) continue;

        p.age += deltaTime;
        if (p.age >= p.maxLifetime) {
            // Kill particle — CAMERA_RELATIVE will respawn via deficit
            int idx = static_cast<int>(&p - unifiedPool_.data());
            freeUnifiedParticle(idx);
            continue;
        }

        float t = p.getNormalizedAge();  // 0 → 1

        // Interpolate color
        p.color = glm::mix(p.colorStart, p.colorEnd, t);

        // Interpolate size
        p.size = glm::mix(p.sizeStart, p.sizeEnd, t);

        if (p.motionType == MotionType::CAMERA_RELATIVE) {
            // Look up emitter config
            auto it = unifiedEmitters_.find(p.emitterID);
            if (it == unifiedEmitters_.end()) continue;
            const EmitterConfig& cfg = it->second.config;

            // Apply gravity
            p.velocity += cfg.gravity * deltaTime;

            // Apply wind
            if (cfg.windResponse > 0.0f && envState_.windStrength > 0.0f) {
                // Wind direction is EQ Z-up; convert to Irrlicht Y-up: (x, z, y)
                glm::vec3 windIrr(envState_.windDirection.x, envState_.windDirection.z, envState_.windDirection.y);
                p.velocity += windIrr * (envState_.windStrength * cfg.windResponse * deltaTime * 10.0f);
            }

            // Apply drag
            if (p.drag > 0.0f) {
                float dampFactor = 1.0f - p.drag * deltaTime;
                if (dampFactor < 0.0f) dampFactor = 0.0f;
                p.velocity *= dampFactor;
            }

            // Snow-specific: lateral drift via sine wave
            if (cfg.driftAmplitude > 0.0f && cfg.driftFrequency > 0.0f) {
                float drift = std::sin(p.age * cfg.driftFrequency * 6.28318f + p.phase) * cfg.driftAmplitude * deltaTime;
                p.position.x += drift;
                p.position.z += drift * 0.5f;  // Slight Z drift too
            }

            // Snow-specific: alpha twinkle
            if (cfg.twinkleSpeed > 0.0f) {
                float baseAlpha = glm::mix(p.colorStart.a, p.colorEnd.a, t);
                p.color.a = baseAlpha * (0.7f + 0.3f * std::sin(p.age * cfg.twinkleSpeed + p.phase));
            }

            // Update position
            p.position += p.velocity * deltaTime;

            // Per-particle light accumulation — rain/snow are only visible
            // where illuminated by nearby light sources (torches, campfires,
            // player lantern).
            //
            // Ambient factor: precipitation catches far less ambient light than
            // opaque surfaces (small translucent droplets/flakes vs textured walls).
            // This ensures rain far from any light source is nearly invisible at
            // night, matching the visual "light sphere" on zone geometry.
            //
            // Quartic falloff: (1-d/r)^4 produces a sharp edge that matches the
            // perceived light boundary on zone surfaces. Quadratic (1-d/r)^2 was
            // too gentle — all particles within the spawn volume received similar
            // illumination, making them appear uniformly bright.
            // Particle light radius kept tighter than zone geometry (~15 units
            // vs 52) so the 3D sphere doesn't visibly extend above the
            // ground-level light circle.  Quartic falloff (1-d/r)^4 gives a
            // sharp perceived edge matching the zone surface boundary.
            constexpr float kParticleLightRadius = 15.0f;
            constexpr float kAmbientFactor = 0.1f;
            float lightR = ambientColor_.x * kAmbientFactor;
            float lightG = ambientColor_.y * kAmbientFactor;
            float lightB = ambientColor_.z * kAmbientFactor;
            for (const auto& light : weatherLights_) {
                glm::vec3 diff = p.position - light.position;
                float distSq = glm::dot(diff, diff);
                float dist = std::sqrt(distSq);
                if (dist < light.radius) {
                    float t = 1.0f - dist / light.radius;
                    float atten = t * t;   // quadratic
                    atten *= atten;         // quartic
                    // Clamp effective radius to kParticleLightRadius
                    if (dist > kParticleLightRadius) atten = 0.0f;
                    lightR += light.color.x * atten;
                    lightG += light.color.y * atten;
                    lightB += light.color.z * atten;
                }
            }
            p.color.r *= std::min(lightR, 1.0f);
            p.color.g *= std::min(lightG, 1.0f);
            p.color.b *= std::min(lightB, 1.0f);

            // Recycle check: if particle is too far from camera or below volume, respawn
            glm::vec3 offset = p.position - cameraPos;
            float hExtX = cfg.spawnVolumeHalfExtents.x * 1.5f;
            float hExtZ = cfg.spawnVolumeHalfExtents.z * 1.5f;
            float hExtY = cfg.spawnVolumeHalfExtents.y;
            if (std::abs(offset.x) > hExtX || std::abs(offset.z) > hExtZ || offset.y < -hExtY) {
                // Respawn at a new position in the spawn volume
                int idx = static_cast<int>(&p - unifiedPool_.data());
                freeUnifiedParticle(idx);
                // Deficit spawning will replace it next frame
            }
        } else if (p.motionType == MotionType::LINEAR ||
                   p.motionType == MotionType::BURST ||
                   p.motionType == MotionType::RADIAL_EXPAND) {
            // LINEAR / BURST / RADIAL_EXPAND — all use the same physics:
            // velocity + gravity + drag. Direction set at spawn time.
            auto it = unifiedEmitters_.find(p.emitterID);
            if (it != unifiedEmitters_.end()) {
                p.velocity += it->second.config.gravity * deltaTime;
            }

            // Apply drag
            if (p.drag > 0.0f) {
                float dampFactor = 1.0f - p.drag * deltaTime;
                if (dampFactor < 0.0f) dampFactor = 0.0f;
                p.velocity *= dampFactor;
            }

            // Update position
            p.position += p.velocity * deltaTime;

        } else if (p.motionType == MotionType::ORBITAL) {
            // ORBITAL — orbit center point with vertical drift
            auto it = unifiedEmitters_.find(p.emitterID);
            if (it != unifiedEmitters_.end()) {
                glm::vec3 center = it->second.position + it->second.attachOffset;
                p.phase += p.angularVelocity * deltaTime;
                p.position.x = center.x + p.radius * std::cos(p.phase);
                p.position.z = center.z + p.radius * std::sin(p.phase);
                p.position.y += p.velocity.y * deltaTime;  // Vertical drift
            }
        }
    }

    // Update spell effects (entity tracking, trigger processing, cleanup)
    updateSpellEffects(deltaTime);

    // Periodic debug log (~every 5 seconds at tier3 rate)
    if (++updateLogCounter >= 50) {
        updateLogCounter = 0;
        LOG_DEBUG(MOD_GRAPHICS,
                  "updateUnified: dt={:.3f} emitters={} active={} culled={} spawned={} "
                  "poolActive={} freeList={} spells={}",
                  deltaTime, unifiedEmitters_.size(), emittersActive, emittersCulled,
                  totalSpawned, unifiedActiveCount_, freeList_.size(),
                  activeSpellEffects_.size());
    }
}

void ParticleManager::renderUnified(const irr::core::matrix4& viewMatrix,
                                     const irr::core::matrix4& projMatrix,
                                     const irr::core::vector3df& cameraPos,
                                     float fogStart, float fogEnd, const float* fogColor,
                                     float screenHeight) {
#ifdef EQT_HAS_GLES2
    if (!unifiedRendererInitialized_ || !unifiedRenderer_) return;
    if (unifiedActiveCount_ <= 0) return;
    if (!atlasTexture_) return;

    unifiedRenderBuf_.clear();
    for (const auto& p : unifiedPool_) {
        if (p.isAlive()) {
            unifiedRenderBuf_.push_back(p);
        }
    }

    if (unifiedRenderBuf_.empty()) return;

    // Get GL texture handle via the patched ITexture::getDriverTextureHandle()
    GLuint atlasGL = static_cast<GLuint>(atlasTexture_->getDriverTextureHandle());
    if (atlasGL == 0) return;

    // Periodic debug logging (every ~5 seconds at 50fps = every 250 frames)
    static int frameCounter = 0;
    if (++frameCounter >= 250) {
        frameCounter = 0;
        const auto& firstParticle = unifiedRenderBuf_[0];
        LOG_DEBUG(MOD_GRAPHICS,
                  "UnifiedParticles: rendering {} alive, {} emitters, atlasGL={}, "
                  "first pos=({:.1f},{:.1f},{:.1f}) size={:.2f} color=({:.2f},{:.2f},{:.2f},{:.2f})",
                  unifiedRenderBuf_.size(), unifiedEmitters_.size(), atlasGL,
                  firstParticle.position.x, firstParticle.position.y, firstParticle.position.z,
                  firstParticle.size,
                  firstParticle.color.r, firstParticle.color.g, firstParticle.color.b, firstParticle.color.a);
    }

    // Pass View and Projection separately — shader multiplies in GLSL
    // (same convention as built-in COGLES2 shaders)
    unifiedRenderer_->render(unifiedRenderBuf_.data(),
                             static_cast<int>(unifiedRenderBuf_.size()),
                             viewMatrix.pointer(), projMatrix.pointer(),
                             fogStart, fogEnd, fogColor,
                             screenHeight, atlasGL);
#endif
}

void ParticleManager::createFireEmitters(const std::vector<glm::vec3>& positions,
                                          const std::vector<float>& lightRadii) {
    // Clear existing fire emitters first
    clearUnifiedEmitters();

    if (positions.empty()) return;

    // Campfire radius threshold — lights with radius >= this get campfire treatment
    const float campfireRadiusThreshold = 150.0f;

    for (size_t i = 0; i < positions.size(); ++i) {
        const glm::vec3& eqPos = positions[i];
        float radius = (i < lightRadii.size()) ? lightRadii[i] : 120.0f;

        // Convert EQ coordinates (Z-up) to Irrlicht (Y-up): (x, y, z) → (x, z, y)
        glm::vec3 irrPos(eqPos.x, eqPos.z, eqPos.y);

        if (radius >= campfireRadiusThreshold) {
            // Large light → campfire: flame + ember emitters
            {
                ActiveEmitter ae;
                ae.config = FirePresets::CampfireFlame();
                ae.position = irrPos;
                ae.emitterID = nextEmitterID_++;
                ae.lightRadius = radius;
                unifiedEmitters_[ae.emitterID] = ae;
            }
            {
                ActiveEmitter ae;
                ae.config = FirePresets::CampfireEmber();
                ae.position = irrPos;
                ae.emitterID = nextEmitterID_++;
                ae.lightRadius = radius;
                unifiedEmitters_[ae.emitterID] = ae;
            }
        } else {
            // Small/medium light → torch
            ActiveEmitter ae;
            ae.config = FirePresets::Torch();
            ae.position = irrPos;
            ae.emitterID = nextEmitterID_++;
            ae.lightRadius = radius;
            unifiedEmitters_[ae.emitterID] = ae;
        }
    }

    // Log first few emitter positions for debugging
    int logCount = 0;
    for (const auto& [id, em] : unifiedEmitters_) {
        if (logCount++ < 3) {
            LOG_INFO(MOD_GRAPHICS,
                     "  Fire emitter #{}: irrPos=({:.1f},{:.1f},{:.1f}) radius={:.0f} rate={:.0f}/s",
                     id, em.position.x, em.position.y, em.position.z,
                     em.lightRadius, em.config.spawnRate);
        }
    }
    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Created {} unified fire emitters for {} sources",
              unifiedEmitters_.size(), positions.size());
}

void ParticleManager::clearUnifiedEmitters() {
    // Kill all particles owned by unified emitters
    for (auto& p : unifiedPool_) {
        if (p.isAlive()) {
            p.setAlive(false);
        }
    }

    // Reset free list
    freeList_.resize(unifiedPool_.size());
    for (uint16_t i = 0; i < static_cast<uint16_t>(unifiedPool_.size()); ++i) {
        freeList_[i] = i;
    }
    unifiedActiveCount_ = 0;

    // Clear emitter map, weather state, and spell effects
    unifiedEmitters_.clear();
    weatherEmitterID_ = 0;
    activeSpellEffects_.clear();
    nextSpellEffectID_ = 1;
}

void ParticleManager::activateWeatherParticles(uint8_t type, uint8_t intensity) {
    // Kill existing weather emitter if any
    deactivateWeatherParticles();

    // Create weather emitter config
    EmitterConfig cfg;
    if (type == 1) {
        cfg = WeatherPresets::Rain(intensity);
    } else if (type == 2) {
        cfg = WeatherPresets::Snow(intensity);
    } else {
        return;
    }

    ActiveEmitter ae;
    ae.config = cfg;
    ae.position = glm::vec3(0.0f);  // Will be updated to camera pos each frame
    ae.emitterID = nextEmitterID_++;
    ae.transitionAlpha = 0.0f;  // Start at 0 for smooth ramp-up
    ae.transitionRate = 0.5f;   // ~2 seconds to full intensity
    unifiedEmitters_[ae.emitterID] = ae;
    weatherEmitterID_ = ae.emitterID;

    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Activated weather particles type={} intensity={} "
             "target={} emitterID={}", type, intensity, cfg.targetCount, ae.emitterID);
}

void ParticleManager::deactivateWeatherParticles() {
    if (weatherEmitterID_ == 0) return;

    // Kill the weather emitter
    auto it = unifiedEmitters_.find(weatherEmitterID_);
    if (it != unifiedEmitters_.end()) {
        it->second.active = false;
        // Kill all particles belonging to this emitter
        for (auto& p : unifiedPool_) {
            if (p.isAlive() && p.emitterID == weatherEmitterID_) {
                int idx = static_cast<int>(&p - unifiedPool_.data());
                freeUnifiedParticle(idx);
            }
        }
        unifiedEmitters_.erase(it);
    }

    weatherEmitterID_ = 0;
    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Deactivated weather particles");
}

void ParticleManager::spawnWeatherParticle(const EmitterConfig& cfg, uint16_t emitterID,
                                            const glm::vec3& cameraPos, float transitionAlpha) {
    int idx = allocateUnifiedParticle();
    if (idx < 0) return;  // Pool full

    UnifiedParticle& p = unifiedPool_[idx];

    // Position: camera-relative spawn volume
    const glm::vec3& he = cfg.spawnVolumeHalfExtents;
    p.position.x = cameraPos.x + randomFloat(-he.x, he.x);
    p.position.z = cameraPos.z + randomFloat(-he.z, he.z);

    // Y position biased toward top of volume
    float yRand = randomFloat(0.0f, 1.0f);
    if (yRand < cfg.spawnVolumeTopBias) {
        // Top portion (upper 20% of volume)
        p.position.y = cameraPos.y + he.y * randomFloat(0.6f, 1.0f);
    } else {
        // Rest of volume
        p.position.y = cameraPos.y + randomFloat(-he.y, he.y * 0.6f);
    }

    // Velocity: base + random spread
    p.velocity.x = cfg.velocityBase.x + randomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
    p.velocity.y = cfg.velocityBase.y + randomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
    p.velocity.z = cfg.velocityBase.z + randomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);

    // Lifetime
    p.maxLifetime = randomFloat(cfg.lifetimeMin, cfg.lifetimeMax);
    p.age = 0.0f;

    // Color
    p.colorStart = cfg.colorStart;
    p.colorEnd = cfg.colorEnd;
    p.color = cfg.colorStart;

    // Rain alpha variation for visual variety
    if (cfg.blendMode == UnifiedBlendMode::ADDITIVE) {
        float alphaVar = randomFloat(0.3f, cfg.colorStart.a);
        p.colorStart.a = alphaVar;
        p.color.a = alphaVar;
    }

    // Size
    p.sizeStart = randomFloat(cfg.sizeStartMin, cfg.sizeStartMax);
    p.sizeEnd = randomFloat(cfg.sizeEndMin, cfg.sizeEndMax);
    p.size = p.sizeStart;

    // Size-speed correlation for snow: larger flakes fall slower
    if (cfg.sizeSpeedCorrelation > 0.0f) {
        float sizeRange = cfg.sizeStartMax - cfg.sizeStartMin;
        float sizeFactor = (sizeRange > 0.0f) ? (p.sizeStart - cfg.sizeStartMin) / sizeRange : 0.0f;
        p.velocity.y *= (1.0f - sizeFactor * cfg.sizeSpeedCorrelation);
    }

    // Motion
    p.drag = cfg.drag;
    p.motionType = cfg.motionType;
    p.phase = randomFloat(0.0f, 6.28318f);

    // Rotation: rain gets wind-angle rotation, snow gets none
    if (cfg.driftAmplitude == 0.0f && cfg.windResponse > 0.0f) {
        // Rain: slight rotation based on wind direction for angled streaks
        float windAngle = std::atan2(envState_.windDirection.x, envState_.windDirection.y);
        p.rotation = windAngle * cfg.windResponse * envState_.windStrength * 0.3f;
    } else {
        p.rotation = 0.0f;
    }

    // Texture: pick random region
    if (cfg.textureRegionCount > 1) {
        p.textureIndex = cfg.textureRegions[randomInt(0, cfg.textureRegionCount - 1)];
    } else {
        p.textureIndex = cfg.textureRegions[0];
    }

    // Metadata
    p.emitterID = emitterID;
    p.setAlive(true);
    p.setBlendMode(cfg.blendMode);
}

bool ParticleManager::resolveEntityPosition(uint16_t entityID, glm::vec3& outPos) const {
    if (entityPosCallback_) return entityPosCallback_(entityID, outPos);
    return false;
}

// =============================================================================
// Spell Effect API
// =============================================================================

void ParticleManager::spawnSpellParticle(const EmitterConfig& cfg, uint16_t emitterID,
                                          const glm::vec3& emitterPos,
                                          const glm::vec3* dynamicDir) {
    int idx = allocateUnifiedParticle();
    if (idx < 0) return;  // Pool full

    UnifiedParticle& p = unifiedPool_[idx];

    // Position: emitter position + spawn shape offset
    p.position = emitterPos;
    if (cfg.spawnShape == SpawnShape::BOX) {
        p.position.x += randomFloat(-cfg.spawnExtents.x, cfg.spawnExtents.x);
        p.position.y += randomFloat(-cfg.spawnExtents.y, cfg.spawnExtents.y);
        p.position.z += randomFloat(-cfg.spawnExtents.z, cfg.spawnExtents.z);
    } else if (cfg.spawnShape == SpawnShape::SPHERE) {
        float r = randomFloat(0.0f, cfg.spawnExtents.x);
        float theta = randomFloat(0.0f, 6.28318f);
        float phi = randomFloat(-1.5708f, 1.5708f);
        p.position.x += r * std::cos(phi) * std::cos(theta);
        p.position.y += r * std::sin(phi);
        p.position.z += r * std::cos(phi) * std::sin(theta);
    } else if (cfg.spawnShape == SpawnShape::RING) {
        float angle = randomFloat(0.0f, 6.28318f);
        p.position.x += cfg.spawnExtents.x * std::cos(angle);
        p.position.z += cfg.spawnExtents.x * std::sin(angle);
    }

    // Velocity: depends on motion type
    if (dynamicDir) {
        // Spray: cone along dynamic direction
        // velocityBase.x = speed, velocitySpread = cone half-angle spread
        float speed = glm::length(cfg.velocityBase);
        if (speed < 0.01f) speed = 4.0f;
        p.velocity = (*dynamicDir) * speed;
        // Add cone spread perpendicular to direction
        p.velocity.x += randomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.y += randomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
        p.velocity.z += randomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
    } else if (cfg.motionType == MotionType::RADIAL_EXPAND) {
        // Radial direction in XZ plane
        float angle = randomFloat(0.0f, 6.28318f);
        p.velocity.x = std::cos(angle) * cfg.expandSpeed + randomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.z = std::sin(angle) * cfg.expandSpeed + randomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
        p.velocity.y = cfg.velocityBase.y + randomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
    } else if (cfg.motionType == MotionType::BURST) {
        // Random scatter direction using velocityBase as magnitude hints
        float angle = randomFloat(0.0f, 6.28318f);
        p.velocity.x = std::cos(angle) * cfg.velocityBase.x + randomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.z = std::sin(angle) * cfg.velocityBase.x + randomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
        p.velocity.y = cfg.velocityBase.y + randomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
    } else {
        // LINEAR / ORBITAL: base + spread
        p.velocity.x = cfg.velocityBase.x + randomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.y = cfg.velocityBase.y + randomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
        p.velocity.z = cfg.velocityBase.z + randomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
    }

    // Lifetime
    p.maxLifetime = randomFloat(cfg.lifetimeMin, cfg.lifetimeMax);
    p.age = 0.0f;

    // Color
    p.colorStart = cfg.colorStart;
    p.colorEnd = cfg.colorEnd;
    p.color = cfg.colorStart;

    // Size
    p.sizeStart = randomFloat(cfg.sizeStartMin, cfg.sizeStartMax);
    p.sizeEnd = randomFloat(cfg.sizeEndMin, cfg.sizeEndMax);
    p.size = p.sizeStart;

    // Motion
    p.drag = cfg.drag;
    p.motionType = cfg.motionType;
    p.phase = randomFloat(0.0f, 6.28318f);
    p.rotation = 0.0f;

    // ORBITAL-specific: set orbit parameters
    if (cfg.motionType == MotionType::ORBITAL) {
        p.radius = cfg.orbitalRadius;
        p.angularVelocity = cfg.orbitalAngularVelocity;
        // Set initial orbital position
        p.position.x = emitterPos.x + p.radius * std::cos(p.phase);
        p.position.z = emitterPos.z + p.radius * std::sin(p.phase);
    }

    // Texture: pick random region
    if (cfg.textureRegionCount > 1) {
        p.textureIndex = cfg.textureRegions[randomInt(0, cfg.textureRegionCount - 1)];
    } else {
        p.textureIndex = cfg.textureRegions[0];
    }

    // Metadata
    p.emitterID = emitterID;
    p.setAlive(true);
    p.setBlendMode(cfg.blendMode);
}

uint32_t ParticleManager::createSpellEffect(const SpellEffectDef& def,
                                             uint16_t casterID, uint16_t targetID,
                                             float duration,
                                             bool useDynamicDir,
                                             float projectileTravelDuration) {
    if (!unifiedRendererInitialized_) return 0;

    SpellEffectInstance inst;
    inst.effectID = nextSpellEffectID_++;
    inst.spellID = 0;
    inst.casterEntityID = casterID;
    inst.targetEntityID = targetID;
    inst.age = 0.0f;
    inst.maxDuration = duration;
    inst.def = def;
    inst.useDynamicDirection = useDynamicDir;
    if (projectileTravelDuration > 0.0f) {
        inst.projectileTravelDuration = projectileTravelDuration;
    }

    // Create emitter states for each emitter definition
    for (int i = 0; i < static_cast<int>(def.emitters.size()); ++i) {
        SpellEffectInstance::EmitterState es;
        es.defIndex = i;
        es.activeEmitterID = 0;
        es.triggered = false;
        inst.emitterStates.push_back(es);
    }

    activeSpellEffects_.push_back(std::move(inst));

    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Created spell effect '{}' id={} caster={} target={}",
              def.name, inst.effectID, casterID, targetID);

    return activeSpellEffects_.back().effectID;
}

uint32_t ParticleManager::createSpellEffectAtPosition(const SpellEffectDef& def,
                                                       const glm::vec3& worldPos,
                                                       float duration) {
    if (!unifiedRendererInitialized_) return 0;

    SpellEffectInstance inst;
    inst.effectID = nextSpellEffectID_++;
    inst.spellID = 0;
    inst.casterEntityID = 0;
    inst.targetEntityID = 0;
    inst.groundTarget = worldPos;
    inst.age = 0.0f;
    inst.maxDuration = duration;
    inst.def = def;

    // Override all emitters to GROUND_TARGET if they aren't already projectile/target
    for (auto& e : inst.def.emitters) {
        if (e.attach == SpellAttach::CASTER) {
            e.attach = SpellAttach::GROUND_TARGET;
        }
    }

    for (int i = 0; i < static_cast<int>(def.emitters.size()); ++i) {
        SpellEffectInstance::EmitterState es;
        es.defIndex = i;
        es.activeEmitterID = 0;
        es.triggered = false;
        inst.emitterStates.push_back(es);
    }

    activeSpellEffects_.push_back(std::move(inst));

    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Created spell effect '{}' id={} at ({:.1f},{:.1f},{:.1f})",
              def.name, activeSpellEffects_.back().effectID, worldPos.x, worldPos.y, worldPos.z);

    return activeSpellEffects_.back().effectID;
}

void ParticleManager::removeSpellEffect(uint32_t effectID) {
    for (auto it = activeSpellEffects_.begin(); it != activeSpellEffects_.end(); ++it) {
        if (it->effectID == effectID) {
            // Kill emitters and their particles
            for (auto& es : it->emitterStates) {
                if (es.activeEmitterID != 0) {
                    // Kill particles owned by this emitter
                    for (auto& p : unifiedPool_) {
                        if (p.isAlive() && p.emitterID == es.activeEmitterID) {
                            int idx = static_cast<int>(&p - unifiedPool_.data());
                            freeUnifiedParticle(idx);
                        }
                    }
                    unifiedEmitters_.erase(es.activeEmitterID);
                }
            }
            activeSpellEffects_.erase(it);
            return;
        }
    }
}

void ParticleManager::removeSpellEffectsForEntity(uint16_t entityID) {
    for (auto it = activeSpellEffects_.begin(); it != activeSpellEffects_.end(); ) {
        if (it->casterEntityID == entityID || it->targetEntityID == entityID) {
            // Kill emitters and their particles
            for (auto& es : it->emitterStates) {
                if (es.activeEmitterID != 0) {
                    for (auto& p : unifiedPool_) {
                        if (p.isAlive() && p.emitterID == es.activeEmitterID) {
                            int idx = static_cast<int>(&p - unifiedPool_.data());
                            freeUnifiedParticle(idx);
                        }
                    }
                    unifiedEmitters_.erase(es.activeEmitterID);
                }
            }
            it = activeSpellEffects_.erase(it);
        } else {
            ++it;
        }
    }
}

void ParticleManager::clearAllSpellEffects() {
    for (auto& effect : activeSpellEffects_) {
        for (auto& es : effect.emitterStates) {
            if (es.activeEmitterID != 0) {
                for (auto& p : unifiedPool_) {
                    if (p.isAlive() && p.emitterID == es.activeEmitterID) {
                        int idx = static_cast<int>(&p - unifiedPool_.data());
                        freeUnifiedParticle(idx);
                    }
                }
                unifiedEmitters_.erase(es.activeEmitterID);
            }
        }
    }
    activeSpellEffects_.clear();
}

void ParticleManager::updateSpellEffects(float deltaTime) {
    if (activeSpellEffects_.empty()) return;

    for (auto it = activeSpellEffects_.begin(); it != activeSpellEffects_.end(); ) {
        auto& effect = *it;
        effect.age += deltaTime;

        // Check max duration
        if (effect.maxDuration > 0.0f && effect.age >= effect.maxDuration) {
            // Kill all emitters for this effect
            for (auto& es : effect.emitterStates) {
                if (es.activeEmitterID != 0) {
                    auto emIt = unifiedEmitters_.find(es.activeEmitterID);
                    if (emIt != unifiedEmitters_.end()) {
                        emIt->second.active = false;
                    }
                }
            }
        }

        // Process triggers and update entity positions
        bool allDone = true;
        for (auto& es : effect.emitterStates) {
            const auto& emDef = effect.def.emitters[es.defIndex];

            // Check trigger conditions
            if (!es.triggered) {
                bool shouldTrigger = false;
                switch (emDef.trigger) {
                    case SpellTrigger::IMMEDIATE:
                        shouldTrigger = true;
                        break;
                    case SpellTrigger::DELAYED:
                        shouldTrigger = (effect.age >= emDef.triggerDelay);
                        break;
                    case SpellTrigger::ON_CAST_COMPLETE:
                        shouldTrigger = effect.castCompleteSignaled;
                        break;
                    case SpellTrigger::ON_HIT:
                        shouldTrigger = effect.hitSignaled;
                        break;
                }

                if (shouldTrigger) {
                    es.triggered = true;

                    // Resolve attach position
                    glm::vec3 attachPos(0.0f);
                    uint16_t attachEntity = 0;

                    switch (emDef.attach) {
                        case SpellAttach::CASTER:
                            attachEntity = effect.casterEntityID;
                            if (entityPosCallback_ && attachEntity != 0) {
                                entityPosCallback_(attachEntity, attachPos);
                            }
                            break;
                        case SpellAttach::TARGET:
                            attachEntity = effect.targetEntityID;
                            if (entityPosCallback_ && attachEntity != 0) {
                                entityPosCallback_(attachEntity, attachPos);
                            }
                            break;
                        case SpellAttach::GROUND_TARGET:
                            attachPos = effect.groundTarget;
                            break;
                        case SpellAttach::PROJECTILE_PATH:
                            // Start at caster position, will lerp to target
                            if (entityPosCallback_ && effect.casterEntityID != 0) {
                                entityPosCallback_(effect.casterEntityID, attachPos);
                            }
                            break;
                    }

                    // Create the unified emitter
                    ActiveEmitter ae;
                    ae.config = emDef.config;
                    ae.position = attachPos;
                    ae.emitterID = nextEmitterID_++;
                    ae.attachEntityID = attachEntity;
                    ae.attachOffset = emDef.positionOffset;
                    ae.useDynamicDirection = effect.useDynamicDirection;

                    // Set up projectile lerp state
                    if (emDef.attach == SpellAttach::PROJECTILE_PATH) {
                        ae.isProjectile = true;
                        ae.projectileStartPos = attachPos;
                        ae.targetEntityID = effect.targetEntityID;
                        ae.travelDuration = effect.projectileTravelDuration;
                        ae.travelElapsed = 0.0f;
                        // Resolve initial target position
                        if (entityPosCallback_ && effect.targetEntityID != 0) {
                            entityPosCallback_(effect.targetEntityID, ae.projectileTargetPos);
                        }
                    }

                    unifiedEmitters_[ae.emitterID] = ae;
                    es.activeEmitterID = ae.emitterID;

                    LOG_TRACE(MOD_GRAPHICS, "Spell effect '{}': triggered emitter {} at ({:.1f},{:.1f},{:.1f})",
                              effect.def.name, ae.emitterID, attachPos.x, attachPos.y, attachPos.z);
                }
            }

            // Update entity position for attached emitters
            if (es.activeEmitterID != 0) {
                auto emIt = unifiedEmitters_.find(es.activeEmitterID);
                if (emIt != unifiedEmitters_.end()) {
                    auto& emitter = emIt->second;

                    // Projectile lerp: move emitter from start to target
                    if (emitter.isProjectile) {
                        emitter.travelElapsed += deltaTime;
                        // Track moving target
                        if (entityPosCallback_ && emitter.targetEntityID != 0) {
                            glm::vec3 targetPos;
                            if (entityPosCallback_(emitter.targetEntityID, targetPos)) {
                                emitter.projectileTargetPos = targetPos;
                            }
                        }
                        float t = (emitter.travelDuration > 0.0f)
                            ? std::min(emitter.travelElapsed / emitter.travelDuration, 1.0f)
                            : 1.0f;
                        emitter.position = glm::mix(emitter.projectileStartPos,
                                                     emitter.projectileTargetPos, t);
                        if (t >= 1.0f) {
                            emitter.active = false;
                            effect.hitSignaled = true;  // triggers ON_HIT emitters
                        }
                    } else if (emitter.attachEntityID != 0 && entityPosCallback_) {
                        glm::vec3 entityPos;
                        if (entityPosCallback_(emitter.attachEntityID, entityPos)) {
                            emitter.position = entityPos;
                        }
                    }

                    // Check if this emitter is still alive
                    if (emitter.active) {
                        allDone = false;
                    } else {
                        // Check if any particles still alive for this emitter
                        for (const auto& p : unifiedPool_) {
                            if (p.isAlive() && p.emitterID == es.activeEmitterID) {
                                allDone = false;
                                break;
                            }
                        }
                    }
                } else {
                    // Emitter was cleaned up by clearUnifiedEmitters
                }
            } else if (!es.triggered) {
                allDone = false;  // Waiting for trigger
            }
        }

        // Remove effect if all emitters are done (and duration expired or no duration)
        if (allDone && (effect.maxDuration <= 0.0f || effect.age >= effect.maxDuration)) {
            // Clean up any remaining emitters
            for (auto& es : effect.emitterStates) {
                if (es.activeEmitterID != 0) {
                    unifiedEmitters_.erase(es.activeEmitterID);
                }
            }
            it = activeSpellEffects_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
