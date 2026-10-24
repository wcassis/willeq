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
 * Everything else in the entry (shape, motion, sizes) is B1147's.
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

    // +0x00 name; +0x40 texture as a lower-case basename with the one shipped
    // entry's trailing '.' padding trimmed ("gena20.bmp......." -> gena20.bmp);
    // empty = the entry names no texture. +0x130 frame count.
    std::string name(int32_t index) const;
    std::string texture(int32_t index) const;
    int32_t frames(int32_t index) const;

private:
    std::vector<uint8_t> m_bytes;
    std::vector<SpellsNewEddTiming> m_timing;
    size_t m_count = 0;
    bool m_loaded = false;
    std::string m_error;
};

}  // namespace EQ

#endif  // EQT_SPELLS_NEW_EDD_READER_H
