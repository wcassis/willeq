#pragma once

#include <irrlicht.h>
#include <json/json.h>
#include <array>
#include <string>
#include <map>
#include <set>

namespace eqt {
namespace ui {

/**
 * UISettings - Centralized configuration for all UI windows and components.
 *
 * This singleton class manages all UI-related settings including:
 * - Global theme colors and layout constants
 * - Per-window position, size, and visibility settings
 * - Component-specific settings (tooltips, slots, chat input)
 * - UI lock state for preventing window movement/resizing
 *
 * Settings are loaded from config/ui_settings.json by default, and can be
 * overridden by the main config file (willeq.json).
 *
 * Position values:
 * - Positive values: offset from top-left
 * - -1: use calculated/default position
 * - Other negative values: offset from bottom-right edge
 */
class UISettings {
public:
    // Singleton access
    static UISettings& instance();

    // Prevent copying
    UISettings(const UISettings&) = delete;
    UISettings& operator=(const UISettings&) = delete;

    // Load/save operations
    bool loadFromFile(const std::string& path = "config/ui_settings.json");
    bool saveToFile(const std::string& path = "");  // Empty = use loaded path
    void applyOverrides(const Json::Value& overrides, const std::string& overrideSourcePath = "");
    void resetToDefaults();

    // Get/set the config file path (for saving)
    const std::string& getConfigPath() const { return m_configPath; }
    void setConfigPath(const std::string& path) { m_configPath = path; }

    // Called when a window is moved/resized - updates settings and auto-saves
    void updateWindowPosition(const std::string& windowName, int x, int y);
    void updateWindowSize(const std::string& windowName, int width, int height);
    void saveIfNeeded();  // Debounced save

    // UI Lock state
    bool isUILocked() const { return m_uiLocked; }
    void setUILocked(bool locked) { m_uiLocked = locked; }
    void toggleUILock() { m_uiLocked = !m_uiLocked; }

    // =========================================================================
    // Global Theme Settings
    // =========================================================================
    struct GlobalTheme {
        // Window chrome colors
        irr::video::SColor windowBackground{255, 64, 64, 64};
        irr::video::SColor titleBarBackground{255, 48, 48, 48};
        irr::video::SColor titleBarText{255, 200, 180, 140};
        irr::video::SColor borderLight{255, 100, 100, 100};
        irr::video::SColor borderDark{255, 32, 32, 32};

        // Button colors
        irr::video::SColor buttonBackground{255, 72, 72, 72};
        irr::video::SColor buttonHighlight{255, 96, 96, 96};
        irr::video::SColor buttonText{255, 200, 200, 200};
        irr::video::SColor buttonDisabledBg{255, 48, 48, 48};
        irr::video::SColor buttonDisabledText{255, 100, 100, 100};

        // Layout constants
        int titleBarHeight = 20;
        int borderWidth = 2;
        int buttonHeight = 22;
        int buttonPadding = 4;
        int contentPadding = 4;
    };

    // =========================================================================
    // Per-Window Settings
    // =========================================================================
    struct WindowSettings {
        // Position (-1 = calculated default, negative = offset from right/bottom)
        int x = -1;
        int y = -1;

        // Size (-1 = calculated default)
        int width = -1;
        int height = -1;

        // Size constraints
        int minWidth = 50;
        int minHeight = 50;
        int maxWidth = 1920;
        int maxHeight = 1080;

        // Visibility
        bool visible = false;
        bool showTitleBar = true;

        // Per-window lock override (if true, window is always locked regardless of global state)
        bool alwaysLocked = false;
    };

    // =========================================================================
    // Chat Window Settings
    // =========================================================================
    struct ChatSettings {
        WindowSettings window;

        // Layout
        int inputFieldHeight = 20;
        int scrollbarWidth = 14;
        int scrollButtonHeight = 14;
        int resizeEdgeWidth = 6;

        // Colors
        irr::video::SColor linkColorNpc{255, 0, 255, 255};
        irr::video::SColor linkColorItem{255, 0, 200, 255};

        // Options
        bool showTimestamps = false;

        // Channel filters (channel ID -> enabled)
        std::set<int> enabledChannels;

