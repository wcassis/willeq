#pragma once

#include <irrlicht.h>
#include <json/json.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace eqt {
namespace input {

/**
 * ModifierFlags - Bitmask for modifier keys in hotkey bindings.
 * Supports up to 3 simultaneous keys (1 primary + 2 modifiers).
 */
enum class ModifierFlags : uint8_t {
    None  = 0x00,
    Ctrl  = 0x01,
    Shift = 0x02,
    Alt   = 0x04
};

inline ModifierFlags operator|(ModifierFlags a, ModifierFlags b) {
    return static_cast<ModifierFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline ModifierFlags operator&(ModifierFlags a, ModifierFlags b) {
    return static_cast<ModifierFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool hasModifier(ModifierFlags flags, ModifierFlags mod) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(mod)) != 0;
}

/**
 * HotkeyMode - Modes that determine which hotkey bindings are active.
 * Bindings in Global mode are always active regardless of current mode.
 */
enum class HotkeyMode : uint8_t {
    Global,   // Always active (F9, F12, Shift+Escape)
    Player,   // Active in Player mode (WASD, Q, F1-F8, 1-0, Alt+1-8)
    Repair,   // Active in Repair mode (X/Y/Z rotation, Ctrl+1/2/3 flip)
    Admin     // Active in Admin mode (Ctrl+F1-F8, [/], PageUp/Down)
};

/**
 * HotkeyAction - All actions that can be bound to hotkeys.
 * Organized by mode category for clarity.
 */
enum class HotkeyAction : uint32_t {
    // === Global Actions ===
    Quit,
    Screenshot,
    ToggleWireframe,
    ToggleHUD,
    ToggleNameTags,
    ToggleZoneLights,
    ToggleCameraMode,
    ToggleOldModels,
    ToggleRendererMode,
    ToggleUILock,
    SaveUILayout,
    ResetUIDefaults,
    ConfirmDialog,
    CancelDialog,
    SubmitInput,
    CancelInput,
    ChatAutocomplete,

    // === Player Mode Actions ===
    // Movement (continuous/held keys)
    MoveForward,
    MoveBackward,
    StrafeLeft,
    StrafeRight,
    TurnLeft,
    TurnRight,
    Jump,
    SwimUp,     // Swim upward ([ key)
    SwimDown,   // Swim downward (] key)

    // Toggles
    ToggleAutorun,
    ToggleAutoAttack,
    ToggleInventory,
    ToggleSkills,
    ToggleGroup,
    TogglePetWindow,
    ToggleVendor,
    ToggleTrainer,
    ToggleSpellbook,    // Spellbook window (Ctrl+B)
    ToggleBuffWindow,   // Buff window (Alt+B)
    ToggleOptions,      // Options window (O key) - available in all modes
    ToggleCollision,
    ToggleCollisionDebug,
    ToggleZoneLineVisualization,
    ToggleMapOverlay,       // Map wireframe overlay (Ctrl+M)
    RotateMapOverlay,       // Rotate map overlay by 90 degrees (Ctrl+Shift+M)
    MirrorMapOverlayX,      // Mirror map overlay placeables on X axis (Ctrl+Alt+M)

    // Interaction
    InteractDoor,
    InteractWorldObject,
    Interact,           // Unified interact - nearest door/object/NPC
    Hail,
    ClearTarget,
    Consider,           // Consider target (C key, sends server packet)
    Attack,             // Initiate attack on target (Ctrl+Q, distinct from toggle)
    ReplyToTell,        // Reply to last tell (R key)

    // Targeting (F1-F8, Tab)
    TargetSelf,             // F1 - Target yourself
    TargetGroupMember1,     // F2 - Target group member 1
    TargetGroupMember2,     // F3 - Target group member 2
    TargetGroupMember3,     // F4 - Target group member 3
    TargetGroupMember4,     // F5 - Target group member 4
    TargetGroupMember5,     // F6 - Target group member 5
    TargetNearestPC,        // F7 - Target nearest player character
    TargetNearestNPC,       // F8 - Target nearest NPC
    CycleTargets,           // Tab - Cycle through nearby targets
    CycleTargetsReverse,    // Shift+Tab - Cycle targets in reverse

    // Chat
    OpenChat,
    OpenChatSlash,

    // Spell gems (1-8)
    SpellGem1, SpellGem2, SpellGem3, SpellGem4,
    SpellGem5, SpellGem6, SpellGem7, SpellGem8,

