/*
 * WillEQ spellsnew.edd colour words (B1146) -- implementation.
 * See include/client/spell/spells_new_edd_colour.h for the format and the
 * client code each word was read from. B1145 (#465): the files are read by
 * SpellsNewEffReader / SpellsNewEddReader, not here.
 */

#include "client/spell/spells_new_edd_colour.h"

#include "client/spell/spells_new_eff_reader.h"

namespace EQ {

SpellsNewEddColour decodeSpellsNewEddColour(const uint8_t* e) {
    SpellsNewEddColour c;
    c.additive = eddInt(e, kEddColourAdditiveOffset) == 1;
    c.fadeIn = eddFloat(e, kEddColourFadeInOffset);
    c.fadeOut = eddFloat(e, kEddColourFadeOutOffset);
    c.peakOpacity = eddFloat(e, kEddColourPeakOpacityOffset);
    for (size_t i = 0; i < 3; ++i) {
        c.start[i] = eddInt(e, kEddColourStartRgbOffset + 4 * i);
        c.end[i] = eddInt(e, kEddColourEndRgbOffset + 4 * i);
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

    SpellsNewEffReader reader;
    if (!reader.loadFiles(effPath, eddPath)) {
        m_error = reader.loadError();
        return false;
    }
    const SpellsNewEddReader& edd = reader.edd();
    const size_t nEdd = edd.emitterCount();
    m_colours.resize(nEdd);
    m_hasTexture.resize(nEdd);
    for (size_t i = 1; i < nEdd; ++i) {
        const int32_t idx = static_cast<int32_t>(i);
        m_colours[i] = decodeSpellsNewEddColour(edd.entryBytes(idx));
        // The same "names a texture" rule as SpellsNewEffReader, so
        // stageColours() lists exactly the slots stageTextures() lists.
        m_hasTexture[i] = !edd.texture(idx).empty();
    }

    const size_t nRec = reader.recordCount();
    m_records.resize(nRec);
    for (size_t r = 1; r < nRec; ++r) {
        const SpellsNewEffRecord* rec = reader.getRecord(static_cast<int32_t>(r));
        if (!rec) continue;
        for (size_t g = 0; g < 3; ++g)
            for (size_t k = 0; k < 4; ++k) m_records[r][g][k] = rec->stages[g].slots[k].emitter;
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
