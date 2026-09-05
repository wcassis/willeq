#include "client/graphics/entity_renderer.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/animation_constants.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>
#include <chrono>
#include <fstream>
#include <fmt/ranges.h>

namespace EQT {
namespace Graphics {

// EQ Titanium con color rules
// Green cutoff thresholds vary by player level
static int getGreenCutoff(int playerLevel) {
    if (playerLevel <= 7) return -4;
    if (playerLevel <= 12) return -6;
    if (playerLevel <= 20) return -8;
    if (playerLevel <= 25) return -10;
    if (playerLevel <= 30) return -12;
    if (playerLevel <= 35) return -14;
    if (playerLevel <= 40) return -16;
    if (playerLevel <= 45) return -18;
    if (playerLevel <= 50) return -20;
    if (playerLevel <= 55) return -21;
    return -22;  // 56-65
}

irr::video::SColor EntityRenderer::getConColor(int playerLevel, int entityLevel) {
    if (playerLevel <= 0 || entityLevel <= 0) {
        return irr::video::SColor(255, 255, 255, 255);  // White for unknown
    }

    int diff = entityLevel - playerLevel;
    int greenCutoff = getGreenCutoff(playerLevel);

    if (diff <= greenCutoff) {
        return irr::video::SColor(255, 128, 128, 128);  // Gray
    } else if (diff < -5) {
        return irr::video::SColor(255, 0, 200, 0);      // Green
    } else if (diff < -1) {
        return irr::video::SColor(255, 0, 160, 255);    // Light blue
    } else if (diff < 3) {
        return irr::video::SColor(255, 255, 255, 255);  // White
    } else if (diff < 6) {
        return irr::video::SColor(255, 255, 255, 0);    // Yellow
    } else {
        return irr::video::SColor(255, 255, 0, 0);      // Red
    }
}

EntityRenderer::EntityRenderer(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                               irr::io::IFileSystem* fileSystem)
    : smgr_(smgr), driver_(driver), fileSystem_(fileSystem) {
    // Create the race model loader
    raceModelLoader_ = std::make_unique<RaceModelLoader>(smgr, driver, fileSystem);

    // Create the equipment model loader
    equipmentModelLoader_ = std::make_unique<EquipmentModelLoader>(smgr, driver, fileSystem);
}

EntityRenderer::~EntityRenderer() {
    clearEntities();

    // Drop cached placeholder meshes
    for (auto& [raceId, mesh] : placeholderMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    placeholderMeshCache_.clear();
}

void EntityRenderer::setClientPath(const std::string& path) {
    clientPath_ = path;
    if (!clientPath_.empty() && clientPath_.back() != '/' && clientPath_.back() != '\\') {
        clientPath_ += '/';
    }
    // Also set path on model loaders
    if (raceModelLoader_) {
        raceModelLoader_->setClientPath(clientPath_);
    }
    if (equipmentModelLoader_) {
        equipmentModelLoader_->setClientPath(clientPath_);
    }
    // Race mappings are loaded once at app startup in IrrlichtRenderer::init()
}

bool EntityRenderer::loadGlobalCharacters() {
    if (!raceModelLoader_) {
        return false;
    }
    return raceModelLoader_->loadGlobalModels();
}

// T304 Leg 2: one colour source. The hand-written switch deleted here was the
// LAST surviving copy of the pre-JSON race numbering (T267/T270/T271 killed
// the other three, all of which were live and wrong): it called 21 "Skeleton"
// when row 21 is BEH Evil Eye, 26 "Snake" (FRO Froglok), 33 "Ghost" (GHU
// Ghoul), 35 "Rat" (EEL Eel), 42 "Rat" (WOL Wolf), 43 "Bat" (BEA Bear), 46
// "Goblin" (IMP Imp), 48/49 "Spider" (KOB Kobold / DRA Lava Dragon) and 85
// "Dragon" (SPE Spectre). The tint now comes from race_models.color, keyed on
// each row's own name.
//
// THOSE COLUMN VALUES ARE INVENTED, by Will's ruling of 2026-08-28 -- the bar
// is "plausible and distinguishable", not authenticity, because the cube is
// reached only when no model could be built for the race (T213). They are not
// sourced from EverQuest data.
irr::video::SColor EntityRenderer::getColorForRace(uint16_t raceId) const {
    const uint32_t argb = EQT::Graphics::getRaceColorARGB(raceId);
    return irr::video::SColor(argb);
}

// T271: one scale source. RaceModelLoader::getRaceScale() is a one-line
// forward to EQT::Graphics::getRaceScale(), which is db-only now, so the
// loader-null branch no longer needs (or is allowed) a second opinion. The
// switch deleted here was a third copy of the pre-JSON numbering and the
// worst of them: 42 -> 0.3 "Rat" (row 42 is WOL Wolf, 0.8) and 43 -> 0.4
// "Bat" (row 43 is BEA Bear, 1.0).
float EntityRenderer::getScaleForRace(uint16_t raceId) const {
    return EQT::Graphics::getRaceScale(raceId);
}

void EntityRenderer::applyAlphaTestMaterialType(irr::video::SMaterial& mat) const {
    // H02d Option A (P21, mirrors ZoneMeshBuilder/DoorManager): ETC1-sourced
    // textures carry a companion alpha texture on unit 1 (uAlphaTexture) —
    // use the existing companion variant, binding the companion if available.
    // The direct RGBA/DDS decode path has real alpha in the main texture's
    // own alpha channel and never builds a companion; using the companion
    // variant there reads whatever the driver's opaque-white fallback puts on
    // unit 1, which never discards — the RGBA variant reads uTexture.a
    // directly instead.
    irr::video::ITexture* tex = mat.getTexture(0);
    bool isETC1 = constrainedTextureCache_ && tex && constrainedTextureCache_->isETC1Texture(tex);
    if (isETC1) {
        if (shaderMaterialAlphaTest_ >= 0) {
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
        }
        if (constrainedTextureCache_) {
            irr::video::ITexture* alphaTex = constrainedTextureCache_->getAlphaTexture(tex);
            if (alphaTex) {
                mat.setTexture(1, alphaTex);
            }
        }
    } else if (shaderMaterialAlphaTestRGBA_ >= 0) {
        mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTestRGBA_);
    } else if (shaderMaterialAlphaTest_ >= 0) {
        // No RGBA variant wired — fall back to the companion variant (pre-P21
        // behavior) rather than leaving MaterialType unset.
        mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
    }
}

irr::scene::IMesh* EntityRenderer::createPlaceholderMesh(float size, irr::video::SColor color) {
    // Create a simple colored cube as placeholder
    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

    float half = size / 2.0f;

    // Define vertices for a cube
    irr::video::S3DVertex vertices[8] = {
        // Bottom
        {-half, 0, -half, 0, -1, 0, color, 0, 1},
        { half, 0, -half, 0, -1, 0, color, 1, 1},
        { half, 0,  half, 0, -1, 0, color, 1, 0},
        {-half, 0,  half, 0, -1, 0, color, 0, 0},
        // Top
        {-half, size, -half, 0, 1, 0, color, 0, 1},
        { half, size, -half, 0, 1, 0, color, 1, 1},
        { half, size,  half, 0, 1, 0, color, 1, 0},
        {-half, size,  half, 0, 1, 0, color, 0, 0},
    };

    // Define indices for cube faces (12 triangles)
    irr::u16 indices[36] = {
        // Bottom
        0, 1, 2, 0, 2, 3,
        // Top
        4, 6, 5, 4, 7, 6,
        // Front
        0, 5, 1, 0, 4, 5,
        // Back
        2, 7, 3, 2, 6, 7,
        // Left
        3, 4, 0, 3, 7, 4,
        // Right
        1, 6, 2, 1, 5, 6,
    };

    for (int i = 0; i < 8; ++i) {
        buffer->Vertices.push_back(vertices[i]);
    }
    for (int i = 0; i < 36; ++i) {
        buffer->Indices.push_back(indices[i]);
    }
    buffer->recalculateBoundingBox();

    buffer->Material.BackfaceCulling = false;
    if (shaderMaterialSolid_ >= 0) {
        buffer->Material.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
        buffer->Material.Lighting = false;
    } else {
        buffer->Material.Lighting = lightingEnabled_;
        if (lightingEnabled_) {
            buffer->Material.NormalizeNormals = true;
            buffer->Material.AmbientColor = irr::video::SColor(255, 255, 255, 255);
            buffer->Material.DiffuseColor = irr::video::SColor(255, 255, 255, 255);
        }
    }

    mesh->addMeshBuffer(buffer);
    buffer->drop();
    mesh->recalculateBoundingBox();

    return mesh;
}

irr::scene::IMesh* EntityRenderer::getMeshForRace(uint16_t raceId, uint8_t gender,
                                                    const EntityAppearance& appearance) {
    // Try to load actual 3D model for this race
    if (raceModelLoader_) {
        irr::scene::IMesh* mesh = nullptr;

        // Use appearance-based variant selection if helm or texture is non-default
        // - helm: head mesh variant (HE01, HE02, etc.)
        // - texture: body armor (10+ = robes with different body mesh)
        if (appearance.helm != 0 || appearance.texture >= 10) {
            uint8_t headVariant = appearance.helm;
            uint8_t bodyVariant = (appearance.texture >= 10) ? appearance.texture : 0;
            mesh = raceModelLoader_->getMeshForRaceWithAppearance(raceId, gender,
                                                                   headVariant, bodyVariant);
        } else {
            mesh = raceModelLoader_->getMeshForRace(raceId, gender);
        }

        if (mesh) {
            return mesh;
        }
    }

    // Fall back to placeholder mesh if no model found
    auto it = placeholderMeshCache_.find(raceId);
    if (it != placeholderMeshCache_.end()) {
        return it->second;
    }

    // Create placeholder mesh
    irr::video::SColor color = getColorForRace(raceId);
    float baseSize = 10.0f;  // Base size for placeholder cubes
    irr::scene::IMesh* mesh = createPlaceholderMesh(baseSize, color);

    // Cache it
    placeholderMeshCache_[raceId] = mesh;
    return mesh;
}

void EntityRenderer::setCurrentZone(const std::string& zoneName) {
    if (raceModelLoader_) {
        raceModelLoader_->setCurrentZone(zoneName);
    }
}

void EntityRenderer::loadNumberedGlobals() {
    if (raceModelLoader_) {
        raceModelLoader_->loadNumberedGlobalModels();
    }
}

bool EntityRenderer::setEntityAnimation(uint16_t spawnId, const std::string& animCode, bool loop, bool playThrough) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        LOG_DEBUG(MOD_GRAPHICS, "setEntityAnimation: spawn_id={} not found in entities_ (size={})",
                  spawnId, entities_.size());
        return false;
    }

