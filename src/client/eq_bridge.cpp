// eq_bridge.cpp
//
// EverQuest <-> renderer bridge marshaling layer, split out of eq.cpp (F10).
// Contains graphics lifecycle (Init/Shutdown), the per-frame tick methods,
// zone-load snapshot creation, spawn/pet bridge helpers, the bridge
// intent/event pumps (ProcessBridgeIntents / ProcessBridgeEvents),
// inventory stat sync, the loot-window UI flow (which rides the bridge),
// hotbar config persistence, and audio init/playback helpers.
//
// This is a pure move of EverQuest method definitions out of eq.cpp; no
// behavior or interface changes. See refactor/F10_bridge.md for the
// moved/left mapping.

#include "client/eq.h"
#include "client/inspect_answer.h"
#include "client/willeq_database.h"
#include "client/pathfinder_interface.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/hc_map.h"
#include "client/combat.h"
#include "client/trade_manager.h"
#include "client/world_object.h"
#include "client/spell/spell_manager.h"
#include "client/spell/cast_refusal.h"  // T244: official refusal strings
#include "client/spell/buff_manager.h"
#include "client/spell/spell_effects.h"
#include "client/spell/spell_type_processor.h"
#include "client/skill/skill_manager.h"
#include "client/zone_lines.h"
#include "client/formatted_message.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"

// P28: entity_pose_state.h, wld_loader.h, and the inventory/chat/hotbar/
// command-registry data-model headers have no renderer dependency — used
// unconditionally below (OnSpawnAddedGraphics's EntityPoseState, the
// Player-mode inventory/loot/vendor/bank/hotbar methods). Only the
// genuinely renderer-only headers (used solely inside InitGraphics/
// ShutdownGraphics below) stay gated.
#include "client/state/entity_pose_state.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/ui/command_registry.h"
#include "client/hotbar/hotbar_model.h"
#include "common/util/json_config.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_config_loader.h"
#include "client/graphics/weather_quality_preset.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/input/hotkey_manager.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/seasonal_controller.h"
#include "client/graphics/environment/spell_effects_config.h"
#endif

#include <limits>

#include "client/bridge/game_state_bridge.h"
#include "client/bridge/irrlicht_bridge.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/zone_audio_manager.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_assets.h"
#include "client/audio/door_sounds.h"
#include "client/audio/water_sounds.h"
#endif

#include "common/net/posix_udp_transport.h"
#include "client/animation_constants.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>
#include <map>
#include <tuple>
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef EQT_HAS_GRAPHICS
// Graphics renderer integration methods

bool EverQuest::InitGraphics(int width, int height,
                             EQT::Graphics::IrrlichtRenderer* renderer,
                             eqt::bridge::GameStateBridge* bridge) {
	if (m_graphics_initialized) {
		return true;
	}

	if (m_eq_client_path.empty()) {
		LOG_ERROR(MOD_GRAPHICS, "EQ client path not set. Call SetEQClientPath() first.");
		return false;
	}

	// D20e3: Store bridge only — renderer is owned by Application.
	// T46: wireBridgeToSubsystems() sets m_bridge and also wires the four
	// managers' own bridge pointers (previously done inline here only,
	// which left the managers unwired headless -- setBridge() now calls
	// the same helper so both paths agree).
	wireBridgeToSubsystems(bridge);

	// U07b: Wire hotbar model's save callback (bridge pointer itself is
	// wired by wireBridgeToSubsystems() above).
	if (m_hotbar_model) {
		m_hotbar_model->setSaveCallback([this]() { SaveHotbarConfig(); });
	}

	EQT::Graphics::RendererConfig config;
	config.width = width;
	config.height = height;
	config.eqClientPath = m_eq_client_path;
	config.regionMapsPath = m_region_maps_path;
	config.windowTitle = "WillEQ - " + m_character;
	config.lighting = false;
	config.constrainedPreset = m_constrained_preset;
	if (m_constrained_config) {
		config.constrainedPreset = EQT::Graphics::ConstrainedRenderingPreset::Custom;
		config.constrainedConfig = *m_constrained_config;
	}

	if (!renderer->initLoadingScreen(config)) {
		LOG_ERROR(MOD_GRAPHICS, "Failed to initialize renderer loading screen");
		m_bridge = nullptr;
		return false;
	}

	// Pump network after Irrlicht device creation (can take 0.5-1s on ARM)
	TickNetwork();

	// Set collision map via bridge (shared_ptr copy, P35 — see eq.h)
	if (m_zone_map && m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CollisionMapChanged,
			eqt::state::CollisionMapChangedData{m_zone_map}));
	}

	// One-time deferred renderer init (render distance, spell VFX)
	if (renderer) {
		renderer->initDeferredGraphics();
	}

	// Setup window callbacks (all empty stubs since D20a/D20b1)
	SetupLootCallbacks();
	SetupVendorCallbacks();
	SetupBankCallbacks();
	SetupTrainerCallbacks();
	SetupTradeWindowCallbacks();
	SetupTradeskillCallbacks();

	// S2: movement callback removed — position flows via PlayerPositionChanged intent only

	m_graphics_initialized = true;
	LOG_INFO(MOD_GRAPHICS, "Renderer initialized successfully ({}x{})", width, height);

#ifdef WITH_AUDIO
	InitializeAudio();
#endif

	// If zone is already fully connected when graphics init, set zone ready
	if (IsFullyZonedIn() && m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::NetworkReady,
			eqt::state::NetworkReadyData{static_cast<uint32_t>(m_entities.size()), true}));
		LOG_DEBUG(MOD_GRAPHICS, "Zone already ready, enabling rendering with {} entities", m_entities.size());
	}

	return true;
}

// SignalGraphicsLoadReady removed — loading thread starts directly from
// Application::gameThreadLoop when ConsumeZoneLoadRequest fires.

void EverQuest::ShutdownGraphics() {
	// D20e2: Loading thread now owned by Application — it joins before calling this.

	if (m_inventory_manager) {
		m_inventory_manager->clearAll();
		m_inventory_manager.reset();
	}

	// U07c3: Detach bridge — Application owns renderer lifecycle
	// T47: no longer clears m_bridge here. Application::shutdown() now calls
	// setBridge(nullptr) unconditionally, in both graphics and true-headless
	// builds, before this function runs (see application.cpp) -- that single
	// call clears m_bridge here and all four managers' bridge pointers at
	// once via wireBridgeToSubsystems(). Duplicating the assignment here
	// would be harmless (idempotent, plain pointer assignment) but would
	// leave two competing partial-clear paths -- one call site is the single
	// source of truth for bridge-pointer clearing now.
	m_loading_status_ptr = nullptr;
	m_graphics_initialized = false;
	LOG_DEBUG(MOD_GRAPHICS, "Graphics detached");
}
#endif // EQT_HAS_GRAPHICS

// T46: wireBridgeToSubsystems()/setBridge() are unconditional (not gated by
// EQT_HAS_GRAPHICS) because the headless path (Application::init()'s
// console-bridge branch, application.cpp) calls setBridge() directly and
// never goes through InitGraphics() at all. Previously setBridge() (then an
// inline one-liner in eq.h) set only m_bridge; the four managers
// (InventoryManager/SpellManager/SkillManager/HotbarModel), each holding its
// own separate bridge pointer, were wired only inside InitGraphics() below —
// a graphics-only call path. That left every manager-originated bridge
// publish (e.g. SpellManager's SpellGemChanged, the T44 spell-delta
// conversion depends on) silently inert headless. Both call sites now route
// through this single helper so the managers are wired whichever path sets
// the bridge.
void EverQuest::wireBridgeToSubsystems(eqt::bridge::GameStateBridge* bridge) {
	m_bridge = bridge;

	// Plain pointer assignment on each manager's own setBridge() -- safe to
	// call repeatedly (idempotent) and safe to call with bridge == nullptr
	// (propagates the clear rather than silently dropping it), though
	// nothing calls this with nullptr today (see deferred.md/T46 Review for
	// the teardown-lifecycle finding).
	if (m_inventory_manager) {
		m_inventory_manager->setBridge(bridge);
	}
	if (m_spell_manager) {
		m_spell_manager->setBridge(bridge);
	}
	if (m_skill_manager) {
		m_skill_manager->setBridge(bridge);
	}
	if (m_hotbar_model) {
		m_hotbar_model->setBridge(bridge);
	}
}

void EverQuest::setBridge(eqt::bridge::GameStateBridge* bridge) {
	wireBridgeToSubsystems(bridge);
}

// P28: GameTick/PostRenderTick and everything below through the
// end of this file (spawn/pet bridge callbacks, ProcessBridgeIntents/Events,
// inventory stats, loot/vendor/bank/trainer window methods, hotbar config
// persistence) are plain game-thread/bridge logic called unconditionally by
// Application regardless of EQT_HAS_GRAPHICS — un-guarded to match their
// unconditional eq.h declarations. Only InitGraphics/ShutdownGraphics above
// and CreateZoneLoadSnapshot below (its own narrower guard) are genuinely
// renderer-lifetime-only.
// D20c1: Pre-render game state tick — spell/buff managers + bridge
// D21a: Game-side tick — runs on game thread (spell/buff updates + intent processing)
void EverQuest::GameTick(float deltaTime) {
#ifdef EQT_HAS_GRAPHICS
	// T45: was `if (!m_graphics_initialized) return;`. m_graphics_initialized
	// gates on the renderer subsystem existing, not on the game being ready,
	// and is permanently false in a --no-graphics run (InitGraphics() is
	// never called), so this second gate independently blocked headless too.
	// IsGraphicsReady() (loading phase reached COMPLETE) is the same
	// game-lifecycle readiness signal Application::updateGameState() now
	// OR's into its own call-site gate -- kept consistent with that
	// condition here rather than removed, since Application's own comment
	// notes GameTick() is deliberately gated twice.
	if (!m_graphics_initialized && !IsGraphicsReady()) return;
#endif

	// Update spell manager (cooldowns, memorization progress, cast timeouts)
	if (m_spell_manager) {
		m_spell_manager->update(deltaTime);
	}

	// Update buff manager (buff durations, expirations)
	if (m_buff_manager) {
		m_buff_manager->update(deltaTime);
	}

	// Process intents from renderer's previous frame
	ProcessBridgeIntents();
}

// T220: PreRenderTick() (D21a's render-thread event drain) is DELETED. In a
// graphics run the bridge drain is now two-stage and owned by Application:
// updateGameState() -> GameStateSyncBridge::stagePendingEvents() on the
// game thread, render() -> GameStateSyncBridge::forwardStagedEvents() on
// the render thread. ProcessBridgeEvents() below remains the single-stage
// drain for headless runs (and single-threaded test harnesses).

