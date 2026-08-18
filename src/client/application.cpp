#include "client/application.h"
#include <json/json.h>  // Must be before eq.h → logging.h so JSONCPP_VERSION_STRING is defined
#include "client/eq.h"
#include "client/help/merged_help.h"
#include "client/eq_action_handler.h"
#include "client/combat.h"
#include "client/inventory/inventory_manager.h"
#include "client/mode/automated_mode.h"
#include "client/mode/headless_mode.h"
#include "client/mode/graphical_mode.h"
#include "client/spell/spell_manager.h"
#include "client/spell/spell_constants.h"
#include "client/output/graphical_renderer.h"
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

    // Create game state
    LOG_TRACE(MOD_MAIN, "Allocating GameState...");
    m_gameState = std::make_unique<state::GameState>();
    LOG_DEBUG(MOD_MAIN, "Game state created");

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
        m_eqClient->SetPathfinding(config.pathfindingEnabled);
        if (!config.navmeshPath.empty()) {
            m_eqClient->SetNavmeshPath(config.navmeshPath);
        }
        if (!config.mapsPath.empty()) {
            m_eqClient->SetMapsPath(config.mapsPath);
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
            m_eqClient->SetRegionMapsPath(config.regionMapsPath);
        }
        m_eqClient->SetConfigPath(config.configFile);

#ifdef WITH_AUDIO
        // Apply audio settings
        m_eqClient->SetAudioEnabled(config.audioEnabled);
        m_eqClient->SetMasterVolume(config.audioMasterVolume);
        m_eqClient->SetMusicVolume(config.audioMusicVolume);
        m_eqClient->SetEffectsVolume(config.audioEffectsVolume);
        if (!config.audioSoundfont.empty()) {
            m_eqClient->SetSoundFont(config.audioSoundfont);
        }
        m_eqClient->SetVendorMusic(config.audioVendorMusic);
#endif

        LOG_DEBUG(MOD_MAIN, "EverQuest client created");

    } catch (const std::exception& e) {
        LOG_FATAL(MOD_MAIN, "Failed to create EverQuest client: {}", e.what());
        EQT::PerformanceMetrics::instance().stopTimer("Client Creation");
        return false;
    }
    EQT::PerformanceMetrics::instance().stopTimer("Client Creation");

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

    // Create action handler adapter
    LOG_TRACE(MOD_MAIN, "Allocating EqActionHandler...");
    m_actionHandler = std::make_unique<EqActionHandler>(*m_eqClient);
    LOG_DEBUG(MOD_MAIN, "Action handler created");

    // Create action dispatcher
    LOG_TRACE(MOD_MAIN, "Allocating ActionDispatcher...");
    m_dispatcher = std::make_unique<action::ActionDispatcher>(*m_gameState);
    m_dispatcher->setActionHandler(m_actionHandler.get());
    LOG_DEBUG(MOD_MAIN, "Action dispatcher created");

    // Create command processor
    LOG_TRACE(MOD_MAIN, "Allocating CommandProcessor...");
    m_commandProcessor = std::make_unique<action::CommandProcessor>(*m_gameState, *m_dispatcher);
    LOG_DEBUG(MOD_MAIN, "Command processor created");

    // H03: Scripted command tour (--script <file>)
    if (!config.scriptPath.empty()) {
        if (loadScriptFile(config.scriptPath)) {
            LOG_INFO(MOD_MAIN, "Script loaded: {} ({} steps)", config.scriptPath, m_scriptSteps.size());
        } else {
            LOG_ERROR(MOD_MAIN, "Failed to load script file: {}", config.scriptPath);
        }
    }

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
    if (!m_gameMode->initialize(*m_gameState, modeConfig)) {
        LOG_FATAL(MOD_MAIN, "Failed to initialize game mode");
        return false;
    }
    LOG_DEBUG(MOD_MAIN, "Game mode initialized: {}",
        config.operatingMode == mode::OperatingMode::GraphicalInteractive ? "graphical" :
        config.operatingMode == mode::OperatingMode::HeadlessInteractive ? "headless" : "automated");

    // Create input action bridge
    LOG_TRACE(MOD_MAIN, "Allocating InputActionBridge...");
    m_inputBridge = std::make_unique<action::InputActionBridge>(*m_gameState, *m_dispatcher);
    m_inputBridge->setCommandProcessor(m_commandProcessor.get());
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

    // Connect command processor output to renderer
    auto* renderer = m_gameMode->getRenderer();
    if (renderer) {
        m_commandProcessor->setOutputRenderer(renderer);
        LOG_DEBUG(MOD_MAIN, "Command processor connected to renderer");
    }

    // Connect renderer callbacks to action dispatcher
    LOG_TRACE(MOD_MAIN, "Connecting renderer callbacks...");
    connectRendererCallbacks();
    LOG_DEBUG(MOD_MAIN, "Renderer callbacks connected");

