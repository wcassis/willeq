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

irr::video::SColor EntityRenderer::getColorForRace(uint16_t raceId) const {
    // Placeholder-cube tint, reached only when no model could be built for
    // the race (T213). T271 audited this against race_models and left the
    // colours alone ON PURPOSE: there is no colour column, so re-keying it to
    // the audited rows would mean inventing a colour per race. The case
    // comments below are therefore NOT a name source -- several carry the
    // pre-JSON numbering and the rows contradict them (21 is BEH Evil Eye not
    // Skeleton, 26 FRO Froglok not Snake, 33 GHU Ghoul not Ghost, 35 EEL Eel
    // not Rat, 42 WOL Wolf not Rat, 43 BEA Bear not Bat, 46 IMP Imp not
    // Goblin, 48/49 KOB Kobold / DRA Lava Dragon not Spiders, 85 SPE Spectre
    // not Dragon). For a race NAME call EverQuest::GetRaceName() (T274).
    switch (raceId) {
        case 1:   // Human
            return irr::video::SColor(255, 200, 180, 160);
        case 2:   // Barbarian
            return irr::video::SColor(255, 180, 160, 140);
        case 3:   // Erudite
            return irr::video::SColor(255, 160, 140, 120);
        case 4:   // Wood Elf
            return irr::video::SColor(255, 140, 180, 140);
        case 5:   // High Elf
            return irr::video::SColor(255, 220, 220, 200);
        case 6:   // Dark Elf
            return irr::video::SColor(255, 80, 80, 100);
        case 7:   // Half Elf
            return irr::video::SColor(255, 180, 180, 160);
        case 8:   // Dwarf
            return irr::video::SColor(255, 180, 140, 100);
        case 9:   // Troll
            return irr::video::SColor(255, 100, 140, 100);
        case 10:  // Ogre
            return irr::video::SColor(255, 140, 120, 100);
        case 11:  // Halfling
            return irr::video::SColor(255, 180, 160, 140);
        case 12:  // Gnome
            return irr::video::SColor(255, 160, 160, 180);
        case 128: // Iksar
            return irr::video::SColor(255, 100, 140, 100);
        case 130: // Vah Shir
            return irr::video::SColor(255, 180, 160, 120);
        case 330: // Froglok
            return irr::video::SColor(255, 80, 160, 80);

        // Common NPCs/monsters
        case 21:  // Skeleton
        case 60:  // Skeleton (variant)
        case 367: // Decaying Skeleton
            return irr::video::SColor(255, 220, 220, 200);
        case 22:  // Beetle / Fire Beetle
            return irr::video::SColor(255, 80, 60, 40);
        case 24:  // Fish / Koalindl
            return irr::video::SColor(255, 100, 140, 180);
        case 26:  // Snake
        case 37:  // Giant Snake / Grass Snake
            return irr::video::SColor(255, 80, 100, 60);
        case 33:  // Ghost
        case 63:  // Ghost (variant)
            return irr::video::SColor(150, 200, 200, 255);  // Translucent blue
        case 34:  // Giant Bat
        case 43:  // Bat
            return irr::video::SColor(255, 60, 40, 40);
        case 35:  // Rat (small)
        case 36:  // Giant Rat / Large Rat
        case 42:  // Rat
            return irr::video::SColor(255, 100, 80, 60);
        case 39:  // Gnoll Pup
        case 44:  // Gnoll
        case 87:  // Gnoll (variant)
            return irr::video::SColor(255, 140, 100, 60);
        case 46:  // Goblin
            return irr::video::SColor(255, 100, 140, 60);
        case 48:  // Spider (small)
        case 49:  // Spider (large)
            return irr::video::SColor(255, 60, 60, 60);
        case 77:  // Werewolf
        case 28:  // Werewolf (variant)
            return irr::video::SColor(255, 120, 100, 80);
        case 85:  // Dragon
            return irr::video::SColor(255, 200, 50, 50);

        // Invisible / Zone entities - fully transparent
        case 127: // Invisible Man
        case 240: // Zone Controller / Marker
            return irr::video::SColor(0, 0, 0, 0);

        default:
            // Generic NPC/monster color - gray
            return irr::video::SColor(255, 150, 150, 150);
    }
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

bool EntityRenderer::registerEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                                     float x, float y, float z, float heading, bool isPlayer,
                                     uint8_t gender, const EntityAppearance& appearance, bool isNPC,
                                     bool isCorpse, float serverSize, uint8_t entityLevel) {
    if (!smgr_ || hasEntity(spawnId)) {
        return false;
    }

    // Skip invisible entities (zone controllers, markers, etc.)
    float scale = getScaleForRace(raceId);
    if (scale <= 0.0f) {
        return true;  // Return true since this is expected behavior
    }

    EntityVisual visual;
    visual.spawnId = spawnId;
    visual.raceId = raceId;
    visual.gender = gender;
    visual.name = name;
    visual.isPlayer = isPlayer;
    visual.isNPC = isNPC;
    visual.isCorpse = isCorpse;
    visual.meshBuilt = false;

    if (isPlayer) {
        LOG_DEBUG(MOD_ENTITY, "Registering PLAYER entity: spawn={} name={} race={}", spawnId, name, raceId);
    }
    visual.lastX = x;
    visual.lastY = y;
    visual.lastZ = z;
    visual.lastHeading = heading;
    // Initialize server position tracking
    visual.serverX = x;
    visual.serverY = y;
    visual.serverZ = z;
    visual.serverHeading = heading;
    visual.timeSinceUpdate = 0;
    visual.appearance = appearance;

    // Store server size in appearance for later use by buildEntityMesh
    // We use the collisionHeight field (0 by default, only set for boats in buildEntityMesh)
    visual.collisionHeight = serverSize;  // Temporarily store serverSize here

    // Add to spatial grid for efficient visibility queries
    updateEntityGridPosition(spawnId, x, y);

    // Create con-colored placeholder cube for immediate spatial awareness
    // Skip for: corpses (npc_type 2,3), invisible entities (already filtered by scale<=0),
    //           zone controllers (race 127, 240)
    bool skipPlaceholder = isCorpse || raceId == 127 || raceId == 240;
    if (!skipPlaceholder && smgr_) {
        // Use con color if both levels are known, otherwise light gray
        irr::video::SColor conColor = (playerLevel_ > 0 && entityLevel > 0)
            ? getConColor(playerLevel_, entityLevel)
            : irr::video::SColor(255, 200, 200, 200);
        float cubeSize = 6.0f * scale;  // ~6 units tall for humanoids
        irr::scene::IMesh* cubeMesh = createPlaceholderMesh(cubeSize, conColor);
        if (cubeMesh) {
            visual.meshNode = smgr_->addMeshSceneNode(cubeMesh);
            cubeMesh->drop();
            if (visual.meshNode) {
                visual.sceneNode = visual.meshNode;
                visual.sceneNode->grab();
                // Position: EQ Z-up to Irrlicht Y-up
                visual.meshNode->setPosition(irr::core::vector3df(x, z, y));
                visual.meshNode->setRotation(irr::core::vector3df(0, -heading, 0));
                visual.usesPlaceholder = true;
                visual.inSceneGraph = true;  // Placeholder is in scene graph — enables SimWorker PVS management and entity prep queueing
                visual.modelYOffset = 0;
                visual.collisionZOffset = (cubeSize / 2.0f);

                // Set material: unlit, no backface cull
                for (irr::u32 i = 0; i < visual.meshNode->getMaterialCount(); ++i) {
                    auto& mat = visual.meshNode->getMaterial(i);
                    mat.Lighting = false;
                    mat.BackfaceCulling = false;
                }
            }
        }
    }

    entities_[spawnId] = visual;

    return true;
}

