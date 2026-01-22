#include "common/event/event_loop.h"
#include "common/util/json_config.h"  // Must be before logging.h for InitLoggingFromJson
#include "common/logging.h"
#include "common/net/daybreak_connection.h"
#include "common/performance_metrics.h"
#include "client/eq.h"
#include "client/combat.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/ui/ui_settings.h"
#include "client/input/hotkey_manager.h"
#endif

#include <thread>
#include <iostream>
#include <sstream>
#include <atomic>
#include <queue>
#include <mutex>

#ifdef _WIN32
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>
#include <signal.h>
#endif

#include <glm/glm.hpp>

void ProcessCommand(const std::string& cmd, std::vector<std::unique_ptr<EverQuest>>& eq_list, std::atomic<bool>& running);

#ifndef _WIN32
// Signal handlers for log level adjustment (Unix)
void HandleSigUsr1(int /*sig*/) {
	LogLevelIncrease();
	// Note: Can't safely use LOG_* in signal handler, but level is changed
}

void HandleSigUsr2(int /*sig*/) {
	LogLevelDecrease();
}

// Signal handler for hotkey reload (SIGHUP)
#ifdef EQT_HAS_GRAPHICS
void HandleSigHup(int /*sig*/) {
	// Reload hotkey configuration
	// Note: This is a simple flag-based approach since we can't safely
	// do complex operations in a signal handler
	eqt::input::HotkeyManager::instance().reload();
}
#endif

// Terminal handling for raw keyboard input (Unix)
struct termios orig_termios;
bool terminal_raw_mode = false;

void DisableRawMode() {
	if (terminal_raw_mode) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
		terminal_raw_mode = false;
		std::cout << "\r\n[Exited keyboard control mode]\r\n";
	}
}

void EnableRawMode() {
	if (!terminal_raw_mode) {
		tcgetattr(STDIN_FILENO, &orig_termios);
		atexit(DisableRawMode);

		struct termios raw = orig_termios;
		raw.c_lflag &= ~(ECHO | ICANON);
		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 0;

		tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
		terminal_raw_mode = true;

		int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
		fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

		LOG_INFO(MOD_INPUT, "Keyboard control mode active. Press Enter to switch to command mode");
	}
}

