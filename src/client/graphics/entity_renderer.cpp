#include "client/graphics/entity_renderer.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/light_source.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/animation_constants.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <set>
#include <unordered_set>
#include <chrono>
#include <fstream>
#include <fmt/ranges.h>

namespace EQT {
namespace Graphics {

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

    // Load race model mappings from JSON
    if (!areRaceMappingsLoaded()) {
        // Try multiple paths to find race_models.json
        std::vector<std::string> searchPaths = {
            "config/race_models.json",                      // Run from project root
            "../config/race_models.json",                   // Run from build directory
            "../../config/race_models.json",                // Run from build/bin directory
        };

        for (const auto& jsonPath : searchPaths) {
            std::ifstream testFile(jsonPath);
            if (testFile.good()) {
                testFile.close();
                if (loadRaceMappings(jsonPath)) {
                    LOG_INFO(MOD_GRAPHICS, "Loaded race mappings from {}", jsonPath);
                    break;
                }
            }
        }

        if (!areRaceMappingsLoaded()) {
            LOG_WARN(MOD_GRAPHICS, "Could not load race_models.json - using hardcoded defaults");
        }
    }
}

bool EntityRenderer::loadGlobalCharacters() {
    if (!raceModelLoader_) {
        return false;
    }
    return raceModelLoader_->loadGlobalModels();
}

irr::video::SColor EntityRenderer::getColorForRace(uint16_t raceId) const {
    // Assign colors based on race type for placeholders
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

float EntityRenderer::getScaleForRace(uint16_t raceId) const {
    if (raceModelLoader_) {
        return raceModelLoader_->getRaceScale(raceId);
    }

    // Fallback scale factors
    switch (raceId) {
        case 2:   return 1.2f;   // Barbarian
        case 8:   return 0.7f;   // Dwarf
        case 9:   return 1.4f;   // Troll
        case 10:  return 1.5f;   // Ogre
        case 11:  return 0.5f;   // Halfling
        case 12:  return 0.6f;   // Gnome
        case 42:  return 0.3f;   // Rat
        case 43:  return 0.4f;   // Bat
        case 85:  return 3.0f;   // Dragon
        default:  return 1.0f;
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
        0, 2, 1, 0, 3, 2,
        // Top
        4, 5, 6, 4, 6, 7,
        // Front
        0, 1, 5, 0, 5, 4,
        // Back
        2, 3, 7, 2, 7, 6,
        // Left
        3, 0, 4, 3, 4, 7,
        // Right
        1, 2, 6, 1, 6, 5,
    };

    for (int i = 0; i < 8; ++i) {
        buffer->Vertices.push_back(vertices[i]);
    }
    for (int i = 0; i < 36; ++i) {
        buffer->Indices.push_back(indices[i]);
    }
    buffer->recalculateBoundingBox();

    buffer->Material.Lighting = lightingEnabled_;
    buffer->Material.BackfaceCulling = false;
    if (lightingEnabled_) {
        buffer->Material.NormalizeNormals = true;
        buffer->Material.AmbientColor = irr::video::SColor(255, 255, 255, 255);
        buffer->Material.DiffuseColor = irr::video::SColor(255, 255, 255, 255);
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

bool EntityRenderer::createEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                                   float x, float y, float z, float heading, bool isPlayer,
                                   uint8_t gender, const EntityAppearance& appearance, bool isNPC,
                                   bool isCorpse, float serverSize) {
    auto createStart = std::chrono::steady_clock::now();

    if (!smgr_ || hasEntity(spawnId)) {
        return false;
    }

    // Skip invisible entities (zone controllers, markers, etc.)
    float scale = getScaleForRace(raceId);
    if (scale <= 0.0f) {
        return true;  // Return true since this is expected behavior
    }

    // Apply server size multiplier
    // Server size is an absolute value where 6.0 is "standard humanoid" size
    // Convert to multiplier by dividing by reference size (6.0)
    // If serverSize is 0, use default multiplier of 1.0
    constexpr float REFERENCE_SIZE = 6.0f;
    float sizeMultiplier = (serverSize > 0.0f) ? (serverSize / REFERENCE_SIZE) : 1.0f;
    float baseScale = scale;
    scale *= sizeMultiplier;

    // Debug: log size information for all entities
    // Formula: finalScale = baseScale * (serverSize / 6.0)
    LOG_DEBUG(MOD_GRAPHICS, "createEntity[{}]: race={} size: {:.2f} * ({:.2f}/6) = {:.2f}",
              name, raceId, baseScale, serverSize, scale);

    EntityVisual visual;
    visual.spawnId = spawnId;
    visual.raceId = raceId;
    visual.gender = gender;
    visual.name = name;
    visual.isPlayer = isPlayer;
    visual.isNPC = isNPC;
    visual.isCorpse = isCorpse;

    if (isPlayer) {
        LOG_DEBUG(MOD_ENTITY, "Creating PLAYER entity: spawn={} name={} race={}", spawnId, name, raceId);
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

    // Add to spatial grid for efficient visibility queries
    updateEntityGridPosition(spawnId, x, y);

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

        // Use full equipment appearance if any equipment is set
        if (hasEquipment || appearance.face != 0 || appearance.texture != 0) {
            animNode = raceModelLoader_->createAnimatedNodeWithEquipment(
                raceId, gender, appearance, smgr_->getRootSceneNode());
        } else {
            animNode = raceModelLoader_->createAnimatedNode(raceId, gender, smgr_->getRootSceneNode());
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
        visual.isAnimated = true;
        visual.usesPlaceholder = false;

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
		animNode->getMaterial(i).Lighting = lightingEnabled_;
		animNode->getMaterial(i).BackfaceCulling = false;
		if (lightingEnabled_) {
			animNode->getMaterial(i).NormalizeNormals = true;
			animNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
			animNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
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

	entities_[spawnId] = visual;

	// Attach equipment models to bone attachment points
	attachEquipment(entities_[spawnId]);

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
			entities_[spawnId].corpsePositionAdjusted = true;
			entities_[spawnId].corpseYOffset = 0.0f;

			entities_[spawnId].currentAnimation = deathAnim;
		}
	}

	// Log total entity creation time if slow
	auto createEnd = std::chrono::steady_clock::now();
	auto createMs = std::chrono::duration_cast<std::chrono::milliseconds>(createEnd - createStart).count();
	if (createMs > 50) {
		LOG_WARN(MOD_GRAPHICS, "PERF: createEntity (animated) for {} race {} took {} ms", name, raceId, createMs);
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
        visual.meshNode->getMaterial(i).Lighting = lightingEnabled_;
        visual.meshNode->getMaterial(i).BackfaceCulling = false;
        if (lightingEnabled_) {
            visual.meshNode->getMaterial(i).NormalizeNormals = true;
            visual.meshNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
            visual.meshNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
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
        LOG_DEBUG(MOD_GRAPHICS, "Boat collision: race {} radius={:.1f} height={:.1f} deckZ={:.1f}",
            raceId, visual.collisionRadius, visual.collisionHeight, visual.deckZ);
    }

    entities_[spawnId] = visual;

    // Log entity creation with model status
    if (!usesPlaceholder) {
        LOG_DEBUG(MOD_ENTITY, "Created entity {} ({}) with race {} 3D model (static)", spawnId, name, raceId);
    } else {
        LOG_DEBUG(MOD_ENTITY, "Created entity {} ({}) with race {} placeholder (no model found)", spawnId, name, raceId);
    }

    // Log total entity creation time if slow
    auto createEnd = std::chrono::steady_clock::now();
    auto createMs = std::chrono::duration_cast<std::chrono::milliseconds>(createEnd - createStart).count();
    if (createMs > 50) {
        LOG_WARN(MOD_GRAPHICS, "PERF: createEntity (static) for {} race {} took {} ms", name, raceId, createMs);
        EQT::PerformanceMetrics::instance().recordSample("Slow Entity Create", createMs);
    }

    return true;
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
            } else if (animation == 5 || animation == 6) {
                // Attack animations - use weapon-based animation, playThrough, don't loop
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
            } else if (effectiveAnim == 22 && !isMoving) {
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
                    LOG_DEBUG(MOD_GRAPHICS, "updateEntity: spawn={} animation '{}' not found, using fallback '{}' (available: {})",
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

    // Remove from spatial grid
    removeEntityFromGrid(spawnId);

    EntityVisual& visual = it->second;

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

void EntityRenderer::updateNameTags(irr::scene::ICameraSceneNode* camera) {
    if (!camera) {
        return;
    }

    irr::core::vector3df cameraPos = camera->getPosition();

    // Pre-compute squared distances to avoid sqrt in hot loop
    float renderDistanceSq = renderDistance_ * renderDistance_;
    float nameTagDistanceSq = nameTagDistance_ * nameTagDistance_;

    // Use spatial grid to only check entities potentially in range
    // Camera pos is in Irrlicht coords (X, Y, Z) where Y is up
    // Entity positions are in EQ coords (x, y, z) where z is up
    // Grid uses EQ X, Y (horizontal plane)
    float eqCameraX = cameraPos.X;
    float eqCameraY = cameraPos.Z;  // EQ Y = Irrlicht Z
    float eqCameraZ = cameraPos.Y;  // EQ Z = Irrlicht Y

    // Update camera's BSP region for PVS culling (with caching)
    if (bspTree_ && !bspTree_->regions.empty()) {
        // Cache BSP lookup - only recompute if position changed significantly (>5 units)
        static float lastBspCamX = -99999.0f, lastBspCamY = -99999.0f, lastBspCamZ = -99999.0f;
        static std::shared_ptr<BspRegion> cachedCamRegion;

        float dx = eqCameraX - lastBspCamX;
        float dy = eqCameraY - lastBspCamY;
        float dz = eqCameraZ - lastBspCamZ;
        float distSq = dx*dx + dy*dy + dz*dz;

        std::shared_ptr<BspRegion> region;
        if (distSq > 25.0f) {  // 5 units squared
            region = bspTree_->findRegionForPoint(eqCameraX, eqCameraY, eqCameraZ);
            cachedCamRegion = region;
            lastBspCamX = eqCameraX;
            lastBspCamY = eqCameraY;
            lastBspCamZ = eqCameraZ;
        } else {
            region = cachedCamRegion;
        }

        if (region) {
            // Find the region's index
            size_t regionIdx = SIZE_MAX;
            for (size_t i = 0; i < bspTree_->regions.size(); ++i) {
                if (bspTree_->regions[i] == region) {
                    regionIdx = i;
                    break;
                }
            }
            if (regionIdx != currentCameraRegionIdx_) {
                currentCameraRegionIdx_ = regionIdx;
                currentCameraRegion_ = region;
                LOG_TRACE(MOD_GRAPHICS, "EntityRenderer: Camera entered region {}", regionIdx);
            }
        } else {
            // Camera outside BSP tree - show all entities
            if (currentCameraRegionIdx_ != SIZE_MAX) {
                currentCameraRegionIdx_ = SIZE_MAX;
                currentCameraRegion_ = nullptr;
                LOG_TRACE(MOD_GRAPHICS, "EntityRenderer: Camera outside BSP tree");
            }
        }
    }

    // Get entities potentially within render distance
    std::vector<uint16_t> nearbyEntities;
    getEntitiesInRange(eqCameraX, eqCameraY, renderDistance_, nearbyEntities);

    // Track which entities we've processed (those not nearby should be hidden)
    std::unordered_set<uint16_t> processedEntities;

    // Process nearby entities
    for (uint16_t spawnId : nearbyEntities) {
        processedEntities.insert(spawnId);

        auto it = entities_.find(spawnId);
        if (it == entities_.end()) continue;

        EntityVisual& visual = it->second;

        // Calculate squared distance from camera to entity (avoids sqrt)
        irr::core::vector3df entityPos(visual.lastX, visual.lastZ, visual.lastY);
        float distanceSq = cameraPos.getDistanceFromSQ(entityPos);

        // Check distance-based visibility
        bool inRenderDistance = (distanceSq <= renderDistanceSq);

        // Check PVS visibility (if BSP tree is set)
        bool pvsVisible = true;  // Default to visible if no PVS data
        if (bspTree_ && currentCameraRegion_ && !currentCameraRegion_->visibleRegions.empty()) {
            // Find which region the entity is in
            auto entityRegion = bspTree_->findRegionForPoint(visual.lastX, visual.lastY, visual.lastZ);
            if (entityRegion) {
                // Find the entity region's index
                size_t entityRegionIdx = SIZE_MAX;
                for (size_t i = 0; i < bspTree_->regions.size(); ++i) {
                    if (bspTree_->regions[i] == entityRegion) {
                        entityRegionIdx = i;
                        break;
                    }
                }
                // Check if entity's region is visible from camera's region
                if (entityRegionIdx < currentCameraRegion_->visibleRegions.size()) {
                    pvsVisible = currentCameraRegion_->visibleRegions[entityRegionIdx];
                }
                // If entity region is outside PVS array, assume visible (conservative)
            }
            // If entity is outside BSP tree, assume visible
        }

        // Combined visibility: must be in render distance AND PVS visible
        bool isVisible = inRenderDistance && pvsVisible;

        // Update model visibility (handles both animated and static mesh nodes)
        if (visual.animatedNode) {
            visual.animatedNode->setVisible(isVisible);
        }
        if (visual.meshNode) {
            visual.meshNode->setVisible(isVisible);
        }

        // Update name tag visibility based on name tag distance, global toggle, and PVS
        if (visual.nameNode) {
            bool nameVisible = nameTagsVisible_ && (distanceSq <= nameTagDistanceSq) && pvsVisible;
            visual.nameNode->setVisible(nameVisible);
        }

        // Update equipment visibility to match entity visibility
        if (visual.primaryEquipNode) {
            visual.primaryEquipNode->setVisible(isVisible);
        }
        if (visual.secondaryEquipNode) {
            visual.secondaryEquipNode->setVisible(isVisible);
        }
    }

    // Hide entities that weren't in range
    for (auto& [spawnId, visual] : entities_) {
        if (processedEntities.find(spawnId) == processedEntities.end()) {
            // Entity not in nearby set - hide it
            if (visual.animatedNode) {
                visual.animatedNode->setVisible(false);
            }
            if (visual.meshNode) {
                visual.meshNode->setVisible(false);
            }
            if (visual.nameNode) {
                visual.nameNode->setVisible(false);
            }
            if (visual.primaryEquipNode) {
                visual.primaryEquipNode->setVisible(false);
            }
            if (visual.secondaryEquipNode) {
                visual.secondaryEquipNode->setVisible(false);
            }
        }
    }
}

void EntityRenderer::setNameTagsVisible(bool visible) {
    nameTagsVisible_ = visible;

    for (auto& [spawnId, visual] : entities_) {
        if (visual.nameNode) {
            visual.nameNode->setVisible(visible);
        }
    }
}

void EntityRenderer::setLightingEnabled(bool enabled) {
    lightingEnabled_ = enabled;

    // Update all entity materials
    for (auto& [spawnId, visual] : entities_) {
        if (visual.animatedNode) {
            for (irr::u32 i = 0; i < visual.animatedNode->getMaterialCount(); ++i) {
                visual.animatedNode->getMaterial(i).Lighting = enabled;
                if (enabled) {
                    visual.animatedNode->getMaterial(i).NormalizeNormals = true;
                    visual.animatedNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                    visual.animatedNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                }
            }
        }
        if (visual.meshNode) {
            for (irr::u32 i = 0; i < visual.meshNode->getMaterialCount(); ++i) {
                visual.meshNode->getMaterial(i).Lighting = enabled;
                if (enabled) {
                    visual.meshNode->getMaterial(i).NormalizeNormals = true;
                    visual.meshNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                    visual.meshNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                }
            }
        }
        if (visual.primaryEquipNode) {
            for (irr::u32 i = 0; i < visual.primaryEquipNode->getMaterialCount(); ++i) {
                visual.primaryEquipNode->getMaterial(i).Lighting = enabled;
            }
        }
        if (visual.secondaryEquipNode) {
            for (irr::u32 i = 0; i < visual.secondaryEquipNode->getMaterialCount(); ++i) {
                visual.secondaryEquipNode->getMaterial(i).Lighting = enabled;
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
            LOG_DEBUG(MOD_GRAPHICS, "setPlayerFirstPersonMode: enabled={}", enabled);

            // Set first-person mode on the animated mesh node
            // This will show only arms while hiding body/head
            if (visual.animatedNode) {
                visual.animatedNode->setFirstPersonMode(enabled);
                // Keep the node visible - the mesh itself handles hiding non-arm parts
                visual.animatedNode->setVisible(true);

            } else if (visual.meshNode) {
                // Static meshes don't support first-person mode, just hide
                visual.meshNode->setVisible(!enabled);
            }

            // Always hide name tag in first-person
            if (visual.nameNode) {
                visual.nameNode->setVisible(!enabled);
            }

            // Equipment stays attached to hand bones (visible with arms)
            // Equipment transforms are updated by updateEquipmentTransforms()

            break;
        }
    }
}

void EntityRenderer::updateFirstPersonWeapons(const irr::core::vector3df& cameraPos,
                                               const irr::core::vector3df& cameraTarget,
                                               float deltaTime) {
    // In proper first-person mode, weapons stay attached to the character's hands
    // via the skeleton bone system. No camera-relative positioning needed.
    // The arms and equipment animate together through the normal skeletal animation.

    // Update equipment transforms as usual (they follow hand bones)
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer && visual.isFirstPersonMode) {
            updateEquipmentTransforms(visual);
            break;
        }
    }
}

void EntityRenderer::triggerFirstPersonAttack() {
    // In proper first-person mode, attack animations are played on the full character
    // model (only arms visible). The animation system handles the weapon movement.
    // This method is kept for compatibility but the actual attack animation
    // is triggered via setEntityAnimation() from ZoneProcessDamage.
    LOG_DEBUG(MOD_GRAPHICS, "triggerFirstPersonAttack: Attack anim via skeleton");
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

bool EntityRenderer::getPlayerHeadBonePosition(float& eqX, float& eqY, float& eqZ) const {
    // Find the player entity
    const EntityVisual* playerVisual = nullptr;
    for (const auto& [id, visual] : entities_) {
        if (visual.isPlayer) {
            playerVisual = &visual;
            break;
        }
    }

    if (!playerVisual || !playerVisual->isAnimated || !playerVisual->animatedNode) {
        return false;
    }

    // Use the BASE mesh bounding box (not instance mesh) for camera height.
    // In first-person mode, the instance mesh has collapsed vertices (Y=-10000),
    // which would corrupt the bounding box. The base mesh is unmodified.
    EQAnimatedMesh* eqMesh = playerVisual->animatedNode->getEQMesh();
    if (!eqMesh) {
        return false;
    }
    irr::core::aabbox3df bbox = eqMesh->getBoundingBox();

    // Camera should be at the TOP of the bounding box, looking straight ahead.
    // User requested: "looking out from the top of a bounding box over the model"
    // Use full bbox height, no offset for "eyes" - just top of model.
    float cameraHeightFromFeet = bbox.MaxEdge.Y - bbox.MinEdge.Y;

    // X and Y use entity position
    eqX = playerVisual->lastX;
    eqY = playerVisual->lastY;

    // For Z: The collision system should have placed feet at ground level.
    // Camera Z = lastZ (feet) + full model height
    eqZ = playerVisual->lastZ + cameraHeightFromFeet;

    // Debug logging
    static int logCount = 0;
    if (logCount++ % 100 == 0) {
        LOG_DEBUG(MOD_GRAPHICS, "HeadPos: lastZ={:.2f} cameraHeight={:.2f} => eqZ={:.2f} (bbox Y: {:.2f} to {:.2f})",
                  playerVisual->lastZ, cameraHeightFromFeet, eqZ, bbox.MinEdge.Y, bbox.MaxEdge.Y);
    }

    return true;
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

float EntityRenderer::getPlayerEyeHeightFromFeet() const {
    // Find the player entity
    for (const auto& [id, visual] : entities_) {
        if (visual.isPlayer && visual.isAnimated && visual.animatedNode) {
            // Use BASE mesh bounding box (not instance mesh) to avoid corruption
            // from first-person mode collapsed vertices
            EQAnimatedMesh* eqMesh = visual.animatedNode->getEQMesh();
            if (!eqMesh) {
                continue;
            }
            irr::core::aabbox3df bbox = eqMesh->getBoundingBox();
            // Camera height = full model height (top of bounding box)
            // No offset for "eyes" - user wants camera at top of bbox looking straight ahead
            float cameraHeight = bbox.MaxEdge.Y - bbox.MinEdge.Y;
            return cameraHeight;
        }
    }
    // Default fallback for human-sized entity
    return 6.0f;
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

bool EntityRenderer::loadEquipmentModels() {
    if (!equipmentModelLoader_) {
        return false;
    }

    // Load item ID to model ID mapping
    // Try multiple paths to find item_models.json
    std::vector<std::string> searchPaths = {
        "data/item_models.json",                      // Run from project root
        "../data/item_models.json",                   // Run from build directory
        clientPath_ + "../data/item_models.json",    // Relative to EQ client path
        clientPath_ + "../../eqt-irrlicht/data/item_models.json"  // EQ client in sibling dir
    };

    int mappingCount = -1;
    std::string successPath;
    for (const auto& path : searchPaths) {
        mappingCount = equipmentModelLoader_->loadItemModelMapping(path);
        if (mappingCount >= 0) {
            successPath = path;
            break;
        }
    }

    if (mappingCount < 0) {
        LOG_ERROR(MOD_ENTITY, "EntityRenderer: Failed to load item model mapping from any path");
        for (const auto& path : searchPaths) {
            LOG_ERROR(MOD_ENTITY, "  - {}", path);
        }
    } else {
        LOG_INFO(MOD_ENTITY, "EntityRenderer: Loaded {} item-to-model mappings from {}", mappingCount, successPath);
    }

    // Load equipment archives
    bool loaded = equipmentModelLoader_->loadEquipmentArchives();
    if (loaded) {
        LOG_INFO(MOD_ENTITY, "EntityRenderer: Loaded {} equipment models", equipmentModelLoader_->getLoadedModelCount());
    }
    return loaded;
}

void EntityRenderer::attachEquipment(EntityVisual& visual) {
    if (!equipmentModelLoader_ || !equipmentModelLoader_->isLoaded()) {
        LOG_DEBUG(MOD_ENTITY, "attachEquipment: Equipment loader not ready");
        return;
    }

    // Get primary and secondary equipment IDs from appearance
    uint32_t primaryId = visual.appearance.equipment[7];    // Primary slot
    uint32_t secondaryId = visual.appearance.equipment[8];  // Secondary slot

    // Skip if no equipment
    if (primaryId == 0 && secondaryId == 0) {
        return;
    }

    LOG_DEBUG(MOD_ENTITY, "attachEquipment: Entity {} ({}) primary={} secondary={}", visual.spawnId, visual.name, primaryId, secondaryId);

    // Get the entity's scene node (parent for equipment)
    irr::scene::ISceneNode* parentNode = visual.sceneNode;
    if (!parentNode) {
        return;
    }

    // Attach primary weapon (right hand)
    if (primaryId != 0) {
        irr::scene::IMesh* primaryMesh = equipmentModelLoader_->getEquipmentMesh(primaryId);
        if (primaryMesh) {
            visual.primaryEquipNode = smgr_->addMeshSceneNode(primaryMesh, parentNode);
            if (visual.primaryEquipNode) {
                visual.currentPrimaryId = primaryId;
                // Initial position will be updated by updateEquipmentTransforms
                visual.primaryEquipNode->setScale(irr::core::vector3df(1.0f, 1.0f, 1.0f));
                for (irr::u32 i = 0; i < visual.primaryEquipNode->getMaterialCount(); ++i) {
                    visual.primaryEquipNode->getMaterial(i).Lighting = lightingEnabled_;
                    visual.primaryEquipNode->getMaterial(i).BackfaceCulling = false;
                }
                LOG_DEBUG(MOD_ENTITY, "Attached primary equipment {} to entity {}", primaryId, visual.spawnId);
            }
        }
    }

    // Attach secondary weapon/shield (left hand or shield point)
    if (secondaryId != 0) {
        irr::scene::IMesh* secondaryMesh = equipmentModelLoader_->getEquipmentMesh(secondaryId);
        if (secondaryMesh) {
            visual.secondaryEquipNode = smgr_->addMeshSceneNode(secondaryMesh, parentNode);
            if (visual.secondaryEquipNode) {
                visual.currentSecondaryId = secondaryId;
                // Initial position will be updated by updateEquipmentTransforms
                visual.secondaryEquipNode->setScale(irr::core::vector3df(1.0f, 1.0f, 1.0f));
                for (irr::u32 i = 0; i < visual.secondaryEquipNode->getMaterialCount(); ++i) {
                    visual.secondaryEquipNode->getMaterial(i).Lighting = lightingEnabled_;
                    visual.secondaryEquipNode->getMaterial(i).BackfaceCulling = false;
                }
                LOG_DEBUG(MOD_ENTITY, "Attached secondary equipment {} to entity {}", secondaryId, visual.spawnId);
            }
        }
    }
}

// Helper: Extract rotation from bone matrix (EQ to Irrlicht coordinate conversion)
static irr::core::vector3df extractBoneRotation(const BoneMat4& m) {
    // Convert EQ bone matrix to Irrlicht rotation
    // We need to swap Y and Z axes
    irr::core::matrix4 irrMat;
    irrMat[0] = m.m[0];   irrMat[1] = m.m[2];   irrMat[2] = m.m[1];   irrMat[3] = 0;
    irrMat[4] = m.m[8];   irrMat[5] = m.m[10];  irrMat[6] = m.m[9];   irrMat[7] = 0;
    irrMat[8] = m.m[4];   irrMat[9] = m.m[6];   irrMat[10] = m.m[5];  irrMat[11] = 0;
    irrMat[12] = 0;       irrMat[13] = 0;       irrMat[14] = 0;       irrMat[15] = 1;

    return irrMat.getRotationDegrees();
}

// Helper: Find bone index trying multiple naming conventions
static int findBoneIndex(const std::shared_ptr<CharacterSkeleton>& skeleton, const std::string& raceCode,
                         const std::vector<std::string>& suffixes) {
    if (!skeleton) return -1;

    for (const auto& suffix : suffixes) {
        // Try uppercase race code
        std::string upperName = raceCode + suffix;
        int idx = skeleton->getBoneIndex(upperName);
        if (idx >= 0) return idx;

        // Try lowercase race code
        std::string lowerRace = raceCode;
        std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        std::string lowerName = lowerRace + suffix;
        idx = skeleton->getBoneIndex(lowerName);
        if (idx >= 0) return idx;

        // Try without underscore (e.g., "qcmr_point" instead of "qcm_r_point")
        if (suffix.length() > 0 && suffix[0] != '_') {
            // Already no leading underscore
        } else if (suffix.length() > 1) {
            std::string noUnderscoreSuffix = suffix.substr(1);
            idx = skeleton->getBoneIndex(lowerRace + noUnderscoreSuffix);
            if (idx >= 0) return idx;
        }

        // Try with _DAG suffix removed if present
        if (suffix.length() > 4 && suffix.substr(suffix.length() - 4) == "_DAG") {
            std::string noDAG = suffix.substr(0, suffix.length() - 4);
            idx = skeleton->getBoneIndex(raceCode + noDAG);
            if (idx >= 0) return idx;
            idx = skeleton->getBoneIndex(lowerRace + noDAG);
            if (idx >= 0) return idx;
        }
    }
    return -1;
}

void EntityRenderer::updateEquipmentTransforms(EntityVisual& visual) {
    // Only update for animated entities with equipment
    if (!visual.isAnimated || !visual.animatedNode) {
        return;
    }

    if (!visual.primaryEquipNode && !visual.secondaryEquipNode) {
        return;
    }

    // Get animator and skeleton
    SkeletalAnimator& animator = visual.animatedNode->getAnimator();
    auto skeleton = animator.getSkeleton();
    if (!skeleton) {
        return;
    }

    const auto& boneStates = animator.getBoneStates();
    if (boneStates.empty()) {
        return;
    }

    // Get race code for bone name construction
    std::string raceCode = RaceModelLoader::getRaceCode(visual.raceId);
    std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                  [](unsigned char c) { return std::toupper(c); });

    // Update primary weapon position and rotation (right hand - R_POINT)
    if (visual.primaryEquipNode) {
        std::vector<std::string> suffixes = {"R_POINT_DAG", "R_POINT", "r_point_dag", "r_point"};
        int boneIndex = findBoneIndex(skeleton, raceCode, suffixes);

        if (boneIndex >= 0 && boneIndex < static_cast<int>(boneStates.size())) {
            const BoneMat4& worldTransform = boneStates[boneIndex].worldTransform;

            // Extract position from matrix (column 3)
            float px = worldTransform.m[12];
            float py = worldTransform.m[13];
            float pz = worldTransform.m[14];

            // Extract and apply bone rotation, then add weapon's base rotation (180° around X)
            irr::core::vector3df boneRot = extractBoneRotation(worldTransform);
            irr::core::vector3df weaponRot = boneRot + irr::core::vector3df(180.0f, 0, 0);
            visual.primaryEquipNode->setRotation(weaponRot);

            // Calculate weapon offset based on mesh bounding box
            // The grip point should be near the hilt (end of weapon) for 1H weapons,
            // or at midpoint for 2H weapons (staffs, etc.)
            float weaponOffset = 0.5f;  // Default fallback
            irr::scene::IMesh* mesh = visual.primaryEquipNode->getMesh();
            if (mesh) {
                irr::core::aabbox3df bbox = mesh->getBoundingBox();
                float weaponLength = bbox.MaxEdge.X - bbox.MinEdge.X;  // Length along X axis

                // Check if this is a two-handed weapon (staffs, 2H swords, etc.)
                // Two-handed weapons are typically longer (> 3 units) or specific model IDs
                bool isTwoHanded = (weaponLength > 3.0f);

                if (isTwoHanded) {
                    // Two-handed: grip at midpoint (half the length from center)
                    weaponOffset = weaponLength * 0.25f;
                } else {
                    // One-handed: grip near the hilt (small offset from end)
                    // The hilt is at MinEdge.X, so offset is distance from center to near-hilt
                    weaponOffset = -bbox.MinEdge.X * 0.8f;  // 80% toward hilt end
                }
            }

            // Apply offset along the weapon's local X axis (blade direction)
            irr::core::matrix4 rotMat;
            rotMat.setRotationDegrees(weaponRot);
            irr::core::vector3df localOffset(weaponOffset, 0, 0);
            rotMat.rotateVect(localOffset);

            // Convert EQ coords to Irrlicht coords: EQ(x,y,z) -> Irr(x,z,y) and apply offset
            visual.primaryEquipNode->setPosition(irr::core::vector3df(
                px + localOffset.X, pz + localOffset.Y, py + localOffset.Z));
        }
    }

    // Update secondary weapon/shield position and rotation (left hand or shield point)
    if (visual.secondaryEquipNode) {
        // Determine if this is a shield based on the direct model ID
        // For NPC equipment, the ID is already the model ID, not a database item ID
        bool isShield = EquipmentModelLoader::isShield(static_cast<int>(visual.currentSecondaryId));

        std::vector<std::string> suffixes = isShield
            ? std::vector<std::string>{"SHIELD_POINT_DAG", "SHIELD_POINT", "shield_point_dag", "shield_point",
                                       "L_POINT_DAG", "L_POINT", "l_point_dag", "l_point"}
            : std::vector<std::string>{"L_POINT_DAG", "L_POINT", "l_point_dag", "l_point"};
        int boneIndex = findBoneIndex(skeleton, raceCode, suffixes);

        if (boneIndex >= 0 && boneIndex < static_cast<int>(boneStates.size())) {
            const BoneMat4& worldTransform = boneStates[boneIndex].worldTransform;

            // Extract position from matrix (column 3)
            float px = worldTransform.m[12];
            float py = worldTransform.m[13];
            float pz = worldTransform.m[14];

            // Extract and apply bone rotation, then add base rotation
            // Shields need 180° around both X and Z axes
            irr::core::vector3df boneRot = extractBoneRotation(worldTransform);
            irr::core::vector3df itemRot = boneRot + irr::core::vector3df(180.0f, 0, isShield ? 180.0f : 0);
            visual.secondaryEquipNode->setRotation(itemRot);

            // Calculate offset based on item type and size
            float itemOffset = 0.5f;  // Default fallback
            irr::scene::IMesh* mesh = visual.secondaryEquipNode->getMesh();
            if (mesh && !isShield) {
                // For weapons (not shields), calculate offset from bounding box
                irr::core::aabbox3df bbox = mesh->getBoundingBox();
                float weaponLength = bbox.MaxEdge.X - bbox.MinEdge.X;

                bool isTwoHanded = (weaponLength > 3.0f);
                if (isTwoHanded) {
                    itemOffset = weaponLength * 0.25f;
                } else {
                    itemOffset = -bbox.MinEdge.X * 0.8f;
                }
            } else if (isShield) {
                // Shields: small offset to position on arm
                itemOffset = 0.3f;
            }

            // Apply offset along local X axis
            irr::core::matrix4 rotMat;
            rotMat.setRotationDegrees(itemRot);
            irr::core::vector3df localOffset(itemOffset, 0, 0);
            rotMat.rotateVect(localOffset);

            // Convert EQ coords to Irrlicht coords and apply offset
            visual.secondaryEquipNode->setPosition(irr::core::vector3df(
                px + localOffset.X, pz + localOffset.Y, py + localOffset.Z));
        }
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

        // No animation of this class available
        LOG_WARN(MOD_GRAPHICS, "setEntityAnimation: spawn_id={} no '{}' class animation available (requested '{}')",
                 spawnId, animClass, animCode);
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

uint8_t EntityRenderer::getEntityPrimaryWeaponSkill(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return 255;  // WEAPON_NONE
    }
    return it->second.primaryWeaponSkill;
}

uint8_t EntityRenderer::getEntitySecondaryWeaponSkill(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return 255;  // WEAPON_NONE
    }
    return it->second.secondaryWeaponSkill;
}

bool EntityRenderer::isEntityPlayingThrough(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return false;
    }

    const EntityVisual& visual = it->second;
    if (!visual.isAnimated || !visual.animatedNode) {
        return false;
    }

    return visual.animatedNode->isPlayingThrough();
}

void EntityRenderer::stopEntityAnimation(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;
    if (visual.isAnimated && visual.animatedNode) {
        visual.animatedNode->stopAnimation();
        visual.currentAnimation.clear();
    }
}

std::string EntityRenderer::getEntityAnimation(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return "";
    }

    const EntityVisual& visual = it->second;
    if (visual.isAnimated && visual.animatedNode) {
        return visual.animatedNode->getCurrentAnimation();
    }
    return "";
}

bool EntityRenderer::hasEntityAnimation(uint16_t spawnId, const std::string& animCode) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return false;
    }

    const EntityVisual& visual = it->second;
    if (visual.isAnimated && visual.animatedNode) {
        return visual.animatedNode->hasAnimation(animCode);
    }
    return false;
}

std::vector<std::string> EntityRenderer::getEntityAnimationList(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return {};
    }

    const EntityVisual& visual = it->second;
    if (visual.isAnimated && visual.animatedNode) {
        return visual.animatedNode->getAnimationList();
    }
    return {};
}

void EntityRenderer::setGlobalAnimationSpeed(float speed) {
    // Clamp to reasonable range
    globalAnimationSpeed_ = std::max(0.1f, std::min(5.0f, speed));

    // Apply to all animated entities
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isAnimated && visual.animatedNode) {
            // setAnimationSpeed expects fps where 10 fps = 1.0x speed multiplier
            visual.animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed_);
        }
    }
}