    // Hotbar slots (Ctrl+1-0)
    HotbarSlot1, HotbarSlot2, HotbarSlot3, HotbarSlot4, HotbarSlot5,
    HotbarSlot6, HotbarSlot7, HotbarSlot8, HotbarSlot9, HotbarSlot10,

    // Camera zoom
    CameraZoomIn,
    CameraZoomOut,

    // Audio volume
    MusicVolumeUp,
    MusicVolumeDown,
    EffectsVolumeUp,
    EffectsVolumeDown,

    // Lighting
    CycleObjectLights,

    // === Admin Mode Actions ===
    // Admin camera movement (free camera)
    CameraForward,
    CameraBackward,
    CameraLeft,
    CameraRight,
    CameraUp,
    CameraDown,

    SaveEntities,
    ToggleLighting,
    ToggleHelmDebug,
    HelmPrintState,
    AnimSpeedDecrease,
    AnimSpeedIncrease,
    AmbientLightDecrease,
    AmbientLightIncrease,
    CorpseZOffsetUp,
    CorpseZOffsetDown,
    EyeHeightUp,
    EyeHeightDown,
    ParticleMultiplierDecrease,
    ParticleMultiplierIncrease,
    DetailDensityDecrease,
    DetailDensityIncrease,
    HeadVariantPrev,
    HeadVariantNext,

    // Helm UV adjustments (Admin/HelmDebug mode)
    HelmUOffsetLeft,
    HelmUOffsetRight,
    HelmVOffsetUp,
    HelmVOffsetDown,
    HelmUScaleDecrease,
    HelmUScaleIncrease,
    HelmVScaleDecrease,
    HelmVScaleIncrease,
    HelmRotateLeft,
    HelmRotateRight,
    HelmReset,
    HelmUVSwap,
    HelmVFlip,
    HelmUFlip,

    // Collision height adjustments
    CollisionHeightUp,
    CollisionHeightDown,
    StepHeightUp,
    StepHeightDown,

    // === Repair Mode Actions ===
    RepairRotateXPos,
    RepairRotateXNeg,
    RepairRotateYPos,
    RepairRotateYNeg,
    RepairRotateZPos,
    RepairRotateZNeg,
    RepairFlipX,
    RepairFlipY,
    RepairFlipZ,
    RepairReset,

    Count
};

/**
 * HotkeyBinding - A single hotkey binding configuration.
 */
struct HotkeyBinding {
    irr::EKEY_CODE keyCode = irr::KEY_KEY_CODES_COUNT;
    ModifierFlags modifiers = ModifierFlags::None;
    HotkeyAction action = HotkeyAction::Count;
    HotkeyMode mode = HotkeyMode::Global;

    /**
     * Check if this binding matches the given key event.
     */
    bool matches(irr::EKEY_CODE key, bool ctrl, bool shift, bool alt) const;

    /**
     * Get a string representation for display/debugging.
     */
    std::string toString() const;

    bool operator==(const HotkeyBinding& other) const {
        return keyCode == other.keyCode && modifiers == other.modifiers;
    }
};

/**
 * ConflictInfo - Information about conflicting hotkey bindings.
 */
struct ConflictInfo {
    HotkeyBinding binding1;
    HotkeyBinding binding2;
    std::string message;
};

/**
 * HotkeyManager - Singleton class for managing configurable hotkey bindings.
 *
 * Features:
 * - Load/save hotkey configurations from JSON files
 * - Mode-based namespacing (Global, Player, Repair, Admin)
 * - Conflict detection with warnings
 * - Support for modifier key combinations (Ctrl+Alt+Key)
 * - Runtime reloading
 *
 * Default config: config/hotkeys.json
 * Override via willeq.json "hotkeys" section
 */
class HotkeyManager {
public:
    /**
     * Get the singleton instance.
     */
    static HotkeyManager& instance();

    // Prevent copying
    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    // === Load/Save Operations ===

    /**
     * Load hotkey configuration from a JSON file.
     * @param path Path to the config file (default: config/hotkeys.json)
     * @return true if loaded successfully
     */
    bool loadFromFile(const std::string& path = "config/hotkeys.json");

    /**
     * Save current hotkey configuration to a JSON file.
     * @param path Path to save to (empty = use last loaded path)
     * @return true if saved successfully
     */
    bool saveToFile(const std::string& path = "");

    /**
     * Apply hotkey overrides from a JSON object (e.g., from willeq.json).
     * @param overrides JSON object with same structure as hotkeys.json bindings section
     */
    void applyOverrides(const Json::Value& overrides);

