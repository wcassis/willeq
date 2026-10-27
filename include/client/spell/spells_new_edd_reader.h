/*
 * WillEQ spellsnew.edd reader (B1145, #465)
 *
 * The one reader of spellsnew.edd's 416-byte emitter entries ("EDD\0"
 * "110\0", then entries; EQGraphicsDX9.dll 0x100644e0 loads it). Every
 * meaning below was read from the official client's code by B1141 (#461):
 * docs/spellsnew_format.md and review/B1141/edd_fields_from_code.tsv cite
 * the read site of each word. SpellsNewEffReader (the record/slot words)
 * and SpellsNewEddColourTable (B1146's colour words) both read the EDD
 * through this class; no other code touches the entry bytes.
 *
 * Decoded per entry here (the timing and emission words, B1145):
 *   +0x40  char[32] texture file           (B1127)
 *   +0x78  float s  emitter duration: < 0 a FIXED run of |value|; >= 0 the
 *                   caller's duration wins when it gives one (> 0 ms), else
 *                   this value (CreateEmitter 0x1005a4f9..0x1005a518)
 *   +0x7C  float s  particle lifetime L; < 0 = the particle lives for the
 *                   emitter's run (0x1005a55e..)
 *   +0x80  int      burst: particles on the emitter's first frame
 *   +0x84  int      particles per emission tick
 *   +0x88  float /s emission ticks per second, from +0x8C until the run ends
 *   +0x8C  float s  emission start delay: no tick before it; the emission
 *                   window is run - delay (0x10060e91)
 *   +0x130 int      texture frame count (B1127)
 * The colour words (+0x68, +0x90, +0x94, +0xA4, +0xD4..+0xE8) are decoded by
 * decodeSpellsNewEddColour() (spells_new_edd_colour.h) from entryBytes().
 *
 * B1149 (#469): the sprite-size words (SpellsNewEddSprite), all read by
 * CParticleSystem::Render 0x100605b0:
 *   +0x6C  int      1 = sizes (and velocities) scale with the attach scale s
 *                   (0x100608b5: actor +0x10 x the model's vtbl+0x84 scale)
 *   +0x98  float s  size grow-in: w,h *= age / D98 while age < D98 (0x1006326c)
 *   +0x9C  float s  size shrink-out: w,h *= remaining / D9C (0x1006328f)
 *   (+0xA0, the full-density distance, is not decoded: 80 on most entries, and the
 *   harness/lane camera sits 29 units away, where the count factor is 1)
 *   +0xCC, +0x184   sprite width min, max: w = CC + (184 - CC) * rand (0x100612a0)
 *   +0x16C float    size multiplier: w,h *= s * D16C (0x1006325e)
 *   +0x17C, +0x180  sprite height min, max: h = 17C + (180 - 17C) * rand (0x10061268)
 *   +0x18C int      1 = width uses the height's random draw (0x10061279)
 *   +0x190 / +0x194 float s  height / width shrink-out (0x100632b1 / 0x100632d2)
 *
 * B1150 (#470): the spawn-geometry words (SpellsNewEddShape). The shape switch
 * itself is CParticleSystem's per-particle step at 0x10061157 (decompile line
 * 68775), one case per shape; the words are read there and just before it:
 *   +0xA8  int      the shape (0..9): 0 point; 1 ring evenly spaced per batch;
 *                   2 stacked rings over +0xB4 (count table 0x10061826);
 *                   3 cylinder surface; 4 filled disc (rejection, 0x101c5948=1.0);
 *                   5 sphere/ellipsoid surface (z uniform, r=sqrt(1.0-z^2)*AC);
 *                   6 box surface (face = u*6.0, 0x101cc274); 7 line/cone (one
 *                   u: r=u*AC, z=u*B4); 8 torus (major AC, minor B4);
 *                   9 ring random angle
 *   +0xAC  float    dim X: the circle radius AC / box half-extent / torus major
 *   +0xB0  float    dim Y: 0.0 (0x101cbd40) = "same as AC" (circle, not ellipse);
 *                   else the ellipse's Y semi-axis
 *   +0xB4  float    dim Z: cylinder/stacked/cone height, torus minor, sphere/box
 *                   Z half-extent (0 = AC there)
 *   +0xB8/+0xBC/+0xC0 float  spawn OFFSETS along the emitter basis axes 1/2/3,
 *                   origin += basis_i * (s19c * off_i) (0x100612b9..0x100613f7)
 *   +0xC4  float    tilt in EQ 512ths rotating basis (1,2) (0x100613fd..0x100615aa)
 *   +0xC8  float    tilt in EQ 512ths rotating basis (1,3) (0x100615ab..0x10061752)
 *   +0xD0  float    view depth bias: the renderer subtracts it from the view
 *                   depth before cull/divide (B1150, unified_particle_renderer)
 *   +0x19C int      1 = the attach scale s multiplies r, z and the offsets
 *                   (fStack_4d8, 0x1006090d..); 0 = attach matrix normalised
 *                   (s = 1.0); fable passes the entity's s (B1164,
 *                   spell_fx_attach_scale.h), 1.0 for the Middle stage
 * Shapes 5/6 also store the outward normal in the (r,z) plane used by +0x110
 * (0x1006205b..0x10062097). The random draws are the 512/1000/2000-entry
 * uniform tables (0x1005d3e0, 0x1005d400, DAT_10232860) -- uniform, so fable
 * uses particleRandomFloat (refactor/B1150/README.md, section 3).
 *
 * A mode-0 sprite is a screen-facing quad w wide and h tall in WORLD units at
 * the particle (half-width w*s*D16C*0.5/depth times the camera's projection
 * scale, 0x1006335a..0x1006336f).
 */