// D20c1: Post-render tick — audio sync, progressive loading check
void EverQuest::PostRenderTick(float deltaTime) {
#ifdef EQT_HAS_GRAPHICS
	if (!m_graphics_initialized) return;
#endif

#ifdef WITH_AUDIO
	// D20c2: Audio listener at player position, not camera position.
	// The player's ears are at the player, not the camera. In third-person
	// the camera can be far behind — placing audio there makes nearby
	// NPCs sound distant. Player position is game state, no renderer query.
	if (m_audio_manager) {
		// Convert EQ heading (0-512 clockwise from north) to forward vector
		float headingRad = m_heading * (2.0f * static_cast<float>(M_PI)) / 512.0f;
		float forwardX = std::sin(headingRad);
		float forwardY = -std::cos(headingRad);
		float forwardZ = 0.0f;

		m_audio_manager->setListenerPosition(
			glm::vec3(m_x, m_y, m_z),
			glm::vec3(forwardX, forwardY, forwardZ),
			glm::vec3(0.0f, 0.0f, 1.0f)  // Up is +Z in EQ coordinates
		);

		if (m_zone_audio_manager && m_loading_phase == LoadingPhase::COMPLETE) {
			m_zone_audio_manager->update(deltaTime, glm::vec3(m_x, m_y, m_z), m_is_daytime);
		}

		// D20c2: Volume hotkeys handled via intents from renderer
		// (MusicVolumeChangeIntent / EffectsVolumeChangeIntent in ProcessBridgeIntents)
	}
#else
	(void)deltaTime;
#endif
}

#ifdef EQT_HAS_GRAPHICS

// D20c3: Create immutable snapshot of game state for zone loading thread
eqt::ZoneLoadSnapshot EverQuest::CreateZoneLoadSnapshot() const {
	eqt::ZoneLoadSnapshot snap;
	snap.zoneName = m_current_zone_name;
	snap.zoneId = m_current_zone_id;
	snap.playerX = m_x;
	snap.playerY = m_y;
	snap.playerZ = m_z;
	snap.playerHeading = m_heading;
	snap.playerSpawnId = m_my_spawn_id;
	snap.playerName = m_character;
	snap.playerLevel = m_level;
	snap.skyType = m_zone_sky_type;
	snap.zoneType = m_zone_type;
	for (int i = 0; i < 4; i++) {
		snap.fogRed[i] = m_zone_fog_red[i];
		snap.fogGreen[i] = m_zone_fog_green[i];
		snap.fogBlue[i] = m_zone_fog_blue[i];
		snap.fogMinClip[i] = m_zone_fog_minclip[i];
		snap.fogMaxClip[i] = m_zone_fog_maxclip[i];
	}
	// Copy entities
	snap.entities.reserve(m_entities.size());
	for (const auto& [id, entity] : m_entities) {
		snap.entities.push_back(entity);
	}
	// Copy doors
	for (const auto& [id, door] : m_game_state.doors().getAllDoors()) {
		snap.doors.push_back(door);
	}
	// Zone lines (expand geometry + extract bounding boxes)
	if (m_zone_lines && m_zone_lines->hasZoneLines()) {
		if (m_zone_map) {
			m_zone_lines->expandZoneLinesToGeometry(m_zone_map.get());
		}
		snap.zoneLineBBoxes = m_zone_lines->getZoneLineBoundingBoxes();
	}
	snap.zoneMap = m_zone_map;
	snap.eqClientPath = m_eq_client_path;

	// P04: zoneArchive/zoneWld snapshot fields removed (never read; see
	// zone_load_snapshot.h) — LoadZoneSources now releases both right after
	// BSP extraction.
	// P17: only trust m_zone_bsp_tree if it was captured for the CURRENT
	// zone. A stale (previous-zone) tree can still be resident here if the
	// game thread's graphicsCompleteReady finalization for a just-completed
	// load (SetZoneBspTree()) lands in the same/adjacent iteration as a
	// fast re-zone's CleanupZone() reset -- observed by P15 as a FATAL with
	// the *target* zone's position paired with the *source* zone's stale
	// name/BSP tree ("Player BSP region is SIZE_MAX" with a mismatched
	// zone= in the log). Falling back to "no tree yet" (leaving
	// playerBspRegion at its SIZE_MAX default, same as the pre-existing
	// null-tree case just below) is always safe: the loading thread builds
	// a fresh BSP tree from the new zone's own map data regardless of what
	// this snapshot carries.
	// T260: the tag is now written by LoadZoneSources() too, so this branch
	// is the normal zone-in path again (P17..T260 it never ran: the tag was
	// empty and every zone-in fell through to the WARN below). A SIZE_MAX
	// result here is a legitimate outcome, not a crash: start positions can
	// sit outside every BSP region (P44's befallen zone-in at x=-195,y=-982
	// probes SIZE_MAX against befallen's OWN tree), nothing consumes
	// playerBspRegion, and the renderer treats a SIZE_MAX camera region as
	// "no PVS" everywhere. The former LOG_FATAL + std::exit(1) was the only
	// effect the lookup ever had.
	if (m_zone_bsp_tree && m_zone_bsp_tree_zone_name == m_current_zone_name) {
		snap.playerBspRegion = m_zone_bsp_tree->findRegionIndexForPoint(m_x, m_y, m_z);
		if (snap.playerBspRegion == SIZE_MAX) {
			LOG_WARN(MOD_MAIN, "Snapshot: player BSP region is SIZE_MAX (start position outside every "
			         "BSP region) pos=({:.2f},{:.2f},{:.2f}) zone={} -- continuing, nothing consumes it (T260)",
			         m_x, m_y, m_z, m_current_zone_name);
		} else {
			LOG_INFO(MOD_MAIN, "Snapshot: player BSP region={} pos=({:.2f},{:.2f},{:.2f}) zone={}",
			         snap.playerBspRegion, m_x, m_y, m_z, m_current_zone_name);
		}
	} else if (m_zone_bsp_tree) {
		LOG_WARN(MOD_MAIN, "Snapshot: m_zone_bsp_tree is stale (captured for '{}', current zone '{}') -- "
		         "skipping region lookup, falling back to no-tree-yet (P17)",
		         m_zone_bsp_tree_zone_name, m_current_zone_name);
	}

	return snap;
}

void EverQuest::SetZoneBspTree(std::shared_ptr<EQT::Graphics::BspTree> bsp, const std::string& builtForZone) {
	if (bsp && builtForZone != m_current_zone_name) {
		// The finalization of a load for a zone we have already left. Keep
		// whatever LoadZoneSources() built for the current zone (or nothing).
		LOG_WARN(MOD_MAIN, "SetZoneBspTree: rejecting tree built for '{}' (current zone '{}') -- "
		         "late finalization of a previous load, keeping the current zone's tree (T260)",
		         builtForZone, m_current_zone_name);
		return;
	}
	m_zone_bsp_tree = std::move(bsp);
	m_zone_bsp_tree_zone_name = m_zone_bsp_tree ? builtForZone : std::string();
}

// D20e2: StartLoadingThread, JoinLoadingThread moved to Application

// D20e2: LoadZoneGraphicsOnLoadingThread and LoadZoneGraphics moved to Application
#endif

// NOTE: OnZoneLoadedGraphics() has been removed. Graphics loading is now handled
// by LoadZoneGraphics() which is called from OnGameStateComplete() when the
// game state is ready. This ensures the progress bar shows during the entire
// loading process from login to gameplay.

void EverQuest::OnSpawnAddedGraphics(const Entity& entity) {
	if (!m_bridge) {
		return;
	}

	// Only create graphics entities if loading is complete.
	// During initial zone load, entities are stored in m_entities and created
	// in bulk by LoadZoneGraphics(). This function is for NEW spawns that
	// appear after the zone is fully loaded (players zoning in, NPC respawns).
	if (m_loading_phase != LoadingPhase::COMPLETE) {
		LOG_TRACE(MOD_ENTITY, "Skipping entity {} ({}) graphics - loading phase {} not complete",
		          entity.spawn_id, entity.name, static_cast<int>(m_loading_phase));
		return;
	}

	// Skip our own player - will be created in ZoneProcessGuildMOTD after fully connected
	// This ensures all player data (inventory, appearance, etc.) has been received first
	if (entity.name == m_character) {
		LOG_DEBUG(MOD_ENTITY, "Skipping player entity {} ({}) in OnSpawnAddedGraphics - will create after fully connected",
		          entity.spawn_id, entity.name);
		return;
	}

	// npc_type: 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
	bool isCorpse = (entity.npc_type == 2 || entity.npc_type == 3);

	// Fallback: Also detect corpse by name (server adds "'s corpse" or "_corpse" suffix)
	if (!isCorpse && entity.name.find("corpse") != std::string::npos) {
		isCorpse = true;
		LOG_TRACE(MOD_ENTITY, "Entity {} ({}) detected as corpse by name (npc_type={})", entity.spawn_id, entity.name, (int)entity.npc_type);
	}

	// Publish EntitySpawned event to bridge (D17b: bridge is sole path to renderer)
	eqt::state::EntitySpawnedData data;
	data.spawnId = entity.spawn_id;
	data.name = entity.name;
	data.x = entity.x;
	data.y = entity.y;
	data.z = entity.z;
	data.heading = entity.heading;
	data.raceId = entity.race_id;
	data.classId = entity.class_id;
	data.level = entity.level;
	data.gender = entity.gender;
	data.npcType = entity.npc_type;
	data.isCorpse = isCorpse;
	data.isPlayer = (entity.spawn_id == m_my_spawn_id);
	data.serverSize = entity.size;
	data.face = entity.face;
	data.haircolor = entity.haircolor;
	data.hairstyle = entity.hairstyle;
	data.beardcolor = entity.beardcolor;
	data.beard = entity.beard;
	data.texture = entity.equip_chest2;
	data.helm = entity.helm;
	for (int i = 0; i < 9; i++) {
		data.equipment[i] = entity.equipment[i];
		data.equipmentTint[i] = entity.equipment_tint[i];
	}
	m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::EntitySpawned, std::move(data)));

	// Set entity light source if equipped
	if (entity.light > 0) {
		eqt::state::EntityLightChangedData lightData;
		lightData.spawnId = entity.spawn_id;
		lightData.lightLevel = entity.light;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::EntityLightChanged, std::move(lightData)));
	}

	// Set initial pose state from spawn animation value
	if (!isCorpse && entity.animation != 0) {
		uint8_t poseState = static_cast<uint8_t>(
			eqt::state::EntityPoseState::Standing);
		std::string animCode;
		bool setPose = false;

		if (entity.animation == ANIM_SITTING) {
			poseState = static_cast<uint8_t>(
				eqt::state::EntityPoseState::Sitting);
			animCode = "p02";
			setPose = true;
		} else if (entity.animation == ANIM_CROUCHING) {
			poseState = static_cast<uint8_t>(
				eqt::state::EntityPoseState::Crouching);
			animCode = "l08";
			setPose = true;
		} else if (entity.animation == ANIM_LYING) {
			poseState = static_cast<uint8_t>(
				eqt::state::EntityPoseState::Lying);
			animCode = "d05";
			setPose = true;
		}

		if (setPose) {
			eqt::state::EntityPoseStateChangedData poseData;
			poseData.spawnId = entity.spawn_id;
			poseData.poseState = poseState;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityPoseStateChanged, std::move(poseData)));
			eqt::state::EntityAnimationEventData animData;
			animData.spawnId = entity.spawn_id;
			animData.animCode = entity.animation;
			animData.animName = animCode;
			animData.loop = true;
			animData.playThrough = false;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityAnimationEvent, std::move(animData)));
			LOG_DEBUG(MOD_ENTITY, "Set initial pose for {} (ID: {}) to {} (anim={})",
			          entity.name, entity.spawn_id, animCode, entity.animation);
		}
	}
}

