#include "client/application.h"
#include <json/json.h>  // Must be before eq.h → logging.h so JSONCPP_VERSION_STRING is defined
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/combat.h"
#include "client/mode/automated_mode.h"
#include "client/mode/headless_mode.h"
#include "client/mode/graphical_mode.h"
#include "client/spell/spell_manager.h"
#include "client/spell/spell_constants.h"
#include "client/output/graphical_renderer.h"
#include "common/util/json_config.h"
#include "common/logging.h"
#include "common/performance_metrics.h"

#include <iostream>
#include <thread>
#include <algorithm>
#include <sys/stat.h>

#ifndef _WIN32
#include <signal.h>
#endif

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/input/hotkey_manager.h"
#include "client/input/graphics_input_handler.h"
#include "client/bridge/irrlicht_bridge.h"
#include "client/bridge/console_bridge.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/state/event_bus.h"
#include "client/hotbar/hotbar_model.h"
#endif

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#endif

namespace eqt {

// ========== Signal Handlers ==========

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
#ifdef EQT_HAS_GRAPHICS
    signal(SIGHUP, HandleSigHup);
#endif
    LOG_DEBUG(MOD_MAIN, "Signal handlers registered (SIGUSR1=log+, SIGUSR2=log-, SIGHUP=hotkey reload)");
#endif

    // Create game state
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

#ifdef EQT_HAS_GRAPHICS
        if (!config.eqClientPath.empty()) {
            m_eqClient->SetEQClientPath(config.eqClientPath);
        }
        if (!config.regionMapsPath.empty()) {
            m_eqClient->SetRegionMapsPath(config.regionMapsPath);
        }
        m_eqClient->SetConfigPath(config.configFile);
#endif

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
    m_actionHandler = std::make_unique<EqActionHandler>(*m_eqClient);
    LOG_DEBUG(MOD_MAIN, "Action handler created");

    // Create action dispatcher
    m_dispatcher = std::make_unique<action::ActionDispatcher>(*m_gameState);
    m_dispatcher->setActionHandler(m_actionHandler.get());
    LOG_DEBUG(MOD_MAIN, "Action dispatcher created");

    // Create command processor
    m_commandProcessor = std::make_unique<action::CommandProcessor>(*m_gameState, *m_dispatcher);
    LOG_DEBUG(MOD_MAIN, "Command processor created");

    // Create game mode
    mode::ModeConfig modeConfig;
    modeConfig.width = config.displayWidth;
    modeConfig.height = config.displayHeight;
    modeConfig.fullscreen = config.fullscreen;
    modeConfig.eqClientPath = config.eqClientPath;
    modeConfig.regionMapsPath = config.regionMapsPath;

    // Game mode created with Software initially; updated to GPU after constrained config resolves
    auto rendererType = mode::GraphicalRendererType::IrrlichtSoftware;
    if (config.rendererBackend == 1 || config.rendererBackend == 2) {
        rendererType = mode::GraphicalRendererType::IrrlichtGPU;
    }
    LOG_INFO(MOD_MAIN, "Creating game mode with renderer type: {}",
        rendererType == mode::GraphicalRendererType::IrrlichtGPU ? "GPU" : "Software");
    m_gameMode = mode::createMode(config.operatingMode, rendererType);
    if (!m_gameMode) {
        LOG_FATAL(MOD_MAIN, "Failed to create game mode");
        return false;
    }

    if (!m_gameMode->initialize(*m_gameState, modeConfig)) {
        LOG_FATAL(MOD_MAIN, "Failed to initialize game mode");
        return false;
    }
    LOG_DEBUG(MOD_MAIN, "Game mode initialized: {}",
        config.operatingMode == mode::OperatingMode::GraphicalInteractive ? "graphical" :
        config.operatingMode == mode::OperatingMode::HeadlessInteractive ? "headless" : "automated");

    // Create input action bridge
    m_inputBridge = std::make_unique<action::InputActionBridge>(*m_gameState, *m_dispatcher);
    m_inputBridge->setCommandProcessor(m_commandProcessor.get());
    LOG_DEBUG(MOD_MAIN, "Input action bridge created");

    // Connect input handler if available
    auto* inputHandler = m_gameMode->getInputHandler();
    if (inputHandler) {
        m_inputBridge->setInputHandler(inputHandler);
        LOG_DEBUG(MOD_MAIN, "Input handler connected to bridge");
    }

    // Connect command processor output to renderer
    auto* renderer = m_gameMode->getRenderer();
    if (renderer) {
        m_commandProcessor->setOutputRenderer(renderer);
        LOG_DEBUG(MOD_MAIN, "Command processor connected to renderer");
    }

