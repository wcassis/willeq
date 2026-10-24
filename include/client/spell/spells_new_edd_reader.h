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
 *   +0xA0  float    full-density distance: counts *= D_A0 / distance beyond it
 *   +0xCC, +0x184   sprite width min, max: w = CC + (184 - CC) * rand (0x100612a0)
 *   +0x16C float    size multiplier: w,h *= s * D16C (0x1006325e)
 *   +0x17C, +0x180  sprite height min, max: h = 17C + (180 - 17C) * rand (0x10061268)
 *   +0x18C int      1 = width uses the height's random draw (0x10061279)
 *   +0x190 / +0x194 float s  height / width shrink-out (0x100632b1 / 0x100632d2)
 *   +0x19C int      1 = shape and offsets scale with the attach scale
 * A mode-0 sprite is a screen-facing quad w wide and h tall in WORLD units at
 * the particle (half-width w*s*D16C*0.5/depth times the camera's projection
 * scale, 0x1006335a..0x1006336f).
 * Shape, offsets and motion are B1150/B1151's.
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
constexpr size_t kEddScaleSizesOffset = 0x6C;
constexpr size_t kEddGrowInOffset = 0x98;
constexpr size_t kEddShrinkOutOffset = 0x9C;
constexpr size_t kEddLodDistanceOffset = 0xA0;
constexpr size_t kEddWidthMinOffset = 0xCC;
constexpr size_t kEddSizeMultiplierOffset = 0x16C;
constexpr size_t kEddHeightMinOffset = 0x17C;
constexpr size_t kEddHeightMaxOffset = 0x180;
constexpr size_t kEddWidthMaxOffset = 0x184;
constexpr size_t kEddSameRandomOffset = 0x18C;
constexpr size_t kEddHeightShrinkOffset = 0x190;
constexpr size_t kEddWidthShrinkOffset = 0x194;
constexpr size_t kEddScaleShapeOffset = 0x19C;

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
    bool scaleShape = false;      // +0x19C == 1
    float growIn = 0.0f;          // +0x98, s
    float shrinkOut = 0.0f;       // +0x9C, s
    float heightShrink = 0.0f;    // +0x190, s
    float widthShrink = 0.0f;     // +0x194, s
    float lodDistance = 0.0f;     // +0xA0

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

// B1149: the attach scale s fable passes for +0x6C. The client's s for an
// actor is the actor's scale word (+0x10) times its model's scale (vtbl+0x84),
// EQGraphicsDX9 0x1006090d..0x1006094a, and for a bone the bone's (0x10060991..);
// which eqgame code writes the actor's +0x10 was NOT traced, so fable has no
// per-actor value to pass: 1 for every actor (refactor/B1149/README.md).
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
    size_t m_count = 0;
    bool m_loaded = false;
    std::string m_error;
};

}  // namespace EQ

#endif  // EQT_SPELLS_NEW_EDD_READER_H