void EverQuest::OnPetCreated(const Entity& pet) {
	LOG_DEBUG(MOD_MAIN, "Pet window: Pet created - {} (ID: {}) Level {}",
	          pet.name, pet.spawn_id, pet.level);

	// T221: source-side write (stage 2 of Option A) -- the READ model's
	// PetCreated projection (GameStateSyncBridge) becomes redundant with this.
	m_game_state.pet().setPet(pet.spawn_id, pet.name, pet.level);

	// Publish PetCreated event to bridge (bridge handler opens pet window)
	if (m_bridge) {
		eqt::state::PetCreatedData data;
		data.spawnId = pet.spawn_id;
		data.name = pet.name;
		data.level = pet.level;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PetCreated, std::move(data)));
	}
}

void EverQuest::OnPetRemoved() {
	LOG_DEBUG(MOD_MAIN, "Pet window: Pet removed");

	// T221: source-side write; PetRemoved projection redundant.
	m_game_state.pet().clearPet();

	// PetRemoved bridge event drives closePetWindow() via bridge handler
	if (m_bridge) {
		eqt::state::PetRemovedData data;
		data.spawnId = m_pet_spawn_id;
		data.name = "";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PetRemoved, std::move(data)));
	}
}

void EverQuest::OnPetButtonStateChanged(EQT::PetButton button, bool state) {
	LOG_DEBUG(MOD_MAIN, "Pet button state changed: {} = {}",
	          EQT::GetPetButtonName(button), state ? "ON" : "OFF");

	// T221: source-side write; PetButtonStateChanged projection redundant.
	m_game_state.pet().setButtonState(static_cast<uint8_t>(button), state);

	// Publish PetButtonStateChanged event to bridge
	if (m_bridge) {
		eqt::state::PetButtonStateChangedData data;
		data.button = static_cast<uint8_t>(button);
		data.state = state;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PetButtonStateChanged, std::move(data)));
	}
}

void EverQuest::SaveEntityDataToFile(const std::string& filename) {
	std::ofstream file(filename);
	if (!file.is_open()) {
		LOG_ERROR(MOD_MAIN, "Failed to open {} for writing", filename);
		return;
	}

	file << "{\n  \"zone\": \"" << m_current_zone_name << "\",\n  \"entities\": [\n";

	bool first = true;
	for (const auto& [spawn_id, entity] : m_entities) {
		if (!first) file << ",\n";
		first = false;

		file << "    {\n";
		file << "      \"spawn_id\": " << entity.spawn_id << ",\n";
		file << "      \"name\": \"" << entity.name << "\",\n";
		file << "      \"race_id\": " << entity.race_id << ",\n";
		file << "      \"gender\": " << (int)entity.gender << ",\n";
		file << "      \"face\": " << (int)entity.face << ",\n";
		file << "      \"haircolor\": " << (int)entity.haircolor << ",\n";
		file << "      \"hairstyle\": " << (int)entity.hairstyle << ",\n";
		file << "      \"beardcolor\": " << (int)entity.beardcolor << ",\n";
		file << "      \"beard\": " << (int)entity.beard << ",\n";
		file << "      \"texture\": " << (int)entity.equip_chest2 << ",\n";
		file << "      \"equipment\": [";
		for (int i = 0; i < 9; i++) {
			if (i > 0) file << ", ";
			file << entity.equipment[i];
		}
		file << "],\n";
		file << "      \"equipment_tint\": [";
		for (int i = 0; i < 9; i++) {
			if (i > 0) file << ", ";
			file << entity.equipment_tint[i];
		}
		file << "],\n";
		file << "      \"position\": [" << entity.x << ", " << entity.y << ", " << entity.z << "],\n";
		file << "      \"heading\": " << entity.heading << "\n";
		file << "    }";
	}

	file << "\n  ]\n}\n";
	file.close();

	LOG_INFO(MOD_ENTITY, "Saved {} entities to {}", m_entities.size(), filename);
}

void EverQuest::OnSpawnRemovedGraphics(uint16_t spawn_id) {
	// Clear target if this entity was targeted (game state update)
	if (m_current_target_id == spawn_id) {
		m_current_target_id = 0;
		if (m_combat_manager) {
			m_combat_manager->ClearTarget();
		}
		// Publish TargetChanged (clear) event
		if (m_bridge) {
			eqt::state::TargetChangedData tdata;  // spawnId=0 = target cleared
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TargetChanged, std::move(tdata)));
		}
	}

	if (!m_bridge) {
		return;
	}

	// Check if this is a corpse - corpses should fade out instead of vanishing instantly
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end() && it->second.is_corpse) {
		eqt::state::CorpseDecayStartedData data;
		data.spawnId = spawn_id;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CorpseDecayStarted, std::move(data)));
	}

	// Publish EntityDespawned event (D17b: bridge is sole path to renderer)
	eqt::state::EntityDespawnedData data;
	data.spawnId = spawn_id;
	if (it != m_entities.end()) {
		data.name = it->second.name;
	}
	m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::EntityDespawned, std::move(data)));
}

void EverQuest::OnSpawnMovedGraphics(uint16_t spawn_id, float x, float y, float z, float heading,
                                      float dx, float dy, float dz, int32_t animation) {
	if (!m_bridge) {
		return;
	}

	// If this is our player entity, publish player position update
	// so the camera follows the server-authoritative position
	if (spawn_id == m_my_spawn_id) {
		// Convert heading from degrees (0-360) to EQ format (0-512)
		float heading_512 = heading * 512.0f / 360.0f;
		eqt::state::PlayerMovedData pdata;
		pdata.x = x; pdata.y = y; pdata.z = z;
		pdata.heading = heading_512;
		pdata.dx = dx; pdata.dy = dy; pdata.dz = dz;
		pdata.isMoving = (dx != 0 || dy != 0 || dz != 0);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMoved, std::move(pdata)));
	}

	// Publish EntityMoved event (D17b: bridge is sole path to renderer)
	eqt::state::EntityMovedData data;
	data.spawnId = spawn_id;
	data.x = x;
	data.y = y;
	data.z = z;
	data.heading = heading;
	data.dx = dx;
	data.dy = dy;
	data.dz = dz;
	data.animation = static_cast<uint8_t>(animation);
	m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::EntityMoved, std::move(data)));
}

void EverQuest::OnGraphicsMovement(const eqt::events::PlayerPositionChanged& update) {
	// Called by the renderer when the player moves in Player Mode
	// This syncs the player's position with the server

	if (!m_bridge) {
		return;
	}

	// Update position from graphics input
	// The renderer provides ground-level Z (where player's feet are)
	// SendMovementUpdate adds m_size/2 to convert to model-center Z for the server
	m_x = update.x;
	m_y = update.y;
	m_z = update.z;  // Ground-level Z from renderer
	// Convert heading from EQ format (0-512) to degrees (0-360) for internal use
	m_heading = update.heading * 360.0f / 512.0f;

	// Check water state at new position
	UpdateWaterState();

	// Derive movement state from velocity
	float speed2D = std::sqrt(update.dx * update.dx + update.dy * update.dy);
	float speed3D = std::sqrt(speed2D * speed2D + update.dz * update.dz);
	bool isMoving = (m_water_state != WaterState::NotInWater) ? speed3D > 0.01f : speed2D > 0.01f;
	m_is_moving = isMoving;

	// Determine if moving backward by comparing velocity direction to heading
	// Convert heading to radians and get unit vector in heading direction
	float headingRad = update.heading / 512.0f * 2.0f * 3.14159265f;
	float headingX = std::sin(headingRad);
	float headingY = std::cos(headingRad);

	// Dot product of velocity and heading direction
	// Positive = forward, Negative = backward
	float dotProduct = update.dx * headingX + update.dy * headingY;
	bool isMovingBackward = isMoving && dotProduct < -0.01f;

	// Determine animation based on movement state
	if (m_water_state != WaterState::NotInWater) {
		// Swimming animations
		// Animation code 7 = swimming/treading, 6 = swim idle
		if (isMoving) {
			m_animation = 7;  // Swim treading (l09)
		} else {
			m_animation = 6;  // Swim idle (p06)
		}
	} else {
		// Normal ground animations
		// Negative animation = play in reverse (e.g., walking backward)
		if (!isMoving) {
			m_animation = ANIM_STAND;
		} else if (speed2D > 5.0f) {  // Running threshold
			m_animation = isMovingBackward ? -ANIM_RUN : ANIM_RUN;
		} else {
			m_animation = isMovingBackward ? -ANIM_WALK : ANIM_WALK;
		}
	}

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MOVEMENT, "OnGraphicsMovement: pos=({:.2f},{:.2f},{:.2f}) heading={:.1f} vel=({:.2f},{:.2f},{:.2f}) anim={} water={}",
			update.x, update.y, update.z, update.heading, update.dx, update.dy, update.dz, m_animation,
			static_cast<int>(m_water_state));
	}

	// Check for zone line collision
	CheckZoneLine();

	// SendPositionUpdate has internal 250ms throttling
	SendPositionUpdate();
}

// ============================================================================
// T269: bank-coin helpers, shared by the two Bank* intent arms below.
// ============================================================================

uint32_t* EverQuest::BankCoinPool(uint8_t coinType, bool bank) {
	switch (coinType) {
	case EQT::COINTYPE_CP: return bank ? &m_bank_copper : &m_copper;
	case EQT::COINTYPE_SP: return bank ? &m_bank_silver : &m_silver;
	case EQT::COINTYPE_GP: return bank ? &m_bank_gold : &m_gold;
	case EQT::COINTYPE_PP: return bank ? &m_bank_platinum : &m_platinum;
	default: return nullptr;
	}
}

const char* EverQuest::BankCoinName(uint8_t coinType) {
	switch (coinType) {
	case EQT::COINTYPE_CP: return "copper";
	case EQT::COINTYPE_SP: return "silver";
	case EQT::COINTYPE_GP: return "gold";
	case EQT::COINTYPE_PP: return "platinum";
	default: return "coins";
	}
}

// The server's own table (zone/client_process.cpp CoinTypeCoppers).
int64_t EverQuest::BankCoinCoppers(uint8_t coinType) {
	switch (coinType) {
	case EQT::COINTYPE_PP: return 1000;
	case EQT::COINTYPE_GP: return 100;
	case EQT::COINTYPE_SP: return 10;
	default: return 1;
	}
}

