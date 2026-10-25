/*
 * WillEQ spell spawn geometry (B1150, #470)
 *
 * The ten spawn shapes the official client decodes from spellsnew.edd +0xA8
 * (EQGraphicsDX9.dll). Every case was read from the per-particle shape switch
 * at 0x10061753 (decompile line 68775; jump table at 0x1006408c, cases
 * 0x10061769 .. 0x10061fa5+):
 *
 *   0 point            case 0x10061769: (0, 0, 0)
 *   1 ring, even       case 0x10061779: theta = 512*i/N (0x101c9954 = 512.0),
 *                      radius AC; B0 != 0 makes the ellipse (cos*AC, sin*B0)
 *   2 stacked rings    case 0x10061826: like 1, plus z = (B4/R)*k - 0.5*B4,
 *                      k = (i/N) % R, R/N from the count table
 *                      (decompile lines 68595-68642)
 *   3 cylinder surface case 0x10061905: random theta, z = (u - 0.5)*B4
 *   4 filled disc      case 0x100619d1: rejection x0,y0 uniform [-1,1) while
 *                      x0^2 + y0^2 > 1.0 (0x101c5948); (x0*AC, y0*B0)
 *   5 sphere surface   case 0x10061acb: z0 uniform [-1,1),
 *                      r0 = sqrt(1.0 - z0^2) (0x101c5948);
 *                      (r0*AC*cos, r0*B0*sin, z0*(B4 or AC)); outward normal
 *   6 box surface      case 0x10061be3: face = floor(u * 6.0) (0x101cc274),
 *                      half-extents (AC, B0 or AC, B4 or AC), uniform face
 *                      point; outward normal; the code's theta = atan2(x, y)
 *                      polar re-expression swaps the two in-plane axes, so the
 *                      final point is (Yf, Xf, Zf) of the face coords
 *   7 line/cone        case 0x10061d9c: ONE u1 (1000-table) drives both
 *                      r = u1*AC and z = u1*B4 (the line coupling); the
 *                      B0 != 0 path (0x10061de1) is the elliptical cone
 *   8 torus            case 0x10061e7d: major AC, minor B4; the B0 != 0 path
 *                      is the elliptic-major torus
 *   9 ring random      case 0x10061fa5: random theta, radius AC (or ellipse)
 *
 * Offsets, tilts and scale (read just before the switch, decompile lines
 * 68701-68774 and 69054-69055):
 *   +0xB8/+0xBC/+0xC0  origin += basis_i * (s19c * off_i), PRE-tilt basis
 *                      (0x100612b9..0x100613f7)
 *   +0xC4              rotate (basis1, basis2) by the EQ 512ths (0x100613fd..)
 *   +0xC8              rotate (basis1, basis3) by the EQ 512ths, using the
 *                      already-tilted basis1 (0x100615ab..)
 *   +0x19C == 1        s19c = the attach scale multiplies r, z and the
 *                      offsets (fStack_4d8, 0x100608b5/0x1006090d..);
 *                      == 0 -> s19c = 1.0 (attach matrix normalised). Fable
 *                      passes kSpellFxAttachScale = 1.0 (B1149's ruling), so
 *                      s19c is 1.0 either way -- the flag still selects it.
 *
 * The random draws are the client's uniform tables (verified in this unit):
 * the 512-table (0x102347a0, counter 0x10235f4c, FUN_1005d3e0) is a
 * permutation of the 512THS angle indices 0..511 (fill 0x1005d2a6, shuffle
 * 0x1005d2c1) feeding the 512-entry SIN/COS lookups at 0x10184500/0x10184520;
 * the 1000-table (0x10234fa0) a permutation of 0..999 scaled /1000 where
 * needed; the 2000-table (0x10232860, fill 0x1005d302: i*0.001 - 1.0, i.e. a
 * permutation of [-1, 1)) for the disc/sphere draws. All are uniform, so
 * fable draws from particleRandomFloat() -- stated once here, the tables are
 * not emulated (refactor/B1150/README.md, section 3).
 */

#ifndef EQT_SPELL_SPAWN_SHAPE_H
#define EQT_SPELL_SPAWN_SHAPE_H

#include <algorithm>
#include <cmath>

#include <glm/vec3.hpp>

