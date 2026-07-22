// EverQuest slash-command registration.
//
// Split out of eq.cpp (Batch F, unit F01): holds RegisterCommands() — the
// ~2,300-line cluster of lambda-based /slash command registrations — plus
// the two manual zone-load diagnostics helpers it depends on
// (runPmemDiagnostics, runLoadDiagnostics). Moved verbatim, no behavior
// change. See refactor/F01_commands.md.

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
#ifdef EQT_HAS_GRAPHICS

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
			// Use current target
			target_id = m_game_state.combat().targetId();
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
		uint32_t target_id = m_game_state.combat().targetId();
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
		uint32_t target_id = m_game_state.combat().targetId();
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
		uint32_t target_id = m_game_state.combat().targetId();
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

	// === Movement Commands ===

	Command loc;
	loc.name = "loc";
	loc.aliases = {"location"};
	loc.usage = "/loc";
	loc.description = "Show current coordinates";
	loc.category = "Movement";
	loc.handler = [this](const std::string& args) {
		AddChatSystemMessage(fmt::format("Your location is {:.1f}, {:.1f}, {:.1f}", m_x, m_y, m_z));
	};
	m_command_registry->registerCommand(loc);

	Command sit;
	sit.name = "sit";
	sit.usage = "/sit";
	sit.description = "Sit down";
	sit.category = "Movement";
	sit.handler = [this](const std::string& args) {
		SetPositionState(POS_SITTING);
		AddChatSystemMessage("You sit down.");
	};
	m_command_registry->registerCommand(sit);

	Command stand;
	stand.name = "stand";
	stand.usage = "/stand";
	stand.description = "Stand up";
	stand.category = "Movement";
	stand.handler = [this](const std::string& args) {
		SetPositionState(POS_STANDING);
		AddChatSystemMessage("You stand up.");
	};
	m_command_registry->registerCommand(stand);

	Command move_cmd;
	move_cmd.name = "move";
	move_cmd.usage = "/move <x> <y> <z>";
	move_cmd.description = "Move to coordinates";
	move_cmd.category = "Movement";
	move_cmd.handler = [this](const std::string& args) {
		std::istringstream iss(args);
		float x, y, z;
		if (iss >> x >> y >> z) {
			Move(x, y, z);
			AddChatSystemMessage(fmt::format("Moving to ({:.1f}, {:.1f}, {:.1f})", x, y, z));
		} else {
			AddChatSystemMessage("Usage: /move <x> <y> <z>");
		}
	};
	m_command_registry->registerCommand(move_cmd);

	Command moveto;
	moveto.name = "moveto";
	moveto.usage = "/moveto <entity_name>";
	moveto.description = "Move to named entity";
	moveto.category = "Movement";
	moveto.handler = [this](const std::string& args) {
		std::string entity = args;
		// Trim leading whitespace
		while (!entity.empty() && entity[0] == ' ') {
			entity = entity.substr(1);
		}
		if (!entity.empty()) {
			MoveToEntity(entity);
			AddChatSystemMessage(fmt::format("Moving to entity: {}", entity));
		} else {
			AddChatSystemMessage("Usage: /moveto <entity_name>");
		}
	};
	m_command_registry->registerCommand(moveto);

	Command warp;
	warp.name = "warp";
	warp.usage = "/warp [x y z | entity_name]";
	warp.description = "Instantly teleport to coordinates, entity, or current target";
	warp.category = "Movement";
	warp.handler = [this](const std::string& args) {
		float x, y, z;
		bool found_destination = false;

		std::string trimmed = args;
		while (!trimmed.empty() && trimmed[0] == ' ') {
			trimmed = trimmed.substr(1);
		}

		if (trimmed.empty()) {
			// No args - use current target if we have one
			if (m_combat_manager && m_combat_manager->HasTarget()) {
				uint16_t target_id = m_combat_manager->GetTargetId();
				auto it = m_entities.find(target_id);
				if (it != m_entities.end()) {
					x = it->second.x;
					y = it->second.y;
					z = it->second.z;
					found_destination = true;
					AddChatSystemMessage(fmt::format("Warping to target: {}", it->second.name));
				}
			}
			if (!found_destination) {
				AddChatSystemMessage("Usage: /warp [x y z | entity_name] (or target an entity first)");
				return;
			}
		} else {
			// Try to parse as coordinates first
			std::istringstream iss(trimmed);
			if (iss >> x >> y >> z) {
				found_destination = true;
			} else {
				// Try to find entity by name
				Entity* entity = FindEntityByName(trimmed);
				if (entity) {
					x = entity->x;
					y = entity->y;
					z = entity->z;
					found_destination = true;
					AddChatSystemMessage(fmt::format("Warping to entity: {}", entity->name));
				} else {
					AddChatSystemMessage(fmt::format("Entity '{}' not found", trimmed));
					return;
				}
			}
		}

		if (found_destination) {
			// Instantly set position
			m_x = x;
			m_y = y;
			m_z = z;

			// Update our entity in the entity list
			auto it = m_entities.find(m_my_spawn_id);
			if (it != m_entities.end()) {
				it->second.x = x;
				it->second.y = y;
				it->second.z = z;
			}

			// Send position update to server
			SendPositionUpdate();

			if (m_bridge) {
				eqt::state::PlayerMovedData pdata;
				pdata.x = x; pdata.y = y; pdata.z = z;
				pdata.heading = m_heading;
				pdata.dx = 0; pdata.dy = 0; pdata.dz = 0;
				pdata.isMoving = false;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::PlayerMoved, std::move(pdata)));
			}

			AddChatSystemMessage(fmt::format("Warped to ({:.1f}, {:.1f}, {:.1f})", x, y, z));
		}
	};
	m_command_registry->registerCommand(warp);

	Command follow;
	follow.name = "follow";
	follow.usage = "/follow <entity_name>";
	follow.description = "Follow an entity";
	follow.category = "Movement";
	follow.handler = [this](const std::string& args) {
		std::string entity = args;
		// Trim leading whitespace
		while (!entity.empty() && entity[0] == ' ') {
			entity = entity.substr(1);
		}
		if (!entity.empty()) {
			Follow(entity);
			AddChatSystemMessage(fmt::format("Following: {}", entity));
		} else {
			AddChatSystemMessage("Usage: /follow <entity_name>");
		}
	};
	m_command_registry->registerCommand(follow);

	Command stopfollow;
	stopfollow.name = "stopfollow";
	stopfollow.aliases = {"stop"};
	stopfollow.usage = "/stopfollow";
	stopfollow.description = "Stop following";
	stopfollow.category = "Movement";
	stopfollow.handler = [this](const std::string& args) {
		StopFollow();
		AddChatSystemMessage("Stopped following");
	};
	m_command_registry->registerCommand(stopfollow);

	// === Combat Commands ===

	Command target;
	target.name = "target";
	target.aliases = {"tar"};
	target.usage = "/target <name>";
	target.description = "Target entity by name";
	target.category = "Combat";
	target.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /target <name>");
			return;
		}
		// Find entity by name (partial match)
		std::string searchName = args;
		std::transform(searchName.begin(), searchName.end(), searchName.begin(), ::tolower);

		for (const auto& [id, entity] : m_entities) {
			std::string entityName = entity.name;
			std::transform(entityName.begin(), entityName.end(), entityName.begin(), ::tolower);
			if (entityName.find(searchName) != std::string::npos) {
				if (m_combat_manager) {
					m_combat_manager->SetTarget(entity.spawn_id);
					AddChatSystemMessage(fmt::format("Targeting: {}", EQT::toDisplayName(entity.name)));
				}
				return;
			}
		}
		AddChatSystemMessage(fmt::format("No target found matching '{}'", args));
	};
	m_command_registry->registerCommand(target);

	Command attack;
	attack.name = "attack";
	attack.aliases = {"att"};
	attack.usage = "/attack";
	attack.description = "Begin auto-attack";
	attack.category = "Combat";
	attack.handler = [this](const std::string& args) {
		if (m_combat_manager && m_combat_manager->GetTargetId() != 0) {
			m_combat_manager->EnableAutoAttack();
			AddChatSystemMessage("Auto attack ON");
		} else {
			AddChatSystemMessage("No target - auto attack not enabled");
		}
	};
	m_command_registry->registerCommand(attack);

	Command stopattack;
	stopattack.name = "stopattack";
	stopattack.usage = "/stopattack";
	stopattack.description = "Stop auto-attack";
	stopattack.category = "Combat";
	stopattack.handler = [this](const std::string& args) {
		if (m_combat_manager) {
			m_combat_manager->DisableAutoAttack();
			AddChatSystemMessage("Auto attack OFF");
		}
	};
	m_command_registry->registerCommand(stopattack);

	Command aa;
	aa.name = "aa";
	aa.aliases = {"~"};
	aa.usage = "/aa";
	aa.description = "Toggle auto-attack on/off";
	aa.category = "Combat";
	aa.handler = [this](const std::string& args) {
		if (m_combat_manager) {
			if (m_combat_manager->IsAutoAttackEnabled()) {
				m_combat_manager->DisableAutoAttack();
				AddChatSystemMessage("Auto attack OFF");
			} else if (m_combat_manager->GetTargetId() != 0) {
				m_combat_manager->EnableAutoAttack();
				AddChatSystemMessage("Auto attack ON");
			} else {
				AddChatSystemMessage("No target - auto attack not enabled");
			}
		}
	};
	m_command_registry->registerCommand(aa);

	Command combat;
	combat.name = "combat";
	combat.aliases = {};
	combat.usage = "/combat [on|off]";
	combat.description = "Enable/disable combat manager (auto-attack, hunting, etc.)";
	combat.category = "Combat";
	combat.handler = [this](const std::string& args) {
		if (!m_combat_manager) return;

		std::string arg = args;
		// Trim whitespace
		while (!arg.empty() && arg[0] == ' ') arg = arg.substr(1);
		while (!arg.empty() && arg.back() == ' ') arg.pop_back();

		if (arg.empty()) {
			// Toggle
			if (m_combat_manager->IsEnabled()) {
				m_combat_manager->Disable();
				AddChatSystemMessage("Combat manager disabled");
			} else {
				m_combat_manager->Enable();
				AddChatSystemMessage("Combat manager enabled");
			}
		} else if (arg == "on" || arg == "1" || arg == "true") {
			m_combat_manager->Enable();
			AddChatSystemMessage("Combat manager enabled");
		} else if (arg == "off" || arg == "0" || arg == "false") {
			m_combat_manager->Disable();
			AddChatSystemMessage("Combat manager disabled");
		} else {
			AddChatSystemMessage("Usage: /combat [on|off]");
		}
	};
	m_command_registry->registerCommand(combat);

	Command taunt;
	taunt.name = "taunt";
	taunt.usage = "/taunt";
	taunt.description = "Taunt current target (warrior/paladin/shadowknight ability)";
	taunt.category = "Combat";
	taunt.handler = [this](const std::string& args) {
		if (m_combat_manager && m_combat_manager->GetTargetId() != 0) {
			m_combat_manager->Taunt();
			AddChatSystemMessage("Taunting target");
		} else {
			AddChatSystemMessage("No target to taunt");
		}
	};
	m_command_registry->registerCommand(taunt);

	Command autofire;
	autofire.name = "autofire";
	autofire.aliases = {"af"};
	autofire.usage = "/autofire";
	autofire.description = "Toggle ranged auto-attack (archery/throwing)";
	autofire.category = "Combat";
	autofire.handler = [this](const std::string& args) {
		if (!m_combat_manager) return;
		if (m_combat_manager->IsAutoFireEnabled()) {
			m_combat_manager->DisableAutoFire();
			AddChatSystemMessage("Auto fire OFF");
		} else if (m_combat_manager->GetTargetId() != 0) {
			m_combat_manager->EnableAutoFire();
			AddChatSystemMessage("Auto fire ON");
		} else {
			AddChatSystemMessage("No target - auto fire not enabled");
		}
	};
	m_command_registry->registerCommand(autofire);

	// === Utility Commands ===

	Command help;
	help.name = "help";
	help.aliases = {"h", "?"};
	help.usage = "/help [command]";
	help.description = "Show help for commands";
	help.category = "Utility";
	help.handler = [this](const std::string& args) {
		if (args.empty()) {
			// Show all categories and commands
			AddChatSystemMessage("=== Available Commands ===");
			for (const auto& category : m_command_registry->getCategories()) {
				auto cmds = m_command_registry->getCommandsByCategory(category);
				if (!cmds.empty()) {
					std::string cmdList;
					for (const auto* cmd : cmds) {
						if (!cmdList.empty()) cmdList += ", ";
						cmdList += cmd->name;
					}
					AddChatSystemMessage(fmt::format("{}: {}", category, cmdList));
				}
			}
			AddChatSystemMessage("Type /help <command> for detailed help.");
		} else {
			// Show help for specific command
			const auto* cmd = m_command_registry->findCommand(args);
			if (cmd) {
				AddChatSystemMessage(fmt::format("=== {} ===", cmd->name));
				AddChatSystemMessage(cmd->description);
				AddChatSystemMessage(fmt::format("Usage: {}", cmd->usage));
				if (!cmd->aliases.empty()) {
					std::string aliases;
					for (const auto& a : cmd->aliases) {
						if (!aliases.empty()) aliases += ", ";
						aliases += "/" + a;
					}
					AddChatSystemMessage(fmt::format("Aliases: {}", aliases));
				}
			} else {
				AddChatSystemMessage(fmt::format("Unknown command: {}", args));
			}
		}
	};
	m_command_registry->registerCommand(help);

	Command quit;
	quit.name = "quit";
	quit.aliases = {"exit"};
	quit.usage = "/quit";
	quit.description = "Exit the game";
	quit.category = "Utility";
	quit.handler = [this](const std::string& args) {
		AddChatSystemMessage("Use /camp to logout safely, or /q to exit immediately.");
	};
	m_command_registry->registerCommand(quit);

	Command q;
	q.name = "q";
	q.usage = "/q";
	q.description = "Exit the game immediately";
	q.category = "Utility";
	q.handler = [this](const std::string& args) {
		RequestQuit();  // D20c4: Application checks quit flag
	};
	m_command_registry->registerCommand(q);

	Command camp;
	camp.name = "camp";
	camp.usage = "/camp";
	camp.description = "Sit down and camp out (logout)";
	camp.category = "Utility";
	camp.handler = [this](const std::string& args) {
		// Sit down
		SetPositionState(POS_SITTING);
		AddChatSystemMessage("You have begun to camp. You will log out in 30 seconds.");
		// Start camp timer
		StartCampTimer();
	};
	m_command_registry->registerCommand(camp);

	Command debug;
	debug.name = "debug";
	debug.usage = "/debug <0-6>";
	debug.description = "Set debug level";
	debug.category = "Utility";
	debug.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage(fmt::format("Debug level: {}", s_debug_level));
		} else {
			try {
				int level = std::stoi(args);
				SetDebugLevel(level);
				AddChatSystemMessage(fmt::format("Debug level set to {}", level));
			} catch (...) {
				AddChatSystemMessage("Usage: /debug <0-6>");
			}
		}
	};
	m_command_registry->registerCommand(debug);

	// D20d: Helper — renderer commands push RendererCommand event via bridge
	auto rendererCmd = [this](const std::string& cmdName) {
		return [this, cmdName](const std::string& args) {
			if (m_bridge) {
				std::string fullCmd = "/" + cmdName;
				if (!args.empty()) fullCmd += " " + args;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::RendererCommand,
					eqt::state::RendererCommandData{fullCmd}));
			}
		};
	};

	Command timestamp;
	timestamp.name = "timestamp";
	timestamp.aliases = {"timestamps", "ts"};
	timestamp.usage = "/timestamp";
	timestamp.description = "Toggle timestamps in chat";
	timestamp.category = "Utility";
	timestamp.handler = rendererCmd("timestamp");
	m_command_registry->registerCommand(timestamp);

	Command perf;
	perf.name = "perf";
	perf.aliases = {"performance", "metrics"};
	perf.usage = "/perf";
	perf.description = "Show performance metrics report";
	perf.category = "Utility";
	perf.handler = [this](const std::string& args) {
		std::string report = EQT::PerformanceMetrics::instance().generateReport();
		// Output to console
		LOG_INFO(MOD_MAIN, "{}", report);
		// Also show summary in chat
		auto stats = EQT::PerformanceMetrics::instance().getStats("Frame Time");
		if (stats.count > 0) {
			double avgFps = stats.avgMs() > 0 ? 1000.0 / stats.avgMs() : 0;
			AddChatSystemMessage(fmt::format("=== Performance Metrics ==="));
			AddChatSystemMessage(fmt::format("Startup: {} ms", EQT::PerformanceMetrics::instance().getCategoryTotalMs(EQT::MetricCategory::Startup)));
			AddChatSystemMessage(fmt::format("Zoning: {} ms", EQT::PerformanceMetrics::instance().getCategoryTotalMs(EQT::MetricCategory::Zoning)));
			AddChatSystemMessage(fmt::format("Avg FPS: {:.1f} (avg frame: {:.1f} ms)", avgFps, stats.avgMs()));
			AddChatSystemMessage(fmt::format("Frame time: min {} ms, max {} ms", stats.minMs, stats.maxMs));
			AddChatSystemMessage("Full report written to console.");
		} else {
			AddChatSystemMessage("No performance data collected yet.");
		}
	};
	m_command_registry->registerCommand(perf);

	Command reloadeffects;
	reloadeffects.name = "reloadeffects";
	reloadeffects.aliases = {"reloadfx", "reloadparticles"};
	reloadeffects.usage = "/reloadeffects";
	reloadeffects.description = "Reload environment effects settings from config/environment_effects.json";
	reloadeffects.category = "Utility";
	reloadeffects.handler = rendererCmd("reloadeffects");
	m_command_registry->registerCommand(reloadeffects);

	Command reloadspells;
	reloadspells.name = "spellreload";
	reloadspells.aliases = {"reloadspells", "reloadspellfx"};
	reloadspells.usage = "/spellreload";
	reloadspells.description = "Reload spell effect settings from config/spell_effects.json";
	reloadspells.category = "Utility";
	reloadspells.handler = [this](const std::string& args) {
		EQT::Graphics::Environment::SpellEffectsConfig::instance().reload();
		AddChatSystemMessage("Spell effect settings reloaded");
	};
	m_command_registry->registerCommand(reloadspells);

	Command reloadweather;
	reloadweather.name = "reloadweather";
	reloadweather.aliases = {"weather_reload"};
	reloadweather.usage = "/reloadweather";
	reloadweather.description = "Reload weather effects settings from config/weather_effects.json";
	reloadweather.category = "Utility";
	reloadweather.handler = rendererCmd("reloadweather");
	m_command_registry->registerCommand(reloadweather);

	Command weatherquality;
	weatherquality.name = "weatherquality";
	weatherquality.aliases = {"wq", "weatherpreset"};
	weatherquality.usage = "/weatherquality [low|medium|high|ultra|custom]";
	weatherquality.description = "Get or set weather effects quality preset";
	weatherquality.category = "Utility";
	weatherquality.handler = [this](const std::string& args) {
		auto& loader = EQT::Graphics::WeatherConfigLoader::instance();

		if (args.empty()) {
			// Show current preset
			auto preset = loader.getQualityPreset();
			std::string presetName = EQT::Graphics::WeatherQualityManager::presetToString(preset);

			AddChatSystemMessage(fmt::format("Weather quality: {}", presetName));
			AddChatSystemMessage("Available: low, medium, high, ultra, custom");
		} else {
			// Set new preset
			if (loader.setQualityPreset(args)) {
				auto preset = loader.getQualityPreset();
				std::string presetName = EQT::Graphics::WeatherQualityManager::presetToString(preset);
				AddChatSystemMessage(fmt::format("Weather quality set to: {}", presetName));
			} else {
				AddChatSystemMessage("Invalid preset. Use: low, medium, high, ultra, custom");
			}
		}
	};
	m_command_registry->registerCommand(weatherquality);

	Command frametiming;
	frametiming.name = "frametiming";
	frametiming.aliases = {"frametime", "ft"};
	frametiming.usage = "/frametiming";
	frametiming.description = "Toggle frame timing profiler (outputs breakdown every 60 frames)";
	frametiming.category = "Utility";
	frametiming.handler = rendererCmd("frametiming");
	m_command_registry->registerCommand(frametiming);

	Command simworkercmd;
	simworkercmd.name = "simworker";
	simworkercmd.aliases = {"sw"};
	simworkercmd.usage = "/simworker";
	simworkercmd.description = "Show simulation worker thread status";
	simworkercmd.category = "Utility";
	simworkercmd.handler = rendererCmd("simworker");
	m_command_registry->registerCommand(simworkercmd);

	Command sceneprofile;
	sceneprofile.name = "sceneprofile";
	sceneprofile.aliases = {"sp", "scenebreak"};
	sceneprofile.usage = "/sceneprofile";
	sceneprofile.description = "Profile scene rendering by category (zone, entities, objects, doors)";
	sceneprofile.category = "Utility";
	sceneprofile.handler = rendererCmd("sceneprofile");
	m_command_registry->registerCommand(sceneprofile);

	Command dumpscenecmd;
	dumpscenecmd.name = "dumpscene";
	dumpscenecmd.aliases = {"ds"};
	dumpscenecmd.usage = "/dumpscene";
	dumpscenecmd.description = "Dump all scene graph nodes with positions and PVS regions to console";
	dumpscenecmd.category = "Utility";
	dumpscenecmd.handler = rendererCmd("dumpscene");
	m_command_registry->registerCommand(dumpscenecmd);

	Command sortcmd;
	sortcmd.name = "sort";
	sortcmd.aliases = {};
	sortcmd.usage = "/sort";
	sortcmd.description = "Toggle front-to-back zone geometry sorting (manual draw vs scene graph)";
	sortcmd.category = "Utility";
	sortcmd.handler = rendererCmd("sort");
	m_command_registry->registerCommand(sortcmd);

	Command uploadcmd;
	uploadcmd.name = "upload";
	uploadcmd.aliases = {};
	uploadcmd.usage = "/upload";
	uploadcmd.description = "Toggle GPU upload thread (async texture/VBO uploads)";
	uploadcmd.category = "Utility";
	uploadcmd.handler = rendererCmd("upload");
	m_command_registry->registerCommand(uploadcmd);

	Command portalcmd;
	portalcmd.name = "portal";
	portalcmd.aliases = {};
	portalcmd.usage = "/portal [on|off|debug]";
	portalcmd.description = "Toggle portal occlusion or portal debug overlay";
	portalcmd.category = "Utility";
	portalcmd.handler = rendererCmd("portal");
	m_command_registry->registerCommand(portalcmd);

	Command stencilcmd;
	stencilcmd.name = "stencil";
	stencilcmd.aliases = {};
	stencilcmd.usage = "/stencil";
	stencilcmd.description = "Toggle stencil buffer debug overlay";
	stencilcmd.category = "Utility";
	stencilcmd.handler = rendererCmd("stencil");
	m_command_registry->registerCommand(stencilcmd);

	Command governorcmd;
	governorcmd.name = "governor";
	governorcmd.aliases = {"gov", "budget"};
	governorcmd.usage = "/governor [status|fps <N>|force green|yellow|red|auto]";
	governorcmd.description = "Frame budget governor: controls loading throttle based on FPS";
	governorcmd.category = "Utility";
	governorcmd.handler = rendererCmd("governor");
	m_command_registry->registerCommand(governorcmd);

	Command placeholdercmd;
	placeholdercmd.name = "placeholder";
	placeholdercmd.aliases = {};
	placeholdercmd.usage = "/placeholder";
	placeholdercmd.description = "Toggle zone placeholder mesh visibility (debug)";
	placeholdercmd.category = "Utility";
	placeholdercmd.handler = rendererCmd("placeholder");
	m_command_registry->registerCommand(placeholdercmd);

	Command plightcmd;
	plightcmd.name = "plight";
	plightcmd.aliases = {};
	plightcmd.usage = "/plight";
	plightcmd.description = "Toggle per-pixel player light (debug: isolate FPS impact)";
	plightcmd.category = "Utility";
	plightcmd.handler = rendererCmd("plight");
	m_command_registry->registerCommand(plightcmd);

	Command olightcmd;
	olightcmd.name = "olight";
	olightcmd.aliases = {};
	olightcmd.usage = "/olight";
	olightcmd.description = "Toggle object/torch point lights (debug: isolate FPS impact)";
	olightcmd.category = "Utility";
	olightcmd.handler = rendererCmd("olight");
	m_command_registry->registerCommand(olightcmd);

	Command zlightcmd;
	zlightcmd.name = "zlight";
	zlightcmd.aliases = {};
	zlightcmd.usage = "/zlight";
	zlightcmd.description = "Toggle directional sun/ambient lighting (debug: isolate FPS impact)";
	zlightcmd.category = "Utility";
	zlightcmd.handler = rendererCmd("zlight");
	m_command_registry->registerCommand(zlightcmd);

	Command lightmodecmd;
	lightmodecmd.name = "lightmode";
	lightmodecmd.aliases = {};
	lightmodecmd.usage = "/lightmode";
	lightmodecmd.description = "Toggle lighting mode: additive (default) vs unified (pointLight scaled by vertex color)";
	lightmodecmd.category = "Utility";
	lightmodecmd.handler = rendererCmd("lightmode");
	m_command_registry->registerCommand(lightmodecmd);

	Command shaderdebugcmd;
	shaderdebugcmd.name = "shaderdebug";
	shaderdebugcmd.aliases = {"sd"};
	shaderdebugcmd.usage = "/shaderdebug [0-8]";
	shaderdebugcmd.description = "Shader debug visualization (0=normal, 1=texColor, 2=vColor, 3=LUT, 4=pLight, 5=normal, 6=lightDir, 7=alpha, 8=tex*vColor)";
	shaderdebugcmd.category = "Utility";
	shaderdebugcmd.handler = rendererCmd("shaderdebug");
	m_command_registry->registerCommand(shaderdebugcmd);

	Command firecmd;
	firecmd.name = "fire";
	firecmd.aliases = {};
	firecmd.usage = "/fire";
	firecmd.description = "Toggle unified fire particle effects (point sprite emitters)";
	firecmd.category = "Utility";
	firecmd.handler = rendererCmd("fire");
	m_command_registry->registerCommand(firecmd);

	Command renderdist;
	renderdist.name = "renderdist";
	renderdist.aliases = {"clipplane", "viewdist"};
	renderdist.usage = "/renderdist [distance]";
	renderdist.description = "Get or set render distance for entities, objects, and zone geometry";
	renderdist.category = "Utility";
	renderdist.handler = rendererCmd("renderdist");
	m_command_registry->registerCommand(renderdist);

	Command clipdist;
	clipdist.name = "clipdist";
	clipdist.aliases = {"clip", "farplane"};
	clipdist.usage = "/clipdist [distance]";
	clipdist.description = "Get or set camera clip distance (far plane)";
	clipdist.category = "Utility";
	clipdist.handler = rendererCmd("clipdist");
	m_command_registry->registerCommand(clipdist);

	Command filter;
	filter.name = "filter";
	filter.usage = "/filter [channel]";
	filter.description = "Toggle chat channel filter";
	filter.detailedHelp = "Channels: say, tell, group, guild, shout, auction, ooc, emote, combat, miss, exp, loot, npc, all";
	filter.category = "Utility";
	filter.handler = rendererCmd("filter");
	m_command_registry->registerCommand(filter);

	// === Sky Commands ===

	Command sky;
	sky.name = "sky";
	sky.usage = "/sky";
	sky.description = "Toggle sky rendering on/off";
	sky.category = "Utility";
	sky.handler = rendererCmd("sky");
	m_command_registry->registerCommand(sky);

	Command skytype;
	skytype.name = "skytype";
	skytype.usage = "/skytype <id>";
	skytype.description = "Force sky type (0=default, 6=luclin, 10=grey, 11=fire, 12=storms)";
	skytype.category = "Utility";
	skytype.requiresArgs = true;
	skytype.handler = rendererCmd("skytype");
	m_command_registry->registerCommand(skytype);

	Command time_cmd;
	time_cmd.name = "time";
	time_cmd.aliases = {"settime"};
	time_cmd.usage = "/time [hour]";
	time_cmd.description = "Show or set game time (0-23)";
	time_cmd.category = "Utility";
	time_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			// Show current time
			AddChatSystemMessage(fmt::format("Game time: {:02d}:{:02d}", m_time_hour, m_time_minute));
		} else {
			// Set time
			try {
				int hour = std::stoi(args);
				if (hour < 0 || hour > 23) {
					AddChatSystemMessage("Hour must be 0-23");
					return;
				}
				m_time_hour = static_cast<uint8_t>(hour);
				m_time_minute = 0;

				// Publish TimeOfDayChanged
				if (m_bridge) {
					eqt::state::TimeOfDayChangedData tdata;
					tdata.hour = m_time_hour;
					tdata.minute = m_time_minute;
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::TimeOfDayChanged, std::move(tdata)));
				}

				AddChatSystemMessage(fmt::format("Time set to {:02d}:00", m_time_hour));
			} catch (...) {
				AddChatSystemMessage("Usage: /time [0-23]");
			}
		}
	};
	m_command_registry->registerCommand(time_cmd);

	Command who;
	who.name = "who";
	who.usage = "/who [filter]";
	who.description = "List nearby entities";
	who.category = "Social";
	who.handler = [this](const std::string& args) {
		std::string filter = args;
		std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

		int count = 0;
		for (const auto& [id, entity] : m_entities) {
			std::string name = entity.name;
			std::transform(name.begin(), name.end(), name.begin(), ::tolower);
			if (filter.empty() || name.find(filter) != std::string::npos) {
				AddChatSystemMessage(fmt::format("[{}] {} (Lvl {})", entity.spawn_id, EQT::toDisplayName(entity.name), entity.level));
				count++;
				if (count >= 20) {
					AddChatSystemMessage(fmt::format("... and {} more", m_entities.size() - count));
					break;
				}
			}
		}
		if (count == 0) {
			AddChatSystemMessage("No entities found.");
		}
	};
	m_command_registry->registerCommand(who);

	// === Spell Commands ===

	Command cast;
	cast.name = "cast";
	cast.usage = "/cast <gem#>";
	cast.description = "Cast spell from gem slot (1-8)";
	cast.category = "Spells";
	cast.handler = [this](const std::string& args) {
		if (!m_spell_manager) {
			AddChatSystemMessage("Spell system not initialized");
			return;
		}
		if (args.empty()) {
			AddChatSystemMessage("Usage: /cast <gem# 1-8>");
			return;
		}
		try {
			int gem = std::stoi(args);
			if (gem < 1 || gem > 8) {
				AddChatSystemMessage("Gem slot must be 1-8");
				return;
			}
			uint16_t target_id = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
			EQ::CastResult result = m_spell_manager->beginCastFromGem(static_cast<uint8_t>(gem - 1), target_id);
			switch (result) {
				case EQ::CastResult::Success:
					AddChatSystemMessage(fmt::format("Casting from gem {}", gem));
					break;
				case EQ::CastResult::NotMemorized:
					AddChatSystemMessage(fmt::format("No spell in gem {}", gem));
					break;
				case EQ::CastResult::NotEnoughMana:
					AddChatSystemMessage("Insufficient mana");
					break;
				case EQ::CastResult::SpellNotReady:
					AddChatSystemMessage("Spell not ready");
					break;
				case EQ::CastResult::AlreadyCasting:
					AddChatSystemMessage("Already casting");
					break;
				default:
					AddChatSystemMessage("Cannot cast spell");
					break;
			}
		} catch (...) {
			AddChatSystemMessage("Usage: /cast <gem# 1-8>");
		}
	};
	m_command_registry->registerCommand(cast);

	Command mem;
	mem.name = "mem";
	mem.aliases = {"memspell", "memorize"};
	mem.usage = "/mem <gem#> <spell_name>";
	mem.description = "Memorize spell to gem slot";
	mem.category = "Spells";
	mem.handler = [this](const std::string& args) {
		if (!m_spell_manager) {
			AddChatSystemMessage("Spell system not initialized");
			return;
		}
		// Parse: gem# spell_name
		std::istringstream iss(args);
		int gem;
		std::string spell_name;
		if (!(iss >> gem)) {
			AddChatSystemMessage("Usage: /mem <gem# 1-8> <spell_name>");
			return;
		}
		std::getline(iss >> std::ws, spell_name);
		if (spell_name.empty()) {
			AddChatSystemMessage("Usage: /mem <gem# 1-8> <spell_name>");
			return;
		}
		if (gem < 1 || gem > 8) {
			AddChatSystemMessage("Gem slot must be 1-8");
			return;
		}
		// Find spell by name
		const EQ::SpellData* spell = m_spell_manager->getDatabase().getSpellByName(spell_name);
		if (!spell) {
			// Try partial match
			auto matches = m_spell_manager->getDatabase().findSpellsByName(spell_name);
			if (matches.empty()) {
				AddChatSystemMessage(fmt::format("Spell '{}' not found", spell_name));
				return;
			}
			if (matches.size() == 1) {
				spell = matches[0];
			} else {
				// Limit to first 5 matches
				AddChatSystemMessage("Multiple matches found:");
				size_t count = 0;
				for (const auto* s : matches) {
					AddChatSystemMessage(fmt::format("  {} (ID: {})", s->name, s->id));
					if (++count >= 5) {
						if (matches.size() > 5) {
							AddChatSystemMessage(fmt::format("  ... and {} more", matches.size() - 5));
						}
						break;
					}
				}
				return;
			}
		}
		if (m_spell_manager->memorizeSpell(spell->id, static_cast<uint8_t>(gem - 1))) {
			AddChatSystemMessage(fmt::format("Memorizing {} in gem {}", spell->name, gem));
		} else {
			AddChatSystemMessage(fmt::format("Cannot memorize {} - check if scribed and level requirement", spell->name));
		}
	};
	m_command_registry->registerCommand(mem);

	Command forget;
	forget.name = "forget";
	forget.usage = "/forget <gem#>";
	forget.description = "Forget spell from gem slot";
	forget.category = "Spells";
	forget.handler = [this](const std::string& args) {
		if (!m_spell_manager) {
			AddChatSystemMessage("Spell system not initialized");
			return;
		}
		if (args.empty()) {
			AddChatSystemMessage("Usage: /forget <gem# 1-8>");
			return;
		}
		try {
			int gem = std::stoi(args);
			if (gem < 1 || gem > 8) {
				AddChatSystemMessage("Gem slot must be 1-8");
				return;
			}
			if (m_spell_manager->forgetSpell(static_cast<uint8_t>(gem - 1))) {
				AddChatSystemMessage(fmt::format("Forgot spell in gem {}", gem));
			} else {
				AddChatSystemMessage(fmt::format("No spell in gem {}", gem));
			}
		} catch (...) {
			AddChatSystemMessage("Usage: /forget <gem# 1-8>");
		}
	};
	m_command_registry->registerCommand(forget);

	Command spellbook;
	spellbook.name = "spellbook";
	spellbook.aliases = {"book"};
	spellbook.usage = "/spellbook";
	spellbook.description = "Open spellbook window";
	spellbook.category = "Spells";
	spellbook.handler = [this](const std::string& args) {
		AddChatSystemMessage("Spellbook window not yet implemented in UI");
		// TODO: Open spellbook window when UI is implemented
	};
	m_command_registry->registerCommand(spellbook);

	Command skills;
	skills.name = "skills";
	skills.usage = "/skills";
	skills.description = "Toggle skills window";
	skills.category = "Utility";
	skills.handler = [this](const std::string& args) {
		if (m_bridge) {
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::ToggleSkillsWindow,
				eqt::state::ToggleSkillsWindowData{}));
		} else {
			AddChatSystemMessage("Skills window requires graphics mode");
		}
	};
	m_command_registry->registerCommand(skills);

	Command bank;
	bank.name = "bank";
	bank.usage = "/bank";
	bank.description = "Toggle bank window";
	bank.category = "Utility";
	bank.handler = [this](const std::string& args) {
#ifdef EQT_HAS_GRAPHICS
		if (IsBankWindowOpen()) {
			CloseBankWindow();
		} else {
			OpenBankWindow();
		}
#else
		AddChatSystemMessage("Bank window requires graphics mode");
#endif
	};
	m_command_registry->registerCommand(bank);

	Command train;
	train.name = "train";
	train.usage = "/train";
	train.description = "Open trainer window with current target";
	train.category = "Skills";
	train.handler = [this](const std::string& args) {
#ifdef EQT_HAS_GRAPHICS
		// Check if we have a target
		uint16_t target_id = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
		if (target_id == 0) {
			AddChatSystemMessage("You must have a trainer targeted.");
			return;
		}

		// Check if target exists
		auto it = m_entities.find(target_id);
		if (it == m_entities.end()) {
			AddChatSystemMessage("Target not found.");
			return;
		}

		const Entity& target = it->second;

		// Check if target is an NPC (not player, not corpse)
		if (target.npc_type != 1) {
			AddChatSystemMessage("That is not an NPC.");
			return;
		}

		// Check if target is a guildmaster trainer (class 20-35)
		// Reference: EQEmu classes.h - WarriorGM=20 through BerserkerGM=35
		constexpr uint8_t CLASS_WARRIOR_GM = 20;
		constexpr uint8_t CLASS_BERSERKER_GM = 35;
		if (target.class_id < CLASS_WARRIOR_GM || target.class_id > CLASS_BERSERKER_GM) {
			AddChatSystemMessage("That is not a trainer.");
			return;
		}

		// Request trainer window from server
		RequestTrainerWindow(target_id);
#else
		AddChatSystemMessage("Training requires graphics mode.");
#endif
	};
	m_command_registry->registerCommand(train);

	Command gems;
	gems.name = "gems";
	gems.usage = "/gems";
	gems.description = "Show memorized spells";
	gems.category = "Spells";
	gems.handler = [this](const std::string& args) {
		if (!m_spell_manager) {
			AddChatSystemMessage("Spell system not initialized");
			return;
		}
		AddChatSystemMessage("=== Spell Gems ===");
		for (int i = 0; i < 8; i++) {
			uint32_t spell_id = m_spell_manager->getMemorizedSpell(static_cast<uint8_t>(i));
			if (spell_id != EQ::SPELL_UNKNOWN && spell_id != 0xFFFFFFFF) {
				const EQ::SpellData* spell = m_spell_manager->getSpell(spell_id);
				std::string state_str;
				EQ::GemState state = m_spell_manager->getGemState(static_cast<uint8_t>(i));
				switch (state) {
					case EQ::GemState::Ready: state_str = "Ready"; break;
					case EQ::GemState::Refresh:
						state_str = fmt::format("Refresh ({}s)",
							m_spell_manager->getGemCooldownRemaining(static_cast<uint8_t>(i)) / 1000);
						break;
					case EQ::GemState::MemorizeProgress:
						state_str = fmt::format("Memorizing ({:.0f}%)",
							m_spell_manager->getMemorizeProgress(static_cast<uint8_t>(i)) * 100);
						break;
					case EQ::GemState::Casting: state_str = "Casting"; break;
					default: state_str = ""; break;
				}
				AddChatSystemMessage(fmt::format("[{}] {} {}", i + 1,
					spell ? spell->name : fmt::format("Unknown({})", spell_id),
					state_str));
			} else {
				AddChatSystemMessage(fmt::format("[{}] <empty>", i + 1));
			}
		}
	};
	m_command_registry->registerCommand(gems);

	Command interrupt;
	interrupt.name = "interrupt";
	interrupt.aliases = {"ducking"};
	interrupt.usage = "/interrupt";
	interrupt.description = "Interrupt current spell cast";
	interrupt.category = "Spells";
	interrupt.handler = [this](const std::string& args) {
		if (!m_spell_manager) {
			AddChatSystemMessage("Spell system not initialized");
			return;
		}
		if (m_spell_manager->isCasting()) {
			m_spell_manager->interruptCast();
			AddChatSystemMessage("Casting interrupted");
		} else {
			AddChatSystemMessage("Not casting");
		}
	};
	m_command_registry->registerCommand(interrupt);

	Command pet_cmd;
	pet_cmd.name = "pet";
	pet_cmd.usage = "/pet <command> [target]";
	pet_cmd.description = "Issue commands to your pet (attack, back, follow, guard, sit, taunt, hold, focus, health, dismiss)";
	pet_cmd.category = "Pet";
	pet_cmd.handler = [this](const std::string& args) {
		if (!HasPet()) {
			AddChatSystemMessage("You do not have a pet.");
			return;
		}

		std::string subcommand;
		std::string target_name;

		// Parse subcommand and optional target
		std::istringstream iss(args);
		iss >> subcommand;
		std::getline(iss >> std::ws, target_name);

		// Convert to lowercase for comparison
		std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

		uint16_t target_id = 0;

		// For attack command, resolve target
		if (subcommand == "attack") {
			if (!target_name.empty()) {
				// Find entity by name
				for (const auto& pair : m_entities) {
					std::string entity_name = pair.second.name;
					std::transform(entity_name.begin(), entity_name.end(), entity_name.begin(), ::tolower);
					std::string search_name = target_name;
					std::transform(search_name.begin(), search_name.end(), search_name.begin(), ::tolower);
					if (entity_name.find(search_name) != std::string::npos) {
						target_id = pair.first;
						break;
					}
				}
				if (target_id == 0) {
					AddChatSystemMessage(fmt::format("Could not find target: {}", target_name));
					return;
				}
			} else if (m_combat_manager && m_combat_manager->GetTargetId() != 0) {
				target_id = m_combat_manager->GetTargetId();
			} else {
				AddChatSystemMessage("You must specify a target or have one selected.");
				return;
			}
			SendPetCommand(EQT::PET_ATTACK, target_id);
			AddChatSystemMessage("Commanding pet to attack.");
		} else if (subcommand == "back" || subcommand == "stop") {
			SendPetCommand(EQT::PET_BACKOFF);
			AddChatSystemMessage("Commanding pet to back off.");
		} else if (subcommand == "follow") {
			SendPetCommand(EQT::PET_FOLLOWME);
			AddChatSystemMessage("Commanding pet to follow.");
		} else if (subcommand == "guard") {
			SendPetCommand(EQT::PET_GUARDHERE);
			AddChatSystemMessage("Commanding pet to guard here.");
		} else if (subcommand == "sit" || subcommand == "sitdown") {
			SendPetCommand(EQT::PET_SIT);
			AddChatSystemMessage("Commanding pet to sit.");
		} else if (subcommand == "stand" || subcommand == "standup") {
			SendPetCommand(EQT::PET_STANDUP);
			AddChatSystemMessage("Commanding pet to stand.");
		} else if (subcommand == "taunt") {
			SendPetCommand(EQT::PET_TAUNT);
			AddChatSystemMessage("Toggling pet taunt.");
		} else if (subcommand == "notaunt") {
			SendPetCommand(EQT::PET_TAUNT_OFF);
			AddChatSystemMessage("Disabling pet taunt.");
		} else if (subcommand == "hold") {
			SendPetCommand(EQT::PET_HOLD);
			AddChatSystemMessage("Toggling pet hold.");
		} else if (subcommand == "nohold" || subcommand == "unhold") {
			SendPetCommand(EQT::PET_HOLD_OFF);
			AddChatSystemMessage("Disabling pet hold.");
		} else if (subcommand == "focus") {
			SendPetCommand(EQT::PET_FOCUS);
			AddChatSystemMessage("Toggling pet focus.");
		} else if (subcommand == "nofocus") {
			// Titanium only has toggle, no explicit off - use toggle
			SendPetCommand(EQT::PET_FOCUS);
			AddChatSystemMessage("Toggling pet focus off.");
		} else if (subcommand == "health" || subcommand == "report") {
			SendPetCommand(EQT::PET_HEALTHREPORT);
			AddChatSystemMessage("Requesting pet health report.");
		} else if (subcommand == "dismiss" || subcommand == "getlost" || subcommand == "leave") {
			DismissPet();
		} else if (subcommand == "leader") {
			SendPetCommand(EQT::PET_LEADER);
			AddChatSystemMessage("Commanding pet to become leader.");
		} else if (subcommand == "spellhold" || subcommand == "ghold") {
			SendPetCommand(EQT::PET_SPELLHOLD);
			AddChatSystemMessage("Toggling pet spell hold.");
		} else if (subcommand == "nospellhold" || subcommand == "noghold") {
			// Titanium only has toggle, no explicit off - use toggle
			SendPetCommand(EQT::PET_SPELLHOLD);
			AddChatSystemMessage("Toggling pet spell hold off.");
		} else if (subcommand.empty()) {
			AddChatSystemMessage("Pet commands: attack, back, follow, guard, sit, stand, taunt, hold, focus, health, dismiss");
		} else {
			AddChatSystemMessage(fmt::format("Unknown pet command: {}. Use /pet for a list of commands.", subcommand));
		}
	};
	m_command_registry->registerCommand(pet_cmd);

	// === System Commands - Hotkeys ===

	Command hotkeys;
	hotkeys.name = "hotkeys";
	hotkeys.usage = "/hotkeys <reload|save|conflicts>";
	hotkeys.description = "Manage hotkey configuration";
	hotkeys.category = "Utility";
	hotkeys.handler = [this](const std::string& args) {
		auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
		if (args == "reload") {
			if (hotkeyMgr.reload()) {
				AddChatSystemMessage("Hotkeys reloaded successfully.");
				auto conflicts = hotkeyMgr.detectConflicts();
				if (!conflicts.empty()) {
					AddChatSystemMessage(fmt::format("Warning: {} hotkey conflicts detected.", conflicts.size()));
				}
			} else {
				AddChatSystemMessage("Failed to reload hotkeys. Check config file.");
			}
		} else if (args == "save") {
			if (hotkeyMgr.saveToFile()) {
				AddChatSystemMessage("Hotkeys saved successfully.");
			} else {
				AddChatSystemMessage("Failed to save hotkeys.");
			}
		} else if (args == "conflicts") {
			auto conflicts = hotkeyMgr.detectConflicts();
			if (conflicts.empty()) {
				AddChatSystemMessage("No hotkey conflicts detected.");
			} else {
				AddChatSystemMessage(fmt::format("{} hotkey conflicts:", conflicts.size()));
				for (const auto& c : conflicts) {
					AddChatSystemMessage(c.message);
				}
			}
		} else if (args.empty()) {
			AddChatSystemMessage("Hotkey commands: reload, save, conflicts");
			AddChatSystemMessage(fmt::format("Config path: {}", hotkeyMgr.getConfigPath()));
		} else {
			AddChatSystemMessage("Unknown hotkey subcommand. Use: reload, save, conflicts");
		}
	};
	m_command_registry->registerCommand(hotkeys);

	// === Detail System Commands (grass, plants, debris) ===

	Command detailCmd;
	detailCmd.name = "detail";
	detailCmd.aliases = {"detaildensity", "grass"};
	detailCmd.usage = "/detail [0-100]";
	detailCmd.description = "Set or show detail density (grass, plants)";
	detailCmd.category = "Utility";
	detailCmd.handler = rendererCmd("detail");
	m_command_registry->registerCommand(detailCmd);

	Command togglegrass;
	togglegrass.name = "togglegrass";
	togglegrass.usage = "/togglegrass";
	togglegrass.description = "Toggle grass rendering on/off";
	togglegrass.category = "Utility";
	togglegrass.handler = rendererCmd("togglegrass");
	m_command_registry->registerCommand(togglegrass);

	Command toggleplants;
	toggleplants.name = "toggleplants";
	toggleplants.usage = "/toggleplants";
	toggleplants.description = "Toggle plants rendering on/off";
	toggleplants.category = "Utility";
	toggleplants.handler = rendererCmd("toggleplants");
	m_command_registry->registerCommand(toggleplants);

	Command togglerocks;
	togglerocks.name = "togglerocks";
	togglerocks.usage = "/togglerocks";
	togglerocks.description = "Toggle rocks rendering on/off";
	togglerocks.category = "Utility";
	togglerocks.handler = rendererCmd("togglerocks");
	m_command_registry->registerCommand(togglerocks);

	Command toggledebris;
	toggledebris.name = "toggledebris";
	toggledebris.usage = "/toggledebris";
	toggledebris.description = "Toggle debris rendering on/off";
	toggledebris.category = "Utility";
	toggledebris.handler = rendererCmd("toggledebris");
	m_command_registry->registerCommand(toggledebris);

	Command season;
	season.name = "season";
	season.usage = "/season [default|snow|autumn|desert|swamp|auto]";
	season.description = "Override seasonal detail variant";
	season.category = "Utility";
	season.handler = rendererCmd("season");
	m_command_registry->registerCommand(season);

	Command detailinfo;
	detailinfo.name = "detailinfo";
	detailinfo.usage = "/detailinfo";
	detailinfo.description = "Show detail system debug info";
	detailinfo.category = "Utility";
	detailinfo.handler = rendererCmd("detailinfo");
	m_command_registry->registerCommand(detailinfo);

	Command pmem;
	pmem.name = "pmem";
	pmem.aliases = {"memory", "mem_report"};
	pmem.usage = "/pmem";
	pmem.description = "Show memory usage breakdown across all subsystems";
	pmem.category = "Utility";
	pmem.handler = rendererCmd("pmem");
	m_command_registry->registerCommand(pmem);

