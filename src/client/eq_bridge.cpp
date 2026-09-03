// eq_bridge.cpp
//
// EverQuest <-> renderer bridge marshaling layer, split out of eq.cpp (F10)
// and split three ways by J13 (refactor/J13_eq_bridge_split.md).
// Contains graphics lifecycle (Init/Shutdown), the per-frame tick methods,
// zone-load snapshot creation, the spawn/pet/movement bridge helpers, and
// the T269 bank-coin projection helpers.
// The bridge intent/event pumps now live in eq_bridge_intents.cpp; inventory
// stat sync, the loot-window UI flow, client config / hotbar / inspect
// persistence and the audio helpers live in eq_bridge_ui_audio.cpp.
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
#include "client/position_frames.h"
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
	m_merchant->SetupLootCallbacks();
	m_merchant->SetupVendorCallbacks();
	m_merchant->SetupBankCallbacks();
	m_merchant->SetupTrainerCallbacks();
	m_merchant->SetupTradeWindowCallbacks();
	m_merchant->SetupTradeskillCallbacks();

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
		float headingRad = HeadingDeg() * (2.0f * static_cast<float>(M_PI)) / 512.0f;
		float forwardX = std::sin(headingRad);
		float forwardY = -std::cos(headingRad);
		float forwardZ = 0.0f;

		m_audio_manager->setListenerPosition(
			glm::vec3(PosX(), PosY(), PosZ()),
			glm::vec3(forwardX, forwardY, forwardZ),
			glm::vec3(0.0f, 0.0f, 1.0f)  // Up is +Z in EQ coordinates
		);

		if (m_zone_audio_manager && m_loading_phase == LoadingPhase::COMPLETE) {
			m_zone_audio_manager->update(deltaTime, glm::vec3(PosX(), PosY(), PosZ()), m_is_daytime);
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
	snap.zoneName = CurrentZoneName();
	snap.zoneId = CurrentZoneId();
	snap.playerX = PosX();
	snap.playerY = PosY();
	snap.playerZ = PosZ();
	snap.playerHeading = HeadingDeg();
	snap.playerSpawnId = MySpawnId();
	snap.playerName = m_character;
	snap.playerLevel = m_game_state.player().level();
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
	if (m_zone->ZoneLinesPtr() && m_zone->ZoneLinesPtr()->hasZoneLines()) {
		if (m_zone_map) {
			m_zone->ZoneLinesPtr()->expandZoneLinesToGeometry(m_zone_map.get());
		}
		snap.zoneLineBBoxes = m_zone->ZoneLinesPtr()->getZoneLineBoundingBoxes();
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
	if (m_zone_bsp_tree && m_zone_bsp_tree_zone_name == CurrentZoneName()) {
		snap.playerBspRegion = m_zone_bsp_tree->findRegionIndexForPoint(PosX(), PosY(), PosZ());
		if (snap.playerBspRegion == SIZE_MAX) {
			LOG_WARN(MOD_MAIN, "Snapshot: player BSP region is SIZE_MAX (start position outside every "
			         "BSP region) pos=({:.2f},{:.2f},{:.2f}) zone={} -- continuing, nothing consumes it (T260)",
			         PosX(), PosY(), PosZ(), CurrentZoneName());
		} else {
			LOG_INFO(MOD_MAIN, "Snapshot: player BSP region={} pos=({:.2f},{:.2f},{:.2f}) zone={}",
			         snap.playerBspRegion, PosX(), PosY(), PosZ(), CurrentZoneName());
		}
	} else if (m_zone_bsp_tree) {
		LOG_WARN(MOD_MAIN, "Snapshot: m_zone_bsp_tree is stale (captured for '{}', current zone '{}') -- "
		         "skipping region lookup, falling back to no-tree-yet (P17)",
		         m_zone_bsp_tree_zone_name, CurrentZoneName());
	}

	return snap;
}

void EverQuest::SetZoneBspTree(std::shared_ptr<EQT::Graphics::BspTree> bsp, const std::string& builtForZone) {
	if (bsp && builtForZone != CurrentZoneName()) {
		// The finalization of a load for a zone we have already left. Keep
		// whatever LoadZoneSources() built for the current zone (or nothing).
		LOG_WARN(MOD_MAIN, "SetZoneBspTree: rejecting tree built for '{}' (current zone '{}') -- "
		         "late finalization of a previous load, keeping the current zone's tree (T260)",
		         builtForZone, CurrentZoneName());
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
	data.isPlayer = (entity.spawn_id == MySpawnId());
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
		data.spawnId = m_game_state.pet().petSpawnId();
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

	file << "{\n  \"zone\": \"" << CurrentZoneName() << "\",\n  \"entities\": [\n";

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
	if (m_game_state.combat().targetId() == spawn_id) {
		// T286: CombatState is the only store. Cleared unconditionally here
		// (CombatManager::ClearTarget below clears it again, idempotently)
		// so a build without a CombatManager still drops the target.
		m_game_state.combat().clearTarget();
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
	if (spawn_id == MySpawnId()) {
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
	// T310 Leg B / T311: the renderer provides playerZ_, which is the model
	// node's ORIGIN, not the feet -- renderer_player_physics.cpp snaps it to
	// groundZ + collisionZOffset. That is the RENDER frame, and GameState's
	// player z stays in it (it is handed straight back to setPlayerPosition).
	// It is NOT what goes on the wire: SendPositionUpdate() converts to the
	// WIRE frame (groundZ + 0.625*size, measured off the official client in
	// T311 Leg A) at the packet edge. See include/client/position_frames.h.
	// T288: one store, and no PlayerMoved on the state bus -- this is the
	// renderer's own movement arriving, echoing it back as a state event
	// would be a new per-frame event stream (see setPositionNoEvent).
	// A z the RENDERER produced is definitively in the render frame, whatever
	// state the self-spawn left behind (T311).
	m_game_state.player().setZAwaitingFrameConversion(false);
	SetPosLocal(update.x, update.y, update.z);  // node-origin (render-frame) Z from the renderer
	// Convert heading from EQ format (0-512) to degrees (0-360) for internal use
	SetHeadingLocal(update.heading * 360.0f / 512.0f);

	// Check water state at new position
	m_movement->UpdateWaterState();

	// Derive movement state from velocity
	float speed2D = std::sqrt(update.dx * update.dx + update.dy * update.dy);
	float speed3D = std::sqrt(speed2D * speed2D + update.dz * update.dz);
	bool isMoving = (m_water_state != WaterState::NotInWater) ? speed3D > 0.01f : speed2D > 0.01f;
	m_game_state.player().setMoving(isMoving);

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
			m_game_state.player().setAnimation(7);  // Swim treading (l09)
		} else {
			m_game_state.player().setAnimation(6);  // Swim idle (p06)
		}
	} else {
		// Normal ground animations
		// Negative animation = play in reverse (e.g., walking backward)
		if (!isMoving) {
			m_game_state.player().setAnimation(ANIM_STAND);
		} else if (speed2D > 5.0f) {  // Running threshold
			m_game_state.player().setAnimation(isMovingBackward ? -ANIM_RUN : ANIM_RUN);
		} else {
			m_game_state.player().setAnimation(isMovingBackward ? -ANIM_WALK : ANIM_WALK);
		}
	}

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MOVEMENT, "OnGraphicsMovement: pos=({:.2f},{:.2f},{:.2f}) heading={:.1f} vel=({:.2f},{:.2f},{:.2f}) anim={} water={}",
			update.x, update.y, update.z, update.heading, update.dx, update.dy, update.dz, m_game_state.player().animation(),
			static_cast<int>(m_water_state));
	}

	// Check for zone line collision
	m_zone->CheckZoneLine();

	// SendPositionUpdate has internal 250ms throttling
	m_movement->SendPositionUpdate();
}

