#include "client/output/console_renderer.h"
#include "client/state/game_state.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <codecvt>
#include <locale>

namespace eqt {
namespace output {

namespace {
// ANSI color codes
const char* COLOR_RESET = "\033[0m";
const char* COLOR_RED = "\033[31m";
const char* COLOR_GREEN = "\033[32m";
const char* COLOR_YELLOW = "\033[33m";
const char* COLOR_BLUE = "\033[34m";
const char* COLOR_MAGENTA = "\033[35m";
const char* COLOR_CYAN = "\033[36m";
const char* COLOR_WHITE = "\033[37m";
const char* COLOR_BRIGHT_RED = "\033[91m";
const char* COLOR_BRIGHT_GREEN = "\033[92m";
const char* COLOR_BRIGHT_YELLOW = "\033[93m";
const char* COLOR_BRIGHT_BLUE = "\033[94m";
const char* COLOR_BRIGHT_MAGENTA = "\033[95m";
const char* COLOR_BRIGHT_CYAN = "\033[96m";
const char* COLOR_GRAY = "\033[90m";

// Helper to convert wstring to string
std::string wstringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(wstr);
}
}  // anonymous namespace

ConsoleRenderer::~ConsoleRenderer() {
    shutdown();
}

bool ConsoleRenderer::initialize(const RendererConfig& config) {
    m_verbose = config.verbose;
    m_showTimestamps = config.showTimestamps;
    m_colorOutput = config.colorOutput;
    m_startTime = std::chrono::steady_clock::now();
    m_running = true;

    std::cout << "\n=== WillEQ Console Mode ===" << std::endl;
    std::cout << "Type 'help' for available commands." << std::endl;
    std::cout << std::endl;

    return true;
}

void ConsoleRenderer::shutdown() {
    if (m_running) {
        disconnectFromGameState();
        m_running = false;
        std::cout << "\n=== Console renderer shutdown ===" << std::endl;
    }
}

bool ConsoleRenderer::update(float deltaTime) {
    m_timeSinceStatusUpdate += deltaTime;

    // Periodically print status line
    if (m_timeSinceStatusUpdate >= m_statusUpdateInterval && m_zoneReady) {
        printStatusLine();
        m_timeSinceStatusUpdate = 0.0f;
    }

    return m_running;
}

void ConsoleRenderer::connectToGameState(state::GameState& state) {
    m_eventBus = &state.events();

    // Subscribe to chat messages
    m_listenerHandles.push_back(
        m_eventBus->subscribe(state::GameEventType::ChatMessage,
            [this](const state::GameEvent& event) { handleEvent(event); }));

    // Subscribe to entity events (if verbose)
    m_listenerHandles.push_back(
        m_eventBus->subscribe(state::GameEventType::EntitySpawned,
            [this](const state::GameEvent& event) { handleEvent(event); }));
    m_listenerHandles.push_back(
        m_eventBus->subscribe(state::GameEventType::EntityDespawned,
            [this](const state::GameEvent& event) { handleEvent(event); }));

    // Subscribe to combat events
    m_listenerHandles.push_back(
        m_eventBus->subscribe(state::GameEventType::CombatEvent,
            [this](const state::GameEvent& event) { handleEvent(event); }));

    // Subscribe to zone events
    m_listenerHandles.push_back(
        m_eventBus->subscribe(state::GameEventType::ZoneChanged,
            [this](const state::GameEvent& event) { handleEvent(event); }));

    // Subscribe to target changed events
    m_listenerHandles.push_back(
        m_eventBus->subscribe(state::GameEventType::TargetChanged,
            [this](const state::GameEvent& event) { handleEvent(event); }));
}

void ConsoleRenderer::disconnectFromGameState() {
    if (m_eventBus) {
        for (auto handle : m_listenerHandles) {
            m_eventBus->unsubscribe(handle);
        }
        m_listenerHandles.clear();
        m_eventBus = nullptr;
    }
}

void ConsoleRenderer::handleEvent(const state::GameEvent& event) {
    std::lock_guard<std::mutex> lock(m_outputMutex);

    switch (event.type) {
        case state::GameEventType::ChatMessage: {
            const auto& data = std::get<state::ChatMessageData>(event.data);
            displayChatMessage(data.channelName, data.sender, data.message);
            break;
        }

        case state::GameEventType::EntitySpawned: {
            if (m_verbose) {
                const auto& data = std::get<state::EntitySpawnedData>(event.data);
                if (data.spawnId != m_playerSpawnId) {
                    std::cout << getTimestamp() << getChannelColor("spawn")
                              << "* " << data.name << " has entered the area."
                              << colorReset() << std::endl;
                }
            }
            break;
        }

        case state::GameEventType::EntityDespawned: {
            if (m_verbose) {
                const auto& data = std::get<state::EntityDespawnedData>(event.data);
                if (data.spawnId != m_playerSpawnId) {
                    std::cout << getTimestamp() << getChannelColor("despawn")
                              << "* " << data.name << " has left the area."
                              << colorReset() << std::endl;
                }
            }
            break;
        }

        case state::GameEventType::CombatEvent: {
            const auto& data = std::get<state::CombatEventData>(event.data);
            std::ostringstream ss;

            switch (data.type) {
                case state::CombatEventData::Type::Hit:
                    ss << data.sourceName << " hits " << data.targetName
                       << " for " << data.damage << " points of damage!";
                    break;
                case state::CombatEventData::Type::Miss:
                    ss << data.sourceName << " tries to hit " << data.targetName
                       << " but misses!";
                    break;
                case state::CombatEventData::Type::CriticalHit:
                    ss << data.sourceName << " scores a CRITICAL HIT on "
                       << data.targetName << " for " << data.damage << " damage!";
                    break;
                case state::CombatEventData::Type::Death:
                    ss << data.targetName << " has been slain!";
                    break;
                default:
                    return;  // Don't display other combat event types
            }

            displayCombatMessage(ss.str());
            break;
        }

        case state::GameEventType::ZoneChanged: {
            const auto& data = std::get<state::ZoneChangedData>(event.data);
            std::cout << "\n" << getChannelColor("zone")
                      << "=== Entering " << data.zoneName << " ==="
                      << colorReset() << "\n" << std::endl;
            break;
        }

        default:
            break;
    }
}

void ConsoleRenderer::loadZone(const std::string& zoneName) {
    std::lock_guard<std::mutex> lock(m_outputMutex);
    m_zoneName = zoneName;
    m_zoneReady = false;
    std::cout << getTimestamp() << getChannelColor("zone")
              << "Loading zone: " << zoneName << "..."
              << colorReset() << std::endl;
}

void ConsoleRenderer::unloadZone() {
    std::lock_guard<std::mutex> lock(m_outputMutex);
    m_zoneName.clear();
    m_zoneReady = false;
}

void ConsoleRenderer::setLoadingProgress(float progress, const std::wstring& statusText) {
    std::lock_guard<std::mutex> lock(m_outputMutex);
    int percent = static_cast<int>(progress * 100);
    std::cout << "\r" << getChannelColor("zone")
              << "[" << std::setw(3) << percent << "%] "
              << wstringToString(statusText) << "          "
              << colorReset() << std::flush;
}

void ConsoleRenderer::setZoneReady(bool ready) {
    std::lock_guard<std::mutex> lock(m_outputMutex);
    m_zoneReady = ready;
    if (ready) {
        std::cout << "\n" << getChannelColor("zone")
                  << "Zone loaded. You are in " << m_zoneName << "."
                  << colorReset() << "\n" << std::endl;
        printStatusLine();
    }
}

void ConsoleRenderer::setPlayerPosition(float x, float y, float z, float heading) {
    m_playerX = x;
    m_playerY = y;
    m_playerZ = z;
    m_playerHeading = heading;
}

void ConsoleRenderer::setCharacterInfo(const std::wstring& name, int level, const std::wstring& className) {
    m_playerName = name;
    m_playerLevel = level;
    m_playerClass = className;
}

void ConsoleRenderer::updateCharacterStats(uint32_t curHp, uint32_t maxHp,
                                            uint32_t curMana, uint32_t maxMana,
                                            uint32_t curEnd, uint32_t maxEnd) {
    m_curHp = curHp;
    m_maxHp = maxHp;
    m_curMana = curMana;
    m_maxMana = maxMana;
    m_curEnd = curEnd;
    m_maxEnd = maxEnd;
}

void ConsoleRenderer::setCurrentTarget(uint16_t spawnId, const std::string& name,
                                        uint8_t hpPercent, uint8_t level) {
    std::lock_guard<std::mutex> lock(m_outputMutex);
    m_targetSpawnId = spawnId;
    m_targetName = name;
    m_targetHpPercent = hpPercent;
    m_targetLevel = level;

    std::cout << getTimestamp() << getChannelColor("target")
              << "Target: " << name;
    if (level > 0) {
        std::cout << " (Level " << static_cast<int>(level) << ")";
    }
    std::cout << " - HP: " << static_cast<int>(hpPercent) << "%"
              << colorReset() << std::endl;
}

void ConsoleRenderer::updateCurrentTargetHP(uint8_t hpPercent) {
    m_targetHpPercent = hpPercent;
}

void ConsoleRenderer::clearCurrentTarget() {
    std::lock_guard<std::mutex> lock(m_outputMutex);
    if (m_targetSpawnId != 0) {
        std::cout << getTimestamp() << getChannelColor("target")
                  << "Target cleared."
                  << colorReset() << std::endl;
    }
    m_targetSpawnId = 0;
    m_targetName.clear();
    m_targetHpPercent = 100;
    m_targetLevel = 0;
}

void ConsoleRenderer::displayChatMessage(const std::string& channel,
                                          const std::string& sender,
                                          const std::string& message) {
    std::lock_guard<std::mutex> lock(m_outputMutex);

    std::cout << getTimestamp() << getChannelColor(channel)
              << "[" << channel << "] ";

    if (!sender.empty()) {
        std::cout << sender << ": ";
    }

    std::cout << message << colorReset() << std::endl;
}

void ConsoleRenderer::displaySystemMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_outputMutex);
    std::cout << getTimestamp() << getChannelColor("system")
              << "[System] " << message
              << colorReset() << std::endl;
}