    // Connect renderer callbacks to action dispatcher
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
                    builtConfig.loadJsonOverrides(config.constrainedPreset, "config/constrained_presets.json");
                } else {
                    auto preset = EQT::Graphics::ConstrainedRendererConfig::parsePreset(config.constrainedPreset);
                    LOG_DEBUG(MOD_GRAPHICS, "Using named preset '{}' -> {}",
                              config.constrainedPreset,
                              EQT::Graphics::ConstrainedRendererConfig::presetName(preset));
                    builtConfig = EQT::Graphics::ConstrainedRendererConfig::fromPreset(preset);
                    builtConfig.loadJsonOverrides(config.constrainedPreset, "config/constrained_presets.json");
                }
            } else {
                // Default to OrangePi preset
                LOG_DEBUG(MOD_GRAPHICS, "No preset specified, defaulting to OrangePi");
                builtConfig = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
                    EQT::Graphics::ConstrainedRenderingPreset::OrangePi);
                builtConfig.loadJsonOverrides("orangepi", "config/constrained_presets.json");
            }

            // CLI override: --renderer always wins over preset
            if (config.rendererBackend >= 0) {
                builtConfig.renderingBackend = static_cast<EQT::Graphics::RenderingBackend>(config.rendererBackend);
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: renderer backend -> {}",
                          EQT::Graphics::backendName(builtConfig.renderingBackend));
            }
            // CLI override: --drm sets DRM mode
            if (config.useDRM) {
                builtConfig.useDRM = true;
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: DRM mode enabled");
            }
            // CLI override: --atlas-path
            if (!config.atlasPath.empty()) {
                builtConfig.atlasPath = config.atlasPath;
                LOG_DEBUG(MOD_GRAPHICS, "CLI override: atlas path -> '{}'", config.atlasPath);
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

            // Runtime validation: GLES2 backend requires EQT_HAS_GLES2
#ifndef EQT_HAS_GLES2
            if (builtConfig.renderingBackend == EQT::Graphics::RenderingBackend::GLES2) {
                LOG_WARN(MOD_GRAPHICS, "GLES2 backend requested but EQT_HAS_GLES2 not defined; falling back to OpenGL");
                builtConfig.renderingBackend = EQT::Graphics::RenderingBackend::OpenGL;
            }
#endif

            // Load debug overrides (skip* flags from config/debug.json)
            builtConfig.loadDebugOverrides("config/debug.json");

            // S02: Validate preset values
            {
                std::string presetErrors;
                if (!builtConfig.validate(presetErrors)) {
                    LOG_FATAL(MOD_GRAPHICS, "Invalid constrained renderer config:\n{}", presetErrors);
                    return false;
                }
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

            // Update mode if preset resolved to GPU backend and CLI didn't override
            if (builtConfig.renderingBackend != EQT::Graphics::RenderingBackend::Software) {
                m_config.rendererBackend = static_cast<int>(builtConfig.renderingBackend);
            }

            LOG_INFO(MOD_GRAPHICS, "Rendering config: backend={}, DRM={}",
                     EQT::Graphics::backendName(builtConfig.renderingBackend),
                     builtConfig.useDRM ? "yes" : "no");
        }

        LOG_DEBUG(MOD_GRAPHICS, "Initializing graphics...");
        EQT::PerformanceMetrics::instance().startTimer("Graphics Init", EQT::MetricCategory::Startup);

        // D20b5: Application creates renderer + bridge, passes to EverQuest
        LOG_DEBUG(MOD_GRAPHICS, "Creating IrrlichtRenderer and IrrlichtBridge...");
        m_renderer = std::make_unique<EQT::Graphics::IrrlichtRenderer>();
        m_irrlichtBridge = std::make_unique<eqt::bridge::IrrlichtBridge>();
        m_irrlichtBridge->setRenderer(m_renderer.get());
        m_renderer->setBridge(m_irrlichtBridge.get());
        m_bridge = m_irrlichtBridge.get();
        LOG_DEBUG(MOD_GRAPHICS, "Renderer and bridge created, calling InitGraphics({}x{})...",
                  config.displayWidth, config.displayHeight);

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
        m_bridge = m_consoleBridge.get();
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
    // Destroy bridge before renderer (bridge holds renderer pointer)
    m_bridge = nullptr;
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

#ifdef EQT_HAS_GRAPHICS
                if (m_config.graphicsEnabled && m_graphicsInitialized && m_eqClient) {
                    m_eqClient->LoadHotbarConfig();
                    // U07b: HotbarModel publishes its own events
                    if (m_eqClient->GetHotbarModel()) {
                        m_eqClient->GetHotbarModel()->publishAllSlots();
                    }
                }
#endif
            }

            if (!isConnected && m_fullyConnected) {
                m_fullyConnected = false;
                if (m_gameState) m_gameState->world().setZoneConnected(false);
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

    // Process input through the action bridge — but NOT when graphics handle input.
    // IrrlichtRenderer owns all input via evdev/X11 and has its own chat-focus gating.
    // Running the bridge in parallel would bypass that gating (ConsoleInputHandler reads
    // stdin, which may receive duplicate keystrokes if K_OFF isn't active).
    if (m_inputBridge && !m_graphicsInitialized) {
        m_inputBridge->update(deltaTime);
    }
}