bool EntityRenderer::buildEntityMesh(uint16_t spawnId) {
    // If entity building is disabled, keep placeholder (zero disk I/O)
    if (constrainedConfig_ && constrainedConfig_->skipEntityBuild) {
        return false;
    }

    auto it = entities_.find(spawnId);
    if (it == entities_.end() || !smgr_) {
        return false;
    }

    EntityVisual& visual = it->second;
    if (visual.meshBuilt) {
        return true;  // Already built
    }

    // Promote any background-preloaded model data before building.
    // The prep worker loads S3D/WLD/animation data on a background thread and stores
    // it in a staging cache. Promotion moves it to loadedModels_ so getMeshForRace()
    // can find it, avoiding a redundant disk load on the main thread.
    if (entityPrepWorker_ && raceModelLoader_) {
        raceModelLoader_->promotePreparedModels();
    }

    // Remove con-colored placeholder cube before building real model
    if (visual.usesPlaceholder && visual.sceneNode) {
        visual.sceneNode->remove();
        visual.sceneNode->drop();
        visual.sceneNode = nullptr;
        visual.meshNode = nullptr;
        visual.usesPlaceholder = false;
    }

    auto createStart = std::chrono::steady_clock::now();

    uint16_t raceId = visual.raceId;
    uint8_t gender = visual.gender;
    const std::string& name = visual.name;
    float x = visual.lastX;
    float y = visual.lastY;
    float z = visual.lastZ;
    float heading = visual.lastHeading;
    bool isPlayer = visual.isPlayer;
    bool isCorpse = visual.isCorpse;
    const EntityAppearance& appearance = visual.appearance;

    float scale = getScaleForRace(raceId);
    if (scale <= 0.0f) {
        visual.meshBuilt = true;
        visual.buildPhase = EntityBuildPhase::Built;
        return true;
    }

    // Recover serverSize from temporary storage in collisionHeight
    float serverSize = visual.collisionHeight;
    visual.collisionHeight = 0;  // Reset to default

    constexpr float REFERENCE_SIZE = 6.0f;
    float sizeMultiplier = (serverSize > 0.0f) ? (serverSize / REFERENCE_SIZE) : 1.0f;
    float baseScale = scale;
    scale *= sizeMultiplier;

    LOG_DEBUG(MOD_GRAPHICS, "buildEntityMesh[{}]: race={} size: {:.2f} * ({:.2f}/6) = {:.2f}",
              name, raceId, baseScale, serverSize, scale);

    // Try to create an animated mesh node first
    EQAnimatedMeshSceneNode* animNode = nullptr;
    if (raceModelLoader_) {
        auto nodeStart = std::chrono::steady_clock::now();

        // Check if any equipment is set
        bool hasEquipment = false;
        for (int i = 0; i < 9; i++) {
            if (appearance.equipment[i] != 0) {
                hasEquipment = true;
                break;
            }
        }

        // P08d: per-entity composite-entity eligibility gate. Composite meshes merge every
        // body part into ONE mesh buffer/material, so per-slot equipment texture overrides
        // and per-slot tints (applied post-build by matching individual body-part texture
        // names on the node's materials — see RaceModelLoader::createAnimatedNodeWithEquipment)
        // silently CANNOT apply to a composite mesh: there is only one material to match
        // against, and its name is the composite sheet's name, not a body-part texture name.
        // Ground truth (P08d review): per-slot equipment[] overrides via WearChange DO render
        // in production today (the textureVariant==0 per-material swap path), so an entity
        // with any per-slot override set must NEVER receive a composite mesh, or its gear
        // would silently vanish. Per Will's 2026-07-25/26 strategy: players are ALWAYS
        // ineligible this unit (even with zero equipment set — per-player runtime baking is
        // deferred); only NPCs with a uniform texture variant and no per-slot data are
        // eligible, and only if a composite sheet actually exists for their
        // (race, gender, textureVariant) — checked at mesh-build time, not here.
        bool hasEquipmentTint = false;
        for (int i = 0; i < 9; i++) {
            if (appearance.equipment_tint[i] != 0) {
                hasEquipmentTint = true;
                break;
            }
        }
        // equipment[] slots 0-6 are body-part textures (Head, Chest, Arms, Wrist, Hands,
        // Legs, Feet — EquipSlot enum order in equipment_textures.h); slots 7-8 are
        // Primary/Secondary weapon models, attached as separate scene nodes, not body
        // textures, so they don't affect composite-mesh eligibility.
        bool hasBodyPartOverride = false;
        for (int i = 0; i <= 6; i++) {
            if (appearance.equipment[i] != 0) {
                hasBodyPartOverride = true;
                break;
            }
        }
        bool compositeEligible = !isPlayer && !hasBodyPartOverride && !hasEquipmentTint;

        // P08d: adoption tally for the per-zone-load INFO summary (see clearEntities()).
        // Only tracked when the feature is actually configured on — counting it
        // unconditionally would be pointless overhead/noise on every hardware tier that
        // never sets enableCompositeEntities.
        if (constrainedConfig_ && constrainedConfig_->enableCompositeEntities) {
            ++compositeTotalCount_;
            if (compositeEligible) {
                ++compositeEligibleCount_;
            }
        }

        // Use full equipment appearance if any equipment is set
        if (hasEquipment || appearance.face != 0 || appearance.texture != 0) {
            animNode = raceModelLoader_->createAnimatedNodeWithEquipment(
                raceId, gender, appearance, smgr_->getRootSceneNode(), -1, compositeEligible);
        } else {
            animNode = raceModelLoader_->createAnimatedNode(
                raceId, gender, smgr_->getRootSceneNode(), -1, compositeEligible);
        }

        auto nodeEnd = std::chrono::steady_clock::now();
        auto nodeMs = std::chrono::duration_cast<std::chrono::milliseconds>(nodeEnd - nodeStart).count();
        if (nodeMs > 50) {
            LOG_WARN(MOD_GRAPHICS, "PERF: createAnimatedNode for race {} took {} ms", raceId, nodeMs);
            EQT::PerformanceMetrics::instance().recordSample("Slow Entity Node", nodeMs);
        }
    }

    if (animNode) {
        // Use animated mesh
        visual.animatedNode = animNode;
        visual.sceneNode = animNode;
        visual.sceneNode->grab();  // Keep alive when removed from scene graph
        visual.inSceneGraph = true;  // Node was created with ROOT parent — mark as in-graph so PVS can manage it
        visual.isAnimated = true;
        visual.usesPlaceholder = false;

        // Mark as player node for vertex-baked rotation (must happen before first render)
        if (isPlayer) {
            animNode->setIsPlayerNode(true);
        }

        // Force animation update to get correct bounding box
        // The initial bounding box is from bind/T-pose, but we need the animated pose
        animNode->forceAnimationUpdate();

        // Calculate model offset for collision calculations
        // Server Z is the geometric MODEL CENTER, not the feet/ground position
        // modelYOffset is for rendering adjustment (set to 0 since server Z works directly)
        // collisionZOffset is the distance from model center to feet for collision detection
        irr::core::aabbox3df bbox = animNode->getBoundingBox();
        float bboxHeight = bbox.MaxEdge.Y - bbox.MinEdge.Y;
        // modelYOffset = 0 for rendering (server Z position works as-is)
        visual.modelYOffset = 0;
        // collisionZOffset = half model height for collision (server Z is center, feet are below)
        visual.collisionZOffset = (bboxHeight / 2.0f) * scale;

        // Debug: log bounding box info for entity creation
        LOG_DEBUG(MOD_GRAPHICS, "createEntity[{}]: race={} bbox Y=[{:.2f},{:.2f}] height={:.2f} scale={:.3f} collisionZOffset={:.2f}",
			name, raceId, bbox.MinEdge.Y, bbox.MaxEdge.Y, bboxHeight, scale, visual.collisionZOffset);

	// Set position (convert EQ Z-up to Irrlicht Y-up)
	// Server Z is the model center, add modelYOffset to position model correctly
	animNode->setPosition(irr::core::vector3df(x, z + visual.modelYOffset, y));

	// Set rotation (EQ heading to Irrlicht Y rotation)
	// Heading arrives already in degrees (0-360) from eq.cpp 11-bit conversion
	// EQ heading: 0=North(+Y), increases clockwise
	// Irrlicht Y rotation: 0=+Z, increases counter-clockwise
	// Must negate to convert between coordinate systems
	float visualHeading = -heading;
	animNode->setRotation(irr::core::vector3df(0, visualHeading, 0));
	if (isPlayer) {
		LOG_DEBUG(MOD_GRAPHICS, "[ROT-CREATE] createEntity PLAYER: spawnId={} heading={:.1f} visualHeading={:.1f} node={}",
				spawnId, heading, visualHeading, (void*)animNode);
	}

	// Scale is already set in createAnimatedNode, but apply adjustments
	animNode->setScale(irr::core::vector3df(scale, scale, scale));

	// Set material properties
	for (irr::u32 i = 0; i < animNode->getMaterialCount(); ++i) {
		auto& mat = animNode->getMaterial(i);
		mat.BackfaceCulling = false;
		// Apply GLSL shader material if available, otherwise use fixed-function lighting
		if (shaderMaterialSolid_ >= 0) {
			if (mat.MaterialType == irr::video::EMT_SOLID) {
				mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
			} else if (mat.MaterialType == irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF) {
				applyAlphaTestMaterialType(mat);
			}
			mat.Lighting = false;  // Shader handles lighting
		} else {
			mat.Lighting = lightingEnabled_;
			if (lightingEnabled_) {
				mat.NormalizeNormals = true;
				mat.AmbientColor = irr::video::SColor(255, 255, 255, 255);
				mat.DiffuseColor = irr::video::SColor(255, 255, 255, 255);
			}
		}
	}

	// Apply global animation speed
	animNode->setAnimationSpeed(10.0f * globalAnimationSpeed_);

	// Highlight player entity
	if (isPlayer) {
		for (irr::u32 i = 0; i < animNode->getMaterialCount(); ++i) {
			animNode->getMaterial(i).EmissiveColor = irr::video::SColor(255, 50, 50, 100);
		}
	}

	// Create name tag (text node above entity)
	if (nameTagsVisible_ && !name.empty()) {
		std::wstring wname = EQT::toDisplayNameW(name);
		// Position name just above the model's head (humanoid models are ~6 units tall)
		// Use 5.5f to place text just above the head
		float nameHeight = 5.5f * scale;

		visual.nameNode = smgr_->addTextSceneNode(
				smgr_->getGUIEnvironment()->getBuiltInFont(),
				wname.c_str(),
				isPlayer ? irr::video::SColor(255, 100, 200, 255) : irr::video::SColor(255, 255, 255, 255),
				animNode,
				irr::core::vector3df(0, nameHeight, 0)
				);
	}

	// Track race model reference for cache eviction
	if (raceModelLoader_) {
		raceModelLoader_->addMeshRef(raceId, gender);
	}

	// Attach equipment models to bone attachment points
	attachEquipment(visual);

	// If this is a corpse, set the death animation and hold on last frame
	if (isCorpse) {
		// Try death animations: d05 is the actual death/dying animation
		// d01/d02 are damage animations, not death
		bool hasD05 = animNode->hasAnimation("d05");
		bool hasD01 = animNode->hasAnimation("d01");
		bool hasD02 = animNode->hasAnimation("d02");

		std::string deathAnim;
		if (hasD05) {
			deathAnim = "d05";
		} else if (hasD02) {
			deathAnim = "d02";  // Fallback to heavy damage
		} else if (hasD01) {
			deathAnim = "d01";  // Fallback to minor damage
		}

		if (!deathAnim.empty()) {
			animNode->playAnimation(deathAnim, false, false);  // Don't loop, not playThrough

			// Jump to the last frame to show the "lying down" pose
			SkeletalAnimator& animator = animNode->getAnimator();
			animator.setToLastFrame();

			// Force immediate mesh update so corpse appears lying down
			animNode->forceAnimationUpdate();

			// Mark corpse position as adjusted (no Z offset needed)
			visual.corpsePositionAdjusted = true;
			visual.corpseYOffset = 0.0f;

			visual.currentAnimation = deathAnim;
		}
	}

	// Queued state (serverAnimation, position) will be applied by the next
	// updateInterpolation() call now that meshBuilt=true.  No explicit
	// replay needed — updateInterpolation picks up serverAnimation/velocities.
	if (visual.lightLevel != 0) {
		setEntityLight(spawnId, visual.lightLevel);
	}

	visual.meshBuilt = true;
	visual.buildPhase = EntityBuildPhase::Built;

	// Log total entity creation time if slow
	auto createEnd = std::chrono::steady_clock::now();
	auto createMs = std::chrono::duration_cast<std::chrono::milliseconds>(createEnd - createStart).count();
	if (createMs > 50) {
		LOG_WARN(MOD_GRAPHICS, "PERF: buildEntityMesh (animated) for {} race {} took {} ms", name, raceId, createMs);
		EQT::PerformanceMetrics::instance().recordSample("Slow Entity Create", createMs);
	}
	return true;
    }

    // Fall back to static mesh
    irr::scene::IMesh* mesh = getMeshForRace(raceId, gender, appearance);
    if (!mesh) {
	    return false;
    }

    // Check if this is a placeholder mesh
    bool usesPlaceholder = (placeholderMeshCache_.find(raceId) != placeholderMeshCache_.end() &&
		    placeholderMeshCache_[raceId] == mesh);

    visual.usesPlaceholder = usesPlaceholder;
    visual.isAnimated = false;

    // Create mesh node
    visual.meshNode = smgr_->addMeshSceneNode(mesh);
    if (!visual.meshNode) {
	    return false;
    }
    visual.sceneNode = visual.meshNode;
    visual.sceneNode->grab();  // Keep alive when removed from scene graph
    visual.inSceneGraph = true;  // Node was created with ROOT parent — mark as in-graph so PVS can manage it

    // Calculate model offset for collision calculations
    // Server Z is the geometric MODEL CENTER, not the feet/ground position
    irr::core::aabbox3df bbox = mesh->getBoundingBox();
    float bboxHeight = bbox.MaxEdge.Y - bbox.MinEdge.Y;
    // modelYOffset = 0 for rendering (server Z position works as-is)
    visual.modelYOffset = 0;
    // collisionZOffset = half model height for collision (server Z is center, feet are below)
    visual.collisionZOffset = (bboxHeight / 2.0f) * scale;

    // Set position (convert EQ Z-up to Irrlicht Y-up)
    // Server Z is model center, add modelYOffset for correct positioning
    visual.meshNode->setPosition(irr::core::vector3df(x, z + visual.modelYOffset, y));

    // Set rotation (EQ heading to Irrlicht Y rotation)
    // Heading arrives already in degrees (0-360) from eq.cpp 11-bit conversion
    // EQ heading: 0=North(+Y), increases clockwise
    // Irrlicht Y rotation: 0=+Z, increases counter-clockwise
    // Must negate to convert between coordinate systems
    float visualHeading = -heading;
    visual.meshNode->setRotation(irr::core::vector3df(0, visualHeading, 0));

    // Apply scale based on race (scale was already calculated above)
    visual.meshNode->setScale(irr::core::vector3df(scale, scale, scale));

    // Set material properties
    for (irr::u32 i = 0; i < visual.meshNode->getMaterialCount(); ++i) {
        auto& mat = visual.meshNode->getMaterial(i);
        mat.BackfaceCulling = false;
        // Apply GLSL shader material if available, otherwise use fixed-function lighting
        if (shaderMaterialSolid_ >= 0) {
            // Preserve alpha-test materials, replace only solid ones
            if (mat.MaterialType == irr::video::EMT_SOLID) {
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
            } else if (mat.MaterialType == irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF) {
                applyAlphaTestMaterialType(mat);
            }
            mat.Lighting = false;  // Shader handles lighting
        } else {
            mat.Lighting = lightingEnabled_;
            if (lightingEnabled_) {
                mat.NormalizeNormals = true;
                mat.AmbientColor = irr::video::SColor(255, 255, 255, 255);
                mat.DiffuseColor = irr::video::SColor(255, 255, 255, 255);
            }
        }
    }

    // Highlight player entity
    if (isPlayer) {
        for (irr::u32 i = 0; i < visual.meshNode->getMaterialCount(); ++i) {
            visual.meshNode->getMaterial(i).EmissiveColor = irr::video::SColor(255, 50, 50, 100);
        }
    }

    // Create name tag (text node above entity)
    if (nameTagsVisible_ && !name.empty()) {
        std::wstring wname = EQT::toDisplayNameW(name);

        // Position name just above the model's head
        // Placeholders are 10 units tall, actual models are ~6 units
        // Use 5.5f for actual models, 10.5f for placeholders
        float nameHeight = usesPlaceholder ? 10.5f : 5.5f;
        nameHeight *= scale;

        visual.nameNode = smgr_->addTextSceneNode(
            smgr_->getGUIEnvironment()->getBuiltInFont(),
            wname.c_str(),
            isPlayer ? irr::video::SColor(255, 100, 200, 255) : irr::video::SColor(255, 255, 255, 255),
            visual.meshNode,
            irr::core::vector3df(0, nameHeight, 0)
        );
    }

    // Set up collision data for boats (race 72 = Ship, race 73 = Launch/Barrel Barge)
    if (raceId == 72 || raceId == 73) {
        visual.hasCollision = true;
        // Use bounding box to determine collision radius and deck height
        irr::core::aabbox3df collBbox = mesh->getBoundingBox();
        float bboxWidth = std::max(collBbox.MaxEdge.X - collBbox.MinEdge.X,
                                   collBbox.MaxEdge.Z - collBbox.MinEdge.Z);
        visual.collisionRadius = (bboxWidth / 2.0f) * scale;
        visual.collisionHeight = (collBbox.MaxEdge.Y - collBbox.MinEdge.Y) * scale;
        // Deck is at the top of the bounding box (server Z is center, so add half height)
        visual.deckZ = z + (visual.collisionHeight / 2.0f);
        registerBoat(spawnId);
        LOG_DEBUG(MOD_GRAPHICS, "Boat collision: race {} radius={:.1f} height={:.1f} deckZ={:.1f}",
            raceId, visual.collisionRadius, visual.collisionHeight, visual.deckZ);
    }

    // Track race model reference for cache eviction
    if (raceModelLoader_) {
        raceModelLoader_->addMeshRef(raceId, gender);
    }

    // Apply any queued state that arrived while mesh was not yet built
    if (visual.lightLevel != 0) {
        setEntityLight(spawnId, visual.lightLevel);
    }

    visual.meshBuilt = true;
    visual.buildPhase = EntityBuildPhase::Built;

    // Log entity creation with model status
    if (!usesPlaceholder) {
        LOG_DEBUG(MOD_ENTITY, "Built entity {} ({}) with race {} 3D model (static)", spawnId, name, raceId);
    } else {
        LOG_DEBUG(MOD_ENTITY, "Built entity {} ({}) with race {} placeholder (no model found)", spawnId, name, raceId);
    }

    // Log total entity creation time if slow
    auto createEnd = std::chrono::steady_clock::now();
    auto createMs = std::chrono::duration_cast<std::chrono::milliseconds>(createEnd - createStart).count();
    if (createMs > 50) {
        LOG_WARN(MOD_GRAPHICS, "PERF: buildEntityMesh (static) for {} race {} took {} ms", name, raceId, createMs);
        EQT::PerformanceMetrics::instance().recordSample("Slow Entity Create", createMs);
    }

    return true;
}

bool EntityRenderer::pollAndDistributePrepResults() {
    if (!entityPrepWorker_) return false;
    bool distributed = false;
    EntityPrepWorker::PrepResult result;
    while (entityPrepWorker_->pollResult(result)) {
        auto it = entities_.find(result.spawnId);
        if (it == entities_.end()) continue;
        auto& vis = it->second;

        // T10: Variant/equipment textures are already decoded in constrained cache
        // via getOrLoad() (called by prep worker on background thread).
        // GPU upload happens one-per-frame in processOneEntityBuildStep (T11).
        // Just move staging data here.
        for (auto& eq : result.equipmentData) {
            EntityVisual::EquipmentStaging staging;
            staging.modelId = eq.modelId;
            staging.equipmentId = eq.equipmentId;
            staging.isPrimary = eq.isPrimary;
            staging.geometry = eq.geometry;
            staging.rawTextures = eq.rawTextures;
            vis.equipmentStaging.push_back(std::move(staging));
        }

        vis.entityPrepComplete = true;
        distributed = true;
        LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) prep complete: {} equip pieces",
                  result.spawnId, vis.name, vis.equipmentStaging.size());
    }
    return distributed;
}

ZoneMeshBuilder* EntityRenderer::getMeshBuilder() const {
    return raceModelLoader_ ? raceModelLoader_->getMeshBuilder() : nullptr;
}

void EntityRenderer::setCompositeEntityPath(const std::string& path) {
    if (raceModelLoader_) {
        raceModelLoader_->setCompositeEntityPath(path);
        LOG_INFO(MOD_GRAPHICS, "EntityRenderer: composite entity path set to '{}'", path);
    }
}

