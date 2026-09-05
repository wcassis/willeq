#include "client/application.h"
#include <json/json.h>  // Must be before eq.h → logging.h so JSONCPP_VERSION_STRING is defined
#include "client/eq.h"
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
#include "client/graphics/irrlicht_renderer.h"
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

// ========== Signal Handlers ==========

// P36: SIGTERM previously had no handler at all -- default disposition
// terminates the process immediately (exit via signal, no shutdown()
// call, no clean exit code), which is one of the "shutdown aborts"
// exit-matrix rows this unit is required to make clean. A signal handler
// must stay async-signal-safe, so it does only the one thing that's
// actually safe: set an atomic flag. mainLoop() (the always-running main
// thread loop, graphics or headless) polls the flag once per iteration
// and calls requestQuit() from ordinary code -- reusing the exact same
// controlled-shutdown path /camp and window-close already use, so it
// gets the same ordering guarantees (and the same self-join fix above)
// for free.
static std::atomic<bool> s_terminateRequested{false};

static void HandleSigTerm(int /*sig*/) {
    s_terminateRequested.store(true, std::memory_order_relaxed);
}

#ifndef _WIN32
static void HandleSigUsr1(int /*sig*/) {
    LogLevelIncrease();
}

static void HandleSigUsr2(int /*sig*/) {
    LogLevelDecrease();
}

#ifdef EQT_HAS_GRAPHICS
static void HandleSigHup(int /*sig*/) {
    eqt::input::HotkeyManager::instance().reload();
}
#endif
#endif

// ========== Application ==========

Application::Application() = default;

Application::~Application() {
    shutdown();
}

bool Application::initialize(const ApplicationConfig& config) {
    m_config = config;

    LOG_INFO(MOD_MAIN, "Initializing application...");

    // G04: the ordered list. See the step banner below -- the order is
    // load-bearing and pinned by tests/test_application_init_order.cpp.
    if (!initFpCamOverrides(config)) return false;
    if (!initConfigValidation(config)) return false;
    if (!initFrameDumpDirectory(config)) return false;
    if (!initRequiredFileValidation(config)) return false;
    if (!initSignalHandlers(config)) return false;
    if (!initEverQuestClient(config)) return false;
    if (!initSpellSystem(config)) return false;
    if (!initActionLayer(config)) return false;
    if (!initScriptedCommandTour(config)) return false;
    if (!initGameMode(config)) return false;
    if (!initInputBridge(config)) return false;
    if (!initCommandOutputRouting(config)) return false;
    if (!initGraphics(config)) return false;
    if (!initConsoleBridge(config)) return false;
    if (!initLoginConnection(config)) return false;

    LOG_INFO(MOD_MAIN, "Application initialized successfully");
    return true;
}

// ========== G04: Application::initialize() subsystem steps ==========
//
// Each function below is a VERBATIM slice of the pre-G04 initialize() body,
// cut at the banners the file already carried; every early `return false`
// stayed inside the step it belonged to. initialize() is now the ordered
// list of those steps, and THE ORDER IS LOAD-BEARING: the EverQuest client
// must exist before the spell system, the action layer, the game mode and
// the input bridge that all take m_eqClient->GetGameState(); the renderer
// and bridge must exist before InitGraphics(); the console bridge only
// attaches when the graphics step did not; and the login connection is
// started LAST, after graphics init, so a slow DRM/EGL bring-up cannot time
// the login server out. tests/test_application_init_order.cpp pins the
// resulting statement order against a golden extracted from the pre-G04
// source (tests/goldens/application_init_order_g04.txt).

bool Application::initFpCamOverrides(const ApplicationConfig& config) {
    // T310 Leg A: apply --fpcam overrides BEFORE anything reads a camera
    // number. A bad name or value is reported and the run continues with that
    // parameter at its default -- a sweep that silently rendered the default
    // would produce a flat, meaningless difference curve, so this is loud.
    for (const std::string& spec : config.fpCamOverrides) {
        std::string err;
        if (EQT::Graphics::fpTunableApplyAssignmentList(spec, err)) {
            LOG_INFO(MOD_MAIN, "[FPCAM] --fpcam applied: {}", spec);
        } else {
            LOG_ERROR(MOD_MAIN, "[FPCAM] --fpcam '{}' REJECTED: {} (that parameter keeps its default)",
                      spec, err);
        }
    }
    return true;
}

