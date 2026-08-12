// EverQuest slash-command registration.
//
// Split out of eq.cpp (Batch F, unit F01): originally held RegisterCommands()
// — the ~2,300-line cluster of lambda-based /slash command registrations —
// plus the two manual zone-load diagnostics helpers it depends on
// (runPmemDiagnostics, runLoadDiagnostics). Moved verbatim, no behavior
// change. See refactor/F01_commands.md.
//
// Batch G (unit G02): RegisterCommands() split into a short driver plus
// per-domain Register*Commands() methods across 3 TUs (this file holds the
// driver, diagnostics helpers, and RegisterSocialCommands(); the remaining
// domains are in eq_commands_movement.cpp and eq_commands_spells.cpp). Lambda
// bodies moved verbatim; registration order preserved exactly. See
// refactor/G02_commands_split.md.

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

// T29: plain data-model headers, no renderer dependency (P28 declassified
// them; see eq_chat.cpp's identical note) -- needed unconditionally now
// that RegisterCommands()/RegisterSocialCommands() are no longer
// graphics-only (below).
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/ui/command_registry.h"
#include "client/hotbar/hotbar_model.h"
// T66: inventory_manager.h/inventory_constants.h -- same "plain data-model
// header" status as the T29 group above (verified against eq.cpp's own
// unconditional include of both): no graphics dependency, needed
// unconditionally now that RegisterEquipmentCommands()'s /equip is not
// graphics-gated either (SendMoveItem is plain network logic).
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_config_loader.h"
#include "client/graphics/weather_quality_preset.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/inventory/item_instance.h"
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
#include <limits>
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

// T29: un-guarded from EQT_HAS_GRAPHICS -- see the identical note in
// eq_commands_movement.cpp. This file has no direct graphics-API usage at
// all (verified by grep for IrrlichtRenderer/LoadingThread/Weather*/
// Detail*/Season*/Wld*/HotkeyManager); every renderer-only command already
// routed through the RendererCommand bridge queue via the RegisterMovement/
// RegisterSpells rendererCmd() helpers in their own files.

// ── Manual zone load diagnostics helpers ────────────────────────────────────

void EverQuest::runPmemDiagnostics(const std::string& label) {
	if (!m_bridge) return;

	eqt::state::DiagnosticsMemoryReportData d;
	d.label = label;

#ifdef __linux__
	{
		FILE* f = fopen("/proc/self/statm", "r");
		if (f) {
			unsigned long vm = 0, rss = 0;
			if (fscanf(f, "%lu %lu", &vm, &rss) == 2) {
				long ps = sysconf(_SC_PAGESIZE);
				d.processRssBytes = static_cast<size_t>(rss) * ps;
				d.processVmBytes = static_cast<size_t>(vm) * ps;
			}
			fclose(f);
		}
	}

	{
		FILE* f = fopen("/proc/self/smaps_rollup", "r");
		if (f) {
			char line[256];
			while (fgets(line, sizeof(line), f)) {
				unsigned long kb = 0;
				if (sscanf(line, "Shared_Clean: %lu kB", &kb) == 1 ||
				    sscanf(line, "Shared_Dirty: %lu kB", &kb) == 1) {
					d.sharedLibBytes += static_cast<size_t>(kb) * 1024;
				} else if (sscanf(line, "Anonymous: %lu kB", &kb) == 1) {
					d.anonBytes = static_cast<size_t>(kb) * 1024;
				}
			}
			fclose(f);
		}
		f = fopen("/proc/self/status", "r");
		if (f) {
			char line[256];
			while (fgets(line, sizeof(line), f)) {
				unsigned long kb = 0;
				if (sscanf(line, "VmStk: %lu kB", &kb) == 1) {
					d.stackBytes = static_cast<size_t>(kb) * 1024;
					break;
				}
			}
			fclose(f);
		}
	}
#endif

#ifdef WITH_AUDIO
	if (m_audio_manager && m_audio_manager->isInitialized()) {
		d.audioAvailable = true;
		d.soundBufferCacheBytes = m_audio_manager->getSoundBufferCacheBytes();
		d.soundBufferCacheMaxBytes = m_audio_manager->getSoundBufferCacheMaxBytes();
		d.soundFontEstimateBytes = m_audio_manager->getSoundFontMemoryEstimate();
		d.musicDecodedBytes = m_audio_manager->getMusicDecodedBytes();
		d.audioPfsArchiveBytes = m_audio_manager->getPfsArchiveCacheBytes();
		if (auto* sfx = m_audio_manager->getSfxManager()) {
			d.sfxCacheBytes = sfx->getCacheSize();
		}
	}
	if (m_zone_audio_manager) {
		d.zoneEmitterCount = m_zone_audio_manager->getEmitterCount();
		d.activeEmitterCount = m_zone_audio_manager->getActiveEmitterCount();
	}
#endif

	auto addConn = [&](const std::string& name, std::shared_ptr<EQ::Net::DaybreakConnection> conn) {
		if (!conn) return;
		auto stats = conn->GetStats();
		eqt::state::DiagnosticsMemoryReportData::ConnectionInfo ci;
		ci.name = name;
		ci.recvBytes = stats.recv_bytes;
		ci.sentBytes = stats.sent_bytes;
		ci.avgPing = conn->GetRollingPing();
		d.connections.push_back(std::move(ci));
	};
	addConn("Zone", m_zone_connection);
	addConn("World", m_world_connection);
	addConn("Login", m_login_connection);

	d.entityCount = m_entities.size();
	d.entityEstimateBytes = m_entities.size() * sizeof(Entity);
	d.doorCount = m_door_state_manager.getDoorCount();
	d.doorEstimateBytes = m_door_state_manager.getDoorCount() * sizeof(EQT::DoorState);
	if (m_spell_manager) {
		d.spellDbCount = m_spell_manager->getDatabase().getSpellCount();
		d.spellDbEstimateBytes = d.spellDbCount * 512;
	}

	m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::DiagnosticsMemoryReport, std::move(d)));
}

void EverQuest::runLoadDiagnostics(const std::string& label) {
	LOG_INFO(MOD_MAIN, "");
	LOG_INFO(MOD_MAIN, "╔══════════════════════════════════════════════════════════════");
	LOG_INFO(MOD_MAIN, "║ LOAD DIAGNOSTICS: {}", label);
	LOG_INFO(MOD_MAIN, "╚══════════════════════════════════════════════════════════════");

	// Scene dump via bridge
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::DiagnosticsSceneDump,
			eqt::state::DiagnosticsSceneDumpData{label}));
	}

	// Memory report via bridge
	runPmemDiagnostics(label);

	LOG_INFO(MOD_MAIN, "══════════════════════════════════════════════════════════════");
	LOG_INFO(MOD_MAIN, "");
}

void EverQuest::RegisterCommands()
{
	// G02: driver — calls per-domain registration methods in the exact
	// order their commands appeared in the original monolithic method.
	// This preserves observable behavior for order-sensitive registrations
	// (e.g. the duplicate "who" command name — see refactor/G02_commands_split.md).
	RegisterSocialCommands();
	RegisterMovementCommands();
	RegisterSpellCommands();
	RegisterVendorCommands();
	RegisterEquipmentCommands();
}