        ChatSettings();
    };

    // =========================================================================
    // Inventory Window Settings
    // =========================================================================
    struct InventorySettings {
        WindowSettings window;

        // Layout
        int slotSize = 32;
        int slotSpacing = 2;
        int equipmentStartX = 80;
        int equipmentStartY = 25;
        int generalStartX = 272;
        int generalStartY = 25;
        int statsWidth = 70;

        InventorySettings();
    };

    // =========================================================================
    // Loot Window Settings
    // =========================================================================
    struct LootSettings {
        WindowSettings window;

        // Layout
        int columns = 4;
        int rows = 2;
        int slotSize = 36;
        int slotSpacing = 4;
        int padding = 8;
        int buttonWidth = 70;
        int buttonSpacing = 8;
        int scrollbarWidth = 16;
        int scrollButtonHeight = 16;
        int topButtonRowHeight = 26;
        int bottomButtonRowHeight = 26;

        LootSettings();
    };

    // =========================================================================
    // Buff Window Settings
    // =========================================================================
    struct BuffSettings {
        WindowSettings window;

        // Layout
        int columns = 5;
        int rows = 3;
        int iconSize = 24;
        int spacing = 2;
        int padding = 1;

        // Colors
        irr::video::SColor buffBackground{200, 30, 50, 30};
        irr::video::SColor buffBorder{255, 80, 140, 80};
        irr::video::SColor debuffBorder{255, 140, 50, 50};
        irr::video::SColor emptySlot{150, 40, 40, 40};
        irr::video::SColor durationBackground{180, 0, 0, 0};
        irr::video::SColor durationText{255, 220, 220, 220};

        BuffSettings();
    };

    // =========================================================================
    // Group Window Settings
    // =========================================================================
    struct GroupSettings {
        WindowSettings window;

        // Layout
        int nameHeight = 12;
        int barHeight = 2;
        int barSpacing = 1;
        int memberSpacing = 2;
        int buttonWidth = 55;
        int buttonHeight = 18;
        int padding = 1;
        int buttonPadding = 2;

        // Colors
        irr::video::SColor hpBackground{200, 40, 0, 0};
        irr::video::SColor hpHigh{255, 0, 180, 0};
        irr::video::SColor hpMedium{255, 180, 180, 0};
        irr::video::SColor hpLow{255, 180, 0, 0};
        irr::video::SColor manaBackground{200, 0, 0, 40};
        irr::video::SColor manaFill{255, 0, 80, 180};
        irr::video::SColor memberBackground{100, 30, 30, 30};
        irr::video::SColor nameInZone{255, 255, 255, 255};
        irr::video::SColor nameOutOfZone{255, 128, 128, 128};
        irr::video::SColor leaderMarker{255, 255, 215, 0};

        GroupSettings();
    };

    // =========================================================================
    // Player Status Window Settings
    // =========================================================================
    struct PlayerStatusSettings {
        WindowSettings window;

        // Layout
        int nameHeight = 14;
        int barHeight = 10;  // Tall enough for text overlay
        int barSpacing = 2;
        int barLabelWidth = 50;  // Width for numeric values like "100 / 200"
        int padding = 4;

        // Colors
        irr::video::SColor nameText{255, 255, 255, 255};
        irr::video::SColor hpBackground{200, 40, 0, 0};
        irr::video::SColor hpFill{255, 200, 0, 0};
        irr::video::SColor manaBackground{200, 0, 0, 40};
        irr::video::SColor manaFill{255, 0, 80, 200};
        irr::video::SColor staminaBackground{200, 40, 40, 0};
        irr::video::SColor staminaFill{255, 200, 200, 0};
        irr::video::SColor barText{255, 255, 255, 255};

        PlayerStatusSettings();
    };

    // =========================================================================
    // Spell Gem Panel Settings
    // =========================================================================
    struct SpellGemSettings {
        // Position (-1 for y = vertically centered)
        int x = 10;
        int y = -1;
        bool visible = true;

        // Layout
        int gemWidth = 20;
        int gemHeight = 20;
        int gemSpacing = 2;
        int panelPadding = 2;
        int spellbookButtonSize = 16;
        int spellbookButtonMargin = 3;
    };