bool Application::initConfigValidation(const ApplicationConfig& config) {
    // S01: Mandatory config validation — fail fast on missing settings
    {
        std::string errors;
        if (config.host.empty()) errors += "  - host: must not be empty\n";
        if (config.port < 1 || config.port > 65535) errors += fmt::format("  - port: {} is not in range 1-65535\n", config.port);
        if (config.user.empty()) errors += "  - user: must not be empty\n";
        if (config.pass.empty()) errors += "  - pass: must not be empty\n";
        if (config.server.empty()) errors += "  - server: must not be empty\n";
        if (config.character.empty()) errors += "  - character: must not be empty\n";

#ifdef EQT_HAS_GRAPHICS
        if (config.graphicsEnabled) {
            if (config.eqClientPath.empty()) {
                errors += "  - eqClientPath: must not be empty when graphics are enabled\n";
            } else {
                struct stat st;
                if (stat(config.eqClientPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                    errors += fmt::format("  - eqClientPath: '{}' is not a valid directory\n", config.eqClientPath);
                }
            }
        }
#endif

        if (!errors.empty()) {
            LOG_FATAL(MOD_MAIN, "Invalid configuration:\n{}", errors);
            return false;
        }

        if (config.constrainedPreset.empty()) {
            LOG_INFO(MOD_MAIN, "No constrained preset specified, defaulting to 'orangepi'");
        }
    }
    LOG_DEBUG(MOD_MAIN, "S01: Config validation passed");
    return true;
}

bool Application::initFrameDumpDirectory(const ApplicationConfig& /*config*/) {
    // T64: video-capture evidence tool (--dump-frames-dir) — ensure the
    // output directory exists up front so render()'s per-frame
    // saveScreenshot() calls don't silently fail. Additive, no-op when the
    // flag isn't passed.
    if (!m_config.dumpFramesDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(m_config.dumpFramesDir, ec);
        if (ec) {
            LOG_ERROR(MOD_MAIN, "--dump-frames-dir: failed to create '{}': {}",
                m_config.dumpFramesDir, ec.message());
        } else {
            LOG_INFO(MOD_MAIN, "--dump-frames-dir: writing frames to '{}' every {} frame(s), capped at {}",
                m_config.dumpFramesDir, m_config.dumpFramesEvery, m_config.dumpFramesMaxCount);
        }
    }
    return true;
}

bool Application::initRequiredFileValidation(const ApplicationConfig& config) {
    // S03: Global file validation — verify all required files exist before proceeding
#ifdef EQT_HAS_GRAPHICS
    if (config.graphicsEnabled) {
        std::string missing;
        auto checkFile = [&missing](const std::string& path, const char* what) {
            struct stat st;
            if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                missing += fmt::format("  - {}: {}\n", what, path);
            }
        };
        checkFile("config/constrained_presets.json", "constrained presets config");
        checkFile("config/race_models.json", "race model mappings");
        checkFile("data/item_models.json", "item model mappings");
        checkFile(config.eqClientPath + "/eqstr_us.txt", "EQ string database");
        checkFile(config.eqClientPath + "/dbstr_us.txt", "EQ database strings");
        checkFile(config.eqClientPath + "/spells_us.txt", "spell database");

        if (!missing.empty()) {
            LOG_FATAL(MOD_MAIN, "Required files missing:\n{}", missing);
            return false;
        }
        LOG_DEBUG(MOD_MAIN, "S03: All required files validated");
    }
#endif

    LOG_INFO(MOD_MAIN, "Config: host={}, server={}, character={}",
        config.host, config.server, config.character);
    return true;
}

bool Application::initSignalHandlers(const ApplicationConfig& /*config*/) {
    // Register signal handlers
#ifndef _WIN32
    signal(SIGUSR1, HandleSigUsr1);
    signal(SIGUSR2, HandleSigUsr2);
    signal(SIGTERM, HandleSigTerm);
#ifdef EQT_HAS_GRAPHICS
    signal(SIGHUP, HandleSigHup);
#endif
    LOG_DEBUG(MOD_MAIN, "Signal handlers registered (SIGUSR1=log+, SIGUSR2=log-, SIGTERM=clean shutdown, SIGHUP=hotkey reload)");
#endif
    return true;
}

bool Application::initEverQuestClient(const ApplicationConfig& config) {
    // T227: there is ONE state::GameState per run -- EverQuest::m_game_state,
    // created with the client below. Every holder after it takes
    // m_eqClient->GetGameState(); the projected copy Application used to own
    // here (and GameStateSyncBridge's projection switch that fed it) is gone.

    // Create EverQuest client
    LOG_DEBUG(MOD_MAIN, "Creating EverQuest client...");
    EQT::PerformanceMetrics::instance().startTimer("Client Creation", EQT::MetricCategory::Startup);
    try {
        m_eqClient = std::make_unique<EverQuest>(
            config.host, config.port,
            config.user, config.pass,
            config.server, config.character
        );

        // Configure EverQuest client
        m_eqClient->Movement().SetPathfinding(config.pathfindingEnabled);
        if (!config.navmeshPath.empty()) {
            m_eqClient->Zone().SetNavmeshPath(config.navmeshPath);
        }
        if (!config.mapsPath.empty()) {
            m_eqClient->Zone().SetMapsPath(config.mapsPath);
        }

        // Open WilleqDatabase before any data lookups (SetEQClientPath loads strings)
        {
            std::string dbPath = "data/willeq.db";
            m_eqClient->OpenDatabase(dbPath);
        }

        // R01: Session recording (--record-session <file>)
        if (!config.recordSessionPath.empty()) {
            m_eqClient->StartSessionRecording(config.recordSessionPath);
        }

        // T42: was #ifdef EQT_HAS_GRAPHICS. SetEQClientPath/SetRegionMapsPath/
        // SetConfigPath are all plain inline setters (eq.h, "un-guarded" per
        // P28's own comment at SetEQClientPath's definition in eq.cpp) with no
        // renderer dependency -- but this call site never caught up, so a true
        // EQT_GRAPHICS=OFF build never set m_eq_client_path at all. Found live:
        // LoadZoneSources() (eq_zone.cpp, itself unconditional -- it builds the
        // BSP tree game state needs for water detection, independent of
        // graphics) treats an empty eq_client_path as fatal and calls
        // RequestQuit(), so every EQT_GRAPHICS=OFF zone-in against a real
        // server self-terminated within ~2s of receiving OP_NewZone, before
        // this unit's fix could even be exercised live. Same declassification
        // family as P28/T29/T41's m_inventory_manager fix.
        if (!config.eqClientPath.empty()) {
            m_eqClient->SetEQClientPath(config.eqClientPath);
        }
        if (!config.regionMapsPath.empty()) {
            m_eqClient->Bridge().SetRegionMapsPath(config.regionMapsPath);
        }
        m_eqClient->Bridge().SetConfigPath(config.configFile);

#ifdef WITH_AUDIO
        // Apply audio settings
        m_eqClient->Bridge().SetAudioEnabled(config.audioEnabled);
        m_eqClient->Bridge().SetMasterVolume(config.audioMasterVolume);
        m_eqClient->Bridge().SetMusicVolume(config.audioMusicVolume);
        m_eqClient->Bridge().SetEffectsVolume(config.audioEffectsVolume);
        if (!config.audioSoundfont.empty()) {
            m_eqClient->Bridge().SetSoundFont(config.audioSoundfont);
        }
        m_eqClient->Bridge().SetVendorMusic(config.audioVendorMusic);
        m_eqClient->Bridge().SetUse3DAudio(config.audioUse3D);
#endif

        LOG_DEBUG(MOD_MAIN, "EverQuest client created");

    } catch (const std::exception& e) {
        LOG_FATAL(MOD_MAIN, "Failed to create EverQuest client: {}", e.what());
        EQT::PerformanceMetrics::instance().stopTimer("Client Creation");
        return false;
    }
    EQT::PerformanceMetrics::instance().stopTimer("Client Creation");
    return true;
}

