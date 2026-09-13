/*
 * T462 (board B506): see ui_quad_batch.h.
 */

#include "client/graphics/ui/ui_quad_batch.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

namespace EQT {
namespace Graphics {

namespace {
// docker/irrlicht-drm/COGLES2Shaders.h: attribute slots are fixed with
// glBindAttribLocation before every program link.
constexpr unsigned kAttrPosition = 0;
constexpr unsigned kAttrColor = 2;
constexpr unsigned kAttrTexCoord0 = 3;
// 16-bit indices: at most 65536 vertices, 4 per quad.
constexpr size_t kMaxQuadsPerDraw = 16384;
}  // namespace

bool UiQuadBatch::driverSupportsBatching(irr::video::IVideoDriver* driver) {
#ifdef EQT_HAS_GLES2
    return driver && driver->getDriverType() == irr::video::EDT_OGLES2;
#else
    (void)driver;
    return false;
#endif
}

size_t UiQuadBatch::drawTexturedRun(irr::video::IVideoDriver* driver, irr::video::ITexture* tex,
                                    const std::vector<UiQuad>& quads, bool batch) {
    if (!driver || !tex || quads.empty()) return 0;

    // The first quad through the driver: it sets every piece of state the
    // rest of the run needs.
    driver->draw2DImage(tex, quads[0].dest, quads[0].src, nullptr, &quads[0].color, true);
    size_t draws = 1;
    if (quads.size() == 1) return draws;

    if (!batch || !driverSupportsBatching(driver)) {
        for (size_t k = 1; k < quads.size(); ++k) {
            driver->draw2DImage(tex, quads[k].dest, quads[k].src, nullptr, &quads[k].color, true);
            ++draws;
        }
        return draws;
    }

#ifdef EQT_HAS_GLES2
    const irr::core::dimension2d<irr::u32>& texSize = tex->getOriginalSize();
    const irr::f32 invW = 1.0f / (irr::f32)texSize.Width;
    const irr::f32 invH = 1.0f / (irr::f32)texSize.Height;

    auto submit = [&]() {
        if (indices_.empty()) return;
        const float* v = vertices_.data();
        glVertexAttribPointer(kAttrPosition, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(irr::f32), v);
        glEnableVertexAttribArray(kAttrPosition);
        glVertexAttribPointer(kAttrColor, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(irr::f32), v + 3);
        glEnableVertexAttribArray(kAttrColor);
        glVertexAttribPointer(kAttrTexCoord0, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(irr::f32), v + 7);
        glEnableVertexAttribArray(kAttrTexCoord0);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_SHORT,
                       indices_.data());
        ++draws;
        vertices_.clear();
        indices_.clear();
    };

    vertices_.clear();
    indices_.clear();
    for (size_t k = 1; k < quads.size(); ++k) {
        if (indices_.size() / 6 == kMaxQuadsPerDraw) submit();
        const UiQuad& q = quads[k];

        // COpenGLES2Driver::draw2DImage()'s own vertex arithmetic.
        irr::f32 srcLeft = q.src.UpperLeftCorner.X * invW;
        irr::f32 srcTop = q.src.UpperLeftCorner.Y * invH;
        irr::f32 srcRight = q.src.LowerRightCorner.X * invW;
        irr::f32 srcBottom = q.src.LowerRightCorner.Y * invH;

        irr::f32 dstLeft = (irr::f32)q.dest.UpperLeftCorner.X;
        irr::f32 dstTop = (irr::f32)q.dest.UpperLeftCorner.Y;
        irr::f32 dstRight = (irr::f32)q.dest.LowerRightCorner.X;
        irr::f32 dstBottom = (irr::f32)q.dest.LowerRightCorner.Y;

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
        const uint16_t base = static_cast<uint16_t>(vertices_.size() / 9);
        vertices_.insert(vertices_.end(), quad, quad + 4 * 9);
        const uint16_t idx[6] = {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2),
                                 base, static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3)};
        indices_.insert(indices_.end(), idx, idx + 6);
    }
    submit();
#endif
    return draws;
}

size_t UiQuadBatch::drawSolidRun(irr::video::IVideoDriver* driver, const std::vector<UiQuad>& quads,
                                 bool batch) {
    if (!driver || quads.empty()) return 0;

    driver->draw2DRectangle(quads[0].color, quads[0].dest);
    size_t draws = 1;
    if (quads.size() == 1) return draws;

    if (!batch || !driverSupportsBatching(driver)) {
        for (size_t k = 1; k < quads.size(); ++k) {
            driver->draw2DRectangle(quads[k].color, quads[k].dest);
            ++draws;
        }
        return draws;
    }

#ifdef EQT_HAS_GLES2
    auto submit = [&]() {
        if (indices_.empty()) return;
        const float* v = vertices_.data();
        glVertexAttribPointer(kAttrPosition, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(irr::f32), v);
        glEnableVertexAttribArray(kAttrPosition);
        glVertexAttribPointer(kAttrColor, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(irr::f32), v + 3);
        glEnableVertexAttribArray(kAttrColor);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_SHORT,
                       indices_.data());
        ++draws;
        vertices_.clear();
        indices_.clear();
    };

    vertices_.clear();
    indices_.clear();
    for (size_t k = 1; k < quads.size(); ++k) {
        if (indices_.size() / 6 == kMaxQuadsPerDraw) submit();
        const UiQuad& q = quads[k];

        // COpenGLES2Driver::draw2DRectangle()'s own vertex arithmetic, one
        // colour for all four corners (the draw2DRectangle(SColor, rect)
        // overload the fallback path calls).
        irr::f32 left = (irr::f32)q.dest.UpperLeftCorner.X;
        irr::f32 right = (irr::f32)q.dest.LowerRightCorner.X;
        irr::f32 top = (irr::f32)q.dest.UpperLeftCorner.Y;
        irr::f32 bottom = (irr::f32)q.dest.LowerRightCorner.Y;
        irr::f32 r = q.color.getRed() / 255.0f;
        irr::f32 g = q.color.getGreen() / 255.0f;
        irr::f32 b = q.color.getBlue() / 255.0f;
        irr::f32 a = q.color.getAlpha() / 255.0f;

        const irr::f32 quad[4 * 7] = {
            left,  top,    0.0f, r, g, b, a,
            right, top,    0.0f, r, g, b, a,
            right, bottom, 0.0f, r, g, b, a,
            left,  bottom, 0.0f, r, g, b, a
        };
        const uint16_t base = static_cast<uint16_t>(vertices_.size() / 7);
        vertices_.insert(vertices_.end(), quad, quad + 4 * 7);
        const uint16_t idx[6] = {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2),
                                 base, static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3)};
        indices_.insert(indices_.end(), idx, idx + 6);
    }
    submit();
#endif
    return draws;
}

} // namespace Graphics
} // namespace EQT
