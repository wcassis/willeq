#include "client/eq.h"
#include "client/willeq_database.h"
#include "client/pathfinder_interface.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/hc_map.h"
#include "client/combat.h"
#include "client/corpse_predicate.h"  // T185: player-corpse rename on the death path
#include "client/trade_manager.h"
#include "client/world_object.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include "client/skill/skill_manager.h"
#include "client/zone_lines.h"
#include "client/formatted_message.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"

// P28: inventory/chat/hotbar/command-registry are plain data-model headers
// with no renderer dependency (used unconditionally below: m_inventory_manager,
// m_hotbar_model, m_command_registry are complete-type-required here). Only
// hotkey_manager.h (pulls in <irrlicht.h>) and json_config.h/loading_thread.h
// (renderer-only helpers) stay gated.
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
#include "client/chat/chat_message_buffer.h"
#include "client/hotbar/hotbar_model.h"
#include "client/graphics/ui/command_registry.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/input/hotkey_manager.h"
#include "common/util/json_config.h"
#endif

#include "client/bridge/game_state_bridge.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/loading_thread.h"
#endif
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



// Static member definition
int EverQuest::s_debug_level = 0;

EverQuest::EverQuest(const std::string &host, int port, const std::string &user, const std::string &pass, const std::string &server, const std::string &character)
	// K01 (Batch K, refactor/K_component_extraction.md): m_ctx bundles the
	// fifteen shared-service members every extracted component receives by
	// reference. Reference members must be bound here; nothing is read
	// through them during construction.
	: m_ctx{m_game_state,
	        m_bridge,
	        m_zone_connection,
	        m_entities,
	        m_character,
	        m_combat_manager,
#ifdef WITH_AUDIO
	        m_audio_manager,
#endif
	        m_trade_manager,
	        m_spell_manager,
	        m_inventory_manager,
	        m_skill_manager,
	        m_zone_connection_manager,
	        m_command_registry,
	        m_buff_manager,
	        m_active_tradeskill_object_id}
{
	// K01: the guild family. Constructed first so nothing below can reach a
	// null m_guild.
	m_guild = std::make_unique<eqt::guild::GuildComponent>(m_ctx, *this);
	// K03: the chat family, same shape.
	m_chat = std::make_unique<eqt::chat::ChatComponent>(m_ctx, *this);

	m_host = host;
	m_port = port;
	m_user = user;
	m_pass = pass;
	m_server = server;
	m_character = character;
	m_dbid = 0;
	m_main_thread_id = std::this_thread::get_id();
	
	// Initialize combat manager
	m_combat_manager = std::make_unique<CombatManager>(this);

	// Initialize trade manager
	m_trade_manager = std::make_unique<TradeManager>();
	SetupTradeManagerCallbacks();

	// Initialize spell manager
	m_spell_manager = std::make_unique<EQ::SpellManager>(this);

	// Initialize skill manager
	m_skill_manager = std::make_unique<EQ::SkillManager>(this);

	// D20b2: Skill activation feedback (game-state handler, not renderer)
	m_skill_manager->setOnSkillActivated([this](uint8_t skill_id, bool success, const std::string& message) {
		const char* skill_name = EQ::getSkillName(skill_id);
		if (success) {
			AddChatSystemMessage(fmt::format("You use {}!", skill_name));
			// Calculate haste-adjusted cooldown
			int32_t totalHaste = 0;
			if (m_inventory_manager) {
				auto equipStats = m_inventory_manager->calculateEquipmentStats();
				totalHaste += equipStats.haste;
			}
			if (m_buff_manager) {
				totalHaste += m_buff_manager->getPlayerStatMod(EQ::SpellEffect::AttackSpeed);
			}
			uint32_t adjustedCooldown = EQ::getAdjustedSkillRecastTime(skill_id, totalHaste);
			// Publish SkillActivationFeedback to bridge (for UI cooldown)
			if (m_bridge && adjustedCooldown > 0) {
				eqt::state::SkillActivationFeedbackData data;
				data.skillId = skill_id;
				data.success = true;
				data.cooldownMs = adjustedCooldown;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::SkillActivationFeedback, std::move(data)));
			}
		} else {
			AddChatSystemMessage(fmt::format("Cannot use {}: {}", skill_name, message));
		}
	});

	// D20b2: Skill-up notification (game-state handler, not renderer)
	m_skill_manager->setOnSkillUpdate([this](uint8_t skill_id, uint32_t old_value, uint32_t new_value) {
		if (new_value > old_value) {
			const char* skill_name = EQ::getSkillName(skill_id);
			AddChatSystemMessage(fmt::format("You have become better at {}! ({})", skill_name, new_value));
		}
	});

	// U07b: Create standalone hotbar model
	m_hotbar_model = std::make_unique<eqt::HotbarModel>();
	m_hotbar_model->setNameResolver([this](eqt::HotbarSlotType type, uint32_t id) -> std::string {
		if (type == eqt::HotbarSlotType::Spell && m_spell_manager) {
			const auto* spell = m_spell_manager->getSpell(id);
			return spell ? spell->name : "";
		}
		if (type == eqt::HotbarSlotType::Skill) {
			return EQ::getSkillName(static_cast<uint8_t>(id));
		}
		return "";
	});

	// S04: Create inventory manager at startup (was lazy-init in ConnectToZone/InitGraphics)
	// T41: this construction was still folded under EQT_HAS_GRAPHICS even
	// though P28 already declassified the *declaration* in eq.h ("plain
	// game state and network logic with no renderer dependency") and T29's
	// own comment right below names InventoryManager alongside
	// CommandRegistry as one of the members that guard misclassification
	// affected. Left guarded here, EQT_GRAPHICS=OFF builds never
	// constructed it at all -- not "polled the wrong way", genuinely null
	// for the process lifetime -- which would have made PublishFullStateSnapshot's
	// inventory block (un-guarded below) silently publish nothing. Same
	// declassification as T29's fix just below, applied to the member T29's
	// comment named but didn't itself touch.
	m_inventory_manager = std::make_unique<eqt::inventory::InventoryManager>();
	SetupInventoryCallbacks();
	// T259: TradeManager::setInventoryManager() had NO caller, so
	// TradeManager::getOwnItem() (the "You give X to Y" lines) always saw a
	// null manager and the trade-end clearing of the trade slots could not
	// reach the inventory model at all.
	if (m_trade_manager) {
		m_trade_manager->setInventoryManager(m_inventory_manager.get());
	}
	LOG_DEBUG(MOD_INVENTORY, "Inventory manager initialized");

	// T29: command registry creation was folded under EQT_HAS_GRAPHICS
	// alongside the inventory manager above, which meant a headless build
	// never registered a single slash command -- not even non-graphics ones
	// like /combat, /assist, /target. RegisterCommands() and its per-domain
	// helpers (eq_commands*.cpp) are themselves un-guarded now (see their
	// T29 notes); only the individual commands that genuinely need graphics
	// stay guarded at their own handler bodies. Create the registry
	// unconditionally so headless builds get the same vocabulary.
	m_command_registry = std::make_unique<eqt::ui::CommandRegistry>();
	RegisterCommands();
	LOG_DEBUG(MOD_MAIN, "Command registry initialized");

	// Connection is deferred to ConnectToLogin() so that graphics initialization
	// (which can take several seconds on slow devices like Orange Pi) completes first.
	// This prevents the login server from timing out while waiting for a response.
}