bool Application::initSpellSystem(const ApplicationConfig& config) {
    // S04: Initialize spell system at startup (spell DB, buff manager, spell effects, spell type processor)
    // Must happen after SetEQClientPath() and before any gameplay packets arrive.
    // Spell file was validated in S03, so failure here is FATAL.
    if (!config.eqClientPath.empty()) {
        LOG_DEBUG(MOD_MAIN, "S04: Initializing spell system from '{}'...", config.eqClientPath);
        if (!m_eqClient->InitializeSpellSystem(config.eqClientPath)) {
            LOG_FATAL(MOD_SPELL, "Failed to load spell database from '{}'", config.eqClientPath);
            return false;
        }
    }
    return true;
}

bool Application::initActionLayer(const ApplicationConfig& /*config*/) {
    // Create action handler adapter
    LOG_TRACE(MOD_MAIN, "Allocating EqActionHandler...");
    m_actionHandler = std::make_unique<EqActionHandler>(*m_eqClient);
    LOG_DEBUG(MOD_MAIN, "Action handler created");

    // Create action dispatcher
    LOG_TRACE(MOD_MAIN, "Allocating ActionDispatcher...");
    m_dispatcher = std::make_unique<action::ActionDispatcher>(m_eqClient->GetGameState());
    m_dispatcher->setActionHandler(m_actionHandler.get());
    LOG_DEBUG(MOD_MAIN, "Action dispatcher created");

    // Create command processor
    LOG_TRACE(MOD_MAIN, "Allocating CommandProcessor...");
    m_commandProcessor = std::make_unique<action::CommandProcessor>(m_eqClient->GetGameState(), *m_dispatcher);
    LOG_DEBUG(MOD_MAIN, "Command processor created");
    return true;
}

bool Application::initScriptedCommandTour(const ApplicationConfig& config) {
    // H03: Scripted command tour (--script <file>)
    if (!config.scriptPath.empty()) {
        if (loadScriptFile(config.scriptPath)) {
            LOG_INFO(MOD_MAIN, "Script loaded: {} ({} steps)", config.scriptPath, m_scriptSteps.size());
        } else {
            LOG_ERROR(MOD_MAIN, "Failed to load script file: {}", config.scriptPath);
        }
    }
    return true;
}

bool Application::initGameMode(const ApplicationConfig& config) {
    // Create game mode
    mode::ModeConfig modeConfig;
    modeConfig.width = config.displayWidth;
    modeConfig.height = config.displayHeight;
    modeConfig.fullscreen = config.fullscreen;
    modeConfig.eqClientPath = config.eqClientPath;
    modeConfig.regionMapsPath = config.regionMapsPath;

    // Game mode created with Software initially; updated to GPU after constrained config resolves.
    // P32: --renderer only ever requests gles2 now (software/opengl removed).
    auto rendererType = mode::GraphicalRendererType::IrrlichtSoftware;
    if (config.rendererGles2Requested) {
        rendererType = mode::GraphicalRendererType::IrrlichtGPU;
    }
    LOG_INFO(MOD_MAIN, "Creating game mode with renderer type: {}",
        rendererType == mode::GraphicalRendererType::IrrlichtGPU ? "GPU" : "Software");
    LOG_TRACE(MOD_MAIN, "Allocating game mode...");
    m_gameMode = mode::createMode(config.operatingMode, rendererType);
    if (!m_gameMode) {
        LOG_FATAL(MOD_MAIN, "Failed to create game mode");
        return false;
    }

    LOG_TRACE(MOD_MAIN, "Initializing game mode...");
    if (!m_gameMode->initialize(m_eqClient->GetGameState(), modeConfig)) {
        LOG_FATAL(MOD_MAIN, "Failed to initialize game mode");
        return false;
    }
    LOG_DEBUG(MOD_MAIN, "Game mode initialized: {}",
        config.operatingMode == mode::OperatingMode::GraphicalInteractive ? "graphical" :
        config.operatingMode == mode::OperatingMode::HeadlessInteractive ? "headless" : "automated");
    return true;
}