void EverQuest::PublishBankCoinProjection(bool inventoryChanged) {
	if (inventoryChanged) {
		m_game_state.player().setCurrency(m_platinum, m_gold, m_silver, m_copper);
	}
	m_game_state.player().setBankCurrency(m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper);

	if (!m_bridge) {
		return;
	}
	if (inventoryChanged) {
		eqt::state::CurrencyChangedData cdata;
		cdata.platinum = static_cast<int32_t>(m_platinum);
		cdata.gold = static_cast<int32_t>(m_gold);
		cdata.silver = static_cast<int32_t>(m_silver);
		cdata.copper = static_cast<int32_t>(m_copper);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(cdata)));
	}

	eqt::state::BankCurrencyChangedData bdata;
	bdata.platinum = static_cast<int32_t>(m_bank_platinum);
	bdata.gold = static_cast<int32_t>(m_bank_gold);
	bdata.silver = static_cast<int32_t>(m_bank_silver);
	bdata.copper = static_cast<int32_t>(m_bank_copper);
	m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::BankCurrencyChanged, std::move(bdata)));
}

void EverQuest::ProcessBridgeIntents() {
	if (!m_bridge) return;

	auto intents = m_bridge->drainIntents();
	for (const auto& intent : intents) {
		std::visit([this](const auto& i) {
			using T = std::decay_t<decltype(i)>;
			if constexpr (std::is_same_v<T, eqt::events::PlayerPositionChanged>) {
				// D14/S2: movement intent — sole movement channel
				OnGraphicsMovement(i);
			}
			// ================================================================
			// D15: Interaction intents (D20a — intent-only, callbacks removed)
			// ================================================================
			else if constexpr (std::is_same_v<T, eqt::events::TargetIntent>) {
				// Check if we have a cursor item or cursor money and clicked on a player - initiate trade
				if (m_inventory_manager && m_trade_manager) {
					const auto* cursorItem = m_inventory_manager->getItem(eqt::inventory::CURSOR_SLOT);
					bool hasCursorMoney = m_inventory_manager->hasCursorMoney();
					if (cursorItem || hasCursorMoney) {
						auto it = m_entities.find(i.spawnId);
						if (it != m_entities.end() && (it->second.npc_type == 0 || it->second.npc_type == 1)) {
							bool isNpc = (it->second.npc_type == 1);
							if (cursorItem) {
								LOG_INFO(MOD_MAIN, "Initiating trade with {} (cursor item: {}, isNpc={})",
								         it->second.name, cursorItem->name, isNpc);
							} else {
								LOG_INFO(MOD_MAIN, "Initiating trade with {} (cursor money, isNpc={})",
								         it->second.name, isNpc);
							}
							m_trade_manager->requestTrade(i.spawnId, it->second.name, isNpc);
							return;  // Don't target, we're initiating trade
						}
					}
				}

				if (m_combat_manager) {
					if (m_combat_manager->SetTarget(i.spawnId)) {
						SetTrackedTargetId(i.spawnId);
						m_current_target_id = i.spawnId;

						auto it = m_entities.find(i.spawnId);
						if (it != m_entities.end()) {
							const Entity& e = it->second;

							// Publish TargetChanged event to bridge (handles renderer update)
							if (m_bridge) {
								eqt::state::TargetChangedData tdata;
								tdata.spawnId = e.spawn_id;
								tdata.name = e.name;
								tdata.level = e.level;
								tdata.hpPercent = e.hp_percent;
								tdata.raceId = e.race_id;
								tdata.gender = e.gender;
								tdata.classId = e.class_id;
								tdata.bodyType = e.bodytype;
								tdata.npcType = e.npc_type;
								tdata.helm = e.helm;
								tdata.showHelm = e.showhelm;
								tdata.texture = e.equip_chest2;
								for (int j = 0; j < 9; ++j) {
									tdata.equipment[j] = e.equipment[j];
									tdata.equipmentTint[j] = e.equipment_tint[j];
								}
								m_bridge->pushEvent(eqt::state::GameEvent(
									eqt::state::GameEventType::TargetChanged, std::move(tdata)));
							}

							LOG_DEBUG(MOD_ENTITY, "=== TARGET SELECTED: {} ===", e.name);
							LOG_DEBUG(MOD_ENTITY, "  spawn_id={} race_id={} gender={} level={} class_id={}",
								i.spawnId, e.race_id, (int)e.gender, (int)e.level, (int)e.class_id);
							LOG_DEBUG(MOD_ENTITY, "  npc_type={} (0=player,1=npc,2=pc_corpse,3=npc_corpse) bodytype={}",
								(int)e.npc_type, (int)e.bodytype);
							LOG_DEBUG(MOD_ENTITY, "  face={} haircolor={} hairstyle={} beardcolor={} beard={}",
								(int)e.face, (int)e.haircolor, (int)e.hairstyle, (int)e.beardcolor, (int)e.beard);
							LOG_DEBUG(MOD_ENTITY, "  texture(equip_chest2)={} helm={} showhelm={} light={}",
								(int)e.equip_chest2, (int)e.helm, (int)e.showhelm, (int)e.light);
							LOG_DEBUG(MOD_ENTITY, "  equipment[0-8]: head={} chest={} arms={} wrist={} hands={} legs={} feet={} primary={} secondary={}",
								e.equipment[0], e.equipment[1], e.equipment[2], e.equipment[3],
								e.equipment[4], e.equipment[5], e.equipment[6], e.equipment[7], e.equipment[8]);
							LOG_DEBUG(MOD_ENTITY, "  equipment_tint[0-8]: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
								e.equipment_tint[0], e.equipment_tint[1], e.equipment_tint[2], e.equipment_tint[3],
								e.equipment_tint[4], e.equipment_tint[5], e.equipment_tint[6], e.equipment_tint[7], e.equipment_tint[8]);
						}
					}
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::LootCorpseIntent>) {
				// T268: the gesture enters the same loot machine /loot does
				// (CombatManager::LootCorpse -> RequestLootCorpse), so
				// LootAllIntent/CloseLootIntent can drive it afterwards.
				if (m_combat_manager) {
					m_combat_manager->LootCorpse(i.spawnId);
				} else {
					RequestLootCorpse(i.spawnId);
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::DoorInteractIntent>) {
				SendClickDoor(i.doorId);
			}
			else if constexpr (std::is_same_v<T, eqt::events::VendorToggleIntent>) {
				// If vendor window is open, close it
				if (IsVendorWindowOpen()) {
					CloseVendorWindow();
					return;
				}

				// Check if we have a target
				if (!m_combat_manager || !m_combat_manager->HasTarget()) {
					LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: No target selected");
					return;
				}

				uint16_t target_id = m_combat_manager->GetTargetId();
				auto it = m_entities.find(target_id);
				if (it == m_entities.end()) {
					LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Target {} not found in entities", target_id);
					return;
				}

				const Entity& target = it->second;

				if (target.npc_type != 1) {
					LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Target {} is not an NPC (type={})", target.name, target.npc_type);
					return;
				}

				constexpr uint8_t CLASS_MERCHANT = 41;
				if (target.class_id != CLASS_MERCHANT) {
					LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Target {} is not a merchant (class={})", target.name, target.class_id);
					return;
				}

				LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Opening vendor {} (id={})", target.name, target_id);
				RequestOpenVendor(target_id);
			}
			else if constexpr (std::is_same_v<T, eqt::events::BankerInteractIntent>) {
				// If bank window is already open, ignore
				if (IsBankWindowOpen()) {
					LOG_DEBUG(MOD_INVENTORY, "Banker interact: Bank already open");
					return;
				}

				auto it = m_entities.find(i.npcId);
				if (it == m_entities.end()) {
					LOG_DEBUG(MOD_INVENTORY, "Banker interact: NPC {} not found in entities", i.npcId);
					return;
				}

				const Entity& target = it->second;

				if (target.npc_type != 1) {
					LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is not an NPC (type={})", target.name, target.npc_type);
					return;
				}

				// Check distance to NPC
				float distSq = CalculateDistance2D(m_x, m_y, target.x, target.y);
				distSq = distSq * distSq;  // CalculateDistance2D returns distance, not squared
				if (distSq > NPC_INTERACTION_DISTANCE_SQUARED) {
					LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is too far away (dist={})", target.name, std::sqrt(distSq));
					AddChatSystemMessage("You are too far away to interact with this NPC.");
					return;
				}

				constexpr uint8_t CLASS_BANKER = 40;
				if (target.class_id != CLASS_BANKER) {
					LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is not a banker (class={})", target.name, target.class_id);
					AddChatSystemMessage("This NPC is not a banker.");
					return;
				}

				LOG_INFO(MOD_INVENTORY, "Opening bank window for {} (id={})", target.name, i.npcId);
				OpenBankWindow(i.npcId);
			}
			else if constexpr (std::is_same_v<T, eqt::events::TrainerToggleIntent>) {
				// If trainer window is open, close it
				if (IsTrainerWindowOpen()) {
					CloseTrainerWindow();
					return;
				}

				if (!m_combat_manager || !m_combat_manager->HasTarget()) {
					LOG_DEBUG(MOD_MAIN, "Trainer toggle: No target selected");
					return;
				}

				uint16_t target_id = m_combat_manager->GetTargetId();
				auto it = m_entities.find(target_id);
				if (it == m_entities.end()) {
					LOG_DEBUG(MOD_MAIN, "Trainer toggle: Target {} not found in entities", target_id);
					return;
				}

				const Entity& target = it->second;

				if (target.npc_type != 1) {
					LOG_DEBUG(MOD_MAIN, "Trainer toggle: Target {} is not an NPC (type={})", target.name, target.npc_type);
					return;
				}

				constexpr uint8_t CLASS_WARRIOR_GM = 20;
				constexpr uint8_t CLASS_BERSERKER_GM = 35;
				if (target.class_id < CLASS_WARRIOR_GM || target.class_id > CLASS_BERSERKER_GM) {
					LOG_DEBUG(MOD_MAIN, "Trainer toggle: Target {} is not a trainer (class={})", target.name, target.class_id);
					AddChatSystemMessage("That is not a trainer.");
					return;
				}

				LOG_DEBUG(MOD_MAIN, "Trainer toggle: Requesting trainer {} (id={}, class={})", target.name, target_id, target.class_id);
				RequestTrainerWindow(target_id);
			}
			else if constexpr (std::is_same_v<T, eqt::events::WorldObjectInteractIntent>) {
				auto it = m_world_objects.find(i.dropId);
				if (it != m_world_objects.end() && it->second.isTradeskillContainer()) {
					LOG_INFO(MOD_INVENTORY, "Clicking tradeskill container: dropId={} name='{}' type={}",
						i.dropId, it->second.name, it->second.object_type);
					SendClickObject(i.dropId);
				} else if (it != m_world_objects.end()) {
					LOG_DEBUG(MOD_ENTITY, "World object {} is not a tradeskill container (type={})",
						i.dropId, it->second.object_type);
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::ZoningEnabledIntent>) {
				SetZoningEnabled(i.enabled);
				LOG_INFO(MOD_ZONE, "Zoning {}", i.enabled ? "enabled" : "disabled");
			}
			else if constexpr (std::is_same_v<T, eqt::events::ChatSubmitIntent>) {
				ProcessChatInput(i.text);
			}
			else if constexpr (std::is_same_v<T, eqt::events::ReadItemIntent>) {
				RequestReadBook(i.bookText, i.type);
			}
			// ================================================================
			// D16: UI intents (D20a — intent-only, callbacks removed)
			// ================================================================
			// Spell intents
			else if constexpr (std::is_same_v<T, eqt::events::CastSpellIntent>) {
				if (!m_spell_manager) return;
				uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
				EQ::CastResult result = m_spell_manager->beginCastFromGem(i.gemSlot, targetId);
				if (result == EQ::CastResult::Success) {
					uint32_t spellId = m_spell_manager->getMemorizedSpell(i.gemSlot);
					const EQ::SpellData* spell = m_spell_manager->getSpell(spellId);
					if (spell) AddChatSystemMessage(fmt::format("Casting {}", spell->name));
				} else if (std::string refusal = EQ::castRefusalMessage(*this, result); !refusal.empty()) {
					// T244: same official-string table as EqActionHandler::
					// beginCastFromGem() -- the two sites had drifted. T251:
					// same renderer too (names a missing component).
					AddChatSystemMessage(refusal);
				} else if (result == EQ::CastResult::NotMemorized) {
					AddChatSystemMessage(fmt::format("No spell in gem {}", i.gemSlot + 1));
				} else if (result == EQ::CastResult::SpellNotReady) {
					AddChatSystemMessage("Spell not ready");
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::ScribeSpellIntent>) {
				ScribeSpellFromScroll(i.spellId, i.bookSlot, i.sourceSlot);
			}
			else if constexpr (std::is_same_v<T, eqt::events::SpellbookStateIntent>) {
				SendSpawnAppearance(AT_ANIMATION, i.isOpen ? 110 : 100);
			}
			// Buff/skill intents
			else if constexpr (std::is_same_v<T, eqt::events::BuffCancelIntent>) {
				if (m_buff_manager) {
					m_buff_manager->removeBuffBySlot(0, i.slot);
					AddChatSystemMessage(fmt::format("Buff in slot {} cancelled", i.slot + 1));
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::SkillActivateIntent>) {
				// T156: gained a real producer this unit (a click on a row of
				// the skills popup, static_ui_input.cpp). Pass the current
				// target — Taunt/Disarm/PickPocket all need a real target id.
				if (m_skill_manager && i.skillId <= 0xFF) {
					uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
					m_skill_manager->activateSkill(static_cast<uint8_t>(i.skillId), targetId);
				}
			}
			// Loot intents (T268: producers are the /loot family; a loot
			// popup would push the same intents)
			else if constexpr (std::is_same_v<T, eqt::events::LootItemIntent>) {
				LootItemFromCorpse(i.corpseId, i.slot);  // to the cursor, like the real client
			}
			else if constexpr (std::is_same_v<T, eqt::events::LootAllIntent>) {
				if (m_combat_manager) {
					m_combat_manager->LootAll();
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::CloseLootIntent>) {
				CloseLootWindow(i.corpseId);
			}
			// Vendor intents
			else if constexpr (std::is_same_v<T, eqt::events::VendorBuyIntent>) {
				BuyFromVendor(i.npcId, i.slot, i.quantity);
			}
			else if constexpr (std::is_same_v<T, eqt::events::VendorSellIntent>) {
				SellToVendor(i.npcId, i.slot, i.quantity);
			}
			// Bank intents (T269: producers are EverQuest::RequestBankCoinMove/
			// Convert via /bank deposit|withdraw|convert). Wire truth, from
			// eqemu zone/client_process.cpp Client::OPMoveCoin: from_slot/
			// to_slot 1 = inventory, 2 = bank; cointype 0=cp 1=sp 2=gp 3=pp;
			// the server clamps amount to the source bucket, converts at
			// CoinTypeCoppers(cointype1)/CoinTypeCoppers(cointype2) truncating,
			// requires a banker within USE_NPC_RANGE2 for slot 2, and SENDS
			// NOTHING BACK on success (it ends in SaveCurrency(), a DB write).
			// So there is no echo to project from: the arm refuses anything
			// the server would clamp or drop, and otherwise applies the same
			// arithmetic the server applies to both the EverQuest counters and
			// the GameState pools /bank's pre-check reads.
			else if constexpr (std::is_same_v<T, eqt::events::BankCurrencyMoveIntent>) {
				if (!IsBankWindowOpen()) {
					LOG_WARN(MOD_INVENTORY, "Bank coin move refused: bank window not open (type={} amount={} fromBank={})",
						i.coinType, i.amount, i.fromBank);
					return;
				}
				if (i.amount <= 0 || i.coinType > EQT::COINTYPE_PP) {
					LOG_WARN(MOD_INVENTORY, "Bank coin move refused: bad amount/type (type={} amount={})", i.coinType, i.amount);
					return;
				}

				uint32_t* src = BankCoinPool(i.coinType, /*bank=*/i.fromBank);
				uint32_t* dst = BankCoinPool(i.coinType, /*bank=*/!i.fromBank);
				if (*src < static_cast<uint32_t>(i.amount)) {
					// The server would silently move only what is there; refuse
					// instead so the projection and the wire never disagree.
					AddChatSystemMessage(fmt::format("You do not have {} {} {}.",
						i.amount, BankCoinName(i.coinType), i.fromBank ? "in the bank" : "on hand"));
					return;
				}

				EQT::MoveCoin_Struct move;
				move.from_slot = i.fromBank ? EQT::COINSLOT_BANK : EQT::COINSLOT_INVENTORY;
				move.to_slot = i.fromBank ? EQT::COINSLOT_INVENTORY : EQT::COINSLOT_BANK;
				move.cointype1 = i.coinType;
				move.cointype2 = i.coinType;
				move.amount = i.amount;
				SendMoveCoin(move);

				*src -= static_cast<uint32_t>(i.amount);
				*dst += static_cast<uint32_t>(i.amount);

				LOG_DEBUG(MOD_INVENTORY, "Bank currency move: type={} amount={} fromBank={}, bank now: {}pp {}gp {}sp {}cp, inv now: {}pp {}gp {}sp {}cp",
					i.coinType, i.amount, i.fromBank,
					m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper,
					m_platinum, m_gold, m_silver, m_copper);

				PublishBankCoinProjection(/*inventoryChanged=*/true);
			}
			else if constexpr (std::is_same_v<T, eqt::events::BankCurrencyConvertIntent>) {
				if (!IsBankWindowOpen()) {
					LOG_WARN(MOD_INVENTORY, "Bank coin convert refused: bank window not open");
					return;
				}
				if (i.amount <= 0 || i.fromCoinType > EQT::COINTYPE_PP || i.toCoinType > EQT::COINTYPE_PP
				    || i.fromCoinType == i.toCoinType) {
					LOG_WARN(MOD_INVENTORY, "Bank coin convert refused: bad amount/types (from={} to={} amount={})",
						i.fromCoinType, i.toCoinType, i.amount);
					return;
				}

				// Same ratio the server computes: CoinTypeCoppers(from)/CoinTypeCoppers(to).
				const int64_t fromCoppers = BankCoinCoppers(i.fromCoinType);
				const int64_t toCoppers = BankCoinCoppers(i.toCoinType);
				int64_t converted = 0;
				if (toCoppers > fromCoppers) {
					const int64_t step = toCoppers / fromCoppers;  // 10, 100 or 1000
					if (i.amount % step != 0) {
						// The server would take only the largest multiple and
						// leave the remainder -- the wire amount would then
						// not describe what happened. Refuse.
						AddChatSystemMessage(fmt::format("Amount must be a multiple of {} to convert {} to {}.",
							step, BankCoinName(i.fromCoinType), BankCoinName(i.toCoinType)));
						return;
					}
					converted = i.amount / step;
				} else {
					converted = static_cast<int64_t>(i.amount) * (fromCoppers / toCoppers);
				}

				uint32_t* src = BankCoinPool(i.fromCoinType, /*bank=*/true);
				uint32_t* dst = BankCoinPool(i.toCoinType, /*bank=*/true);
				if (*src < static_cast<uint32_t>(i.amount)) {
					AddChatSystemMessage(fmt::format("You do not have {} {} in the bank.",
						i.amount, BankCoinName(i.fromCoinType)));
					return;
				}
				if (converted > static_cast<int64_t>(std::numeric_limits<uint32_t>::max() - *dst)) {
					AddChatSystemMessage("That conversion would overflow the destination pool.");
					return;
				}

				EQT::MoveCoin_Struct move;
				move.from_slot = EQT::COINSLOT_BANK;
				move.to_slot = EQT::COINSLOT_BANK;
				move.cointype1 = i.fromCoinType;
				move.cointype2 = i.toCoinType;
				move.amount = i.amount;
				SendMoveCoin(move);

				*src -= static_cast<uint32_t>(i.amount);
				*dst += static_cast<uint32_t>(converted);

				LOG_DEBUG(MOD_INVENTORY, "Bank currency convert: {} {} -> {} {}; bank now: {}pp {}gp {}sp {}cp",
					i.amount, BankCoinName(i.fromCoinType), converted, BankCoinName(i.toCoinType),
					m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper);

				PublishBankCoinProjection(/*inventoryChanged=*/false);
			}
			// Volume change intents (D20c2)
			else if constexpr (std::is_same_v<T, eqt::events::MusicVolumeChangeIntent>) {
#ifdef WITH_AUDIO
				if (m_audio_manager) {
					float newVol = std::max(0.0f, std::min(1.0f, m_audio_manager->getMusicVolume() + i.delta));
					m_audio_manager->setMusicVolume(newVol);
					AddChatSystemMessage(fmt::format("Music volume: {}%", static_cast<int>(newVol * 100)));
				}
#endif
			}
			else if constexpr (std::is_same_v<T, eqt::events::EffectsVolumeChangeIntent>) {
#ifdef WITH_AUDIO
				if (m_audio_manager) {
					float newVol = std::max(0.0f, std::min(1.0f, m_audio_manager->getEffectsVolume() + i.delta));
					m_audio_manager->setEffectsVolume(newVol);
					AddChatSystemMessage(fmt::format("Effects volume: {}%", static_cast<int>(newVol * 100)));
				}
#endif
			}
			// Hotbar activate intent (D20b2)
			else if constexpr (std::is_same_v<T, eqt::events::HotbarActivateIntent>) {
				// T156: switch on the model's enumerators, not on raw ints.
				// The old numbering here (0=Spell, 1=Item, 2=Emote, 3=Skill)
				// matched NEITHER enum in the tree; nothing ever produced it.
				// -Wswitch now catches the next drift, so there is no default.
				switch (i.type) {
				case eqt::HotbarSlotType::Spell: { // Spell
					if (m_spell_manager) {
						for (uint8_t gem = 0; gem < EQ::MAX_SPELL_GEMS; gem++) {
							if (m_spell_manager->getMemorizedSpell(gem) == i.id) {
								uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
								EQ::CastResult result = m_spell_manager->beginCastFromGem(gem, targetId);
								if (result == EQ::CastResult::Success) {
									const EQ::SpellData* spell = m_spell_manager->getSpell(i.id);
									if (spell) AddChatSystemMessage(fmt::format("Casting {}", spell->name));
								} else if (std::string refusal = EQ::castRefusalMessage(*this, result); !refusal.empty()) {
									// T253: third refusal site, same table/renderer as
									// CastSpellIntent above and EqActionHandler::
									// beginCastFromGem(). Was three literals (mana /
									// cooldown / already casting) and silence for
									// every other refusal.
									AddChatSystemMessage(refusal);
								} else if (result == EQ::CastResult::SpellNotReady) {
									AddChatSystemMessage("Spell not ready");
								}
								break;
							}
						}
					}
					break;
				}
				case eqt::HotbarSlotType::Item: {
					// T230: the id->slot hop + click-effect send now live in
					// UseInventoryItem(), shared with the keyboard route
					// (EqActionHandler::useItem <- ActionDispatcher::useItemById).
					if (m_inventory_manager) {
						int16_t slot = m_inventory_manager->findItemSlotByItemId(i.id);
						if (slot == eqt::inventory::SLOT_INVALID) {
							AddChatSystemMessage("Item not found in inventory");
							break;
						}
						UseInventoryItem(slot);
					}
					break;
				}
				case eqt::HotbarSlotType::Emote:
				case eqt::HotbarSlotType::Macro:
				case eqt::HotbarSlotType::Combat:
					// T239: the command-slot body is shared with the keyboard
					// route (EqActionHandler::activateHotbarCommandSlot). Was:
					// Emote -> ProcessChatInput inline, Macro/Combat no-ops.
					ActivateHotbarCommandSlot(i.type, i.emoteText);
					break;
				case eqt::HotbarSlotType::Skill: {
					// T156: reachable at last — see refactor/T156_skill_reachability.md.
					if (m_skill_manager && i.id <= 0xFF) {
						uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
						m_skill_manager->activateSkill(static_cast<uint8_t>(i.id), targetId);
					}
					break;
				}
				case eqt::HotbarSlotType::Empty:
					break;
				}
			}
			// Tradeskill intents (D20b1)
			else if constexpr (std::is_same_v<T, eqt::events::TradeskillCombineIntent>) {
				SendTradeSkillCombine(i.containerSlot);
			}
			else if constexpr (std::is_same_v<T, eqt::events::TradeskillCloseIntent>) {
				if (i.dropId != 0) {
					SendCloseContainer(i.dropId);
				}
				m_active_tradeskill_object_id = 0;
			}
			// Trainer intents (D20b1)
			else if constexpr (std::is_same_v<T, eqt::events::TrainSkillIntent>) {
				TrainSkill(i.skillId);
			}
			else if constexpr (std::is_same_v<T, eqt::events::CloseTrainerIntent>) {
				CloseTrainerWindow();
			}
			// Inventory intents (D20b1)
			else if constexpr (std::is_same_v<T, eqt::events::MoveItemIntent>) {
				SendMoveItem(i.fromSlot, i.toSlot, i.quantity);
			}
			else if constexpr (std::is_same_v<T, eqt::events::DeleteItemIntent>) {
				SendDeleteItem(i.slot);
			}
			else if constexpr (std::is_same_v<T, eqt::events::EquipmentChangedIntent>) {
				UpdatePlayerAppearanceFromInventory();
			}
			else if constexpr (std::is_same_v<T, eqt::events::MemorizeSpellIntent>) {
				// T272: this arm had its ARGUMENTS SWAPPED --
				// memorizeSpell(uint32_t spell_id, uint8_t gem_slot) was
				// called as memorizeSpell(i.gemSlot, i.spellId), so it asked
				// SpellManager to memorize spell <gem number> into gem
				// <spell id truncated to 8 bits>. Nothing ever produced this
				// intent, so nothing ever executed the line (T264's dead-arm
				// class). Fixed with the producer that now exists.
				//
				// SpellManager is the gem authority (T240) and owns the same
				// body /mem reaches via EqActionHandler::memorizeSpellByName;
				// the feedback below mirrors the CastSpellIntent arm above,
				// which likewise renders its own chat text.
				if (!m_spell_manager) {
					AddChatSystemMessage("Spell system not initialized");
				} else if (m_spell_manager->memorizeSpell(i.spellId, i.gemSlot)) {
					AddChatSystemMessage(fmt::format("Memorizing spell in gem {}", i.gemSlot + 1));
				} else {
					AddChatSystemMessage(fmt::format(
						"Cannot memorize into gem {} - check if scribed and level requirement",
						i.gemSlot + 1));
				}
			}
			else {
				LOG_TRACE(MOD_MAIN, "Bridge: unhandled intent type");
			}
		}, intent);
	}
}

// Single-stage drain: drain + applyEvent() on the CALLING thread. Production
// caller is Application::updateGameState() in headless mode only (game
// thread, ConsoleBridge). Graphics runs must not call this -- their drain is
// split across threads (T220, see the note above and game_state_sync_bridge.h).
void EverQuest::ProcessBridgeEvents() {
	if (!m_bridge) return;

	auto events = m_bridge->drainEvents();
	for (const auto& event : events) {
		m_bridge->applyEvent(event);
	}
}

void EverQuest::UpdateInventoryStats() {

	// Calculate equipment bonuses from inventory
	eqt::inventory::EquipmentStats equipStats;
	float totalWeight = 0.0f;

	// Track weapon skill types for combat animations
	uint8_t primaryWeaponSkill = EQ::WEAPON_HAND_TO_HAND;  // Default to H2H (unarmed)
	uint8_t secondaryWeaponSkill = EQ::WEAPON_NONE;

	if (m_inventory_manager) {
		equipStats = m_inventory_manager->calculateEquipmentStats();
		totalWeight = m_inventory_manager->calculateTotalWeight();

		// Get weapon skill types from equipped items
		using namespace eqt::inventory;
		const eqt::inventory::ItemInstance* primaryItem = m_inventory_manager->getItem(SLOT_PRIMARY);
		const eqt::inventory::ItemInstance* secondaryItem = m_inventory_manager->getItem(SLOT_SECONDARY);

		if (primaryItem && primaryItem->itemId != 0) {
			primaryWeaponSkill = primaryItem->skillType;
		}
		if (secondaryItem && secondaryItem->itemId != 0) {
			secondaryWeaponSkill = secondaryItem->skillType;
		}

		// Update our entity's weapon skill types
		auto it = m_entities.find(m_my_spawn_id);
		if (it != m_entities.end()) {
			it->second.primary_weapon_skill = primaryWeaponSkill;
			it->second.secondary_weapon_skill = secondaryWeaponSkill;
		}

		// Propagate weapon skill types via bridge (D17b: bridge is sole path to renderer)
		if (m_bridge) {
			eqt::state::EntityWeaponSkillsChangedData data;
			data.spawnId = m_my_spawn_id;
			data.primaryWeaponSkill = primaryWeaponSkill;
			data.secondaryWeaponSkill = secondaryWeaponSkill;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityWeaponSkillsChanged, std::move(data)));
		}
	}

	// Calculate total stats (base + equipment)
	int totalStr = static_cast<int>(m_STR) + equipStats.str;
	int totalSta = static_cast<int>(m_STA) + equipStats.sta;
	int totalAgi = static_cast<int>(m_AGI) + equipStats.agi;
	int totalDex = static_cast<int>(m_DEX) + equipStats.dex;
	int totalWis = static_cast<int>(m_WIS) + equipStats.wis;
	int totalInt = static_cast<int>(m_INT) + equipStats.int_;
	int totalCha = static_cast<int>(m_CHA) + equipStats.cha;

	// Max weight capacity is equal to total STR
	float maxWeight = static_cast<float>(totalStr);

	// Update weight tracking (both local and GameState)
	m_weight = totalWeight;
	m_max_weight = maxWeight;
	m_game_state.player().setWeight(m_weight, m_max_weight);

	// Publish stats via bridge (D18a: bridge is sole path to renderer)
	if (m_bridge) {
		eqt::state::PlayerStatsChangedData data;
		data.curHP = m_cur_hp;
		data.maxHP = m_max_hp;
		data.curMana = m_mana;
		data.maxMana = m_max_mana;
		data.curEndurance = m_endurance;
		data.maxEndurance = m_max_endurance;
		data.level = m_level;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerStatsChanged, std::move(data)));

		// Publish EquipmentStatsChanged
		eqt::state::EquipmentStatsChangedData edata;
		edata.ac = equipStats.ac;
		edata.atk = equipStats.atk;
		edata.hp = equipStats.hp;
		edata.mana = equipStats.mana;
		edata.weight = m_weight;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::EquipmentStatsChanged, std::move(edata)));

		// Publish CurrencyChanged (updateCharacterStats includes currency)
		eqt::state::CurrencyChangedData cdata;
		cdata.platinum = static_cast<int32_t>(m_platinum);
		cdata.gold = static_cast<int32_t>(m_gold);
		cdata.silver = static_cast<int32_t>(m_silver);
		cdata.copper = static_cast<int32_t>(m_copper);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(cdata)));
	}
}

