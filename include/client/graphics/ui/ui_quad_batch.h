/*
 * T462 (board B506): 2D quad runs as one GLES2 draw call, pixel-identical to
 * the driver's own one-draw-per-quad calls.
 *
 * The static UI issued one COpenGLES2Driver::draw2DImage() per atlas quad, one
 * draw2DRectangle() per fallback rectangle and one CGUIFont::draw() (itself a
 * draw2DImageBatch with its own allocations) per text entry. On the Orange Pi
 * that was the Static UI section: 18 ms closed, 26 ms with the inventory open
 * at 1280x720 (T462's account). A RUN is consecutive quads that share every
 * piece of GL state -- the same texture (UI2D program) or, for solid
 * rectangles, the same blend enable (Color2D program).
 *
 * How a run stays pixel-identical: its FIRST quad is drawn by the driver's own
 * call, which establishes the program, uniforms, texture units, blend, scissor
 * and attribute enables exactly as each per-quad call would re-establish them.
 * The rest of the run is then drawn in one glDrawElements over vertices built
 * with the driver's own arithmetic and layout (docker/irrlicht-drm/
 * COpenGLES2Driver.cpp: draw2DImage() is x,y,z,r,g,b,a,u,v; draw2DRectangle()
 * is x,y,z,r,g,b,a), triangles in the same order, so blending sees the same
 * sequence. tests/test_static_ui_frame_cost.cpp compares the batched and the
 * per-quad flush pixel for pixel on the GLES2 device.
 *
 * Only the GLES2 driver (EDT_OGLES2, an EQT_HAS_GLES2 build) is batched; any
 * other driver gets the per-quad calls.
 */

#pragma once

#include <irrlicht.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace EQT {
namespace Graphics {

struct UiQuad {
    irr::core::rect<irr::s32> dest;
    irr::core::rect<irr::s32> src;   // texture pixels; unused by solid runs
    irr::video::SColor color;
};

class UiQuadBatch {
public:
    /** True when `driver` is the GLES2 driver the raw run draw is written for. */
    static bool driverSupportsBatching(irr::video::IVideoDriver* driver);

    /**
     * Draw `quads` sampling `tex`, alpha-blended, as draw2DImage() would one at
     * a time. `batch` false (or an unsupported driver) issues exactly those
     * per-quad calls. Returns the driver draw calls issued.
     */
    size_t drawTexturedRun(irr::video::IVideoDriver* driver, irr::video::ITexture* tex,
                           const std::vector<UiQuad>& quads, bool batch);

    /**
     * Draw solid `quads`, as draw2DRectangle(color, dest) would one at a time.
     * Every quad must have the same blend enable (alpha < 255 or not) -- that
     * is what makes them one run. Returns the driver draw calls issued.
     */
    size_t drawSolidRun(irr::video::IVideoDriver* driver, const std::vector<UiQuad>& quads,
                        bool batch);

    /** B975: bytes the reusable vertex/index buffers hold (capacity). */
    size_t bytesReserved() const {
        return vertices_.capacity() * sizeof(float) + indices_.capacity() * sizeof(uint16_t);
    }

    /** B975: release capacity beyond `quads` textured quads. */
    void trim(size_t quads) {
        if (vertices_.capacity() > quads * 4 * 9) {
            std::vector<float>().swap(vertices_);
            vertices_.reserve(quads * 4 * 9);
        }
        if (indices_.capacity() > quads * 6) {
            std::vector<uint16_t>().swap(indices_);
            indices_.reserve(quads * 6);
        }
    }

private:
    std::vector<float> vertices_;      // reused across frames
    std::vector<uint16_t> indices_;
};

} // namespace Graphics
} // namespace EQT
