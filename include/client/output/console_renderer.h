#pragma once

#include "client/output/output_renderer.h"
#include "client/state/event_bus.h"
#include <vector>
#include <chrono>
#include <mutex>

namespace eqt {
namespace output {

/**
 * ConsoleRenderer - Renderer for headless interactive mode.
 *
 * This renderer outputs game state changes to stdout in formatted text.
 * It provides real-time status updates, chat display with timestamps
 * and channel filtering, and entity spawn/despawn notifications.
 *
 * Features:
 * - Chat messages with timestamps and channel prefixes
 * - Status line showing HP/Mana/Location
 * - Entity spawn/despawn notifications (when verbose mode enabled)
 * - Combat messages
 * - ANSI color support (Unix)
 */
class ConsoleRenderer : public IOutputRenderer {
public:
    ConsoleRenderer() = default;
    ~ConsoleRenderer() override;

    // ========== Lifecycle ==========

    bool initialize(const RendererConfig& config) override;
    void shutdown() override;
    bool isRunning() const override { return m_running; }
    bool update(float deltaTime) override;

    // ========== Event Bus Integration ==========

    void connectToGameState(state::GameState& state) override;
    void disconnectFromGameState() override;

    // ========== Zone Management ==========

    void loadZone(const std::string& zoneName) override;
    void unloadZone() override;
    const std::string& getCurrentZoneName() const override { return m_zoneName; }
    void setLoadingProgress(float progress, const std::wstring& statusText) override;
    void setZoneReady(bool ready) override;
    bool isZoneReady() const override { return m_zoneReady; }

    // ========== Player Display ==========

    void setPlayerSpawnId(uint16_t spawnId) override { m_playerSpawnId = spawnId; }
    void setPlayerPosition(float x, float y, float z, float heading) override;
    void setCharacterInfo(const std::wstring& name, int level, const std::wstring& className) override;
    void updateCharacterStats(uint32_t curHp, uint32_t maxHp,
                               uint32_t curMana, uint32_t maxMana,
                               uint32_t curEnd, uint32_t maxEnd) override;

    // ========== Target Display ==========

    void setCurrentTarget(uint16_t spawnId, const std::string& name,
                           uint8_t hpPercent = 100, uint8_t level = 0) override;
    void updateCurrentTargetHP(uint8_t hpPercent) override;
    void clearCurrentTarget() override;

    // ========== Chat/Message Output ==========

    void displayChatMessage(const std::string& channel,
                             const std::string& sender,
                             const std::string& message) override;
    void displaySystemMessage(const std::string& message) override;
    void displayCombatMessage(const std::string& message) override;

    // ========== Control ==========

    void requestQuit() override { m_running = false; }

    // ========== Console-Specific Options (overrides from IOutputRenderer) ==========

    /**
     * Enable/disable verbose mode (entity spawn/despawn messages).
     */
    void setVerbose(bool verbose) override { m_verbose = verbose; }
    bool getVerbose() const override { return m_verbose; }

    /**
     * Enable/disable timestamp display.
     */
    void setShowTimestamps(bool show) override { m_showTimestamps = show; }
    bool getShowTimestamps() const override { return m_showTimestamps; }

    /**
     * Enable/disable color output.
     */
    void setColorOutput(bool color) override { m_colorOutput = color; }
    bool getColorOutput() const override { return m_colorOutput; }

    /**
     * Set status line update interval.
     */
    void setStatusUpdateInterval(float seconds) { m_statusUpdateInterval = seconds; }

private:
    void handleEvent(const state::GameEvent& event);
    void printStatusLine();
    std::string getTimestamp() const;
    std::string getChannelColor(const std::string& channel) const;
    std::string colorReset() const;

    bool m_running = false;
    bool m_verbose = false;
    bool m_showTimestamps = true;
    bool m_colorOutput = true;
    float m_statusUpdateInterval = 5.0f;  // Update status every 5 seconds

    std::string m_zoneName;
    bool m_zoneReady = false;
    uint16_t m_playerSpawnId = 0;

    // Player info
    std::wstring m_playerName;
    int m_playerLevel = 0;
    std::wstring m_playerClass;
    float m_playerX = 0, m_playerY = 0, m_playerZ = 0;
    float m_playerHeading = 0;
    uint32_t m_curHp = 0, m_maxHp = 0;
    uint32_t m_curMana = 0, m_maxMana = 0;
    uint32_t m_curEnd = 0, m_maxEnd = 0;

    // Target info
    uint16_t m_targetSpawnId = 0;
    std::string m_targetName;
    uint8_t m_targetHpPercent = 100;
    uint8_t m_targetLevel = 0;

    // Timing
    float m_timeSinceStatusUpdate = 0.0f;
    std::chrono::steady_clock::time_point m_startTime;

    // Event subscription
    state::EventBus* m_eventBus = nullptr;
    std::vector<state::ListenerHandle> m_listenerHandles;
    mutable std::mutex m_outputMutex;
};

} // namespace output
} // namespace eqt
