// bridge_component_ui_audio.cpp
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
#include "client/audio/volume_steps.h"  // T439: the oracle's 0..20 volume step
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
#include "client/hotbar/hotbar_persistence.h"
#include "client/login_world/login_world_component.h"
#include "common/util/json_config.h"
#ifdef EQT_HAS_GRAPHICS
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

void eqt::bridge::BridgeComponent::UpdateInventoryStats() {

	// Calculate equipment bonuses from inventory
	eqt::inventory::EquipmentStats equipStats;
	float totalWeight = 0.0f;

	// Track weapon skill types for combat animations
	uint8_t primaryWeaponSkill = EQ::WEAPON_HAND_TO_HAND;  // Default to H2H (unarmed)
	uint8_t secondaryWeaponSkill = EQ::WEAPON_NONE;

	if (ctx_.inventory_manager) {
		equipStats = ctx_.inventory_manager->calculateEquipmentStats();
		totalWeight = ctx_.inventory_manager->calculateTotalWeight();

		// Get weapon skill types from equipped items
		using namespace eqt::inventory;
		const eqt::inventory::ItemInstance* primaryItem = ctx_.inventory_manager->getItem(SLOT_PRIMARY);
		const eqt::inventory::ItemInstance* secondaryItem = ctx_.inventory_manager->getItem(SLOT_SECONDARY);

		if (primaryItem && primaryItem->itemId != 0) {
			primaryWeaponSkill = primaryItem->skillType;
		}
		if (secondaryItem && secondaryItem->itemId != 0) {
			secondaryWeaponSkill = secondaryItem->skillType;
		}

		// Update our entity's weapon skill types
		auto it = ctx_.entities.find(ctx_.self.MySpawnId());
		if (it != ctx_.entities.end()) {
			it->second.primary_weapon_skill = primaryWeaponSkill;
			it->second.secondary_weapon_skill = secondaryWeaponSkill;
		}

		// Propagate weapon skill types via bridge (D17b: bridge is sole path to renderer)
		if (ctx_.bridge) {
			eqt::state::EntityWeaponSkillsChangedData data;
			data.spawnId = ctx_.self.MySpawnId();
			data.primaryWeaponSkill = primaryWeaponSkill;
			data.secondaryWeaponSkill = secondaryWeaponSkill;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityWeaponSkillsChanged, std::move(data)));
		}
	}

	// Calculate total stats (base + equipment)
	int totalStr = static_cast<int>(ctx_.game_state.player().STR()) + equipStats.str;
	int totalSta = static_cast<int>(ctx_.game_state.player().STA()) + equipStats.sta;
	int totalAgi = static_cast<int>(ctx_.game_state.player().AGI()) + equipStats.agi;
	int totalDex = static_cast<int>(ctx_.game_state.player().DEX()) + equipStats.dex;
	int totalWis = static_cast<int>(ctx_.game_state.player().WIS()) + equipStats.wis;
	int totalInt = static_cast<int>(ctx_.game_state.player().INT()) + equipStats.int_;
	int totalCha = static_cast<int>(ctx_.game_state.player().CHA()) + equipStats.cha;

	// Max weight capacity is equal to total STR
	float maxWeight = static_cast<float>(totalStr);

	// Update weight tracking (T286: PlayerState is the only store)
	ctx_.game_state.player().setWeight(totalWeight, maxWeight);

	// Publish stats via bridge (D18a: bridge is sole path to renderer)
	if (ctx_.bridge) {
		eqt::state::PlayerStatsChangedData data;
		data.curHP = ctx_.game_state.player().curHP();
		data.maxHP = ctx_.game_state.player().maxHP();
		data.curMana = ctx_.game_state.player().curMana();
		data.maxMana = ctx_.game_state.player().maxMana();
		data.curEndurance = ctx_.game_state.player().curEndurance();
		data.maxEndurance = ctx_.game_state.player().maxEndurance();
		data.level = ctx_.game_state.player().level();
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerStatsChanged, std::move(data)));

		// Publish EquipmentStatsChanged
		eqt::state::EquipmentStatsChangedData edata;
		edata.ac = equipStats.ac;
		edata.atk = equipStats.atk;
		edata.hp = equipStats.hp;
		edata.mana = equipStats.mana;
		edata.weight = ctx_.game_state.player().weight();
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::EquipmentStatsChanged, std::move(edata)));

		// Publish CurrencyChanged (updateCharacterStats includes currency)
		eqt::state::CurrencyChangedData cdata;
		cdata.platinum = static_cast<int32_t>(ctx_.game_state.player().platinum());
		cdata.gold = static_cast<int32_t>(ctx_.game_state.player().gold());
		cdata.silver = static_cast<int32_t>(ctx_.game_state.player().silver());
		cdata.copper = static_cast<int32_t>(ctx_.game_state.player().copper());
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(cdata)));
	}
}

