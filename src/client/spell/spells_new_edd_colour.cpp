/*
 * WillEQ spellsnew.edd colour words (B1146) -- implementation.
 * See include/client/spell/spells_new_edd_colour.h for the format and the
 * client code each word was read from.
 */

#include "client/spell/spells_new_edd_colour.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace EQ {

namespace {

constexpr size_t kEffRecordSize = 268;
constexpr size_t kEffNameBytes = 64;
constexpr size_t kEffStageBytes = 68;
constexpr size_t kEddHeaderBytes = 8;
constexpr size_t kEddEntrySize = 416;
constexpr size_t kEddTextureOffset = 64;

bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

int32_t le32(const uint8_t* p) {
    uint32_t v = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
                 (uint32_t(p[3]) << 24);
    int32_t s;
    std::memcpy(&s, &v, sizeof s);
    return s;
}

float lef32(const uint8_t* p) {
    const int32_t i = le32(p);
    float f;
    std::memcpy(&f, &i, sizeof f);
    return f;
}

// The same "names a texture" rule as SpellsNewEffReader (NUL-terminated,
// trailing '.' padding trimmed, non-empty), so stageColours() lists exactly
// the slots stageTextures() lists.
bool namesTexture(const uint8_t* entry) {
    const uint8_t* t = entry + kEddTextureOffset;
    size_t len = 0;
    while (len < 64 && t[len] != 0) ++len;
    while (len > 0 && t[len - 1] == '.') --len;
    return len > 0;
}

}  // namespace

SpellsNewEddColour decodeSpellsNewEddColour(const uint8_t* e) {
    SpellsNewEddColour c;
    c.additive = le32(e + kEddColourAdditiveOffset) == 1;
    c.fadeIn = lef32(e + kEddColourFadeInOffset);
    c.fadeOut = lef32(e + kEddColourFadeOutOffset);
    c.peakOpacity = lef32(e + kEddColourPeakOpacityOffset);
    for (size_t i = 0; i < 3; ++i) {
        c.start[i] = le32(e + kEddColourStartRgbOffset + 4 * i);
        c.end[i] = le32(e + kEddColourEndRgbOffset + 4 * i);
    }
    return c;
}

bool SpellsNewEddColourTable::load(const std::string& eqClientDir) {
    std::string dir = eqClientDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    return loadFiles(dir + "spellsnew.eff", dir + "spellsnew.edd");
}

bool SpellsNewEddColourTable::loadFiles(const std::string& effPath, const std::string& eddPath) {
    m_loaded = false;
    m_records.clear();
    m_colours.clear();
    m_hasTexture.clear();

    std::vector<uint8_t> eff, edd;
    if (!readAll(effPath, eff) || eff.empty() || eff.size() % kEffRecordSize != 0) {
        m_error = "spellsnew.eff missing or not a multiple of 268 bytes: " + effPath;
        return false;
    }
    if (!readAll(eddPath, edd) || edd.size() < kEddHeaderBytes ||
        std::memcmp(edd.data(), "EDD\0", 4) != 0 ||
        (edd.size() - kEddHeaderBytes) % kEddEntrySize != 0) {
        m_error = "spellsnew.edd missing or malformed: " + eddPath;
        return false;
    }

    const size_t nEdd = (edd.size() - kEddHeaderBytes) / kEddEntrySize;
    m_colours.resize(nEdd);
    m_hasTexture.resize(nEdd);
    for (size_t i = 0; i < nEdd; ++i) {
        const uint8_t* b = edd.data() + kEddHeaderBytes + i * kEddEntrySize;
        m_colours[i] = decodeSpellsNewEddColour(b);
        m_hasTexture[i] = namesTexture(b);
    }

    const size_t nRec = eff.size() / kEffRecordSize;
    m_records.resize(nRec);
    for (size_t r = 0; r < nRec; ++r) {
        const uint8_t* b = eff.data() + r * kEffRecordSize;
        for (size_t g = 0; g < 3; ++g) {
            const uint8_t* s = b + kEffNameBytes + g * kEffStageBytes;
            for (size_t k = 0; k < 4; ++k) m_records[r][g][k] = le32(s + 4 + 16 * k);
        }
    }

    m_error.clear();
    m_loaded = true;
    return true;
}

const SpellsNewEddColour* SpellsNewEddColourTable::emitter(int32_t index) const {
    if (index <= 0 || static_cast<size_t>(index) >= m_colours.size()) return nullptr;
    return &m_colours[static_cast<size_t>(index)];
}

std::vector<SpellsNewEddColour> SpellsNewEddColourTable::stageColours(int32_t record,
                                                                      int stage) const {
    std::vector<SpellsNewEddColour> out;
    if (record <= 0 || static_cast<size_t>(record) >= m_records.size() || stage < 0 || stage > 2)
        return out;
    for (int32_t e : m_records[static_cast<size_t>(record)][static_cast<size_t>(stage)]) {
        const SpellsNewEddColour* c = emitter(e);
        if (c && m_hasTexture[static_cast<size_t>(e)]) out.push_back(*c);
    }
    return out;
}

}  // namespace EQ
