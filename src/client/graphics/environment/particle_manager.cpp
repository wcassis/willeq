#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/environment_config.h"
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

bool ParticleManager::initUnifiedRenderer() {
#ifdef EQT_HAS_GLES2
    if (unifiedRendererInitialized_) return true;

    // Allocate the fixed particle pool
    unifiedPool_.resize(1024);
    freeList_.resize(1024);
    for (uint16_t i = 0; i < 1024; ++i) {
        freeList_[i] = i;
        unifiedPool_[i].setAlive(false);
    }
    unifiedActiveCount_ = 0;
    unifiedRenderBuf_.reserve(1024);

    // Create and init the GLES2 renderer (self-contained, uses raw GL calls)
    unifiedRenderer_ = std::make_unique<UnifiedParticleRenderer>();
    if (!unifiedRenderer_->init()) {
        LOG_WARN(MOD_GRAPHICS, "ParticleManager: Failed to init unified particle renderer");
        unifiedRenderer_.reset();
        return false;
    }

    unifiedRendererInitialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Unified particle system initialized (pool: 1024)");
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

void ParticleManager::updateUnified(float deltaTime) {
    if (!unifiedRendererInitialized_ || !unifiedFireEnabled_) return;
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
        emittersActive++;

        // Check emitter lifetime
        if (emitter.config.emitterLifetime > 0.0f) {
            emitter.emitterAge += deltaTime;
            if (emitter.emitterAge >= emitter.config.emitterLifetime) {
                emitter.active = false;
                continue;
            }
        }

        // Frustum cull emitters: skip spawning if outside view
        if (frustum) {
            irr::core::aabbox3df emitterBox(
                emitter.position.x - 2.0f, emitter.position.y - 2.0f, emitter.position.z - 2.0f,
                emitter.position.x + 2.0f, emitter.position.y + 8.0f, emitter.position.z + 2.0f);
            if (!frustum->getBoundingBox().intersectsWithBox(emitterBox)) {
                emittersCulled++;
                continue;  // Outside frustum — skip spawning, existing particles still age
            }
        }

        // Accumulate spawn timer
        emitter.spawnAccumulator += emitter.config.spawnRate * deltaTime;
        int toSpawn = static_cast<int>(emitter.spawnAccumulator);
        emitter.spawnAccumulator -= static_cast<float>(toSpawn);

        for (int s = 0; s < toSpawn; ++s) {
            int idx = allocateUnifiedParticle();
            if (idx < 0) break;  // Pool full
            totalSpawned++;

            UnifiedParticle& p = unifiedPool_[idx];
            const EmitterConfig& cfg = emitter.config;

            // Position: emitter position + spawn shape offset
            p.position = emitter.position;
            if (cfg.spawnShape == SpawnShape::BOX) {
                p.position.x += randomFloat(-cfg.spawnExtents.x, cfg.spawnExtents.x);
                p.position.y += randomFloat(-cfg.spawnExtents.y, cfg.spawnExtents.y);
                p.position.z += randomFloat(-cfg.spawnExtents.z, cfg.spawnExtents.z);
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

            // Size
            p.sizeStart = randomFloat(cfg.sizeStartMin, cfg.sizeStartMax);
            p.sizeEnd = randomFloat(cfg.sizeEndMin, cfg.sizeEndMax);
            p.size = p.sizeStart;

            // Motion
            p.drag = cfg.drag;
            p.motionType = cfg.motionType;
            p.phase = randomFloat(0.0f, 6.28318f);

            // Texture: pick random region
            if (cfg.textureRegionCount > 1) {
                p.textureIndex = cfg.textureRegions[randomInt(0, cfg.textureRegionCount - 1)];
            } else {
                p.textureIndex = cfg.textureRegions[0];
            }

            // Metadata
            p.emitterID = emitter.emitterID;
            p.setAlive(true);
            p.setBlendMode(cfg.blendMode);
        }
    }

    // Update all alive particles
    for (auto& p : unifiedPool_) {
        if (!p.isAlive()) continue;

        p.age += deltaTime;
        if (p.age >= p.maxLifetime) {
            // Kill particle
            int idx = static_cast<int>(&p - unifiedPool_.data());
            freeUnifiedParticle(idx);
            continue;
        }

        float t = p.getNormalizedAge();  // 0 → 1

        // Interpolate color
        p.color = glm::mix(p.colorStart, p.colorEnd, t);

        // Interpolate size
        p.size = glm::mix(p.sizeStart, p.sizeEnd, t);

        // LINEAR motion
        if (p.motionType == MotionType::LINEAR) {
            // Look up emitter for gravity
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
        }
    }

    // Periodic debug log (~every 5 seconds at tier3 rate)
    if (++updateLogCounter >= 50) {
        updateLogCounter = 0;
        LOG_DEBUG(MOD_GRAPHICS,
                  "updateUnified: dt={:.3f} emitters={} active={} culled={} spawned={} "
                  "poolActive={} freeList={}",
                  deltaTime, unifiedEmitters_.size(), emittersActive, emittersCulled,
                  totalSpawned, unifiedActiveCount_, freeList_.size());
    }
}

void ParticleManager::renderUnified(const irr::core::matrix4& viewMatrix,
                                     const irr::core::matrix4& projMatrix,
                                     const irr::core::vector3df& cameraPos,
                                     float fogStart, float fogEnd, const float* fogColor,
                                     float screenHeight) {
#ifdef EQT_HAS_GLES2
    if (!unifiedRendererInitialized_ || !unifiedRenderer_ || !unifiedFireEnabled_) return;
    if (unifiedActiveCount_ <= 0) return;
    if (!atlasTexture_) return;

    // Collect alive particles
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

    // Clear emitter map
    unifiedEmitters_.clear();
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