void EntityRenderer::adjustGlobalAnimationSpeed(float delta) {
    setGlobalAnimationSpeed(globalAnimationSpeed_ + delta);
}

void EntityRenderer::adjustCorpseZOffset(float delta) {
    corpseZOffset_ += delta;

    LOG_DEBUG(MOD_ENTITY, "CORPSE Z DEBUG Adjusting corpse Z offset by {} -> new offset: {}", delta, corpseZOffset_);

    // Apply the delta to all existing corpses
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isCorpse && visual.sceneNode) {
            irr::core::vector3df pos = visual.sceneNode->getPosition();
            pos.Y += delta;  // Y is vertical in Irrlicht
            visual.sceneNode->setPosition(pos);
            visual.corpseYOffset += delta;
            LOG_DEBUG(MOD_ENTITY, "CORPSE Z DEBUG Entity {} new Y={} (corpseYOffset={})", spawnId, pos.Y, visual.corpseYOffset);
        }
    }
}

// Helm texture debugging functions
void EntityRenderer::setHelmDebugEnabled(bool enabled) {
    helmDebugEnabled_ = enabled;
    if (enabled) {
        // Store original UV coords for all QCM (race 71) entities' helm meshes
        helmOriginalUVs_.clear();

        for (auto& [spawnId, visual] : entities_) {
            if (visual.raceId != 71 || !visual.isAnimated || !visual.animatedNode) {
                continue;
            }

            irr::scene::IAnimatedMesh* animMesh = visual.animatedNode->getMesh();
            if (!animMesh) continue;

            irr::scene::IMesh* mesh = animMesh->getMesh(0);
            if (!mesh) continue;

            // Find helm mesh buffer (usually has "he" in texture name)
            for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
                if (!buffer) continue;

                irr::video::ITexture* tex = buffer->getMaterial().getTexture(0);
                if (!tex) continue;

                std::string texName = tex->getName().getPath().c_str();
                std::string lowerTexName = texName;
                std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                // Check if this is a helm texture (contains "he" after race code)
                if (lowerTexName.find("he") != std::string::npos &&
                    (lowerTexName.find("qcm") != std::string::npos ||
                     lowerTexName.find("hum") != std::string::npos)) {

                    HelmUVData uvData;
                    uvData.spawnId = spawnId;
                    uvData.bufferIndex = static_cast<int>(b);

                    // Store original UV coords
                    irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
                    irr::u32 vertCount = buffer->getVertexCount();
                    uvData.originalUVs.reserve(vertCount);

                    LOG_DEBUG(MOD_GRAPHICS, "HELM DEBUG: Found helm buffer for spawn {} (race {}) buffer {} tex={} verts={}",
                              spawnId, visual.raceId, b, texName, vertCount);

                    // Store UV coords for debugging
                    for (irr::u32 v = 0; v < vertCount; ++v) {
                        uvData.originalUVs.push_back(vertices[v].TCoords);
                        if (v < 8) {
                            LOG_TRACE(MOD_GRAPHICS, "  v{}: U={} V={}", v, vertices[v].TCoords.X, vertices[v].TCoords.Y);
                        }
                    }

                    helmOriginalUVs_.push_back(uvData);
                }
            }
        }

        LOG_DEBUG(MOD_GRAPHICS, "HELM DEBUG: Found {} helm mesh buffers", helmOriginalUVs_.size());
    }
}