void EverQuest::StartSessionRecording(const std::string& path) {
	m_session_recorder = std::make_unique<EQT::Net::SessionRecorder>();
	if (m_session_recorder->open(path, m_character, CurrentZoneName())) {
		LOG_INFO(MOD_MAIN, "Session recording started: {}", path);
	} else {
		LOG_ERROR(MOD_MAIN, "Failed to start session recording to '{}'", path);
		m_session_recorder.reset();
	}
}

void EverQuest::OpenDatabase(const std::string& dbPath) {
	m_willeq_db = std::make_unique<EQT::WilleqDatabase>();
	if (m_willeq_db->open(dbPath)) {
		EQT::WilleqDatabase::setGlobalInstance(m_willeq_db.get());
		LOG_INFO(MOD_MAIN, "WilleqDatabase opened: {} (cache: {} bytes)",
				 dbPath, m_willeq_db->getMemoryUsage());
	} else {
		LOG_WARN(MOD_MAIN, "WilleqDatabase not found at {} — using legacy file loading", dbPath);
		m_willeq_db.reset();
	}
}

bool EverQuest::InitializeSpellSystem(const std::string& eqClientPath)
{
	// S04: Initialize spell database from SQLite
	if (!m_spell_manager->initialize()) {
		return false;
	}
	LOG_DEBUG(MOD_SPELL, "Spell database loaded from SQLite");

	// S04: Create buff manager (depends on spell DB)
	m_buff_manager = std::make_unique<EQ::BuffManager>(&m_spell_manager->getDatabase());
	LOG_DEBUG(MOD_SPELL, "Buff manager initialized");

	// D20b2: Buff fade callback for vision buff expiration (game-state handler, not renderer)
	m_buff_manager->setBuffFadeCallback([this](uint16_t entity_id, uint32_t spell_id) {
		if (entity_id != 0) return;  // Only handle player buffs
		if (!m_spell_manager || !m_bridge) return;

		const EQ::SpellData* spell = m_spell_manager->getSpell(spell_id);
		if (!spell) return;

		bool hadVisionEffect = spell->hasEffect(EQ::SpellEffect::UltraVision) ||
		                       spell->hasEffect(EQ::SpellEffect::InfraVision);
		if (!hadVisionEffect) return;

		LOG_DEBUG(MOD_SPELL, "Vision buff faded (spell {}), recalculating vision", spell_id);
		uint8_t finalVisionType = 0;
		for (const auto& buff : m_buff_manager->getPlayerBuffs()) {
			const EQ::SpellData* buffSpell = m_spell_manager->getSpell(buff.spell_id);
			if (!buffSpell) continue;
			if (buffSpell->hasEffect(EQ::SpellEffect::UltraVision)) { finalVisionType = 2; break; }
			else if (buffSpell->hasEffect(EQ::SpellEffect::InfraVision)) { finalVisionType = 1; }
		}
		eqt::state::VisionChangedData vdata;
		vdata.visionType = finalVisionType;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::VisionChanged, std::move(vdata)));
	});

	// ========================================================================
	// TOMBSTONE: the client-side spell RESOLUTION stack used to be built here.
	// Both layers of it are gone. DO NOT REBUILD EITHER ONE.
	// ========================================================================
	//
	// T324 (2026-09-02, commit ef2c3080) deleted the TARGET-RESOLUTION layer:
	// EQ::SpellTypeProcessor (include/client/spell/spell_type_processor.h,
	// src/client/spell/spell_type_processor.cpp) and its rain-spell instance
	// struct, together with the accessor on this class. It had ZERO callers:
	// the accessor was never used, its per-frame tick was never run, and its
	// entry point was never reached. It resolved single/self/PBAE/targeted-AE/
	// group/rain target sets locally.
	//
	// T327 (2026-09-02, commit 25bc9eaa) deleted the EFFECT-APPLICATION
	// layer below it:
	// EQ::SpellEffects (include/client/spell/spell_effects.h,
	// src/client/spell/spell_effects.cpp, ~1,120 lines), the EffectResult
	// struct, the accessor on this class and the member that used to be
	// constructed on this line. Its only external consumer was the class T324
	// removed, so it had zero callers as of that commit and no entry point
	// from the server-packet path at all: it published no bridge event, pushed
	// no intent, implemented no action-handler virtual, and reached this class
	// at exactly two lines -- one entity-id read and one system chat message.
	// All 49 of its per-effect handlers were logging stubs that applied
	// nothing: the heal handler did not heal, the teleport handler did not
	// teleport. The BuffManager it was handed was never dereferenced once.
	//
	// WHY, for both: the server is authoritative for spell resolution. eqemu
	// picks the targets and reports each landing individually (action, damage,
	// buff and appearance packets), and this client displays what it is told.
	// That is the same conclusion T318 reached for shrink/grow, where the
	// server already broadcasts every size change.
	//
	// WARNING if client-side effect calculation is ever wanted: the deleted
	// value-scaling table was WRONG against the server, and nothing ever
	// caught it because nothing ever called it. Every linear formula was
	// shifted by one slot against eqemu, formulas 107/108/122 are tick-based
	// decay rather than level scaling, the sign handling for spells whose max
	// lies below their base (snares, damage) was absent, and two thirds of the
	// table was missing. Transcribe it from eqemu's
	// Mob::CalcSpellEffectValue_formula (zone/spell_effects.cpp). Do NOT
	// recover it from this file's git history. Board item 45 in
	// refactor/deferred.md carries the full comparison.
	//
	// What did NOT go with either deletion, and who keeps it alive:
	//   - EQ::SpellManager and EQ::BuffManager are both LIVE (gem click, /cast,
	//     the buff window and its click-off path).
	//   - EQ::SpellDatabase, EQ::SpellEffectSlot, EQ::ResistType and the
	//     EQ::SpellEffect enum are all reached through those two and through
	//     the particle system.
	//   - the body-type targeting rule lives in
	//     include/client/spell/spell_body_type_target.h and is used by
	//     EQ::SpellManager.
	//
	// See board items 33 and 44 in refactor/deferred.md,
	// refactor/T324_delete_spell_type_processor.md,
	// refactor/T327_delete_spell_effects.md, and the Review of
	// refactor/T314_spell_targeting_correctness.md, which transcribes eqemu's
	// group-targeting rule in full should it ever be needed again.

	return true;
}