// ── T48: vendor slash-command argument parsing helpers ─────────────────────
// Same idiom as action::CommandProcessor::cmdLoot() (T34): std::stol with a
// consumed-length check so trailing garbage ("5abc") is rejected instead of
// silently truncated, plus an explicit range check instead of relying on
// stol's own (wider, signed) overflow behavior.
namespace {

bool ParseVendorSlot(const std::string& tok, uint32_t& out) {
	if (tok.empty()) return false;
	try {
		size_t consumed = 0;
		long v = std::stol(tok, &consumed);
		if (v < 0 || v > 0xFFFF || consumed != tok.size()) return false;
		out = static_cast<uint32_t>(v);
		return true;
	} catch (const std::exception&) {
		return false;
	}
}

bool ParseVendorQuantity(const std::string& tok, uint8_t& out) {
	if (tok.empty()) return false;
	try {
		size_t consumed = 0;
		long v = std::stol(tok, &consumed);
		if (v < 1 || v > 0xFF || consumed != tok.size()) return false;
		out = static_cast<uint8_t>(v);
		return true;
	} catch (const std::exception&) {
		return false;
	}
}

// T66: /equip slot argument parsing. Same consumed-length idiom as
// ParseVendorSlot above, but signed/int16_t range since inventory slot ids
// (include/client/inventory/inventory_constants.h) are the same type
// MoveItemIntent::fromSlot/toSlot and SendMoveItem() use -- no separate
// slot numbering invented here (T57's ruling: that enum/header is canonical).
bool ParseEquipSlot(const std::string& tok, int16_t& out) {
	if (tok.empty()) return false;
	try {
		size_t consumed = 0;
		long v = std::stol(tok, &consumed);
		if (v < std::numeric_limits<int16_t>::min() || v > std::numeric_limits<int16_t>::max() ||
		    consumed != tok.size()) {
			return false;
		}
		out = static_cast<int16_t>(v);
		return true;
	} catch (const std::exception&) {
		return false;
	}
}

// T66: mirrors InventoryManager::isValidSlot() (inventory_manager.cpp) --
// that method is private (an internal helper for the UI's own drag/drop
// path), so this reproduces its exact OR-chain over the same public
// inventory_constants.h predicates rather than reaching into
// InventoryManager's internals or widening its access for one caller.
// Any recognized slot category (equipment/general/bag/cursor/bank/shared
// bank/bank-bag/shared-bank-bag/trade/trade-bag) passes; this does NOT
// mean the move is a legal equip (item type, class, etc.) -- that
// judgment stays server-side, per the plan's scope fence.
bool IsRecognizedInventorySlot(int16_t slot) {
	using namespace eqt::inventory;
	return isEquipmentSlot(slot) ||
	       isGeneralSlot(slot) ||
	       isBagSlot(slot) ||
	       isCursorSlot(slot) ||
	       isBankSlot(slot) ||
	       isSharedBankSlot(slot) ||
	       isBankBagSlot(slot) ||
	       isSharedBankBagSlot(slot) ||
	       isTradeSlot(slot) ||
	       isTradeBagSlot(slot);
}

// Friendlier reply text: names the slot when it's a canonical equipment
// slot (getSlotName(), inventory_constants.h), otherwise just the number --
// getSlotName() itself returns "Unknown" for anything outside 0-21, which
// would be a worse message than just showing the slot id.
std::string DescribeEquipSlot(int16_t slot) {
	if (eqt::inventory::isEquipmentSlot(slot)) {
		return fmt::format("{} ({})", eqt::inventory::getSlotName(slot), slot);
	}
	return fmt::format("slot {}", slot);
}

}  // namespace

