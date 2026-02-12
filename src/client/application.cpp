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
#include "common/event/event_loop.h"
#include "common/util/json_config.h"
#include "common/logging.h"
#include "common/performance_metrics.h"

#include <iostream>
#include <thread>
#include <algorithm>

#ifndef _WIN32
#include <signal.h>
#endif

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/ui/ui_settings.h"
#include "client/input/hotkey_manager.h"
#include "client/input/graphics_input_handler.h"
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
    LOG_INFO(MOD_MAIN, "Config: host={}, server={}, character={}",
        config.host, config.server, config.character);

    // Register signal handlers
#ifndef _WIN32
    signal(SIGUSR1, HandleSigUsr1);
    signal(SIGUSR2, HandleSigUsr2);
#ifdef EQT_HAS_GRAPHICS
    signal(SIGHUP, HandleSigHup);
#endif
#endif

    // Create game state
    m_gameState = std::make_unique<state::GameState>();
    LOG_DEBUG(MOD_MAIN, "Game state created");

    // Create EverQuest client
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
        LOG_ERROR(MOD_MAIN, "Failed to create EverQuest client: {}", e.what());
        EQT::PerformanceMetrics::instance().stopTimer("Client Creation");
        return false;
    }
    EQT::PerformanceMetrics::instance().stopTimer("Client Creation");

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

    LOG_INFO(MOD_MAIN, "Creating game mode with renderer type: {}",
        config.graphicalRendererType == mode::GraphicalRendererType::IrrlichtGPU ? "OpenGL" : "Software");
    m_gameMode = mode::createMode(config.operatingMode, config.graphicalRendererType);
    if (!m_gameMode) {
        LOG_ERROR(MOD_MAIN, "Failed to create game mode");
        return false;
    }

    if (!m_gameMode->initialize(*m_gameState, modeConfig)) {
        LOG_ERROR(MOD_MAIN, "Failed to initialize game mode");
        return false;
    }
    LOG_DEBUG(MOD_MAIN, "Game mode initialized: {}",
        config.operatingMode == mode::OperatingMode::GraphicalInteractive ? "graphical" :
        config.operatingMode == mode::OperatingMode::HeadlessInteractive ? "headless" : "automated");

    // Create input action bridge
    m_inputBridge = std::make_unique<action::InputActionBridge>(*m_gameState, *m_dispatcher);
    m_inputBridge->setCommandProcessor(m_commandProcessor.get());

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

