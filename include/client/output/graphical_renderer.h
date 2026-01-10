#pragma once

#include "client/output/output_renderer.h"
#include "client/input/input_handler.h"
#include "client/state/event_bus.h"
#include <memory>
#include <vector>

namespace eqt {
namespace output {

/**
 * EntityAppearanceData - Appearance data for entity rendering.
 *
 * This mirrors the EntityAppearance struct from IrrlichtRenderer but
 * is defined here to avoid graphics dependencies in the output interface.
 */
struct EntityAppearanceData {
    uint8_t texture = 0;           // Body texture variant
    uint8_t helm = 0;              // Helm texture
    uint8_t face = 0;              // Face texture
    uint8_t hairColor = 0;         // Hair color
    uint8_t beardColor = 0;        // Beard color
    uint8_t eyeColor1 = 0;         // Primary eye color
    uint8_t eyeColor2 = 0;         // Secondary eye color
    uint8_t hairStyle = 0;         // Hair style
    uint8_t beard = 0;             // Beard style
    uint8_t heritage = 0;          // Heritage (Drakkin)
    uint8_t tattoo = 0;            // Tattoo (Drakkin)
    uint8_t details = 0;           // Face details (Drakkin)
    uint32_t drakkinHeritage = 0;  // Drakkin heritage color
    uint32_t drakkinTattoo = 0;    // Drakkin tattoo color
    uint32_t drakkinDetails = 0;   // Drakkin details color
    float size = 1.0f;             // Model size multiplier
    bool showHelm = true;          // Whether to display helm
    uint32_t equipmentMaterial[9] = {0};  // Equipment material IDs
    uint32_t equipmentTint[9] = {0};      // Equipment tint colors
};

/**
 * GraphicalRenderer - Abstract base class for visual renderers.
 *
 * This class provides common functionality for all graphical renderers:
 * - Input handler management
 * - Camera mode support
 * - Render quality settings
 * - Entity/door management methods
 *
 * Concrete implementations include:
 * - IrrlichtRenderer (software or GPU)
 * - ASCIIRenderer (terminal graphics)
 * - TopDownRenderer (2D overhead view)
 */
class GraphicalRenderer : public IOutputRenderer {
public:
    GraphicalRenderer() = default;
    ~GraphicalRenderer() override = default;

    // ========== Input Handler ==========

    /**
     * Get the renderer's input handler.
     * Graphical renderers typically provide their own input handler
     * that reads from keyboard/mouse through the graphics system.
     */
    input::IInputHandler* getInputHandler() override { return m_inputHandler.get(); }

    // ========== Camera Control ==========

    /**
     * Set camera mode.
     */
    void setCameraMode(CameraMode mode) override { m_cameraMode = mode; }

    /**
     * Get current camera mode.
     */
    CameraMode getCameraMode() const { return m_cameraMode; }

    /**
     * Cycle to next camera mode.
     */
    void cycleCameraMode() override;

    /**
     * Get camera mode as string.
     */
    std::string getCameraModeString() const;

    // ========== Render Quality ==========

    /**
     * Set render quality.
     */
    void setRenderQuality(RenderQuality quality) override { m_renderQuality = quality; }

    /**
     * Get current render quality.
     */
    RenderQuality getRenderQuality() const { return m_renderQuality; }

    // ========== Entity Management (Graphical) ==========

    /**
     * Create an entity for rendering.
     * @param spawnId Entity spawn ID
     * @param raceId Race ID for model selection
     * @param name Entity name
     * @param x, y, z Position in EQ coordinates
     * @param heading Heading in degrees (0-360)
     * @param isPlayer true if this is the player character
     * @param gender Entity gender (0=male, 1=female)
     * @param appearance Appearance data for model customization
     * @param isNPC true if this is an NPC (vs player character)
     * @param isCorpse true if this is a corpse
     * @return true if entity created successfully
     */
    virtual bool createEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                              float x, float y, float z, float heading,
                              bool isPlayer = false, uint8_t gender = 0,
                              const EntityAppearanceData& appearance = EntityAppearanceData(),
                              bool isNPC = true, bool isCorpse = false) = 0;

