/*
 * UIAtlas — runtime loader for the pre-generated UI sprite atlas.
 *
 * U02: Loads ui_atlas.png + ui_atlas.json from <atlasPath>/.
 * FATAL if files are missing when enableTextureAtlas is true.
 * No procedural generation — atlas is created offline by ui_atlas_builder.
 */

#pragma once

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
     * Load atlas from <atlasPath>/ui_atlas.png + ui_atlas.json.
     * Returns false on failure (caller decides whether to FATAL).
     */
    bool load(irr::video::IVideoDriver* driver, const std::string& atlasPath);

    bool isLoaded() const { return texture_ != nullptr; }

    irr::video::ITexture* getTexture() { return texture_; }
    const UISpriteRect& getSprite(UISprite sprite) const;

private:
    bool loadJson(const std::string& jsonPath);
    void mapSprite(const std::string& name, const irr::core::rect<irr::s32>& rect);

    irr::video::ITexture* texture_ = nullptr;
    UISpriteRect sprites_[static_cast<int>(UISprite::Count)];
    UISpriteRect emptySprite_{};
};

} // namespace Graphics
} // namespace EQT
