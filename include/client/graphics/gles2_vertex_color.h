#ifndef EQT_GRAPHICS_GLES2_VERTEX_COLOR_H
#define EQT_GRAPHICS_GLES2_VERTEX_COLOR_H

// T484 (board B726): the byte order a GLES2 built-in shader receives a vertex
// colour in.
//
// COpenGLES2Driver (docker/irrlicht-drm/COpenGLES2Driver.cpp,
// drawVertexPrimitiveList() and the drawMeshBuffer() VBO path) hands the four
// bytes at S3DVertex offset 24 to GL as GL_UNSIGNED_BYTE RGBA, unconverted.
// irr::video::SColor is a u32 0xAARRGGBB, which a little-endian host stores as
// B,G,R,A, so `aColor` arrives with RED AND BLUE EXCHANGED. A grey colour is
// unaffected, which is why almost every mesh in the client never showed it.
//
// The sky dome's time-of-day tint is not grey: at 11:00 it is (140,175,220),
// and the dome drew (220,175,140), a tan sky (T484 Review). This helper writes
// an SColor whose stored bytes are R,G,B,A, so the shader sees the colour that
// was meant. Use it only for a vertex colour whose hue matters and that goes
// through the built-in programs.
//
// It deliberately does NOT fix the driver: the zone shaders read `aColor.rgb`
// as baked light through the same upload, and the lighting constants T405/T428
// solved live against the oracle were solved with that order in place (board
// B780). tests/test_sky_dome_tint_channel_order.cpp pins the driver convention;
// when it is changed that test fails and this helper must go.

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

inline irr::video::SColor toGles2VertexColor(irr::video::SColor c) {
    return irr::video::SColor(c.getAlpha(), c.getBlue(), c.getGreen(), c.getRed());
}

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_GLES2_VERTEX_COLOR_H