#ifdef EQT_HAS_GRAPHICS
    // Initialize graphics if graphical mode
    m_graphicsInitialized = false;
    if (config.graphicsEnabled && config.operatingMode == mode::OperatingMode::GraphicalInteractive) {
        // eqClientPath already validated in S01 block above
        // Build constrained config: preset → JSON overrides → CLI overrides
        {
            EQT::Graphics::ConstrainedRendererConfig builtConfig;
            bool customMemorySpec = false;

            if (!config.constrainedPreset.empty()) {
                if (EQT::Graphics::ConstrainedRendererConfig::parseMemorySpec(config.constrainedPreset, builtConfig)) {
                    customMemorySpec = true;
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
        // single queue (the decorator's), which the decorator then applies
        // to state and forwards to m_irrlichtBridge unchanged.
        m_stateSyncBridge = std::make_unique<eqt::bridge::GameStateSyncBridge>(
            *m_irrlichtBridge, *m_gameState);
        m_renderer->setBridge(m_stateSyncBridge.get());
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
    }
#endif

    // D25: Attach console bridge in headless mode for event logging
    if (!m_graphicsInitialized && !m_bridge) {
        m_consoleBridge = std::make_unique<bridge::ConsoleBridge>();
        // T40: same decorator wrapping as the graphics path above.
        m_stateSyncBridge = std::make_unique<eqt::bridge::GameStateSyncBridge>(
            *m_consoleBridge, *m_gameState);
        m_bridge = m_stateSyncBridge.get();
        m_eqClient->setBridge(m_bridge);
        LOG_INFO(MOD_MAIN, "Console bridge attached (headless mode)");
    }

    // Start login connection AFTER graphics init to avoid timeout on slow devices.
    // DRM/EGL initialization can take 4-5 seconds on first run (Orange Pi), and the
    // login server will disconnect if no network pumping occurs during that window.
    LOG_INFO(MOD_MAIN, "Connecting to login server ({}:{})...", config.host, config.port);
    m_eqClient->ConnectToLogin();

    m_running.store(true);
    m_lastUpdate = std::chrono::steady_clock::now();
    m_lastGraphicsUpdate = std::chrono::steady_clock::now();

    LOG_INFO(MOD_MAIN, "Application initialized successfully");
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
    m_eqClient.reset();
    m_gameState.reset();

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
                if (m_gameState) m_gameState->world().setZoneConnected(true);

                // T43: was #ifdef EQT_HAS_GRAPHICS && m_graphicsInitialized.
                // LoadHotbarConfig()/GetHotbarModel() are both plain game-state
                // (P28 declassified them in eq.h -- HotbarModel has no renderer
                // dependency, publishAllSlots() just pushes bridge events), so a
                // headless build never loaded saved hotbar bindings or published
                // them on zone connect. Same declassification family as the
                // other T43 fixes -- gate removed, matches the unconditional
                // eq.h declaration.
                if (m_eqClient) {
                    m_eqClient->LoadHotbarConfig();
                    // U07b: HotbarModel publishes its own events
                    if (m_eqClient->GetHotbarModel()) {
                        m_eqClient->GetHotbarModel()->publishAllSlots();
                    }
                }
            }

            if (!isConnected && m_fullyConnected) {
                m_fullyConnected = false;
                if (m_gameState) m_gameState->world().setZoneConnected(false);
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
                    m_zoneLoadSnapshot = m_eqClient->CreateZoneLoadSnapshot();
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
                        m_eqClient->SetZoneBspTree(std::move(m_pendingBspTree));
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

void Application::processNetworkEvents() {
    if (m_eqClient) {
        m_eqClient->TickNetwork();
    }
}

void Application::processInput(float deltaTime) {
    // Update game mode input only when graphics are NOT handling input.
    // When graphics are active, IrrlichtRenderer handles all input through evdev/X11.
    // The game mode's ConsoleInputHandler must not run in parallel because in DRM mode
    // the same keystrokes reach both evdev and stdin, causing ESC to trigger both
    // ClearTarget (evdev) and Quit (stdin) simultaneously.
    if (m_gameMode && !m_graphicsInitialized) {
        m_gameMode->update(deltaTime);
    }

    // Process input through the action bridge.
    // In graphics mode, GraphicsInputHandler drains the renderer's bridgeQueue_ (evdev/X11
    // key events) — this is safe because it doesn't touch stdin. The game mode update above
    // is still gated to headless-only to prevent ConsoleInputHandler stdin duplicate reads.
    // Skip during loading — no collision detection means movement would drift the player
    // off the map, and the server would interpolate the unbounded position.
    if (m_inputBridge && m_fullyConnected) {
        m_inputBridge->update(deltaTime);
    } else if (!m_fullyConnected) {
        // T30: the gate above holds the ENTIRE console command queue
        // (including /quit) until the client is fully connected -- with
        // no server available, that never happens, and a piped script
        // hangs forever with nothing to show for it (see
        // refactor/T30_headless_shutdown_fix.md). This narrow intercept
        // handles only client-local lifecycle commands (/quit, /help,
        // ...) directly, and makes anything else held observable on
        // stdout -- it must NOT become a second, wider command channel.
        processPreConnectionLifecycleCommands();
    }

    // H03: Advance the scripted command tour, if one was loaded via --script.
    updateScript(deltaTime);
}

void Application::processPreConnectionLifecycleCommands() {
    if (!m_consoleInputHandler) {
        return;
    }

    // Only ever look at (and, if matched, pop) the FRONT of the queue.
    // If it's not a lifecycle command, stop -- leave it and everything
    // behind it exactly where it was, for the normal gated path once
    // m_fullyConnected flips true. This is what keeps this an intercept
    // for a narrow, fixed vocabulary rather than a parallel dispatch
    // path for everything (the plan's STOP condition).
    auto peeked = m_consoleInputHandler->peekRawCommand();
    if (!peeked) {
        return;
    }

    std::string raw = *peeked;
    size_t start = raw.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        // Blank line -- consume and drop it silently, same as the
        // fully-connected path would (DispatchSlashCommand() also
        // treats an all-whitespace line as a no-op).
        m_consoleInputHandler->consumeRawCommand();
        return;
    }
    size_t end = raw.find_last_not_of(" \t\r\n");
    std::string trimmed = raw.substr(start, end - start + 1);
    std::string bare = (trimmed[0] == '/') ? trimmed.substr(1) : trimmed;

    std::string token = bare;
    size_t spacePos = token.find(' ');
    if (spacePos != std::string::npos) {
        token = token.substr(0, spacePos);
    }
    std::string lowerToken = token;
    std::transform(lowerToken.begin(), lowerToken.end(), lowerToken.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // The narrow class: client-local lifecycle commands that have no
    // gameplay effect and no dependency on a live connection. Deliberately
    // does NOT include movement/combat/chat/etc -- opening this gate wider
    // would defeat the loading-drift protection it exists alongside (see
    // the comment on the m_fullyConnected gate above). /quit and its
    // registered aliases /exit and /q (see eq_commands_movement.cpp's
    // "quit" Command) terminate the process; /help and its alias /? are
    // read-only and safe to answer at any time.
    //
    // "q" is here per Will's ruling 2026-08-03 ("/q is a shortcut for
    // /quit and should have the same functionality: quit the application
    // immediately without camping"). Keep this set in sync with that
    // Command's alias list -- if they diverge, /q gets held here while
    // /quit exits, which is exactly the asymmetry the ruling removed.
    static const std::set<std::string> kPreConnectionLifecycleCommands = {
        "quit", "exit", "q", "help", "?"
    };

    if (kPreConnectionLifecycleCommands.count(lowerToken) == 0) {
        // Not a lifecycle command -- hold it (do not consume), but make
        // the wait observable: previously a script piping into a client
        // that never connects got no stdout at all for this, just
        // silence until something killed the process.
        if (!m_preConnectionHeldAnnounced || m_preConnectionHeldLastCmd != trimmed) {
            CommandDispatchResult held;
            held.command = trimmed;
            held.registry = "pending";
            held.recognized = false;
            held.held = true;
            held.message = "waiting for connection";
            printCommandResult(held);
            m_preConnectionHeldAnnounced = true;
            m_preConnectionHeldLastCmd = trimmed;
        }
        return;
    }

    // Recognized lifecycle command -- now actually consume it.
    m_consoleInputHandler->consumeRawCommand();
    m_preConnectionHeldAnnounced = false;
    m_preConnectionHeldLastCmd.clear();

    if (lowerToken == "quit" || lowerToken == "exit") {
        // No live session to protect pre-connection (that's what
        // EverQuest's own "/quit" handler's advisory "use /camp or /q"
        // message is for -- it stays untouched, still reachable once
        // connected). Here there's nothing to safely log out of, so
        // /quit is a direct, immediate shutdown request -- the same
        // controlled path SIGTERM and window-close already use (P36).
        CommandDispatchResult result;
        result.command = trimmed;
        result.registry = "lifecycle";
        result.recognized = true;
        result.success = true;
        result.message = "pre-connection quit";
        printCommandResult(result);
        requestQuit();
    } else {
        // /help, /?: read-only and safe regardless of connection state --
        // route through the normal unified dispatcher so it gets the same
        // registry-1 handler (and stdout ack) it would once connected.
        printCommandResult(DispatchSlashCommand(trimmed));
    }
}

bool Application::loadScriptFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        LOG_ERROR(MOD_MAIN, "Script file not found: {}", path);
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        // Trim leading/trailing whitespace.
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue;  // blank line
        }
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line.empty() || line[0] == '#') {
            continue;  // comment
        }

        std::istringstream iss(line);
        std::string first;
        iss >> first;

        ScriptStep step;
        if (first == "wait") {
            float seconds = 0.0f;
            iss >> seconds;
            step.isWait = true;
            step.waitSeconds = std::max(0.0f, seconds);
        } else {
            step.isWait = false;
            step.commandText = line;
        }
        m_scriptSteps.push_back(step);
    }

    return true;
}

