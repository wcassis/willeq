// bridge_component.cpp
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
#include "client/corpse_predicate.h"  // T456 (B478): the ONE corpse predicate
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
#include "client/core/memory_ledger.h"
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
// K16 (B006): InitGraphics drives the renderer through this two-method
// surface; the umbrella renderer header below is still needed by the rest of
// this TU.
#include "client/graphics/zone_load_surface.h"
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

bool eqt::bridge::BridgeComponent::InitGraphics(int width, int height,
                             EQT::Graphics::IZoneLoadSurface* renderer,
                             eqt::bridge::GameStateBridge* bridge) {
	if (m_graphics_initialized) {
		return true;
	}

	if (ctx_.eq_client_path.empty()) {
		LOG_ERROR(MOD_GRAPHICS, "EQ client path not set. Call SetEQClientPath() first.");
		return false;
	}

	// B839 (issue #94): the renderer bring-up -- initLoadingScreen's
	// Irrlicht device, fonts, UI atlas and the audio system it starts at
	// the end -- is the "graphics" sub-owner. On the Pi this is the
	// dominant startup owner (the 28 MB bucket is mostly device/driver,
	// which the allocator ledger cannot see at all -- see run()'s
	// recordMappings); this row names what the ledger CAN see of it.
	eqt::memory::StartupScope graphics("graphics");

	// D20e3: Store bridge only — renderer is owned by Application.
	// T46: wireBridgeToSubsystems() sets m_bridge and also wires the four
	// managers' own bridge pointers (previously done inline here only,
	// which left the managers unwired headless -- setBridge() now calls
	// the same helper so both paths agree).
	wireBridgeToSubsystems(bridge);

	EQT::Graphics::RendererConfig config;
	config.width = width;
	config.height = height;
	config.eqClientPath = ctx_.eq_client_path;
	config.regionMapsPath = m_region_maps_path;
	config.windowTitle = "WillEQ - " + ctx_.character;
	config.lighting = false;
	config.constrainedPreset = m_constrained_preset;
	if (m_constrained_config) {
		config.constrainedPreset = EQT::Graphics::ConstrainedRenderingPreset::Custom;
		config.constrainedConfig = *m_constrained_config;
	}

	if (!renderer->initLoadingScreen(config)) {
		LOG_ERROR(MOD_GRAPHICS, "Failed to initialize renderer loading screen");
		ctx_.bridge = nullptr;
		return false;
	}

	// Pump network after Irrlicht device creation (can take 0.5-1s on ARM)
	ctx_.lifecycle.TickNetwork();

	// Set collision map via bridge (shared_ptr copy, P35 — see eq.h)
	if (ctx_.Zone().ZoneMap() && ctx_.bridge) {
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CollisionMapChanged,
			eqt::state::CollisionMapChangedData{ctx_.Zone().ZoneMap()}));
	}

	// One-time deferred renderer init (render distance, spell VFX)
	if (renderer) {
		renderer->initDeferredGraphics();
	}

	// Setup window callbacks (all empty stubs since D20a/D20b1)
	ctx_.Merchant().SetupLootCallbacks();
	ctx_.Merchant().SetupVendorCallbacks();
	ctx_.Merchant().SetupBankCallbacks();
	ctx_.Merchant().SetupTrainerCallbacks();
	// B896 (#212): the trade window's empty stub was deleted; the window is
	// fed by the trade events SetupTradeManagerCallbacks() publishes.
	ctx_.Merchant().SetupTradeskillCallbacks();

	// S2: movement callback removed — position flows via PlayerPositionChanged intent only

	m_graphics_initialized = true;
	LOG_INFO(MOD_GRAPHICS, "Renderer initialized successfully ({}x{})", width, height);

#ifdef WITH_AUDIO
	InitializeAudio();
#endif

	// If zone is already fully connected when graphics init, set zone ready
	if (ctx_.self.IsFullyZonedIn() && ctx_.bridge) {
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::NetworkReady,
			eqt::state::NetworkReadyData{static_cast<uint32_t>(ctx_.entities.size()), true}));
		LOG_DEBUG(MOD_GRAPHICS, "Zone already ready, enabling rendering with {} entities", ctx_.entities.size());
	}

	return true;
}

// SignalGraphicsLoadReady removed — loading thread starts directly from
// Application::gameThreadLoop when ConsumeZoneLoadRequest fires.

