/*
 * UIAtlas — runtime loader for the pre-generated UI sprite atlas.
 *
 * U02: Loads ui_atlas texture + JSON from EQE1 flat pack (ui.eqe1).
 * FATAL if files are missing when enableTextureAtlas is true.
 * No procedural generation — atlas is created offline by ui_atlas_builder.
 */

#pragma once

#include "client/graphics/constrained_texture_cache.h"
#include <irrlicht.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace EQT {
namespace Graphics {

// Named UI sprite regions in the atlas
enum class UISprite : uint8_t {
    SlotBackground,
    SlotBorderNormal,
    SlotBorderHover,
    SlotBorderSelected,
    ButtonNormal,
    ButtonPressed,
    ButtonDisabled,
    BarHP,
    BarMana,
    BarStamina,
    BarXP,
    BarCasting,
    BarBackground,
    PanelBackground,
    PanelBorder,
    White,
    ScrollIndicator,
    // T398 (board B218): the four coin decals the official client blits into
    // its IW_Money buttons, lifted from window_pieces01.tga (90,140) by
    // ui_atlas_builder --uifiles-path. Order matches
    // InventoryLayout::COIN_PLATINUM..COIN_COPPER, so
    // coinSprite(denom) is CoinPlatinum + denom.
    CoinPlatinum,
    CoinGold,
    CoinSilver,
    CoinCopper,
    Count
};

struct UISpriteRect {
    irr::core::rect<irr::s32> srcRect;
};

class UIAtlas {
public:
    UIAtlas() = default;
    ~UIAtlas() = default;

    /**
     * Load atlas texture + JSON from EQE1 flat pack (ui.eqe1).
     * Texture loaded as ETC1 compressed entry "ui_atlas".
     * JSON loaded as raw data entry "ui_atlas.json".
     * Returns false on failure (FATAL — all assets must exist).
     */
    bool load(irr::video::IVideoDriver* driver, ConstrainedTextureCache* cache);

    bool isLoaded() const { return texture_ != nullptr; }

    irr::video::ITexture* getTexture() { return texture_; }
    const UISpriteRect& getSprite(UISprite sprite) const;

private:
    bool loadJsonFromMemory(const char* data, size_t size);
    void mapSprite(const std::string& name, const irr::core::rect<irr::s32>& rect);

    irr::video::ITexture* texture_ = nullptr;
    UISpriteRect sprites_[static_cast<int>(UISprite::Count)];
    UISpriteRect emptySprite_{};
};

} // namespace Graphics
} // namespace EQT
