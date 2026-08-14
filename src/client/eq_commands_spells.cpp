// EverQuest slash-command registration — spell/skill/hotkey/detail/audio domain.
//
// Split out of eq_commands.cpp (Batch G, unit G02): holds
// RegisterSpellCommands(), covering the original "Spell Commands",
// "System Commands - Hotkeys", "Detail System Commands", and
// "Audio Commands" sections. Lambda bodies moved verbatim, no behavior
// change. Called from EverQuest::RegisterCommands() (eq_commands.cpp) in
// original file order. See refactor/G02_commands_split.md.

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
// that RegisterSpellCommands() is no longer graphics-only (below).
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

// T29: un-guarded from EQT_HAS_GRAPHICS -- see the identical note in
// eq_commands_movement.cpp. The two commands here that called
// graphics-only singletons directly (/hotkeys, plus /bank and /train
// which were already individually guarded) are now guarded at their own
// handler bodies instead of at the whole-function level.
void EverQuest::RegisterSpellCommands()
{
	using namespace eqt::ui;

	// D20d: Helper — renderer commands push RendererCommand event via bridge
	// (G02: duplicated verbatim from RegisterMovementCommands, where it's
	// also used — this domain's Detail System Commands and /pmem also route
	// through it. Stateless, no behavior change from having two copies.)
	// T29: headless honesty -- see the identical note in
	// eq_commands_movement.cpp's rendererCmd.
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

	// === Spell Commands ===
	//
	// T101: cast/mem/forget/spellbook/gems/interrupt (this registry's whole
	// "Spells" category) and pet_cmd (below, "Pet" category) migrated to
	// action::CommandProcessor -- see refactor/T101_migrate_spells_pet_
	// family.md's atomicity section. Removed here in the same change (game
	// is tried before action, so leaving these registered would silently
	// keep routing through this copy, per the atomicity rule T99
	// established). /skills stays: it's registered category "Utility", not
	// "Spells", outside this unit's confirmed membership.

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
	bank.description = "Open (or close) a bank window with your current target";
	bank.category = "Utility";
	bank.handler = [this](const std::string& args) {
		// T50: was a no-arg OpenBankWindow()/CloseBankWindow() toggle. The
		// no-arg OpenBankWindow() falls back to a hardcoded dummy npc id of 1
		// (bankerNpcId != 0 ? bankerNpcId : 1) -- in the "arena" test zone
		// that id belongs to an unrelated entity (zone_controller000) tens of
		// thousands of units away, so the very next movement tick's proximity
		// check closes the window it just opened (T49's live finding). The
		// renderer's ctrl+click-a-banker gesture doesn't have this bug: it
		// pushes BankerInteractIntent{npcId} with the REAL target id, and
		// that intent's handler (eq_bridge.cpp, untouched by this unit)
		// validates npc_type/distance/class==CLASS_BANKER and opens with the
		// real id. So /bank now pushes the same intent for the current
		// target instead of bypassing it -- this is a deliberate behaviour
		// change: /bank now REQUIRES a targeted banker to open, where before
		// it "worked" only in the degenerate self-closing sense. See Review.
		//
		// Close does NOT go through the intent: BankerInteractIntent's
		// handler is open-only (if already open, it just logs and returns --
		// it has no toggle/close branch, since the renderer gesture always
		// tries to open). So closing keeps calling CloseBankWindow()
		// directly, same as before, and deliberately does not require a
		// valid (or any) current target -- you may have walked away or
		// changed target since opening, and closing must still work.
		if (IsBankWindowOpen()) {
			CloseBankWindow();
			return;
		}

		uint16_t target_id = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
		if (target_id == 0) {
			AddChatSystemMessage("You don't have a target - target a banker first.");
			return;
		}

		if (m_bridge) {
			m_bridge->pushIntent(eqt::events::BankerInteractIntent{target_id});
		} else {
			AddChatSystemMessage("Cannot open bank - no bridge attached.");
		}
	};
	m_command_registry->registerCommand(bank);

	Command train;
	train.name = "train";
	train.usage = "/train";
	train.description = "Open trainer window with current target";
	train.category = "Skills";
	train.handler = [this](const std::string& args) {
		// T43: was #ifdef EQT_HAS_GRAPHICS. RequestTrainerWindow()
		// (eq_merchant.cpp) just queues HC_OP_GMTraining -- plain network
		// logic, no renderer dependency -- only this command handler's own
		// wrapping refused to call it headless.
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

		// T50: /train previously had no distance or class checks of its own,
		// so a refusal (too far, or the wrong trainer for your class) was
		// pure silence -- the server's Client::OPGMTraining
		// (zone/client_process.cpp) drops the packet with no reply in either
		// case. Pre-validate both here so the player gets a message instead.
		//
		// Distance: verified against the real eqemu source
		// (client_process.cpp) -- the server's own limit is
		// USE_NPC_RANGE2=40000 (200 units squared, common/features.h),
		// looser than the client's own NPC_INTERACTION_DISTANCE (15,
		// eq.h) used everywhere else a client-side interaction range is
		// checked (the banker gesture's BankerInteractIntent handler,
		// eq_bridge.cpp, and the movement proximity gate, eq_movement.cpp).
		// Checking at 15 is therefore STRICTER than the server would
		// require -- a target between 15 and 200 units away that the server
		// would happily train is refused here. Chose 15 anyway, for
		// consistency with every other client-side NPC-interaction check
		// rather than inventing a third distance constant (the plan
		// explicitly forbids that); see Review for the tradeoff stated
		// plainly.
		float dist = CalculateDistance2D(m_x, m_y, target.x, target.y);
		if (dist > NPC_INTERACTION_DISTANCE) {
			AddChatSystemMessage(fmt::format("You are too far away to train with {}.", target.name));
			return;
		}

		// Class match: mirrors the server's own check exactly --
		// Client::OPGMTraining computes
		// "trains_class = pTrainer->GetClass() - (Class::WarriorGM -
		// Class::Warrior)" i.e. trainerClass - 19, and refuses silently if
		// it doesn't equal the player's class, UNLESS
		// RuleB(Character, AllowCrossClassTrainers) is true (default false,
		// common/ruletypes.h). Since the default is false and the whole
		// point of this unit is replacing silence with an actionable
		// message, this is a hard client-side block naming both classes --
		// not a hidden refusal. A server that flips the rule on would make
		// this block stricter than necessary; noted as a caveat in the
		// Review and deferred rather than built as client-side
		// configuration now.
		constexpr uint8_t TRAINER_CLASS_OFFSET = CLASS_WARRIOR_GM - 1; // WarriorGM(20) trains Warrior(1)
		uint32_t trained_class = static_cast<uint32_t>(target.class_id) - TRAINER_CLASS_OFFSET;
		if (trained_class != m_class) {
			AddChatSystemMessage(fmt::format("{} trains {}s; you are a {}.",
				target.name, GetClassName(trained_class), GetClassName(m_class)));
			return;
		}

		// Request trainer window from server
		RequestTrainerWindow(target_id);
	};
	m_command_registry->registerCommand(train);

	// T101: gems/interrupt (Spells) and pet_cmd (Pet) removed here -- see
	// the removal note above RegisterSpellCommands()'s Spell Commands
	// section. Ported to action::CommandProcessor's cmdGems/cmdInterrupt/
	// cmdPet.

	// === System Commands - Hotkeys ===

	Command hotkeys;
	hotkeys.name = "hotkeys";
	hotkeys.usage = "/hotkeys <reload|save|conflicts>";
	hotkeys.description = "Manage hotkey configuration";
	hotkeys.category = "Utility";
	hotkeys.handler = [this](const std::string& args) {
#ifdef EQT_HAS_GRAPHICS
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
#else
		(void)args;
		AddChatSystemMessage("/hotkeys requires graphics mode (not available in this headless build).");
#endif
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
	// T108: was rendererCmd("pmem") -- routed to a RendererCommand bridge
	// event handled entirely inside IrrlichtRenderer::processSlashCommand's
	// "/pmem" branch, which built an empty MemoryReportInput{} and so could
	// never report real processRssBytes/audio/entity/door/spellDb data (the
	// exact reachability gap T105 and T106 both named: "runLoadDiagnostics
	// has zero callers"). EverQuest::runPmemDiagnostics() already exists,
	// fully built, and populates all of that from real subsystems via the
	// DiagnosticsMemoryReport bridge event (see irrlicht_bridge.cpp) -- it
	// just had nothing calling it. Call it directly instead of routing
	// through the empty renderer-side path.
	pmem.handler = [this](const std::string& args) {
#ifdef EQT_HAS_GRAPHICS
		runPmemDiagnostics(args.empty() ? "pmem" : args);
#else
		AddChatSystemMessage("/pmem requires graphics mode (not available in this headless build).");
#endif
	};
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