void EntityRenderer::setConstrainedTextureCache(ConstrainedTextureCache* cache) {
    constrainedTextureCache_ = cache;
    if (equipmentModelLoader_)
        equipmentModelLoader_->setConstrainedTextureCache(cache);
    if (raceModelLoader_) {
        raceModelLoader_->setConstrainedTextureCache(cache);
        if (raceModelLoader_->getMeshBuilder())
            raceModelLoader_->getMeshBuilder()->setConstrainedTextureCache(cache);
    }
}

bool EntityRenderer::processOneEntityBuildStep(size_t pvsRegion) {
    if (!smgr_ || !raceModelLoader_) return false;

    // Find nearest unbuilt entity that's ready for work
    float playerX = 0, playerY = 0;
    if (playerSpawnId_ != 0) {
        auto playerIt = entities_.find(playerSpawnId_);
        if (playerIt != entities_.end()) {
            playerX = playerIt->second.lastX;
            playerY = playerIt->second.lastY;
        }
    }

    // Collect entities in active phases first (in-progress), then Placeholder
    uint16_t bestId = 0;
    float bestDistSq = std::numeric_limits<float>::max();
    EntityBuildPhase bestPhase = EntityBuildPhase::Built;

    for (auto& [spawnId, vis] : entities_) {
        // Use buildPhase instead of meshBuilt — entity may have mesh built but still
        // need equipment texture upload + attach in later phases
        if (vis.buildPhase == EntityBuildPhase::Built) continue;

        // In-progress = any phase past Placeholder
        bool inProgress = (vis.buildPhase != EntityBuildPhase::Placeholder);

        // Only build entities that are currently visible (in scene graph).
        // Culled entities (distance/frustum/portal/occlusion) stay as placeholders.
        // Exception: entities already in-progress — finish them to avoid wasted partial work.
        if (!inProgress && !vis.inSceneGraph) continue;

        // PVS region gate: only build entities near the player's region.
        // Before PVS is active (pvsRegion == SIZE_MAX), skip all non-in-progress entities
        // — entity loading must wait until BSP/PVS culling has run.
        // Entities with entityPrepComplete (already PVS-depth-gated by prep system)
        // are eligible regardless of exact BSP region. Without prep, require same-region.
        if (!inProgress && spawnId != playerSpawnId_) {
            if (pvsRegion == SIZE_MAX) continue;
            if (!vis.entityPrepComplete && vis.cachedBspRegion != pvsRegion) continue;
        }

        float dx = vis.lastX - playerX;
        float dy = vis.lastY - playerY;
        float distSq = dx * dx + dy * dy;

        // Prefer in-progress over placeholder, then closer over farther
        if (inProgress && bestPhase == EntityBuildPhase::Placeholder) {
            bestId = spawnId;
            bestDistSq = distSq;
            bestPhase = vis.buildPhase;
        } else if (inProgress == (bestPhase != EntityBuildPhase::Placeholder)) {
            if (distSq < bestDistSq) {
                bestId = spawnId;
                bestDistSq = distSq;
                bestPhase = vis.buildPhase;
            }
        } else if (!inProgress && bestPhase == EntityBuildPhase::Built) {
            bestId = spawnId;
            bestDistSq = distSq;
            bestPhase = vis.buildPhase;
        }
    }

    // Always prioritize the player entity over distance-based selection
    if (playerSpawnId_ != 0) {
        auto playerIt = entities_.find(playerSpawnId_);
        if (playerIt != entities_.end() &&
            playerIt->second.buildPhase != EntityBuildPhase::Built) {
            bestId = playerSpawnId_;
        }
    }

    if (bestId == 0) return false;

    auto& vis = entities_[bestId];
    auto entryPhase = vis.buildPhase;
    auto stepStart = std::chrono::steady_clock::now();

    static auto phaseName = [](EntityBuildPhase p) -> const char* {
        switch (p) {
            case EntityBuildPhase::Placeholder: return "Placeholder";
            case EntityBuildPhase::TextureUploading: return "TextureUpload";
            case EntityBuildPhase::VariantTextureUploading: return "VariantTexUpload";
            case EntityBuildPhase::SceneNodeCreation: return "SceneNodeCreate";
            case EntityBuildPhase::NodeSetup: return "NodeSetup";
            case EntityBuildPhase::MeshFinalize: return "MeshFinalize";
            case EntityBuildPhase::EquipTextureUploading: return "EquipTexUpload";
            case EntityBuildPhase::EquipmentAttach: return "EquipAttach";
            case EntityBuildPhase::Built: return "Built";
        }
        return "Unknown";
    };

    // RAII logger — fires when processOneEntityBuildStep returns
    struct PhaseTimer {
        uint16_t id; const char* name; EntityBuildPhase entry; EntityBuildPhase* current;
        std::chrono::steady_clock::time_point start;
        ~PhaseTimer() {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            LOG_INFO(MOD_GRAPHICS, "EntityBuild: {} ({}) {} -> {} in {}us",
                     id, name, phaseName(entry), phaseName(*current), us);
        }
    } phaseTimer{bestId, vis.name.c_str(), entryPhase, &vis.buildPhase, stepStart};

    for (;;) { // Loop to fall through trivial phase transitions without wasting frames
    switch (vis.buildPhase) {
        case EntityBuildPhase::Placeholder: {
            // Wait for background prep to complete (both race model AND per-entity data)
            if (!vis.entityPrepComplete) {
                // Check if at least the race model data is cached (for entities that
                // were registered before the extended prep system was active)
                uint32_t cacheKey = (static_cast<uint32_t>(vis.raceId) << 8) | vis.gender;
                if (!raceModelLoader_->isModelDataCached(cacheKey)) {
                    return false;  // Background thread still working
                }
                // Race model is cached but entityPrepComplete not set — this entity
                // was queued with old-style prep. Mark it complete so we can proceed.
                vis.entityPrepComplete = true;
            }

            // Promote prepared data to main cache
            raceModelLoader_->promotePreparedModels();

            auto modelData = raceModelLoader_->getRaceModelData(vis.raceId, vis.gender);
            if (!modelData) {
                // No model data — fallback to synchronous placeholder build
                buildEntityMesh(bestId);
                return true;
            }

            // T13: Textures already in constrained cache via getOrLoad() (T05/T06/T07).
            // Skip texture decode/upload phases — go straight to scene node creation.
            vis.buildPhase = EntityBuildPhase::SceneNodeCreation;
            continue;
        }

        case EntityBuildPhase::TextureUploading: {
            // T13: Phase skipped — textures in constrained cache.
            vis.buildPhase = EntityBuildPhase::SceneNodeCreation;
            continue;
        }

        case EntityBuildPhase::VariantTextureUploading: {
            // T11: Variant textures already in constrained cache via getOrLoad() (T06).
            // Skip directly to scene node creation.
            vis.buildPhase = EntityBuildPhase::SceneNodeCreation;
            continue;
        }

        case EntityBuildPhase::SceneNodeCreation: {
            // Frame 1/3: Create node + set position/rotation/scale.
            // This is the heaviest frame — mesh creation (~10-15ms).
            // Materials and name tags are deferred to NodeSetup and MeshFinalize.

            // T11: Upload any decoded textures to GPU before mesh building.
            // During gameplay, only this entity's textures should be pending.
            if (constrainedTextureCache_) {
                constrainedTextureCache_->uploadAllPending();
            }

            // Promote any background-preloaded model data
            if (entityPrepWorker_ && raceModelLoader_) {
                raceModelLoader_->promotePreparedModels();
            }

            // Remove con-colored placeholder cube
            if (vis.usesPlaceholder && vis.sceneNode) {
                vis.sceneNode->remove();
                vis.sceneNode->drop();
                vis.sceneNode = nullptr;
                vis.meshNode = nullptr;
                vis.usesPlaceholder = false;
            }

            uint16_t raceId = vis.raceId;
            uint8_t gender = vis.gender;

            float scale = getScaleForRace(raceId);
            if (scale <= 0.0f) {
                vis.meshBuilt = true;
                vis.buildPhase = EntityBuildPhase::Built;
                return true;
            }

            // Recover serverSize from temporary storage in collisionHeight
            float serverSize = vis.collisionHeight;
            vis.collisionHeight = 0;

            constexpr float REFERENCE_SIZE = 6.0f;
            float sizeMultiplier = (serverSize > 0.0f) ? (serverSize / REFERENCE_SIZE) : 1.0f;
            scale *= sizeMultiplier;

            // Temporarily clear weapon equipment IDs so createAnimatedNodeWithEquipment
            // doesn't bake weapon geometry — weapons are attached in EquipmentAttach phase
            uint32_t savedPrimary = vis.appearance.equipment[7];
            uint32_t savedSecondary = vis.appearance.equipment[8];
            vis.appearance.equipment[7] = 0;
            vis.appearance.equipment[8] = 0;

            // Try animated mesh first
            EQAnimatedMeshSceneNode* animNode = nullptr;
            if (raceModelLoader_) {
                bool hasEquipment = false;
                for (int i = 0; i < 9; i++) {
                    if (vis.appearance.equipment[i] != 0) {
                        hasEquipment = true;
                        break;
                    }
                }
                if (hasEquipment || vis.appearance.face != 0 || vis.appearance.texture != 0) {
                    animNode = raceModelLoader_->createAnimatedNodeWithEquipment(
                        raceId, gender, vis.appearance, smgr_->getRootSceneNode());
                } else {
                    animNode = raceModelLoader_->createAnimatedNode(raceId, gender, smgr_->getRootSceneNode());
                }
            }

            // Restore weapon equipment IDs for EquipmentAttach phase
            vis.appearance.equipment[7] = savedPrimary;
            vis.appearance.equipment[8] = savedSecondary;

            if (animNode) {
                vis.animatedNode = animNode;
                vis.sceneNode = animNode;
                vis.sceneNode->grab();
                vis.inSceneGraph = true;
                vis.isAnimated = true;
                vis.usesPlaceholder = false;

                if (vis.isPlayer) {
                    animNode->setIsPlayerNode(true);
                }
                animNode->forceAnimationUpdate();

                // Collision offset
                irr::core::aabbox3df bbox = animNode->getBoundingBox();
                float bboxHeight = bbox.MaxEdge.Y - bbox.MinEdge.Y;
                vis.modelYOffset = 0;
                vis.collisionZOffset = (bboxHeight / 2.0f) * scale;

                // Position, rotation, scale
                animNode->setPosition(irr::core::vector3df(vis.lastX, vis.lastZ + vis.modelYOffset, vis.lastY));
                float visualHeading = -vis.lastHeading;
                animNode->setRotation(irr::core::vector3df(0, visualHeading, 0));
                animNode->setScale(irr::core::vector3df(scale, scale, scale));
            } else {
                // Fall back to static mesh
                irr::scene::IMesh* mesh = getMeshForRace(raceId, gender, vis.appearance);
                if (!mesh) {
                    // No model at all — fall back to synchronous buildEntityMesh
                    buildEntityMesh(bestId);
                    return true;
                }

                bool usesPlaceholder = (placeholderMeshCache_.find(raceId) != placeholderMeshCache_.end() &&
                    placeholderMeshCache_[raceId] == mesh);
                vis.usesPlaceholder = usesPlaceholder;
                vis.isAnimated = false;

                vis.meshNode = smgr_->addMeshSceneNode(mesh);
                if (!vis.meshNode) {
                    return false;
                }
                vis.sceneNode = vis.meshNode;
                vis.sceneNode->grab();
                vis.inSceneGraph = true;

                // Collision offset
                irr::core::aabbox3df bbox = mesh->getBoundingBox();
                float bboxHeight = bbox.MaxEdge.Y - bbox.MinEdge.Y;
                vis.modelYOffset = 0;
                vis.collisionZOffset = (bboxHeight / 2.0f) * scale;

                // Position, rotation, scale
                vis.meshNode->setPosition(irr::core::vector3df(vis.lastX, vis.lastZ + vis.modelYOffset, vis.lastY));
                float visualHeading = -vis.lastHeading;
                vis.meshNode->setRotation(irr::core::vector3df(0, visualHeading, 0));
                vis.meshNode->setScale(irr::core::vector3df(scale, scale, scale));
            }

            vis.buildPhase = EntityBuildPhase::NodeSetup;
            return true;
        }

        case EntityBuildPhase::NodeSetup: {
            // Frame 2/3: Set materials, animation speed, player highlight.
            // Node was created in SceneNodeCreation — now configure rendering properties.

            irr::scene::ISceneNode* node = vis.sceneNode;
            if (!node) {
                // Shouldn't happen — node was created in Frame 1
                vis.buildPhase = EntityBuildPhase::MeshFinalize;
                continue;  // trivial transition — fall through
            }

            // Set material properties
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                auto& mat = node->getMaterial(i);
                mat.BackfaceCulling = false;
                if (shaderMaterialSolid_ >= 0) {
                    if (mat.MaterialType == irr::video::EMT_SOLID) {
                        mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                    } else if (mat.MaterialType == irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF) {
                        applyAlphaTestMaterialType(mat);
                    }
                    mat.Lighting = false;
                } else {
                    mat.Lighting = lightingEnabled_;
                    if (lightingEnabled_) {
                        mat.NormalizeNormals = true;
                        mat.AmbientColor = irr::video::SColor(255, 255, 255, 255);
                        mat.DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                    }
                }
            }

            // Apply animation speed for animated nodes
            if (vis.animatedNode) {
                vis.animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed_);
            }

            // Player highlight
            if (vis.isPlayer) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).EmissiveColor = irr::video::SColor(255, 50, 50, 100);
                }
            }

            vis.buildPhase = EntityBuildPhase::MeshFinalize;
            return true;
        }

        case EntityBuildPhase::MeshFinalize: {
            // Frame 3/3: Name tag, corpse animation, light level, race ref tracking,
            // and decide whether equipment phases are needed.

            // Read scale back from the node (set in SceneNodeCreation)
            float scale = vis.sceneNode ? vis.sceneNode->getScale().X : 1.0f;

            // Create name tag
            if (nameTagsVisible_ && !vis.name.empty() && vis.sceneNode) {
                std::wstring wname = EQT::toDisplayNameW(vis.name);
                float nameHeight;
                if (vis.isAnimated) {
                    nameHeight = 5.5f * scale;
                } else {
                    nameHeight = vis.usesPlaceholder ? 10.5f : 5.5f;
                    nameHeight *= scale;
                }
                irr::video::SColor nameColor = vis.isPlayer
                    ? irr::video::SColor(255, 100, 200, 255)
                    : irr::video::SColor(255, 255, 255, 255);
                vis.nameNode = smgr_->addTextSceneNode(
                    smgr_->getGUIEnvironment()->getBuiltInFont(),
                    wname.c_str(), nameColor, vis.sceneNode,
                    irr::core::vector3df(0, nameHeight, 0));
            }

            // Boat collision (static mesh only, race 72=Ship, 73=Launch)
            if (!vis.isAnimated && vis.meshNode && (vis.raceId == 72 || vis.raceId == 73)) {
                vis.hasCollision = true;
                irr::core::aabbox3df collBbox = vis.meshNode->getMesh()->getBoundingBox();
                float bboxWidth = std::max(collBbox.MaxEdge.X - collBbox.MinEdge.X,
                                           collBbox.MaxEdge.Z - collBbox.MinEdge.Z);
                vis.collisionRadius = (bboxWidth / 2.0f) * scale;
                vis.collisionHeight = (collBbox.MaxEdge.Y - collBbox.MinEdge.Y) * scale;
                vis.deckZ = vis.lastZ + (vis.collisionHeight / 2.0f);
                registerBoat(bestId);
            }

            // Track race model reference for cache eviction
            if (raceModelLoader_) {
                raceModelLoader_->addMeshRef(vis.raceId, vis.gender);
            }

            // Corpse death animation (animated only)
            if (vis.isCorpse && vis.animatedNode) {
                bool hasD05 = vis.animatedNode->hasAnimation("d05");
                bool hasD01 = vis.animatedNode->hasAnimation("d01");
                bool hasD02 = vis.animatedNode->hasAnimation("d02");

                std::string deathAnim;
                if (hasD05) deathAnim = "d05";
                else if (hasD02) deathAnim = "d02";
                else if (hasD01) deathAnim = "d01";

                if (!deathAnim.empty()) {
                    vis.animatedNode->playAnimation(deathAnim, false, false);
                    SkeletalAnimator& animator = vis.animatedNode->getAnimator();
                    animator.setToLastFrame();
                    vis.animatedNode->forceAnimationUpdate();
                    vis.corpsePositionAdjusted = true;
                    vis.corpseYOffset = 0.0f;
                    vis.currentAnimation = deathAnim;
                }
            }

            // Apply queued light level
            if (vis.lightLevel != 0) {
                setEntityLight(bestId, vis.lightLevel);
            }

            vis.meshBuilt = true;

            // T13: equipmentTextures removed. Equipment textures in constrained cache.
            // Decide next phase based on equipment staging data.
            if (!vis.equipmentStaging.empty()) {
                vis.buildPhase = EntityBuildPhase::EquipmentAttach;
            } else if (vis.appearance.equipment[7] != 0 || vis.appearance.equipment[8] != 0) {
                vis.buildPhase = EntityBuildPhase::EquipmentAttach;
            } else {
                vis.buildPhase = EntityBuildPhase::Built;
            }
            return true;
        }

        case EntityBuildPhase::EquipTextureUploading: {
            // T11: Equipment textures already in constrained cache via getOrLoad() (T07).
            // Skip directly to equipment attach.
            vis.buildPhase = EntityBuildPhase::EquipmentAttach;
            continue;
        }

        case EntityBuildPhase::EquipmentAttach: {
            // Cache pre-extracted equipment model data so getEquipmentMeshByModelId finds it
            if (equipmentModelLoader_) {
                for (const auto& staging : vis.equipmentStaging) {
                    if (staging.geometry) {
                        auto equipData = std::make_shared<EquipmentModelData>();
                        equipData->modelId = staging.modelId;
                        equipData->modelName = "IT" + std::to_string(staging.modelId);
                        equipData->geometry = staging.geometry;
                        equipData->textures = staging.rawTextures;
                        for (const auto& texName : staging.geometry->textureNames()) {
                            equipData->textureNames.push_back(texName);
                        }
                        equipmentModelLoader_->cacheEquipmentModelData(staging.modelId, equipData);
                    }
                }
            }

            // Now attach equipment using the normal path (which will find cached data)
            attachEquipment(vis);

            // Apply corpse death animation to equipment nodes if needed
            if (vis.isCorpse && vis.animatedNode) {
                // Equipment nodes are children and inherit parent transform
            }

            // Apply light level if set
            if (vis.lightLevel != 0) {
                setEntityLight(bestId, vis.lightLevel);
            }

            // Clean up staging data (free memory)
            vis.equipmentStaging.clear();
            vis.equipmentStaging.shrink_to_fit();

            vis.buildPhase = EntityBuildPhase::Built;
            LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) equipment attached, build complete", bestId, vis.name);
            return true;
        }

        case EntityBuildPhase::Built:
            return false;
    }

    return false;
    } // end for(;;) fall-through loop
}

