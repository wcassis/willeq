#pragma once

#include "client/bridge/game_state_bridge.h"

namespace eqt {
namespace bridge {

/**
 * IrrlichtBridge - Bridge adapter for the Irrlicht 3D renderer.
 *
 * Translates game events into IrrlichtRenderer calls. Currently a skeleton
 * with stub implementations that log each event type at TRACE level.
 * Actual renderer calls will be wired in Phase 3 (D09-D13).
 */
class IrrlichtBridge : public GameStateBridge {
public:
    IrrlichtBridge() = default;
    ~IrrlichtBridge() override = default;

    /**
     * Apply a game event to the Irrlicht renderer.
     * Currently logs each event type at TRACE level.
     */
    void applyEvent(const state::GameEvent& event) override;
};

} // namespace bridge
} // namespace eqt