#ifndef EQT_SPELLS_NEW_EDD_READER_H
#define EQT_SPELLS_NEW_EDD_READER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace EQ {

constexpr size_t kSpellsNewEddMagicBytes = 8;   // "EDD\0" "110\0"
constexpr size_t kSpellsNewEddEntryBytes = 416;
constexpr size_t kEddTextureOffset = 0x40;
constexpr size_t kEddTextureBytes = 32;
constexpr size_t kEddDurationOffset = 0x78;
constexpr size_t kEddLifetimeOffset = 0x7C;
constexpr size_t kEddBurstOffset = 0x80;
constexpr size_t kEddPerTickOffset = 0x84;
constexpr size_t kEddTicksPerSecondOffset = 0x88;
constexpr size_t kEddDelayOffset = 0x8C;
constexpr size_t kEddFrameCountOffset = 0x130;
constexpr size_t kEddFrameRateOffset = 0x134;
constexpr size_t kEddScaleSizesOffset = 0x6C;
constexpr size_t kEddGrowInOffset = 0x98;
constexpr size_t kEddShrinkOutOffset = 0x9C;
constexpr size_t kEddWidthMinOffset = 0xCC;
constexpr size_t kEddSizeMultiplierOffset = 0x16C;
constexpr size_t kEddHeightMinOffset = 0x17C;
constexpr size_t kEddHeightMaxOffset = 0x180;
constexpr size_t kEddWidthMaxOffset = 0x184;
constexpr size_t kEddSameRandomOffset = 0x18C;
constexpr size_t kEddHeightShrinkOffset = 0x190;
constexpr size_t kEddWidthShrinkOffset = 0x194;
// B1150 (#470): the spawn-geometry words.
constexpr size_t kEddShapeOffset = 0xA8;
constexpr size_t kEddDimXOffset = 0xAC;
constexpr size_t kEddDimYOffset = 0xB0;
constexpr size_t kEddDimZOffset = 0xB4;
constexpr size_t kEddOffXOffset = 0xB8;
constexpr size_t kEddOffYOffset = 0xBC;
constexpr size_t kEddOffZOffset = 0xC0;
constexpr size_t kEddTiltAOffset = 0xC4;
constexpr size_t kEddTiltBOffset = 0xC8;
constexpr size_t kEddDepthOffset = 0xD0;
constexpr size_t kEddScaleShapesOffset = 0x19C;

// Little-endian words of an entry (the files are x86 dumps).
inline int32_t eddInt(const uint8_t* entry, size_t offset) {
    const uint8_t* p = entry + offset;
    const uint32_t v = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
                       (uint32_t(p[3]) << 24);
    int32_t s;
    std::memcpy(&s, &v, sizeof s);
    return s;
}

inline float eddFloat(const uint8_t* entry, size_t offset) {
    const int32_t i = eddInt(entry, offset);
    float f;
    std::memcpy(&f, &i, sizeof f);
    return f;
}

// The timing and emission words of one emitter, raw as the file holds them.
struct SpellsNewEddTiming {
    float duration = 0.0f;        // +0x78
    float lifetime = 0.0f;        // +0x7C
    int32_t burst = 0;            // +0x80
    int32_t perTick = 0;          // +0x84
    float ticksPerSecond = 0.0f;  // +0x88
    float delay = 0.0f;           // +0x8C

    // +0x78 < 0: the run is |duration| whatever the caller passes.
    bool fixedDuration() const { return duration < 0.0f; }
};

SpellsNewEddTiming decodeSpellsNewEddTiming(const uint8_t* entry416);

