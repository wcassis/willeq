/*
 * UIRenderer — batched quad drawing API for the static layout UI.
 *
 * U03b: Collects UI quads and flushes them efficiently.
 * Two paths: atlas-backed (textured quads) and fallback (solid rectangles,
 * when no UI atlas is loaded -- the client runs this one whenever it is started
 * without --eqe1-path). Since T462 BOTH paths queue every call and endFrame()
 * flushes the queue in submission order, in runs (ui_quad_batch.h).
 */

#pragma once

#include <irrlicht.h>
#include <cstdint>
#include <vector>

#include "client/graphics/ui/ui_quad_batch.h"

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
     * T416 (board B316): draw a `thickness`-pixel rectangular OUTLINE.
     *
     * The one body for something five call sites had each open-coded as four
     * drawRect() calls (drawPanel, the casting bar, a buff icon, a spell
     * gem's state border and its 2px memorize marker) -- same four rects,
     * same order, five times.
     *
     * It is also the FIX for the coin rows. A frame drawn by stretching the
     * atlas's `slot_border_normal` sprite (a 1px border inside a 32x32 tile)
     * survives only while the destination is 32x32: the purse's rows are 18px
     * tall, which minifies the tile and selects mip level 1, where the 1px
     * ring has averaged into its transparent interior. That was invisible
     * until T411, because the UI painted the atlas fully opaque and the
     * washed-out ring sat on an opaque black tile; with the alpha live the
     * row's frame drops to a 17/255 contrast step. drawRect()'s 4x4 `white`
     * sprite has no such problem -- it is uniform, so every mip of it is
     * still white.
     */
    void drawFrame(const irr::core::rect<irr::s32>& rect, irr::video::SColor color,
                   irr::s32 thickness = 1);

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

    /**
     * T430 (board B376): draw an item icon from the dragitem sheets.
     *
     * Returns FALSE when there is nothing to draw -- no atlas, no pack entry,
     * or an icon id outside the 500..1723 the official sheets carry -- and
     * every caller keeps its pre-T430 text placeholder for that case, so a
     * headless test or a client without the pack looks exactly as it did.
     *
     * The oracle draws these at 40x40
     * (`<DecalSize><CX>40</CX><CY>40</CY></DecalSize>` on every A_DragItem
     * button); fable's slots are 32x32, so the destination rect is the
     * caller's and the 40x40 cell is minified into it.
     */
    bool drawItemIcon(const irr::core::rect<irr::s32>& rect, uint32_t iconId);

    /** Flush all accumulated quads to the driver. */
    void endFrame();

    /** Get the text batch for adding text. */
    TextBatch* getTextBatch() { return textBatch_; }

    /**
     * T462 (boards B505/B506): what the last endFrame() cost, for the frame
     * timing breakdown's Static UI sub-sections and the per-frame draw-count
     * pins in tests/test_static_ui_frame_cost.cpp.
     *
     * `commands` is every drawRect/drawSprite/drawTexture/drawItemIcon call
     * accumulated since beginFrame(); `driverDraws` is how many draw calls
     * endFrame() issued to the driver for them (one per command until T462
     * batched runs of commands that share their GL state); `textEntries` is
     * how many text entries the TextBatch flushed and `textDraws` how many
     * draw calls that took. The two times are wall-clock microseconds spent
     * inside the command flush and the text flush.
     *
     * TAKE-and-reset: a frame that does not call endFrame() (F11 hides the UI)
     * reads zeros rather than the previous frame's numbers.
     */
    struct FlushStats {
        size_t commands = 0;
        size_t driverDraws = 0;
        // Maximal runs of consecutive drawable commands that share their GL
        // state (one texture, or solid with one blend enable) -- the most a
        // batched flush can merge.
        size_t textureRuns = 0;
        size_t textEntries = 0;
        // Driver draw calls the text flush issued (one per entry until T462
        // batched the glyphs of every entry into texture runs).
        size_t textDraws = 0;
        int64_t commandFlushUs = 0;
        int64_t textFlushUs = 0;
    };
    FlushStats takeFlushStats() {
        FlushStats s = flushStats_;
        flushStats_ = FlushStats{};
        return s;
    }

    /**
     * T462 test seam: false replays the command list one driver draw per
     * command and the text one font draw per entry -- the pre-T462 flush --
     * so a test can compare the two paths' pixels on the same device.
     * Production never calls it.
     */
    void setBatchingEnabled(bool enabled);

private:
    irr::video::IVideoDriver* driver_ = nullptr;
    UIAtlas* atlas_ = nullptr;
    TextBatch* textBatch_ = nullptr;

    // Accumulated draw commands, flushed in SUBMISSION ORDER by endFrame().
    // `solid` is a fallback-path rectangle (no atlas: draw2DRectangle's Color2D
    // program). Otherwise `texture == nullptr` means "the atlas texture" (the
    // overwhelmingly common case — drawRect/drawSprite on the atlas path) and a
    // non-null texture is an external blit (drawTexture, drawItemIcon) that
    // breaks the atlas run so later quads land on top of it and earlier quads
    // under it.
    struct DrawCmd {
        irr::core::rect<irr::s32> destRect;
        irr::core::rect<irr::s32> srcRect;
        irr::video::SColor color;
        irr::video::ITexture* texture = nullptr;
        bool solid = false;
    };
    std::vector<DrawCmd> atlasQuads_;

    FlushStats flushStats_;
    bool batchingEnabled_ = true;
    UiQuadBatch quadBatch_;        // T462: the run draws
    std::vector<UiQuad> run_;      // reused across frames
};

} // namespace Graphics
} // namespace EQT