    // =========================================================================
    // Skills Window Settings
    // =========================================================================
    struct SkillsSettings {
        WindowSettings window;

        SkillsSettings();
    };

    // =========================================================================
    // Spellbook Window Settings
    // =========================================================================
    struct SpellBookSettings {
        WindowSettings window;

        // Layout
        int pageWidth = 160;
        int pageSpacing = 16;
        int spellsPerPage = 8;
        int iconSize = 20;
        int rowHeight = 22;
        int rowSpacing = 2;
        int leftPageX = 8;
        int slotsStartY = 28;
        int nameOffsetX = 24;
        int nameMaxWidth = 130;
        int navButtonWidth = 30;
        int navButtonHeight = 20;

        SpellBookSettings();
    };

    // =========================================================================
    // Hotbar Window Settings
    // =========================================================================
    struct HotbarSettings {
        WindowSettings window;

        // Layout
        int buttonSize = 32;
        int buttonSpacing = 2;
        int padding = 4;
        int buttonCount = 10;

        // Per-button saved data (10 buttons max)
        struct ButtonData {
            int type = 0;           // HotbarButtonType as int
            uint32_t id = 0;
            std::string emoteText;
            uint32_t iconId = 0;
        };
        std::array<ButtonData, 10> buttons;

        HotbarSettings();
    };

    // =========================================================================
    // Trade Window Settings
    // =========================================================================
    struct TradeSettings {
        WindowSettings window;

        TradeSettings();
    };

    // =========================================================================
    // Bank Window Settings
    // =========================================================================
    struct BankSettings {
        WindowSettings window;

        // Layout
        int slotSize = 36;
        int slotSpacing = 4;
        int padding = 8;

        BankSettings();
    };

    // =========================================================================
    // Skill Trainer Window Settings
    // =========================================================================
    struct SkillTrainerSettings {
        WindowSettings window;

        SkillTrainerSettings();
    };

    // =========================================================================
    // Casting Bar Settings
    // =========================================================================
    struct CastingBarSettings {
        // Player casting bar
        struct BarSettings {
            int x = -1;  // -1 = centered
            int y = -1;  // -1 = above chat window
            int width = 200;
            int height = 16;
        };

        BarSettings playerBar;
        BarSettings targetBar;

        // Colors
        irr::video::SColor barFill{255, 100, 150, 255};
        irr::video::SColor background{200, 40, 40, 60};
        irr::video::SColor border{255, 150, 150, 180};
        irr::video::SColor text{255, 255, 255, 255};
        irr::video::SColor interrupted{255, 255, 80, 80};

        CastingBarSettings();
    };

    // =========================================================================
    // Item Tooltip Settings
    // =========================================================================
    struct ItemTooltipSettings {
        int minWidth = 250;
        int maxWidth = 350;
        int lineHeight = 14;
        int padding = 8;
        int sectionSpacing = 4;
        int mouseOffset = 16;

        // Colors
        irr::video::SColor background{240, 24, 24, 24};
        irr::video::SColor border{255, 80, 80, 80};
        irr::video::SColor title{255, 240, 220, 180};
        irr::video::SColor magic{255, 100, 149, 237};
        irr::video::SColor flags{255, 200, 200, 200};
        irr::video::SColor restrictions{255, 180, 180, 180};
        irr::video::SColor stats{255, 200, 200, 200};
        irr::video::SColor statsValue{255, 255, 255, 255};
        irr::video::SColor effect{255, 180, 220, 180};
        irr::video::SColor effectDesc{255, 160, 200, 160};
        irr::video::SColor lore{255, 180, 160, 140};
    };

    // =========================================================================
    // Buff Tooltip Settings
    // =========================================================================
    struct BuffTooltipSettings {
        int minWidth = 180;
        int maxWidth = 280;
        int lineHeight = 14;
        int padding = 6;
        int mouseOffset = 12;