void EntityRenderer::adjustHelmUOffset(float delta) {
    helmUOffset_ += delta;
    LOG_INFO(MOD_GRAPHICS, "Helm U offset: {}", helmUOffset_);
}

void EntityRenderer::adjustHelmVOffset(float delta) {
    helmVOffset_ += delta;
    LOG_INFO(MOD_GRAPHICS, "Helm V offset: {}", helmVOffset_);
}

void EntityRenderer::adjustHelmUScale(float delta) {
    helmUScale_ += delta;
    if (helmUScale_ < 0.1f) helmUScale_ = 0.1f;
    LOG_INFO(MOD_GRAPHICS, "Helm U scale: {}", helmUScale_);
}

void EntityRenderer::adjustHelmVScale(float delta) {
    helmVScale_ += delta;
    if (helmVScale_ < 0.1f) helmVScale_ = 0.1f;
    LOG_INFO(MOD_GRAPHICS, "Helm V scale: {}", helmVScale_);
}

void EntityRenderer::adjustHelmRotation(float delta) {
    helmRotation_ += delta;
    // Normalize to 0-360
    while (helmRotation_ >= 360.0f) helmRotation_ -= 360.0f;
    while (helmRotation_ < 0.0f) helmRotation_ += 360.0f;
    LOG_INFO(MOD_GRAPHICS, "Helm rotation: {} degrees", helmRotation_);
}