// B1149 (#469): the sprite-size words of one emitter, raw as the file holds
// them (see the header comment for each word's read site).
struct SpellsNewEddSprite {
    float widthMin = 0.0f;        // +0xCC
    float widthMax = 0.0f;        // +0x184
    float heightMin = 0.0f;       // +0x17C
    float heightMax = 0.0f;       // +0x180
    float multiplier = 1.0f;      // +0x16C
    bool sameRandom = false;      // +0x18C == 1
    bool scaleSizes = false;      // +0x6C == 1
    float growIn = 0.0f;          // +0x98, s
    float shrinkOut = 0.0f;       // +0x9C, s
    float heightShrink = 0.0f;    // +0x190, s
    float widthShrink = 0.0f;     // +0x194, s

    // The width and height of one sprite for the random draws u (height)
    // and v (width; u again when sameRandom), times the multiplier and the
    // attach scale `s` when +0x6C is set (0x10061262..0x100612a8, 0x1006325e).
    float width(float u, float v, float s) const {
        const float r = sameRandom ? u : v;
        return (widthMin + (widthMax - widthMin) * r) * multiplier * (scaleSizes ? s : 1.0f);
    }
    float height(float u, float s) const {
        return (heightMin + (heightMax - heightMin) * u) * multiplier * (scaleSizes ? s : 1.0f);
    }
};

SpellsNewEddSprite decodeSpellsNewEddSprite(const uint8_t* entry416);

// B1150 (#470): the spawn-geometry words of one emitter, raw as the file holds
// them (see the header comment for each word's read site; the shape cases are
// the switch at 0x10061157, decompile lines 68775-69043).
struct SpellsNewEddShape {
    int32_t shape = 0;           // +0xA8: 0..9 (0 point .. 9 ring random angle)
    float dimX = 0.0f;           // +0xAC: radius AC / box half-extent / torus major
    float dimY = 0.0f;           // +0xB0: 0.0 = "same as +0xAC" (0x101cbd40 compare)
    float dimZ = 0.0f;           // +0xB4: height/depth minor; 0 = AC where noted
    float offX = 0.0f;           // +0xB8: spawn offset along EDD axis 1 (the shape's height/"up" axis)
    float offY = 0.0f;           // +0xBC: spawn offset along EDD axis 2 (radial plane)
    float offZ = 0.0f;           // +0xC0: spawn offset along EDD axis 3 (radial plane)
    float tiltA = 0.0f;          // +0xC4: EQ 512ths, rotates EDD axes 1/2 (0x100613fd)
    float tiltB = 0.0f;          // +0xC8: EQ 512ths, rotates EDD axes 1/3 (0x100615ab)
    float depth = 0.0f;          // +0xD0: view depth bias (rendered, not spawned)
    bool scaleShapes = false;    // +0x19C == 1: r, z and offsets times the attach scale
};

SpellsNewEddShape decodeSpellsNewEddShape(const uint8_t* entry416);

// B1151 (#471): the motion / spin / frame / flag words. TSV rows in
// review/B1141/edd_fields_from_code.tsv (all PROVEN-BY-CODE except +0x178,
// PARTIAL on which attach-matrix row is which: FUN_100399f0). The per-frame
// integration and its decompile addresses are in refactor/B1151/README.md.
constexpr size_t kEddOrientOffset = 0x60;
constexpr size_t kEddNoDepthWriteOffset = 0x64;
constexpr size_t kEddSpriteModeOffset = 0x70;
constexpr size_t kEddFollowOffset = 0x74;
constexpr size_t kEddUpMinOffset = 0xEC;
constexpr size_t kEddUpMaxOffset = 0xF0;
constexpr size_t kEddUpAccelOffset = 0xF4;
constexpr size_t kEddAxis2MinOffset = 0xF8;
constexpr size_t kEddAxis2MaxOffset = 0xFC;
constexpr size_t kEddAxis2AccelOffset = 0x100;
constexpr size_t kEddAxis3MinOffset = 0x104;
constexpr size_t kEddAxis3MaxOffset = 0x108;
constexpr size_t kEddAxis3AccelOffset = 0x10C;
constexpr size_t kEddRadialMinOffset = 0x110;
constexpr size_t kEddRadialMaxOffset = 0x114;
constexpr size_t kEddRadialAccelOffset = 0x118;
constexpr size_t kEddSwirlMinOffset = 0x11C;
constexpr size_t kEddSwirlMaxOffset = 0x120;
constexpr size_t kEddSwirlAccelOffset = 0x124;
constexpr size_t kEddGravityOffset = 0x128;
constexpr size_t kEddDriftXOffset = 0x12C;
constexpr size_t kEddRandomStartSpinOffset = 0x174;
constexpr size_t kEddSpinMinOffset = 0x138;
constexpr size_t kEddSpinMaxOffset = 0x188;
constexpr size_t kEddSpinAlignOffset = 0x178;
constexpr size_t kEddDrawNegativeRadiusOffset = 0x198;

