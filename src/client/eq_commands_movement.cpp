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
#include "client/graphics/eq/wld_loader.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
#include "client/input/hotkey_manager.h"
#include "common/util/json_config.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/seasonal_controller.h"
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

	// T143: /help (aliases h, ?) is DELETED from this registry, not just
	// moved. It used to live here as a "stay-game" command (T138's
	// needs-decision row) specifically BECAUSE it only ever introspected
	// this registry's own catalog -- migrating it as-is would just relocate
	// the blindness to wherever it landed. That blindness stopped being
	// tolerable once the migration programme (T139/T140/T142) emptied this
	// registry down to a handful of commands: /help was describing 11 of
	// 122 commands, a regression this programme caused.
	//
	// The fix requires seeing BOTH EverQuest::m_command_registry (this
	// registry) and action::CommandProcessor's catalog, and only
	// Application holds pointers to both. So /help now lives in
	// Application::dispatchToActionOrRenderer() (application_commands.cpp),
	// implemented via eqt::help::BuildHelpResponse()
	// (include/client/help/merged_help.h) -- it is reached through the SAME
	// unrecognized-command fallback every other action/renderer command
	// already uses, now that no registration here shadows it. See
	// refactor/T143_registry_aware_help.md.

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
	// application_commands.cpp) forwards the identical "/" + cmdBody string to
	// IrrlichtRenderer::processSlashCommand() automatically once no game- or
	// action-registry command claims the name, so nothing here needs
	// replacing -- the same renderer entry points still run.

	// T142: spellreload (aliases reloadspells, reloadspellfx) and
	// weatherquality (aliases wq, weatherpreset) DELETED from the game
	// registry -- these were the two exceptions noted above that called
	// graphics-only singletons (SpellEffectsConfig, WeatherConfigLoader)
	// directly from the game thread, with no lock protecting them against
	// the render thread (T138). Both are now implemented in
	// IrrlichtRenderer::processSlashCommand() (renderer_slash_commands.cpp),
	// reached via registry-3's fallback exactly like reloadeffects/
	// reloadweather already were -- see refactor/T142_bridge_route_
	// singletons.md. All four aliases were taught to processSlashCommand()
	// as literal cmd == "/alias" arms (T139's alias trap: registry 3 does
	// not resolve Command::aliases).

	// === Sky Commands ===

	// T140: time MIGRATED to action::CommandProcessor (cmdTime) -- needed a
	// thin EverQuest wrapper (RequestTimeOfDay(), below, outside this
	// function) carrying the show/set body verbatim, same recipe as
	// RequestWarp(): m_game_state.world().timeHour()/m_game_state.world().timeMinute() and m_bridge are private, so
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
// m_bridge, all private. No behavior change: still writes the client position and
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
		// Instantly set position. T288: one store; the hand-rolled bridge
		// publish below is still the only thing the renderer sees, so this
		// write stays event-free exactly as the raw members were.
		SetPosLocal(x, y, z);

		// Update our entity in the entity list
		auto it = m_entities.find(MySpawnId());
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
			pdata.heading = HeadingDeg();
			pdata.dx = 0; pdata.dy = 0; pdata.dz = 0;
			pdata.isMoving = false;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::PlayerMoved, std::move(pdata)));
		}

		AddChatSystemMessage(fmt::format("Warped to ({:.1f}, {:.1f}, {:.1f})", x, y, z));
	}
}

// T140: time's show/set body, carried verbatim into a thin EverQuest
// wrapper (same recipe as RequestWarp() above) -- m_game_state.world().timeHour()/m_game_state.world().timeMinute()
// and m_bridge are private, so EqActionHandler routes through this rather
// than duplicating the read/write. No behavior change: same 0-23 range
// check, same TimeOfDayChanged bridge push on set.
void EverQuest::RequestTimeOfDay(const std::string& args)
{
	if (args.empty()) {
		// Show current time
		AddChatSystemMessage(fmt::format("Game time: {:02d}:{:02d}", m_game_state.world().timeHour(), m_game_state.world().timeMinute()));
	} else {
		// Set time
		try {
			int hour = std::stoi(args);
			if (hour < 0 || hour > 23) {
				AddChatSystemMessage("Hour must be 0-23");
				return;
			}
			// T286: WorldState is the only store. /time used to write only
			// the EverQuest copy, so world().timeHour() kept the server's
			// value and the two disagreed for the rest of the session.
			m_game_state.world().setTimeOfDay(static_cast<uint8_t>(hour), 0);

			// Publish TimeOfDayChanged
			if (m_bridge) {
				eqt::state::TimeOfDayChangedData tdata;
				tdata.hour = m_game_state.world().timeHour();
				tdata.minute = m_game_state.world().timeMinute();
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::TimeOfDayChanged, std::move(tdata)));
			}

			AddChatSystemMessage(fmt::format("Time set to {:02d}:00", m_game_state.world().timeHour()));
		} catch (...) {
			AddChatSystemMessage("Usage: /time [0-23]");
		}
	}
}