    /**
     * Reset all bindings to hardcoded defaults.
     */
    void resetToDefaults();

    /**
     * Reload configuration from the last loaded file path.
     * @return true if reloaded successfully
     */
    bool reload();

    /**
     * Get the current config file path.
     */
    const std::string& getConfigPath() const { return m_configPath; }

    // === Binding Lookup ===

    /**
     * Look up the action for a key event in the current mode.
     * Checks Global bindings first, then mode-specific bindings.
     *
     * @param keyCode The key that was pressed
     * @param ctrl Whether Ctrl is held
     * @param shift Whether Shift is held
     * @param alt Whether Alt is held
     * @param currentMode The current renderer mode
     * @return The matching action, or empty if no binding found
     */
    std::optional<HotkeyAction> getAction(
        irr::EKEY_CODE keyCode,
        bool ctrl, bool shift, bool alt,
        HotkeyMode currentMode) const;

    /**
     * Check if a key (when held) is a movement key.
     * Movement keys don't use modifiers and are checked separately.
     *
     * @param keyCode The key being held
     * @param outAction Output: the movement action if found
     * @return true if this is a movement key
     */
    bool isMovementKey(irr::EKEY_CODE keyCode, HotkeyAction& outAction) const;

    /**
     * Get all bindings for an action (for UI display).
     */
    std::vector<HotkeyBinding> getBindingsForAction(HotkeyAction action) const;

    // === Conflict Detection ===

    /**
     * Detect all conflicting bindings.
     * Two bindings conflict if they have the same key+modifiers and:
     * - Are in the same mode, OR
     * - Either is in Global mode
     */
    std::vector<ConflictInfo> detectConflicts() const;

    /**
     * Log all conflicts as warnings.
     */
    void logConflicts() const;

    // === Utility ===

    /**
     * Convert a binding to a human-readable string (e.g., "Ctrl+Shift+F1").
     */
    static std::string bindingToString(const HotkeyBinding& binding);

    /**
     * Convert a key name string to an Irrlicht key code.
     * @param name Key name (e.g., "F1", "W", "Escape", "BracketLeft")
     * @return The key code, or KEY_KEY_CODES_COUNT if not found
     */
    static irr::EKEY_CODE keyNameToCode(const std::string& name);

    /**
     * Convert an Irrlicht key code to a key name string.
     */
    static std::string keyCodeToName(irr::EKEY_CODE code);

    /**
     * Convert an action name string to the enum value.
     * @param name Action name (e.g., "ToggleInventory")
     * @return The action, or HotkeyAction::Count if not found
     */
    static HotkeyAction actionNameToEnum(const std::string& name);

    /**
     * Convert an action enum to its name string.
     */
    static std::string actionEnumToName(HotkeyAction action);

    /**
     * Convert a mode name string to the enum value.
     */
    static HotkeyMode modeNameToEnum(const std::string& name);

    /**
     * Convert a mode enum to its name string.
     */
    static std::string modeEnumToName(HotkeyMode mode);

private:
    HotkeyManager();

    /**
     * Set up default bindings based on current hardcoded values.
     */
    void setupDefaults();

    /**
     * Parse a key binding string (e.g., "Ctrl+Shift+F1") into components.
     * @param bindingStr The binding string to parse
     * @param outKeyCode Output: the primary key code
     * @param outModifiers Output: the modifier flags
     * @return true if parsed successfully
     */
    bool parseBindingString(const std::string& bindingStr,
                           irr::EKEY_CODE& outKeyCode,
                           ModifierFlags& outModifiers) const;

    /**
     * Load bindings for a specific mode from JSON.
     */
    void loadModeBindings(const Json::Value& modeObj, HotkeyMode mode);

    /**
     * Rebuild the lookup index after bindings change.
     */
    void rebuildIndex();

    /**
     * Create a hash key for fast binding lookup.
     */
    uint32_t makeKeyHash(irr::EKEY_CODE key, ModifierFlags mods) const;

    // Configuration
    std::string m_configPath = "config/hotkeys.json";

    // All bindings
    std::vector<HotkeyBinding> m_bindings;

    // Fast lookup index: hash(key+modifiers) -> indices into m_bindings
    std::unordered_multimap<uint32_t, size_t> m_keyIndex;

    // Movement key lookup (no modifiers, just key -> action)
    std::unordered_map<irr::EKEY_CODE, HotkeyAction> m_movementKeys;
};

} // namespace input
} // namespace eqt
