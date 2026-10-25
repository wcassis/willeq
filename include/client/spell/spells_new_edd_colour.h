/*
 * WillEQ spellsnew.edd colour words (B1146, #466)
 *
 * The colour an official-client spell particle is drawn with, read from its
 * spellsnew.edd emitter (416-byte entry, docs/spellsnew_format.md; every
 * meaning below was read from EQGraphicsDX9.dll's code by B1141):
 *
 *   +0x68  int    1 = additive blend: SRCBLEND SRCALPHA, DESTBLEND ONE
 *                 (0x1005f7c2); else DESTBLEND INVSRCALPHA
 *   +0x90  float  alpha fade-in time, s: alpha *= age / D90 while age < D90
 *   +0x94  float  alpha fade-out time, s: alpha *= remaining / D94 while
 *                 remaining < D94
 *   +0xA4  float  peak opacity: alpha = min(fadeIn * fadeOut, DA4)
 *                 x SpellParticleOpacity (default 1.0) x actor alpha
 *                 (0x10063a42..0x10063a85)
 *   +0xD4..+0xDC  int  start R, G, B (0..255)
 *   +0xE0..+0xE8  int  end R, G, B: R = D4 + (E0 - D4) * age / lifetime,
 *                 vertex colour A<<24|R<<16|G<<8|B (0x100607e0,
 *                 0x10063ab7..0x10063b02)
 *
 * The particle draw (0x1005f400) sets texture stage 0 ALPHAOP MODULATE and
 * leaves COLOROP at the device's MODULATE (TEXTURE x DIFFUSE; the two
 * SELECTARG1 sites, 0x10074a80 and 0x10088e80, restore MODULATE after), so
 * the vertex colour multiplies the sprite texel in RGB and in alpha.
 *
 * B1145 (#465): the files are read through the shared spellsnew.edd reader
 * (spells_new_edd_reader.h, via SpellsNewEffReader); this header keeps only
 * the colour words' meaning and their decode.
 */

#ifndef EQT_SPELLS_NEW_EDD_COLOUR_H
#define EQT_SPELLS_NEW_EDD_COLOUR_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace EQ {

constexpr size_t kEddColourAdditiveOffset = 0x68;
constexpr size_t kEddColourFadeInOffset = 0x90;
constexpr size_t kEddColourFadeOutOffset = 0x94;
constexpr size_t kEddColourPeakOpacityOffset = 0xA4;
constexpr size_t kEddColourStartRgbOffset = 0xD4;
constexpr size_t kEddColourEndRgbOffset = 0xE0;

// The client's alpha factor for a particle `age` seconds old with `remaining`
// seconds left (EQGraphicsDX9 0x10063a42..0x10063a85), before the
// SpellParticleOpacity option and the actor's alpha multiply it: the fade is 1
// unless the particle is younger than fadeIn or has less than fadeOut left (a
// time <= 0 never fades), then clamped to peakOpacity. One formula for the
// decode and the particle path (UnifiedParticle::eddAlpha()).
inline float eddParticleAlpha(float age, float remaining, float fadeIn, float fadeOut,
                              float peakOpacity) {
    float a = 1.0f;
    if (fadeIn > 0.0f && age < fadeIn) a = age / fadeIn;
    if (fadeOut > 0.0f && remaining < fadeOut) a *= remaining / fadeOut;
    if (peakOpacity < a) a = peakOpacity;
    return a < 0.0f ? 0.0f : a;
}

// One spellsnew.edd emitter's colour words, as the client uses them.
struct SpellsNewEddColour {
    std::array<int32_t, 3> start{255, 255, 255};
    std::array<int32_t, 3> end{255, 255, 255};
    bool additive = true;      // +0x68 == 1
    float fadeIn = 0.0f;       // +0x90, seconds (<= 0: no fade-in)
    float fadeOut = 0.0f;      // +0x94, seconds (<= 0: no fade-out)
    float peakOpacity = 1.0f;  // +0xA4

    // Both triples 255,255,255: the vertex colour leaves the texel's RGB as
    // it is (on the client's alpha it still takes the fade x peak, see
    // SpellPresets::applyStageEmitterColours()).
    bool isWhite() const {
        for (size_t i = 0; i < 3; ++i) {
            if (start[i] != 255 || end[i] != 255) return false;
        }
        return true;
    }

};

// Decodes the colour words of ONE 416-byte spellsnew.edd entry.
SpellsNewEddColour decodeSpellsNewEddColour(const uint8_t* entry416);

// spellsnew.eff + spellsnew.edd read for their colour words only.
class SpellsNewEddColourTable {
public:
    bool load(const std::string& eqClientDir);
    bool loadFiles(const std::string& effPath, const std::string& eddPath);

    bool isLoaded() const { return m_loaded; }
    const std::string& loadError() const { return m_error; }
    size_t emitterCount() const { return m_colours.size(); }

    // nullptr for index <= 0 or out of range.
    const SpellsNewEddColour* emitter(int32_t index) const;

    // One entry per slot of stage `stage` (0 Source, 1 Middle, 2 Target) of
    // spellsnew.eff record `record` whose emitter resolves and names a
    // texture -- exactly the slots, in slot order, that
    // SpellsNewEffReader::stageTextures() returns, so entry k is the colour
    // of texture k. Empty when the record or stage is empty.
    std::vector<SpellsNewEddColour> stageColours(int32_t record, int stage) const;

private:
    std::vector<std::array<std::array<int32_t, 4>, 3>> m_records;  // emitter index per slot
    std::vector<SpellsNewEddColour> m_colours;
    std::vector<bool> m_hasTexture;
    bool m_loaded = false;
    std::string m_error;
};

}  // namespace EQ

#endif  // EQT_SPELLS_NEW_EDD_COLOUR_H
