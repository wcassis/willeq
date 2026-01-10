#pragma once

#include "client/output/output_renderer.h"

namespace eqt {
namespace output {

/**
 * NullRenderer - Renderer for automated mode.
 *
 * This renderer provides no output. All methods are no-ops or return
 * minimal values. Used when the client runs in fully automated mode
 * where no visual or text output is needed.
 *
 * Note: System messages and errors are still logged via the logging
 * system, but not displayed through this renderer.
 */
class NullRenderer : public IOutputRenderer {
public:
    NullRenderer() = default;
    ~NullRenderer() override = default;

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
    void setZoneReady(bool ready) override { m_zoneReady = ready; }
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

private:
    bool m_running = false;
    std::string m_zoneName;
    bool m_zoneReady = false;
    uint16_t m_playerSpawnId = 0;
};

} // namespace output
} // namespace eqt
