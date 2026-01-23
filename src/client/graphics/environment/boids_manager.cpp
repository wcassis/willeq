#include "client/graphics/environment/boids_manager.h"
#include "client/graphics/environment/environment_config.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace EQT {
namespace Graphics {
namespace Environment {

BoidsManager::BoidsManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver)
    : smgr_(smgr)
    , driver_(driver)
    , rng_(std::random_device{}())
{
    // Initialize all types as enabled
    for (size_t i = 0; i < static_cast<size_t>(CreatureType::Count); ++i) {
        typeEnabled_[i] = true;
    }

    // Set default budget based on medium quality
    budget_ = BoidsBudget::fromQuality(2);  // Medium
}

BoidsManager::~BoidsManager() {
    flocks_.clear();
}

bool BoidsManager::init(const std::string& eqClientPath) {
    if (initialized_) {
        return true;
    }

    // Try to load creature atlas texture, or create a default one
    std::string atlasPath = eqClientPath + "/data/textures/creature_atlas.png";
    if (!loadCreatureAtlas(atlasPath)) {
        LOG_DEBUG(MOD_GRAPHICS, "BoidsManager: Creating procedural creature atlas");
        createDefaultAtlas();
    }

    // Set up creature material
    creatureMaterial_.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
    creatureMaterial_.setTexture(0, atlasTexture_);
    creatureMaterial_.Lighting = false;
    creatureMaterial_.ZWriteEnable = false;
    creatureMaterial_.BackfaceCulling = false;
    creatureMaterial_.FogEnable = false;

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "BoidsManager initialized");
    return true;
}

void BoidsManager::update(float deltaTime) {
    if (!enabled_ || !initialized_ || quality_ == 0) {
        return;
    }

    // Update spawning
    updateSpawning(deltaTime);

    // Update all flocks
    for (auto& flock : flocks_) {
        if (flock) {
            flock->update(deltaTime, envState_);
        }
    }

    // Check player proximity for scatter behavior
    checkPlayerProximity();

    // Remove expired flocks
    removeExpiredFlocks();
}

void BoidsManager::render() {
    if (!enabled_ || !initialized_ || quality_ == 0 || !driver_) {
        return;
    }

    // Get camera info for billboard orientation
    irr::scene::ICameraSceneNode* camera = smgr_->getActiveCamera();
    if (!camera) {
        return;
    }

    irr::core::vector3df cameraPos = camera->getAbsolutePosition();
    irr::core::vector3df cameraUp = camera->getUpVector();

    // Set up the textured material with alpha transparency
    driver_->setMaterial(creatureMaterial_);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    int renderedCount = 0;
    int culledCount = 0;

    // Render all creatures from all flocks
    for (const auto& flock : flocks_) {
        if (!flock || !isTypeEnabled(flock->getType())) {
            continue;
        }

        for (const Creature& c : flock->getCreatures()) {
            // Distance culling
            glm::vec3 diff = c.position - envState_.playerPosition;
            float distSq = glm::dot(diff, diff);
            if (distSq > budget_.cullDistance * budget_.cullDistance) {
                culledCount++;
                continue;
            }

            // Render textured billboard
            renderBillboard(c, cameraPos, cameraUp);
            renderedCount++;
        }
    }

    // Log status periodically
    static int frameCounter = 0;
    if (++frameCounter >= 120) {  // Every ~2 seconds at 60fps
        if (!flocks_.empty()) {
            // Log flock positions relative to player
            for (const auto& flock : flocks_) {
                if (flock) {
                    glm::vec3 center = flock->getCenter();
                    float dist = glm::distance(center, envState_.playerPosition);
                    LOG_DEBUG(MOD_GRAPHICS, "Boids: {} flock at ({:.0f},{:.0f},{:.0f}), dist={:.0f}, player=({:.0f},{:.0f},{:.0f}), cullDist={:.0f}",
                             getCreatureTypeName(flock->getType()),
                             center.x, center.y, center.z, dist,
                             envState_.playerPosition.x, envState_.playerPosition.y, envState_.playerPosition.z,
                             budget_.cullDistance);
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "Boids render: {} rendered, {} culled from {} flocks",
                     renderedCount, culledCount, flocks_.size());
        }
        frameCounter = 0;
    }
}

void BoidsManager::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    LOG_INFO(MOD_GRAPHICS, "BoidsManager: Entering zone '{}' with biome {}",
             zoneName, static_cast<int>(biome));

    currentZoneName_ = zoneName;
    currentBiome_ = biome;

    // Clear existing flocks
    flocks_.clear();

    // Reset spawn timer to spawn soon
    spawnTimer_ = 5.0f;  // First flock spawns after 5 seconds
}