bool EntityRenderer::createEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                                   float x, float y, float z, float heading, bool isPlayer,
                                   uint8_t gender, const EntityAppearance& appearance, bool isNPC,
                                   bool isCorpse, float serverSize) {
    if (!registerEntity(spawnId, raceId, name, x, y, z, heading, isPlayer,
                        gender, appearance, isNPC, isCorpse, serverSize)) {
        return false;
    }
    return buildEntityMesh(spawnId);
}

bool EntityRenderer::isEntityMeshBuilt(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    return it != entities_.end() && it->second.meshBuilt;
}

void EntityRenderer::getUnbuiltEntities(std::vector<uint16_t>& out) const {
    out.clear();
    for (const auto& [id, visual] : entities_) {
        if (!visual.meshBuilt) {
            out.push_back(id);
        }
    }
}

void EntityRenderer::updateEntity(uint16_t spawnId, float x, float y, float z, float heading,
                                   float dx, float dy, float dz, int32_t animation) {
    // Queue the update for batched processing
    // Using map automatically deduplicates - only keeps the latest update per entity
    pendingUpdates_[spawnId] = {spawnId, x, y, z, heading, dx, dy, dz, animation};
}

void EntityRenderer::flushPendingUpdates() {
    if (pendingUpdates_.empty()) {
        return;
    }

    // Debug: Check if target entity has an update
    if (debugTargetId_ != 0 && pendingUpdates_.count(debugTargetId_) > 0) {
        LOG_TRACE(MOD_ENTITY, "TARGET ANIM BATCH: processing update for target");
    }

    // Process all pending updates (one per entity due to deduplication)
    for (const auto& [spawnId, update] : pendingUpdates_) {
        processUpdate(update);
    }

    pendingUpdates_.clear();
}

std::vector<DrainedEntityUpdate> EntityRenderer::drainPendingUpdates() {
    std::vector<DrainedEntityUpdate> result;
    if (pendingUpdates_.empty()) return result;

    result.reserve(pendingUpdates_.size());
    for (const auto& [spawnId, pu] : pendingUpdates_) {
        DrainedEntityUpdate epu;
        epu.spawnId = pu.spawnId;
        epu.x = pu.x;
        epu.y = pu.y;
        epu.z = pu.z;
        epu.heading = pu.heading;
        epu.dx = pu.dx;
        epu.dy = pu.dy;
        epu.dz = pu.dz;
        epu.animation = pu.animation;
        result.push_back(epu);
    }
    // Don't clear yet — flushPendingUpdatesForAnimations will process animations
    return result;
}

void EntityRenderer::flushPendingUpdatesForAnimations() {
    if (pendingUpdates_.empty()) return;

    // Process only animation and spatial-grid side effects.
    // Position/velocity math is handled by the worker.
    for (const auto& [spawnId, update] : pendingUpdates_) {
        processUpdate(update);
    }

    pendingUpdates_.clear();
}

void EntityRenderer::updateMainThreadEntityState(float deltaTime) {
    // Handle things that must stay on main thread:
    // - Corpse fading (material mutations)
    // - Corpse animation timing
    // - Equipment transforms for visible entities
    // - Idle animation transitions
    // - Fully-faded corpse removal

    std::vector<uint16_t> toRemove;
    std::vector<uint16_t> toDeactivate;

    for (uint16_t spawnId : activeEntities_) {
        auto it = entities_.find(spawnId);
        if (it == entities_.end()) continue;
        EntityVisual& visual = it->second;
        if (!visual.sceneNode || !visual.inSceneGraph) continue;

        // Player equipment transforms
        if (visual.isPlayer) {
            updateEquipmentTransforms(visual);
            continue;
        }

        // Handle fading entities (corpses decaying)
        if (visual.isFading) {
            visual.fadeTimer += deltaTime;
            visual.fadeAlpha = 1.0f - (visual.fadeTimer / EntityVisual::FADE_DURATION);
            if (visual.fadeAlpha <= 0.0f) {
                visual.fadeAlpha = 0.0f;
            }

            irr::u8 colorVal = static_cast<irr::u8>(visual.fadeAlpha * 255);
            irr::video::SColor fadeColor(255, colorVal, colorVal, colorVal);

            if (visual.animatedNode) {
                for (irr::u32 i = 0; i < visual.animatedNode->getMaterialCount(); ++i) {
                    irr::video::SMaterial& mat = visual.animatedNode->getMaterial(i);
                    mat.MaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;
                    mat.AmbientColor = fadeColor;
                    mat.DiffuseColor = fadeColor;
                    mat.EmissiveColor = irr::video::SColor(255, colorVal/2, colorVal/2, colorVal/2);
                }
            } else if (visual.meshNode) {
                for (irr::u32 i = 0; i < visual.meshNode->getMaterialCount(); ++i) {
                    irr::video::SMaterial& mat = visual.meshNode->getMaterial(i);
                    mat.MaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;
                    mat.AmbientColor = fadeColor;
                    mat.DiffuseColor = fadeColor;
                    mat.EmissiveColor = irr::video::SColor(255, colorVal/2, colorVal/2, colorVal/2);
                }
            }
            if (visual.primaryEquipNode) {
                for (irr::u32 i = 0; i < visual.primaryEquipNode->getMaterialCount(); ++i) {
                    irr::video::SMaterial& mat = visual.primaryEquipNode->getMaterial(i);
                    mat.MaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;
                    mat.AmbientColor = fadeColor;
                    mat.DiffuseColor = fadeColor;
                    mat.EmissiveColor = irr::video::SColor(255, colorVal/2, colorVal/2, colorVal/2);
                }
            }
            if (visual.secondaryEquipNode) {
                for (irr::u32 i = 0; i < visual.secondaryEquipNode->getMaterialCount(); ++i) {
                    irr::video::SMaterial& mat = visual.secondaryEquipNode->getMaterial(i);
                    mat.MaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;
                    mat.AmbientColor = fadeColor;
                    mat.DiffuseColor = fadeColor;
                    mat.EmissiveColor = irr::video::SColor(255, colorVal/2, colorVal/2, colorVal/2);
                }
            }
            if (visual.nameNode) {
                irr::video::SColor nameColor(colorVal, 255, 255, 255);
                visual.nameNode->setTextColor(nameColor);
            }
            continue;
        }

        // Corpse animation timing
        if (visual.isCorpse && !visual.corpsePositionAdjusted && visual.isAnimated && visual.animatedNode) {
            visual.corpseTime += deltaTime;
            SkeletalAnimator& animator = visual.animatedNode->getAnimator();
            if (visual.corpseTime >= 0.5f) {
                if (animator.getState() == AnimationState::Stopped ||
                    (animator.getState() == AnimationState::Playing && !animator.isPlayingThrough())) {
                    visual.corpsePositionAdjusted = true;
                    visual.corpseYOffset = 0.0f;
                }
            }
        }

        // Equipment transforms for visible moving entities
        updateEquipmentTransforms(visual);
    }

    // Remove fully faded entities
    for (const auto& [spawnId, visual] : entities_) {
        if (visual.isFading && visual.fadeAlpha <= 0.0f) {
            toRemove.push_back(spawnId);
        }
    }
    for (uint16_t spawnId : toRemove) {
        LOG_DEBUG(MOD_ENTITY, "Removing fully faded corpse {}", spawnId);
        removeEntity(spawnId);
    }
}

