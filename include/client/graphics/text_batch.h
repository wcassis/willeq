/*
 * TextBatch — batched text renderer for UI and 3D name tags.
 *
 * U01: Replaces per-string font->draw() calls scattered across 20+ UI files
 * with a centralized batch. All text is accumulated via addText/addText3D,
 * then flushed in one pass.
 *
 * Phase 1 (this implementation): Uses Irrlicht's IGUIFont::draw() internally.
 * Centralizes all text rendering through one API, enabling future optimization.
 *
 * Phase 2 (future): Replace with direct GL VBO + custom font atlas for true
 * single-draw-call text rendering with per-vertex color.
 */

#pragma once

#include <irrlicht.h>
#include <string>
#include <vector>
#include <unordered_map>

#include "client/graphics/ui/ui_quad_batch.h"  // T462: glyph runs
#include "client/graphics/ui/bitmap_glyph_table.h"

namespace EQT {
namespace Graphics {

/**
 * TextBatch — accumulates text across the frame, flushes in bulk.
 *
 * Usage:
 *   textBatch.begin();
 *   textBatch.addText("Hello", 10, 20, SColor(255,255,255,255));
 *   textBatch.addText("World", 10, 40, SColor(255,255,200,0));
 *   textBatch.addText3D("Goblin", worldPos, camera, driver, SColor(255,255,255,255));
 *   textBatch.flush(driver);
 */
class TextBatch {
public:
    TextBatch() = default;
    ~TextBatch() = default;

    /**
     * Initialize from Irrlicht's built-in font.
     * Extracts glyph metrics. Must be called before addText.
     */
    bool init(irr::gui::IGUIEnvironment* guienv);

    bool isInitialized() const { return font_ != nullptr; }

    /** Clear accumulated text for a new frame. */
    void begin();

    /** Add screen-space text (UTF-8). */
    void addText(const std::string& text, irr::s32 x, irr::s32 y,
                 irr::video::SColor color = irr::video::SColor(255, 255, 255, 255));

    /** Add screen-space text (wide string). */
    void addTextW(const wchar_t* text, irr::s32 x, irr::s32 y,
                  irr::video::SColor color = irr::video::SColor(255, 255, 255, 255));

    /** Add text centered horizontally within a rect. */
    void addTextCentered(const std::string& text, const irr::core::rect<irr::s32>& rect,
                         irr::video::SColor color = irr::video::SColor(255, 255, 255, 255));

    /**
     * Add 3D world-space text projected to screen (for entity name tags).
     * Requires active camera and driver for projection + viewport.
     */
    void addText3D(const std::string& text,
                   const irr::core::vector3df& worldPos,
                   irr::scene::ICameraSceneNode* camera,
                   irr::video::IVideoDriver* driver,
                   irr::video::SColor color = irr::video::SColor(255, 255, 255, 255));

    /** Flush all accumulated text to the driver. */
    void flush(irr::video::IVideoDriver* driver);

    /** Measure text width in pixels. */
    irr::s32 getTextWidth(const std::string& text) const;
    irr::s32 getTextWidthW(const wchar_t* text) const;

    /** Font line height in pixels. */
    irr::s32 getLineHeight() const { return lineHeight_; }

    /** Number of text entries accumulated since begin(). */
    size_t getQuadCount() const;

    /**
     * T462 (board B506): driver draw calls the last flush() issued. With
     * batching, the glyphs of every plain entry share texture runs, so a
     * frame's text is a couple of draws; without it, one font draw per entry.
     */
    size_t lastFlushDriverDraws() const { return lastFlushDriverDraws_; }

    /**
     * T462 test seam: false flushes one IGUIFont::draw() per entry, the
     * pre-T462 flush, so a test can compare the two on the same device.
     */
    void setBatchingEnabled(bool enabled) { batchingEnabled_ = enabled; }

private:
    // T462: the built-in bitmap font's glyphs, laid out the way
    // CGUIFont::draw() lays them out (B975 moved the table into
    // client/graphics/ui/bitmap_glyph_table.h, shared with the name tags).
    BitmapGlyphTable glyphs_;
    bool glyphTableBuilt_ = false;
    bool glyphTableUsable_ = false;
    bool batchingEnabled_ = true;
    size_t lastFlushDriverDraws_ = 0;
    UiQuadBatch quadBatch_;
    std::vector<UiQuad> glyphRun_;

    irr::gui::IGUIFont* font_ = nullptr;
    irr::s32 lineHeight_ = 14;

    // Per-glyph advance widths for measurement
    std::unordered_map<wchar_t, irr::s32> glyphAdvance_;

    // Accumulated text entries
    struct TextEntry {
        std::wstring text;
        irr::s32 x, y;
        irr::video::SColor color;
        bool hcenter;
        bool vcenter;
    };
    std::vector<TextEntry> entries_;
};

} // namespace Graphics
} // namespace EQT