namespace EQT::Graphics::Environment {

// The spawn-geometry words of one spellsnew.edd entry, as the sim consumes
// them (SpellsNewEddShape's words plus the attach scale s19c fable passes).
struct EddShapeParams {
    int shape = 0;             // 0..9 (+0xA8)
    float dimX = 0.0f;         // AC (+0xAC)
    float dimY = 0.0f;         // B0 (+0xB0; 0.0 = "same as AC")
    float dimZ = 0.0f;         // B4 (+0xB4)
    float offX = 0.0f;         // +0xB8
    float offY = 0.0f;         // +0xBC
    float offZ = 0.0f;         // +0xC0
    float tiltA = 0.0f;        // +0xC4, EQ 512ths
    float tiltB = 0.0f;        // +0xC8, EQ 512ths
    float scale = 1.0f;        // s19c: the attach scale (1.0 when +0x19C == 0)
};

// The particle's local spawn point in the emitter frame, one component per
// EDD axis: the x component along EDD axis 1 (the shape's height/"up" axis),
// y along axis 2, z along axis 3 (the client's basis array: pfVar21[3..5] =
// axis 1, [6..8] = axis 2, [9..11] = axis 3; the shape point is
// axis1*[0xe] + axis2*(cos([0xc])*[0xd]) + axis3*(sin([0xc])*[0xd]), decompile
// lines 69352-69370). The caller maps the three axes to world and adds the
// emitter position. For shapes 5/6, normal is the outward unit normal (the
// client's (r,z)-plane normal of +0x110, expanded to 3D; hasNormal false at
// the degenerate r = z = 0 point).
struct EddShapePoint {
    glm::vec3 point = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f);
    bool hasNormal = false;
};

// The shape 2 count table (decompile lines 68595-68642, .dis 0x10061002..
// 0x10061155): for a batch of m particles, R z-levels of N = m/R particles
// each (R = 8, N = 16 fixed when m < 9; the last tier is R = 100 for m >=
// 7200, R = 60 for 3201..7199).
inline void eddStackedRingCounts(int m, int& rings, int& perRing) {
    if (m < 9) {
        rings = 8;
        perRing = 16;
        return;
    }
    if (m < 0x13)      rings = 3;
    else if (m < 0x21) rings = 4;
    else if (m < 0x49) rings = 6;
    else if (m < 0x81) rings = 8;
    else if (m < 0xc9) rings = 10;
    else if (m < 0x121) rings = 12;
    else if (m < 0x201) rings = 16;
    else if (m < 0x481) rings = 24;
    else if (m < 0xc81) rings = 40;
    else if (m < 0x1c20) rings = 60;
    else                 rings = 100;
    perRing = m / rings;
}

