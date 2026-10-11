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
    Global,   // Always active (F12, Shift+Escape)
    Player    // Active in Player mode (WASD, Q, F1-F8, 1-0, Alt+1-8)
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
    ToggleCameraMode,   // T461: F9, cycles the official camera set (Ctrl+F5 retired, no alias)
    ToggleOldModels,
    ToggleAllUI,
    // T313: scroll the static-UI panel under the cursor (chat scrollback,
    // the skills list, the loot list). Global, not Player, because the two
    // popups that scroll are reachable in any mode.
    ScrollUIUp,
    ScrollUIDown,
    // B905 (#225, UIGAP-small) DELETED eight Global actions that were bound
    // and had no dispatch consumer: ToggleUILock, SaveUILayout,
    // ResetUIDefaults (the deleted WindowManager's draggable-window layout --
    // fable's windows are static by design), ConfirmDialog, CancelDialog,
    // SubmitInput, CancelInput, ChatAutocomplete (Return / Escape / Tab / Y /
    // N: text-entry and dialog keys that the entry or the ESC stack owns
    // directly). None is on the oracle's measured Options > Keys UI or
    // Commands pages, and as Global bindings they SHADOWED real Player keys
    // (Tab = CycleTargets, Escape = ClearTarget, Return = OpenChat). Their
    // names are still accepted -- and ignored -- in a saved hotkeys.json
    // (hotkey_manager.cpp kRetiredActionNames). Pinned by
    // tests/test_hotkey_dispatch_census.cpp.

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
    ToggleCombatAbilities,  // T415: Combat abilities window (Alt+C, measured
                            // at the oracle's Options > Keys > UI page)
    ToggleOptions,      // T434: Options window (Alt+O, measured at the
                        // oracle's Options > Keys > UI page) - available in
                        // all modes
    ToggleFriends,      // B897: Friends window (Alt+F, measured at the
                        // oracle's Options > Keys > UI page, eqstr 6599)
    ToggleCollision,
    ToggleCollisionDebug,
    ToggleZoneLineVisualization,
    ToggleMapOverlay,       // Map wireframe overlay (Ctrl+M)
    RotateMapOverlay,       // Rotate map overlay by 90 degrees (Ctrl+Shift+M)
    MirrorMapOverlayX,      // Mirror map overlay placeables on X axis (Ctrl+Alt+M)
    ToggleNavmeshOverlay,   // Navmesh wireframe overlay (Ctrl+N)
    RotateNavmeshOverlay,   // Rotate navmesh overlay by 90 degrees (Ctrl+Shift+N)
    MirrorNavmeshOverlayX,  // Mirror navmesh overlay on X axis (Ctrl+Alt+N)
    ToggleFrustumCulling,   // Toggle frustum culling on/off (Ctrl+V)

    // Interaction
    InteractDoor,
    InteractWorldObject,
    Interact,           // Unified interact - nearest door/object/NPC
    Hail,
    ClearTarget,
    Consider,           // Consider target (C key, sends server packet)
    Attack,             // Initiate attack on target (Ctrl+Q, distinct from toggle)
    // B900 (#216): one ranged shot (bow/crossbow/throwing) -> OP_CombatAbility
    // with m_atk = slotRange. The oracle's default key is [assumed]-owed (Leg
    // O: read from the live eqclient.ini [KeyMaps]), so it is UNBOUND in
    // setupDefaults() and config/hotkeys.json; it is listed on the Options
    // Keys page where the user can bind it.
    RangeAttack,
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

    // Camera zoom. T471: bound to the official Zoom in NPAD7 / INSERT and Zoom
    // out NPAD1 / DELETE (measured, T466) as well as fable's own +/-.
    CameraZoomIn,
    CameraZoomOut,
    // T461 named these four after eqgame.exe's KEYMAPPING_ LEFT_CAM, RIGHT_CAM,
    // PITCHUP_CAM and PITCHDOWN_CAM and left them unbound; T471 binds them to
    // the keys T466 MEASURED on the official Options > Keys > Camera page
    // (board B531):
    //   CameraOrbitLeft/Right -- "Tether-slide camera left/right", Alt NPAD4 /
    //     Alt LEFT and Alt NPAD6 / Alt RIGHT: slides the TETHER camera.
    //   CameraPitchUp/Down -- "Pitch camera up/down", Alt NPAD9 / Alt PGUP and
    //     Alt NPAD3 / Alt PGDOWN: pitches the active USER camera; Pitch up
    //     tilts the view up, as the official key does.
    CameraOrbitLeft,
    CameraOrbitRight,
    CameraPitchUp,
    CameraPitchDown,
    // T471: "Raise camera" / "Lower camera", Alt NPAD8 / Alt UP and Alt NPAD2
    // / Alt DOWN (measured, T466) -- FORWARD_CAM / BACK_CAM by the order the
    // exe's action table and eqstr 6692-6697 share (an inference). fable moves
    // the Tether camera's look-at height; the official effect and step are
    // ORACLE-OWED (board B653).
    CameraRaise,
    CameraLower,

    // Audio volume
    MusicVolumeUp,
    MusicVolumeDown,
    EffectsVolumeUp,
    EffectsVolumeDown,

    // Lighting
    CycleObjectLights,

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
 * B345 deleted binding1/binding2 (the report never formats them); the
 * message string carries both actions and both bindings.
 */
struct ConflictInfo {
    std::string message;
};

/**
 * HotkeyManager - Singleton class for managing configurable hotkey bindings.
 *
 * Features:
 * - Load/save hotkey configurations from JSON files
 * - Mode-based namespacing (Global, Player)
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
     *
     * A file that loads REPLACES every binding (the compiled-in defaults are
     * the fallback for a file that cannot be opened or parsed, which returns
     * false with the bindings untouched). So the shipped config/hotkeys.json
     * must carry every default: T464 (board B535) found five it did not, and
     * test_hotkey_manager.cpp T464_TheShippedFileCarriesEveryDefaultBinding
     * now compares the two tables binding by binding. A new default binding
     * goes in setupDefaults() AND the shipped file.
     *
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

    // === B433: the Options window's Keys page (hotkey_manager_user.cpp) ===
    //
    // Bindings stay GLOBAL (not per character -- T432's settled rule). A
    // rebinding is written to the user copy `<cache dir>/hotkeys.json`
    // (T402's generated-state rule: never the shipped config/hotkeys.json in
    // the checkout), and the client loads that copy instead of the shipped
    // file when it exists. "Reload defaults" deletes it and reloads the
    // shipped file.

    /**
     * Bind `action`'s `slot` (0 = Keypress, 1 = Alternate column) to key +
     * modifiers. The same key+modifiers is taken off every OTHER action first,
     * so a key never means two things. A slot past the action's last binding
     * appends. Returns false for an invalid action/key.
     */
    bool setBinding(HotkeyAction action, int slot, irr::EKEY_CODE key, ModifierFlags mods);

    /** Write every binding to the user copy. False if no cache dir is writable. */
    bool saveUserBindings();

    /** Delete the user copy and reload the shipped bindings. */
    bool restoreShippedBindings();

    /** What the client loads at startup: the user copy if present, else the shipped file. */
    static std::string startupBindingsPath();

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