void EntityRenderer::toggleHelmUVSwap() {
    helmUVSwap_ = !helmUVSwap_;
    LOG_INFO(MOD_GRAPHICS, "Helm UV swap: {}", (helmUVSwap_ ? "ON" : "OFF"));
}

void EntityRenderer::toggleHelmVFlip() {
    helmVFlip_ = !helmVFlip_;
    LOG_INFO(MOD_GRAPHICS, "Helm V flip: {}", (helmVFlip_ ? "ON" : "OFF"));
}

void EntityRenderer::toggleHelmUFlip() {
    helmUFlip_ = !helmUFlip_;
    LOG_INFO(MOD_GRAPHICS, "Helm U flip: {}", (helmUFlip_ ? "ON" : "OFF"));
}

void EntityRenderer::resetHelmUVParams() {
    helmUOffset_ = 0.0f;
    helmVOffset_ = 0.0f;
    helmUScale_ = 1.0f;
    helmVScale_ = 1.0f;
    helmRotation_ = 0.0f;
    helmUVSwap_ = false;
    helmVFlip_ = false;
    helmUFlip_ = false;
    LOG_INFO(MOD_GRAPHICS, "Helm UV params reset to defaults");
}

void EntityRenderer::printHelmDebugState() const {
    LOG_INFO(MOD_GRAPHICS, "=== HELM DEBUG STATE ===");
    LOG_INFO(MOD_GRAPHICS, "  U offset: {}", helmUOffset_);
    LOG_INFO(MOD_GRAPHICS, "  V offset: {}", helmVOffset_);
    LOG_INFO(MOD_GRAPHICS, "  U scale:  {}", helmUScale_);
    LOG_INFO(MOD_GRAPHICS, "  V scale:  {}", helmVScale_);
    LOG_INFO(MOD_GRAPHICS, "  Rotation: {} degrees", helmRotation_);
    LOG_INFO(MOD_GRAPHICS, "  UV swap:  {}", (helmUVSwap_ ? "ON" : "OFF"));
    LOG_INFO(MOD_GRAPHICS, "  U flip:   {}", (helmUFlip_ ? "ON" : "OFF"));
    LOG_INFO(MOD_GRAPHICS, "  V flip:   {}", (helmVFlip_ ? "ON" : "OFF"));
    LOG_INFO(MOD_GRAPHICS, "  Helm buffers tracked: {}", helmOriginalUVs_.size());
    LOG_INFO(MOD_GRAPHICS, "========================");
}