bool KeyPressed() {
	struct timeval tv = {0, 0};
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

char ReadKey() {
	char c = 0;
	if (read(STDIN_FILENO, &c, 1) == 1) {
		return c;
	}
	return 0;
}
#endif

void ProcessCommand(const std::string& cmd, std::vector<std::unique_ptr<EverQuest>>& eq_list, std::atomic<bool>& running) {
	LOG_DEBUG(MOD_MAIN, "Processing command: '{}'", cmd);

	std::istringstream iss(cmd);
	std::string command;
	iss >> command;

	if (command == "help") {
		std::cout << "Keyboard Controls:\n";
		std::cout << "  WASD / Arrow Keys          - Move forward/backward, turn left/right\n";
		std::cout << "  Space                      - Jump\n";
		std::cout << "  Enter                      - Switch between keyboard and command mode\n";
		std::cout << "\nAvailable commands:\n";
		std::cout << "  say <message>              - Say message in current zone\n";
		std::cout << "  tell <player> <message>    - Send tell to player\n";
		std::cout << "  shout <message>            - Shout message (zone-wide)\n";
		std::cout << "  ooc <message>              - OOC message (cross-zone)\n";
		std::cout << "  auction <message>          - Auction message (cross-zone)\n";
		std::cout << "  move <x> <y> <z>           - Move to coordinates\n";
		std::cout << "  moveto <entity>            - Move to named entity\n";
		std::cout << "  follow <entity>            - Follow named entity\n";
		std::cout << "  stopfollow                 - Stop following\n";
		std::cout << "  walk                       - Set movement speed to walk\n";
		std::cout << "  run                        - Set movement speed to run\n";
		std::cout << "  sneak                      - Set movement speed to sneak\n";
		std::cout << "  face <x> <y> <z>           - Face coordinates\n";
		std::cout << "  face <entity>              - Face named entity\n";
		std::cout << "  turn <degrees>             - Turn to heading (0=N, 90=E, 180=S, 270=W)\n";
		std::cout << "  loc                        - Show current location\n";
		std::cout << "  list [search]              - List nearby entities (optional: filter by name)\n";
		std::cout << "  pathfinding <on|off>       - Toggle pathfinding (default: on)\n";
		std::cout << "  debug <level>              - Set debug level (0-3)\n";
		std::cout << "  target <name>              - Target an entity by name\n";
		std::cout << "  attack                     - Start auto attack on current target\n";
		std::cout << "  stopattack                 - Stop auto attack\n";
		std::cout << "  ~ or aa                    - Toggle auto attack on/off\n";
		std::cout << "  consider                   - Consider current target\n";
		std::cout << "  loot                       - Loot nearest corpse\n";
		std::cout << "  listtargets                - List potential hunt targets\n";
		std::cout << "  dump <name>                - Dump entity appearance/equipment info\n";
		std::cout << "  hunt <on|off>              - Toggle auto-hunting mode\n";
		std::cout << "  autoloot <on|off>          - Toggle auto-looting\n";
		std::cout << "  sit                        - Sit down\n";
		std::cout << "  stand                      - Stand up\n";
		std::cout << "  crouch                     - Crouch/duck\n";
		std::cout << "  feign                      - Feign death\n";
		std::cout << "  afk [on|off]               - Toggle AFK status\n";
		std::cout << "  anon [on|off]              - Toggle anonymous status\n";
		std::cout << "  roleplay [on|off]          - Toggle roleplay status\n";
		std::cout << "  emote <name>               - Perform an emote (wave, dance, cheer, etc.)\n";
		std::cout << "  quit                       - Exit program\n";
		return;
	}
	else if (command == "quit" || command == "exit") {
		running.store(false);
		return;
	}
	else if (command == "debug") {
		int level;
		if (iss >> level) {
			EverQuest::SetDebugLevel(level);
			std::cout << fmt::format("Debug level set to {}\n", level);
		}
		return;
	}

	if (!eq_list.empty()) {
		auto& eq = eq_list[0];

		LOG_DEBUG(MOD_MAIN, "Executing command '{}' on EverQuest client", command);

		if (command == "say") {
			std::string message;
			std::getline(iss, message);
			if (!message.empty() && message[0] == ' ') message = message.substr(1);
			eq->SendChatMessage(message, "say");
		}
		else if (command == "tell") {
			std::string target, message;
			iss >> target;
			std::getline(iss, message);
			if (!message.empty() && message[0] == ' ') message = message.substr(1);
			eq->SendChatMessage(message, "tell", target);
		}
		else if (command == "shout") {
			std::string message;
			std::getline(iss, message);
			if (!message.empty() && message[0] == ' ') message = message.substr(1);
			eq->SendChatMessage(message, "shout");
		}
		else if (command == "ooc") {
			std::string message;
			std::getline(iss, message);
			if (!message.empty() && message[0] == ' ') message = message.substr(1);
			eq->SendChatMessage(message, "ooc");
		}
		else if (command == "auction") {
			std::string message;
			std::getline(iss, message);
			if (!message.empty() && message[0] == ' ') message = message.substr(1);
			eq->SendChatMessage(message, "auction");
		}
		else if (command == "move") {
			float x, y, z;
			if (iss >> x >> y >> z) {
				LOG_DEBUG(MOD_MAIN, "Executing move command to ({}, {}, {})", x, y, z);
				eq->Move(x, y, z);
			} else {
				std::cout << "Usage: move <x> <y> <z>\n";
			}
		}
		else if (command == "moveto") {
			std::string entity;
			std::getline(iss, entity);
			if (!entity.empty() && entity[0] == ' ') entity = entity.substr(1);
			if (!entity.empty()) {
				eq->MoveToEntity(entity);
			} else {
				std::cout << "Usage: moveto <entity_name>\n";
			}
		}
		else if (command == "follow") {
			std::string entity;
			std::getline(iss, entity);
			if (!entity.empty() && entity[0] == ' ') entity = entity.substr(1);
			if (!entity.empty()) {
				eq->Follow(entity);
			} else {
				std::cout << "Usage: follow <entity_name>\n";
			}
		}
		else if (command == "stopfollow") {
			eq->StopFollow();
		}
		else if (command == "face") {
			std::string arg;
			if (iss >> arg) {
				float x, y, z;
				std::istringstream coord_stream(arg);
				if (coord_stream >> x && iss >> y >> z) {
					eq->Face(x, y, z);
				} else {
					std::string rest;
					std::getline(iss, rest);
					std::string full_name = arg + rest;
					eq->FaceEntity(full_name);
				}
			} else {
				std::cout << "Usage: face <x> <y> <z> or face <entity_name>\n";
			}
		}
		else if (command == "turn") {
			float degrees;
			if (iss >> degrees) {
				float heading = degrees * 512.0f / 360.0f;
				while (heading < 0.0f) heading += 512.0f;
				while (heading >= 512.0f) heading -= 512.0f;
				eq->SetHeading(heading);
				eq->SendPositionUpdate();
				std::cout << fmt::format("Turned to heading {:.1f} degrees (EQ heading: {:.1f})\n", degrees, heading);
			} else {
				std::cout << "Usage: turn <degrees> (0=North, 90=East, 180=South, 270=West)\n";
			}
		}
		else if (command == "loc") {
			auto pos = eq->GetPosition();
			std::cout << fmt::format("Current position: ({:.2f}, {:.2f}, {:.2f}) heading {:.1f}\n",
				pos.x, pos.y, pos.z, eq->GetHeading());
		}
		else if (command == "list") {
			std::string search;
			std::getline(iss, search);
			if (!search.empty() && search[0] == ' ') search = search.substr(1);
			eq->ListEntities(search);
		}
		else if (command == "dump") {
			std::string name;
			std::getline(iss, name);
			if (!name.empty() && name[0] == ' ') name = name.substr(1);
			if (!name.empty()) {
				eq->DumpEntityAppearance(name);
			} else {
				std::cout << "Usage: dump <entity_name>\n";
			}
		}
		else if (command == "walk") {
			eq->SetMovementMode(MOVE_MODE_WALK);
			std::cout << "Movement mode set to walk\n";
		}
		else if (command == "run") {
			eq->SetMovementMode(MOVE_MODE_RUN);
			std::cout << "Movement mode set to run\n";
		}
		else if (command == "sneak") {
			eq->SetMovementMode(MOVE_MODE_SNEAK);
			std::cout << "Movement mode set to sneak\n";
		}
		else if (command == "sit") {
			eq->SetPositionState(POS_SITTING);
			std::cout << "Character is now sitting\n";
		}
		else if (command == "stand") {
			eq->SetPositionState(POS_STANDING);
			std::cout << "Character is now standing\n";
		}
		else if (command == "crouch" || command == "duck") {
			eq->SetPositionState(POS_CROUCHING);
			std::cout << "Character is now crouching\n";
		}
		else if (command == "feign" || command == "fd") {
			eq->SetPositionState(POS_FEIGN_DEATH);
			std::cout << "Character is feigning death\n";
		}
		else if (command == "jump") {
			eq->Jump();
			std::cout << "Character jumps!\n";
		}
		else if (command == "afk") {
			std::string state;
			if (iss >> state) {
				bool afk = (state == "on" || state == "true" || state == "1");
				eq->SetAFK(afk);
			} else {
				eq->SetAFK(!eq->IsAFK());
			}
		}
		else if (command == "anon" || command == "anonymous") {
			std::string state;
			if (iss >> state) {
				bool anon = (state == "on" || state == "true" || state == "1");
				eq->SetAnonymous(anon);
			} else {
				eq->SetAnonymous(!eq->IsAnonymous());
			}
		}
		else if (command == "roleplay" || command == "rp") {
			std::string state;
			if (iss >> state) {
				bool rp = (state == "on" || state == "true" || state == "1");
				eq->SetRoleplay(rp);
			} else {
				eq->SetRoleplay(!eq->IsRoleplay());
			}
		}
		else if (command == "emote" || command == "em") {
			std::string emote_name;
			if (iss >> emote_name) {
				uint32_t anim = 0;
				if (emote_name == "wave") anim = ANIM_WAVE;
				else if (emote_name == "cheer") anim = ANIM_CHEER;
				else if (emote_name == "dance") anim = ANIM_DANCE;
				else if (emote_name == "cry") anim = ANIM_CRY;
				else if (emote_name == "kneel") anim = ANIM_KNEEL;
				else if (emote_name == "laugh") anim = ANIM_LAUGH;
				else if (emote_name == "point") anim = ANIM_POINT;
				else if (emote_name == "salute") anim = ANIM_SALUTE;
				else if (emote_name == "shrug") anim = ANIM_SHRUG;
				else {
					std::cout << "Unknown emote. Available: wave, cheer, dance, cry, kneel, laugh, point, salute, shrug\n";
				}
				if (anim != 0) {
					eq->PerformEmote(anim);
					std::cout << fmt::format("Performing {} emote\n", emote_name);
				}
			} else {
				std::cout << "Usage: emote <name>\n";
				std::cout << "Available emotes: wave, cheer, dance, cry, kneel, laugh, point, salute, shrug\n";
			}
		}
		else if (command == "wave") {
			eq->PerformEmote(ANIM_WAVE);
			std::cout << "You wave\n";
		}
		else if (command == "dance") {
			eq->PerformEmote(ANIM_DANCE);
			std::cout << "You dance\n";
		}
		else if (command == "cheer") {
			eq->PerformEmote(ANIM_CHEER);
			std::cout << "You cheer\n";
		}
		else if (command == "laugh") {
			eq->PerformEmote(ANIM_LAUGH);
			std::cout << "You laugh\n";
		}
		else if (command == "pathfinding") {
			std::string state;
			if (iss >> state) {
				if (state == "on" || state == "true" || state == "1") {
					eq->SetPathfinding(true);
					std::cout << "Pathfinding enabled\n";
				} else if (state == "off" || state == "false" || state == "0") {
					eq->SetPathfinding(false);
					std::cout << "Pathfinding disabled\n";
				} else {
					std::cout << fmt::format("Current pathfinding state: {}\n",
						eq->IsPathfindingEnabled() ? "enabled" : "disabled");
				}
			} else {
				std::cout << fmt::format("Current pathfinding state: {}\n",
					eq->IsPathfindingEnabled() ? "enabled" : "disabled");
			}
		}
		else if (command == "target") {
			std::string entity_name;
			std::getline(iss, entity_name);
			if (!entity_name.empty() && entity_name[0] == ' ') entity_name = entity_name.substr(1);
			if (!entity_name.empty()) {
				auto* combat = eq->GetCombatManager();
				if (combat && combat->SetTarget(entity_name)) {
					std::cout << fmt::format("Target set to: {}\n", entity_name);
				} else {
					std::cout << fmt::format("Failed to target '{}'\n", entity_name);
				}
			} else {
				std::cout << "Usage: target <entity_name>\n";
			}
		}
		else if (command == "attack") {
			auto* combat = eq->GetCombatManager();
			if (combat) {
				if (combat->HasTarget()) {
					combat->EnableAutoAttack();
					combat->EnableAutoMovement();
					std::cout << "Auto attack enabled (with auto movement)\n";
				} else {
					std::cout << "No target selected. Use 'target <name>' first.\n";
				}
			}
		}
		else if (command == "stopattack") {
			auto* combat = eq->GetCombatManager();
			if (combat) {
				combat->DisableAutoAttack();
				combat->DisableAutoMovement();
				std::cout << "Auto attack disabled\n";
			}
		}
		else if (command == "~" || command == "aa") {
			auto* combat = eq->GetCombatManager();
			if (combat) {
				if (combat->IsAutoAttackEnabled()) {
					combat->DisableAutoAttack();
					combat->DisableAutoMovement();
					std::cout << "Auto attack disabled\n";
				} else {
					if (combat->HasTarget()) {
						combat->EnableAutoAttack();
						std::cout << "Auto attack enabled (no auto movement)\n";
					} else {
						std::cout << "No target selected. Use 'target <name>' first.\n";
					}
				}
			}
		}
		else if (command == "consider" || command == "con") {
			auto* combat = eq->GetCombatManager();
			if (combat) {
				if (combat->HasTarget()) {
					combat->ConsiderTarget();
					std::cout << "Considering target...\n";
				} else {
					std::cout << "No target selected. Use 'target <name>' first.\n";
				}
			}
		}
		else if (command == "loot") {
			std::cout << "Loot functionality requires corpse detection - use 'list' to find corpse IDs\n";
		}
		else if (command == "listtargets") {
			auto* combat = eq->GetCombatManager();
			if (combat) {
				combat->ListHuntTargets();
			}
		}
		else if (command == "hunt") {
			std::string state;
			auto* combat = eq->GetCombatManager();
			if (combat) {
				if (iss >> state) {
					bool enable = (state == "on" || state == "true" || state == "1");
					combat->SetAutoHunting(enable);
					std::cout << fmt::format("Auto-hunting mode {}\n", enable ? "enabled" : "disabled");
				} else {
					combat->SetAutoHunting(!combat->IsAutoHunting());
					std::cout << fmt::format("Auto-hunting mode {}\n",
						combat->IsAutoHunting() ? "enabled" : "disabled");
				}
			}
		}
		else if (command == "autoloot") {
			std::string state;
			auto* combat = eq->GetCombatManager();
			if (combat) {
				if (iss >> state) {
					bool enable = (state == "on" || state == "true" || state == "1");
					combat->SetAutoLootEnabled(enable);
					std::cout << fmt::format("Auto-loot {}\n", enable ? "enabled" : "disabled");
				} else {
					std::cout << fmt::format("Auto-loot is currently {}\n",
						combat->IsAutoLootEnabled() ? "enabled" : "disabled");
				}
			}
		}
		else {
			LOG_DEBUG(MOD_MAIN, "Unknown command received: '{}'", command);
			std::cout << fmt::format("Unknown command: '{}'. Type 'help' for commands.\n", command);
		}
	}
}

int main(int argc, char *argv[]) {
	// Parse command line arguments
	int debug_level = 0;
	std::string config_file = "willeq.json";
	bool pathfinding_enabled = true;
#ifdef EQT_HAS_GRAPHICS
	bool graphics_enabled = true;
	bool use_opengl = false;
	int graphics_width = 800;
	int graphics_height = 600;
	EQT::Graphics::ConstrainedRenderingPreset constrained_preset = EQT::Graphics::ConstrainedRenderingPreset::None;
	bool frame_timing_enabled = false;
	bool scene_profile_enabled = false;
#ifdef WITH_RDP
	bool rdp_enabled = false;
	uint16_t rdp_port = 3389;
#endif
#endif

#ifdef WITH_AUDIO
	bool audio_enabled = true;
	float audio_master_volume = 1.0f;
	float audio_music_volume = 0.5f;   // 50% - XMI music can be loud
	float audio_effects_volume = 1.0f;
	std::string audio_soundfont;
	std::string audio_vendor_music = "gl.xmi";  // Music played when vendor window opens
#endif

	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--debug" || arg == "-d") {
			if (i + 1 < argc) {
				debug_level = std::atoi(argv[++i]);
			}
		} else if (arg == "--config" || arg == "-c") {
			if (i + 1 < argc) {
				config_file = argv[++i];
			}
		} else if (arg == "--no-pathfinding" || arg == "-np") {
			pathfinding_enabled = false;
#ifdef EQT_HAS_GRAPHICS
		} else if (arg == "--no-graphics" || arg == "-ng") {
			graphics_enabled = false;
		} else if (arg == "--resolution" || arg == "-r") {
			if (i + 2 < argc) {
				graphics_width = std::atoi(argv[++i]);
				graphics_height = std::atoi(argv[++i]);
			}
		} else if (arg == "--opengl" || arg == "--gpu") {
			use_opengl = true;
		} else if (arg == "--constrained") {
			if (i + 1 < argc) {
				std::string preset_name = argv[++i];
				constrained_preset = EQT::Graphics::ConstrainedRendererConfig::parsePreset(preset_name);
				if (constrained_preset == EQT::Graphics::ConstrainedRenderingPreset::None && preset_name != "none") {
					std::cerr << "Unknown constrained preset: " << preset_name << "\n";
					std::cerr << "Valid presets: none, voodoo1, voodoo2, tnt\n";
					return 1;
				}
			}
		} else if (arg == "--frame-timing" || arg == "--ft") {
			frame_timing_enabled = true;
		} else if (arg == "--scene-profile" || arg == "--sp") {
			scene_profile_enabled = true;
#ifdef WITH_RDP
		} else if (arg == "--rdp" || arg == "--enable-rdp") {
			rdp_enabled = true;
		} else if (arg == "--rdp-port") {
			if (i + 1 < argc) {
				rdp_port = static_cast<uint16_t>(std::atoi(argv[++i]));
			}
#endif
#endif
#ifdef WITH_AUDIO
		} else if (arg == "--no-audio" || arg == "-na") {
			audio_enabled = false;
		} else if (arg == "--audio-volume") {
			if (i + 1 < argc) {
				int vol = std::atoi(argv[++i]);
				audio_master_volume = std::clamp(vol, 0, 100) / 100.0f;
			}
		} else if (arg == "--music-volume") {
			if (i + 1 < argc) {
				int vol = std::atoi(argv[++i]);
				audio_music_volume = std::clamp(vol, 0, 100) / 100.0f;
			}
		} else if (arg == "--effects-volume") {
			if (i + 1 < argc) {
				int vol = std::atoi(argv[++i]);
				audio_effects_volume = std::clamp(vol, 0, 100) / 100.0f;
			}
		} else if (arg == "--soundfont") {
			if (i + 1 < argc) {
				audio_soundfont = argv[++i];
			}
#endif
		} else if (arg == "--help" || arg == "-h") {
			std::cout << "Usage: " << argv[0] << " [options]\n";
			std::cout << "Options:\n";
			std::cout << "  -d, --debug <level>      Set debug level (0-3)\n";
			std::cout << "  -c, --config <file>      Set config file (default: willeq.json)\n";
			std::cout << "  -np, --no-pathfinding    Disable navmesh pathfinding\n";
#ifdef EQT_HAS_GRAPHICS
			std::cout << "  -ng, --no-graphics       Disable graphical rendering\n";
			std::cout << "  -r, --resolution <W> <H> Set graphics resolution (default: 800 600)\n";
			std::cout << "  --opengl, --gpu          Use OpenGL renderer (default: software)\n";
			std::cout << "  --constrained <preset>   Enable constrained rendering mode (voodoo1, voodoo2, tnt)\n";
			std::cout << "  --frame-timing, --ft     Enable frame timing profiler (logs every ~2s)\n";
			std::cout << "  --scene-profile, --sp    Run scene breakdown profiler after zone load\n";
#ifdef WITH_RDP
			std::cout << "  --rdp, --enable-rdp      Enable native RDP server for remote access\n";
			std::cout << "  --rdp-port <port>        RDP server port (default: 3389)\n";
#endif
#endif
#ifdef WITH_AUDIO
			std::cout << "  -na, --no-audio          Disable audio\n";
			std::cout << "  --audio-volume <0-100>   Master volume (default: 100)\n";
			std::cout << "  --music-volume <0-100>   Music volume (default: 70)\n";
			std::cout << "  --effects-volume <0-100> Sound effects volume (default: 100)\n";
			std::cout << "  --soundfont <path>       Path to SoundFont for MIDI playback\n";
#endif
			std::cout << "  --log-level=LEVEL        Set log level (NONE, FATAL, ERROR, WARN, INFO, DEBUG, TRACE)\n";
			std::cout << "  --log-module=MOD:LEVEL   Set per-module log level (e.g., NET:DEBUG, GRAPHICS:TRACE)\n";
			std::cout << "                           Modules: NET, NET_PACKET, LOGIN, WORLD, ZONE, ENTITY,\n";
			std::cout << "                                    MOVEMENT, COMBAT, INVENTORY, GRAPHICS, GRAPHICS_LOAD,\n";
			std::cout << "                                    CAMERA, INPUT, AUDIO, PATHFIND, MAP, UI, CONFIG, MAIN\n";
#ifndef _WIN32
			std::cout << "  Signal SIGUSR1           Increase log level at runtime\n";
			std::cout << "  Signal SIGUSR2           Decrease log level at runtime\n";
#endif
			std::cout << "  -h, --help               Show this help message\n";
			return 0;
		}
	}

	// Initialize logging from command-line args (--log-level, --log-module)
	InitLogging(argc, argv);

	// Legacy debug level support
	EverQuest::SetDebugLevel(debug_level);
	if (debug_level > 0) {
		// Only set if not already set by --log-level
		if (GetLogLevel() < LOG_DEBUG) {
			SetLogLevel(LOG_DEBUG);
		}
	}