// ============================================================================
// Player Mode Loot Window Methods
// ============================================================================

void eqt::bridge::BridgeComponent::RequestLootCorpse(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "RequestLootCorpse: corpseId={}", corpseId);

	// Store the corpse being looted (T286: PlayerState is the only store)
	ctx_.game_state.player().setLootingCorpse(corpseId);  // Phase 7.7
	LOG_TRACE(MOD_INVENTORY, "Set lootingCorpseId={}", ctx_.game_state.player().lootingCorpseId());

	// B1183 (#502): the client's loot-OPEN path is this 28061 gate, the
	// SETTER standing -> looting ('d' -> FUN_00483556(0x69)). The other
	// direct 0x69 write (eqgame.exe.c:105797, FUN_00484d8d) is
	// immediately re-100'd at 105798 (its tail is `*(param_1+0x251)=0x69;
	// FUN_00483556(100);`): a transient 105 on the loot-close / stand-up
	// path, not a second loot-open path — the gate below is the settled
	// loot-open mechanism.
	{
		auto self = ctx_.entities.find(ctx_.self.MySpawnId());
		if (self != ctx_.entities.end()) {
			auto& st = self->second.stand_state;
			if (st == 100) {
				eqt::state::StandStateContext sctx;
				sctx.in_world = ctx_.self.IsFullyZonedIn();
				st.transition(eqt::state::StandValue::Looting, sctx);
			}
		}
	}

	// Send loot request packet - server expects only the corpse ID (4 bytes)
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt32(0, corpseId);
	ctx_.net.Queue(HC_OP_LootRequest, &packet);

	// Clear game-side loot items for new corpse
	m_loot_items.clear();

	// Publish LootWindowOpened (D18a: bridge is sole path to renderer)
	if (ctx_.bridge) {
		std::string corpseName = "Corpse";
		auto it = ctx_.entities.find(corpseId);
		if (it != ctx_.entities.end()) {
			corpseName = EQT::toDisplayName(it->second.name);
		}
		eqt::state::LootWindowOpenedData data;
		data.corpseId = corpseId;
		data.corpseName = corpseName;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::LootWindowOpened, std::move(data)));
	}
}