    EntityVisual& visual = it->second;
    if (!visual.isAnimated || !visual.animatedNode) {
        LOG_DEBUG(MOD_GRAPHICS, "setEntityAnimation: spawn_id={} isAnimated={} animatedNode={}",
                  spawnId, visual.isAnimated, (visual.animatedNode ? "yes" : "no"));
        return false;
    }

    // Try the requested animation first
    auto& animator = visual.animatedNode->getAnimator();
    auto skeleton = animator.getSkeleton();
    if (!skeleton) {
        LOG_WARN(MOD_GRAPHICS, "setEntityAnimation: spawn_id={} has NO skeleton!", spawnId);
    }
    if (visual.animatedNode->playAnimation(animCode, loop, playThrough)) {
        visual.currentAnimation = animCode;
        return true;
    }

    // Animation not found - try fallback to first available animation of same class
    if (animCode.length() >= 1) {
        char animClass = std::tolower(animCode[0]);

        // Get list of available animations and find first matching class
        auto availableAnims = visual.animatedNode->getAnimationList();
        std::string fallbackAnim;

        for (const auto& anim : availableAnims) {
            if (!anim.empty() && std::tolower(anim[0]) == animClass) {
                fallbackAnim = anim;
                break;  // Use first matching animation
            }
        }

        if (!fallbackAnim.empty()) {
            if (visual.animatedNode->playAnimation(fallbackAnim, loop, playThrough)) {
                LOG_DEBUG(MOD_GRAPHICS, "setEntityAnimation: spawn_id={} animation '{}' not found, falling back to '{}'",
                          spawnId, animCode, fallbackAnim);
                visual.currentAnimation = fallbackAnim;
                return true;
            }
        }

        // No animation of this class available - log what we DO have
        std::string availAnimsStr;
        for (const auto& a : availableAnims) {
            if (!availAnimsStr.empty()) availAnimsStr += ",";
            availAnimsStr += a;
        }
        LOG_WARN(MOD_GRAPHICS, "setEntityAnimation: spawn_id={} no '{}' class animation available (requested '{}'), available: [{}]",
                 spawnId, animClass, animCode, availAnimsStr);
    } else {
        LOG_DEBUG(MOD_GRAPHICS, "setEntityAnimation: spawn_id={} playAnimation('{}') returned false",
                  spawnId, animCode);
    }

