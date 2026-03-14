/*
 * ConsoleBridge — lightweight bridge that logs game events to the console.
 *
 * D25: Proves the multi-renderer architecture. Attaches in headless mode
 * (--no-graphics) so game state events are visible without a 3D renderer.
 * No graphics dependencies.
 */

#pragma once

#include "client/bridge/game_state_bridge.h"

namespace eqt {
namespace bridge {

class ConsoleBridge : public GameStateBridge {
public:
    ConsoleBridge() = default;
    ~ConsoleBridge() override = default;

    void applyEvent(const state::GameEvent& event) override;
};

} // namespace bridge
} // namespace eqt