bool Application::initInputBridge(const ApplicationConfig& /*config*/) {
    // Create input action bridge
    LOG_TRACE(MOD_MAIN, "Allocating InputActionBridge...");
    m_inputBridge = std::make_unique<action::InputActionBridge>(m_eqClient->GetGameState(), *m_dispatcher);
    m_inputBridge->setCommandProcessor(m_commandProcessor.get());
    // T174: let keyboard hotbar presses route by slot content type. This is
    // the one hotbar model the mouse path also reads (owned by EverQuest,
    // created in its constructor, so non-null here).
    m_inputBridge->setHotbarModel(m_eqClient->GetHotbarModel());
    LOG_DEBUG(MOD_MAIN, "Input action bridge created");

    // Connect input handler if available
    auto* inputHandler = m_gameMode->getInputHandler();
    if (inputHandler) {
        m_inputBridge->setInputHandler(inputHandler);
        LOG_DEBUG(MOD_MAIN, "Input handler connected to bridge");

        // T29: this is the ONE stdin reader for the process's whole
        // lifetime (see console_input_handler.cpp) -- setInputHandler()
        // above may later be swapped to GraphicsInputHandler once graphics
        // initializes (see the "Create GraphicsInputHandler" block below),
        // but that only redirects movement/mouse input. Wiring the SAME
        // instance here too keeps its raw commands (piped slash commands)
        // reaching the app afterward, without a second stdin reader.
        m_inputBridge->setConsoleCommandSource(inputHandler);

        // T30: stash the same handler for processPreConnectionLifecycleCommands()
        // -- it needs to peek the raw-command queue directly, bypassing
        // m_inputBridge's m_fullyConnected gate (see that method's doc
        // comment in application.h).
        m_consoleInputHandler = inputHandler;
    }

    // T29: unified slash-command dispatch -- see DispatchSlashCommand()'s
    // doc comment in application.h. Wires both automation front doors
    // (console raw commands -> here) and the live chat window's fallback
    // (EverQuest::SetUnrecognizedCommandFallback -> here) to the same
    // registries-2/3 implementation.
    m_inputBridge->setUnifiedCommandDispatcher([this](const std::string& rawLine) {
        printCommandResult(DispatchSlashCommand(rawLine));
    });
    wireUnifiedCommandDispatch();
    return true;
}

bool Application::initCommandOutputRouting(const ApplicationConfig& /*config*/) {
    // Connect command processor output to renderer
    auto* renderer = m_gameMode->getRenderer();
    if (renderer) {
        m_commandProcessor->setOutputRenderer(renderer);
        LOG_DEBUG(MOD_MAIN, "Command processor connected to renderer");
    }

    // T241: connectRendererCallbacks() deleted. Nothing derives from
    // output::GraphicalRenderer, so its dynamic_cast always failed and it
    // never set a callback; every route it mirrored is a bridge intent
    // (TargetIntent, DoorInteractIntent, ChatSubmitIntent, ...).
    return true;
}

bool Application::initGraphics(const ApplicationConfig& config) {
#ifdef EQT_HAS_GRAPHICS
    // Initialize graphics if graphical mode
    m_graphicsInitialized = false;
    if (config.graphicsEnabled && config.operatingMode == mode::OperatingMode::GraphicalInteractive) {
        if (!initConstrainedRendererConfig(config)) return false;
        if (!initRendererAndGraphics(config)) return false;
    }
#endif
    return true;
}