void Application::updateScript(float deltaTime) {
    if (m_scriptIndex >= m_scriptSteps.size()) {
        return;
    }

    ScriptStep& step = m_scriptSteps[m_scriptIndex];

    if (step.isWait) {
        if (!m_scriptWaitStarted) {
            m_scriptWaitRemaining = step.waitSeconds;
            m_scriptWaitStarted = true;
            LOG_INFO(MOD_MAIN, "Script: wait {:.1f}s (step {}/{})",
                step.waitSeconds, m_scriptIndex + 1, m_scriptSteps.size());
        }
        m_scriptWaitRemaining -= deltaTime;
        if (m_scriptWaitRemaining <= 0.0f) {
            m_scriptWaitStarted = false;
            ++m_scriptIndex;
        }
        return;
    }

    // Command step: hold here (without consuming it) until fully zoned in, so a
    // script that starts with a command line rather than "wait" doesn't drop it.
    if (!m_fullyConnected) {
        return;
    }

    LOG_INFO(MOD_MAIN, "Script: executing '{}' (step {}/{})",
        step.commandText, m_scriptIndex + 1, m_scriptSteps.size());
    // T29: --script now goes through the same unified dispatch order as
    // piped stdin (registry 1: EverQuest::m_command_registry, THEN registry
    // 2: action::CommandProcessor, THEN registry 3: the renderer command
    // bridge -- see DispatchSlashCommand()'s doc comment). Previously this
    // only tried action::CommandProcessor and fell back straight to the
    // renderer bridge, never consulting EverQuest::m_command_registry at
    // all -- exactly the hole that left /combat (and the rest of registry
    // 1) unreachable from --script.
    printCommandResult(DispatchSlashCommand(step.commandText));
    ++m_scriptIndex;

    if (m_scriptIndex >= m_scriptSteps.size()) {
        LOG_INFO(MOD_MAIN, "Script: complete ({} steps)", m_scriptSteps.size());
    }
}

// T29: registries 2+3 of the unified dispatch order. Shared by
// DispatchSlashCommand() (stage 1 already tried and failed) and the
// fallback wired into EverQuest for the live chat window (registry 1
// already tried and failed there too, via ProcessChatInput's own call to
// EverQuest::DispatchSlashCommand). See refactor/T29_unified_command_channel.md.
bool Application::dispatchToActionOrRenderer(const std::string& cmdBody, CommandDispatchResult& result) {
    // T143: registry-aware /help. /help (and aliases h, ?) is no longer
    // registered in registry 1 (EverQuest::m_command_registry) precisely so
    // it reaches HERE -- Application is the only object holding pointers to
    // both registry 1 (via m_eqClient->GetCommandRegistry()) and registry 2
    // (m_commandProcessor), so it is the only place that can build a merged
    // view. Intercepted before the registry-2 name check below so a /help
    // that also happens to collide with a real registry-2 command name
    // (it doesn't today, but nothing enforces that) can never be shadowed.
    // See include/client/help/merged_help.h and
    // refactor/T143_registry_aware_help.md.
    if (m_eqClient) {
        eqt::help::HelpResponse helpResponse = eqt::help::BuildHelpResponse(
            cmdBody, m_eqClient->GetCommandRegistry(), m_commandProcessor.get());
        if (helpResponse.handled) {
            for (const auto& line : helpResponse.lines) {
                m_eqClient->AddChatSystemMessage(line);
            }
            result.recognized = true;
            result.registry = "help";
            result.success = true;
            result.message = fmt::format("{} lines", helpResponse.lines.size());
            return true;
        }
    }

    // Registry 2: action::CommandProcessor. Only attempt it if it actually
    // recognizes the command name -- calling processCommand() unconditionally
    // would also swallow "Unknown command" as a (failed) CommandProcessor
    // result for names that were really meant for registry 3, which would
    // stop them from ever reaching the renderer bridge.
    std::string name = cmdBody;
    size_t spacePos = name.find(' ');
    if (spacePos != std::string::npos) {
        name = name.substr(0, spacePos);
    }
    if (m_commandProcessor && m_commandProcessor->getCommandInfo(name).has_value()) {
        eqt::action::ActionResult ar = m_commandProcessor->processCommand(cmdBody);
        result.recognized = true;
        result.registry = "action";
        result.success = ar.success;
        result.message = ar.message;
        return true;
    }

    // Registry 3: renderer debug commands, via the RendererCommand bridge
    // queue -- drained on the RENDER thread (IrrlichtBridge's event switch).
    // P10: never call m_renderer->processSlashCommand() (or any renderer
    // method) directly from here -- this runs on the GAME thread
    // (processInput() <- gameThreadLoop()), and calling renderer/GL code
    // off the render thread runs without the EGL context current. P10 hit
    // this and got deterministic-but-garbage screenshot pixels instead of a
    // crash, because Mesa doesn't fail loudly on the misuse. Going through
    // m_bridge instead reuses the existing, already-thread-safe path.
    // This stage is a fire-and-forget push, not a call -- there is no ack
    // path back from the render thread, so it is honestly reported as
    // "queued", never "applied": we cannot know here whether the renderer
    // recognized it either (the renderer-side registry has no query
    // interface reachable from the game thread).
    if (m_bridge) {
        std::string fullCmd = "/" + cmdBody;
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::RendererCommand,
            eqt::state::RendererCommandData{fullCmd}));
        result.recognized = true;
        result.registry = "renderer";
        result.success = false;  // "queued", not "applied" -- see comment above
        result.message = "queued to renderer command bridge (async, no ack)";
        return true;
    }

    result.registry = "none";
    return false;
}