// One shape case. i/n is the particle's index within its emission batch
// (shapes 1/2 are per-batch deterministic: evenly spaced / the count table;
// the client uses its global emitter counter, which differs only by whole
// turns and so not at all in the pattern). draw() returns one uniform [0,1)
// value; each case consumes its draws in the client's table order (the 2000-
// table pair is two consecutive draws; rejection redrews both).
template <typename Draw>
EddShapePoint computeEddShapePoint(const EddShapeParams& p, int i, int n, Draw draw) {
    constexpr float kTwoPi = 6.283185307179586f;
    const float AC = p.dimX;
    const float B0 = p.dimY != 0.0f ? p.dimY : p.dimX;  // +0xB0 == 0.0 -> AC
    const float s = p.scale;

    // Pre-tilt basis (the abstract orthonormal emitter frame).
    glm::vec3 b1(1.0f, 0.0f, 0.0f), b2(0.0f, 1.0f, 0.0f), b3(0.0f, 0.0f, 1.0f);

    // Offsets (0x100612b9..0x100613f7): along the PRE-tilt basis, times s19c.
    const glm::vec3 origin = b1 * (s * p.offX) + b2 * (s * p.offY) + b3 * (s * p.offZ);

    // Tilt A (0x100613fd..0x100615aa): b1' = c*b1 + s*b2, b2' = c*b2 - s*b1.
    if (p.tiltA != 0.0f) {
        const float a = p.tiltA / 512.0f * kTwoPi;
        const float c = std::cos(a), sn = std::sin(a);
        const glm::vec3 nb1 = b1 * c + b2 * sn;
        const glm::vec3 nb2 = b2 * c - b1 * sn;
        b1 = nb1;
        b2 = nb2;
    }
    // Tilt B (0x100615ab..0x10061752): the same on (b1, b3), using b1'.
    if (p.tiltB != 0.0f) {
        const float b = p.tiltB / 512.0f * kTwoPi;
        const float c = std::cos(b), sn = std::sin(b);
        const glm::vec3 nb1 = b1 * c + b3 * sn;
        const glm::vec3 nb3 = b3 * c - b1 * sn;
        b1 = nb1;
        b3 = nb3;
    }

    // (x, y, z) in the POST-tilt basis: x = r*cos(theta), y = r*sin(theta).
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool hasNormal = false;
    switch (p.shape) {
        case 0:  // point (0x10061769)
            break;
        case 1: {  // ring evenly spaced per batch (0x10061779)
            const int m = n > 1 ? n : 1;
            const float t = static_cast<float>(i % m) / static_cast<float>(m);
            x = AC * std::cos(kTwoPi * t);
            y = B0 * std::sin(kTwoPi * t);
            break;
        }
        case 2: {  // stacked rings (0x10061826)
            int rings = 0, perRing = 0;
            eddStackedRingCounts(n, rings, perRing);
            const int m = perRing > 0 ? perRing : 1;
            const float t = static_cast<float>(i % m) / static_cast<float>(m);
            x = AC * std::cos(kTwoPi * t);
            y = B0 * std::sin(kTwoPi * t);
            const int k = (i / m) % rings;
            z = (p.dimZ / static_cast<float>(rings)) * static_cast<float>(k) - 0.5f * p.dimZ;
            break;
        }
        case 3: {  // cylinder surface (0x10061905)
            const float th = kTwoPi * draw();
            x = AC * std::cos(th);
            y = B0 * std::sin(th);
            z = (draw() - 0.5f) * p.dimZ;
            break;
        }
        case 4: {  // filled disc, rejection (0x100619d1)
            float x0 = 0.0f, y0 = 0.0f;
            do {
                x0 = 2.0f * draw() - 1.0f;
                y0 = 2.0f * draw() - 1.0f;
            } while (x0 * x0 + y0 * y0 > 1.0f);  // 0x101c5948 = 1.0
            x = x0 * AC;
            y = y0 * B0;
            break;
        }
        case 5: {  // sphere / ellipsoid surface (0x10061acb)
            const float z0 = 2.0f * draw() - 1.0f;
            const float r0 = std::sqrt(std::max(0.0f, 1.0f - z0 * z0));  // 0x101c5948 = 1.0
            const float th = kTwoPi * draw();
            const float B4 = p.dimZ != 0.0f ? p.dimZ : AC;  // LAB_10061dd3
            x = r0 * AC * std::cos(th);
            y = r0 * B0 * std::sin(th);
            z = z0 * B4;
            hasNormal = true;
            break;
        }
        case 6: {  // box surface (0x10061be3)
            const float x0 = 2.0f * draw() - 1.0f;
            const float y0 = 2.0f * draw() - 1.0f;
            const int face = std::min(5, static_cast<int>(std::floor(draw() * 6.0f)));  // 0x101cc274 = 6.0
            const float ex = B0;  // the Y half-extent (+0xB0 or AC)
            const float ez = p.dimZ != 0.0f ? p.dimZ : AC;  // the Z half-extent
            switch (face) {
                case 0:  x = -AC; y = x0 * ex; z = y0 * ez; break;
                case 1:  x = AC;  y = x0 * ex; z = y0 * ez; break;
                case 2:  x = x0 * AC; y = -ex; z = y0 * ez; break;
                case 3:  x = x0 * AC; y = ex;  z = y0 * ez; break;
                case 4:  x = x0 * AC; y = y0 * ex; z = -ez; break;
                default: x = x0 * AC; y = y0 * ex; z = ez; break;
            }
            hasNormal = true;
            break;
        }
        case 7: {  // line / cone (0x10061d9c; B0 != 0 path 0x10061de1)
            const float u1 = draw();  // the ONE draw that couples r and z
            const float th = kTwoPi * draw();
            x = u1 * AC * std::cos(th);
            y = u1 * B0 * std::sin(th);
            z = u1 * p.dimZ;
            break;
        }
        case 8: {  // torus (0x10061e7d)
            const float phi = kTwoPi * draw();
            const float th = kTwoPi * draw();
            const float mx = AC * std::cos(th);
            const float my = B0 * std::sin(th);
            const float thp = std::atan2(my, mx);
            const float r = std::hypot(mx, my) + std::sin(phi) * p.dimZ;
            x = r * std::cos(thp);
            y = r * std::sin(thp);
            z = std::sin(phi) * p.dimZ;
            break;
        }
        case 9:  // ring random angle (0x10061fa5)
        default: {
            const float th = kTwoPi * draw();
            x = AC * std::cos(th);
            y = B0 * std::sin(th);
            break;
        }
    }

    // r, z and the offsets times s19c (decompile lines 69054-69055 and
    // 68701-68724). (x, y, z) above are the shape-local (r*cos(theta),
    // r*sin(theta), height); the client's point combines them as
    // axis1*height + axis2*(r*cos(theta)) + axis3*(r*sin(theta)), so the
    // abstract frame here is (xDD = E1, yDD = E2, zDD = E3). The normal is
    // the same combination unscaled (the normalised (r, z), unchanged by s>0).
    EddShapePoint out;
    out.point = origin + b1 * (s * z) + b2 * (s * x) + b3 * (s * y);
    if (hasNormal) {
        const float rl = std::hypot(x, y);
        if (rl != 0.0f || z != 0.0f) {
            out.normal = glm::normalize(b1 * z + b2 * x + b3 * y);
            out.hasNormal = true;
        }
    }
    return out;
}

}  // namespace EQT::Graphics::Environment

#endif  // EQT_SPELL_SPAWN_SHAPE_H