void EverQuest::RegisterSocialCommands()
{
	using namespace eqt::ui;

	// === Chat Commands ===

	Command say;
	say.name = "say";
	say.aliases = {"s"};
	say.usage = "/say <message>";
	say.description = "Say message to nearby players";
	say.category = "Chat";
	say.handler = [this](const std::string& args) {
		SendChatMessage(args, "say");
	};
	m_command_registry->registerCommand(say);

	Command shout;
	shout.name = "shout";
	shout.aliases = {"sho"};
	shout.usage = "/shout <message>";
	shout.description = "Shout message to entire zone";
	shout.category = "Chat";
	shout.handler = [this](const std::string& args) {
		SendChatMessage(args, "shout");
	};
	m_command_registry->registerCommand(shout);

	Command ooc;
	ooc.name = "ooc";
	ooc.aliases = {"o"};
	ooc.usage = "/ooc <message>";
	ooc.description = "Out of character message";
	ooc.category = "Chat";
	ooc.handler = [this](const std::string& args) {
		SendChatMessage(args, "ooc");
	};
	m_command_registry->registerCommand(ooc);

	Command auction;
	auction.name = "auction";
	auction.aliases = {"auc"};
	auction.usage = "/auction <message>";
	auction.description = "Auction channel message";
	auction.category = "Chat";
	auction.handler = [this](const std::string& args) {
		SendChatMessage(args, "auction");
	};
	m_command_registry->registerCommand(auction);

	Command gsay;
	gsay.name = "gsay";
	gsay.aliases = {"g"};
	gsay.usage = "/gsay <message>";
	gsay.description = "Group chat message";
	gsay.category = "Chat";
	gsay.handler = [this](const std::string& args) {
		SendChatMessage(args, "group");
	};
	m_command_registry->registerCommand(gsay);

	Command gu;
	gu.name = "gu";
	gu.aliases = {"guildsay"};
	gu.usage = "/gu <message>";
	gu.description = "Guild chat message";
	gu.category = "Chat";
	gu.handler = [this](const std::string& args) {
		SendChatMessage(args, "guild");
	};
	m_command_registry->registerCommand(gu);

	Command tell;
	tell.name = "tell";
	tell.aliases = {"t", "msg"};
	tell.usage = "/tell <player> <message>";
	tell.description = "Send private message to player";
	tell.category = "Chat";
	tell.requiresArgs = true;
	tell.handler = [this](const std::string& args) {
		size_t space_pos = args.find(' ');
		if (space_pos != std::string::npos) {
			std::string target = args.substr(0, space_pos);
			std::string message = args.substr(space_pos + 1);
			SendChatMessage(message, "tell", target);
		} else if (!args.empty()) {
			AddChatSystemMessage("Usage: /tell <player> <message>");
		}
	};
	m_command_registry->registerCommand(tell);

	Command emote;
	emote.name = "emote";
	emote.aliases = {"em", "me"};
	emote.usage = "/emote <action>";
	emote.description = "Perform custom emote";
	emote.category = "Chat";
	emote.handler = [this](const std::string& args) {
		SendChatMessage(args, "emote");
	};
	m_command_registry->registerCommand(emote);

	Command reply;
	reply.name = "reply";
	reply.aliases = {"r"};
	reply.usage = "/reply <message>";
	reply.description = "Reply to last tell";
	reply.category = "Chat";
	reply.handler = [this](const std::string& args) {
		if (m_last_tell_sender.empty()) {
			AddChatSystemMessage("No one to reply to");
			return;
		}
		if (args.empty()) {
			AddChatSystemMessage("Usage: /reply <message>");
			return;
		}
		SendChatMessage(args, "tell", m_last_tell_sender);
	};
	m_command_registry->registerCommand(reply);

	// === Group Commands ===

	Command invite_cmd;
	invite_cmd.name = "invite";
	invite_cmd.aliases = {"inv"};
	invite_cmd.usage = "/invite [name]";
	invite_cmd.description = "Invite player to group";
	invite_cmd.category = "Group";
	invite_cmd.handler = [this](const std::string& args) {
		std::string target_name = args;

		// If no name provided, use current target
		if (target_name.empty()) {
			if (m_combat_manager && m_combat_manager->HasTarget()) {
				uint16_t target_id = m_combat_manager->GetTargetId();
				auto it = m_entities.find(target_id);
				if (it != m_entities.end()) {
					target_name = it->second.name;
				}
			}
		}

		if (target_name.empty()) {
			AddChatSystemMessage("Usage: /invite <name> or target a player");
			return;
		}

		SendGroupInvite(target_name);
		AddChatSystemMessage(fmt::format("Inviting {} to group", EQT::toDisplayName(target_name)));
	};
	m_command_registry->registerCommand(invite_cmd);

	Command follow_cmd;
	follow_cmd.name = "follow";
	follow_cmd.aliases = {"fol"};
	follow_cmd.usage = "/follow [name]";
	follow_cmd.description = "Accept group invite or follow player";
	follow_cmd.category = "Group";
	follow_cmd.handler = [this](const std::string& args) {
		// If we have a pending invite, accept it
		if (m_has_pending_invite) {
			AcceptGroupInvite();
			return;
		}

		// Otherwise, try to follow a player
		std::string target_name = args;
		if (target_name.empty() && m_combat_manager && m_combat_manager->HasTarget()) {
			uint16_t target_id = m_combat_manager->GetTargetId();
			auto it = m_entities.find(target_id);
			if (it != m_entities.end()) {
				target_name = it->second.name;
			}
		}

		if (!target_name.empty()) {
			// Start following the target
			Follow(target_name);
			AddChatSystemMessage(fmt::format("Following {}", EQT::toDisplayName(target_name)));
		} else {
			AddChatSystemMessage("No pending invite. Use /follow <name> to follow a player.");
		}
	};
	m_command_registry->registerCommand(follow_cmd);

	Command disband_cmd;
	disband_cmd.name = "disband";
	disband_cmd.usage = "/disband";
	disband_cmd.description = "Leave or disband group";
	disband_cmd.category = "Group";
	disband_cmd.handler = [this](const std::string& args) {
		if (!m_in_group) {
			AddChatSystemMessage("You are not in a group");
			return;
		}

		if (m_is_group_leader) {
			SendGroupDisband();
			AddChatSystemMessage("Group disbanded");
		} else {
			SendLeaveGroup();
			AddChatSystemMessage("You have left the group");
		}
	};
	m_command_registry->registerCommand(disband_cmd);

	Command decline_cmd;
	decline_cmd.name = "decline";
	decline_cmd.usage = "/decline";
	decline_cmd.description = "Decline pending group invite";
	decline_cmd.category = "Group";
	decline_cmd.handler = [this](const std::string& args) {
		if (!m_has_pending_invite) {
			AddChatSystemMessage("No pending group invite");
			return;
		}
		DeclineGroupInvite();
	};
	m_command_registry->registerCommand(decline_cmd);

	// === Trade Commands ===

	Command trade_cmd;
	trade_cmd.name = "trade";
	trade_cmd.usage = "/trade [name]";
	trade_cmd.description = "Initiate trade with target or named player";
	trade_cmd.category = "Social";
	trade_cmd.handler = [this](const std::string& args) {
		if (!m_trade_manager) {
			AddChatSystemMessage("Trade not available");
			return;
		}
		if (m_trade_manager->isTrading()) {
			AddChatSystemMessage("You are already trading");
			return;
		}
		// Cannot trade while in combat
		if (m_combat_manager && m_combat_manager->GetCombatState() == COMBAT_STATE_ENGAGED) {
			AddChatSystemMessage("You cannot trade while in combat");
			return;
		}

		uint32_t target_spawn_id = 0;
		std::string target_name;

		if (args.empty()) {
			// Use current target
			if (m_combat_manager && m_combat_manager->GetTargetId() != 0) {
				target_spawn_id = m_combat_manager->GetTargetId();
				auto it = m_entities.find(target_spawn_id);
				if (it != m_entities.end()) {
					target_name = it->second.name;
				}
			}
			if (target_spawn_id == 0) {
				AddChatSystemMessage("No target - use /trade <name> or target a player");
				return;
			}
		} else {
			// Find player by name
			std::string searchName = args;
			std::transform(searchName.begin(), searchName.end(), searchName.begin(), ::tolower);

			for (const auto& [id, entity] : m_entities) {
				// Only trade with players (not corpses, class_id > 0 indicates player class)
				if (entity.class_id == 0 || entity.is_corpse) continue;

				std::string entityName = entity.name;
				std::transform(entityName.begin(), entityName.end(), entityName.begin(), ::tolower);
				if (entityName.find(searchName) != std::string::npos) {
					target_spawn_id = entity.spawn_id;
					target_name = entity.name;
					break;
				}
			}
			if (target_spawn_id == 0) {
				AddChatSystemMessage(fmt::format("No player found matching '{}'", args));
				return;
			}
		}

		// Cannot trade with self
		if (target_spawn_id == m_my_spawn_id) {
			AddChatSystemMessage("You cannot trade with yourself");
			return;
		}

		// Check distance to target (max trade range ~150 units)
		constexpr float MAX_TRADE_DISTANCE = 150.0f;
		bool isNpc = false;
		auto target_it = m_entities.find(target_spawn_id);
		if (target_it != m_entities.end()) {
			const auto& target_entity = target_it->second;
			isNpc = (target_entity.npc_type == 1);
			float dx = m_x - target_entity.x;
			float dy = m_y - target_entity.y;
			float dz = m_z - target_entity.z;
			float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
			if (distance > MAX_TRADE_DISTANCE) {
				AddChatSystemMessage("You are too far away to trade");
				return;
			}
		}

		m_trade_manager->requestTrade(target_spawn_id, target_name, isNpc);
		AddChatSystemMessage(fmt::format("Requesting trade with {}", EQT::toDisplayName(target_name)));
	};
	m_command_registry->registerCommand(trade_cmd);

	Command accept_cmd;
	accept_cmd.name = "accept";
	accept_cmd.usage = "/accept";
	accept_cmd.description = "Accept pending trade request";
	accept_cmd.category = "Social";
	accept_cmd.handler = [this](const std::string& args) {
		if (!m_trade_manager) {
			AddChatSystemMessage("Trade not available");
			return;
		}
		if (m_trade_manager->getState() != TradeState::PendingAccept) {
			AddChatSystemMessage("No pending trade request");
			return;
		}
		// Cannot accept trade while in combat
		if (m_combat_manager && m_combat_manager->GetCombatState() == COMBAT_STATE_ENGAGED) {
			AddChatSystemMessage("You cannot trade while in combat");
			return;
		}
		m_trade_manager->acceptTradeRequest();
		AddChatSystemMessage(fmt::format("Accepting trade with {}", EQT::toDisplayName(m_trade_manager->getPartnerName())));
	};
	m_command_registry->registerCommand(accept_cmd);

	Command tradedecline_cmd;
	tradedecline_cmd.name = "tradedecline";
	tradedecline_cmd.aliases = {"rejecttrade"};
	tradedecline_cmd.usage = "/tradedecline";
	tradedecline_cmd.description = "Decline pending trade request";
	tradedecline_cmd.category = "Social";
	tradedecline_cmd.handler = [this](const std::string& args) {
		if (!m_trade_manager) {
			AddChatSystemMessage("Trade not available");
			return;
		}
		if (m_trade_manager->getState() != TradeState::PendingAccept) {
			AddChatSystemMessage("No pending trade request");
			return;
		}
		std::string partner_name = m_trade_manager->getPartnerName();
		m_trade_manager->rejectTradeRequest();
		AddChatSystemMessage(fmt::format("Declined trade request from {}", EQT::toDisplayName(partner_name)));
	};
	m_command_registry->registerCommand(tradedecline_cmd);

	Command canceltrade_cmd;
	canceltrade_cmd.name = "canceltrade";
	canceltrade_cmd.aliases = {"stoptrade"};
	canceltrade_cmd.usage = "/canceltrade";
	canceltrade_cmd.description = "Cancel active trade";
	canceltrade_cmd.category = "Social";
	canceltrade_cmd.handler = [this](const std::string& args) {
		if (!m_trade_manager) {
			AddChatSystemMessage("Trade not available");
			return;
		}
		if (m_trade_manager->getState() != TradeState::Active) {
			AddChatSystemMessage("You are not trading");
			return;
		}
		m_trade_manager->cancelTrade();
		AddChatSystemMessage("Trade cancelled");
	};
	m_command_registry->registerCommand(canceltrade_cmd);

	// === Resurrection Commands ===

	Command rezaccept_cmd;
	rezaccept_cmd.name = "rezaccept";
	rezaccept_cmd.aliases = {"acceptrez"};
	rezaccept_cmd.usage = "/rezaccept";
	rezaccept_cmd.description = "Accept pending resurrection offer";
	rezaccept_cmd.category = "Utility";
	rezaccept_cmd.handler = [this](const std::string& args) {
		SendRezzAnswer(true);
	};
	m_command_registry->registerCommand(rezaccept_cmd);

	Command rezdecline_cmd;
	rezdecline_cmd.name = "rezdecline";
	rezdecline_cmd.aliases = {"declinerez"};
	rezdecline_cmd.usage = "/rezdecline";
	rezdecline_cmd.description = "Decline pending resurrection offer";
	rezdecline_cmd.category = "Utility";
	rezdecline_cmd.handler = [this](const std::string& args) {
		SendRezzAnswer(false);
	};
	m_command_registry->registerCommand(rezdecline_cmd);

	// === Who Command ===

	Command who_cmd;
	who_cmd.name = "who";
	who_cmd.aliases = {"whoall"};
	who_cmd.usage = "/who [name] [level range]";
	who_cmd.description = "Search for players online";
	who_cmd.category = "Social";
	who_cmd.handler = [this](const std::string& args) {
		// Parse arguments: /who [name] [minlvl maxlvl]
		std::string name;
		int lvllow = -1, lvlhigh = -1;

		std::istringstream iss(args);
		std::string token;
		std::vector<std::string> tokens;
		while (iss >> token) {
			tokens.push_back(token);
		}

		if (!tokens.empty()) {
			// First check if first arg is a number (level range)
			bool first_is_level = false;
			try {
				lvllow = std::stoi(tokens[0]);
				first_is_level = true;
			} catch (...) {
				name = tokens[0];
			}

			if (first_is_level && tokens.size() >= 2) {
				try {
					lvlhigh = std::stoi(tokens[1]);
				} catch (...) {
					lvlhigh = lvllow;  // Single level
				}
			} else if (!first_is_level && tokens.size() >= 2) {
				try {
					lvllow = std::stoi(tokens[1]);
					if (tokens.size() >= 3) {
						lvlhigh = std::stoi(tokens[2]);
					} else {
						lvlhigh = lvllow;
					}
				} catch (...) {
					// Ignore level parsing errors
				}
			}
		}

		SendWhoAllRequest(name, lvllow, lvlhigh);
		AddChatSystemMessage("Searching for players...");
	};
	m_command_registry->registerCommand(who_cmd);

	Command inspect_cmd;
	inspect_cmd.name = "inspect";
	inspect_cmd.usage = "/inspect [name]";
	inspect_cmd.description = "Inspect target or named player";
	inspect_cmd.category = "Social";
	inspect_cmd.handler = [this](const std::string& args) {
		uint32_t target_id = 0;

		if (args.empty()) {
			// Use current target
			if (m_combat_manager && m_combat_manager->GetTargetId() != 0) {
				target_id = m_combat_manager->GetTargetId();
			}
			if (target_id == 0) {
				AddChatSystemMessage("No target - use /inspect <name> or target a player");
				return;
			}
		} else {
			// Find player by name
			std::string searchName = args;
			std::transform(searchName.begin(), searchName.end(), searchName.begin(), ::tolower);

			for (const auto& [id, entity] : m_entities) {
				std::string entityName = entity.name;
				std::transform(entityName.begin(), entityName.end(), entityName.begin(), ::tolower);
				if (entityName.find(searchName) != std::string::npos) {
					target_id = id;
					break;
				}
			}
			if (target_id == 0) {
				AddChatSystemMessage(fmt::format("Player '{}' not found", args));
				return;
			}
		}

		SendInspectRequest(target_id);
		AddChatSystemMessage("Inspecting...");
	};
	m_command_registry->registerCommand(inspect_cmd);

	// === Guild Commands ===

	Command guildinvite_cmd;
	guildinvite_cmd.name = "guildinvite";
	guildinvite_cmd.aliases = {"ginvite"};
	guildinvite_cmd.usage = "/guildinvite <name>";
	guildinvite_cmd.description = "Invite player to guild";
	guildinvite_cmd.category = "Guild";
	guildinvite_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /guildinvite <name>");
			return;
		}
		SendGuildInvite(args);
	};
	m_command_registry->registerCommand(guildinvite_cmd);

	Command guildaccept_cmd;
	guildaccept_cmd.name = "guildaccept";
	guildaccept_cmd.aliases = {"acceptguild"};
	guildaccept_cmd.usage = "/guildaccept";
	guildaccept_cmd.description = "Accept pending guild invite";
	guildaccept_cmd.category = "Guild";
	guildaccept_cmd.handler = [this](const std::string& args) {
		SendGuildInviteAccept(true);
	};
	m_command_registry->registerCommand(guildaccept_cmd);

	Command guilddecline_cmd;
	guilddecline_cmd.name = "guilddecline";
	guilddecline_cmd.aliases = {"declineguild"};
	guilddecline_cmd.usage = "/guilddecline";
	guilddecline_cmd.description = "Decline pending guild invite";
	guilddecline_cmd.category = "Guild";
	guilddecline_cmd.handler = [this](const std::string& args) {
		SendGuildInviteAccept(false);
	};
	m_command_registry->registerCommand(guilddecline_cmd);

	Command guildremove_cmd;
	guildremove_cmd.name = "guildremove";
	guildremove_cmd.aliases = {"gremove", "gkick"};
	guildremove_cmd.usage = "/guildremove <name>";
	guildremove_cmd.description = "Remove member from guild";
	guildremove_cmd.category = "Guild";
	guildremove_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /guildremove <name>");
			return;
		}
		SendGuildRemove(args);
	};
	m_command_registry->registerCommand(guildremove_cmd);

	Command guilddemote_cmd;
	guilddemote_cmd.name = "guilddemote";
	guilddemote_cmd.aliases = {"gdemote"};
	guilddemote_cmd.usage = "/guilddemote <name>";
	guilddemote_cmd.description = "Demote guild member";
	guilddemote_cmd.category = "Guild";
	guilddemote_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /guilddemote <name>");
			return;
		}
		SendGuildDemote(args);
	};
	m_command_registry->registerCommand(guilddemote_cmd);

	Command guildleader_cmd;
	guildleader_cmd.name = "guildleader";
	guildleader_cmd.aliases = {"gleader"};
	guildleader_cmd.usage = "/guildleader <name>";
	guildleader_cmd.description = "Transfer guild leadership";
	guildleader_cmd.category = "Guild";
	guildleader_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /guildleader <name>");
			return;
		}
		SendGuildLeader(args);
	};
	m_command_registry->registerCommand(guildleader_cmd);

	Command guildmotd_cmd;
	guildmotd_cmd.name = "guildmotd";
	guildmotd_cmd.aliases = {"gmotd"};
	guildmotd_cmd.usage = "/guildmotd [message]";
	guildmotd_cmd.description = "View or set guild MOTD";
	guildmotd_cmd.category = "Guild";
	guildmotd_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			SendGetGuildMOTD();
		} else {
			SendSetGuildMOTD(args);
		}
	};
	m_command_registry->registerCommand(guildmotd_cmd);

	// === Phase 3: Corpse Commands ===

	Command corpsedrag_cmd;
	corpsedrag_cmd.name = "corpsedrag";
	corpsedrag_cmd.aliases = {"drag"};
	corpsedrag_cmd.usage = "/corpsedrag <corpse_name>";
	corpsedrag_cmd.description = "Start dragging a corpse";
	corpsedrag_cmd.category = "Corpse";
	corpsedrag_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /corpsedrag <corpse_name>");
			return;
		}
		SendCorpseDrag(args);
	};
	m_command_registry->registerCommand(corpsedrag_cmd);

	Command corpsedrop_cmd;
	corpsedrop_cmd.name = "corpsedrop";
	corpsedrop_cmd.aliases = {"drop"};
	corpsedrop_cmd.usage = "/corpsedrop";
	corpsedrop_cmd.description = "Drop the corpse you are dragging";
	corpsedrop_cmd.category = "Corpse";
	corpsedrop_cmd.handler = [this](const std::string& args) {
		SendCorpseDrop();
	};
	m_command_registry->registerCommand(corpsedrop_cmd);

	Command consent_cmd;
	consent_cmd.name = "consent";
	consent_cmd.usage = "/consent <player_name>";
	consent_cmd.description = "Grant permission to drag your corpse";
	consent_cmd.category = "Corpse";
	consent_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /consent <player_name>");
			return;
		}
		SendConsent(args);
	};
	m_command_registry->registerCommand(consent_cmd);

	Command deny_cmd;
	deny_cmd.name = "deny";
	deny_cmd.usage = "/deny <player_name>";
	deny_cmd.description = "Revoke permission to drag your corpse";
	deny_cmd.category = "Corpse";
	deny_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /deny <player_name>");
			return;
		}
		SendConsentDeny(args);
	};
	m_command_registry->registerCommand(deny_cmd);

	// === Phase 3: Combat Targeting Commands ===

	Command assist_cmd;
	assist_cmd.name = "assist";
	assist_cmd.aliases = {"a"};
	assist_cmd.usage = "/assist [target]";
	assist_cmd.description = "Assist your target or specified player";
	assist_cmd.category = "Combat";
	assist_cmd.handler = [this](const std::string& args) {
		uint32_t target_id = 0;
		if (args.empty()) {
			// Use current target. T28: repointed from m_game_state.combat()
			// (state-layer CombatState target fields, never populated in
			// production -- see refactor/deferred.md "Dead parallel state
			// copies...") to the authoritative CombatManager, matching
			// eq_bridge.cpp's usage.
			target_id = (m_combat_manager && m_combat_manager->HasTarget())
				? m_combat_manager->GetTargetId() : 0;
			if (target_id == 0) {
				AddChatSystemMessage("You don't have a target.");
				return;
			}
		} else {
			// Find player by name
			Entity* entity = FindEntityByName(args);
			if (entity) {
				target_id = entity->spawn_id;
			} else {
				AddChatSystemMessage(fmt::format("Could not find '{}'", args));
				return;
			}
		}
		SendAssist(target_id);
	};
	m_command_registry->registerCommand(assist_cmd);

	// === Phase 3: Group Commands ===

	Command split_cmd;
	split_cmd.name = "split";
	split_cmd.usage = "/split <platinum> [gold] [silver] [copper]";
	split_cmd.description = "Split money with your group";
	split_cmd.category = "Group";
	split_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /split <platinum> [gold] [silver] [copper]");
			return;
		}
		uint32_t p = 0, g = 0, s = 0, c = 0;
		std::istringstream iss(args);
		iss >> p >> g >> s >> c;
		SendSplit(p, g, s, c);
	};
	m_command_registry->registerCommand(split_cmd);

	// === Phase 3: LFG Commands ===

	Command lfg_cmd;
	lfg_cmd.name = "lfg";
	lfg_cmd.usage = "/lfg";
	lfg_cmd.description = "Toggle Looking For Group status";
	lfg_cmd.category = "Social";
	lfg_cmd.handler = [this](const std::string& args) {
		SendLFGCommand(!m_is_lfg);
	};
	m_command_registry->registerCommand(lfg_cmd);

	// === Phase 3: Combat Ability Commands ===

	Command shield_cmd;
	shield_cmd.name = "shield";
	shield_cmd.usage = "/shield";
	shield_cmd.description = "Shield your current target (Warrior/Paladin)";
	shield_cmd.category = "Combat";
	shield_cmd.handler = [this](const std::string& args) {
		// T28: repointed from the never-populated state-layer CombatState
		// target to the authoritative CombatManager (see /assist above).
		uint32_t target_id = (m_combat_manager && m_combat_manager->HasTarget())
			? m_combat_manager->GetTargetId() : 0;
		if (target_id == 0) {
			AddChatSystemMessage("You don't have a target to shield.");
			return;
		}
		SendShielding(target_id);
	};
	m_command_registry->registerCommand(shield_cmd);

	// === Phase 3: Misc Commands ===

	Command save_cmd;
	save_cmd.name = "save";
	save_cmd.usage = "/save";
	save_cmd.description = "Request character save";
	save_cmd.category = "Utility";
	save_cmd.handler = [this](const std::string& args) {
		SendSave();
	};
	m_command_registry->registerCommand(save_cmd);

	// === Phase 4: Duel Commands ===

	Command duel_cmd;
	duel_cmd.name = "duel";
	duel_cmd.usage = "/duel";
	duel_cmd.description = "Challenge your target to a duel";
	duel_cmd.category = "Combat";
	duel_cmd.handler = [this](const std::string& args) {
		// T28: repointed from the never-populated state-layer CombatState
		// target to the authoritative CombatManager (see /assist above).
		uint32_t target_id = (m_combat_manager && m_combat_manager->HasTarget())
			? m_combat_manager->GetTargetId() : 0;
		if (target_id == 0) {
			AddChatSystemMessage("You don't have a target.");
			return;
		}
		SendDuelRequest(target_id);
	};
	m_command_registry->registerCommand(duel_cmd);

	Command duelaccept_cmd;
	duelaccept_cmd.name = "duelaccept";
	duelaccept_cmd.usage = "/duelaccept";
	duelaccept_cmd.description = "Accept a duel challenge";
	duelaccept_cmd.category = "Combat";
	duelaccept_cmd.handler = [this](const std::string& args) {
		if (!m_has_pending_duel) {
			AddChatSystemMessage("You have no pending duel challenge.");
			return;
		}
		SendDuelAccept(m_duel_initiator_id);
	};
	m_command_registry->registerCommand(duelaccept_cmd);

	Command dueldecline_cmd;
	dueldecline_cmd.name = "dueldecline";
	dueldecline_cmd.usage = "/dueldecline";
	dueldecline_cmd.description = "Decline a duel challenge";
	dueldecline_cmd.category = "Combat";
	dueldecline_cmd.handler = [this](const std::string& args) {
		if (!m_has_pending_duel) {
			AddChatSystemMessage("You have no pending duel challenge.");
			return;
		}
		SendDuelDecline(m_duel_initiator_id);
	};
	m_command_registry->registerCommand(dueldecline_cmd);

	// === Phase 4: Skill Commands ===

	Command bindwound_cmd;
	bindwound_cmd.name = "bandage";
	bindwound_cmd.aliases = {"bindwound"};
	bindwound_cmd.usage = "/bandage";
	bindwound_cmd.description = "Use bind wound skill on target";
	bindwound_cmd.category = "Skills";
	bindwound_cmd.handler = [this](const std::string& args) {
		// T28: repointed from the never-populated state-layer CombatState
		// target to the authoritative CombatManager (see /assist above).
		uint32_t target_id = (m_combat_manager && m_combat_manager->HasTarget())
			? m_combat_manager->GetTargetId() : 0;
		if (target_id == 0) {
			target_id = m_my_spawn_id;  // Self-target
		}
		SendBindWound(target_id);
	};
	m_command_registry->registerCommand(bindwound_cmd);

	// === Phase 4: Cosmetic Commands ===

	Command surname_cmd;
	surname_cmd.name = "surname";
	surname_cmd.usage = "/surname <lastname>";
	surname_cmd.description = "Request to set your surname";
	surname_cmd.category = "Social";
	surname_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /surname <lastname>");
			return;
		}
		SendSurname(args);
	};
	m_command_registry->registerCommand(surname_cmd);

	// === Phase 4: Misc Commands ===

	Command random_cmd;
	random_cmd.name = "random";
	random_cmd.aliases = {"roll"};
	random_cmd.usage = "/random [low] [high]";
	random_cmd.description = "Roll a random number";
	random_cmd.category = "Social";
	random_cmd.handler = [this](const std::string& args) {
		uint32_t low = 0, high = 100;
		std::istringstream iss(args);
		iss >> low >> high;
		if (high < low) high = low;
		SendRandom(low, high);
	};
	m_command_registry->registerCommand(random_cmd);

	Command rewind_cmd;
	rewind_cmd.name = "rewind";
	rewind_cmd.usage = "/rewind";
	rewind_cmd.description = "Attempt to return to a safe position";
	rewind_cmd.category = "Movement";
	rewind_cmd.handler = [this](const std::string& args) {
		SendRewind();
	};
	m_command_registry->registerCommand(rewind_cmd);

	Command yell_cmd;
	yell_cmd.name = "yell";
	yell_cmd.usage = "/yell";
	yell_cmd.description = "Yell for help";
	yell_cmd.category = "Social";
	yell_cmd.handler = [this](const std::string& args) {
		SendYellForHelp();
	};
	m_command_registry->registerCommand(yell_cmd);

	Command bug_cmd;
	bug_cmd.name = "bug";
	bug_cmd.aliases = {"report"};
	bug_cmd.usage = "/bug <description>";
	bug_cmd.description = "Submit a bug report";
	bug_cmd.category = "Utility";
	bug_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /bug <description>");
			return;
		}
		SendReport(args);
	};
	m_command_registry->registerCommand(bug_cmd);

	Command petition_cmd;
	petition_cmd.name = "petition";
	petition_cmd.usage = "/petition <text>";
	petition_cmd.description = "Submit a petition";
	petition_cmd.category = "Utility";
	petition_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /petition <text>");
			return;
		}
		SendPetition(args);
	};
	m_command_registry->registerCommand(petition_cmd);
}

