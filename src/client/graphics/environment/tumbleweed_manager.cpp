#include "client/graphics/environment/tumbleweed_manager.h"
#include "client/graphics/environment/environment_config.h"
#include "client/graphics/simulation_worker.h"
#include "common/logging.h"
#include <cmath>
#include <sstream>

namespace EQT {
namespace Graphics {
namespace Environment {

TumbleweedManager::TumbleweedManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver)
    : smgr_(smgr)
    , driver_(driver)
{
}

TumbleweedManager::~TumbleweedManager() {
    for (auto& tw : pool_) {
        if (tw.node) {
            tw.node->remove();
            tw.node = nullptr;
        }
    }
    if (tumbleweedMesh_) {
        tumbleweedMesh_->drop();
        tumbleweedMesh_ = nullptr;
    }
    tumbleweedTexture_ = nullptr;
}

bool TumbleweedManager::init() {
    if (initialized_) return true;

    reloadSettings();

    std::string texPath = "data/textures/tumbleweed.png";
    tumbleweedTexture_ = driver_->getTexture(texPath.c_str());
    if (!tumbleweedTexture_) {
        LOG_WARN(MOD_GRAPHICS, "TumbleweedManager: Texture not found at {}, disabling tumbleweeds. Run generate_textures tool.", texPath);
        initialized_ = false;
        return false;
    }

    tumbleweedMesh_ = createTumbleweedMesh();
    if (!tumbleweedMesh_) {
        LOG_ERROR(MOD_GRAPHICS, "TumbleweedManager: Failed to create tumbleweed mesh");
        return false;
    }

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "TumbleweedManager initialized (max={})", settings_.maxActive);
    return true;
}

void TumbleweedManager::reloadSettings() {
    const auto& cfg = EnvironmentEffectsConfig::instance().getTumbleweeds();
    settings_.enabled = cfg.enabled;
    settings_.maxActive = cfg.maxActive;
    settings_.spawnRate = cfg.spawnRate;
    settings_.spawnDistance = cfg.spawnDistance;
    settings_.despawnDistance = cfg.despawnDistance;
    settings_.minSpeed = cfg.minSpeed;
    settings_.maxSpeed = cfg.maxSpeed;
    settings_.windInfluence = cfg.windInfluence;
    settings_.bounceDecay = cfg.bounceDecay;
    settings_.maxLifetime = cfg.maxLifetime;
    settings_.groundOffset = cfg.groundOffset;
    settings_.sizeMin = cfg.sizeMin;
    settings_.sizeMax = cfg.sizeMax;
    settings_.maxBounces = cfg.maxBounces;
}

void TumbleweedManager::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    TumbleweedCommandData cmd;
    cmd.type = TumbleweedCommand::SetEnabled;
    cmd.enabled = enabled;
    pendingCommands_.push_back(std::move(cmd));
    if (!enabled) {
        for (auto& tw : pool_) {
            if (tw.active && tw.node) tw.node->setVisible(false);
            tw.active = false;
        }
        cachedActiveCount_ = 0;
    }
}

void TumbleweedManager::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    // Hide existing nodes
    for (auto& tw : pool_) {
        if (tw.active && tw.node) tw.node->setVisible(false);
        tw.active = false;
    }
    cachedActiveCount_ = 0;

    TumbleweedCommandData cmd;
    cmd.type = TumbleweedCommand::ZoneEnter;
    cmd.zoneName = zoneName;
    cmd.zoneBiome = static_cast<int>(biome);
    pendingCommands_.push_back(std::move(cmd));
    LOG_DEBUG(MOD_GRAPHICS, "TumbleweedManager: Zone '{}' biome={}", zoneName, static_cast<int>(biome));
}

void TumbleweedManager::onZoneLeave() {
    for (auto& tw : pool_) {
        if (tw.active && tw.node) tw.node->setVisible(false);
        tw.active = false;
    }
    cachedActiveCount_ = 0;

    TumbleweedCommandData cmd;
    cmd.type = TumbleweedCommand::ZoneLeave;
    pendingCommands_.push_back(std::move(cmd));
}

std::vector<TumbleweedCommandData> TumbleweedManager::drainCommands() {
    std::vector<TumbleweedCommandData> result;
    result.swap(pendingCommands_);
    return result;
}

TumbleweedInstance* TumbleweedManager::findOrCreatePoolSlot(int poolIndex) {
    // Search existing pool for this index
    for (auto& tw : pool_) {
        if (tw.poolIndex == poolIndex) return &tw;
    }
    // Search for an inactive slot
    for (auto& tw : pool_) {
        if (!tw.active && tw.poolIndex == -1) {
            tw.poolIndex = poolIndex;
            return &tw;
        }
    }
    // Create new slot
    TumbleweedInstance newSlot;
    newSlot.poolIndex = poolIndex;
    pool_.push_back(newSlot);
    return &pool_.back();
}

