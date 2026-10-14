#include "client/graphics/entity_renderer.h"
#include "client/graphics/corpse_death_pose.h"  // T486: the ONE death-pose ladder
#include "client/graphics/panfrost_gate.h"      // B1051: the ETC1 companion-alpha gate
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
#include "client/graphics/impostor/impostor_cache.h"  // B964: complete type for impostors_
#include "client/animation_constants.h"
#include "client/con_tier.h"  // B895: the one level-based con table
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

// EQ Titanium con color rules. B895 (GitLab #211) moved the table itself to
// src/client/con_tier.cpp so the game-thread tracking list colours its rows
// with the SAME one; this is now tier -> colour and nothing else.
irr::video::SColor EntityRenderer::getConColor(int playerLevel, int entityLevel) {
    const eqt::ConRgb c = eqt::ConTierRgb(eqt::ConTierForLevels(playerLevel, entityLevel));
    return irr::video::SColor(255, c.r, c.g, c.b);
}

EntityRenderer::EntityRenderer(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                               irr::io::IFileSystem* fileSystem)
    : smgr_(smgr), driver_(driver), fileSystem_(fileSystem) {
    buildScheduler_.setLegacy(EntityBuildScheduler::legacyFromEnv());   // B973 A/B
    // Create the race model loader
    raceModelLoader_ = std::make_unique<RaceModelLoader>(smgr, driver, fileSystem);
    // T504b: the render thread never parses an archive -- a model that is not
    // loaded is requested from the prep worker (spawnId 0 = race-level
    // request; its result is promoted like any other) and the caller waits
    // for the next frames, as a late spawn does.
    raceModelLoader_->setWorkerPreloadRequest([this](uint16_t raceId, uint8_t gender) {
        if (!entityPrepWorker_) return false;   // nobody to hand it to: load synchronously
        EntityPrepWorker::PrepRequest req;
        req.spawnId = 0;
        req.raceId = raceId;
        req.gender = gender;
        entityPrepWorker_->requestPrep(req);
        return true;
    });

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
    impostors_.reset();  // B964: pages, scratch, batch node, the tex.impostor row
    releaseNameTagBatch();  // B975
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
    //
    // B1051 (GitLab #371): on Panfrost the companion variant is NOT used for
    // ETC1 textures either — the single-sampler RGBA variant (which on ETC1
    // storage reads uTexture.a == 1.0 and thus draws opaque, no discard —
    // the working behaviour) is preferred. The WILLEQ_B1051_TRACE readback
    // showed the per-buffer GL binds are correct (distinct unit-0 + unit-1
    // objects per buffer), so the fault is the dual-sampler stage itself; the
    // single-sampler variant is the one render-path difference from the
    // correct RGBA config. See panfrost_gate.h and test_b1051_panfrost_gate.
    irr::video::ITexture* tex = mat.getTexture(0);
    bool isETC1 = constrainedTextureCache_ && tex && constrainedTextureCache_->isETC1Texture(tex);
    const AlphaVariant variant = selectAlphaVariant(
        glRendererString_, isETC1, shaderMaterialAlphaTestRGBA_ >= 0);
    if (variant == AlphaVariant::Companion) {
        if (shaderMaterialAlphaTest_ >= 0) {
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
        }
        if (constrainedTextureCache_) {
            irr::video::ITexture* alphaTex = constrainedTextureCache_->getAlphaTexture(tex);
            if (alphaTex) {
                mat.setTexture(1, alphaTex);
            }
        }
    } else {  // AlphaVariant::MainAlpha
        if (shaderMaterialAlphaTestRGBA_ >= 0) {
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTestRGBA_);
        } else if (shaderMaterialAlphaTest_ >= 0) {
            // No RGBA variant wired — fall back to the companion variant's
            // type only (no companion bound: either the texture is not ETC1,
            // or on Panfrost the gate fell through). Identical to the
            // pre-B1051 else-if chain.
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
        }
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

    // T486 (2026-09-14 audit, item 3): the ONE death-pose ladder
    // (corpse_death_pose.cpp), in its witnessed form -- the fall is played
    // through to its last frame rather than jumped to, because the player saw
    // it. This arm used to carry a third textual copy of the d05/d02/d01
    // ladder; tests/test_corpse_death_pose.cpp keeps it folded.
    if (visual.isAnimated && visual.animatedNode) {
        if (ApplyCorpseDeathPose(visual, visual.animatedNode, /*witnessed=*/true)) {
            SkeletalAnimator& animator = visual.animatedNode->getAnimator();
            LOG_DEBUG(MOD_ENTITY, "CORPSE Entity {} playing death animation '{}' playThrough={} state={}",
                      spawnId, visual.currentAnimation, animator.isPlayingThrough(),
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
    if (config && raceModelLoader_) {
        // T504b Leg 2b: budget pressure evicts despawned models at once.
        raceModelLoader_->setEntityModelBudgetBytes(
            config->memoryLedgerBudgetBytes[static_cast<size_t>(eqt::memory::Category::EntityModels)]);
    }
    if (config) {
        LOG_INFO(MOD_GRAPHICS, "EntityRenderer: max {} entities within {} units",
                 config->maxVisibleEntities, config->entityRenderDistance);
    }
}

} // namespace Graphics
} // namespace EQT
