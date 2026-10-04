/*
 * B975 (GitLab #293): every entity name tag drawn in ONE batch.
 *
 * Before: each tag was an Irrlicht CTextSceneNode (addTextSceneNode), which
 * the scene manager registered in the transparent pass, sorted far-to-near
 * and rendered one at a time: a screen projection, CGUIFont::draw()'s layout
 * (getDimension + two core::arrays), CGUISpriteBank::draw2DSpriteBatch() (more
 * arrays) and one COpenGLES2Driver::draw2DImageBatch() -- one program/uniform
 * setup and one glDrawElements over client arrays per tag. On the Orange Pi
 * that was 0.12-0.16 ms a tag: 18 ms of a 36.6 ms frame for ~150 tags in the
 * B964 crowd (refactor/B964/README.md).
 *
 * Now:
 *  - NameTagNode is the per-entity node. It keeps the ITextSceneNode interface
 *    every call site already uses (setText, setTextColor, setVisible, remove),
 *    the same parent and the same offset above the head, so visibility and
 *    placement are decided exactly as before. Instead of registering ITSELF
 *    for rendering it hands itself to the batch node -- same condition
 *    (IsVisible, reached by the parent's registration, or by
 *    ImpostorBatchNode's re-registration of a withheld entity's tag).
 *  - NameTagBatchNode is one scene node registered in the transparent pass
 *    with a distance of zero, so it renders after every other transparent node
 *    of the pass. It sorts the tags it was handed far-to-near with the scene
 *    manager's own comparison (TransparentNodeEntry, core::array::sort), projects
 *    each with CSceneCollisionManager::getScreenCoordinatesFrom3DPosition()'s
 *    arithmetic, lays each out from its cached glyph run (rebuilt only when the
 *    text changes) and centres it exactly as CGUIFont::draw(hcenter, vcenter)
 *    does, then draws every glyph of every tag through UiQuadBatch: the first
 *    glyph through the driver (it sets the program, texture, blend and
 *    uniforms) and all the rest in one glDrawElements -- TWO draw calls per
 *    frame whatever the tag count, glyph quads in the order the per-tag draws
 *    rasterised them, so blending sees the same sequence.
 *
 * Pixels: identical to the per-node path wherever no other transparent node
 * nearer than a tag overlaps that tag (before, such a node was drawn over the
 * tag; now every tag is drawn after it). tests/test_name_tag_batch.cpp diffs
 * the two paths per pixel on the GLES2 device.
 *
 * A string the glyph table cannot reproduce (a line break, a non-ASCII
 * character) is drawn through font->draw() in its place in the order.
 * `WILLEQ_NAMETAG_BATCH=0` makes EntityRenderer create the old
 * CTextSceneNodes instead (the A/B switch).
 */

#pragma once

#include "client/graphics/ui/bitmap_glyph_table.h"
#include "client/graphics/ui/ui_quad_batch.h"

#include <irrlicht.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

/** B975: the tag pass's projection, CSceneCollisionManager's exact
 *  arithmetic on the ONE proj x view product of the frame:
 *  `trans = cam->getProjectionMatrix(); trans *= cam->getViewMatrix();`
 *  (CSceneCollisionManager::getScreenCoordinatesFrom3DPosition builds that
 *  product once per call; the batch builds it once per render() and feeds it
 *  through the same multiplyWith1x4Matrix + round32 steps). A result of
 *  (-10000, -10000) means behind the camera, as the per-call path's.
 *  halfW/halfH are the driver's viewPort width/height over 2, as the
 *  per-call path's `dim`. */
void projectNameTag(const irr::core::matrix4& projView,
                    const irr::core::vector3df& world,
                    irr::s32 halfW, irr::s32 halfH,
                    irr::core::position2d<irr::s32>& out);

class NameTagBatchNode;

// B975's shrink: the tag pass's compact vertex, 20 bytes, no padding
// {f32 x, y; u8 r, g, b, a (normalized); f32 u, v}. A glyph quad is 4 of these
// (80 B), plus 6 u16 indices, against the UiQuad intermediate's
// 36 B UiQuad + 144 B of 9-float vertices + 12 B indices.
// Attribute slots 0/2/3, the same program (UI2D) and the same per-vertex
// values COpenGLES2Driver::draw2DImage() feeds (x, y, z=0 the constant 2D
// z -- the 2-component position pointer fills z with 0, red/255.0f ... via
// the normalized byte decode, src * (1.0f/texW)); tests and the per-pose
// pixel tool are the nets.
struct CompactVertex {
    irr::f32 x, y;
    irr::u8 r, g, b, a;
    irr::f32 u, v;
};
static_assert(sizeof(CompactVertex) == 20, "the compact vertex is 20 B by construction");

