#include "client/graphics/ui/ui_settings.h"
#include "common/logging.h"
#include <fstream>

namespace eqt {
namespace ui {

// ============================================================================
// Constructors for nested settings structs
// ============================================================================

UISettings::ChatSettings::ChatSettings() {
    window.x = 10;
    window.y = -182;  // Offset from bottom
    window.width = 651;
    window.height = 172;
    window.minWidth = 200;
    window.minHeight = 80;
    window.maxWidth = 1920;
    window.maxHeight = 600;
    window.visible = true;
    window.showTitleBar = false;

    // Enable all channels by default
    enabledChannels = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 100, 101, 269};
}

UISettings::InventorySettings::InventorySettings() {
    window.x = 50;
    window.y = 50;
    window.width = 365;
    window.height = 340;
    window.visible = false;
    window.showTitleBar = true;
}

UISettings::LootSettings::LootSettings() {
    window.x = -1;  // Calculated (right of inventory)
    window.y = -1;  // Same as inventory
    window.visible = false;
    window.showTitleBar = true;
}

UISettings::BuffSettings::BuffSettings() {
    window.x = 650;
    window.y = 53;
    window.visible = true;
    window.showTitleBar = false;
}

UISettings::GroupSettings::GroupSettings() {
    window.x = -1;  // Calculated (right side of screen)
    window.y = 210;
    window.visible = false;
    window.showTitleBar = false;
}

UISettings::PlayerStatusSettings::PlayerStatusSettings() {
    window.x = 10;
    window.y = 10;
    window.width = 90;
    window.height = 110;  // Taller to fit player + target info with full-height bars
    window.visible = true;
    window.showTitleBar = false;
    window.alwaysLocked = true;  // Player status window should always be locked
    barHeight = 10;  // Tall enough for text overlay
}

UISettings::SkillsSettings::SkillsSettings() {
    window.x = 50;
    window.y = 50;
    window.width = 340;
    window.height = 400;
    window.visible = false;
    window.showTitleBar = true;
}

UISettings::SpellBookSettings::SpellBookSettings() {
    window.x = 106;
    window.y = 131;
    window.width = 352;
    window.height = 280;
    window.visible = false;
    window.showTitleBar = true;
}

UISettings::TradeSettings::TradeSettings() {
    window.x = -1;  // Centered by default
    window.y = -1;
    window.visible = false;
    window.showTitleBar = true;
}

UISettings::CastingBarSettings::CastingBarSettings() {
    playerBar.x = -1;  // Centered
    playerBar.y = -1;  // Above chat
    targetBar.x = -1;  // Centered
    targetBar.y = 80;
}

UISettings::HotbarSettings::HotbarSettings() {
    window.x = -1;  // Centered
    window.y = -1;  // Above chat
    window.visible = true;
    window.showTitleBar = false;
}

// ============================================================================
// Singleton
// ============================================================================

UISettings& UISettings::instance() {
    static UISettings instance;
    return instance;
}

UISettings::UISettings() {
    resetToDefaults();
}

// ============================================================================
// Load/Save Operations
// ============================================================================

bool UISettings::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_DEBUG(MOD_UI, "[UISettings] Config file not found: {}, using defaults", path);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_UI, "[UISettings] Failed to parse {}: {}", path, errors);
        return false;
    }

    // Load UI lock state
    if (root.isMember("uiLocked")) {
        m_uiLocked = root["uiLocked"].asBool();
    }

    // Load global theme
    if (root.isMember("globalTheme")) {
        loadGlobalTheme(root["globalTheme"]);
    }

    // Load window settings
    if (root.isMember("windows")) {
        const Json::Value& windows = root["windows"];

        if (windows.isMember("chat")) {
            loadChatSettings(windows["chat"]);
        }
        if (windows.isMember("inventory")) {
            loadInventorySettings(windows["inventory"]);
        }
        if (windows.isMember("loot")) {
            loadLootSettings(windows["loot"]);
        }
        if (windows.isMember("buff")) {
            loadBuffSettings(windows["buff"]);
        }
        if (windows.isMember("group")) {
            loadGroupSettings(windows["group"]);
        }
        if (windows.isMember("playerStatus")) {
            loadPlayerStatusSettings(windows["playerStatus"]);
        }
        if (windows.isMember("spellGems")) {
            loadSpellGemSettings(windows["spellGems"]);
        }
        if (windows.isMember("skills")) {
            loadSkillsSettings(windows["skills"]);
        }
        if (windows.isMember("spellbook")) {
            loadSpellBookSettings(windows["spellbook"]);
        }
        if (windows.isMember("trade")) {
            loadTradeSettings(windows["trade"]);
        }
        if (windows.isMember("castingBar")) {
            loadCastingBarSettings(windows["castingBar"]);
        }
        if (windows.isMember("hotbar")) {
            loadHotbarSettings(windows["hotbar"]);
        }
    }

    // Load tooltip settings
    if (root.isMember("tooltips")) {
        loadTooltipSettings(root["tooltips"]);
    }

    // Store the config path for future saves
    m_configPath = path;
    m_dirty = false;

    // Load slot settings
    if (root.isMember("slots")) {
        loadSlotSettings(root["slots"]);
    }

    // Load chat input settings
    if (root.isMember("chatInput")) {
        loadChatInputSettings(root["chatInput"]);
    }

    // Load scrollbar settings
    if (root.isMember("scrollbar")) {
        loadScrollbarSettings(root["scrollbar"]);
    }

    LOG_INFO(MOD_UI, "[UISettings] Loaded settings from {}", path);
    return true;
}

bool UISettings::saveToFile(const std::string& path) {
    // Use provided path or fall back to stored config path
    std::string savePath = path.empty() ? m_configPath : path;
    if (savePath.empty()) {
        savePath = "config/ui_settings.json";
    }

    Json::Value root;

    root["version"] = 1;
    root["uiLocked"] = m_uiLocked;

    // Save global theme
    saveGlobalTheme(root["globalTheme"]);

    // Save window settings
    Json::Value& windows = root["windows"];
    saveChatSettings(windows["chat"]);
    saveInventorySettings(windows["inventory"]);
    saveLootSettings(windows["loot"]);
    saveBuffSettings(windows["buff"]);
    saveGroupSettings(windows["group"]);
    savePlayerStatusSettings(windows["playerStatus"]);
    saveSpellGemSettings(windows["spellGems"]);
    saveSkillsSettings(windows["skills"]);
    saveSpellBookSettings(windows["spellbook"]);
    saveTradeSettings(windows["trade"]);
    saveCastingBarSettings(windows["castingBar"]);
    saveHotbarSettings(windows["hotbar"]);

    // Save tooltip settings
    saveTooltipSettings(root["tooltips"]);

    // Save slot settings
    saveSlotSettings(root["slots"]);

    // Save chat input settings
    saveChatInputSettings(root["chatInput"]);

    // Save scrollbar settings
    saveScrollbarSettings(root["scrollbar"]);

    // Write to file
    std::ofstream file(savePath);
    if (!file.is_open()) {
        LOG_ERROR(MOD_UI, "[UISettings] Failed to open {} for writing", savePath);
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);

    m_dirty = false;
    LOG_INFO(MOD_UI, "[UISettings] Saved settings to {}", savePath);
    return true;
}