bool Application::initConstrainedRendererConfig(const ApplicationConfig& config) {
#ifdef EQT_HAS_GRAPHICS
        // eqClientPath already validated in S01 block above
        // Build constrained config: preset → JSON overrides → CLI overrides
        {
            EQT::Graphics::ConstrainedRendererConfig builtConfig;
            // T347 (B051): `bool customMemorySpec` lived here, set true in the
            // parseMemorySpec() arm below and read NOWHERE. Both arms of that
            // if/else call loadJsonOverrides() identically afterwards, so the
            // flag distinguished nothing; the LOG_DEBUG in each arm already
            // says which path was taken. Deleted rather than read: there is no
            // downstream decision for it to make.

            if (!config.constrainedPreset.empty()) {
                if (EQT::Graphics::ConstrainedRendererConfig::parseMemorySpec(config.constrainedPreset, builtConfig)) {
                    LOG_DEBUG(MOD_GRAPHICS, "Parsed memory spec '{}': total={}MB, tex={}MB, fb={}MB",
                              config.constrainedPreset,
                              builtConfig.totalMemoryBudgetBytes / (1024*1024),
                              builtConfig.textureMemoryBytes / (1024*1024),
                              builtConfig.framebufferMemoryBytes / (1024*1024));
                    LOG_TRACE(MOD_GRAPHICS, "Loading JSON overrides for memory spec...");
                    builtConfig.loadJsonOverrides(config.constrainedPreset, "config/constrained_presets.json");
                } else {
                    auto preset = EQT::Graphics::ConstrainedRendererConfig::parsePreset(config.constrainedPreset);
                    LOG_DEBUG(MOD_GRAPHICS, "Using named preset '{}' -> {}",
                              config.constrainedPreset,
                              EQT::Graphics::ConstrainedRendererConfig::presetName(preset));
                    builtConfig = EQT::Graphics::ConstrainedRendererConfig::fromPreset(preset);
                    LOG_TRACE(MOD_GRAPHICS, "Loading JSON overrides for named preset...");
                    builtConfig.loadJsonOverrides(config.constrainedPreset, "config/constrained_presets.json");
                }
            } else {
                // Default to OrangePi preset
                LOG_DEBUG(MOD_GRAPHICS, "No preset specified, defaulting to OrangePi");
                builtConfig = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
                    EQT::Graphics::ConstrainedRenderingPreset::OrangePi);
                LOG_TRACE(MOD_GRAPHICS, "Loading JSON overrides for default preset...");
                builtConfig.loadJsonOverrides("orangepi", "config/constrained_presets.json");
            }

            // CLI override: --renderer gles2 is a no-op (GLES2 is the only
            // backend, P32) — kept for explicit-confirmation logging only.
            if (config.rendererGles2Requested) {
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: renderer backend -> {}",
                          EQT::Graphics::backendName(builtConfig.renderingBackend));
            }
            // CLI override: --drm sets DRM mode
            if (config.useDRM) {
                builtConfig.useDRM = true;
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: DRM mode enabled");
            }
            // CLI override: --egl-x11 sets the X11-windowed EGL backend (P25)
            if (config.useEglX11) {
                builtConfig.useEglX11 = true;
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: X11-windowed EGL backend enabled");
            }
            // CLI override: --eqe1-path
            if (!config.eqe1Path.empty()) {
                builtConfig.eqe1Path = config.eqe1Path;
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: eqe1 path -> '{}'", config.eqe1Path);
            }
            // CLI override: --load-mem-audit
            if (config.loadMemAudit) {
                builtConfig.loadMemAudit = true;
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: load-mem-audit enabled");
            }
            // CLI override: --texture-ledger <path> (P13)
            if (!config.textureUploadLedgerPath.empty()) {
                builtConfig.textureUploadLedgerPath = config.textureUploadLedgerPath;
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: texture-ledger -> '{}'", config.textureUploadLedgerPath);
            }
            // CLI override: --composite-entities
            if (!config.compositeEntityPath.empty()) {
                builtConfig.enableCompositeEntities = true;
                builtConfig.compositeEntityPath = config.compositeEntityPath;
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: composite entity path -> '{}'", config.compositeEntityPath);
            }
            // CLI override: --threads
            if (config.backgroundThreadCount > 0) {
                builtConfig.backgroundThreadCount = config.backgroundThreadCount;
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: background threads -> {}", config.backgroundThreadCount);
            }
            // CLI override: --zone-load
            if (config.zoneLoadMode >= 0) {
                builtConfig.deferredAssetLoading = (config.zoneLoadMode == 1);
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: deferred asset loading -> {}",
                          builtConfig.deferredAssetLoading ? "automatic" : "manual");
            }

            // P32: the EQT_HAS_GLES2-not-defined fallback (GLES2 -> OpenGL)
            // was removed along with RenderingBackend::OpenGL — a build with
            // EQT_HAS_GRAPHICS but not EQT_HAS_GLES2 is no longer a
            // supported configuration (CMakeLists.txt's mandatory-Irrlicht
            // gate requires EQT_DRM or EQT_GLES2; the ARM EQT_DRM-only Lima
            // path was retired this same unit).

            // Load debug overrides (skip* flags from config/debug.json)
            LOG_TRACE(MOD_GRAPHICS, "Loading debug overrides...");
            builtConfig.loadDebugOverrides("config/debug.json");

            // S02: Validate preset values
            {
                std::string presetErrors;
                if (!builtConfig.validate(presetErrors)) {
                    LOG_FATAL(MOD_GRAPHICS, "Invalid constrained renderer config:\n{}", presetErrors);
                    return false;
                }
            }

            // E06: Validate eqe1Path if set
            if (!builtConfig.eqe1Path.empty()) {
                if (!std::filesystem::is_directory(builtConfig.eqe1Path)) {
                    LOG_FATAL(MOD_GRAPHICS, "EQE1 path does not exist or is not a directory: '{}'", builtConfig.eqe1Path);
                    return false;
                }
                int eqe1Count = 0;
                for (const auto& entry : std::filesystem::directory_iterator(builtConfig.eqe1Path)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".eqe1")
                        eqe1Count++;
                }
                LOG_INFO(MOD_GRAPHICS, "EQE1 texture path: '{}' ({} .eqe1 files)", builtConfig.eqe1Path, eqe1Count);
            }

            // S02: Validate framebuffer budget vs requested resolution
            {
                std::string fbError;
                if (!builtConfig.validateResolution(config.displayWidth, config.displayHeight, fbError)) {
                    LOG_FATAL(MOD_GRAPHICS, "Framebuffer budget exceeded: {}", fbError);
                    return false;
                }
            }
            LOG_DEBUG(MOD_GRAPHICS, "S02: Preset and resolution validation passed");

            m_eqClient->SetConstrainedConfig(builtConfig);

            // P32: builtConfig.renderingBackend is always GLES2 now (the only
            // value); this bookkeeping write is unconditional (was previously
            // gated on "!= Software", which is now always true).
            m_config.rendererGles2Requested = true;

            LOG_INFO(MOD_GRAPHICS, "Rendering config: backend={}, DRM={}",
                     EQT::Graphics::backendName(builtConfig.renderingBackend),
                     builtConfig.useDRM ? "yes" : "no");

            // Create constrained asset cache (config-only, no driver yet)
            LOG_TRACE(MOD_GRAPHICS, "Allocating ConstrainedTextureCache ({}MB budget)...",
                      builtConfig.textureMemoryBytes / (1024 * 1024));
            m_constrainedCache = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(builtConfig);
            LOG_INFO(MOD_GRAPHICS, "Constrained cache created ({}MB budget)",
                     builtConfig.textureMemoryBytes / (1024 * 1024));
        }
#endif
    return true;
}

