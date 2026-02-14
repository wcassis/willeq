#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include <queue>
#include <optional>

namespace eqt {
namespace input {

/**
 * InputAction - Discrete input actions that can be triggered.
 *
 * These are one-shot actions that are consumed when processed.
 * For continuous state (like movement keys held), use InputState.
 */
enum class InputAction {
    // System
    Quit,
    Screenshot,

    // Movement toggles
    ToggleAutorun,
    Jump,

    // Combat
    ToggleAutoAttack,
    Attack,             // Initiate attack on target (distinct from toggle)
    ClearTarget,
    Consider,           // Consider target (sends server packet)
    Hail,

    // Targeting
    TargetSelf,
    TargetGroupMember1,
    TargetGroupMember2,
    TargetGroupMember3,
    TargetGroupMember4,
    TargetGroupMember5,
    TargetNearestPC,
    TargetNearestNPC,
    CycleTargets,
    CycleTargetsReverse,

    // UI toggles
    ToggleInventory,
    ToggleSkills,
    ToggleGroup,
    ToggleVendor,
    TogglePetWindow,
    ToggleTrainer,
    ToggleSpellbook,

    // Interaction
    InteractDoor,
    InteractWorldObject,
    Interact,           // Unified interact - nearest door/object/NPC
    ReplyToTell,        // Reply to last tell

    // Graphics-only toggles (handled by GraphicsInputHandler)
    ToggleWireframe,
    ToggleHUD,
    ToggleNameTags,
    ToggleZoneLights,
    ToggleZoneLineVisualization,
    CycleObjectLights,
    ToggleCameraMode,
    ToggleOldModels,
    ToggleCollision,
    ToggleCollisionDebug,

    // Chat input
    OpenChat,       // Enter key pressed - open chat input
    OpenChatSlash,  // Slash key pressed - open chat with '/'
    CloseChat,      // Escape key pressed - close chat

    Count
};

/**
 * SpellCastRequest - Request to cast a spell from a gem slot.
 */
struct SpellCastRequest {
    uint8_t gemSlot;  // 0-7 for gem slots 1-8
};

/**
 * HotbarRequest - Request to activate a hotbar button.
 */
struct HotbarRequest {
    uint8_t slot;  // 0-9 for hotbar buttons
};

/**
 * TargetRequest - Request to target an entity.
 */
struct TargetRequest {
    uint16_t spawnId;
};

/**
 * LootRequest - Request to loot a corpse.
 */
struct LootRequest {
    uint16_t corpseId;
};

/**
 * ChatMessage - A chat message to send.
 */
struct ChatMessage {
    std::string text;
    std::string channel;  // "say", "tell", "shout", "ooc", "auction", "group", "guild"
    std::string target;   // For tells
};

/**
 * MoveCommand - Command to move to a location or entity.
 */
struct MoveCommand {
    enum class Type { Coordinates, Entity, Face, Stop };
    Type type;
    float x = 0, y = 0, z = 0;
    std::string entityName;
};

/**
 * InputState - Continuous input state for polling.
 *
 * This represents the current state of input devices.
 * For movement, these are polled each frame.
 */
struct InputState {
    // Movement keys (continuous state)
    bool moveForward = false;
    bool moveBackward = false;
    bool strafeLeft = false;
    bool strafeRight = false;
    bool turnLeft = false;
    bool turnRight = false;

    // Mouse state (for graphics mode)
    int mouseX = 0;
    int mouseY = 0;
    int mouseDeltaX = 0;
    int mouseDeltaY = 0;
    bool leftButtonDown = false;
    bool rightButtonDown = false;
    bool leftButtonClicked = false;   // Just clicked this frame
    bool leftButtonReleased = false;  // Just released this frame
    int clickMouseX = 0;              // Position where click started
    int clickMouseY = 0;

    // Shift key state (for loot corpse with shift+click)
    bool shiftHeld = false;
};

/**
 * KeyEvent - A keyboard event for text input.
 */
struct KeyEvent {
    uint32_t keyCode;   // Platform-specific key code
    wchar_t character;  // Unicode character (0 if not a printable char)
    bool shift;
    bool ctrl;
};

/**
 * IInputHandler - Abstract interface for input handling.
 *
 * This interface abstracts input handling for different modes:
 * - NullInputHandler: For automated mode (no user input)
 * - ConsoleInputHandler: For headless mode (stdin commands)
 * - GraphicsInputHandler: For graphical mode (keyboard/mouse)
 *
 * The interface supports:
 * - Discrete actions (one-shot events)
 * - Continuous state (movement keys, mouse position)
 * - Text input (chat, commands)
 * - Special requests (spell casting, targeting)
 */
class IInputHandler {
public:
    virtual ~IInputHandler() = default;

    /**
     * Update input state. Called once per frame/tick.
     * This should poll for new input and update internal state.
     */
    virtual void update() = 0;

    /**
     * Check if the input handler is still active.
     * Returns false if the handler has been shut down.
     */
    virtual bool isActive() const = 0;

    // ========== Discrete Actions ==========

    /**
     * Check if an action was triggered since the last consume.
     */
    virtual bool hasAction(InputAction action) const = 0;

    /**
     * Consume an action, returning true if it was triggered.
     */
    virtual bool consumeAction(InputAction action) = 0;

    // ========== Continuous State ==========

    /**
     * Get the current input state for polling.
     */
    virtual const InputState& getState() const = 0;

    /**
     * Reset mouse delta values (called after consuming deltas).
     */
    virtual void resetMouseDeltas() = 0;

    // ========== Special Requests ==========

    /**
     * Get a pending spell cast request.
     * @return Spell cast request if one is pending, empty otherwise.
     */
    virtual std::optional<SpellCastRequest> consumeSpellCastRequest() = 0;

    /**
     * Get a pending hotbar activation request.
     * @return Hotbar request if one is pending, empty otherwise.
     */
    virtual std::optional<HotbarRequest> consumeHotbarRequest() = 0;

    /**
     * Get a pending target request (from mouse click).
     * @return Target request if one is pending, empty otherwise.
     */
    virtual std::optional<TargetRequest> consumeTargetRequest() = 0;

    /**
     * Get a pending loot request (from shift+click on corpse).
     * @return Loot request if one is pending, empty otherwise.
     */
    virtual std::optional<LootRequest> consumeLootRequest() = 0;

    // ========== Text Input ==========

    /**
     * Check if there are pending key events for text input.
     */
    virtual bool hasPendingKeyEvents() const = 0;

    /**
     * Pop a pending key event.
     * @return Key event if one is pending, empty otherwise.
     */
    virtual std::optional<KeyEvent> popKeyEvent() = 0;

    /**
     * Clear all pending key events.
     */
    virtual void clearPendingKeyEvents() = 0;

    // ========== Console-specific (for headless mode) ==========

    /**
     * Get a pending chat message from console input.
     * @return Chat message if one is pending, empty otherwise.
     */
    virtual std::optional<ChatMessage> consumeChatMessage() = 0;

    /**
     * Get a pending move command from console input.
     * @return Move command if one is pending, empty otherwise.
     */
    virtual std::optional<MoveCommand> consumeMoveCommand() = 0;

    /**
     * Get a pending raw command string (for commands not in the enum).
     * @return Command string if one is pending, empty otherwise.
     */
    virtual std::optional<std::string> consumeRawCommand() = 0;

};

} // namespace input
} // namespace eqt