    /**
     * Update entity position and animation.
     */
    virtual void updateEntity(uint16_t spawnId, float x, float y, float z, float heading,
                              float dx = 0, float dy = 0, float dz = 0, uint32_t animation = 0) = 0;

    /**
     * Remove an entity from rendering.
     */
    virtual void removeEntity(uint16_t spawnId) = 0;

    /**
     * Clear all entities.
     */
    virtual void clearEntities() = 0;

    /**
     * Play death animation for an entity.
     */
    virtual void playEntityDeathAnimation(uint16_t spawnId) = 0;

    /**
     * Set entity animation.
     */
    virtual bool setEntityAnimation(uint16_t spawnId, const std::string& animCode,
                                    bool loop = false, bool playThrough = true) = 0;

    // ========== Door Management (Graphical) ==========

    /**
     * Create a door for rendering.
     */
    virtual bool createDoor(uint8_t doorId, const std::string& name,
                            float x, float y, float z, float heading,
                            uint32_t incline, uint16_t size, uint8_t opentype,
                            bool initiallyOpen) = 0;

    /**
     * Set door state (open/closed).
     */
    virtual void setDoorState(uint8_t doorId, bool open, bool userInitiated = false) = 0;

    /**
     * Clear all doors.
     */
    virtual void clearDoors() = 0;

    // ========== Callbacks (Graphical) ==========

    /**
     * Door interaction callback type.
     */
    using DoorInteractCallback = std::function<void(uint8_t doorId)>;

    /**
     * Set door interaction callback.
     */
    virtual void setDoorInteractCallback(DoorInteractCallback callback) {
        m_doorInteractCallback = callback;
    }

    /**
     * Spell gem cast callback type.
     */
    using SpellGemCastCallback = std::function<void(uint8_t gemSlot)>;

    /**
     * Set spell gem cast callback.
     */
    virtual void setSpellGemCastCallback(SpellGemCastCallback callback) {
        m_spellGemCastCallback = callback;
    }

    /**
     * Target selection callback type.
     */
    using TargetCallback = std::function<void(uint16_t spawnId)>;

    /**
     * Set target selection callback.
     */
    virtual void setTargetCallback(TargetCallback callback) {
        m_targetCallback = callback;
    }

    /**
     * Movement callback type for position updates.
     */
    struct PlayerPositionUpdate {
        float x = 0, y = 0, z = 0;
        float heading = 0;
        float dx = 0, dy = 0, dz = 0;
        bool isMoving(float threshold = 0.1f) const {
            return (dx * dx + dy * dy + dz * dz) > (threshold * threshold);
        }
    };
    using MovementCallback = std::function<void(const PlayerPositionUpdate& update)>;

    /**
     * Set movement callback for player position sync.
     */
    virtual void setMovementCallback(MovementCallback callback) {
        m_movementCallback = callback;
    }

    /**
     * Chat submit callback type.
     */
    using ChatSubmitCallback = std::function<void(const std::string& text)>;

    /**
     * Set chat submit callback.
     */
    virtual void setChatSubmitCallback(ChatSubmitCallback callback) {
        m_chatSubmitCallback = callback;
    }

protected:
    std::unique_ptr<input::IInputHandler> m_inputHandler;
    CameraMode m_cameraMode = CameraMode::FirstPerson;
    RenderQuality m_renderQuality = RenderQuality::Medium;

    // Callbacks
    DoorInteractCallback m_doorInteractCallback;
    SpellGemCastCallback m_spellGemCastCallback;
    TargetCallback m_targetCallback;
    MovementCallback m_movementCallback;
    ChatSubmitCallback m_chatSubmitCallback;

    // Event bus subscription
    state::EventBus* m_eventBus = nullptr;
    std::vector<state::ListenerHandle> m_listenerHandles;
};

} // namespace output
} // namespace eqt