void Application::printCommandResult(const CommandDispatchResult& result) const {
    // T29: stable, greppable, single-line stdout ack -- this is the piece
    // that actually delivers automation (see refactor/T29_unified_command_
    // channel.md scope item 3). A script can synchronise on:
    //   - "recognized=true/false": did ANY registry accept it at all.
    //   - "registry=...": which vocabulary handled it (chat/game/action/
    //     renderer/help/none). T143: "help" is the merged registry-1 +
    //     registry-2 /help view (Application::dispatchToActionOrRenderer),
    //     not a registry of its own.
    //   - "outcome=...": applied (registries 1/2, handler ran and reported
    //     success), failed (registries 1/2, handler ran and reported
    //     failure -- see message=), queued (registry 3 -- fire-and-forget
    //     onto the render-thread bridge, no ack path, NOT a claim the
    //     effect landed), held (T30 -- the client isn't fully connected
    //     yet, so this command was deliberately NOT dispatched; it is
    //     still queued and will run once connected -- distinct from
    //     unrecognized, which means no registry claimed it at all), or
    //     unrecognized (registry=none).
    // A script canNOT synchronise on "the game state has updated" from this
    // line alone -- most handlers just send a packet and the server answers
    // asynchronously later (e.g. "/combat on" acks locally as applied
    // immediately, but the auto-attack/loot cycle it enables plays out over
    // subsequent server packets the script must watch for separately).
    std::string outcome;
    if (result.held) {
        outcome = "held";
    } else if (!result.recognized) {
        outcome = "unrecognized";
    } else if (result.registry == "renderer") {
        outcome = "queued";
    } else {
        outcome = result.success ? "applied" : "failed";
    }

    std::string escaped;
    escaped.reserve(result.command.size());
    for (char c : result.command) {
        if (c == '"' || c == '\\') escaped += '\\';
        escaped += c;
    }

    std::cout << "CMD outcome=" << outcome
               << " registry=" << result.registry
               << " recognized=" << (result.recognized ? "true" : "false");
    if (!result.message.empty()) {
        std::string escapedMsg;
        escapedMsg.reserve(result.message.size());
        for (char c : result.message) {
            if (c == '"' || c == '\\') escapedMsg += '\\';
            escapedMsg += c;
        }
        std::cout << " message=\"" << escapedMsg << "\"";
    }
    std::cout << " cmd=\"" << escaped << "\"" << std::endl;
}

void Application::wireUnifiedCommandDispatch() {
    if (!m_eqClient) {
        return;
    }
    // T29: registry 1 (EverQuest::m_command_registry, tried by
    // ProcessChatInput() itself via EverQuest::DispatchSlashCommand())
    // already failed by the time this fallback runs. cmdLine has no
    // leading '/' (EverQuest strips it before calling the fallback).
    m_eqClient->SetUnrecognizedCommandFallback([this](const std::string& cmdLine) {
        CommandDispatchResult result;
        result.command = "/" + cmdLine;
        return dispatchToActionOrRenderer(cmdLine, result);
    });
}

CommandDispatchResult Application::DispatchSlashCommand(const std::string& rawLine) {
    CommandDispatchResult result;
    result.command = rawLine;

    // Trim surrounding whitespace (piped lines commonly carry trailing \r
    // from CRLF-authored script files).
    size_t start = rawLine.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        result.registry = "none";
        return result;
    }
    size_t end = rawLine.find_last_not_of(" \t\r\n");
    std::string trimmed = rawLine.substr(start, end - start + 1);

    // Accept an optional leading '/' (matches CommandProcessor::
    // processCommand()'s existing looseness) -- but unlike EverQuest::
    // ProcessChatInput()'s live chat-window convention, a line with NO
    // leading '/' is NOT auto-sent as chat here. This entry point is the
    // slash command vocabulary specifically (Will's ruling, docs/
    // decisions.md 2026-08-02); plain chat still works via an explicit
    // /say, or ConsoleInputHandler's separate bespoke "say"/"tell" words.
    std::string cmdBody = (trimmed[0] == '/') ? trimmed.substr(1) : trimmed;
    if (cmdBody.empty()) {
        result.registry = "none";
        return result;
    }

    // Stage 1: EverQuest::m_command_registry -- the SAME registry the live
    // chat window uses (EverQuest::ProcessChatInput -> DispatchSlashCommand).
    // This is what --script never consulted before T29, which is why
    // /combat (T26's blocker) couldn't be reached from automation.
    if (m_eqClient && m_eqClient->DispatchSlashCommand(cmdBody)) {
        result.recognized = true;
        result.registry = "game";
        result.success = true;
        return result;
    }

    // Stages 2/3: shared with the live chat window's fallback.
    dispatchToActionOrRenderer(cmdBody, result);
    return result;
}

void Application::updateGameState(float deltaTime) {
    // D21a: Game-side tick (spell/buff updates + intent processing)
    // T45: was `m_graphicsInitialized` alone -- that flag means "the renderer
    // subsystem exists", not "the game is ready to tick", and is permanently
    // false in a --no-graphics run, so GameTick() (spell cooldowns,
    // memorization, cast timeouts, buff expiry, intent draining) never ran
    // headless at all (refactor/deferred.md, T44 finding). Fixed by OR'ing in
    // EverQuest::IsGraphicsReady() (loading phase reached COMPLETE), the same
    // game-lifecycle "state ready" signal T41 used for the full-state
    // snapshot -- true once EverQuest::OnGameStateComplete()'s headless arm
    // runs (immediately at zone-in, no graphics phase to wait on).
    //
    // Graphics mode is provably unaffected: m_graphicsInitialized is a
    // one-way latch, set once inside Init() before gameThreadLoop() (and
    // therefore this function) is ever called, and reset only by
    // ShutdownGraphics() at teardown -- see application.h's own comment on
    // the member ("Set once at init, read by both (safe)") and InitGraphics's
    // failure path returning false out of Init() entirely rather than
    // proceeding with the flag false. So in every graphics-mode run,
    // m_graphicsInitialized is already true for every single iteration of
    // this loop, including during zone loading (re-zones do not reset it) --
    // OR'ing in a second condition that can only ever help the *headless*
    // side cannot change graphics-mode timing.
    if (m_eqClient && (m_graphicsInitialized || m_eqClient->IsGraphicsReady())) {
        m_eqClient->GameTick(deltaTime);
    }

    // Update movement in EverQuest
    if (m_eqClient) {
        m_eqClient->UpdateMovement();

        // T26: advance the loot state machine every game tick, unconditionally
        // (graphics or headless) -- same rationale as UpdateMovement() above.
        // GameTick() (guarded by m_graphicsInitialized, just above) is NOT a
        // valid home: the loot path fires in headless mode too, and driving
        // it from CombatManager::Update() (m_update_thread, 2Hz) would
        // re-create the P36 self-join deadlock class -- see the CombatManager
        // friend-class comment and refactor/T26_loot_state_machine.md.
        if (CombatManager* cm = m_eqClient->GetCombatManager()) {
            cm->UpdateLootState();
        }
    }

    // syncGameStateFromClient() was deleted (T104) -- it had no remaining
    // work; see application.h's "Synchronization" section for the history.

    // Update loading progress
    updateLoadingProgress();

    // D21a: Audio update (was in PostRenderTick — game thread responsibility)
    if (m_eqClient && m_graphicsInitialized) {
        m_eqClient->PostRenderTick(deltaTime);
    }

    // D25: In headless mode, drain and apply events to the console bridge
    if (m_eqClient && m_consoleBridge && !m_graphicsInitialized) {
        m_eqClient->ProcessBridgeEvents();
    }
}