void eqt::bridge::BridgeComponent::ShutdownGraphics() {
	// D20e2: Loading thread now owned by Application — it joins before calling this.

	if (ctx_.inventory_manager) {
		ctx_.inventory_manager->clearAll();
		ctx_.inventory_manager.reset();
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

// T341 (B038 cluster 1): moved verbatim from EverQuest
// (src/client/core/everquest.cpp) with the two members it writes.
void eqt::bridge::BridgeComponent::SetConstrainedConfig(const EQT::Graphics::ConstrainedRendererConfig& config) {
	m_constrained_config = config;
	m_constrained_preset = EQT::Graphics::ConstrainedRenderingPreset::Custom;
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
void eqt::bridge::BridgeComponent::wireBridgeToSubsystems(eqt::bridge::GameStateBridge* bridge) {
	ctx_.bridge = bridge;

	// Plain pointer assignment on each manager's own setBridge() -- safe to
	// call repeatedly (idempotent) and safe to call with bridge == nullptr
	// (propagates the clear rather than silently dropping it), though
	// nothing calls this with nullptr today (see deferred.md/T46 Review for
	// the teardown-lifecycle finding).
	if (ctx_.inventory_manager) {
		ctx_.inventory_manager->setBridge(bridge);
	}
	if (ctx_.spell_manager) {
		ctx_.spell_manager->setBridge(bridge);
	}
	if (ctx_.skill_manager) {
		ctx_.skill_manager->setBridge(bridge);
	}
	if (ctx_.hotbar_model) {
		ctx_.hotbar_model->setBridge(bridge);
		ctx_.hotbar_model->setSaveCallback([this]() { SaveHotbarConfig(); });
	}
}

void eqt::bridge::BridgeComponent::setBridge(eqt::bridge::GameStateBridge* bridge) {
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
void eqt::bridge::BridgeComponent::GameTick(float deltaTime) {
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
	if (ctx_.spell_manager) {
		ctx_.spell_manager->update(deltaTime);
	}

	// Update buff manager (buff durations, expirations)
	if (ctx_.buff_manager) {
		ctx_.buff_manager->update(deltaTime);
	}

	// Process intents from renderer's previous frame
	ProcessBridgeIntents();

	// B897: load/save the friends and ignore lists and tell the window.
	// After the intents, so a window edit is persisted in the same tick.
	SyncSocialLists();
}

// T220: PreRenderTick() (D21a's render-thread event drain) is DELETED. In a
// graphics run the bridge drain is now two-stage and owned by Application:
// updateGameState() -> GameStateSyncBridge::stagePendingEvents() on the
// game thread, render() -> GameStateSyncBridge::forwardStagedEvents() on
// the render thread. ProcessBridgeEvents() below remains the single-stage
// drain for headless runs (and single-threaded test harnesses).

// D20c1: Post-render tick — audio sync, progressive loading check
void eqt::bridge::BridgeComponent::PostRenderTick(float deltaTime) {
#ifdef EQT_HAS_GRAPHICS
	if (!m_graphics_initialized) return;
#endif

#ifdef WITH_AUDIO
	// T334 (B045): body moved to UpdateAudioListener() in
	// bridge_component_ui_audio.cpp, with D20c2's player-position rationale,
	// and its heading->forward conversion corrected there.
	UpdateAudioListener(deltaTime);
#else
	(void)deltaTime;
#endif
}

#ifdef EQT_HAS_GRAPHICS

// D20c3: Create immutable snapshot of game state for zone loading thread
eqt::ZoneLoadSnapshot eqt::bridge::BridgeComponent::CreateZoneLoadSnapshot() const {
	eqt::ZoneLoadSnapshot snap;
	snap.zoneName = ctx_.self.CurrentZoneName();
	snap.zoneId = ctx_.self.CurrentZoneId();
	snap.playerX = ctx_.self.PosX();
	snap.playerY = ctx_.self.PosY();
	snap.playerZ = ctx_.self.PosZ();
	snap.playerHeading = ctx_.self.HeadingDeg();
	snap.playerSpawnId = ctx_.self.MySpawnId();
	snap.playerName = ctx_.character;
	snap.playerLevel = ctx_.game_state.player().level();
	snap.skyType = ctx_.Zone().ZoneSkyType();
	snap.zoneType = ctx_.Zone().ZoneTypeCode();
	for (int i = 0; i < 4; i++) {
		snap.fogRed[i] = ctx_.Zone().ZoneFogRed()[i];
		snap.fogGreen[i] = ctx_.Zone().ZoneFogGreen()[i];
		snap.fogBlue[i] = ctx_.Zone().ZoneFogBlue()[i];
		snap.fogMinClip[i] = ctx_.Zone().ZoneFogMinClip()[i];
		snap.fogMaxClip[i] = ctx_.Zone().ZoneFogMaxClip()[i];
	}
	// Copy entities
	snap.entities.reserve(ctx_.entities.size());
	for (const auto& [id, entity] : ctx_.entities) {
		snap.entities.push_back(entity);
	}
	// Copy doors
	for (const auto& [id, door] : ctx_.game_state.doors().getAllDoors()) {
		snap.doors.push_back(door);
	}
	// Zone lines (expand geometry + extract bounding boxes)
	if (ctx_.Zone().ZoneLinesPtr() && ctx_.Zone().ZoneLinesPtr()->hasZoneLines()) {
		if (ctx_.Zone().ZoneMap()) {
			ctx_.Zone().ZoneLinesPtr()->expandZoneLinesToGeometry(ctx_.Zone().ZoneMap().get());
		}
		snap.zoneLineBBoxes = ctx_.Zone().ZoneLinesPtr()->getZoneLineBoundingBoxes();
	}
	snap.zoneMap = ctx_.Zone().ZoneMap();
	snap.eqClientPath = ctx_.eq_client_path;

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
	if (ctx_.Zone().ZoneBspTree() && ctx_.Zone().ZoneBspTreeZoneName() == ctx_.self.CurrentZoneName()) {
		snap.playerBspRegion = ctx_.Zone().ZoneBspTree()->findRegionIndexForPoint(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ());
		if (snap.playerBspRegion == SIZE_MAX) {
			LOG_WARN(MOD_MAIN, "Snapshot: player BSP region is SIZE_MAX (start position outside every "
			         "BSP region) pos=({:.2f},{:.2f},{:.2f}) zone={} -- continuing, nothing consumes it (T260)",
			         ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), ctx_.self.CurrentZoneName());
		} else {
			LOG_INFO(MOD_MAIN, "Snapshot: player BSP region={} pos=({:.2f},{:.2f},{:.2f}) zone={}",
			         snap.playerBspRegion, ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), ctx_.self.CurrentZoneName());
		}
	} else if (ctx_.Zone().ZoneBspTree()) {
		LOG_WARN(MOD_MAIN, "Snapshot: m_zone_bsp_tree is stale (captured for '{}', current zone '{}') -- "
		         "skipping region lookup, falling back to no-tree-yet (P17)",
		         ctx_.Zone().ZoneBspTreeZoneName(), ctx_.self.CurrentZoneName());
	}

	return snap;
}

void eqt::bridge::BridgeComponent::SetZoneBspTree(std::shared_ptr<EQT::Graphics::BspTree> bsp, const std::string& builtForZone) {
	if (bsp && builtForZone != ctx_.self.CurrentZoneName()) {
		// The finalization of a load for a zone we have already left. Keep
		// whatever LoadZoneSources() built for the current zone (or nothing).
		LOG_WARN(MOD_MAIN, "SetZoneBspTree: rejecting tree built for '{}' (current zone '{}') -- "
		         "late finalization of a previous load, keeping the current zone's tree (T260)",
		         builtForZone, ctx_.self.CurrentZoneName());
		return;
	}
	ctx_.Zone().ZoneBspTree() = std::move(bsp);
	ctx_.Zone().ZoneBspTreeZoneName() = ctx_.Zone().ZoneBspTree() ? builtForZone : std::string();
}

// D20e2: StartLoadingThread, JoinLoadingThread moved to Application

// D20e2: LoadZoneGraphicsOnLoadingThread and LoadZoneGraphics moved to Application
#endif

// NOTE: OnZoneLoadedGraphics() has been removed. Graphics loading is now handled
// by LoadZoneGraphics() which is called from OnGameStateComplete() when the
// game state is ready. This ensures the progress bar shows during the entire
// loading process from login to gameplay.

void eqt::bridge::BridgeComponent::OnSpawnAddedGraphics(const Entity& entity) {
	if (!ctx_.bridge) {
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
	if (entity.name == ctx_.character) {
		LOG_DEBUG(MOD_ENTITY, "Skipping player entity {} ({}) in OnSpawnAddedGraphics - will create after fully connected",
		          entity.spawn_id, entity.name);
		return;
	}

	// T456 (board B478): THE corpse predicate, not a fourth copy of it.
	// What was here recomputed npc_type 2/3 inline and then fell back to a
	// bare name.find("corpse") with no trailing-id check -- which classifies a
	// LIVE npc named "a corpse beetle" as a corpse and freezes it in the death
	// pose. IsCorpseByFields() is the flag, then the authoritative
	// Spawn_Struct.NPC byte (eqemu writes 2 for every corpse --
	// /mnt/projects/akk-stack/code/zone/corpse.cpp:1809), then eqemu's three
	// real corpse name forms WITH their mandatory id.
	const bool isCorpse =
	    IsCorpseByFields(entity.is_corpse, entity.npc_type, entity.name);

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
	data.isPlayer = (entity.spawn_id == ctx_.self.MySpawnId());
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
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::EntitySpawned, std::move(data)));

	// Set entity light source if equipped
	if (entity.light > 0) {
		eqt::state::EntityLightChangedData lightData;
		lightData.spawnId = entity.spawn_id;
		lightData.lightLevel = entity.light;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
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
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityPoseStateChanged, std::move(poseData)));
			eqt::state::EntityAnimationEventData animData;
			animData.spawnId = entity.spawn_id;
			animData.animCode = entity.animation;
			animData.animName = animCode;
			animData.loop = true;
			animData.playThrough = false;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityAnimationEvent, std::move(animData)));
			LOG_DEBUG(MOD_ENTITY, "Set initial pose for {} (ID: {}) to {} (anim={})",
			          entity.name, entity.spawn_id, animCode, entity.animation);
		}
	}
}

