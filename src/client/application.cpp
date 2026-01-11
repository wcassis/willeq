#include "client/application.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/mode/automated_mode.h"
#include "client/mode/headless_mode.h"
#include "client/mode/graphical_mode.h"
#include "client/spell/spell_manager.h"
#include "client/spell/spell_constants.h"
#include "client/output/graphical_renderer.h"
#include "common/event/event_loop.h"
#include "common/util/json_config.h"
#include "common/logging.h"

#include <iostream>
#include <thread>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/ui_settings.h"
#endif

namespace eqt {

Application::Application() = default;

Application::~Application() {
    shutdown();
}

bool Application::initialize(const ApplicationConfig& config) {
    m_config = config;

    LOG_INFO(MOD_MAIN, "Initializing application...");
    LOG_INFO(MOD_MAIN, "Config: host={}, server={}, character={}",
        config.host, config.server, config.character);

    // Create game state
    m_gameState = std::make_unique<state::GameState>();
    LOG_DEBUG(MOD_MAIN, "Game state created");

    // Create EverQuest client
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

        LOG_DEBUG(MOD_MAIN, "EverQuest client created");

    } catch (const std::exception& e) {
        LOG_ERROR(MOD_MAIN, "Failed to create EverQuest client: {}", e.what());
        return false;
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

    m_gameMode = mode::createMode(config.operatingMode);
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
    if (config.graphicsEnabled && config.operatingMode == mode::OperatingMode::GraphicalInteractive) {
        if (!config.eqClientPath.empty()) {
            LOG_DEBUG(MOD_GRAPHICS, "Initializing graphics...");
            if (m_eqClient->InitGraphics(config.displayWidth, config.displayHeight)) {
                LOG_INFO(MOD_GRAPHICS, "Graphics initialized");

                // Set initial loading state
                auto* eqRenderer = m_eqClient->GetRenderer();
                if (eqRenderer) {
                    eqRenderer->setLoadingTitle(L"EverQuest");
                    eqRenderer->setLoadingProgress(0.0f, L"Connecting to login server...");
                }
            } else {
                LOG_WARN(MOD_GRAPHICS, "Failed to initialize graphics - running headless");
            }
        } else {
            LOG_INFO(MOD_GRAPHICS, "No eq_client_path - running headless");
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
    if (m_eqClient) {
        m_eqClient->ShutdownGraphics();
    }
#endif

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
    while (m_running.load()) {
        try {
            // Process network events
            processNetworkEvents();

            // Check for zone connection
            bool isConnected = m_eqClient && m_eqClient->IsFullyZonedIn();

            if (isConnected && !m_fullyConnected) {
                LOG_INFO(MOD_MAIN, "Fully connected to zone!");
                m_fullyConnected = true;

#ifdef EQT_HAS_GRAPHICS
                if (m_config.graphicsEnabled && m_eqClient) {
                    LOG_INFO(MOD_GRAPHICS, "Zone connected, loading zone graphics");
                    m_eqClient->OnZoneLoadedGraphics();
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
                updateGameState(deltaTime);
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
    if (m_config.graphicsEnabled && m_eqClient) {
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
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  -d, --debug <level>      Set debug level (0-3)\n";
            std::cout << "  -c, --config <file>      Set config file (default: willeq.json)\n";
            std::cout << "  -np, --no-pathfinding    Disable navmesh pathfinding\n";
            std::cout << "  -ng, --no-graphics       Disable graphical rendering (headless mode)\n";
            std::cout << "  --headless               Run in headless interactive mode\n";
            std::cout << "  --automated              Run in automated/bot mode\n";
            std::cout << "  -r, --resolution <W> <H> Set graphics resolution (default: 800 600)\n";
            std::cout << "  --log-level=LEVEL        Set log level\n";
            std::cout << "  --log-module=MOD:LEVEL   Set per-module log level\n";
            std::cout << "  -h, --help               Show this help message\n";
            // Note: caller should handle help flag and exit
        }
    }

    return config;
}

bool Application::loadConfigFile(const std::string& configFile, ApplicationConfig& config) {
    try {
        auto jsonConfig = EQ::JsonConfigFile::Load(configFile);
        auto handle = jsonConfig.RawHandle();

        // Handle both array format (legacy) and object format
        Json::Value clientConfig;
        if (handle.isArray() && handle.size() > 0) {
            clientConfig = handle[0];
        } else if (handle.isObject()) {
            if (handle.isMember("clients") && handle["clients"].isArray() && handle["clients"].size() > 0) {
                clientConfig = handle["clients"][0];
            } else {
                clientConfig = handle;
            }
        } else {
            LOG_ERROR(MOD_CONFIG, "Invalid config file format");
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

        // Parse mode settings from top-level config
        if (handle.isObject()) {
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

#ifdef EQT_HAS_GRAPHICS
            // Load UI settings
            eqt::ui::UISettings::instance().loadFromFile("config/ui_settings.json");
            if (handle.isMember("uiSettings")) {
                eqt::ui::UISettings::instance().applyOverrides(handle["uiSettings"]);
            }
#endif
        }

        config.configFile = configFile;
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR(MOD_CONFIG, "Failed to load config file '{}': {}", configFile, e.what());
        return false;
    }
}

} // namespace eqt