void EverQuest::ConnectToLogin()
{
	auto transport = std::make_unique<EQ::Net::PosixUdpTransport>();
	m_login_connection_manager.reset(new EQ::Net::DaybreakConnectionManager(std::move(transport)));

	m_login_connection_manager->OnNewConnection(std::bind(&EverQuest::LoginOnNewConnection, this, std::placeholders::_1));
	m_login_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::LoginOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_login_connection_manager->OnPacketRecv(std::bind(&EverQuest::LoginOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));

	SetLoadingPhase(LoadingPhase::LOGIN_CONNECTING);
	m_login_connection_manager->Connect(m_host, m_port);
}

void EverQuest::TickNetwork()
{
	if (m_login_connection_manager) {
		m_login_connection_manager->Tick();
	}
	if (m_world_connection_manager) {
		m_world_connection_manager->Tick();
	}
	if (m_zone_connection_manager) {
		m_zone_connection_manager->Tick();
	}
}

EverQuest::~EverQuest()
{
	LOG_DEBUG(MOD_MAIN, "EverQuest destructor called, outputting performance report...");

	// Output final performance metrics
	std::string perfReport = EQT::PerformanceMetrics::instance().generateReport();
	if (!perfReport.empty()) {
		LOG_INFO(MOD_MAIN, "{}", perfReport);
	} else {
		LOG_WARN(MOD_MAIN, "Performance report is empty");
	}
	std::cout.flush();  // Ensure performance report is flushed before cleanup

	// Shutdown audio before other cleanup
#ifdef WITH_AUDIO
	ShutdownAudio();
#endif

	// Stop the update loop before destroying
	StopUpdateLoop();
	
	// Clean up connections properly to avoid pure virtual method calls
	if (m_zone_connection) {
		m_zone_connection.reset();
	}
	if (m_zone_connection_manager) {
		m_zone_connection_manager.reset();
	}
	if (m_login_connection) {
		m_login_connection.reset();
	}
	if (m_login_connection_manager) {
		m_login_connection_manager.reset();
	}
	
	// Clear movement history
	m_movement_history.clear();
}