void eqt::bridge::BridgeComponent::OnPetCreated(const Entity& pet) {
	LOG_DEBUG(MOD_MAIN, "Pet window: Pet created - {} (ID: {}) Level {}",
	          pet.name, pet.spawn_id, pet.level);

	// T221: source-side write (stage 2 of Option A) -- the READ model's
	// PetCreated projection (GameStateSyncBridge) becomes redundant with this.
	ctx_.game_state.pet().setPet(pet.spawn_id, pet.name, pet.level);

	// Publish PetCreated event to bridge (bridge handler opens pet window)
	if (ctx_.bridge) {
		eqt::state::PetCreatedData data;
		data.spawnId = pet.spawn_id;
		data.name = pet.name;
		data.level = pet.level;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PetCreated, std::move(data)));
	}
}

void eqt::bridge::BridgeComponent::OnPetRemoved() {
	LOG_DEBUG(MOD_MAIN, "Pet window: Pet removed");

	// T221: source-side write; PetRemoved projection redundant.
	ctx_.game_state.pet().clearPet();

	// PetRemoved bridge event drives closePetWindow() via bridge handler
	if (ctx_.bridge) {
		eqt::state::PetRemovedData data;
		data.spawnId = ctx_.game_state.pet().petSpawnId();
		data.name = "";
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PetRemoved, std::move(data)));
	}
}