// ============================================================================
// Player Mode Loot Window Methods
// ============================================================================

void EverQuest::RequestLootCorpse(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "RequestLootCorpse: corpseId={}", corpseId);

	// Store the corpse being looted
	m_player_looting_corpse_id = corpseId;
	m_game_state.player().setLootingCorpse(corpseId);  // Phase 7.7
	LOG_TRACE(MOD_INVENTORY, "Set m_player_looting_corpse_id={}", m_player_looting_corpse_id);

	// Send loot request packet - server expects only the corpse ID (4 bytes)
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt32(0, corpseId);
	QueuePacket(HC_OP_LootRequest, &packet);

	// Clear game-side loot items for new corpse
	m_loot_items.clear();

	// Publish LootWindowOpened (D18a: bridge is sole path to renderer)
	if (m_bridge) {
		std::string corpseName = "Corpse";
		auto it = m_entities.find(corpseId);
		if (it != m_entities.end()) {
			corpseName = EQT::toDisplayName(it->second.name);
		}
		eqt::state::LootWindowOpenedData data;
		data.corpseId = corpseId;
		data.corpseName = corpseName;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::LootWindowOpened, std::move(data)));
	}
}

bool EverQuest::LootItemFromCorpse(uint16_t corpseId, int16_t slot, bool autoLoot) {
	LOG_DEBUG(MOD_INVENTORY, "LootItemFromCorpse: corpseId={} slot={} autoLoot={} m_player_looting_corpse_id={} m_my_spawn_id={}",
		corpseId, slot, autoLoot, m_player_looting_corpse_id, m_my_spawn_id);

	// T268: no window, nothing to take -- eqemu would answer a bare
	// OP_LootItem with an EndLoot error anyway (Corpse::LootCorpseItem's
	// IsBeingLootedBy check).
	if (m_player_looting_corpse_id == 0) {
		LOG_DEBUG(MOD_INVENTORY, "LootItemFromCorpse: no loot window open");
		return false;
	}
	if (corpseId == 0) {
		corpseId = m_player_looting_corpse_id;
	}

	// Track this slot as pending loot (waiting for server confirmation)
	m_pending_loot_slots.push_back(slot);
	LOG_TRACE(MOD_INVENTORY, "Added slot {} to pending_loot_slots, size={}", slot, m_pending_loot_slots.size());

	// Send loot item packet
	#pragma pack(1)
	struct LootingItem_Struct {
		uint32_t lootee;
		uint32_t looter;
		uint16_t slot_id;
		uint8_t  unknown3[2];
		uint32_t auto_loot;
	};
	#pragma pack()
	static_assert(sizeof(LootingItem_Struct) == 16, "LootingItem_Struct must be 16 bytes");

	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(LootingItem_Struct));
	LootingItem_Struct* li = (LootingItem_Struct*)packet.Data();
	li->lootee = corpseId;
	li->looter = m_my_spawn_id;
	li->slot_id = static_cast<uint16_t>(slot);
	li->unknown3[0] = 0;
	li->unknown3[1] = 0;
	li->auto_loot = autoLoot ? 1 : 0;

	// Debug: print packet bytes (only at high debug level)
	if (s_debug_level >= 3) {
		std::string hex_str;
		const uint8_t* data = (const uint8_t*)packet.Data();
		for (size_t i = 0; i < packet.Length(); i++) {
			char buf[4];
			snprintf(buf, sizeof(buf), "%02x ", data[i]);
			hex_str += buf;
		}
		LOG_TRACE(MOD_INVENTORY, "Sending OP_LootItem packet ({} bytes): {}", packet.Length(), hex_str);
	}
	LOG_DEBUG(MOD_INVENTORY, "OP_LootItem: lootee={} looter={} slot_id={} auto_loot={}",
		li->lootee, li->looter, li->slot_id, li->auto_loot);

	QueuePacket(HC_OP_LootItem, &packet);
	return true;
}