    return false;
}

void EntityRenderer::markEntityAsCorpse(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;
    visual.isCorpse = true;
    visual.corpseTime = 0.0f;  // Reset corpse timer

    // Stop all movement - corpses don't move
    visual.velocityX = 0.0f;
    visual.velocityY = 0.0f;
    visual.velocityZ = 0.0f;
    visual.serverAnimation = 0;  // Clear server animation (which drives NPC movement)

    // Update name to include "'s corpse" suffix
    if (visual.name.find("'s corpse") == std::string::npos &&
        visual.name.find("'s_corpse") == std::string::npos) {
        visual.name += "'s corpse";

        // Update the name tag text node
        if (visual.nameNode) {
            std::wstring wname = EQT::toDisplayNameW(visual.name);
            visual.nameNode->setText(wname.c_str());
        }
    }

    // Play death animation if animated - d05 is the actual death animation
    if (visual.isAnimated && visual.animatedNode) {
        std::string deathAnim;
        bool animFound = false;

        if (visual.animatedNode->hasAnimation("d05")) {
            deathAnim = "d05";
            animFound = true;
        } else if (visual.animatedNode->hasAnimation("d02")) {
            deathAnim = "d02";
            animFound = true;
        } else if (visual.animatedNode->hasAnimation("d01")) {
            deathAnim = "d01";
            animFound = true;
        }

        if (animFound) {
            // Stop any current animation first to ensure clean state
            visual.animatedNode->stopAnimation();

            // Play death animation with loop=false (holds on last frame) and playThrough=true
            bool playResult = visual.animatedNode->playAnimation(deathAnim, false, true);
            visual.currentAnimation = deathAnim;

            // Verify playThrough was set
            SkeletalAnimator& animator = visual.animatedNode->getAnimator();
            LOG_DEBUG(MOD_ENTITY, "CORPSE Entity {} playing death animation '{}' result={} playThrough={} state={}",
                      spawnId, deathAnim, playResult, animator.isPlayingThrough(),
                      (animator.getState() == AnimationState::Playing ? "Playing" : "Other"));
        } else {
            LOG_WARN(MOD_ENTITY, "CORPSE Entity {} WARNING: No death animation found!", spawnId);
            // List available animations for debugging
            auto anims = visual.animatedNode->getAnimationList();
            std::string animList;
            for (const auto& a : anims) {
                animList += a + " ";
            }
            LOG_DEBUG(MOD_ENTITY, "CORPSE Available animations: {}", animList);
        }
    }
}