void EntityRenderer::cycleHeadVariant(int direction) {
    // Find available head variants for QCM
    std::vector<std::string> availableVariants;

    if (raceModelLoader_) {
        // Get list of available head variants from the loader
        // For now, we'll try variants 0-10
        for (int i = 0; i <= 10; ++i) {
            availableVariants.push_back(std::to_string(i));
        }
    }

    if (availableVariants.empty()) {
        LOG_INFO(MOD_GRAPHICS, "No head variants available");
        return;
    }

    // Cycle the variant
    debugHeadVariant_ += direction;
    if (debugHeadVariant_ < -1) {
        debugHeadVariant_ = 10;
    } else if (debugHeadVariant_ > 10) {
        debugHeadVariant_ = -1;
    }

    LOG_INFO(MOD_GRAPHICS, "Head variant: {}", (debugHeadVariant_ == -1 ? "DEFAULT" : std::to_string(debugHeadVariant_)));

    // Reload all QCM entities with the new head variant
    for (auto& [spawnId, visual] : entities_) {
        if (visual.raceId != 71) {
            continue;
        }

        LOG_DEBUG(MOD_GRAPHICS, "  Reloading entity {} ({}) with head variant {}",
                  spawnId, visual.name, (debugHeadVariant_ == -1 ? "DEFAULT" : std::to_string(debugHeadVariant_)));

        // Get the appearance, override the face with our debug variant
        EntityAppearance newAppearance = visual.appearance;
        if (debugHeadVariant_ >= 0) {
            newAppearance.face = static_cast<uint8_t>(debugHeadVariant_);
        }

        // Remove old scene node
        if (visual.sceneNode) {
            visual.sceneNode->remove();
            visual.sceneNode->drop();  // Release our reference
            visual.sceneNode = nullptr;
        }
        if (visual.nameNode) {
            visual.nameNode->remove();
        }

        // Create new animated node with the new variant
        EQAnimatedMeshSceneNode* animNode = nullptr;
        if (raceModelLoader_) {
            animNode = raceModelLoader_->createAnimatedNodeWithEquipment(
                visual.raceId, visual.gender, newAppearance, smgr_->getRootSceneNode(), -1);
        }

        if (animNode) {
            visual.animatedNode = animNode;
            visual.sceneNode = animNode;
            visual.sceneNode->grab();  // Keep alive when removed from scene graph
            visual.meshNode = nullptr;
            visual.isAnimated = true;
            visual.usesPlaceholder = false;

            // Force animation update to get correct bounding box
            animNode->forceAnimationUpdate();

            // Calculate height offset - server Z is the geometric MODEL CENTER
            float scale = raceModelLoader_ ? raceModelLoader_->getRaceScale(visual.raceId) : 1.0f;
            irr::core::aabbox3df bbox = animNode->getBoundingBox();
            float bboxHeight = bbox.MaxEdge.Y - bbox.MinEdge.Y;
            // modelYOffset = 0 for rendering (server Z position works as-is)
            visual.modelYOffset = 0;
            // collisionZOffset = half model height for collision (server Z is center, feet are below)
            visual.collisionZOffset = (bboxHeight / 2.0f) * scale;

            // Restore position with center offset
            float irrlichtX = visual.lastX;
            float irrlichtY = visual.lastZ + visual.modelYOffset;  // EQ Z -> Irrlicht Y
            float irrlichtZ = visual.lastY;  // EQ Y -> Irrlicht Z
            animNode->setPosition(irr::core::vector3df(irrlichtX, irrlichtY, irrlichtZ));

            // Restore rotation (EQ heading to Irrlicht Y rotation)
            // Heading is stored in degrees (0-360) from eq.cpp 11-bit conversion
            // Must negate to convert between coordinate systems
            float visualHeading = -visual.lastHeading;
            animNode->setRotation(irr::core::vector3df(0, visualHeading, 0));
            if (visual.isPlayer) {
                LOG_DEBUG(MOD_GRAPHICS, "[ROT-HEADVAR] cycleHeadVariant PLAYER: lastHeading={:.1f} visualHeading={:.1f} node={}",
                          visual.lastHeading, visualHeading, (void*)animNode);
            }

            // Create name tag (text node above entity)
            // scale was already calculated above for modelYOffset
            float nameHeight = 5.5f * scale;
            visual.nameNode = smgr_->addTextSceneNode(
                nameFont_ ? nameFont_ : smgr_->getGUIEnvironment()->getBuiltInFont(),
                EQT::toDisplayNameW(visual.name).c_str(),
                visual.isPlayer ? irr::video::SColor(255, 100, 200, 255) : irr::video::SColor(255, 255, 255, 255),
                animNode,
                irr::core::vector3df(0, nameHeight, 0)
            );
            if (visual.nameNode) {
                visual.nameNode->setVisible(nameTagsVisible_);
            }

            // Start idle animation
            if (animNode->hasAnimation("o01")) {
                animNode->playAnimation("o01", true);
            }
            animNode->setAnimationSpeed(10.0f * globalAnimationSpeed_);

            // Print texture info for debugging
            irr::scene::IAnimatedMesh* mesh = animNode->getMesh();
            if (mesh) {
                irr::scene::IMesh* frame0 = mesh->getMesh(0);
                if (frame0) {
                    LOG_DEBUG(MOD_GRAPHICS, "    Mesh buffers: {}", frame0->getMeshBufferCount());
                    for (irr::u32 b = 0; b < frame0->getMeshBufferCount(); ++b) {
                        irr::scene::IMeshBuffer* buf = frame0->getMeshBuffer(b);
                        if (buf) {
                            irr::video::ITexture* tex = buf->getMaterial().getTexture(0);
                            if (tex) {
                                LOG_DEBUG(MOD_GRAPHICS, "    Buffer {}: {} ({} verts)", b, tex->getName().getPath().c_str(), buf->getVertexCount());
                            }
                        }
                    }
                }
            }
        } else {
            LOG_WARN(MOD_GRAPHICS, "    Failed to create animated node!");
        }
    }

    // Re-enable helm debug to capture new UVs
    if (helmDebugEnabled_) {
        setHelmDebugEnabled(true);
    }
}

