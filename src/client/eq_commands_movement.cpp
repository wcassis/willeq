// EverQuest slash-command registration — movement/combat/utility/sky domain.
//
// Split out of eq_commands.cpp (Batch G, unit G02): holds
// RegisterMovementCommands(), covering the original "Movement Commands",
// "Combat Commands", "Utility Commands", and "Sky Commands" sections.
// Lambda bodies moved verbatim, no behavior change. Called from
// EverQuest::RegisterCommands() (eq_commands.cpp) in original file order.
// See refactor/G02_commands_split.md.

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

// T29: chat_message_buffer.h/command_registry.h/hotbar_model.h are plain
// data-model headers with no renderer dependency (P28 declassified them;
// see eq_chat.cpp's identical note) -- needed unconditionally because
// RegisterMovementCommands() itself is no longer graphics-only (below).
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/ui/command_registry.h"
#include "client/hotbar/hotbar_model.h"

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

// T29: this whole registration function used to be wrapped in
// #ifdef EQT_HAS_GRAPHICS, which meant a headless-compiled build had NO
// movement/combat/utility/sky slash commands at all -- including /combat,
// the exact command T26 needed to verify its live kill-to-loot cycle and
// could not reach. Per P28's established direction (declassify what
// doesn't actually touch the renderer rather than adding more guards):
// almost none of this function touches graphics APIs directly -- it was
// already routed through the RendererCommand bridge queue (rendererCmd()
// below) for the handful of commands that are genuinely renderer-only.
// The two exceptions that called graphics-only singletons directly
// (spellreload, weatherquality) are now individually guarded at their
// call sites, matching the existing /bank, /train pattern in
// eq_commands_spells.cpp.
void EverQuest::RegisterMovementCommands()
{
	using namespace eqt::ui;

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
	// (drained on the render thread, never called directly -- see the P10
	// note on eq_commands_movement.cpp's rendererCmd/RendererCommand usage
	// in docs/decisions.md 2026-08-02).
	// T29: in a headless-compiled build there is no render thread to drain
	// this queue, so silently queuing would look accepted but never take
	// effect. Say so honestly instead (plan item 4: an unrecognised-
	// because-headless command must say so, not fail silently).
	auto rendererCmd = [this](const std::string& cmdName) {
		return [this, cmdName](const std::string& args) {
#ifdef EQT_HAS_GRAPHICS
			if (m_bridge) {
				std::string fullCmd = "/" + cmdName;
				if (!args.empty()) fullCmd += " " + args;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::RendererCommand,
					eqt::state::RendererCommandData{fullCmd}));
			}
#else
			AddChatSystemMessage(fmt::format("/{} requires graphics mode (not available in this headless build).", cmdName));
#endif
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
#ifdef EQT_HAS_GRAPHICS
		EQT::Graphics::Environment::SpellEffectsConfig::instance().reload();
		AddChatSystemMessage("Spell effect settings reloaded");
#else
		AddChatSystemMessage("/spellreload requires graphics mode (not available in this headless build).");
#endif
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
#ifdef EQT_HAS_GRAPHICS
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
#else
		AddChatSystemMessage("/weatherquality requires graphics mode (not available in this headless build).");
#endif
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

	Command mergedrawscmd;
	mergedrawscmd.name = "mergedraws";
	mergedrawscmd.aliases = {};
	mergedrawscmd.usage = "/mergedraws";
	mergedrawscmd.description = "Toggle cross-region zone draw merge (P10, flag-gated production landing)";
	mergedrawscmd.category = "Utility";
	mergedrawscmd.handler = rendererCmd("mergedraws");
	m_command_registry->registerCommand(mergedrawscmd);

	Command screenshotcmd;
	screenshotcmd.name = "screenshot";
	screenshotcmd.aliases = {};
	screenshotcmd.usage = "/screenshot <path>";
	screenshotcmd.description = "Save a screenshot to the given path (P10: scriptable A/B capture hook)";
	screenshotcmd.category = "Utility";
	screenshotcmd.handler = rendererCmd("screenshot");
	m_command_registry->registerCommand(screenshotcmd);

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

}