#ifndef _WIN32
	// Register signal handlers for runtime log level adjustment
	signal(SIGUSR1, HandleSigUsr1);
	signal(SIGUSR2, HandleSigUsr2);
#ifdef EQT_HAS_GRAPHICS
	// Register SIGHUP handler for hotkey configuration reload
	signal(SIGHUP, HandleSigHup);
#endif
#endif

	LOG_INFO(MOD_MAIN, "Starting WillEQ with debug level {}, config file: {}, pathfinding: {}",
		debug_level, config_file, pathfinding_enabled ? "enabled" : "disabled");
	LOG_INFO(MOD_MAIN, "Log level: {} (use --log-level=LEVEL to change)", GetLevelName(static_cast<LogLevel>(GetLogLevel())));

	// Start tracking startup metrics
	EQT::PerformanceMetrics::instance().startTimer("Config Loading", EQT::MetricCategory::Startup);
	auto config = EQ::JsonConfigFile::Load(config_file);
	auto config_handle = config.RawHandle();

	// Parse logging configuration from config file (if present)
	// Config can be either:
	// 1. An array of client configs (legacy)
	// 2. An object with "logging" and "clients" sections (new format)
	Json::Value clients_config;
	if (config_handle.isArray()) {
		// Legacy format: top-level array
		clients_config = config_handle;

#ifdef EQT_HAS_GRAPHICS
		// Still load UI settings even for legacy config format
		eqt::ui::UISettings::instance().loadFromFile("config/ui_settings.json");

		// Load hotkey settings for legacy config format
		{
			auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
			hotkeyMgr.resetToDefaults();
			hotkeyMgr.loadFromFile("config/hotkeys.json");
			hotkeyMgr.logConflicts();
		}
#endif
	} else if (config_handle.isObject()) {
		// New format: object with optional logging and clients sections
		InitLoggingFromJson(config_handle);

#ifdef EQT_HAS_GRAPHICS
		// Load UI settings from default file, then apply any overrides from main config
		{
			auto& uiSettings = eqt::ui::UISettings::instance();

			// 1. Load defaults from config/ui_settings.json
			uiSettings.loadFromFile("config/ui_settings.json");

			// 2. Apply overrides from main config if present
			if (config_handle.isMember("uiSettings")) {
				LOG_INFO(MOD_UI, "Applying UI settings overrides from main config");
				uiSettings.applyOverrides(config_handle["uiSettings"]);
			}

			// 3. Apply chat settings overrides from main config if present
			// This allows configuring chat tabs (Combat tab, etc.) from the main config
			if (config_handle.isMember("chatSettings")) {
				LOG_INFO(MOD_UI, "Applying chat settings overrides from main config");
				uiSettings.applyChatSettingsOverride(config_handle["chatSettings"]);
			}
		}

		// Load hotkey settings from default file, then apply any overrides from main config
		{
			auto& hotkeyMgr = eqt::input::HotkeyManager::instance();

			// 1. Reset to defaults first
			hotkeyMgr.resetToDefaults();

			// 2. Load from config/hotkeys.json if it exists
			hotkeyMgr.loadFromFile("config/hotkeys.json");

			// 3. Apply overrides from main config if present
			if (config_handle.isMember("hotkeys")) {
				LOG_INFO(MOD_INPUT, "Applying hotkey overrides from main config");
				hotkeyMgr.applyOverrides(config_handle["hotkeys"]);
			}

			// 4. Log any conflicts
			hotkeyMgr.logConflicts();
		}

		// Parse rendering config from main config
		if (config_handle.isMember("rendering")) {
			const auto& rendering = config_handle["rendering"];
			if (rendering.isMember("constrained_mode")) {
				std::string preset_name = rendering["constrained_mode"].asString();
				auto preset = EQT::Graphics::ConstrainedRendererConfig::parsePreset(preset_name);
				if (preset != EQT::Graphics::ConstrainedRenderingPreset::None || preset_name == "none") {
					constrained_preset = preset;
					LOG_INFO(MOD_GRAPHICS, "Constrained rendering mode from config: {}", preset_name);
				} else {
					LOG_WARN(MOD_GRAPHICS, "Unknown constrained preset in config: {}", preset_name);
				}
			}
		}

#ifdef WITH_RDP
		// Parse RDP config from main config
		if (config_handle.isMember("rdp")) {
			const auto& rdp_config = config_handle["rdp"];
			if (rdp_config.isMember("enabled") && rdp_config["enabled"].asBool()) {
				rdp_enabled = true;
				LOG_INFO(MOD_GRAPHICS, "RDP server enabled from config");
			}
			if (rdp_config.isMember("port")) {
				rdp_port = static_cast<uint16_t>(rdp_config["port"].asInt());
				LOG_INFO(MOD_GRAPHICS, "RDP port from config: {}", rdp_port);
			}
		}
#endif
#endif

#ifdef WITH_AUDIO
		// Parse audio config from main config
		if (config_handle.isMember("audio")) {
			const auto& audio_config = config_handle["audio"];
			if (audio_config.isMember("enabled")) {
				audio_enabled = audio_config["enabled"].asBool();
				LOG_INFO(MOD_AUDIO, "Audio {} from config", audio_enabled ? "enabled" : "disabled");
			}
			if (audio_config.isMember("master_volume")) {
				int vol = audio_config["master_volume"].asInt();
				audio_master_volume = std::clamp(vol, 0, 100) / 100.0f;
				LOG_INFO(MOD_AUDIO, "Master volume from config: {}%", vol);
			}
			if (audio_config.isMember("music_volume")) {
				int vol = audio_config["music_volume"].asInt();
				audio_music_volume = std::clamp(vol, 0, 100) / 100.0f;
				LOG_INFO(MOD_AUDIO, "Music volume from config: {}%", vol);
			}
			if (audio_config.isMember("effects_volume")) {
				int vol = audio_config["effects_volume"].asInt();
				audio_effects_volume = std::clamp(vol, 0, 100) / 100.0f;
				LOG_INFO(MOD_AUDIO, "Effects volume from config: {}%", vol);
			}
			if (audio_config.isMember("soundfont")) {
				audio_soundfont = audio_config["soundfont"].asString();
				LOG_INFO(MOD_AUDIO, "SoundFont from config: {}", audio_soundfont);
			}
			if (audio_config.isMember("vendor_music")) {
				audio_vendor_music = audio_config["vendor_music"].asString();
				LOG_INFO(MOD_AUDIO, "Vendor music from config: {}", audio_vendor_music);
			}
		}
#endif

		if (config_handle.isMember("clients") && config_handle["clients"].isArray()) {
			clients_config = config_handle["clients"];
		} else {
			// Single client config at top level (also legacy)
			clients_config.append(config_handle);
		}
	}

	EQT::PerformanceMetrics::instance().stopTimer("Config Loading");

	std::vector<std::unique_ptr<EverQuest>> eq_list;

	EQT::PerformanceMetrics::instance().startTimer("Client Creation", EQT::MetricCategory::Startup);
	try {
		for (int i = 0; i < (int)clients_config.size(); ++i) {
			auto c = clients_config[i];

			auto host = c["host"].asString();
			auto port = c["port"].asInt();
			auto user = c["user"].asString();
			auto pass = c["pass"].asString();
			auto server = c["server"].asString();
			auto character = c["character"].asString();

			std::string navmesh_path;
			if (c.isMember("navmesh_path")) {
				navmesh_path = c["navmesh_path"].asString();
			}

			std::string maps_path;
			if (c.isMember("maps_path")) {
				maps_path = c["maps_path"].asString();
			}

#ifdef EQT_HAS_GRAPHICS
			std::string eq_client_path;
			if (c.isMember("eq_client_path")) {
				eq_client_path = c["eq_client_path"].asString();
			}
#endif

			LOG_INFO(MOD_MAIN, "Connecting to {}:{} as Account '{}' to Server '{}' under Character '{}'",
				host, port, user, server, character);

			auto eq = std::make_unique<EverQuest>(host, port, user, pass, server, character);
			eq->SetPathfinding(pathfinding_enabled);
			if (!navmesh_path.empty()) {
				eq->SetNavmeshPath(navmesh_path);
			}
			if (!maps_path.empty()) {
				eq->SetMapsPath(maps_path);
			}

#ifdef EQT_HAS_GRAPHICS
			if (!eq_client_path.empty()) {
				eq->SetEQClientPath(eq_client_path);
			}
			eq->SetConfigPath(config_file);
#endif

#ifdef WITH_AUDIO
			// Apply audio settings from command line
			eq->SetAudioEnabled(audio_enabled);
			eq->SetMasterVolume(audio_master_volume);
			eq->SetMusicVolume(audio_music_volume);
			eq->SetEffectsVolume(audio_effects_volume);
			if (!audio_soundfont.empty()) {
				eq->SetSoundFont(audio_soundfont);
			}
			eq->SetVendorMusic(audio_vendor_music);
#endif

			eq_list.push_back(std::move(eq));
		}
	}
	catch (std::exception &ex) {
		std::cout << fmt::format("Error parsing config file: {}\n", ex.what());
		return 1;
	}
	EQT::PerformanceMetrics::instance().stopTimer("Client Creation");

	if (eq_list.empty()) {
		std::cout << "No client configurations found in config file.\n";
		return 1;
	}