void UISettings::applyOverrides(const Json::Value& overrides, const std::string& overrideSourcePath) {
    // If overrides come from command-line config, use that path for saving
    if (!overrideSourcePath.empty()) {
        m_configPath = overrideSourcePath;
    }

    // Apply overrides in the same manner as loadFromFile
    if (overrides.isMember("uiLocked")) {
        m_uiLocked = overrides["uiLocked"].asBool();
    }

    if (overrides.isMember("globalTheme")) {
        loadGlobalTheme(overrides["globalTheme"]);
    }

    if (overrides.isMember("windows")) {
        const Json::Value& windows = overrides["windows"];

        if (windows.isMember("chat")) {
            loadChatSettings(windows["chat"]);
        }
        if (windows.isMember("inventory")) {
            loadInventorySettings(windows["inventory"]);
        }
        if (windows.isMember("loot")) {
            loadLootSettings(windows["loot"]);
        }
        if (windows.isMember("buff")) {
            loadBuffSettings(windows["buff"]);
        }
        if (windows.isMember("group")) {
            loadGroupSettings(windows["group"]);
        }
        if (windows.isMember("playerStatus")) {
            loadPlayerStatusSettings(windows["playerStatus"]);
        }
        if (windows.isMember("spellGems")) {
            loadSpellGemSettings(windows["spellGems"]);
        }
        if (windows.isMember("skills")) {
            loadSkillsSettings(windows["skills"]);
        }
        if (windows.isMember("spellbook")) {
            loadSpellBookSettings(windows["spellbook"]);
        }
        if (windows.isMember("trade")) {
            loadTradeSettings(windows["trade"]);
        }
        if (windows.isMember("castingBar")) {
            loadCastingBarSettings(windows["castingBar"]);
        }
        if (windows.isMember("hotbar")) {
            loadHotbarSettings(windows["hotbar"]);
        }
    }

    if (overrides.isMember("tooltips")) {
        loadTooltipSettings(overrides["tooltips"]);
    }

    if (overrides.isMember("slots")) {
        loadSlotSettings(overrides["slots"]);
    }

    if (overrides.isMember("chatInput")) {
        loadChatInputSettings(overrides["chatInput"]);
    }

    if (overrides.isMember("scrollbar")) {
        loadScrollbarSettings(overrides["scrollbar"]);
    }

    LOG_DEBUG(MOD_UI, "[UISettings] Applied config overrides");
}

void UISettings::resetToDefaults() {
    m_uiLocked = true;
    m_globalTheme = GlobalTheme{};
    m_chat = ChatSettings{};
    m_inventory = InventorySettings{};
    m_loot = LootSettings{};
    m_buff = BuffSettings{};
    m_group = GroupSettings{};
    m_playerStatus = PlayerStatusSettings{};
    m_spellGems = SpellGemSettings{};
    m_spellBook = SpellBookSettings{};
    m_hotbar = HotbarSettings{};
    m_castingBar = CastingBarSettings{};
    m_itemTooltip = ItemTooltipSettings{};
    m_buffTooltip = BuffTooltipSettings{};
    m_slots = SlotSettings{};
    m_chatInput = ChatInputSettings{};
    m_scrollbar = ScrollbarSettings{};

    LOG_DEBUG(MOD_UI, "[UISettings] Reset to defaults");
}

// ============================================================================
// Position Resolution Helper
// ============================================================================

int UISettings::resolvePosition(int value, int screenSize, int defaultVal) {
    if (value == -1) {
        return defaultVal;
    }
    if (value < -1) {
        // Negative value (other than -1) means offset from right/bottom edge
        return screenSize + value;
    }
    return value;
}

// ============================================================================
// Color Serialization
// ============================================================================

irr::video::SColor UISettings::jsonToColor(const Json::Value& json, const irr::video::SColor& defaultColor) {
    if (!json.isArray() || json.size() < 3) {
        return defaultColor;
    }

    int r = json[0].asInt();
    int g = json[1].asInt();
    int b = json[2].asInt();
    int a = (json.size() >= 4) ? json[3].asInt() : 255;

    return irr::video::SColor(a, r, g, b);
}

Json::Value UISettings::colorToJson(const irr::video::SColor& color) {
    Json::Value arr(Json::arrayValue);
    arr.append(static_cast<int>(color.getRed()));
    arr.append(static_cast<int>(color.getGreen()));
    arr.append(static_cast<int>(color.getBlue()));
    arr.append(static_cast<int>(color.getAlpha()));
    return arr;
}

// ============================================================================
// Window Settings Serialization
// ============================================================================

void UISettings::loadWindowSettings(WindowSettings& settings, const Json::Value& json) {
    if (json.isMember("position")) {
        const Json::Value& pos = json["position"];
        if (pos.isMember("x")) settings.x = pos["x"].asInt();
        if (pos.isMember("y")) settings.y = pos["y"].asInt();
    }

    if (json.isMember("size")) {
        const Json::Value& size = json["size"];
        if (size.isMember("width")) settings.width = size["width"].asInt();
        if (size.isMember("height")) settings.height = size["height"].asInt();
    }

    if (json.isMember("minSize")) {
        const Json::Value& minSize = json["minSize"];
        if (minSize.isMember("width")) settings.minWidth = minSize["width"].asInt();
        if (minSize.isMember("height")) settings.minHeight = minSize["height"].asInt();
    }

    if (json.isMember("maxSize")) {
        const Json::Value& maxSize = json["maxSize"];
        if (maxSize.isMember("width")) settings.maxWidth = maxSize["width"].asInt();
        if (maxSize.isMember("height")) settings.maxHeight = maxSize["height"].asInt();
    }

    if (json.isMember("visible")) settings.visible = json["visible"].asBool();
    if (json.isMember("showTitleBar")) settings.showTitleBar = json["showTitleBar"].asBool();
    if (json.isMember("alwaysLocked")) settings.alwaysLocked = json["alwaysLocked"].asBool();
}

void UISettings::saveWindowSettings(const WindowSettings& settings, Json::Value& json) const {
    Json::Value pos;
    pos["x"] = settings.x;
    pos["y"] = settings.y;
    json["position"] = pos;

    Json::Value size;
    size["width"] = settings.width;
    size["height"] = settings.height;
    json["size"] = size;

    Json::Value minSize;
    minSize["width"] = settings.minWidth;
    minSize["height"] = settings.minHeight;
    json["minSize"] = minSize;

    Json::Value maxSize;
    maxSize["width"] = settings.maxWidth;
    maxSize["height"] = settings.maxHeight;
    json["maxSize"] = maxSize;

    json["visible"] = settings.visible;
    json["showTitleBar"] = settings.showTitleBar;
    json["alwaysLocked"] = settings.alwaysLocked;
}

// ============================================================================
// Global Theme Serialization
// ============================================================================