// Loading phase tracking implementation
void EverQuest::SetLoadingPhase(LoadingPhase phase, const char* statusText) {
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

	// Write to LoadingStatus for the loading thread's passive display loop
#ifdef EQT_HAS_GRAPHICS
	if (m_loading_status_ptr) {
		m_loading_status_ptr->setProgress(static_cast<int>(progress * 100.0f), m_loading_status_text);
	}
#endif

	// Publish ZoneLoading event to bridge (also drives loading screen progress via bridge handler)
	if (m_bridge) {
		eqt::state::ZoneLoadingData data;
		data.zoneName = CurrentZoneName();
		data.zoneId = CurrentZoneId();
		data.progress = progress;
		data.statusMessage = m_loading_status_text ? m_loading_status_text : "";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ZoneLoading, std::move(data)));
	}
}

float EverQuest::GetLoadingProgress() const {
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

const char* EverQuest::GetLoadingStatusText() const {
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

void EverQuest::OnGameStateComplete() {
	LOG_INFO(MOD_MAIN, "[LOADING] Game state setup complete. Spawn ID: {}, Zone: {}",
	         MySpawnId(), CurrentZoneName());

#ifdef EQT_HAS_GRAPHICS
	if (m_graphics_initialized) {
		SetLoadingPhase(LoadingPhase::GRAPHICS_LOADING_ZONE, "Entering world...");

		// Signal Application to create snapshot and start zone loading.
		// Application's game thread loop picks this up, creates the snapshot
		// (with full zone data), then starts the loading thread.
		// PublishFullStateSnapshot() is NOT called here -- graphics mode
		// publishes it later from OnGraphicsComplete(), once, after zone
		// graphics finish loading (T41: do not add a second call site here,
		// that would double-publish for every graphics-mode zone-in).
		m_zone_load_requested = true;
		LOG_INFO(MOD_GRAPHICS, "OnGameStateComplete: zone load requested");
	} else {
		// No graphics mode - we're done
		SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!");
		if (m_bridge) {
			eqt::state::ZoneLoadedData data;
			data.zoneName = CurrentZoneName();
			data.zoneId = CurrentZoneId();
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::ZoneLoaded, std::move(data)));
		}
		// T41: OnGameStateComplete() is the correct GAME-lifecycle
		// zone-in/state-ready signal (eq_zone.cpp's re-zone comment calls it
		// "the single signal point ... which only fires once the new zone's
		// NewZone packet has actually been received"). Graphics mode reaches
		// PublishFullStateSnapshot() via OnGraphicsComplete() instead (a
		// GRAPHICS lifecycle hook that only exists because zone graphics
		// take time to build); this is the equivalent trigger for the run
		// that has no graphics phase to wait on, so the snapshot fires
		// exactly once per zone-in here too instead of never firing at all.
		PublishFullStateSnapshot();
	}
#else
	// No graphics mode - we're done
	SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!");
	if (m_bridge) {
		eqt::state::ZoneLoadedData data;
		data.zoneName = CurrentZoneName();
		data.zoneId = CurrentZoneId();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ZoneLoaded, std::move(data)));
	}
	// T41: see the twin branch above (EQT_HAS_GRAPHICS defined, graphics not
	// initialized) -- same reasoning, this is the EQT_GRAPHICS=OFF configure's
	// copy of the same headless path.
	PublishFullStateSnapshot();
#endif
}

void EverQuest::OnGraphicsComplete() {
	SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!");
	LOG_INFO(MOD_MAIN, "[LOADING] Graphics loading complete. Game ready!");

	// ZoneLoaded bridge event drives setZoneReady(true) + hideLoadingScreen() via bridge handler
	if (m_bridge) {
		eqt::state::ZoneLoadedData data;
		data.zoneName = CurrentZoneName();
		data.zoneId = CurrentZoneId();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ZoneLoaded, std::move(data)));
	}

	// U00: Publish full state snapshot so new UI panels have data on first frame
	PublishFullStateSnapshot();
}

