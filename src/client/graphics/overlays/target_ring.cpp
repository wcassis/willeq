/*
 * The 3D target ring's geometry (B994) -- see target_ring.h for the oracle
 * file every number is quoted from and the flagged assumptions.
 */

#include "client/graphics/overlays/target_ring.h"

#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {
namespace TargetRing {

namespace {

// /mnt/projects/EverQuestP1999/uifiles/default/TargetIndicator.ini, the six
// con sections in file order: Red, Green, Blue, Alpha, OpaqueStart,
// OpaqueEnd, FadeStart, FadeEnd, FloorOffset. Every section uses the same
// four distances and floor offset; only the tint (and Deadly's Alpha=255)
// differ.
const Style kStyles[static_cast<int>(Section::Count)] = {
    {0, 255, 0, 240, 1.0f, 1.5f, 10.0f, 20.0f, 0.5f},    // [Trivial]
    {0, 128, 255, 240, 1.0f, 1.5f, 10.0f, 20.0f, 0.5f},  // [VeryEasy]
    {0, 0, 255, 240, 1.0f, 1.5f, 10.0f, 20.0f, 0.5f},    // [Easy]
    {255, 255, 255, 240, 1.0f, 1.5f, 10.0f, 20.0f, 0.5f},// [FairMatch]
    {255, 255, 0, 240, 1.0f, 1.5f, 10.0f, 20.0f, 0.5f},  // [Difficult]
    {255, 0, 0, 255, 1.0f, 1.5f, 10.0f, 20.0f, 0.5f},    // [Deadly]
};

}  // namespace

const Style& styleFor(Section s) {
    const int i = static_cast<int>(s);
    if (i < 0 || i >= static_cast<int>(Section::Count)) return kStyles[static_cast<int>(Section::FairMatch)];
    return kStyles[i];
}

Section sectionForCon(eqt::ConTier tier) {
    switch (tier) {
        case eqt::ConTier::Gray:
        case eqt::ConTier::Green:     return Section::Trivial;
        case eqt::ConTier::LightBlue: return Section::VeryEasy;
        case eqt::ConTier::White:     return Section::FairMatch;
        case eqt::ConTier::Yellow:    return Section::Difficult;
        case eqt::ConTier::Red:       return Section::Deadly;
    }
    return Section::FairMatch;
}

float unitScale(float footprintRadius) {
    // A target with no measurable footprint still gets a ring a human's size.
    const float r = footprintRadius > 0.1f ? footprintRadius : 1.5f;
    return r / kFootprintUnits;
}

void build(const Style& style, float x, float floorY, float z, float footprintRadius,
           Geometry& out) {
    const float k = unitScale(footprintRadius);
    const float radii[kCircles] = {style.opaqueStart * k, style.opaqueEnd * k,
                                   style.fadeStart * k, style.fadeEnd * k};
    const float edge[kCircles] = {0.0f, 1.0f, 1.0f, 0.0f};  // the file's fade definitions
    const float y = floorY + style.floorOffset;               // R2

    out.vertices.resize(kCircles * kPointCount);
    for (int c = 0; c < kCircles; ++c) {
        // Additive=1: blend GL_ONE, GL_ONE, so alpha is folded into RGB.
        const float a = (style.alpha / 255.0f) * edge[c] * kTextureMeanLuminance;
        // MEASURED (TargetRingPixels test, target_ring_capture): the GLES2
        // driver hands S3DVertex::Color to `aColor` as its raw little-endian
        // bytes, B,G,R,A, with no swizzle on the 3D path -- an SColor with
        // red in its red slot draws BLUE. So the red and blue slots are
        // swapped here, where the vertex is written, not in the shared driver.
        const irr::video::SColor col(255, static_cast<irr::u32>(style.blue * a + 0.5f),
                                     static_cast<irr::u32>(style.green * a + 0.5f),
                                     static_cast<irr::u32>(style.red * a + 0.5f));
        for (int i = 0; i < kPointCount; ++i) {
            const float t = 6.28318530718f * static_cast<float>(i) / kPointCount;
            irr::video::S3DVertex& v = out.vertices[c * kPointCount + i];
            v.Pos.set(x + radii[c] * std::cos(t), y, z + radii[c] * std::sin(t));
            v.Normal.set(0.0f, 1.0f, 0.0f);
            v.Color = col;
            v.TCoords.set(static_cast<float>(i) / kPointCount, static_cast<float>(c) / (kCircles - 1));
        }
    }
    out.indices.clear();
    for (int c = 0; c + 1 < kCircles; ++c) {
        for (int i = 0; i < kPointCount; ++i) {
            const irr::u16 a0 = static_cast<irr::u16>(c * kPointCount + i);
            const irr::u16 a1 = static_cast<irr::u16>(c * kPointCount + (i + 1) % kPointCount);
            const irr::u16 b0 = static_cast<irr::u16>((c + 1) * kPointCount + i);
            const irr::u16 b1 = static_cast<irr::u16>((c + 1) * kPointCount + (i + 1) % kPointCount);
            out.indices.insert(out.indices.end(), {a0, b0, a1, a1, b0, b1});
        }
    }
}

bool shouldDraw(bool optionOn, uint16_t targetId, bool targetFound, bool targetVisible) {
    return optionOn && targetId != 0 && targetFound && targetVisible;
}

size_t geometryBytes(const Geometry& g) {
    return g.vertices.capacity() * sizeof(irr::video::S3DVertex) +
           g.indices.capacity() * sizeof(irr::u16);
}

}  // namespace TargetRing
}  // namespace Graphics
}  // namespace EQT