void eqt::bridge::BridgeComponent::OnPetButtonStateChanged(EQT::PetButton button, bool state) {
	LOG_DEBUG(MOD_MAIN, "Pet button state changed: {} = {}",
	          EQT::GetPetButtonName(button), state ? "ON" : "OFF");

	// T221: source-side write; PetButtonStateChanged projection redundant.
	ctx_.game_state.pet().setButtonState(static_cast<uint8_t>(button), state);

	// Publish PetButtonStateChanged event to bridge
	if (ctx_.bridge) {
		eqt::state::PetButtonStateChangedData data;
		data.button = static_cast<uint8_t>(button);
		data.state = state;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PetButtonStateChanged, std::move(data)));
	}
}

void eqt::bridge::BridgeComponent::OnSpawnRemovedGraphics(uint16_t spawn_id) {
	// Clear target if this entity was targeted (game state update)
	if (ctx_.game_state.combat().targetId() == spawn_id) {
		// T286: CombatState is the only store. Cleared unconditionally here
		// (CombatManager::ClearTarget below clears it again, idempotently)
		// so a build without a CombatManager still drops the target.
		ctx_.game_state.combat().clearTarget();
		if (ctx_.combat_manager) {
			ctx_.combat_manager->ClearTarget();
		}
		// Publish TargetChanged (clear) event
		if (ctx_.bridge) {
			eqt::state::TargetChangedData tdata;  // spawnId=0 = target cleared
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TargetChanged, std::move(tdata)));
		}
	}

	if (!ctx_.bridge) {
		return;
	}

	// Check if this is a corpse - corpses should fade out instead of vanishing instantly
	auto it = ctx_.entities.find(spawn_id);
	if (it != ctx_.entities.end() && it->second.is_corpse) {
		eqt::state::CorpseDecayStartedData data;
		data.spawnId = spawn_id;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CorpseDecayStarted, std::move(data)));
	}

	// Publish EntityDespawned event (D17b: bridge is sole path to renderer)
	eqt::state::EntityDespawnedData data;
	data.spawnId = spawn_id;
	if (it != ctx_.entities.end()) {
		data.name = it->second.name;
	}
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::EntityDespawned, std::move(data)));
}

