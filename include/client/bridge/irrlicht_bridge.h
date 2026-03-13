#pragma once

#include "client/bridge/game_state_bridge.h"

namespace EQT {
namespace Graphics {
    class IrrlichtRenderer;
}
}

namespace eqt {
namespace bridge {

/**
 * IrrlichtBridge - Bridge adapter for the Irrlicht 3D renderer.
 *
 * Translates game events into IrrlichtRenderer calls.
 * Events that carry insufficient data for a full renderer call
 * remain as log-only stubs (e.g., EntityAnimationEvent lacks the
 * animation string, EntityAppearanceChanged lacks full equipment data).
 */
class IrrlichtBridge : public GameStateBridge {
public:
    IrrlichtBridge() = default;
    ~IrrlichtBridge() override = default;

    /**
     * Set the renderer to receive translated calls.
     * Must be called before applyEvent() does anything useful.
     */
    void setRenderer(EQT::Graphics::IrrlichtRenderer* renderer) { renderer_ = renderer; }

    /**
     * Apply a game event to the Irrlicht renderer.
     */
    void applyEvent(const state::GameEvent& event) override;

private:
    EQT::Graphics::IrrlichtRenderer* renderer_ = nullptr;
};

} // namespace bridge
} // namespace eqt