void EntityRenderer::setEntityPoseState(uint16_t spawnId, EntityVisual::PoseState pose) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }
    it->second.poseState = pose;
}

EntityVisual::PoseState EntityRenderer::getEntityPoseState(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return EntityVisual::PoseState::Standing;
    }
    return it->second.poseState;
}

void EntityRenderer::setEntityWeaponSkills(uint16_t spawnId, uint8_t primaryWeaponSkill, uint8_t secondaryWeaponSkill) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }
    it->second.primaryWeaponSkill = primaryWeaponSkill;
    it->second.secondaryWeaponSkill = secondaryWeaponSkill;
    LOG_DEBUG(MOD_ENTITY, "Set weapon skills for spawn {}: primary={}, secondary={}",
              spawnId, primaryWeaponSkill, secondaryWeaponSkill);
}

void EntityRenderer::setEntityWeaponDelay(uint16_t spawnId, float delayMs) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }
    it->second.weaponDelayMs = delayMs;
    LOG_DEBUG(MOD_ENTITY, "Set weapon delay for spawn {}: {}ms", spawnId, delayMs);
}

void EntityRenderer::setEntityLight(uint16_t spawnId, uint8_t lightLevel) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    // No change needed
    if (visual.lightLevel == lightLevel) {
        return;
    }

    visual.lightLevel = lightLevel;

    // If mesh not built yet, just store level for later application in buildEntityMesh()
    if (!visual.meshBuilt) {
        return;
    }

    if (lightLevel == 0) {
        // Remove existing light
        if (visual.lightNode) {
            // T391 (B202): detach, then release OUR reference (the grab below).
            // remove() is a no-op when the worker has already culled the entity
            // and detached this node.
            visual.lightNode->remove();
            visual.lightNode->drop();
            visual.lightNode = nullptr;
            LOG_DEBUG(MOD_ENTITY, "Removed light from entity {} ({})", spawnId, visual.name);
        }
        return;
    }

    // Calculate light properties based on level
    // Server sends light TYPE (0-15), convert to level (0-10) for intensity
    uint8_t level = lightsource::TypeToLevel(lightLevel);
    float intensity = level / 10.0f;  // 0-10 scale to 0.0-1.0
    float radius = 20.0f + (level / 10.0f) * 80.0f;  // 20-100 range

    // Warm light color (slightly yellow/orange like torchlight)
    float r = std::min(1.0f, 0.9f + intensity * 0.1f);
    float g = std::min(1.0f, 0.7f + intensity * 0.2f);
    float b = std::min(1.0f, 0.4f + intensity * 0.2f);

    // Get entity position in Irrlicht coordinates (Y-up)
    // Light positioned slightly above entity's head
    irr::core::vector3df lightPos(visual.lastX, visual.lastZ + visual.modelYOffset + 3.0f, visual.lastY);

    if (visual.lightNode) {
        // Update existing light
        irr::video::SLight& lightData = visual.lightNode->getLightData();
        lightData.DiffuseColor = irr::video::SColorf(r * intensity, g * intensity, b * intensity, 1.0f);
        lightData.Radius = radius;
        visual.lightNode->setPosition(lightPos);
    } else {
        // Create new light
        visual.lightNode = smgr_->addLightSceneNode(
            nullptr,  // Not parented to entity so we can control position independently
            lightPos,
            irr::video::SColorf(r * intensity, g * intensity, b * intensity, 1.0f),
            radius
        );

        if (visual.lightNode) {
            // T391 (B202): OWN a reference. addLightSceneNode(nullptr, ...)
            // parents the node to the ROOT scene node and drops its
            // construction reference, so the node comes back with a count of
            // ONE, held by the parent alone -- while EntityVisual keeps a raw
            // pointer to it. SimBridgeComponent::applySimulationOutput()
            // detaches this node with remove() every time the worker culls the
            // entity, and remove() is Parent->removeChild(this), which drops:
            // at a count of one that DELETES the node and leaves
            // visual.lightNode dangling, so the next frame that re-admits the
            // entity ran addChild() on freed memory on the render thread.
            // That is the whole of B202 (see refactor/T391_two_client_segv.md
            // and tests/test_entity_light_node_lifetime.cpp). This is the same
            // grab-then-detach ownership the object lights already use
            // (renderer_zone_load_objects.cpp) and that the entity's own
            // sceneNode already has. Every path that clears lightNode must
            // drop() this reference.
            visual.lightNode->grab();
            irr::video::SLight& lightData = visual.lightNode->getLightData();
            lightData.Type = irr::video::ELT_POINT;
            // Attenuation: 1/(constant + linear*d + quadratic*d²)
            // constant=1 for full brightness at source
            lightData.Attenuation = irr::core::vector3df(1.0f, 0.007f, 0.0002f);
            LOG_INFO(MOD_ENTITY, "Created light for entity {} ({}): level={}, radius={:.1f}",
                     spawnId, visual.name, lightLevel, radius);
        }
    }
}

void EntityRenderer::setConstrainedConfig(const ConstrainedRendererConfig* config) {
    constrainedConfig_ = config;
    if (config) {
        LOG_INFO(MOD_GRAPHICS, "EntityRenderer: max {} entities within {} units",
                 config->maxVisibleEntities, config->entityRenderDistance);
    }
}

} // namespace Graphics
} // namespace EQT
