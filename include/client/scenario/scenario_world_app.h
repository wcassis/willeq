#pragma once

// T410: the one place a scenario touches the real client.

#include "client/scenario/scenario.h"

class EverQuest;

namespace eqt {

class Application;

namespace scenario {

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
    ApplicationScenarioWorld(Application& app, EverQuest& client);

    bool IsConnected() const override;
    bool IsFullyZonedIn() const override;
    std::string ZoneName() const override;
    int OtherPlayerCount() const override;
    std::string PlayerName() const override;
    std::vector<uint32_t> PlayerBuffSpellIds() const override;
    long long PeerEquipmentMaterial(const std::string& peerName, int materialSlot) const override;
    long long SelfEquipmentMaterial(int materialSlot) const override;
    void SendCommand(const std::string& rawLine) override;
    void Note(const std::string& line) override;

private:
    Application& app_;
    EverQuest& client_;
};

}  // namespace scenario
}  // namespace eqt
