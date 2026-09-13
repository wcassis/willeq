/*
 * UIRenderer implementation.
 * U03b: Batched quad drawing with atlas and fallback paths.
 */

#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/text_batch.h"

#include <chrono>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

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
            //
            // T462 (board B506): consecutive commands that sample the same
            // texture are a RUN, and a run is two driver draw calls, not one
            // per command. On the Orange Pi the open inventory's ~100 commands
            // were ~100 full 2D state setups + client-array draws, +6-7 ms of
            // Static UI per frame (T459, T462's account). The run's first
            // command still goes through draw2DImage(), which establishes the
            // whole state; drawRunTail() draws the rest with that state.
            // draw2DImageBatch() could not do this: it takes one colour for
            // the batch and no destination size.
#ifdef EQT_HAS_GLES2
            const bool canBatch = batchingEnabled_ &&
                driver_->getDriverType() == irr::video::EDT_OGLES2;
#else
            const bool canBatch = false;
#endif
            const size_t n = atlasQuads_.size();
            auto resolve = [&](size_t k) -> irr::video::ITexture* {
                return atlasQuads_[k].texture ? atlasQuads_[k].texture : atlasTex;
            };
            size_t i = 0;
            while (i < n) {
                irr::video::ITexture* tex = resolve(i);
                if (!tex) { ++i; continue; }  // drew nothing before T462 either
                ++flushStats_.textureRuns;
                // The run: every following command on the same texture. A
                // command that resolves to no texture draws nothing and is
                // passed over without ending the run, as the old loop did.
                size_t end = i + 1;
                size_t tail = 0;
                while (end < n) {
                    irr::video::ITexture* t = resolve(end);
                    if (t && t != tex) break;
                    if (t) ++tail;
                    ++end;
                }
                const auto& first = atlasQuads_[i];
                driver_->draw2DImage(tex, first.destRect, first.srcRect, nullptr,
                    &first.color, true);
                ++flushStats_.driverDraws;
                if (tail > 0 && canBatch) {
                    drawRunTail(tex, atlasTex, i + 1, end);
                    ++flushStats_.driverDraws;
                } else if (tail > 0) {
                    for (size_t k = i + 1; k < end; ++k) {
                        const auto& q = atlasQuads_[k];
                        if (!resolve(k)) continue;
                        driver_->draw2DImage(tex, q.destRect, q.srcRect, nullptr,
                            &q.color, true);
                        ++flushStats_.driverDraws;
                    }
                }
                i = end;
            }
        }
        atlasQuads_.clear();
    }
    const auto textStart = Clock::now();

    // Flush text batch
    if (textBatch_) {
        flushStats_.textEntries = textBatch_->getQuadCount();
        textBatch_->flush(driver_);
    }
    const auto flushEnd = Clock::now();
    flushStats_.commandFlushUs =
        std::chrono::duration_cast<std::chrono::microseconds>(textStart - flushStart).count();
    flushStats_.textFlushUs =
        std::chrono::duration_cast<std::chrono::microseconds>(flushEnd - textStart).count();
}

