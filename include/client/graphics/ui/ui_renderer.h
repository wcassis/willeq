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
     * stretched to fill rect — the one-off, per-frame-optional case of
     * blitting a separately-rendered texture (T60: the inventory
     * paperdoll). No-op if texture is null.
     *
     * T294: this used to draw IMMEDIATELY while drawRect()/drawSprite()
     * only ACCUMULATED, so on the atlas path every quad submitted before
     * the blit was flushed in endFrame() ON TOP of it. The inventory
     * popup's own background is submitted before the paperdoll, so the
     * live atlas-backed client painted the doll and then covered it —
     * see review/T294/before/. The blit now enters the SAME ordered
     * command list as the atlas quads, so submission order is draw order
     * for every UIRenderer call.
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

    // For atlas path: accumulated draw commands, flushed in SUBMISSION
    // ORDER by endFrame(). `texture == nullptr` means "the atlas texture"
    // (the overwhelmingly common case — drawRect/drawSprite); a non-null
    // texture is an external blit (drawTexture) that breaks the atlas run
    // so that later quads land on top of it and earlier quads under it.
    struct DrawCmd {
        irr::core::rect<irr::s32> destRect;
        irr::core::rect<irr::s32> srcRect;
        irr::video::SColor color;
        irr::video::ITexture* texture = nullptr;
    };
    std::vector<DrawCmd> atlasQuads_;
};

} // namespace Graphics
} // namespace EQT