bool Application::initRendererAndGraphics(const ApplicationConfig& config) {
#ifdef EQT_HAS_GRAPHICS
        LOG_DEBUG(MOD_GRAPHICS, "Initializing graphics...");
        EQT::PerformanceMetrics::instance().startTimer("Graphics Init", EQT::MetricCategory::Startup);

        // D20b5: Application creates renderer + bridge, passes to EverQuest
        LOG_TRACE(MOD_GRAPHICS, "Allocating IrrlichtRenderer...");
        m_renderer = std::make_unique<EQT::Graphics::IrrlichtRenderer>();
        m_renderer->setConstrainedTextureCache(m_constrainedCache.get());
        LOG_TRACE(MOD_GRAPHICS, "Allocating IrrlichtBridge...");
        m_irrlichtBridge = std::make_unique<eqt::bridge::IrrlichtBridge>();
        m_irrlichtBridge->setRenderer(m_renderer.get());
        // T40: wrap the mode bridge in the state-sync decorator. The
        // decorator -- not m_irrlichtBridge directly -- is what the renderer
        // and EverQuest are both wired to below, so events pushed by
        // EverQuest and intents pushed by the renderer flow through the same
        // single queue (the decorator's), which the decorator then stages
        // for the render thread and forwards to m_irrlichtBridge unchanged.
        m_stateSyncBridge = std::make_unique<eqt::bridge::GameStateSyncBridge>(
            *m_irrlichtBridge);
        m_renderer->setBridge(m_stateSyncBridge.get());
        // T357 (B057): the chat box's Tab key. See Application::commandCompletions().
        m_renderer->setCommandCompletionProvider([this](const std::string& p) { return commandCompletions(p); });
        m_bridge = m_stateSyncBridge.get();
        LOG_DEBUG(MOD_GRAPHICS, "Renderer and bridge created");
        LOG_TRACE(MOD_GRAPHICS, "Entering InitGraphics({}x{})...", config.displayWidth, config.displayHeight);

        if (m_eqClient->InitGraphics(config.displayWidth, config.displayHeight,
                                      m_renderer.get(), m_bridge)) {
            m_graphicsInitialized = true;
            LOG_INFO(MOD_GRAPHICS, "Graphics initialized");

            // Set initial loading state
            auto* eqRenderer = m_renderer.get();
            if (eqRenderer) {
                eqRenderer->setLoadingTitle(L"EverQuest");
                eqRenderer->setLoadingProgress(0.0f, L"Connecting to login server...");

                // Cache font for render thread loading screen (0-50%)
                if (eqRenderer->getGUIEnvironment())
                    m_loadingScreenFont = eqRenderer->getGUIEnvironment()->getBuiltInFont();

                if (config.frameTimingEnabled) {
                    eqRenderer->setFrameTimingEnabled(true);
                }
                if (config.sceneProfileEnabled) {
                    eqRenderer->runSceneProfile();
                }

                // Create GraphicsInputHandler from renderer's event receiver
                // and connect it to InputActionBridge for game action routing
                auto* eventReceiver = eqRenderer->getEventReceiver();
                if (eventReceiver && m_inputBridge) {
                    m_graphicsInputHandler = std::make_unique<input::GraphicsInputHandler>(eventReceiver);
                    m_inputBridge->setInputHandler(m_graphicsInputHandler.get());
                    LOG_INFO(MOD_GRAPHICS, "Graphics input handler connected to bridge");

                    // T319: setActionCallback() had NO production caller, so
                    // every dispatcher result the bridge produced went to a
                    // debug log and nowhere else -- which is why wiring the R
                    // key alone would not have made it work. This is the
                    // return leg: the game thread hands the result to the
                    // renderer, which parks a chat prefill for its own thread
                    // to pick up. The decision lives in
                    // IrrlichtRenderer::handleActionResult(), not here, so
                    // the test can drive the same code the app does.
                    // m_renderer is destroyed BEFORE m_inputBridge in
                    // shutdown(), so the callback is cleared there.
                    m_inputBridge->setActionCallback(
                        [eqRenderer](const std::string& name,
                                     const action::ActionResult& result) {
                            eqRenderer->handleActionResult(name, result.success, result.message);
                        });
                }

                // Wire loading status and set initial progress text.
                // Loading thread is NOT started here — it starts when
                // OnGameStateComplete fires and the game thread creates
                // the snapshot with full zone data.
                m_loadingStatus.setTitle(L"EverQuest");
                m_loadingStatus.setProgress(0, "Connecting to login server...");
                m_eqClient->SetLoadingStatus(&m_loadingStatus);

#ifdef WITH_RDP
                // Initialize and start RDP server if enabled
                if (config.rdpEnabled) {
                    LOG_INFO(MOD_GRAPHICS, "Initializing RDP server on port {}...", config.rdpPort);
                    if (eqRenderer->initRDP(config.rdpPort)) {
                        if (eqRenderer->startRDPServer()) {
                            LOG_INFO(MOD_GRAPHICS, "RDP server started on port {}", config.rdpPort);
                            // D20g: RDP audio setup moved to Application
                            setupRDPAudio(eqRenderer->getRDPServer());
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
            EQT::PerformanceMetrics::instance().stopTimer("Graphics Init");
            LOG_FATAL(MOD_GRAPHICS, "Failed to initialize graphics");
            return false;
        }
        EQT::PerformanceMetrics::instance().stopTimer("Graphics Init");
#endif
    return true;
}

bool Application::initConsoleBridge(const ApplicationConfig& /*config*/) {
    // D25: Attach console bridge in headless mode for event logging
    if (!m_graphicsInitialized && !m_bridge) {
        m_consoleBridge = std::make_unique<bridge::ConsoleBridge>();
        // T40: same decorator wrapping as the graphics path above.
        m_stateSyncBridge = std::make_unique<eqt::bridge::GameStateSyncBridge>(
            *m_consoleBridge);
        m_bridge = m_stateSyncBridge.get();
        m_eqClient->setBridge(m_bridge);
        LOG_INFO(MOD_MAIN, "Console bridge attached (headless mode)");
    }
    return true;
}

bool Application::initLoginConnection(const ApplicationConfig& config) {
    // Start login connection AFTER graphics init to avoid timeout on slow devices.
    // DRM/EGL initialization can take 4-5 seconds on first run (Orange Pi), and the
    // login server will disconnect if no network pumping occurs during that window.
    LOG_INFO(MOD_MAIN, "Connecting to login server ({}:{})...", config.host, config.port);
    m_eqClient->LoginWorld().ConnectToLogin();

    m_running.store(true);
    m_lastUpdate = std::chrono::steady_clock::now();
    m_lastGraphicsUpdate = std::chrono::steady_clock::now();
    return true;
}

void Application::run() {
    LOG_INFO(MOD_MAIN, "Starting main loop...");
    mainLoop();
    LOG_INFO(MOD_MAIN, "Main loop exited");
}

void Application::shutdown() {
    if (!m_running.load() && !m_eqClient) {
        return; // Already shut down
    }

    LOG_INFO(MOD_MAIN, "Shutting down application...");
    m_running.store(false);

    // T47: clear all five bridge pointers (EverQuest::m_bridge plus the four
    // managers' -- InventoryManager/SpellManager/SkillManager/HotbarModel)
    // before the bridge itself is destroyed, in BOTH graphics and
    // true-headless (EQT_GRAPHICS=OFF) builds. setBridge() ->
    // wireBridgeToSubsystems() (T46) is unconditional and propagates null to
    // all five in one call. Placed here -- immediately after the running
    // flag drops and before anything else in shutdown() -- because nothing
    // downstream in this function publishes through the bridge (verified by
    // reading stopRDPServer(), joinLoadingThread(), ShutdownGraphics(),
    // IrrlichtRenderer::shutdown(), IGameMode::shutdown(), and ~EverQuest():
    // none of them push events/intents through eqt::bridge::GameStateBridge,
    // so clearing early drops nothing). See refactor/T47_bridge_teardown.md.
    if (m_eqClient) {
        m_eqClient->setBridge(nullptr);
    }

#ifdef EQT_HAS_GRAPHICS
    // Stop RDP server if running
#ifdef WITH_RDP
    if (m_graphicsInitialized && m_renderer) {
        if (m_renderer->isRDPRunning()) {
            LOG_INFO(MOD_GRAPHICS, "Stopping RDP server...");
            m_renderer->stopRDPServer();
        }
    }
#endif
    // D20e2: Join loading thread before shutdown (it may own GL context)
    if (m_loadingThread && m_loadingThread->isRunning()) {
        m_loadingStatus.quitRequested.store(true, std::memory_order_release);
        joinLoadingThread();
    }
    m_loadingThread.reset();

    // D20b5: EverQuest detaches bridge, Application destroys renderer
    if (m_eqClient) {
        m_eqClient->ShutdownGraphics();
    }
    // Destroy bridge before renderer (bridge holds renderer pointer).
    // T40: m_stateSyncBridge holds a reference to m_irrlichtBridge, so drop
    // it first to avoid leaving a dangling reference around.
    m_bridge = nullptr;
    m_stateSyncBridge.reset();
    m_irrlichtBridge.reset();
    if (m_renderer) {
        // T319: the action callback captures the raw renderer pointer and
        // m_inputBridge outlives m_renderer (it is reset further down), so
        // drop the callback before the renderer goes away.
        if (m_inputBridge) {
            m_inputBridge->setActionCallback(nullptr);
        }
        m_renderer->shutdown();
        m_renderer.reset();
    }
#endif

    // Output performance report before cleanup
    std::string perfReport = EQT::PerformanceMetrics::instance().generateReport();
    if (!perfReport.empty()) {
        LOG_INFO(MOD_MAIN, "{}", perfReport);
    }

    if (m_gameMode) {
        m_gameMode->shutdown();
        m_gameMode.reset();
    }

    m_inputBridge.reset();
    m_commandProcessor.reset();
    m_dispatcher.reset();
    m_actionHandler.reset();
    // T227: m_eqClient owns the one GameState; it is the LAST reset, after
    // every holder of a reference to it (mode, input bridge, command
    // processor, dispatcher) above.
    m_eqClient.reset();

    LOG_INFO(MOD_MAIN, "Application shutdown complete");
}

void Application::requestQuit() {
    m_running.store(false);
    if (m_gameMode) {
        m_gameMode->requestQuit();
    }
}

// D21b: Game thread loop — network, game state, intent processing
void Application::gameThreadLoop() {
    LOG_INFO(MOD_MAIN, "Game thread started");
    auto lastTick = std::chrono::steady_clock::now();

    while (m_running.load()) {
        try {
            // Process network events
            processNetworkEvents();

            // Check for zone connection state changes
            bool isConnected = m_eqClient && m_eqClient->IsFullyZonedIn();

            if (isConnected && !m_fullyConnected) {
                LOG_INFO(MOD_MAIN, "Fully connected to zone!");
                m_fullyConnected = true;
                // T227: world().setZoneConnected(true) is written at the
                // source (eq_entities.cpp, first ClientUpdate, the same
                // instant IsFullyZonedIn() flips) -- nothing to mirror here.

                // T43: was #ifdef EQT_HAS_GRAPHICS && m_graphicsInitialized.
                // LoadHotbarConfig()/GetHotbarModel() are both plain game-state
                // (P28 declassified them in eq.h -- HotbarModel has no renderer
                // dependency, publishAllSlots() just pushes bridge events), so a
                // headless build never loaded saved hotbar bindings or published
                // them on zone connect. Same declassification family as the
                // other T43 fixes -- gate removed, matches the unconditional
                // eq.h declaration.
                if (m_eqClient) {
                    m_eqClient->Bridge().LoadHotbarConfig();
                    // T171: same config file, same moment. It must be loaded
                    // before anyone can inspect us, and being in the zone is
                    // the first point at which that is possible.
                    m_eqClient->Bridge().LoadInspectMessage();
                    // U07b: HotbarModel publishes its own events
                    if (m_eqClient->GetHotbarModel()) {
                        m_eqClient->GetHotbarModel()->publishAllSlots();
                    }
                }
            }

            if (!isConnected && m_fullyConnected) {
                m_fullyConnected = false;
                // T30: fresh disconnect -- allow the next held command (even
                // if it's textually the same one as before, e.g. after a
                // reconnect cycle) to be announced again.
                m_preConnectionHeldAnnounced = false;
                m_preConnectionHeldLastCmd.clear();
            }

            // Fixed-rate game tick (~60 Hz)
            auto now = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastTick).count();

            if (deltaTime >= 1.0f / 60.0f) {
                processInput(deltaTime);
                updateGameState(deltaTime);
                lastTick = now;
            }

#ifdef EQT_HAS_GRAPHICS
            // Zone load requested (initial zone-in or re-zone).
            // Create snapshot, flag render thread to start loading thread.
            if (m_eqClient && m_eqClient->ConsumeZoneLoadRequest()) {
                LOG_INFO(MOD_MAIN, "Game thread: zone load requested, creating snapshot...");
                {
                    std::lock_guard<std::mutex> lock(m_zoneLoadMutex);
                    m_zoneLoadSnapshot = m_eqClient->Bridge().CreateZoneLoadSnapshot();
                }
                LOG_INFO(MOD_MAIN, "Game thread: snapshot created (zone='{}', entities={}, doors={})",
                         m_zoneLoadSnapshot.zoneName, m_zoneLoadSnapshot.entities.size(),
                         m_zoneLoadSnapshot.doors.size());
                m_zoneLoadReady.store(true, std::memory_order_release);
            }

            // Pick up graphics complete signal from render thread
            if (m_graphicsCompleteReady.load(std::memory_order_acquire)) {
                LOG_INFO(MOD_MAIN, "Game thread: graphicsCompleteReady=true, finalizing zone load...");
                if (m_eqClient) {
                    {
                        std::lock_guard<std::mutex> lock(m_zoneLoadMutex);
                        m_eqClient->Bridge().SetZoneBspTree(std::move(m_pendingBspTree), m_pendingBspTreeZone);
                    }
                    m_eqClient->OnGraphicsComplete();
                }
                // P04 (memory audit fix): the loading thread has been joined
                // (checkLoadingComplete joins BEFORE setting the flag we just
                // consumed), so nothing reads the snapshot anymore. Release its
                // entity/door/zone-line copies now instead of holding them
                // until the next zone's snapshot overwrites them.
                {
                    std::lock_guard<std::mutex> lock(m_zoneLoadMutex);
                    m_zoneLoadSnapshot = eqt::ZoneLoadSnapshot{};
                }
                m_graphicsCompleteReady.store(false, std::memory_order_release);
                LOG_INFO(MOD_MAIN, "Game thread: OnGraphicsComplete done");
            }
#endif

            // Check quit requests
            if (m_gameMode && m_gameMode->isQuitRequested()) {
                m_running.store(false);
            }
            if (m_eqClient && m_eqClient->IsQuitRequested()) {
                m_running.store(false);
            }

            // Sleep to target ~60 Hz game tick without spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        } catch (const std::exception& e) {
            LOG_ERROR(MOD_MAIN, "Exception in game thread: {}", e.what());
        }
    }

    LOG_INFO(MOD_MAIN, "Game thread stopped");
}

// D21b: Main loop is now render-only (main thread)
void Application::mainLoop() {
    LOG_INFO(MOD_MAIN, "Starting game thread");
    m_gameThread = std::make_unique<std::thread>(&Application::gameThreadLoop, this);

    LOG_INFO(MOD_MAIN, "Entering render loop (main thread)");

    while (m_running.load()) {
        try {
            // P36: SIGTERM handler (above) only sets a flag -- do the actual
            // quit request here, in ordinary (non-signal-handler) code on the
            // main thread, going through the same requestQuit() path as
            // /camp and window-close.
            if (s_terminateRequested.load(std::memory_order_relaxed)) {
                LOG_INFO(MOD_MAIN, "SIGTERM received, requesting clean shutdown...");
                requestQuit();
            }

            // Render
            render(0.0f);  // deltaTime computed inside render()

            // If no graphics, sleep to avoid spinning
            if (!m_graphicsInitialized) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }

        } catch (const std::exception& e) {
            LOG_ERROR(MOD_MAIN, "Exception in render loop: {}", e.what());
        }
    }

    LOG_INFO(MOD_MAIN, "Render loop exited, joining game thread");
    if (m_gameThread && m_gameThread->joinable()) {
        m_gameThread->join();
    }
    m_gameThread.reset();
    LOG_INFO(MOD_MAIN, "Game thread joined");
}

} // namespace eqt