void Application::updateGameState(float deltaTime) {
    // D21a: Game-side tick (spell/buff updates + intent processing)
    if (m_eqClient && m_graphicsInitialized) {
        m_eqClient->GameTick(deltaTime);
    }

    // Update movement in EverQuest
    if (m_eqClient) {
        m_eqClient->UpdateMovement();
    }

    // Sync game state from client
    syncGameStateFromClient();

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

void Application::syncGameStateFromClient() {
    if (!m_eqClient || !m_gameState) {
        return;
    }

    // Phase 7 Migration Note:
    // Player position, stats, and zone info are now synced directly from EQ class
    // packet handlers (Phase 7.1-7.3), so polling is no longer needed for those.
    // The sub-syncs below (pet, NPC interaction, spell) haven't been migrated yet
    // and still use polling for now.

    // Sync subsystems (not yet migrated to direct sync)
    syncPetState();
    syncNPCInteractionState();
    syncSpellState();
}

void Application::syncPetState() {
    if (!m_eqClient || !m_gameState) {
        return;
    }

    uint16_t petSpawnId = m_eqClient->GetPetSpawnId();

    // Check for pet creation/removal
    if (petSpawnId != m_lastPetSpawnId) {
        if (petSpawnId != 0) {
            // Pet created
            std::string petName = m_eqClient->GetPetName();
            uint8_t petLevel = m_eqClient->GetPetLevel();
            m_gameState->pet().setPet(petSpawnId, petName, petLevel);
            LOG_DEBUG(MOD_MAIN, "Pet state synced: {} (Level {})", petName, petLevel);
        } else {
            // Pet removed
            m_gameState->pet().clearPet();
            LOG_DEBUG(MOD_MAIN, "Pet cleared");
        }
        m_lastPetSpawnId = petSpawnId;
        m_lastPetHpPercent = 100;
        m_lastPetManaPercent = 100;
    }

    // Sync pet stats if we have a pet
    if (petSpawnId != 0) {
        uint8_t hpPercent = m_eqClient->GetPetHpPercent();
        // Note: GetPetManaPercent doesn't exist, we'd need to track separately
        // For now just sync HP
        if (hpPercent != m_lastPetHpPercent) {
            m_gameState->pet().updatePetStats(hpPercent, m_lastPetManaPercent);
            m_lastPetHpPercent = hpPercent;
        }

        // Sync button states
        for (uint8_t i = 0; i < 10; ++i) {
            bool state = m_eqClient->GetPetButtonState(static_cast<EQT::PetButton>(i));
            if (state != m_gameState->pet().getButtonState(i)) {
                m_gameState->pet().setButtonState(i, state);
            }
        }
    }
}

void Application::syncNPCInteractionState() {
    if (!m_eqClient || !m_gameState) {
        return;
    }

    // Sync vendor state
    uint16_t vendorNpcId = m_eqClient->GetVendorNpcId();
    if (vendorNpcId != m_lastVendorNpcId) {
        if (vendorNpcId != 0) {
            // Vendor window opened - get name from entity if available
            std::string vendorName = "Vendor";
            const auto& entities = m_eqClient->GetEntities();
            auto it = entities.find(vendorNpcId);
            if (it != entities.end()) {
                vendorName = it->second.name;
            }
            m_gameState->player().setVendor(vendorNpcId, 1.0f, vendorName);
            LOG_DEBUG(MOD_MAIN, "Vendor opened: {}", vendorName);
        } else {
            m_gameState->player().clearVendor();
            LOG_DEBUG(MOD_MAIN, "Vendor closed");
        }
        m_lastVendorNpcId = vendorNpcId;
    }

    // Sync banker state
    uint16_t bankerNpcId = m_eqClient->GetBankerNpcId();
    if (bankerNpcId != m_lastBankerNpcId) {
        if (bankerNpcId != 0) {
            m_gameState->player().setBanker(bankerNpcId);
            LOG_DEBUG(MOD_MAIN, "Bank opened");
        } else {
            m_gameState->player().clearBanker();
            LOG_DEBUG(MOD_MAIN, "Bank closed");
        }
        m_lastBankerNpcId = bankerNpcId;
    }

    // Sync trainer state
    uint16_t trainerNpcId = m_eqClient->GetTrainerNpcId();
    if (trainerNpcId != m_lastTrainerNpcId) {
        if (trainerNpcId != 0) {
            // Get trainer name from entity if available
            std::string trainerName = "Trainer";
            const auto& entities = m_eqClient->GetEntities();
            auto it = entities.find(trainerNpcId);
            if (it != entities.end()) {
                trainerName = it->second.name;
            }
            m_gameState->player().setTrainer(trainerNpcId, trainerName);
            LOG_DEBUG(MOD_MAIN, "Trainer opened: {}", trainerName);
        } else {
            m_gameState->player().clearTrainer();
            LOG_DEBUG(MOD_MAIN, "Trainer closed");
        }
        m_lastTrainerNpcId = trainerNpcId;
    }
}

void Application::syncSpellState() {
    if (!m_eqClient || !m_gameState) {
        return;
    }

    auto* spellMgr = m_eqClient->GetSpellManager();
    if (!spellMgr) {
        return;
    }

    // Sync casting state
    bool isCasting = spellMgr->isCasting();
    uint32_t castingSpellId = spellMgr->getCurrentSpellId();

    if (isCasting != m_lastIsCasting || castingSpellId != m_lastCastingSpellId) {
        if (isCasting) {
            uint16_t targetId = spellMgr->getCurrentTargetId();
            uint32_t castTimeMs = static_cast<uint32_t>(
                spellMgr->getCastProgress() > 0 ?
                spellMgr->getCastTimeRemaining() / (1.0f - spellMgr->getCastProgress()) :
                spellMgr->getCastTimeRemaining());
            m_gameState->spells().setCasting(true, castingSpellId, targetId, castTimeMs);
        } else {
            m_gameState->spells().clearCasting();
        }
        m_lastIsCasting = isCasting;
        m_lastCastingSpellId = castingSpellId;
    }

    // Update cast progress
    if (isCasting) {
        m_gameState->spells().updateCastProgress(spellMgr->getCastTimeRemaining());
    }

    // Sync spell gems
    for (uint8_t slot = 0; slot < 8; ++slot) {
        uint32_t spellId = spellMgr->getMemorizedSpell(slot);
        EQ::GemState gemState = spellMgr->getGemState(slot);

        // Convert EQ::GemState to state::SpellGemState
        state::SpellGemState stateGemState = state::SpellGemState::Empty;
        switch (gemState) {
            case EQ::GemState::Empty:
                stateGemState = state::SpellGemState::Empty;
                break;
            case EQ::GemState::Ready:
                stateGemState = state::SpellGemState::Ready;
                break;
            case EQ::GemState::Casting:
                stateGemState = state::SpellGemState::Casting;
                break;
            case EQ::GemState::Refresh:
                stateGemState = state::SpellGemState::Refresh;
                break;
            case EQ::GemState::MemorizeProgress:
                stateGemState = state::SpellGemState::MemorizeProgress;
                break;
        }

        m_gameState->spells().setGem(slot, spellId, stateGemState);

        // Update cooldown if refreshing
        if (gemState == EQ::GemState::Refresh) {
            uint32_t remaining = spellMgr->getGemCooldownRemaining(slot);
            float progress = spellMgr->getGemCooldownProgress(slot);
            uint32_t total = progress > 0 ? static_cast<uint32_t>(remaining / (1.0f - progress)) : remaining;
            m_gameState->spells().setGemCooldown(slot, remaining, total);
        }
    }
}

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
                if (backend == "software") {
                    config.rendererBackend = 0;
                } else if (backend == "opengl") {
                    config.rendererBackend = 1;
                } else if (backend == "gles2") {
                    config.rendererBackend = 2;
                } else {
                    std::cerr << "Unknown renderer: " << backend << " (use: software, opengl, gles2)\n";
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
        } else if (arg == "--atlas-path") {
            if (i + 1 < argc) {
                config.atlasPath = argv[++i];
            }
        } else if (arg == "--drm") {
            config.useDRM = true;
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
        } else if (arg == "--help" || arg == "-h") {
            config.showHelp = true;
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  -d, --debug <level>      Set debug level (0-6)\n";
            std::cout << "  -c, --config <file>      Set config file (default: willeq.json)\n";
            std::cout << "  -np, --no-pathfinding    Disable navmesh pathfinding\n";
#ifdef EQT_HAS_GRAPHICS
            std::cout << "  -ng, --no-graphics       Disable graphical rendering\n";
            std::cout << "  -r, --resolution <W> <H> Set graphics resolution (default: 800 600)\n";
            std::cout << "  --renderer <backend>     Rendering backend: software, opengl, gles2\n";
            std::cout << "  --constrained <preset|NxNxN>  Rendering preset (default: orangepi; voodoo1, voodoo2, tnt, orangepi, or 128x32x4)\n";
            std::cout << "  --atlas-path <dir>       Directory containing .atlas texture atlas files\n";
            std::cout << "  --drm                    Use DRM/KMS display (no X11 required)\n";
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