void UISettings::loadGlobalTheme(const Json::Value& json) {
    if (json.isMember("colors")) {
        const Json::Value& colors = json["colors"];

        if (colors.isMember("windowBackground"))
            m_globalTheme.windowBackground = jsonToColor(colors["windowBackground"], m_globalTheme.windowBackground);
        if (colors.isMember("titleBarBackground"))
            m_globalTheme.titleBarBackground = jsonToColor(colors["titleBarBackground"], m_globalTheme.titleBarBackground);
        if (colors.isMember("titleBarText"))
            m_globalTheme.titleBarText = jsonToColor(colors["titleBarText"], m_globalTheme.titleBarText);
        if (colors.isMember("borderLight"))
            m_globalTheme.borderLight = jsonToColor(colors["borderLight"], m_globalTheme.borderLight);
        if (colors.isMember("borderDark"))
            m_globalTheme.borderDark = jsonToColor(colors["borderDark"], m_globalTheme.borderDark);
        if (colors.isMember("buttonBackground"))
            m_globalTheme.buttonBackground = jsonToColor(colors["buttonBackground"], m_globalTheme.buttonBackground);
        if (colors.isMember("buttonHighlight"))
            m_globalTheme.buttonHighlight = jsonToColor(colors["buttonHighlight"], m_globalTheme.buttonHighlight);
        if (colors.isMember("buttonText"))
            m_globalTheme.buttonText = jsonToColor(colors["buttonText"], m_globalTheme.buttonText);
        if (colors.isMember("buttonDisabledBg"))
            m_globalTheme.buttonDisabledBg = jsonToColor(colors["buttonDisabledBg"], m_globalTheme.buttonDisabledBg);
        if (colors.isMember("buttonDisabledText"))
            m_globalTheme.buttonDisabledText = jsonToColor(colors["buttonDisabledText"], m_globalTheme.buttonDisabledText);
    }

    if (json.isMember("layout")) {
        const Json::Value& layout = json["layout"];

        if (layout.isMember("titleBarHeight")) m_globalTheme.titleBarHeight = layout["titleBarHeight"].asInt();
        if (layout.isMember("borderWidth")) m_globalTheme.borderWidth = layout["borderWidth"].asInt();
        if (layout.isMember("buttonHeight")) m_globalTheme.buttonHeight = layout["buttonHeight"].asInt();
        if (layout.isMember("buttonPadding")) m_globalTheme.buttonPadding = layout["buttonPadding"].asInt();
        if (layout.isMember("contentPadding")) m_globalTheme.contentPadding = layout["contentPadding"].asInt();
    }
}

void UISettings::saveGlobalTheme(Json::Value& json) const {
    Json::Value colors;
    colors["windowBackground"] = colorToJson(m_globalTheme.windowBackground);
    colors["titleBarBackground"] = colorToJson(m_globalTheme.titleBarBackground);
    colors["titleBarText"] = colorToJson(m_globalTheme.titleBarText);
    colors["borderLight"] = colorToJson(m_globalTheme.borderLight);
    colors["borderDark"] = colorToJson(m_globalTheme.borderDark);
    colors["buttonBackground"] = colorToJson(m_globalTheme.buttonBackground);
    colors["buttonHighlight"] = colorToJson(m_globalTheme.buttonHighlight);
    colors["buttonText"] = colorToJson(m_globalTheme.buttonText);
    colors["buttonDisabledBg"] = colorToJson(m_globalTheme.buttonDisabledBg);
    colors["buttonDisabledText"] = colorToJson(m_globalTheme.buttonDisabledText);
    json["colors"] = colors;

    Json::Value layout;
    layout["titleBarHeight"] = m_globalTheme.titleBarHeight;
    layout["borderWidth"] = m_globalTheme.borderWidth;
    layout["buttonHeight"] = m_globalTheme.buttonHeight;
    layout["buttonPadding"] = m_globalTheme.buttonPadding;
    layout["contentPadding"] = m_globalTheme.contentPadding;
    json["layout"] = layout;
}

// ============================================================================
// Chat Settings Serialization
// ============================================================================

void UISettings::loadChatSettings(const Json::Value& json) {
    loadWindowSettings(m_chat.window, json);

    if (json.isMember("layout")) {
        const Json::Value& layout = json["layout"];
        if (layout.isMember("inputFieldHeight")) m_chat.inputFieldHeight = layout["inputFieldHeight"].asInt();
        if (layout.isMember("scrollbarWidth")) m_chat.scrollbarWidth = layout["scrollbarWidth"].asInt();
        if (layout.isMember("scrollButtonHeight")) m_chat.scrollButtonHeight = layout["scrollButtonHeight"].asInt();
        if (layout.isMember("resizeEdgeWidth")) m_chat.resizeEdgeWidth = layout["resizeEdgeWidth"].asInt();
    }

    if (json.isMember("colors")) {
        const Json::Value& colors = json["colors"];
        if (colors.isMember("linkNpc")) m_chat.linkColorNpc = jsonToColor(colors["linkNpc"], m_chat.linkColorNpc);
        if (colors.isMember("linkItem")) m_chat.linkColorItem = jsonToColor(colors["linkItem"], m_chat.linkColorItem);
    }

    if (json.isMember("showTimestamps")) m_chat.showTimestamps = json["showTimestamps"].asBool();

    if (json.isMember("channelFilters")) {
        const Json::Value& filters = json["channelFilters"];
        m_chat.enabledChannels.clear();
        for (const auto& key : filters.getMemberNames()) {
            if (filters[key].asBool()) {
                m_chat.enabledChannels.insert(std::stoi(key));
            }
        }
    }
}

void UISettings::saveChatSettings(Json::Value& json) const {
    saveWindowSettings(m_chat.window, json);

    Json::Value layout;
    layout["inputFieldHeight"] = m_chat.inputFieldHeight;
    layout["scrollbarWidth"] = m_chat.scrollbarWidth;
    layout["scrollButtonHeight"] = m_chat.scrollButtonHeight;
    layout["resizeEdgeWidth"] = m_chat.resizeEdgeWidth;
    json["layout"] = layout;

    Json::Value colors;
    colors["linkNpc"] = colorToJson(m_chat.linkColorNpc);
    colors["linkItem"] = colorToJson(m_chat.linkColorItem);
    json["colors"] = colors;

    json["showTimestamps"] = m_chat.showTimestamps;

    Json::Value filters;
    for (int ch : m_chat.enabledChannels) {
        filters[std::to_string(ch)] = true;
    }
    json["channelFilters"] = filters;
}

// ============================================================================
// Inventory Settings Serialization
// ============================================================================

void UISettings::loadInventorySettings(const Json::Value& json) {
    loadWindowSettings(m_inventory.window, json);

    if (json.isMember("layout")) {
        const Json::Value& layout = json["layout"];
        if (layout.isMember("slotSize")) m_inventory.slotSize = layout["slotSize"].asInt();
        if (layout.isMember("slotSpacing")) m_inventory.slotSpacing = layout["slotSpacing"].asInt();
        if (layout.isMember("equipmentStartX")) m_inventory.equipmentStartX = layout["equipmentStartX"].asInt();
        if (layout.isMember("equipmentStartY")) m_inventory.equipmentStartY = layout["equipmentStartY"].asInt();
        if (layout.isMember("generalStartX")) m_inventory.generalStartX = layout["generalStartX"].asInt();
        if (layout.isMember("generalStartY")) m_inventory.generalStartY = layout["generalStartY"].asInt();
        if (layout.isMember("statsWidth")) m_inventory.statsWidth = layout["statsWidth"].asInt();
    }
}

void UISettings::saveInventorySettings(Json::Value& json) const {
    saveWindowSettings(m_inventory.window, json);

    Json::Value layout;
    layout["slotSize"] = m_inventory.slotSize;
    layout["slotSpacing"] = m_inventory.slotSpacing;
    layout["equipmentStartX"] = m_inventory.equipmentStartX;
    layout["equipmentStartY"] = m_inventory.equipmentStartY;
    layout["generalStartX"] = m_inventory.generalStartX;
    layout["generalStartY"] = m_inventory.generalStartY;
    layout["statsWidth"] = m_inventory.statsWidth;
    json["layout"] = layout;
}