// T462 (board B506): the tail of a texture run as one draw call.
//
// Why this paints exactly what one draw2DImage() per command painted:
// COpenGLES2Driver::draw2DImage() (docker/irrlicht-drm/COpenGLES2Driver.cpp)
// leaves, after drawing the run's FIRST command, the UI2D program in use with
// its world-view-projection and sampler uniforms set, `tex` on unit 0 and its
// alpha companion on unit 1, the same blend state, scissor off, and vertex
// attributes 0/2/3 (position, colour, texcoord0) enabled with 1 and 4
// disabled -- and a later draw2DImage() for the same texture would set every
// one of those to the same values again. The only per-draw inputs are the
// four vertices, and they are computed below with the driver's own
// arithmetic, in the driver's own 9-float layout (x, y, z, r, g, b, a, u, v)
// and triangle order. One GL draw of the concatenated quads rasterises the
// triangles in the same order as the separate draws did, so blending sees the
// same sequence. TheBatchedFlushPaintsExactlyThePixelsOfThePerCommandFlush
// pins that on the GLES2 device.
void UIRenderer::drawRunTail(irr::video::ITexture* tex, irr::video::ITexture* atlasTex,
                             size_t begin, size_t end) {
#ifdef EQT_HAS_GLES2
    // docker/irrlicht-drm/COGLES2Shaders.h: attribute slots are fixed with
    // glBindAttribLocation before every program link.
    constexpr GLuint kPosition = 0, kColor = 2, kTexCoord0 = 3;
    constexpr size_t kMaxQuadsPerDraw = 65536 / 4;  // 16-bit indices

    const irr::core::dimension2d<irr::u32>& texSize = tex->getOriginalSize();
    const irr::f32 invW = 1.0f / (irr::f32)texSize.Width;
    const irr::f32 invH = 1.0f / (irr::f32)texSize.Height;

    auto flush = [&]() {
        if (runIndices_.empty()) return;
        const float* v = runVertices_.data();
        glVertexAttribPointer(kPosition, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(irr::f32), v);
        glEnableVertexAttribArray(kPosition);
        glVertexAttribPointer(kColor, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(irr::f32), v + 3);
        glEnableVertexAttribArray(kColor);
        glVertexAttribPointer(kTexCoord0, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(irr::f32), v + 7);
        glEnableVertexAttribArray(kTexCoord0);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(runIndices_.size()),
                       GL_UNSIGNED_SHORT, runIndices_.data());
        runVertices_.clear();
        runIndices_.clear();
    };

    runVertices_.clear();
    runIndices_.clear();
    for (size_t k = begin; k < end; ++k) {
        const DrawCmd& q = atlasQuads_[k];
        if ((q.texture ? q.texture : atlasTex) != tex) continue;  // resolves to nothing
        if (runIndices_.size() / 6 == kMaxQuadsPerDraw) flush();

        // COpenGLES2Driver::draw2DImage()'s own vertex arithmetic.
        irr::f32 srcLeft = q.srcRect.UpperLeftCorner.X * invW;
        irr::f32 srcTop = q.srcRect.UpperLeftCorner.Y * invH;
        irr::f32 srcRight = q.srcRect.LowerRightCorner.X * invW;
        irr::f32 srcBottom = q.srcRect.LowerRightCorner.Y * invH;

        irr::f32 dstLeft = (irr::f32)q.destRect.UpperLeftCorner.X;
        irr::f32 dstTop = (irr::f32)q.destRect.UpperLeftCorner.Y;
        irr::f32 dstRight = (irr::f32)q.destRect.LowerRightCorner.X;
        irr::f32 dstBottom = (irr::f32)q.destRect.LowerRightCorner.Y;

        irr::f32 r = q.color.getRed() / 255.0f;
        irr::f32 g = q.color.getGreen() / 255.0f;
        irr::f32 b = q.color.getBlue() / 255.0f;
        irr::f32 a = q.color.getAlpha() / 255.0f;

        const irr::f32 quad[4 * 9] = {
            dstLeft,  dstTop,    0.0f, r, g, b, a, srcLeft,  srcTop,
            dstRight, dstTop,    0.0f, r, g, b, a, srcRight, srcTop,
            dstRight, dstBottom, 0.0f, r, g, b, a, srcRight, srcBottom,
            dstLeft,  dstBottom, 0.0f, r, g, b, a, srcLeft,  srcBottom
        };
        const uint16_t base = static_cast<uint16_t>(runVertices_.size() / 9);
        runVertices_.insert(runVertices_.end(), quad, quad + 4 * 9);
        const uint16_t idx[6] = {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2),
                                 base, static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3)};
        runIndices_.insert(runIndices_.end(), idx, idx + 6);
    }
    flush();
#else
    (void)tex; (void)atlasTex; (void)begin; (void)end;
#endif
}

} // namespace Graphics
} // namespace EQT