void EntityRenderer::processUpdate(const PendingUpdate& update) {
    auto it = entities_.find(update.spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    // Log when processing the player's spawn ID
    if (update.spawnId == playerSpawnId_) {
        static int processPlayerCounter = 0;
        if (++processPlayerCounter % 30 == 0) {
            LOG_TRACE(MOD_GRAPHICS, "[PROCESS-PLAYER] processUpdate called for player spawn={} heading={:.1f}",
                     update.spawnId, update.heading);
        }
    }

    uint16_t spawnId = update.spawnId;
    float x = update.x, y = update.y, z = update.z, heading = update.heading;
    float dx = update.dx, dy = update.dy, dz = update.dz;
    int32_t animation = update.animation;

    // Check if position actually changed (ignore heading-only updates)
    float serverDeltaX = x - visual.serverX;
    float serverDeltaY = y - visual.serverY;
    float serverDeltaZ = z - visual.serverZ;
    bool positionChanged = (std::abs(serverDeltaX) > 0.01f ||
                            std::abs(serverDeltaY) > 0.01f ||
                            std::abs(serverDeltaZ) > 0.01f);

    // Invalidate cached BSP region when server position changes
    if (positionChanged) {
        visual.bspRegionDirty = true;
    }

    // For NPCs, use heading-based velocity when animation (speed) is non-zero
    // The animation field for NPCs is actually "SpeedRun" - a movement speed value
    // Server sends: anim=0 (stop), turn heading, anim=18 (start moving)
    // IMPORTANT: Trust the server's animation value directly for NPCs
    // anim=0 means stopped, anim>0 means walking/running at that speed
    // The server may send multiple packets during heading turns with the same position
    // but we should still animate based on the animation value, not position change
    bool isDebugTarget = (update.spawnId == debugTargetId_);

    // Debug: Log interpolation accuracy for targeted entity when position changes
    if (isDebugTarget && positionChanged && visual.timeSinceUpdate > 0.01f) {
        float interpErrorX = visual.lastX - x;
        float interpErrorY = visual.lastY - y;
        float interpErrorDist = std::sqrt(interpErrorX * interpErrorX + interpErrorY * interpErrorY);
        LOG_TRACE(MOD_ENTITY, "TARGET UPDATE spawn={} interpolatedPos=({},{}) serverPos=({},{}) error=({},{}) errorDist={} dt={}s",
                  spawnId, visual.lastX, visual.lastY, x, y, interpErrorX, interpErrorY, interpErrorDist, visual.timeSinceUpdate);
    }

    if (visual.isNPC) {
        if (animation != 0) {
            // NPC is moving - calculate velocity from heading and speed
            // Negative animation = moving backward (animation played in reverse)
            // Convert heading to radians: heading * π/180 (no *2, heading is full resolution from 11-bit)
            float headingRad = heading * 3.14159265f / 180.0f;

            // Speed conversion: animation value ~18 = walking speed
            // Empirically measured: anim=18 gives ~10.4 units/sec server movement
            // Multiplier: 10.4 / 18 ≈ 0.58
            // Use absolute value for speed magnitude
            float speed = static_cast<float>(std::abs(animation)) * 0.58f;

            // Calculate velocity components from heading
            // If animation is negative, reverse the direction (moving backward)
            float direction = (animation < 0) ? -1.0f : 1.0f;
            visual.velocityX = std::cos(headingRad) * speed * direction;
            visual.velocityY = std::sin(headingRad) * speed * direction;
            visual.velocityZ = 0;  // NPCs generally don't have vertical velocity while walking

            // Debug: Log velocity calculation for targeted entity
            if (isDebugTarget) {
                float predictedX = x + visual.velocityX * visual.lastUpdateInterval;
                float predictedY = y + visual.velocityY * visual.lastUpdateInterval;
                LOG_TRACE(MOD_ENTITY, "TARGET MOVE spawn={} serverPos=({},{},{}) heading={}deg anim={} vel=({},{}) predicted@{}s=({},{})",
                          update.spawnId, x, y, z, heading, animation, visual.velocityX, visual.velocityY, visual.lastUpdateInterval, predictedX, predictedY);
            }
        } else {
            // Server explicitly sent anim=0 - NPC is stopped
            visual.velocityX = 0;
            visual.velocityY = 0;
            visual.velocityZ = 0;

            // Debug: Log stop for targeted entity
            if (isDebugTarget) {
                LOG_TRACE(MOD_ENTITY, "TARGET STOP spawn={} serverPos=({},{},{}) heading={}deg anim=0 (stopped)",
                          update.spawnId, x, y, z, heading);
            }
        }
    } else {
        // For players, use server-provided delta or calculate from position change
        if (std::abs(dx) > 0.01f || std::abs(dy) > 0.01f || std::abs(dz) > 0.01f) {
            // Server provided velocity - use it directly
            visual.velocityX = dx;
            visual.velocityY = dy;
            visual.velocityZ = dz;
        } else if (positionChanged && visual.timeSinceUpdate > 0.05f) {
            // Calculate velocity from position change over time
            float invTime = 1.0f / visual.timeSinceUpdate;
            visual.velocityX = serverDeltaX * invTime;
            visual.velocityY = serverDeltaY * invTime;
            visual.velocityZ = serverDeltaZ * invTime;
        } else if (!positionChanged) {
            // Position didn't change - entity is stationary, clear velocity
            visual.velocityX = 0;
            visual.velocityY = 0;
            visual.velocityZ = 0;
        }
        // If positionChanged but timeSinceUpdate is too small, keep previous velocity
    }

    // Update server position tracking
    visual.serverX = x;
    visual.serverY = y;
    visual.serverZ = z;
    visual.serverHeading = heading;

    // Update spatial grid position if position changed significantly
    if (positionChanged) {
        updateEntityGridPosition(update.spawnId, x, y);
    }

    // Snap interpolated position to server position on update
    visual.lastX = x;
    visual.lastY = y;
    visual.lastZ = z;
    visual.lastHeading = heading;

    // Track update interval for interpolation timing
    if (visual.timeSinceUpdate > 0.05f && visual.timeSinceUpdate < 2.0f) {
        visual.lastUpdateInterval = visual.lastUpdateInterval * 0.7f + visual.timeSinceUpdate * 0.3f;
    }
    visual.timeSinceUpdate = 0;
    visual.serverAnimation = animation;

    // Performance optimization: track entities that need interpolation updates
    // Add to active set if entity has velocity (will be removed when stationary)
    if (std::abs(visual.velocityX) > 0.01f ||
        std::abs(visual.velocityY) > 0.01f ||
        std::abs(visual.velocityZ) > 0.01f) {
        activeEntities_.insert(update.spawnId);
    }

    // Track last non-zero animation (0 means "no change" from server)
    if (animation != 0) {
        visual.lastNonZeroAnimation = animation;
    }

    if (visual.sceneNode) {
        // Convert EQ Z-up to Irrlicht Y-up, add modelYOffset for correct positioning
        visual.sceneNode->setPosition(irr::core::vector3df(x, z + visual.modelYOffset, y));

        // Convert EQ heading to Irrlicht Y rotation
        // Heading arrives already in degrees (0-360) from eq.cpp 11-bit conversion
        // EQ heading: 0=North(+Y), increases clockwise
        // Irrlicht Y rotation: 0=+Z, increases counter-clockwise
        // Must negate to convert between coordinate systems
        float visualHeading = -heading;
        visual.sceneNode->setRotation(irr::core::vector3df(0, visualHeading, 0));

        // Log rotation for player
        if (spawnId == playerSpawnId_) {
            static int rotLogCounter = 0;
            if (++rotLogCounter % 30 == 0) {
                LOG_TRACE(MOD_GRAPHICS, "[PROCESS-ROT] Player setRotation: spawn={} heading={:.1f} visualHeading={:.1f}",
                          spawnId, heading, visualHeading);
            }
        }

        // Debug: Compare rotation vs velocity direction for targeted NPC
        if (isDebugTarget && visual.isNPC) {
            float velAngle = std::atan2(visual.velocityY, visual.velocityX) * 180.0f / 3.14159265f;
            float rotAngle = visualHeading;
            float diff = rotAngle - velAngle;
            // Normalize diff to -180..180
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;
            LOG_TRACE(MOD_ENTITY, "TARGET ROT vs VEL spawn={} heading={} rotation={} velAngle={} diff={} vel=({},{})",
                      spawnId, heading, rotAngle, velAngle, diff, visual.velocityX, visual.velocityY);
        }
    }

    // Set animation based on server data
    // For NPCs: animation field is "SpeedRun" - a movement speed value (0=idle, >0=moving)
    // For Players: animation field is an animation ID
    // Skip animation updates for corpses - they should stay in death pose
    if (visual.isCorpse && spawnId == debugTargetId_) {
        LOG_TRACE(MOD_ENTITY, "TARGET CORPSE Skipping animation update for corpse spawn={} currentAnim='{}'", spawnId, visual.currentAnimation);
    }
    // Skip animation updates for the local player - controlled by setPlayerEntityAnimation()
    if (visual.isAnimated && visual.animatedNode && !visual.isCorpse && !visual.isPlayer) {
        std::string targetAnim;
        bool playThrough = false;  // True for one-shot animations (jumps, attacks, emotes)
        bool loopAnim = true;

        // EQ Model animation codes:
        // l01 = Walk
        // l02 = Run
        // l05 = Falling
        // l06 = Crouch walk
        // l07 = Climbing
        // l09 = Swim
        // o01 = Stand idle
        // p02 = Sitting
        // c01-c09 = Combat animations
        // d01-d02 = Death animations
        // o01-o09 = One-shot animations (emotes, etc.)
        // t01-t09 = Skill animations

        // Check if entity is in a pose state (sitting, crouching, etc.) - don't override with movement
        if (visual.poseState == EntityVisual::PoseState::Sitting) {
            // Entity is sitting - keep sitting animation, don't override
            targetAnim = visual.currentAnimation;
            if (targetAnim.empty()) {
                targetAnim = "p02";  // Sitting idle
            }
        } else if (visual.poseState == EntityVisual::PoseState::Crouching) {
            // Entity is crouching - keep crouch animation
            targetAnim = visual.currentAnimation;
            if (targetAnim.empty()) {
                targetAnim = "l08";  // Crouching
            }
        } else if (visual.poseState == EntityVisual::PoseState::Lying) {
            // Entity is lying down - keep lying animation
            targetAnim = visual.currentAnimation;
            if (targetAnim.empty()) {
                targetAnim = "d05";  // Lying down
            }
        } else if (visual.isNPC) {
            // For NPCs, the animation field is actually "SpeedRun" - a movement speed value
            // Typical values: 0=idle, 12=walk, 15-18=slow patrol, 22-28=run, 50=fast
            // The server sends the NPC's movement speed, not an animation ID
            // 0 = stationary/idle (NOT "no change" like for players)

            if (animation > 0) {
                // NPC is moving - choose walk or run based on speed
                // Speed ~12-18 = walk, ~20+ = run
                if (animation > 20) {
                    targetAnim = "l02";  // Run
                } else {
                    targetAnim = "l01";  // Walk
                }
            } else {
                // Speed is 0 - NPC is idle
                targetAnim = "o01";  // Idle/stand
            }
        } else {
            // For other players, the animation field is an actual animation ID
            // Player Animation IDs:
            // 0 = idle/stand (or no change)
            // 1 = walk
            // 3 = crouch walk
            // 20 = jump (playThrough)
            // 27 = run
            // 22 = fly/swim
            // 16 = death (playThrough)
            // 17 = looting
            // 5 = attack (playThrough)
            // 6 = attack2 (playThrough)
            // etc.

            uint32_t effectiveAnim = (animation != 0) ? animation : visual.lastNonZeroAnimation;
            bool isMoving = (std::abs(visual.velocityX) > 0.5f || std::abs(visual.velocityY) > 0.5f);

            // Player Animation IDs to Animation Codes mapping:
            // 0 = idle/stand -> o01
            // 1 = walk -> l01
            // 3 = crouch walk -> l06 (sneaking)
            // 5/6 = attack -> weapon-based
            // 16 = death -> d05
            // 17 = looting -> p05
            // 20 = jump -> l04 (standing) or l03 (running) based on movement
            // 22 = fly/swim -> l09 (swim treading)
            // 27 = run -> l02

            // Check for playThrough animations (one-shot animations that must complete)
            if (animation == 20) {
                // Jump animation - use l04 (standing jump) or l03 (running jump)
                if (isMoving) {
                    targetAnim = "l03";  // l03 = Jump (Running)
                } else {
                    targetAnim = "l04";  // l04 = Jump (Standing)
                }
                playThrough = true;
                loopAnim = false;
            } else if (animation == 16) {
                // Death animation - playThrough, don't loop
                targetAnim = "d05";  // Death/Dying animation
                playThrough = true;
                loopAnim = false;
            } else if (animation == 5) {
                // Attack animation - use weapon-based animation, playThrough, don't loop
                targetAnim = EQ::getWeaponAttackAnimation(visual.primaryWeaponSkill, false, false);
                playThrough = true;
                loopAnim = false;

                // Phase 6.2: Match attack animation speed to weapon delay (if available)
                // Default weapon delay is ~30 (3.0 seconds), adjust animation to match
                // Note: weaponDelay in EQ is 10ths of a second, so delay 30 = 3000ms
                if (visual.animatedNode) {
                    float weaponDelayMs = visual.weaponDelayMs > 0 ? visual.weaponDelayMs : 3000.0f;
                    // We'll set duration after animation starts
                }
            } else if (animation == 6 || animation == 7) {
                // Swimming animations: 6 = swim idle (treading), 7 = swimming forward
                targetAnim = "l09";  // l09 = Swim Treading
                playThrough = false;
                loopAnim = true;
            } else if (animation == 8) {
                // Swim attack - use swim attack animation
                targetAnim = EQ::getWeaponAttackAnimation(visual.primaryWeaponSkill, false, true);
                playThrough = true;
                loopAnim = false;
            } else if (animation == 17) {
                // Looting animation - kneeling
                targetAnim = "p05";  // p05 = Kneel (Loot)
                playThrough = true;
                loopAnim = false;
            } else if (!isMoving && animation == 0) {
                // Entity is NOT moving and animation is 0 - return to idle
                // This takes priority over effectiveAnim (lastNonZeroAnimation)
                targetAnim = "o01";
            } else if (isMoving && effectiveAnim == 22) {
                // Swim/fly animation (only if actually moving)
                targetAnim = "l09";  // l09 = Swim Treading
            } else if (isMoving && effectiveAnim == 27) {
                // Run animation (only if actually moving)
                targetAnim = "l02";  // l02 = Run
            } else if (isMoving && effectiveAnim == 3) {
                // Crouch walk (sneaking) - only if actually moving
                targetAnim = "l06";  // l06 = Crouch Walk
            } else if (isMoving && effectiveAnim == 1) {
                // Walk animation (only if actually moving)
                targetAnim = "l01";  // l01 = Walk
            } else if (isMoving) {
                // Moving but no specific animation - use velocity to determine
                float speed = std::sqrt(visual.velocityX*visual.velocityX + visual.velocityY*visual.velocityY);
                if (speed > 15.0f) {
                    targetAnim = "l02";  // Run
                } else {
                    targetAnim = "l01";  // Walk
                }

                // Phase 6.2: Match animation speed to movement speed
                if (visual.animatedNode) {
                    // Base speeds: walk ~10, run ~23
                    float baseSpeed = (speed > 15.0f) ? 23.0f : 10.0f;
                    visual.animatedNode->getAnimator().matchMovementSpeed(baseSpeed, speed);
                }
            } else if ((effectiveAnim == 22 || effectiveAnim == 6 || effectiveAnim == 7) && !isMoving) {
                // Swimming but not moving - treading water
                targetAnim = "l09";  // l09 = Swim Treading
            } else if (animation != 0) {
                // Some other animation - default to idle for now
                targetAnim = "o01";
            } else {
                // Fallback - return to idle
                targetAnim = "o01";
            }
        }

        // Only change animation if different from current (unless it's a playThrough)
        // playThrough animations will be queued if another playThrough is active
        if (visual.currentAnimation != targetAnim || playThrough) {
            if (visual.animatedNode->hasAnimation(targetAnim)) {
                if (visual.animatedNode->playAnimation(targetAnim, loopAnim, playThrough)) {
                    visual.currentAnimation = targetAnim;

                    // Phase 6.2: Set attack animation speed to match weapon delay
                    if ((animation == 5 || animation == 6) && visual.weaponDelayMs > 0) {
                        visual.animatedNode->getAnimator().setTargetDuration(visual.weaponDelayMs * 0.5f);
                    }
                }
            } else {
                // Animation not found - try fallback
                auto availableAnims = visual.animatedNode->getAnimationList();
                std::string fallbackAnim;
                char animClass = !targetAnim.empty() ? std::tolower(targetAnim[0]) : 'o';

                for (const auto& anim : availableAnims) {
                    if (!anim.empty() && std::tolower(anim[0]) == animClass) {
                        fallbackAnim = anim;
                        break;
                    }
                }

                if (!fallbackAnim.empty() && visual.animatedNode->playAnimation(fallbackAnim, loopAnim, playThrough)) {
                    LOG_TRACE(MOD_GRAPHICS, "updateEntity: spawn={} animation '{}' not found, using fallback '{}' (available: {})",
                              spawnId, targetAnim, fallbackAnim, fmt::join(availableAnims, ","));
                    visual.currentAnimation = fallbackAnim;
                } else if (visual.isNPC && animation > 0) {
                    // Only log warning for moving NPCs that have no animation
                    static std::unordered_set<uint32_t> loggedEntities;
                    if (loggedEntities.find(spawnId) == loggedEntities.end()) {
                        LOG_WARN(MOD_GRAPHICS, "updateEntity: spawn={} animation '{}' not found for moving NPC (available: {})",
                                  spawnId, targetAnim, fmt::join(availableAnims, ","));
                        loggedEntities.insert(spawnId);
                    }
                }
            }
        }

        // Debug output for targeted entity - show animation state every update
        if (spawnId == debugTargetId_) {
            // Get animator state from scene node's per-instance animator
            SkeletalAnimator& animator = visual.animatedNode->getAnimator();
            const char* stateStr = "Unknown";
            switch (animator.getState()) {
                case AnimationState::Playing: stateStr = "Playing"; break;
                case AnimationState::Stopped: stateStr = "Stopped"; break;
                case AnimationState::Paused: stateStr = "Paused"; break;
            }
            LOG_TRACE(MOD_ENTITY, "TARGET ANIM updateEntity spawn={} posChanged={} serverAnim={} current='{}' target='{}' animatorCurrent='{}' state={} frame={} playThrough={} queued='{}' isNPC={}",
                      spawnId, positionChanged, animation, visual.currentAnimation, targetAnim, animator.getCurrentAnimation(), stateStr, animator.getCurrentFrame(), animator.isPlayingThrough(), animator.getQueuedAnimation(), visual.isNPC);
        }
    }
}

void EntityRenderer::updateInterpolation(float deltaTime) {
    // Flush any pending updates in reverse order before interpolation
    flushPendingUpdates();

    static int targetDebugCounter = 0;

    // Performance optimization: only iterate active entities for interpolation
    // First pass: update time tracking for all entities (cheap operation)
    for (auto& [spawnId, visual] : entities_) {
        visual.timeSinceUpdate += deltaTime;
    }

    // Second pass: process only active entities (entities with velocity or special states)
    // Also process player entity and fading entities
    std::vector<uint16_t> toDeactivate;  // Entities that became stationary

    for (uint16_t spawnId : activeEntities_) {
        auto it = entities_.find(spawnId);
        if (it == entities_.end()) continue;
        EntityVisual& visual = it->second;

        // Skip entities with no scene node at all (not yet created)
        if (!visual.sceneNode) continue;
        // Skip entities that were culled (not in scene graph)
        if (!visual.inSceneGraph) continue;

        bool isDebugTarget = (spawnId == debugTargetId_);

        // Skip player entity position interpolation - controlled by camera/input
        // But still update equipment transforms
        if (visual.isPlayer) {
            updateEquipmentTransforms(visual);
            continue;
        }

        // Handle fading entities (corpses decaying)
        if (visual.isFading) {
            visual.fadeTimer += deltaTime;
            visual.fadeAlpha = 1.0f - (visual.fadeTimer / EntityVisual::FADE_DURATION);

            if (visual.fadeAlpha <= 0.0f) {
                // Fade complete - mark for removal (can't remove during iteration)
                visual.fadeAlpha = 0.0f;
                // We'll collect and remove these after the loop
            }

            // Apply fade using additive transparency (fades to transparent/dark)
            // This works better with textured meshes than vertex alpha
            irr::u8 colorVal = static_cast<irr::u8>(visual.fadeAlpha * 255);
            irr::video::SColor fadeColor(255, colorVal, colorVal, colorVal);

            if (visual.sceneNode) {
                if (visual.animatedNode) {
                    for (irr::u32 i = 0; i < visual.animatedNode->getMaterialCount(); ++i) {
                        irr::video::SMaterial& mat = visual.animatedNode->getMaterial(i);
                        // Use additive blending which fades to transparent as color approaches black
                        mat.MaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;
                        mat.AmbientColor = fadeColor;
                        mat.DiffuseColor = fadeColor;
                        mat.EmissiveColor = irr::video::SColor(255, colorVal/2, colorVal/2, colorVal/2);
                    }
                } else if (visual.meshNode) {
                    for (irr::u32 i = 0; i < visual.meshNode->getMaterialCount(); ++i) {
                        irr::video::SMaterial& mat = visual.meshNode->getMaterial(i);
                        mat.MaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;
                        mat.AmbientColor = fadeColor;
                        mat.DiffuseColor = fadeColor;
                        mat.EmissiveColor = irr::video::SColor(255, colorVal/2, colorVal/2, colorVal/2);
                    }
                }

                // Also fade equipment
                if (visual.primaryEquipNode) {
                    for (irr::u32 i = 0; i < visual.primaryEquipNode->getMaterialCount(); ++i) {
                        irr::video::SMaterial& mat = visual.primaryEquipNode->getMaterial(i);
                        mat.MaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;
                        mat.AmbientColor = fadeColor;
                        mat.DiffuseColor = fadeColor;
                        mat.EmissiveColor = irr::video::SColor(255, colorVal/2, colorVal/2, colorVal/2);
                    }
                }
                if (visual.secondaryEquipNode) {
                    for (irr::u32 i = 0; i < visual.secondaryEquipNode->getMaterialCount(); ++i) {
                        irr::video::SMaterial& mat = visual.secondaryEquipNode->getMaterial(i);
                        mat.MaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;
                        mat.AmbientColor = fadeColor;
                        mat.DiffuseColor = fadeColor;
                        mat.EmissiveColor = irr::video::SColor(255, colorVal/2, colorVal/2, colorVal/2);
                    }
                }

                // Fade name tag
                if (visual.nameNode) {
                    irr::video::SColor nameColor(colorVal, 255, 255, 255);
                    visual.nameNode->setTextColor(nameColor);
                }
            }
            continue;  // Skip normal interpolation for fading entities
        }

        // Check if corpse needs position adjustment (mid-game death animation just finished)
        if (visual.isCorpse && !visual.corpsePositionAdjusted && visual.isAnimated && visual.animatedNode) {
            // Increment corpse timer
            visual.corpseTime += deltaTime;

            SkeletalAnimator& animator = visual.animatedNode->getAnimator();

            // Wait for at least 0.5 seconds before checking if death animation finished
            // This gives the animation time to start playing
            if (visual.corpseTime < 0.5f) {
                // Still waiting for animation to play
            } else if (animator.getState() == AnimationState::Stopped ||
                       (animator.getState() == AnimationState::Playing && !animator.isPlayingThrough())) {
                // Animation finished - mark as adjusted (no Z offset needed)
                visual.corpsePositionAdjusted = true;
                visual.corpseYOffset = 0.0f;
            }
        }

        // Skip if no velocity (stationary) - mark for deactivation
        if (std::abs(visual.velocityX) < 0.01f &&
            std::abs(visual.velocityY) < 0.01f &&
            std::abs(visual.velocityZ) < 0.01f) {
            // Debug: Log periodically for stationary target (about every second at 60fps)
            if (isDebugTarget && ++targetDebugCounter % 60 == 0) {
                std::string stateStr = "N/A";
                std::string animatorCurrent = "N/A";
                std::string queuedAnim = "";
                int frame = 0;
                bool playingThrough = false;

                if (visual.isAnimated && visual.animatedNode) {
                    SkeletalAnimator& animator = visual.animatedNode->getAnimator();
                    switch (animator.getState()) {
                        case AnimationState::Playing: stateStr = "Playing"; break;
                        case AnimationState::Stopped: stateStr = "Stopped"; break;
                        case AnimationState::Paused: stateStr = "Paused"; break;
                    }
                    animatorCurrent = animator.getCurrentAnimation();
                    queuedAnim = animator.getQueuedAnimation();
                    frame = animator.getCurrentFrame();
                    playingThrough = animator.isPlayingThrough();
                }

                LOG_TRACE(MOD_ENTITY, "TARGET ANIM STATIONARY spawn={} serverAnim={} visualAnim='{}' animatorAnim='{}' state={} frame={} playThrough={} queued='{}' timeSinceUpdate={}",
                          spawnId, visual.serverAnimation, visual.currentAnimation, animatorCurrent, stateStr, frame, playingThrough, queuedAnim, visual.timeSinceUpdate);
            }
            // Still update equipment transforms for stationary entities (they still animate)
            updateEquipmentTransforms(visual);
            // Mark for deactivation (can't modify set during iteration)
            toDeactivate.push_back(spawnId);
            continue;
        }

        // Corpses should never move - skip interpolation entirely
        if (visual.isCorpse) {
            updateEquipmentTransforms(visual);
            toDeactivate.push_back(spawnId);
            continue;
        }

        // Don't interpolate too far past the expected update time
        // This prevents entities from running off into infinity if updates stop
        if (visual.timeSinceUpdate > visual.lastUpdateInterval * 1.5f) {
            // For NPCs that are still supposed to be moving (serverAnimation != 0),
            // recalculate velocity from heading instead of stopping
            // Negative animation = moving backward
            if (visual.isNPC && !visual.isCorpse && visual.serverAnimation != 0) {
                // Recalculate velocity from heading - NPC should keep moving
                // Convert heading to radians: heading * π/180 (no *2, heading is full resolution from 11-bit)
                float headingRad = visual.serverHeading * 3.14159265f / 180.0f;
                // Use same speed multiplier as in processUpdate (0.58)
                // Apply slight damping since we're past the expected update time
                float dampingFactor = 0.85f;
                float speed = static_cast<float>(std::abs(visual.serverAnimation)) * 0.58f * dampingFactor;
                // Negative animation = moving backward (reverse direction)
                float direction = (visual.serverAnimation < 0) ? -1.0f : 1.0f;
                visual.velocityX = std::cos(headingRad) * speed * direction;
                visual.velocityY = std::sin(headingRad) * speed * direction;
                visual.velocityZ = 0;

                // Debug output throttled to ~1/sec (using targetDebugCounter from outer scope)
                if (isDebugTarget && targetDebugCounter % 60 == 0) {
                    std::string animatorCurrent = visual.currentAnimation;
                    std::string stateStr = "N/A";
                    if (visual.isAnimated && visual.animatedNode) {
                        SkeletalAnimator& animator = visual.animatedNode->getAnimator();
                        animatorCurrent = animator.getCurrentAnimation();
                        switch (animator.getState()) {
                            case AnimationState::Playing: stateStr = "Playing"; break;
                            case AnimationState::Stopped: stateStr = "Stopped"; break;
                            case AnimationState::Paused: stateStr = "Paused"; break;
                        }
                    }
                    LOG_TRACE(MOD_ENTITY, "TARGET ANIM TIMEOUT_KEEP_MOVING spawn={} serverAnim={} animatorAnim='{}' state={} recalcVel=({},{}) heading={}",
                              spawnId, visual.serverAnimation, animatorCurrent, stateStr, visual.velocityX, visual.velocityY, visual.serverHeading);
                }
                // Don't skip - continue to interpolate
            } else {
                // Stop movement until next update
                visual.velocityX = 0;
                visual.velocityY = 0;
                visual.velocityZ = 0;

                // Mark for deactivation since velocity is now zero
                toDeactivate.push_back(spawnId);

                // Determine if we should switch to idle or keep current animation
                bool keepMovingAnim = false;
                if (visual.isNPC) {
                    // For NPCs, serverAnimation is a speed value (non-zero = moving)
                    keepMovingAnim = (visual.serverAnimation != 0);
                } else {
                    // For players, check if the animation ID indicates movement
                    // Use absolute value since negative = same animation in reverse
                    int32_t absAnim = std::abs(static_cast<int32_t>(visual.lastNonZeroAnimation));
                    keepMovingAnim = (absAnim == 1 ||   // walk
                                      absAnim == 12 ||  // walk (actual)
                                      absAnim == 27 ||  // run
                                      absAnim == 22);   // fly/swim
                }

                // Don't interrupt playThrough animations (jumps, attacks, emotes)
                bool isPlayingThrough = visual.animatedNode && visual.animatedNode->isPlayingThrough();

                // Debug output throttled to ~1/sec
                if (isDebugTarget && targetDebugCounter % 60 == 0) {
                    std::string animatorCurrent = visual.currentAnimation;
                    std::string stateStr = "N/A";
                    std::string queuedAnim = "";
                    int frame = 0;
                    if (visual.isAnimated && visual.animatedNode) {
                        SkeletalAnimator& animator = visual.animatedNode->getAnimator();
                        animatorCurrent = animator.getCurrentAnimation();
                        queuedAnim = animator.getQueuedAnimation();
                        frame = animator.getCurrentFrame();
                        switch (animator.getState()) {
                            case AnimationState::Playing: stateStr = "Playing"; break;
                            case AnimationState::Stopped: stateStr = "Stopped"; break;
                            case AnimationState::Paused: stateStr = "Paused"; break;
                        }
                    }
                    LOG_TRACE(MOD_ENTITY, "TARGET ANIM TIMEOUT_STOP spawn={} serverAnim={} visualAnim='{}' animatorAnim='{}' state={} frame={} playThrough={} queued='{}' keepMovingAnim={}",
                              spawnId, visual.serverAnimation, visual.currentAnimation, animatorCurrent, stateStr, frame, isPlayingThrough, queuedAnim, keepMovingAnim);
                }

                if (!keepMovingAnim && !isPlayingThrough && visual.isAnimated && visual.animatedNode && visual.currentAnimation != "o01") {
                    if (isDebugTarget) {
                        LOG_TRACE(MOD_ENTITY, "TARGET ANIM SWITCH_TO_IDLE spawn={} from='{}'", spawnId, visual.currentAnimation);
                    }
                    if (visual.animatedNode->hasAnimation("o01")) {
                        visual.animatedNode->playAnimation("o01", true, false);
                        visual.currentAnimation = "o01";
                    }
                }
                continue;
            }
        }

        // Interpolate position based on velocity
        // Apply gentle damping only after significantly exceeding expected update interval
        // This prevents runaway if NPC changes direction, but doesn't hurt straight-line movement
        float damping = 1.0f;
        if (visual.timeSinceUpdate > visual.lastUpdateInterval * 3.0f) {
            // Well past expected update time - start slowing down gently
            float overshootTime = visual.timeSinceUpdate - visual.lastUpdateInterval * 3.0f;
            // Dampen by 50% every 5 seconds past the threshold
            damping = std::exp(-0.14f * overshootTime);
            damping = std::max(0.3f, damping);  // Don't go below 30% speed
        }
        visual.lastX += visual.velocityX * deltaTime * damping;
        visual.lastY += visual.velocityY * deltaTime * damping;
        visual.lastZ += visual.velocityZ * deltaTime * damping;

        // Snap NPC to ground during interpolation to follow terrain in hilly zones
        // Only snap if moving horizontally (has XY velocity) and we have a ground finder
        if (visual.isNPC && groundFinderCallback_ &&
            (std::abs(visual.velocityX) > 0.01f || std::abs(visual.velocityY) > 0.01f)) {
            float groundZ = groundFinderCallback_(visual.lastX, visual.lastY, visual.lastZ);
            // Server Z is the model CENTER, not feet. groundZ is the floor level.
            // We need to place the model center at groundZ + collisionZOffset (half model height).
            float targetZ = groundZ + visual.collisionZOffset;
            // Only snap if ground is within reasonable range (avoid teleporting through floors/ceilings)
            float heightDiff = targetZ - visual.lastZ;
            if (std::abs(heightDiff) < 20.0f) {
                visual.lastZ = targetZ;
            }
        }

        // Debug moving target periodically
        if (isDebugTarget && targetDebugCounter % 60 == 0) {
            std::string animatorCurrent = visual.currentAnimation;
            std::string stateStr = "N/A";
            int frame = 0;
            if (visual.isAnimated && visual.animatedNode) {
                SkeletalAnimator& animator = visual.animatedNode->getAnimator();
                animatorCurrent = animator.getCurrentAnimation();
                frame = animator.getCurrentFrame();
                switch (animator.getState()) {
                    case AnimationState::Playing: stateStr = "Playing"; break;
                    case AnimationState::Stopped: stateStr = "Stopped"; break;
                    case AnimationState::Paused: stateStr = "Paused"; break;
                }
            }
            LOG_TRACE(MOD_ENTITY, "TARGET ANIM MOVING spawn={} serverAnim={} animatorAnim='{}' state={} frame={} vel=({},{})",
                      spawnId, visual.serverAnimation, animatorCurrent, stateStr, frame, visual.velocityX, visual.velocityY);
        }

        if (visual.sceneNode) {
            // Convert EQ Z-up to Irrlicht Y-up, apply model height offset
            visual.sceneNode->setPosition(irr::core::vector3df(visual.lastX, visual.lastZ + visual.modelYOffset, visual.lastY));
        }

        // Update light position to follow entity
        if (visual.lightNode) {
            visual.lightNode->setPosition(irr::core::vector3df(visual.lastX, visual.lastZ + visual.modelYOffset + 3.0f, visual.lastY));
        }

        // Update equipment positions to follow bone attachment points
        updateEquipmentTransforms(visual);
    }

    // Remove fully faded entities (can't remove during iteration above)
    std::vector<uint16_t> toRemove;
    for (const auto& [spawnId, visual] : entities_) {
        if (visual.isFading && visual.fadeAlpha <= 0.0f) {
            toRemove.push_back(spawnId);
        }
    }
    for (uint16_t spawnId : toRemove) {
        LOG_DEBUG(MOD_ENTITY, "Removing fully faded corpse {}", spawnId);
        removeEntity(spawnId);
    }

    // Remove deactivated entities from active set (they became stationary)
    for (uint16_t spawnId : toDeactivate) {
        activeEntities_.erase(spawnId);
    }
}

void EntityRenderer::removeEntity(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    // Remove from active entities tracking
    activeEntities_.erase(spawnId);

    // Remove from boat tracking
    if (it->second.hasCollision) {
        unregisterBoat(spawnId);
    }

    // Remove from spatial grid
    removeEntityFromGrid(spawnId);

    EntityVisual& visual = it->second;

    // Release mesh ref counts for equipment
    if (equipmentModelLoader_) {
        if (visual.currentPrimaryId != 0) {
            int modelId = equipmentModelLoader_->getModelIdForItem(visual.currentPrimaryId);
            if (modelId < 0) modelId = static_cast<int>(visual.currentPrimaryId);
            equipmentModelLoader_->removeMeshRef(modelId);
        }
        if (visual.currentSecondaryId != 0) {
            int modelId = equipmentModelLoader_->getModelIdForItem(visual.currentSecondaryId);
            if (modelId < 0) modelId = static_cast<int>(visual.currentSecondaryId);
            equipmentModelLoader_->removeMeshRef(modelId);
        }
    }

    // Release mesh ref count for race model
    if (raceModelLoader_) {
        raceModelLoader_->removeMeshRef(visual.raceId, visual.gender);
    }

    // Remove equipment nodes first (they're children of sceneNode)
    if (visual.primaryEquipNode) {
        visual.primaryEquipNode->remove();
        visual.primaryEquipNode = nullptr;
    }
    if (visual.secondaryEquipNode) {
        visual.secondaryEquipNode->remove();
        visual.secondaryEquipNode = nullptr;
    }

    // Remove casting bar billboard
    if (visual.castBarBillboard) {
        visual.castBarBillboard->remove();
        visual.castBarBillboard = nullptr;
    }

    // Remove light source
    if (visual.lightNode) {
        visual.lightNode->remove();
        visual.lightNode = nullptr;
    }

    if (visual.nameNode) {
        visual.nameNode->remove();
    }
    if (visual.sceneNode) {
        // If not in scene graph, we still hold the reference from grab()
        if (visual.inSceneGraph) {
            visual.sceneNode->remove();
        }
        visual.sceneNode->drop();  // Release our reference
    }

    entities_.erase(it);
}

void EntityRenderer::startCorpseDecay(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    // Only start decay for corpses, and only if not already fading
    if (!visual.isCorpse || visual.isFading) {
        // Not a corpse or already fading - just remove immediately
        if (!visual.isCorpse) {
            removeEntity(spawnId);
        }
        return;
    }

    LOG_DEBUG(MOD_ENTITY, "Starting corpse decay for {} ({})", spawnId, visual.name);

    // Start the fade-out animation
    visual.isFading = true;
    visual.fadeTimer = 0.0f;
    visual.fadeAlpha = 1.0f;

    // Add to active entities - fading entities need per-frame updates
    activeEntities_.insert(spawnId);
}

void EntityRenderer::updateEntityAppearance(uint16_t spawnId, uint16_t raceId, uint8_t gender,
                                             const EntityAppearance& appearance) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        LOG_DEBUG(MOD_GRAPHICS, "updateEntityAppearance: entity {} not found", spawnId);
        return;
    }

    // Save current entity state
    EntityVisual& oldVisual = it->second;
    std::string name = oldVisual.name;
    float x = oldVisual.lastX;
    float y = oldVisual.lastY;
    float z = oldVisual.lastZ;
    float heading = oldVisual.lastHeading;
    bool isPlayer = oldVisual.isPlayer;
    bool isNPC = oldVisual.isNPC;
    bool isCorpse = oldVisual.isCorpse;
    std::string currentAnimation = oldVisual.currentAnimation;

    LOG_INFO(MOD_GRAPHICS, "Updating appearance for entity {} ({}) to race {} gender {}",
             spawnId, name, raceId, gender);

    // Remove the old entity (this cleans up scene nodes)
    removeEntity(spawnId);

    // Recreate with new appearance
    bool result = createEntity(spawnId, raceId, name, x, y, z, heading,
                               isPlayer, gender, appearance, isNPC, isCorpse);

    if (result) {
        // Restore animation state if entity had one
        if (!currentAnimation.empty()) {
            setEntityAnimation(spawnId, currentAnimation, true, false);
        }

        // Re-mark as player if needed (createEntity may not preserve this)
        if (isPlayer) {
            setPlayerSpawnId(spawnId);
        }

        LOG_DEBUG(MOD_GRAPHICS, "Successfully updated appearance for entity {}", spawnId);
    } else {
        LOG_WARN(MOD_GRAPHICS, "Failed to recreate entity {} with new appearance", spawnId);
    }
}