#ifdef EQT_HAS_GRAPHICS
	// Initialize graphics early so we can show loading screen during connection
	bool graphics_initialized = false;
	if (graphics_enabled && !eq_list.empty() && !eq_list[0]->GetEQClientPath().empty()) {
		LOG_DEBUG(MOD_GRAPHICS, "Initializing graphics early for loading screen...");
		eq_list[0]->SetUseOpenGL(use_opengl);
		eq_list[0]->SetConstrainedPreset(constrained_preset);
		EQT::PerformanceMetrics::instance().startTimer("Graphics Init", EQT::MetricCategory::Startup);
		if (eq_list[0]->InitGraphics(graphics_width, graphics_height)) {
			graphics_initialized = true;
			LOG_INFO(MOD_GRAPHICS, "Graphics initialized - showing loading screen");
			// Set initial loading state
			auto* renderer = eq_list[0]->GetRenderer();
			if (renderer) {
				renderer->setLoadingTitle(L"EverQuest");
				renderer->setLoadingProgress(0.0f, L"Connecting to login server...");
				if (frame_timing_enabled) {
					renderer->setFrameTimingEnabled(true);
				}
				if (scene_profile_enabled) {
					// Schedule scene profile - it will run after zone is loaded
					renderer->runSceneProfile();
				}
#ifdef WITH_RDP
				// Initialize and start RDP server if enabled
				if (rdp_enabled) {
					LOG_INFO(MOD_GRAPHICS, "Initializing RDP server on port {}...", rdp_port);
					if (renderer->initRDP(rdp_port)) {
						if (renderer->startRDPServer()) {
							LOG_INFO(MOD_GRAPHICS, "RDP server started on port {}", rdp_port);
							// Set up RDP audio streaming for all clients
							for (auto& eq : eq_list) {
								if (eq) {
									eq->SetupRDPAudio();
								}
							}
						} else {
							LOG_WARN(MOD_GRAPHICS, "Failed to start RDP server");
						}
					} else {
						LOG_WARN(MOD_GRAPHICS, "Failed to initialize RDP server");
					}
				}
#endif
			}
		} else {
			LOG_WARN(MOD_GRAPHICS, "Failed to initialize graphics - running headless");
			graphics_enabled = false;
		}
		EQT::PerformanceMetrics::instance().stopTimer("Graphics Init");
	} else if (graphics_enabled && !eq_list.empty() && eq_list[0]->GetEQClientPath().empty()) {
		LOG_INFO(MOD_GRAPHICS, "No eq_client_path in config - running headless");
		graphics_enabled = false;
	}