bool eqt::bridge::BridgeComponent::LootItemFromCorpse(uint16_t corpseId, int16_t slot, bool autoLoot) {
	LOG_DEBUG(MOD_INVENTORY, "LootItemFromCorpse: corpseId={} slot={} autoLoot={} lootingCorpseId={} mySpawnId={}",
		corpseId, slot, autoLoot, ctx_.game_state.player().lootingCorpseId(), ctx_.self.MySpawnId());

	// T268: no window, nothing to take -- eqemu would answer a bare
	// OP_LootItem with an EndLoot error anyway (Corpse::LootCorpseItem's
	// IsBeingLootedBy check).
	if (ctx_.game_state.player().lootingCorpseId() == 0) {
		LOG_DEBUG(MOD_INVENTORY, "LootItemFromCorpse: no loot window open");
		return false;
	}
	if (corpseId == 0) {
		corpseId = ctx_.game_state.player().lootingCorpseId();
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
	li->looter = ctx_.self.MySpawnId();
	li->slot_id = static_cast<uint16_t>(slot);
	li->unknown3[0] = 0;
	li->unknown3[1] = 0;
	li->auto_loot = autoLoot ? 1 : 0;

	// Debug: print packet bytes (only at high debug level)
	if (EverQuest::GetDebugLevel() >= 3) {
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

	ctx_.net.Queue(HC_OP_LootItem, &packet);
	return true;
}

int16_t eqt::bridge::BridgeComponent::FindLootSlotByName(const std::string& name) const {
	if (ctx_.game_state.player().lootingCorpseId() == 0 || name.empty()) {
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

void eqt::bridge::BridgeComponent::CloseLootWindow(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "CloseLootWindow: corpseId={} m_game_state.player().lootingCorpseId()={}",
		corpseId, ctx_.game_state.player().lootingCorpseId());

	// Use passed corpseId, or fall back to the store if 0
	uint16_t targetCorpseId = (corpseId != 0) ? corpseId : ctx_.game_state.player().lootingCorpseId();

	// Clear the looting state
	m_pending_loot_slots.clear();
	ctx_.game_state.player().clearLootingCorpse();  // Phase 7.7

	// B1183 (#502): the client's loot-window close stands the looter back up
	// through the SETTER, and only while looting (eqgame.exe.c:230933, the
	// FUN_0052b026 window-close: 'i' -> FUN_00483556(100)).
	{
		auto self = ctx_.entities.find(ctx_.self.MySpawnId());
		if (self != ctx_.entities.end()) {
			auto& st = self->second.stand_state;
			if (st == 105) {
				eqt::state::StandStateContext sctx;
				sctx.in_world = ctx_.self.IsFullyZonedIn();
				st.transition(eqt::state::StandValue::Standing, sctx);
			}
		}
	}

	// Send end loot request to server (OP_EndLootRequest, not OP_LootComplete)
	// Server expects 4 bytes with corpse entity ID as uint16 at offset 0
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt16(0, targetCorpseId);
	packet.PutUInt16(2, 0);  // padding
	ctx_.net.Queue(HC_OP_EndLootRequest, &packet);
	LOG_DEBUG(MOD_INVENTORY, "CloseLootWindow: Sent HC_OP_EndLootRequest with corpseId={}", targetCorpseId);

	// Clear game-side loot items
	m_loot_items.clear();

	// T268: the loot policy machine leaves LOOTING with the window (this is
	// the ONE close path; CombatManager::CloseLootWindow routes here).
	if (ctx_.combat_manager) {
		ctx_.combat_manager->OnLootWindowClosed();
	}

	// Publish LootWindowClosed (D18a: bridge is sole path to renderer)
	if (ctx_.bridge) {
		eqt::state::LootWindowClosedData data;
		data.corpseId = targetCorpseId;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::LootWindowClosed, std::move(data)));
	}
}

// T171: the per-client config node, resolved for WRITING. Extracted verbatim
// from SaveHotbarConfig() so the inspect-message pair below cannot drift from
// the hotbar pair -- duplicated-then-drifted is this codebase's second most
// common defect class, and two more hand-rolled copies of this shape is
// exactly how it starts. Migrates the legacy top-level-array layout on the
// way. Returns nullptr when the config has a shape we should not rewrite.
Json::Value* eqt::bridge::BridgeComponent::ResolveClientConfigForWrite(Json::Value& root) {
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
const Json::Value* eqt::bridge::BridgeComponent::ResolveClientConfigForRead(const Json::Value& root) {
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

void eqt::bridge::BridgeComponent::SaveHotbarConfig() {
	if (!ctx_.hotbar_model || ctx_.character.empty()) return;
	if (!eqt::SaveCharacterHotbar(ctx_.character, ctx_.LoginWorld().ServerName(),
	                              *ctx_.hotbar_model)) {
		LOG_WARN(MOD_CONFIG, "Could not save per-character hotbar");
	}
}

eqt::HotbarLoadResult eqt::bridge::BridgeComponent::LoadHotbarConfig() {
	if (!ctx_.hotbar_model || ctx_.character.empty()) return eqt::HotbarLoadResult::Missing;
	ctx_.hotbar_model->reset();
	const auto& server = ctx_.LoginWorld().ServerName();
	const auto load = eqt::LoadCharacterHotbar(ctx_.character, server, *ctx_.hotbar_model);
	if (load == eqt::HotbarLoadResult::Loaded) return load;
	if (load == eqt::HotbarLoadResult::Error) {
		LOG_ERROR(MOD_CONFIG, "Could not load per-character hotbar; legacy import skipped");
		return load;
	}
	if (m_config_path.empty()) return load;

	auto config = EQ::JsonConfigFile::Load(m_config_path);
	Json::Value& root = config.RawHandle();
	const Json::Value* clientConfig = ResolveClientConfigForRead(root);
	if (clientConfig && clientConfig->isObject() && clientConfig->isMember("hotbar")) {
		ctx_.hotbar_model->loadFromJson((*clientConfig)["hotbar"]);
		if (eqt::SaveCharacterHotbar(ctx_.character, server, *ctx_.hotbar_model)) {
			// Consume once so a second character cannot inherit these assignments.
			Json::Value* writable = ResolveClientConfigForWrite(root);
			if (writable) {
				writable->removeMember("hotbar");
				config.Save(m_config_path);
			}
			return eqt::HotbarLoadResult::Loaded;
		}
		LOG_ERROR(MOD_CONFIG, "Could not import legacy hotbar into per-character INI");
		ctx_.hotbar_model->reset();
		return eqt::HotbarLoadResult::Error;
	}
	return load;
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
void eqt::bridge::BridgeComponent::SetInspectMessage(const std::string& message) {
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

void eqt::bridge::BridgeComponent::SaveInspectMessage() {
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

void eqt::bridge::BridgeComponent::LoadInspectMessage() {
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

void eqt::bridge::BridgeComponent::InitializeAudio() {
	if (ctx_.audio_manager) {
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
	std::string eqPath = ctx_.eq_client_path;

	if (eqPath.empty()) {
		LOG_WARN(MOD_AUDIO, "EQ client path not set, audio disabled");
		return;
	}

	ctx_.audio_manager = std::make_unique<EQT::Audio::AudioManager>();

	// Pass event loop pump as tick callback so network stays alive during slow audio init
	auto networkTick = [this]() {
		ctx_.lifecycle.TickNetwork();
	};

	if (!ctx_.audio_manager->initialize(eqPath, false, m_audio_config_soundfont, networkTick)) {
		LOG_WARN(MOD_AUDIO, "Failed to initialize audio system");
		ctx_.audio_manager.reset();
		return;
	}

	// Apply memory constraints from constrained rendering config.
	// P28: m_constrained_config/m_constrained_preset are renderer-startup-only
	// state (no meaning without a renderer to constrain); skip when graphics
	// is compiled out rather than guessing at a headless equivalent.
#ifdef EQT_HAS_GRAPHICS
	if (m_constrained_config && m_constrained_config->totalMemoryBudgetBytes > 0) {
		ctx_.audio_manager->setMemoryConstraints(
			m_constrained_config->soundBufferCacheBytes,
			m_constrained_config->lazyPfsLoading);
		LOG_INFO(MOD_AUDIO, "Audio memory constraints: soundCache={}KB, lazyPfs={}",
			m_constrained_config->soundBufferCacheBytes / 1024,
			m_constrained_config->lazyPfsLoading ? "yes" : "no");
	} else {
		auto cfg = EQT::Graphics::ConstrainedRendererConfig::fromPreset(m_constrained_preset);
		if (cfg.totalMemoryBudgetBytes > 0) {
			ctx_.audio_manager->setMemoryConstraints(cfg.soundBufferCacheBytes, cfg.lazyPfsLoading);
			LOG_INFO(MOD_AUDIO, "Audio memory constraints (from preset): soundCache={}KB, lazyPfs={}",
				cfg.soundBufferCacheBytes / 1024, cfg.lazyPfsLoading ? "yes" : "no");
		}
	}
#endif

	// Apply configured volume levels
	ctx_.audio_manager->setMasterVolume(m_audio_config_master_volume);
	ctx_.audio_manager->setMusicVolume(m_audio_config_music_volume);
	ctx_.audio_manager->setEffectsVolume(m_audio_config_effects_volume);
	// T439 (board B432): the first of the four volume-moving sites. The
	// renderer's Options sliders start from the panel struct's own defaults
	// (14 / 20); if the config says something else, this is what tells them.
	PublishVolumesChanged();

	// K15 (board B037): the configured 3D-positional-audio switch. Off means
	// positioned sounds play centred (AudioManager::playSoundInternal).
	ctx_.audio_manager->setUse3DAudio(m_audio_config_use_3d_audio);
	LOG_DEBUG(MOD_AUDIO, "3D positional audio: {}",
		m_audio_config_use_3d_audio ? "on" : "off");

	LOG_DEBUG(MOD_AUDIO, "Audio volumes: master={:.0f}%, music={:.0f}%, effects={:.0f}%",
		m_audio_config_master_volume * 100.0f,
		m_audio_config_music_volume * 100.0f,
		m_audio_config_effects_volume * 100.0f);

	// Pump network before sound preloading
	ctx_.lifecycle.TickNetwork();

	// Preload common sounds for faster playback
	ctx_.audio_manager->preloadCommonSounds();

	// Phase 13: Initialize zone audio manager for positioned sound emitters
	ctx_.zone_audio_manager = std::make_unique<EQT::Audio::ZoneAudioManager>();
	ctx_.zone_audio_manager->setAudioManager(ctx_.audio_manager.get());
	LOG_DEBUG(MOD_AUDIO, "Zone audio manager initialized");

	LOG_INFO(MOD_AUDIO, "Audio system initialized");
}

void eqt::bridge::BridgeComponent::ShutdownAudio() {
	// Shut down zone audio manager first
	if (ctx_.zone_audio_manager) {
		ctx_.zone_audio_manager->unloadZone();
		ctx_.zone_audio_manager.reset();
	}

	if (ctx_.audio_manager) {
		ctx_.audio_manager->shutdown();
		ctx_.audio_manager.reset();
		LOG_INFO(MOD_AUDIO, "Audio system shut down");
	}
}

// D20g: SetupRDPAudio moved to Application::setupRDPAudio()

void eqt::bridge::BridgeComponent::UpdateDayNightState() {
	// EverQuest day/night cycle:
	// Day: 6:00 AM (hour 6) to 6:59 PM (hour 18)
	// Night: 7:00 PM (hour 19) to 5:59 AM (hour 5)
	bool wasDay = m_is_daytime;
	m_is_daytime = (ctx_.game_state.world().timeHour() >= 6 && ctx_.game_state.world().timeHour() < 19);

	// If day/night state changed, notify zone audio manager
	if (wasDay != m_is_daytime && ctx_.zone_audio_manager) {
		ctx_.zone_audio_manager->setDayNight(m_is_daytime);
		LOG_DEBUG(MOD_AUDIO, "Day/night changed to {} (hour {})",
			m_is_daytime ? "day" : "night", ctx_.game_state.world().timeHour());
	}
}

// T334 (board B045): the per-frame listener sync, moved here verbatim from
// PostRenderTick() apart from the forward vector.
//
// D20c2's choice of the PLAYER position over the camera position stands and is
// deliberate: the player's ears are at the player, and in third person the
// camera can sit far behind, which would make a nearby NPC sound distant.
// (That is why PlayerCameraComponent::getCameraTransform(), written "for the
// audio listener" and never called, was deleted by this unit rather than
// wired -- see refactor/T334_camera_audio_and_vision.md. Should a camera-based
// listener ever be wanted, it is a listening measurement against the official
// client, not an argument.)
//
// What WAS broken is the orientation. HeadingDeg() returns DEGREES, and the
// old inline conversion divided by 512 as though it were wire units and then
// negated cos, so the listener faced a mirrored direction turning 1.42x too
// fast and every positioned sound panned to the wrong side. The convention now
// comes from one shared helper, pinned by tests/test_spatial_audio.cpp.
void eqt::bridge::BridgeComponent::UpdateAudioListener(float deltaTime) {
	if (!ctx_.audio_manager) return;

	const glm::vec3 listenerPos(ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ());

	ctx_.audio_manager->setListenerPosition(
		listenerPos,
		EQT::Audio::eqHeadingToListenerForward(ctx_.self.HeadingDeg()),
		glm::vec3(0.0f, 0.0f, 1.0f)  // Up is +Z in EQ coordinates
	);

	if (ctx_.zone_audio_manager && m_loading_phase == LoadingPhase::COMPLETE) {
		ctx_.zone_audio_manager->update(deltaTime, listenerPos, m_is_daytime);
	}

	// B1014 (issue #335): the game-thread drain of the custom-Font release
	// the audio thread flagged at fade completion. This body runs every game
	// tick (PostRenderTick), so a flagged release lands within a tick instead
	// of waiting for the next public MusicPlayer call.
	ctx_.audio_manager->update();

	// D20c2: Volume hotkeys handled via intents from renderer
	// (MusicVolumeChangeIntent / EffectsVolumeChangeIntent in ProcessBridgeIntents)
}

// T439 (board B432). Four sites move a volume -- this body is what they all
// call, so there is no per-site "and publish it" step to forget. It reads
// AudioManager back instead of echoing the value it was handed: the audio
// manager clamps, and the event must state what it HOLDS, not what it was
// asked for. The 0..1 float rounds onto the oracle's 21 positions here, so
// a /musicvolume 53 lands the slider on 55 % -- lossy on purpose, because
// 21 positions is the control the oracle gives us (volume_steps.h).
void eqt::bridge::BridgeComponent::PublishVolumesChanged() {
	if (!ctx_.bridge || !ctx_.audio_manager) return;
	eqt::state::VolumesChangedData data;
	data.musicStep =
		eqt::audio::volumeStepFromFraction(ctx_.audio_manager->getMusicVolume());
	data.soundStep =
		eqt::audio::volumeStepFromFraction(ctx_.audio_manager->getEffectsVolume());
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::VolumesChanged, std::move(data)));
}

void eqt::bridge::BridgeComponent::PlaySound(uint32_t soundId) {
	// Don't play sounds during zone loading
	if (ctx_.audio_manager && m_loading_phase == LoadingPhase::COMPLETE) {
		ctx_.audio_manager->playSound(soundId);
	}
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
bool eqt::bridge::BridgeComponent::UseInventoryItem(int16_t slot) {
	if (!ctx_.inventory_manager) {
		ctx_.out.System("Item not found in inventory");
		return false;
	}
	const auto* item = ctx_.inventory_manager->getItem(slot);
	if (!item) {
		ctx_.out.System("Item not found");
		return false;
	}
	if (item->clickEffect.effectId == 0) {
		ctx_.out.System(fmt::format("{} has no click effect", item->name));
		return false;
	}
	if (item->clickEffect.type == 5 && slot >= eqt::inventory::GENERAL_BEGIN) {
		ctx_.out.System("You must equip this item to use its effect");
		return false;
	}
	EQ::Net::DynamicPacket packet;
	packet.Resize(20);
	packet.PutUInt32(0, 10);  // slot = 10 for item clicks
	packet.PutUInt32(4, static_cast<uint32_t>(item->clickEffect.effectId));
	packet.PutUInt32(8, static_cast<uint32_t>(slot));
	// Default the target to the player's own id when no combat target is set.
	// The official client sends self for a self-buff item click; the server's
	// DoCastingChecksOnTarget (spells.cpp) resolves a null target to the caster
	// ONLY for ST_Self spells. For an item click whose spell is ST_Target (e.g.
	// 8443's "Enduring Breath", spells_new targettype=5), a 0 target makes that
	// gate return false -> StopCastSpell (silent for a client) -> no charge is
	// ever consumed. Self-id mirrors beginCastFromGem's "no target specified"
	// resolve (spell_manager.cpp:209).
	uint16_t targetId = ctx_.combat_manager ? ctx_.combat_manager->GetTargetId() : 0;
	if (targetId == 0) {
		targetId = ctx_.self.MySpawnId();
	}
	packet.PutUInt32(12, targetId);
	packet.PutUInt32(16, 0);
	ctx_.net.Queue(HC_OP_CastSpell, &packet);
	ctx_.out.System(fmt::format("Using {}", item->name));

	// Cooldown feedback to the renderer for every hotbar slot holding this item.
	if (item->clickEffect.recastDelay > 0 && ctx_.bridge && ctx_.hotbar_model) {
		for (int idx = 0; idx < ctx_.hotbar_model->getSlotCount(); ++idx) {
			const eqt::HotbarSlot& hs = ctx_.hotbar_model->getSlot({ctx_.hotbar_model->currentPage(), idx});
			if (hs.type != eqt::HotbarSlotType::Item || hs.id != item->itemId) continue;
			eqt::state::HotbarCooldownStartedData cd;
			cd.index = idx;
			cd.durationMs = static_cast<uint32_t>(item->clickEffect.recastDelay);
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
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
bool eqt::bridge::BridgeComponent::ActivateHotbarCommandSlot(eqt::HotbarSlotType type, const std::string& text) {
	switch (type) {
	case eqt::HotbarSlotType::Emote:
	case eqt::HotbarSlotType::Macro:
		if (text.empty()) {
			ctx_.out.System("Hotbar slot has no command");
			return false;
		}
		ctx_.Chat().ProcessChatInput(text);
		return true;
	case eqt::HotbarSlotType::Combat:
		if (!ctx_.combat_manager) return false;
		if (!ctx_.combat_manager->IsAutoAttackEnabled() && !ctx_.combat_manager->HasTarget()) {
			ctx_.out.System("You must have a target to attack");
			return false;
		}
		ctx_.combat_manager->ToggleAutoAttack();
		return true;
	case eqt::HotbarSlotType::Empty:
	case eqt::HotbarSlotType::Spell:
	case eqt::HotbarSlotType::Skill:
	case eqt::HotbarSlotType::Item:
		break;
	}
	return false;
}