void BoidsManager::onZoneLeave() {
    LOG_DEBUG(MOD_GRAPHICS, "BoidsManager: Leaving zone '{}'", currentZoneName_);

    flocks_.clear();
    currentZoneName_.clear();
    currentBiome_ = ZoneBiome::Unknown;
}

void BoidsManager::setQuality(int quality) {
    quality_ = glm::clamp(quality, 0, 3);
    budget_ = BoidsBudget::fromQuality(quality_);

    LOG_DEBUG(MOD_GRAPHICS, "BoidsManager: Quality set to {}, max flocks={}, max creatures={}",
              quality_, budget_.maxFlocks, budget_.maxCreatures);
}

void BoidsManager::setDensity(float density) {
    userDensity_ = glm::clamp(density, 0.0f, 1.0f);
}

void BoidsManager::setTypeEnabled(CreatureType type, bool enabled) {
    if (type < CreatureType::Count) {
        typeEnabled_[static_cast<size_t>(type)] = enabled;
    }
}

bool BoidsManager::isTypeEnabled(CreatureType type) const {
    if (type < CreatureType::Count) {
        return typeEnabled_[static_cast<size_t>(type)];
    }
    return false;
}

void BoidsManager::setTimeOfDay(float hour) {
    envState_.timeOfDay = std::fmod(hour, 24.0f);
    if (envState_.timeOfDay < 0.0f) {
        envState_.timeOfDay += 24.0f;
    }
}

void BoidsManager::setWeather(WeatherType weather) {
    envState_.weather = weather;
}

void BoidsManager::setWind(const glm::vec3& direction, float strength) {
    envState_.windDirection = glm::length(direction) > 0.0f ?
        glm::normalize(direction) : glm::vec3(1.0f, 0.0f, 0.0f);
    envState_.windStrength = glm::clamp(strength, 0.0f, 1.0f);
}

void BoidsManager::setPlayerPosition(const glm::vec3& pos, float heading) {
    envState_.playerPosition = pos;
    envState_.playerHeading = heading;
}

void BoidsManager::setZoneBounds(const glm::vec3& min, const glm::vec3& max) {
    zoneBoundsMin_ = min;
    zoneBoundsMax_ = max;
    hasZoneBounds_ = true;

    // Update existing flocks with new bounds
    for (auto& flock : flocks_) {
        if (flock) {
            flock->setZoneBounds(min, max);
        }
    }
}

int BoidsManager::getTotalActiveCreatures() const {
    int total = 0;
    for (const auto& flock : flocks_) {
        if (flock) {
            total += static_cast<int>(flock->getCreatures().size());
        }
    }
    return total;
}

std::string BoidsManager::getDebugInfo() const {
    std::ostringstream ss;
    ss << "Boids: " << getTotalActiveCreatures() << "/" << budget_.maxCreatures;
    ss << " | Flocks: " << flocks_.size() << "/" << budget_.maxFlocks;
    ss << " | Quality: ";
    switch (quality_) {
        case 0: ss << "Off"; break;
        case 1: ss << "Low"; break;
        case 2: ss << "Med"; break;
        case 3: ss << "High"; break;
    }
    ss << " | Time: " << (envState_.isDaytime() ? "Day" : "Night");
    return ss.str();
}

void BoidsManager::reloadSettings() {
    // Reload the config file
    if (!EnvironmentEffectsConfig::instance().reload()) {
        LOG_WARN(MOD_GRAPHICS, "BoidsManager: Failed to reload config");
        return;
    }

    LOG_INFO(MOD_GRAPHICS, "BoidsManager: Reloaded settings");
}