void Application::render(float deltaTime) {
#ifdef EQT_HAS_GRAPHICS
    if (m_graphicsInitialized && m_eqClient && m_renderer) {
        auto now = std::chrono::steady_clock::now();
        float gfxDeltaTime = std::chrono::duration<float>(now - m_lastGraphicsUpdate).count();

        if (gfxDeltaTime >= 1.0f / 60.0f) {
            // Loading thread active — it owns the GL context, just check if done
            if (isLoadingThreadActive()) {
                checkLoadingComplete();
                m_lastGraphicsUpdate = now;
                return;
            }

            // Game thread flagged zone load ready — start loading thread
            if (m_zoneLoadReady.load(std::memory_order_acquire)) {
                startLoadingThread();
                m_zoneLoadReady.store(false, std::memory_order_release);
                m_lastGraphicsUpdate = now;
                return;  // Loading thread now owns GL context
            }

            // Network handshake phase (0-50%): render loading screen on main thread
            if (!m_loadingStatus.loadingComplete.load(std::memory_order_relaxed) &&
                m_loadingStatus.percent.load(std::memory_order_relaxed) < 50) {
                float progress = m_loadingStatus.percent.load(std::memory_order_relaxed) / 100.0f;
                std::wstring title = m_loadingStatus.getTitle();
                std::string textNarrow = m_loadingStatus.getText();
                std::wstring text(textNarrow.begin(), textNarrow.end());
                EQT::Graphics::LoadingThread::drawLoadingFrame(
                    m_renderer->getDriver(), m_loadingScreenFont, progress, title, text);
                m_lastGraphicsUpdate = now;
                return;
            }

            // Normal gameplay: drain bridge events, render frame
            m_eqClient->PreRenderTick(gfxDeltaTime);

            bool result = m_renderer->processFrame(gfxDeltaTime);

            if (!result) {
                LOG_DEBUG(MOD_GRAPHICS, "Graphics window closed");
                m_running.store(false);
            } else if (!m_config.dumpFramesDir.empty() && m_config.dumpFramesEvery > 0
                       && m_dumpFrameIndex < static_cast<uint64_t>(m_config.dumpFramesMaxCount)) {
                // T64: video-capture evidence tool (--dump-frames-dir/--dump-every),
                // extracted from the P12 anim video-check session and made a
                // first-class tool. Use this when the thing under review is
                // temporal -- a weapon appearing mid-swing, a cast, a zone
                // transition, particle behavior -- that a still can't show.
                // Runs on the render thread (same thread that owns the
                // EGL/GL context), same constraint /screenshot's
                // saveScreenshot() already requires. No-op (zero cost beyond
                // one string-empty + two int checks) when the flags aren't
                // passed, so production behavior is untouched by default.
                // dumpFramesMaxCount bounds total frames written regardless
                // of session length -- see the field comment in
                // application.h for why (Pi /tmp tmpfs is small).
                ++m_dumpFrameCounter;
                if (m_dumpFrameCounter % static_cast<uint64_t>(m_config.dumpFramesEvery) == 0) {
                    char filename[64];
                    std::snprintf(filename, sizeof(filename), "/frame_%06llu.png",
                        static_cast<unsigned long long>(m_dumpFrameIndex));
                    std::string path = m_config.dumpFramesDir + filename;
                    m_renderer->saveScreenshot(path);
                    LOG_INFO(MOD_GRAPHICS, "Frame dump: {} (frame counter {})", path, m_dumpFrameCounter);
                    ++m_dumpFrameIndex;
                    if (m_dumpFrameIndex >= static_cast<uint64_t>(m_config.dumpFramesMaxCount)) {
                        LOG_INFO(MOD_GRAPHICS, "Frame dump: reached --dump-max-frames cap ({}), stopping capture",
                            m_config.dumpFramesMaxCount);
                    }
                }
            }
            m_lastGraphicsUpdate = now;
        }
    }
#else
    (void)deltaTime;
#endif
}

// =============================================================================
// D20e2: Zone Loading (moved from EverQuest)
// =============================================================================

#ifdef EQT_HAS_GRAPHICS
void Application::startLoadingThread() {
    if (!m_renderer || !m_graphicsInitialized) return;
    if (m_loadingThread && m_loadingThread->isRunning()) {
        LOG_WARN(MOD_GRAPHICS, "startLoadingThread: loading thread already running");
        return;
    }

    // Extract GL handles while main thread still owns context
    m_glHandles = EQT::Graphics::LoadingThread::extractGLHandles(
        m_renderer->getDevice(), m_renderer->getDriver());

    // Reset loading status
    m_loadingStatus.reset();

    // Get font for loading screen
    irr::gui::IGUIFont* font = nullptr;
    if (m_renderer->getGUIEnvironment())
        font = m_renderer->getGUIEnvironment()->getBuiltInFont();

    // Release GL context on main thread
    EQT::Graphics::LoadingThread::releaseContext(m_glHandles);

    // Set the renderer loading flag so main thread skips GL calls
    m_renderer->setLoading(true);

    // Start loading thread with active phase callback
    m_loadingThread = std::make_unique<EQT::Graphics::LoadingThread>();
    m_loadingThread->start(
        m_renderer->getDevice(), m_renderer->getDriver(), font, m_glHandles,
        m_loadingStatus,
        [this](EQT::Graphics::LoadingStatus& status) {
            loadZoneGraphicsOnThread(status);
        });

    LOG_INFO(MOD_GRAPHICS, "startLoadingThread: loading thread started (backend={})",
             m_glHandles.backend == EQT::Graphics::GLContextHandles::Backend::GLX ? "GLX" :
             m_glHandles.backend == EQT::Graphics::GLContextHandles::Backend::EGL ? "EGL" : "software");
}

void Application::joinLoadingThread() {
    if (!m_loadingThread) return;

    m_loadingThread->join();
    m_loadingThread.reset();

    // Reacquire GL context on main thread
    EQT::Graphics::LoadingThread::acquireContext(m_glHandles);

    // Clear loading flag
    if (m_renderer)
        m_renderer->setLoading(false);

    LOG_INFO(MOD_GRAPHICS, "joinLoadingThread: loading thread joined, GL context reacquired");
}