// T48: vendor buy/sell were previously reachable ONLY through the renderer's
// vendor-window UI gesture -- VendorBuyIntent/VendorSellIntent existed as
// types with working handlers in ProcessBridgeIntents() (eq_bridge.cpp:
// 1069-1074, dispatching to BuyFromVendor()/SellToVendor()), but nothing
// anywhere constructed either intent (refactor/deferred.md "PATTERN: real
// features reachable only through a UI gesture, invisible to automation";
// refactor/T48_vendor_reachability.md). T42 had to add temporary scratch
// hooks calling BuyFromVendor()/SellToVendor() directly just to run its live
// acceptance, then revert them.
//
// These three commands close that gap. /vendorlist surfaces m_vendor_items
// (populated in memory by ZoneProcessVendorItemToUI, eq_merchant.cpp) so a
// script can discover what's for sale without guessing slot numbers -- T42's
// exact blocker. /buy and /sell push the SAME VendorBuyIntent/VendorSellIntent
// the vendor-window gesture pushes (maximum-fidelity choice; see the
// commentary on buy_cmd's handler below for exactly which hops differ from
// the gesture's). All three default to the currently open vendor
// (GetVendorNpcId()/IsVendorWindowOpen()) rather than making the caller pass
// an npc id, per the plan.
void EverQuest::RegisterVendorCommands()
{
	using namespace eqt::ui;

	// /vendor was added alongside /vendorlist, /buy, /sell once scoping this
	// unit's mandated live acceptance ("open a vendor, list its inventory,
	// buy an item, sell it back -- all via piped commands, no scratch
	// hooks") surfaced that VendorToggleIntent -- the intent the renderer's
	// right-click-a-merchant gesture pushes to OPEN the window in the first
	// place (renderer_frame.cpp:618) -- has exactly the same problem as
	// VendorBuyIntent/VendorSellIntent: its handler exists in
	// ProcessBridgeIntents() (eq_bridge.cpp:866, validating target/NPC-type/
	// class==merchant then calling RequestOpenVendor()), but nothing ever
	// constructed it either. Without a command-channel way to OPEN a vendor,
	// the plan's own mandated live acceptance would have been impossible to
	// run without a scratch hook -- precisely what this unit exists to
	// avoid. This command pushes the SAME VendorToggleIntent{} the gesture
	// pushes (same fidelity choice as /buy and /sell below), so it is a
	// direct, in-spirit extension of the T48 pattern, not a new feature:
	// the actual open/close logic (target/NPC-type/class validation,
	// RequestOpenVendor()/CloseVendorWindow()) is untouched, unmoved,
	// pre-existing code.
	Command vendor_cmd;
	vendor_cmd.name = "vendor";
	vendor_cmd.usage = "/vendor";
	vendor_cmd.description = "Open (or close) a vendor window with your current target";
	vendor_cmd.category = "Commerce";
	vendor_cmd.handler = [this](const std::string& /*args*/) {
		if (!IsVendorWindowOpen() && (!m_combat_manager || !m_combat_manager->HasTarget())) {
			AddChatSystemMessage("You don't have a target - target a merchant first.");
			return;
		}
		if (m_bridge) {
			m_bridge->pushIntent(eqt::events::VendorToggleIntent{});
		} else {
			AddChatSystemMessage("Cannot open vendor - no bridge attached.");
		}
	};
	m_command_registry->registerCommand(vendor_cmd);

	Command vendorlist_cmd;
	vendorlist_cmd.name = "vendorlist";
	vendorlist_cmd.usage = "/vendorlist";
	vendorlist_cmd.description = "List the open vendor's inventory (slot, name, price, quantity)";
	vendorlist_cmd.category = "Commerce";
	vendorlist_cmd.handler = [this](const std::string& /*args*/) {
		if (!IsVendorWindowOpen()) {
			AddChatSystemMessage("You are not at a vendor.");
			return;
		}
		if (m_vendor_items.empty()) {
			AddChatSystemMessage(fmt::format("{}'s inventory is empty (or still loading).", m_vendor_name));
			return;
		}
		AddChatSystemMessage(fmt::format("{}'s inventory ({} item{}):",
			m_vendor_name, m_vendor_items.size(), m_vendor_items.size() == 1 ? "" : "s"));
		for (const auto& [slot, item] : m_vendor_items) {
			std::string qtyStr = (item.quantity < 0) ? "unlimited" : std::to_string(item.quantity);
			AddChatSystemMessage(fmt::format("  [slot {}] {} - {} (qty: {})",
				slot, item.name, FormatPrice(item.price), qtyStr));
		}
	};
	m_command_registry->registerCommand(vendorlist_cmd);

	Command buy_cmd;
	buy_cmd.name = "buy";
	buy_cmd.usage = "/buy <slot> [quantity]";
	buy_cmd.description = "Buy an item from the open vendor (see /vendorlist)";
	buy_cmd.category = "Commerce";
	buy_cmd.handler = [this](const std::string& args) {
		if (!IsVendorWindowOpen()) {
			AddChatSystemMessage("You are not at a vendor.");
			return;
		}
		if (args.empty()) {
			AddChatSystemMessage("Usage: /buy <slot> [quantity] - see /vendorlist");
			return;
		}

		std::istringstream iss(args);
		std::string slotTok, qtyTok;
		iss >> slotTok;
		iss >> qtyTok;  // optional; empty if not present

		uint32_t slot = 0;
		if (!ParseVendorSlot(slotTok, slot)) {
			AddChatSystemMessage("Invalid slot - see /vendorlist");
			return;
		}

		uint8_t quantity = 1;  // default, per the plan ("optional quantity")
		if (!qtyTok.empty() && !ParseVendorQuantity(qtyTok, quantity)) {
			AddChatSystemMessage("Invalid quantity (must be 1-255)");
			return;
		}

		if (m_vendor_items.find(slot) == m_vendor_items.end()) {
			AddChatSystemMessage(fmt::format("No item in vendor slot {} - see /vendorlist", slot));
			return;
		}

		// FIDELITY (maximum-fidelity choice, per the plan): this pushes the
		// exact same eqt::events::VendorBuyIntent the renderer's vendor-window
		// "buy" click pushes. From here every hop is IDENTICAL to the
		// gesture's: the intent sits in GameStateBridge's intent queue and is
		// drained by the same EverQuest::ProcessBridgeIntents() ->
		// BuyFromVendor(npcId, slot, quantity) dispatch (eq_bridge.cpp:1069)
		// on the next GameTick(), which (T45) now runs in headless mode too.
		// The ONE hop that differs: WHO calls pushIntent() and on which
		// thread. The gesture's caller is renderer code on the render
		// thread, crossing to the game thread via the bridge. Here the
		// caller is EverQuest itself (m_command_registry's handler, which
		// piped-command dispatch invokes on the GAME thread -- see
		// Application::DispatchSlashCommand()/dispatchToActionOrRenderer()'s
		// registry-1 stage), so push and the later drain both happen on the
		// game thread. GameStateBridge::pushIntent()'s mutex makes this safe
		// either way; it is an honest difference in the calling thread, not
		// a gap in what gets dispatched or how.
		if (m_bridge) {
			m_bridge->pushIntent(eqt::events::VendorBuyIntent{
				GetVendorNpcId(), static_cast<uint16_t>(slot), quantity});
		} else {
			AddChatSystemMessage("Cannot buy - no bridge attached.");
		}
	};
	m_command_registry->registerCommand(buy_cmd);

	Command sell_cmd;
	sell_cmd.name = "sell";
	sell_cmd.usage = "/sell <slot> [quantity]";
	sell_cmd.description = "Sell an item from your inventory to the open vendor";
	sell_cmd.category = "Commerce";
	sell_cmd.handler = [this](const std::string& args) {
		if (!IsVendorWindowOpen()) {
			AddChatSystemMessage("You are not at a vendor.");
			return;
		}
		if (args.empty()) {
			AddChatSystemMessage("Usage: /sell <slot> [quantity] - <slot> is your inventory slot");
			return;
		}

		std::istringstream iss(args);
		std::string slotTok, qtyTok;
		iss >> slotTok;
		iss >> qtyTok;

		uint32_t slot = 0;
		if (!ParseVendorSlot(slotTok, slot)) {
			AddChatSystemMessage("Invalid slot");
			return;
		}

		uint8_t quantity = 1;
		if (!qtyTok.empty() && !ParseVendorQuantity(qtyTok, quantity)) {
			AddChatSystemMessage("Invalid quantity (must be 1-255)");
			return;
		}

		// Same fidelity note as /buy above: pushes the SAME VendorSellIntent
		// the vendor-window gesture pushes, drained by the same
		// ProcessBridgeIntents() -> SellToVendor() dispatch. Item-in-slot /
		// NO_DROP / stack-size validation stays entirely inside SellToVendor()
		// (eq_merchant.cpp) -- unchanged, per the plan's fence against
		// touching that function -- so this handler does not duplicate it.
		if (m_bridge) {
			m_bridge->pushIntent(eqt::events::VendorSellIntent{
				GetVendorNpcId(), static_cast<uint16_t>(slot), quantity});
		} else {
			AddChatSystemMessage("Cannot sell - no bridge attached.");
		}
	};
	m_command_registry->registerCommand(sell_cmd);
}