std::vector<CreatureType> BoidsManager::getCreatureTypesForBiome(ZoneBiome biome, bool isDay) const {
    std::vector<CreatureType> types;

    switch (biome) {
        case ZoneBiome::Forest:
            if (isDay) {
                types.push_back(CreatureType::Bird);
                types.push_back(CreatureType::Butterfly);
            } else {
                types.push_back(CreatureType::Bat);
                types.push_back(CreatureType::Firefly);
            }
            break;

        case ZoneBiome::Swamp:
            if (isDay) {
                types.push_back(CreatureType::Dragonfly);
            } else {
                types.push_back(CreatureType::Bat);
                types.push_back(CreatureType::Firefly);
            }
            break;

        case ZoneBiome::Desert:
            if (isDay) {
                types.push_back(CreatureType::Crow);
            } else {
                types.push_back(CreatureType::Bat);
            }
            break;

        case ZoneBiome::Plains:
            if (isDay) {
                types.push_back(CreatureType::Bird);
                types.push_back(CreatureType::Butterfly);
            } else {
                types.push_back(CreatureType::Bat);
                types.push_back(CreatureType::Firefly);
            }
            break;

        case ZoneBiome::Urban:
            if (isDay) {
                types.push_back(CreatureType::Crow);
                types.push_back(CreatureType::Bird);
            } else {
                types.push_back(CreatureType::Bat);
            }
            break;

        case ZoneBiome::Ocean:
            if (isDay) {
                types.push_back(CreatureType::Seagull);
            }
            // No creatures at night on ocean
            break;

        case ZoneBiome::Dungeon:
        case ZoneBiome::Cave:
            // Always bats in dungeons/caves
            types.push_back(CreatureType::Bat);
            break;

        case ZoneBiome::Volcanic:
            if (isDay) {
                types.push_back(CreatureType::Crow);
            } else {
                types.push_back(CreatureType::Bat);
            }
            break;

        case ZoneBiome::Snow:
            // Few creatures in snow zones
            if (isDay) {
                types.push_back(CreatureType::Crow);
            }
            break;

        default:
            // Unknown biome: basic birds during day
            if (isDay) {
                types.push_back(CreatureType::Bird);
            }
            break;
    }

    // Filter out disabled types
    types.erase(
        std::remove_if(types.begin(), types.end(),
            [this](CreatureType t) { return !isTypeEnabled(t); }),
        types.end());

    return types;
}

void BoidsManager::trySpawnFlock() {
    // Check if we can spawn more flocks
    if (static_cast<int>(flocks_.size()) >= budget_.maxFlocks) {
        return;
    }

    // Check if we have room for more creatures
    int currentCreatures = getTotalActiveCreatures();
    if (currentCreatures >= budget_.maxCreatures) {
        return;
    }

    // Get valid creature types for current conditions
    bool isDay = envState_.isDaytime();
    auto validTypes = getCreatureTypesForBiome(currentBiome_, isDay);

    if (validTypes.empty()) {
        return;
    }

    // Pick a random type
    std::uniform_int_distribution<size_t> dist(0, validTypes.size() - 1);
    CreatureType type = validTypes[dist(rng_)];

    spawnFlock(type);
}

void BoidsManager::spawnFlock(CreatureType type) {
    FlockConfig config = getDefaultFlockConfig(type);

    // Adjust flock size based on density
    float effectiveDensity = userDensity_ * budget_.densityMult;
    config.minSize = std::max(2, static_cast<int>(config.minSize * effectiveDensity));
    config.maxSize = std::max(config.minSize, static_cast<int>(config.maxSize * effectiveDensity));

    // Get spawn position
    glm::vec3 spawnPos = getRandomSpawnPosition();

    // Adjust height - spawn above player's Z position
    // heightMin/heightMax are offsets above player, not absolute values
    float minSpawnZ = envState_.playerPosition.z + config.heightMin;
    spawnPos.z = minSpawnZ + (config.heightMax - config.heightMin) * 0.5f;

    // Create the flock
    auto flock = std::make_unique<FlockController>(config, spawnPos, rng_);

    // Set zone bounds
    if (hasZoneBounds_) {
        flock->setZoneBounds(zoneBoundsMin_, zoneBoundsMax_);
    }

    // Set collision detection for terrain/obstacle avoidance
    const std::vector<PlaceableBounds>* placeablesPtr = placeableObjects_.empty() ? nullptr : &placeableObjects_;
    flock->setCollisionDetection(smgr_, collisionSelector_, surfaceMap_, placeablesPtr);

    // Set initial destination (same height as spawn)
    glm::vec3 destination = getRandomSpawnPosition();
    destination.z = spawnPos.z;  // Keep at spawn height, relative to player
    flock->setDestination(destination);

    flocks_.push_back(std::move(flock));

    LOG_DEBUG(MOD_GRAPHICS, "BoidsManager: Spawned {} flock with {} creatures at ({:.1f}, {:.1f}, {:.1f})",
              getCreatureTypeName(type), config.maxSize, spawnPos.x, spawnPos.y, spawnPos.z);
}