bool Application::checkLoadingComplete() {
    if (!m_loadingThread) return false;
    if (m_loadingStatus.loadingComplete.load(std::memory_order_acquire)) {
        LOG_INFO(MOD_GRAPHICS, "checkLoadingComplete: loading thread signaled done, joining...");
        joinLoadingThread();

        // Check if loading failed — if so, quit instead of finalizing
        if (m_loadingStatus.quitRequested.load(std::memory_order_acquire)) {
            LOG_FATAL(MOD_GRAPHICS, "checkLoadingComplete: loading thread failed — shutting down");
            m_running.store(false);
            return true;
        }

        LOG_INFO(MOD_GRAPHICS, "checkLoadingComplete: joined, extracting BSP tree...");
        // D21b: Signal game thread to finalize (BSP tree + OnGraphicsComplete)
        if (m_renderer) {
            std::lock_guard<std::mutex> lock(m_zoneLoadMutex);
            m_pendingBspTree = m_renderer->getZoneBspTree();
        }
        LOG_INFO(MOD_GRAPHICS, "checkLoadingComplete: setting graphicsCompleteReady=true");
        m_graphicsCompleteReady.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

void Application::loadZoneGraphicsOnThread(EQT::Graphics::LoadingStatus& status) {
    if (!m_renderer || !m_eqClient) {
        LOG_FATAL(MOD_GRAPHICS, "loadZoneGraphicsOnThread: renderer or eqClient is null");
        return;
    }
    const auto& snap = m_zoneLoadSnapshot;
    LOG_INFO(MOD_GRAPHICS, "loadZoneGraphicsOnThread: starting (zone='{}', entities={}, doors={}, player='{}')",
             snap.zoneName, snap.entities.size(), snap.doors.size(), snap.playerName);

    if (snap.zoneName.empty()) {
        LOG_FATAL(MOD_GRAPHICS, "loadZoneGraphicsOnThread: snapshot has empty zone name — aborting");
        return;
    }

    // Temporarily clear loading flag so renderer methods work on this thread.
    m_renderer->setLoading(false);

    // P17: unload the PREVIOUS zone's renderer state (region meshes, entities,
    // constrained texture cache, etc.) synchronously, right here, before this
    // (new) zone touches any of it. This replaces relying on EverQuest's
    // queued "/unloadzone_internal" RendererCommand (pushed from
    // CleanupZone(), applied only by PreRenderTick -> ProcessBridgeEvents()),
    // which is a real bug: PreRenderTick is only reached from the "normal
    // gameplay" branch of Application::render() -- during the entire
    // network-handshake phase and the whole loading-thread-active phase of a
    // zone transition, render() takes early-return branches that never call
    // it. That queued event therefore sits unapplied until AFTER this new
    // zone's own load has already finished and normal gameplay resumes,
    // meaning the OLD zone's textures were still resident in
    // ConstrainedTextureCache the entire time this zone's textures were
    // being uploaded -- the root mechanism behind the P13/P15 cross-zone
    // eviction findings (P15: "ConstrainedTextureCache is never reset
    // across zone unloads"; true in effect, though the reset code itself
    // does exist -- it was just starved out by this ordering). Calling
    // unloadZone() here, synchronously, on the loading thread (which owns
    // the GL context for the entire duration of this function), guarantees
    // the old zone's cache/scene state is gone before a single byte of the
    // new zone uploads. See CleanupZone() (eq_zone.cpp) for the matching
    // removal of the now-redundant (and, if left in, actively harmful --
    // it would fire AFTER this zone loads and wipe ITS textures instead)
    // queued push.
    m_renderer->unloadZone();

    // 1. Collision map
    if (snap.zoneMap && m_bridge) {
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::CollisionMapChanged,
            eqt::state::CollisionMapChangedData{snap.zoneMap}));
    }

    // 2. Zone lines
    if (!snap.zoneLineBBoxes.empty()) {
        m_renderer->setZoneLineBoundingBoxes(snap.zoneLineBBoxes);
    }

    // 3. Build HCMap placeholder + minimal collision
    m_renderer->setupInstantScene(snap.zoneName, snap.playerX, snap.playerY, snap.playerZ);

    // 4. Zone environment data
    if (!snap.zoneName.empty()) {
        m_renderer->storeZoneEnvironment(snap.skyType, snap.zoneType,
            snap.fogRed, snap.fogGreen, snap.fogBlue,
            snap.fogMinClip, snap.fogMaxClip);
    }

    // 5. Register all entities from snapshot
    if (m_renderer->getEntityRenderer())
        m_renderer->getEntityRenderer()->setPlayerLevel(snap.playerLevel);

    LOG_INFO(MOD_GRAPHICS, "loadZoneGraphicsOnThread: registering {} entities (from snapshot)", snap.entities.size());

    size_t registered = 0, failed = 0;
    for (const auto& entity : snap.entities) {
        bool isPlayer = (entity.name == snap.playerName);
        bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);
        bool isCorpse = (entity.npc_type == 2 || entity.npc_type == 3);
        if (!isCorpse && entity.name.find("corpse") != std::string::npos)
            isCorpse = true;

        EQT::Graphics::EntityAppearance appearance;
        appearance.face = entity.face;
        appearance.haircolor = entity.haircolor;
        appearance.hairstyle = entity.hairstyle;
        appearance.beardcolor = entity.beardcolor;
        appearance.beard = entity.beard;
        appearance.texture = entity.equip_chest2;
        appearance.helm = entity.helm;
        for (int i = 0; i < 9; i++) {
            appearance.equipment[i] = entity.equipment[i];
            appearance.equipment_tint[i] = entity.equipment_tint[i];
        }

        bool ok = m_renderer->registerEntity(entity.spawn_id, entity.race_id, entity.name,
                                   entity.x, entity.y, entity.z, entity.heading,
                                   isPlayer, entity.gender, appearance, isNPC, isCorpse, entity.size,
                                   entity.level);
        if (ok) registered++; else failed++;

        if (isPlayer) {
            m_renderer->setPlayerSpawnId(snap.playerSpawnId);
            if (entity.light > 0)
                m_renderer->setEntityLight(entity.spawn_id, entity.light);
            m_renderer->updatePlayerAppearance(entity.race_id, entity.gender, appearance);
        } else if (entity.light > 0) {
            m_renderer->setEntityLight(entity.spawn_id, entity.light);
        }
    }

    LOG_INFO(MOD_GRAPHICS, "loadZoneGraphicsOnThread: {} registered, {} failed", registered, failed);

    // 6. Register doors from snapshot
    for (const auto& door : snap.doors) {
        m_renderer->registerDoor(door.doorId, door.name, door.x, door.y, door.z,
                                 door.heading, door.incline, door.size, door.opentype,
                                 door.isOpen);
    }

    // 7. Camera
    m_renderer->setCameraMode(EQT::Graphics::IrrlichtRenderer::CameraMode::Follow);
    float heading512 = snap.playerHeading * 512.0f / 360.0f;
    m_renderer->setPlayerPosition(snap.playerX, snap.playerY, snap.playerZ, heading512);

    // 8. Hotbar callback
    setupHotbarCallback();

    m_eqClient->SetPlayerGraphicsEntityPending(true);

    // 9. Sequential zone asset loading
    m_renderer->loadZoneSequential(snap.eqClientPath, status);

    if (m_renderer->hasLoadFailed()) {
        LOG_FATAL(MOD_GRAPHICS, "loadZoneGraphicsOnThread: sequential loader failed — requesting quit");
        status.quitRequested.store(true, std::memory_order_release);
        return;
    }

    LOG_INFO(MOD_GRAPHICS, "loadZoneGraphicsOnThread: complete for zone '{}'", snap.zoneName);

    // Restore loading flag — will be cleared by joinLoadingThread on main thread
    m_renderer->setLoading(true);
}

