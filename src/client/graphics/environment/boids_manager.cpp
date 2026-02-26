#include "client/graphics/environment/boids_manager.h"
#include "client/graphics/environment/environment_config.h"
#include "client/graphics/simulation_worker.h"
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
{
    for (size_t i = 0; i < static_cast<size_t>(CreatureType::Count); ++i) {
        typeEnabled_[i] = true;
    }
}

BoidsManager::~BoidsManager() = default;

bool BoidsManager::init(const std::string& eqClientPath) {
    if (initialized_) return true;

    std::string atlasPath = "data/textures/creature_atlas.png";
    if (!loadCreatureAtlas(atlasPath)) {
        LOG_WARN(MOD_GRAPHICS, "BoidsManager: Creature atlas not found at {}, disabling boids. Run generate_textures tool.", atlasPath);
        enabled_ = false;
        return true;
    }

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

void BoidsManager::render() {
    if (!enabled_ || !initialized_ || quality_ == 0 || !driver_ || cachedCreatures_.empty()) {
        return;
    }

    irr::scene::ICameraSceneNode* camera = smgr_->getActiveCamera();
    if (!camera) return;

    irr::core::vector3df cameraPos = camera->getAbsolutePosition();
    irr::core::vector3df cameraUp = camera->getUpVector();

    driver_->setMaterial(creatureMaterial_);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    for (const auto& c : cachedCreatures_) {
        // Convert to Irrlicht coordinates (EQ Z-up → Irrlicht Y-up)
        irr::core::vector3df pos(c.position.x, c.position.z, c.position.y);

        // Billboard orientation
        irr::core::vector3df toCamera = cameraPos - pos;
        toCamera.normalize();
        irr::core::vector3df right = cameraUp.crossProduct(toCamera);
        right.normalize();
        irr::core::vector3df up = toCamera.crossProduct(right);
        up.normalize();

        float halfSize = c.size * 0.8f;
        irr::core::vector3df v0 = pos + (-right + up) * halfSize;
        irr::core::vector3df v1 = pos + (right + up) * halfSize;
        irr::core::vector3df v2 = pos + (right - up) * halfSize;
        irr::core::vector3df v3 = pos + (-right - up) * halfSize;

        float u0, v0Uv, u1, v1Uv;
        getAtlasUVs(c.textureIndex, u0, v0Uv, u1, v1Uv);

        irr::video::SColor color(255, 255, 255, 255);
        irr::video::S3DVertex vertices[4];
        vertices[0] = irr::video::S3DVertex(v0, toCamera, color, irr::core::vector2df(u0, v0Uv));
        vertices[1] = irr::video::S3DVertex(v1, toCamera, color, irr::core::vector2df(u1, v0Uv));
        vertices[2] = irr::video::S3DVertex(v2, toCamera, color, irr::core::vector2df(u1, v1Uv));
        vertices[3] = irr::video::S3DVertex(v3, toCamera, color, irr::core::vector2df(u0, v1Uv));

        irr::u16 indices[6] = {0, 1, 2, 0, 2, 3};
        driver_->drawVertexPrimitiveList(vertices, 4, indices, 2,
                                          irr::video::EVT_STANDARD,
                                          irr::scene::EPT_TRIANGLES,
                                          irr::video::EIT_16BIT);
    }
}

void BoidsManager::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    currentBiome_ = biome;
    BoidsCommandData cmd;
    cmd.type = BoidsCommand::ZoneEnter;
    cmd.zoneName = zoneName;
    cmd.zoneBiome = static_cast<int>(biome);
    cmd.hasBounds = false;
    pendingCommands_.push_back(std::move(cmd));
    LOG_INFO(MOD_GRAPHICS, "BoidsManager: Entering zone '{}' biome={}", zoneName, static_cast<int>(biome));
}

void BoidsManager::onZoneEnter(const std::string& zoneName, ZoneBiome biome,
                                const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
    currentBiome_ = biome;
    BoidsCommandData cmd;
    cmd.type = BoidsCommand::ZoneEnter;
    cmd.zoneName = zoneName;
    cmd.zoneBiome = static_cast<int>(biome);
    cmd.boundsMin = boundsMin;
    cmd.boundsMax = boundsMax;
    cmd.hasBounds = true;
    pendingCommands_.push_back(std::move(cmd));
    LOG_INFO(MOD_GRAPHICS, "BoidsManager: Entering zone '{}' biome={} with bounds", zoneName, static_cast<int>(biome));
}