void ConsoleRenderer::displayCombatMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_outputMutex);
    std::cout << getTimestamp() << getChannelColor("combat")
              << message << colorReset() << std::endl;
}

void ConsoleRenderer::printStatusLine() {
    std::lock_guard<std::mutex> lock(m_outputMutex);

    std::cout << getChannelColor("status");

    // Character name and level
    if (!m_playerName.empty()) {
        std::cout << wstringToString(m_playerName);
        if (m_playerLevel > 0) {
            std::cout << " L" << m_playerLevel;
        }
        if (!m_playerClass.empty()) {
            std::cout << " " << wstringToString(m_playerClass);
        }
        std::cout << " | ";
    }

    // HP/Mana
    std::cout << "HP: " << m_curHp << "/" << m_maxHp;
    if (m_maxMana > 0) {
        std::cout << " | Mana: " << m_curMana << "/" << m_maxMana;
    }

    // Location
    std::cout << " | Loc: " << std::fixed << std::setprecision(1)
              << m_playerX << ", " << m_playerY << ", " << m_playerZ;

    // Zone
    if (!m_zoneName.empty()) {
        std::cout << " (" << m_zoneName << ")";
    }

    // Target
    if (m_targetSpawnId != 0 && !m_targetName.empty()) {
        std::cout << " | Target: " << m_targetName
                  << " " << static_cast<int>(m_targetHpPercent) << "%";
    }

    std::cout << colorReset() << std::endl;
}