// ============================================================================
// Loot Settings Serialization
// ============================================================================

void UISettings::loadLootSettings(const Json::Value& json) {
    loadWindowSettings(m_loot.window, json);

    if (json.isMember("layout")) {
        const Json::Value& layout = json["layout"];
        if (layout.isMember("columns")) m_loot.columns = layout["columns"].asInt();
        if (layout.isMember("rows")) m_loot.rows = layout["rows"].asInt();
        if (layout.isMember("slotSize")) m_loot.slotSize = layout["slotSize"].asInt();
        if (layout.isMember("slotSpacing")) m_loot.slotSpacing = layout["slotSpacing"].asInt();
        if (layout.isMember("padding")) m_loot.padding = layout["padding"].asInt();
        if (layout.isMember("buttonWidth")) m_loot.buttonWidth = layout["buttonWidth"].asInt();
        if (layout.isMember("buttonSpacing")) m_loot.buttonSpacing = layout["buttonSpacing"].asInt();
        if (layout.isMember("scrollbarWidth")) m_loot.scrollbarWidth = layout["scrollbarWidth"].asInt();
        if (layout.isMember("scrollButtonHeight")) m_loot.scrollButtonHeight = layout["scrollButtonHeight"].asInt();
        if (layout.isMember("topButtonRowHeight")) m_loot.topButtonRowHeight = layout["topButtonRowHeight"].asInt();
        if (layout.isMember("bottomButtonRowHeight")) m_loot.bottomButtonRowHeight = layout["bottomButtonRowHeight"].asInt();
    }
}

void UISettings::saveLootSettings(Json::Value& json) const {
    saveWindowSettings(m_loot.window, json);

    Json::Value layout;
    layout["columns"] = m_loot.columns;
    layout["rows"] = m_loot.rows;
    layout["slotSize"] = m_loot.slotSize;
    layout["slotSpacing"] = m_loot.slotSpacing;
    layout["padding"] = m_loot.padding;
    layout["buttonWidth"] = m_loot.buttonWidth;
    layout["buttonSpacing"] = m_loot.buttonSpacing;
    layout["scrollbarWidth"] = m_loot.scrollbarWidth;
    layout["scrollButtonHeight"] = m_loot.scrollButtonHeight;
    layout["topButtonRowHeight"] = m_loot.topButtonRowHeight;
    layout["bottomButtonRowHeight"] = m_loot.bottomButtonRowHeight;
    json["layout"] = layout;
}

// ============================================================================
// Buff Settings Serialization
// ============================================================================

void UISettings::loadBuffSettings(const Json::Value& json) {
    loadWindowSettings(m_buff.window, json);

    if (json.isMember("layout")) {
        const Json::Value& layout = json["layout"];
        if (layout.isMember("columns")) m_buff.columns = layout["columns"].asInt();
        if (layout.isMember("rows")) m_buff.rows = layout["rows"].asInt();
        if (layout.isMember("iconSize")) m_buff.iconSize = layout["iconSize"].asInt();
        if (layout.isMember("spacing")) m_buff.spacing = layout["spacing"].asInt();
        if (layout.isMember("padding")) m_buff.padding = layout["padding"].asInt();
    }

    if (json.isMember("colors")) {
        const Json::Value& colors = json["colors"];
        if (colors.isMember("buffBackground")) m_buff.buffBackground = jsonToColor(colors["buffBackground"], m_buff.buffBackground);
        if (colors.isMember("buffBorder")) m_buff.buffBorder = jsonToColor(colors["buffBorder"], m_buff.buffBorder);
        if (colors.isMember("debuffBorder")) m_buff.debuffBorder = jsonToColor(colors["debuffBorder"], m_buff.debuffBorder);
        if (colors.isMember("emptySlot")) m_buff.emptySlot = jsonToColor(colors["emptySlot"], m_buff.emptySlot);
        if (colors.isMember("durationBackground")) m_buff.durationBackground = jsonToColor(colors["durationBackground"], m_buff.durationBackground);
        if (colors.isMember("durationText")) m_buff.durationText = jsonToColor(colors["durationText"], m_buff.durationText);
    }
}

void UISettings::saveBuffSettings(Json::Value& json) const {
    saveWindowSettings(m_buff.window, json);

    Json::Value layout;
    layout["columns"] = m_buff.columns;
    layout["rows"] = m_buff.rows;
    layout["iconSize"] = m_buff.iconSize;
    layout["spacing"] = m_buff.spacing;
    layout["padding"] = m_buff.padding;
    json["layout"] = layout;

    Json::Value colors;
    colors["buffBackground"] = colorToJson(m_buff.buffBackground);
    colors["buffBorder"] = colorToJson(m_buff.buffBorder);
    colors["debuffBorder"] = colorToJson(m_buff.debuffBorder);
    colors["emptySlot"] = colorToJson(m_buff.emptySlot);
    colors["durationBackground"] = colorToJson(m_buff.durationBackground);
    colors["durationText"] = colorToJson(m_buff.durationText);
    json["colors"] = colors;
}

// ============================================================================
// Group Settings Serialization
// ============================================================================

void UISettings::loadGroupSettings(const Json::Value& json) {
    loadWindowSettings(m_group.window, json);

    if (json.isMember("layout")) {
        const Json::Value& layout = json["layout"];
        if (layout.isMember("nameHeight")) m_group.nameHeight = layout["nameHeight"].asInt();
        if (layout.isMember("barHeight")) m_group.barHeight = layout["barHeight"].asInt();
        if (layout.isMember("barSpacing")) m_group.barSpacing = layout["barSpacing"].asInt();
        if (layout.isMember("memberSpacing")) m_group.memberSpacing = layout["memberSpacing"].asInt();
        if (layout.isMember("buttonWidth")) m_group.buttonWidth = layout["buttonWidth"].asInt();
        if (layout.isMember("buttonHeight")) m_group.buttonHeight = layout["buttonHeight"].asInt();
        if (layout.isMember("padding")) m_group.padding = layout["padding"].asInt();
        if (layout.isMember("buttonPadding")) m_group.buttonPadding = layout["buttonPadding"].asInt();
    }

    if (json.isMember("colors")) {
        const Json::Value& colors = json["colors"];
        if (colors.isMember("hpBackground")) m_group.hpBackground = jsonToColor(colors["hpBackground"], m_group.hpBackground);
        if (colors.isMember("hpHigh")) m_group.hpHigh = jsonToColor(colors["hpHigh"], m_group.hpHigh);
        if (colors.isMember("hpMedium")) m_group.hpMedium = jsonToColor(colors["hpMedium"], m_group.hpMedium);
        if (colors.isMember("hpLow")) m_group.hpLow = jsonToColor(colors["hpLow"], m_group.hpLow);
        if (colors.isMember("manaBackground")) m_group.manaBackground = jsonToColor(colors["manaBackground"], m_group.manaBackground);
        if (colors.isMember("manaFill")) m_group.manaFill = jsonToColor(colors["manaFill"], m_group.manaFill);
        if (colors.isMember("memberBackground")) m_group.memberBackground = jsonToColor(colors["memberBackground"], m_group.memberBackground);
        if (colors.isMember("nameInZone")) m_group.nameInZone = jsonToColor(colors["nameInZone"], m_group.nameInZone);
        if (colors.isMember("nameOutOfZone")) m_group.nameOutOfZone = jsonToColor(colors["nameOutOfZone"], m_group.nameOutOfZone);
        if (colors.isMember("leaderMarker")) m_group.leaderMarker = jsonToColor(colors["leaderMarker"], m_group.leaderMarker);
    }
}