void eqt::bridge::BridgeComponent::OnSpawnMovedGraphics(uint16_t spawn_id, float x, float y, float z, float heading,
                                      float dx, float dy, float dz, int32_t animation) {
	if (!ctx_.bridge) {
		return;
	}

	// If this is our player entity, publish player position update
	// so the camera follows the server-authoritative position
	if (spawn_id == ctx_.self.MySpawnId()) {
		// T426 (board B399): the CAMERA takes OUR heading in the client's
		// INTERNAL convention -- `90 - server_deg`, the player-only flip every
		// inbound self-heading edge applies (ZoneProcessPlayerProfile,
		// RelocateWithinZone, the self branch of ZoneProcessClientUpdate) and
		// SendPositionUpdate inverts on the way out. `heading` here is the RAW
		// server degrees the packet carried, which is correct for the ENTITY
		// row below (other entities' models take a server heading verbatim --
		// docs/CLAUDE.md, Heading/Rotation Conventions) and WRONG for the
		// camera. ZoneProcessClientUpdate's self branch computes the internal
		// value into ctx_.self a hundred lines before it calls this, then
		// passed the raw one; so every server-forced relocation that arrives
		// as a self OP_ClientUpdate (zone-in, a GM move with no zone-change
		// packet, a server position correction) pointed the camera somewhere
		// other than where the server aimed us, and it stayed there until the
		// player turned. Read the store instead of trusting the argument.
		float heading_512 = ctx_.self.HeadingDeg() * 512.0f / 360.0f;
		eqt::state::PlayerMovedData pdata;
		pdata.x = x; pdata.y = y; pdata.z = z;
		pdata.heading = heading_512;
		pdata.dx = dx; pdata.dy = dy; pdata.dz = dz;
		// B912: isMoving on the renderer bridge means "the GAME thread is
		// walking us" (MovementComponent::PublishSelfPoseToRenderer); the
		// renderer latches it until the next PlayerMoved. A server self-update
		// carrying velocity is not such a walk, and would latch it on.
		pdata.isMoving = ctx_.Movement().HasCommandMovement();
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
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
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::EntityMoved, std::move(data)));
}