void EverQuest::PublishFullStateSnapshot() {
	if (!m_bridge) return;

	LOG_INFO(MOD_MAIN, "Publishing full state snapshot to bridge");

	// --- Player stats ---
	{
		eqt::state::PlayerStatsChangedData data;
		data.curHP = m_game_state.player().curHP();
		data.maxHP = m_game_state.player().maxHP();
		data.curMana = m_game_state.player().curMana();
		data.maxMana = m_game_state.player().maxMana();
		data.curEndurance = m_game_state.player().curEndurance();
		data.maxEndurance = m_game_state.player().maxEndurance();
		data.level = m_game_state.player().level();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerStatsChanged, std::move(data)));
	}

	// --- Character info ---
	{
		eqt::state::CharacterInfoChangedData data;
		data.name = m_character;
		data.level = m_game_state.player().level();
		data.className = GetClassName(m_game_state.player().classId());
		data.deity = GetDeityName(m_game_state.player().deity());
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CharacterInfoChanged, std::move(data)));
	}

	// --- XP progress ---
	{
		// EQ XP is 0-330 for current level (330 = level up)
		float progress = (m_exp > 0) ? static_cast<float>(m_exp % 330) / 330.0f : 0.0f;
		eqt::state::ExpProgressChangedData data;
		data.progress = progress;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ExpProgressChanged, std::move(data)));
	}

	// --- Currency ---
	{
		eqt::state::CurrencyChangedData data;
		data.platinum = m_game_state.player().platinum();
		data.gold = m_game_state.player().gold();
		data.silver = m_game_state.player().silver();
		data.copper = m_game_state.player().copper();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(data)));
	}

	// --- Spell gems ---
	if (m_spell_manager) {
		for (uint8_t i = 0; i < EQ::MAX_SPELL_GEMS; ++i) {
			uint32_t spellId = m_spell_manager->getMemorizedSpell(i);
			eqt::state::SpellGemChangedData data;
			data.gemSlot = i;
			data.spellId = spellId;
			data.gemState = static_cast<uint8_t>(m_spell_manager->getGemState(i));
			data.cooldownRemainingMs = m_spell_manager->getGemCooldownRemaining(i);
			data.cooldownTotalMs = 0;
			data.memorizeTotalMs = 0;
			if (spellId != EQ::SPELL_UNKNOWN) {
				const auto* spell = m_spell_manager->getSpell(spellId);
				data.spellName = spell ? spell->name : "";
				data.iconId = spell ? spell->gem_icon : 0;
			}
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::SpellGemChanged, std::move(data)));
		}
	}

	// --- Spellbook snapshot ---
	if (m_spell_manager) {
		eqt::state::SpellbookSnapshotData sbdata;
		for (uint16_t slot = 0; slot < EQ::MAX_SPELLBOOK_SLOTS; ++slot) {
			uint32_t spellId = m_spell_manager->getSpellbookSpell(slot);
			if (spellId == 0 || spellId == EQ::SPELL_UNKNOWN) continue;
			const auto* spell = m_spell_manager->getSpell(spellId);
			eqt::state::SpellbookEntry entry;
			entry.slot = slot;
			entry.spellId = spellId;
			entry.name = spell ? spell->name : "";
			entry.iconId = spell ? spell->spell_icon : 0;
			entry.level = (spell && m_game_state.player().classId() > 0) ? spell->class_levels[m_game_state.player().classId() - 1] : 0;
			sbdata.spells.push_back(std::move(entry));
		}
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SpellbookSnapshot, std::move(sbdata)));
	}

	// --- Skills snapshot ---
	if (m_skill_manager) {
		eqt::state::SkillsSnapshotData skdata;
		auto allSkills = m_skill_manager->getAllSkills();
		for (const auto* skill : allSkills) {
			if (!skill || skill->current_value == 0) continue;
			eqt::state::SkillDisplayEntry entry;
			entry.skillId = skill->skill_id;
			entry.name = skill->name;
			entry.value = skill->current_value;
			entry.maxValue = skill->max_value;
			skdata.skills.push_back(std::move(entry));
		}
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SkillsSnapshot, std::move(skdata)));
	}

	// --- Buffs ---
	if (m_buff_manager) {
		const auto& buffs = m_buff_manager->getPlayerBuffs();
		for (size_t i = 0; i < buffs.size(); ++i) {
			const auto& buff = buffs[i];
			if (buff.spell_id == 0 || buff.spell_id == EQ::SPELL_UNKNOWN) continue;
			eqt::state::BuffUpdatedData data;
			data.slot = (buff.slot >= 0) ? static_cast<uint8_t>(buff.slot) : static_cast<uint8_t>(i);
			data.spellId = buff.spell_id;
			data.ticksLeft = buff.remaining_seconds / 6;  // Convert seconds to ticks (6s per tick)
			data.casterName = "";  // Caster name not stored in ActiveBuff
			if (m_spell_manager) {
				const auto* spell = m_spell_manager->getSpell(buff.spell_id);
				data.spellName = spell ? spell->name : "";
				data.iconId = spell ? spell->spell_icon : 0;
			}
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::BuffUpdated, std::move(data)));
		}
	}

	// --- Inventory ---
	// T41: was #ifdef EQT_HAS_GRAPHICS. m_inventory_manager (per P28's
	// comment at the top of this file) is a renderer-free data model used
	// unconditionally elsewhere in this TU; nothing in this block touches
	// the renderer. Un-guarding it is what makes the headless configure's
	// snapshot (see OnGameStateComplete() above) actually publish inventory
	// data instead of silently compiling out.
	if (m_inventory_manager) {
		// Equipment slots (0-21) + general inventory (22-29)
		for (int16_t slot = 0; slot < 30; ++slot) {
			const auto* item = m_inventory_manager->getItem(slot);
			eqt::state::InventorySlotChangedData data;
			data.slotId = slot;
			data.hasItem = (item != nullptr);
			SyncInventorySlotToGameState(slot, data.hasItem);  // T221: zone-in parity
			data.itemId = item ? item->itemId : 0;
			data.itemName = item ? item->name : "";
			data.iconId = item ? item->icon : 0;
			data.quantity = item ? item->quantity : 0;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
		}
	}

	// --- Group ---
	if (m_game_state.group().inGroup()) {
		const auto& group = m_game_state.group();
		eqt::state::GroupChangedData data;
		data.inGroup = true;
		data.isLeader = group.isLeader();
		data.leaderName = group.leaderName();
		data.memberCount = 0;
		for (int i = 0; i < 5; ++i) {
			const GroupMember* gm = group.getMember(i);
			if (gm && !gm->name.empty()) data.memberCount++;
		}
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::GroupChanged, std::move(data)));

		for (int i = 0; i < 5; ++i) {
			const GroupMember* gm = group.getMember(i);
			if (!gm || gm->name.empty()) continue;
			eqt::state::GroupMemberUpdatedData mdata;
			mdata.memberIndex = i;
			mdata.name = gm->name;
			mdata.level = gm->level;
			mdata.classId = gm->classId;
			mdata.hpPercent = gm->hpPercent;
			mdata.inZone = gm->inZone;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::GroupMemberUpdated, std::move(mdata)));
		}
	}

	// --- Pet ---
	if (m_game_state.pet().petSpawnId() != 0) {
		eqt::state::PetCreatedData data;
		data.spawnId = m_game_state.pet().petSpawnId();
		data.name = GetPetName();
		data.level = GetPetLevel();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PetCreated, std::move(data)));
	}

	LOG_INFO(MOD_MAIN, "State snapshot published");
}