int16_t EverQuest::FindLootSlotByName(const std::string& name) const {
	if (m_player_looting_corpse_id == 0 || name.empty()) {
		return -1;
	}
	auto lower = [](std::string v) {
		for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return v;
	};
	// Accept either spelling of a space: the player may type the name with
	// underscores the way entity names print.
	std::string want = lower(name);
	std::replace(want.begin(), want.end(), '_', ' ');
	int16_t partial = -1;
	for (const auto& [slot, item] : m_loot_items) {
		std::string have = lower(item.name);
		std::replace(have.begin(), have.end(), '_', ' ');
		if (have == want) {
			return slot;
		}
		if (partial < 0 && have.find(want) != std::string::npos) {
			partial = slot;
		}
	}
	return partial;
}

void EverQuest::CloseLootWindow(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "CloseLootWindow: corpseId={} m_player_looting_corpse_id={}",
		corpseId, m_player_looting_corpse_id);

	// Use passed corpseId, or fall back to m_player_looting_corpse_id if 0
	uint16_t targetCorpseId = (corpseId != 0) ? corpseId : m_player_looting_corpse_id;

	// Clear the looting state
	m_player_looting_corpse_id = 0;
	m_pending_loot_slots.clear();
	m_game_state.player().clearLootingCorpse();  // Phase 7.7

	// Mark this corpse as ready for deletion (server will send DeleteSpawn after EndLootRequest)
	m_loot_complete_corpse_id = targetCorpseId;

	// Send end loot request to server (OP_EndLootRequest, not OP_LootComplete)
	// Server expects 4 bytes with corpse entity ID as uint16 at offset 0
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt16(0, targetCorpseId);
	packet.PutUInt16(2, 0);  // padding
	QueuePacket(HC_OP_EndLootRequest, &packet);
	LOG_DEBUG(MOD_INVENTORY, "CloseLootWindow: Sent HC_OP_EndLootRequest with corpseId={}", targetCorpseId);

	// Clear game-side loot items
	m_loot_items.clear();

	// T268: the loot policy machine leaves LOOTING with the window (this is
	// the ONE close path; CombatManager::CloseLootWindow routes here).
	if (m_combat_manager) {
		m_combat_manager->OnLootWindowClosed();
	}

	// Publish LootWindowClosed (D18a: bridge is sole path to renderer)
	if (m_bridge) {
		eqt::state::LootWindowClosedData data;
		data.corpseId = targetCorpseId;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::LootWindowClosed, std::move(data)));
	}
}

