#include "client/graphics/entity_renderer.h"
#include "client/graphics/corpse_death_pose.h"  // T456 (B477): the ONE death-pose ladder
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
#include "client/core/memory_ledger.h"
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

bool EntityRenderer::registerEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                                     float x, float y, float z, float heading, bool isPlayer,
                                     uint8_t gender, const EntityAppearance& appearance, bool isNPC,
                                     bool isCorpse, float serverSize, uint8_t entityLevel) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityInstances);  // T491
    // B1148: a corpse on the fade list is an actor whose spawn is gone; the
    // client keeps the two apart, fable keys both by id, so a new spawn of the
    // same id ends the old actor's fade early.
    if (auto fading = entities_.find(spawnId); fading != entities_.end() && fading->second.isFading) {
        removeEntity(spawnId);
    }
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

    // T318: a real field. This used to be smuggled through collisionHeight and
    // cleared by the mesh build, so nothing could rescale the entity afterwards.
    visual.serverSize = serverSize;

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
                // The placeholder cube is authored with its origin ON ITS BASE
                // (createPlaceholderMesh() spans Y = [0, size]), so its
                // origin-to-feet distance is zero. Derived from the bounds like
                // every other entity rather than assumed from the shape.
                visual.collisionZOffset =
                    collisionZOffsetFromBounds(visual.meshNode->getBoundingBox(), 1.0f);

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
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityInstances);  // T491
    // T504: owner labels for WILLEQ_LEDGER_OWNERS=entity_instances (no effect
    // otherwise -- the rows are only charged for the selected category).
    eqt::memory::StartupScope ownerScope("ent.lookup");
    auto entOwner = [](const char* name) {
        if (eqt::memory::enabled()) {
            eqt::memory::detail::t_sublabel = eqt::memory::startupSublabelIndex(name);
        }
    };
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
        // T504b: this is the render thread. If the model is not loaded yet,
        // keep the placeholder, hand the entity to the prep worker and let
        // the build step finish it when the result is promoted -- the same
        // path a late spawn takes. The loader would otherwise parse here.
        // B973: NOT for a race/gender the loader has proven absent (the prep
        // worker's negative cache): its prep "completes" in 0 ms with no
        // model, the build step comes straight back here, and the entity was
        // deferred again -- forever, taking the nearest-first build slot every
        // time (measured: 6 Crowd spawns of race 130, 400+ deferrals each in
        // one x86 run). It falls through to the synchronous build below,
        // which gives it its placeholder model for good.
        const uint32_t modelKey = (static_cast<uint32_t>(visual.raceId) << 8) | visual.gender;
        if (visual.usesPlaceholder && visual.sceneNode
            && !raceModelLoader_->isModelReady(visual.raceId, visual.gender)
            && !raceModelLoader_->isRaceModelKnownMissing(modelKey)) {
            if (!entityPrepWorker_->isPendingForEntity(spawnId)) {
                EntityPrepWorker::PrepRequest req;
                req.spawnId = spawnId;
                req.raceId = visual.raceId;
                req.gender = visual.gender;
                req.appearance = visual.appearance;
                req.bspRegion = visual.cachedBspRegion;
                entityPrepWorker_->requestPrep(req);
            }
            visual.buildPhase = EntityBuildPhase::Placeholder;
            visual.entityPrepComplete = false;
            LOG_INFO(MOD_GRAPHICS, "[srcchar] entity {} ({}) race={} gender={} deferred to the prep worker",
                     spawnId, visual.name, visual.raceId, visual.gender);
            return true;   // the entity exists (as its placeholder) and will be upgraded
        }
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

    // B1172: the official client's actor scale (actor_scale.h), not the
    // race_models.json scale column -- that column only gates invisibility.
    // B1176: the local player included (the client has no local-player
    // branch); `isPlayer` no longer enters the scale.
    float serverSize = visual.serverSize;
    scale = entityNodeScale(raceId, gender, serverSize);

    LOG_DEBUG(MOD_GRAPHICS, "buildEntityMesh[{}]: race={} gender={} size {:.2f} -> actor scale {:.3f}",
              name, raceId, gender, serverSize, scale);

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
            entOwner("ent.animnode");
            animNode = raceModelLoader_->createAnimatedNodeWithEquipment(
                raceId, gender, appearance, smgr_->getRootSceneNode(), -1, compositeEligible);
        } else {
            entOwner("ent.animnode");
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

        // The node origin IS the server Z (modelYOffset is 0); see the
        // Z-convention note in entity_renderer.h. collisionZOffset is the
        // ORIGIN-TO-FEET distance -- NOT half the bbox height. For a Human male
        // those are 3.00 and 3.34, so the half-height form stood every entity
        // 0.33 above its own collision ground (T310 Leg B).
        irr::core::aabbox3df bbox = animNode->getBoundingBox();
        float bboxHeight = bbox.MaxEdge.Y - bbox.MinEdge.Y;
        visual.modelYOffset = 0;
        // B1176: the local player sits at the client's own placement
        // (ground + ROffset x scale); every other entity keeps the measured
        // bbox origin-to-feet distance (entityNodeCollisionZOffset).
        visual.collisionZOffset =
            entityNodeCollisionZOffset(isPlayer, bbox, scale, raceId, gender, serverSize);

        // Debug: log bounding box info for entity creation
        LOG_DEBUG(MOD_GRAPHICS, "createEntity[{}]: race={} bbox Y=[{:.2f},{:.2f}] height={:.2f} scale={:.3f} collisionZOffset={:.2f}",
			name, raceId, bbox.MinEdge.Y, bbox.MaxEdge.Y, bboxHeight, scale, visual.collisionZOffset);

	// Set position (convert EQ Z-up to Irrlicht Y-up)
	// The node origin goes at the server Z; modelYOffset is a rendering nudge (0)
	animNode->setPosition(irr::core::vector3df(x, z + visual.modelYOffset, y));

	// Set rotation (EQ heading to Irrlicht Y rotation)
	// Heading arrives already in degrees (0-360) from eq_entities.cpp 11-bit conversion
	// EQ heading: 0=North(+Y), increases clockwise
	// Irrlicht Y rotation: 0=+Z, increases counter-clockwise
	// Must negate to convert between coordinate systems
	float visualHeading = -heading;
	animNode->setRotation(irr::core::vector3df(0, visualHeading, 0));
	if (isPlayer) {
		LOG_DEBUG(MOD_GRAPHICS, "[ROT-CREATE] createEntity PLAYER: spawnId={} heading={:.1f} visualHeading={:.1f} node={}",
				spawnId, heading, visualHeading, (void*)animNode);
	}

	entOwner("ent.setup");
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

		entOwner("ent.nametag");
		visual.nameNode = createNameTagNode(  // B975: drawn by the name-tag batch
				animNode, wname,
				isPlayer ? irr::video::SColor(255, 100, 200, 255) : irr::video::SColor(255, 255, 255, 255),
				irr::core::vector3df(0, nameHeight, 0));
	}

	// Track race model reference for cache eviction
	if (raceModelLoader_) {
		raceModelLoader_->addMeshRef(raceId, gender);
	}

	// Attach equipment models to bone attachment points
	entOwner("ent.equipment");
	attachEquipment(visual);
	entOwner("ent.after");

	// If this is a corpse, set the death animation and hold on last frame.
	// T456 (board B477): the ladder itself lives in ONE place now --
	// ApplyCorpseDeathPose() -- which the async build path
	// (processOneEntityBuildStep's MeshFinalize phase) calls too. It used to
	// be duplicated textually there, and only this copy was ever tested.
	if (isCorpse) {
		ApplyCorpseDeathPose(visual, animNode);
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

    // The node origin IS the server Z; collisionZOffset is the origin-to-feet
    // distance. See the Z-convention note in entity_renderer.h. B1176: the
    // local player takes the client's ROffset placement (entityNodeCollisionZOffset).
    irr::core::aabbox3df bbox = mesh->getBoundingBox();
    visual.modelYOffset = 0;
    visual.collisionZOffset =
        entityNodeCollisionZOffset(visual.isPlayer, bbox, scale, raceId, gender, serverSize);

    // Set position (convert EQ Z-up to Irrlicht Y-up)
    // The node origin goes at the server Z; modelYOffset is a rendering nudge (0)
    visual.meshNode->setPosition(irr::core::vector3df(x, z + visual.modelYOffset, y));

    // Set rotation (EQ heading to Irrlicht Y rotation)
    // Heading arrives already in degrees (0-360) from eq_entities.cpp 11-bit conversion
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

        visual.nameNode = createNameTagNode(  // B975: drawn by the name-tag batch
            visual.meshNode, wname,
            isPlayer ? irr::video::SColor(255, 100, 200, 255) : irr::video::SColor(255, 255, 255, 255),
            irr::core::vector3df(0, nameHeight, 0));
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
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityInstances);  // T491
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

} // namespace Graphics
} // namespace EQT