void UISettings::saveGroupSettings(Json::Value& json) const {
    saveWindowSettings(m_group.window, json);

    Json::Value layout;
    layout["nameHeight"] = m_group.nameHeight;
    layout["barHeight"] = m_group.barHeight;
    layout["barSpacing"] = m_group.barSpacing;
    layout["memberSpacing"] = m_group.memberSpacing;
    layout["buttonWidth"] = m_group.buttonWidth;
    layout["buttonHeight"] = m_group.buttonHeight;
    layout["padding"] = m_group.padding;
    layout["buttonPadding"] = m_group.buttonPadding;
    json["layout"] = layout;

    Json::Value colors;
    colors["hpBackground"] = colorToJson(m_group.hpBackground);
    colors["hpHigh"] = colorToJson(m_group.hpHigh);
    colors["hpMedium"] = colorToJson(m_group.hpMedium);
    colors["hpLow"] = colorToJson(m_group.hpLow);
    colors["manaBackground"] = colorToJson(m_group.manaBackground);
    colors["manaFill"] = colorToJson(m_group.manaFill);
    colors["memberBackground"] = colorToJson(m_group.memberBackground);
    colors["nameInZone"] = colorToJson(m_group.nameInZone);
    colors["nameOutOfZone"] = colorToJson(m_group.nameOutOfZone);
    colors["leaderMarker"] = colorToJson(m_group.leaderMarker);
    json["colors"] = colors;
}

// ============================================================================
// Player Status Settings Serialization
// ============================================================================

void UISettings::loadPlayerStatusSettings(const Json::Value& json) {
    loadWindowSettings(m_playerStatus.window, json);

    if (json.isMember("layout")) {
        const Json::Value& layout = json["layout"];
        if (layout.isMember("nameHeight")) m_playerStatus.nameHeight = layout["nameHeight"].asInt();
        if (layout.isMember("barHeight")) m_playerStatus.barHeight = layout["barHeight"].asInt();
        if (layout.isMember("barSpacing")) m_playerStatus.barSpacing = layout["barSpacing"].asInt();
        if (layout.isMember("barLabelWidth")) m_playerStatus.barLabelWidth = layout["barLabelWidth"].asInt();
        if (layout.isMember("padding")) m_playerStatus.padding = layout["padding"].asInt();
    }

    if (json.isMember("colors")) {
        const Json::Value& colors = json["colors"];
        if (colors.isMember("nameText")) m_playerStatus.nameText = jsonToColor(colors["nameText"], m_playerStatus.nameText);
        if (colors.isMember("hpBackground")) m_playerStatus.hpBackground = jsonToColor(colors["hpBackground"], m_playerStatus.hpBackground);
        if (colors.isMember("hpFill")) m_playerStatus.hpFill = jsonToColor(colors["hpFill"], m_playerStatus.hpFill);
        if (colors.isMember("manaBackground")) m_playerStatus.manaBackground = jsonToColor(colors["manaBackground"], m_playerStatus.manaBackground);
        if (colors.isMember("manaFill")) m_playerStatus.manaFill = jsonToColor(colors["manaFill"], m_playerStatus.manaFill);
        if (colors.isMember("staminaBackground")) m_playerStatus.staminaBackground = jsonToColor(colors["staminaBackground"], m_playerStatus.staminaBackground);
        if (colors.isMember("staminaFill")) m_playerStatus.staminaFill = jsonToColor(colors["staminaFill"], m_playerStatus.staminaFill);
        if (colors.isMember("barText")) m_playerStatus.barText = jsonToColor(colors["barText"], m_playerStatus.barText);
    }
}

void UISettings::savePlayerStatusSettings(Json::Value& json) const {
    saveWindowSettings(m_playerStatus.window, json);

    Json::Value layout;
    layout["nameHeight"] = m_playerStatus.nameHeight;
    layout["barHeight"] = m_playerStatus.barHeight;
    layout["barSpacing"] = m_playerStatus.barSpacing;
    layout["barLabelWidth"] = m_playerStatus.barLabelWidth;
    layout["padding"] = m_playerStatus.padding;
    json["layout"] = layout;

    Json::Value colors;
    colors["nameText"] = colorToJson(m_playerStatus.nameText);
    colors["hpBackground"] = colorToJson(m_playerStatus.hpBackground);
    colors["hpFill"] = colorToJson(m_playerStatus.hpFill);
    colors["manaBackground"] = colorToJson(m_playerStatus.manaBackground);
    colors["manaFill"] = colorToJson(m_playerStatus.manaFill);
    colors["staminaBackground"] = colorToJson(m_playerStatus.staminaBackground);
    colors["staminaFill"] = colorToJson(m_playerStatus.staminaFill);
    colors["barText"] = colorToJson(m_playerStatus.barText);
    json["colors"] = colors;
}

// ============================================================================
// Spell Gem Settings Serialization
// ============================================================================

void UISettings::loadSpellGemSettings(const Json::Value& json) {
    if (json.isMember("position")) {
        const Json::Value& pos = json["position"];
        if (pos.isMember("x")) m_spellGems.x = pos["x"].asInt();
        if (pos.isMember("y")) m_spellGems.y = pos["y"].asInt();
    }

    if (json.isMember("visible")) m_spellGems.visible = json["visible"].asBool();

    if (json.isMember("layout")) {
        const Json::Value& layout = json["layout"];
        if (layout.isMember("gemWidth")) m_spellGems.gemWidth = layout["gemWidth"].asInt();
        if (layout.isMember("gemHeight")) m_spellGems.gemHeight = layout["gemHeight"].asInt();
        if (layout.isMember("gemSpacing")) m_spellGems.gemSpacing = layout["gemSpacing"].asInt();
        if (layout.isMember("panelPadding")) m_spellGems.panelPadding = layout["panelPadding"].asInt();
        if (layout.isMember("spellbookButtonSize")) m_spellGems.spellbookButtonSize = layout["spellbookButtonSize"].asInt();
        if (layout.isMember("spellbookButtonMargin")) m_spellGems.spellbookButtonMargin = layout["spellbookButtonMargin"].asInt();
    }
}

void UISettings::saveSpellGemSettings(Json::Value& json) const {
    Json::Value pos;
    pos["x"] = m_spellGems.x;
    pos["y"] = m_spellGems.y;
    json["position"] = pos;

    json["visible"] = m_spellGems.visible;

    Json::Value layout;
    layout["gemWidth"] = m_spellGems.gemWidth;
    layout["gemHeight"] = m_spellGems.gemHeight;
    layout["gemSpacing"] = m_spellGems.gemSpacing;
    layout["panelPadding"] = m_spellGems.panelPadding;
    layout["spellbookButtonSize"] = m_spellGems.spellbookButtonSize;
    layout["spellbookButtonMargin"] = m_spellGems.spellbookButtonMargin;
    json["layout"] = layout;
}

// ============================================================================
// Skills Settings Serialization
// ============================================================================

void UISettings::loadSkillsSettings(const Json::Value& json) {
    loadWindowSettings(m_skills.window, json);
}

void UISettings::saveSkillsSettings(Json::Value& json) const {
    saveWindowSettings(m_skills.window, json);
}

// ============================================================================
// Spellbook Settings Serialization
// ============================================================================