void Application::loadZoneGraphics() {
    if (!m_graphicsInitialized || !m_renderer || !m_eqClient) {
        LOG_WARN(MOD_GRAPHICS, "loadZoneGraphics called but graphics not initialized");
        m_eqClient->SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!");
        return;
    }

    m_eqClient->SetLoadingPhase(LoadingPhase::GRAPHICS_LOADING_ZONE, "Entering world...");

    // Use snapshot (create if not already created by threaded path)
    if (m_zoneLoadSnapshot.zoneName.empty()) {
        m_zoneLoadSnapshot = m_eqClient->CreateZoneLoadSnapshot();
    }
    const auto& snap = m_zoneLoadSnapshot;

    // 1. Collision map
    if (snap.zoneMap && m_bridge) {
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::CollisionMapChanged,
            eqt::state::CollisionMapChangedData{snap.zoneMap}));
    }

    // 2. Zone lines
    if (!snap.zoneLineBBoxes.empty())
        m_renderer->setZoneLineBoundingBoxes(snap.zoneLineBBoxes);

    // 3. HCMap placeholder
    m_renderer->setupInstantScene(snap.zoneName, snap.playerX, snap.playerY, snap.playerZ);

    // 4. Environment
    if (!snap.zoneName.empty()) {
        m_renderer->storeZoneEnvironment(snap.skyType, snap.zoneType,
            snap.fogRed, snap.fogGreen, snap.fogBlue,
            snap.fogMinClip, snap.fogMaxClip);
    }

    // 5. Entities from snapshot
    if (m_renderer->getEntityRenderer())
        m_renderer->getEntityRenderer()->setPlayerLevel(snap.playerLevel);

    LOG_INFO(MOD_GRAPHICS, "loadZoneGraphics: registering {} entities (from snapshot)", snap.entities.size());

    size_t registered = 0, failed = 0;
    for (const auto& entity : snap.entities) {
        bool isPlayer = (entity.name == snap.playerName);
        bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);
        bool isCorpse = (entity.npc_type == 2 || entity.npc_type == 3);
        if (!isCorpse && entity.name.find("corpse") != std::string::npos)
            isCorpse = true;

        EQT::Graphics::EntityAppearance appearance;
        appearance.face = entity.face;
        appearance.haircolor = entity.haircolor;
        appearance.hairstyle = entity.hairstyle;
        appearance.beardcolor = entity.beardcolor;
        appearance.beard = entity.beard;
        appearance.texture = entity.equip_chest2;
        appearance.helm = entity.helm;
        for (int i = 0; i < 9; i++) {
            appearance.equipment[i] = entity.equipment[i];
            appearance.equipment_tint[i] = entity.equipment_tint[i];
        }

        bool ok = m_renderer->registerEntity(entity.spawn_id, entity.race_id, entity.name,
                                   entity.x, entity.y, entity.z, entity.heading,
                                   isPlayer, entity.gender, appearance, isNPC, isCorpse, entity.size,
                                   entity.level);
        if (ok) registered++; else failed++;

        if (isPlayer) {
            m_renderer->setPlayerSpawnId(snap.playerSpawnId);
            if (entity.light > 0)
                m_renderer->setEntityLight(entity.spawn_id, entity.light);
            m_renderer->updatePlayerAppearance(entity.race_id, entity.gender, appearance);
        } else if (entity.light > 0) {
            m_renderer->setEntityLight(entity.spawn_id, entity.light);
        }
    }

    LOG_INFO(MOD_GRAPHICS, "loadZoneGraphics: {} registered, {} failed", registered, failed);

    // 6. Doors from snapshot
    for (const auto& door : snap.doors) {
        m_renderer->registerDoor(door.doorId, door.name, door.x, door.y, door.z,
                                 door.heading, door.incline, door.size, door.opentype,
                                 door.isOpen);
    }

    // 7. Camera
    m_renderer->setCameraMode(EQT::Graphics::IrrlichtRenderer::CameraMode::Follow);
    float heading512 = snap.playerHeading * 512.0f / 360.0f;
    m_renderer->setPlayerPosition(snap.playerX, snap.playerY, snap.playerZ, heading512);

    // 8. Hotbar
    setupHotbarCallback();

    m_eqClient->SetPlayerGraphicsEntityPending(true);

    if (m_renderer->isProgressiveLoadingActive()) {
        LOG_INFO(MOD_GRAPHICS, "loadZoneGraphics: automatic mode — loading screen remains visible");
    } else {
        // D21b: Signal game thread to finalize
        if (m_renderer) {
            std::lock_guard<std::mutex> lock(m_zoneLoadMutex);
            m_pendingBspTree = m_renderer->getZoneBspTree();
        }
        m_graphicsCompleteReady.store(true, std::memory_order_release);
        LOG_INFO(MOD_GRAPHICS, "loadZoneGraphics: instant scene ready");
    }
}

void Application::setupHotbarCallback() {
    // WindowManager removed — hotbar changed callback not wired
    // TODO: Wire hotbar changed callback through new UI path
}

// U07b: publishHotbarSnapshot removed — HotbarModel::publishAllSlots() handles this

void Application::setupRDPAudio([[maybe_unused]] void* rdpServerPtr) {
#if defined(WITH_RDP) && defined(WITH_AUDIO)
    if (!m_eqClient) return;
    auto* audioMgr = m_eqClient->GetAudioManager();
    if (!audioMgr) {
        LOG_WARN(MOD_AUDIO, "Cannot setup RDP audio - audio manager not initialized");
        return;
    }
    if (!rdpServerPtr) {
        LOG_WARN(MOD_AUDIO, "Cannot setup RDP audio - RDP server not available");
        return;
    }

    auto* rdpServer = static_cast<EQT::Graphics::RDPServer*>(rdpServerPtr);
    if (audioMgr->enableLoopbackMode()) {
        audioMgr->setAudioOutputCallback(
            [rdpServer](const int16_t* samples, size_t count,
                        uint32_t sampleRate, uint8_t channels) {
                if (rdpServer && rdpServer->isRunning()) {
                    rdpServer->sendAudioSamples(samples, count, sampleRate, channels);
                }
            }
        );
        LOG_INFO(MOD_AUDIO, "RDP audio streaming enabled (loopback mode)");
    } else {
        LOG_WARN(MOD_AUDIO, "Failed to enable loopback mode for RDP audio streaming");
    }
#endif
}
#endif