void eqt::bridge::BridgeComponent::OnGraphicsMovement(const eqt::events::PlayerPositionChanged& update) {
	// Called by the renderer when the player moves in Player Mode
	// This syncs the player's position with the server

	if (!ctx_.bridge) {
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
	// A z the RENDERER PRODUCED is definitively in the render frame, whatever
	// state the self-spawn left behind (T311).
	//
	// T332 (B049), MEASURED LIVE by T331: that premise is about PROVENANCE, and
	// this code used to apply it to every intent. At zone-in the renderer does
	// not produce our z -- it is SEEDED with it (setPlayerPosition, from the
	// zone-load snapshot, which carries the raw WIRE z straight off
	// OP_PlayerProfile) and echoes it straight back. Relabelling that echo
	// "render frame" cleared the pending conversion without performing it, so
	// SendPositionUpdate then added 0.625*size to a z that already contained
	// it -- and because the server persists what we send and hands it back on
	// the next login, the error COMPOUNDED 0.625 every login (Testchar:
	// 3.75 -> 4.38 -> 5.01 -> 5.63, against the official client's steady 3.751).
	// Headless there is no renderer and so no echo, which is why T311's
	// three-login test was green and wrong.
	//
	// So: an echo is not information. Do not store it (we already hold exactly
	// what we handed over), and take it as the cue to run the deferred
	// conversion instead -- which is a no-op until the mesh offset is
	// measurable, leaving the flag set for onPlayerModelGeometryChanged.
	if (update.seededPosition) {
		ConvertSelfZToRenderFrame(ctx_.game_state.player().collisionZOffset());
	} else {
		ctx_.game_state.player().setZAwaitingFrameConversion(false);
		ctx_.self.SetPosLocal(update.x, update.y, update.z);  // node-origin (render-frame) Z from the renderer
	}
	// Convert heading from EQ format (0-512) to degrees (0-360) for internal use
	ctx_.self.SetHeadingLocal(update.heading * 360.0f / 512.0f);

	// Check water state at new position
	ctx_.Movement().UpdateWaterState();

	// Derive movement state from velocity
	float speed2D = std::sqrt(update.dx * update.dx + update.dy * update.dy);
	float speed3D = std::sqrt(speed2D * speed2D + update.dz * update.dz);
	bool isMoving = (ctx_.Movement().GetWaterState() != WaterState::NotInWater) ? speed3D > 0.01f : speed2D > 0.01f;
	ctx_.game_state.player().setMoving(isMoving);

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
	if (ctx_.Movement().GetWaterState() != WaterState::NotInWater) {
		// Swimming animations
		// Animation code 7 = swimming/treading, 6 = swim idle
		if (isMoving) {
			ctx_.game_state.player().setAnimation(7);  // Swim treading (l09)
		} else {
			ctx_.game_state.player().setAnimation(6);  // Swim idle (p06)
		}
	} else {
		// Normal ground animations
		// Negative animation = play in reverse (e.g., walking backward)
		if (!isMoving) {
			ctx_.game_state.player().setAnimation(ANIM_STAND);
		} else if (speed2D > 5.0f) {  // Running threshold
			ctx_.game_state.player().setAnimation(isMovingBackward ? -ANIM_RUN : ANIM_RUN);
		} else {
			ctx_.game_state.player().setAnimation(isMovingBackward ? -ANIM_WALK : ANIM_WALK);
		}
	}

	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MOVEMENT, "OnGraphicsMovement: pos=({:.2f},{:.2f},{:.2f}) heading={:.1f} vel=({:.2f},{:.2f},{:.2f}) anim={} water={}",
			update.x, update.y, update.z, update.heading, update.dx, update.dy, update.dz, ctx_.game_state.player().animation(),
			static_cast<int>(ctx_.Movement().GetWaterState()));
	}

	// Check for zone line collision
	ctx_.Zone().CheckZoneLine();

	// SendPositionUpdate has internal 250ms throttling
	ctx_.Movement().SendPositionUpdate();
}

// ============================================================================
// T269: bank-coin helpers, shared by the two Bank* intent arms below.
// ============================================================================

uint32_t eqt::bridge::BridgeComponent::BankCoinGet(uint8_t coinType, bool bank) const {
	const auto& player = ctx_.game_state.player();
	switch (coinType) {
	case EQT::COINTYPE_CP: return bank ? player.bankCopper() : player.copper();
	case EQT::COINTYPE_SP: return bank ? player.bankSilver() : player.silver();
	case EQT::COINTYPE_GP: return bank ? player.bankGold() : player.gold();
	case EQT::COINTYPE_PP: return bank ? player.bankPlatinum() : player.platinum();
	default: return 0;
	}
}

void eqt::bridge::BridgeComponent::BankCoinSet(uint8_t coinType, bool bank, uint32_t value) {
	auto& player = ctx_.game_state.player();
	switch (coinType) {
	case EQT::COINTYPE_CP: bank ? player.setBankCopper(value) : player.setCopper(value); break;
	case EQT::COINTYPE_SP: bank ? player.setBankSilver(value) : player.setSilver(value); break;
	case EQT::COINTYPE_GP: bank ? player.setBankGold(value) : player.setGold(value); break;
	case EQT::COINTYPE_PP: bank ? player.setBankPlatinum(value) : player.setPlatinum(value); break;
	default: break;
	}
}

