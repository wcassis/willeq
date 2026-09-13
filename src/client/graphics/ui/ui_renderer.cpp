/*
 * UIRenderer implementation.
 * U03b: Batched quad drawing with atlas and fallback paths.
 */

#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/text_batch.h"

#include <chrono>

namespace EQT {
namespace Graphics {

void UIRenderer::init(irr::video::IVideoDriver* driver, UIAtlas* atlas, TextBatch* textBatch) {
    driver_ = driver;
    atlas_ = atlas;
    textBatch_ = textBatch;
    if (textBatch_) textBatch_->setBatchingEnabled(batchingEnabled_);
}

void UIRenderer::setBatchingEnabled(bool enabled) {
    batchingEnabled_ = enabled;
    if (textBatch_) textBatch_->setBatchingEnabled(enabled);
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
        // Fallback: a solid rectangle. T462: QUEUED like everything else and
        // drawn by endFrame() in submission order (it used to be an immediate
        // draw2DRectangle() here, one GL draw per call during submission).
        atlasQuads_.push_back({rect, {}, color, nullptr, true});
    }
}

void UIRenderer::drawSprite(const irr::core::rect<irr::s32>& rect, uint8_t spriteId,
                            irr::video::SColor tint) {
    if (!driver_) return;

    if (atlas_ && spriteId < static_cast<uint8_t>(UISprite::Count)) {
        const auto& sprite = atlas_->getSprite(static_cast<UISprite>(spriteId));
        atlasQuads_.push_back({rect, sprite.srcRect, tint});
    } else {
        // Fallback: a dark rectangle (queued, T462 -- see drawRect()).
        atlasQuads_.push_back({rect, {}, irr::video::SColor(180, 20, 20, 25), nullptr, true});
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

    // T294: this MUST be ordered against the accumulated quads, not drawn
    // immediately — the popup background that the caller submitted a few
    // calls earlier is still sitting in the queue and would be flushed over
    // the top of it. Queue it as a command with its own texture; endFrame()
    // replays the queue in submission order. Opaque white is the same colour
    // draw2DImage falls back to when the `colors` argument is null (T275), so
    // the blit is unchanged. T462: the fallback path queues too (it used to
    // blit immediately, which was only correct while its rectangles were
    // immediate as well).
    atlasQuads_.push_back({rect, srcRect,
        irr::video::SColor(255, 255, 255, 255), texture});
}

bool UIRenderer::drawItemIcon(const irr::core::rect<irr::s32>& rect,
                              uint32_t iconId) {
    if (!driver_ || !atlas_) return false;

    irr::core::rect<irr::s32> srcRect;
    irr::video::ITexture* tex = atlas_->icons().lookup(iconId, srcRect);
    if (!tex) return false;

    const irr::video::SColor white(255, 255, 255, 255);

    // Same ordering contract as drawTexture(): on the atlas path the icon is
    // queued as a command carrying its OWN texture, so it replays at its
    // submission position -- behind anything a panel draws after it (a
    // quantity string, a hover border) and in front of the slot background
    // it was drawn after.
    if (atlas_->getTexture()) {
        atlasQuads_.push_back({rect, srcRect, white, tex});
        return true;
    }

    driver_->draw2DImage(tex, rect, srcRect, nullptr, &white, true);
    return true;
}

void UIRenderer::endFrame() {
    if (!driver_) return;

    // T462: the Static UI sub-sections of the frame timing breakdown.
    using Clock = std::chrono::steady_clock;
    const auto flushStart = Clock::now();
    flushStats_.commands = atlasQuads_.size();
    flushStats_.driverDraws = 0;
    flushStats_.textureRuns = 0;

    // Flush the queued draw commands IN SUBMISSION ORDER (T294).
    //
    // T275: the per-quad colour MUST reach the driver. The flush used to pass
    // nullptr for draw2DImage's `colors` argument, which makes the driver fall
    // back to opaque white -- so every drawRect() tint and every drawSprite()
    // tint was thrown away at the last step. The spell gems' state-coloured
    // borders, the gold memorize marker (T272), the cooldown darkening overlay
    // and the chat scrollbar thumb all depend on it.
    //
    // T294: a command with its own texture (drawTexture — the T60 paperdoll
    // RTT, drawItemIcon) is replayed at its submission position, so a panel
    // background submitted before it stays behind it and a slot border
    // submitted after it stays in front. Commands with a null texture are
    // atlas quads; if the atlas has no texture they are skipped, exactly as
    // before.
    //
    // T462 (board B506): consecutive commands that share their GL state are a
    // RUN, and a run is at most two driver draw calls, not one per command
    // (UiQuadBatch: the first quad through the driver, the rest in one draw).
    // On the Orange Pi the open inventory's ~100 commands were ~100 full 2D
    // state setups + client-array draws. A texture run is every following
    // command on the same texture; a solid run (the fallback path's
    // rectangles) every following solid command with the same blend enable,
    // because draw2DRectangle() turns blending on only for alpha < 255.
    if (!atlasQuads_.empty()) {
        irr::video::ITexture* atlasTex = atlas_ ? atlas_->getTexture() : nullptr;
        auto resolve = [&](const DrawCmd& c) -> irr::video::ITexture* {
            return c.texture ? c.texture : atlasTex;
        };
        const size_t n = atlasQuads_.size();
        size_t i = 0;
        while (i < n) {
            const DrawCmd& head = atlasQuads_[i];
            run_.clear();
            if (head.solid) {
                const bool blend = head.color.getAlpha() < 255;
                size_t end = i;
                while (end < n && atlasQuads_[end].solid &&
                       (atlasQuads_[end].color.getAlpha() < 255) == blend) {
                    run_.push_back({atlasQuads_[end].destRect, {}, atlasQuads_[end].color});
                    ++end;
                }
                ++flushStats_.textureRuns;
                flushStats_.driverDraws += quadBatch_.drawSolidRun(driver_, run_, batchingEnabled_);
                i = end;
                continue;
            }
            irr::video::ITexture* tex = resolve(head);
            if (!tex) { ++i; continue; }  // drew nothing before T462 either
            // A command that resolves to no texture draws nothing and is
            // passed over without ending the run, as the old loop did.
            size_t end = i;
            while (end < n && !atlasQuads_[end].solid) {
                irr::video::ITexture* t = resolve(atlasQuads_[end]);
                if (t && t != tex) break;
                if (t) run_.push_back({atlasQuads_[end].destRect, atlasQuads_[end].srcRect,
                                       atlasQuads_[end].color});
                ++end;
            }
            ++flushStats_.textureRuns;
            flushStats_.driverDraws += quadBatch_.drawTexturedRun(driver_, tex, run_, batchingEnabled_);
            i = end;
        }
        atlasQuads_.clear();
    }
    const auto textStart = Clock::now();

    // Flush text batch
    if (textBatch_) {
        flushStats_.textEntries = textBatch_->getQuadCount();
        textBatch_->flush(driver_);
        flushStats_.textDraws = textBatch_->lastFlushDriverDraws();
    }
    const auto flushEnd = Clock::now();
    flushStats_.commandFlushUs =
        std::chrono::duration_cast<std::chrono::microseconds>(textStart - flushStart).count();
    flushStats_.textFlushUs =
        std::chrono::duration_cast<std::chrono::microseconds>(flushEnd - textStart).count();
}

} // namespace Graphics
} // namespace EQT