        // Colors
        irr::video::SColor background{230, 20, 20, 30};
        irr::video::SColor border{255, 80, 80, 100};
        irr::video::SColor spellName{255, 255, 255, 100};
        irr::video::SColor duration{255, 200, 200, 200};
        irr::video::SColor info{255, 180, 180, 180};
    };

    // =========================================================================
    // Item Slot Settings
    // =========================================================================
    struct SlotSettings {
        int defaultSize = 32;
        int iconPadding = 2;

        // Colors
        irr::video::SColor background{255, 32, 32, 32};
        irr::video::SColor border{255, 64, 64, 64};
        irr::video::SColor highlight{255, 128, 128, 64};
        irr::video::SColor invalid{255, 128, 32, 32};
        irr::video::SColor itemBackground{255, 48, 48, 48};
        irr::video::SColor stackText{255, 255, 255, 255};
        irr::video::SColor labelText{255, 160, 160, 160};
    };

    // =========================================================================
    // Chat Input Field Settings
    // =========================================================================
    struct ChatInputSettings {
        int blinkIntervalMs = 500;
        int maxHistory = 100;

        // Colors
        irr::video::SColor background{200, 0, 0, 0};
        irr::video::SColor text{255, 255, 255, 255};
        irr::video::SColor cursor{255, 255, 255, 255};
        irr::video::SColor prompt{255, 200, 200, 200};
    };

    // =========================================================================
    // Scrollbar Settings
    // =========================================================================
    struct ScrollbarSettings {
        // Colors
        irr::video::SColor trackColor{255, 40, 40, 40};
        irr::video::SColor thumbColor{255, 100, 100, 100};
        irr::video::SColor thumbHoverColor{255, 130, 130, 130};
        irr::video::SColor buttonColor{255, 70, 70, 70};
        irr::video::SColor buttonHoverColor{255, 100, 100, 100};
        irr::video::SColor arrowColor{255, 180, 180, 180};
    };

    // =========================================================================
    // Accessors
    // =========================================================================
    GlobalTheme& globalTheme() { return m_globalTheme; }
    const GlobalTheme& globalTheme() const { return m_globalTheme; }

    ChatSettings& chat() { return m_chat; }
    const ChatSettings& chat() const { return m_chat; }

    InventorySettings& inventory() { return m_inventory; }
    const InventorySettings& inventory() const { return m_inventory; }

    LootSettings& loot() { return m_loot; }
    const LootSettings& loot() const { return m_loot; }

    BuffSettings& buff() { return m_buff; }
    const BuffSettings& buff() const { return m_buff; }

    GroupSettings& group() { return m_group; }
    const GroupSettings& group() const { return m_group; }

    PlayerStatusSettings& playerStatus() { return m_playerStatus; }
    const PlayerStatusSettings& playerStatus() const { return m_playerStatus; }

    SpellGemSettings& spellGems() { return m_spellGems; }
    const SpellGemSettings& spellGems() const { return m_spellGems; }

    SkillsSettings& skills() { return m_skills; }
    const SkillsSettings& skills() const { return m_skills; }

    SpellBookSettings& spellBook() { return m_spellBook; }
    const SpellBookSettings& spellBook() const { return m_spellBook; }

    HotbarSettings& hotbar() { return m_hotbar; }
    const HotbarSettings& hotbar() const { return m_hotbar; }

    TradeSettings& trade() { return m_trade; }
    const TradeSettings& trade() const { return m_trade; }

    BankSettings& bank() { return m_bank; }
    const BankSettings& bank() const { return m_bank; }

    SkillTrainerSettings& skillTrainer() { return m_skillTrainer; }
    const SkillTrainerSettings& skillTrainer() const { return m_skillTrainer; }

    CastingBarSettings& castingBar() { return m_castingBar; }
    const CastingBarSettings& castingBar() const { return m_castingBar; }

    ItemTooltipSettings& itemTooltip() { return m_itemTooltip; }
    const ItemTooltipSettings& itemTooltip() const { return m_itemTooltip; }

    BuffTooltipSettings& buffTooltip() { return m_buffTooltip; }
    const BuffTooltipSettings& buffTooltip() const { return m_buffTooltip; }

    SlotSettings& slots() { return m_slots; }
    const SlotSettings& slots() const { return m_slots; }

