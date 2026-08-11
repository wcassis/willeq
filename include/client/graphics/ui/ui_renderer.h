/*
 * UIRenderer — batched quad drawing API for the static layout UI.
 *
 * U03b: Collects UI quads and flushes them efficiently.
 * Two paths: atlas-backed (draw2DImageBatch) and fallback (draw2DRectangle).
 */

#pragma once

#include <irrlicht.h>
#include <cstdint>
#include <vector>

namespace EQT {
namespace Graphics {

class UIAtlas;
class TextBatch;

class UIRenderer {
public:
    UIRenderer() = default;
    ~UIRenderer() = default;

    /**
     * Set the atlas and text batch for this frame.
     * atlas may be nullptr (fallback path).
     */
    void init(irr::video::IVideoDriver* driver, UIAtlas* atlas, TextBatch* textBatch);

    /** Clear accumulated quads for a new frame. */
    void beginFrame();

    /**
     * Draw a filled rectangle with a solid color.
     */
    void drawRect(const irr::core::rect<irr::s32>& rect, irr::video::SColor color);

    /**
     * Draw a UI atlas sprite stretched to fill the rect.
     * Falls back to drawRect with a default color if atlas is unavailable.
     */
    void drawSprite(const irr::core::rect<irr::s32>& rect, uint8_t spriteId,
                    irr::video::SColor tint = irr::video::SColor(255, 255, 255, 255));

    /**
     * Draw a progress bar (HP, mana, etc.).
     * Draws background + filled portion.
     */
    void drawBar(const irr::core::rect<irr::s32>& rect, float fillPercent,
                 irr::video::SColor fillColor, irr::video::SColor bgColor);

    /**
     * Draw a bar using atlas sprites for fill and background.
     * spriteId is the UISprite enum value for the fill texture.
     */
    void drawBarSprite(const irr::core::rect<irr::s32>& rect, float fillPercent,
                       uint8_t fillSpriteId, uint8_t bgSpriteId);

    /**
     * Draw a bordered panel background.
     */
    void drawPanel(const irr::core::rect<irr::s32>& rect);

    /**
     * Draw an arbitrary external texture (e.g. a render-to-texture target)
     * stretched to fill rect. Unlike drawSprite(), this is not atlas-backed
     * and draws immediately rather than batching — for the one-off,
     * per-frame-optional case of blitting a separately-rendered texture
     * (T60: the inventory paperdoll). No-op if texture is null.
     */
    void drawTexture(const irr::core::rect<irr::s32>& rect, irr::video::ITexture* texture);

    /** Flush all accumulated quads to the driver. */
    void endFrame();

    /** Get the text batch for adding text. */
    TextBatch* getTextBatch() { return textBatch_; }

private:
    irr::video::IVideoDriver* driver_ = nullptr;
    UIAtlas* atlas_ = nullptr;
    TextBatch* textBatch_ = nullptr;

    // For atlas path: accumulated sprite quads
    struct SpriteQuad {
        irr::core::rect<irr::s32> destRect;
        irr::core::rect<irr::s32> srcRect;
        irr::video::SColor color;
    };
    std::vector<SpriteQuad> atlasQuads_;
};

} // namespace Graphics
} // namespace EQT