void EntityRenderer::clearEntities() {
    // P08d: emit the per-zone-load composite-entity adoption summary before tearing
    // down this zone's entities (plan B6). Only fires when the tally is non-empty (i.e.
    // enableCompositeEntities was on and at least one entity was built).
    if (compositeTotalCount_ > 0) {
        size_t sheetHits = raceModelLoader_ ? raceModelLoader_->getCompositeSheetHits() : 0;
        size_t sheetMisses = raceModelLoader_ ? raceModelLoader_->getCompositeSheetMisses() : 0;
        LOG_INFO(MOD_GRAPHICS,
                 "Composite entity adoption: {}/{} entities eligible, {} sheet hits, {} sheet misses",
                 compositeEligibleCount_, compositeTotalCount_, sheetHits, sheetMisses);
        compositeEligibleCount_ = 0;
        compositeTotalCount_ = 0;
        if (raceModelLoader_) {
            raceModelLoader_->resetCompositeAdoptionStats();
        }
    }

    for (auto& [spawnId, visual] : entities_) {
        // Remove equipment nodes first
        if (visual.primaryEquipNode) {
            visual.primaryEquipNode->remove();
        }
        if (visual.secondaryEquipNode) {
            visual.secondaryEquipNode->remove();
        }
        // Remove casting bar billboard
        if (visual.castBarBillboard) {
            visual.castBarBillboard->remove();
        }
        // Remove light node
        if (visual.lightNode) {
            visual.lightNode->remove();
        }
        if (visual.nameNode) {
            visual.nameNode->remove();
        }
        if (visual.sceneNode) {
            // If not in scene graph, we still hold the reference from grab()
            if (visual.inSceneGraph) {
                visual.sceneNode->remove();
            }
            visual.sceneNode->drop();  // Release our reference
        }
    }
    entities_.clear();
    boatSpawnIds_.clear();
}