class NameTagNode : public irr::scene::ITextSceneNode {
public:
    NameTagNode(irr::scene::ISceneNode* parent, irr::scene::ISceneManager* mgr,
                NameTagBatchNode* batch, const wchar_t* text, irr::video::SColor color,
                const irr::core::vector3df& position);
    ~NameTagNode() override;

    void OnRegisterSceneNode() override;
    void render() override {}  // drawn by the batch
    const irr::core::aabbox3d<irr::f32>& getBoundingBox() const override { return box_; }
    irr::scene::ESCENE_NODE_TYPE getType() const override { return irr::scene::ESNT_TEXT; }

    void setText(const wchar_t* text) override;
    void setTextColor(irr::video::SColor color) override { color_ = color; }

    const std::wstring& text() const { return text_; }
    irr::video::SColor textColor() const { return color_; }

private:
    friend class NameTagBatchNode;

    // The cached layout, rebuilt by the batch when `layoutDirty_`.
    struct GlyphPlace {
        irr::s32 dx;      // pen position relative to the text's left edge
        wchar_t c;
    };
    std::wstring text_;
    irr::video::SColor color_;
    NameTagBatchNode* batch_ = nullptr;
    irr::core::aabbox3d<irr::f32> box_;
    bool layoutDirty_ = true;
    bool batchable_ = false;          // false: font->draw() for this one
    irr::s32 width_ = 0, height_ = 0; // CGUIFont::getDimension(text)
    std::vector<GlyphPlace> glyphs_;
};

class NameTagBatchNode : public irr::scene::ISceneNode {
public:
    NameTagBatchNode(irr::scene::ISceneNode* parent, irr::scene::ISceneManager* mgr,
                     irr::gui::IGUIFont* font);
    ~NameTagBatchNode() override;

    void OnRegisterSceneNode() override;
    void render() override;
    const irr::core::aabbox3d<irr::f32>& getBoundingBox() const override { return box_; }

    /** A tag asks to be drawn this frame (called from its OnRegisterSceneNode). */
    void enqueue(NameTagNode* tag);

    irr::gui::IGUIFont* font() const { return font_; }

    struct FrameStats {
        size_t tags = 0;          // tags drawn
        size_t glyphQuads = 0;    // batched glyph quads
        size_t fallbackTags = 0;  // tags drawn by font->draw()
        size_t driverDraws = 0;   // draw calls issued
        int64_t cpuMicros = 0;    // render(): sort + project + layout + quads + submit
        int64_t submitMicros = 0; // run submission (index growth + GL calls, or fallback draw)
    };
    const FrameStats& lastFrame() const { return last_; }

    /** Bytes held by the batch's reusable buffers (the owner row's value). */
    size_t bufferBytes() const;

private:
    struct Entry {
        Entry() = default;
        Entry(NameTagNode* n, const irr::core::vector3df& cam)
            : node(n), distance(n->getAbsoluteTransformation().getTranslation().getDistanceFromSQ(cam)) {}
        // CSceneManager::TransparentNodeEntry's order: farthest first.
        bool operator<(const Entry& o) const { return distance > o.distance; }
        NameTagNode* node = nullptr;
        irr::f64 distance = 0.0;
    };

    void layout(NameTagNode& tag);
    void publishRow();
    void logWindow(int64_t micros);

    irr::gui::IGUIFont* font_ = nullptr;
    BitmapGlyphTable table_;
    irr::video::ITexture* texture_ = nullptr;   // the one texture every batchable glyph uses
    irr::core::aabbox3d<irr::f32> box_;
    std::vector<NameTagNode*> pending_;          // grabbed until render()
    irr::core::array<Entry> sorted_;
    // The compact run (B975's shrink): no UiQuad intermediate.
    // 80 B/quad: 4 x 20 B CompactVertex, plus 6 u16 indices in a grow-once
    // pattern {0,1,2, 0,2,3}, reused for each chunk; the colour bytes are
    // normalized at the attribute. Buffers at need, trimmed at a window
    // boundary (logWindow) when capacity > 4x the window's peak. Used only
    // when the driver is the GLES2 one the layout is written for.
    std::vector<CompactVertex> compactVertices_;
    std::vector<uint16_t> compactIndices_;
    // The per-quad fallback (any other driver): the old UiQuad + UiQuadBatch
    // path, untouched. Zero capacity on the GLES2 driver, which never fills it.
    std::vector<UiQuad> quads_;
    UiQuadBatch quadBatch_;
    FrameStats last_;
    size_t publishedBytes_ = 0;
    // 5 s [nametags] window
    int64_t windowStartMs_ = 0;
    size_t windowFrames_ = 0;
    size_t windowTags_ = 0, windowMaxTags_ = 0;
    std::vector<int64_t> windowMicros_;
};

/** False when WILLEQ_NAMETAG_BATCH=0 (the per-node A/B path). Read once. */
bool nameTagBatchEnabled();

} // namespace Graphics
} // namespace EQT
