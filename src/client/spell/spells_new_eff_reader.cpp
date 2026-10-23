/*
 * WillEQ spellsnew.eff + spellsnew.edd reader (B1127) -- implementation.
 * See include/client/spell/spells_new_eff_reader.h for the format.
 */

#include "client/spell/spells_new_eff_reader.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace EQ {

namespace {

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

std::string fixedString(const uint8_t* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

}  // namespace

bool SpellsNewEffStage::hasContent() const {
    for (const auto& s : slots) {
        if (s.emitter > 0) return true;
    }
    return false;
}

bool SpellsNewEffRecord::hasContent() const {
    for (const auto& s : stages) {
        if (s.hasContent()) return true;
    }
    return false;
}

bool SpellsNewEffReader::load(const std::string& eqClientDir) {
    return loadFiles(eqClientDir + "/spellsnew.eff", eqClientDir + "/spellsnew.edd");
}

bool SpellsNewEffReader::loadFiles(const std::string& effPath, const std::string& eddPath) {
    m_loaded = false;
    m_records.clear();
    m_edd.clear();

    std::vector<uint8_t> eff, edd;
    if (!readAll(effPath, eff)) { m_error = "cannot open " + effPath; return false; }
    if (!readAll(eddPath, edd)) { m_error = "cannot open " + eddPath; return false; }
    if (eff.empty() || eff.size() % kSpellsNewEffRecordSize != 0) {
        m_error = effPath + " is not a whole number of 268-byte records";
        return false;
    }
    if (edd.size() < kSpellsNewEddHeaderBytes ||
        (edd.size() - kSpellsNewEddHeaderBytes) % kSpellsNewEddEntrySize != 0 ||
        std::memcmp(edd.data(), "EDD\0", 4) != 0) {
        m_error = eddPath + " is not an EDD file of 416-byte entries";
        return false;
    }

    const size_t nRec = eff.size() / kSpellsNewEffRecordSize;
    m_records.resize(nRec);
    for (size_t r = 0; r < nRec; ++r) {
        const uint8_t* b = eff.data() + r * kSpellsNewEffRecordSize;
        SpellsNewEffRecord& rec = m_records[r];
        rec.name = fixedString(b, kSpellsNewEffNameBytes);
        for (size_t g = 0; g < 3; ++g) {
            const uint8_t* s = b + kSpellsNewEffNameBytes + g * kSpellsNewEffStageBytes;
            rec.stages[g].sound = le32(s);
            for (size_t k = 0; k < kSpellsNewEffSlotsPerStage; ++k) {
                const uint8_t* e = s + 4 + k * 16;
                rec.stages[g].slots[k] = {le32(e), le32(e + 4), le32(e + 8), le32(e + 12)};
            }
        }
    }

    const size_t nEdd = (edd.size() - kSpellsNewEddHeaderBytes) / kSpellsNewEddEntrySize;
    m_edd.resize(nEdd);
    for (size_t i = 0; i < nEdd; ++i) {
        const uint8_t* b = edd.data() + kSpellsNewEddHeaderBytes + i * kSpellsNewEddEntrySize;
        SpellsNewEddEntry& e = m_edd[i];
        e.name = fixedString(b, 64);
        std::string tex = fixedString(b + 64, 64);
        // One shipped entry spells its file "gena20.bmp......." -- trailing
        // dots are padding, not part of a Windows file name.
        while (!tex.empty() && tex.back() == '.') tex.pop_back();
        std::transform(tex.begin(), tex.end(), tex.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        e.texture = tex;
        e.frames = le32(b + kSpellsNewEddFramesOffset);
    }

    // Every emitter a record names must exist.
    for (const auto& rec : m_records) {
        for (const auto& st : rec.stages) {
            for (const auto& sl : st.slots) {
                if (sl.emitter < 0 || static_cast<size_t>(sl.emitter) >= nEdd) {
                    m_error = "spellsnew.eff record '" + rec.name + "' names emitter " +
                              std::to_string(sl.emitter) + " outside spellsnew.edd";
                    m_records.clear();
                    m_edd.clear();
                    return false;
                }
            }
        }
    }

    m_error.clear();
    m_loaded = true;
    return true;
}

const SpellsNewEffRecord* SpellsNewEffReader::getRecord(int32_t index) const {
    if (index <= 0 || static_cast<size_t>(index) >= m_records.size()) return nullptr;
    return &m_records[static_cast<size_t>(index)];
}

const SpellsNewEddEntry* SpellsNewEffReader::getEmitter(int32_t index) const {
    if (index <= 0 || static_cast<size_t>(index) >= m_edd.size()) return nullptr;
    return &m_edd[static_cast<size_t>(index)];
}

std::vector<std::string> SpellsNewEffReader::stageTextures(int32_t record, int stage) const {
    std::vector<std::string> out;
    const SpellsNewEffRecord* rec = getRecord(record);
    if (!rec || stage < 0 || stage > 2) return out;
    for (const auto& sl : rec->stages[static_cast<size_t>(stage)].slots) {
        if (const SpellsNewEddEntry* e = getEmitter(sl.emitter)) {
            if (!e->texture.empty()) out.push_back(e->texture);
        }
    }
    return out;
}

}  // namespace EQ
