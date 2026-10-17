#include "client/application.h"
#include <json/json.h>  // Must be before eq.h → logging.h so JSONCPP_VERSION_STRING is defined
#include "client/eq.h"
#include "client/data_paths.h"
#include "client/help/merged_help.h"
// T310 Leg A: --fpcam. No renderer dependency -- the tunable store is a plain
// process-global of floats (see the header), so this include is safe in an
// EQT_GRAPHICS=OFF build too.
#include "client/graphics/first_person_tunables.h"
#include "client/help/renderer_command_catalog.h"
#include "client/eq_action_handler.h"
#include "client/combat.h"
#include "client/inventory/inventory_manager.h"
#include "client/mode/automated_mode.h"
#include "client/mode/headless_mode.h"
#include "client/mode/graphical_mode.h"
#include "client/spell/spell_manager.h"
#include "client/spell/spell_constants.h"
#include "common/util/json_config.h"
#include "common/logging.h"
#include "common/performance_metrics.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <algorithm>
#include <set>
#include <cctype>
#include <cstdlib>
#include <sys/stat.h>

#ifndef _WIN32
#include <signal.h>
#endif

// P28: GameStateBridge/ConsoleBridge are the headless bridge pair — needed
// unconditionally (ConsoleBridge is attached in headless mode; m_bridge is
// used from updateScript() regardless of graphics). Only the renderer/input
// headers that actually require Irrlicht stay gated.
#include "client/bridge/console_bridge.h"
#include "client/bridge/game_state_bridge.h"
#include "client/bridge/game_state_sync_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/state/event_bus.h"
#include "client/hotbar/hotbar_model.h"
#ifdef EQT_HAS_GRAPHICS
// K15 (board B006): irrlicht_renderer.h removed -- this TU names no
// symbol from it (S2 residue).
#include "client/graphics/entity_renderer.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/input/hotkey_manager.h"
#include "client/input/graphics_input_handler.h"
#include "client/bridge/irrlicht_bridge.h"
#endif

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#endif

