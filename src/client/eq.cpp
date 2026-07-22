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
 