void BoidsManager::removeExpiredFlocks() {
    const float maxLifetime = 300.0f;  // 5 minutes

    flocks_.erase(
        std::remove_if(flocks_.begin(), flocks_.end(),
            [maxLifetime](const std::unique_ptr<FlockController>& flock) {
                if (!flock) return true;
                if (flock->hasExitedBounds()) return true;
                if (flock->getTimeAlive() > maxLifetime) return true;
                return false;
            }),
        flocks_.end());
}

void BoidsManager::updateSpawning(float deltaTime) {
    spawnTimer_ -= deltaTime;

    if (spawnTimer_ <= 0.0f) {
        trySpawnFlock();
        spawnTimer_ = spawnCooldown_;
    }
}

void BoidsManager::checkPlayerProximity() {
    for (auto& flock : flocks_) {
        if (!flock) continue;

        glm::vec3 flockCenter = flock->getCenter();
        float dist = glm::distance(flockCenter, envState_.playerPosition);

        if (dist < scatterRadius_) {
            // Calculate scatter strength based on distance
            float strength = 1.0f - (dist / scatterRadius_);
            flock->scatter(envState_.playerPosition, strength);
        }
    }
}

bool BoidsManager::loadCreatureAtlas(const std::string& path) {
    if (!driver_) {
        return false;
    }

    atlasTexture_ = driver_->getTexture(path.c_str());
    return atlasTexture_ != nullptr;
}