namespace eqt {

// ========== Static Helpers ==========

ApplicationConfig Application::parseArguments(int argc, char* argv[]) {
    ApplicationConfig config;
    // B874 audit: the pose-log knobs as given (0 = not given); resolved
    // against --pose-log after the loop so flag order cannot matter.
    int poseEveryArg = 0;
    int poseMaxArg = 0;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--debug" || arg == "-d") {
            if (i + 1 < argc) {
                config.debugLevel = std::atoi(argv[++i]);
            }
        } else if (arg == "--config" || arg == "-c") {
            if (i + 1 < argc) {
                config.configFile = argv[++i];
            }
        } else if (arg == "--no-pathfinding" || arg == "-np") {
            config.pathfindingEnabled = false;
        } else if (arg == "--no-graphics" || arg == "-ng") {
            config.graphicsEnabled = false;
            config.operatingMode = mode::OperatingMode::HeadlessInteractive;
        } else if (arg == "--headless") {
            config.graphicsEnabled = false;
            config.operatingMode = mode::OperatingMode::HeadlessInteractive;
        } else if (arg == "--automated" || arg == "--bot") {
            config.graphicsEnabled = false;
            config.operatingMode = mode::OperatingMode::Automated;
        } else if (arg == "--resolution" || arg == "-r") {
            if (i + 2 < argc) {
                config.displayWidth = std::atoi(argv[++i]);
                config.displayHeight = std::atoi(argv[++i]);
            }
        } else if (arg == "--renderer") {
            if (i + 1 < argc) {
                std::string backend = argv[++i];
                if (backend == "gles2") {
                    config.rendererGles2Requested = true;
                } else if (backend == "software" || backend == "opengl") {
                    std::cerr << "Renderer '" << backend << "' was removed: only gles2 is "
                                 "supported; software/opengl were removed — "
                                 "docs/decisions.md 2026-07-28\n";
                    config.parseError = true;
                } else {
                    std::cerr << "Unknown renderer: " << backend << " (use: gles2) — "
                                 "only gles2 is supported; software/opengl were removed — "
                                 "docs/decisions.md 2026-07-28\n";
                    config.parseError = true;
                }
            }
        } else if (arg == "--constrained") {
            if (i + 1 < argc) {
                config.constrainedPreset = argv[++i];
            }
        } else if (arg == "--frame-timing" || arg == "--ft") {
            config.frameTimingEnabled = true;
        } else if (arg == "--scene-profile" || arg == "--sp") {
            config.sceneProfileEnabled = true;
        } else if (arg == "--no-audio" || arg == "-na") {
            config.audioEnabled = false;
        } else if (arg == "--no-3d-audio") {
            // K15 (board B037)
            config.audioUse3D = false;
        } else if (arg == "--audio-volume") {
            if (i + 1 < argc) {
                int vol = std::atoi(argv[++i]);
                config.audioMasterVolume = std::clamp(vol, 0, 100) / 100.0f;
            }
        } else if (arg == "--music-volume") {
            if (i + 1 < argc) {
                int vol = std::atoi(argv[++i]);
                config.audioMusicVolume = std::clamp(vol, 0, 100) / 100.0f;
            }
        } else if (arg == "--effects-volume") {
            if (i + 1 < argc) {
                int vol = std::atoi(argv[++i]);
                config.audioEffectsVolume = std::clamp(vol, 0, 100) / 100.0f;
            }
        } else if (arg == "--audio-rate") {
            // B1069 (#389): playback DEVICE rate in Hz; 0 = backend native.
            if (i + 1 < argc) {
                long hz = std::atol(argv[++i]);
                config.audioDeviceSampleRate = static_cast<uint32_t>(std::clamp(hz, 0L, 384000L));
                config.audioDeviceSampleRateFromCli = true;
            }
        } else if (arg == "--audio-format") {
            // B1069: playback DEVICE sample format (s16|f32|s24|s32|native).
            if (i + 1 < argc) {
                const std::string fmt = argv[++i];
                if (EQT::Audio::parseDeviceFormat(fmt, config.audioDeviceFormat)) {
                    config.audioDeviceFormatFromCli = true;
                } else {
                    std::cerr << "--audio-format: unknown format '" << fmt
                              << "' (s16, f32, s24, s32, native); keeping "
                              << EQT::Audio::deviceFormatName(config.audioDeviceFormat) << "\n";
                }
            }
        } else if (arg == "--soundfont") {
            if (i + 1 < argc) {
                config.audioSoundfont = argv[++i];
            }
        } else if (arg == "--eqe1-path") {
            if (i + 1 < argc) {
                config.eqe1Path = argv[++i];
            }
        } else if (arg == "--composite-entities") {
            if (i + 1 < argc) {
                config.compositeEntityPath = argv[++i];
            }
        } else if (arg == "--load-mem-audit") {
            config.loadMemAudit = true;
        } else if (arg == "--texture-ledger") {
            if (i + 1 < argc) {
                config.textureUploadLedgerPath = argv[++i];
            }
        } else if (arg == "--drm") {
            config.useDRM = true;
        } else if (arg == "--egl-x11") {
            config.useEglX11 = true;
        } else if (arg == "--rdp" || arg == "--enable-rdp") {
            config.rdpEnabled = true;
        } else if (arg == "--rdp-port") {
            if (i + 1 < argc) {
                config.rdpPort = static_cast<uint16_t>(std::atoi(argv[++i]));
            }
        } else if (arg == "--zone-load") {
            if (i + 1 < argc) {
                std::string mode = argv[++i];
                if (mode == "auto" || mode == "automatic") {
                    config.zoneLoadMode = 1;
                } else if (mode == "manual") {
                    config.zoneLoadMode = 0;
                }
            }
        } else if (arg == "--threads") {
            if (i + 1 < argc) {
                config.backgroundThreadCount = std::atoi(argv[++i]);
            }
        } else if (arg == "--record-session") {
            if (i + 1 < argc) {
                config.recordSessionPath = argv[++i];
            }
        } else if (arg == "--fpcam") {
            // T310 Leg A: the startup form of /fpcam set. Repeatable, and one
            // occurrence may carry a comma-separated list. Deliberately NOT
            // validated here -- parseArguments() is documented side-effect-
            // free (tests/test_video_capture_flags.cpp) and the tunable store
            // is a process global, so the strings are carried on the config
            // and applied by initialize().
            if (i + 1 < argc) {
                config.fpCamOverrides.push_back(argv[++i]);
            }
        } else if (arg == "--create-character") {
            // B888 (#204): world_script.h. Parsed (against the era data) in
            // initialize(); parseArguments() stays side-effect-free.
            if (i + 1 < argc) config.createCharacterSpec = argv[++i];
        } else if (arg == "--delete-character") {
            if (i + 1 < argc) config.deleteCharacterName = argv[++i];
        } else if (arg == "--list-characters") {
            config.listCharacters = true;
        } else if (arg == "--character-select") {
            // B998 (#318): login_world/character_select.h.
            config.characterSelectScreen = true;
        } else if (arg == "--scenario") {
            // T410: the scenario lane implies Automated mode -- no graphics,
            // no stdin reader, and AutomatedMode's per-tick automation
            // callback free for the runner (board B139's seam). Set here
            // rather than left to the operator so `--scenario X` alone is a
            // complete, correct invocation.
            if (i + 1 < argc) {
                config.scenarioName = argv[++i];
                config.graphicsEnabled = false;
                config.operatingMode = mode::OperatingMode::Automated;
            }
        } else if (arg == "--timeout") {
            if (i + 1 < argc) {
                config.scenarioTimeoutSeconds = std::atof(argv[++i]);
            }
        } else if (arg == "--scenario-hold") {
            if (i + 1 < argc) {
                config.scenarioHoldSeconds = std::atof(argv[++i]);
            }
        } else if (arg == "--scenario-spell") {
            if (i + 1 < argc) {
                config.scenarioSpellId =
                    static_cast<unsigned int>(std::atoi(argv[++i]));
            }
        } else if (arg == "--scenario-silent-zone") {
            // B1079 (#399): zone_connect_timeout's "zone answered then went
            // silent" arm (the 2026-10-15 XU4 shape). Boolean, no value.
            config.scenarioSilentZone = true;
        } else if (arg == "--scenario-expect-material") {
            if (i + 1 < argc) {
                config.scenarioExpectMaterial = std::atoll(argv[++i]);
            }
        } else if (arg == "--scenario-cast-target") {
            if (i + 1 < argc) {
                config.scenarioCastTarget = argv[++i];
            }
        } else if (arg == "--scenario-success-spell") {
            if (i + 1 < argc) {
                config.scenarioSuccessSpellId =
                    static_cast<unsigned int>(std::atoi(argv[++i]));
            }
        } else if (arg == "--scenario-recast-stop-spell") {
            if (i + 1 < argc) {
                config.scenarioRecastStopSpellId =
                    static_cast<unsigned int>(std::atoi(argv[++i]));
            }
        } else if (arg == "--scenario-buff-notake-spell") {
            if (i + 1 < argc) {
                config.scenarioBuffNotakeSpellId =
                    static_cast<unsigned int>(std::atoi(argv[++i]));
            }
        } else if (arg == "--scenario-fizzle-spell") {
            if (i + 1 < argc) {
                config.scenarioFizzleSpellId =
                    static_cast<unsigned int>(std::atoi(argv[++i]));
            }
        } else if (arg == "--scenario-zone-id") {
            if (i + 1 < argc) {
                config.scenarioZoneId =
                    static_cast<unsigned int>(std::atoi(argv[++i]));
            }
        } else if (arg == "--scenario-zone-name") {
            if (i + 1 < argc) {
                config.scenarioZoneName = argv[++i];
            }
        } else if (arg == "--script") {
            if (i + 1 < argc) {
                config.scriptPath = argv[++i];
            }
        } else if (arg == "--dump-frames-dir") {
            if (i + 1 < argc) {
                config.dumpFramesDir = argv[++i];
            }
        } else if (arg == "--dump-every") {
            if (i + 1 < argc) {
                config.dumpFramesEvery = std::atoi(argv[++i]);
            }
        } else if (arg == "--dump-max-frames") {
            if (i + 1 < argc) {
                config.dumpFramesMaxCount = std::atoi(argv[++i]);
            }
        } else if (arg == "--pose-log") {
            // B874 audit: order-independent. This used to RESET every/max to
            // 1/600, so `--pose-every 5 --pose-log f` silently logged every
            // frame; the defaults are applied after the loop instead.
            if (i + 1 < argc) {
                config.poseLogPath = argv[++i];
            }
            if (config.poseLogPath.empty()) {
                std::cerr << "--pose-log needs a file path\n";
                config.parseError = true;
            }
        } else if (arg == "--pose-every" || arg == "--pose-max-frames") {
            // A pose-log knob is a positive integer or a fail-fast error:
            // atoi()'s 0 for "0", "" or "abc" used to disable the writer
            // without a word (poseLogEvery 0 = off).
            int* target = (arg == "--pose-every") ? &poseEveryArg : &poseMaxArg;
            const char* value = (i + 1 < argc) ? argv[++i] : "";
            char* end = nullptr;
            const long n = std::strtol(value, &end, 10);
            if (*value == '\0' || end == nullptr || *end != '\0' || n < 1 || n > 1000000000L) {
                std::cerr << arg << " needs a positive integer, got '" << value << "'\n";
                config.parseError = true;
            } else {
                *target = static_cast<int>(n);
            }
        } else if (arg == "--help" || arg == "-h") {
            config.showHelp = true;
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  -d, --debug <level>      Set debug level (0-6)\n";
            std::cout << "  -c, --config <file>      Set config file (default: willeq.json)\n";
            std::cout << "  -np, --no-pathfinding    Disable navmesh pathfinding\n";
            std::cout << "  --record-session <file>  Record decoded session packets to <file> for later replay\n";
            std::cout << "  --script <file>          Run a scripted command tour ('wait N' + slash-command lines)\n";
            std::cout << "  --create-character \"Name,Race,Gender,Class,Deity,City[,STR=n,...][,FACE=n,HAIR=n,HAIRCOLOR=n,EYES=n,BEARD=n,BEARDCOLOR=n]\"\n"
                         "                           Create the character at character select, then enter world with it (home city)\n";
            std::cout << "  --delete-character <Name>  Delete the character at character select, then exit\n";
            std::cout << "  --list-characters        Print the account's characters at character select, then exit\n";
            std::cout << "  --character-select       Show the character-select screen even with a configured character\n"
                         "                           (graphics only; it is shown anyway when no character is configured)\n";
            std::cout << "  --scenario <name>        Run a live scenario headless and exit 0 pass / 1 fail / 2 timeout / 3 could-not-start\n";
            std::cout << "  --timeout <s>            --scenario: overall wall-clock budget (default 300)\n";
            std::cout << "  --scenario-hold <s>      --scenario two_client_survive: how long the pair must hold (default 300)\n";
            std::cout << "  --scenario-spell <id>    --scenario cast_lands: beneficial spell to memorise and cast (default 288, Minor Shielding)\n";
            std::cout << "  --scenario-silent-zone   --scenario zone_connect_timeout: the 'zone answered then went silent' arm (B1079)\n";
            std::cout << "  --scenario-expect-material <n>  --scenario equip_actor_return: assert our OWN primary material is <n> (T498)\n";
            std::cout << "  --scenario-cast-target <name>   --scenario cast_interrupt*: the character to target (B630 two-client lane)\n";
            std::cout << "  --scenario-success-spell <id>   --scenario cast_interrupt_chain: D1 nuke to let finish (default 372)\n";
            std::cout << "  --scenario-recast-stop-spell <id>  D2 recast-stop spell (default 293 Haze)\n";
            std::cout << "  --scenario-buff-notake-spell <id>  D3: the caster's own beneficial buff (default 293 Haze)\n";
            std::cout << "  --scenario-fizzle-spell <id>    --scenario cast_fizzle: spell the non-GM caster rolls to fizzle (0=refuse)\n";
            std::cout << "  --fpcam <name=value,...>  Override first-person camera tunables at startup (repeatable; same names as /fpcam)\n";
            std::cout << "  --dump-frames-dir <dir>  Video-capture evidence tool: write numbered PNG frames to <dir> (needs --dump-every)\n";
            std::cout << "  --dump-every <N>         Video-capture: dump every Nth rendered frame\n";
            std::cout << "  --dump-max-frames <N>    Video-capture: stop after N frames written (default 600)\n";
            std::cout << "  --pose-log <file>        Pose-logger: write exact-float camera/player pose to 14-column TSV (every frame default;\n";
            std::cout << "                           needs graphics; an unwritable <file> is a startup error)\n";
            std::cout << "  --pose-every <N>         Pose-logger: write one row every Nth rendered frame (N >= 1, default 1; needs --pose-log)\n";
            std::cout << "  --pose-max-frames <N>    Pose-logger: stop after N rows written (N >= 1, default 600; needs --pose-log)\n";
#ifdef EQT_HAS_GRAPHICS
            std::cout << "  -ng, --no-graphics       Disable graphical rendering\n";
            std::cout << "  -r, --resolution <W> <H> Set graphics resolution (default: 800 600)\n";
            std::cout << "  --renderer <backend>     Rendering backend: gles2 (only option; software/opengl were removed, docs/decisions.md 2026-07-28)\n";
            std::cout << "  --constrained <preset|NxNxN>  Rendering preset (default: orangepi; voodoo1, voodoo2, tnt, orangepi, or 128x32x4)\n";
            std::cout << "  --eqe1-path <dir>        Directory containing .eqe1 ETC1 texture files\n";
            std::cout << "  --composite-entities <dir>  Directory containing composite entity texture sheets\n";
            std::cout << "  --drm                    Use DRM/KMS display (no X11 required)\n";
            std::cout << "  --egl-x11                Use X11-windowed EGL surface (x86 GLES2 dev builds; opt-in, never auto-detected)\n";
            std::cout << "  --threads <N>            Background thread pool size (default: from preset)\n";
            std::cout << "  --zone-load <mode>       Zone loading mode: manual (all at load) or automatic (progressive)\n";
            std::cout << "  --frame-timing, --ft     Enable frame timing profiler (logs every ~2s)\n";
            std::cout << "  --scene-profile, --sp    Run scene breakdown profiler after zone load\n";
#ifdef WITH_RDP
            std::cout << "  --rdp, --enable-rdp      Enable native RDP server for remote access\n";
            std::cout << "  --rdp-port <port>        RDP server port (default: 3389)\n";
#endif
#endif
#ifdef WITH_AUDIO
            std::cout << "  -na, --no-audio          Disable audio\n";
            std::cout << "       --no-3d-audio       Disable 3D positional audio (sounds play centred)\n";
            std::cout << "  --audio-volume <0-100>   Master volume (default: 100)\n";
            std::cout << "  --music-volume <0-100>   Music volume (default: 50)\n";
            std::cout << "  --effects-volume <0-100> Sound effects volume (default: 100)\n";
            std::cout << "  --soundfont <path>       Path to SoundFont for MIDI playback\n";
            std::cout << "  --audio-rate <hz>        Audio device sample rate (default: 48000, 0 = native)\n";
            std::cout << "  --audio-format <fmt>     Audio device format: s16 (default), f32, s24, s32, native\n";
#endif
            std::cout << "  --headless               Run in headless interactive mode\n";
            std::cout << "  --automated              Run in automated/bot mode\n";
            std::cout << "  --log-level=LEVEL        Set log level (NONE, FATAL, ERROR, WARN, INFO, DEBUG, TRACE)\n";
            std::cout << "                           ('--log-level LEVEL' is accepted too, B1079); an unparseable level is a loud ERROR\n";
            std::cout << "                           If both --log-level= and -d <n> are given, --log-level=\n";
            std::cout << "                           wins (a one-line notice is logged when they disagree)\n";
            std::cout << "  --log-file=PATH          Also write the log to PATH, line-flushed (B1079; env WILLEQ_LOG_FILE)\n";
            std::cout << "                           -- the play-box log: survives a kill mid-zone-in, makes the play.sh tee unnecessary\n";
            std::cout << "  --load-mem-audit         Log [LOAD-MEM-AUDIT] memory lines after each zone-load step and the\n";
            std::cout << "                           [memledger] lifecycle points (needs --log-level=INFO or higher)\n";
            std::cout << "                           Env: WILLEQ_BASELINE=1 (original-client feature parity), WILLEQ_LEDGER_TRACE=<category>[:<point>],\n";
            std::cout << "                           WILLEQ_LEDGER_IDLE_EVERY=<s>, WILLEQ_LEDGER_OWNERS=<category>\n";
            std::cout << "                           WILLEQ_FPS_CAP=<fps> (B924: pace the render loop, measurement only),\n";
            std::cout << "                           WILLEQ_IDLE_MEMMAP=1 (B924: the smaps census at every idle point)\n";
            std::cout << "  --log-module=MOD:LEVEL   Set per-module log level; comma-separated list (e.g., NET:DEBUG,GRAPHICS:TRACE)\n";
            std::cout << "                           Unparseable level names are rejected with an error (never mapped silently)\n";
            std::cout << "                           Modules: NET, NET_PACKET, LOGIN, WORLD, ZONE, ENTITY,\n";
            std::cout << "                                    MOVEMENT, COMBAT, INVENTORY, GRAPHICS, GRAPHICS_LOAD,\n";
            std::cout << "                                    CAMERA, INPUT, AUDIO, PATHFIND, MAP, UI, CONFIG, MAIN\n";
#ifndef _WIN32
            std::cout << "  Signal SIGUSR1           Increase log level at runtime\n";
            std::cout << "  Signal SIGUSR2           Decrease log level at runtime\n";
#endif
            std::cout << "  -h, --help               Show this help message\n";
        }
    }

    // B874 audit: resolve the pose-log knobs, and refuse every combination
    // that would run a capture which silently writes nothing.
    if (!config.showHelp) {
        if (config.poseLogPath.empty()) {
            if (poseEveryArg > 0 || poseMaxArg > 0) {
                std::cerr << "--pose-every/--pose-max-frames have no effect without --pose-log <file>\n";
                config.parseError = true;
            }
        } else {
            config.poseLogEvery = poseEveryArg > 0 ? poseEveryArg : 1;  // every frame default
            if (poseMaxArg > 0) config.poseLogMaxCount = poseMaxArg;
#ifdef EQT_HAS_GRAPHICS
            const bool renders = config.graphicsEnabled;
#else
            const bool renders = false;
#endif
            if (!renders) {
                std::cerr << "--pose-log needs the renderer: it writes one row per RENDERED frame, and "
                             "this run has none (--no-graphics/--headless/--automated/--scenario, or a "
                             "build without graphics)\n";
                config.parseError = true;
            }
        }
    }

    return config;
}