// T171: the per-client config node, resolved for WRITING. Extracted verbatim
// from SaveHotbarConfig() so the inspect-message pair below cannot drift from
// the hotbar pair -- duplicated-then-drifted is this codebase's second most
// common defect class, and two more hand-rolled copies of this shape is
// exactly how it starts. Migrates the legacy top-level-array layout on the
// way. Returns nullptr when the config has a shape we should not rewrite.
Json::Value* EverQuest::ResolveClientConfigForWrite(Json::Value& root) {
	// A missing or unparseable config file leaves a NULL root
	// (JsonConfigFile::Load returns a default-constructed Json::Value for
	// both). Initialise it rather than bail: bailing is what the hotbar code
	// did before this extraction, and it meant a first save against a
	// not-yet-existing config silently wrote nothing at all. Creating the
	// structure is a deliberate behaviour change, and the only case it
	// touches is the one that previously did nothing.
	if (root.isNull()) {
		root = Json::Value(Json::objectValue);
	}

	// Migrate old config format to new format if needed
	if (root.isArray()) {
		Json::Value newRoot;
		newRoot["clients"] = root;
		root = newRoot;
	}

	if (!root.isObject()) {
		return nullptr;
	}
	if (root.isMember("clients") && root["clients"].isArray() && root["clients"].size() > 0) {
		return &root["clients"][0];
	}
	if (!root.isMember("clients")) {
		Json::Value clientsCopy = root;
		root = Json::Value(Json::objectValue);
		root["clients"] = Json::Value(Json::arrayValue);
		root["clients"].append(clientsCopy);
		return &root["clients"][0];
	}
	return nullptr;
}

// The same node resolved for READING. Deliberately NOT the same function: the
// read side accepts a bare top-level object as the client config (the last
// branch below) and never rewrites the file, and collapsing the two would
// change one of the two behaviours. Extracted verbatim from
// LoadHotbarConfig().
const Json::Value* EverQuest::ResolveClientConfigForRead(const Json::Value& root) {
	if (root.isArray() && root.size() > 0) {
		return &root[0];
	}
	if (root.isObject()) {
		if (root.isMember("clients") && root["clients"].isArray() && root["clients"].size() > 0) {
			return &root["clients"][0];
		}
		return &root;
	}
	return nullptr;
}

void EverQuest::SaveHotbarConfig() {
	if (m_config_path.empty() || !m_hotbar_model) return;

	auto config = EQ::JsonConfigFile::Load(m_config_path);
	Json::Value& root = config.RawHandle();

	Json::Value* clientConfig = ResolveClientConfigForWrite(root);
	if (clientConfig && clientConfig->isObject()) {
		(*clientConfig)["hotbar"] = m_hotbar_model->saveToJson();
		config.Save(m_config_path);
		LOG_DEBUG(MOD_CONFIG, "Saved hotbar config to {}", m_config_path);
	}
}

void EverQuest::LoadHotbarConfig() {
	if (m_config_path.empty() || !m_hotbar_model) return;

	auto config = EQ::JsonConfigFile::Load(m_config_path);
	const Json::Value& root = config.RawHandle();

	const Json::Value* clientConfig = ResolveClientConfigForRead(root);
	if (clientConfig && clientConfig->isObject() && clientConfig->isMember("hotbar")) {
		m_hotbar_model->loadFromJson((*clientConfig)["hotbar"]);
		LOG_DEBUG(MOD_CONFIG, "Loaded hotbar config from {}", m_config_path);
	} else {
		LOG_DEBUG(MOD_CONFIG, "No hotbar config found in {}", m_config_path);
	}
}

// T171: the inspect message, persisted per character alongside the hotbar.
//
// This client has to store it locally because the SERVER never tells a
// Titanium client its own: eqemu loads it at zone-in and only ever emits it
// inside an inspect answer about somebody ELSE. Meanwhile every answer we send
// overwrites the server's copy (SendInspectAnswer's hazard note). Holding the
// text locally is therefore not a convenience -- it is the only way this
// client can participate in the inspect protocol without destroying data, and
// it is exactly what a real Titanium client does with its inspect window.
void EverQuest::SetInspectMessage(const std::string& message) {
	// Truncate here rather than at send time so what the user is shown after
	// setting it is what will actually reach the server.
	if (message.size() > EQT::INSPECT_MESSAGE_MAX_LEN) {
		m_inspect_message = message.substr(0, EQT::INSPECT_MESSAGE_MAX_LEN);
		LOG_WARN(MOD_MAIN, "Inspect message truncated to {} characters (was {})",
		         EQT::INSPECT_MESSAGE_MAX_LEN, message.size());
	} else {
		m_inspect_message = message;
	}
}

void EverQuest::SaveInspectMessage() {
	if (m_config_path.empty()) return;

	auto config = EQ::JsonConfigFile::Load(m_config_path);
	Json::Value& root = config.RawHandle();

	Json::Value* clientConfig = ResolveClientConfigForWrite(root);
	if (clientConfig && clientConfig->isObject()) {
		(*clientConfig)["inspectMessage"] = m_inspect_message;
		config.Save(m_config_path);
		LOG_DEBUG(MOD_CONFIG, "Saved inspect message to {}", m_config_path);
	}
}

void EverQuest::LoadInspectMessage() {
	if (m_config_path.empty()) return;

	auto config = EQ::JsonConfigFile::Load(m_config_path);
	const Json::Value& root = config.RawHandle();

	const Json::Value* clientConfig = ResolveClientConfigForRead(root);
	if (clientConfig && clientConfig->isObject() && clientConfig->isMember("inspectMessage") &&
	    (*clientConfig)["inspectMessage"].isString()) {
		SetInspectMessage((*clientConfig)["inspectMessage"].asString());
		LOG_DEBUG(MOD_CONFIG, "Loaded inspect message from {} ({} chars)",
		          m_config_path, m_inspect_message.size());
	}
}