// T66 -- instance 8 of "reachable only through a UI gesture, invisible to
// automation" (refactor/deferred.md): EverQuest::SendMoveItem(fromSlot,
// toSlot, quantity) (eq_merchant.cpp, opcode HC_OP_MoveItem=0x420f) is the
// real protocol path and was already fully implemented; nothing invoked it
// from a command. T63 had to write inventory.item_id directly into the DB
// and restart the client to get a character holding a weapon at all.
//
// /equip pushes the SAME MoveItemIntent{fromSlot, toSlot, quantity} the
// inventory UI's drag-and-drop gesture pushes (inventory_manager.cpp),
// drained by the same ProcessBridgeIntents() -> SendMoveItem() dispatch --
// same maximum-fidelity choice T48/T50 made for vendor/bank. Slot ids use
// the canonical eqt::inventory::EquipmentSlot enum/ranges
// (inventory_constants.h, T57's ruling) -- no numbering invented here.
//
// Scope discipline (per the plan's fence): this is a thin wrapper. No
// inventory management, no swap semantics, no client-side prediction of the
// result -- the server is authoritative and the existing (untouched)
// ZoneProcessMoveItem path handles the reply. Validation below only rejects
// slot ids that don't correspond to any recognized inventory location
// (InventoryManager::isValidSlot(), the same check the UI's own drag/drop
// path already applies) -- it does not second-guess whether a specific
// move is a *legal* equip (item type vs. slot, class restrictions, etc.);
// that is exactly the swap-semantics judgment the plan says stays server-side.
//
// The live end-to-end leg (does a real packet actually equip a real item on
// a real server) is explicitly OUT of this unit's territory -- sibling T65
// holds the eqemu lock this round. See the plan's Review for what is
// covered here (wiring: registered, parsed, invalid slots rejected, right
// intent pushed with right fields) vs. what remains outstanding.
void EverQuest::RegisterEquipmentCommands()
{
	using namespace eqt::ui;

	Command equip_cmd;
	equip_cmd.name = "equip";
	equip_cmd.usage = "/equip <fromSlot> <toSlot>";
	equip_cmd.description = "Move an item between inventory slots (e.g. equip a weapon: /equip 22 13)";
	equip_cmd.category = "Inventory";
	equip_cmd.handler = [this](const std::string& args) {
		std::istringstream iss(args);
		std::string fromTok, toTok;
		iss >> fromTok;
		iss >> toTok;

		int16_t fromSlot = 0;
		int16_t toSlot = 0;
		if (fromTok.empty() || toTok.empty() ||
		    !ParseEquipSlot(fromTok, fromSlot) || !ParseEquipSlot(toTok, toSlot)) {
			AddChatSystemMessage("Usage: /equip <fromSlot> <toSlot> (see include/client/inventory/inventory_constants.h for slot ids)");
			return;
		}

		if (!IsRecognizedInventorySlot(fromSlot)) {
			AddChatSystemMessage(fmt::format("Invalid source slot: {}", fromSlot));
			return;
		}
		if (!IsRecognizedInventorySlot(toSlot)) {
			AddChatSystemMessage(fmt::format("Invalid destination slot: {}", toSlot));
			return;
		}

		if (!m_bridge) {
			AddChatSystemMessage("Cannot move item - no bridge attached.");
			return;
		}

		// T71 -- Stage 1 fix. Characterisation (refactor/T71_equip_fix_and_first_person.md
		// Review): SendMoveItem/ZoneProcessMoveItem (the wire protocol) were
		// proven correct via three live eqemu round trips (direct
		// equipment<->equipment swap, cursor-mediated swap with a clean
		// cursor, and cursor-mediated swap with a congested multi-item
		// cursor) -- all three produced correct server-side results verified
		// via fresh `inventory` table reads. The actual defect was here: this
		// handler only ever pushed the wire-level MoveItemIntent and never
		// touched InventoryManager's own local model (items_/cursorQueue_)
		// or fired EquipmentChangedIntent. The protocol has no
		// success-acknowledgement (only a correction on failure -- see
		// ZoneProcessMoveItem/processMoveItemResponse), so a request that the
		// server silently accepts left the client's own inventory view (and
		// therefore the rendered/paperdoll equipment appearance, which reads
		// from that same local model in UpdatePlayerAppearanceFromInventory())
		// stale until an unrelated event (zoning, relogging, or a drag/drop
		// gesture touching the same slot) happened to refresh it. Drag/drop's
		// own gesture (InventoryManager::pickupItem()/placeItem()) never hits
		// this gap because it already updates local state (and fires
		// EquipmentChangedIntent) before sending -- exactly mirroring how the
		// real EQ protocol expects a client to behave (optimistic local
		// apply, corrected server-side only on failure). This fix does not
		// change that division of labor or add any new legality judgment
		// here -- the server remains the sole authority on whether a move is
		// ALLOWED (T66's "no swap semantics" rule stands unchanged); it only
		// keeps the local mirror in sync with what was requested, and the
		// existing resync-on-failure path (already proven live, see the
		// Review) corrects it back if the server refuses.
		//
		// Cursor-involving moves (either endpoint is CURSOR_SLOT/
		// SERVER_CURSOR_SLOT) are delegated to InventoryManager's own
		// pickupItem()/placeItem() -- already-correct, already-tested code
		// that pushes the IDENTICAL MoveItemIntent{fromSlot,toSlot,0} this
		// handler used to push directly, so the wire behaviour for these
		// cases is unchanged; only the missing local sync is added, for
		// free. A direct slot<->slot move where NEITHER endpoint is the
		// cursor (e.g. /equip 13 14, primary<->secondary) has no existing
		// helper that preserves the single-packet atomic swap the server's
		// own SwapItem() already performs correctly (proven live) -- for
		// that case only, this mirrors the two map entries locally via the
		// public getItem()/setItem()/removeItem() API (no legality checks,
		// matching this handler's existing "recognized slot" -- not
		// "legal move" -- validation bar) and still sends the SAME single
		// MoveItemIntent{fromSlot,toSlot,0} as before.
		if (IsRecognizedInventorySlot(fromSlot) &&
		    eqt::inventory::isCursorSlot(fromSlot) && !eqt::inventory::isCursorSlot(toSlot)) {
			// e.g. /equip 30 14 -- cursor -> a real slot.
			if (!m_inventory_manager || !m_inventory_manager->hasCursorItem()) {
				AddChatSystemMessage("Cannot move item - cursor is empty.");
				return;
			}
			if (!m_inventory_manager->placeItem(toSlot)) {
				AddChatSystemMessage(fmt::format("Could not place cursor item in {}.", DescribeEquipSlot(toSlot)));
				return;
			}
		} else if (!eqt::inventory::isCursorSlot(fromSlot) && eqt::inventory::isCursorSlot(toSlot)) {
			// e.g. /equip 13 30 -- a real slot -> cursor.
			if (!m_inventory_manager || !m_inventory_manager->pickupItem(fromSlot)) {
				AddChatSystemMessage(fmt::format("Could not pick up item from {}.", DescribeEquipSlot(fromSlot)));
				return;
			}
		} else if (!eqt::inventory::isCursorSlot(fromSlot) && !eqt::inventory::isCursorSlot(toSlot)) {
			// e.g. /equip 13 14 -- direct slot<->slot swap, no cursor
			// staging (matches the server's own atomic SwapItem(), proven
			// live). No legality judgment beyond what IsRecognizedInventorySlot()
			// already checked above -- the server remains the sole arbiter
			// of whether the move is legal; this only keeps the client's own
			// display in sync with the request.
			const eqt::inventory::ItemInstance* fromItemPtr = m_inventory_manager ? m_inventory_manager->getItem(fromSlot) : nullptr;
			if (!fromItemPtr) {
				AddChatSystemMessage(fmt::format("Nothing to move: {} is empty.", DescribeEquipSlot(fromSlot)));
				return;
			}
			eqt::inventory::ItemInstance movedFromItem = *fromItemPtr;  // copy: setItem() below invalidates fromItemPtr
			const eqt::inventory::ItemInstance* toItemPtr = m_inventory_manager->getItem(toSlot);
			if (toItemPtr) {
				eqt::inventory::ItemInstance movedToItem = *toItemPtr;
				m_inventory_manager->setItem(toSlot, movedFromItem);
				m_inventory_manager->setItem(fromSlot, movedToItem);
			} else {
				m_inventory_manager->setItem(toSlot, movedFromItem);
				m_inventory_manager->removeItem(fromSlot);
			}
			m_bridge->pushIntent(eqt::events::MoveItemIntent{fromSlot, toSlot, /*quantity=*/0});
			if (eqt::inventory::isEquipmentSlot(fromSlot)) {
				m_bridge->pushIntent(eqt::events::EquipmentChangedIntent{fromSlot});
			}
			if (eqt::inventory::isEquipmentSlot(toSlot)) {
				m_bridge->pushIntent(eqt::events::EquipmentChangedIntent{toSlot});
			}
		} else {
			// Both endpoints are cursor slots (e.g. /equip 30 30) -- nothing
			// meaningful to do; fall through to the same wire request as
			// before for parity with pre-fix behaviour.
			m_bridge->pushIntent(eqt::events::MoveItemIntent{fromSlot, toSlot, /*quantity=*/0});
		}

		AddChatSystemMessage(fmt::format("Moving item: {} -> {}",
			DescribeEquipSlot(fromSlot), DescribeEquipSlot(toSlot)));
	};
	m_command_registry->registerCommand(equip_cmd);
}
