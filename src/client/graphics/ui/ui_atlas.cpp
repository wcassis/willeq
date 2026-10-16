/*
 * UIAtlas runtime loader implementation.
 * U02: Loads ui_atlas texture + ui_atlas.json from EQE1 flat pack (ui.eqe1).
 */

#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/constrained_texture_cache.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"
#include <json/json.h>

namespace EQT {
namespace Graphics {

bool UIAtlas::load(irr::video::IVideoDriver* driver, ConstrainedTextureCache* cache) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::UI);  // T491
    if (!driver || !cache) return false;

    // Load texture via constrained cache — EQE1 compressed entry "ui_atlas"
    std::vector<char> emptyData;
    texture_ = cache->getOrLoad("ui_atlas", emptyData);
    if (!texture_) texture_ = cache->uploadTexture("ui_atlas");

    if (!texture_) {
        LOG_FATAL(MOD_GRAPHICS, "UIAtlas: texture 'ui_atlas' not found in any EQE1 flat pack");
        return false;
    }

    // T202: the atlas is drawn every frame for the life of the process and we
    // keep a RAW pointer to it, but it lives in the zone-scoped LRU cache like
    // any other EQE1 texture. Without this, memory pressure evicted it and
    // UIRenderer::endFrame drew a freed COGLES2Texture -- a SIGSEGV that
    // reproduced in 7 of 8 zone/tree/preset combinations and blocked EQE1
    // entirely (T201).
    cache->protectTexture("ui_atlas");

    // T430 (board B376): the item-icon sheets, out of the same pack and the
    // same cache. EAGER on purpose -- getOrLoad() exits the process on a pack
    // miss, so loading these on first use would turn a stale pack into a
    // mid-SESSION exit the moment a player opened their inventory. Here it is
    // the same startup failure a missing ui_atlas already gives.
    icons_.load(cache);
    spellIcons_.load(cache);

    // Load sprite rects from JSON raw data entry in EQE1
    const uint8_t* jsonData = nullptr;
    uint32_t jsonSize = 0;
    if (!cache->getEQE1RawData("ui_atlas.json", jsonData, jsonSize) || !jsonData || jsonSize == 0) {
        LOG_FATAL(MOD_GRAPHICS, "UIAtlas: raw data 'ui_atlas.json' not found in any EQE1 flat pack");
        texture_ = nullptr;
        return false;
    }

    if (!loadJsonFromMemory(reinterpret_cast<const char*>(jsonData), jsonSize)) {
        LOG_FATAL(MOD_GRAPHICS, "UIAtlas: failed to parse ui_atlas.json from EQE1");
        texture_ = nullptr;
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "UIAtlas: loaded from EQE1 ({}x{})",
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

bool UIAtlas::loadJsonFromMemory(const char* data, size_t size) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(data, data + size, &root, &errors)) {
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
        {"bar_hp",              UISprite::BarHP},
        {"bar_mana",            UISprite::BarMana},
        {"bar_stamina",         UISprite::BarStamina},
        {"bar_xp",              UISprite::BarXP},
        {"bar_casting",         UISprite::BarCasting},
        {"bar_background",      UISprite::BarBackground},
        {"panel_background",    UISprite::PanelBackground},
        {"white",               UISprite::White},
        {"coin_platinum",       UISprite::CoinPlatinum},
        {"coin_gold",           UISprite::CoinGold},
        {"coin_silver",         UISprite::CoinSilver},
        {"coin_copper",         UISprite::CoinCopper},
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
