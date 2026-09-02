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
 * For continuous state (like movement keys held), poll the source on the
 * thread that consumes it (T315 -- see the note above IInputHandler).
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

    // T241: OpenChat / OpenChatSlash / CloseChat removed -- chat focus is
    // handled on the render thread; nothing consumed them here.

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

/*
 * T315: the polled-snapshot struct that used to live here (a per-handler
 * copy of movement flags, mouse position, mouse delta and the two click
 * one-shots) is GONE, together with the getState() and resetMouseDeltas()
 * interface members that were its only route out.
 *
 * It had no reader anywhere in the tree -- getState() had zero callers in
 * src/ or tests/ -- so every handler spent each tick filling in a struct
 * nobody looked at. That was not merely waste: filling it meant CONSUMING
 * RendererEventReceiver's destructive read-and-reset accessors from the
 * game thread, and those accessors have a real consumer on the render
 * thread. T312 removed the mouse-delta theft that froze the camera; T315
 * removed the click theft that silently swallowed clicks on inventory
 * slots, loot rows and spell gems.
 *
 * Continuous state is polled where it is used, on the thread that uses it:
 * movement from RendererEventReceiver::isKeyDown() in
 * renderer_player_physics.cpp, mouse position and clicks in
 * renderer_frame.cpp's processFrameInput(). If a future consumer needs a
 * snapshot, wire the consumer FIRST -- see
 * tests/test_read_and_reset_ownership.cpp, which fails if this struct
 * comes back.
 */

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

    // T241: the text-input trio (hasPendingKeyEvents / popKeyEvent /
    // clearPendingKeyEvents) was deleted -- no caller through this
    // interface; typed chat text is drained by the renderer itself.

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

    /**
     * T30: non-destructive look at the next pending raw command, without
     * removing it from the queue. Used by Application's pre-connection
     * lifecycle intercept (see processPreConnectionLifecycleCommands())
     * to decide whether the front of the queue is a command it's allowed
     * to handle early (/quit, /help, ...) without disturbing the order or
     * presence of everything else queued behind/including it.
     * @return Command string if one is pending, empty otherwise.
     */
    virtual std::optional<std::string> peekRawCommand() const = 0;

};

} // namespace input
} // namespace eqt