// ============================================================================
// T269: bank-coin helpers, shared by the two Bank* intent arms below.
// ============================================================================

uint32_t EverQuest::BankCoinGet(uint8_t coinType, bool bank) const {
	const auto& player = m_game_state.player();
	switch (coinType) {
	case EQT::COINTYPE_CP: return bank ? player.bankCopper() : player.copper();
	case EQT::COINTYPE_SP: return bank ? player.bankSilver() : player.silver();
	case EQT::COINTYPE_GP: return bank ? player.bankGold() : player.gold();
	case EQT::COINTYPE_PP: return bank ? player.bankPlatinum() : player.platinum();
	default: return 0;
	}
}

void EverQuest::BankCoinSet(uint8_t coinType, bool bank, uint32_t value) {
	auto& player = m_game_state.player();
	switch (coinType) {
	case EQT::COINTYPE_CP: bank ? player.setBankCopper(value) : player.setCopper(value); break;
	case EQT::COINTYPE_SP: bank ? player.setBankSilver(value) : player.setSilver(value); break;
	case EQT::COINTYPE_GP: bank ? player.setBankGold(value) : player.setGold(value); break;
	case EQT::COINTYPE_PP: bank ? player.setBankPlatinum(value) : player.setPlatinum(value); break;
	default: break;
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
	// T286: the counters ARE m_game_state.player()'s -- the mirror-into-
	// GameState step this used to do here is gone with the duplicate.
	if (!m_bridge) {
		return;
	}
	if (inventoryChanged) {
		eqt::state::CurrencyChangedData cdata;
		cdata.platinum = static_cast<int32_t>(m_game_state.player().platinum());
		cdata.gold = static_cast<int32_t>(m_game_state.player().gold());
		cdata.silver = static_cast<int32_t>(m_game_state.player().silver());
		cdata.copper = static_cast<int32_t>(m_game_state.player().copper());
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(cdata)));
	}

	eqt::state::BankCurrencyChangedData bdata;
	bdata.platinum = static_cast<int32_t>(m_game_state.player().bankPlatinum());
	bdata.gold = static_cast<int32_t>(m_game_state.player().bankGold());
	bdata.silver = static_cast<int32_t>(m_game_state.player().bankSilver());
	bdata.copper = static_cast<int32_t>(m_game_state.player().bankCopper());
	m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::BankCurrencyChanged, std::move(bdata)));
}