// Application::syncGameStateFromClient() was deleted (T104) -- its only
// remaining body was a call to syncCombatState() (also deleted, see below);
// once that was gone there was nothing left to sync from this hook. This
// closes the state-layer poll sweep started at T39 (see application.h's
// "Synchronization" section for the full history).

// Application::syncCombatState() was deleted (T104) -- CombatManager remains
// authoritative; its target id/auto-attack flag now publish
// TargetChanged/AutoAttackChanged bridge events from their sole choke points
// (CombatManager::SetTarget/ClearTarget/EnableAutoAttack/DisableAutoAttack,
// combat.cpp), picked up by GameStateSyncBridge into GameState::combat() --
// see game_state_sync_bridge.cpp's TargetChanged/AutoAttackChanged cases and
// refactor/T104_combat_poll_to_bridge.md's Review for the reconstructibility
// argument (including how target-HP-while-still-targeted is covered without
// a new event, via the pre-existing ZoneProcessMobHealth() re-publish in
// eq_entities.cpp).

// Application::syncNPCInteractionState() was deleted (T49) -- vendor,
// banker, and trainer window state (m_vendor_npc_id/m_banker_npc_id/
// m_trainer_npc_id and their names/sell rate) are now fed into
// m_gameState->player() by GameStateSyncBridge::applyEvent() from the six
// window-open/close bridge events (VendorWindowOpened/Closed,
// BankWindowOpened/Closed, TrainerWindowOpened/Closed), all of which are
// published from eq_merchant.cpp at exactly the same six mutation sites this
// poll used to read from -- see game_state_sync_bridge.cpp for the mapping.

void Application::updateLoadingProgress() {
    // Loading progress is updated by EverQuest client callbacks directly
    // This method is a placeholder for future centralized loading state management
}

void Application::connectRendererCallbacks() {
    if (!m_gameMode || !m_dispatcher) {
        return;
    }

    auto* renderer = m_gameMode->getRenderer();
    if (!renderer) {
        return;
    }

    // Check if this is a graphical renderer with callbacks
    auto* graphicalRenderer = dynamic_cast<output::GraphicalRenderer*>(renderer);
    if (!graphicalRenderer) {
        return;
    }

    LOG_DEBUG(MOD_MAIN, "Connecting renderer callbacks to action dispatcher");

    // Connect spell gem cast callback
    graphicalRenderer->setSpellGemCastCallback([this](uint8_t gemSlot) {
        if (m_dispatcher) {
            m_dispatcher->castSpell(gemSlot);
        }
    });

    // Connect target selection callback
    graphicalRenderer->setTargetCallback([this](uint16_t spawnId) {
        if (m_dispatcher) {
            m_dispatcher->targetEntity(spawnId);
        }
    });

    // Connect door interaction callback
    graphicalRenderer->setDoorInteractCallback([this](uint8_t doorId) {
        if (m_actionHandler) {
            m_actionHandler->clickDoor(doorId);
        }
    });

    // Connect chat submit callback
    graphicalRenderer->setChatSubmitCallback([this](const std::string& text) {
        if (m_commandProcessor && !text.empty()) {
            if (text[0] == '/') {
                // Process as command
                m_commandProcessor->processCommand(text);
            } else {
                // Send as chat
                if (m_dispatcher) {
                    m_dispatcher->sendChatMessage(action::ChatChannel::Say, text);
                }
            }
        }
    });

    // Connect pet command callback
    graphicalRenderer->setPetCommandCallback([this](uint8_t command, uint16_t targetId) {
        if (m_dispatcher) {
            m_dispatcher->sendPetCommand(command, targetId);
        }
    });

    // Connect inventory action callback
    graphicalRenderer->setInventoryActionCallback([this](int16_t slotId, uint8_t action) {
        // Handle inventory actions through the action handler
        if (m_actionHandler && action == 0) {  // 0 = use/equip
            // For now just log - actual item use would go through EQ client
            LOG_DEBUG(MOD_MAIN, "Inventory action: slot={}, action={}", slotId, action);
        }
    });

    LOG_INFO(MOD_MAIN, "Renderer callbacks connected");
}

// ========== Static Helpers ==========

ApplicationConfig Application::parseArguments(int argc, char* argv[]) {
    ApplicationConfig config;

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
        } else if (arg == "--help" || arg == "-h") {
            config.showHelp = true;
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  -d, --debug <level>      Set debug level (0-6)\n";
            std::cout << "  -c, --config <file>      Set config file (default: willeq.json)\n";
            std::cout << "  -np, --no-pathfinding    Disable navmesh pathfinding\n";
            std::cout << "  --record-session <file>  Record decoded session packets to <file> for later replay\n";
            std::cout << "  --script <file>          Run a scripted command tour ('wait N' + slash-command lines)\n";
            std::cout << "  --dump-frames-dir <dir>  Video-capture evidence tool: write numbered PNG frames to <dir> (needs --dump-every)\n";
            std::cout << "  --dump-every <N>         Video-capture: dump every Nth rendered frame\n";
            std::cout << "  --dump-max-frames <N>    Video-capture: stop after N frames written (default 600)\n";
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
            std::cout << "  --audio-volume <0-100>   Master volume (default: 100)\n";
            std::cout << "  --music-volume <0-100>   Music volume (default: 50)\n";
            std::cout << "  --effects-volume <0-100> Sound effects volume (default: 100)\n";
            std::cout << "  --soundfont <path>       Path to SoundFont for MIDI playback\n";
#endif
            std::cout << "  --headless               Run in headless interactive mode\n";
            std::cout << "  --automated              Run in automated/bot mode\n";
            std::cout << "  --log-level=LEVEL        Set log level (NONE, FATAL, ERROR, WARN, INFO, DEBUG, TRACE)\n";
            std::cout << "                           If both --log-level= and -d <n> are given, --log-level=\n";
            std::cout << "                           wins (a one-line notice is logged when they disagree)\n";
            std::cout << "  --log-module=MOD:LEVEL   Set per-module log level (e.g., NET:DEBUG, GRAPHICS:TRACE)\n";
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

    return config;
}

bool Application::loadConfigFile(const std::string& configFile, ApplicationConfig& config) {
    try {
        EQT::PerformanceMetrics::instance().startTimer("Config Loading", EQT::MetricCategory::Startup);

        auto jsonConfig = EQ::JsonConfigFile::Load(configFile);
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
                hotkeyMgr.loadFromFile("config/hotkeys.json");
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
                hotkeyMgr.loadFromFile("config/hotkeys.json");
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
                    LOG_INFO(MOD_AUDIO, "Vendor music from config: {}", config.audioVendorMusic);
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
