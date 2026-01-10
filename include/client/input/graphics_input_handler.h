#pragma once

#include "client/input/input_handler.h"

#ifdef EQT_HAS_GRAPHICS
#include <irrlicht.h>
#endif

namespace EQT {
namespace Graphics {
class RendererEventReceiver;
}
}

namespace eqt {
namespace input {

/**
 * GraphicsInputHandler - Input handler for graphical mode.
 *
 * This handler wraps the RendererEventReceiver from the graphics layer
 * and translates Irrlicht input events to the IInputHandler interface.
 *
 * It supports:
 * - Keyboard input (movement, actions, text input)
 * - Mouse input (targeting, camera control)
 * - All debug adjustments from the graphics layer
 */
class GraphicsInputHandler : public IInputHandler {
public:
    /**
     * Create a graphics input handler.
     * @param eventReceiver Pointer to the RendererEventReceiver (owned by IrrlichtRenderer)
     */
    explicit GraphicsInputHandler(EQT::Graphics::RendererEventReceiver* eventReceiver);
    ~GraphicsInputHandler() override = default;

    // IInputHandler interface
    void update() override;
    bool isActive() const override;

    bool hasAction(InputAction action) const override;
    bool consumeAction(InputAction action) override;

    const InputState& getState() const override { return m_state; }
    void resetMouseDeltas() override;

    std::optional<SpellCastRequest> consumeSpellCastRequest() override;
    std::optional<HotbarRequest> consumeHotbarRequest() override;
    std::optional<TargetRequest> consumeTargetRequest() override;
    std::optional<LootRequest> consumeLootRequest() override;

    bool hasPendingKeyEvents() const override;
    std::optional<KeyEvent> popKeyEvent() override;
    void clearPendingKeyEvents() override;

    // Console-specific methods return empty (not used in graphics mode)
    std::optional<ChatMessage> consumeChatMessage() override { return std::nullopt; }
    std::optional<MoveCommand> consumeMoveCommand() override { return std::nullopt; }
    std::optional<std::string> consumeRawCommand() override { return std::nullopt; }

    // Graphics-specific debug adjustments
    float consumeAnimSpeedDelta() override;
    float consumeAmbientLightDelta() override;
    float consumeCorpseZOffsetDelta() override;
    float consumeEyeHeightDelta() override;
    float consumeParticleMultiplierDelta() override;
    float consumeCollisionHeightDelta() override;
    float consumeStepHeightDelta() override;

    float consumeOffsetXDelta() override;
    float consumeOffsetYDelta() override;
    float consumeOffsetZDelta() override;

    float consumeRotationXDelta() override;
    float consumeRotationYDelta() override;
    float consumeRotationZDelta() override;

    float consumeHelmUOffsetDelta() override;
    float consumeHelmVOffsetDelta() override;
    float consumeHelmUScaleDelta() override;
    float consumeHelmVScaleDelta() override;
    float consumeHelmRotationDelta() override;
    bool consumeHelmUVSwapRequest() override;
    bool consumeHelmVFlipRequest() override;
    bool consumeHelmUFlipRequest() override;
    bool consumeHelmResetRequest() override;
    bool consumeHelmPrintStateRequest() override;
    int consumeHeadVariantCycleDelta() override;

    float consumeRepairRotateXDelta() override;
    float consumeRepairRotateYDelta() override;
    float consumeRepairRotateZDelta() override;
    bool consumeRepairFlipXRequest() override;
    bool consumeRepairFlipYRequest() override;
    bool consumeRepairFlipZRequest() override;
    bool consumeRepairResetRequest() override;

    /**
     * Inject a target request (called by IrrlichtRenderer when entity is clicked).
     */
    void injectTargetRequest(uint16_t spawnId);

    /**
     * Inject a loot request (called by IrrlichtRenderer when corpse is shift-clicked).
     */
    void injectLootRequest(uint16_t corpseId);

private:
    void updateFromEventReceiver();

    EQT::Graphics::RendererEventReceiver* m_eventReceiver;
    InputState m_state;
    bool m_pendingActions[static_cast<size_t>(InputAction::Count)] = {false};

    // Pending requests (injected by IrrlichtRenderer)
    std::queue<TargetRequest> m_targetRequests;
    std::queue<LootRequest> m_lootRequests;
};

} // namespace input
} // namespace eqt