#ifdef EQT_HAS_GRAPHICS
    // Initialize graphics if graphical mode
    m_graphicsInitialized = false;
    if (config.graphicsEnabled && config.operatingMode == mode::OperatingMode::GraphicalInteractive) {
        if (!config.eqClientPath.empty()) {
            // Apply constrained rendering and OpenGL settings before init
            if (config.graphicalRendererType == mode::GraphicalRendererType::IrrlichtGPU) {
                m_eqClient->SetUseOpenGL(true);
            }
            if (config.useDRM) {
                m_eqClient->SetUseDRM(true);
            }
            if (!config.constrainedPreset.empty()) {
                auto preset = EQT::Graphics::ConstrainedRendererConfig::parsePreset(config.constrainedPreset);
                m_eqClient->SetConstrainedPreset(preset);
            }

            LOG_DEBUG(MOD_GRAPHICS, "Initializing graphics...");
            EQT::PerformanceMetrics::instance().startTimer("Graphics Init", EQT::MetricCategory::Startup);
            if (m_eqClient->InitGraphics(config.displayWidth, config.displayHeight)) {
                m_graphicsInitialized = true;
                LOG_INFO(MOD_GRAPHICS, "Graphics initialized");

                // Set initial loading state
                auto* eqRenderer = m_eqClient->GetRenderer();
                if (eqRenderer) {
                    eqRenderer->setLoadingTitle(L"EverQuest");
                    eqRenderer->setLoadingProgress(0.0f, L"Connecting to login server...");

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

#ifdef WITH_RDP
                    // Initialize and start RDP server if enabled
                    if (config.rdpEnabled) {
                        LOG_INFO(MOD_GRAPHICS, "Initializing RDP server on port {}...", config.rdpPort);
                        if (eqRenderer->initRDP(config.rdpPort)) {
                            if (eqRenderer->startRDPServer()) {
                                LOG_INFO(MOD_GRAPHICS, "RDP server started on port {}", config.rdpPort);
                                m_eqClient->SetupRDPAudio();
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
                m_config.graphicsEnabled = false;
            }
            EQT::PerformanceMetrics::instance().stopTimer("Graphics Init");
        } else {
            LOG_INFO(MOD_GRAPHICS, "No eq_client_path - running headless");
            m_config.graphicsEnabled = false;
        }
    }
#endif

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
    if (m_graphicsInitialized && m_eqClient) {
        auto* renderer = m_eqClient->GetRenderer();
        if (renderer && renderer->isRDPRunning()) {
            LOG_INFO(MOD_GRAPHICS, "Stopping RDP server...");
            renderer->stopRDPServer();
        }
    }
#endif
    if (m_eqClient) {
        m_eqClient->ShutdownGraphics();
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

void Application::mainLoop() {
    static int loop_counter = 0;
    LOG_TRACE(MOD_MAIN, "Entering main loop");

    while (m_running.load()) {
        try {
            loop_counter++;

            // Log zone change activity
            bool zone_change_happening = m_eqClient && m_eqClient->IsZoneChangeApproved();
            if (loop_counter % 100 == 0 || zone_change_happening) {
                LOG_TRACE(MOD_MAIN, "Main loop iteration {} running={} zone_change={}",
                          loop_counter, m_running.load(), zone_change_happening);
            }

            // Process network events with performance tracking
            auto eventLoopStart = std::chrono::steady_clock::now();
            processNetworkEvents();
            auto eventLoopEnd = std::chrono::steady_clock::now();
            auto eventLoopMs = std::chrono::duration_cast<std::chrono::milliseconds>(eventLoopEnd - eventLoopStart).count();
            if (eventLoopMs > 50) {
                LOG_WARN(MOD_MAIN, "PERF: EventLoop::Process() took {} ms", eventLoopMs);
            }

            // Check for zone connection
            bool isConnected = m_eqClient && m_eqClient->IsFullyZonedIn();

            if (isConnected && !m_fullyConnected) {
                LOG_INFO(MOD_MAIN, "Fully connected to zone!");
                m_fullyConnected = true;

#ifdef EQT_HAS_GRAPHICS
                // NOTE: Zone graphics are now loaded automatically via the LoadingPhase system.
                // LoadZoneGraphics() is called from OnGameStateComplete() when the game state is ready.
                // We only need to load the hotbar config here.
                if (m_config.graphicsEnabled && m_graphicsInitialized && m_eqClient) {
                    m_eqClient->LoadHotbarConfig();
                }
#endif
            }

            // Calculate delta time
            auto now = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(now - m_lastUpdate).count();

            // Process input and update game state at ~60 fps
            if (deltaTime >= 1.0f / 60.0f) {
                processInput(deltaTime);

                // Update movement with performance tracking
                auto movementStart = std::chrono::steady_clock::now();
                updateGameState(deltaTime);
                auto movementEnd = std::chrono::steady_clock::now();
                auto movementMs = std::chrono::duration_cast<std::chrono::milliseconds>(movementEnd - movementStart).count();
                if (movementMs > 50) {
                    LOG_WARN(MOD_MAIN, "PERF: UpdateMovement() took {} ms", movementMs);
                }

                m_lastUpdate = now;
            }

            // Render
            render(deltaTime);

            // Check if mode requested quit
            if (m_gameMode && m_gameMode->isQuitRequested()) {
                m_running.store(false);
            }

            // Small sleep to prevent CPU spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        } catch (const std::exception& e) {
            LOG_ERROR(MOD_MAIN, "Exception in main loop: {}", e.what());
        }
    }

    LOG_TRACE(MOD_MAIN, "Exited main loop running={} loop_counter={}", m_running.load(), loop_counter);
}

void Application::processNetworkEvents() {
    EQ::EventLoop::Get().Process();
}

void Application::processInput(float deltaTime) {
    // Update game mode (handles internal input processing)
    if (m_gameMode) {
        m_gameMode->update(deltaTime);
    }

    // Process input through the action bridge
    if (m_inputBridge) {
        m_inputBridge->update(deltaTime);
    }
}

void Application::updateGameState(float deltaTime) {
    // Update movement in EverQuest
    if (m_eqClient) {
        m_eqClient->UpdateMovement();
    }

    // Sync game state from client
    syncGameStateFromClient();

    // Update loading progress
    updateLoadingProgress();
}

void Application::render(float deltaTime) {
#ifdef EQT_HAS_GRAPHICS
    if (m_graphicsInitialized && m_eqClient) {
        auto now = std::chrono::steady_clock::now();
        float gfxDeltaTime = std::chrono::duration<float>(now - m_lastGraphicsUpdate).count();

        if (gfxDeltaTime >= 1.0f / 60.0f) {
            if (!m_eqClient->UpdateGraphics(gfxDeltaTime)) {
                // Graphics window closed
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
        } else if (arg == "--opengl" || arg == "--gpu") {
            config.graphicalRendererType = mode::GraphicalRendererType::IrrlichtGPU;
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
        } else if (arg == "--drm") {
            config.useDRM = true;
        } else if (arg == "--rdp" || arg == "--enable-rdp") {
            config.rdpEnabled = true;
        } else if (arg == "--rdp-port") {
            if (i + 1 < argc) {
                config.rdpPort = static_cast<uint16_t>(std::atoi(argv[++i]));
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
            std::cout << "  --opengl, --gpu          Use OpenGL renderer (default: software)\n";
            std::cout << "  --constrained <preset>   Enable constrained rendering mode (voodoo1, voodoo2, tnt, orangepi)\n";
            std::cout << "  --drm                    Use DRM/KMS display (no X11 required)\n";
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
            // Load UI and hotkey settings for legacy config format
            eqt::ui::UISettings::instance().loadFromFile("config/ui_settings.json");
            {
                auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
                hotkeyMgr.resetToDefaults();
                hotkeyMgr.loadFromFile("config/hotkeys.json");
                hotkeyMgr.logConflicts();
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
            // Load UI settings from default file, then apply any overrides from main config
            {
                auto& uiSettings = eqt::ui::UISettings::instance();
                uiSettings.loadFromFile("config/ui_settings.json");
                if (handle.isMember("uiSettings")) {
                    LOG_INFO(MOD_UI, "Applying UI settings overrides from main config");
                    uiSettings.applyOverrides(handle["uiSettings"]);
                }
                if (handle.isMember("chatSettings")) {
                    LOG_INFO(MOD_UI, "Applying chat settings overrides from main config");
                    uiSettings.applyChatSettingsOverride(handle["chatSettings"]);
                }
            }

            // Load hotkey settings
            {
                auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
                hotkeyMgr.resetToDefaults();
                hotkeyMgr.loadFromFile("config/hotkeys.json");
                if (handle.isMember("hotkeys")) {
                    LOG_INFO(MOD_INPUT, "Applying hotkey overrides from main config");
                    hotkeyMgr.applyOverrides(handle["hotkeys"]);
                }
                hotkeyMgr.logConflicts();
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