const char* eqt::bridge::BridgeComponent::BankCoinName(uint8_t coinType) {
	switch (coinType) {
	case EQT::COINTYPE_CP: return "copper";
	case EQT::COINTYPE_SP: return "silver";
	case EQT::COINTYPE_GP: return "gold";
	case EQT::COINTYPE_PP: return "platinum";
	default: return "coins";
	}
}

// The server's own table (zone/client_process.cpp CoinTypeCoppers).
int64_t eqt::bridge::BridgeComponent::BankCoinCoppers(uint8_t coinType) {
	switch (coinType) {
	case EQT::COINTYPE_PP: return 1000;
	case EQT::COINTYPE_GP: return 100;
	case EQT::COINTYPE_SP: return 10;
	default: return 1;
	}
}

void eqt::bridge::BridgeComponent::PublishBankCoinProjection(bool inventoryChanged) {
	// T286: the counters ARE m_game_state.player()'s -- the mirror-into-
	// GameState step this used to do here is gone with the duplicate.
	if (!ctx_.bridge) {
		return;
	}
	if (inventoryChanged) {
		eqt::state::CurrencyChangedData cdata;
		cdata.platinum = static_cast<int32_t>(ctx_.game_state.player().platinum());
		cdata.gold = static_cast<int32_t>(ctx_.game_state.player().gold());
		cdata.silver = static_cast<int32_t>(ctx_.game_state.player().silver());
		cdata.copper = static_cast<int32_t>(ctx_.game_state.player().copper());
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(cdata)));
	}

	eqt::state::BankCurrencyChangedData bdata;
	bdata.platinum = static_cast<int32_t>(ctx_.game_state.player().bankPlatinum());
	bdata.gold = static_cast<int32_t>(ctx_.game_state.player().bankGold());
	bdata.silver = static_cast<int32_t>(ctx_.game_state.player().bankSilver());
	bdata.copper = static_cast<int32_t>(ctx_.game_state.player().bankCopper());
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::BankCurrencyChanged, std::move(bdata)));
}

// ---------------------------------------------------------------------------
// T341 (B038 cluster 2): the loading-phase state machine, moved verbatim from
// EverQuest (src/client/core/everquest.cpp). It writes m_loading_phase and
// m_loading_status_text, signals the graphics-guarded m_loading_status_ptr
// (T341 cluster 1, now this component's too) and publishes ZoneLoading -- all
// three of those now live here, which is why the machine does. EverQuest keeps
// six one-line forwarders (eq.h) for Application and the tests.
//
// Substitutions, and nothing else: m_bridge -> ctx_.bridge,
// m_player_facade -> ctx_.self.
void eqt::bridge::BridgeComponent::SetLoadingPhase(LoadingPhase phase, const char* statusText) {
	if (m_loading_phase == phase) {
		return;  // No change
	}

	m_loading_phase = phase;
	if (statusText) {
		m_loading_status_text = statusText;
	} else {
		m_loading_status_text = GetLoadingStatusText();
	}

	int phaseNum = static_cast<int>(phase);
	float progress = GetLoadingProgress();
	LOG_INFO(MOD_MAIN, "[LOADING][Phase {}/15] ({:.0f}%) {}",
	         phaseNum, progress * 100.0f, m_loading_status_text);

	// T491 lifecycle points on the game-thread side of the machine: the world
	// handshake done (character select reached) and the zone fully ready.
	// "ready" also arms the steady-state (60 s) point.
	if (phase == LoadingPhase::WORLD_CHARACTER_SELECT) {
		eqt::memory::recordPoint("login_world");
	} else if (phase == LoadingPhase::COMPLETE) {
		eqt::memory::recordPoint("ready", ctx_.self.CurrentZoneName());
		// B841: the arena census at the same instant -- where the slack sits
		// (top pad vs interior free chunks vs mmap) is answered by the
		// [memarena] line, and its delta against the steady_60s one says
		// whether the freed bytes landed in a trimmable place. Experiment
		// (a) of B841's card: B841_TRIM_READY=1 trims the top pad ONCE here,
		// so the census shows the state a trim leaves, not the state before.
		eqt::memory::recordArena("ready", eqt::memory::arenaExperiments().trimAtReady);
		// T491 addendum (B801): the second mapping census. Diffing it against
		// the startup one says how much of a zone-in is heap and how much is
		// driver/mmap the ledger can never charge.
		// T500 (Will's addendum): the FULL census, every owner row, not the top 16.
		eqt::memory::recordMappings("ready", 0);
		eqt::memory::noteReady();
	}

	// Write to LoadingStatus for the loading thread's passive display loop
#ifdef EQT_HAS_GRAPHICS
	if (m_loading_status_ptr) {
		m_loading_status_ptr->setProgress(static_cast<int>(progress * 100.0f), m_loading_status_text);
	}
#endif

	// Publish ZoneLoading event to bridge (also drives loading screen progress via bridge handler)
	if (ctx_.bridge) {
		eqt::state::ZoneLoadingData data;
		data.zoneName = ctx_.self.CurrentZoneName();
		data.zoneId = ctx_.self.CurrentZoneId();
		data.progress = progress;
		data.statusMessage = m_loading_status_text ? m_loading_status_text : "";
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ZoneLoading, std::move(data)));
	}
}

