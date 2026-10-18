/*
 * UIAtlas — runtime loader for the pre-generated UI sprite atlas.
 *
 * U02: Loads ui_atlas texture + JSON from EQE1 flat pack (ui.eqe1).
 * FATAL if files are missing when enableTextureAtlas is true.
 * No procedural generation — atlas is created offline by ui_atlas_builder.
 */

#pragma once

#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/ui/item_icon_sheets.h"
#include "client/graphics/ui/spell_icon_sheets.h"
#include "client/graphics/ui/spellbook_backdrop_loader.h"
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
    // T424 (board B351): SlotBorderSelected, ButtonNormal, ButtonPressed,
    // ButtonDisabled, PanelBorder and ScrollIndicator were removed here, in
    // UIAtlas::mapSprite's nameMap and in tools/ui_atlas_builder together.
    // T416's census found all six with no consumer, and none is oracle art --
    // they are procedural leftovers of the WindowManager UI deleted in
    // 0f25cb57. Giving the loot buttons and the chat scrollbar thumb real
    // chrome is board B385's oracle question (window_pieces01.tga), not a
    // matter of re-adding a gradient. Pinned by tests/test_ui_sprite_roster.cpp.
    BarHP,
    BarMana,
    BarStamina,
    BarXP,
    BarCasting,
    BarBackground,
    PanelBackground,
    White,
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

    /**
     * T430 (board B376): the item-icon sheets, lifted from the official
     * client's dragitem*.tga. They are a SEPARATE pair of pack entries, not
     * regions of ui_atlas -- 1224 40x40 icons do not fit in a 256x256 UI
     * atlas -- and they are loaded by load() below, beside ui_atlas itself.
     * Held here because UIAtlas is the object every panel already reaches
     * through UIRenderer; see UIRenderer::drawItemIcon().
     */
    ItemIconSheets& icons() { return icons_; }
    const ItemIconSheets& icons() const { return icons_; }

    /**
     * B1077 (GitLab #397): the spell-icon sheets (gem bar / spellbook /
     * buff families + the CW_SpellGemHolder piece), lifted from the
     * official client's gemicons*.tga / Spells*.tga / Spell_Icons.tga.
     * Separate pack entries, loaded by load() beside the item sheets; see
     * UIRenderer::drawSpellIcon().
     */
    SpellIconSheets& spellIcons() { return spellIcons_; }
    const SpellIconSheets& spellIcons() const { return spellIcons_; }

    /**
     * B1076 (GitLab #396): the composed spellbook backdrop
     * (data/textures/spellbook_backdrop.bmp, one ETC1 pack entry the way
     * spell_icon_sheets carries its four). Loaded by load() beside the
     * sheets; renderSpellbookPopup() blits its four tiles through
     * UIRenderer::drawSpellBookBackdrop().
     */
    SpellBookBackdropLoader& spellBookBackdrop() { return spellBookBackdrop_; }
    const SpellBookBackdropLoader& spellBookBackdrop() const { return spellBookBackdrop_; }

private:
    bool loadJsonFromMemory(const char* data, size_t size);
    void mapSprite(const std::string& name, const irr::core::rect<irr::s32>& rect);

    irr::video::ITexture* texture_ = nullptr;
    ItemIconSheets icons_;
    SpellIconSheets spellIcons_;
    SpellBookBackdropLoader spellBookBackdrop_;  // B1076 (GitLab #396)
    UISpriteRect sprites_[static_cast<int>(UISprite::Count)];
    UISpriteRect emptySprite_{};
};

} // namespace Graphics
} // namespace EQT
