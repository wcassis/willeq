/*
 * UIAtlas runtime loader implementation.
 * U02: Loads pre-generated ui_atlas.png + ui_atlas.json from atlas path.
 */

#include "client/graphics/ui/ui_atlas.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>

namespace EQT {
namespace Graphics {

bool UIAtlas::load(irr::video::IVideoDriver* driver, const std::string& atlasPath) {
    if (!driver || atlasPath.empty()) return false;

    std::string pngPath = atlasPath + "/ui_atlas.png";
    std::string jsonPath = atlasPath + "/ui_atlas.json";

    // Load texture
    texture_ = driver->getTexture(pngPath.c_str());
    if (!texture_) {
        LOG_ERROR(MOD_GRAPHICS, "UIAtlas: failed to load {}", pngPath);
        return false;
    }

    // Load sprite rects from JSON
    if (!loadJson(jsonPath)) {
        LOG_ERROR(MOD_GRAPHICS, "UIAtlas: failed to load {}", jsonPath);
        texture_ = nullptr;
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "UIAtlas: loaded from {} ({}x{})",
        atlasPath,
        texture_->getOriginalSize().Width,
        texture_->getOriginalSize().Height);
    return true;
}

const UISpriteRect& UIAtlas::getSprite(UISprite sprite) const {
    int idx = static_cast<int>(sprite);
    if (idx >= 0 && idx < static_cast<int>(UISprite::Count)) {
        return sprites_[idx];
    }
    return emptySprite_;
}

bool UIAtlas::loadJson(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) return false;

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "UIAtlas: JSON parse error: {}", errors);
        return false;
    }

    if (!root.isMember("sprites") || !root["sprites"].isObject()) {
        LOG_ERROR(MOD_GRAPHICS, "UIAtlas: JSON missing 'sprites' object");
        return false;
    }

    const Json::Value& spritesObj = root["sprites"];
    for (const auto& name : spritesObj.getMemberNames()) {
        const Json::Value& s = spritesObj[name];
        int x = s.get("x", 0).asInt();
        int y = s.get("y", 0).asInt();
        int w = s.get("w", 0).asInt();
        int h = s.get("h", 0).asInt();
        mapSprite(name, irr::core::rect<irr::s32>(x, y, x + w, y + h));
    }

    return true;
}

void UIAtlas::mapSprite(const std::string& name, const irr::core::rect<irr::s32>& rect) {
    // Map JSON sprite names to UISprite enum values
    static const std::unordered_map<std::string, UISprite> nameMap = {
        {"slot_background",     UISprite::SlotBackground},
        {"slot_border_normal",  UISprite::SlotBorderNormal},
        {"slot_border_hover",   UISprite::SlotBorderHover},
        {"slot_border_selected",UISprite::SlotBorderSelected},
        {"button_normal",       UISprite::ButtonNormal},
        {"button_pressed",      UISprite::ButtonPressed},
        {"button_disabled",     UISprite::ButtonDisabled},
        {"bar_hp",              UISprite::BarHP},
        {"bar_mana",            UISprite::BarMana},
        {"bar_stamina",         UISprite::BarStamina},
        {"bar_xp",              UISprite::BarXP},
        {"bar_casting",         UISprite::BarCasting},
        {"bar_background",      UISprite::BarBackground},
        {"panel_background",    UISprite::PanelBackground},
        {"panel_border",        UISprite::PanelBorder},
        {"white",               UISprite::White},
        {"scroll_indicator",    UISprite::ScrollIndicator},
    };

    auto it = nameMap.find(name);
    if (it != nameMap.end()) {
        sprites_[static_cast<int>(it->second)].srcRect = rect;
    } else {
        LOG_WARN(MOD_GRAPHICS, "UIAtlas: unknown sprite '{}' in JSON", name);
    }
}

} // namespace Graphics
} // namespace EQT
