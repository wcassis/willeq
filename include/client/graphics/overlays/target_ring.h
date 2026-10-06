/*
 * The 3D target ring -- Options > Display "Show 3D Target Ring" (B994, #314;
 * Will 2026-09-23 on #145: "3D target ring (Display) IN, wire it").
 *
 * THE ORACLE IS THE CLIENT'S OWN uifiles/default/TargetIndicator.ini, whose
 * [instructions] say: "The target indicator is 4 concentric circles textured
 * in such a way that a repeating texure is UV animated towards the center.
 * Each con color can have completely different settings. It is not a
 * particle effect, it is a specially generated procedural object ... and can
 * be toggled on/off in the options window. There is a command, /indicator
 * [on|off] which performs the same function." The checkbox's own tooltip
 * (EQUI_OptionsWindow.xml ODP_ShowTargetRingCheckbox) reads "A colored ring
 * appears around your current target's feet."
 *
 * QUOTED from that file, per con section (kStyles in target_ring.cpp):
 * Additive=1, PointCount=64, Red/Green/Blue/Alpha, OpaqueStart=1,
 * OpaqueEnd=1.5, FadeStart=10, FadeEnd=20, FloorOffset=0.5. The four circles
 * sit at OpaqueStart (alpha 0), OpaqueEnd (full), FadeStart (full) and
 * FadeEnd (0) -- the file's own definitions of the four distances.
 *
 * ASSUMPTIONS AWAITING ORACLE (flagged, none is in the file):
 *   R1. The four distances are in units where FadeStart (10) is the
 *       target's footprint radius (half its larger horizontal bounding-box
 *       extent): the ring is full-bright under the target and fades out to
 *       twice its footprint. The file gives no unit.
 *   R2. FloorOffset (0.5) is in world units, unscaled.
 *   R3. The texture (TargetIndicator.tga, a caustic pattern) is NOT drawn:
 *       the unit's rule is no new texture outside the atlas path when the
 *       ring can be procedural. The vertex colour is scaled by the texture's
 *       MEASURED mean luminance, 0.2469 (63.0/255), so the ring's average
 *       brightness is the textured one's; the moving caustic pattern is
 *       absent (a deviation).
 *   R4. Con tiers -> sections: Gray and Green -> Trivial, LightBlue ->
 *       VeryEasy, White -> FairMatch, Yellow -> Difficult, Red -> Deadly.
 *       fable's con table (con_tier.h) has no dark-blue tier, so Easy is
 *       never selected.
 *   R5. The default is OFF and the key is fable's own (`ShowTargetRing`):
 *       none of the six eqclient*.ini files under EverQuestP1999 carries a
 *       ring key, and the [instructions] describe turning it ON.
 *
 * Device-free: plain geometry the overlay hands to the driver
 * (target_ring_draw.cpp). Irrlicht types appear only as value structs.
 */

#pragma once

#include <irrlicht.h>
#include <cstdint>
#include <vector>

#include "client/con_tier.h"

namespace EQT {
namespace Graphics {
namespace TargetRing {

constexpr int kPointCount = 64;          // [TargetIndicator] PointCount
constexpr int kCircles = 4;
constexpr float kTextureMeanLuminance = 0.2469f;  // R3, measured
constexpr float kFootprintUnits = 10.0f;          // R1: FadeStart

/** The six con sections TargetIndicator.ini configures. */
enum class Section : uint8_t { Trivial = 0, VeryEasy, Easy, FairMatch, Difficult, Deadly, Count };

struct Style {
    uint8_t red, green, blue, alpha;
    float opaqueStart, opaqueEnd, fadeStart, fadeEnd;
    float floorOffset;
};

const Style& styleFor(Section s);
Section sectionForCon(eqt::ConTier tier);  // R4

struct Geometry {
    std::vector<irr::video::S3DVertex> vertices;  // kCircles * kPointCount
    std::vector<irr::u16> indices;                // 3 bands of quads
};

/** The world radius one ini unit is for a target of `footprintRadius` (R1). */
float unitScale(float footprintRadius);

/**
 * Build the ring around (x, z) on a floor at `floorY` (Irrlicht Y up),
 * for a target of `footprintRadius`. Reuses `out`'s storage.
 */
void build(const Style& style, float x, float floorY, float z, float footprintRadius,
           Geometry& out);

/** Does the ring draw this frame? The option, a target, its node shown. */
bool shouldDraw(bool optionOn, uint16_t targetId, bool targetFound, bool targetVisible);

/** The bytes a built Geometry holds (its vectors' capacity) -- the ledger figure. */
size_t geometryBytes(const Geometry& g);

}  // namespace TargetRing
}  // namespace Graphics
}  // namespace EQT