void TumbleweedManager::applyWorkerResults(const SimulationOutput::TumbleweedOutput& results) {
    if (!initialized_ || !smgr_) return;

    // Process spawn events — create/activate scene nodes
    for (const auto& spawn : results.spawns) {
        auto* tw = findOrCreatePoolSlot(spawn.poolIndex);
        if (!tw->node && tumbleweedMesh_) {
            tw->node = smgr_->addMeshSceneNode(tumbleweedMesh_);
            if (tw->node) {
                tw->node->setMaterialFlag(irr::video::EMF_LIGHTING, true);
                tw->node->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
            }
        }
        if (tw->node) {
            tw->node->setVisible(true);
            tw->node->setScale(irr::core::vector3df(spawn.size, spawn.size, spawn.size));
        }
        tw->active = true;
    }

    // Process despawn events — hide scene nodes
    for (const auto& despawn : results.despawns) {
        for (auto& tw : pool_) {
            if (tw.poolIndex == despawn.poolIndex) {
                tw.active = false;
                tw.poolIndex = -1;  // Free slot
                if (tw.node) tw.node->setVisible(false);
                break;
            }
        }
    }

    // Update positions/rotations of active tumbleweeds
    for (const auto& tr : results.tumbleweeds) {
        for (auto& tw : pool_) {
            if (tw.poolIndex == tr.poolIndex && tw.node) {
                // EQ→Irrlicht: (x, z, y)
                tw.node->setPosition(irr::core::vector3df(
                    tr.position.x, tr.position.z, tr.position.y));
                tw.node->setRotation(irr::core::vector3df(
                    tr.rotation.x, tr.rotation.y, tr.rotation.z));
                break;
            }
        }
    }

    cachedActiveCount_ = results.activeCount;
}

std::string TumbleweedManager::getDebugInfo() const {
    std::ostringstream ss;
    ss << "Tumbleweeds: " << cachedActiveCount_ << "/" << settings_.maxActive;
    return ss.str();
}

irr::scene::IMesh* TumbleweedManager::createTumbleweedMesh() {
    irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

    irr::video::SColor colorLight(255, 180, 150, 100);
    irr::video::SColor colorDark(255, 140, 110, 70);

    const float size = 1.0f;
    const int numPlanes = 3;

    for (int p = 0; p < numPlanes; ++p) {
        float angle = (float)p * 3.14159f / numPlanes;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        irr::core::vector3df v0(-size * cosA, -size, -size * sinA);
        irr::core::vector3df v1(size * cosA, -size, size * sinA);
        irr::core::vector3df v2(size * cosA, size, size * sinA);
        irr::core::vector3df v3(-size * cosA, size, -size * sinA);
        irr::core::vector3df normal(sinA, 0.0f, -cosA);

        irr::u32 baseIdx = buffer->Vertices.size();
        irr::video::SColor c0 = (p % 2 == 0) ? colorLight : colorDark;
        irr::video::SColor c1 = (p % 2 == 0) ? colorDark : colorLight;

        buffer->Vertices.push_back(irr::video::S3DVertex(v0, normal, c0, irr::core::vector2df(0, 1)));
        buffer->Vertices.push_back(irr::video::S3DVertex(v1, normal, c1, irr::core::vector2df(1, 1)));
        buffer->Vertices.push_back(irr::video::S3DVertex(v2, normal, c0, irr::core::vector2df(1, 0)));
        buffer->Vertices.push_back(irr::video::S3DVertex(v3, normal, c1, irr::core::vector2df(0, 0)));

        buffer->Indices.push_back(baseIdx + 0);
        buffer->Indices.push_back(baseIdx + 1);
        buffer->Indices.push_back(baseIdx + 2);
        buffer->Indices.push_back(baseIdx + 0);
        buffer->Indices.push_back(baseIdx + 2);
        buffer->Indices.push_back(baseIdx + 3);

        buffer->Indices.push_back(baseIdx + 0);
        buffer->Indices.push_back(baseIdx + 2);
        buffer->Indices.push_back(baseIdx + 1);
        buffer->Indices.push_back(baseIdx + 0);
        buffer->Indices.push_back(baseIdx + 3);
        buffer->Indices.push_back(baseIdx + 2);
    }

    buffer->recalculateBoundingBox();
    buffer->Material.Lighting = false;
    buffer->Material.BackfaceCulling = false;
    buffer->Material.AmbientColor = colorLight;
    buffer->Material.DiffuseColor = colorLight;

    if (tumbleweedTexture_) {
        buffer->Material.setTexture(0, tumbleweedTexture_);
        buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
        buffer->Material.MaterialTypeParam = 0.1f;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    mesh->addMeshBuffer(buffer);
    mesh->recalculateBoundingBox();
    buffer->drop();

    return mesh;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