void BoidsManager::onZoneLeave() {
    BoidsCommandData cmd;
    cmd.type = BoidsCommand::ZoneLeave;
    pendingCommands_.push_back(std::move(cmd));
    cachedCreatures_.clear();
    cachedActiveCount_ = 0;
    currentBiome_ = ZoneBiome::Unknown;
    LOG_DEBUG(MOD_GRAPHICS, "BoidsManager: Zone leave");
}

void BoidsManager::setQuality(int quality) {
    quality_ = glm::clamp(quality, 0, 3);
    BoidsCommandData cmd;
    cmd.type = BoidsCommand::SetQuality;
    cmd.quality = quality_;
    pendingCommands_.push_back(std::move(cmd));
}

void BoidsManager::setDensity(float density) {
    userDensity_ = glm::clamp(density, 0.0f, 1.0f);
    BoidsCommandData cmd;
    cmd.type = BoidsCommand::SetDensity;
    cmd.density = userDensity_;
    pendingCommands_.push_back(std::move(cmd));
}

void BoidsManager::setTypeEnabled(CreatureType type, bool enabled) {
    if (type < CreatureType::Count) {
        typeEnabled_[static_cast<size_t>(type)] = enabled;
        BoidsCommandData cmd;
        cmd.type = BoidsCommand::SetTypeEnabled;
        cmd.creatureType = static_cast<uint8_t>(type);
        cmd.typeEnabled = enabled;
        pendingCommands_.push_back(std::move(cmd));
    }
}

bool BoidsManager::isTypeEnabled(CreatureType type) const {
    if (type < CreatureType::Count) return typeEnabled_[static_cast<size_t>(type)];
    return false;
}

void BoidsManager::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    BoidsCommandData cmd;
    cmd.type = BoidsCommand::SetEnabled;
    cmd.enabled = enabled;
    pendingCommands_.push_back(std::move(cmd));
    if (!enabled) {
        cachedCreatures_.clear();
        cachedActiveCount_ = 0;
    }
}

std::vector<BoidsCommandData> BoidsManager::drainCommands() {
    std::vector<BoidsCommandData> result;
    result.swap(pendingCommands_);
    return result;
}

void BoidsManager::applyWorkerResults(const SimulationOutput::BoidsOutput& results) {
    cachedCreatures_.clear();
    cachedCreatures_.reserve(results.creatures.size());
    for (const auto& cr : results.creatures) {
        cachedCreatures_.push_back({cr.position, cr.size, cr.textureIndex, cr.alpha});
    }
    cachedActiveCount_ = results.activeCount;
}

std::string BoidsManager::getDebugInfo() const {
    std::ostringstream ss;
    ss << "Boids: " << cachedActiveCount_;
    ss << " | Quality: ";
    switch (quality_) {
        case 0: ss << "Off"; break;
        case 1: ss << "Low"; break;
        case 2: ss << "Med"; break;
        case 3: ss << "High"; break;
    }
    return ss.str();
}

void BoidsManager::reloadSettings() {
    if (!EnvironmentEffectsConfig::instance().reload()) {
        LOG_WARN(MOD_GRAPHICS, "BoidsManager: Failed to reload config");
        return;
    }
    LOG_INFO(MOD_GRAPHICS, "BoidsManager: Reloaded settings");
}

bool BoidsManager::loadCreatureAtlas(const std::string& path) {
    if (!driver_) return false;
    atlasTexture_ = driver_->getTexture(path.c_str());
    return atlasTexture_ != nullptr;
}

void BoidsManager::getAtlasUVs(uint8_t tileIndex, float& u0, float& v0, float& u1, float& v1) const {
    int col = tileIndex % CreatureAtlas::AtlasColumns;
    int row = tileIndex / CreatureAtlas::AtlasColumns;
    float tileWidth = 1.0f / CreatureAtlas::AtlasColumns;
    float tileHeight = 1.0f / CreatureAtlas::AtlasRows;
    u0 = col * tileWidth;
    v0 = row * tileHeight;
    u1 = u0 + tileWidth;
    v1 = v0 + tileHeight;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