// =============================================================================
// Audio System
// =============================================================================

#ifdef WITH_AUDIO

void EverQuest::InitializeAudio() {
	if (m_audio_manager) {
		return;  // Already initialized
	}

	// Check if audio is disabled via configuration
	if (!m_audio_config_enabled) {
		LOG_INFO(MOD_AUDIO, "Audio disabled by configuration");
		return;
	}

	// Get EQ client path.
	// T43: was #ifdef EQT_HAS_GRAPHICS. m_eq_client_path is plain game config
	// (P28: "no renderer dependency", set unconditionally by
	// SetEQClientPath() since T42) -- gating the read here meant a
	// EQT_GRAPHICS=OFF + WITH_AUDIO=ON build could never find its sound
	// assets and audio silently stayed disabled ("EQ client path not set")
	// even though the path had actually been configured.
	std::string eqPath = m_eq_client_path;

	if (eqPath.empty()) {
		LOG_WARN(MOD_AUDIO, "EQ client path not set, audio disabled");
		return;
	}

	m_audio_manager = std::make_unique<EQT::Audio::AudioManager>();

	// Pass event loop pump as tick callback so network stays alive during slow audio init
	auto networkTick = [this]() {
		TickNetwork();
	};

	if (!m_audio_manager->initialize(eqPath, false, m_audio_config_soundfont, networkTick)) {
		LOG_WARN(MOD_AUDIO, "Failed to initialize audio system");
		m_audio_manager.reset();
		return;
	}

	// Apply memory constraints from constrained rendering config.
	// P28: m_constrained_config/m_constrained_preset are renderer-startup-only
	// state (no meaning without a renderer to constrain); skip when graphics
	// is compiled out rather than guessing at a headless equivalent.
#ifdef EQT_HAS_GRAPHICS
	if (m_constrained_config && m_constrained_config->totalMemoryBudgetBytes > 0) {
		m_audio_manager->setMemoryConstraints(
			m_constrained_config->soundBufferCacheBytes,
			m_constrained_config->lazyPfsLoading);
		LOG_INFO(MOD_AUDIO, "Audio memory constraints: soundCache={}KB, lazyPfs={}",
			m_constrained_config->soundBufferCacheBytes / 1024,
			m_constrained_config->lazyPfsLoading ? "yes" : "no");
	} else {
		auto cfg = EQT::Graphics::ConstrainedRendererConfig::fromPreset(m_constrained_preset);
		if (cfg.totalMemoryBudgetBytes > 0) {
			m_audio_manager->setMemoryConstraints(cfg.soundBufferCacheBytes, cfg.lazyPfsLoading);
			LOG_INFO(MOD_AUDIO, "Audio memory constraints (from preset): soundCache={}KB, lazyPfs={}",
				cfg.soundBufferCacheBytes / 1024, cfg.lazyPfsLoading ? "yes" : "no");
		}
	}
#endif

	// Apply configured volume levels
	m_audio_manager->setMasterVolume(m_audio_config_master_volume);
	m_audio_manager->setMusicVolume(m_audio_config_music_volume);
	m_audio_manager->setEffectsVolume(m_audio_config_effects_volume);

	LOG_DEBUG(MOD_AUDIO, "Audio volumes: master={:.0f}%, music={:.0f}%, effects={:.0f}%",
		m_audio_config_master_volume * 100.0f,
		m_audio_config_music_volume * 100.0f,
		m_audio_config_effects_volume * 100.0f);

	// Pump network before sound preloading
	TickNetwork();

	// Preload common sounds for faster playback
	m_audio_manager->preloadCommonSounds();

	// Phase 13: Initialize zone audio manager for positioned sound emitters
	m_zone_audio_manager = std::make_unique<EQT::Audio::ZoneAudioManager>();
	m_zone_audio_manager->setAudioManager(m_audio_manager.get());
	LOG_DEBUG(MOD_AUDIO, "Zone audio manager initialized");

	LOG_INFO(MOD_AUDIO, "Audio system initialized");
}

void EverQuest::ShutdownAudio() {
	// Shut down zone audio manager first
	if (m_zone_audio_manager) {
		m_zone_audio_manager->unloadZone();
		m_zone_audio_manager.reset();
	}

	if (m_audio_manager) {
		m_audio_manager->shutdown();
		m_audio_manager.reset();
		LOG_INFO(MOD_AUDIO, "Audio system shut down");
	}
}

// D20g: SetupRDPAudio moved to Application::setupRDPAudio()

void EverQuest::UpdateDayNightState() {
	// EverQuest day/night cycle:
	// Day: 6:00 AM (hour 6) to 6:59 PM (hour 18)
	// Night: 7:00 PM (hour 19) to 5:59 AM (hour 5)
	bool wasDay = m_is_daytime;
	m_is_daytime = (m_time_hour >= 6 && m_time_hour < 19);

	// If day/night state changed, notify zone audio manager
	if (wasDay != m_is_daytime && m_zone_audio_manager) {
		m_zone_audio_manager->setDayNight(m_is_daytime);
		LOG_DEBUG(MOD_AUDIO, "Day/night changed to {} (hour {})",
			m_is_daytime ? "day" : "night", m_time_hour);
	}
}

void EverQuest::PlaySound(uint32_t soundId) {
	// Don't play sounds during zone loading
	if (m_audio_manager && m_loading_phase == LoadingPhase::COMPLETE) {
		m_audio_manager->playSound(soundId);
	}
}

void EverQuest::PlaySoundAt(uint32_t soundId, float x, float y, float z) {
	// Don't play sounds during zone loading
	if (m_audio_manager && m_loading_phase == LoadingPhase::COMPLETE) {
		m_audio_manager->playSound(soundId, glm::vec3(x, y, z));
	}
}

void EverQuest::PlayCombatSound(bool hit, float x, float y, float z) {
	// Don't play sounds during zone loading
	if (!m_audio_manager || m_loading_phase != LoadingPhase::COMPLETE) return;

	// Use sound IDs from SoundAssets
	uint32_t soundId = hit ? EQT::Audio::SoundId::MELEE_HIT : EQT::Audio::SoundId::MELEE_SWING;
	m_audio_manager->playSound(soundId, glm::vec3(x, y, z));
}

void EverQuest::PlaySpellSound(uint32_t spellId) {
	// Don't play sounds during zone loading
	if (!m_audio_manager || m_loading_phase != LoadingPhase::COMPLETE) return;

	// Play generic spell cast sound
	// TODO: Could map specific spells to specific sounds if needed
	m_audio_manager->playSound(EQT::Audio::SoundId::SPELL_CAST);
}

void EverQuest::PlayUISound(uint32_t soundId) {
	// Don't play sounds during zone loading
	if (!m_audio_manager || m_loading_phase != LoadingPhase::COMPLETE) return;

	// UI sounds are played at listener position (non-positional)
	m_audio_manager->playSound(soundId);
}

#endif // WITH_AUDIO

// T230: the one item click-effect sender. Extracted verbatim from the
// HotbarActivateIntent Item arm above (the mouse path), which now calls it
// after its own id->slot hop; EqActionHandler::useItem() (the keyboard
// hotbar route, via ActionDispatcher::useItemById) calls it directly with an
// already-resolved slot. The cooldown feedback is no longer tied to the one
// clicked hotbar index: every hotbar slot holding this item id gets the
// HotbarCooldownStarted event, which is what the keyboard route needs (it has
// no intent carrying an index) and is a superset of what the mouse path did.
bool EverQuest::UseInventoryItem(int16_t slot) {
	if (!m_inventory_manager) {
		AddChatSystemMessage("Item not found in inventory");
		return false;
	}
	const auto* item = m_inventory_manager->getItem(slot);
	if (!item) {
		AddChatSystemMessage("Item not found");
		return false;
	}
	if (item->clickEffect.effectId == 0) {
		AddChatSystemMessage(fmt::format("{} has no click effect", item->name));
		return false;
	}
	if (item->clickEffect.type == 5 && slot >= eqt::inventory::GENERAL_BEGIN) {
		AddChatSystemMessage("You must equip this item to use its effect");
		return false;
	}
	EQ::Net::DynamicPacket packet;
	packet.Resize(20);
	packet.PutUInt32(0, 10);  // slot = 10 for item clicks
	packet.PutUInt32(4, static_cast<uint32_t>(item->clickEffect.effectId));
	packet.PutUInt32(8, static_cast<uint32_t>(slot));
	uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
	packet.PutUInt32(12, targetId);
	packet.PutUInt32(16, 0);
	QueuePacket(HC_OP_CastSpell, &packet);
	AddChatSystemMessage(fmt::format("Using {}", item->name));

	// Cooldown feedback to the renderer for every hotbar slot holding this item.
	if (item->clickEffect.recastDelay > 0 && m_bridge && m_hotbar_model) {
		for (int idx = 0; idx < m_hotbar_model->getSlotCount(); ++idx) {
			const eqt::HotbarSlot& hs = m_hotbar_model->getSlot(idx);
			if (hs.type != eqt::HotbarSlotType::Item || hs.id != item->itemId) continue;
			eqt::state::HotbarCooldownStartedData cd;
			cd.index = idx;
			cd.durationMs = static_cast<uint32_t>(item->clickEffect.recastDelay);
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::HotbarCooldownStarted, std::move(cd)));
		}
	}
	return true;
}

// T239: the ONE body for the three command-type hotbar slots, reached from
// the mouse HotbarActivateIntent arm above and from the keyboard route
// (InputActionBridge -> ActionDispatcher::activateHotbarCommandSlot ->
// EqActionHandler -> here). Emote and Macro are the same thing at this level
// -- a line of command text, exactly what the chat window would run -- and go
// through ProcessChatInput so a hotbar key and a typed command take the same
// dispatch path (registry 1, then Application's fallback registries). Combat
// is the auto-attack toggle, the same rule /aa applies
// (CombatManager::ToggleAutoAttack), with a message where /aa is silent.
bool EverQuest::ActivateHotbarCommandSlot(eqt::HotbarSlotType type, const std::string& text) {
	switch (type) {
	case eqt::HotbarSlotType::Emote:
	case eqt::HotbarSlotType::Macro:
		if (text.empty()) {
			AddChatSystemMessage("Hotbar slot has no command");
			return false;
		}
		ProcessChatInput(text);
		return true;
	case eqt::HotbarSlotType::Combat:
		if (!m_combat_manager) return false;
		if (!m_combat_manager->IsAutoAttackEnabled() && !m_combat_manager->HasTarget()) {
			AddChatSystemMessage("You must have a target to attack");
			return false;
		}
		m_combat_manager->ToggleAutoAttack();
		return true;
	case eqt::HotbarSlotType::Empty:
	case eqt::HotbarSlotType::Spell:
	case eqt::HotbarSlotType::Skill:
	case eqt::HotbarSlotType::Item:
		break;
	}
	return false;
}