// The record's motion words (one per entry), raw as the file holds them.
struct SpellsNewEddMotion {
    bool orientToAttach = false;     // +0x60: orient the basis to the attached object each frame
    bool noDepthWrite = false;       // +0x64: depth write OFF for the emitter's draw
    int32_t spriteMode = 0;          // +0x70: 0 billboard, 1 streak, 2 world-oriented quad
    bool follow = false;             // +0x74: particles follow the emitter
    float upMin = 0.0f, upMax = 0.0f, upAccel = 0.0f;          // +0xEC/+0xF0/+0xF4
    float axis2Min = 0.0f, axis2Max = 0.0f, axis2Accel = 0.0f; // +0xF8/+0xFC/+0x100
    float axis3Min = 0.0f, axis3Max = 0.0f, axis3Accel = 0.0f; // +0x104/+0x108/+0x10C
    float radialMin = 0.0f, radialMax = 0.0f, radialAccel = 0.0f;  // +0x110/+0x114/+0x118
    float swirlMin = 0.0f, swirlMax = 0.0f, swirlAccel = 0.0f;    // +0x11C/+0x120/+0x124, 512ths/s
    float gravity = 0.0f;            // +0x128, u/s^2 (EQ world z up; the z OFFSET -= g*s*dt)
    float driftX = 0.0f;             // +0x12C, u/s (world +X only)
    bool randomStartSpin = false;    // +0x174: start angle = rand()/32767*1000 (512ths), else 0
    float spinMin = 0.0f, spinMax = 0.0f;  // +0x138/+0x188, 512ths/s
    int32_t spinAlign = 0;           // +0x178
    int32_t frameCount = 1;          // +0x130
    float frameRate = 0.0f;          // +0x134
    bool drawNegativeRadius = false; // +0x198 == 1: r < 0 still drawn
};

SpellsNewEddMotion decodeSpellsNewEddMotion(const uint8_t* entry416);

// B1149: the attach scale s for +0x6C / +0x19C. B1164 (#483) pinned the
// client's s for a Source / Target slot on an actor: its actor scale, 0.2 x
// its server size (EQ::SpellFxAttachScale, spell_fx_attach_scale.h; lane-5
// read 1.200000 on a size-6 human). This constant is what an entity nothing
// resolves, a race that rule does not cover, and the Middle stage's missile
// actor (not traced) still get.
constexpr float kSpellFxAttachScale = 1.0f;

class SpellsNewEddReader {
public:
    // Parses <path> whole: the "EDD\0" magic and a whole number of entries,
    // or the load fails (loadError()).
    bool loadFile(const std::string& path);
    bool loadBytes(std::vector<uint8_t> bytes, const std::string& label);

    bool isLoaded() const { return m_loaded; }
    const std::string& loadError() const { return m_error; }
    size_t emitterCount() const { return m_count; }

    // nullptr for index <= 0 (spellsnew.eff's "empty slot") or out of range.
    const uint8_t* entryBytes(int32_t index) const;
    const SpellsNewEddTiming* timing(int32_t index) const;
    // B1149: the sprite-size words; nullptr like timing().
    const SpellsNewEddSprite* sprite(int32_t index) const;
    // B1150: the spawn-geometry words; nullptr like timing().
    const SpellsNewEddShape* shape(int32_t index) const;
    // B1151: the motion / spin / frame / flag words; nullptr like timing().
    const SpellsNewEddMotion* motion(int32_t index) const;
    // B1150 census: the decoded words for the zero-BASED file entry
    // (0..emitterCount()-1), unlike the 1-based shape()/timing()/sprite() slot
    // accessors. spellsnew.eff's slot numbers are 1-based and its "empty slot"
    // is 0, so the 1-based accessors reject 0 -- but the census counts every
    // file entry, entry 0 included (it is a real shape-0 point, the 297th).
    const SpellsNewEddShape& shapeAt(size_t zeroBasedEntry) const;
    const SpellsNewEddMotion& motionAt(size_t zeroBasedEntry) const;

    // +0x00 name; +0x40 texture as a lower-case basename with the one shipped
    // entry's trailing '.' padding trimmed ("gena20.bmp......." -> gena20.bmp);
    // empty = the entry names no texture. +0x130 frame count.
    std::string name(int32_t index) const;
    std::string texture(int32_t index) const;
    int32_t frames(int32_t index) const;

private:
    std::vector<uint8_t> m_bytes;
    std::vector<SpellsNewEddTiming> m_timing;
    std::vector<SpellsNewEddSprite> m_sprite;
    std::vector<SpellsNewEddShape> m_shape;
    std::vector<SpellsNewEddMotion> m_motion;
    size_t m_count = 0;
    bool m_loaded = false;
    std::string m_error;
};

}  // namespace EQ

#endif  // EQT_SPELLS_NEW_EDD_READER_H
