#include "client/eq.h"
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

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_config_loader.h"
#include "client/graphics/weather_quality_preset.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/ui/command_registry.h"
#include "client/hotbar/hotbar_model.h"
#include "client/input/hotkey_manager.h"
#include "common/util/json_config.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/seasonal_controller.h"
#include "client/graphics/environment/spell_effects_config.h"
#endif

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

// Movement constants
const float DEFAULT_RUN_SPEED = 6.0f;     // Real EQ run speed based on good client testing
const float DEFAULT_WALK_SPEED = 3.0f;    // Normal EQ walk speed (half of run)
const float WALK_SPEED_THRESHOLD = 4.5f;  // Speed threshold for walk vs run animation
const float POSITION_UPDATE_INTERVAL_MS = 200.0f;  // Real clients update every 200ms

// Following constants
const float FOLLOW_CLOSE_DISTANCE = 10.0f;   // Stop following when this close
const float FOLLOW_FAR_DISTANCE = 30.0f;     // Full speed when this far
const float FOLLOW_MIN_SPEED_MULT = 0.5f;    // Minimum speed multiplier when close
const float FOLLOW_MAX_SPEED_MULT = 1.5f;    // Maximum speed multiplier when far

// Experience required for each level (Titanium-era values)
// These are cumulative totals needed to reach each level (levels 1-65)
static const uint32_t EXP_FOR_LEVEL[] = {
    0,          // Level 1 (no exp required)
    1000,       // Level 2
    2000,       // Level 3
    3250,       // Level 4
    5250,       // Level 5
    8750,       // Level 6
    14000,      // Level 7
    22000,      // Level 8
    34000,      // Level 9
    52000,      // Level 10
    77000,      // Level 11
    110000,     // Level 12
    152500,     // Level 13
    207500,     // Level 14
    277500,     // Level 15
    365000,     // Level 16
    472500,     // Level 17
    602500,     // Level 18
    757500,     // Level 19
    940000,     // Level 20
    1152500,    // Level 21
    1397500,    // Level 22
    1677500,    // Level 23
    1995000,    // Level 24
    2352500,    // Level 25
    2752500,    // Level 26
    3197500,    // Level 27
    3690000,    // Level 28
    4232500,    // Level 29
    4827500,    // Level 30
    5477500,    // Level 31
    6185000,    // Level 32
    6952500,    // Level 33
    7782500,    // Level 34
    8677500,    // Level 35
    9640000,    // Level 36
    10672500,   // Level 37
    11777500,   // Level 38
    12957500,   // Level 39
    14215000,   // Level 40
    15552500,   // Level 41
    16972500,   // Level 42
    18477500,   // Level 43
    20070000,   // Level 44
    21752500,   // Level 45
    23527500,   // Level 46
    25397500,   // Level 47
    27365000,   // Level 48
    29432500,   // Level 49
    31602500,   // Level 50
    33877500,   // Level 51
    39677500,   // Level 52
    46477500,   // Level 53
    54377500,   // Level 54
    63477500,   // Level 55
    73877500,   // Level 56
    85677500,   // Level 57
    98977500,   // Level 58
    113877500,  // Level 59
    130477500,  // Level 60
    148877500,  // Level 61
    169177500,  // Level 62
    191477500,  // Level 63
    215877500,  // Level 64
    242477500,  // Level 65
};
static const int MAX_EXP_LEVEL = sizeof(EXP_FOR_LEVEL) / sizeof(EXP_FOR_LEVEL[0]) - 1;

// Calculate experience progress as 0.0 to 1.0 for the current level
static float CalculateExpProgress(uint32_t exp, uint8_t level) {
    if (level < 1 || level >= MAX_EXP_LEVEL) {
        return 0.0f;  // Invalid level or max level
    }

    uint32_t expForCurrentLevel = EXP_FOR_LEVEL[level];
    uint32_t expForNextLevel = EXP_FOR_LEVEL[level + 1];

    if (exp < expForCurrentLevel) {
        return 0.0f;  // Below current level (shouldn't happen)
    }
    if (exp >= expForNextLevel) {
        return 1.0f;  // At or above next level
    }

    uint32_t expNeeded = expForNextLevel - expForCurrentLevel;
    uint32_t expGained = exp - expForCurrentLevel;

    return static_cast<float>(expGained) / static_cast<float>(expNeeded);
}

// Static member definition
int EverQuest::s_debug_level = 0;

EverQuest::EverQuest(const std::string &host, int port, const std::string &user, const std::string &pass, const std::string &server, const std::string &character)
{
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
#ifdef EQT_HAS_GRAPHICS
	m_inventory_manager = std::make_unique<eqt::inventory::InventoryManager>();
	SetupInventoryCallbacks();
	LOG_DEBUG(MOD_INVENTORY, "Inventory manager initialized");

	// S04: Create command registry at startup (was lazy-init in ProcessChatInput)
	m_command_registry = std::make_unique<eqt::ui::CommandRegistry>();
	RegisterCommands();
	LOG_DEBUG(MOD_MAIN, "Command registry initialized");
#endif

	// Connection is deferred to ConnectToLogin() so that graphics initialization
	// (which can take several seconds on slow devices like Orange Pi) completes first.
	// This prevents the login server from timing out while waiting for a response.
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

	// S04: Create spell effects processor (depends on spell DB + buff manager)
	m_spell_effects = std::make_unique<EQ::SpellEffects>(
		this, &m_spell_manager->getDatabase(), m_buff_manager.get());
	LOG_DEBUG(MOD_SPELL, "Spell effects processor initialized");

	// S04: Create spell type processor (depends on spell DB + spell effects)
	m_spell_type_processor = std::make_unique<EQ::SpellTypeProcessor>(
		this, &m_spell_manager->getDatabase(), m_spell_effects.get());
	LOG_DEBUG(MOD_SPELL, "Spell type processor initialized");

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
	if (m_loading_status_ptr) {
		m_loading_status_ptr->setProgress(static_cast<int>(progress * 100.0f), m_loading_status_text);
	}

	// Publish ZoneLoading event to bridge (also drives loading screen progress via bridge handler)
	if (m_bridge) {
		eqt::state::ZoneLoadingData data;
		data.zoneName = m_current_zone_name;
		data.zoneId = m_current_zone_id;
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
	         m_my_spawn_id, m_current_zone_name);

#ifdef EQT_HAS_GRAPHICS
	if (m_graphics_initialized) {
		SetLoadingPhase(LoadingPhase::GRAPHICS_LOADING_ZONE, "Entering world...");

		// Signal Application to create snapshot and start zone loading.
		// Application's game thread loop picks this up, creates the snapshot
		// (with full zone data), then starts the loading thread.
		m_zone_load_requested = true;
		LOG_INFO(MOD_GRAPHICS, "OnGameStateComplete: zone load requested");
	} else {
		// No graphics mode - we're done
		SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!");
		if (m_bridge) {
			eqt::state::ZoneLoadedData data;
			data.zoneName = m_current_zone_name;
			data.zoneId = m_current_zone_id;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::ZoneLoaded, std::move(data)));
		}
	}
#else
	// No graphics mode - we're done
	SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!");
	if (m_bridge) {
		eqt::state::ZoneLoadedData data;
		data.zoneName = m_current_zone_name;
		data.zoneId = m_current_zone_id;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ZoneLoaded, std::move(data)));
	}
#endif
}

void EverQuest::OnGraphicsComplete() {
	SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!");
	LOG_INFO(MOD_MAIN, "[LOADING] Graphics loading complete. Game ready!");

	// ZoneLoaded bridge event drives setZoneReady(true) + hideLoadingScreen() via bridge handler
	if (m_bridge) {
		eqt::state::ZoneLoadedData data;
		data.zoneName = m_current_zone_name;
		data.zoneId = m_current_zone_id;
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
		data.curHP = m_cur_hp;
		data.maxHP = m_max_hp;
		data.curMana = m_mana;
		data.maxMana = m_max_mana;
		data.curEndurance = m_endurance;
		data.maxEndurance = m_max_endurance;
		data.level = m_level;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerStatsChanged, std::move(data)));
	}

	// --- Character info ---
	{
		eqt::state::CharacterInfoChangedData data;
		data.name = m_character;
		data.level = m_level;
		data.className = GetClassName(m_class);
		data.deity = GetDeityName(m_deity);
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
		data.platinum = m_platinum;
		data.gold = m_gold;
		data.silver = m_silver;
		data.copper = m_copper;
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
			entry.level = (spell && m_class > 0) ? spell->class_levels[m_class - 1] : 0;
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
#ifdef EQT_HAS_GRAPHICS
	if (m_inventory_manager) {
		// Equipment slots (0-21) + general inventory (22-29)
		for (int16_t slot = 0; slot < 30; ++slot) {
			const auto* item = m_inventory_manager->getItem(slot);
			eqt::state::InventorySlotChangedData data;
			data.slotId = slot;
			data.hasItem = (item != nullptr);
			data.itemId = item ? item->itemId : 0;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
		}
	}
#endif

	// --- Group ---
	if (m_in_group) {
		eqt::state::GroupChangedData data;
		data.inGroup = true;
		data.isLeader = m_is_group_leader;
		data.leaderName = m_group_leader_name;
		data.memberCount = 0;
		for (int i = 0; i < 5; ++i) {
			if (!m_group_members[i].name.empty()) data.memberCount++;
		}
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::GroupChanged, std::move(data)));

		for (int i = 0; i < 5; ++i) {
			if (m_group_members[i].name.empty()) continue;
			eqt::state::GroupMemberUpdatedData mdata;
			mdata.memberIndex = i;
			mdata.name = m_group_members[i].name;
			mdata.level = m_group_members[i].level;
			mdata.classId = m_group_members[i].class_id;
			mdata.hpPercent = m_group_members[i].hp_percent;
			mdata.inZone = m_group_members[i].in_zone;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::GroupMemberUpdated, std::move(mdata)));
		}
	}

	// --- Pet ---
	if (m_pet_spawn_id != 0) {
		eqt::state::PetCreatedData data;
		data.spawnId = m_pet_spawn_id;
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

	// S04: inventory_manager, spell DB, buff_manager, spell_effects, spell_type_processor
	// are all pre-allocated in EverQuest constructor / Application::initialize()

	EQ::Net::DaybreakConnectionManagerOptions zone_opts;
	zone_opts.skip_crc_validation = true;  // TODO: Remove after CRC bug is fixed
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
		m_zone_connected = true;
		// Send stream identify immediately after connection
		ZoneSendStreamIdentify();
		m_zone_session_established = true;

		// Send ack packet and zone entry immediately after session established
		ZoneSendAckPacket();
		ZoneSendZoneEntry();
	}

	if (to == EQ::Net::StatusDisconnected) {
		LOG_INFO(MOD_ZONE, "Zone connection lost, reconnecting.");
		m_zone_connected = false;
		m_game_state.world().setZoneConnected(false);
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

	switch (opcode) {
	case HC_OP_SessionReady:
		LOG_DEBUG(MOD_ZONE, "Zone session established, sending ack and zone entry");
		// Send ack packet
		ZoneSendAckPacket();
		// Send zone entry
		ZoneSendZoneEntry();
		break;
	case HC_OP_PlayerProfile:
		ZoneProcessPlayerProfile(p);
		break;
	case HC_OP_ZoneEntry:
		// Server echoes our zone entry back with ServerZoneEntry_Struct
		LOG_DEBUG(MOD_MAIN, "Zone entry response, size: {}", p.Length());
		
		// ServerZoneEntry_Struct contains NewSpawn_Struct which contains Spawn_Struct
		// Let's check for spawn ID at various offsets
		if (p.Length() > 10) {
			// Try to find spawn ID - it might be early in the spawn struct
			LOG_DEBUG(MOD_MAIN, "Potential spawn IDs in ZoneEntry response:");
			LOG_DEBUG(MOD_MAIN, "  uint16 at offset 2: {}", p.GetUInt16(2));
			LOG_DEBUG(MOD_MAIN, "  uint16 at offset 4: {}", p.GetUInt16(4));
			LOG_DEBUG(MOD_MAIN, "  uint32 at offset 2: {}", p.GetUInt32(2));
			LOG_DEBUG(MOD_MAIN, "  uint32 at offset 6: {}", p.GetUInt32(6));
			
			// The spawn struct has name at offset 7, let's check before that
			if (p.Length() > 71) {
				auto spawn_name = p.GetCString(9); // offset 2 + 7
				LOG_DEBUG(MOD_MAIN, "  Spawn name at offset 7: '{}'", spawn_name);
			}
		}
		break;
	case HC_OP_ZoneSpawns:
		ZoneProcessZoneSpawns(p);
		break;
	case HC_OP_TimeOfDay:
		ZoneProcessTimeOfDay(p);
		break;
	case HC_OP_TributeUpdate:
		ZoneProcessTributeUpdate(p);
		break;
	case HC_OP_TributeTimer:
		ZoneProcessTributeTimer(p);
		break;
	case HC_OP_CharInventory:
		ZoneProcessCharInventory(p);
		break;
	case HC_OP_Weather:
		ZoneProcessWeather(p);
		break;
	case HC_OP_NewZone:
		ZoneProcessNewZone(p);
		break;
	case HC_OP_SendAATable:
		ZoneProcessSendAATable(p);
		break;
	case HC_OP_RespondAA:
		ZoneProcessRespondAA(p);
		break;
	case HC_OP_TributeInfo:
		ZoneProcessTributeInfo(p);
		break;
	case HC_OP_SendGuildTributes:
		ZoneProcessSendGuildTributes(p);
		break;
	case HC_OP_SpawnDoor:
		ZoneProcessSpawnDoor(p);
		break;
	case HC_OP_GroundSpawn:
		ZoneProcessGroundSpawn(p);
		break;
	case HC_OP_ClickObjectAction:
		ZoneProcessClickObjectAction(p);
		break;
	case HC_OP_TradeSkillCombine:
		ZoneProcessTradeSkillCombine(p);
		break;
	case HC_OP_SendZonepoints:
		ZoneProcessSendZonepoints(p);
		break;
	case HC_OP_SendAAStats:
		ZoneProcessSendAAStats(p);
		break;
	case HC_OP_SendExpZonein:
		ZoneProcessSendExpZonein(p);
		break;
	case HC_OP_WorldObjectsSent:
		ZoneProcessWorldObjectsSent(p);
		break;
	case HC_OP_SpawnAppearance:
		ZoneProcessSpawnAppearance(p);
		break;
	case HC_OP_ExpUpdate:
		ZoneProcessExpUpdate(p);
		break;
	case HC_OP_RaidUpdate:
		ZoneProcessRaidUpdate(p);
		break;
	case HC_OP_GuildMOTD:
		ZoneProcessGuildMOTD(p);
		break;
	case HC_OP_NewSpawn:
		ZoneProcessNewSpawn(p);
		break;
	case HC_OP_ClientUpdate:
		ZoneProcessClientUpdate(p);
		break;
	case HC_OP_DeleteSpawn:
		ZoneProcessDeleteSpawn(p);
		break;
	case HC_OP_MobHealth:
		ZoneProcessMobHealth(p);
		break;
	case HC_OP_HPUpdate:
		ZoneProcessHPUpdate(p);
		break;
	case HC_OP_ChannelMessage:
		ZoneProcessChannelMessage(p);
		break;
	case HC_OP_WearChange:
		ZoneProcessWearChange(p);
		break;
	case HC_OP_Illusion:
		ZoneProcessIllusion(p);
		break;
	case HC_OP_MoveDoor:
		ZoneProcessMoveDoor(p);
		break;
	case HC_OP_CompletedTasks:
		ZoneProcessCompletedTasks(p);
		break;
	case HC_OP_TaskDescription:
		// Task description data - not used in P1999
		LOG_DEBUG(MOD_ZONE, "Received TaskDescription ({} bytes)", p.Length());
		break;
	case HC_OP_TaskActivity:
		// Task activity update - not used in P1999
		LOG_DEBUG(MOD_ZONE, "Received TaskActivity ({} bytes)", p.Length());
		break;
	case HC_OP_ClearLeadershipAbilities:
		// Clear leadership AA on zone-in
		LOG_DEBUG(MOD_ZONE, "Received ClearLeadershipAbilities");
		break;
	case HC_OP_ClearAA:
		// Clear AA on zone-in (resets client state)
		LOG_DEBUG(MOD_ZONE, "Received ClearAA");
		break;
	case HC_OP_AAExpUpdate:
		// AA experience update
		LOG_DEBUG(MOD_ZONE, "Received AAExpUpdate ({} bytes)", p.Length());
		break;
	case HC_OP_DzCompass:
		ZoneProcessDzCompass(p);
		break;
	case HC_OP_DzExpeditionLockoutTimers:
		ZoneProcessDzExpeditionLockoutTimers(p);
		break;
	case HC_OP_BeginCast:
		ZoneProcessBeginCast(p);
		break;
	case HC_OP_ManaChange:
		ZoneProcessManaChange(p);
		break;
	case HC_OP_Buff:
		ZoneProcessBuff(p);
		break;
	case HC_OP_ColoredText:
		ZoneProcessColoredText(p);
		break;
	case HC_OP_FormattedMessage:
		ZoneProcessFormattedMessage(p);
		break;
	case HC_OP_PlayerStateAdd:
		ZoneProcessPlayerStateAdd(p);
		break;
	case HC_OP_Death:
		ZoneProcessDeath(p);
		break;
	case HC_OP_PlayerStateRemove:
		ZoneProcessPlayerStateRemove(p);
		break;
	case HC_OP_Stamina:
		ZoneProcessStamina(p);
		break;
	case HC_OP_Emote:
		// Animation packet - applies to all entities (not just self)
		// This is used for combat animations, emotes, and other action animations
		ZoneProcessEmote(p);
		break;
	
	// Combat-related opcodes
	case 0x65ca: // OP_Consider response
		ZoneProcessConsider(p);
		break;
	case HC_OP_Action: // OP_Action (combat/spell actions)
		ZoneProcessAction(p);
		break;
	case HC_OP_Damage:
		ZoneProcessDamage(p);
		break;
	case HC_OP_MoneyOnCorpse:
		ZoneProcessMoneyOnCorpse(p);
		break;
	case HC_OP_LootRequest:
		// Server responds to our loot request - confirms we can loot the corpse
		// The actual loot items come via ItemPacket (type 102) BEFORE this response
		if (p.Length() >= 6) {
			uint32_t corpse_id = p.GetUInt32(2);
			LOG_DEBUG(MOD_INVENTORY, "LootRequest response: corpseId={}", corpse_id);

			// Check if loot window is empty (no items to loot)
			// If empty, auto-complete the loot session immediately
			if (m_player_looting_corpse_id != 0 && m_loot_items.empty()) {
				LOG_DEBUG(MOD_INVENTORY, "LootRequest response: No items on corpse, auto-completing loot");
				CloseLootWindow(static_cast<uint16_t>(corpse_id));
			}
		}
		break;
	case HC_OP_LootItem:
		// Server echoes OP_LootItem back as acknowledgment
		if (p.Length() >= 16) {
			uint32_t lootee = p.GetUInt32(2);
			uint32_t looter = p.GetUInt32(6);
			uint16_t slot_id = p.GetUInt16(10);
			int32_t auto_loot = static_cast<int32_t>(p.GetUInt32(12));
			LOG_DEBUG(MOD_INVENTORY, "LootItem ACK: lootee={} looter={} slot={} auto_loot={}",
				lootee, looter, slot_id, auto_loot);
			// auto_loot == -1 means the loot was denied
			if (auto_loot == -1) {
				LOG_WARN(MOD_INVENTORY, "Loot DENIED by server!");
				// Remove from pending slots
				auto it = std::find(m_pending_loot_slots.begin(), m_pending_loot_slots.end(), slot_id);
				if (it != m_pending_loot_slots.end()) {
					m_pending_loot_slots.erase(it);
				}
			}
		} else {
			LOG_DEBUG(MOD_INVENTORY, "LootItem ACK: short packet, len={}", p.Length());
		}
		break;
	case HC_OP_LootComplete:
		// Server signals end of loot session (rarely sent by P1999/Titanium servers)
		LOG_DEBUG(MOD_INVENTORY, "LootComplete received from server");
		m_pending_loot_slots.clear();
		break;
	case HC_OP_ItemPacket:
		// Item packets can be: vendor items (100), inventory items (103), or loot items
		// Check the packet type in the first 4 bytes after the header
		{
			uint32_t item_packet_type = 0;
			if (p.Length() >= 6) {
				item_packet_type = p.GetUInt32(2);
			}
			LOG_DEBUG(MOD_INVENTORY, "ItemPacket received, type={} m_player_looting_corpse_id={} m_vendor_npc_id={} pending_slots={}",
				item_packet_type, m_player_looting_corpse_id, m_vendor_npc_id, m_pending_loot_slots.size());
			// Check if this is a vendor item (type 100)
			if (item_packet_type == EQT::ITEM_PACKET_MERCHANT && m_vendor_npc_id != 0) {
				// Vendor item - add to vendor window via bridge
				ZoneProcessVendorItemToUI(p);
			} else if (m_player_looting_corpse_id != 0) {
				// Player mode looting - check if this is a looted item or loot window population
				if (!m_pending_loot_slots.empty()) {
					// We have pending loot requests - this item is going to inventory
					ZoneProcessLootedItemToInventory(p);
				} else {
					// No pending requests - this is loot window population
					ZoneProcessLootItemToUI(p);
				}
			} else if (m_combat_manager && m_combat_manager->IsLooting()) {
				// Headless mode looting
				ZoneProcessLootItem(p);
			} else {
				// Check if this is a trade partner item
#ifdef EQT_HAS_GRAPHICS
				bool handled_as_trade = false;
				if (m_trade_manager && m_trade_manager->isTrading()) {
					handled_as_trade = ZoneProcessTradePartnerItem(p);
				}
				if (!handled_as_trade) {
					// Regular inventory item packet - pass to inventory manager
					if (m_inventory_manager) {
						m_inventory_manager->processItemPacket(p);
					}
				}
#endif
				if (s_debug_level >= 2) {
					LOG_DEBUG(MOD_MAIN, "Received ItemPacket type={} while not looting/trading", item_packet_type);
				}
			}
		}
		break;
#ifdef EQT_HAS_GRAPHICS
	case HC_OP_MoveItem:
		ZoneProcessMoveItem(p);
		break;
	case HC_OP_DeleteItem:
		ZoneProcessDeleteItem(p);
		break;
	case HC_OP_ShopRequest:
		ZoneProcessShopRequest(p);
		break;
	case HC_OP_ShopPlayerBuy:
		ZoneProcessShopPlayerBuy(p);
		break;
	case HC_OP_ShopPlayerSell:
		ZoneProcessShopPlayerSell(p);
		break;
	case HC_OP_ShopEndConfirm:
		ZoneProcessShopEndConfirm(p);
		break;
	case HC_OP_MoneyUpdate:
		ZoneProcessMoneyUpdate(p);
		break;
	case HC_OP_GMTraining:
		ZoneProcessGMTraining(p);
		break;
#endif
	case 0x61f9: // OP_TargetCommand response
		// Server may reject our target
		if (p.Length() >= 4) {
			uint16_t result = p.GetUInt16(2);
			if (result != 0 && s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Target rejected: {}", result);
			}
		}
		break;
	
	// Additional opcodes
	case HC_OP_BecomeCorpse:
		// Someone died and became a corpse
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Received BecomeCorpse packet");
		}
		break;
	case HC_OP_ZonePlayerToBind:
		ZoneProcessZonePlayerToBind(p);
		break;
	case HC_OP_LevelUpdate:
		ZoneProcessLevelUpdate(p);
		break;
	case HC_OP_ZoneChange:
		ZoneProcessZoneChange(p);
		break;
	case HC_OP_RequestClientZoneChange:
		ZoneProcessRequestClientZoneChange(p);
		break;
	case HC_OP_SimpleMessage:
		ZoneProcessSimpleMessage(p);
		break;
	case HC_OP_TargetHoTT:
		// Target's target information
		if (p.Length() >= 6) {
			uint16_t target_id = p.GetUInt16(2);
			uint16_t hott_id = p.GetUInt16(4);
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Target {} has target {}", target_id, hott_id);
			}
		}
		break;
	case HC_OP_SkillUpdate:
		// Skill level update
		// SkillUpdate_Struct: skillId (uint32) + value (uint32) = 8 bytes + 2 byte header
		if (p.Length() >= 10) {
			uint32_t skill_id_32 = p.GetUInt32(2);  // 4-byte skillId at offset 2
			uint32_t value = p.GetUInt32(6);        // 4-byte value at offset 6
			uint8_t skill_id = static_cast<uint8_t>(skill_id_32);  // Cast to uint8_t for skill manager

			// Update skill manager
			if (m_skill_manager) {
				m_skill_manager->updateSkill(skill_id, value);
			}

			// Decrement practice points if trainer window is open (training succeeded)
			if (m_trainer_npc_id != 0 && m_practice_points > 0) {
				--m_practice_points;
				m_game_state.player().decrementPracticePoints();
			}

			// Publish SkillValueChanged event to bridge
			if (m_bridge) {
				eqt::state::SkillValueChangedData sdata;
				sdata.skillId = static_cast<uint16_t>(skill_id);
				sdata.value = static_cast<uint16_t>(value);
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::SkillValueChanged, std::move(sdata)));
			}

			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Skill {} updated to {}", static_cast<int>(skill_id), value);
			}
		}
		break;
	case HC_OP_CancelTrade:
		// Trade cancelled
		if (p.Length() >= 2 + sizeof(EQT::CancelTrade_Struct)) {
			const EQT::CancelTrade_Struct* cancel = reinterpret_cast<const EQT::CancelTrade_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleCancelTrade(*cancel);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade cancelled by spawn {}", cancel->spawn_id);
			}
		}
		break;
	case HC_OP_TradeRequest:
		// Trade request from another player
		if (p.Length() >= 2 + sizeof(EQT::TradeRequest_Struct)) {
			const EQT::TradeRequest_Struct* req = reinterpret_cast<const EQT::TradeRequest_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				// Look up the name of the requesting player
				std::string from_name = "Unknown";
				auto it = m_entities.find(static_cast<uint16_t>(req->from_spawn_id));
				if (it != m_entities.end()) {
					from_name = it->second.name;
				}
				m_trade_manager->handleTradeRequest(*req, from_name);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade request from spawn {} to spawn {}",
				         req->from_spawn_id, req->target_spawn_id);
			}
		}
		break;
	case HC_OP_TradeRequestAck:
		// Trade request accepted
		if (p.Length() >= 2 + sizeof(EQT::TradeRequestAck_Struct)) {
			const EQT::TradeRequestAck_Struct* ack = reinterpret_cast<const EQT::TradeRequestAck_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleTradeRequestAck(*ack);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade request ack from spawn {} to spawn {}",
				         ack->from_spawn_id, ack->target_spawn_id);
			}
		}
		break;
	case HC_OP_TradeCoins:
		// Trade partner changed money
		if (p.Length() >= 2 + sizeof(EQT::TradeCoins_Struct)) {
			const EQT::TradeCoins_Struct* coins = reinterpret_cast<const EQT::TradeCoins_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleTradeCoins(*coins);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade coins from spawn {}: slot {} amount {}",
				         coins->spawn_id, coins->slot, coins->amount);
			}
		}
		break;
	case HC_OP_TradeAcceptClick:
		// Trade partner clicked accept
		if (p.Length() >= 2 + sizeof(EQT::TradeAcceptClick_Struct)) {
			const EQT::TradeAcceptClick_Struct* accept = reinterpret_cast<const EQT::TradeAcceptClick_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleTradeAcceptClick(*accept);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade accept click from spawn {}: accepted={}",
				         accept->spawn_id, accept->accepted);
			}
		}
		break;
	case HC_OP_FinishTrade:
		// Trade completed - server may send with or without struct data
		{
			EQT::FinishTrade_Struct finish = {};
			if (p.Length() >= 2 + sizeof(EQT::FinishTrade_Struct)) {
				finish = *reinterpret_cast<const EQT::FinishTrade_Struct*>(p.Data() + 2);
			}
			if (m_trade_manager) {
				m_trade_manager->handleFinishTrade(finish);
			}
			LOG_INFO(MOD_MAIN, "Trade finished");
		}
		break;
	case HC_OP_PreLogoutReply:
		// Pre-logout confirmation (server acknowledges camp request)
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Pre-logout reply received (camp acknowledged)");
		}
		break;
	case HC_OP_LogoutReply:
		ZoneProcessLogoutReply(p);
		break;
	case HC_OP_RezzRequest:
		ZoneProcessRezzRequest(p);
		break;
	case HC_OP_RezzComplete:
		ZoneProcessRezzComplete(p);
		break;
	case HC_OP_WhoAllResponse:
		ZoneProcessWhoAllResponse(p);
		break;
	case HC_OP_InspectRequest:
		ZoneProcessInspectRequest(p);
		break;
	case HC_OP_InspectAnswer:
		ZoneProcessInspectAnswer(p);
		break;
	case HC_OP_GuildInvite:
		ZoneProcessGuildInvite(p);
		break;
	case HC_OP_GetGuildMOTDReply:
		ZoneProcessGuildMOTDReply(p);
		break;
	case HC_OP_GuildMemberUpdate:
		ZoneProcessGuildMemberUpdate(p);
		break;
	case HC_OP_GuildMemberAdd:
		ZoneProcessGuildMemberAdd(p);
		break;
	// Phase 3 packet handlers
	case HC_OP_ConsentResponse:
		ZoneProcessConsentResponse(p);
		break;
	case HC_OP_DenyResponse:
		ZoneProcessDenyResponse(p);
		break;
	case HC_OP_EnvDamage:
		ZoneProcessEnvDamage(p);
		break;
	case HC_OP_DisciplineUpdate:
		ZoneProcessDisciplineUpdate(p);
		break;
	case HC_OP_DisciplineTimer:
		ZoneProcessDisciplineTimer(p);
		break;
	case HC_OP_BankerChange:
		ZoneProcessBankerChange(p);
		break;
	case HC_OP_ClearObject:
		ZoneProcessClearObject(p);
		break;
	case HC_OP_LFGAppearance:
		ZoneProcessLFGAppearance(p);
		break;
	// Phase 4 packet handlers
	case HC_OP_RequestDuel:
		ZoneProcessDuelRequest(p);
		break;
	case HC_OP_RecipeReply:
		ZoneProcessRecipeReply(p);
		break;
	case HC_OP_RecipeAutoCombine:
		ZoneProcessRecipeAutoCombine(p);
		break;
	case HC_OP_RandomReply:
		ZoneProcessRandomReply(p);
		break;
	case HC_OP_FindPersonReply:
		ZoneProcessFindPersonReply(p);
		break;
	case HC_OP_CameraEffect:
		ZoneProcessCameraEffect(p);
		break;
	case HC_OP_PlayMP3:
		ZoneProcessPlayMP3(p);
		break;
	case HC_OP_Sound:
		ZoneProcessSound(p);
		break;
	case HC_OP_GMZoneRequest:
		ZoneProcessGMZoneRequest(p);
		break;
	case HC_OP_GMFind:
		ZoneProcessGMFind(p);
		break;
	case HC_OP_GMSummon:
		ZoneProcessGMSummon(p);
		break;
	case HC_OP_MobRename:
		// Mob name change - typically when mob becomes corpse
		// The packet contains 3 name fields (each 64 bytes)
		if (p.Length() >= 4) {  // Need at least opcode + 2 bytes for parsing
			// Parse the packet to find the entity ID
			// From the log, we see the mob name appears multiple times with "'s_corpse" appended
			const char* data = (const char*)p.Data() + 2;  // Skip opcode
			int data_len = p.Length() - 2;
			
			// Extract the new name from the first occurrence
			char new_name[65] = {0};
			if (data_len >= 64) {
				memcpy(new_name, data, 64);
				new_name[64] = '\0';
			}
			
			// The mob ID isn't directly in the packet, but we can find the entity by matching the base name
			// Extract base name without "'s_corpse" suffix
			std::string name_str(new_name);
			bool is_corpse = name_str.find("'s_corpse") != std::string::npos;
			
			if (is_corpse) {
				// Extract the base name and corpse ID from name like "a_gopher_snake's_corpse126"
				size_t corpse_pos = name_str.find("'s_corpse");
				if (corpse_pos != std::string::npos) {
					std::string base_name = name_str.substr(0, corpse_pos);
					std::string corpse_suffix = name_str.substr(corpse_pos + 9);  // Skip "'s_corpse"
					
					// The number at the end is the entity ID
					uint16_t entity_id = 0;
					try {
						entity_id = std::stoi(corpse_suffix);
					} catch (...) {
						// If we can't parse the ID, try to find by matching base name
						for (auto& [id, entity] : m_entities) {
							if (entity.name == base_name + "001" || entity.name == base_name) {
								entity_id = id;
								break;
							}
						}
					}
					
					if (entity_id > 0) {
						auto it = m_entities.find(entity_id);
						if (it != m_entities.end()) {
							std::string old_name = it->second.name;
							it->second.name = new_name;
							it->second.is_corpse = true;
							if (s_debug_level >= 1) {
								LOG_INFO(MOD_ENTITY, "Entity {} became corpse: '{}' -> '{}'",
									entity_id, old_name, new_name);
							}
						}
					}
				}
			}
		}
		break;
	case HC_OP_Stun:
		// Stun effect notification
		if (p.Length() >= 6) {
			uint16_t target_id = p.GetUInt16(2);
			uint16_t duration = p.GetUInt16(4);  // Duration in 10ths of a second
			float duration_sec = duration / 10.0f;

			if (target_id == m_my_spawn_id) {
				if (s_debug_level >= 1) {
					std::cout << fmt::format("[WARNING] You have been stunned for {:.1f} seconds!", duration_sec) << std::endl;
				}
			} else if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Entity {} stunned for {:.1f} seconds", target_id, duration_sec);
			}
		}
		break;

	// Group packet handlers
	case HC_OP_GroupInvite:
		ZoneProcessGroupInvite(p);
		break;
	case HC_OP_GroupInvite2:
		// Alternate invite opcode - same handler
		ZoneProcessGroupInvite(p);
		break;
	case HC_OP_GroupFollow:
		ZoneProcessGroupFollow(p);
		break;
	case HC_OP_GroupUpdate:
		ZoneProcessGroupUpdate(p);
		break;
	case HC_OP_GroupDisband:
		ZoneProcessGroupDisband(p);
		break;
	case HC_OP_GroupCancelInvite:
		ZoneProcessGroupCancelInvite(p);
		break;
	case HC_OP_SetGroupTarget:
		// Group target indicator - ignore for now
		break;
	// Note: HC_OP_LFGAppearance handled earlier with ZoneProcessLFGAppearance
	case HC_OP_LinkedReuse:
		// Linked spell reuse timer - ignore for now
		break;
	case HC_OP_MemorizeSpell:
		{
			// MemorizeSpell_Struct: slot(4), spell_id(4), scribing(4), unknown0(4)
			// Total: 16 bytes after opcode (2 bytes)
			if (p.Length() < 18) {
				LOG_WARN(MOD_SPELL, "MemorizeSpell packet too short: {} bytes", p.Length());
				break;
			}

			uint32_t slot = p.GetUInt32(2);
			uint32_t spell_id = p.GetUInt32(6);
			uint32_t scribing = p.GetUInt32(10);

			LOG_DEBUG(MOD_SPELL, "MemorizeSpell response: slot={} spell_id={} scribing={}",
				slot, spell_id, scribing);

			// Check if this is a scribe confirmation (scribing flag cleared to 0)
			if (m_pending_scribe_spell_id != 0 &&
				m_pending_scribe_spell_id == spell_id &&
				scribing == 0) {

				// Add spell to spellbook
				if (m_spell_manager->scribeSpell(spell_id, static_cast<uint16_t>(slot))) {
					// Get spell name for message
					const EQ::SpellData* spellData = m_spell_manager->getSpell(spell_id);
					std::string spellName = spellData ? spellData->name : "spell";

					AddChatSystemMessage(fmt::format("You have learned {}!", spellName));
					LOG_INFO(MOD_SPELL, "Successfully scribed spell {} ({}) to slot {}",
						spellName, spell_id, slot);

					// Refresh spellbook UI via bridge
					if (m_bridge) {
						eqt::state::SpellScribeCompletedData sdata;
						sdata.spellId = spell_id;
						sdata.slot = static_cast<uint16_t>(slot);
						sdata.spellName = spellName;
						sdata.iconId = spellData ? spellData->spell_icon : 0;
						m_bridge->pushEvent(eqt::state::GameEvent(
							eqt::state::GameEventType::SpellScribeCompleted, std::move(sdata)));
					}
				} else {
					LOG_WARN(MOD_SPELL, "Failed to add spell {} to spellbook slot {}",
						spell_id, slot);
				}

				// Clear pending scribe state
				m_pending_scribe_spell_id = 0;
				m_pending_scribe_book_slot = 0;
				m_pending_scribe_source_slot = -1;
			}
			// Handle gem slot memorization confirmations (slot < MAX_SPELL_GEMS)
			else if (slot < EQ::MAX_SPELL_GEMS && scribing == 0) {
				// Existing gem memorization - SpellManager handles this via other mechanisms
				LOG_DEBUG(MOD_SPELL, "Gem memorization confirmation: gem={} spell={}", slot, spell_id);
			}
		}
		break;
	case HC_OP_SpecialMesg:
		{
			// Special messages (trade notifications, skill-ups, etc.)
			// Format: 2 bytes opcode + 24 bytes header + null-terminated message
			constexpr size_t HEADER_SIZE = 24;
			constexpr size_t MSG_OFFSET = 2 + HEADER_SIZE;  // After opcode and header

			if (p.Length() > MSG_OFFSET) {
				const char* msgStart = reinterpret_cast<const char*>(p.Data()) + MSG_OFFSET;
				size_t maxLen = p.Length() - MSG_OFFSET;
				std::string message(msgStart, strnlen(msgStart, maxLen));

				if (!message.empty()) {
					AddChatSystemMessage(message);
				}
			}
		}
		break;
	case HC_OP_InterruptCast:
		// Cast interrupted - spell manager handles this via other packets
		break;

	// ========================================================================
	// Skill Response Handlers
	// ========================================================================
	case HC_OP_Begging:
		// Begging response - BeggingResponse_Struct (20 bytes after opcode)
		if (p.Length() >= 22) {
			uint32_t result = p.GetUInt32(14);  // offset 12 in struct + 2 for opcode
			uint32_t amount = p.GetUInt32(18);  // offset 16 in struct + 2 for opcode
			// Result: 0=Fail, 1=Plat, 2=Gold, 3=Silver, 4=Copper
			if (result == 0) {
				AddChatSystemMessage("You have been unable to convince your target to give you money.");
			} else {
				const char* coin_types[] = {"", "platinum", "gold", "silver", "copper"};
				if (result <= 4) {
					AddChatSystemMessage(fmt::format("You receive {} {}.", amount, coin_types[result]));
				}
			}
		}
		break;
	case HC_OP_Hide:
		// Hide skill response - typically no data, success indicated by SpawnAppearance
		LOG_DEBUG(MOD_MAIN, "Hide response received: {} bytes", p.Length());
		break;
	case HC_OP_Sneak:
		// Sneak skill response
		LOG_DEBUG(MOD_MAIN, "Sneak response received: {} bytes", p.Length());
		break;
	case HC_OP_SenseHeading:
		// Sense Heading response - may contain heading info
		LOG_DEBUG(MOD_MAIN, "SenseHeading response received: {} bytes", p.Length());
		break;
	case HC_OP_Forage:
		// Forage response - contains item found or failure
		LOG_DEBUG(MOD_MAIN, "Forage response received: {} bytes", p.Length());
		break;
	case HC_OP_Fishing:
		// Fishing response
		LOG_DEBUG(MOD_MAIN, "Fishing response received: {} bytes", p.Length());
		break;
	case HC_OP_Mend:
		// Mend response
		LOG_DEBUG(MOD_MAIN, "Mend response received: {} bytes", p.Length());
		break;
	case HC_OP_FeignDeath:
		// Feign Death response
		LOG_DEBUG(MOD_MAIN, "FeignDeath response received: {} bytes", p.Length());
		break;
	case HC_OP_Track:
		// Track response - contains list of trackable entities
		ZoneProcessTrack(p);
		break;
	case HC_OP_ApplyPoison:
		// Apply Poison response - success/failure from server
		ZoneProcessApplyPoison(p);
		break;
	case HC_OP_PickPocket:
		// Pick Pocket response
		LOG_DEBUG(MOD_MAIN, "PickPocket response received: {} bytes", p.Length());
		break;
	case HC_OP_SenseTraps:
		// Sense Traps response
		LOG_DEBUG(MOD_MAIN, "SenseTraps response received: {} bytes", p.Length());
		break;
	case HC_OP_DisarmTraps:
		// Disarm Traps response
		LOG_DEBUG(MOD_MAIN, "DisarmTraps response received: {} bytes", p.Length());
		break;
	case HC_OP_InstillDoubt:
		// Intimidation response
		LOG_DEBUG(MOD_MAIN, "InstillDoubt response received: {} bytes", p.Length());
		break;
	case HC_OP_ReadBook:
		ZoneProcessReadBook(p);
		break;
	case HC_OP_PetBuffWindow:
		ZoneProcessPetBuffWindow(p);
		break;

	default:
		LOG_WARN(MOD_ZONE, "Unhandled zone opcode: {}", GetOpcodeName(opcode));
		break;
	}
}

// Zone packet senders
void EverQuest::ZoneSendSessionReady()
{
	EQ::Net::DynamicPacket p;
	p.Resize(14);
	p.PutUInt16(0, HC_OP_SessionReady);
	p.PutUInt32(2, m_zone_sequence++);  // sequence
	p.PutUInt32(6, 0);  // unknown
	p.PutUInt32(10, 2048);  // max length
	
	DumpPacket("C->S", HC_OP_SessionReady, p);
	m_zone_connection->QueuePacket(p);
}

void EverQuest::ZoneSendZoneEntry()
{
	// Zone entry packet - ClientZoneEntry_Struct
	EQ::Net::DynamicPacket p;
	p.Resize(70);  // 2 bytes opcode + 68 bytes data
	
	p.PutUInt16(0, HC_OP_ZoneEntry);
	
	// ClientZoneEntry_Struct starts at offset 2
	// unknown00 - 32-bit value (0xFFF67726 in good log)
	p.PutUInt32(2, 0xFFF67726);  // This might need to be calculated or could be a checksum
	
	// char_name[64] at offset 6
	size_t name_offset = 6;
	size_t name_len = m_character.length();
	if (name_len > 63) name_len = 63;
	memcpy((char*)p.Data() + name_offset, m_character.c_str(), name_len);
	
	// Fill rest of name field with zeros (64 bytes total for name)
	for (size_t i = name_offset + name_len; i < name_offset + 64; i++) {
		p.PutUInt8(i, 0);
	}
	
	DumpPacket("C->S", HC_OP_ZoneEntry, p);
	m_zone_connection->QueuePacket(p);
	m_zone_entry_sent = true;
	
	// Clear movement history on zone entry
	m_movement_history.clear();
	m_last_movement_history_send = 0;  // Reset the timer to prevent immediate sends
	
	// Add zone entry to movement history
	MovementHistoryEntry zone_entry;
	zone_entry.x = m_x;
	zone_entry.y = m_y;
	zone_entry.z = m_z;
	zone_entry.type = 4; // ZoneLine type
	zone_entry.timestamp = static_cast<uint32_t>(std::time(nullptr));
	m_movement_history.push_back(zone_entry);
}

void EverQuest::ZoneSendReqClientSpawn()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ReqClientSpawn);
	
	DumpPacket("C->S", HC_OP_ReqClientSpawn, p);
	m_zone_connection->QueuePacket(p);
}

void EverQuest::ZoneSendClientReady()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ClientReady);
	
	LOG_DEBUG(MOD_ZONE, "Sending OP_ClientReady");

	DumpPacket("C->S", HC_OP_ClientReady, p);
	m_zone_connection->QueuePacket(p);
	m_client_ready_sent = true;
}

void EverQuest::ZoneSendSetServerFilter()
{
	// Set server filter for what types of messages we want
	// SetServerFilter_Struct is uint32 filters[29] = 116 bytes
	EQ::Net::DynamicPacket p;
	p.Resize(118);  // 2 bytes opcode + 116 bytes data
	
	p.PutUInt16(0, HC_OP_SetServerFilter);
	
	// Set all filters to 0xFFFFFFFF (all enabled) for now
	for (size_t i = 0; i < 29; i++) {
		p.PutUInt32(2 + (i * 4), 0xFFFFFFFF);
	}
	
	LOG_DEBUG(MOD_ZONE, "Sending OP_SetServerFilter");

	DumpPacket("C->S", HC_OP_SetServerFilter, p);
	m_zone_connection->QueuePacket(p);
	m_server_filter_sent = true;
}

void EverQuest::ZoneSendStreamIdentify()
{
	// Send stream identify with Titanium zone opcode
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ZoneEntry);  // 0x7213 - Titanium_zone
	
	LOG_DEBUG(MOD_ZONE, "Sending stream identify with opcode {:#06x} (Titanium_zone)", 0x7213);
	
	m_zone_connection->QueuePacket(p, 0, false);  // Send unreliable
}

void EverQuest::ZoneSendAckPacket()
{
	EQ::Net::DynamicPacket p;
	p.Resize(6);
	
	p.PutUInt16(0, HC_OP_AckPacket);
	p.PutUInt32(2, 0);  // sequence or something
	
	DumpPacket("C->S", HC_OP_AckPacket, p);
	m_zone_connection->QueuePacket(p);
}

void EverQuest::ZoneSendReqNewZone()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ReqNewZone);
	
	DumpPacket("C->S", HC_OP_ReqNewZone, p);
	m_zone_connection->QueuePacket(p);
	m_req_new_zone_sent = true;
}

void EverQuest::ZoneSendSendAATable()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_SendAATable);
	
	DumpPacket("C->S", HC_OP_SendAATable, p);
	m_zone_connection->QueuePacket(p);
	m_aa_table_sent = true;
}

void EverQuest::ZoneSendUpdateAA()
{
	EQ::Net::DynamicPacket p;
	p.Resize(12);  // Typical UpdateAA size
	
	p.PutUInt16(0, HC_OP_UpdateAA);
	// Rest is AA data - zeros for now
	
	DumpPacket("C->S", HC_OP_UpdateAA, p);
	m_zone_connection->QueuePacket(p);
	m_update_aa_sent = true;
}

void EverQuest::ZoneSendSendTributes()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_SendTributes);
	
	DumpPacket("C->S", HC_OP_SendTributes, p);
	m_zone_connection->QueuePacket(p);
	m_tributes_sent = true;
}

void EverQuest::ZoneSendRequestGuildTributes()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_RequestGuildTributes);
	
	DumpPacket("C->S", HC_OP_RequestGuildTributes, p);
	m_zone_connection->QueuePacket(p);
	m_guild_tributes_sent = true;
}

void EverQuest::ZoneSendSpawnAppearance()
{
	EQ::Net::DynamicPacket p;
	p.Resize(14);  // Typical spawn appearance size
	
	p.PutUInt16(0, HC_OP_SpawnAppearance);
	p.PutUInt16(2, 0);  // spawn id (0 = self)
	p.PutUInt16(4, 14);  // type (14 = animation)
	p.PutUInt32(6, 0);  // value
	p.PutUInt32(10, 0);  // unknown
	
	DumpPacket("C->S", HC_OP_SpawnAppearance, p);
	m_zone_connection->QueuePacket(p);
	m_spawn_appearance_sent = true;
}

void EverQuest::ZoneSendSendExpZonein()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_SendExpZonein);
	
	DumpPacket("C->S", HC_OP_SendExpZonein, p);
	m_zone_connection->QueuePacket(p);
	m_exp_zonein_sent = true;
}

// Zone packet processors
void EverQuest::ZoneProcessNewZone(const EQ::Net::Packet &p)
{
	// NewZone packet contains zone information
	// NewZone_Struct: char_name[64], zone_short_name[32], zone_long_name[278], ...
	// zone_id at offset 684, total struct size 700 bytes
	if (p.Length() >= 96) {  // Need at least 64 + 32 bytes for zone name
		// Skip opcode (2 bytes) and char_name (64 bytes) to get to zone_short_name
		m_current_zone_name = p.GetCString(66);  // 2 (opcode) + 64 (char_name)

		// Extract zone_id if packet is large enough (offset 684 in struct, 686 in packet)
		if (p.Length() >= 688) {  // 2 (opcode) + 684 + 2 (zone_id)
			m_current_zone_id = p.GetUInt16(686);
		}

		// Extract zone environment data (sky type, fog colors)
		// Offsets are +2 for opcode prefix
		if (p.Length() >= 477) {  // Need sky field at offset 474 (+2 = 476)
			// Zone type at offset 374 (0=outdoor, 1=dungeon, etc.)
			m_zone_type = p.GetUInt8(376);

			// Sky type at offset 474
			m_zone_sky_type = p.GetUInt8(476);

			// Fog colors at offsets 375-386
			for (int i = 0; i < 4; i++) {
				m_zone_fog_red[i] = p.GetUInt8(377 + i);
				m_zone_fog_green[i] = p.GetUInt8(381 + i);
				m_zone_fog_blue[i] = p.GetUInt8(385 + i);
			}

			// Fog clip distances at offsets 388-419
			if (p.Length() >= 422) {
				for (int i = 0; i < 4; i++) {
					m_zone_fog_minclip[i] = p.GetFloat(390 + i * 4);
					m_zone_fog_maxclip[i] = p.GetFloat(406 + i * 4);
				}
			}

			if (s_debug_level >= 3) {
				LOG_DEBUG(MOD_MAIN, "Zone environment: ztype={}, sky={}, fog RGB=({},{},{}), clip={}-{}",
					m_zone_type, m_zone_sky_type,
					m_zone_fog_red[0], m_zone_fog_green[0], m_zone_fog_blue[0],
					m_zone_fog_minclip[0], m_zone_fog_maxclip[0]);
			}
		}

		// Extract safe spawn coordinates (struct offsets 492/496/500, +2 for opcode)
		if (p.Length() >= 504) {
			float safe_server_y = p.GetFloat(494);  // safe_y in struct = server Y
			float safe_server_x = p.GetFloat(498);  // safe_x in struct = server X
			float safe_z = p.GetFloat(502);         // safe_z
			// Swap X/Y from server INVERSEXY format to client coords
			m_safe_x = safe_server_y;
			m_safe_y = safe_server_x;
			m_safe_z = safe_z;
			LOG_DEBUG(MOD_ZONE, "Zone safe coordinates: server=({:.2f},{:.2f},{:.2f}) -> client=({:.2f},{:.2f},{:.2f})",
			          safe_server_x, safe_server_y, safe_z, m_safe_x, m_safe_y, m_safe_z);
		}

		// Phase 7.3: Also update WorldState
		m_game_state.world().setZone(m_current_zone_name, m_current_zone_id);

		// Publish ZoneChanged event to bridge
		if (m_bridge) {
			eqt::state::ZoneChangedData data;
			data.zoneName = m_current_zone_name;
			data.zoneId = m_current_zone_id;
			data.x = m_safe_x;
			data.y = m_safe_y;
			data.z = m_safe_z;
			data.heading = 0.0f;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::ZoneChanged, std::move(data)));
		}

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Received new zone data for: {} (zone_id={})",
				m_current_zone_name, m_current_zone_id);
		}

		// Load pathfinder for this zone
		LoadPathfinder(m_current_zone_name);

		// Load zone map for terrain height calculations
		LoadZoneMap(m_current_zone_name);

		// Load zone lines for zone transitions
		LoadZoneLines(m_current_zone_name);

		// Open zone S3D archive, parse WLD, extract BSP tree
		LoadZoneSources(m_current_zone_name);

		// Update zone music and sound emitters
#ifdef WITH_AUDIO
		// Phase 13: Load zone sound emitters FIRST (unloadZone() stops music)
		if (m_zone_audio_manager && !m_eq_client_path.empty()) {
			m_zone_audio_manager->loadZone(m_current_zone_name, m_eq_client_path);
		}
		// THEN start zone music (after loadZone to avoid being stopped by unloadZone)
		if (m_audio_manager) {
			m_audio_manager->onZoneChange(m_current_zone_name);
		}
#endif
	} else {
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Received new zone data");
		}
	}
	
	m_new_zone_received = true;

	// NOTE: Zone graphics are now loaded in LoadZoneGraphics() which is called from
	// OnGameStateComplete() when we receive our first ClientUpdate. This ensures
	// the progress bar shows during the entire loading process.

	// After NewZone, send all parallel requests
	// 1. AA table request
	if (!m_aa_table_sent) {
		ZoneSendSendAATable();
	}
	
	// 2. Update AA
	if (!m_update_aa_sent) {
		ZoneSendUpdateAA();
	}
	
	// 3. Send tributes request
	if (!m_tributes_sent) {
		ZoneSendSendTributes();
	}
	
	// 4. Request guild tributes
	if (!m_guild_tributes_sent) {
		ZoneSendRequestGuildTributes();
	}
}

void EverQuest::ZoneProcessPlayerProfile(const EQ::Net::Packet &p)
{
	SetLoadingPhase(LoadingPhase::ZONE_RECEIVING_PROFILE);

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "PlayerProfile received, size={} bytes", p.Length());
	}

	// PlayerProfile_Struct from titanium_structs.h
	// All offsets need +2 for the opcode
	const size_t base = 2;

	// Extract all player data from the profile
	if (p.Length() < base + 19588) {  // PlayerProfile_Struct is 19588 bytes
		LOG_WARN(MOD_MAIN, "PlayerProfile packet too small: {} bytes (expected at least {})",
			p.Length(), base + 19588);
	}
	
	// Basic attributes (offsets from struct)
	uint32_t gender = p.GetUInt32(base + 4);     // offset 4
	uint32_t race = p.GetUInt32(base + 8);       // offset 8
	uint32_t class_ = p.GetUInt32(base + 12);    // offset 12
	uint8_t level = p.GetUInt8(base + 20);       // offset 20
	uint32_t deity = p.GetUInt32(base + 124);    // offset 124

	// Bind points - array of 5 BindStruct at offset 24, each 20 bytes
	// BindStruct: zone_id (4), x (4), y (4), z (4), heading (4)
	// First bind point (index 0) is the main bind location
	uint32_t bind_zone_id = p.GetUInt32(base + 24);       // offset 24
	float bind_x = p.GetFloat(base + 28);                  // offset 28
	float bind_y = p.GetFloat(base + 32);                  // offset 32
	float bind_z = p.GetFloat(base + 36);                  // offset 36
	float bind_heading = p.GetFloat(base + 40);            // offset 40

	// Appearance
	uint8_t haircolor = p.GetUInt8(base + 172);  // offset 172
	uint8_t beardcolor = p.GetUInt8(base + 173); // offset 173
	uint8_t eyecolor1 = p.GetUInt8(base + 174);  // offset 174
	uint8_t eyecolor2 = p.GetUInt8(base + 175);  // offset 175
	uint8_t hairstyle = p.GetUInt8(base + 176);  // offset 176
	uint8_t beard = p.GetUInt8(base + 177);      // offset 177
	uint8_t face = p.GetUInt8(base + 2264);      // offset 2264
	
	// Experience points
	uint32_t exp = p.GetUInt32(base + 2268);     // offset 2268
	uint32_t exp_aa = p.GetUInt32(base + 2288);  // offset 2288 (AA exp)

	// Stats - these are base stats without equipment
	uint32_t cur_hp = p.GetUInt32(base + 2232);  // offset 2232
	uint32_t mana = p.GetUInt32(base + 2228);    // offset 2228
	uint32_t endurance = p.GetUInt32(base + 6148); // offset 6148
	uint32_t STR = p.GetUInt32(base + 2236);     // offset 2236
	uint32_t STA = p.GetUInt32(base + 2240);     // offset 2240
	uint32_t CHA = p.GetUInt32(base + 2244);     // offset 2244
	uint32_t DEX = p.GetUInt32(base + 2248);     // offset 2248
	uint32_t INT = p.GetUInt32(base + 2252);     // offset 2252
	uint32_t AGI = p.GetUInt32(base + 2256);     // offset 2256
	uint32_t WIS = p.GetUInt32(base + 2260);     // offset 2260

	// Practice points (unspent training points)
	uint32_t practice_points = p.GetUInt32(base + 2224);  // offset 2224

	// Currency - player's on-person money
	uint32_t platinum = p.GetUInt32(base + 4428);  // offset 4428
	uint32_t gold = p.GetUInt32(base + 4432);      // offset 4432
	uint32_t silver = p.GetUInt32(base + 4436);    // offset 4436
	uint32_t copper = p.GetUInt32(base + 4440);    // offset 4440

	// Bank currency - money stored in bank
	// From titanium_structs.h: platinum_bank at 13136, gold_bank at 13140, etc.
	uint32_t bank_platinum = p.GetUInt32(base + 13136);  // offset 13136
	uint32_t bank_gold = p.GetUInt32(base + 13140);      // offset 13140
	uint32_t bank_silver = p.GetUInt32(base + 13144);    // offset 13144
	uint32_t bank_copper = p.GetUInt32(base + 13148);    // offset 13148

	// Character identity
	char name[64] = {0};
	char last_name[32] = {0};
	if (p.Length() >= base + 13036) {
		memcpy(name, static_cast<const char*>(p.Data()) + base + 12940, 64);      // offset 12940
		memcpy(last_name, static_cast<const char*>(p.Data()) + base + 13004, 32); // offset 13004
	}
	uint32_t guild_id = p.GetUInt32(base + 13036); // offset 13036
	
	// Position data is stored differently - it's in bind points at offset 24
	// But the current position is at offset 13116-13128
	// Note: Server x/y are swapped relative to zone geometry
	float server_x = p.GetFloat(base + 13116);      // offset 13116
	float server_y = p.GetFloat(base + 13120);      // offset 13120
	float z = p.GetFloat(base + 13124);      // offset 13124
	float heading = p.GetFloat(base + 13128); // offset 13128

	// Update our position (swap x and y from server)
	m_x = server_y;
	m_y = server_x;
	m_z = z;
	// PlayerProfile heading is stored as (12-bit scaled value / 4)
	// Convert back: server_heading_deg = heading * 360 / 512, then m_heading = 90 - server_heading_deg
	float server_heading_deg = heading * 360.0f / 512.0f;
	m_heading = 90.0f - server_heading_deg;
	if (m_heading < 0.0f) m_heading += 360.0f;
	if (m_heading >= 360.0f) m_heading -= 360.0f;

	// Debug: Zone-in position tracking
	if (s_debug_level >= 1) {
		LOG_INFO(MOD_ZONE, "[ZONE-IN] PlayerProfile position: server=({:.2f},{:.2f},{:.2f}) heading={:.2f} -> client=({:.2f},{:.2f},{:.2f}) heading={:.2f}",
		         server_x, server_y, z, heading, m_x, m_y, m_z, m_heading);
		// Additional logging in consistent format for position debugging
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C PlayerProfile [SELF] profile_heading={:.2f} -> server_heading_deg={:.2f} -> m_heading={:.2f}deg",
			heading, server_heading_deg, m_heading);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C PlayerProfile [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> client_pos=({:.2f},{:.2f},{:.2f})",
			server_x, server_y, z, m_x, m_y, m_z);
	}

	// Sync initial position to bridge (important for Player mode)
	if (m_bridge) {
		float heading512 = m_heading * 512.0f / 360.0f;
		if (s_debug_level >= 1) {
			LOG_INFO(MOD_ZONE, "[ZONE-IN] Publishing PlayerMoved from PlayerProfile: ({:.2f},{:.2f},{:.2f}) heading={:.2f}deg -> {:.2f} (512 fmt)",
			         m_x, m_y, m_z, m_heading, heading512);
		}
		eqt::state::PlayerMovedData data;
		data.x = m_x; data.y = m_y; data.z = m_z;
		data.heading = heading512;
		data.dx = 0; data.dy = 0; data.dz = 0;
		data.isMoving = false;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMoved, std::move(data)));
	}

	// Store character stats (update both local members and GameState during migration)
	m_level = level;
	m_exp = exp;
	m_exp_aa = exp_aa;
	m_class = class_;
	m_race = race;
	m_gender = gender;
	m_deity = deity;
	m_cur_hp = cur_hp;
	m_max_hp = cur_hp;  // Will be updated when we get buffs/equipment
	m_mana = mana;
	m_max_mana = mana;
	m_endurance = endurance;
	m_max_endurance = endurance;
	m_STR = STR;
	m_STA = STA;
	m_CHA = CHA;
	m_DEX = DEX;
	m_INT = INT;
	m_AGI = AGI;
	m_WIS = WIS;
	m_platinum = platinum;
	m_gold = gold;
	m_silver = silver;
	m_copper = copper;
	m_bank_platinum = bank_platinum;
	m_bank_gold = bank_gold;
	m_bank_silver = bank_silver;
	m_bank_copper = bank_copper;
	m_practice_points = practice_points;
	m_last_name = last_name;

	// Phase 7.2: Also update GameState
	auto& player = m_game_state.player();
	player.setName(name[0] ? name : m_character);  // Use server name if available, else local
	player.setLastName(last_name);
	player.setLevel(level);
	player.setClass(class_);
	player.setRace(race);
	player.setGender(gender);
	player.setDeity(deity);
	player.setHP(cur_hp, cur_hp);
	player.setMana(mana, mana);
	player.setEndurance(endurance, endurance);
	player.setAttributes(STR, STA, CHA, DEX, INT, AGI, WIS);
	player.setCurrency(platinum, gold, silver, copper);
	player.setBankCurrency(bank_platinum, bank_gold, bank_silver, bank_copper);
	player.setPracticePoints(practice_points);

	// Initialize skill manager with player info first
	if (m_skill_manager) {
		m_skill_manager->initialize(static_cast<uint8_t>(class_), static_cast<uint8_t>(race), level);

		// Skills are stored as uint32_t[100] starting at offset 4460
		// Reference: EQEmu titanium_structs.h PlayerProfile_Struct
		const size_t skill_offset = 4460;
		if (p.Length() >= base + skill_offset + (EQT::MAX_PP_SKILL * 4)) {
			uint32_t skills[EQT::MAX_PP_SKILL];
			int nonzero_count = 0;
			for (size_t i = 0; i < EQT::MAX_PP_SKILL; ++i) {
				skills[i] = p.GetUInt32(base + skill_offset + (i * 4));
				if (skills[i] > 0) {
					nonzero_count++;
					if (s_debug_level >= 2) {
						LOG_DEBUG(MOD_MAIN, "Skill[{}] = {}", i, skills[i]);
					}
				}
			}
			m_skill_manager->updateAllSkills(skills, EQT::MAX_PP_SKILL);
			LOG_INFO(MOD_MAIN, "Loaded {} skills from PlayerProfile ({} non-zero)", EQT::MAX_PP_SKILL, nonzero_count);
			if (m_bridge) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::SkillsRefreshed, eqt::state::SkillsRefreshedData{}));
			}
		} else {
			LOG_WARN(MOD_MAIN, "PlayerProfile too small for skills: {} < {}",
				p.Length(), base + skill_offset + (EQT::MAX_PP_SKILL * 4));
		}
	}

#ifdef EQT_HAS_GRAPHICS
	// Update inventory manager with player info for item restriction validation
	if (m_inventory_manager) {
		m_inventory_manager->setPlayerInfo(race, class_, level);
	}
#endif

	// Store bind point (swap x/y like we do for position)
	m_bind_zone_id = bind_zone_id;
	m_bind_x = bind_y;  // Server x/y are swapped
	m_bind_y = bind_x;
	m_bind_z = bind_z;
	m_bind_heading = bind_heading;
	// Phase 7.3: Also update PlayerState bind point
	m_game_state.player().setBindPoint(bind_zone_id, m_bind_x, m_bind_y, m_bind_z, bind_heading);

	// The entity ID appears to be at offset 14384 based on previous debugging
	uint32_t entity_id = 0;
	if (p.Length() > base + 14384) {
		entity_id = p.GetUInt32(base + 14384);
		uint16_t new_spawn_id = static_cast<uint16_t>(entity_id);
		if (m_my_spawn_id != 0 && m_my_spawn_id != new_spawn_id) {
			LOG_WARN(MOD_ZONE, "Spawn ID changed unexpectedly: {} -> {}", m_my_spawn_id, new_spawn_id);
		}
		m_my_spawn_id = new_spawn_id;
	}
	
	// For size, we need to check if it's sent in the profile or if we get it from spawn
	// It's not in the standard PlayerProfile_Struct, so we'll use default
	float size = 6.0f;  // Default player size
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "=== CHARACTER DATA ===");
		LOG_DEBUG(MOD_MAIN, "Name: {} {} (Level {} {} {})",
			name, last_name, level, GetClassName(class_), GetRaceName(race));
		LOG_DEBUG(MOD_MAIN, "Gender: {} Race: {} Class: {} Deity: {}",
			gender, race, class_, deity);
		LOG_DEBUG(MOD_MAIN, "=== POSITION DATA ===");
		LOG_DEBUG(MOD_MAIN, "Server raw: x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
			server_x, server_y, z, heading);
		LOG_DEBUG(MOD_MAIN, "Internal (swapped): m_x={:.2f} m_y={:.2f} m_z={:.2f} m_heading={:.2f}",
			m_x, m_y, m_z, m_heading);
		LOG_DEBUG(MOD_MAIN, "Size: {:.2f} (default, will be updated from spawn)", size);
		LOG_DEBUG(MOD_MAIN, "=== BIND POINT ===");
		LOG_DEBUG(MOD_MAIN, "Bind raw: zone={} x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
			bind_zone_id, bind_x, bind_y, bind_z, bind_heading);
		LOG_DEBUG(MOD_MAIN, "Bind internal: zone={} m_bind_x={:.2f} m_bind_y={:.2f} m_bind_z={:.2f}",
			m_bind_zone_id, m_bind_x, m_bind_y, m_bind_z);
		LOG_DEBUG(MOD_MAIN, "=== STATS ===");
		LOG_DEBUG(MOD_MAIN, "HP: {} Mana: {} End: {}", cur_hp, mana, endurance);
		LOG_DEBUG(MOD_MAIN, "STR:{} STA:{} CHA:{} DEX:{} INT:{} AGI:{} WIS:{}",
			STR, STA, CHA, DEX, INT, AGI, WIS);
		LOG_DEBUG(MOD_MAIN, "Currency: {}pp {}gp {}sp {}cp", platinum, gold, silver, copper);
		LOG_DEBUG(MOD_MAIN, "Bank Currency: {}pp {}gp {}sp {}cp", bank_platinum, bank_gold, bank_silver, bank_copper);
		LOG_DEBUG(MOD_MAIN, "Practice points (training sessions): {}", practice_points);
		LOG_DEBUG(MOD_MAIN, "Entity ID: {} (from offset 14384)", entity_id);
		LOG_DEBUG(MOD_MAIN, "===========================");
	}
	
	// Add ourselves to the entity list
	Entity self_entity;
	self_entity.spawn_id = m_my_spawn_id;
	self_entity.name = std::string(name);
	// Use already-swapped m_x and m_y, and transformed heading
	self_entity.x = m_x;
	self_entity.y = m_y;
	self_entity.z = m_z;
	self_entity.heading = m_heading;  // Use transformed heading (360 - raw_heading)
	self_entity.level = level;
	self_entity.class_id = static_cast<uint8_t>(class_);
	self_entity.race_id = static_cast<uint16_t>(race);
	self_entity.gender = static_cast<uint8_t>(gender);
	self_entity.guild_id = guild_id;
	self_entity.hp_percent = 100;  // We'll get actual HP from spawn or HP update packets
	self_entity.cur_mana = static_cast<uint16_t>(mana);
	self_entity.max_mana = static_cast<uint16_t>(mana);  // Will be updated from spawn
	self_entity.size = size;
	self_entity.animation = 0;
	self_entity.delta_x = 0;
	self_entity.delta_y = 0;
	self_entity.delta_z = 0;
	self_entity.delta_heading = 0;
	self_entity.last_update_time = std::time(nullptr);
	
	// Update our own size
	m_size = size;
	
	// Store in entity list (sync to both local map and EntityManager)
	m_entities[m_my_spawn_id] = self_entity;
	SyncEntityToGameState(self_entity);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Added self to entity list: {} (ID: {})", self_entity.name, m_my_spawn_id);
	}

	// Extract spell gems and spellbook from PlayerProfile
	// Titanium offsets: spell_book at 2312 (400 x uint32), mem_spells at 4360 (8 x uint32)
	if (m_spell_manager) {
		constexpr size_t SPELLBOOK_OFFSET = 2312;
		constexpr size_t MEM_SPELLS_OFFSET = 4360;
		constexpr size_t SPELLBOOK_SIZE = 400;
		constexpr size_t SPELL_GEM_COUNT = 8;

		// Extract spellbook (400 spell IDs)
		if (p.Length() >= base + SPELLBOOK_OFFSET + SPELLBOOK_SIZE * 4) {
			std::array<uint32_t, 400> spellbook;
			for (size_t i = 0; i < SPELLBOOK_SIZE; ++i) {
				spellbook[i] = p.GetUInt32(base + SPELLBOOK_OFFSET + i * 4);
			}
			m_spell_manager->setSpellbook(spellbook.data(), SPELLBOOK_SIZE);

			// Count non-empty spellbook entries for debug
			size_t scribed_count = 0;
			for (size_t i = 0; i < SPELLBOOK_SIZE; ++i) {
				if (spellbook[i] != 0xFFFFFFFF && spellbook[i] != 0) {
					scribed_count++;
				}
			}
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_SPELL, "Loaded {} spells from spellbook", scribed_count);
			}
		}

		// Extract memorized spells (8 spell gem slots)
		if (p.Length() >= base + MEM_SPELLS_OFFSET + SPELL_GEM_COUNT * 4) {
			std::array<uint32_t, 8> mem_spells;
			for (size_t i = 0; i < SPELL_GEM_COUNT; ++i) {
				mem_spells[i] = p.GetUInt32(base + MEM_SPELLS_OFFSET + i * 4);
			}
			m_spell_manager->setSpellGems(mem_spells.data(), SPELL_GEM_COUNT);

			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_SPELL, "Memorized spells:");
				for (size_t i = 0; i < SPELL_GEM_COUNT; ++i) {
					if (mem_spells[i] != 0xFFFFFFFF && mem_spells[i] != 0) {
						const EQ::SpellData* spell = m_spell_manager->getSpell(mem_spells[i]);
						std::string name = spell ? spell->name : "Unknown";
						LOG_DEBUG(MOD_SPELL, "  Gem {}: {} (ID {})", i + 1, name, mem_spells[i]);
					}
				}
			}
		}
	}

	// Extract player buffs from PlayerProfile
	// Titanium offsets: buffs at 5008, 25 SpellBuff_Struct entries (20 bytes each)
	if (m_buff_manager) {
		constexpr size_t BUFF_OFFSET = 5008;
		constexpr size_t BUFF_COUNT = 25;
		constexpr size_t BUFF_STRUCT_SIZE = 20;

		if (p.Length() >= base + BUFF_OFFSET + BUFF_COUNT * BUFF_STRUCT_SIZE) {
			std::array<EQT::SpellBuff_Struct, 25> buffs;
			for (size_t i = 0; i < BUFF_COUNT; ++i) {
				size_t offset = base + BUFF_OFFSET + i * BUFF_STRUCT_SIZE;
				buffs[i].effect_type = p.GetUInt8(offset + 0);
				buffs[i].level = p.GetUInt8(offset + 1);
				buffs[i].bard_modifier = p.GetUInt8(offset + 2);
				buffs[i].unknown003 = p.GetUInt8(offset + 3);
				buffs[i].spellid = p.GetUInt32(offset + 4);
				buffs[i].duration = p.GetInt32(offset + 8);
				buffs[i].counters = p.GetUInt32(offset + 12);
				buffs[i].player_id = p.GetUInt32(offset + 16);
			}
			m_buff_manager->setPlayerBuffs(buffs.data(), BUFF_COUNT);

			if (s_debug_level >= 1) {
				size_t active_count = 0;
				for (size_t i = 0; i < BUFF_COUNT; ++i) {
					if (buffs[i].spellid != 0 && buffs[i].spellid != 0xFFFFFFFF && buffs[i].effect_type != 0) {
						active_count++;
						if (m_spell_manager) {
							const EQ::SpellData* spell = m_spell_manager->getSpell(buffs[i].spellid);
							std::string name = spell ? spell->name : "Unknown";
							LOG_DEBUG(MOD_SPELL, "  Buff slot {}: {} (ID {}, duration={})",
								i, name, buffs[i].spellid, buffs[i].duration);
						}
					}
				}
				LOG_DEBUG(MOD_SPELL, "Loaded {} active buffs from profile", active_count);
			}
		}
	}

	// Calculate stats from equipment and update
	UpdateInventoryStats();

	// Publish CharacterInfoChanged event to bridge
	if (m_bridge) {
		eqt::state::CharacterInfoChangedData data;
		data.name = m_character;
		data.level = m_level;
		data.className = GetClassName(m_class);
		data.deity = GetDeityName(m_deity);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CharacterInfoChanged, std::move(data)));
	}
}


void EverQuest::ZoneProcessZoneSpawns(const EQ::Net::Packet &p)
{
	SetLoadingPhase(LoadingPhase::ZONE_RECEIVING_SPAWNS);

	// ZoneSpawns contains multiple Spawn_Struct entries
	// Based on titanium_structs.h, Spawn_Struct is 385 bytes

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_ENTITY, "Received zone spawns packet, size: {} bytes", p.Length());
	}
	
	// Don't clear existing entities - ZoneSpawns packets can come in multiple batches
	// Only add/update spawns, don't remove existing ones
	
	// The ZoneSpawns packet appears to have a header before the spawn data
	size_t offset = 2;
	int spawn_count = 0;
	
	/*	
	// Debug: Dump the beginning of the packet to understand structure
	if (s_debug_level >= 1) {
		std::cout << "First 50 bytes of ZoneSpawns packet:" << std::endl;
		for (int i = 0; i < 50 && i < p.Length(); i++) {
			std::cout << fmt::format("{:02x} ", p.GetUInt8(i));
			if ((i + 1) % 16 == 0) std::cout << std::endl;
		}
		std::cout << std::endl;
		
		// Let's analyze the hex dump more carefully:
		// At offset 9: "Renux_Herkanor000" - but this is the middle of a name
		// The name field in Spawn_Struct is at offset 7
		// If "Renux_Herkanor000" is at packet offset 9, and name is at struct offset 7,
		// then the spawn struct starts at packet offset 9 - 7 = 2
		
		// But wait - we're seeing truncated names like "e_rat005" instead of "a_large_rat005"
		// This means we're starting too late. Let's check different offsets
		
		// First, let's look for a complete name pattern
		for (int test_offset = 0; test_offset < 20 && test_offset + 70 < p.Length(); test_offset++) {
			char c = (char)p.GetUInt8(test_offset);
			if (c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z') {
				std::string test_name = p.GetCString(test_offset);
				if (test_name.length() > 3 && test_name.length() < 64) {
					std::cout << fmt::format("  Possible name at offset {}: '{}'", test_offset, test_name) << std::endl;
				}
			}
		}
		
		// If we're getting "e_rat005" instead of "a_large_rat005", we're missing 6 characters
		// Current offset is 8, name field is at +7, so we're reading from packet offset 15
		// We need to start 6 bytes earlier: 8 - 6 = 2
		offset = 2;
		
		// Calculate expected spawn count based on packet size
		size_t data_size = p.Length() - offset;
		size_t expected_spawns = data_size / 385;
		std::cout << fmt::format("Packet has {} bytes of data, expecting approximately {} spawns", 
			data_size, expected_spawns) << std::endl;
	}
	*/

	// Parse Spawn_Struct entries based on titanium_structs.h
	while (offset + 385 <= p.Length()) { // Spawn_Struct is 385 bytes
		Entity entity;
		
		// Based on Spawn_Struct from titanium_structs.h:
		// name is at offset 7 (64 bytes)
		entity.name = p.GetCString(offset + 7);
		
		// If name is empty, we've likely reached the end of spawn data
		if (entity.name.empty()) {
			LOG_DEBUG(MOD_ENTITY, "Found empty name at offset {}, ending spawn parsing", offset);
			break;
		}
		
		// spawnId is at offset 340 in the struct (uint32)
		entity.spawn_id = p.GetUInt32(offset + 340);
		
		// Debug: Check what's around the spawn_id location for first few spawns
		if (spawn_count < 3 && ShouldLog(MOD_ENTITY, LOG_TRACE)) {
			LOG_TRACE(MOD_ENTITY, "Spawn at offset {}: Name='{}', checking spawn_id area:", offset, entity.name);
			for (int i = 330; i < 350 && offset + i + 4 < p.Length(); i += 4) {
				uint32_t val = p.GetUInt32(offset + i);
				if (val > 0 && val < 100000) {
					LOG_TRACE(MOD_ENTITY, "  Offset +{}: u32={} ({:#010x})", i, val, val);
				}
			}
		}
		
		// level is at offset 151
		entity.level = p.GetUInt8(offset + 151);
		
		// Position data from the bitfield structure:
		// x is at offset 94 (19 bits within a bitfield)
		// y is at offset 98 (19 bits)
		// z is at offset 102 (19 bits)
		// heading is at offset 106 (12 bits)
		
		// Read the bitfields
		uint32_t field1 = p.GetUInt32(offset + 94);  // contains deltaHeading and x
		uint32_t field2 = p.GetUInt32(offset + 98);  // contains y and animation
		uint32_t field3 = p.GetUInt32(offset + 102); // contains z and deltaY
		uint32_t field4 = p.GetUInt32(offset + 106); // contains deltaX and heading
		
		// Extract x, y, z (19 bits each, signed)
		// Positions are stored as fixed-point values (multiplied by 8)
		// Note: Server x/y are swapped relative to zone geometry, so we swap them here
		int32_t x_raw = (field1 >> 10) & 0x7FFFF;
		if (x_raw & 0x40000) x_raw |= 0xFFF80000; // Sign extend
		float server_x = static_cast<float>(x_raw) / 8.0f;

		int32_t y_raw = field2 & 0x7FFFF;
		if (y_raw & 0x40000) y_raw |= 0xFFF80000; // Sign extend
		float server_y = static_cast<float>(y_raw) / 8.0f;

		int32_t z_raw = field3 & 0x7FFFF;
		if (z_raw & 0x40000) z_raw |= 0xFFF80000; // Sign extend
		entity.z = static_cast<float>(z_raw) / 8.0f;

		// Swap x and y from server
		entity.x = server_y;
		entity.y = server_x;

		// Read npc_type early - needed for heading transformation
		entity.npc_type = p.GetUInt8(offset + 83);
		bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);

		// Extract heading (11 bits to match C->S format)
		// Server uses 512 EQ units = 360 degrees, so multiply by 360/512
		uint32_t raw_heading = (field4 >> 13) & 0x7FF;
		float server_heading = (static_cast<float>(raw_heading) / 4.0f) * 360.0f / 512.0f;
		// Use heading directly for all entities (NPCs and players)
		entity.heading = server_heading;
		if (entity.heading >= 360.0f) entity.heading -= 360.0f;

		// Debug logging for current player at level 1+
		if (s_debug_level >= 1 && entity.name == m_character) {
			LOG_DEBUG(MOD_MOVEMENT, "POS S->C ZoneSpawns [SELF] spawn_id={} name='{}'", entity.spawn_id, entity.name);
			LOG_DEBUG(MOD_MOVEMENT, "POS S->C ZoneSpawns [SELF] raw_heading={} -> server_heading={:.2f}deg -> entity.heading={:.2f}deg (isNPC={})",
				raw_heading, server_heading, entity.heading, isNPC);
			LOG_DEBUG(MOD_MOVEMENT, "POS S->C ZoneSpawns [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> entity_pos=({:.2f},{:.2f},{:.2f})",
				server_x, server_y, entity.z, entity.x, entity.y, entity.z);
		}

		// race is at offset 284 (uint32)
		entity.race_id = p.GetUInt32(offset + 284);

		// class_ is at offset 331
		entity.class_id = p.GetUInt8(offset + 331);

		// gender is at offset 334
		entity.gender = p.GetUInt8(offset + 334);

		// guildID is at offset 238
		entity.guild_id = p.GetUInt32(offset + 238);

		// curHp is at offset 86 (hp percentage)
		entity.hp_percent = p.GetUInt8(offset + 86);

		// animation is in the bitfield at offset 98
		entity.animation = (field2 >> 19) & 0x3FF;

		// size is at offset 75 (float)
		entity.size = *reinterpret_cast<const float*>(static_cast<const char*>(p.Data()) + offset + 75);

		// Appearance data from Spawn_Struct
		entity.face = p.GetUInt8(offset + 6);           // Face/head variant
		// npc_type already read above for heading transformation
		entity.haircolor = p.GetUInt8(offset + 85);     // Hair color
		entity.showhelm = p.GetUInt8(offset + 139);     // Show helm
		entity.hairstyle = p.GetUInt8(offset + 145);    // Hair style
		entity.beardcolor = p.GetUInt8(offset + 146);   // Beard color
		entity.beard = p.GetUInt8(offset + 156);        // Beard style
		entity.helm = p.GetUInt8(offset + 275);         // Helm texture
		entity.light = p.GetUInt8(offset + 330);        // Light source
		entity.bodytype = p.GetUInt8(offset + 335);     // Body type
		entity.equip_chest2 = p.GetUInt8(offset + 339); // Body texture variant

		// Equipment textures (TextureProfile at offset 197, 9 slots * 4 bytes)
		for (int i = 0; i < 9; i++) {
			entity.equipment[i] = p.GetUInt32(offset + 197 + i * 4);
		}

		// Equipment tint colors (TintProfile at offset 348, 9 slots * 4 bytes)
		for (int i = 0; i < 9; i++) {
			entity.equipment_tint[i] = p.GetUInt32(offset + 348 + i * 4);
		}

		// Initialize movement fields
		entity.delta_x = 0;
		entity.delta_y = 0;
		entity.delta_z = 0;
		entity.delta_heading = 0;
		entity.last_update_time = std::time(nullptr);

		// Store pet info (for deferred pet detection after our spawn ID is known)
		entity.is_pet = p.GetUInt8(offset + 329);
		entity.pet_owner_id = p.GetUInt32(offset + 189);

		// Validate the spawn data before adding
		if (entity.spawn_id > 0 && entity.spawn_id < 100000 && !entity.name.empty()) {
			// Check if this is our own character and update our position
			if (entity.name == m_character) {
				if (m_my_spawn_id != 0 && m_my_spawn_id != entity.spawn_id) {
					LOG_WARN(MOD_ZONE, "Spawn ID changed in ZoneSpawns: {} -> {}", m_my_spawn_id, entity.spawn_id);
				}
				m_my_spawn_id = entity.spawn_id;
				// Phase 7.4: Also update PlayerState
				m_game_state.player().setSpawnId(m_my_spawn_id);
				if (m_trade_manager) {
					m_trade_manager->setMySpawnId(m_my_spawn_id);
				}

				// Debug: Zone-in position tracking at level 1
				if (s_debug_level >= 1) {
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Found our spawn in ZoneSpawns: ID={} Name='{}'", m_my_spawn_id, m_character);
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Spawn entity: pos=({:.2f},{:.2f},{:.2f}) heading={:.2f} size={:.2f}",
					         entity.x, entity.y, entity.z, entity.heading, entity.size);
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Previous client pos: ({:.2f},{:.2f},{:.2f}) heading={:.2f}",
					         m_x, m_y, m_z, m_heading);
				}

				if (s_debug_level >= 2) {
					LOG_DEBUG(MOD_ENTITY, "=== FOUND OUR CHARACTER ===");
					LOG_DEBUG(MOD_ENTITY, "Name: '{}' Spawn ID: {}", m_character, m_my_spawn_id);
					LOG_DEBUG(MOD_ENTITY, "Position: x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
						entity.x, entity.y, entity.z, entity.heading);
					LOG_DEBUG(MOD_ENTITY, "Size: {:.2f} (THIS IS THE ACTUAL SIZE)", entity.size);
					LOG_DEBUG(MOD_ENTITY, "Previous m_z={:.2f}, updating to entity.z={:.2f}", m_z, entity.z);
					LOG_DEBUG(MOD_ENTITY, "Previous m_size={:.2f}, updating to entity.size={:.2f}", m_size, entity.size);
					LOG_DEBUG(MOD_ENTITY, "==============================");
				}
#ifdef EQT_HAS_GRAPHICS
				// Notify bridge of player spawn ID (D17b: bridge is sole path to renderer)
				if (m_bridge) {
					if (s_debug_level >= 1) {
						LOG_INFO(MOD_ZONE, "[ZONE-IN] Publishing PlayerSpawnIdSet({}) from ZoneSpawns", m_my_spawn_id);
					}
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::PlayerSpawnIdSet,
						eqt::state::PlayerSpawnIdSetData{m_my_spawn_id}));
				}
				// Mark player entity as pending creation - will be created when ClientUpdate arrives
				// This ensures the loading screen stays visible until the player model is fully loaded
				m_player_graphics_entity_pending = true;
#endif
				// Update our position to match server spawn position
				m_x = entity.x;
				m_y = entity.y;
				m_size = entity.size;
				// Server Z is model center. Convert to feet position for internal use.
				// SendPositionUpdate will add m_size/2 to convert back to model center.
				m_z = entity.z - (entity.size / 2.0f);
				// Don't update heading from spawn data as it may be scaled differently

				if (s_debug_level >= 1) {
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Updated client pos from spawn: ({:.2f},{:.2f},{:.2f}) (feet Z, server Z was {:.2f}) heading unchanged={:.2f}",
					         m_x, m_y, m_z, entity.z, m_heading);
				}

				// Update bridge with corrected feet Z position
				if (m_bridge) {
					float heading512 = m_heading * 512.0f / 360.0f;
					eqt::state::PlayerMovedData pdata;
					pdata.x = m_x; pdata.y = m_y; pdata.z = m_z;
					pdata.heading = heading512;
					pdata.dx = 0; pdata.dy = 0; pdata.dz = 0;
					pdata.isMoving = false;
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::PlayerMoved, std::move(pdata)));
				}
			}

			// Add to entity list (sync to both local map and EntityManager)
			m_entities[entity.spawn_id] = entity;
			SyncEntityToGameState(entity);
			spawn_count++;

			// NOTE: Graphics entities are NOT created here during ZoneSpawns processing.
			// All entities are created later in LoadZoneGraphics() during the graphics loading phase.
			// This ensures the progress bar shows during the entire loading process.

				LOG_DEBUG(MOD_ENTITY, "Loaded spawn {}: {} (ID: {}) Level {} Race {} Size {:.2f} at ({:.2f}, {:.2f}, {:.2f})",
				spawn_count, entity.name, entity.spawn_id, entity.level,
				entity.race_id, entity.size, entity.x, entity.y, entity.z);
			// Log all spawns with position and heading at trace level
			LOG_TRACE(MOD_ENTITY, "ZoneSpawn: {} (ID:{}) pos=({:.2f},{:.2f},{:.2f}) heading={:.2f} npc_type={}",
				entity.name, entity.spawn_id, entity.x, entity.y, entity.z, entity.heading, entity.npc_type);
		} else {
			LOG_DEBUG(MOD_ENTITY, "Skipping invalid spawn at offset {}: ID={}, Name='{}'",
				offset, entity.spawn_id, entity.name);
		}
		
		// Move to next spawn struct
		offset += 385;
	}
	
	LOG_INFO(MOD_ZONE, "Loaded {} spawns in zone", spawn_count);

	// Second pass: Check for pet after all spawns are loaded (if we know our spawn ID)
	// This handles the case where our character is in ZoneSpawns. If not, deferred
	// pet detection in ClientUpdate will find the pet when our spawn ID is set.
	if (m_my_spawn_id != 0 && m_pet_spawn_id == 0) {
		for (const auto& pair : m_entities) {
			const Entity& ent = pair.second;
			if (ent.is_pet != 0 && ent.pet_owner_id == m_my_spawn_id) {
				m_pet_spawn_id = ent.spawn_id;
				std::fill(std::begin(m_pet_button_states), std::end(m_pet_button_states), false);
				m_pet_button_states[EQT::PET_BUTTON_FOLLOW] = true;
				LOG_INFO(MOD_MAIN, "Pet detected in ZoneSpawns: {} (ID: {}) Level {} owned by us (ID: {})",
					ent.name, ent.spawn_id, ent.level, m_my_spawn_id);
#ifdef EQT_HAS_GRAPHICS
				OnPetCreated(ent);
#endif
				break;
			}
		}
	}
}

void EverQuest::ZoneProcessTimeOfDay(const EQ::Net::Packet &p)
{
	m_time_hour = p.GetUInt8(2);
	m_time_minute = p.GetUInt8(3);
	m_time_day = p.GetUInt8(4);
	m_time_month = p.GetUInt8(5);
	m_time_year = p.GetUInt16(6);

	// Phase 7.3: Also update WorldState
	m_game_state.world().setTimeOfDay(m_time_hour, m_time_minute, m_time_day, m_time_month, m_time_year);

	// Publish TimeOfDayChanged event to bridge
	if (m_bridge) {
		eqt::state::TimeOfDayChangedData data;
		data.hour = m_time_hour;
		data.minute = m_time_minute;
		data.day = m_time_day;
		data.month = m_time_month;
		data.year = m_time_year;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::TimeOfDayChanged, std::move(data)));
	}

#ifdef WITH_AUDIO
	// Phase 13: Update day/night state for audio
	UpdateDayNightState();
#endif

	LOG_DEBUG(MOD_ZONE, "Time of day: {:02d}:{:02d} {:02d}/{:02d}{}",
		m_time_hour, m_time_minute, m_time_day, m_time_month, m_time_year);
}

void EverQuest::ZoneProcessSpawnDoor(const EQ::Net::Packet &p)
{
	// Packet format: opcode(2) + array of Door_Struct(80)
	// No count field - calculate from packet size
	if (p.Length() < 2) {
		LOG_WARN(MOD_ENTITY, "SpawnDoor packet too small: {} bytes", p.Length());
		return;
	}

	size_t data_size = p.Length() - 2;  // Subtract opcode
	if (data_size % sizeof(EQT::Door_Struct) != 0) {
		LOG_WARN(MOD_ENTITY, "SpawnDoor packet size {} not divisible by Door_Struct size {}",
		         data_size, sizeof(EQT::Door_Struct));
		// Try to parse what we can
	}

	uint32_t count = static_cast<uint32_t>(data_size / sizeof(EQT::Door_Struct));
	LOG_INFO(MOD_ENTITY, "Received {} doors ({} bytes)", count, p.Length());

	// Parse each door
	for (uint32_t i = 0; i < count; ++i) {
		size_t offset = 2 + i * sizeof(EQT::Door_Struct);  // Start after opcode
		const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + offset;
		const EQT::Door_Struct* door_data = reinterpret_cast<const EQT::Door_Struct*>(data);

		EQT::DoorState door;
		door.doorId = door_data->doorId;

		// Extract name (null-terminated, max 32 chars)
		char name_buf[33] = {0};
		std::memcpy(name_buf, door_data->name, 32);
		door.name = name_buf;

		// Position - swap x and y from server (same as entity spawns)
		door.x = door_data->yPos;
		door.y = door_data->xPos;
		door.z = door_data->zPos;
		door.heading = door_data->heading;
		door.incline = door_data->incline;
		door.size = door_data->size;
		door.opentype = door_data->opentype;
		door.invertState = (door_data->invert_state != 0);
		door.doorParam = door_data->door_param;

		// Compute initial open state: XOR of spawn state and invert flag
		door.isOpen = (door_data->state_at_spawn != 0) != door.invertState;

		// Store in door state manager
		m_door_state_manager.addDoor(door);

		// Debug: dump raw bytes at critical offsets (60-68)
		LOG_DEBUG(MOD_ENTITY, "Door {} raw bytes @60-67: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
		         i, data[60], data[61], data[62], data[63], data[64], data[65], data[66], data[67]);

		LOG_DEBUG(MOD_ENTITY, "Door {}: '{}' at ({:.1f}, {:.1f}, {:.1f}) heading={:.1f} incline={} size={} type={} state_at_spawn={} invert={}",
		         door.doorId, door.name, door.x, door.y, door.z, door.heading,
		         door.incline, door.size, door.opentype, door_data->state_at_spawn, door.invertState ? 1 : 0);

		// Publish DoorSpawned event to bridge
		if (m_bridge) {
			eqt::state::DoorSpawnedData data;
			data.doorId = door.doorId;
			data.name = door.name;
			data.x = door.x;
			data.y = door.y;
			data.z = door.z;
			data.heading = door.heading;
			data.incline = door.incline;
			data.size = door.size;
			data.opentype = door.opentype;
			data.isOpen = door.isOpen;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::DoorSpawned, std::move(data)));
		}
	}
}

void EverQuest::ZoneProcessSendZonepoints(const EQ::Net::Packet &p)
{
	// Packet format: opcode(2) + count(4) + count * ZonePoint_Entry(24)
	// Minimum size: 2 + 4 = 6 bytes
	if (p.Length() < 6) {
		return;
	}

	uint32_t count = p.GetUInt32(2);

	LOG_DEBUG(MOD_ZONE, "Received zone points: {} entries", count);

	// Parse zone points and store them in ZoneLines
	if (m_zone_lines && count > 0) {
		std::vector<EQT::ZonePoint> zonePoints;
		zonePoints.reserve(count);

		// Each ZonePoint_Entry is 24 bytes, starting at offset 6
		const size_t entrySize = 24;
		const size_t dataStart = 6;

		for (uint32_t i = 0; i < count; ++i) {
			size_t offset = dataStart + i * entrySize;
			if (offset + entrySize > p.Length()) {
				break;
			}

			EQT::ZonePoint zp;
			zp.number = p.GetUInt32(offset);       // iterator/zone point ID
			zp.targetY = p.GetFloat(offset + 4);   // y
			zp.targetX = p.GetFloat(offset + 8);   // x
			zp.targetZ = p.GetFloat(offset + 12);  // z
			zp.heading = p.GetFloat(offset + 16);  // heading
			zp.targetZoneId = p.GetUInt16(offset + 20);  // zoneid
			// zoneinstance at offset + 22 is ignored for now

			zonePoints.push_back(zp);

			if (s_debug_level >= 3) {
				LOG_TRACE(MOD_ZONE, "  Zone point {}: zone={} pos=({},{},{}) heading={}",
					zp.number, zp.targetZoneId, zp.targetX, zp.targetY, zp.targetZ, zp.heading);
			}
		}

		m_zone_lines->setServerZonePoints(zonePoints);

		// Publish ZoneLineBoundingBoxes event to bridge
		if (m_bridge) {
			eqt::state::ZoneLineBoundingBoxesData data;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::ZoneLineBoundingBoxes, std::move(data)));
		}

		LOG_DEBUG(MOD_ZONE, "Stored {} zone points in ZoneLines", zonePoints.size());
	}

	// After zone points, check if we need to send server filter
	// This is old logic - server filter is now sent after GuildMOTD
	// But keeping it here for compatibility if the flow is different
	if (m_zone_entry_sent && !m_client_ready_sent) {
		// This path shouldn't normally be taken anymore
		if (!m_server_filter_sent && m_send_exp_zonein_received) {
			LOG_DEBUG(MOD_ZONE, "ZoneProcessSendZonepoints calling ZoneSendSetServerFilter (fallback)");
			ZoneSendSetServerFilter();
		}

		// Send client ready - This shouldn't happen here anymore
		if (!m_client_ready_sent && m_server_filter_sent) {
			LOG_DEBUG(MOD_ZONE, "ZoneProcessSendZonepoints calling ZoneSendClientReady");
			ZoneSendClientReady();
		}
	}
}

void EverQuest::ZoneProcessSpawnAppearance(const EQ::Net::Packet &p)
{
	// SpawnAppearance_Struct: spawn_id(2), type(2), parameter(4) = 8 bytes + 2 opcode = 10 bytes
	if (p.Length() < 10) {
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);
	uint16_t type = p.GetUInt16(4);
	uint32_t parameter = p.GetUInt32(6);

	LogTargetEntity(spawn_id, "SpawnAppearance spawn_id={}, type={}, parameter={}",
		spawn_id, type, parameter);

	switch (type) {
	case AT_ANIMATION:
		// Animation update - set the entity's animation
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.animation = static_cast<uint8_t>(parameter);
			}

			// Publish animation events via bridge (D17b: bridge is sole path to renderer)
			if (m_bridge) {
				// Map animation parameter to EQ model animation code
				std::string animCode;
				bool loop = true;
				bool playThrough = false;

				// Zone server animation values (100+) are pose/state animations
				// Lower values are action animations
				uint8_t poseState = static_cast<uint8_t>(
					EQT::Graphics::IrrlichtRenderer::EntityPoseState::Standing);
				bool setPose = false;

				switch (parameter) {
				case ANIM_STANDING:
				case ANIM_STAND:
					animCode = "o01";  // Idle/standing
					poseState = static_cast<uint8_t>(EQT::Graphics::IrrlichtRenderer::EntityPoseState::Standing);
					setPose = true;
					break;
				case ANIM_SITTING:
					animCode = "p02";  // Sitting idle pose
					loop = false;      // Don't loop (holds on last frame automatically)
					poseState = static_cast<uint8_t>(EQT::Graphics::IrrlichtRenderer::EntityPoseState::Sitting);
					setPose = true;
					break;
				case ANIM_CROUCHING:
					animCode = "l08";  // Crouching
					loop = false;      // Don't loop (holds on last frame automatically)
					poseState = static_cast<uint8_t>(EQT::Graphics::IrrlichtRenderer::EntityPoseState::Crouching);
					setPose = true;
					break;
				case ANIM_LYING:
					animCode = "d05";  // Lying down (death pose)
					loop = false;      // Don't loop (holds on last frame automatically)
					poseState = static_cast<uint8_t>(EQT::Graphics::IrrlichtRenderer::EntityPoseState::Lying);
					setPose = true;
					break;
				case ANIM_FREEZE:
					animCode = "o01";  // Freeze in place (use idle)
					break;
				case ANIM_LOOT:
					animCode = "t07";  // Looting animation
					loop = false;
					playThrough = true;
					break;
				default:
					// For other animations, don't change current animation
					// (movement animations come through ClientUpdate)
					break;
				}

				if (!animCode.empty()) {
					// Set pose state BEFORE animation to prevent updateEntity from overriding
					if (setPose) {
						eqt::state::EntityPoseStateChangedData data;
						data.spawnId = spawn_id;
						data.poseState = poseState;
						m_bridge->pushEvent(eqt::state::GameEvent(
							eqt::state::GameEventType::EntityPoseStateChanged, std::move(data)));
					}
					eqt::state::EntityAnimationEventData data;
					data.spawnId = spawn_id;
					data.animCode = 0;
					data.animName = animCode;
					data.loop = loop;
					data.playThrough = playThrough;
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::EntityAnimationEvent, std::move(data)));
					LogTargetEntity(spawn_id, "SpawnAppearance set animation '{}' pose={} on spawn_id={}",
						animCode, static_cast<int>(poseState), spawn_id);
				}
			}
		}
		break;

	case AT_DIE:
		// Entity died - trigger death animation (D17b: bridge-only)
		if (m_bridge) {
			eqt::state::EntityDeathAnimationData data;
			data.spawnId = spawn_id;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityDeathAnimation, std::move(data)));
		}
		break;

	case AT_HP_UPDATE:
		// HP update
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.hp_percent = static_cast<uint8_t>(parameter);
			}
		}
		break;

	case AT_SPAWN_ID:
		// Server is assigning us a new spawn ID (happens after zoning)
		// spawn_id=0 means "self", parameter contains the new spawn ID
		if (spawn_id == 0 && parameter != 0) {
			uint16_t newSpawnId = static_cast<uint16_t>(parameter);
			LOG_INFO(MOD_ZONE, "Server assigned new spawn ID: {} (was {})", newSpawnId, m_my_spawn_id);
			m_my_spawn_id = newSpawnId;
			m_game_state.player().setSpawnId(m_my_spawn_id);
			if (m_trade_manager) {
				m_trade_manager->setMySpawnId(m_my_spawn_id);
			}
		}
		break;

	case AT_INVISIBLE:
		// Invisibility state change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.is_invisible = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} invisibility set to {}", spawn_id, it->second.is_invisible);
			}
			// TODO: Graphics rendering could hide/fade invisible entities
		}
		break;

	case AT_SNEAK:
		// Sneaking state change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.is_sneaking = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} sneaking set to {}", spawn_id, it->second.is_sneaking);
			}
		}
		break;

	case AT_LINKDEAD:
		// Linkdead state change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.is_linkdead = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} linkdead set to {}", spawn_id, it->second.is_linkdead);
			}
		}
		break;

	case AT_FLYMODE:
		// Levitate/fly mode change
		// 0=ground, 1=flying, 2=levitate, 3=water, 4=floating, 5=levitate+run
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.flymode = static_cast<uint8_t>(parameter);
				LOG_DEBUG(MOD_ENTITY, "Entity {} flymode set to {}", spawn_id, parameter);
			}
			// TODO: Graphics rendering could show levitation animation
		}
		break;

	case AT_AFK:
		// AFK state change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.is_afk = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} AFK set to {}", spawn_id, it->second.is_afk);
			}
		}
		break;

	case AT_ANONYMOUS:
		// Anonymous/roleplay status change: 0=normal, 1=anon, 2=roleplay
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.anon_status = static_cast<uint8_t>(parameter);
				LOG_DEBUG(MOD_ENTITY, "Entity {} anon_status set to {}", spawn_id, parameter);
			}
		}
		break;

	case AT_PET:
		// Pet owner ID update
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.pet_owner_id = parameter;
				it->second.is_pet = (parameter != 0) ? 1 : 0;
				LOG_DEBUG(MOD_ENTITY, "Entity {} pet_owner_id set to {}", spawn_id, parameter);
			}
		}
		break;

	case AT_SIZE:
		// Size change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				// Size is stored as a float, parameter contains size value
				it->second.size = static_cast<float>(parameter) / 10.0f;  // EQ uses size*10
				LOG_DEBUG(MOD_ENTITY, "Entity {} size set to {}", spawn_id, it->second.size);
			}
			// TODO: Graphics rendering could update entity scale
		}
		break;

	case AT_LIGHT:
		// Light source change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.light = static_cast<uint8_t>(parameter);
				LOG_DEBUG(MOD_ENTITY, "Entity {} light set to {}", spawn_id, parameter);
				if (m_bridge) {
					eqt::state::EntityLightChangedData data;
					data.spawnId = spawn_id;
					data.lightLevel = static_cast<uint8_t>(parameter);
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::EntityLightChanged, std::move(data)));
				}
			}
		}
		break;

	default:
		// Unhandled appearance types - log for debugging
		LOG_DEBUG(MOD_ENTITY, "Unhandled SpawnAppearance type {} for spawn_id={}, parameter={}",
			type, spawn_id, parameter);
		break;
	}
}

void EverQuest::ZoneProcessEmote(const EQ::Net::Packet &p)
{
	// Animation/Emote packet format:
	// Offset 0-1: Opcode
	// Offset 2-3: spawn_id (uint16)
	// Offset 4: animation_speed (uint8)
	// Offset 5: animation_id (uint8)
	if (p.Length() < 6) {
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);
	uint8_t anim_speed = p.GetUInt8(4);
	uint8_t anim_id = p.GetUInt8(5);

	if (s_debug_level >= 2 || IsTrackedTarget(spawn_id)) {
		LOG_DEBUG(MOD_ENTITY, "[EMOTE] spawn_id={}, speed={}, anim_id={}", spawn_id, anim_speed, anim_id);
	}

	// Update entity animation state
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		it->second.animation = anim_id;
	}

	// Publish animation events via bridge (D17b: bridge is sole path to renderer)
	if (m_bridge) {
		// Map animation ID to EQ model animation code
		std::string animCode;
		bool loop = false;
		bool playThrough = true;  // Most emotes/attacks should play through

		// Get entity's weapon skill types for weapon-based attack animations
		// Read from game state (entity struct) instead of querying the renderer (D17a)
		uint8_t primaryWeaponSkill = 255;
		uint8_t secondaryWeaponSkill = 255;
		auto weaponIt = m_entities.find(spawn_id);
		if (weaponIt != m_entities.end()) {
			primaryWeaponSkill = weaponIt->second.primary_weapon_skill;
			secondaryWeaponSkill = weaponIt->second.secondary_weapon_skill;
		}

		// Map common animation IDs to EQ model animation codes
		// Combat animations (these are the key ones for NPC combat)
		switch (anim_id) {
		// Combat attack animations - use weapon-based animation
		case 1:  // Primary hand attack
		case 5:  // Attack
		case 6:  // Attack2
			// Use weapon skill type to determine correct animation
			animCode = EQ::getWeaponAttackAnimation(primaryWeaponSkill, false, false);
			break;
		case 2:  // Secondary hand attack
			// Use secondary weapon skill type
			animCode = EQ::getWeaponAttackAnimation(secondaryWeaponSkill, true, false);
			break;

		// Combat skill animations (use correct animation codes from reference)
		// For the PLAYER, queue these into the combat buffer so they're
		// processed with proper priority alongside weapon attacks
		case 10: // Round kick
			if (spawn_id == m_my_spawn_id) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_ROUND_KICK}));
				LOG_DEBUG(MOD_COMBAT, "Queued round kick animation for player");
				break;
			}
			animCode = EQ::ANIM_ROUND_KICK;  // c11
			break;
		case 11: // ANIM_KICK
			if (spawn_id == m_my_spawn_id) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_KICK}));
				LOG_DEBUG(MOD_COMBAT, "Queued kick animation for player");
				break;
			}
			animCode = EQ::ANIM_KICK;  // c01 - basic kick
			break;
		case 12: // ANIM_BASH
			if (spawn_id == m_my_spawn_id) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_BASH}));
				LOG_DEBUG(MOD_COMBAT, "Queued bash animation for player");
				break;
			}
			animCode = EQ::ANIM_BASH;  // c07 - Shield bash
			break;
		case 13: // Tiger Claw / Eagle Strike (rapid strikes)
			if (spawn_id == m_my_spawn_id) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_TIGER_STRIKE}));
				LOG_DEBUG(MOD_COMBAT, "Queued tiger claw/eagle strike animation for player");
				break;
			}
			animCode = EQ::ANIM_TIGER_STRIKE;  // t08
			break;
		case 14: // Flying kick
			if (spawn_id == m_my_spawn_id) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_FLYING_KICK}));
				LOG_DEBUG(MOD_COMBAT, "Queued flying kick animation for player");
				break;
			}
			animCode = EQ::ANIM_FLYING_KICK;  // t07
			break;
		case 15: // Dragon Punch
			if (spawn_id == m_my_spawn_id) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_DRAGON_PUNCH}));
				LOG_DEBUG(MOD_COMBAT, "Queued dragon punch animation for player");
				break;
			}
			animCode = EQ::ANIM_DRAGON_PUNCH;  // t09
			break;

		// Damage/hit reaction
		case 3:  // Damage from front
		case 4:  // Damage from back
			// Server sends damage emote when entity takes melee damage
			// For the PLAYER, we need to integrate this with the combat buffer system
			// so that attack animations take priority over damage reactions when the
			// player is both attacking and being attacked simultaneously
			if (spawn_id == m_my_spawn_id) {
				// Queue received damage into player's combat buffer instead of playing immediately
				// This allows the buffer processing to decide attack vs damage priority
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::ReceivedDamageAnimation,
					eqt::state::ReceivedDamageAnimationData{spawn_id}));
				LOG_DEBUG(MOD_COMBAT, "Queued received damage animation for player (will process with combat buffer)");
				break;  // Don't play immediately - buffer will handle it
			}
			animCode = EQ::ANIM_DAMAGE_MINOR;  // d01
			break;

		// Death
		case 16: // ANIM_DEATH
			animCode = EQ::ANIM_DEATH;  // d05
			break;

		// Social/Emote animations (s01-s28)
		// EQ Titanium animation IDs mapped to pre-Luclin animation codes
		case 18: // ANIM_CHEER
			animCode = EQ::ANIM_EMOTE_CHEER;  // s01 - Cheer
			break;
		case 19: // ANIM_KNEEL
			animCode = EQ::ANIM_CROUCHING;  // l08 - Crouching
			loop = true;
			playThrough = false;
			break;
		case 20: // ANIM_JUMP
			animCode = EQ::ANIM_FALLING;  // l05 - Falling/jump
			break;
		case 21: // ANIM_CRY / ANIM_MOURN
			animCode = EQ::ANIM_EMOTE_MOURN;  // s02 - Mourn/Disappointed
			break;
		case 23: // ANIM_RUDE
			animCode = EQ::ANIM_EMOTE_RUDE;  // s04 - Rude
			break;
		case 24: // ANIM_YAWN
			animCode = EQ::ANIM_EMOTE_YAWN;  // s05 - Yawn
			break;
		case 26: // ANIM_NOD
			animCode = EQ::ANIM_EMOTE_NOD;  // s06 - Nod
			break;
		case 27: // ANIM_AMAZED
			animCode = EQ::ANIM_EMOTE_AMAZED;  // s07 - Amazed
			break;
		case 28: // ANIM_PLEAD
			animCode = EQ::ANIM_EMOTE_PLEAD;  // s08 - Plead
			break;
		case 29: // ANIM_WAVE
			animCode = EQ::ANIM_EMOTE_WAVE;  // s03 - Wave
			break;
		case 30: // ANIM_CLAP
			animCode = EQ::ANIM_EMOTE_CLAP;  // s09 - Clap
			break;
		case 31: // ANIM_DISTRESS
			animCode = EQ::ANIM_EMOTE_DISTRESS;  // s10 - Distress/Hungry
			break;
		case 32: // ANIM_BLUSH
			animCode = EQ::ANIM_EMOTE_BLUSH;  // s11 - Blush
			break;
		case 33: // ANIM_CHUCKLE
			animCode = EQ::ANIM_EMOTE_CHUCKLE;  // s12 - Chuckle
			break;
		case 34: // ANIM_COUGH / ANIM_BURP
			animCode = EQ::ANIM_EMOTE_BURP;  // s13 - Burp/Cough
			break;
		case 35: // ANIM_DUCK
			animCode = EQ::ANIM_EMOTE_DUCK;  // s14 - Duck
			break;
		case 36: // ANIM_PUZZLE / ANIM_LOOK_AROUND
			animCode = EQ::ANIM_EMOTE_PUZZLE;  // s15 - Look Around/Puzzle
			break;
		case 58: // ANIM_DANCE
			animCode = EQ::ANIM_EMOTE_DANCE;  // s16 - Dance
			loop = true;
			playThrough = false;
			break;
		case 59: // ANIM_BLINK
			animCode = EQ::ANIM_EMOTE_BLINK;  // s17 - Blink
			break;
		case 60: // ANIM_GLARE
			animCode = EQ::ANIM_EMOTE_GLARE;  // s18 - Glare
			break;
		case 61: // ANIM_DROOL
			animCode = EQ::ANIM_EMOTE_DROOL;  // s19 - Drool
			break;
		case 62: // ANIM_KNEEL_EMOTE
			animCode = EQ::ANIM_EMOTE_KNEEL;  // s20 - Kneel (emote)
			break;
		case 63: // ANIM_LAUGH
			animCode = EQ::ANIM_EMOTE_LAUGH;  // s21 - Laugh
			break;
		case 64: // ANIM_POINT
			animCode = EQ::ANIM_EMOTE_POINT;  // s22 - Point
			break;
		case 65: // ANIM_SHRUG / ANIM_PONDER
			animCode = EQ::ANIM_EMOTE_SHRUG;  // s23 - Ponder/Shrug
			break;
		case 66: // ANIM_READY
			animCode = EQ::ANIM_EMOTE_READY;  // s24 - Ready
			break;
		case 67: // ANIM_SALUTE
			animCode = EQ::ANIM_EMOTE_SALUTE;  // s25 - Salute
			break;
		case 68: // ANIM_SHIVER
			animCode = EQ::ANIM_EMOTE_SHIVER;  // s26 - Shiver
			break;
		case 69: // ANIM_TAP_FOOT
			animCode = EQ::ANIM_EMOTE_TAP_FOOT;  // s27 - Tap Foot
			break;
		case 70: // ANIM_BOW
			animCode = EQ::ANIM_EMOTE_BOW;  // s28 - Bow
			break;

		// Bard instrument animations
		case 43: // ANIM_STRINGED_INSTRUMENT
			animCode = EQ::ANIM_STRINGED_INST;  // t02 - Stringed Instrument
			loop = true;
			playThrough = false;
			break;
		case 44: // ANIM_WIND_INSTRUMENT
			animCode = EQ::ANIM_WIND_INST;  // t03 - Wind Instrument
			loop = true;
			playThrough = false;
			break;

		// Casting animations (when triggered via emote packet)
		case 42: // ANIM_CAST
			animCode = EQ::ANIM_CAST_PULLBACK;  // t04 - Cast Pull Back
			break;

		// Special animations
		case 105: // ANIM_LOOT
			animCode = EQ::ANIM_POSE_KNEEL;  // p05 - kneeling/looting
			break;

		default:
			// Unknown animation - use weapon-based attack for combat-range IDs
			if (anim_id >= 1 && anim_id <= 15) {
				animCode = EQ::getWeaponAttackAnimation(primaryWeaponSkill, false, false);
			}
			break;
		}

		if (!animCode.empty()) {
			eqt::state::EntityAnimationEventData data;
			data.spawnId = spawn_id;
			data.animCode = anim_id;
			data.animName = animCode;
			data.loop = loop;
			data.playThrough = playThrough;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityAnimationEvent, std::move(data)));
			if (s_debug_level >= 2 || IsTrackedTarget(spawn_id)) {
				LOG_DEBUG(MOD_ENTITY, "[EMOTE] Set animation '{}' on spawn_id={} (anim_id={}, weaponSkill={})",
					animCode, spawn_id, anim_id, primaryWeaponSkill);
			}
		}
	}
}

void EverQuest::ZoneProcessGroundSpawn(const EQ::Net::Packet &p)
{
	// Parse Object_Struct from packet (skip 2-byte opcode)
	// Note: Server may send shorter packets (e.g., 97 bytes instead of 102)
	// Minimum needed: up to y coordinate at offset 52 + 4 bytes = 56 bytes
	// We need object_type at offset 92 for tradeskill detection, so minimum 96 bytes of data
	constexpr size_t MIN_REQUIRED = 2 + 56;  // opcode + up to y coordinate
	constexpr size_t FULL_SIZE = 2 + sizeof(EQT::Object_Struct);  // 102 bytes

	if (p.Length() < MIN_REQUIRED) {
		LOG_WARN(MOD_ENTITY, "GroundSpawn packet too small: {} bytes (need at least {})",
			p.Length(), MIN_REQUIRED);
		return;
	}

	const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + 2;  // Skip opcode
	size_t data_len = p.Length() - 2;

	// Create WorldObject from parsed data
	// Read fields manually to handle variable-length packets
	eqt::WorldObject worldObj;

	// These fields are always present (offsets 0-55)
	worldObj.size = *reinterpret_cast<const float*>(data + 8);
	worldObj.solid_type = *reinterpret_cast<const uint16_t*>(data + 12);
	worldObj.drop_id = *reinterpret_cast<const uint32_t*>(data + 16);
	worldObj.zone_id = *reinterpret_cast<const uint16_t*>(data + 20);
	worldObj.zone_instance = *reinterpret_cast<const uint16_t*>(data + 22);
	worldObj.incline = *reinterpret_cast<const uint32_t*>(data + 24);
	worldObj.tilt_x = *reinterpret_cast<const float*>(data + 32);
	worldObj.tilt_y = *reinterpret_cast<const float*>(data + 36);
	worldObj.heading = *reinterpret_cast<const float*>(data + 40);
	worldObj.z = *reinterpret_cast<const float*>(data + 44);
	worldObj.x = *reinterpret_cast<const float*>(data + 48);
	worldObj.y = *reinterpret_cast<const float*>(data + 52);

	// Object name at offset 56, 32 bytes
	if (data_len >= 88) {
		worldObj.name = std::string(reinterpret_cast<const char*>(data + 56),
			strnlen(reinterpret_cast<const char*>(data + 56), 32));
	}

	// Object type at offset 92, 4 bytes (needed for tradeskill detection)
	if (data_len >= 96) {
		worldObj.object_type = *reinterpret_cast<const uint32_t*>(data + 92);
	} else {
		worldObj.object_type = 0;  // Default if not present
		LOG_TRACE(MOD_ENTITY, "GroundSpawn packet short ({} bytes), defaulting object_type to 0", p.Length());
	}

	// Store the world object
	m_world_objects[worldObj.drop_id] = worldObj;

	// Publish WorldObjectSpawned event to bridge
	if (m_bridge) {
		eqt::state::WorldObjectSpawnedData wdata;
		wdata.dropId = worldObj.drop_id;
		wdata.x = worldObj.x;
		wdata.y = worldObj.y;
		wdata.z = worldObj.z;
		wdata.heading = worldObj.heading;
		wdata.modelName = worldObj.name;
		wdata.objectType = static_cast<uint8_t>(worldObj.object_type);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::WorldObjectSpawned, std::move(wdata)));
	}

	if (worldObj.isTradeskillContainer()) {
		LOG_DEBUG(MOD_ENTITY, "Tradeskill object spawned: id={} name='{}' type={} ({}) at ({:.1f}, {:.1f}, {:.1f})",
			worldObj.drop_id, worldObj.name, worldObj.object_type,
			worldObj.getTradeskillName(), worldObj.x, worldObj.y, worldObj.z);
	} else {
		LOG_TRACE(MOD_ENTITY, "Ground object spawned: id={} name='{}' type={} at ({:.1f}, {:.1f}, {:.1f})",
			worldObj.drop_id, worldObj.name, worldObj.object_type,
			worldObj.x, worldObj.y, worldObj.z);
	}
}

void EverQuest::ZoneProcessWeather(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Weather update received");

	m_weather_received = true;

	// Parse weather packet (8 bytes)
	// type: 0=rain off, 1=snow off, 2=snow on (type 0 + intensity > 0 = rain on)
	// intensity: 1-10
	if (p.Length() >= 10) {  // 2 byte opcode + 8 byte struct
		uint8_t type = p.GetUInt8(2);
		uint8_t intensity = p.GetUInt8(6);

		LOG_INFO(MOD_ZONE, "Weather packet: type={}, intensity={}", type, intensity);

		// Publish WeatherChanged event to bridge
		if (m_bridge) {
			eqt::state::WeatherChangedData data;
			data.type = type;
			data.intensity = intensity;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::WeatherChanged, std::move(data)));
		}
	}

	// Weather is the last packet in Zone Entry phase
	// Now we can send ReqNewZone
	if (!m_req_new_zone_sent) {
		ZoneSendReqNewZone();
	}
}

void EverQuest::ZoneProcessNewSpawn(const EQ::Net::Packet &p)
{
	// NewSpawn packet is 387 bytes total: opcode (2 bytes) + Spawn_Struct (385 bytes)
	// The spawn_id is embedded within the Spawn_Struct, not separate
	
	if (p.Length() < 387) {
		LOG_WARN(MOD_ENTITY, "NewSpawn packet too small: {} bytes (expected 387)", p.Length());
		return;
	}
	
	Entity entity;
	
	// Spawn_Struct starts at offset 2 (after opcode)
	size_t offset = 2;
	
	// Use the same parsing logic as ZoneSpawns
	// spawnId is at offset 340 in the struct (uint32)
	entity.spawn_id = p.GetUInt32(offset + 340);
	
	// Name is at offset 7 in Spawn_Struct
	entity.name = p.GetCString(offset + 7);
	
	// Level at offset 151
	entity.level = p.GetUInt8(offset + 151);
	
	// Position data from the bitfield structure (same as ZoneSpawns):
	uint32_t field1 = p.GetUInt32(offset + 94);  // contains deltaHeading and x
	uint32_t field2 = p.GetUInt32(offset + 98);  // contains y and animation
	uint32_t field3 = p.GetUInt32(offset + 102); // contains z and deltaY
	uint32_t field4 = p.GetUInt32(offset + 106); // contains deltaX and heading
	
	// Extract x, y, z (19 bits each, signed) - positions are in 10ths
	// Note: Server x/y are swapped relative to zone geometry, so we swap them here
	int32_t x_raw = (field1 >> 10) & 0x7FFFF;
	if (x_raw & 0x40000) x_raw |= 0xFFF80000; // Sign extend
	float server_x = static_cast<float>(x_raw);

	int32_t y_raw = field2 & 0x7FFFF;
	if (y_raw & 0x40000) y_raw |= 0xFFF80000; // Sign extend
	float server_y = static_cast<float>(y_raw);

	int32_t z_raw = field3 & 0x7FFFF;
	if (z_raw & 0x40000) z_raw |= 0xFFF80000; // Sign extend
	entity.z = static_cast<float>(z_raw);

	// Swap x and y from server
	entity.x = server_y;
	entity.y = server_x;

	// Read npc_type early - needed to determine heading transformation
	// NPC type: 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
	entity.npc_type = p.GetUInt8(offset + 83);
	bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);

	// Extract heading (11 bits to match C->S format)
	// Server uses 512 EQ units = 360 degrees, so multiply by 360/2048
	uint32_t raw_heading = (field4 >> 13) & 0x7FF;
	float server_heading = static_cast<float>(raw_heading) * 360.0f / 2048.0f;
	// Use heading directly for all entities (NPCs and players)
	entity.heading = server_heading;
	if (entity.heading >= 360.0f) entity.heading -= 360.0f;

	// Debug logging for current player at level 1+
	if (s_debug_level >= 1 && entity.name == m_character) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C NewSpawn [SELF] spawn_id={} name='{}'", entity.spawn_id, entity.name);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C NewSpawn [SELF] raw_heading={} -> server_heading={:.2f}deg -> entity.heading={:.2f}deg (isNPC={})",
			raw_heading, server_heading, entity.heading, isNPC);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C NewSpawn [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> entity_pos=({:.2f},{:.2f},{:.2f})",
			server_x, server_y, static_cast<float>(z_raw), entity.x, entity.y, entity.z);
	}

	// race is at offset 284 (uint32)
	entity.race_id = p.GetUInt32(offset + 284);

	// class_ is at offset 331
	entity.class_id = p.GetUInt8(offset + 331);

	// gender is at offset 334
	entity.gender = p.GetUInt8(offset + 334);

	// guildID is at offset 238
	entity.guild_id = p.GetUInt32(offset + 238);

	// curHp is at offset 86 (hp percentage)
	entity.hp_percent = p.GetUInt8(offset + 86);
	
	// animation is in the bitfield at offset 98
	entity.animation = (field2 >> 19) & 0x3FF;

	// size is at offset 75 (float)
	entity.size = *reinterpret_cast<const float*>(static_cast<const char*>(p.Data()) + offset + 75);

	// Appearance data from Spawn_Struct
	entity.face = p.GetUInt8(offset + 6);           // Face/head variant
	// npc_type already read above for heading transformation
	entity.haircolor = p.GetUInt8(offset + 85);     // Hair color
	entity.showhelm = p.GetUInt8(offset + 139);     // Show helm
	entity.hairstyle = p.GetUInt8(offset + 145);    // Hair style
	entity.beardcolor = p.GetUInt8(offset + 146);   // Beard color
	entity.beard = p.GetUInt8(offset + 156);        // Beard style
	entity.helm = p.GetUInt8(offset + 275);         // Helm texture
	entity.light = p.GetUInt8(offset + 330);        // Light source
	entity.bodytype = p.GetUInt8(offset + 335);     // Body type
	entity.equip_chest2 = p.GetUInt8(offset + 339); // Body texture variant

	// Equipment textures (TextureProfile at offset 197, 9 slots * 4 bytes)
	for (int i = 0; i < 9; i++) {
		entity.equipment[i] = p.GetUInt32(offset + 197 + i * 4);
	}

	// Equipment tint colors (TintProfile at offset 348, 9 slots * 4 bytes)
	for (int i = 0; i < 9; i++) {
		entity.equipment_tint[i] = p.GetUInt32(offset + 348 + i * 4);
	}

	// Initialize movement fields
	entity.delta_x = 0;
	entity.delta_y = 0;
	entity.delta_z = 0;
	entity.delta_heading = 0;
	entity.last_update_time = std::time(nullptr);

	// Validate the spawn data before adding
	if (entity.spawn_id > 0 && entity.spawn_id < 100000 && !entity.name.empty()) {
		// Check if this is our own spawn by comparing name
		if (entity.name == m_character) {
			m_my_spawn_id = entity.spawn_id;
			// Phase 7.4: Also update PlayerState
			m_game_state.player().setSpawnId(m_my_spawn_id);
			if (m_trade_manager) {
				m_trade_manager->setMySpawnId(m_my_spawn_id);
			}
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Found our own spawn in NewSpawn! Name: {}, Spawn ID: {}, server pos=({:.2f}, {:.2f}, {:.2f}) size={:.2f}",
					entity.name, m_my_spawn_id, entity.x, entity.y, entity.z, entity.size);
			}
			// Update our position to match server spawn position
			m_x = entity.x;
			m_y = entity.y;
			m_size = entity.size;
			// Server Z is model center. Convert to feet position for internal use.
			// SendPositionUpdate will add m_size/2 to convert back to model center.
			m_z = entity.z - (entity.size / 2.0f);

			if (s_debug_level >= 1) {
				LOG_INFO(MOD_ZONE, "[ZONE-IN] Updated client pos from NewSpawn: ({:.2f},{:.2f},{:.2f}) (feet Z, server Z was {:.2f})",
					m_x, m_y, m_z, entity.z);
			}

			// Update bridge with corrected feet Z position
			if (m_bridge) {
				float heading512 = m_heading * 512.0f / 360.0f;
				eqt::state::PlayerMovedData pdata;
				pdata.x = m_x; pdata.y = m_y; pdata.z = m_z;
				pdata.heading = heading512;
				pdata.dx = 0; pdata.dy = 0; pdata.dz = 0;
				pdata.isMoving = false;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::PlayerMoved, std::move(pdata)));
			}
		}

		// Store pet info in entity (for deferred pet detection)
		// is_pet is at offset 329 in Spawn_Struct (uint8_t)
		// petOwnerId is at offset 189 in Spawn_Struct (uint32_t)
		// MUST be set BEFORE adding to m_entities
		entity.is_pet = p.GetUInt8(offset + 329);
		entity.pet_owner_id = p.GetUInt32(offset + 189);

		// Add to entity list (sync to both local map and EntityManager)
		m_entities[entity.spawn_id] = entity;
		SyncEntityToGameState(entity);

#ifdef EQT_HAS_GRAPHICS
		OnSpawnAddedGraphics(entity);
		// Notify bridge of player spawn ID (D17b: bridge is sole path to renderer)
		if (entity.name == m_character && m_bridge) {
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::PlayerSpawnIdSet,
				eqt::state::PlayerSpawnIdSetData{m_my_spawn_id}));
		}
#endif

		// Check if this spawn is our pet (if we already know our spawn ID)
		if (entity.is_pet != 0 && entity.pet_owner_id == m_my_spawn_id && m_my_spawn_id != 0) {
			m_pet_spawn_id = entity.spawn_id;

			// Initialize default button states (Follow=ON, others=OFF per server behavior)
			std::fill(std::begin(m_pet_button_states), std::end(m_pet_button_states), false);
			m_pet_button_states[EQT::PET_BUTTON_FOLLOW] = true;

			LOG_INFO(MOD_MAIN, "Pet detected: {} (ID: {}) Level {} owned by us (ID: {})",
				entity.name, entity.spawn_id, entity.level, m_my_spawn_id);

#ifdef EQT_HAS_GRAPHICS
			OnPetCreated(entity);
#endif
		}

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_ENTITY, "New spawn: {} (ID: {}) Level {} {} Race {} at ({:.2f}, {:.2f}, {:.2f})",
				entity.name, entity.spawn_id, entity.level,
				entity.class_id, entity.race_id, entity.x, entity.y, entity.z);
		}
	} else {
		LOG_WARN(MOD_ENTITY, "Invalid spawn data in NewSpawn: ID={}, Name='{}'",
			entity.spawn_id, entity.name);
	}
}


void EverQuest::ZoneProcessExpUpdate(const EQ::Net::Packet &p)
{
	// ExpUpdate_Struct: { uint32_t exp; uint32_t aaxp; }
	const size_t base = 2;  // Skip opcode

	if (p.Length() < base + 8) {
		LOG_WARN(MOD_ZONE, "ExpUpdate packet too small: {} bytes", p.Length());
		return;
	}

	uint32_t exp = p.GetUInt32(base + 0);
	uint32_t aaxp = p.GetUInt32(base + 4);

	LOG_DEBUG(MOD_ZONE, "Received exp update: exp={} aaxp={} (was exp={} aaxp={})",
	          exp, aaxp, m_exp, m_exp_aa);

	m_exp = exp;
	m_exp_aa = aaxp;

	// Publish ExpProgressChanged event
	if (m_bridge) {
		eqt::state::ExpProgressChangedData data;
		data.progress = CalculateExpProgress(m_exp, m_level);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ExpProgressChanged, std::move(data)));
	}
}

void EverQuest::ZoneProcessRaidUpdate(const EQ::Net::Packet &p)
{
	// Raid update packets contain information about raid membership changes
	// The format varies based on the action type in the packet
	// For now, log the packet details for debugging

	if (p.Length() < 6) {
		LOG_DEBUG(MOD_ZONE, "Received short raid update: {} bytes", p.Length());
		return;
	}

	uint32_t action = p.GetUInt32(2);
	LOG_DEBUG(MOD_ZONE, "Received raid update: action={}, {} bytes", action, p.Length());

	// Common raid actions:
	// 0 = raid formed/joined
	// 1 = member left
	// 2 = member joined
	// 3 = leader changed
	// 6 = raid disbanded

	switch (action) {
	case 0:
		AddChatSystemMessage("You have joined the raid.");
		break;
	case 1:
		// Member left - could parse name from packet
		LOG_DEBUG(MOD_ZONE, "Raid member left");
		break;
	case 2:
		// Member joined - could parse name from packet
		LOG_DEBUG(MOD_ZONE, "Raid member joined");
		break;
	case 3:
		AddChatSystemMessage("The raid leader has changed.");
		break;
	case 6:
		AddChatSystemMessage("The raid has disbanded.");
		break;
	case 10:
		// Raid lock state update - ignore silently
		LOG_TRACE(MOD_ZONE, "Raid lock state update");
		break;
	default:
		LOG_DEBUG(MOD_ZONE, "Unknown raid action: {}", action);
		break;
	}
}

void EverQuest::ZoneProcessGuildMOTD(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received guild MOTD");

	// GuildMOTD is the last packet from SendZoneInPackets()
	// Now we send SetServerFilter and ClientReady
	if (!m_server_filter_sent) {
		ZoneSendSetServerFilter();
		m_server_filter_sent = true;
	}

	if (!m_client_ready_sent) {
		SetLoadingPhase(LoadingPhase::ZONE_PLAYER_READY, "Finalizing connection...");
		ZoneSendClientReady();
		m_client_ready_sent = true;

		// Note: We don't start the update loop here - we wait for our first ClientUpdate
		// which confirms the server has our position. StartUpdateLoop() is called in
		// ZoneProcessClientUpdate() when we receive our own spawn's position update.
		// That's also where we trigger OnGameStateComplete() for graphics loading.
		LOG_DEBUG(MOD_ZONE, "Sent ClientReady, waiting for position confirmation...");
	}
}

void EverQuest::ZoneProcessClientUpdate(const EQ::Net::Packet &p)
{
	// ClientUpdate packets contain position updates from other players
	// Format based on PlayerPositionUpdateServer_Struct (bit-packed):
	// spawn_id (2 bytes) + 20 bytes of bit-packed data

	if (p.Length() < 24) { // 2 (opcode) + 2 (spawn_id) + 20 (bit-packed data)
		LOG_WARN(MOD_ZONE, "ClientUpdate packet too small: {} bytes", p.Length());
		return;
	}

	// Read spawn_id (first 2 bytes after opcode)
	uint16_t spawn_id = p.GetUInt16(2);

	// Log if this is potentially our own ClientUpdate
	if (m_my_spawn_id != 0 && spawn_id == m_my_spawn_id) {
		LOG_INFO(MOD_ZONE, "*** RECEIVED OUR OWN ClientUpdate! spawn_id={} ***", spawn_id);
	}
	
	// Read the bit-packed data as 5 32-bit integers
	uint32_t field1 = p.GetUInt32(4);  // delta_heading:10, x_pos:19, padding:3
	uint32_t field2 = p.GetUInt32(8);  // y_pos:19, animation:10, padding:3
	uint32_t field3 = p.GetUInt32(12); // z_pos:19, delta_y:13
	uint32_t field4 = p.GetUInt32(16); // delta_x:13, heading:12, padding:7
	uint32_t field5 = p.GetUInt32(20); // delta_z:13, padding:19
	
	// Extract values from bit fields
	int32_t delta_heading = (field1 & 0x3FF);          // bits 0-9 (10 bits)
	int32_t x_pos_raw = (field1 >> 10) & 0x7FFFF;      // bits 10-28 (19 bits)
	
	int32_t y_pos_raw = (field2 & 0x7FFFF);            // bits 0-18 (19 bits)
	int32_t animation = (field2 >> 19) & 0x3FF;        // bits 19-28 (10 bits, signed)
	
	int32_t z_pos_raw = (field3 & 0x7FFFF);            // bits 0-18 (19 bits)
	int32_t delta_y = (field3 >> 19) & 0x1FFF;         // bits 19-31 (13 bits)
	
	int32_t delta_x = (field4 & 0x1FFF);               // bits 0-12 (13 bits)
	uint32_t heading = (field4 >> 13) & 0xFFF;         // bits 13-24 (12 bits)
	
	int32_t delta_z = (field5 & 0x1FFF);               // bits 0-12 (13 bits)
	
	// Sign extend delta values (they are signed)
	if (delta_heading & 0x200) delta_heading |= 0xFFFFFC00; // sign extend from 10 bits
	if (delta_x & 0x1000) delta_x |= 0xFFFFE000;           // sign extend from 13 bits
	if (delta_y & 0x1000) delta_y |= 0xFFFFE000;           // sign extend from 13 bits
	if (delta_z & 0x1000) delta_z |= 0xFFFFE000;           // sign extend from 13 bits

	// Sign extend animation (negative = play animation in reverse)
	// abs(animation) = animation ID, sign = playback direction
	if (animation & 0x200) animation |= 0xFFFFFC00;        // sign extend from 10 bits
	
	// Sign extend position values (they are signed 19-bit integers)
	if (x_pos_raw & 0x40000) x_pos_raw |= 0xFFF80000;      // sign extend from 19 bits
	if (y_pos_raw & 0x40000) y_pos_raw |= 0xFFF80000;      // sign extend from 19 bits
	if (z_pos_raw & 0x40000) z_pos_raw |= 0xFFF80000;      // sign extend from 19 bits
	
	// Convert raw position values to floats (divided by 8 for fixed-point)
	// Note: Server x/y are swapped relative to zone geometry
	float server_x = static_cast<float>(x_pos_raw) / 8.0f;
	float server_y = static_cast<float>(y_pos_raw) / 8.0f;
	float x = server_y;  // Swap
	float y = server_x;  // Swap
	float z = static_cast<float>(z_pos_raw) / 8.0f;
	// Delta values must also be swapped to match position coordinate system
	float server_dx = static_cast<float>(delta_x) / 8.0f;
	float server_dy = static_cast<float>(delta_y) / 8.0f;
	float dx = server_dy;  // Swap to match position swap
	float dy = server_dx;  // Swap to match position swap
	float dz = static_cast<float>(delta_z) / 8.0f;
	float dh = static_cast<float>(delta_heading);
	// Convert heading to degrees: 512 EQ units = 360 degrees
	uint32_t raw_heading = heading & 0x7FF;  // Lower 11 bits
	float server_h = static_cast<float>(raw_heading) * 360.0f / 2048.0f;
	// Use heading directly for all entities (no mirroring)
	float h_player = server_h;
	if (h_player >= 360.0f) h_player -= 360.0f;
	float h_npc = server_h;
	if (h_npc >= 360.0f) h_npc -= 360.0f;

	// Debug: log position updates
	// Level 1: current player, current target, or tracked targets
	// Level 2+: all entities
	uint16_t currentTargetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
	bool isCurrentPlayer = (spawn_id == m_my_spawn_id);
	bool shouldLogPosition = (s_debug_level >= 2) ||
	                         (s_debug_level >= 1 && (isCurrentPlayer || spawn_id == currentTargetId || IsTrackedTarget(spawn_id)));
	if (shouldLogPosition) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C spawn_id={} pos=({:.2f}, {:.2f}, {:.2f}) heading={:.1f} anim={} delta=({:.2f}, {:.2f}, {:.2f}) (my_id={})",
			spawn_id, x, y, z, server_h, animation, dx, dy, dz, m_my_spawn_id);
	}
	// Additional detailed logging for current player at debug level 1+
	if (s_debug_level >= 1 && isCurrentPlayer) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C [SELF] raw_heading={} (12-bit field={}) -> server_h={:.2f}deg -> h_player={:.2f}deg",
			raw_heading, heading, server_h, h_player);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> client_pos=({:.2f},{:.2f},{:.2f})",
			server_x, server_y, z, x, y, z);
	}

	// Check if this is an update for our own character
	if (spawn_id == m_my_spawn_id) {
		// Update our position to match what the server says
		m_x = x;
		m_y = y;
		m_z = z;
		// S->C transformation for our player: inverse of C->S (server_heading = 90 - m_heading)
		m_heading = 90.0f - server_h;
		if (m_heading < 0.0f) m_heading += 360.0f;
		if (m_heading >= 360.0f) m_heading -= 360.0f;

		// Track whether we need to create player entity on renderer
		bool needPlayerEntityCreation = false;

		// Also update our spawn ID if not set
		if (m_my_spawn_id == 0) {
			m_my_spawn_id = spawn_id;
			// Phase 7.4: Also update PlayerState
			m_game_state.player().setSpawnId(m_my_spawn_id);
			if (m_trade_manager) {
				m_trade_manager->setMySpawnId(m_my_spawn_id);
			}
			LOG_INFO(MOD_MOVEMENT, "Set our spawn ID to {} from ClientUpdate", m_my_spawn_id);

			// Deferred pet detection: now that we know our spawn ID, check if any
			// existing entity is our pet (spawns arrive before we know our ID)
			if (m_pet_spawn_id == 0) {
				for (const auto& pair : m_entities) {
					const Entity& ent = pair.second;
					if (ent.is_pet != 0 && ent.pet_owner_id == m_my_spawn_id) {
						m_pet_spawn_id = ent.spawn_id;
						std::fill(std::begin(m_pet_button_states), std::end(m_pet_button_states), false);
						m_pet_button_states[EQT::PET_BUTTON_FOLLOW] = true;
						LOG_INFO(MOD_MAIN, "Pet detected (deferred): {} (ID: {}) Level {} owned by us (ID: {})",
							ent.name, ent.spawn_id, ent.level, m_my_spawn_id);
#ifdef EQT_HAS_GRAPHICS
						OnPetCreated(ent);
#endif
						break;
					}
				}
			}

			needPlayerEntityCreation = true;
		}

		// Also create player entity if it's pending (after zoning, entities were cleared)
		if (m_player_graphics_entity_pending && m_my_spawn_id != 0) {
			needPlayerEntityCreation = true;
		}

		// Now create the player entity via bridge (D17b: bridge is sole path to renderer)
		// Deferred from OnZoneLoadedGraphics to ensure all player data has been received
		if (needPlayerEntityCreation && m_bridge) {
			auto playerIt = m_entities.find(m_my_spawn_id);
			if (playerIt != m_entities.end()) {
				const Entity& entity = playerIt->second;

				LOG_INFO(MOD_ENTITY, "Creating player entity {} ({}) from ClientUpdate - equipment: primary={} secondary={}",
				         m_my_spawn_id, entity.name, entity.equipment[7], entity.equipment[8]);

				// Publish EntitySpawned for player entity
				eqt::state::EntitySpawnedData data;
				data.spawnId = m_my_spawn_id;
				data.name = entity.name;
				data.x = x;
				data.y = y;
				data.z = z;
				data.heading = h_player;
				data.raceId = entity.race_id;
				data.classId = entity.class_id;
				data.level = entity.level;
				data.gender = entity.gender;
				data.npcType = entity.npc_type;
				data.isCorpse = false;
				data.isPlayer = true;
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

				// Publish PlayerSpawnIdSet so renderer marks the player node
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::PlayerSpawnIdSet,
					eqt::state::PlayerSpawnIdSetData{m_my_spawn_id}));

				// Set entity light source if equipped
				if (entity.light > 0) {
					eqt::state::EntityLightChangedData lightData;
					lightData.spawnId = m_my_spawn_id;
					lightData.lightLevel = entity.light;
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::EntityLightChanged, std::move(lightData)));
				}

				// Player entity has been created, clear the pending flag
				m_player_graphics_entity_pending = false;
			} else {
				LOG_WARN(MOD_ENTITY, "Player entity {} not found in m_entities when setting spawn ID from ClientUpdate", m_my_spawn_id);
			}
		}

		// Start the update loop when we first receive our ClientUpdate
		// This indicates we're fully in the game world
		if (!m_update_running) {
			SetLoadingPhase(LoadingPhase::ZONE_AWAITING_CONFIRM, "Player confirmed...");
			LOG_INFO(MOD_ZONE, "Zone connection complete! Player position confirmed (spawn_id={}).", m_my_spawn_id);
			m_game_state.world().setZoneConnected(true);

			// Signal renderer that network phase is complete and entities are ready (D17b: bridge-only)
			if (m_bridge) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::NetworkReady,
					eqt::state::NetworkReadyData{static_cast<uint32_t>(m_entities.size()), true}));
				LOG_DEBUG(MOD_GRAPHICS, "Expected entity count: {}, network ready: true", m_entities.size());
			}

			StartUpdateLoop();
			// Game state is now complete - trigger graphics loading if graphics mode
			OnGameStateComplete();
		}
		
		// Update our entity in the entity list
		auto it = m_entities.find(m_my_spawn_id);
		if (it != m_entities.end()) {
			it->second.x = x;
			it->second.y = y;
			it->second.z = z;
			it->second.heading = h_player;
			it->second.animation = animation;
			it->second.delta_x = dx;
			it->second.delta_y = dy;
			it->second.delta_z = dz;
			it->second.delta_heading = dh;
			it->second.last_update_time = std::time(nullptr);

#ifdef EQT_HAS_GRAPHICS
			OnSpawnMovedGraphics(m_my_spawn_id, x, y, z, h_player, dx, dy, dz, animation);
#endif
		}
		return;
	}

	// Update entity position in our list
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		// Use heading directly for all entity types
		bool isNPC = (it->second.npc_type == 1 || it->second.npc_type == 3);
		float entity_heading = isNPC ? h_npc : h_player;

		it->second.x = x;
		it->second.y = y;
		it->second.z = z;
		it->second.heading = entity_heading;
		it->second.animation = animation;
		it->second.delta_x = dx;
		it->second.delta_y = dy;
		it->second.delta_z = dz;
		it->second.delta_heading = dh;
		it->second.last_update_time = std::time(nullptr);

#ifdef EQT_HAS_GRAPHICS
		OnSpawnMovedGraphics(spawn_id, x, y, z, entity_heading, dx, dy, dz, animation);
#endif

	}
}

void EverQuest::ZoneProcessDeleteSpawn(const EQ::Net::Packet &p)
{
	// DeleteSpawn is sent when an entity despawns
	// Format: spawn_id (2 bytes)
	
	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "DeleteSpawn packet too small: {} bytes", p.Length());
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);

	// Special case: spawn_id 0 might mean "delete current target"
	if (spawn_id == 0 && m_combat_manager && m_combat_manager->HasTarget()) {
		spawn_id = m_combat_manager->GetTargetId();
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "DeleteSpawn with ID 0 interpreted as current target: {}", spawn_id);
		}
	}
	
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		// Handle corpse deletion - only delete corpses we've finished looting
		if (it->second.is_corpse) {
			// If we're actively looting this corpse, close the loot window first
			if (m_player_looting_corpse_id == spawn_id) {
				LOG_DEBUG(MOD_ENTITY, "Corpse {} ({}) being deleted while looting, closing loot window", spawn_id, it->second.name);
				// Publish LootWindowClosed (D18a: bridge is sole path to renderer)
				if (m_bridge) {
					eqt::state::LootWindowClosedData lwdata;
					lwdata.corpseId = spawn_id;
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::LootWindowClosed, std::move(lwdata)));
				}
				m_loot_items.clear();
				m_player_looting_corpse_id = 0;
				m_loot_all_in_progress = false;
				m_loot_all_remaining_slots.clear();
				m_game_state.player().clearLootingCorpse();  // Phase 7.7
				// Mark as ready for deletion
				m_loot_complete_corpse_id = spawn_id;
			}

			// Only delete corpses we've completed looting
			// This prevents corpses from being deleted when NPCs die (server sends DeleteSpawn(0))
			if (m_loot_complete_corpse_id != spawn_id) {
				LOG_TRACE(MOD_ENTITY, "Ignoring DeleteSpawn for corpse {} ({}) - not finished looting", spawn_id, it->second.name);
				return;
			}

			LOG_DEBUG(MOD_ENTITY, "Corpse {} ({}) removed by server after looting", spawn_id, it->second.name);
			m_loot_complete_corpse_id = 0;  // Clear the flag
		}

		LOG_DEBUG(MOD_ENTITY, "Entity {} ({}) despawned", spawn_id, it->second.name);

		// Cancel trade if partner despawned
		if (m_trade_manager && m_trade_manager->isTrading() &&
			m_trade_manager->getPartnerSpawnId() == spawn_id) {
			LOG_DEBUG(MOD_MAIN, "Trade partner despawned, canceling trade");
			m_trade_manager->cancelTrade();
			AddChatSystemMessage("Trade cancelled - partner is no longer available");
		}

		// Check if our pet despawned
		if (spawn_id == m_pet_spawn_id) {
			LOG_INFO(MOD_MAIN, "Pet {} ({}) despawned", spawn_id, it->second.name);
			m_pet_spawn_id = 0;
			std::fill(std::begin(m_pet_button_states), std::end(m_pet_button_states), false);
			// Clear pet buffs
			if (m_buff_manager) {
				m_buff_manager->clearPetBuffs();
			}
#ifdef EQT_HAS_GRAPHICS
			OnPetRemoved();
#endif
		}

		// Check if our combat target despawned - disable auto-attack
		if (m_combat_manager && m_combat_manager->HasTarget() &&
			m_combat_manager->GetTargetId() == spawn_id) {
			LOG_DEBUG(MOD_COMBAT, "Combat target {} ({}) despawned, disabling auto-attack", spawn_id, it->second.name);
			m_combat_manager->DisableAutoAttack();
			m_combat_manager->ClearTarget();
		}

#ifdef EQT_HAS_GRAPHICS
		OnSpawnRemovedGraphics(spawn_id);
#endif

		// Remove from both local map and EntityManager
		m_entities.erase(it);
		RemoveEntityFromGameState(spawn_id);
	} else {
		LOG_DEBUG(MOD_ENTITY, "DeleteSpawn for unknown spawn_id: {}", spawn_id);
	}
}

void EverQuest::ZoneProcessMobHealth(const EQ::Net::Packet &p)
{
	// MobHealth updates an entity's health percentage
	// Format: spawn_id (2 bytes), hp_percent (1 byte)
	
	if (p.Length() < 5) {
		LOG_WARN(MOD_ENTITY, "MobHealth packet too small: {} bytes", p.Length());
		return;
	}
	
	uint16_t spawn_id = p.GetUInt16(2);
	uint8_t hp_percent = p.GetUInt8(4);
	
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		uint8_t old_hp = it->second.hp_percent;
		it->second.hp_percent = hp_percent;
		if (s_debug_level >= 2 || IsTrackedTarget(spawn_id)) {
			LOG_DEBUG(MOD_ENTITY, "[HP] Entity {} ({}) health: {}% -> {}%", spawn_id, it->second.name, old_hp, hp_percent);
		}
		// Update target HP via bridge if this is our current target (D17b)
		// Note: EntityStatsChanged event published below covers this
		// Publish EntityStatsChanged event
		if (m_bridge) {
			eqt::state::EntityStatsChangedData data;
			data.spawnId = spawn_id;
			data.hpPercent = hp_percent;
			data.curMana = 0;
			data.maxMana = 0;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityStatsChanged, std::move(data)));
			// If this entity is the current target, push TargetChanged with updated HP
			if (m_current_target_id == spawn_id) {
				eqt::state::TargetChangedData tdata;
				tdata.spawnId = spawn_id;
				tdata.hpPercent = hp_percent;
				tdata.name = it->second.name;
				tdata.level = it->second.level;
				tdata.raceId = it->second.race_id;
				tdata.gender = it->second.gender;
				tdata.classId = it->second.class_id;
				tdata.bodyType = it->second.bodytype;
				tdata.npcType = it->second.npc_type;
				tdata.helm = it->second.helm;
				tdata.showHelm = it->second.showhelm;
				tdata.texture = it->second.equip_chest2;
				for (int i = 0; i < 9; ++i) {
					tdata.equipment[i] = it->second.equipment[i];
					tdata.equipmentTint[i] = it->second.equipment_tint[i];
				}
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::TargetChanged, std::move(tdata)));
			}
		}
	}
}

void EverQuest::ZoneProcessHPUpdate(const EQ::Net::Packet &p)
{
	// HPUpdate is sent for an entity's HP
	// SpawnHPUpdate_Struct format: cur_hp (4 bytes), max_hp (4 bytes), spawn_id (2 bytes)
	// Total: 10 bytes + 2 byte opcode = 12 bytes
	// Note: Mana is sent separately via HC_OP_ManaChange

	if (p.Length() < 12) {
		LOG_WARN(MOD_ENTITY, "HPUpdate packet too small: {} bytes", p.Length());
		return;
	}

	uint32_t cur_hp = p.GetUInt32(2);
	int32_t max_hp = p.GetInt32(6);
	uint16_t spawn_id = p.GetUInt16(10);

	// Check if this is our own HP update
	bool is_self = (spawn_id == m_my_spawn_id);

	if (is_self) {
		// Update player HP tracking (both local and GameState)
		m_cur_hp = cur_hp;
		m_max_hp = static_cast<uint32_t>(max_hp > 0 ? max_hp : 0);
		m_game_state.player().setHP(m_cur_hp, m_max_hp);
	}

	if (is_self) {
		LOG_DEBUG(MOD_ENTITY, "Player HP: {}/{} (spawn_id={})", cur_hp, max_hp, spawn_id);
	} else {
		LOG_TRACE(MOD_ENTITY, "Entity {} HP: {}/{}", spawn_id, cur_hp, max_hp);
	}

	// Update entity HP if we're tracking it
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		it->second.hp_percent = (max_hp > 0) ? static_cast<uint8_t>(cur_hp * 100 / max_hp) : 100;
	}

	// Update combat stats for our player
	if (is_self && m_combat_manager) {
		CombatStats stats;
		stats.current_hp = cur_hp;
		stats.max_hp = max_hp > 0 ? static_cast<uint32_t>(max_hp) : 0;
		// Don't update mana here - it comes from ManaChange packet
		stats.current_mana = m_mana;
		stats.max_mana = m_max_mana;
		stats.current_endurance = 0;
		stats.max_endurance = 0;
		stats.hp_percent = (max_hp > 0) ? (static_cast<float>(cur_hp) * 100.0f / static_cast<float>(max_hp)) : 100.0f;
		stats.mana_percent = (m_max_mana > 0) ? (static_cast<float>(m_mana) * 100.0f / static_cast<float>(m_max_mana)) : 100.0f;
		stats.endurance_percent = 100.0f;
		m_combat_manager->UpdateCombatStats(stats);
	}

#ifdef EQT_HAS_GRAPHICS
	// Update inventory window stats when player HP changes
	if (is_self) {
		UpdateInventoryStats();
	}
#endif

	// Publish PlayerStatsChanged when our own HP changes
	if (is_self && m_bridge) {
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
	}
}

void EverQuest::CheckZoneRequestPhaseComplete()
{
	// Check if we've received all expected responses from Zone Request phase
	// We need to have received:
	// 1. At least one AA table (m_aa_table_count > 0)
	// 2. RespondAA packet
	// 3. At least one tribute info (m_tribute_count > 0)
	// 4. At least one guild tribute (m_guild_tribute_count > 0)

	// For simplicity, we'll check if we've received at least one of each
	if (m_new_zone_received &&
	    m_aa_table_count > 0 &&
	    m_tribute_count > 0 &&
	    m_guild_tribute_count > 0 &&
	    !m_req_client_spawn_sent) {

		SetLoadingPhase(LoadingPhase::ZONE_REQUEST_PHASE);
		LOG_INFO(MOD_ZONE, "Zone Request phase complete, sending ReqClientSpawn");

		// Move to Player Spawn Request phase
		ZoneSendReqClientSpawn();
		m_req_client_spawn_sent = true;
	}
}



// UCS (Universal Chat Service) connection implementation
// TODO: Fix UCS implementation to use correct EQStream API
/*
void EverQuest::ConnectToUCS(const std::string& host, uint16_t port)
{
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Connecting to UCS at {}:{}", host, port) << std::endl;
	}
	
	// UCS uses EQStream with legacy options (1-byte opcodes)
	EQ::Net::EQStreamManagerInterfaceOptions ucs_opts(port, false, false);
	ucs_opts.opcode_size = 1;  // Legacy 1-byte opcodes
	ucs_opts.daybreak_options.stale_connection_ms = 600000;
	
	m_ucs_stream_manager = std::make_unique<EQ::Net::EQStreamManager>(ucs_opts);
	
	m_ucs_stream_manager->OnNewConnection([this](std::shared_ptr<EQ::Net::EQStream> stream) {
		UCSOnNewConnection(stream);
	});
	
	m_ucs_stream_manager->OnConnectionLost([this](std::shared_ptr<EQ::Net::EQStream> stream) {
		UCSOnConnectionLost();
	});
	
	// Connect to UCS server
	m_ucs_stream = m_ucs_stream_manager->CreateConnection(host, port);
	m_ucs_stream->SetStreamType(EQ::Net::EQStreamType::Chat);
}

void EverQuest::UCSOnNewConnection(std::shared_ptr<EQ::Net::EQStream> stream)
{
	if (s_debug_level >= 1) {
		std::cout << "UCS connection established" << std::endl;
	}
	
	m_ucs_stream = stream;
	m_ucs_connected = true;
	
	// Set packet callback
	m_ucs_stream->SetPacketCallback([this](const EQ::Net::Packet &p) {
		UCSOnPacketRecv(p);
	});
	
	// Send mail login packet to authenticate
	UCSSendMailLogin();
}

void EverQuest::UCSOnConnectionLost()
{
	if (s_debug_level >= 1) {
		std::cout << "UCS connection lost" << std::endl;
	}
	
	m_ucs_connected = false;
	m_ucs_stream.reset();
}

void EverQuest::UCSOnPacketRecv(const EQ::Net::Packet &p)
{
	if (p.Length() < 1) return;
	
	uint8_t opcode = p.GetUInt8(0);
	
	if (s_debug_level >= 2) {
		std::cout << fmt::format("UCS packet received: opcode={:#04x}, size={}", opcode, p.Length()) << std::endl;
	}
	
	switch (opcode) {
		case HC_OP_ChatMessage:
			UCSProcessChatMessage(p);
			break;
		case HC_OP_UCS_MailNew:
			UCSProcessMailNew(p);
			break;
		case HC_OP_UCS_Buddy:
			UCSProcessBuddy(p);
			break;
		default:
			if (s_debug_level >= 1) {
				std::cout << fmt::format("Unhandled UCS opcode: {:#04x}", opcode) << std::endl;
			}
			break;
	}
}

void EverQuest::UCSSendMailLogin()
{
	if (!m_ucs_stream || !m_ucs_connected) {
		return;
	}
	
	// MailLogin packet format: opcode(1) + mail_key(null-terminated string)
	size_t key_len = m_mail_key.length() + 1; // Include null terminator
	size_t packet_size = 1 + key_len;
	
	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);
	
	p.PutUInt8(0, HC_OP_UCS_MailLogin);
	memcpy((char*)p.Data() + 1, m_mail_key.c_str(), key_len);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Sending UCS MailLogin with key: {}", m_mail_key) << std::endl;
	}
	
	m_ucs_stream->QueuePacket(p);
}

void EverQuest::UCSSendTell(const std::string &to, const std::string &message)
{
	if (!m_ucs_stream || !m_ucs_connected) {
		if (s_debug_level >= 1) {
			std::cout << "UCS not connected, cannot send tell" << std::endl;
		}
		return;
	}
	
	// Tell format via UCS: opcode(1) + target(null-terminated) + sender(null-terminated) + message(null-terminated)
	size_t to_len = to.length() + 1;
	size_t from_len = m_character.length() + 1;
	size_t msg_len = message.length() + 1;
	size_t packet_size = 1 + to_len + from_len + msg_len;
	
	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);
	
	size_t offset = 0;
	p.PutUInt8(offset++, HC_OP_ChatMessage);
	
	memcpy((char*)p.Data() + offset, to.c_str(), to_len);
	offset += to_len;
	
	memcpy((char*)p.Data() + offset, m_character.c_str(), from_len);
	offset += from_len;
	
	memcpy((char*)p.Data() + offset, message.c_str(), msg_len);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Sending UCS tell to {}: {}", to, message) << std::endl;
	}
	
	m_ucs_stream->QueuePacket(p);
}

void EverQuest::UCSSendJoinChannel(const std::string &channel)
{
	if (!m_ucs_stream || !m_ucs_connected) {
		return;
	}
	
	// Join channel format: opcode(1) + channel_name(null-terminated) + character_name(null-terminated)
	size_t ch_len = channel.length() + 1;
	size_t name_len = m_character.length() + 1;
	size_t packet_size = 1 + ch_len + name_len;
	
	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);
	
	size_t offset = 0;
	p.PutUInt8(offset++, HC_OP_UCS_ChatJoin);
	
	memcpy((char*)p.Data() + offset, channel.c_str(), ch_len);
	offset += ch_len;
	
	memcpy((char*)p.Data() + offset, m_character.c_str(), name_len);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Joining UCS channel: {}", channel) << std::endl;
	}
	
	m_ucs_stream->QueuePacket(p);
}

void EverQuest::UCSSendLeaveChannel(const std::string &channel)
{
	if (!m_ucs_stream || !m_ucs_connected) {
		return;
	}
	
	// Leave channel format: opcode(1) + channel_name(null-terminated) + character_name(null-terminated)
	size_t ch_len = channel.length() + 1;
	size_t name_len = m_character.length() + 1;
	size_t packet_size = 1 + ch_len + name_len;
	
	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);
	
	size_t offset = 0;
	p.PutUInt8(offset++, HC_OP_UCS_ChatLeave);
	
	memcpy((char*)p.Data() + offset, channel.c_str(), ch_len);
	offset += ch_len;
	
	memcpy((char*)p.Data() + offset, m_character.c_str(), name_len);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Leaving UCS channel: {}", channel) << std::endl;
	}
	
	m_ucs_stream->QueuePacket(p);
}

void EverQuest::UCSProcessChatMessage(const EQ::Net::Packet &p)
{
	if (p.Length() < 2) return;
	
	// UCS chat message format: opcode(1) + channel(null-terminated) + sender(null-terminated) + message(null-terminated)
	size_t offset = 1; // Skip opcode
	
	std::string channel = p.GetCString(offset);
	offset += channel.length() + 1;
	
	if (offset >= p.Length()) return;
	
	std::string sender = p.GetCString(offset);
	offset += sender.length() + 1;
	
	if (offset >= p.Length()) return;
	
	std::string message = p.GetCString(offset);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("[UCS] {}.{}: {}", channel, sender, message) << std::endl;
	}
}

void EverQuest::UCSProcessMailNew(const EQ::Net::Packet &p)
{
	if (s_debug_level >= 1) {
		std::cout << "New mail notification received" << std::endl;
	}
}

void EverQuest::UCSProcessBuddy(const EQ::Net::Packet &p)
{
	if (s_debug_level >= 1) {
		std::cout << "Buddy list update received" << std::endl;
	}
}
*/

// Movement implementation
void EverQuest::Move(float x, float y, float z)
{
	LOG_DEBUG(MOD_MAIN, "Move called: target=({:.2f}, {:.2f}, {:.2f})", x, y, z);
	// Use pathfinding by default
	MoveToWithPath(x, y, z);
	
	// Non-blocking - movement will be handled by the update system
}

void EverQuest::MoveToEntity(const std::string &name)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Found entity '{}' at ({:.2f}, {:.2f}, {:.2f})", 
				entity->name, entity->x, entity->y, entity->z) << std::endl;
		}
		LOG_DEBUG(MOD_MAIN, "MoveToEntity: Moving to {} at ({:.2f}, {:.2f}, {:.2f})", 
			entity->name, entity->x, entity->y, entity->z);
		Move(entity->x, entity->y, entity->z);  // This will block until complete
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::MoveToEntityWithinRange(const std::string &name, float stop_distance)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Found entity '{}' at ({:.2f}, {:.2f}, {:.2f}), stopping within {:.1f} units", 
				entity->name, entity->x, entity->y, entity->z, stop_distance) << std::endl;
		}
		
		// Calculate current distance
		float dx = entity->x - m_x;
		float dy = entity->y - m_y;
		float dz = entity->z - m_z;
		float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		
		// If we're already within range, don't move
		if (dist <= stop_distance) {
			if (s_debug_level >= 1) {
				std::cout << fmt::format("Already within range ({:.1f} <= {:.1f})", dist, stop_distance) << std::endl;
			}
			return;
		}
		
		// Predict where the target will be based on its movement
		// Use a prediction time based on how long it would take us to reach them
		float our_speed = 4.3f;  // Approximate run speed
		float time_to_reach = dist / our_speed;
		
		// Limit prediction time to avoid overshooting
		if (time_to_reach > 2.0f) time_to_reach = 2.0f;
		
		// Calculate predicted position
		float predicted_x = entity->x + entity->delta_x * time_to_reach;
		float predicted_y = entity->y + entity->delta_y * time_to_reach;
		float predicted_z = entity->z + entity->delta_z * time_to_reach;
		
		// Calculate direction to predicted position
		dx = predicted_x - m_x;
		dy = predicted_y - m_y;
		dz = predicted_z - m_z;
		dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		
		// Calculate position to stop at (stop_distance units away from predicted position)
		// We want to move to 80% of the stop distance to ensure we're well within range
		float move_ratio = (dist - stop_distance * 0.8f) / dist;
		float target_x = m_x + dx * move_ratio;
		float target_y = m_y + dy * move_ratio;
		float target_z = m_z + dz * move_ratio;
		
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Predictive targeting: entity at ({:.1f},{:.1f},{:.1f}), velocity ({:.1f},{:.1f},{:.1f})", entity->x, entity->y, entity->z, entity->delta_x, entity->delta_y, entity->delta_z);
			LOG_DEBUG(MOD_MAIN, "Predicted position in {:.1f}s: ({:.1f},{:.1f},{:.1f})", time_to_reach, predicted_x, predicted_y, predicted_z);
		}
		
		LOG_DEBUG(MOD_MAIN, "MoveToEntityWithinRange: Moving to ({:.2f}, {:.2f}, {:.2f}) to be within {:.1f} of {}", 
			target_x, target_y, target_z, stop_distance, entity->name);
		
		// For combat movement, don't block - let UpdateCombatMovement handle continuous tracking
		if (m_in_combat_movement) {
			// Just set the target and let the movement system handle it
			m_target_x = target_x;
			m_target_y = target_y;
			m_target_z = target_z;
			m_is_moving = true;
			m_heading = CalculateHeading(m_x, m_y, predicted_x, predicted_y);
			
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Non-blocking combat movement initiated");
			}
		} else {
			Move(target_x, target_y, target_z);  // This will block until complete
		}
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::Follow(const std::string &name)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		m_follow_target = entity->name;  // Store the actual entity name
		std::cout << fmt::format("Following {}", entity->name) << std::endl;
		
		// Calculate initial path if pathfinding is enabled
		if (m_use_pathfinding && m_pathfinder) {
			LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding enabled, m_pathfinder={}", 
				m_pathfinder ? "valid" : "null");
			float dist = CalculateDistance2D(m_x, m_y, entity->x, entity->y);
			LOG_DEBUG(MOD_MAIN, "Follow: Distance to target: {:.2f}, follow_distance: {:.2f}", 
				dist, m_follow_distance);
			if (dist > m_follow_distance) {
				// Find path from current position to target
				LOG_DEBUG(MOD_MAIN, "Follow: Calculating path from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", m_x, m_y, m_z, entity->x, entity->y, entity->z);
				if (FindPath(m_x, m_y, m_z, entity->x, entity->y, entity->z)) {
					// Start following the path
					m_current_path_index = 0;
					m_is_moving = true;
					LOG_DEBUG(MOD_MOVEMENT, "Follow: Path calculated successfully with {} waypoints",
						m_current_path.size());
					for (size_t i = 0; i < std::min(m_current_path.size(), size_t(5)); i++) {
						LOG_DEBUG(MOD_MOVEMENT, "  Waypoint {}: ({:.2f},{:.2f},{:.2f})",
							i, m_current_path[i].x, m_current_path[i].y, m_current_path[i].z);
					}
				} else {
					// Pathfinding failed, use direct movement
					LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding failed, using direct movement");
					m_target_x = entity->x;
					m_target_y = entity->y;
					m_target_z = entity->z;
					m_is_moving = true;
				}
			}
		} else {
			LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding disabled (m_use_pathfinding={}, m_pathfinder={})", m_use_pathfinding, m_pathfinder ? "valid" : "null");
		}
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::StopFollow()
{
	if (!m_follow_target.empty()) {
		std::cout << fmt::format("Stopped following {}", m_follow_target) << std::endl;
		m_follow_target.clear();
	}
	StopMovement();
}

void EverQuest::StartCombatMovement(const std::string &name, float stop_distance)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		m_combat_target = entity->name;
		m_combat_stop_distance = stop_distance;
		m_in_combat_movement = true;
		m_last_combat_movement_update = std::chrono::steady_clock::now();

		// Phase 7.6: Sync to GameState
		m_game_state.combat().setCombatTarget(entity->name);
		m_game_state.combat().setCombatStopDistance(stop_distance);
		m_game_state.combat().setInCombatMovement(true);
		m_game_state.combat().setLastCombatMovementUpdate(m_last_combat_movement_update);

		if (s_debug_level >= 1) {
			std::cout << fmt::format("Starting combat movement to '{}' with stop distance {:.1f}",
				entity->name, stop_distance) << std::endl;
		}

		// Start initial movement immediately
		UpdateCombatMovement();
	} else {
		std::cout << fmt::format("Combat target '{}' not found", name) << std::endl;
	}
}

void EverQuest::UpdateCombatMovement()
{
	if (!m_in_combat_movement || m_combat_target.empty()) {
		return;
	}
	
	Entity* entity = FindEntityByName(m_combat_target);
	if (!entity) {
		// Target disappeared
		m_in_combat_movement = false;
		m_game_state.combat().setInCombatMovement(false);  // Phase 7.6
		StopMovement();
		return;
	}
	
	// Calculate current distance
	float dx = entity->x - m_x;
	float dy = entity->y - m_y;
	float dz = entity->z - m_z;
	float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
	
	// Check if we're within range
	if (dist <= m_combat_stop_distance) {
		// We're in range, stop if moving
		if (m_is_moving) {
			StopMovement();
		}
		// Keep combat movement active to track if target moves away
		return;
	}
	
	// We're out of range, need to move closer
	// Only update movement if target has moved significantly or enough time has passed
	auto now = std::chrono::steady_clock::now();
	auto time_since_update = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - m_last_combat_movement_update).count();
	
	// For hunting, always update to track the moving target
	bool should_update = false;
	bool is_hunting = m_combat_manager && m_combat_manager->IsAutoHunting();
	
	if (is_hunting) {
		// When hunting, always update position to track moving targets
		should_update = true;
	} else {
		// Normal combat movement - update less frequently
		int update_interval = 500;
		float movement_threshold = 5.0f;
		
		if (time_since_update >= update_interval) {
			should_update = true;
		} else if (m_is_moving) {
			// Check if target moved significantly from where we're heading
			if (!m_current_path.empty()) {
				// Check distance from path endpoint to target
				const auto& endpoint = m_current_path.back();
				float path_dx = std::abs(entity->x - endpoint.x);
				float path_dy = std::abs(entity->y - endpoint.y);
				if (path_dx > movement_threshold || path_dy > movement_threshold) {
					should_update = true;
					LOG_DEBUG(MOD_MOVEMENT, "Target moved {:.1f} units from path endpoint, updating",
						std::sqrt(path_dx*path_dx + path_dy*path_dy));
				}
			} else {
				// Direct movement - check against target position
				float target_dx = std::abs(entity->x - m_target_x);
				float target_dy = std::abs(entity->y - m_target_y);
				if (target_dx > movement_threshold || target_dy > movement_threshold) {
					should_update = true;
				}
			}
		}
	}
	
	if (should_update || !m_is_moving) {
		// Only update direct target if we're not following a path
		bool following_path = !m_current_path.empty() && m_current_path_index < m_current_path.size();
		
		if (!following_path) {
			// Use predictive targeting for hunting
			if (is_hunting) {
				// Predict where the target will be
				float our_speed = 4.3f;
				float time_to_reach = dist / our_speed;
				if (time_to_reach > 1.0f) time_to_reach = 1.0f; // Shorter prediction for updates
				
				float predicted_x = entity->x + entity->delta_x * time_to_reach;
				float predicted_y = entity->y + entity->delta_y * time_to_reach;
				float predicted_z = entity->z + entity->delta_z * time_to_reach;
				
				// Recalculate direction to predicted position
				dx = predicted_x - m_x;
				dy = predicted_y - m_y;
				dz = predicted_z - m_z;
				dist = std::sqrt(dx*dx + dy*dy + dz*dz);
			}
			
			// Calculate new position to move to
			float move_ratio = (dist - m_combat_stop_distance * 0.8f) / dist;
			m_target_x = m_x + dx * move_ratio;
			m_target_y = m_y + dy * move_ratio;
			m_target_z = m_z + dz * move_ratio;
			
			// Update heading and start moving
			m_heading = CalculateHeading(m_x, m_y, m_target_x, m_target_y);
			
			LOG_DEBUG(MOD_MOVEMENT, "Combat movement (direct): Set target=({:.1f},{:.1f},{:.1f})", m_target_x, m_target_y, m_target_z);
		} else {
			LOG_DEBUG(MOD_MOVEMENT, "Combat movement (path): Keeping target=({:.1f},{:.1f},{:.1f}), path_index={}/{}",
				m_target_x, m_target_y, m_target_z, m_current_path_index, m_current_path.size());
		}
		
		m_is_moving = true;
		m_last_combat_movement_update = now;
		
		LOG_DEBUG(MOD_MOVEMENT, "Combat movement: from ({:.1f},{:.1f},{:.1f}) to target {} at ({:.1f},{:.1f},{:.1f}), dist={:.1f}, stop_dist={:.1f}", m_x, m_y, m_z, entity->name, entity->x, entity->y, entity->z, dist, m_combat_stop_distance);
		LOG_DEBUG(MOD_MOVEMENT, "Combat movement: moving to ({:.1f},{:.1f},{:.1f}), m_is_moving set to {}",
			m_target_x, m_target_y, m_target_z, m_is_moving);
		
		// For now, use direct movement for combat (like the working attack command)
		// We can add pathfinding later once basic movement works
		m_current_path.clear();
		m_current_path_index = 0;
		
		LOG_DEBUG(MOD_MOVEMENT, "Combat movement using direct approach (no pathfinding)");
	}
}

// ============================================================================
// Movement State Accessors (Phase 7.1 - use embedded m_game_state)
// ============================================================================

glm::vec3 EverQuest::GetPosition() const
{
	// Return actual position (m_x, m_y, m_z are kept up to date)
	return glm::vec3(m_x, m_y, m_z);
}

float EverQuest::GetHeading() const
{
	// Read from GameState (single source of truth)
	return m_game_state.player().heading();
}

bool EverQuest::IsMoving() const
{
	// Read from GameState (single source of truth)
	return m_game_state.player().isMoving();
}

void EverQuest::SetHeading(float heading)
{
	// Update both local and GameState (during migration)
	m_heading = heading;
	m_game_state.player().setHeading(heading);
}

void EverQuest::SetMoveSpeed(float speed)
{
	// Update both local and GameState (during migration)
	m_move_speed = speed;
	m_game_state.player().setMoveSpeed(speed);
}

void EverQuest::SetPosition(float x, float y, float z)
{
	// Update both local and GameState (during migration)
	m_x = x;
	m_y = y;
	m_z = z;
	m_game_state.player().setPosition(x, y, z);
}

void EverQuest::SetMoving(bool moving)
{
	// Update both local and GameState (during migration)
	m_is_moving = moving;
	m_game_state.player().setMoving(moving);
}

// ============================================================================
// Character Stat Accessors (Phase 7.2 - use embedded m_game_state)
// ============================================================================


// ============================================================================
// Entity Sync Helpers (Phase 7.4 - sync to EntityManager)
// ============================================================================


// ============================================================================
// Combat State Sync (Phase 7.6 - sync to CombatState)
// ============================================================================

void EverQuest::SetCombatStopDistance(float distance)
{
	m_combat_stop_distance = distance;
	m_game_state.combat().setCombatStopDistance(distance);
}

// ============================================================================
// Behavioral Flag Accessors (Phase 7.8 - read from PlayerState)
// ============================================================================

bool EverQuest::IsAFK() const
{
	return m_game_state.player().isAFK();
}

bool EverQuest::IsAnonymous() const
{
	return m_game_state.player().isAnonymous();
}

bool EverQuest::IsRoleplay() const
{
	return m_game_state.player().isRoleplay();
}

bool EverQuest::IsCamping() const
{
	return m_game_state.player().isCamping();
}

// ============================================================================

void EverQuest::Face(float x, float y, float z)
{
	// Calculate heading to face the given position
	float new_heading = CalculateHeading(m_x, m_y, x, y);
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Face: current pos ({:.1f},{:.1f}), target ({:.1f},{:.1f}), old heading {:.1f}°, new heading {:.1f}°", m_x, m_y, x, y, m_heading, new_heading);
	}
	
	// Always update heading - even small changes should be sent
	// The previous 0.1f threshold was preventing the face command from working
	m_heading = new_heading;
	SendPositionUpdate();
}

void EverQuest::FaceEntity(const std::string &name)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		if (s_debug_level >= 2) {
			std::cout << fmt::format("Facing entity '{}'", entity->name) << std::endl;
		}
		Face(entity->x, entity->y, entity->z);
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::MoveTo(float x, float y, float z)
{
	// Trace who's calling MoveTo
	if (s_debug_level >= 1 && std::abs(x - m_x) < 0.1f && std::abs(y - m_y) < 0.1f) {
		LOG_DEBUG(MOD_MAIN, "WARNING: MoveTo called with current position! target=({:.2f},{:.2f},{:.2f}) current=({:.2f},{:.2f},{:.2f})", x, y, z, m_x, m_y, m_z);
	}
	
	m_target_x = x;
	m_target_y = y;
	m_target_z = z;
	m_is_moving = true;
	
	// Calculate initial heading towards target
	float new_heading = CalculateHeading(m_x, m_y, x, y);
	m_heading = new_heading;
	// Send immediate position update to show the heading change
	SendPositionUpdate();
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "MoveTo: Setting target=({:.2f}, {:.2f}, {:.2f}) from current=({:.2f}, {:.2f}, {:.2f}), heading={:.1f}, m_is_moving={}", 
			x, y, z, m_x, m_y, m_z, m_heading, m_is_moving);
	}
}

void EverQuest::StopMovement()
{
	if (m_is_moving) {
		m_is_moving = false;
		m_animation = ANIM_STAND;

		// Clear any active path
		m_current_path.clear();
		m_current_path_index = 0;

		// Clear target coordinates so UpdateMovement doesn't keep moving back
		m_target_x = m_target_y = m_target_z = 0.0f;

		SendPositionUpdate();

		if (s_debug_level >= 1) {
			std::cout << "Movement stopped" << std::endl;
		}
	}
}

void EverQuest::UpdateMovement()
{
	// Process deferred zone change if approved
	// This MUST be at the very start of UpdateMovement to process outside of any callback context
	if (m_zone_change_approved) {
		m_zone_change_approved = false;
		LOG_DEBUG(MOD_ZONE, "Processing deferred zone change...");
		ProcessDeferredZoneChange();
		LOG_DEBUG(MOD_ZONE, "Deferred zone change processed");
		return;  // Don't process movement while zone changing
	}

	// Update camp timer
	UpdateCampTimer();

	// Check distance to banker NPC - close bank if player moved too far
	if (IsBankWindowOpen() && m_banker_npc_id != 0) {
		auto it = m_entities.find(m_banker_npc_id);
		if (it == m_entities.end()) {
			// Banker NPC no longer exists (despawned, etc.)
			LOG_DEBUG(MOD_INVENTORY, "Banker NPC {} no longer exists, closing bank", m_banker_npc_id);
			CloseBankWindow();
		} else {
			float dist = CalculateDistance2D(m_x, m_y, it->second.x, it->second.y);
			if (dist > NPC_INTERACTION_DISTANCE) {
				LOG_DEBUG(MOD_INVENTORY, "Player moved too far from banker ({:.1f} > {:.1f}), closing bank",
					dist, NPC_INTERACTION_DISTANCE);
				CloseBankWindow();
				AddChatSystemMessage("You have moved too far from the banker.");
			}
		}
	}

	// Debug: Log target at start of UpdateMovement
	static auto last_update_debug = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	if (m_in_combat_movement &&
	    std::chrono::duration_cast<std::chrono::seconds>(now - last_update_debug).count() >= 1) {
		LOG_DEBUG(MOD_MOVEMENT, "UpdateMovement START: target=({:.1f},{:.1f},{:.1f}) is_moving={} path_size={}",
			m_target_x, m_target_y, m_target_z, m_is_moving, m_current_path.size());
		last_update_debug = now;
	}
	
	// Update keyboard-based movement first
	if (m_move_forward || m_move_backward || m_turn_left || m_turn_right) {
		UpdateKeyboardMovement();
		// Check for zone line collision
		CheckZoneLine();
		// SendPositionUpdate has internal 250ms throttling
		SendPositionUpdate();
		// Don't process other movement if using keyboard
		return;
	}
	
	// Update combat movement if active
	if (m_in_combat_movement) {
		UpdateCombatMovement();
	}
	
	// Check if we're following someone
	if (!m_follow_target.empty()) {
		// Find the entity we're following
		for (const auto& pair : m_entities) {
			const Entity& entity = pair.second;
			if (entity.name == m_follow_target) {
				// Check if we're close enough
				float dist = CalculateDistance2D(m_x, m_y, entity.x, entity.y);
				float z_diff = std::abs(m_z - entity.z);
				
				// Log follow status periodically
				static std::chrono::steady_clock::time_point last_follow_log = std::chrono::steady_clock::now();
				auto now_follow = std::chrono::steady_clock::now();
				auto elapsed_follow = std::chrono::duration_cast<std::chrono::milliseconds>(now_follow - last_follow_log);
				
				if (elapsed_follow.count() >= 1000 && s_debug_level >= 1) {
					LOG_DEBUG(MOD_MAIN, "Following {}: Distance={:.1f} (stop at {:.1f}), Z-diff={:.1f}, Speed={:.1f}", m_follow_target, dist, m_follow_distance, z_diff, m_move_speed);
					last_follow_log = now_follow;
				}
				
				if (dist < m_follow_distance) {
					// We're close enough, stop if we were moving
					if (m_is_moving) {
						if (s_debug_level >= 1) {
							LOG_DEBUG(MOD_MAIN, "Reached follow distance ({:.1f}), stopping", dist);
						}
						StopMovement();
					}
					return;
				} else if (dist > m_follow_distance * 1.5f) {
					// Target has moved significantly, recalculate path
					// Check if final destination changed significantly
					float final_dest_dist = 0.0f;
					if (!m_current_path.empty() && m_current_path.size() > 0) {
						// Compare with the final waypoint in our path
						const auto& final_waypoint = m_current_path.back();
						final_dest_dist = CalculateDistance2D(final_waypoint.x, final_waypoint.y, entity.x, entity.y);
					} else {
						// No path, compare with our immediate target
						final_dest_dist = CalculateDistance2D(m_target_x, m_target_y, entity.x, entity.y);
					}
					
					if (final_dest_dist > 5.0f || m_current_path.empty()) {
						// Recalculate path using pathfinding
						LOG_DEBUG(MOD_MAIN, "UpdateMovement: Target moved significantly (dist={:.2f})", final_dest_dist);
						if (m_use_pathfinding && m_pathfinder) {
							// Find path from current position to target
							LOG_DEBUG(MOD_MAIN, "UpdateMovement: Recalculating path from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", m_x, m_y, m_z, entity.x, entity.y, entity.z);
							if (FindPath(m_x, m_y, m_z, entity.x, entity.y, entity.z)) {
								// Start following the path
								m_current_path_index = 0;
								m_is_moving = true;
								LOG_DEBUG(MOD_MOVEMENT, "UpdateMovement: Path recalculated with {} waypoints",
									m_current_path.size());
								for (size_t i = 0; i < std::min(m_current_path.size(), size_t(3)); i++) {
									LOG_DEBUG(MOD_MOVEMENT, "  Waypoint {}: ({:.2f},{:.2f},{:.2f})",
										i, m_current_path[i].x, m_current_path[i].y, m_current_path[i].z);
								}
							} else {
								// Pathfinding failed, use direct movement
								LOG_DEBUG(MOD_MAIN, "UpdateMovement: Pathfinding failed, using direct movement");
								m_target_x = entity.x;
								m_target_y = entity.y;
								m_target_z = entity.z;
								m_is_moving = true;
							}
						} else {
							// Fallback to direct movement
							LOG_DEBUG(MOD_MAIN, "UpdateMovement: Pathfinding disabled, using direct movement");
							m_target_x = entity.x;
							m_target_y = entity.y;
							m_target_z = entity.z;
							m_is_moving = true;
						}
					}
				}
				break;
			}
		}
	}
	
	// Always update jump if jumping, even when not moving
	if (m_is_jumping) {
		float old_z = m_z;
		UpdateJump();
		
		// If Z changed, send position update immediately
		if (m_z != old_z) {
			SendPositionUpdate();
		}
	}
	
	if (!m_is_moving) {
		// Still check for zone lines when stopped (player might have stopped in one)
		CheckZoneLine();

		// Send idle position updates like real clients (every 1-2 seconds)
		static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_idle_updates;
		if (last_idle_updates.find(this) == last_idle_updates.end()) {
			last_idle_updates[this] = std::chrono::steady_clock::now();
		}

		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_idle_updates[this]);

		if (elapsed.count() >= 1500) {  // 1.5 seconds average
			SendPositionUpdate();
			last_idle_updates[this] = now;
		}
		return;
	}
	
	// Check if we're following a path
	if (!m_current_path.empty() && m_current_path_index < m_current_path.size()) {
		// Check if we've reached the current waypoint
		const auto& waypoint = m_current_path[m_current_path_index];
		float dist_to_waypoint = CalculateDistance2D(m_x, m_y, waypoint.x, waypoint.y);

		// Always log path progress for debugging coordinate issues
		static auto last_path_debug = std::chrono::steady_clock::now();
		auto now_path_debug = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::milliseconds>(now_path_debug - last_path_debug).count() >= 500) {
			LOG_INFO(MOD_MAIN, "Path progress: pos=({:.1f},{:.1f},{:.1f}) waypoint[{}]=({:.1f},{:.1f},{:.1f}) dist={:.1f}",
				m_x, m_y, m_z, m_current_path_index, waypoint.x, waypoint.y, waypoint.z, dist_to_waypoint);
			last_path_debug = now_path_debug;
		}

		LOG_DEBUG(MOD_MOVEMENT, "Following path: waypoint {}/{}, dist to waypoint: {:.2f}",
			m_current_path_index, m_current_path.size()-1, dist_to_waypoint);
		
		// Stuck detection - track if we're not making progress
		static std::map<EverQuest*, std::pair<float, std::chrono::steady_clock::time_point>> stuck_detection;
		auto now = std::chrono::steady_clock::now();
		
		if (stuck_detection.find(this) == stuck_detection.end()) {
			stuck_detection[this] = {dist_to_waypoint, now};
		} else {
			auto& [last_dist, last_time] = stuck_detection[this];
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_time).count();
			
			// If we haven't moved closer to the waypoint in 3 seconds, we're stuck
			if (elapsed >= 3 && std::abs(dist_to_waypoint - last_dist) < 1.0f) {
				LOG_WARN(MOD_MOVEMENT, "Stuck at waypoint {} - distance hasn't changed in {} seconds",
					m_current_path_index, elapsed);

				// Skip to next waypoint or stop
				if (m_current_path_index < m_current_path.size() - 1) {
					m_current_path_index++;
					LOG_DEBUG(MOD_MOVEMENT, "Skipping to next waypoint due to being stuck");
					// Reset stuck detection for the new waypoint
					stuck_detection.erase(this);
				} else {
					LOG_DEBUG(MOD_MOVEMENT, "Stuck on final waypoint, stopping movement");
					StopMovement();
					stuck_detection.erase(this);
					return;
				}
			} else if (elapsed >= 1) {
				// Update every second if we've made progress
				if (std::abs(dist_to_waypoint - last_dist) > 0.5f) {
					stuck_detection[this] = {dist_to_waypoint, now};
				}
			}
		}
		
		// Check both 2D distance and Z difference for waypoint reached
		float z_diff_to_waypoint = std::abs(m_z - waypoint.z);
		bool waypoint_reached = (dist_to_waypoint < 5.0f && z_diff_to_waypoint < 10.0f) || dist_to_waypoint < 3.0f;
		
		if (waypoint_reached) { // Within range of waypoint
			// Move to next waypoint
			m_current_path_index++;
			stuck_detection.erase(this); // Clear stuck detection when reaching waypoint
			
			if (m_current_path_index >= m_current_path.size()) {
				// Reached the end of the path
				LOG_DEBUG(MOD_MAIN, "Reached end of path");
				StopMovement();
				return;
			} else {
				// Move to next waypoint
				const auto& next_waypoint = m_current_path[m_current_path_index];
				
				// Check if the next waypoint is different enough from current position
				float dist_to_next = CalculateDistance2D(m_x, m_y, next_waypoint.x, next_waypoint.y);
				if (dist_to_next > 2.0f) {  // Only move if it's far enough away
					MoveTo(next_waypoint.x, next_waypoint.y, next_waypoint.z);

					LOG_TRACE(MOD_MOVEMENT, "Reached waypoint {}, moving to waypoint {} of {}",
						m_current_path_index - 1, m_current_path_index, m_current_path.size() - 1);
				} else {
					// Skip this waypoint, it's too close
					LOG_TRACE(MOD_MOVEMENT, "Skipping waypoint {} (too close: {:.2f} units)",
						m_current_path_index, dist_to_next);
					// Continue to check the next waypoint in the next update
				}
			}
		}
	}
	
	// Only process console-based target movement if we have an active target or path
	// Skip this if we're in graphics Player mode without a console movement command
	bool has_console_movement = !m_follow_target.empty() || !m_current_path.empty() ||
	                            m_in_combat_movement ||
	                            (m_target_x != 0.0f || m_target_y != 0.0f || m_target_z != 0.0f);

	if (!has_console_movement) {
		// No console-based movement active, skip target processing
		return;
	}

	// Debug: Check what target we're moving to
	static auto last_target_debug = std::chrono::steady_clock::now();
	auto now_debug = std::chrono::steady_clock::now();
	if (ShouldLog(MOD_MOVEMENT, LOG_DEBUG) && std::chrono::duration_cast<std::chrono::seconds>(now_debug - last_target_debug).count() >= 1) {
		LOG_DEBUG(MOD_MOVEMENT, "Movement target check: current=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) path_size={} path_index={}",
			m_x, m_y, m_z, m_target_x, m_target_y, m_target_z, m_current_path.size(), m_current_path_index);
		last_target_debug = now_debug;
	}

	// Calculate distance to target
	float dx = m_target_x - m_x;
	float dy = m_target_y - m_y;
	float dz = m_target_z - m_z;
	float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

	// Check if we've reached the target
	if (distance < 2.0f) {  // Stop within 2 units of target
		LOG_DEBUG(MOD_MOVEMENT, "Reached target at distance {:.2f}, target=({:.1f},{:.1f},{:.1f})",
			distance, m_target_x, m_target_y, m_target_z);

		m_x = m_target_x;
		m_y = m_target_y;
		m_z = m_target_z;

		// If following a path, check for next waypoint
		if (!m_current_path.empty() && m_current_path_index < m_current_path.size() - 1) {
			// Continue to next waypoint instead of stopping
			return;
		}

		StopMovement();
		return;
	}
	
	// Calculate time delta based on actual elapsed time
	static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_move_times;
	if (last_move_times.find(this) == last_move_times.end()) {
		last_move_times[this] = std::chrono::steady_clock::now();
	}
	auto now_movement = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_movement - last_move_times[this]);
	last_move_times[this] = now_movement;
	
	float delta_time = elapsed.count() / 1000.0f;  // Convert to seconds
	if (delta_time > 0.1f) delta_time = 0.1f;  // Cap at 100ms to prevent large jumps
	
	// Debug delta time issues
	if (m_in_combat_movement && delta_time < 0.01f) {
		LOG_WARN(MOD_MOVEMENT, "Very small delta_time: {:.6f}s ({}ms)",
			delta_time, elapsed.count());
	}
	
	// Calculate movement step using current movement speed
	float current_speed = m_move_speed;
	
	// Adjust speed when following
	if (!m_follow_target.empty()) {
		// Speed up when far, slow down when close
		if (distance > FOLLOW_FAR_DISTANCE) {
			current_speed *= FOLLOW_MAX_SPEED_MULT;
		} else if (distance < FOLLOW_CLOSE_DISTANCE) {
			current_speed *= FOLLOW_MIN_SPEED_MULT;
		} else {
			// Linear interpolation between min and max speed
			float speed_factor = (distance - FOLLOW_CLOSE_DISTANCE) / (FOLLOW_FAR_DISTANCE - FOLLOW_CLOSE_DISTANCE);
			current_speed *= FOLLOW_MIN_SPEED_MULT + (FOLLOW_MAX_SPEED_MULT - FOLLOW_MIN_SPEED_MULT) * speed_factor;
		}
	}
	
	float step = current_speed * delta_time;
	if (step > distance) {
		step = distance;
	}
	
	// Debug output for combat movement
	static auto last_move_debug = std::chrono::steady_clock::now();
	if (s_debug_level >= 1 && m_in_combat_movement && 
	    std::chrono::duration_cast<std::chrono::seconds>(now_movement - last_move_debug).count() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Movement update: pos=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) dist={:.1f} speed={:.1f} step={:.1f}", m_x, m_y, m_z, m_target_x, m_target_y, m_target_z, distance, current_speed, step);
		last_move_debug = now_movement;
	}
	
	// Normalize direction and move
	float factor = step / distance;
	float old_x = m_x;
	float old_y = m_y;
	m_x += dx * factor;
	m_y += dy * factor;
	m_z += dz * factor;
	
	// Debug: verify position actually changed
	if (s_debug_level >= 1 && m_in_combat_movement) {
		LOG_DEBUG(MOD_MAIN, "Position update: ({:.1f},{:.1f}) -> ({:.1f},{:.1f}) delta=({:.3f},{:.3f})", old_x, old_y, m_x, m_y, m_x - old_x, m_y - old_y);
	}
	
	// Update heading
	m_heading = CalculateHeading(m_x - dx * factor, m_y - dy * factor, m_x, m_y);

	// Update bridge position for console-driven movement (moveto, follow, etc.)
	if (m_bridge) {
		eqt::state::PlayerMovedData pdata;
		pdata.x = m_x; pdata.y = m_y; pdata.z = m_z;
		pdata.heading = m_heading;
		pdata.dx = dx; pdata.dy = dy; pdata.dz = dz;
		pdata.isMoving = true;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMoved, std::move(pdata)));
	}

	// Set animation based on actual movement distance
	// Don't change animation if we're jumping
	if (!m_is_jumping) {
		// Calculate movement distance in this update
		float move_dist = std::sqrt(dx * dx + dy * dy);
		
		// Determine animation based on movement distance
		uint32_t new_animation = ANIM_STAND;
		
		if (move_dist < 0.1f) {
			// Very small movement (< 0.1 units) - no animation change
			new_animation = ANIM_STAND;
		} else if (move_dist < 2.0f) {
			// Small movement - walk animation
			new_animation = ANIM_WALK;
		} else {
			// Larger movement - run animation
			new_animation = ANIM_RUN;
		}
		
		// Only change animation if it's different
		if (m_animation != new_animation) {
			m_animation = new_animation;
		}
	}
	
	// Update jump if jumping
	bool was_jumping = m_is_jumping;
	float old_z = m_z;
	UpdateJump();
	
	// If jumping and Z changed, send position update immediately
	if (m_is_jumping && m_z != old_z) {
		SendPositionUpdate();
		m_last_position_update_time = now;
	}
	
	// Fix Z position periodically to follow terrain (but not while jumping)
	if (!m_is_jumping) {
		static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_z_fix_times;
		if (last_z_fix_times.find(this) == last_z_fix_times.end()) {
			last_z_fix_times[this] = now;
		}
		
		auto z_fix_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_z_fix_times[this]);
		// Fix Z every 1000ms while moving (once per second to reduce adjustments)
		if (z_fix_elapsed.count() >= 1000) {
			FixZ();
			last_z_fix_times[this] = now;
		}
	}
	
	// Check for zone line collision
	CheckZoneLine();

	// Send position update with proper throttling per instance
	if (!m_last_position_update_time.time_since_epoch().count()) {
		m_last_position_update_time = std::chrono::steady_clock::now();
	}

	auto update_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_movement - m_last_position_update_time);

	// Send updates at regular intervals when moving
	if (update_elapsed.count() >= POSITION_UPDATE_INTERVAL_MS) {
		SendPositionUpdate();
		m_last_position_update_time = now_movement;
	}
}

void EverQuest::SendPositionUpdate()
{
	if (!IsFullyZonedIn() || !m_zone_connection) {
		LOG_DEBUG(MOD_MOVEMENT, "SendPositionUpdate skipped: zoned_in={} zone_conn={}",
			IsFullyZonedIn(), m_zone_connection != nullptr);
		return;
	}

	// Throttle position updates to ~250ms minimum (working client averages ~275ms)
	// This prevents jerky motion when viewed by other players
	static auto lastSendTime = std::chrono::steady_clock::now();
	static int16_t lastSentAnimation = 0;
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSendTime);

	// Allow immediate send when stopping (animation goes to 0) for responsiveness
	bool isStoppingNow = (m_animation == 0 && lastSentAnimation != 0);

	if (!isStoppingNow && elapsed.count() < 250) {
		// Throttled - skip this update
		return;
	}

	lastSendTime = now;
	lastSentAnimation = m_animation;
	
	// Keep track of last position for delta calculations (per instance, not static)
	static std::map<EverQuest*, std::tuple<float, float, float, float>> last_positions;
	
	// Initialize last position if this is the first update
	if (last_positions.find(this) == last_positions.end()) {
		last_positions[this] = std::make_tuple(m_x, m_y, m_z, m_heading);
	}
	
	auto& [last_x, last_y, last_z, last_heading] = last_positions[this];
	
	// Calculate deltas
	float delta_x = m_x - last_x;
	float delta_y = m_y - last_y;
	float delta_z = m_z - last_z;
	float delta_heading_deg = m_heading - last_heading;
	
	// Normalize delta heading to -180 to 180
	while (delta_heading_deg > 180.0f) delta_heading_deg -= 360.0f;
	while (delta_heading_deg < -180.0f) delta_heading_deg += 360.0f;
	
	// Convert heading values for the packet
	// EQ uses a different heading system where values might need adjustment
	int delta_heading_scaled = static_cast<int>(delta_heading_deg * 512.0f / 360.0f) & 0x3FF; // 10 bits
	
	// Convert heading to 12-bit value for packet
	// Server uses EQ12toFloat which divides by 4, then converts: degrees = (raw/4) * 360/512
	// This simplifies to: degrees = raw * 360 / 2048
	// So to convert degrees to raw: raw = degrees * 2048 / 360
	//
	// C->S transformation: -m_heading + 90 to match server convention
	// Inverse of S->C transformation (m_heading = 90 - server_h)
	float server_heading = 90.0f - m_heading;
	if (server_heading < 0.0f) server_heading += 360.0f;
	if (server_heading >= 360.0f) server_heading -= 360.0f;
	int heading_raw = static_cast<int>(server_heading * 2048.0f / 360.0f);
	// FloatToEQ12 does modulo 2048, so values wrap
	uint16_t heading_scaled = heading_raw % 2048;
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S [SELF] m_heading={:.2f}deg -> server_heading={:.2f}deg -> heading_scaled={} (12-bit)",
			m_heading, server_heading, heading_scaled);
	}
	
	// Build PlayerPositionUpdateClient_Struct
	// NOTE: Field order must match server's expected binary layout exactly
	// Server struct has: spawn_id, sequence, y_pos, delta_z, delta_x, delta_y, ...
	struct {
		uint16_t spawn_id;
		uint16_t sequence;
		float y_pos;
		float delta_z;
		float delta_x;   // Server expects delta_x at offset 12
		float delta_y;   // Server expects delta_y at offset 16
		uint32_t anim_and_delta_heading;
		float x_pos;
		float z_pos;
		uint16_t heading_and_padding;
		uint8_t unknown[2];
	} update;
	
	memset(&update, 0, sizeof(update));

	// Use the spawn ID we extracted from PlayerProfile or ZoneSpawns
	// This is what the server expects in ClientUpdate packets
	update.spawn_id = m_my_spawn_id;
	update.sequence = ++m_movement_sequence;

	// Validate that we have a valid spawn ID
	if (m_my_spawn_id == 0) {
		LOG_ERROR(MOD_MOVEMENT, "SendMovementUpdate called with m_my_spawn_id = 0! Not sending update.");
		return;
	}

	// Check if we're accidentally using another player's ID
	auto it = m_entities.find(m_my_spawn_id);
	if (it != m_entities.end() && it->second.name != m_character) {
		LOG_WARN(MOD_MOVEMENT, "m_my_spawn_id {} belongs to '{}', not our character '{}'!",
			m_my_spawn_id, it->second.name, m_character);
	}
	
	// Convert from internal coordinate system back to server coordinate system
	// When receiving from server: m_x = server_y, m_y = server_x (see ZoneProcessPlayerProfile)
	// So to send back: server's y_pos = m_x, server's x_pos = m_y
	update.y_pos = m_x;  // m_x holds server's original y
	update.x_pos = m_y;  // m_y holds server's original x
	// Send Z position directly without offset - server stores the actual position
	update.z_pos = m_z;
	// Deltas follow the same transformation:
	// Internal delta_x (change in m_x) = change in server's y = server's delta_y
	// Internal delta_y (change in m_y) = change in server's x = server's delta_x
	update.delta_x = delta_y;  // server's delta_x = change in m_y (server's x)
	update.delta_y = delta_x;  // server's delta_y = change in m_x (server's y)
	update.delta_z = delta_z;
	
	// Pack animation and delta_heading into bitfield
	// Use current animation state (which includes jump animation)
	uint32_t anim_value = m_animation;
	update.anim_and_delta_heading = (anim_value & 0x3FF) | (0 << 10) | (1 << 20);
	
	// Pack heading into bitfield (12 bits)
	update.heading_and_padding = heading_scaled;
	
	// Build packet
	EQ::Net::DynamicPacket p;
	p.Resize(sizeof(update) + 2);
	p.PutUInt16(0, HC_OP_ClientUpdate);
	memcpy((char*)p.Data() + 2, &update, sizeof(update));
	
	// Debug: Verify what's in the packet
	if (s_debug_level >= 1) {
		uint16_t packet_spawn_id = p.GetUInt16(2); // spawn_id is first field after opcode
		// LOG_DEBUG(MOD_MAIN, "ClientUpdate packet: spawn_id in struct = {}, spawn_id in packet = {}", 
		// 	update.spawn_id, packet_spawn_id);
	}
	
	// Log C->S position updates at debug level 2+
	if (s_debug_level >= 2) {
		// Log server coordinates (x_pos, y_pos) = (m_y, m_x) due to coordinate swap
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S spawn_id={} server_pos=({:.2f}, {:.2f}, {:.2f}) heading={:.1f} anim={} server_delta=({:.2f}, {:.2f}, {:.2f})",
			m_my_spawn_id, update.x_pos, update.y_pos, update.z_pos, m_heading, m_animation, update.delta_x, update.delta_y, update.delta_z);
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S [SELF] client_pos=({:.2f},{:.2f},{:.2f}) -> server_pos=({:.2f},{:.2f},{:.2f})",
			m_x, m_y, m_z, update.x_pos, update.y_pos, update.z_pos);
	}

	DumpPacket("C->S", HC_OP_ClientUpdate, p);
	SafeQueueZonePacket(p, 0, false);  // Send unreliable
	
	// Update last position
	last_x = m_x;
	last_y = m_y;
	last_z = m_z;
	last_heading = m_heading;
	
	// Track movement history for anti-cheat
	MovementHistoryEntry history_entry;
	history_entry.x = m_x;
	history_entry.y = m_y;
	history_entry.z = m_z;
	history_entry.type = 1; // Normal movement (Collision type)
	history_entry.timestamp = static_cast<uint32_t>(std::time(nullptr));
	
	m_movement_history.push_back(history_entry);
	
	// Send movement history every second (matching client behavior)
	uint32_t current_time = static_cast<uint32_t>(std::time(nullptr));
	if (current_time - m_last_movement_history_send >= 1) {
		m_last_movement_history_send = current_time;  // Update timestamp BEFORE sending to prevent duplicates
		try {
			SendMovementHistory();
		}
		catch (const std::exception& e) {
			LOG_ERROR(MOD_MOVEMENT, "Exception in SendMovementHistory: {}", e.what());
		}
		catch (...) {
			LOG_ERROR(MOD_MOVEMENT, "Unknown exception in SendMovementHistory");
		}
	}
}

float EverQuest::CalculateHeading(float x1, float y1, float x2, float y2)
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	
	// If we're not moving, preserve current heading
	if (std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) {
		return m_heading;
	}
	
	// Calculate angle in radians
	// In EQ's coordinate system: +X is East, +Y is North (verified)
	// We want: 0° = North, 90° = East, 180° = South, 270° = West
	// Standard atan2(y,x) gives: 0° = East, 90° = North, 180° = West, 270° = South
	// So we need to use atan2(x,y) and then adjust by 90 degrees
	float angle = std::atan2(dx, dy);  // This gives 0° = North, 90° = East
	
	// Convert radians to degrees
	float degrees = angle * 180.0f / M_PI;
	
	// atan2 returns -180 to 180, convert to 0-360
	if (degrees < 0.0f) {
		degrees += 360.0f;
	}
	
	// No 180° adjustment needed - the known good client shows:
	// North ≈ 3.5°, East ≈ 137.7°, South ≈ 89.5°, West ≈ 45.8°
	// (converted from raw values 14, 1567, 1018, 521)
	
	// Debug output with more detail
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "CalculateHeading: from ({:.1f},{:.1f}) to ({:.1f},{:.1f}), dx={:.1f}, dy={:.1f}, raw angle={:.1f}°, adjusted={:.1f}°", 
			x1, y1, x2, y2, dx, dy, angle * 180.0f / M_PI, degrees);
	}
	
	return degrees;
}

float EverQuest::CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	float dz = z2 - z1;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float EverQuest::CalculateDistance2D(float x1, float y1, float x2, float y2) const
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	return std::sqrt(dx * dx + dy * dy);
}

bool EverQuest::HasReachedDestination() const
{
	if (!m_is_moving) {
		return true;
	}
	
	// Check if we're close enough to the target (within 2 units)
	float dist = CalculateDistance2D(m_x, m_y, m_target_x, m_target_y);
	return dist < 2.0f;
}

Entity* EverQuest::FindEntityByName(const std::string& name)
{
	// Find entity by name (case-insensitive prefix match)
	// Replace spaces with underscores in search term
	std::string name_lower = name;
	std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
	std::replace(name_lower.begin(), name_lower.end(), ' ', '_');
	
	for (auto& pair : m_entities) {
		Entity& entity = pair.second;
		std::string entity_name_lower = entity.name;
		std::transform(entity_name_lower.begin(), entity_name_lower.end(), entity_name_lower.begin(), ::tolower);
		
		if (entity_name_lower.find(name_lower) == 0) {  // Starts with
			return &entity;
		}
	}
	
	return nullptr;
}

void EverQuest::ListEntities(const std::string& search) const
{
	if (!IsFullyZonedIn()) {
		std::cout << "Not in zone yet" << std::endl;
		return;
	}
	
	if (m_entities.empty()) {
		std::cout << "No entities in zone" << std::endl;
		return;
	}
	
	// Prepare search string
	std::string search_lower = search;
	if (!search.empty()) {
		std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
		std::replace(search_lower.begin(), search_lower.end(), ' ', '_');
	}
	
	if (search.empty()) {
		std::cout << fmt::format("Entities in zone ({} total):", m_entities.size()) << std::endl;
	} else {
		std::cout << fmt::format("Entities matching '{}' in zone:", search) << std::endl;
	}
	
	// Sort entities by distance from player
	std::vector<std::pair<float, const Entity*>> sorted_entities;
	for (const auto& pair : m_entities) {
		const Entity& entity = pair.second;
		
		// Filter by search term if provided
		if (!search.empty()) {
			std::string entity_name_lower = entity.name;
			std::transform(entity_name_lower.begin(), entity_name_lower.end(), entity_name_lower.begin(), ::tolower);
			if (entity_name_lower.find(search_lower) == std::string::npos) {
				continue;  // Skip entities that don't match search
			}
		}
		
		float dist = CalculateDistance(m_x, m_y, m_z, entity.x, entity.y, entity.z);
		sorted_entities.push_back({dist, &entity});
	}
	
	std::sort(sorted_entities.begin(), sorted_entities.end(), 
		[](const auto& a, const auto& b) { return a.first < b.first; });
	
	// Check if any entities matched
	if (sorted_entities.empty() && !search.empty()) {
		std::cout << fmt::format("  No entities found matching '{}'", search) << std::endl;
		return;
	}
	
	// Display up to 20 nearest entities
	int count = 0;
	for (const auto& [dist, entity] : sorted_entities) {
		if (count++ >= 20) {
			std::cout << "  ... and more" << std::endl;
			break;
		}
		
		std::cout << fmt::format("  {} (ID: {}) - Level {} {} - {:.1f} units away at ({:.0f}, {:.0f}, {:.0f})",
			entity->name, entity->spawn_id, entity->level,
			entity->class_id == 0 ? "NPC" : fmt::format("Class {}", entity->class_id),
			dist, entity->x, entity->y, entity->z) << std::endl;
			
		if (entity->hp_percent < 100) {
			std::cout << fmt::format("    HP: {}%", entity->hp_percent) << std::endl;
		}
	}
}

void EverQuest::ZoneProcessWearChange(const EQ::Net::Packet &p)
{
	// WearChange indicates equipment changes on entities
	// Titanium Format: spawn_id (2 bytes), material (2 bytes), color (4 bytes), wear_slot_id (1 byte)
	// Total: 9 bytes + 2 byte opcode = 11 bytes

	if (p.Length() != 11) {
		LOG_WARN(MOD_ENTITY, "WearChange packet wrong size: {} bytes (expected 11)", p.Length());
		return;
	}

	// Skip opcode (2 bytes), then parse struct
	uint16_t spawn_id = p.GetUInt16(2);      // offset 0 in struct, 2 in packet
	uint16_t material = p.GetUInt16(4);      // offset 2 in struct, 4 in packet
	uint32_t color = p.GetUInt32(6);         // offset 4 in struct, 6 in packet
	uint8_t wear_slot = p.GetUInt8(10);      // offset 8 in struct, 10 in packet

	LOG_DEBUG(MOD_ENTITY, "Equipment change for spawn_id={}: slot {} material {} color {:08X}",
		spawn_id, wear_slot, material, color);

	// Update the entity's equipment data
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		Entity& entity = it->second;

		// Update equipment slot if valid (slots 0-8 map to equipment array)
		if (wear_slot < 9) {
			entity.equipment[wear_slot] = material;
			entity.equipment_tint[wear_slot] = color;
		}

		// Publish EntityAppearanceChanged event (D17b: bridge is sole path to renderer)
		if (m_bridge) {
			eqt::state::EntityAppearanceChangedData data;
			data.spawnId = spawn_id;
			data.raceId = entity.race_id;
			data.gender = entity.gender;
			data.isPlayer = (spawn_id == m_my_spawn_id);
			data.face = entity.face;
			data.haircolor = entity.haircolor;
			data.hairstyle = entity.hairstyle;
			data.beardcolor = entity.beardcolor;
			data.beard = entity.beard;
			data.texture = entity.equip_chest2;
			data.helm = entity.helm;
			for (int j = 0; j < 9; j++) {
				data.equipment[j] = entity.equipment[j];
				data.equipmentTint[j] = entity.equipment_tint[j];
			}
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityAppearanceChanged, std::move(data)));
		}
	}
}

void EverQuest::UpdatePlayerAppearanceFromInventory()
{
	if (!m_bridge || !m_inventory_manager) {
		return;
	}

	// Find the player entity
	auto it = m_entities.find(m_my_spawn_id);
	if (it == m_entities.end()) {
		LOG_WARN(MOD_ENTITY, "UpdatePlayerAppearanceFromInventory: player entity {} not found", m_my_spawn_id);
		return;
	}

	Entity& entity = it->second;

	// Build appearance data from inventory items (D17b: bridge is sole path to renderer)
	eqt::state::EntityAppearanceChangedData data;
	data.spawnId = m_my_spawn_id;
	data.raceId = entity.race_id;
	data.gender = entity.gender;
	data.isPlayer = true;
	data.face = entity.face;
	data.haircolor = entity.haircolor;
	data.hairstyle = entity.hairstyle;
	data.beardcolor = entity.beardcolor;
	data.beard = entity.beard;
	data.texture = entity.equip_chest2;
	data.helm = entity.helm;

	// Initialize equipment arrays from entity (fallback)
	for (int i = 0; i < 9; i++) {
		data.equipment[i] = entity.equipment[i];
		data.equipmentTint[i] = entity.equipment_tint[i];
	}

	// Mapping from inventory slots to equipment appearance indices
	// Note: EQ equipment indices are: 0=Head, 1=Chest, 2=Arms, 3=Wrist, 4=Hands, 5=Legs, 6=Feet, 7=Primary, 8=Secondary
	struct SlotMapping {
		int16_t inventorySlot;
		int equipSlotIndex;
	};
	const SlotMapping mappings[] = {
		{ eqt::inventory::SLOT_HEAD, 0 },      // Head
		{ eqt::inventory::SLOT_CHEST, 1 },     // Chest
		{ eqt::inventory::SLOT_ARMS, 2 },      // Arms
		{ eqt::inventory::SLOT_WRIST1, 3 },    // Wrist (use first wrist slot)
		{ eqt::inventory::SLOT_HANDS, 4 },     // Hands
		{ eqt::inventory::SLOT_LEGS, 5 },      // Legs
		{ eqt::inventory::SLOT_FEET, 6 },      // Feet
		{ eqt::inventory::SLOT_PRIMARY, 7 },   // Primary
		{ eqt::inventory::SLOT_SECONDARY, 8 }, // Secondary
	};

	LOG_DEBUG(MOD_INVENTORY, "UpdatePlayerAppearanceFromInventory: Building appearance from inventory");
	for (const auto& mapping : mappings) {
		const auto* item = m_inventory_manager->getItem(mapping.inventorySlot);
		if (item) {
			// Body armor: use material for texture variant and color for tint
			if (mapping.equipSlotIndex < 7) {
				data.equipment[mapping.equipSlotIndex] = item->material;
				data.equipmentTint[mapping.equipSlotIndex] = item->color;
			} else {
				// Weapons: use item ID for model lookup
				data.equipment[mapping.equipSlotIndex] = item->itemId;
			}
		} else {
			// No item in slot - clear the appearance slot
			data.equipment[mapping.equipSlotIndex] = 0;
			data.equipmentTint[mapping.equipSlotIndex] = 0;
		}
	}

	m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::EntityAppearanceChanged, std::move(data)));

	LOG_DEBUG(MOD_INVENTORY, "UpdatePlayerAppearanceFromInventory: Updated player appearance");
}

void EverQuest::ZoneProcessIllusion(const EQ::Net::Packet &p)
{
	// Illusion packet indicates race/appearance changes from illusion spells
	// Packet format: opcode(2) + Illusion_Struct(168)

	if (p.Length() < 2 + sizeof(EQT::Illusion_Struct)) {
		LOG_WARN(MOD_ENTITY, "Illusion packet too small: {} bytes (expected {})",
		         p.Length(), 2 + sizeof(EQT::Illusion_Struct));
		return;
	}

	const EQT::Illusion_Struct* illusion = reinterpret_cast<const EQT::Illusion_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	uint32_t spawn_id = illusion->spawnid;
	int32_t new_race = illusion->race;
	uint8_t new_gender = illusion->gender;
	uint8_t new_texture = illusion->texture;
	uint8_t new_helm = illusion->helmtexture;
	float new_size = illusion->size;

	auto it = m_entities.find(static_cast<uint16_t>(spawn_id));
	if (it == m_entities.end()) {
		LOG_DEBUG(MOD_ENTITY, "Illusion for unknown entity {}", spawn_id);
		return;
	}

	Entity& entity = it->second;
	std::string entity_name = entity.name;

	LOG_INFO(MOD_ENTITY, "{} (ID: {}) illusioned to race {} gender {} texture {} helm {} size {:.2f}",
	         entity_name, spawn_id, new_race, new_gender, new_texture, new_helm, new_size);

	// Update entity data
	entity.race_id = static_cast<uint16_t>(new_race);
	entity.gender = new_gender;
	entity.equip_chest2 = new_texture;
	entity.helm = new_helm;
	entity.size = new_size;

	// Copy additional appearance fields if present
	entity.face = illusion->face;
	entity.hairstyle = illusion->hairstyle;
	entity.haircolor = illusion->haircolor;
	entity.beard = illusion->beard;
	entity.beardcolor = illusion->beardcolor;

	// Publish EntityAppearanceChanged event (D17b: bridge is sole path to renderer)
	if (m_bridge) {
		eqt::state::EntityAppearanceChangedData data;
		data.spawnId = static_cast<uint16_t>(spawn_id);
		data.raceId = entity.race_id;
		data.gender = entity.gender;
		data.isPlayer = (spawn_id == m_my_spawn_id);
		data.face = entity.face;
		data.haircolor = entity.haircolor;
		data.hairstyle = entity.hairstyle;
		data.beardcolor = entity.beardcolor;
		data.beard = entity.beard;
		data.texture = entity.equip_chest2;
		data.helm = entity.helm;
		for (int j = 0; j < 9; j++) {
			data.equipment[j] = entity.equipment[j];
			data.equipmentTint[j] = entity.equipment_tint[j];
		}
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::EntityAppearanceChanged, std::move(data)));
	}
}

// Helper function to determine door type from door name
#ifdef WITH_AUDIO
static EQT::Audio::DoorType getDoorTypeFromName(const std::string& name) {
	// Convert to lowercase for comparison
	std::string lowerName = name;
	std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

	// Check for secret doors
	if (lowerName.find("secret") != std::string::npos ||
	    lowerName.find("hidden") != std::string::npos) {
		return EQT::Audio::DoorType::Secret;
	}

	// Check for metal doors/gates
	if (lowerName.find("gate") != std::string::npos ||
	    lowerName.find("iron") != std::string::npos ||
	    lowerName.find("metal") != std::string::npos ||
	    lowerName.find("grate") != std::string::npos ||
	    lowerName.find("portcullis") != std::string::npos ||
	    lowerName.find("bars") != std::string::npos) {
		return EQT::Audio::DoorType::Metal;
	}

	// Check for stone doors
	if (lowerName.find("stone") != std::string::npos ||
	    lowerName.find("rock") != std::string::npos ||
	    lowerName.find("boulder") != std::string::npos ||
	    lowerName.find("crypt") != std::string::npos ||
	    lowerName.find("tomb") != std::string::npos) {
		return EQT::Audio::DoorType::Stone;
	}

	// Check for sliding doors
	if (lowerName.find("slide") != std::string::npos ||
	    lowerName.find("panel") != std::string::npos) {
		return EQT::Audio::DoorType::Sliding;
	}

	// Default to wood for most doors
	return EQT::Audio::DoorType::Wood;
}
#endif

void EverQuest::ZoneProcessMoveDoor(const EQ::Net::Packet &p)
{
	// MoveDoor indicates door animations (opening/closing)
	// Packet format: opcode(2) + MoveDoor_Struct(2)
	// action: 0x02 = close (go to closed visual state)
	//         0x03 = open (go to open visual state)

	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "MoveDoor packet too small: {} bytes", p.Length());
		return;
	}

	uint8_t door_id = p.GetUInt8(2);
	uint8_t action = p.GetUInt8(3);

	// Update door state in game state manager
	const EQT::DoorState* door = m_door_state_manager.getDoor(door_id);
	if (door) {
		bool is_open = (action == 0x03);
		m_door_state_manager.setDoorState(door_id, is_open);

		// Check if this was a user-initiated door click
		bool user_initiated = (m_pending_door_clicks.erase(door_id) > 0);

		// Log at debug level 2+ for all doors, or level 1+ for user-initiated
		if (s_debug_level >= 2 || (user_initiated && s_debug_level >= 1)) {
			LOG_DEBUG(MOD_ENTITY, "Door {} {}{}", door_id,
			          is_open ? "opened" : "closed",
			          user_initiated ? " (user)" : "");
		}

#ifdef WITH_AUDIO
		// Play door sound only if player is close to the door
		if (m_audio_manager) {
			glm::vec3 doorPos(door->x, door->y, door->z);
			glm::vec3 playerPos(m_x, m_y, m_z);
			float distance = glm::distance(doorPos, playerPos);

			// Only play door sounds within 10 units
			constexpr float DOOR_SOUND_MAX_DISTANCE = 10.0f;
			if (distance <= DOOR_SOUND_MAX_DISTANCE) {
				EQT::Audio::DoorType doorType = getDoorTypeFromName(door->name);
				uint32_t soundId = EQT::Audio::DoorSounds::getDoorSound(doorType, is_open);
				m_audio_manager->playSound(soundId, doorPos);
			}
		}
#endif

		// Publish DoorStateChanged event to bridge
		if (m_bridge) {
			eqt::state::DoorStateChangedData data;
			data.doorId = door_id;
			data.isOpen = is_open;
			data.userInitiated = user_initiated;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::DoorStateChanged, std::move(data)));
		}
	} else {
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_ENTITY, "MoveDoor for unknown door {}", door_id);
		}
	}
}



void EverQuest::ZoneProcessPlayerStateAdd(const EQ::Net::Packet &p)
{
	// PlayerStateAdd adds a state/buff to a player
	// This could be buffs, debuffs, or other state changes

	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "PlayerStateAdd packet too small: {} bytes", p.Length());
		return;
	}
	
	// The exact structure depends on the state being added (buff/debuff/state change)
	// Parse what we can from the packet
	if (s_debug_level >= 2 && p.Length() >= 6) {
		uint16_t stateType = p.GetUInt16(2);
		uint16_t stateValue = p.GetUInt16(4);
		LOG_DEBUG(MOD_ZONE, "[PlayerStateAdd] State added: type={:#06x}, value={:#06x}, size={} bytes",
			stateType, stateValue, p.Length());
	}
}

void EverQuest::ZoneProcessDeath(const EQ::Net::Packet &p)
{
	// Death packet indicates an entity has died
	// Death_Struct layout (all uint32):
	//   spawn_id, killer_id, corpseid, bindzoneid, spell_id, attack_skill, damage, unknown

	if (p.Length() < 30) {  // Need at least through damage field
		LOG_WARN(MOD_ENTITY, "Death packet too small: {} bytes", p.Length());
		return;
	}

	// Parse death information - Death_Struct uses uint32 fields
	// Offsets include 2-byte opcode prefix
	uint32_t victim_id = p.GetUInt32(2);    // spawn_id - Who died
	uint32_t killer_id = p.GetUInt32(6);    // killer_id - Who killed them
	// uint32_t corpse_id = p.GetUInt32(10);  // corpseid (unused)
	// uint32_t bind_zone = p.GetUInt32(14);  // bindzoneid (unused)
	uint32_t spell_id = p.GetUInt32(18);    // spell_id - Spell that killed (0 if melee)
	// uint32_t attack_skill = p.GetUInt32(22);  // attack_skill (unused)
	uint32_t damage = p.GetUInt32(26);      // damage - Final damage amount
	
	// Get entity names if we have them
	std::string victim_name = "Unknown";
	std::string killer_name = "Unknown";
	
	// Special case: killer_id 0 means the player character killed it
	if (killer_id == 0) {
		killer_id = m_my_spawn_id;
		killer_name = m_character;
	} else {
		auto killer_it = m_entities.find(killer_id);
		if (killer_it != m_entities.end()) {
			killer_name = killer_it->second.name;
		}
	}
	
	auto victim_it = m_entities.find(victim_id);
	if (victim_it != m_entities.end()) {
		victim_name = victim_it->second.name;
		// Mark entity as dead (0 HP)
		victim_it->second.hp_percent = 0;
		victim_it->second.is_corpse = true;  // Mark as corpse

#ifdef WITH_AUDIO
		// Play death sound at victim's position
		if (m_audio_manager) {
			m_audio_manager->playSound(EQT::Audio::SoundId::DEATH,
				glm::vec3(victim_it->second.x, victim_it->second.y, victim_it->second.z));
		}
#endif

		// Trigger death animation via bridge (D17b: bridge is sole path to renderer)
		if (m_bridge) {
			eqt::state::EntityDeathAnimationData data;
			data.spawnId = victim_id;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityDeathAnimation, std::move(data)));
		}
	}

	if (s_debug_level >= 1) {
		// spell_id of 0, 0xFFFF (65535), or 0xFFFFFFFF means no spell was used
		bool hasValidSpell = spell_id > 0 && spell_id != 0xFFFF && spell_id != 0xFFFFFFFF;
		if (hasValidSpell) {
			LOG_INFO(MOD_COMBAT, "{} ({}) was killed by {} ({}) for {} damage (spell: {})",
				victim_name, victim_id, killer_name, killer_id, damage, spell_id);
		} else {
			LOG_INFO(MOD_COMBAT, "{} ({}) was killed by {} ({}) for {} damage",
				victim_name, victim_id, killer_name, killer_id, damage);
		}
	}

	// Add combat chat messages for deaths
	std::string victim_display = EQT::toDisplayName(victim_name);
	std::string killer_display = EQT::toDisplayName(killer_name);
	if (killer_id == m_my_spawn_id) {
		// Player killed something
		AddChatCombatMessage(fmt::format("You hit {} for {} points of damage.", victim_display, damage), true);
		AddChatCombatMessage(fmt::format("You have slain {}!", victim_display), true);
	} else if (victim_id == m_my_spawn_id) {
		// Player was killed
		AddChatCombatMessage(fmt::format("{} hit you for {} points of damage.", killer_display, damage), true);
		AddChatCombatMessage(fmt::format("You have been slain by {}!", killer_display), true);
	} else if (!victim_display.empty() && !killer_display.empty()) {
		// Observing a kill
		AddChatCombatMessage(fmt::format("{} hits {} for {} points of damage.", killer_display, victim_display, damage), false);
		AddChatCombatMessage(fmt::format("{} has been slain by {}!", victim_display, killer_display), false);
	}

	// Track the last entity we killed for SimpleMessage substitution
	// Store the display name now before it gets modified to include "'s corpse"
	if (killer_id == m_my_spawn_id) {
		m_last_slain_entity_name = EQT::toDisplayName(victim_name);
		m_game_state.combat().setLastSlainEntityName(m_last_slain_entity_name);  // Phase 7.6
	}

	// Notify combat manager if this was our target
	if (m_combat_manager && victim_id == m_combat_manager->GetTargetId()) {
		// Target died - disable auto-attack
		LOG_DEBUG(MOD_COMBAT, "Our combat target {} died, disabling auto-attack", victim_id);
		m_combat_manager->DisableAutoAttack();
	}
	
	// If we died, log it prominently
	if (victim_id == m_my_spawn_id) {
		std::cout << "YOU HAVE BEEN SLAIN!" << std::endl;
		// Close vendor window if open (can't shop while dead)
		if (IsVendorWindowOpen()) {
			LOG_DEBUG(MOD_INVENTORY, "Closed vendor window due to player death");
			CloseVendorWindow();
		}

		// Cancel trade if we died
		if (m_trade_manager && m_trade_manager->isTrading()) {
			LOG_DEBUG(MOD_MAIN, "Player died during trade, canceling trade");
			m_trade_manager->cancelTrade();
		}
	}

	// Cancel trade if trade partner died
	if (m_trade_manager && m_trade_manager->isTrading() &&
		m_trade_manager->getPartnerSpawnId() == victim_id) {
		LOG_DEBUG(MOD_MAIN, "Trade partner died, canceling trade");
		m_trade_manager->cancelTrade();
		AddChatSystemMessage("Trade cancelled - partner died");
	}

	// If the vendor NPC died, close vendor window
	if (m_vendor_npc_id != 0 && m_vendor_npc_id == victim_id) {
		LOG_DEBUG(MOD_INVENTORY, "Closed vendor window due to vendor death");
		CloseVendorWindow();
	}
}

void EverQuest::ZoneProcessPlayerStateRemove(const EQ::Net::Packet &p)
{
	// PlayerStateRemove removes a state/buff from a player
	// This is the counterpart to PlayerStateAdd
	
	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "PlayerStateRemove packet too small: {} bytes", p.Length());
		return;
	}
	
	// The exact structure depends on the state being removed (buff/debuff/state change)
	// Parse what we can from the packet
	if (s_debug_level >= 2 && p.Length() >= 6) {
		uint16_t stateType = p.GetUInt16(2);
		uint16_t stateValue = p.GetUInt16(4);
		LOG_DEBUG(MOD_ZONE, "[PlayerStateRemove] State removed: type={:#06x}, value={:#06x}, size={} bytes",
			stateType, stateValue, p.Length());
	}
}

void EverQuest::ZoneProcessStamina(const EQ::Net::Packet &p)
{
	// Stamina (endurance) update packet
	// Format: spawn_id (2 bytes), current_stamina (4 bytes), max_stamina (4 bytes)
	
	if (p.Length() < 10) {
		LOG_WARN(MOD_ENTITY, "Stamina packet too small: {} bytes", p.Length());
		return;
	}
	
	uint16_t spawn_id = p.GetUInt16(2);
	uint32_t current_stamina = p.GetUInt32(4);
	uint32_t max_stamina = p.GetUInt32(8);
	
	LOG_DEBUG(MOD_ENTITY, "Stamina update: spawn_id={}, current={}, max={}",
		spawn_id, current_stamina, max_stamina);
	
	// Store stamina values if this is for our character
	if (spawn_id == m_my_spawn_id) {
		// TODO: Store stamina values as member variables if needed
	}
}

void EverQuest::ZoneProcessZonePlayerToBind(const EQ::Net::Packet &p)
{
	// ZonePlayerToBind is sent when the player dies and needs to respawn at bind point
	// Uses ZonePlayerToBind_Struct (variable length):
	// bind_zone_id (2), bind_instance_id (2), x (4), y (4), z (4), heading (4), zone_name[] (variable)
	// Minimum size: 2 opcode + 20 fixed = 22 bytes

	LOG_INFO(MOD_MAIN, "You are being sent to your bind point...");

	if (p.Length() < 22) {  // 2 bytes opcode + 20 bytes minimum struct
		LOG_WARN(MOD_MAIN, "ZonePlayerToBind packet too small: {} bytes (need at least 22)", p.Length());
		// Fall back to stored bind point
		LOG_DEBUG(MOD_MAIN, "Using stored bind point: zone {} at ({:.2f}, {:.2f}, {:.2f})",
			m_bind_zone_id, m_bind_x, m_bind_y, m_bind_z);
		return;
	}

	// Parse ZonePlayerToBind_Struct
	uint16_t target_zone_id = p.GetUInt16(2);
	uint16_t instance_id = p.GetUInt16(4);
	float zone_x = p.GetFloat(6);   // Server x
	float zone_y = p.GetFloat(10);  // Server y
	float zone_z = p.GetFloat(14);
	float zone_heading = p.GetFloat(18);

	// Extract zone_name if present (starts at offset 22)
	std::string zone_name;
	if (p.Length() > 22) {
		const char* name_ptr = static_cast<const char*>(p.Data()) + 22;
		size_t max_len = p.Length() - 22;
		zone_name = std::string(name_ptr, strnlen(name_ptr, max_len));
	}

	// Swap x/y for our coordinate system (server sends x,y but we use y,x internally)
	float bind_x = zone_y;
	float bind_y = zone_x;
	float bind_z = zone_z;
	float bind_heading = zone_heading;

	LOG_DEBUG(MOD_MAIN, "ZonePlayerToBind: zone={} instance={} pos=({:.2f}, {:.2f}, {:.2f}) heading={:.1f} name='{}'",
		target_zone_id, instance_id, bind_x, bind_y, bind_z, bind_heading, zone_name);

	// Store pending zone info
	m_pending_zone_id = target_zone_id;
	m_pending_zone_x = bind_x;
	m_pending_zone_y = bind_y;
	m_pending_zone_z = bind_z;
	m_pending_zone_heading = bind_heading;

	// Check if we're staying in the same zone or going to a different zone
	bool is_same_zone = (target_zone_id == m_current_zone_id) || (target_zone_id == 0);

	if (is_same_zone) {
		LOG_DEBUG(MOD_MAIN, "Same-zone respawn to ({:.2f}, {:.2f}, {:.2f})", bind_x, bind_y, bind_z);
	} else {
		LOG_DEBUG(MOD_MAIN, "Cross-zone respawn: current zone {} -> bind zone {}",
			m_current_zone_id, target_zone_id);
	}

	// Client must respond with OP_ZoneChange to initiate the zone change
	// The server will then send OP_ZoneChange back with success=1
	// For same-zone deaths, the server still expects this handshake
	RequestZoneChange(target_zone_id, bind_x, bind_y, bind_z, bind_heading);
}

void EverQuest::ZoneProcessLevelUpdate(const EQ::Net::Packet &p)
{
	// LevelUpdate_Struct: level (4), level_old (4), exp (4) = 12 bytes
	// Sent when player levels up or down (death exp loss can cause level loss)

	if (p.Length() < 14) {  // 2 bytes opcode + 12 bytes struct
		LOG_WARN(MOD_MAIN, "LevelUpdate packet too small: {} bytes (need 14)", p.Length());
		return;
	}

	uint32_t new_level = p.GetUInt32(2);
	uint32_t old_level = p.GetUInt32(6);
	uint32_t exp = p.GetUInt32(10);

	LOG_DEBUG(MOD_MAIN, "LevelUpdate: level {} -> {}, exp={}", old_level, new_level, exp);

	// Update player level
	m_level = static_cast<uint8_t>(new_level);

	// Update entity level if we have a spawn ID
	if (m_my_spawn_id != 0) {
		auto it = m_entities.find(m_my_spawn_id);
		if (it != m_entities.end()) {
			it->second.level = m_level;
		}
	}

	// Update game state
	m_game_state.player().setLevel(m_level);

	if (new_level > old_level) {
		LOG_INFO(MOD_MAIN, "LEVEL UP! You are now level {}!", new_level);
	} else if (new_level < old_level) {
		LOG_INFO(MOD_MAIN, "You have lost a level. You are now level {}.", new_level);
	}
}

void EverQuest::ZoneProcessRequestClientZoneChange(const EQ::Net::Packet &p)
{
	// Server is requesting that we initiate a zone change
	// Uses RequestClientZoneChange_Struct (24 bytes)

	if (p.Length() < 26) {  // 2 bytes opcode + 24 bytes struct
		LOG_WARN(MOD_ZONE, "RequestClientZoneChange packet too small: {} bytes", p.Length());
		return;
	}

	// Parse RequestClientZoneChange_Struct
	uint16_t zone_id = p.GetUInt16(2);
	uint16_t instance_id = p.GetUInt16(4);
	float zone_y = p.GetFloat(6);   // Server sends y first
	float zone_x = p.GetFloat(10);  // Then x
	float zone_z = p.GetFloat(14);
	float heading = p.GetFloat(18);
	uint32_t type = p.GetUInt32(22);

	// Swap x/y for our coordinate system
	float target_x = zone_y;
	float target_y = zone_x;
	float target_z = zone_z;

	LOG_INFO(MOD_ZONE, "Server requests zone change: zone={} instance={} pos=({:.1f}, {:.1f}, {:.1f}) heading={:.1f} type={}",
		zone_id, instance_id, target_x, target_y, target_z, heading, type);

	// Respond with OP_ZoneChange to initiate the actual zone change
	// The server will then respond with OP_ZoneChange with success=1
	RequestZoneChange(zone_id, target_x, target_y, target_z, heading);
}

void EverQuest::ZoneProcessZoneChange(const EQ::Net::Packet &p)
{
	// Server response to our zone change request (or server-initiated zone change)
	// Uses ZoneChange_Struct (88 bytes)

	if (p.Length() < 90) {  // 2 bytes opcode + 88 bytes struct
		LOG_WARN(MOD_ZONE, "ZoneChange packet too small: {} bytes", p.Length());
		return;
	}

	// Parse ZoneChange_Struct
	char char_name[65] = {0};
	memcpy(char_name, static_cast<const char*>(p.Data()) + 2, 64);
	uint16_t zone_id = p.GetUInt16(66);
	uint16_t instance_id = p.GetUInt16(68);
	float zone_y = p.GetFloat(70);  // Server sends y first
	float zone_x = p.GetFloat(74);  // Then x
	float zone_z = p.GetFloat(78);
	uint32_t zone_reason = p.GetUInt32(82);
	int32_t success = p.GetInt32(86);

	// Swap x/y for our coordinate system
	float target_x = zone_y;
	float target_y = zone_x;
	float target_z = zone_z;

	LOG_DEBUG(MOD_ZONE, "Zone change response: zone={} instance={} pos=({:.1f}, {:.1f}, {:.1f}) reason={} success={}",
		zone_id, instance_id, target_x, target_y, target_z, zone_reason, success);

	// Check success code
	// success == 1 means approved
	// success != 1 means denied (various error codes)
	if (success != 1) {
		LOG_WARN(MOD_ZONE, "Zone change DENIED (success={})", success);
		m_zone_change_requested = false;
		m_zone_line_triggered = false;
		m_pending_zone_id = 0;
		return;
	}

	LOG_DEBUG(MOD_ZONE, "Zone change APPROVED to zone {} at ({:.1f}, {:.1f}, {:.1f})",
		zone_id, target_x, target_y, target_z);

	// Store the target coordinates for the new zone
	m_pending_zone_id = zone_id;
	m_pending_zone_x = target_x;
	m_pending_zone_y = target_y;
	m_pending_zone_z = target_z;

	// Zone-to-zone transition flow:
	// 1. Disconnect from current zone server
	// 2. Reconnect to world server (with zoning=1 flag)
	// 3. World server sends ZoneServerInfo for new zone
	// 4. Connect to new zone server
	// 5. Send WorldComplete, world disconnects (normal)

	// Close vendor window if open (we're leaving the zone)
	if (IsVendorWindowOpen()) {
		LOG_DEBUG(MOD_INVENTORY, "Closed vendor window due to zone change");
		CloseVendorWindow();
	}

	// IMPORTANT: We cannot directly call DisconnectFromZone() here because we're
	// inside a packet callback. Destroying the connection manager from within its
	// own callback causes issues with libuv handle cleanup.
	// Instead, we set a flag and let UpdateMovement() handle the disconnect safely.
	m_zone_change_approved = true;
	LOG_TRACE(MOD_ZONE, "Zone change approved, will process disconnect on next update");
}

void EverQuest::RequestZoneChange(uint16_t zone_id, float x, float y, float z, float heading)
{
	if (!IsFullyZonedIn() || !m_zone_connection) {
		LOG_WARN(MOD_ZONE, "Cannot request zone change - not fully zoned in");
		return;
	}

	if (m_zone_change_requested) {
		LOG_DEBUG(MOD_ZONE, "Zone change already pending, ignoring duplicate request");
		return;
	}

	LOG_DEBUG(MOD_ZONE, "Requesting zone change to zone {} at ({:.1f}, {:.1f}, {:.1f})",
		zone_id, x, y, z);

	// Build ZoneChange_Struct packet
	// The packet is 90 bytes (2 opcode + 88 struct)
	EQ::Net::DynamicPacket p;
	p.Resize(90);  // 2 bytes opcode + 88 bytes ZoneChange_Struct
	memset(p.Data(), 0, 90);

	p.PutUInt16(0, HC_OP_ZoneChange);

	// char_name[64] at offset 2
	size_t name_len = m_character.length();
	if (name_len > 63) name_len = 63;
	memcpy((char*)p.Data() + 2, m_character.c_str(), name_len);

	// zone_id at offset 66
	p.PutUInt16(66, zone_id);

	// instance_id at offset 68 (0 for non-instanced zones)
	p.PutUInt16(68, 0);

	// Server expects Y, X, Z order (opposite of our internal order)
	// Our internal: m_x = server_y, m_y = server_x
	// So to send to server: offset 70 = x (our y), offset 74 = y (our x)
	p.PutFloat(70, y);  // Server's y_pos = our x
	p.PutFloat(74, x);  // Server's x_pos = our y
	p.PutFloat(78, z);

	// zone_reason at offset 82
	// 0 = ZoneSolicited (server-requested)
	// 1 = ZoneUnsolicited (client-initiated, like zone line)
	p.PutUInt32(82, 1);  // ZoneUnsolicited - we're initiating this

	// success at offset 86
	// 0 = request (we're asking)
	// 1 = response (server's reply)
	p.PutInt32(86, 0);  // This is a request

	DumpPacket("C->S", HC_OP_ZoneChange, p);

	if (SafeQueueZonePacket(p)) {
		m_zone_change_requested = true;
		LOG_DEBUG(MOD_ZONE, "Zone change request sent to server");
	} else {
		LOG_ERROR(MOD_ZONE, "Failed to send zone change request");
	}
}

void EverQuest::CleanupZone()
{
	LOG_DEBUG(MOD_ZONE, "Cleaning up current zone state");

	// Stop the update loop first - this must happen before zone transition
	// so that ZoneProcessClientUpdate() can restart it for the new zone
	StopUpdateLoop();

	SetLoadingPhase(LoadingPhase::DISCONNECTED, "Leaving zone...");

#ifdef EQT_HAS_GRAPHICS
	// D20e1: Zone is no longer ready - show loading screen via bridge
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ZoneUnloading,
			eqt::state::ZoneUnloadingData{}));
	}
#endif

	// Stop any movement
	StopMovement();
	m_is_moving = false;
	m_current_path.clear();
	m_current_path_index = 0;
	m_follow_target.clear();
	m_in_combat_movement = false;
	m_combat_target.clear();

	// Phase 7.6: Sync combat state reset to GameState
	m_game_state.combat().setInCombatMovement(false);
	m_game_state.combat().clearCombatTarget();

	// Clear entity map completely - server will re-send all entity data for the new zone
	// (including the player with a fresh spawn_id)
	m_entities.clear();
	m_game_state.entities().clear();

	// Clear door data
	m_door_state_manager.clear();

	// Clear pet state (pet won't exist in new zone)
	if (m_pet_spawn_id != 0) {
		m_pet_spawn_id = 0;
		std::fill(std::begin(m_pet_button_states), std::end(m_pet_button_states), false);
		// Clear pet buffs
		if (m_buff_manager) {
			m_buff_manager->clearPetBuffs();
		}
		OnPetRemoved();
	}

	// Clear world objects (forges, looms, groundspawns)
	ClearWorldObjects();

	// Clear pathfinding, BSP, and zone source data
	m_zone_bsp_tree.reset();
	m_zone_wld.reset();
	m_zone_archive.reset();
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::NavmeshChanged,
			eqt::state::NavmeshChangedData{nullptr}));
	}
	m_pathfinder.reset();

	// Clear collision map via bridge, then reset zone map
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CollisionMapChanged,
			eqt::state::CollisionMapChangedData{nullptr}));
	}
	m_zone_map.reset();

	// Clear zone lines
	m_zone_lines.reset();

	// Reset zone line detection state
	m_zone_line_triggered = false;
	m_zone_change_requested = false;

#ifdef EQT_HAS_GRAPHICS
	// D20e3: Unload zone graphics via bridge
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::RendererCommand,
			eqt::state::RendererCommandData{"/unloadzone_internal"}));
	}
	// Player entity will need to be recreated on renderer after zoning
	m_player_graphics_entity_pending = true;
#endif

	// Reset zone connection state flags
	m_zone_connected = false;
	m_game_state.world().setZoneConnected(false);
	m_zone_session_established = false;
	m_zone_entry_sent = false;
	m_weather_received = false;
	m_req_new_zone_sent = false;
	m_new_zone_received = false;
	m_aa_table_sent = false;
	m_update_aa_sent = false;
	m_tributes_sent = false;
	m_guild_tributes_sent = false;
	m_req_client_spawn_sent = false;
	m_spawn_appearance_sent = false;
	m_exp_zonein_sent = false;
	m_send_exp_zonein_received = false;
	m_server_filter_sent = false;
	m_client_ready_sent = false;
	m_client_spawned = false;

	// Reset AA/tribute tracking
	m_aa_table_count = 0;
	m_tribute_count = 0;
	m_guild_tribute_count = 0;

	// Clear movement history
	m_movement_history.clear();
	m_last_movement_history_send = 0;

	// Reset zone name and ID (both local and WorldState)
	m_current_zone_name.clear();
	m_current_zone_id = 0;
	m_game_state.world().resetZoneState();

	LOG_DEBUG(MOD_ZONE, "Zone cleanup complete");
}

void EverQuest::DisconnectFromZone()
{
	LOG_DEBUG(MOD_ZONE, "Disconnecting from current zone server");

	// Cleanup zone state first
	CleanupZone();

	// Disable reconnect on the zone connection before disconnecting
	if (m_zone_connection_manager) {
		m_zone_connection_manager->OnConnectionStateChange(
			std::bind(&EverQuest::ZoneOnStatusChangeReconnectDisabled, this,
				std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	}

	// Close zone connection
	if (m_zone_connection) {
		m_zone_connection->Close();
		m_zone_connection.reset();
	}

	// Reset zone connection manager (will be recreated when connecting to new zone)
	m_zone_connection_manager.reset();

	LOG_DEBUG(MOD_ZONE, "Disconnected from zone server");
}

void EverQuest::ProcessDeferredZoneChange()
{
	// This method is called from UpdateMovement, which is outside of any callback context.
	// This ensures safe destruction of connection managers.
	LOG_DEBUG(MOD_ZONE, "Processing deferred zone change");

	LOG_TRACE(MOD_ZONE, "Step 1: Disconnecting from current zone");
	// NOTE: DisconnectFromZone() calls CleanupZone() which does unloadZone() and
	// sets DISCONNECTED phase. Main thread still owns GL at this point.
	DisconnectFromZone();
	LOG_TRACE(MOD_ZONE, "Step 1 complete: Zone disconnected");

#ifdef EQT_HAS_GRAPHICS
	// D20e2: Signal Application to start a new loading thread for the re-zone.
	// Application checks m_zone_load_requested in its render loop.
	m_zone_load_requested = true;
#endif

	// Reconnect to world server to get new zone info
	if (!m_world_server_host.empty()) {
		LOG_TRACE(MOD_ZONE, "Step 2: Creating new world connection manager");

		SetLoadingPhase(LoadingPhase::WORLD_CONNECTING);

		// Reset world connection state flags so we go through the full handshake again
		m_world_ready = false;
		m_enter_world_sent = false;

		// Reset old connections first
		m_world_connection.reset();
		LOG_TRACE(MOD_ZONE, "Step 2a: Old world connection reset");

		m_world_connection_manager.reset(new EQ::Net::DaybreakConnectionManager(std::make_unique<EQ::Net::PosixUdpTransport>()));
		LOG_TRACE(MOD_ZONE, "Step 2b: New world connection manager created");

		m_world_connection_manager->OnNewConnection(std::bind(&EverQuest::WorldOnNewConnection, this, std::placeholders::_1));
		m_world_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::WorldOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		m_world_connection_manager->OnPacketRecv(std::bind(&EverQuest::WorldOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));
		LOG_TRACE(MOD_ZONE, "Step 2c: Callbacks registered");

		LOG_DEBUG(MOD_ZONE, "Step 3: Connecting to world server at {}:9000", m_world_server_host);
		m_world_connection_manager->Connect(m_world_server_host, 9000);
		LOG_TRACE(MOD_ZONE, "Step 3 complete: Connect() called, waiting for callbacks");
	} else {
		LOG_ERROR(MOD_ZONE, "No world server address stored for reconnection!");
	}

	LOG_TRACE(MOD_ZONE, "ProcessDeferredZoneChange returning");
}

// Pathfinding implementation
void EverQuest::LoadPathfinder(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_DEBUG(MOD_MAIN, "LoadPathfinder: Zone name is empty, skipping");
		return;
	}
	
	LOG_DEBUG(MOD_MAIN, "LoadPathfinder: Loading pathfinder for zone '{}'", zone_name);

	// Release previous pathfinder if any - clear renderer's reference via bridge
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::NavmeshChanged,
			eqt::state::NavmeshChangedData{nullptr}));
	}
	m_pathfinder.reset();
	m_current_path.clear();
	m_current_path_index = 0;
	
	try {
		// Use the IPathfinder factory method to load the navigation mesh
		m_pathfinder.reset(IPathfinder::Load(zone_name, m_navmesh_path));
		
		if (m_pathfinder) {
			// Check if it's actually a PathfinderNavmesh vs PathfinderNull
			// This is a bit of a hack but helps us understand what type we got
			PathfinderOptions test_opts;
			bool partial = false, stuck = false;
			auto test_path = m_pathfinder->FindPath(
				glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), partial, stuck, test_opts);

			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Loaded pathfinder for zone: {} (type: {})",
					zone_name, test_path.empty() ? "NavMesh" : "Null");
			}

#if defined(EQT_HAS_NAVMESH)
			// Pass navmesh to renderer via bridge for visualization overlay
			if (m_bridge) {
				PathfinderNavmesh* navmesh = dynamic_cast<PathfinderNavmesh*>(m_pathfinder.get());
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::NavmeshChanged,
					eqt::state::NavmeshChangedData{navmesh}));
			}
#endif
		} else {
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "No navigation mesh available for zone: {}", zone_name);
			}
		}
	} catch (const std::exception& e) {
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Failed to load navigation mesh for {}: {}", zone_name, e.what());
		}
	}
}

bool EverQuest::FindPath(float start_x, float start_y, float start_z, float end_x, float end_y, float end_z)
{
	LOG_DEBUG(MOD_MAIN, "FindPath called: from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", start_x, start_y, start_z, end_x, end_y, end_z);
	
	if (!m_pathfinder) {
		LOG_DEBUG(MOD_MAIN, "FindPath: No pathfinder object available (m_pathfinder is null)");
		return false;
	}
	
	// Clear previous path
	m_current_path.clear();
	m_current_path_index = 0;
	
	// Set up pathfinding options
	PathfinderOptions opts;
	opts.smooth_path = true;
	opts.step_size = 10.0f; // Distance between waypoints
	opts.offset = 5.0f; // Height offset for character
	
	// Find the path
	bool partial = false;
	bool stuck = false;
	LOG_DEBUG(MOD_MAIN, "FindPath: Calling m_pathfinder->FindPath()...");
	IPathfinder::IPath path = m_pathfinder->FindPath(
		glm::vec3(start_x, start_y, start_z),
		glm::vec3(end_x, end_y, end_z),
		partial,
		stuck,
		opts
	);
	LOG_DEBUG(MOD_PATHFIND, "FindPath: Result - path size: {}, partial: {}, stuck: {}",
		path.size(), partial, stuck);

	if (path.empty()) {
		LOG_DEBUG(MOD_PATHFIND, "No path found from ({:.1f}, {:.1f}, {:.1f}) to ({:.1f}, {:.1f}, {:.1f})",
			start_x, start_y, start_z, end_x, end_y, end_z);
		if (partial) {
			LOG_DEBUG(MOD_PATHFIND, "  (Partial path available)");
		}
		if (stuck) {
			LOG_DEBUG(MOD_PATHFIND, "  (Path leads back to start - stuck)");
		}
		return false;
	}

	// Convert path nodes to vec3 waypoints
	for (const auto& node : path) {
		// Skip teleport nodes for now
		if (!node.teleport) {
			m_current_path.push_back(node.pos);
		}
	}

	LOG_DEBUG(MOD_PATHFIND, "Found path with {} waypoints", m_current_path.size());
	if (ShouldLog(MOD_PATHFIND, LOG_TRACE)) {
		for (size_t i = 0; i < m_current_path.size() && i < 5; i++) {
			const auto& pos = m_current_path[i];
			LOG_TRACE(MOD_PATHFIND, "  Waypoint {}: ({:.1f}, {:.1f}, {:.1f})",
				i, pos.x, pos.y, pos.z);
		}
		if (m_current_path.size() > 5) {
			LOG_TRACE(MOD_PATHFIND, "  ...");
		}
	}
	
	return true;
}

void EverQuest::MoveToWithPath(float x, float y, float z)
{
	if (!IsFullyZonedIn()) {
		LOG_ERROR(MOD_PATHFIND, "Not in zone yet");
		return;
	}
	
	// If pathfinding is disabled or not available, use direct movement
	if (!m_use_pathfinding || !m_pathfinder) {
		MoveTo(x, y, z);
		return;
	}
	
	// Find path from current position to target
	if (FindPath(m_x, m_y, m_z, x, y, z)) {
		// Face the final destination immediately
		float new_heading = CalculateHeading(m_x, m_y, x, y);
		if (std::abs(new_heading - m_heading) > 0.1f) {
			m_heading = new_heading;
			SendPositionUpdate();
		}
		
		// Start following the path
		m_current_path_index = 0;
		FollowPath();
	} else {
		// Fallback to direct movement if pathfinding fails
		LOG_WARN(MOD_PATHFIND, "Pathfinding failed, using direct movement");
		MoveTo(x, y, z);
	}
}

void EverQuest::FollowPath()
{
	if (m_current_path.empty() || m_current_path_index >= m_current_path.size()) {
		StopMovement();
		return;
	}
	
	// Skip waypoints that are too close to current position (within 1 unit)
	while (m_current_path_index < m_current_path.size()) {
		const auto& waypoint = m_current_path[m_current_path_index];
		float dist = CalculateDistance2D(m_x, m_y, waypoint.x, waypoint.y);
		
		if (dist > 1.0f) {
			// This waypoint is far enough, move to it
			MoveTo(waypoint.x, waypoint.y, waypoint.z);
			break;
		} else {
			// Skip this waypoint, it's too close
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Skipping waypoint {} at ({:.1f},{:.1f},{:.1f}) - too close (dist={:.1f})", m_current_path_index, waypoint.x, waypoint.y, waypoint.z, dist);
			}
			m_current_path_index++;
		}
	}
	
	// If we've gone through all waypoints, stop
	if (m_current_path_index >= m_current_path.size()) {
		StopMovement();
		return;
	}
	
	// The UpdateMovement function will handle checking when we reach waypoints
}

void EverQuest::LoadZoneMap(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_DEBUG(MOD_MAIN, "LoadZoneMap: Zone name is empty, skipping");
		return;
	}
	
	LOG_DEBUG(MOD_MAIN, "LoadZoneMap: Loading map for zone '{}'", zone_name);

	// Clear collision map via bridge, then reset zone map
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CollisionMapChanged,
			eqt::state::CollisionMapChangedData{nullptr}));
	}
	m_zone_map.reset();
	
	// Use custom maps path if set, otherwise try common locations
	std::string maps_path = m_maps_path;
	if (maps_path.empty()) {
		// Try to use the same path as navmesh
		if (!m_navmesh_path.empty()) {
			// Navigate from nav directory to maps directory
			size_t nav_pos = m_navmesh_path.find("/nav");
			if (nav_pos != std::string::npos) {
				maps_path = m_navmesh_path.substr(0, nav_pos);
			}
		}
		
		// Fallback to default location (current directory)
		if (maps_path.empty()) {
			maps_path = "./maps";
		}
	}
	
	LOG_DEBUG(MOD_MAIN, "LoadZoneMap: Using maps path: {}", maps_path);
	
	m_zone_map.reset(HCMap::LoadMapFile(zone_name, maps_path));

	if (!m_zone_map) {
		LOG_WARN(MOD_MAP, "Failed to load map for zone: {}", zone_name);
	}

	// Publish CollisionMapChanged event to bridge
	if (m_bridge) {
		eqt::state::CollisionMapChangedData data;
		data.map = m_zone_map.get();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CollisionMapChanged, std::move(data)));
	}
}

void EverQuest::LoadZoneLines(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_DEBUG(MOD_MAIN, "LoadZoneLines: Zone name is empty, skipping");
		return;
	}

	LOG_DEBUG(MOD_MAIN, "LoadZoneLines: Loading zone lines for '{}'", zone_name);

	// Create new ZoneLines instance
	m_zone_lines = std::make_unique<EQT::ZoneLines>();

	// Reset zone line detection state (both local and WorldState)
	m_zone_line_triggered = false;
	m_zone_change_requested = false;
	m_pending_zone_id = 0;
	m_last_zone_check_x = m_x;
	m_last_zone_check_y = m_y;
	m_last_zone_check_z = m_z;
	m_game_state.world().setZoneLineTriggered(false);
	m_game_state.world().setZoneChangeRequested(false);
	m_game_state.world().clearPendingZone();
	m_game_state.world().setLastZoneCheckPosition(m_x, m_y, m_z);

	// Try to load zone lines from the zone's S3D file
	if (!m_eq_client_path.empty()) {
		if (m_zone_lines->loadFromZone(zone_name, m_eq_client_path)) {
			LOG_DEBUG(MOD_MAP, "LoadZoneLines: Loaded {} zone lines from WLD", m_zone_lines->getZoneLineCount());
		} else {
			LOG_DEBUG(MOD_MAP, "LoadZoneLines: No zone lines found in WLD for '{}'", zone_name);
		}
	} else {
		LOG_DEBUG(MOD_MAP, "LoadZoneLines: No EQ client path set, zone lines from WLD unavailable");
	}

	// NOTE: Zone line visualization boxes are sent to the renderer in LoadZoneGraphics()
	// AFTER zone loading completes. If done here, the boxes get cleared when
	// unloadZone() runs at the start of zone loading.
}

void EverQuest::LoadZoneSources(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: Zone name is empty");
		RequestQuit();
		return;
	}

	if (m_eq_client_path.empty()) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: EQ client path is empty");
		RequestQuit();
		return;
	}

	auto startTime = std::chrono::steady_clock::now();

	// Clear previous zone sources
	m_zone_archive.reset();
	m_zone_wld.reset();
	m_zone_bsp_tree.reset();

	// Open zone S3D archive
	std::string archivePath = m_eq_client_path;
	if (!archivePath.empty() && archivePath.back() != '/' && archivePath.back() != '\\')
		archivePath += '/';
	archivePath += zone_name + ".s3d";

	m_zone_archive = std::make_shared<EQT::Graphics::PfsArchive>();
	if (!m_zone_archive->open(archivePath)) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: Failed to open zone archive: {}", archivePath);
		m_zone_archive.reset();
		RequestQuit();
		return;
	}

	// Find main WLD file (skip _obj, _chr, lights.wld)
	std::vector<std::string> wldFiles;
	m_zone_archive->getFilenames(".wld", wldFiles);

	std::string mainWld;
	for (const auto& wld : wldFiles) {
		if (wld.find("_obj") == std::string::npos &&
		    wld.find("_chr") == std::string::npos &&
		    wld != "objects.wld" &&
		    wld != "lights.wld") {
			mainWld = wld;
			break;
		}
	}
	if (mainWld.empty() && !wldFiles.empty()) {
		mainWld = wldFiles[0];
	}
	if (mainWld.empty()) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: No WLD file found in archive: {}", archivePath);
		m_zone_archive.reset();
		RequestQuit();
		return;
	}

	// Parse WLD
	m_zone_wld = std::make_shared<EQT::Graphics::WldLoader>();
	if (!m_zone_wld->parseFromOpenArchive(*m_zone_archive, mainWld)) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: Failed to parse WLD '{}' from archive: {}", mainWld, archivePath);
		m_zone_wld.reset();
		m_zone_archive.reset();
		RequestQuit();
		return;
	}

	// Extract BSP tree
	m_zone_bsp_tree = m_zone_wld->getBspTree();
	if (!m_zone_bsp_tree || m_zone_bsp_tree->regions.empty()) {
		LOG_FATAL(MOD_MAIN, "LoadZoneSources: BSP tree is null or has no regions for zone '{}'", zone_name);
		m_zone_bsp_tree.reset();
		m_zone_wld.reset();
		m_zone_archive.reset();
		RequestQuit();
		return;
	}

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - startTime).count();
	LOG_INFO(MOD_MAIN, "LoadZoneSources: Loaded zone '{}' in {}ms ({} BSP regions, {} nodes)",
	         zone_name, elapsed,
	         m_zone_bsp_tree->regions.size(), m_zone_bsp_tree->nodes.size());
}

void EverQuest::CheckZoneLine()
{
	// Skip if not fully zoned in or no zone lines loaded
	if (!IsFullyZonedIn() || !m_zone_lines || !m_zone_lines->hasZoneLines()) {
		return;
	}

	// Skip if a zone change is already in progress
	if (m_zone_change_requested) {
		return;
	}

	// Debug: periodically log zone line check status
	static int checkCount = 0;
	checkCount++;
	if (checkCount % 100 == 0) {
		LOG_TRACE(MOD_ZONE, "Zone line check #{}: pos=({:.1f}, {:.1f}, {:.1f}) zone_lines={} regions={}",
			checkCount, m_x, m_y, m_z,
			m_zone_lines->hasZoneLines() ? "yes" : "no",
			m_zone_lines->getZoneLineCount());
	}

	// Skip if we haven't moved since last check
	constexpr float MIN_MOVE_THRESHOLD = 0.1f;
	float dx = m_x - m_last_zone_check_x;
	float dy = m_y - m_last_zone_check_y;
	float dz = m_z - m_last_zone_check_z;
	float dist_sq = dx * dx + dy * dy + dz * dz;

	if (dist_sq < MIN_MOVE_THRESHOLD * MIN_MOVE_THRESHOLD) {
		return;
	}

	// Update last checked position
	float old_x = m_last_zone_check_x;
	float old_y = m_last_zone_check_y;
	float old_z = m_last_zone_check_z;
	m_last_zone_check_x = m_x;
	m_last_zone_check_y = m_y;
	m_last_zone_check_z = m_z;

	// Check if currently in a zone line region
	// The extracted zone lines use BSP coordinates which need to be checked against player position
	// m_x = server_y, m_y = server_x (from spawn struct)
	// Testing with player position directly (coordinates may need adjustment based on zone)
	float check_x = m_x;
	float check_y = m_y;
	float check_z = m_z;

	// Debug: log position being checked
	static int posLogCount = 0;
	posLogCount++;
	if (posLogCount % 100 == 1) {
		LOG_DEBUG(MOD_ZONE, "CheckZoneLine: m_x={:.1f} m_y={:.1f} m_z={:.1f} (server: x={:.1f} y={:.1f})",
			m_x, m_y, m_z, m_y, m_x);
	}

	EQT::ZoneLineResult result = m_zone_lines->checkPosition(check_x, check_y, check_z, check_x, check_y, check_z);

	// Debug output for every position check (throttled)
	if (checkCount % 50 == 0) {
		LOG_TRACE(MOD_ZONE, "Zone line check: check=({:.1f}, {:.1f}, {:.1f}) m_pos=({:.1f}, {:.1f}, {:.1f}) -> isZoneLine={}",
			check_x, check_y, check_z, m_x, m_y, m_z, result.isZoneLine ? "YES" : "no");
	}

	if (result.isZoneLine) {
		// We're in a zone line
		auto now = std::chrono::steady_clock::now();

		// Skip actual zone trigger if zoning is disabled
		if (!m_zoning_enabled) {
			return;
		}

		// Debounce: require 500ms cooldown after a trigger before allowing another
		if (m_zone_line_triggered) {
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_zone_line_trigger_time);
			if (elapsed.count() < 500) {
				// Still in cooldown period, ignore
				return;
			}
		}

		// Check if this is a reference-type zone line that needs server lookup
		if (result.needsServerLookup) {
			// Log warning - the server should have sent us the zone point data
			LOG_WARN(MOD_ZONE, "Zone line triggered but missing zone point data for index {}", result.zonePointIndex);
			// Still trigger the zone line, the server will handle it
		}

		// Trigger zone line!
		m_zone_line_triggered = true;
		m_zone_line_trigger_time = now;
		m_pending_zone_id = result.targetZoneId;
		m_pending_zone_x = result.targetX;
		m_pending_zone_y = result.targetY;
		m_pending_zone_z = result.targetZ;
		m_pending_zone_heading = result.heading;

		// Phase 7.3: Also update WorldState
		m_game_state.world().setZoneLineTriggered(true);
		m_game_state.world().setZoneLineTriggerTime(now);
		m_game_state.world().setPendingZone(m_pending_zone_id, m_pending_zone_x, m_pending_zone_y, m_pending_zone_z, m_pending_zone_heading);

		LOG_INFO(MOD_ZONE, "Zone line triggered! Target zone: {}, coords: ({:.1f}, {:.1f}, {:.1f}), heading: {:.1f}",
		        m_pending_zone_id, m_pending_zone_x, m_pending_zone_y, m_pending_zone_z, m_pending_zone_heading);

		// Send zone change request to server
		RequestZoneChange(m_pending_zone_id, m_pending_zone_x, m_pending_zone_y, m_pending_zone_z, m_pending_zone_heading);
	} else {
		// Not in a zone line - clear the triggered flag if we were previously in one
		if (m_zone_line_triggered) {
			// Check if enough time has passed and we're no longer in a zone line
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_zone_line_trigger_time);
			if (elapsed.count() >= 500) {
				// Cooldown expired and we're not in a zone line, reset state
				m_zone_line_triggered = false;
				m_pending_zone_id = 0;
				// Phase 7.3: Also update WorldState
				m_game_state.world().setZoneLineTriggered(false);
				m_game_state.world().clearPendingZone();
			}
		}
	}
}

float EverQuest::GetBestZ(float x, float y, float z)
{
	if (!m_zone_map) {
		return z; // Return current Z if no map loaded
	}
	
	glm::vec3 pos(x, y, z);
	glm::vec3 result;
	float best_z = m_zone_map->FindBestZ(pos, &result);

	// BEST_Z_INVALID from hc_map.h is a sentinel value indicating no valid ground found
	if (best_z == (float)BEST_Z_INVALID) {
		return z; // Return current Z if no valid ground found
	}
	
	return best_z;
}

void EverQuest::FixZ()
{
	if (!m_zone_map) {
		// Log once when following starts
		static bool logged_no_map = false;
		if (!logged_no_map && !m_follow_target.empty()) {
			LOG_DEBUG(MOD_MAIN, "FixZ: No zone map loaded - Z-height fixing disabled");
			logged_no_map = true;
		}
		return;
	}
	
	// Get the best Z for our current position
	float new_z = GetBestZ(m_x, m_y, m_z);
	float z_diff = new_z - m_z;
	float abs_diff = std::abs(z_diff);
	
	// Always log Z-height info when following for debugging
	if (!m_follow_target.empty() && s_debug_level >= 1) {
		// Find the entity we're following to compare Z values
		Entity* target = nullptr;
		for (const auto& pair : m_entities) {
			if (pair.second.name == m_follow_target) {
				target = const_cast<Entity*>(&pair.second);
				break;
			}
		}
		
		if (target) {
			LOG_DEBUG(MOD_MAIN, "FixZ: Following {} - My Z: {:.2f}, Target Z: {:.2f}, Map thinks Z should be: {:.2f}, Diff: {:.2f}", m_follow_target, m_z, target->z, new_z, z_diff);
		}
	}
	
	// Only update if the difference is significant
	if (abs_diff > 1.5f && abs_diff < 20.0f) {
		// Much more conservative Z adjustment to prevent bouncing
		float adjustment;
		
		if (abs_diff < 3.0f) {
			// Small differences: move only 20% of the way
			adjustment = z_diff * 0.2f;
		} else if (abs_diff < 5.0f) {
			// Medium differences: move 15% of the way
			adjustment = z_diff * 0.15f;
		} else {
			// Large differences: move 10% of the way, capped at 1.0 units
			adjustment = z_diff * 0.1f;
			if (std::abs(adjustment) > 1.0f) {
				adjustment = (adjustment > 0) ? 1.0f : -1.0f;
			}
		}
		
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "FixZ: Adjusting Z from {:.2f} to {:.2f} (adjustment: {:.2f}, diff was: {:.2f})", 
				m_z, m_z + adjustment, adjustment, z_diff);
		}
		
		m_z += adjustment;
	} else if (s_debug_level >= 2 && abs_diff > 0.01f) {
		LOG_DEBUG(MOD_MAIN, "FixZ: Z difference too small to adjust: {:.2f} (current: {:.2f}, best: {:.2f})", 
			z_diff, m_z, new_z);
	}
}

void EverQuest::SetMovementMode(MovementMode mode)
{
	if (m_movement_mode == mode) {
		return;
	}
	
	m_movement_mode = mode;

	if (m_bridge) {
		eqt::state::PlayerMovementModeChangedData data;
		data.mode = static_cast<uint8_t>(mode);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMovementModeChanged, std::move(data)));
	}

	// Update movement speed based on mode
	switch (mode) {
		case MOVE_MODE_RUN:
			m_move_speed = DEFAULT_RUN_SPEED;
			break;
		case MOVE_MODE_WALK:
			m_move_speed = DEFAULT_WALK_SPEED;
			break;
		case MOVE_MODE_SNEAK:
			m_move_speed = DEFAULT_WALK_SPEED * 0.6f;  // Sneak is 60% of walk speed
			SetSneak(true);
			break;
	}
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Movement mode changed to {} (speed: {:.1f})", 
			mode == MOVE_MODE_RUN ? "RUN" : mode == MOVE_MODE_WALK ? "WALK" : "SNEAK", 
			m_move_speed);
	}
}

void EverQuest::SetPositionState(PositionState state)
{
	if (m_position_state == state) {
		return;
	}
	
	m_position_state = state;

	if (m_bridge) {
		eqt::state::PlayerPositionStateChangedData data;
		data.state = static_cast<uint8_t>(state);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerPositionStateChanged, std::move(data)));
	}

	switch (state) {
		case POS_STANDING:
			SendSpawnAppearance(AT_ANIMATION, 100);  // Animation::Standing
			if (m_is_moving) {
				StopMovement();
			}
			// Cancel camp timer if standing up
			if (m_is_camping) {
				CancelCamp();
			}
			break;
		case POS_SITTING:
			SendSpawnAppearance(AT_ANIMATION, 110);  // Animation::Sitting
			if (m_is_moving) {
				StopMovement();
			}
			break;
		case POS_CROUCHING:
			SendSpawnAppearance(AT_ANIMATION, 111);  // Animation::Crouching
			break;
		case POS_FEIGN_DEATH:
			SendSpawnAppearance(AT_ANIMATION, ANIM_LYING);  // Use lying animation for feign death
			if (m_is_moving) {
				StopMovement();
			}
			break;
		case POS_DEAD:
			SendSpawnAppearance(AT_DIE, 0);  // Use AT_DIE for actual death
			if (m_is_moving) {
				StopMovement();
			}
			break;
	}
	
	if (s_debug_level >= 1) {
		const char* state_names[] = {"STANDING", "SITTING", "CROUCHING", "FEIGN_DEATH", "DEAD"};
		LOG_DEBUG(MOD_MAIN, "Position state changed to {}", state_names[state]);
	}
}

void EverQuest::SendSpawnAppearance(uint16_t type, uint32_t value)
{
	if (!IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(10);  // SpawnAppearance packet size: 2 bytes opcode + 8 bytes struct
	
	p.PutUInt16(0, HC_OP_SpawnAppearance);
	p.PutUInt16(2, m_my_spawn_id);  // spawn_id
	p.PutUInt16(4, type);            // type
	p.PutUInt32(6, value);           // parameter
	// Removed the extra 4 bytes that were at offset 10
	
	m_zone_connection->QueuePacket(p);
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent SpawnAppearance: spawn_id={}, type={}, value={}", 
			m_my_spawn_id, type, value);
	}
}

void EverQuest::SendAnimation(uint8_t animation_id, uint8_t animation_speed)
{
	if (!IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(6);  // Animation packet size: 2 bytes opcode + 4 bytes data
	
	p.PutUInt16(0, HC_OP_Emote);          // opcode (OP_Animation)
	p.PutUInt16(2, m_my_spawn_id);        // spawn_id
	p.PutUInt8(4, animation_speed);       // animation speed (usually 10)
	p.PutUInt8(5, animation_id);          // animation ID
	
	m_zone_connection->QueuePacket(p, 0, false);  // Send unreliable
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent Animation: spawn_id={}, speed={}, animation_id={}",
			m_my_spawn_id, animation_speed, animation_id);
	}
}

void EverQuest::SendClickDoor(uint8_t door_id, uint32_t item_id)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot click door - not fully zoned in");
		return;
	}

	// Check if door exists
	const EQT::DoorState* door = m_door_state_manager.getDoor(door_id);
	if (!door) {
		LOG_WARN(MOD_ENTITY, "Attempted to click unknown door {}", door_id);
		return;
	}

	LOG_INFO(MOD_ENTITY, "Clicking door {} ('{}')", door_id, door->name);

	// Track this as a user-initiated door click
	m_pending_door_clicks.insert(door_id);

	// Build ClickDoor packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickDoor_Struct));

	p.PutUInt16(0, HC_OP_ClickDoor);
	p.PutUInt8(2, door_id);           // doorid
	p.PutUInt8(3, 0);                 // unknown001
	p.PutUInt8(4, 0);                 // unknown002
	p.PutUInt8(5, 0);                 // unknown003
	p.PutUInt8(6, 0);                 // picklockskill
	p.PutUInt8(7, 0);                 // unknown005[0]
	p.PutUInt8(8, 0);                 // unknown005[1]
	p.PutUInt8(9, 0);                 // unknown005[2]
	p.PutUInt32(10, item_id);         // item_id (key)
	p.PutUInt16(14, m_my_spawn_id);   // player_id
	p.PutUInt16(16, 0);               // unknown014

	m_zone_connection->QueuePacket(p, 0, true);
}

// ============================================================================
// World Object / Tradeskill Methods
// ============================================================================

void EverQuest::SendClickObject(uint32_t drop_id)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot click object - not fully zoned in");
		return;
	}

	// Check if object exists
	auto it = m_world_objects.find(drop_id);
	if (it == m_world_objects.end()) {
		LOG_WARN(MOD_ENTITY, "Attempted to click unknown object {}", drop_id);
		return;
	}

	LOG_INFO(MOD_ENTITY, "Clicking object {} ('{}') type={} ({})",
		drop_id, it->second.name, it->second.object_type,
		it->second.getTradeskillName());

	// Build ClickObject packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickObject_Struct));

	p.PutUInt16(0, HC_OP_ClickObject);
	p.PutUInt32(2, drop_id);           // drop_id
	p.PutUInt32(6, m_my_spawn_id);     // player_id

	m_zone_connection->QueuePacket(p, 0, true);
}

void EverQuest::SendTradeSkillCombine(int16_t container_slot)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot combine - not fully zoned in");
		return;
	}

	LOG_INFO(MOD_ENTITY, "Sending tradeskill combine for container slot {}", container_slot);

	// Build TradeSkillCombine packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::NewCombine_Struct));

	p.PutUInt16(0, HC_OP_TradeSkillCombine);
	p.PutInt16(2, container_slot);     // container_slot
	p.PutInt16(4, 0);                  // guildtribute_slot (usually 0)

	m_zone_connection->QueuePacket(p, 0, true);
}

void EverQuest::SendCloseContainer(uint32_t drop_id)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot close container - not fully zoned in");
		return;
	}

	LOG_DEBUG(MOD_ENTITY, "Closing tradeskill container {}", drop_id);

	// Build ClickObjectAction packet to close the container
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickObjectAction_Struct));

	p.PutUInt16(0, HC_OP_ClickObjectAction);
	p.PutUInt32(2, m_my_spawn_id);     // player_id
	p.PutUInt32(6, drop_id);           // drop_id
	p.PutUInt32(10, 0);                // open = 0 (closing)
	p.PutUInt32(14, 0);                // type
	p.PutUInt32(18, 0);                // unknown16
	p.PutUInt32(22, 0);                // icon
	p.PutUInt32(26, 0);                // unknown24
	// object_name is 64 bytes of zeros (not needed for close)
	for (int i = 0; i < 64; i++) {
		p.PutUInt8(30 + i, 0);
	}

	m_zone_connection->QueuePacket(p, 0, true);

	// Clear active tradeskill object
	m_active_tradeskill_object_id = 0;
}

const eqt::WorldObject* EverQuest::GetWorldObject(uint32_t drop_id) const
{
	auto it = m_world_objects.find(drop_id);
	if (it != m_world_objects.end()) {
		return &it->second;
	}
	return nullptr;
}

void EverQuest::ClearWorldObjects()
{
	LOG_DEBUG(MOD_ENTITY, "Clearing {} world objects", m_world_objects.size());
	m_world_objects.clear();
	m_active_tradeskill_object_id = 0;
}

void EverQuest::ZoneProcessClickObjectAction(const EQ::Net::Packet &p)
{
	// Parse ClickObjectAction_Struct from packet (skip 2-byte opcode)
	if (p.Length() < 2 + sizeof(EQT::ClickObjectAction_Struct)) {
		LOG_WARN(MOD_ENTITY, "ClickObjectAction packet too small: {} bytes (need {})",
			p.Length(), 2 + sizeof(EQT::ClickObjectAction_Struct));
		return;
	}

	const auto* action = reinterpret_cast<const EQT::ClickObjectAction_Struct*>(p.Data() + 2);

	std::string objectName(action->object_name, strnlen(action->object_name, sizeof(action->object_name)));

	if (action->open == 1) {
		// Server is telling us to open a tradeskill container
		LOG_INFO(MOD_ENTITY, "Opening tradeskill container: drop_id={} type={} icon={} name='{}'",
			action->drop_id, action->type, action->icon, objectName);

		// Store the active tradeskill object ID
		m_active_tradeskill_object_id = action->drop_id;

		// Open tradeskill container via bridge
		if (m_bridge) {
			eqt::state::TradeskillContainerOpenedEvent data;
			data.isWorldObject = true;
			data.objectId = action->drop_id;
			data.inventorySlot = -1;
			data.containerName = objectName;
			data.containerType = static_cast<uint8_t>(action->type);
			data.slotCount = eqt::inventory::WORLD_COUNT;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeskillContainerOpened, std::move(data)));
		}
	} else {
		// Server is confirming container is closed
		LOG_DEBUG(MOD_ENTITY, "Tradeskill container closed: drop_id={}", action->drop_id);
		m_active_tradeskill_object_id = 0;

		// Close tradeskill container via bridge
		if (m_bridge) {
			eqt::state::TradeskillContainerClosedEvent data;
			data.wasWorldObject = true;
			data.objectId = action->drop_id;
			data.inventorySlot = -1;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeskillContainerClosed, std::move(data)));
		}
	}
}

void EverQuest::ZoneProcessTradeSkillCombine(const EQ::Net::Packet &p)
{
	// Server response to a combine attempt
	// The server sends an empty acknowledgment packet (0 bytes payload)
	// Actual results come via separate packets:
	// - OP_FormattedMessage with string_id 339 for success ("You create a <item>!")
	// - OP_ItemPacket for inventory updates
	// - OP_SkillUpdate for skill increases
	// These are already handled in their respective handlers.

	LOG_DEBUG(MOD_ENTITY, "TradeSkillCombine acknowledgment received ({} bytes)", p.Length());
}


void EverQuest::SendJump()
{
	if (!IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(2);  // Jump packet size: 2 bytes opcode only (no data)
	
	p.PutUInt16(0, HC_OP_Jump);  // opcode
	
	m_zone_connection->QueuePacket(p, 0, false);
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_Jump packet (2 bytes)");
	}
}

void EverQuest::SendMovementHistory()
{
	if (!IsFullyZonedIn() || m_movement_history.empty() || !m_zone_connection) {
		return;
	}

	// Additional safety check
	if (!m_zone_connected || !m_zone_connection_manager) {
		LOG_WARN(MOD_ZONE, "SendMovementHistory called with invalid connection state");
		return;
	}

	// Add debugging info about connection state
	LOG_TRACE(MOD_ZONE, "SendMovementHistory called at time {} - connection ptr: {}, connected: {}, history size: {}",
		static_cast<uint32_t>(std::time(nullptr)),
		static_cast<void*>(m_zone_connection.get()),
		m_zone_connected,
		m_movement_history.size());

	// Limit history to last 70 entries (similar to what client sends)
	while (m_movement_history.size() > 70) {
		m_movement_history.pop_front();
	}

	// For unreliable packets, we have a max of ~250 bytes (255 - CRC)
	// Each entry is 17 bytes, plus 2 bytes for opcode
	// So we can fit about 14 entries per unreliable packet: (250 - 2) / 17 = 14.5
	const size_t MAX_ENTRIES_PER_PACKET = 14;
	(void)MAX_ENTRIES_PER_PACKET; // Unused but kept for documentation

	// Real clients send all entries in one packet, so we must send as reliable
	// to match their behavior (they send 67-70 entries = 1139-1190 bytes + 2 = 1141-1192 total)
	size_t packet_size = 2 + (m_movement_history.size() * sizeof(MovementHistoryEntry));

	LOG_TRACE(MOD_ZONE, "Creating OP_FloatListThing packet: {} entries, {} bytes total",
		m_movement_history.size(), packet_size);

	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);

	p.PutUInt16(0, HC_OP_FloatListThing);

	// Pack movement entries
	size_t offset = 2;
	try {
		for (const auto& entry : m_movement_history) {
			// UpdateMovementEntry struct layout:
			// float Y, float X, float Z, uint8 type, uint32 timestamp
			p.PutFloat(offset, entry.y);      // Y first
			p.PutFloat(offset + 4, entry.x);  // then X
			p.PutFloat(offset + 8, entry.z);  // then Z
			p.PutUInt8(offset + 12, entry.type);
			p.PutUInt32(offset + 13, entry.timestamp);
			offset += 17;
		}

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_ZONE, "About to queue OP_FloatListThing as reliable packet");
		}

		// Send as reliable since it exceeds unreliable packet size limit
		if (!SafeQueueZonePacket(p, 0, true)) {  // Send reliable
			LOG_ERROR(MOD_ZONE, "Failed to queue OP_FloatListThing packet - SafeQueueZonePacket returned false");
		} else {
			LOG_TRACE(MOD_ZONE, "Successfully queued OP_FloatListThing with {} movement entries ({} bytes) as reliable",
				m_movement_history.size(), packet_size);
		}
	}
	catch (const std::exception& e) {
		LOG_ERROR(MOD_ZONE, "Exception packing movement history: {}", e.what());
	}
	catch (...) {
		LOG_ERROR(MOD_ZONE, "Unknown exception packing movement history");
	}
}

bool EverQuest::SafeQueueZonePacket(EQ::Net::Packet &p, int stream, bool reliable)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_ZONE, "Attempted to send packet with null zone connection");
		return false;
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
		if (stats.recv_packets == 0 && stats.sent_packets == 0 && m_zone_connected) {
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
		m_zone_connected = false;
		m_game_state.world().setZoneConnected(false);
		return false;
	}
	catch (...) {
		LOG_ERROR(MOD_ZONE, "Unknown exception in SafeQueueZonePacket (opcode={:#06x}, len={}, reliable={})",
			p.GetUInt16(0), p.Length(), reliable);
		m_zone_connection.reset();  // Clear the corrupted connection
		m_zone_connected = false;
		m_game_state.world().setZoneConnected(false);
		return false;
	}
}

void EverQuest::Jump()
{
	if (m_is_jumping) {
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Jump blocked - already jumping");
		}
		return;  // Can't jump while already jumping
	}
	
	// Allow jumping while moving or standing
	m_is_jumping = true;
	m_jump_start_z = m_z;
	m_jump_start_time = std::chrono::steady_clock::now();
	
	// Real clients have a tiny position shift when jumping (to trigger server broadcasts)
	// Add a small random X/Y movement like real clients do
	static int jump_count = 0;
	jump_count++;
	float tiny_shift = (jump_count % 2 == 0) ? -0.04f : 0.04f;  // Alternate direction
	
	// Apply tiny shift to X or Y position (alternate between them)
	if (jump_count % 4 < 2) {
		m_x += tiny_shift;
	} else {
		m_y += tiny_shift;
	}
	
	// Real clients send standing animation before jumping
	SendSpawnAppearance(AT_ANIMATION, ANIM_STANDING);   // 100
	
	// Send OP_Jump packet like real clients do
	SendJump();
	
	// Don't set animation to jump - real clients keep animation at 0 in ClientUpdate
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Jump initiated from z={}", m_z);
	}
	
	// Don't block - let the update loop handle the jump
	// The jump will be processed by UpdateMovement() calls
}

void EverQuest::StartUpdateLoop()
{
	if (m_update_running) {
		return;  // Already running
	}
	
	m_update_running = true;
	m_update_thread = std::thread([this]() {
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Update loop started");
		}

		while (m_update_running && IsFullyZonedIn()) {

			// Note: Network events are processed by the main thread's EventLoop
			// We only handle game logic updates here

			// Update all game systems
			UpdateMovement();

			// Update combat system
			if (m_combat_manager) {
				m_combat_manager->Update();
			}

			// Sleep to maintain ~60 FPS
			// std::this_thread::sleep_for(std::chrono::milliseconds(16));
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			FlushThreadLog();
		}

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Update loop stopped");
		}
	});
}

void EverQuest::StopUpdateLoop()
{
	if (!m_update_running) {
		return;
	}
	
	m_update_running = false;
	if (m_update_thread.joinable()) {
		m_update_thread.join();
	}
}

void EverQuest::UpdateJump()
{
	if (!m_is_jumping) {
		return;
	}
	
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_jump_start_time).count();
	
	// Jump physics: parabolic arc
	// Jump lasts about 1 second, peaks at 0.5 seconds
	const float jump_duration_ms = 1000.0f;
	const float jump_height = 25.0f;  // Maximum height of jump (increased for visibility)
	
	if (elapsed >= jump_duration_ms) {
		// Jump complete
		m_is_jumping = false;
		m_z = m_jump_start_z;  // Return to original height (or use FixZ)
		
		// Return to appropriate animation
		if (m_is_moving) {
			m_animation = (m_move_speed >= WALK_SPEED_THRESHOLD) ? ANIM_RUN : ANIM_WALK;
		} else {
			m_animation = ANIM_STAND;
		}
		
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Jump completed");
		}
	} else {
		// Calculate jump height using parabola
		float t = elapsed / jump_duration_ms;  // 0 to 1
		float height = 4.0f * jump_height * t * (1.0f - t);  // Parabola peaking at t=0.5
		m_z = m_jump_start_z + height;
		
		if (s_debug_level >= 2) {
			static int jump_frame = 0;
			if (jump_frame % 10 == 0) {  // Log every 10th frame
				LOG_DEBUG(MOD_MAIN, "Jump progress: t={:.2f}, height={:.2f}, z={:.2f}", 
					t, height, m_z);
			}
			jump_frame++;
		}
	}
}

void EverQuest::UpdateWaterState()
{
	if (!IsFullyZonedIn()) {
		return;
	}

	WaterState newState = WaterState::NotInWater;
	float waterSurfaceZ = 0.0f;

	// Fully underwater zones - these don't have BSP water regions
	// The entire zone is submerged so we hardcode them here
	bool isFullyUnderwaterZone = (m_current_zone_name == "kedge");

	if (isFullyUnderwaterZone) {
		newState = WaterState::Submerged;
	} else {
		// Normal water detection via BSP regions (D20f3: reads typed game-state BSP tree)
		const auto& bspTree = m_zone_bsp_tree;
		if (bspTree) {
			// Check the region at current player position
			// In EQ, the "head" is approximately 6 units above feet position
			constexpr float HEAD_HEIGHT_OFFSET = 6.0f;
			float headZ = m_z + HEAD_HEIGHT_OFFSET;

			auto region = bspTree->findRegionForPoint(m_x, m_y, m_z);

			if (region) {
				// Check if this region is water
				bool isWaterRegion = false;
				for (auto type : region->regionTypes) {
					if (type == EQT::Graphics::RegionType::Water ||
					    type == EQT::Graphics::RegionType::FreezingWater ||
					    type == EQT::Graphics::RegionType::WaterBlockLOS) {
						isWaterRegion = true;
						break;
					}
				}

				if (isWaterRegion) {
					// Check if head is also in water (determines OnSurface vs Submerged)
					auto headRegion = bspTree->findRegionForPoint(m_x, m_y, headZ);
					bool headInWater = false;
					if (headRegion) {
						for (auto type : headRegion->regionTypes) {
							if (type == EQT::Graphics::RegionType::Water ||
							    type == EQT::Graphics::RegionType::FreezingWater ||
							    type == EQT::Graphics::RegionType::WaterBlockLOS) {
								headInWater = true;
								break;
							}
						}
					}

					if (headInWater) {
						newState = WaterState::Submerged;
					} else {
						newState = WaterState::OnSurface;
						// Estimate water surface Z (head is above water, feet are in)
						waterSurfaceZ = m_z + (HEAD_HEIGHT_OFFSET / 2.0f);
					}
				}
			}
		}
	}

	// Handle state transitions
	if (newState != m_water_state) {
		WaterState oldState = m_water_state;
		m_water_state = newState;
		m_water_surface_z = waterSurfaceZ;

		if (oldState == WaterState::NotInWater && newState != WaterState::NotInWater) {
			OnEnterWater();
		} else if (oldState != WaterState::NotInWater && newState == WaterState::NotInWater) {
			OnExitWater();
		}

		// Publish SwimmingStateChanged event to bridge (only on state change)
		if (m_bridge) {
			float swimSpd = GetSwimSpeed();
			bool lev = false;
			auto me = m_entities.find(m_my_spawn_id);
			if (me != m_entities.end() && me->second.flymode == 2) {
				lev = true;
			}
			eqt::state::SwimmingStateChangedData data;
			data.isSwimming = (newState != WaterState::NotInWater);
			data.swimSpeed = swimSpd;
			data.isLevitating = lev;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::SwimmingStateChanged, std::move(data)));
		}
	}

}

void EverQuest::OnEnterWater()
{
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Entering water at ({:.2f}, {:.2f}, {:.2f})", m_x, m_y, m_z);
	}

	// Cancel any jump in progress
	m_is_jumping = false;

	// Send flymode=3 (water) to server
	SendSpawnAppearance(AT_FLYMODE, 3);

#ifdef WITH_AUDIO
	// Play water entry sound
	if (m_audio_manager) {
		PlaySound(EQT::Audio::WaterSoundIds::WaterIn);
	}
#endif
}

void EverQuest::OnExitWater()
{
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Exiting water at ({:.2f}, {:.2f}, {:.2f})", m_x, m_y, m_z);
	}

	// Check if player is levitating (flymode 2 from buffs)
	// If levitating, keep flymode 2, otherwise reset to 0
	auto myEntity = m_entities.find(m_my_spawn_id);
	uint8_t newFlymode = 0;
	if (myEntity != m_entities.end() && myEntity->second.flymode == 2) {
		newFlymode = 2;  // Keep levitating
	}

	SendSpawnAppearance(AT_FLYMODE, newFlymode);

	m_water_state = WaterState::NotInWater;
}

float EverQuest::GetSwimSpeed() const
{
	// Base swim speed is slower than running
	constexpr float BASE_SWIM_SPEED = 20.0f;

	// Skill bonus: up to 50% at max skill (200)
	uint32_t swimSkill = 0;
	if (m_skill_manager) {
		swimSkill = m_skill_manager->getSkillValue(50);  // Skill ID 50 = Swimming
	}

	// Cap skill at 200 for calculation
	if (swimSkill > 200) swimSkill = 200;

	// Formula: BASE_SPEED * (1.0 + (skill / 200) * 0.5)
	// At skill 0: 20 units/sec
	// At skill 200: 30 units/sec
	float skillBonus = (static_cast<float>(swimSkill) / 200.0f) * 0.5f;
	return BASE_SWIM_SPEED * (1.0f + skillBonus);
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
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Petition);
	packet.PutString(2, text);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Petition submitted");
		AddChatSystemMessage("Your petition has been submitted.");
	}
}

void EverQuest::SendPetitionDelete(uint32_t petition_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_PetitionDelete);
	packet.PutUInt32(2, petition_id);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Petition {} deleted", petition_id);
	}
}

void EverQuest::SetSneak(bool sneak)
{
	if (m_is_sneaking == sneak) {
		return;
	}

	m_is_sneaking = sneak;
	m_game_state.player().setSneaking(sneak);  // Phase 7.8
	SendSpawnAppearance(AT_SNEAK, sneak ? 1 : 0);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sneak status: {}", sneak ? "ON" : "OFF");
	}
}

float EverQuest::GetMovementSpeed() const
{
	// Return current movement speed, which may be modified by buffs, encumbrance, etc.
	return m_move_speed;
}

// Combat-related packet handlers
void EverQuest::ZoneProcessConsider(const EQ::Net::Packet &p)
{
	// Consider response from server
	if (p.Length() < 30) { // 2 opcode + 28 Consider_Struct
		return;
	}

	struct Consider_Struct {
		uint32_t playerid;
		uint32_t targetid;
		uint32_t faction;
		uint32_t level;
		int32_t  cur_hp;
		int32_t  max_hp;
		uint8_t  pvpcon;
		uint8_t  unknown3[3];
	} *con = (Consider_Struct*)((uint8_t*)p.Data() + 2);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Consider: target={}, faction={}, level={}, hp={}/{}",
			con->targetid, con->faction, con->level, con->cur_hp, con->max_hp);
	}

	// Update combat manager with consider info
	if (m_combat_manager) {
		m_combat_manager->ProcessConsiderResponse(con->targetid, con->faction, con->level,
			con->cur_hp, con->max_hp);
	}

	// Display consider message to chat
	std::string target_name = "Unknown";
	auto it = m_entities.find(static_cast<uint16_t>(con->targetid));
	if (it != m_entities.end()) {
		target_name = EQT::toDisplayName(it->second.name);
	}

	// Determine faction standing message
	// Faction values: 1=ally, 2=warmly, 3=kindly, 4=amiably, 5=indifferent,
	//                 6=apprehensive, 7=dubious, 8=threatening, 9=scowling
	std::string faction_msg;
	switch (con->faction) {
		case 1: faction_msg = "regards you as an ally"; break;
		case 2: faction_msg = "looks upon you warmly"; break;
		case 3: faction_msg = "kindly considers you"; break;
		case 4: faction_msg = "judges you amiably"; break;
		case 5: faction_msg = "regards you indifferently"; break;
		case 6: faction_msg = "looks your way apprehensively"; break;
		case 7: faction_msg = "glowers at you dubiously"; break;
		case 8: faction_msg = "threatens you"; break;
		case 9: faction_msg = "scowls at you, ready to attack"; break;
		default: faction_msg = fmt::format("regards you (faction {})", con->faction); break;
	}

	// Determine con color name and display color based on server level value
	std::string con_color;
	switch (con->level) {
		case 2:  con_color = "green"; break;
		case 4:  con_color = "blue"; break;
		case 6:  con_color = "gray"; break;
		case 10: con_color = "light blue"; break;
		case 13: con_color = "red"; break;
		case 15: con_color = "yellow"; break;
		case 18: con_color = "light blue"; break;
		case 20: con_color = "white"; break;
		default: con_color = "white"; break;
	}

	// Format: "a gnoll scout regards you indifferently -- cons green"
	std::string message = fmt::format("{} {} -- cons {}", target_name, faction_msg, con_color);
	// Publish SystemMessage event to bridge
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.message = message;
		data.channelType = static_cast<uint32_t>(eqt::ui::ChatChannel::System);
		data.channelName = "system";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SystemMessage, std::move(data)));
	}
	LOG_INFO(MOD_MAIN, "{}", message);
}

void EverQuest::ZoneProcessAction(const EQ::Net::Packet &p)
{
	// Combat action (attack, spell cast, etc)
	if (p.Length() < 33) { // 2 opcode + 31 Action_Struct
		return;
	}

	// Local struct matching wire format (31 bytes)
	struct ActionPacket_Struct {
		uint16_t target;
		uint16_t source;
		uint16_t level;
		uint32_t instrument_mod;
		float    force;
		float    hit_heading;
		float    hit_pitch;
		uint8_t  type;
		uint16_t unknown23;
		uint16_t unknown25;
		uint16_t spell;
		uint8_t  spell_level;
		uint8_t  effect_flag;
	} __attribute__((packed));

	const ActionPacket_Struct* action = (const ActionPacket_Struct*)((uint8_t*)p.Data() + 2);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Action: source={} -> target={}, type={}, spell={}", action->source, action->target, action->type, action->spell);
	}

	// Convert to EQT::Action_Struct for spell manager
	EQT::Action_Struct spellAction;
	spellAction.target = action->target;
	spellAction.source = action->source;
	spellAction.level = action->level;
	spellAction.instrument_mod = action->instrument_mod;
	spellAction.force = action->force;
	spellAction.hit_heading = action->hit_heading;
	spellAction.hit_pitch = action->hit_pitch;
	spellAction.type = action->type;
	spellAction.spell = action->spell;
	spellAction.level2 = action->spell_level;
	spellAction.effect_flag = action->effect_flag;

	// Pass to spell manager for spell effect handling
	if (m_spell_manager) {
		m_spell_manager->handleAction(spellAction);
	}

	// Note: Attack animations are NOT triggered here - they come from animation updates
	// when the entity initiates an attack. The Action packet is for spell/ability effects.
}

// Helper: Get damage verb based on damage type
// Returns pair: {second_person, third_person} e.g. {"crush", "crushes"}
static std::pair<const char*, const char*> getDamageVerb(uint8_t damage_type) {
	switch (damage_type) {
		case 0:  // Blunt (1HBlunt, 2HBlunt)
			return {"crush", "crushes"};
		case 1:  // Slashing (1HSlashing, 2HSlashing)
			return {"slash", "slashes"};
		case 4:  // Hand-to-hand, Feign Death
			return {"hit", "hits"};
		case 7:  // Archery
			return {"hit", "hits"};
		case 8:  // Backstab
			return {"backstab", "backstabs"};
		case 10: // Bash
			return {"bash", "bashes"};
		case 21: // Dragon Punch
			return {"strike", "strikes"};
		case 23: // Eagle Strike, Tiger Claw
			return {"strike", "strikes"};
		case 30: // Kick, Flying Kick, Round Kick
			return {"kick", "kicks"};
		case 36: // Piercing (1HPiercing, 2HPiercing)
			return {"pierce", "pierces"};
		case 51: // Throwing
			return {"hit", "hits"};
		case 74: // Frenzy
			return {"hit", "hits"};
		default:
			return {"hit", "hits"};
	}
}

void EverQuest::ZoneProcessDamage(const EQ::Net::Packet &p)
{
	LOG_INFO(MOD_COMBAT, "ZoneProcessDamage called, packet length={}", p.Length());

	// Damage notification
	if (p.Length() < 25) { // 2 opcode + 23 CombatDamage_Struct
		LOG_INFO(MOD_COMBAT, "Damage packet too short: {} < 25", p.Length());
		return;
	}
	
	struct CombatDamage_Struct {
		uint16_t target;
		uint16_t source;
		uint8_t  type;
		uint16_t spellid;
		uint32_t damage;
		float    force;
		float    hit_heading;
		float    hit_pitch;
		uint8_t  unknown06;
	} __attribute__((packed)) *dmg = (CombatDamage_Struct*)((uint8_t*)p.Data() + 2);
	
	// Debug: show raw bytes for damage field
	// Copy packed struct fields to local variables for fmt::format
	uint16_t target_id = dmg->target;
	uint16_t source_id = dmg->source;
	uint8_t damage_type = dmg->type;
	uint16_t spell_id = dmg->spellid;
	int32_t damage_amount = dmg->damage;

	// Cancel trade if player takes damage (entering combat)
	if (target_id == m_my_spawn_id && damage_amount > 0 &&
		m_trade_manager && m_trade_manager->isTrading()) {
		LOG_DEBUG(MOD_MAIN, "Player took damage during trade, canceling trade");
		m_trade_manager->cancelTrade();
		AddChatSystemMessage("Trade cancelled - you entered combat");
	}

	if (s_debug_level >= 3) {
		const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + 2;
		std::string hexBytes;
		for (int i = 0; i < 12 && i < static_cast<int>(p.Length()) - 2; ++i) {
			hexBytes += fmt::format("{:02x} ", data[i]);
		}
		LOG_TRACE(MOD_COMBAT, "Damage packet raw bytes (first 12): {}", hexBytes);
		LOG_DEBUG(MOD_COMBAT, "Damage target={} ({:04x}), source={} ({:04x}), type={} ({:02x}), spell={} ({:04x})", target_id, target_id, source_id, source_id, damage_type, damage_type, spell_id, spell_id);
		LOG_TRACE(MOD_COMBAT, "Damage bytes at offset 7: {:02x} {:02x} {:02x} {:02x}", data[7], data[8], data[9], data[10]);
	}

	// Get entity names for combat messages
	std::string target_name;
	std::string source_name;
	auto target_it = m_entities.find(target_id);
	auto source_it = m_entities.find(source_id);
	if (target_it != m_entities.end()) {
		target_name = EQT::toDisplayName(target_it->second.name);
	}
	if (source_it != m_entities.end()) {
		source_name = EQT::toDisplayName(source_it->second.name);
	}

	if (s_debug_level >= 2 || IsTrackedTarget(target_id) || IsTrackedTarget(source_id)) {
		LOG_DEBUG(MOD_COMBAT, "{} -> {} for {} damage (type {})",
			source_name.empty() ? std::to_string(source_id) : source_name,
			target_name.empty() ? std::to_string(target_id) : target_name,
			damage_amount, damage_type);
	}

	// Add combat chat messages
	if (damage_amount > 0 && !target_name.empty()) {
		bool player_is_target = (target_id == m_my_spawn_id);
		bool player_is_source = (source_id == m_my_spawn_id);

		// Get verb based on damage type (crush, slash, pierce, kick, etc.)
		auto [verb_second, verb_third] = getDamageVerb(damage_type);

		std::string msg;
		if (player_is_source) {
			// Player dealt damage
			if (spell_id > 0 && spell_id != 0xFFFF) {
				msg = fmt::format("You hit {} for {} points of non-melee damage.", target_name, damage_amount);
			} else {
				msg = fmt::format("You {} {} for {} points of damage.", verb_second, target_name, damage_amount);
			}
			AddChatCombatMessage(msg, true);
		} else if (player_is_target) {
			// Player took damage
			if (spell_id > 0 && spell_id != 0xFFFF) {
				msg = fmt::format("{} hit you for {} points of non-melee damage.", source_name.empty() ? "Unknown" : source_name, damage_amount);
			} else {
				msg = fmt::format("{} {} YOU for {} points of damage.", source_name.empty() ? "Unknown" : source_name, verb_third, damage_amount);
			}
			AddChatCombatMessage(msg, true);
		} else if (!source_name.empty()) {
			// Observing combat between others
			if (spell_id > 0 && spell_id != 0xFFFF) {
				msg = fmt::format("{} hit {} for {} points of non-melee damage.", source_name, target_name, damage_amount);
			} else {
				msg = fmt::format("{} {} {} for {} points of damage.", source_name, verb_third, target_name, damage_amount);
			}
			AddChatCombatMessage(msg, false);
		}
	} else if (damage_amount == 0 && !target_name.empty()) {
		// Miss - use CombatMiss channel (filterable separately from hits)
		bool player_is_source = (source_id == m_my_spawn_id);
		bool player_is_target = (target_id == m_my_spawn_id);

		// Get verb for miss messages too
		auto [verb_second, verb_third] = getDamageVerb(damage_type);

		std::string msg;
		if (player_is_source) {
			msg = fmt::format("You try to {} {} but miss!", verb_second, target_name);
			AddChatMissMessage(msg);
		} else if (player_is_target && !source_name.empty()) {
			msg = fmt::format("{} tries to {} YOU but misses!", source_name, verb_second);
			AddChatMissMessage(msg);
		} else if (!source_name.empty()) {
			// Observing combat between others - also show misses
			msg = fmt::format("{} tries to {} {} but misses!", source_name, verb_second, target_name);
			AddChatMissMessage(msg);
		}
	}

#ifdef WITH_AUDIO
	// Play combat sounds at target position
	if (m_audio_manager && target_it != m_entities.end()) {
		const Entity& target = target_it->second;
		bool isMelee = (spell_id == 0 || spell_id == 0xFFFF);

		if (damage_amount > 0) {
			if (isMelee) {
				// Melee hit sound
				m_audio_manager->playSound(EQT::Audio::SoundId::MELEE_HIT,
					glm::vec3(target.x, target.y, target.z));
			} else {
				// Spell damage - play spell impact sound
				// TODO: Map specific spells to specific impact sounds
				m_audio_manager->playSound(EQT::Audio::SoundId::SPELL_CAST,
					glm::vec3(target.x, target.y, target.z));
			}
		} else if (isMelee) {
			// Miss - play swing sound
			m_audio_manager->playSound(EQT::Audio::SoundId::MELEE_MISS,
				glm::vec3(target.x, target.y, target.z));
		}
	}
#endif

	// Publish DamageEvent to bridge
	if (m_bridge && damage_amount > 0) {
		eqt::state::DamageEventData data;
		data.sourceId = source_id;
		data.targetId = target_id;
		data.amount = damage_amount;
		data.type = damage_type;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::DamageEvent, std::move(data)));
	}

	// Publish combat animation events via bridge (D17b: bridge is sole path to renderer)
	if (m_bridge) {
		bool isMeleeDamage = (damage_type < 80);

		// Don't animate dead entities
		auto target_entity_it = m_entities.find(target_id);
		bool targetAlive = (target_entity_it != m_entities.end() && target_entity_it->second.hp_percent > 0);

		if (isMeleeDamage && targetAlive) {
			// Calculate damage percentage for animation selection
			float damagePercent = 0.0f;
			if (target_id == m_my_spawn_id && m_max_hp > 0) {
				damagePercent = (static_cast<float>(damage_amount) / static_cast<float>(m_max_hp)) * 100.0f;
			} else if (target_entity_it != m_entities.end()) {
				uint8_t level = target_entity_it->second.level > 0 ? target_entity_it->second.level : 1;
				float estimatedMaxHP = static_cast<float>(level) * 15.0f;
				damagePercent = (static_cast<float>(damage_amount) / estimatedMaxHP) * 100.0f;
			}

			eqt::state::CombatAnimationData data;
			data.sourceId = source_id;
			data.targetId = target_id;
			data.damageType = damage_type;
			data.damageAmount = damage_amount;
			data.damagePercent = static_cast<uint8_t>(damagePercent);
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::CombatAnimation, std::move(data)));

			if (s_debug_level >= 2 || IsTrackedTarget(target_id) || IsTrackedTarget(source_id)) {
				LOG_DEBUG(MOD_COMBAT, "Queued melee anim: source={} target={} skill={} dmg={} pct={:.1f}%",
					source_id, target_id, damage_type, damage_amount, damagePercent);
			}
		} else if (damage_amount > 0 && targetAlive && !isMeleeDamage) {
			// Non-melee damage (spells, DoT, environmental) - play immediately
			float damagePercent = 0.0f;
			if (target_id == m_my_spawn_id && m_max_hp > 0) {
				damagePercent = (static_cast<float>(damage_amount) / static_cast<float>(m_max_hp)) * 100.0f;
			} else if (target_entity_it != m_entities.end()) {
				uint8_t level = target_entity_it->second.level > 0 ? target_entity_it->second.level : 1;
				float estimatedMaxHP = static_cast<float>(level) * 15.0f;
				damagePercent = (static_cast<float>(damage_amount) / estimatedMaxHP) * 100.0f;
			}

			bool isDrowning = (damage_type == 253);
			bool isTrap = (damage_type == 254);
			const char* damageAnim = EQ::getDamageAnimation(damagePercent, isDrowning, isTrap);

			eqt::state::EntityAnimationEventData data;
			data.spawnId = target_id;
			data.animCode = 0;
			data.animName = damageAnim;
			data.loop = false;
			data.playThrough = true;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityAnimationEvent, std::move(data)));
			if (s_debug_level >= 2 || IsTrackedTarget(target_id)) {
				LOG_DEBUG(MOD_COMBAT, "Damage reaction '{}' on target={} (dmg={}, pct={:.1f}%, type={})",
					damageAnim, target_id, damage_amount, damagePercent, damage_type);
			}
		}
	}

	// Check if target died
	if (damage_amount > 0 && target_id != 0) {
		auto it = m_entities.find(target_id);
		if (it != m_entities.end() && it->second.hp_percent == 0) {
			// Target died, might want to loot
			if (m_combat_manager && m_combat_manager->IsAutoAttackEnabled() &&
				target_id == m_combat_manager->GetTargetId()) {
				// Our target died
				if (s_debug_level >= 2 || IsTrackedTarget(target_id)) {
					LOG_DEBUG(MOD_COMBAT, "Target {} ({}) died", target_id, it->second.name);
				}
			}
		}
	}
}

void EverQuest::ZoneProcessMoneyOnCorpse(const EQ::Net::Packet &p)
{
	// Money on corpse packet
	if (p.Length() < 22) {
		return;
	}
	
	// Parse money amounts
	struct MoneyOnCorpse_Struct {
		uint8_t response;   // 0x01 = success
		uint8_t unknown1[3];
		uint32_t platinum;
		uint32_t gold;
		uint32_t silver;
		uint32_t copper;
	} __attribute__((packed)) *money = (MoneyOnCorpse_Struct*)((uint8_t*)p.Data() + 2);

	// Copy packed struct fields to local variables for fmt::format
	uint32_t plat = money->platinum;
	uint32_t gold = money->gold;
	uint32_t silver = money->silver;
	uint32_t copper = money->copper;

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "MoneyOnCorpse: {} platinum, {} gold, {} silver, {} copper\n", plat, gold, silver, copper);
	}
	
	// For now, just log it - money is auto-looted when we loot any item
}

void EverQuest::ZoneProcessLootItem(const EQ::Net::Packet &p)
{
	// Parse the serialized item data
	// The packet contains pipe-delimited item data
	if (p.Length() < 10) {
		return;
	}
	
	// Skip the first 4 bytes (opcode is already removed, but there's a header)
	const char* data = (const char*)p.Data() + 2;
	int data_len = p.Length() - 2;
	
	// Skip the first 4 bytes of the data (unknown header)
	if (data_len > 4) {
		data += 4;
		data_len -= 4;
	}
	
	std::string item_data(data, data_len);
	
	LOG_DEBUG(MOD_INVENTORY, "ProcessLootItem: Received item data, length={}", item_data.length());
	LOG_TRACE(MOD_INVENTORY, "Item data preview: {}",
		item_data.substr(0, std::min(size_t(100), item_data.length())));
	
	// Parse the pipe-delimited fields to extract item slot and name
	std::vector<std::string> fields;
	std::stringstream ss(item_data);
	std::string field;
	while (std::getline(ss, field, '|')) {
		fields.push_back(field);
	}
	
	// The format appears to be: slot|unknown|slot_number|....|item_name|...
	if (fields.size() > 12) {
		try {
			uint32_t slot_num = std::stoi(fields[2]);  // Slot number in loot window
			std::string item_name = fields[11];         // Item name
			
			LOG_DEBUG(MOD_INVENTORY, "Loot window item: slot {} = '{}'", slot_num, item_name);
			
			// Add to combat manager's loot list
			if (m_combat_manager) {
				m_combat_manager->AddLootItem(slot_num);
			}
		} catch (...) {
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Failed to parse loot item data");
			}
		}
	}
	
	// If auto-loot is enabled and we have items, start looting after a delay
	if (m_combat_manager && m_combat_manager->IsAutoLootEnabled() && 
	    m_combat_manager->GetCombatState() == COMBAT_STATE_LOOTING) {
		// The combat manager will handle the actual looting
		m_combat_manager->CheckAutoLoot();
	}
}

void EverQuest::QueuePacket(uint16_t opcode, EQ::Net::DynamicPacket* packet)
{
	if (!m_zone_connection || !packet) {
		return;
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

void EverQuest::StartCombatMovement(uint16_t entity_id)
{
	// Find the entity
	auto it = m_entities.find(entity_id);
	if (it == m_entities.end()) {
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "StartCombatMovement: Entity {} not found", entity_id);
		}
		return;
	}

	const auto& entity = it->second;
	m_combat_target = entity.name;
	// Use previously set combat stop distance, or default to 5.0f
	if (m_combat_stop_distance <= 0.0f) {
		m_combat_stop_distance = 5.0f;
	}
	m_in_combat_movement = true;

	// Phase 7.6: Sync to GameState
	m_game_state.combat().setCombatTarget(entity.name);
	m_game_state.combat().setCombatStopDistance(m_combat_stop_distance);
	m_game_state.combat().setInCombatMovement(true);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Starting combat movement to {} (ID: {})", entity.name, entity_id);
	}

	// Start moving to the target
	MoveToEntityWithinRange(entity.name, m_combat_stop_distance);
}

// Keyboard control methods
void EverQuest::StartMoveForward()
{
	if (!IsFullyZonedIn()) return;

	// Clear any console-based movement (moveto, follow) when user takes manual control
	if (!m_current_path.empty() || m_target_x != 0.0f || m_target_y != 0.0f || m_target_z != 0.0f) {
		m_current_path.clear();
		m_current_path_index = 0;
		m_target_x = m_target_y = m_target_z = 0.0f;
		m_follow_target.clear();
		LOG_DEBUG(MOD_MAIN, "Cleared pathfinding state due to manual movement");
	}

	m_move_forward = true;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting forward movement");
	}
}

void EverQuest::StartMoveBackward()
{
	if (!IsFullyZonedIn()) return;
	m_move_backward = true;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting backward movement");
	}
}

void EverQuest::StartTurnLeft()
{
	if (!IsFullyZonedIn()) return;
	m_turn_left = true;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting left turn");
	}
}

void EverQuest::StartTurnRight()
{
	if (!IsFullyZonedIn()) return;
	m_turn_right = true;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting right turn");
	}
}

void EverQuest::StopMoveForward()
{
	m_move_forward = false;
	if (!m_move_backward && !m_turn_left && !m_turn_right) {
		m_is_moving = false;
		m_animation = ANIM_STAND;
		SendPositionUpdate();  // Send immediate update when stopping
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Stopping all movement");
		}
	}
}

void EverQuest::StopMoveBackward()
{
	m_move_backward = false;
	if (!m_move_forward && !m_turn_left && !m_turn_right) {
		m_is_moving = false;
		m_animation = ANIM_STAND;
		SendPositionUpdate();  // Send immediate update when stopping
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Stopping all movement");
		}
	}
}

void EverQuest::StopTurnLeft()
{
	m_turn_left = false;
	if (!m_move_forward && !m_move_backward && !m_turn_right) {
		m_is_moving = false;
		m_animation = ANIM_STAND;
		SendPositionUpdate();  // Send immediate update when stopping
	}
}

void EverQuest::StopTurnRight()
{
	m_turn_right = false;
	if (!m_move_forward && !m_move_backward && !m_turn_left) {
		m_is_moving = false;
		m_animation = ANIM_STAND;
		SendPositionUpdate();  // Send immediate update when stopping
	}
}

void EverQuest::UpdateKeyboardMovement()
{
	if (!IsFullyZonedIn()) return;
	
	// Handle turning
	const float turn_speed = 90.0f; // degrees per second (4 seconds for full rotation)
	float delta_heading = 0.0f;
	
	if (m_turn_left && !m_turn_right) {
		delta_heading = -turn_speed * 0.5f; // 500ms tick rate
	} else if (m_turn_right && !m_turn_left) {
		delta_heading = turn_speed * 0.5f;
	}
	
	if (delta_heading != 0.0f) {
		m_heading += delta_heading;
		// Normalize heading to 0-360
		while (m_heading < 0.0f) m_heading += 360.0f;
		while (m_heading >= 360.0f) m_heading -= 360.0f;
	}
	
	// Handle forward/backward movement
	float move_distance = 0.0f;
	
	if (m_move_forward && !m_move_backward) {
		move_distance = GetMovementSpeed() * 0.5f; // 500ms tick rate
		m_is_moving = true;
		m_animation = (m_move_speed >= WALK_SPEED_THRESHOLD) ? ANIM_RUN : ANIM_WALK;
	} else if (m_move_backward && !m_move_forward) {
		move_distance = -DEFAULT_WALK_SPEED * 0.5f; // Backward uses walk speed, 500ms tick
		m_is_moving = true;
		m_animation = ANIM_WALK; // Always walk when moving backward
	} else if (!m_move_forward && !m_move_backward && !m_turn_left && !m_turn_right) {
		// Not moving
		if (m_is_moving) {
			m_is_moving = false;
			m_animation = ANIM_STAND;
		}
	}
	
	// Apply movement
	if (move_distance != 0.0f) {
		// Convert heading to radians for calculations
		float heading_rad = m_heading * M_PI / 180.0f;

		// Calculate new position
		// In EQ coordinates: +X = East, +Y = North
		// Heading 0 = North, 90 = East, 180 = South, 270 = West
		float new_x = m_x + move_distance * sin(heading_rad);
		float new_y = m_y + move_distance * cos(heading_rad);

		// Update position
		m_x = new_x;
		m_y = new_y;

		// Fix Z position if we have map data
		FixZ();
	}
}

#ifdef EQT_HAS_GRAPHICS
// Graphics renderer integration methods

void EverQuest::SetEQClientPath(const std::string& path) {
	m_eq_client_path = path;

	// Load EQ string files for message lookups
	LoadStringFiles(path);
}

void EverQuest::SetConstrainedConfig(const EQT::Graphics::ConstrainedRendererConfig& config) {
	m_constrained_config = config;
	m_constrained_preset = EQT::Graphics::ConstrainedRenderingPreset::Custom;
}

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
	m_bridge = bridge;

	// Wire bridge to subsystems for intent pushing
	// WindowManager removed — bridge wiring handled by new UI path
	if (m_inventory_manager) {
		m_inventory_manager->setBridge(m_bridge);
	}
	if (m_spell_manager) {
		m_spell_manager->setBridge(m_bridge);
	}
	if (m_skill_manager) {
		m_skill_manager->setBridge(m_bridge);
	}
	// U07b: Wire hotbar model to bridge
	if (m_hotbar_model) {
		m_hotbar_model->setBridge(m_bridge);
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

	// Set collision map via bridge
	if (m_zone_map && m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CollisionMapChanged,
			eqt::state::CollisionMapChangedData{m_zone_map.get()}));
	}

	// Connect inventory manager to renderer
	if (renderer && m_inventory_manager) {
		renderer->setInventoryManager(m_inventory_manager.get());
	}

	// Setup window callbacks (all empty stubs since D20a/D20b1)
	SetupLootCallbacks();
	SetupVendorCallbacks();
	SetupBankCallbacks();
	SetupTrainerCallbacks();
	SetupTradeWindowCallbacks();
	SetupTradeskillCallbacks();

	// Wire up movement callback — renderer calls this when player moves
	renderer->setMovementCallback([this](const EQT::Graphics::PlayerPositionUpdate& update) {
		OnGraphicsMovement(update);
	});

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
	m_bridge = nullptr;
	m_loading_status_ptr = nullptr;
	m_graphics_initialized = false;
	LOG_DEBUG(MOD_GRAPHICS, "Graphics detached");
}

// D20c1: Pre-render game state tick — spell/buff managers + bridge
// D21a: Game-side tick — runs on game thread (spell/buff updates + intent processing)
void EverQuest::GameTick(float deltaTime) {
	if (!m_graphics_initialized) return;

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

// D21a: Render-side tick — runs on render thread (applies events to renderer)
void EverQuest::PreRenderTick(float /*deltaTime*/) {
	if (!m_graphics_initialized) return;

	// Drain bridge events and apply to renderer
	ProcessBridgeEvents();
}

// D20c1: Post-render tick — audio sync, progressive loading check
void EverQuest::PostRenderTick(float deltaTime) {
	if (!m_graphics_initialized) return;

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
	for (const auto& [id, door] : m_door_state_manager.getAllDoors()) {
		snap.doors.push_back(door);
	}
	// Zone lines (expand geometry + extract bounding boxes)
	if (m_zone_lines && m_zone_lines->hasZoneLines()) {
		if (m_zone_map) {
			m_zone_lines->expandZoneLinesToGeometry(m_zone_map.get());
		}
		snap.zoneLineBBoxes = m_zone_lines->getZoneLineBoundingBoxes();
	}
	snap.zoneMap = m_zone_map.get();
	snap.eqClientPath = m_eq_client_path;

	// Pre-parsed zone sources for sequential loader
	snap.zoneArchive = m_zone_archive;
	snap.zoneWld = m_zone_wld;
	if (m_zone_bsp_tree) {
		snap.playerBspRegion = m_zone_bsp_tree->findRegionIndexForPoint(m_x, m_y, m_z);
		if (snap.playerBspRegion == SIZE_MAX) {
			LOG_FATAL(MOD_MAIN, "Player BSP region is SIZE_MAX! pos=({:.2f},{:.2f},{:.2f}) zone={}",
			          m_x, m_y, m_z, m_current_zone_name);
			std::exit(1);
		}
		LOG_INFO(MOD_MAIN, "Snapshot: player BSP region={} pos=({:.2f},{:.2f},{:.2f})",
		         snap.playerBspRegion, m_x, m_y, m_z);
	}

	return snap;
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
			EQT::Graphics::IrrlichtRenderer::EntityPoseState::Standing);
		std::string animCode;
		bool setPose = false;

		if (entity.animation == ANIM_SITTING) {
			poseState = static_cast<uint8_t>(
				EQT::Graphics::IrrlichtRenderer::EntityPoseState::Sitting);
			animCode = "p02";
			setPose = true;
		} else if (entity.animation == ANIM_CROUCHING) {
			poseState = static_cast<uint8_t>(
				EQT::Graphics::IrrlichtRenderer::EntityPoseState::Crouching);
			animCode = "l08";
			setPose = true;
		} else if (entity.animation == ANIM_LYING) {
			poseState = static_cast<uint8_t>(
				EQT::Graphics::IrrlichtRenderer::EntityPoseState::Lying);
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

void EverQuest::OnGraphicsMovement(const EQT::Graphics::PlayerPositionUpdate& update) {
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

void EverQuest::ProcessBridgeIntents() {
	if (!m_bridge) return;

	auto intents = m_bridge->drainIntents();
	for (const auto& intent : intents) {
		std::visit([this](const auto& i) {
			using T = std::decay_t<decltype(i)>;
			if constexpr (std::is_same_v<T, eqt::events::PlayerPositionChanged>) {
				// D14: Movement intent — same logic as movementCallback_ path
				EQT::Graphics::PlayerPositionUpdate update;
				update.x = i.x;
				update.y = i.y;
				update.z = i.z;
				update.heading = i.heading;
				update.dx = i.dx;
				update.dy = i.dy;
				update.dz = i.dz;
				OnGraphicsMovement(update);
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
				RequestLootCorpse(i.spawnId);
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
				} else if (result == EQ::CastResult::NotMemorized) {
					AddChatSystemMessage(fmt::format("No spell in gem {}", i.gemSlot + 1));
				} else if (result == EQ::CastResult::NotEnoughMana) {
					AddChatSystemMessage("Insufficient mana");
				} else if (result == EQ::CastResult::GemCooldown) {
					AddChatSystemMessage("Spell not ready");
				} else if (result == EQ::CastResult::AlreadyCasting) {
					AddChatSystemMessage("Already casting");
				} else if (result == EQ::CastResult::OutOfRange) {
					AddChatSystemMessage("Target out of range");
				} else if (result == EQ::CastResult::InvalidTarget) {
					AddChatSystemMessage("Invalid target");
				} else if (result == EQ::CastResult::NoLineOfSight) {
					AddChatSystemMessage("You cannot see your target");
				} else if (result == EQ::CastResult::Stunned) {
					AddChatSystemMessage("You are stunned");
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::ForgetSpellIntent>) {
				if (m_spell_manager && m_spell_manager->forgetSpell(i.gemSlot)) {
					AddChatSystemMessage(fmt::format("Forgot spell in gem {}", i.gemSlot + 1));
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::ScribeSpellIntent>) {
				ScribeSpellFromScroll(i.spellId, i.bookSlot, i.sourceSlot);
			}
			else if constexpr (std::is_same_v<T, eqt::events::SpellbookStateIntent>) {
				SendSpawnAppearance(AT_ANIMATION, i.isOpen ? 110 : 100);
			}
			else if constexpr (std::is_same_v<T, eqt::events::InterruptSpellIntent>) {
				if (m_spell_manager) m_spell_manager->interruptCast();
			}
			// Buff/skill intents
			else if constexpr (std::is_same_v<T, eqt::events::BuffCancelIntent>) {
				if (m_buff_manager) {
					m_buff_manager->removeBuffBySlot(0, i.slot);
					AddChatSystemMessage(fmt::format("Buff in slot {} cancelled", i.slot + 1));
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::SkillActivateIntent>) {
				if (m_skill_manager) {
					m_skill_manager->activateSkill(static_cast<uint8_t>(i.skillId));
				}
			}
			// Loot intents
			else if constexpr (std::is_same_v<T, eqt::events::LootItemIntent>) {
				LootItemFromCorpse(i.corpseId, i.slot);
			}
			else if constexpr (std::is_same_v<T, eqt::events::LootAllIntent>) {
				LootAllFromCorpse(i.corpseId);
			}
			else if constexpr (std::is_same_v<T, eqt::events::DestroyAllLootIntent>) {
				DestroyAllCorpseLoot(i.corpseId);
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
			else if constexpr (std::is_same_v<T, eqt::events::CloseVendorIntent>) {
				CloseVendorWindow();
			}
			// Bank intents
			else if constexpr (std::is_same_v<T, eqt::events::CloseBankIntent>) {
				CloseBankWindow();
			}
			else if constexpr (std::is_same_v<T, eqt::events::BankCurrencyMoveIntent>) {
				// Build and send MoveCoin packet
				EQT::MoveCoin_Struct move;
				if (i.fromBank) {
					move.from_slot = EQT::COINSLOT_BANK;
					move.to_slot = EQT::COINSLOT_INVENTORY;
				} else {
					move.from_slot = EQT::COINSLOT_INVENTORY;
					move.to_slot = EQT::COINSLOT_BANK;
				}
				move.cointype1 = i.coinType;
				move.cointype2 = i.coinType;
				move.amount = i.amount;

				SendMoveCoin(move);

				// Update local currency values (server will confirm, but update locally for responsiveness)
				uint32_t* srcPlatinum = i.fromBank ? &m_bank_platinum : &m_platinum;
				uint32_t* srcGold = i.fromBank ? &m_bank_gold : &m_gold;
				uint32_t* srcSilver = i.fromBank ? &m_bank_silver : &m_silver;
				uint32_t* srcCopper = i.fromBank ? &m_bank_copper : &m_copper;

				uint32_t* dstPlatinum = i.fromBank ? &m_platinum : &m_bank_platinum;
				uint32_t* dstGold = i.fromBank ? &m_gold : &m_bank_gold;
				uint32_t* dstSilver = i.fromBank ? &m_silver : &m_bank_silver;
				uint32_t* dstCopper = i.fromBank ? &m_copper : &m_bank_copper;

				switch (i.coinType) {
					case EQT::COINTYPE_PP:
						if (*srcPlatinum >= static_cast<uint32_t>(i.amount)) {
							*srcPlatinum -= i.amount;
							*dstPlatinum += i.amount;
						}
						break;
					case EQT::COINTYPE_GP:
						if (*srcGold >= static_cast<uint32_t>(i.amount)) {
							*srcGold -= i.amount;
							*dstGold += i.amount;
						}
						break;
					case EQT::COINTYPE_SP:
						if (*srcSilver >= static_cast<uint32_t>(i.amount)) {
							*srcSilver -= i.amount;
							*dstSilver += i.amount;
						}
						break;
					case EQT::COINTYPE_CP:
						if (*srcCopper >= static_cast<uint32_t>(i.amount)) {
							*srcCopper -= i.amount;
							*dstCopper += i.amount;
						}
						break;
				}

				LOG_DEBUG(MOD_INVENTORY, "Bank currency move: type={} amount={} fromBank={}, bank now: {}pp {}gp {}sp {}cp, inv now: {}pp {}gp {}sp {}cp",
					i.coinType, i.amount, i.fromBank,
					m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper,
					m_platinum, m_gold, m_silver, m_copper);

				// Publish CurrencyChanged + BankCurrencyChanged (bridge events handle UI updates)
				if (m_bridge) {
					eqt::state::CurrencyChangedData cdata;
					cdata.platinum = static_cast<int32_t>(m_platinum);
					cdata.gold = static_cast<int32_t>(m_gold);
					cdata.silver = static_cast<int32_t>(m_silver);
					cdata.copper = static_cast<int32_t>(m_copper);
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::CurrencyChanged, std::move(cdata)));

					eqt::state::BankCurrencyChangedData bdata;
					bdata.platinum = static_cast<int32_t>(m_bank_platinum);
					bdata.gold = static_cast<int32_t>(m_bank_gold);
					bdata.silver = static_cast<int32_t>(m_bank_silver);
					bdata.copper = static_cast<int32_t>(m_bank_copper);
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::BankCurrencyChanged, std::move(bdata)));
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::BankCurrencyConvertIntent>) {
				// Conversion is always 10:1 ratio: 10 cp -> 1 sp, 10 sp -> 1 gp, 10 gp -> 1 pp
				if (i.amount < 10 || (i.amount % 10) != 0) {
					LOG_WARN(MOD_INVENTORY, "Invalid conversion amount: {} (must be multiple of 10)", i.amount);
					return;
				}

				int32_t toCoinType = i.fromCoinType + 1;
				if (toCoinType > EQT::COINTYPE_PP) {
					LOG_WARN(MOD_INVENTORY, "Cannot convert platinum further");
					return;
				}

				EQT::MoveCoin_Struct move;
				move.from_slot = EQT::COINSLOT_BANK;
				move.to_slot = EQT::COINSLOT_BANK;
				move.cointype1 = i.fromCoinType;
				move.cointype2 = toCoinType;
				move.amount = i.amount;

				SendMoveCoin(move);

				LOG_DEBUG(MOD_INVENTORY, "Sent bank currency conversion: {} {} -> {} (cointype {} -> {})",
					i.amount, (i.fromCoinType == EQT::COINTYPE_CP ? "copper" :
					           i.fromCoinType == EQT::COINTYPE_SP ? "silver" : "gold"),
					(toCoinType == EQT::COINTYPE_SP ? "silver" :
					 toCoinType == EQT::COINTYPE_GP ? "gold" : "platinum"),
					i.fromCoinType, toCoinType);

				// Update local values for UI responsiveness (server will confirm)
				uint32_t convertedAmount = i.amount / 10;
				switch (i.fromCoinType) {
					case EQT::COINTYPE_CP:
						if (m_bank_copper >= static_cast<uint32_t>(i.amount)) {
							m_bank_copper -= i.amount;
							m_bank_silver += convertedAmount;
						}
						break;
					case EQT::COINTYPE_SP:
						if (m_bank_silver >= static_cast<uint32_t>(i.amount)) {
							m_bank_silver -= i.amount;
							m_bank_gold += convertedAmount;
						}
						break;
					case EQT::COINTYPE_GP:
						if (m_bank_gold >= static_cast<uint32_t>(i.amount)) {
							m_bank_gold -= i.amount;
							m_bank_platinum += convertedAmount;
						}
						break;
				}

				LOG_DEBUG(MOD_INVENTORY, "Bank currency after conversion: {}pp {}gp {}sp {}cp",
					m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper);

				// Publish BankCurrencyChanged (bridge event handles UI update)
				if (m_bridge) {
					eqt::state::BankCurrencyChangedData bdata;
					bdata.platinum = static_cast<int32_t>(m_bank_platinum);
					bdata.gold = static_cast<int32_t>(m_bank_gold);
					bdata.silver = static_cast<int32_t>(m_bank_silver);
					bdata.copper = static_cast<int32_t>(m_bank_copper);
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::BankCurrencyChanged, std::move(bdata)));
				}
			}
			// Trade intents
			else if constexpr (std::is_same_v<T, eqt::events::TradeRequestIntent>) {
				if (m_trade_manager) {
					auto it = m_entities.find(static_cast<uint16_t>(i.targetId));
					if (it != m_entities.end()) {
						bool isNpc = (it->second.npc_type == 1);
						m_trade_manager->requestTrade(static_cast<uint16_t>(i.targetId), it->second.name, isNpc);
					}
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::TradeAcceptIntent>) {
				if (m_trade_manager) m_trade_manager->clickAccept();
			}
			else if constexpr (std::is_same_v<T, eqt::events::TradeCancelIntent>) {
				if (m_trade_manager) m_trade_manager->cancelTrade();
			}
			// Pet/group intents
			else if constexpr (std::is_same_v<T, eqt::events::PetCommandIntent>) {
				SendPetCommand(static_cast<EQT::PetCommand>(i.command), i.targetId);
			}
			else if constexpr (std::is_same_v<T, eqt::events::GroupInviteIntent>) {
				SendGroupInvite(i.targetName);
				AddChatSystemMessage(fmt::format("Inviting {} to group", i.targetName));
			}
			else if constexpr (std::is_same_v<T, eqt::events::DisbandIntent>) {
				if (m_in_group) {
					if (m_is_group_leader) SendGroupDisband();
					else SendLeaveGroup();
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::DeclineInviteIntent>) {
				DeclineGroupInvite();
			}
			// General intents
			else if constexpr (std::is_same_v<T, eqt::events::RequestCampIntent>) {
				ProcessChatInput("/camp");
			}
			else if constexpr (std::is_same_v<T, eqt::events::RequestQuitIntent>) {
				ProcessChatInput("/q");
			}
			else if constexpr (std::is_same_v<T, eqt::events::HotbarChangedIntent>) {
				LOG_TRACE(MOD_MAIN, "Bridge intent: HotbarChangedIntent");
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
			// Group accept intent (D20b2)
			else if constexpr (std::is_same_v<T, eqt::events::GroupAcceptIntent>) {
				AcceptGroupInvite();
			}
			// Hotbar activate intent (D20b2)
			else if constexpr (std::is_same_v<T, eqt::events::HotbarActivateIntent>) {
				switch (i.buttonType) {
				case 0: { // Spell
					if (m_spell_manager) {
						for (uint8_t gem = 0; gem < EQ::MAX_SPELL_GEMS; gem++) {
							if (m_spell_manager->getMemorizedSpell(gem) == i.id) {
								uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
								EQ::CastResult result = m_spell_manager->beginCastFromGem(gem, targetId);
								if (result == EQ::CastResult::Success) {
									const EQ::SpellData* spell = m_spell_manager->getSpell(i.id);
									if (spell) AddChatSystemMessage(fmt::format("Casting {}", spell->name));
								} else if (result == EQ::CastResult::NotEnoughMana) {
									AddChatSystemMessage("Insufficient mana");
								} else if (result == EQ::CastResult::GemCooldown) {
									AddChatSystemMessage("Spell not ready");
								} else if (result == EQ::CastResult::AlreadyCasting) {
									AddChatSystemMessage("Already casting");
								}
								break;
							}
						}
					}
					break;
				}
				case 1: { // Item
					if (m_inventory_manager) {
						int16_t slot = m_inventory_manager->findItemSlotByItemId(i.id);
						if (slot == eqt::inventory::SLOT_INVALID) {
							AddChatSystemMessage("Item not found in inventory");
							break;
						}
						const auto* item = m_inventory_manager->getItem(slot);
						if (!item) { AddChatSystemMessage("Item not found"); break; }
						if (item->clickEffect.effectId == 0) {
							AddChatSystemMessage(fmt::format("{} has no click effect", item->name));
							break;
						}
						if (item->clickEffect.type == 5 && slot >= eqt::inventory::GENERAL_BEGIN) {
							AddChatSystemMessage("You must equip this item to use its effect");
							break;
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
						// Send cooldown feedback to renderer
						if (item->clickEffect.recastDelay > 0 && m_bridge) {
							eqt::state::HotbarCooldownStartedData cd;
							cd.index = i.index;
							cd.durationMs = item->clickEffect.recastDelay;
							m_bridge->pushEvent(eqt::state::GameEvent(
								eqt::state::GameEventType::HotbarCooldownStarted, std::move(cd)));
						}
					}
					break;
				}
				case 2: { // Emote
					if (!i.emoteText.empty()) ProcessChatInput(i.emoteText);
					break;
				}
				case 3: { // Skill
					if (m_skill_manager) m_skill_manager->activateSkill(static_cast<uint8_t>(i.id));
					break;
				}
				default: break;
				}
			}
			// Chat link click intent (D20b2)
			else if constexpr (std::is_same_v<T, eqt::events::ChatLinkClickIntent>) {
				if (i.linkType == 0) {  // NPCName
					ZoneSendChannelMessage(i.displayText, CHAT_CHANNEL_SAY, "");
				}
				// Item tooltip links handled on renderer side
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
			// Debug/diagnostic intents
			else if constexpr (std::is_same_v<T, eqt::events::SlashCommandIntent>) {
				ProcessChatInput(i.fullCommand);
			}
			else if constexpr (std::is_same_v<T, eqt::events::RequestMemoryReport>) {
				LOG_TRACE(MOD_MAIN, "Bridge intent: RequestMemoryReport (D20d)");
			}
			else if constexpr (std::is_same_v<T, eqt::events::RequestSceneDump>) {
				LOG_TRACE(MOD_MAIN, "Bridge intent: RequestSceneDump (D20d)");
			}
			else if constexpr (std::is_same_v<T, eqt::events::ToggleAutoAttackIntent>) {
				if (m_combat_manager) {
					if (m_combat_manager->IsAutoAttackEnabled()) {
						m_combat_manager->DisableAutoAttack();
					} else if (m_combat_manager->HasTarget()) {
						m_combat_manager->EnableAutoAttack();
					}
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::AttackIntent>) {
				if (m_combat_manager) m_combat_manager->SetTarget(static_cast<uint16_t>(i.targetId));
				if (m_combat_manager && m_combat_manager->HasTarget()) m_combat_manager->EnableAutoAttack();
			}
			else if constexpr (std::is_same_v<T, eqt::events::MemorizeSpellIntent>) {
				if (m_spell_manager) m_spell_manager->memorizeSpell(i.gemSlot, i.spellId);
			}
			else {
				LOG_TRACE(MOD_MAIN, "Bridge: unhandled intent type");
			}
		}, intent);
	}
}

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

void EverQuest::LootItemFromCorpse(uint16_t corpseId, int16_t slot, bool autoLoot) {
	LOG_DEBUG(MOD_INVENTORY, "LootItemFromCorpse: corpseId={} slot={} autoLoot={} m_player_looting_corpse_id={} m_my_spawn_id={}",
		corpseId, slot, autoLoot, m_player_looting_corpse_id, m_my_spawn_id);

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
}

void EverQuest::LootAllFromCorpse(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "LootAllFromCorpse: corpseId={}", corpseId);

	// Validate we're actively looting (D18a: uses game-side loot item tracking)
	if (m_player_looting_corpse_id == 0) {
		LOG_DEBUG(MOD_INVENTORY, "LootAllFromCorpse: Not currently looting");
		return;
	}

	if (m_loot_items.empty()) {
		LOG_DEBUG(MOD_INVENTORY, "LootAllFromCorpse: No items to loot");
		return;
	}

	// Store remaining slots for sequential looting
	// In Titanium, server closes loot session after each item,
	// so we must loot one item at a time and re-request loot for remaining items
	m_loot_all_remaining_slots.clear();
	for (const auto& [slot, item] : m_loot_items) {
		m_loot_all_remaining_slots.push_back(slot);
	}

	LOG_DEBUG(MOD_INVENTORY, "LootAllFromCorpse: Queued {} items for sequential looting", m_loot_all_remaining_slots.size());

	// Start loot-all operation - loot first item only
	m_loot_all_in_progress = true;
	int16_t firstSlot = m_loot_all_remaining_slots.front();
	m_loot_all_remaining_slots.erase(m_loot_all_remaining_slots.begin());
	LootItemFromCorpse(corpseId, firstSlot, true);  // autoLoot=true for Loot All
}

void EverQuest::DestroyAllCorpseLoot(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "DestroyAllCorpseLoot: corpseId={}", corpseId);

	// For now, just close the loot window - items stay on corpse until despawn
	// The Titanium protocol doesn't have explicit destroy packets for loot items
	CloseLootWindow(corpseId);
}

void EverQuest::CloseLootWindow(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "CloseLootWindow: corpseId={} m_player_looting_corpse_id={}",
		corpseId, m_player_looting_corpse_id);

	// Use passed corpseId, or fall back to m_player_looting_corpse_id if 0
	uint16_t targetCorpseId = (corpseId != 0) ? corpseId : m_player_looting_corpse_id;

	// Clear the looting state
	m_player_looting_corpse_id = 0;
	m_pending_loot_slots.clear();
	m_loot_all_in_progress = false;
	m_loot_all_remaining_slots.clear();
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

	// Publish LootWindowClosed (D18a: bridge is sole path to renderer)
	if (m_bridge) {
		eqt::state::LootWindowClosedData data;
		data.corpseId = targetCorpseId;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::LootWindowClosed, std::move(data)));
	}
}

void EverQuest::SaveHotbarConfig() {
	if (m_config_path.empty() || !m_hotbar_model) return;

	auto config = EQ::JsonConfigFile::Load(m_config_path);
	Json::Value& root = config.RawHandle();

	// Migrate old config format to new format if needed
	if (root.isArray()) {
		Json::Value newRoot;
		newRoot["clients"] = root;
		root = newRoot;
	}

	Json::Value* clientConfig = nullptr;
	if (root.isObject()) {
		if (root.isMember("clients") && root["clients"].isArray() && root["clients"].size() > 0) {
			clientConfig = &root["clients"][0];
		} else if (!root.isMember("clients")) {
			Json::Value clientsCopy = root;
			root = Json::Value(Json::objectValue);
			root["clients"] = Json::Value(Json::arrayValue);
			root["clients"].append(clientsCopy);
			clientConfig = &root["clients"][0];
		}
	}

	if (clientConfig && clientConfig->isObject()) {
		(*clientConfig)["hotbar"] = m_hotbar_model->saveToJson();
		config.Save(m_config_path);
		LOG_DEBUG(MOD_CONFIG, "Saved hotbar config to {}", m_config_path);
	}
}

void EverQuest::LoadHotbarConfig() {
	if (m_config_path.empty() || !m_hotbar_model) return;

	auto config = EQ::JsonConfigFile::Load(m_config_path);
	Json::Value& root = config.RawHandle();

	const Json::Value* clientConfig = nullptr;
	if (root.isArray() && root.size() > 0) {
		clientConfig = &root[0];
	} else if (root.isObject()) {
		if (root.isMember("clients") && root["clients"].isArray() && root["clients"].size() > 0) {
			clientConfig = &root["clients"][0];
		} else {
			clientConfig = &root;
		}
	}

	if (clientConfig && clientConfig->isObject() && clientConfig->isMember("hotbar")) {
		m_hotbar_model->loadFromJson((*clientConfig)["hotbar"]);
		LOG_DEBUG(MOD_CONFIG, "Loaded hotbar config from {}", m_config_path);
	} else {
		LOG_DEBUG(MOD_CONFIG, "No hotbar config found in {}", m_config_path);
	}
}

#endif // EQT_HAS_GRAPHICS

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

	// Get EQ client path from config or graphics settings
	std::string eqPath;
#ifdef EQT_HAS_GRAPHICS
	eqPath = m_eq_client_path;
#endif

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

	// Apply memory constraints from constrained rendering config
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
 