bool EntityRenderer::hasEntity(uint16_t spawnId) const {
    return entities_.find(spawnId) != entities_.end();
}

void EntityRenderer::setAllEntitiesVisible(bool visible) {
    for (auto& [spawnId, visual] : entities_) {
        if (visual.sceneNode) {
            if (visible) {
                // Add back to scene graph if not already there
                if (!visual.inSceneGraph) {
                    smgr_->getRootSceneNode()->addChild(visual.sceneNode);
                    visual.inSceneGraph = true;
                }
                visual.sceneNode->setVisible(true);
            } else {
                visual.sceneNode->setVisible(false);
            }
        }
    }
}

size_t EntityRenderer::getModeledEntityCount() const {
    size_t count = 0;
    for (const auto& [spawnId, visual] : entities_) {
        if (!visual.usesPlaceholder) {
            count++;
        }
    }
    return count;
}

size_t EntityRenderer::getPerInstanceMemoryBytes() const {
    size_t total = 0;
    for (const auto& [spawnId, visual] : entities_) {
        total += sizeof(EntityVisual);
        if (visual.animatedNode) {
            auto* instanceMesh = visual.animatedNode->getInstanceMesh();
            if (instanceMesh) {
                for (irr::u32 i = 0; i < instanceMesh->getMeshBufferCount(); ++i) {
                    auto* buf = instanceMesh->getMeshBuffer(i);
                    if (buf) {
                        total += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
                        total += buf->getIndexCount() * sizeof(irr::u16);
                    }
                }
            }
        }
    }
    return total;
}

void EntityRenderer::setNameTagsVisible(bool visible) {
    nameTagsVisible_ = visible;

    for (auto& [spawnId, visual] : entities_) {
        if (visual.nameNode) {
            visual.nameNode->setVisible(visible);
        }
    }
}

void EntityRenderer::swapShaderMaterials(irr::s32 oldSolid, irr::s32 newSolid,
                                         irr::s32 oldAlpha, irr::s32 newAlpha,
                                         irr::s32 oldAlphaRGBA, irr::s32 newAlphaRGBA) {
    if (oldSolid == newSolid && oldAlpha == newAlpha && oldAlphaRGBA == newAlphaRGBA) return;

    auto swapMat = [&](irr::video::SMaterial& mat) {
        if (mat.MaterialType == static_cast<irr::video::E_MATERIAL_TYPE>(oldSolid))
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(newSolid);
        else if (mat.MaterialType == static_cast<irr::video::E_MATERIAL_TYPE>(oldAlpha))
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(newAlpha);
        else if (oldAlphaRGBA >= 0 && mat.MaterialType == static_cast<irr::video::E_MATERIAL_TYPE>(oldAlphaRGBA))
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(newAlphaRGBA);
    };

    auto swapSceneNode = [&](irr::scene::ISceneNode* node) {
        if (!node) return;
        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i)
            swapMat(node->getMaterial(i));
    };

    int swapped = 0;
    for (auto& [spawnId, visual] : entities_) {
        swapSceneNode(visual.meshNode);
        swapSceneNode(visual.animatedNode);
        swapSceneNode(visual.primaryEquipNode);
        swapSceneNode(visual.secondaryEquipNode);
        ++swapped;
    }
    LOG_INFO(MOD_GRAPHICS, "EntityRenderer::swapShaderMaterials: {} entities swapped (old={}/{}/{} new={}/{}/{})",
             swapped, oldSolid, oldAlpha, oldAlphaRGBA, newSolid, newAlpha, newAlphaRGBA);
}