std::string ConsoleRenderer::getTimestamp() const {
    if (!m_showTimestamps) {
        return "";
    }

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream ss;
    if (m_colorOutput) {
        ss << COLOR_GRAY;
    }
    ss << "[" << std::setfill('0')
       << std::setw(2) << tm.tm_hour << ":"
       << std::setw(2) << tm.tm_min << ":"
       << std::setw(2) << tm.tm_sec << "] ";
    if (m_colorOutput) {
        ss << COLOR_RESET;
    }
    return ss.str();
}

std::string ConsoleRenderer::getChannelColor(const std::string& channel) const {
    if (!m_colorOutput) {
        return "";
    }

    if (channel == "say") return COLOR_WHITE;
    if (channel == "shout") return COLOR_BRIGHT_RED;
    if (channel == "ooc") return COLOR_GREEN;
    if (channel == "auction") return COLOR_BRIGHT_GREEN;
    if (channel == "tell") return COLOR_BRIGHT_MAGENTA;
    if (channel == "group" || channel == "gsay") return COLOR_CYAN;
    if (channel == "guild" || channel == "gu") return COLOR_BRIGHT_GREEN;
    if (channel == "raid") return COLOR_BRIGHT_YELLOW;
    if (channel == "emote") return COLOR_YELLOW;
    if (channel == "combat") return COLOR_BRIGHT_RED;
    if (channel == "system") return COLOR_BRIGHT_YELLOW;
    if (channel == "zone") return COLOR_BRIGHT_CYAN;
    if (channel == "target") return COLOR_BRIGHT_BLUE;
    if (channel == "status") return COLOR_BLUE;
    if (channel == "spawn") return COLOR_GRAY;
    if (channel == "despawn") return COLOR_GRAY;

    return COLOR_WHITE;
}

std::string ConsoleRenderer::colorReset() const {
    if (!m_colorOutput) {
        return "";
    }
    return COLOR_RESET;
}

} // namespace output
} // namespace eqt