void EverQuest::ConnectToWorld(const std::string& world_address)
{
	LOG_DEBUG(MOD_WORLD, "Creating new world connection manager for {}:9000", world_address);

	SetLoadingPhase(LoadingPhase::WORLD_CONNECTING);

	// Store the world server address for reconnection
	m_world_server_host = world_address;

	auto transport = std::make_unique<EQ::Net::PosixUdpTransport>();
	m_world_connection_manager.reset(new EQ::Net::DaybreakConnectionManager(std::move(transport)));
	m_world_connection_manager->OnNewConnection(std::bind(&EverQuest::WorldOnNewConnection, this, std::placeholders::_1));
	m_world_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::WorldOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_world_connection_manager->OnPacketRecv(std::bind(&EverQuest::WorldOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));
	m_world_connection_manager->Connect(world_address, 9000);
}


// Zone server connection functions
void EverQuest::ConnectToZone()
{
	LOG_DEBUG(MOD_ZONE, "Connecting to zone server at {}:{}", m_zone_server_host, m_zone_server_port);

	SetLoadingPhase(LoadingPhase::ZONE_CONNECTING);

	// S04: inventory_manager, spell DB and buff_manager are all
	// pre-allocated in EverQuest constructor / Application::initialize()

	EQ::Net::DaybreakConnectionManagerOptions zone_opts;
	auto transport = std::make_unique<EQ::Net::PosixUdpTransport>();
	m_zone_connection_manager.reset(new EQ::Net::DaybreakConnectionManager(zone_opts, std::move(transport)));
	m_zone_connection_manager->OnNewConnection(std::bind(&EverQuest::ZoneOnNewConnection, this, std::placeholders::_1));
	m_zone_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::ZoneOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_zone_connection_manager->OnPacketRecv(std::bind(&EverQuest::ZoneOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));
	m_zone_connection_manager->Connect(m_zone_server_host, m_zone_server_port);
}

void EverQuest::ZoneOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection)
{
	m_zone_connection = connection;
	LOG_DEBUG(MOD_ZONE, "Zone connection created");
}