float eqt::bridge::BridgeComponent::GetLoadingProgress() const {
	switch (m_loading_phase) {
		// Connection phases: 0% -> 25%
		case LoadingPhase::DISCONNECTED:             return 0.00f;
		case LoadingPhase::LOGIN_CONNECTING:         return 0.02f;
		case LoadingPhase::LOGIN_AUTHENTICATING:     return 0.05f;
		case LoadingPhase::WORLD_CONNECTING:         return 0.10f;
		case LoadingPhase::WORLD_CHARACTER_SELECT:   return 0.15f;
		case LoadingPhase::ZONE_CONNECTING:          return 0.20f;
		// Game state phases: 25% -> 50%
		case LoadingPhase::ZONE_RECEIVING_PROFILE:   return 0.25f;
		case LoadingPhase::ZONE_RECEIVING_SPAWNS:    return 0.30f;
		case LoadingPhase::ZONE_REQUEST_PHASE:       return 0.35f;
		case LoadingPhase::ZONE_PLAYER_READY:        return 0.40f;
		case LoadingPhase::ZONE_AWAITING_CONFIRM:    return 0.45f;
		// Graphics loading phases: 50% -> 100%
		case LoadingPhase::GRAPHICS_LOADING_ZONE:    return 0.50f;
		case LoadingPhase::GRAPHICS_LOADING_MODELS:  return 0.65f;
		case LoadingPhase::GRAPHICS_CREATING_ENTITIES: return 0.80f;
		case LoadingPhase::GRAPHICS_FINALIZING:      return 0.95f;
		case LoadingPhase::COMPLETE:                 return 1.00f;
	}
	return 0.0f;
}

const char* eqt::bridge::BridgeComponent::GetLoadingStatusText() const {
	switch (m_loading_phase) {
		case LoadingPhase::DISCONNECTED:             return "";
		case LoadingPhase::LOGIN_CONNECTING:         return "Connecting to login server...";
		case LoadingPhase::LOGIN_AUTHENTICATING:     return "Authenticating...";
		case LoadingPhase::WORLD_CONNECTING:         return "Connecting to world server...";
		case LoadingPhase::WORLD_CHARACTER_SELECT:   return "Loading characters...";
		case LoadingPhase::ZONE_CONNECTING:          return "Connecting to zone...";
		case LoadingPhase::ZONE_RECEIVING_PROFILE:   return "Receiving player data...";
		case LoadingPhase::ZONE_RECEIVING_SPAWNS:    return "Receiving zone data...";
		case LoadingPhase::ZONE_REQUEST_PHASE:       return "Synchronizing...";
		case LoadingPhase::ZONE_PLAYER_READY:        return "Finalizing connection...";
		case LoadingPhase::ZONE_AWAITING_CONFIRM:    return "Waiting for confirmation...";
		case LoadingPhase::GRAPHICS_LOADING_ZONE:    return "Loading zone geometry...";
		case LoadingPhase::GRAPHICS_LOADING_MODELS:  return "Loading character models...";
		case LoadingPhase::GRAPHICS_CREATING_ENTITIES: return "Creating entities...";
		case LoadingPhase::GRAPHICS_FINALIZING:      return "Preparing world...";
		case LoadingPhase::COMPLETE:                 return "Ready!";
	}
	return "";
}
