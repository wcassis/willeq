// eq_bridge_ui_audio.cpp
//
// The parts of the EverQuest <-> renderer bridge layer that ride the bridge
// rather than being it, split out of eq_bridge.cpp by J13
// (refactor/J13_eq_bridge_split.md); originally split out of eq.cpp (F10).
// Contains inventory stat sync, the player-mode loot-window UI flow, client
// config / hotbar / inspect-message persistence, the audio system (init and
// shutdown, day-night state, the PlaySound* helpers) and inventory item use.
//
// This is a pure move; no behavior or interface changes. See
// refactor/F10_bridge.md for the original moved/left mapping.
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
		auto it = m_entities.find(MySpawnId());
		if (it != m_entities.end()) {
			it->second.primary_weapon_skill = primaryWeaponSkill;
			it->second.secondary_weapon_skill = secondaryWeaponSkill;
		}

		// Propagate weapon skill types via bridge (D17b: bridge is sole path to renderer)
		if (m_bridge) {
			eqt::state::EntityWeaponSkillsChangedData data;
			data.spawnId = MySpawnId();
			data.primaryWeaponSkill = primaryWeaponSkill;
			data.secondaryWeaponSkill = secondaryWeaponSkill;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityWeaponSkillsChanged, std::move(data)));
		}
	}

	// Calculate total stats (base + equipment)
	int totalStr = static_cast<int>(m_game_state.player().STR()) + equipStats.str;
	int totalSta = static_cast<int>(m_game_state.player().STA()) + equipStats.sta;
	int totalAgi = static_cast<int>(m_game_state.player().AGI()) + equipStats.agi;
	int totalDex = static_cast<int>(m_game_state.player().DEX()) + equipStats.dex;
	int totalWis = static_cast<int>(m_game_state.player().WIS()) + equipStats.wis;
	int totalInt = static_cast<int>(m_game_state.player().INT()) + equipStats.int_;
	int totalCha = static_cast<int>(m_game_state.player().CHA()) + equipStats.cha;

	// Max weight capacity is equal to total STR
	float maxWeight = static_cast<float>(totalStr);

	// Update weight tracking (T286: PlayerState is the only store)
	m_game_state.player().setWeight(totalWeight, maxWeight);

	// Publish stats via bridge (D18a: bridge is sole path to renderer)
	if (m_bridge) {
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

		// Publish EquipmentStatsChanged
		eqt::state::EquipmentStatsChangedData edata;
		edata.ac = equipStats.ac;
		edata.atk = equipStats.atk;
		edata.hp = equipStats.hp;
		edata.mana = equipStats.mana;
		edata.weight = m_game_state.player().weight();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::EquipmentStatsChanged, std::move(edata)));

		// Publish CurrencyChanged (updateCharacterStats includes currency)
		eqt::state::CurrencyChangedData cdata;
		cdata.platinum = static_cast<int32_t>(m_game_state.player().platinum());
		cdata.gold = static_cast<int32_t>(m_game_state.player().gold());
		cdata.silver = static_cast<int32_t>(m_game_state.player().silver());
		cdata.copper = static_cast<int32_t>(m_game_state.player().copper());
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(cdata)));
	}
}

// ============================================================================
// Player Mode Loot Window Methods
// ============================================================================

void EverQuest::RequestLootCorpse(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "RequestLootCorpse: corpseId={}", corpseId);

	// Store the corpse being looted (T286: PlayerState is the only store)
	m_game_state.player().setLootingCorpse(corpseId);  // Phase 7.7
	LOG_TRACE(MOD_INVENTORY, "Set lootingCorpseId={}", m_game_state.player().lootingCorpseId());

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
	LOG_DEBUG(MOD_INVENTORY, "LootItemFromCorpse: corpseId={} slot={} autoLoot={} lootingCorpseId={} mySpawnId={}",
		corpseId, slot, autoLoot, m_game_state.player().lootingCorpseId(), MySpawnId());

	// T268: no window, nothing to take -- eqemu would answer a bare
	// OP_LootItem with an EndLoot error anyway (Corpse::LootCorpseItem's
	// IsBeingLootedBy check).
	if (m_game_state.player().lootingCorpseId() == 0) {
		LOG_DEBUG(MOD_INVENTORY, "LootItemFromCorpse: no loot window open");
		return false;
	}
	if (corpseId == 0) {
		corpseId = m_game_state.player().lootingCorpseId();
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
	li->looter = MySpawnId();
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
	if (m_game_state.player().lootingCorpseId() == 0 || name.empty()) {
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
	LOG_DEBUG(MOD_INVENTORY, "CloseLootWindow: corpseId={} m_game_state.player().lootingCorpseId()={}",
		corpseId, m_game_state.player().lootingCorpseId());

	// Use passed corpseId, or fall back to the store if 0
	uint16_t targetCorpseId = (corpseId != 0) ? corpseId : m_game_state.player().lootingCorpseId();

	// Clear the looting state
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
	m_is_daytime = (m_game_state.world().timeHour() >= 6 && m_game_state.world().timeHour() < 19);

	// If day/night state changed, notify zone audio manager
	if (wasDay != m_is_daytime && m_zone_audio_manager) {
		m_zone_audio_manager->setDayNight(m_is_daytime);
		LOG_DEBUG(MOD_AUDIO, "Day/night changed to {} (hour {})",
			m_is_daytime ? "day" : "night", m_game_state.world().timeHour());
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
		m_chat->ProcessChatInput(text);
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
