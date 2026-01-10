#include "client/output/null_renderer.h"
#include "client/state/game_state.h"

namespace eqt {
namespace output {

bool NullRenderer::initialize(const RendererConfig& config) {
    (void)config;  // Unused - null renderer doesn't need configuration
    m_running = true;
    return true;
}

void NullRenderer::shutdown() {
    m_running = false;
    m_zoneName.clear();
    m_zoneReady = false;
}

bool NullRenderer::update(float deltaTime) {
    (void)deltaTime;
    return m_running;
}

void NullRenderer::connectToGameState(state::GameState& state) {
    // Null renderer doesn't subscribe to events
    (void)state;
}

void NullRenderer::disconnectFromGameState() {
    // Nothing to disconnect
}

void NullRenderer::loadZone(const std::string& zoneName) {
    m_zoneName = zoneName;
    m_zoneReady = false;
}

void NullRenderer::unloadZone() {
    m_zoneName.clear();
    m_zoneReady = false;
}

void NullRenderer::setLoadingProgress(float progress, const std::wstring& statusText) {
    (void)progress;
    (void)statusText;
}

void NullRenderer::setPlayerPosition(float x, float y, float z, float heading) {
    (void)x; (void)y; (void)z; (void)heading;
}

void NullRenderer::setCharacterInfo(const std::wstring& name, int level, const std::wstring& className) {
    (void)name; (void)level; (void)className;
}

void NullRenderer::updateCharacterStats(uint32_t curHp, uint32_t maxHp,
                                         uint32_t curMana, uint32_t maxMana,
                                         uint32_t curEnd, uint32_t maxEnd) {
    (void)curHp; (void)maxHp;
    (void)curMana; (void)maxMana;
    (void)curEnd; (void)maxEnd;
}

void NullRenderer::setCurrentTarget(uint16_t spawnId, const std::string& name,
                                     uint8_t hpPercent, uint8_t level) {
    (void)spawnId; (void)name; (void)hpPercent; (void)level;
}

void NullRenderer::updateCurrentTargetHP(uint8_t hpPercent) {
    (void)hpPercent;
}

void NullRenderer::clearCurrentTarget() {
    // Nothing to clear
}

void NullRenderer::displayChatMessage(const std::string& channel,
                                       const std::string& sender,
                                       const std::string& message) {
    (void)channel; (void)sender; (void)message;
}

void NullRenderer::displaySystemMessage(const std::string& message) {
    (void)message;
}

void NullRenderer::displayCombatMessage(const std::string& message) {
    (void)message;
}

} // namespace output
} // namespace eqt