void BoidsManager::createDefaultAtlas() {
    if (!driver_) {
        return;
    }

    // Create a procedural creature atlas (64x64 with 4x4 tiles of 16x16 each)
    const int tileSize = 16;
    const int atlasWidth = tileSize * CreatureAtlas::AtlasColumns;
    const int atlasHeight = tileSize * CreatureAtlas::AtlasRows;

    // Create image
    irr::video::IImage* img = driver_->createImage(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(atlasWidth, atlasHeight));

    if (!img) {
        LOG_ERROR(MOD_GRAPHICS, "BoidsManager: Failed to create atlas image");
        return;
    }

    // Clear to transparent
    img->fill(irr::video::SColor(0, 0, 0, 0));

    // Helper to draw a simple bird shape
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

    // Helper to draw a bat shape
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

    // Helper to draw a butterfly shape
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

    // Helper to draw firefly (glowing dot)
    auto drawFirefly = [&](int tileX, int tileY, float glow, irr::video::SColor color) {
        int baseX = tileX * tileSize;
        int baseY = tileY * tileSize;
        int cx = baseX + tileSize / 2;
        int cy = baseY + tileSize / 2;

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

    // Draw creature tiles with bright, visible colors
    // Row 0: Bird, Bat (wings up/down)
    drawBird(0, 0, true, irr::video::SColor(255, 180, 140, 100));   // Bird wings up (tan/brown)
    drawBird(1, 0, false, irr::video::SColor(255, 180, 140, 100));  // Bird wings down
    drawBat(2, 0, true, irr::video::SColor(255, 80, 60, 90));       // Bat wings up (dark purple)
    drawBat(3, 0, false, irr::video::SColor(255, 80, 60, 90));      // Bat wings down

    // Row 1: Butterfly, Dragonfly
    drawButterfly(0, 1, true, irr::video::SColor(255, 255, 200, 50));   // Butterfly wings up (yellow/orange)
    drawButterfly(1, 1, false, irr::video::SColor(255, 255, 200, 50));  // Butterfly wings down

    // Dragonfly (blue/teal)
    drawBird(2, 1, true, irr::video::SColor(255, 80, 180, 220));   // Dragonfly wings up
    drawBird(3, 1, false, irr::video::SColor(255, 80, 180, 220));  // Dragonfly wings down

    // Row 2: Crow, Seagull
    drawBird(0, 2, true, irr::video::SColor(255, 40, 40, 45));     // Crow wings up (dark gray)
    drawBird(1, 2, false, irr::video::SColor(255, 40, 40, 45));    // Crow wings down
    drawBird(2, 2, true, irr::video::SColor(255, 240, 240, 245));  // Seagull wings up (white)
    drawBird(3, 2, false, irr::video::SColor(255, 240, 240, 245)); // Seagull wings down

    // Row 3: Firefly glow states (bright yellow-green)
    drawFirefly(0, 3, 0.5f, irr::video::SColor(255, 200, 255, 50)); // Firefly glow 1
    drawFirefly(1, 3, 1.0f, irr::video::SColor(255, 220, 255, 80)); // Firefly glow 2

    // Create texture from image
    atlasTexture_ = driver_->addTexture("creature_atlas", img);
    img->drop();

    if (atlasTexture_) {
        LOG_DEBUG(MOD_GRAPHICS, "BoidsManager: Created procedural creature atlas");
    } else {
        LOG_ERROR(MOD_GRAPHICS, "BoidsManager: Failed to create atlas texture");
    }
}

void BoidsManager::renderBillboard(const Creature& c,
                                    const irr::core::vector3df& cameraPos,
                                    const irr::core::vector3df& cameraUp)
{
    // Convert creature position to Irrlicht coordinates (EQ uses Z-up, Irrlicht uses Y-up)
    irr::core::vector3df pos(c.position.x, c.position.z, c.position.y);

    // Calculate billboard orientation
    irr::core::vector3df toCamera = cameraPos - pos;
    toCamera.normalize();

    irr::core::vector3df right = cameraUp.crossProduct(toCamera);
    right.normalize();

    irr::core::vector3df up = toCamera.crossProduct(right);
    up.normalize();

    // Calculate quad vertices
    // c.size is typically 0.8-1.2, scale for reasonable bird size
    float halfSize = c.size * 0.8f;
    irr::core::vector3df v0 = pos + (-right + up) * halfSize;
    irr::core::vector3df v1 = pos + (right + up) * halfSize;
    irr::core::vector3df v2 = pos + (right - up) * halfSize;
    irr::core::vector3df v3 = pos + (-right - up) * halfSize;

    // Get UV coordinates for this creature's texture tile
    float u0, v0Uv, u1, v1Uv;
    getAtlasUVs(c.textureIndex, u0, v0Uv, u1, v1Uv);

    // Use white color to show texture colors properly, with full alpha
    irr::video::SColor color(255, 255, 255, 255);

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

void BoidsManager::getAtlasUVs(uint8_t tileIndex, float& u0, float& v0, float& u1, float& v1) const {
    // Calculate tile position in atlas
    int col = tileIndex % CreatureAtlas::AtlasColumns;
    int row = tileIndex / CreatureAtlas::AtlasColumns;

    float tileWidth = 1.0f / CreatureAtlas::AtlasColumns;
    float tileHeight = 1.0f / CreatureAtlas::AtlasRows;

    u0 = col * tileWidth;
    v0 = row * tileHeight;
    u1 = u0 + tileWidth;
    v1 = v0 + tileHeight;
}

glm::vec3 BoidsManager::getRandomSpawnPosition() const {
    glm::vec3 pos = envState_.playerPosition;

    // Spawn at a random distance from player
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> distDist(40.0f, 80.0f);

    float angle = angleDist(const_cast<std::mt19937&>(rng_));
    float dist = distDist(const_cast<std::mt19937&>(rng_));

    pos.x += std::cos(angle) * dist;
    pos.y += std::sin(angle) * dist;

    // Clamp to zone bounds if available
    if (hasZoneBounds_) {
        pos.x = glm::clamp(pos.x, zoneBoundsMin_.x + 20.0f, zoneBoundsMax_.x - 20.0f);
        pos.y = glm::clamp(pos.y, zoneBoundsMin_.y + 20.0f, zoneBoundsMax_.y - 20.0f);
    }

    return pos;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
