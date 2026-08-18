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

	// T140: loc/sit/stand/move/moveto/warp/zoning/follow/stopfollow MIGRATED
	// to action::CommandProcessor (cmdLoc/cmdSit/cmdStand/cmdMove/cmdMoveTo/
	// cmdWarp/cmdZoning/cmdFollow/cmdStopFollow). loc/warp/zoning are
	// net-new EqActionHandler surface (RequestWarp(), defined below outside
	// this function, carries warp's ~80-line body verbatim -- same recipe
	// as T136's RequestVendorToggle; zoning's read+report body moved to
	// EqActionHandler::showOrSetZoning() verbatim, since
	// SetZoningEnabled()/IsZoningEnabled() were already public); sit/stand/
	// move/moveto/follow/stopfollow REUSE the pre-existing
	// IActionHandler::sit()/stand()/moveToLocation()/moveToEntity()/
	// followEntity()/stopFollow() -- no new plumbing needed for those six.
	// See refactor/T140_migrate_final_seventeen.md.

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

	// T140: quit/camp/debug/perf MIGRATED to action::CommandProcessor
	// (cmdQuit/cmdCamp/cmdDebug/cmdPerf) -- all net-new EqActionHandler
	// surface (requestQuit()/campNow()/debugLevel()/perfReport()), each
	// carrying its old lambda body verbatim (quit's T30 rationale, camp's
	// sit-then-StartCampTimer() sequence, debug's static SetDebugLevel/
	// GetDebugLevel, perf's EQT::PerformanceMetrics report -- unchanged).
	// See refactor/T140_migrate_final_seventeen.md.

	// T139: rendererCmd() (the helper that pushed a RendererCommand bridge
	// event for a game-registry command name) is deleted here -- every
	// remaining caller in this file was one of the 26 redundant duplicates
	// removed by T139 (see refactor/T139_delete_duplicate_commands.md).
	// registry-3's fallback (Application::dispatchToActionOrRenderer,
	// application.cpp) forwards the identical "/" + cmdBody string to
	// IrrlichtRenderer::processSlashCommand() automatically once no game- or
	// action-registry command claims the name, so nothing here needs
	// replacing -- the same renderer entry points still run.

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

	// T140: time MIGRATED to action::CommandProcessor (cmdTime) -- needed a
	// thin EverQuest wrapper (RequestTimeOfDay(), below, outside this
	// function) carrying the show/set body verbatim, same recipe as
	// RequestWarp(): m_time_hour/m_time_minute and m_bridge are private, so
	// EqActionHandler routes through the new method rather than duplicating
	// the read/write here.

	// T102: this `who` (client-side m_entities scan, previously winning
	// the intra-registry collision with Social's who_cmd over in
	// eq_commands.cpp -- T97/T82) is DELETED, not migrated. Social's
	// who_cmd (the real server /who request) was ported instead -- see
	// action::CommandProcessor::cmdWho and
	// refactor/T102_migrate_combat_social.md for why.

}

// T140: warp's ~80-line teleport body, carried verbatim into a thin
// EverQuest wrapper (same recipe as T136's RequestVendorToggle) --
// action::EqActionHandler has no access to m_combat_manager/m_entities/
// m_bridge, all private. No behavior change: still writes m_x/m_y/m_z and
// the entity list directly (no pathfinding, no server round trip for the
// teleport itself), still calls SendPositionUpdate() (real network send)
// and pushes PlayerMovedData afterward, exactly as before.
void EverQuest::RequestWarp(const std::string& args)
{
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
}

// T140: time's show/set body, carried verbatim into a thin EverQuest
// wrapper (same recipe as RequestWarp() above) -- m_time_hour/m_time_minute
// and m_bridge are private, so EqActionHandler routes through this rather
// than duplicating the read/write. No behavior change: same 0-23 range
// check, same TimeOfDayChanged bridge push on set.
void EverQuest::RequestTimeOfDay(const std::string& args)
{
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
}