#endif

	std::atomic<bool> running(true);
	std::queue<std::string> command_queue;
	std::mutex command_mutex;
	bool fully_connected_announced = false;

#ifndef _WIN32
	std::atomic<bool> keyboard_mode(true);
	std::string command_buffer;

	EnableRawMode();

	std::thread input_thread([&]() {
		LOG_DEBUG(MOD_MAIN, "Input thread started in keyboard mode");

		// Track key states with timestamps for timeout
		struct KeyState {
			bool pressed = false;
			std::chrono::steady_clock::time_point last_press;
		};

		KeyState w_key, a_key, s_key, d_key;
		KeyState up_key, down_key, left_key, right_key;
		KeyState space_key;

		const auto key_timeout = std::chrono::milliseconds(500);

		while (running.load()) {
			if (KeyPressed()) {
				char c = ReadKey();

				if (keyboard_mode.load()) {
					if (c == '\r' || c == '\n') {
						keyboard_mode.store(false);
						DisableRawMode();
						std::cout << "[Command mode active. Type command and press Enter]\n> ";
						std::cout.flush();

						if (!eq_list.empty() && eq_list[0]->IsFullyZonedIn()) {
							eq_list[0]->StopMoveForward();
							eq_list[0]->StopMoveBackward();
							eq_list[0]->StopTurnLeft();
							eq_list[0]->StopTurnRight();
							w_key.pressed = false;
							s_key.pressed = false;
							a_key.pressed = false;
							d_key.pressed = false;
							up_key.pressed = false;
							down_key.pressed = false;
							left_key.pressed = false;
							right_key.pressed = false;
							space_key.pressed = false;
						}
					} else if (!eq_list.empty() && eq_list[0]->IsFullyZonedIn()) {
						if (c == 'w' || c == 'W') {
							if (!w_key.pressed) {
								eq_list[0]->StartMoveForward();
								w_key.pressed = true;
							}
							w_key.last_press = std::chrono::steady_clock::now();
						} else if (c == 's' || c == 'S') {
							if (!s_key.pressed) {
								eq_list[0]->StartMoveBackward();
								s_key.pressed = true;
							}
							s_key.last_press = std::chrono::steady_clock::now();
						} else if (c == 'a' || c == 'A') {
							if (!a_key.pressed) {
								eq_list[0]->StartTurnLeft();
								a_key.pressed = true;
							}
							a_key.last_press = std::chrono::steady_clock::now();
						} else if (c == 'd' || c == 'D') {
							if (!d_key.pressed) {
								eq_list[0]->StartTurnRight();
								d_key.pressed = true;
							}
							d_key.last_press = std::chrono::steady_clock::now();
						} else if (c == ' ') {
							eq_list[0]->Jump();
							space_key.pressed = true;
							space_key.last_press = std::chrono::steady_clock::now();
						} else if (c == 27) {  // ESC sequence for arrow keys
							char seq[2];
							if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
								if (read(STDIN_FILENO, &seq[1], 1) == 1) {
									switch (seq[1]) {
										case 'A':  // Up arrow
											if (!up_key.pressed) {
												eq_list[0]->StartMoveForward();
												up_key.pressed = true;
											}
											up_key.last_press = std::chrono::steady_clock::now();
											break;
										case 'B':  // Down arrow
											if (!down_key.pressed) {
												eq_list[0]->StartMoveBackward();
												down_key.pressed = true;
											}
											down_key.last_press = std::chrono::steady_clock::now();
											break;
										case 'C':  // Right arrow
											if (!right_key.pressed) {
												eq_list[0]->StartTurnRight();
												right_key.pressed = true;
											}
											right_key.last_press = std::chrono::steady_clock::now();
											break;
										case 'D':  // Left arrow
											if (!left_key.pressed) {
												eq_list[0]->StartTurnLeft();
												left_key.pressed = true;
											}
											left_key.last_press = std::chrono::steady_clock::now();
											break;
									}
								}
							}
						}
					}
				} else {
					if (c == '\r' || c == '\n') {
						if (!command_buffer.empty()) {
							std::cout << std::endl;
							{
								std::lock_guard<std::mutex> lock(command_mutex);
								command_queue.push(command_buffer);
							}
							command_buffer.clear();
						}
						keyboard_mode.store(true);
						EnableRawMode();
					} else if (c == 127 || c == 8) {
						if (!command_buffer.empty()) {
							command_buffer.pop_back();
							std::cout << "\b \b";
							std::cout.flush();
						}
					} else if (c >= 32 && c < 127) {
						command_buffer += c;
						std::cout << c;
						std::cout.flush();
					}
				}
			}

			// Check for key timeouts and release keys if needed
			if (keyboard_mode.load() && !eq_list.empty() && eq_list[0]->IsFullyZonedIn()) {
				auto now = std::chrono::steady_clock::now();

				// Check W/Up keys
				if ((w_key.pressed && std::chrono::duration_cast<std::chrono::milliseconds>(now - w_key.last_press) > key_timeout) ||
				    (up_key.pressed && std::chrono::duration_cast<std::chrono::milliseconds>(now - up_key.last_press) > key_timeout)) {
					if (w_key.pressed || up_key.pressed) {
						eq_list[0]->StopMoveForward();
						w_key.pressed = false;
						up_key.pressed = false;
					}
				}

				// Check S/Down keys
				if ((s_key.pressed && std::chrono::duration_cast<std::chrono::milliseconds>(now - s_key.last_press) > key_timeout) ||
				    (down_key.pressed && std::chrono::duration_cast<std::chrono::milliseconds>(now - down_key.last_press) > key_timeout)) {
					if (s_key.pressed || down_key.pressed) {
						eq_list[0]->StopMoveBackward();
						s_key.pressed = false;
						down_key.pressed = false;
					}
				}

				// Check A/Left keys
				if ((a_key.pressed && std::chrono::duration_cast<std::chrono::milliseconds>(now - a_key.last_press) > key_timeout) ||
				    (left_key.pressed && std::chrono::duration_cast<std::chrono::milliseconds>(now - left_key.last_press) > key_timeout)) {
					if (a_key.pressed || left_key.pressed) {
						eq_list[0]->StopTurnLeft();
						a_key.pressed = false;
						left_key.pressed = false;
					}
				}

				// Check D/Right keys
				if ((d_key.pressed && std::chrono::duration_cast<std::chrono::milliseconds>(now - d_key.last_press) > key_timeout) ||
				    (right_key.pressed && std::chrono::duration_cast<std::chrono::milliseconds>(now - right_key.last_press) > key_timeout)) {
					if (d_key.pressed || right_key.pressed) {
						eq_list[0]->StopTurnRight();
						d_key.pressed = false;
						right_key.pressed = false;
					}
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		DisableRawMode();
		LOG_DEBUG(MOD_MAIN, "Input thread terminating");
	});
#else
	// Windows simple line input
	std::thread input_thread([&]() {
		std::string line;
		while (running.load()) {
			std::cout << "> ";
			if (std::getline(std::cin, line)) {
				if (!line.empty()) {
					std::lock_guard<std::mutex> lock(command_mutex);
					command_queue.push(line);
				}
			}
		}
	});
#endif

	std::thread command_thread([&]() {
		while (running.load() && (eq_list.empty() || !eq_list[0]->IsFullyZonedIn())) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		while (running.load()) {
			std::string cmd;
			{
				std::lock_guard<std::mutex> lock(command_mutex);
				if (!command_queue.empty()) {
					cmd = command_queue.front();
					command_queue.pop();
				}
			}

			if (!cmd.empty()) {
				ProcessCommand(cmd, eq_list, running);
			} else {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}
	});

	LOG_INFO(MOD_MAIN, "WillEQ ready. Type 'help' for commands");
	LOG_INFO(MOD_MAIN, "Waiting for zone connection...");

#ifdef EQT_HAS_GRAPHICS
	auto last_graphics_update = std::chrono::steady_clock::now();
#endif

	auto last_update = std::chrono::steady_clock::now();

	static int loop_counter = 0;
	LOG_TRACE(MOD_MAIN, "Entering main loop");

	while (running.load()) {
		try {
			loop_counter++;

			// Log every iteration when a zone change might be happening
			bool zone_change_happening = !eq_list.empty() && eq_list[0]->IsZoneChangeApproved();
			if (loop_counter % 100 == 0 || zone_change_happening) {
				LOG_TRACE(MOD_MAIN, "Main loop iteration {} graphics_init={} running={} zone_change={}",
				          loop_counter, graphics_initialized, running.load(), zone_change_happening);
			}

			auto eventLoopStart = std::chrono::steady_clock::now();
			EQ::EventLoop::Get().Process();
			auto eventLoopEnd = std::chrono::steady_clock::now();
			auto eventLoopMs = std::chrono::duration_cast<std::chrono::milliseconds>(eventLoopEnd - eventLoopStart).count();
			if (eventLoopMs > 50) {
				LOG_WARN(MOD_MAIN, "PERF: EventLoop::Process() took {} ms", eventLoopMs);
			}

			// Debug: Check if we're still alive after event processing
			if (loop_counter % 100 == 1 || zone_change_happening) {
				LOG_TRACE(MOD_MAIN, "After EventLoop::Process()");
			}

			bool any_connected = !eq_list.empty() && eq_list[0]->IsFullyZonedIn();

			if (any_connected && !fully_connected_announced) {
				LOG_INFO(MOD_MAIN, "Fully connected to zone!");
				fully_connected_announced = true;

#ifdef EQT_HAS_GRAPHICS
				// NOTE: Zone graphics are now loaded automatically via the LoadingPhase system.
				// LoadZoneGraphics() is called from OnGameStateComplete() when the game state is ready.
				// We only need to load the hotbar config here.
				if (graphics_enabled && graphics_initialized && !eq_list.empty()) {
					// Load hotbar config from per-character config file
					eq_list[0]->LoadHotbarConfig();
				}
#endif
			}

			// Track zone change state for debugging
			bool was_zone_change = !eq_list.empty() && eq_list[0]->IsZoneChangeApproved();

			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count() >= 16) {
				auto movementStart = std::chrono::steady_clock::now();
				for (auto& eq : eq_list) {
					try {
						eq->UpdateMovement();
					} catch (const std::exception& e) {
						std::cerr << "[ERROR] Exception in UpdateMovement: " << e.what() << std::endl;
					}
				}
				auto movementEnd = std::chrono::steady_clock::now();
				auto movementMs = std::chrono::duration_cast<std::chrono::milliseconds>(movementEnd - movementStart).count();
				if (movementMs > 50) {
					LOG_WARN(MOD_MAIN, "PERF: UpdateMovement() took {} ms", movementMs);
				}
				last_update = now;

				// Debug: log after UpdateMovement if zone change was happening
				if (was_zone_change) {
					LOG_TRACE(MOD_MAIN, "After UpdateMovement (zone change was {}, now {}) running={}",
					          was_zone_change, (!eq_list.empty() && eq_list[0]->IsZoneChangeApproved()), running.load());
				} else if (loop_counter % 100 == 2) {
					LOG_TRACE(MOD_MAIN, "After UpdateMovement");
				}
			}

#ifdef EQT_HAS_GRAPHICS
			// Update graphics at ~60 FPS
			if (graphics_initialized && !eq_list.empty()) {
				auto gfx_now = std::chrono::steady_clock::now();
				float deltaTime = std::chrono::duration<float>(gfx_now - last_graphics_update).count();
				if (deltaTime >= 1.0f / 60.0f) {
					// Extra logging when zone change just happened
					bool zone_connected = eq_list[0]->IsFullyZonedIn();
					if (!zone_connected && was_zone_change) {
						LOG_TRACE(MOD_MAIN, "About to call UpdateGraphics (zone_connected={})", zone_connected);
					}

					bool graphics_ok = eq_list[0]->UpdateGraphics(deltaTime);

					if (!zone_connected && was_zone_change) {
						LOG_TRACE(MOD_MAIN, "UpdateGraphics returned {}", graphics_ok);
					}

					if (!graphics_ok) {
						// Graphics window closed
						LOG_DEBUG(MOD_GRAPHICS, "Graphics window closed");
						running.store(false);
					}
					last_graphics_update = gfx_now;
				}
			}
#endif

			// Log before sleep when zone change happened
			if (was_zone_change) {
				LOG_TRACE(MOD_MAIN, "About to sleep, running={}", running.load());
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));

			// Log after sleep when zone change happened
			if (was_zone_change) {
				LOG_TRACE(MOD_MAIN, "Woke from sleep, next iteration...");
			}
		}
		catch (const std::exception& e) {
			LOG_ERROR(MOD_MAIN, "Exception in main loop: {}", e.what());
		}
	}

	LOG_TRACE(MOD_MAIN, "Exited main loop running={} loop_counter={}", running.load(), loop_counter);

	input_thread.join();
	command_thread.join();

#ifdef EQT_HAS_GRAPHICS
	// Shutdown RDP server if running
#ifdef WITH_RDP
	if (graphics_initialized && !eq_list.empty()) {
		auto* renderer = eq_list[0]->GetRenderer();
		if (renderer && renderer->isRDPRunning()) {
			LOG_INFO(MOD_GRAPHICS, "Stopping RDP server...");
			renderer->stopRDPServer();
		}
	}
#endif
	// Shutdown graphics
	if (graphics_initialized && !eq_list.empty()) {
		eq_list[0]->ShutdownGraphics();
	}
#endif

	return 0;
}