    ChatInputSettings& chatInput() { return m_chatInput; }
    const ChatInputSettings& chatInput() const { return m_chatInput; }

    ScrollbarSettings& scrollbar() { return m_scrollbar; }
    const ScrollbarSettings& scrollbar() const { return m_scrollbar; }

    // Helper to resolve position values
    // If value is -1, returns defaultVal
    // If value is negative (but not -1), treats as offset from screenSize
    static int resolvePosition(int value, int screenSize, int defaultVal);

private:
    UISettings();

    // Serialization helpers
    void loadGlobalTheme(const Json::Value& json);
    void saveGlobalTheme(Json::Value& json) const;

    void loadWindowSettings(WindowSettings& settings, const Json::Value& json);
    void saveWindowSettings(const WindowSettings& settings, Json::Value& json) const;

    void loadChatSettings(const Json::Value& json);
    void saveChatSettings(Json::Value& json) const;

    void loadInventorySettings(const Json::Value& json);
    void saveInventorySettings(Json::Value& json) const;

    void loadLootSettings(const Json::Value& json);
    void saveLootSettings(Json::Value& json) const;

    void loadBuffSettings(const Json::Value& json);
    void saveBuffSettings(Json::Value& json) const;

    void loadGroupSettings(const Json::Value& json);
    void saveGroupSettings(Json::Value& json) const;

    void loadPlayerStatusSettings(const Json::Value& json);
    void savePlayerStatusSettings(Json::Value& json) const;

    void loadSpellGemSettings(const Json::Value& json);
    void saveSpellGemSettings(Json::Value& json) const;

    void loadSkillsSettings(const Json::Value& json);
    void saveSkillsSettings(Json::Value& json) const;

    void loadSpellBookSettings(const Json::Value& json);
    void saveSpellBookSettings(Json::Value& json) const;

    void loadHotbarSettings(const Json::Value& json);
    void saveHotbarSettings(Json::Value& json) const;

    void loadTradeSettings(const Json::Value& json);
    void saveTradeSettings(Json::Value& json) const;

    void loadBankSettings(const Json::Value& json);
    void saveBankSettings(Json::Value& json) const;

    void loadSkillTrainerSettings(const Json::Value& json);
    void saveSkillTrainerSettings(Json::Value& json) const;

    void loadCastingBarSettings(const Json::Value& json);
    void saveCastingBarSettings(Json::Value& json) const;

    void loadTooltipSettings(const Json::Value& json);
    void saveTooltipSettings(Json::Value& json) const;

    void loadSlotSettings(const Json::Value& json);
    void saveSlotSettings(Json::Value& json) const;

    void loadChatInputSettings(const Json::Value& json);
    void saveChatInputSettings(Json::Value& json) const;

    void loadScrollbarSettings(const Json::Value& json);
    void saveScrollbarSettings(Json::Value& json) const;

    // Color serialization
    static irr::video::SColor jsonToColor(const Json::Value& json, const irr::video::SColor& defaultColor);
    static Json::Value colorToJson(const irr::video::SColor& color);

    // Config file path (for saving)
    std::string m_configPath = "config/ui_settings.json";
    bool m_dirty = false;  // True if settings have changed since last save

    // Settings data
    bool m_uiLocked = true;

    GlobalTheme m_globalTheme;
    ChatSettings m_chat;
    InventorySettings m_inventory;
    LootSettings m_loot;
    BuffSettings m_buff;
    GroupSettings m_group;
    PlayerStatusSettings m_playerStatus;
    SpellGemSettings m_spellGems;
    SkillsSettings m_skills;
    SpellBookSettings m_spellBook;
    HotbarSettings m_hotbar;
    TradeSettings m_trade;
    BankSettings m_bank;
    SkillTrainerSettings m_skillTrainer;
    CastingBarSettings m_castingBar;
    ItemTooltipSettings m_itemTooltip;
    BuffTooltipSettings m_buffTooltip;
    SlotSettings m_slots;
    ChatInputSettings m_chatInput;
    ScrollbarSettings m_scrollbar;
};

} // namespace ui
} // namespace eqt