void UISettings::loadSpellBookSettings(const Json::Value& json) {
    loadWindowSettings(m_spellBook.window, json);

    if (json.isMember("layout")) {
        const Json::Value& layout = json["layout"];
        if (layout.isMember("pageWidth")) m_spellBook.pageWidth = layout["pageWidth"].asInt();
        if (layout.isMember("pageSpacing")) m_spellBook.pageSpacing = layout["pageSpacing"].asInt();
        if (layout.isMember("spellsPerPage")) m_spellBook.spellsPerPage = layout["spellsPerPage"].asInt();
        if (layout.isMember("iconSize")) m_spellBook.iconSize = layout["iconSize"].asInt();
        if (layout.isMember("rowHeight")) m_spellBook.rowHeight = layout["rowHeight"].asInt();
        if (layout.isMember("rowSpacing")) m_spellBook.rowSpacing = layout["rowSpacing"].asInt();
        if (layout.isMember("leftPageX")) m_spellBook.leftPageX = layout["leftPageX"].asInt();
        if (layout.isMember("slotsStartY")) m_spellBook.slotsStartY = layout["slotsStartY"].asInt();
        if (layout.isMember("nameOffsetX")) m_spellBook.nameOffsetX = layout["nameOffsetX"].asInt();
        if (layout.isMember("nameMaxWidth")) m_spellBook.nameMaxWidth = layout["nameMaxWidth"].asInt();
        if (layout.isMember("navButtonWidth")) m_spellBook.navButtonWidth = layout["navButtonWidth"].asInt();
        if (layout.isMember("navButtonHeight")) m_spellBook.navButtonHeight = layout["navButtonHeight"].asInt();
    }
}

void UISettings::saveSpellBookSettings(Json::Value& json) const {
    saveWindowSettings(m_spellBook.window, json);

    Json::Value layout;
    layout["pageWidth"] = m_spellBook.pageWidth;
    layout["pageSpacing"] = m_spellBook.pageSpacing;
    layout["spellsPerPage"] = m_spellBook.spellsPerPage;
    layout["iconSize"] = m_spellBook.iconSize;
    layout["rowHeight"] = m_spellBook.rowHeight;
    layout["rowSpacing"] = m_spellBook.rowSpacing;
    layout["leftPageX"] = m_spellBook.leftPageX;
    layout["slotsStartY"] = m_spellBook.slotsStartY;
    layout["nameOffsetX"] = m_spellBook.nameOffsetX;
    layout["nameMaxWidth"] = m_spellBook.nameMaxWidth;
    layout["navButtonWidth"] = m_spellBook.navButtonWidth;
    layout["navButtonHeight"] = m_spellBook.navButtonHeight;
    json["layout"] = layout;
}

// ============================================================================
// Hotbar Settings Serialization
// ============================================================================

void UISettings::loadHotbarSettings(const Json::Value& json) {
    loadWindowSettings(m_hotbar.window, json);

    if (json.isMember("layout")) {
        const Json::Value& layout = json["layout"];
        if (layout.isMember("buttonSize")) m_hotbar.buttonSize = layout["buttonSize"].asInt();
        if (layout.isMember("buttonSpacing")) m_hotbar.buttonSpacing = layout["buttonSpacing"].asInt();
        if (layout.isMember("padding")) m_hotbar.padding = layout["padding"].asInt();
        if (layout.isMember("buttonCount")) m_hotbar.buttonCount = layout["buttonCount"].asInt();
    }

    // Load button data
    if (json.isMember("buttons")) {
        const Json::Value& buttons = json["buttons"];
        for (Json::ArrayIndex i = 0; i < buttons.size() && i < 10; i++) {
            const Json::Value& btn = buttons[i];
            if (btn.isMember("type")) m_hotbar.buttons[i].type = btn["type"].asInt();
            if (btn.isMember("id")) m_hotbar.buttons[i].id = btn["id"].asUInt();
            if (btn.isMember("emoteText")) m_hotbar.buttons[i].emoteText = btn["emoteText"].asString();
            if (btn.isMember("iconId")) m_hotbar.buttons[i].iconId = btn["iconId"].asUInt();
        }
    }
}

void UISettings::saveHotbarSettings(Json::Value& json) const {
    saveWindowSettings(m_hotbar.window, json);

    Json::Value layout;
    layout["buttonSize"] = m_hotbar.buttonSize;
    layout["buttonSpacing"] = m_hotbar.buttonSpacing;
    layout["padding"] = m_hotbar.padding;
    layout["buttonCount"] = m_hotbar.buttonCount;
    json["layout"] = layout;

    // Save button data
    Json::Value buttons(Json::arrayValue);
    for (size_t i = 0; i < 10; i++) {
        Json::Value btn;
        btn["type"] = m_hotbar.buttons[i].type;
        btn["id"] = m_hotbar.buttons[i].id;
        btn["emoteText"] = m_hotbar.buttons[i].emoteText;
        btn["iconId"] = m_hotbar.buttons[i].iconId;
        buttons.append(btn);
    }
    json["buttons"] = buttons;
}

// ============================================================================
// Trade Settings Serialization
// ============================================================================

void UISettings::loadTradeSettings(const Json::Value& json) {
    loadWindowSettings(m_trade.window, json);
}

void UISettings::saveTradeSettings(Json::Value& json) const {
    saveWindowSettings(m_trade.window, json);
}

// ============================================================================
// Casting Bar Settings Serialization
// ============================================================================

void UISettings::loadCastingBarSettings(const Json::Value& json) {
    if (json.isMember("playerBar")) {
        const Json::Value& bar = json["playerBar"];
        if (bar.isMember("position")) {
            if (bar["position"].isMember("x")) m_castingBar.playerBar.x = bar["position"]["x"].asInt();
            if (bar["position"].isMember("y")) m_castingBar.playerBar.y = bar["position"]["y"].asInt();
        }
        if (bar.isMember("size")) {
            if (bar["size"].isMember("width")) m_castingBar.playerBar.width = bar["size"]["width"].asInt();
            if (bar["size"].isMember("height")) m_castingBar.playerBar.height = bar["size"]["height"].asInt();
        }
    }

    if (json.isMember("targetBar")) {
        const Json::Value& bar = json["targetBar"];
        if (bar.isMember("position")) {
            if (bar["position"].isMember("x")) m_castingBar.targetBar.x = bar["position"]["x"].asInt();
            if (bar["position"].isMember("y")) m_castingBar.targetBar.y = bar["position"]["y"].asInt();
        }
        if (bar.isMember("size")) {
            if (bar["size"].isMember("width")) m_castingBar.targetBar.width = bar["size"]["width"].asInt();
            if (bar["size"].isMember("height")) m_castingBar.targetBar.height = bar["size"]["height"].asInt();
        }
    }

    if (json.isMember("colors")) {
        const Json::Value& colors = json["colors"];
        if (colors.isMember("barFill")) m_castingBar.barFill = jsonToColor(colors["barFill"], m_castingBar.barFill);
        if (colors.isMember("background")) m_castingBar.background = jsonToColor(colors["background"], m_castingBar.background);
        if (colors.isMember("border")) m_castingBar.border = jsonToColor(colors["border"], m_castingBar.border);
        if (colors.isMember("text")) m_castingBar.text = jsonToColor(colors["text"], m_castingBar.text);
        if (colors.isMember("interrupted")) m_castingBar.interrupted = jsonToColor(colors["interrupted"], m_castingBar.interrupted);
    }
}