#ifdef WITH_AUDIO
	// === Audio Commands ===

	Command music_cmd;
	music_cmd.name = "music";
	music_cmd.usage = "/music [on|off]";
	music_cmd.description = "Toggle or set music playback";
	music_cmd.category = "Audio";
	music_cmd.handler = [this](const std::string& args) {
		if (!m_audio_manager) {
			AddChatSystemMessage("Audio system not available.");
			return;
		}

		std::string arg = args;
		std::transform(arg.begin(), arg.end(), arg.begin(), ::tolower);

		if (arg == "on") {
			m_audio_manager->resumeMusic();
			AddChatSystemMessage("Music enabled.");
		} else if (arg == "off") {
			m_audio_manager->pauseMusic();
			AddChatSystemMessage("Music disabled.");
		} else if (arg.empty()) {
			// Toggle
			if (m_audio_manager->isMusicPlaying()) {
				m_audio_manager->pauseMusic();
				AddChatSystemMessage("Music disabled.");
			} else {
				m_audio_manager->resumeMusic();
				AddChatSystemMessage("Music enabled.");
			}
		} else {
			AddChatSystemMessage("Usage: /music [on|off]");
		}
	};
	m_command_registry->registerCommand(music_cmd);

	Command sound_cmd;
	sound_cmd.name = "sound";
	sound_cmd.usage = "/sound [on|off]";
	sound_cmd.description = "Toggle or set sound effects";
	sound_cmd.category = "Audio";
	sound_cmd.handler = [this](const std::string& args) {
		if (!m_audio_manager) {
			AddChatSystemMessage("Audio system not available.");
			return;
		}

		std::string arg = args;
		std::transform(arg.begin(), arg.end(), arg.begin(), ::tolower);

		if (arg == "on") {
			m_audio_manager->setAudioEnabled(true);
			AddChatSystemMessage("Sound effects enabled.");
		} else if (arg == "off") {
			m_audio_manager->setAudioEnabled(false);
			AddChatSystemMessage("Sound effects disabled.");
		} else if (arg.empty()) {
			// Toggle
			bool currentState = m_audio_manager->isAudioEnabled();
			m_audio_manager->setAudioEnabled(!currentState);
			AddChatSystemMessage(fmt::format("Sound effects {}.", !currentState ? "enabled" : "disabled"));
		} else {
			AddChatSystemMessage("Usage: /sound [on|off]");
		}
	};
	m_command_registry->registerCommand(sound_cmd);

	Command volume_cmd;
	volume_cmd.name = "volume";
	volume_cmd.aliases = {"vol"};
	volume_cmd.usage = "/volume <0-100>";
	volume_cmd.description = "Set master volume";
	volume_cmd.category = "Audio";
	volume_cmd.handler = [this](const std::string& args) {
		if (!m_audio_manager) {
			AddChatSystemMessage("Audio system not available.");
			return;
		}

		if (args.empty()) {
			int currentVol = static_cast<int>(m_audio_manager->getMasterVolume() * 100.0f);
			AddChatSystemMessage(fmt::format("Master volume: {}%", currentVol));
			return;
		}

		try {
			int vol = std::stoi(args);
			vol = std::clamp(vol, 0, 100);
			m_audio_manager->setMasterVolume(vol / 100.0f);
			AddChatSystemMessage(fmt::format("Master volume set to {}%", vol));
		} catch (const std::exception&) {
			AddChatSystemMessage("Usage: /volume <0-100>");
		}
	};
	m_command_registry->registerCommand(volume_cmd);

	Command musicvolume_cmd;
	musicvolume_cmd.name = "musicvolume";
	musicvolume_cmd.aliases = {"mvol"};
	musicvolume_cmd.usage = "/musicvolume <0-100>";
	musicvolume_cmd.description = "Set music volume";
	musicvolume_cmd.category = "Audio";
	musicvolume_cmd.handler = [this](const std::string& args) {
		if (!m_audio_manager) {
			AddChatSystemMessage("Audio system not available.");
			return;
		}

		if (args.empty()) {
			int currentVol = static_cast<int>(m_audio_manager->getMusicVolume() * 100.0f);
			AddChatSystemMessage(fmt::format("Music volume: {}%", currentVol));
			return;
		}

		try {
			int vol = std::stoi(args);
			vol = std::clamp(vol, 0, 100);
			m_audio_manager->setMusicVolume(vol / 100.0f);
			AddChatSystemMessage(fmt::format("Music volume set to {}%", vol));
		} catch (const std::exception&) {
			AddChatSystemMessage("Usage: /musicvolume <0-100>");
		}
	};
	m_command_registry->registerCommand(musicvolume_cmd);

	Command effectsvolume_cmd;
	effectsvolume_cmd.name = "effectsvolume";
	effectsvolume_cmd.aliases = {"evol", "sfxvol"};
	effectsvolume_cmd.usage = "/effectsvolume <0-100>";
	effectsvolume_cmd.description = "Set sound effects volume";
	effectsvolume_cmd.category = "Audio";
	effectsvolume_cmd.handler = [this](const std::string& args) {
		if (!m_audio_manager) {
			AddChatSystemMessage("Audio system not available.");
			return;
		}

		if (args.empty()) {
			int currentVol = static_cast<int>(m_audio_manager->getEffectsVolume() * 100.0f);
			AddChatSystemMessage(fmt::format("Effects volume: {}%", currentVol));
			return;
		}

		try {
			int vol = std::stoi(args);
			vol = std::clamp(vol, 0, 100);
			m_audio_manager->setEffectsVolume(vol / 100.0f);
			AddChatSystemMessage(fmt::format("Effects volume set to {}%", vol));
		} catch (const std::exception&) {
			AddChatSystemMessage("Usage: /effectsvolume <0-100>");
		}
	};
	m_command_registry->registerCommand(effectsvolume_cmd);
#endif
}
#endif