void EverQuest::ZoneOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusConnected) {
		LOG_DEBUG(MOD_ZONE, "Zone connected");
		// T286: this write had NO WorldState counterpart, so world()
		// .isZoneConnected() never became true on the reconnect path.
		m_game_state.world().setZoneConnected(true);
		// T322: the destination zone session is up, so the zone-to-zone
		// transition is over -- the next world login is a fresh one unless
		// ProcessDeferredZoneChange latches it again.
		m_zone_to_zone_in_progress = false;
		// Send stream identify immediately after connection
		ZoneSendStreamIdentify();
		m_zone_session_established = true;

		// Send ack packet and zone entry immediately after session established
		ZoneSendAckPacket();
		ZoneSendZoneEntry();
	}

	if (to == EQ::Net::StatusDisconnected) {
		LOG_INFO(MOD_ZONE, "Zone connection lost, reconnecting.");
		m_game_state.world().setZoneConnected(false);  // T286: only store
		m_zone_session_established = false;
		m_zone_entry_sent = false;
		// m_client_spawned = false; // Deprecated
		m_zone_connection.reset();
		SetLoadingPhase(LoadingPhase::ZONE_CONNECTING);
		m_zone_connection_manager->Connect(m_zone_server_host, m_zone_server_port);
	}
}

void EverQuest::ZoneOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusDisconnected) {
		m_zone_connection.reset();
	}
}

void EverQuest::ZoneOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet & p)
{
	auto opcode = p.GetUInt16(0);

	LOG_TRACE(MOD_ZONE, "[ZONE RECV] opcode=0x{:04x} len={}", opcode, p.Length());

	DumpPacket("S->C", opcode, p);

	// R01: record real network traffic only (replayed packets enter via
	// ReplayZonePacket -> DispatchZonePacket and are never re-recorded)
	if (m_session_recorder && p.Length() >= 2) {
		m_session_recorder->record(EQT::Net::SessionStream::Zone, EQT::Net::SessionDirection::ServerToClient,
			opcode, (const uint8_t*)p.Data() + 2, static_cast<uint32_t>(p.Length() - 2));
	}

	DispatchZonePacket(p);
}

// Batch R replay/test seam: build a synthetic app packet (2-byte opcode
// prefix + payload, matching session_log.h's on-disk layout) and run it
// through the same opcode switch ZoneOnPacketRecv uses.
void EverQuest::ReplayZonePacket(uint16_t opcode, const uint8_t* data, size_t size)
{
	EQ::Net::DynamicPacket p;
	p.Resize(2 + size);
	p.PutUInt16(0, opcode);
	if (size > 0 && data != nullptr) {
		memcpy(static_cast<char*>(p.Data()) + 2, data, size);
	}
	DispatchZonePacket(p);
}

bool EverQuest::SafeQueueZonePacket(EQ::Net::Packet &p, int stream, bool reliable)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_ZONE, "Attempted to send packet with null zone connection");
		return false;
	}

	if (m_session_recorder && p.Length() >= 2) {
		m_session_recorder->record(EQT::Net::SessionStream::Zone, EQT::Net::SessionDirection::ClientToServer,
			p.GetUInt16(0), (const uint8_t*)p.Data() + 2, static_cast<uint32_t>(p.Length() - 2));
	}

	// Update thread must use async to avoid libuv thread-safety violation
	if (std::this_thread::get_id() != m_main_thread_id) {
		if (m_zone_connection_manager) {
			m_zone_connection_manager->QueuePacketAsync(m_zone_connection, p, stream, reliable);
			return true;
		}
		return false;
	}

	// Add debugging for large reliable packets
	if (reliable && p.Length() > 400 && s_debug_level >= 2) {
		uint16_t opcode = p.GetUInt16(0);
		LOG_TRACE(MOD_ZONE, "SafeQueueZonePacket: Large reliable packet - opcode={:#06x} ({}), length={}, stream={}",
			opcode, GetOpcodeName(opcode), p.Length(), stream);

		// Check connection state before sending large packets
		try {
			auto stats = m_zone_connection->GetStats();
			LOG_DEBUG(MOD_ZONE, "Connection stats before large packet: sent={}, recv={}, resent={}", stats.sent_packets, stats.recv_packets, stats.resent_packets);
		} catch (...) {
			LOG_WARN(MOD_ZONE, "Failed to get connection stats");
		}
	}

	// Check if connection is still valid
	try {
		// Get connection stats to verify the connection is still valid
		auto stats = m_zone_connection->GetStats();

		// Additional check - if stats look corrupted, don't send
		if (stats.recv_packets == 0 && stats.sent_packets == 0 && m_game_state.world().isZoneConnected()) {
			LOG_WARN(MOD_ZONE, "Zone connection appears invalid (zero stats)");
			return false;
		}

		m_zone_connection->QueuePacket(p, stream, reliable);

		// Log successful queueing of large reliable packets
		if (reliable && p.Length() > 400 && s_debug_level >= 2) {
			LOG_DEBUG(MOD_ZONE, "Large reliable packet queued successfully");
		}

		return true;
	}
	catch (const std::exception& e) {
		LOG_ERROR(MOD_ZONE, "Exception in SafeQueueZonePacket: {} (opcode={:#06x}, len={}, reliable={})",
			e.what(), p.GetUInt16(0), p.Length(), reliable);
		m_zone_connection.reset();  // Clear the corrupted connection
		m_game_state.world().setZoneConnected(false);  // T286: only store
		return false;
	}
	catch (...) {
		LOG_ERROR(MOD_ZONE, "Unknown exception in SafeQueueZonePacket (opcode={:#06x}, len={}, reliable={})",
			p.GetUInt16(0), p.Length(), reliable);
		m_zone_connection.reset();  // Clear the corrupted connection
		m_game_state.world().setZoneConnected(false);  // T286: only store
		return false;
	}
}


