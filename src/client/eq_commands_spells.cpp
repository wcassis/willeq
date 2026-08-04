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
		// T43: was #ifdef EQT_HAS_GRAPHICS. IsBankWindowOpen/OpenBankWindow/
		// CloseBankWindow (eq.h/eq_merchant.cpp) are plain game state + bridge
		// events, already unguarded at their own definitions since P28 -- only
		// this command handler still refused to call them headless.
		if (IsBankWindowOpen()) {
			CloseBankWindow();
		} else {
			OpenBankWindow();
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

		// Request trainer window from server
		RequestTrainerWindow(target_id);
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