void UISettings::saveCastingBarSettings(Json::Value& json) const {
    Json::Value playerBar;
    Json::Value playerPos;
    playerPos["x"] = m_castingBar.playerBar.x;
    playerPos["y"] = m_castingBar.playerBar.y;
    playerBar["position"] = playerPos;
    Json::Value playerSize;
    playerSize["width"] = m_castingBar.playerBar.width;
    playerSize["height"] = m_castingBar.playerBar.height;
    playerBar["size"] = playerSize;
    json["playerBar"] = playerBar;

    Json::Value targetBar;
    Json::Value targetPos;
    targetPos["x"] = m_castingBar.targetBar.x;
    targetPos["y"] = m_castingBar.targetBar.y;
    targetBar["position"] = targetPos;
    Json::Value targetSize;
    targetSize["width"] = m_castingBar.targetBar.width;
    targetSize["height"] = m_castingBar.targetBar.height;
    targetBar["size"] = targetSize;
    json["targetBar"] = targetBar;

    Json::Value colors;
    colors["barFill"] = colorToJson(m_castingBar.barFill);
    colors["background"] = colorToJson(m_castingBar.background);
    colors["border"] = colorToJson(m_castingBar.border);
    colors["text"] = colorToJson(m_castingBar.text);
    colors["interrupted"] = colorToJson(m_castingBar.interrupted);
    json["colors"] = colors;
}

// ============================================================================
// Tooltip Settings Serialization
// ============================================================================

void UISettings::loadTooltipSettings(const Json::Value& json) {
    if (json.isMember("item")) {
        const Json::Value& item = json["item"];
        if (item.isMember("minWidth")) m_itemTooltip.minWidth = item["minWidth"].asInt();
        if (item.isMember("maxWidth")) m_itemTooltip.maxWidth = item["maxWidth"].asInt();
        if (item.isMember("lineHeight")) m_itemTooltip.lineHeight = item["lineHeight"].asInt();
        if (item.isMember("padding")) m_itemTooltip.padding = item["padding"].asInt();
        if (item.isMember("sectionSpacing")) m_itemTooltip.sectionSpacing = item["sectionSpacing"].asInt();
        if (item.isMember("mouseOffset")) m_itemTooltip.mouseOffset = item["mouseOffset"].asInt();

        if (item.isMember("colors")) {
            const Json::Value& colors = item["colors"];
            if (colors.isMember("background")) m_itemTooltip.background = jsonToColor(colors["background"], m_itemTooltip.background);
            if (colors.isMember("border")) m_itemTooltip.border = jsonToColor(colors["border"], m_itemTooltip.border);
            if (colors.isMember("title")) m_itemTooltip.title = jsonToColor(colors["title"], m_itemTooltip.title);
            if (colors.isMember("magic")) m_itemTooltip.magic = jsonToColor(colors["magic"], m_itemTooltip.magic);
            if (colors.isMember("flags")) m_itemTooltip.flags = jsonToColor(colors["flags"], m_itemTooltip.flags);
            if (colors.isMember("restrictions")) m_itemTooltip.restrictions = jsonToColor(colors["restrictions"], m_itemTooltip.restrictions);
            if (colors.isMember("stats")) m_itemTooltip.stats = jsonToColor(colors["stats"], m_itemTooltip.stats);
            if (colors.isMember("statsValue")) m_itemTooltip.statsValue = jsonToColor(colors["statsValue"], m_itemTooltip.statsValue);
            if (colors.isMember("effect")) m_itemTooltip.effect = jsonToColor(colors["effect"], m_itemTooltip.effect);
            if (colors.isMember("effectDesc")) m_itemTooltip.effectDesc = jsonToColor(colors["effectDesc"], m_itemTooltip.effectDesc);
            if (colors.isMember("lore")) m_itemTooltip.lore = jsonToColor(colors["lore"], m_itemTooltip.lore);
        }
    }

    if (json.isMember("buff")) {
        const Json::Value& buff = json["buff"];
        if (buff.isMember("minWidth")) m_buffTooltip.minWidth = buff["minWidth"].asInt();
        if (buff.isMember("maxWidth")) m_buffTooltip.maxWidth = buff["maxWidth"].asInt();
        if (buff.isMember("lineHeight")) m_buffTooltip.lineHeight = buff["lineHeight"].asInt();
        if (buff.isMember("padding")) m_buffTooltip.padding = buff["padding"].asInt();
        if (buff.isMember("mouseOffset")) m_buffTooltip.mouseOffset = buff["mouseOffset"].asInt();

        if (buff.isMember("colors")) {
            const Json::Value& colors = buff["colors"];
            if (colors.isMember("background")) m_buffTooltip.background = jsonToColor(colors["background"], m_buffTooltip.background);
            if (colors.isMember("border")) m_buffTooltip.border = jsonToColor(colors["border"], m_buffTooltip.border);
            if (colors.isMember("spellName")) m_buffTooltip.spellName = jsonToColor(colors["spellName"], m_buffTooltip.spellName);
            if (colors.isMember("duration")) m_buffTooltip.duration = jsonToColor(colors["duration"], m_buffTooltip.duration);
            if (colors.isMember("info")) m_buffTooltip.info = jsonToColor(colors["info"], m_buffTooltip.info);
        }
    }
}

void UISettings::saveTooltipSettings(Json::Value& json) const {
    // Item tooltip
    Json::Value item;
    item["minWidth"] = m_itemTooltip.minWidth;
    item["maxWidth"] = m_itemTooltip.maxWidth;
    item["lineHeight"] = m_itemTooltip.lineHeight;
    item["padding"] = m_itemTooltip.padding;
    item["sectionSpacing"] = m_itemTooltip.sectionSpacing;
    item["mouseOffset"] = m_itemTooltip.mouseOffset;

    Json::Value itemColors;
    itemColors["background"] = colorToJson(m_itemTooltip.background);
    itemColors["border"] = colorToJson(m_itemTooltip.border);
    itemColors["title"] = colorToJson(m_itemTooltip.title);
    itemColors["magic"] = colorToJson(m_itemTooltip.magic);
    itemColors["flags"] = colorToJson(m_itemTooltip.flags);
    itemColors["restrictions"] = colorToJson(m_itemTooltip.restrictions);
    itemColors["stats"] = colorToJson(m_itemTooltip.stats);
    itemColors["statsValue"] = colorToJson(m_itemTooltip.statsValue);
    itemColors["effect"] = colorToJson(m_itemTooltip.effect);
    itemColors["effectDesc"] = colorToJson(m_itemTooltip.effectDesc);
    itemColors["lore"] = colorToJson(m_itemTooltip.lore);
    item["colors"] = itemColors;
    json["item"] = item;

    // Buff tooltip
    Json::Value buff;
    buff["minWidth"] = m_buffTooltip.minWidth;
    buff["maxWidth"] = m_buffTooltip.maxWidth;
    buff["lineHeight"] = m_buffTooltip.lineHeight;
    buff["padding"] = m_buffTooltip.padding;
    buff["mouseOffset"] = m_buffTooltip.mouseOffset;

    Json::Value buffColors;
    buffColors["background"] = colorToJson(m_buffTooltip.background);
    buffColors["border"] = colorToJson(m_buffTooltip.border);
    buffColors["spellName"] = colorToJson(m_buffTooltip.spellName);
    buffColors["duration"] = colorToJson(m_buffTooltip.duration);
    buffColors["info"] = colorToJson(m_buffTooltip.info);
    buff["colors"] = buffColors;
    json["buff"] = buff;
}