void EntityRenderer::applyHelmUVTransform() {
    if (!helmDebugEnabled_ || helmOriginalUVs_.empty()) {
        return;
    }

    const float PI = 3.14159265358979f;
    float rotRad = helmRotation_ * PI / 180.0f;
    float cosR = std::cos(rotRad);
    float sinR = std::sin(rotRad);

    for (const auto& uvData : helmOriginalUVs_) {
        auto it = entities_.find(uvData.spawnId);
        if (it == entities_.end() || !it->second.animatedNode) {
            continue;
        }

        irr::scene::IAnimatedMesh* animMesh = it->second.animatedNode->getMesh();
        if (!animMesh) continue;

        irr::scene::IMesh* mesh = animMesh->getMesh(0);
        if (!mesh) continue;

        if (uvData.bufferIndex < 0 || static_cast<irr::u32>(uvData.bufferIndex) >= mesh->getMeshBufferCount()) {
            continue;
        }

        irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(uvData.bufferIndex);
        if (!buffer) continue;

        irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
        irr::u32 vertCount = buffer->getVertexCount();

        if (vertCount != uvData.originalUVs.size()) {
            LOG_WARN(MOD_GRAPHICS, "HELM DEBUG: Vertex count mismatch for spawn {}", uvData.spawnId);
            continue;
        }

        for (irr::u32 v = 0; v < vertCount; ++v) {
            float u = uvData.originalUVs[v].X;
            float vCoord = uvData.originalUVs[v].Y;

            // Apply U flip
            if (helmUFlip_) {
                u = 1.0f - u;
            }

            // Apply V flip (additional, on top of any existing flip)
            if (helmVFlip_) {
                vCoord = 1.0f - vCoord;
            }

            // Apply UV swap
            if (helmUVSwap_) {
                std::swap(u, vCoord);
            }

            // Center UVs for rotation (rotate around 0.5, 0.5)
            float uCentered = u - 0.5f;
            float vCentered = vCoord - 0.5f;

            // Apply rotation
            float uRotated = uCentered * cosR - vCentered * sinR;
            float vRotated = uCentered * sinR + vCentered * cosR;

            // Uncenter
            u = uRotated + 0.5f;
            vCoord = vRotated + 0.5f;

            // Apply scale (around center)
            u = (u - 0.5f) * helmUScale_ + 0.5f;
            vCoord = (vCoord - 0.5f) * helmVScale_ + 0.5f;

            // Apply offset
            u += helmUOffset_;
            vCoord += helmVOffset_;

            vertices[v].TCoords.X = u;
            vertices[v].TCoords.Y = vCoord;
        }

        // Mark buffer as dirty so Irrlicht knows to re-upload
        buffer->setDirty();
    }
}

