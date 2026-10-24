/*
 * WillEQ spellsnew.edd reader (B1145) -- implementation.
 * See include/client/spell/spells_new_edd_reader.h for the words and the
 * client code each one was read from.
 */

#include "client/spell/spells_new_edd_reader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace EQ {

SpellsNewEddTiming decodeSpellsNewEddTiming(const uint8_t* e) {
    SpellsNewEddTiming t;
    t.duration = eddFloat(e, kEddDurationOffset);
    t.lifetime = eddFloat(e, kEddLifetimeOffset);
    t.burst = eddInt(e, kEddBurstOffset);
    t.perTick = eddInt(e, kEddPerTickOffset);
    t.ticksPerSecond = eddFloat(e, kEddTicksPerSecondOffset);
    t.delay = eddFloat(e, kEddDelayOffset);
    return t;
}

SpellsNewEddSprite decodeSpellsNewEddSprite(const uint8_t* e) {
    SpellsNewEddSprite s;
    s.widthMin = eddFloat(e, kEddWidthMinOffset);
    s.widthMax = eddFloat(e, kEddWidthMaxOffset);
    s.heightMin = eddFloat(e, kEddHeightMinOffset);
    s.heightMax = eddFloat(e, kEddHeightMaxOffset);
    s.multiplier = eddFloat(e, kEddSizeMultiplierOffset);
    s.sameRandom = eddInt(e, kEddSameRandomOffset) == 1;
    s.scaleSizes = eddInt(e, kEddScaleSizesOffset) == 1;
    s.scaleShape = eddInt(e, kEddScaleShapeOffset) == 1;
    s.growIn = eddFloat(e, kEddGrowInOffset);
    s.shrinkOut = eddFloat(e, kEddShrinkOutOffset);
    s.heightShrink = eddFloat(e, kEddHeightShrinkOffset);
    s.widthShrink = eddFloat(e, kEddWidthShrinkOffset);
    s.lodDistance = eddFloat(e, kEddLodDistanceOffset);
    return s;
}

bool SpellsNewEddReader::loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        m_loaded = false;
        m_error = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return loadBytes(std::move(bytes), path);
}

bool SpellsNewEddReader::loadBytes(std::vector<uint8_t> bytes, const std::string& label) {
    m_loaded = false;
    m_bytes.clear();
    m_timing.clear();
    m_sprite.clear();
    m_count = 0;
    if (bytes.size() < kSpellsNewEddMagicBytes ||
        (bytes.size() - kSpellsNewEddMagicBytes) % kSpellsNewEddEntryBytes != 0 ||
        std::memcmp(bytes.data(), "EDD\0", 4) != 0) {
        m_error = label + " is not an EDD file of 416-byte entries";
        return false;
    }
    m_bytes = std::move(bytes);
    m_count = (m_bytes.size() - kSpellsNewEddMagicBytes) / kSpellsNewEddEntryBytes;
    m_timing.resize(m_count);
    m_sprite.resize(m_count);
    for (size_t i = 0; i < m_count; ++i) {
        const uint8_t* e = m_bytes.data() + kSpellsNewEddMagicBytes + i * kSpellsNewEddEntryBytes;
        m_timing[i] = decodeSpellsNewEddTiming(e);
        m_sprite[i] = decodeSpellsNewEddSprite(e);
    }
    m_error.clear();
    m_loaded = true;
    return true;
}

const uint8_t* SpellsNewEddReader::entryBytes(int32_t index) const {
    if (index <= 0 || static_cast<size_t>(index) >= m_count) return nullptr;
    return m_bytes.data() + kSpellsNewEddMagicBytes + static_cast<size_t>(index) * kSpellsNewEddEntryBytes;
}

const SpellsNewEddTiming* SpellsNewEddReader::timing(int32_t index) const {
    if (index <= 0 || static_cast<size_t>(index) >= m_count) return nullptr;
    return &m_timing[static_cast<size_t>(index)];
}

const SpellsNewEddSprite* SpellsNewEddReader::sprite(int32_t index) const {
    if (index <= 0 || static_cast<size_t>(index) >= m_count) return nullptr;
    return &m_sprite[static_cast<size_t>(index)];
}

std::string SpellsNewEddReader::name(int32_t index) const {
    const uint8_t* e = entryBytes(index);
    if (!e) return {};
    size_t len = 0;
    while (len < kEddTextureOffset && e[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(e), len);
}

std::string SpellsNewEddReader::texture(int32_t index) const {
    const uint8_t* e = entryBytes(index);
    if (!e) return {};
    const uint8_t* t = e + kEddTextureOffset;
    size_t len = 0;
    while (len < kEddTextureBytes && t[len] != 0) ++len;
    std::string tex(reinterpret_cast<const char*>(t), len);
    // One shipped entry spells its file "gena20.bmp......." -- trailing dots
    // are padding, not part of a Windows file name.
    while (!tex.empty() && tex.back() == '.') tex.pop_back();
    std::transform(tex.begin(), tex.end(), tex.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tex;
}

int32_t SpellsNewEddReader::frames(int32_t index) const {
    const uint8_t* e = entryBytes(index);
    return e ? eddInt(e, kEddFrameCountOffset) : 0;
}

}  // namespace EQ
