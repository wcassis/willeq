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
	// T88: frame stated explicitly in the usage string itself -- this is
	// the CLIENT frame (same as m_x/m_y; docs/CLAUDE.md's "Client Internal
	// Coordinates"), axis-swapped from the server/wire frame that a /loc or
	// another client's own self-report would give you. This ambiguity
	// (undocumented at this exact surface) is what cost T85's live capture
	// session -- see refactor/T88_position_frame_determination.md.
	move_cmd.usage = "/move <x> <y> <z> (CLIENT frame -- swap x/y if reading a server-frame /loc)";
	move_cmd.description = "Move to coordinates (client frame; see usage)";
	move_cmd.category = "Movement";
	move_cmd.handler = [this](const std::string& args) {
		std::istringstream iss(args);
		float x, y, z;
		if (iss >> x >> y >> z) {
			Move(x, y, z);
			// T88: echo BOTH frames so a caller who fed the wrong-frame
			// numbers sees it immediately instead of only discovering it
			// via an absent position update minutes later.
			AddChatSystemMessage(fmt::format(
				"Moving to client=({:.1f}, {:.1f}, {:.1f}) -> server=({:.1f}, {:.1f}, {:.1f})",
				x, y, z, /*server_x=*/y, /*server_y=*/x, z));
		} else {
			AddChatSystemMessage("Usage: /move <x> <y> <z> (CLIENT frame -- swap x/y if reading a server-frame /loc)");
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

	// T52: zone-line-triggered zoning (CheckZoneLine's m_zoning_enabled gate)
	// has always been reachable ONLY via a renderer Ctrl+Z keybind
	// (ZoningEnabledIntent -> EverQuest::SetZoningEnabled(), eq_bridge.cpp)
	// -- no slash command existed at all, so a purely piped/headless
	// session (no renderer, no keyboard) could never trigger a real
	// zone-line crossing, only GM-teleport-style transitions. This is
	// another instance of the automation-reachability pattern (T26
	// /combat, T29 registry, T34 /loot, T42->T48 vendor, T50 /bank+/train):
	// exposing an EXISTING production API (SetZoningEnabled/IsZoningEnabled,
	// eq.h:776-777, unchanged here) through the standard command registry,
	// not a scratch hook. Needed to drive this unit's zone-line-crossing
	// live-acceptance control leg via pure piped stdin.
	Command zoning;
	zoning.name = "zoning";
	zoning.usage = "/zoning [on|off]";
	zoning.description = "Show or set whether crossing a zone line triggers an actual zone change";
	zoning.category = "Movement";
	zoning.handler = [this](const std::string& args) {
		std::string trimmed = args;
		while (!trimmed.empty() && trimmed[0] == ' ') {
			trimmed = trimmed.substr(1);
		}
		if (trimmed.empty()) {
			AddChatSystemMessage(fmt::format("Zoning is {}", IsZoningEnabled() ? "on" : "off"));
			return;
		}
		if (trimmed == "on") {
			SetZoningEnabled(true);
			AddChatSystemMessage("Zoning is now on");
		} else if (trimmed == "off") {
			SetZoningEnabled(false);
			AddChatSystemMessage("Zoning is now off");
		} else {
			AddChatSystemMessage("Usage: /zoning [on|off]");
		}
	};
	m_command_registry->registerCommand(zoning);

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

	// T102: target/attack/stopattack/aa/combat/taunt/autofire MIGRATED to
	// action::CommandProcessor (cmdTarget/cmdAttack/cmdStopAttack/cmdAA/
	// cmdCombat/cmdTaunt/cmdAutofire). See
	// refactor/T102_migrate_combat_social.md.

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
	// Will's ruling 2026-08-03: "/q is a shortcut for /quit and should have
	// the same functionality: quit the application immediately without
	// camping." Registered as ALIASES of one command rather than as separate
	// Command objects with duplicate handlers, so the two can never drift
	// apart (the registry maps every alias to this primary name).
	quit.aliases = {"exit", "q"};
	quit.usage = "/quit";
	quit.description = "Exit the game immediately (aliases: /exit, /q)";
	quit.category = "Utility";
	quit.handler = [this](const std::string& args) {
		// T30: previously advisory-only ("use /camp or /q instead") --
		// never actually quit, connected or not. That made "/quit" (the
		// name automation and the exit matrix's own repro reach for) a
		// no-op while CONNECTED, on top of the separate pre-connection
		// gating bug (see Application::processPreConnectionLifecycleCommands()
		// in application.cpp for that half). /camp remains the distinct,
		// correct choice for a graceful in-game logout (sits down, notifies
		// the server, waits for confirmation before disconnecting); /quit,
		// /exit and /q are an immediate local exit with no server-side
		// logout courtesy -- which is also what the real Titanium client
		// does, and what refactor/T30_headless_shutdown_fix.md's exit-matrix
		// verification expects of a command literally named "quit".
		RequestQuit();  // D20c4: Application checks quit flag
	};
	m_command_registry->registerCommand(quit);

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

	// T139: rendererCmd() (the helper that pushed a RendererCommand bridge
	// event for a game-registry command name) is deleted here -- every
	// remaining caller in this file was one of the 26 redundant duplicates
	// removed by T139 (see refactor/T139_delete_duplicate_commands.md).
	// registry-3's fallback (Application::dispatchToActionOrRenderer,
	// application.cpp) forwards the identical "/" + cmdBody string to
	// IrrlichtRenderer::processSlashCommand() automatically once no game- or
	// action-registry command claims the name, so nothing here needs
	// replacing -- the same renderer entry points still run.

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

	// === Sky Commands ===

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

	// T102: this `who` (client-side m_entities scan, previously winning
	// the intra-registry collision with Social's who_cmd over in
	// eq_commands.cpp -- T97/T82) is DELETED, not migrated. Social's
	// who_cmd (the real server /who request) was ported instead -- see
	// action::CommandProcessor::cmdWho and
	// refactor/T102_migrate_combat_social.md for why.

}