// ============================================================================
// Entity Casting Management
// ============================================================================

void EntityRenderer::startEntityCast(uint16_t spawnId, uint32_t spellId, const std::string& spellName, uint32_t castTimeMs) {
    // Don't track casting for our own player (handled separately by player casting bar)
    // Check against stored playerSpawnId_ as primary check (in case isPlayer flag isn't set yet)
    if (spawnId == playerSpawnId_ && playerSpawnId_ != 0) {
        return;
    }

    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    // Also check the isPlayer flag as a backup
    if (visual.isPlayer) {
        return;
    }

    // Set casting state
    visual.isCasting = true;
    visual.castSpellId = spellId;
    visual.castSpellName = spellName;
    visual.castDurationMs = castTimeMs;
    visual.castStartTime = std::chrono::steady_clock::now();

    // Phase 6.2: Match casting animation speed to cast time
    // Play cast animation (t04 = cast start, t05 = channeling loop)
    if (visual.animatedNode) {
        // Start with cast pullback animation, then it will loop on t05
        if (visual.animatedNode->hasAnimation("t04")) {
            visual.animatedNode->playAnimation("t04", false, true);  // playThrough, no loop
            // Set duration to roughly match cast time (cast start is quick, most time in loop)
            visual.animatedNode->getAnimator().setTargetDuration(static_cast<float>(castTimeMs) * 0.2f);
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) started casting '{}' ({}ms)",
              spawnId, visual.name, spellName, castTimeMs);
}

void EntityRenderer::cancelEntityCast(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    if (visual.isCasting) {
        LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) cast interrupted", spawnId, visual.name);
    }

    // Clear casting state
    visual.isCasting = false;
    visual.castSpellId = 0;
    visual.castSpellName.clear();
    visual.castDurationMs = 0;
}

void EntityRenderer::completeEntityCast(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    if (visual.isCasting) {
        LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) completed casting '{}'",
                  spawnId, visual.name, visual.castSpellName);
    }

    // Clear casting state
    visual.isCasting = false;
    visual.castSpellId = 0;
    visual.castSpellName.clear();
    visual.castDurationMs = 0;
}

bool EntityRenderer::isEntityCasting(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return false;
    }
    return it->second.isCasting;
}

float EntityRenderer::getEntityCastProgress(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end() || !it->second.isCasting) {
        return 0.0f;
    }

    const EntityVisual& visual = it->second;
    if (visual.castDurationMs == 0) {
        return 1.0f;  // Instant cast
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - visual.castStartTime).count();

    return std::min(1.0f, static_cast<float>(elapsed) / visual.castDurationMs);
}

void EntityRenderer::updateEntityCastingBars(float deltaTime, irr::scene::ICameraSceneNode* camera) {
    if (!camera) {
        return;
    }

    // Check and auto-complete any casts that have timed out
    auto now = std::chrono::steady_clock::now();

    for (auto& [spawnId, visual] : entities_) {
        if (!visual.isCasting) {
            continue;
        }

        // Check if cast has completed (server should send action packet, but this is a safety check)
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - visual.castStartTime).count();

        // Give some extra time (500ms) before auto-completing to allow server response
        if (elapsed > visual.castDurationMs + 500) {
            // Cast timed out without server confirmation - clear state
            visual.isCasting = false;
            visual.castSpellId = 0;
            visual.castSpellName.clear();
            visual.castDurationMs = 0;
        }
    }
}

void EntityRenderer::renderEntityCastingBars(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                                              irr::scene::ICameraSceneNode* camera) {
    if (!driver || !camera || !smgr_) {
        return;
    }

    irr::gui::IGUIFont* font = gui ? gui->getBuiltInFont() : nullptr;

    // Get screen dimensions
    irr::core::dimension2du screenSize = driver->getScreenSize();
    int screenWidth = screenSize.Width;
    int screenHeight = screenSize.Height;

    // Casting bar dimensions (in pixels)
    const int BAR_WIDTH = 60;
    const int BAR_HEIGHT = 6;
    const int BAR_OFFSET_Y = -35;  // Offset above entity center (negative = up in screen space)

    // Colors
    const irr::video::SColor BAR_BG_COLOR(180, 40, 40, 40);
    const irr::video::SColor BAR_FILL_COLOR(255, 80, 180, 255);  // Blue/purple for casting
    const irr::video::SColor BAR_BORDER_COLOR(255, 100, 100, 100);
    const irr::video::SColor TEXT_COLOR(255, 255, 255, 255);

    for (auto& [spawnId, visual] : entities_) {
        // Skip if not casting, or if this is the player entity
        if (!visual.isCasting || visual.isPlayer || (spawnId == playerSpawnId_ && playerSpawnId_ != 0)) {
            continue;
        }

        // Get entity's 3D position
        irr::core::vector3df entityPos(visual.lastX, visual.lastZ + 8.0f, visual.lastY);  // Offset up for head height

        // Project to screen space
        irr::core::vector2di screenPos2D = smgr_->getSceneCollisionManager()->getScreenCoordinatesFrom3DPosition(
                entityPos, camera, false);

        // Check if position is valid (not offscreen)
        // getScreenCoordinatesFrom3DPosition returns (-1000, -1000) if behind camera
        if (screenPos2D.X == -1000 && screenPos2D.Y == -1000) {
            continue;  // Entity is behind camera
        }

        int x = screenPos2D.X;
        int y = screenPos2D.Y;

        // Check if on screen
        if (x < -BAR_WIDTH || x > screenWidth + BAR_WIDTH ||
            y < -50 || y > screenHeight + 50) {
            continue;
        }

        // Apply offset (move bar up above entity)
        y += BAR_OFFSET_Y;

        // Calculate progress
        float progress = getEntityCastProgress(spawnId);

        // Calculate bar position (centered on x)
        int barLeft = x - BAR_WIDTH / 2;
        int barTop = y - BAR_HEIGHT / 2;
        int barRight = barLeft + BAR_WIDTH;
        int barBottom = barTop + BAR_HEIGHT;

        // Draw background
        driver->draw2DRectangle(BAR_BG_COLOR,
            irr::core::recti(barLeft, barTop, barRight, barBottom));

        // Draw fill based on progress
        int fillWidth = static_cast<int>(BAR_WIDTH * progress);
        if (fillWidth > 0) {
            driver->draw2DRectangle(BAR_FILL_COLOR,
                irr::core::recti(barLeft, barTop, barLeft + fillWidth, barBottom));
        }

        // Draw border
        driver->draw2DLine(irr::core::position2di(barLeft, barTop),
                           irr::core::position2di(barRight, barTop), BAR_BORDER_COLOR);
        driver->draw2DLine(irr::core::position2di(barLeft, barBottom),
                           irr::core::position2di(barRight, barBottom), BAR_BORDER_COLOR);
        driver->draw2DLine(irr::core::position2di(barLeft, barTop),
                           irr::core::position2di(barLeft, barBottom), BAR_BORDER_COLOR);
        driver->draw2DLine(irr::core::position2di(barRight, barTop),
                           irr::core::position2di(barRight, barBottom), BAR_BORDER_COLOR);

        // Draw spell name below the bar
        if (font && !visual.castSpellName.empty()) {
            std::wstring spellNameW(visual.castSpellName.begin(), visual.castSpellName.end());
            int textY = barBottom + 2;
            irr::core::recti textRect(barLeft - 30, textY, barRight + 30, textY + 12);
            font->draw(spellNameW.c_str(), textRect, TEXT_COLOR, true, false);
        }
    }
}

// ============================================================================
// Spatial Grid Functions - for efficient O(1) visibility queries
// ============================================================================

void EntityRenderer::updateEntityGridPosition(uint16_t spawnId, float x, float y) {
    int64_t newCellKey = positionToGridKey(x, y);

    // Check if entity is already in this cell
    auto cellIt = entityGridCell_.find(spawnId);
    if (cellIt != entityGridCell_.end()) {
        if (cellIt->second == newCellKey) {
            // Already in correct cell, nothing to do
            return;
        }

        // Remove from old cell
        int64_t oldCellKey = cellIt->second;
        auto oldCellIt = spatialGrid_.find(oldCellKey);
        if (oldCellIt != spatialGrid_.end()) {
            oldCellIt->second.erase(spawnId);
            if (oldCellIt->second.empty()) {
                spatialGrid_.erase(oldCellIt);
            }
        }
    }

    // Add to new cell
    spatialGrid_[newCellKey].insert(spawnId);
    entityGridCell_[spawnId] = newCellKey;
}