void EverQuest::StartUpdateLoop()
{
	// T245: no thread any more. This used to spawn a 500 ms std::thread whose
	// only remaining job (after P36 moved UpdateMovement() off it) was
	// CombatManager::Update() -- an off-game-thread writer of m_game_state
	// (combat().setTarget/clearTarget via CheckForAggro / ProcessCombatRound /
	// FindNextHuntTarget whenever /combat on; T227 STOP note, TSAN-proven in
	// review/T245/). Update() is now ticked from UpdateMovement() on the game
	// thread and paces itself at the same 500 ms (CombatManager::kUpdateIntervalMs).
	// The flag stays: IsFullyZonedIn() and OnGameStateComplete()'s once-only
	// gate read it, and the P36 self-join hazard (CleanupZone -> StopUpdateLoop
	// -> join() from the thread being joined) is gone with the thread.
	if (m_update_running) {
		return;  // Already running
	}
	m_update_running = true;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Update loop started (game-thread tick)");
	}
}

void EverQuest::StopUpdateLoop()
{
	if (!m_update_running) {
		return;
	}
	m_update_running = false;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Update loop stopped");
	}
}






// ============================================================================
// Phase 3: Corpse Management
// ============================================================================


// ============================================================================
// Phase 3: Combat Targeting
// ============================================================================


// ============================================================================
// Phase 3: Misc
// ============================================================================



// ============================================================================
// Phase 4: Cosmetic
// ============================================================================




// ============================================================================
// Phase 4: Petitions
// ============================================================================

void EverQuest::SendPetition(const std::string& text)
{
	// T135: PutCString() not PutString() -- eqemu's Handle_OP_Petition reads
	// this as a NUL-terminated C string; PutString() wrote none, so the
	// server's read ran into adjacent heap memory (confirmed live via a
	// corrupted peq.petitions row). Pre-existing bug, not a T135 regression
	// -- see refactor/deferred.md's T135 entry.
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Petition);
	packet.PutCString(2, text.c_str());

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Petition submitted");
		AddChatSystemMessage("Your petition has been submitted.");
	}
}

void EverQuest::SetSneak(bool sneak)
{
	if (m_game_state.player().isSneaking() == sneak) {
		return;
	}

	m_game_state.player().setSneaking(sneak);  // Phase 7.8 / T286: only store
	SendSpawnAppearance(AT_SNEAK, sneak ? 1 : 0);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sneak status: {}", sneak ? "ON" : "OFF");
	}
}


void EverQuest::QueuePacket(uint16_t opcode, EQ::Net::DynamicPacket* packet)
{
	if (!packet) {
		return;
	}
	Test_RecordQueuedPacket(opcode, packet->Data(), packet->Length());  // T268 seam (no-op unless armed)
	if (!m_zone_connection) {
		return;
	}

	if (m_session_recorder) {
		m_session_recorder->record(EQT::Net::SessionStream::Zone, EQT::Net::SessionDirection::ClientToServer,
			opcode, packet->Data(), static_cast<uint32_t>(packet->Length()));
	}

	EQ::Net::DynamicPacket p;
	p.Resize(packet->Length() + 2); // Add space for opcode
	p.PutUInt16(0, opcode);
	if (packet->Length() > 0) {
		memcpy(static_cast<uint8_t*>(p.Data()) + 2, packet->Data(), packet->Length());
	}

	// Update thread must use async to avoid libuv thread-safety violation
	if (std::this_thread::get_id() != m_main_thread_id) {
		if (m_zone_connection_manager) {
			m_zone_connection_manager->QueuePacketAsync(m_zone_connection, p);
			return;
		}
		return;
	}

	m_zone_connection->QueuePacket(p);
}


// P28: SetEQClientPath is plain game config (EQ install path, string-file
// loading) with no renderer dependency — un-guarded to match its
// unconditional eq.h declaration. SetConstrainedConfig genuinely needs the
// renderer-only ConstrainedRendererConfig/ConstrainedRenderingPreset types
// and stays gated.
void EverQuest::SetEQClientPath(const std::string& path) {
	m_eq_client_path = path;

	// Load EQ string files for message lookups
	LoadStringFiles(path);
}

#ifdef EQT_HAS_GRAPHICS
void EverQuest::SetConstrainedConfig(const EQT::Graphics::ConstrainedRendererConfig& config) {
	m_constrained_config = config;
	m_constrained_preset = EQT::Graphics::ConstrainedRenderingPreset::Custom;
}
#endif // EQT_HAS_GRAPHICS