// ============================================================================
// Slot Settings Serialization
// ============================================================================

void UISettings::loadSlotSettings(const Json::Value& json) {
    if (json.isMember("defaultSize")) m_slots.defaultSize = json["defaultSize"].asInt();
    if (json.isMember("iconPadding")) m_slots.iconPadding = json["iconPadding"].asInt();

    if (json.isMember("colors")) {
        const Json::Value& colors = json["colors"];
        if (colors.isMember("background")) m_slots.background = jsonToColor(colors["background"], m_slots.background);
        if (colors.isMember("border")) m_slots.border = jsonToColor(colors["border"], m_slots.border);
        if (colors.isMember("highlight")) m_slots.highlight = jsonToColor(colors["highlight"], m_slots.highlight);
        if (colors.isMember("invalid")) m_slots.invalid = jsonToColor(colors["invalid"], m_slots.invalid);
        if (colors.isMember("itemBackground")) m_slots.itemBackground = jsonToColor(colors["itemBackground"], m_slots.itemBackground);
        if (colors.isMember("stackText")) m_slots.stackText = jsonToColor(colors["stackText"], m_slots.stackText);
        if (colors.isMember("labelText")) m_slots.labelText = jsonToColor(colors["labelText"], m_slots.labelText);
    }
}

void UISettings::saveSlotSettings(Json::Value& json) const {
    json["defaultSize"] = m_slots.defaultSize;
    json["iconPadding"] = m_slots.iconPadding;

    Json::Value colors;
    colors["background"] = colorToJson(m_slots.background);
    colors["border"] = colorToJson(m_slots.border);
    colors["highlight"] = colorToJson(m_slots.highlight);
    colors["invalid"] = colorToJson(m_slots.invalid);
    colors["itemBackground"] = colorToJson(m_slots.itemBackground);
    colors["stackText"] = colorToJson(m_slots.stackText);
    colors["labelText"] = colorToJson(m_slots.labelText);
    json["colors"] = colors;
}

// ============================================================================
// Chat Input Settings Serialization
// ============================================================================

void UISettings::loadChatInputSettings(const Json::Value& json) {
    if (json.isMember("blinkIntervalMs")) m_chatInput.blinkIntervalMs = json["blinkIntervalMs"].asInt();
    if (json.isMember("maxHistory")) m_chatInput.maxHistory = json["maxHistory"].asInt();

    if (json.isMember("colors")) {
        const Json::Value& colors = json["colors"];
        if (colors.isMember("background")) m_chatInput.background = jsonToColor(colors["background"], m_chatInput.background);
        if (colors.isMember("text")) m_chatInput.text = jsonToColor(colors["text"], m_chatInput.text);
        if (colors.isMember("cursor")) m_chatInput.cursor = jsonToColor(colors["cursor"], m_chatInput.cursor);
        if (colors.isMember("prompt")) m_chatInput.prompt = jsonToColor(colors["prompt"], m_chatInput.prompt);
    }
}

void UISettings::saveChatInputSettings(Json::Value& json) const {
    json["blinkIntervalMs"] = m_chatInput.blinkIntervalMs;
    json["maxHistory"] = m_chatInput.maxHistory;

    Json::Value colors;
    colors["background"] = colorToJson(m_chatInput.background);
    colors["text"] = colorToJson(m_chatInput.text);
    colors["cursor"] = colorToJson(m_chatInput.cursor);
    colors["prompt"] = colorToJson(m_chatInput.prompt);
    json["colors"] = colors;
}

// ============================================================================
// Scrollbar Settings Serialization
// ============================================================================

void UISettings::loadScrollbarSettings(const Json::Value& json) {
    if (json.isMember("colors")) {
        const Json::Value& colors = json["colors"];
        if (colors.isMember("trackColor")) m_scrollbar.trackColor = jsonToColor(colors["trackColor"], m_scrollbar.trackColor);
        if (colors.isMember("thumbColor")) m_scrollbar.thumbColor = jsonToColor(colors["thumbColor"], m_scrollbar.thumbColor);
        if (colors.isMember("thumbHoverColor")) m_scrollbar.thumbHoverColor = jsonToColor(colors["thumbHoverColor"], m_scrollbar.thumbHoverColor);
        if (colors.isMember("buttonColor")) m_scrollbar.buttonColor = jsonToColor(colors["buttonColor"], m_scrollbar.buttonColor);
        if (colors.isMember("buttonHoverColor")) m_scrollbar.buttonHoverColor = jsonToColor(colors["buttonHoverColor"], m_scrollbar.buttonHoverColor);
        if (colors.isMember("arrowColor")) m_scrollbar.arrowColor = jsonToColor(colors["arrowColor"], m_scrollbar.arrowColor);
    }
}

void UISettings::saveScrollbarSettings(Json::Value& json) const {
    Json::Value colors;
    colors["trackColor"] = colorToJson(m_scrollbar.trackColor);
    colors["thumbColor"] = colorToJson(m_scrollbar.thumbColor);
    colors["thumbHoverColor"] = colorToJson(m_scrollbar.thumbHoverColor);
    colors["buttonColor"] = colorToJson(m_scrollbar.buttonColor);
    colors["buttonHoverColor"] = colorToJson(m_scrollbar.buttonHoverColor);
    colors["arrowColor"] = colorToJson(m_scrollbar.arrowColor);
    json["colors"] = colors;
}

// ============================================================================
// Window Position/Size Update Methods
// ============================================================================

void UISettings::updateWindowPosition(const std::string& windowName, int x, int y) {
    WindowSettings* settings = nullptr;

    if (windowName == "chat") settings = &m_chat.window;
    else if (windowName == "inventory") settings = &m_inventory.window;
    else if (windowName == "loot") settings = &m_loot.window;
    else if (windowName == "buff") settings = &m_buff.window;
    else if (windowName == "group") settings = &m_group.window;
    else if (windowName == "playerStatus") settings = &m_playerStatus.window;
    else if (windowName == "skills") settings = &m_skills.window;
    else if (windowName == "spellbook") settings = &m_spellBook.window;

    if (settings) {
        settings->x = x;
        settings->y = y;
        m_dirty = true;
        LOG_DEBUG(MOD_UI, "[UISettings] Window '{}' position updated to ({}, {})", windowName, x, y);
    }
}

void UISettings::updateWindowSize(const std::string& windowName, int width, int height) {
    WindowSettings* settings = nullptr;

    if (windowName == "chat") settings = &m_chat.window;
    else if (windowName == "inventory") settings = &m_inventory.window;
    else if (windowName == "loot") settings = &m_loot.window;
    else if (windowName == "buff") settings = &m_buff.window;
    else if (windowName == "group") settings = &m_group.window;
    else if (windowName == "playerStatus") settings = &m_playerStatus.window;
    else if (windowName == "skills") settings = &m_skills.window;
    else if (windowName == "spellbook") settings = &m_spellBook.window;

    if (settings) {
        settings->width = width;
        settings->height = height;
        m_dirty = true;
        LOG_DEBUG(MOD_UI, "[UISettings] Window '{}' size updated to ({} x {})", windowName, width, height);
    }
}

void UISettings::saveIfNeeded() {
    if (m_dirty) {
        saveToFile();
    }
}

} // namespace ui
} // namespace eqt
