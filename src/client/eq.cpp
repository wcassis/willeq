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


#endif // EQT_HAS_GRAPHICS