bool Application::preflightPoseLog(const ApplicationConfig& config, std::string& err) {
    if (config.poseLogPath.empty()) return true;
    EQT::Graphics::PoseLogWriter probe;
    // The same open the render loop makes; the probe's destructor closes it,
    // leaving a header-only file the first gameplay frame truncates again.
    return probe.open(config.poseLogPath, config.poseLogEvery, config.poseLogMaxCount, &err);
}

bool Application::loadConfigFile(const std::string& configFile, ApplicationConfig& config) {
    try {
        EQT::PerformanceMetrics::instance().startTimer("Config Loading", EQT::MetricCategory::Startup);

        // T399 (board B235): willeq.json itself is CWD-relative by default
        // ("willeq.json", include/client/application.h). Resolving it lets a
        // client started outside the repo root find the checked-out config.
        auto jsonConfig = EQ::JsonConfigFile::Load(eqt::ResolveDataPath(configFile));
        auto handle = jsonConfig.RawHandle();

        // Handle both array format (legacy) and object format
        Json::Value clientConfig;
        if (handle.isArray() && handle.size() > 0) {
            clientConfig = handle[0];

#ifdef EQT_HAS_GRAPHICS
            // Load hotkey settings for legacy config format
            {
                auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
                hotkeyMgr.resetToDefaults();
                // B433: the Options Keys page user copy wins over the shipped file.
                hotkeyMgr.loadFromFile(eqt::input::HotkeyManager::startupBindingsPath());
            }
#endif
        } else if (handle.isObject()) {
            if (handle.isMember("clients") && handle["clients"].isArray() && handle["clients"].size() > 0) {
                clientConfig = handle["clients"][0];
            } else {
                clientConfig = handle;
            }

            // Parse logging configuration from object format
            InitLoggingFromJson(handle);

#ifdef EQT_HAS_GRAPHICS
            // Load hotkey settings
            {
                auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
                hotkeyMgr.resetToDefaults();
                // B433: the Options Keys page user copy wins over the shipped file.
                hotkeyMgr.loadFromFile(eqt::input::HotkeyManager::startupBindingsPath());
                if (handle.isMember("hotkeys")) {
                    LOG_INFO(MOD_INPUT, "Applying hotkey overrides from main config");
                    hotkeyMgr.applyOverrides(handle["hotkeys"]);
                }
            }

            // Parse rendering config
            if (handle.isMember("rendering")) {
                const auto& rendering = handle["rendering"];
                if (rendering.isMember("constrained_mode") && config.constrainedPreset.empty()) {
                    config.constrainedPreset = rendering["constrained_mode"].asString();
                    LOG_INFO(MOD_GRAPHICS, "Constrained rendering mode from config: {}", config.constrainedPreset);
                }
            }

#ifdef WITH_RDP
            // Parse RDP config
            if (handle.isMember("rdp")) {
                const auto& rdp_config = handle["rdp"];
                if (rdp_config.isMember("enabled") && rdp_config["enabled"].asBool()) {
                    config.rdpEnabled = true;
                    LOG_INFO(MOD_GRAPHICS, "RDP server enabled from config");
                }
                if (rdp_config.isMember("port")) {
                    config.rdpPort = static_cast<uint16_t>(rdp_config["port"].asInt());
                    LOG_INFO(MOD_GRAPHICS, "RDP port from config: {}", config.rdpPort);
                }
            }
#endif
#endif

#ifdef WITH_AUDIO
            // Parse audio config
            if (handle.isMember("audio")) {
                const auto& audio_config = handle["audio"];
                if (audio_config.isMember("enabled")) {
                    config.audioEnabled = audio_config["enabled"].asBool();
                    LOG_INFO(MOD_AUDIO, "Audio {} from config", config.audioEnabled ? "enabled" : "disabled");
                }
                if (audio_config.isMember("master_volume")) {
                    int vol = audio_config["master_volume"].asInt();
                    config.audioMasterVolume = std::clamp(vol, 0, 100) / 100.0f;
                    LOG_INFO(MOD_AUDIO, "Master volume from config: {}%", vol);
                }
                if (audio_config.isMember("music_volume")) {
                    int vol = audio_config["music_volume"].asInt();
                    config.audioMusicVolume = std::clamp(vol, 0, 100) / 100.0f;
                    LOG_INFO(MOD_AUDIO, "Music volume from config: {}%", vol);
                }
                if (audio_config.isMember("effects_volume")) {
                    int vol = audio_config["effects_volume"].asInt();
                    config.audioEffectsVolume = std::clamp(vol, 0, 100) / 100.0f;
                    LOG_INFO(MOD_AUDIO, "Effects volume from config: {}%", vol);
                }
                if (audio_config.isMember("soundfont")) {
                    config.audioSoundfont = audio_config["soundfont"].asString();
                    LOG_INFO(MOD_AUDIO, "SoundFont from config: {}", config.audioSoundfont);
                }
                if (audio_config.isMember("vendor_music")) {
                    config.audioVendorMusic = audio_config["vendor_music"].asString();
                    // B1018 (GitLab #339): the "parsed but ignored" WARN lives
                    // HERE -- this arm runs only when a config actually named
                    // the key. B344 put it in SetVendorMusic()'s body, which
                    // Application::initEverQuestClient() calls unconditionally
                    // with the default "gl.xmi", so it fired on every
                    // WITH_AUDIO run. No config sets the key, hence nobody
                    // noticed. Pinned by test_vendor_music_config_warn (0
                    // absent / exactly 1 present).
                    LOG_WARN(MOD_AUDIO, "audio.vendor_music is parsed but ignored: {}",
                             config.audioVendorMusic);
                }
                // K15 (board B037)
                if (audio_config.isMember("use_3d")) {
                    config.audioUse3D = audio_config["use_3d"].asBool();
                    LOG_INFO(MOD_AUDIO, "3D positional audio {} from config",
                             config.audioUse3D ? "enabled" : "disabled");
                }
                // B1069 (#389): the playback DEVICE rate. --audio-rate wins.
                if (audio_config.isMember("device_sample_rate") &&
                    !config.audioDeviceSampleRateFromCli) {
                    const Json::Int64 hz = audio_config["device_sample_rate"].asInt64();
                    config.audioDeviceSampleRate = static_cast<uint32_t>(
                        std::clamp<Json::Int64>(hz, 0, 384000));
                    LOG_INFO(MOD_AUDIO, "Audio device sample rate from config: {}Hz",
                             config.audioDeviceSampleRate);
                }
                // B1069: the playback DEVICE format. --audio-format wins.
                if (audio_config.isMember("device_format") && !config.audioDeviceFormatFromCli) {
                    const std::string fmt = audio_config["device_format"].asString();
                    if (EQT::Audio::parseDeviceFormat(fmt, config.audioDeviceFormat)) {
                        LOG_INFO(MOD_AUDIO, "Audio device format from config: {}", fmt);
                    } else {
                        LOG_WARN(MOD_AUDIO,
                                 "audio.device_format '{}' unknown (s16, f32, s24, s32, native); keeping {}",
                                 fmt, EQT::Audio::deviceFormatName(config.audioDeviceFormat));
                    }
                }
            }
#endif

            // Parse mode settings from top-level config
            if (handle.isMember("mode")) {
                config.operatingMode = mode::parseModeString(handle["mode"].asString());
            }

            if (handle.isMember("renderer") && handle["renderer"].isObject()) {
                auto& renderer = handle["renderer"];
                if (renderer.isMember("width")) {
                    config.displayWidth = renderer["width"].asInt();
                }
                if (renderer.isMember("height")) {
                    config.displayHeight = renderer["height"].asInt();
                }
                if (renderer.isMember("fullscreen")) {
                    config.fullscreen = renderer["fullscreen"].asBool();
                }
            }
        } else {
            LOG_ERROR(MOD_CONFIG, "Invalid config file format");
            EQT::PerformanceMetrics::instance().stopTimer("Config Loading");
            return false;
        }

        // Parse connection settings
        if (clientConfig.isMember("host")) {
            config.host = clientConfig["host"].asString();
        }
        if (clientConfig.isMember("port")) {
            config.port = clientConfig["port"].asInt();
        }
        if (clientConfig.isMember("user")) {
            config.user = clientConfig["user"].asString();
        }
        if (clientConfig.isMember("pass")) {
            config.pass = clientConfig["pass"].asString();
        }
        if (clientConfig.isMember("server")) {
            config.server = clientConfig["server"].asString();
        }
        if (clientConfig.isMember("character")) {
            config.character = clientConfig["character"].asString();
        }

        // Parse path settings
        if (clientConfig.isMember("navmesh_path")) {
            config.navmeshPath = clientConfig["navmesh_path"].asString();
        }
        if (clientConfig.isMember("maps_path")) {
            config.mapsPath = clientConfig["maps_path"].asString();
        }
        if (clientConfig.isMember("eq_client_path")) {
            config.eqClientPath = clientConfig["eq_client_path"].asString();
        }
        if (clientConfig.isMember("region_maps_path")) {
            config.regionMapsPath = clientConfig["region_maps_path"].asString();
        }

        config.configFile = configFile;
        EQT::PerformanceMetrics::instance().stopTimer("Config Loading");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR(MOD_CONFIG, "Failed to load config file '{}': {}", configFile, e.what());
        EQT::PerformanceMetrics::instance().stopTimer("Config Loading");
        return false;
    }
}

} // namespace eqt
