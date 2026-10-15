#pragma once

// T410: the one place a scenario touches the real client.

#include "client/scenario/scenario.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class EverQuest;

namespace eqt {

class Application;

namespace bridge {
class GameStateBridge;
}
namespace state {
struct GameEvent;
}

namespace scenario {

/**
 * UIGAP-live: what the bridge observer records. Its own object (shared with
 * the observer lambda) so the bridge never holds a pointer into a world that
 * could be gone first.
 */
struct ScenarioTranscript {
    std::mutex mutex;
    std::vector<std::string> chatLines;            // "<channel>|<sender>|<text>"
    std::map<int, long long> eventCounts;          // GameEventType -> count
    std::map<int, long long> lastSkillValue;       // skill id -> value (SkillValueChanged)
    long long trackingRows = -1;                   // latest TrackingListChanged row count

    void Record(const eqt::state::GameEvent& event);
};

/**
 * IScenarioWorld over a live Application + EverQuest.
 *
 * Every method here runs on the GAME THREAD (the automation callback is
 * invoked from Application::processInput(), which the game thread owns), so
 * it reads game state with no lock -- the same thread that writes it. Do not
 * call it from the render thread.
 */
class ApplicationScenarioWorld final : public IScenarioWorld {
public:
    /**
     * `bridge` (may be null): the bridge EverQuest pushes events into. When
     * given, the world installs its transcript observer on it (UIGAP-live).
     */
    ApplicationScenarioWorld(Application& app, EverQuest& client,
                             eqt::bridge::GameStateBridge* bridge = nullptr);

    bool IsConnected() const override;
    bool IsFullyZonedIn() const override;
    std::string ZoneName() const override;
    int OtherPlayerCount() const override;
    std::string PlayerName() const override;
    std::vector<uint32_t> PlayerBuffSpellIds() const override;
    bool PlayerLevitating() const override;
    long long PeerEquipmentMaterial(const std::string& peerName, int materialSlot) const override;
    long long SelfEquipmentMaterial(int materialSlot) const override;

    // UIGAP-live (scenario_world_app_ui.cpp)
    long long ClientItemAt(int serverSlot) const override;
    long long ClientChargesAt(int serverSlot) const override;
    long long CursorQueueSize() const override;
    long long SkillValue(int skillId) const override;
    long long PracticePoints() const override;
    size_t ChatLineCount() const override;
    std::string ChatLine(size_t index) const override;
    long long EventCount(int gameEventType) const override;
    long long LastSkillUpdateValue(int skillId) const override;
    long long TrackingRowCount() const override;
    long long NearestWorldObjectId(int objectType) const override;
    int ClickSlot(int serverSlot, bool rightButton, bool shift, bool ctrl) override;

    // UIGAP-live2 (scenario_world_app_trade.cpp)
    int TradeFlags() const override;
    long long FindItemSlot(long long itemId) const override;
    std::vector<long long> Coins(bool bank) const override;
    long long CursorCoinCopper() const override;
    long long WorldObjectCount() const override;
    bool ClickEntity(const std::string& name) override;
    bool TradeButton(int op) override;
    bool CursorGesture(int op, int coinType, int quantity, int coinSource) override;

    // B1005 (scenario_world_app_camp.cpp)
    int CampPhase() const override;
    bool AtCharacterSelect() const override;
    std::vector<std::string> CharacterListNames() const override;
    long long CharacterListGeneration() const override;
    bool EnterWorldAs(const std::string& name) override;
    long long ZoneConnectOpens() const override;                          // B1072
    bool SimulateUnreachableZoneConnects(int attempts, int port) override;  // B1072

    // B893 (fall_damage): the fall's live observable and its driver.
    long long PlayerHP() const override;
    bool SimulateFall(float fallHeight) override;

    void SendCommand(const std::string& rawLine) override;
    void Note(const std::string& line) override;

private:
    Application& app_;
    EverQuest& client_;
    eqt::bridge::GameStateBridge* bridge_ = nullptr;
    std::shared_ptr<ScenarioTranscript> transcript_;
};

}  // namespace scenario
}  // namespace eqt