void EntityRenderer::removeEntityFromGrid(uint16_t spawnId) {
    auto cellIt = entityGridCell_.find(spawnId);
    if (cellIt == entityGridCell_.end()) {
        return;
    }

    // Remove from spatial grid cell
    int64_t cellKey = cellIt->second;
    auto gridIt = spatialGrid_.find(cellKey);
    if (gridIt != spatialGrid_.end()) {
        gridIt->second.erase(spawnId);
        if (gridIt->second.empty()) {
            spatialGrid_.erase(gridIt);
        }
    }

    // Remove from cell tracking
    entityGridCell_.erase(cellIt);
}

void EntityRenderer::getEntitiesInRange(float centerX, float centerY, float range,
                                         std::vector<uint16_t>& outEntities) const {
    outEntities.clear();

    // Calculate which grid cells are within range
    // Need to check all cells that could contain entities within 'range' distance
    int32_t minCellX = static_cast<int32_t>(std::floor((centerX - range) / GRID_CELL_SIZE));
    int32_t maxCellX = static_cast<int32_t>(std::floor((centerX + range) / GRID_CELL_SIZE));
    int32_t minCellY = static_cast<int32_t>(std::floor((centerY - range) / GRID_CELL_SIZE));
    int32_t maxCellY = static_cast<int32_t>(std::floor((centerY + range) / GRID_CELL_SIZE));

    // Iterate over potentially relevant cells
    for (int32_t cellX = minCellX; cellX <= maxCellX; ++cellX) {
        for (int32_t cellY = minCellY; cellY <= maxCellY; ++cellY) {
            int64_t cellKey = (static_cast<int64_t>(cellX) << 32) | static_cast<uint32_t>(cellY);
            auto it = spatialGrid_.find(cellKey);
            if (it != spatialGrid_.end()) {
                // Add all entities in this cell - fine-grained distance check done by caller
                for (uint16_t spawnId : it->second) {
                    outEntities.push_back(spawnId);
                }
            }
        }
    }
}

// ============================================================================
// PVS-based Visibility Culling
// ============================================================================

void EntityRenderer::setBspTree(std::shared_ptr<BspTree> bspTree) {
    bspTree_ = bspTree;
    currentCameraRegionIdx_ = SIZE_MAX;
    currentCameraRegion_ = nullptr;
    LOG_DEBUG(MOD_GRAPHICS, "EntityRenderer: BSP tree set with {} regions",
              bspTree_ ? bspTree_->regions.size() : 0);
}

void EntityRenderer::clearBspTree() {
    bspTree_ = nullptr;
    currentCameraRegionIdx_ = SIZE_MAX;
    currentCameraRegion_ = nullptr;
    LOG_DEBUG(MOD_GRAPHICS, "EntityRenderer: BSP tree cleared");
}

void EntityRenderer::setConstrainedConfig(const ConstrainedRendererConfig* config) {
    constrainedConfig_ = config;
    if (config && config->enabled) {
        LOG_INFO(MOD_GRAPHICS, "EntityRenderer: Constrained mode enabled - max {} entities within {} units",
                 config->maxVisibleEntities, config->entityRenderDistance);
    } else {
        LOG_DEBUG(MOD_GRAPHICS, "EntityRenderer: Constrained mode disabled");
    }
}

void EntityRenderer::updateConstrainedVisibility(const irr::core::vector3df& cameraPos) {
    // If constrained mode is not enabled, show all entities
    if (!constrainedConfig_ || !constrainedConfig_->enabled) {
        visibleEntityCount_ = static_cast<int>(entities_.size());
        return;
    }

    const float maxDistance = constrainedConfig_->entityRenderDistance;
    const int maxEntities = constrainedConfig_->maxVisibleEntities;
    const float maxDistSq = maxDistance * maxDistance;

    // Build a list of entity distances from camera
    struct EntityDistance {
        uint16_t spawnId;
        float distanceSq;
    };
    std::vector<EntityDistance> entityDistances;
    entityDistances.reserve(entities_.size());

    for (auto& [spawnId, visual] : entities_) {
        if (!visual.sceneNode) continue;

        // Get entity position in Irrlicht coordinates (already stored this way)
        irr::core::vector3df entityPos = visual.sceneNode->getPosition();

        // Calculate squared distance to camera
        float dx = entityPos.X - cameraPos.X;
        float dy = entityPos.Y - cameraPos.Y;
        float dz = entityPos.Z - cameraPos.Z;
        float distSq = dx * dx + dy * dy + dz * dz;

        entityDistances.push_back({spawnId, distSq});
    }

    // Sort by distance (closest first)
    std::sort(entityDistances.begin(), entityDistances.end(),
              [](const EntityDistance& a, const EntityDistance& b) {
                  return a.distanceSq < b.distanceSq;
              });

    // Apply visibility limits
    visibleEntityCount_ = 0;
    for (size_t i = 0; i < entityDistances.size(); ++i) {
        const auto& ed = entityDistances[i];
        auto it = entities_.find(ed.spawnId);
        if (it == entities_.end()) continue;

        EntityVisual& visual = it->second;

        // Check if entity should be visible
        bool shouldBeVisible = (visibleEntityCount_ < maxEntities) && (ed.distanceSq <= maxDistSq);

        // Always show the player entity
        if (visual.isPlayer) {
            shouldBeVisible = true;
        }

        // Update scene graph membership (not just visibility)
        // This removes nodes entirely from the scene graph to skip traversal overhead
        if (visual.sceneNode) {
            if (shouldBeVisible && !visual.inSceneGraph) {
                // Add back to scene graph
                smgr_->getRootSceneNode()->addChild(visual.sceneNode);
                visual.sceneNode->setVisible(true);
                visual.inSceneGraph = true;
            } else if (!shouldBeVisible && visual.inSceneGraph) {
                // Remove from scene graph (but keep the node alive)
                visual.sceneNode->remove();
                visual.inSceneGraph = false;
            }
        }
        if (visual.nameNode) {
            if (shouldBeVisible && nameTagsVisible_) {
                if (!visual.nameNode->isVisible()) {
                    visual.nameNode->setVisible(true);
                }
            } else {
                if (visual.nameNode->isVisible()) {
                    visual.nameNode->setVisible(false);
                }
            }
        }

        if (shouldBeVisible) {
            visibleEntityCount_++;
        }
    }

    // Log when we're at capacity (but not every frame)
    static int logThrottle = 0;
    if (++logThrottle >= 60) {  // Log every ~60 frames
        logThrottle = 0;
        if (visibleEntityCount_ >= maxEntities) {
            LOG_DEBUG(MOD_GRAPHICS, "Constrained mode: showing {}/{} entities (at limit), {} in scene graph",
                      visibleEntityCount_, static_cast<int>(entities_.size()),
                      std::count_if(entities_.begin(), entities_.end(),
                                   [](const auto& p) { return p.second.inSceneGraph; }));
        }
    }
}

float EntityRenderer::findBoatDeckZ(float x, float y, float currentZ) const {
    // BEST_Z_INVALID from hc_map.h
    constexpr float BEST_Z_INVALID = -999999.0f;

    float bestDeckZ = BEST_Z_INVALID;
    float bestDistance = 999999.0f;

    // Check all entities with collision (boats)
    for (const auto& [spawnId, visual] : entities_) {
        if (!visual.hasCollision) {
            continue;
        }

        // Check horizontal distance to boat center
        float dx = x - visual.lastX;
        float dy = y - visual.lastY;
        float horizontalDist = std::sqrt(dx * dx + dy * dy);

        // If within boat's collision radius
        if (horizontalDist <= visual.collisionRadius) {
            // Deck Z is at the top of the boat model
            float deckZ = visual.lastZ + (visual.collisionHeight / 2.0f);
            // Bottom of boat (for checking if we're within the boat's vertical range)
            float boatBottomZ = visual.lastZ - (visual.collisionHeight / 2.0f);

            // Allow standing on deck if:
            // 1. We're on the deck (within small tolerance above)
            // 2. We're below the deck but above the bottom of the boat (inside the boat's height)
            // 3. We're slightly below the boat (allowing step up from water)
            float zDiff = currentZ - deckZ;
            // Allow up to 2 units above deck, or anywhere from deck down to 10 units below boat bottom
            // This allows stepping onto boats from water level
            if (zDiff <= 2.0f && currentZ >= (boatBottomZ - 10.0f)) {
                // This boat is a valid floor - check if it's closer than previous best
                if (horizontalDist < bestDistance) {
                    bestDeckZ = deckZ;
                    bestDistance = horizontalDist;
                }
            }
        }
    }

    return bestDeckZ;
}

} // namespace Graphics
} // namespace EQT