void EntityRenderer::setLightingEnabled(bool enabled) {
    lightingEnabled_ = enabled;

    // When GLSL shaders are active, Lighting must stay false (shader handles it).
    // Only change Lighting flag for fixed-function materials.
    bool useFixedFunction = (shaderMaterialSolid_ < 0);

    // Update all entity materials
    for (auto& [spawnId, visual] : entities_) {
        if (visual.animatedNode) {
            for (irr::u32 i = 0; i < visual.animatedNode->getMaterialCount(); ++i) {
                if (useFixedFunction) {
                    visual.animatedNode->getMaterial(i).Lighting = enabled;
                    if (enabled) {
                        visual.animatedNode->getMaterial(i).NormalizeNormals = true;
                        visual.animatedNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                        visual.animatedNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                    }
                }
            }
        }
        if (visual.meshNode) {
            for (irr::u32 i = 0; i < visual.meshNode->getMaterialCount(); ++i) {
                if (useFixedFunction) {
                    visual.meshNode->getMaterial(i).Lighting = enabled;
                    if (enabled) {
                        visual.meshNode->getMaterial(i).NormalizeNormals = true;
                        visual.meshNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                        visual.meshNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                    }
                }
            }
        }
        if (visual.primaryEquipNode) {
            for (irr::u32 i = 0; i < visual.primaryEquipNode->getMaterialCount(); ++i) {
                if (useFixedFunction) {
                    visual.primaryEquipNode->getMaterial(i).Lighting = enabled;
                }
            }
        }
        if (visual.secondaryEquipNode) {
            for (irr::u32 i = 0; i < visual.secondaryEquipNode->getMaterialCount(); ++i) {
                if (useFixedFunction) {
                    visual.secondaryEquipNode->getMaterial(i).Lighting = enabled;
                }
            }
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "Entity lighting: {}", enabled ? "ON" : "OFF");
}

void EntityRenderer::setPlayerEntityVisible(bool visible) {
    bool foundPlayer = false;
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer) {
            foundPlayer = true;
            // Show/hide the mesh node (animated or static)
            if (visual.animatedNode) {
                visual.animatedNode->setVisible(visible);
                LOG_DEBUG(MOD_GRAPHICS, "setPlayerEntityVisible: visible={} spawnId={} animatedNode={} IsVisible={}",
                          visible, spawnId, (void*)visual.animatedNode, visual.animatedNode->isVisible());
            } else if (visual.meshNode) {
                visual.meshNode->setVisible(visible);
            }
            // Show/hide the name tag
            if (visual.nameNode) {
                visual.nameNode->setVisible(visible);
            }
            // Show/hide equipment
            if (visual.primaryEquipNode) {
                visual.primaryEquipNode->setVisible(visible);
            }
            if (visual.secondaryEquipNode) {
                visual.secondaryEquipNode->setVisible(visible);
            }
            break; // Only one player entity
        }
    }
    if (!foundPlayer) {
        LOG_DEBUG(MOD_GRAPHICS, "setPlayerEntityVisible: No player entity found!");
    }
}

void EntityRenderer::setDebugTargetId(uint16_t spawnId) {
    // Disable verbose logging on previous target
    if (debugTargetId_ != 0 && debugTargetId_ != spawnId) {
        auto it = entities_.find(debugTargetId_);
        if (it != entities_.end() && it->second.isAnimated && it->second.animatedNode) {
            it->second.animatedNode->getAnimator().setVerboseLogging(false);
        }
    }

    debugTargetId_ = spawnId;

    // Enable verbose logging on new target
    if (spawnId != 0) {
        auto it = entities_.find(spawnId);
        if (it != entities_.end() && it->second.isAnimated && it->second.animatedNode) {
            it->second.animatedNode->getAnimator().setVerboseLogging(true);
        }
    }
}

void EntityRenderer::debugLogPlayerVisibility() const {
    static int frameCounter = 0;
    if (++frameCounter % 60 != 0) return;  // Log once per second at 60fps

    for (const auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer) {
            bool nodeVisible = false;
            bool parentVisible = true;
            irr::scene::ISceneNode* parent = nullptr;

            if (visual.animatedNode) {
                nodeVisible = visual.animatedNode->isVisible();
                parent = visual.animatedNode->getParent();
                if (parent) {
                    parentVisible = parent->isVisible();
                }
            }

            LOG_DEBUG(MOD_GRAPHICS, "debugLogPlayerVisibility: spawnId={} animatedNode={} nodeVisible={} parent={} parentVisible={} isFirstPersonMode={}",
                      spawnId, (void*)visual.animatedNode, nodeVisible, (void*)parent, parentVisible, visual.isFirstPersonMode);
            return;
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "debugLogPlayerVisibility: No player entity found!");
}

void EntityRenderer::setPlayerFirstPersonMode(bool enabled) {
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer) {
            visual.isFirstPersonMode = enabled;

            // T300: the player model is drawn NORMALLY in first person --
            // full body, unfiltered, in the world render list, with its
            // equipment attached to its hand bones. It is hidden by the
            // first-person NEAR CLIP PLANE and by nothing else
            // (applyCameraProjectionForMode()).
            //
            // Everything this function used to do is gone on purpose:
            //   * T74's arm-bone index filter (setFirstPersonMode() on the
            //     node) drew arms only, and fell back to hiding the player
            //     when a skeleton yielded no arm bones;
            //   * T296 additionally withheld the node from the render list
            //     (setDeferredToViewmodelPass()) and hid the equipment, so a
            //     separate viewmodel pass could draw them in its own view
            //     space.
            // T299 settled the mechanism against two client versions: the
            // official client draws the ordinary world model from an eye-point
            // camera, and geometry REAPPEARS when an animation extends it
            // (a kick's boot, a swing's blade). Geometry that reappears was
            // clipped, not omitted -- so there is nothing to filter and
            // nothing to defer.
            //
            // Note for anyone tempted to "just hide the node" again: Irrlicht
            // skips OnAnimate() on invisible nodes, so a hidden player node
            // ships a FROZEN skeleton. That is why T296 withheld rather than
            // hid. Under this mechanism the node is simply visible and
            // registered, which has neither problem.
            if (visual.animatedNode) {
                visual.animatedNode->setVisible(true);
            } else if (visual.meshNode) {
                visual.meshNode->setVisible(true);
            }

            // The floating name tag is the one thing that must still go: it
            // is a camera-facing 2D billboard drawn in screen space, so the
            // near plane does not clip it and the player would read his own
            // name across the middle of the frame.
            if (visual.nameNode) {
                visual.nameNode->setVisible(!enabled);
            }

            if (visual.primaryEquipNode) {
                visual.primaryEquipNode->setVisible(true);
            }
            if (visual.secondaryEquipNode) {
                visual.secondaryEquipNode->setVisible(true);
            }

            LOG_DEBUG(MOD_GRAPHICS, "setPlayerFirstPersonMode: enabled={} (player model drawn normally; the near plane hides it)",
                      enabled);
            break;
        }
    }
}

bool EntityRenderer::isPlayerInFirstPersonMode() const {
    for (const auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer) {
            return visual.isFirstPersonMode;
        }
    }
    return false;
}

void EntityRenderer::updatePlayerEntityPosition(float x, float y, float z, float heading) {
    // Convert heading from 0-512 to degrees (0-360) for processUpdate
    float headingDegrees = heading * 360.0f / 512.0f;

    // Use stored playerSpawnId_ instead of searching for isPlayer flag
    uint16_t playerSpawnId = playerSpawnId_;

    if (playerSpawnId != 0 && entities_.find(playerSpawnId) != entities_.end()) {
        // Queue update to go through processUpdate() like NPCs do
        PendingUpdate update;
        update.spawnId = playerSpawnId;
        update.x = x;
        update.y = y;
        update.z = z;
        update.heading = headingDegrees;
        update.dx = 0;
        update.dy = 0;
        update.dz = 0;
        update.animation = 0;

        // Log player position updates
        static int playerUpdateCounter = 0;
        if (++playerUpdateCounter % 60 == 0) {
            LOG_TRACE(MOD_GRAPHICS, "[PLAYER-UPDATE] processUpdate for spawn={} pos=({:.1f},{:.1f},{:.1f}) heading={:.1f}",
                     playerSpawnId, x, y, z, headingDegrees);
        }

        processUpdate(update);
    } else {
        static int notFoundCounter = 0;
        if (++notFoundCounter % 60 == 0) {
            LOG_WARN(MOD_GRAPHICS, "[PLAYER-UPDATE] Player entity not found! spawnId={}", playerSpawnId);
        }
    }
}

void EntityRenderer::setPlayerEntityAnimation(const std::string& animCode, bool loop, float movementSpeed, bool playThrough) {
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer) {
            if (visual.isAnimated && visual.animatedNode) {
                // Don't interrupt playThrough animations (combat, skills, jump, etc.)
                // They must complete before returning to movement/idle animations
                // Exception: a new playThrough animation CAN interrupt an existing one
                if (visual.animatedNode->isPlayingThrough() && !playThrough) {
                    break;  // Let the playThrough animation finish
                }

                // Only change animation if it's different from current
                if (visual.currentAnimation != animCode) {
                    if (visual.animatedNode->hasAnimation(animCode)) {
                        visual.animatedNode->playAnimation(animCode, loop, playThrough);
                        visual.currentAnimation = animCode;
                    }
                }

                // Match animation speed to movement speed (like NPCs)
                // Base speeds: walk animation ~10 units/sec, run animation ~23 units/sec
                if (movementSpeed > 0.0f && (animCode == "l01" || animCode == "l02")) {
                    float baseSpeed = (animCode == "l02") ? 23.0f : 10.0f;
                    visual.animatedNode->getAnimator().matchMovementSpeed(baseSpeed, movementSpeed);
                } else {
                    // Reset to normal speed for non-movement animations (jump, idle, etc.)
                    visual.animatedNode->getAnimator().matchMovementSpeed(0.0f, 0.0f);
                }
            }
            break; // Only one player entity
        }
    }
}

void EntityRenderer::setPlayerSpawnId(uint16_t spawnId) {
    // Store the player spawn ID for filtering
    playerSpawnId_ = spawnId;

    // First, unmark any existing player entity
    for (auto& [id, visual] : entities_) {
        if (visual.isPlayer) {
            visual.isPlayer = false;
            // Also unmark the node for debug logging
            if (visual.animatedNode) {
                visual.animatedNode->setIsPlayerNode(false);
            }
        }
    }

    // Now mark the new entity as player
    auto it = entities_.find(spawnId);
    if (it != entities_.end()) {
        // Mark entity as player
        it->second.isPlayer = true;
        // Also mark the node for debug logging
        if (it->second.animatedNode) {
            it->second.animatedNode->setIsPlayerNode(true);
        }

        // Player always needs to be in active set for equipment transform updates
        activeEntities_.insert(spawnId);

        LOG_DEBUG(MOD_GRAPHICS, "setPlayerSpawnId: spawn={} name={} modelYOffset={:.2f} sceneNode={} animatedNode={}",
                  spawnId, it->second.name, it->second.modelYOffset,
                  (void*)it->second.sceneNode, (void*)it->second.animatedNode);
    }
}

float EntityRenderer::getPlayerModelYOffset() const {
    // Find the player entity
    for (const auto& [id, visual] : entities_) {
        if (visual.isPlayer) {
            return visual.modelYOffset;
        }
    }
    return 0.0f;
}

float EntityRenderer::getPlayerCollisionZOffset() const {
    // Find the player entity
    for (const auto& [id, visual] : entities_) {
        if (visual.isPlayer) {
            return visual.collisionZOffset;
        }
    }
    return 0.0f;
}

float EntityRenderer::getPlayerEyeHeightAboveNodeOrigin() const {
    // T300 -- READ THE FRAME BEFORE CHANGING THE NUMBER.
    //
    // The returned value is measured from the player NODE ORIGIN, which is
    // where createEntity() puts the node and which equals the server's Z for
    // this entity ("Server Z is the geometric MODEL CENTER", :594). The
    // origin is INSIDE the body: the Human male base mesh spans
    // Y = [-3.05, +3.65] about it. So bbox.MaxEdge.Y == 3.65 is the top of
    // the head, 6.70 above the mesh's own feet.
    //
    // T298 A3 measured the official client's first-person eye at
    // 6.75 +/- 0.20 above the player's server-reported z, with the ground
    // ring plane 0.10 below that z -- i.e. the official client stands the
    // model with its feet at the server z, and puts the eye at the crown of
    // a 6.7-unit body. fable stands the same model CENTRED on the server z.
    // The two clients therefore disagree about the model's placement
    // relative to z by ~3 units, and agree about the eye's placement
    // relative to the BODY to within 0.05. 3.65 (this value) and 6.75 (the
    // official measurement) are the SAME eye expressed in two frames:
    // 3.65 above the node origin == 6.70 above the feet == 6.75 +/- 0.20.
    // "Correcting" 3.65 to 6.75 here raises the camera by 3.05 units, out
    // through the top of the head. Do not.
    //
    // T76's HEAD-bone preference (3.10, i.e. 0.55 lower) is GONE: its only
    // rationale was vertical framing for the viewmodel pass T300 retired,
    // and it disagrees with the official measurement by more than that
    // measurement's error bar. It also never fired live -- T295 logged
    // [FP-EYE] source=BBOX_FALLBACK headBoneIdx=-1 on Testchar's model in
    // every session. The bbox top is now the single, unconditional answer.
    for (const auto& [id, visual] : entities_) {
        if (visual.isPlayer && visual.isAnimated && visual.animatedNode) {
            // BASE mesh bbox, not the per-instance mesh.
            EQAnimatedMesh* eqMesh = visual.animatedNode->getEQMesh();
            if (!eqMesh) {
                continue;
            }
            irr::core::aabbox3df bbox = eqMesh->getBoundingBox();
            float cameraHeight = bbox.MaxEdge.Y;
            static int eyeLogCount = 0;
            if (eyeLogCount++ % 500 == 0) {
                LOG_DEBUG(MOD_GRAPHICS, "[FP-EYE] source=BBOX_TOP eyeAboveNodeOrigin={:.4f} bboxY=[{:.4f},{:.4f}]",
                          cameraHeight, bbox.MinEdge.Y, bbox.MaxEdge.Y);
            }
            return cameraHeight;
        }
    }
    // Default fallback for human-sized entity (node-origin-relative, so
    // roughly the 3.65 a Human male measures -- NOT a feet-relative 6.x).
    return 3.65f;
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
            visual.lightNode->remove();
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
