/*
 * UIRenderer implementation.
 * U03b: Batched quad drawing with atlas and fallback paths.
 */

#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/text_batch.h"

namespace EQT {
namespace Graphics {

void UIRenderer::init(irr::video::IVideoDriver* driver, UIAtlas* atlas, TextBatch* textBatch) {
    driver_ = driver;
    atlas_ = atlas;
    textBatch_ = textBatch;
}

void UIRenderer::beginFrame() {
    atlasQuads_.clear();
    if (textBatch_) textBatch_->begin();
}

void UIRenderer::drawRect(const irr::core::rect<irr::s32>& rect, irr::video::SColor color) {
    if (!driver_) return;

    if (atlas_) {
        // Use the white sprite from atlas, tinted with color
        const auto& white = atlas_->getSprite(UISprite::White);
        atlasQuads_.push_back({rect, white.srcRect, color});
    } else {
        // Fallback: immediate draw
        driver_->draw2DRectangle(color, rect);
    }
}

void UIRenderer::drawSprite(const irr::core::rect<irr::s32>& rect, uint8_t spriteId,
                            irr::video::SColor tint) {
    if (!driver_) return;

    if (atlas_ && spriteId < static_cast<uint8_t>(UISprite::Count)) {
        const auto& sprite = atlas_->getSprite(static_cast<UISprite>(spriteId));
        atlasQuads_.push_back({rect, sprite.srcRect, tint});
    } else {
        // Fallback: draw a dark rectangle
        driver_->draw2DRectangle(irr::video::SColor(180, 20, 20, 25), rect);
    }
}

void UIRenderer::drawFrame(const irr::core::rect<irr::s32>& rect,
                           irr::video::SColor color, irr::s32 thickness) {
    if (!driver_ || thickness <= 0) return;

    const irr::s32 x1 = rect.UpperLeftCorner.X;
    const irr::s32 y1 = rect.UpperLeftCorner.Y;
    const irr::s32 x2 = rect.LowerRightCorner.X;
    const irr::s32 y2 = rect.LowerRightCorner.Y;

    // Exactly the four rects, in exactly the order, that every open-coded
    // copy of this used -- so migrating them cannot move a pixel.
    drawRect({x1, y1, x2, y1 + thickness}, color);           // Top
    drawRect({x1, y2 - thickness, x2, y2}, color);           // Bottom
    drawRect({x1, y1, x1 + thickness, y2}, color);           // Left
    drawRect({x2 - thickness, y1, x2, y2}, color);           // Right
}

void UIRenderer::drawBar(const irr::core::rect<irr::s32>& rect, float fillPercent,
                         irr::video::SColor fillColor, irr::video::SColor bgColor) {
    if (!driver_) return;

    fillPercent = std::max(0.0f, std::min(1.0f, fillPercent));

    // Background
    drawRect(rect, bgColor);

    // Fill
    if (fillPercent > 0.0f) {
        irr::s32 fillW = static_cast<irr::s32>(rect.getWidth() * fillPercent);
        irr::core::rect<irr::s32> fillRect(
            rect.UpperLeftCorner.X, rect.UpperLeftCorner.Y,
            rect.UpperLeftCorner.X + fillW, rect.LowerRightCorner.Y);
        drawRect(fillRect, fillColor);
    }
}

void UIRenderer::drawBarSprite(const irr::core::rect<irr::s32>& rect, float fillPercent,
                               uint8_t fillSpriteId, uint8_t bgSpriteId) {
    if (!driver_) return;

    fillPercent = std::max(0.0f, std::min(1.0f, fillPercent));

    // Background sprite
    drawSprite(rect, bgSpriteId);

    // Fill sprite (clipped to fill percentage)
    if (fillPercent > 0.0f) {
        irr::s32 fillW = static_cast<irr::s32>(rect.getWidth() * fillPercent);
        irr::core::rect<irr::s32> fillRect(
            rect.UpperLeftCorner.X, rect.UpperLeftCorner.Y,
            rect.UpperLeftCorner.X + fillW, rect.LowerRightCorner.Y);
        drawSprite(fillRect, fillSpriteId);
    }
}

void UIRenderer::drawPanel(const irr::core::rect<irr::s32>& rect) {
    drawSprite(rect, static_cast<uint8_t>(UISprite::PanelBackground));

    // Border (1px lines around the panel)
    drawFrame(rect, irr::video::SColor(255, 60, 60, 70));
}

void UIRenderer::drawTexture(const irr::core::rect<irr::s32>& rect, irr::video::ITexture* texture) {
    if (!driver_ || !texture) return;

    // T60 finding: a render-target texture's V=0 corresponds to the
    // BOTTOM of what was rendered into it (standard GL FBO convention —
    // origin bottom-left), while draw2DImage's source-rect convention
    // (built for ordinary loaded images, V=0 = top row) assumes the
    // opposite. Blitting an RTT with a normal top-to-bottom source rect
    // renders it upside-down — confirmed visually via the paperdoll
    // captures before this fix. Only drawTexture() (this RTT-blit path)
    // needs the flip; drawSprite()'s atlas path uses ordinary loaded
    // textures and is unaffected.
    const auto size = texture->getOriginalSize();
    irr::core::rect<irr::s32> srcRect(0, static_cast<irr::s32>(size.Height),
                                       static_cast<irr::s32>(size.Width), 0);

    // T294: on the atlas path this MUST be ordered against the accumulated
    // atlas quads, not drawn immediately — the popup background that the
    // caller submitted a few calls earlier is still sitting in the queue
    // and would be flushed over the top of it. Queue it as a command with
    // its own texture; endFrame() replays the queue in submission order.
    // Opaque white is the same colour draw2DImage falls back to when the
    // `colors` argument is null (T275), so the blit is unchanged.
    if (atlas_) {
        atlasQuads_.push_back({rect, srcRect,
            irr::video::SColor(255, 255, 255, 255), texture});
        return;
    }

    // Fallback path (no atlas): drawRect/drawSprite are immediate too, so
    // submission order is already draw order — keep the direct blit.
    driver_->draw2DImage(texture, rect, srcRect, nullptr, nullptr, true);
}

void UIRenderer::endFrame() {
    if (!driver_) return;

    // Flush the queued draw commands IN SUBMISSION ORDER (T294).
    if (atlas_ && !atlasQuads_.empty()) {
        auto* atlasTex = atlas_->getTexture();
        {
            // Draw each quad using draw2DImage with source rect.
            // T275: the per-quad colour MUST be handed to the driver here.
            // This loop used to pass nullptr for draw2DImage's `colors`
            // argument, which makes the driver fall back to opaque white --
            // so every drawRect() tint and every drawSprite() tint that
            // beginFrame..endFrame had carefully accumulated was thrown away
            // at the last step. The spell gems' state-coloured borders, the
            // gold memorize marker (T272), the cooldown darkening overlay and
            // the chat scrollbar thumb all depend on it.
            //
            // The overload takes a POINTER TO AN ARRAY of four corner colours
            // and this driver reads colors[0]; a single-element address is
            // what every other caller in the tree passes.
            //
            // T294: a command with its own texture (drawTexture — the T60
            // paperdoll RTT) is replayed at its submission position, so a
            // panel background submitted before it stays behind it and a
            // slot border submitted after it stays in front. Commands with
            // a null texture are atlas quads; if the atlas has no texture
            // they are skipped, exactly as before.
            // Future optimization: batch runs of atlas quads into a single
            // draw2DImageBatch call.
            for (const auto& q : atlasQuads_) {
                auto* tex = q.texture ? q.texture : atlasTex;
                if (!tex) continue;
                driver_->draw2DImage(tex, q.destRect, q.srcRect, nullptr,
                    &q.color, true);
            }
        }
        atlasQuads_.clear();
    }

    // Flush text batch
    if (textBatch_) {
        textBatch_->flush(driver_);
    }
}

} // namespace Graphics
} // namespace EQT
