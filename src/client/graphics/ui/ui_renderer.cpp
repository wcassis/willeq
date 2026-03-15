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
    irr::video::SColor borderColor(255, 60, 60, 70);
    irr::s32 x1 = rect.UpperLeftCorner.X;
    irr::s32 y1 = rect.UpperLeftCorner.Y;
    irr::s32 x2 = rect.LowerRightCorner.X;
    irr::s32 y2 = rect.LowerRightCorner.Y;
    drawRect({x1, y1, x2, y1 + 1}, borderColor);  // Top
    drawRect({x1, y2 - 1, x2, y2}, borderColor);  // Bottom
    drawRect({x1, y1, x1 + 1, y2}, borderColor);  // Left
    drawRect({x2 - 1, y1, x2, y2}, borderColor);  // Right
}

void UIRenderer::endFrame() {
    if (!driver_) return;

    // Flush atlas quads
    if (atlas_ && !atlasQuads_.empty()) {
        auto* tex = atlas_->getTexture();
        if (tex) {
            // Draw each quad using draw2DImage with source rect
            // Future optimization: batch into single draw2DImageBatch call
            for (const auto& q : atlasQuads_) {
                driver_->draw2DImage(tex, q.destRect, q.srcRect, nullptr,
                    nullptr, true);
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
