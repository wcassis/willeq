/*
 * WillEQ spellsnew.eff + spellsnew.edd reader (B1127, #447)
 *
 * THE OFFICIAL CLIENT DRAWS THESE, NOT spells.eff. A spell's effect is the
 * spellsnew.eff record named by spells_us.txt field 145 (SpellField::
 * NewEffectIndex); each record lists the emitters (spellsnew.edd entries)
 * of its three stages, and an emitter's art is the texture its EDD entry
 * names. Proven on the oracle by same-field-123 / different-field-145 A/B
 * casts and by the art of the B1130 survey's 41 records (GENI00 records
 * draw flare_blsp50a.png, not speli1's skulls; 458's GENA20 record draws
 * gena20.bmp) -- review/B1127/README.md has the captures.
 *
 * spellsnew.eff (no header, 268-byte records, record N at N*268):
 *   +0    char name[64]
 *   +64   three stage groups of 68 bytes: Source, Middle, Target
 *           int32 sound              (spells.eff block +0x21C carries the
 *                                     same 103..112 values; NOT used)
 *           4 x { int32 emitter      spellsnew.edd index, 0 = empty slot
 *                 int32 b            0/24/39/1/2, NOT proven, not used
 *                 int32 actor        1 caster / 3 target (record 113 =
 *                                    B1130 record 27 lands on the caster);
 *                                    exposed raw, not used here
 *                 int32 d }          1..12, NOT proven, not used
 * spellsnew.edd ("EDD\0" "110\0", then 416-byte entries):
 *   +0    char name[64]
 *   +64   char texture[64]   file under SpellEffects/ (any case; one entry
 *                            pads it with '.', trimmed here)
 *   +0x130 int32 frames      16 / 4 / 1 -- agrees with every texture's
 *                            layout (4x4, 2x2, one picture)
 *   everything else          NOT decoded (colours at +0xD4.., rates,
 *                            velocities): unproven fields are not used.
 */

#ifndef EQT_SPELLS_NEW_EFF_READER_H
#define EQT_SPELLS_NEW_EFF_READER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace EQ {

constexpr size_t kSpellsNewEffRecordSize = 268;
constexpr size_t kSpellsNewEffNameBytes = 64;
constexpr size_t kSpellsNewEffStageBytes = 68;
constexpr size_t kSpellsNewEffSlotsPerStage = 4;
constexpr size_t kSpellsNewEddHeaderBytes = 8;
constexpr size_t kSpellsNewEddEntrySize = 416;
constexpr size_t kSpellsNewEddFramesOffset = 0x130;

struct SpellsNewEffSlot {
    int32_t emitter = 0;  // spellsnew.edd index; 0 = empty
    int32_t b = 0;
    int32_t actor = 0;
    int32_t d = 0;
};

struct SpellsNewEffStage {
    int32_t sound = 0;
    std::array<SpellsNewEffSlot, kSpellsNewEffSlotsPerStage> slots{};
    bool hasContent() const;
};

struct SpellsNewEffRecord {
    std::string name;
    std::array<SpellsNewEffStage, 3> stages{};  // Source, Middle, Target
    bool hasContent() const;
};

struct SpellsNewEddEntry {
    std::string name;
    std::string texture;  // lower-case basename, e.g. "spelua.png"
    int32_t frames = 0;
};

class SpellsNewEffReader {
public:
    // Loads <dir>/spellsnew.eff and <dir>/spellsnew.edd. Both must parse
    // exactly (record-size multiple, EDD magic) or the load fails.
    bool load(const std::string& eqClientDir);
    bool loadFiles(const std::string& effPath, const std::string& eddPath);

    bool isLoaded() const { return m_loaded; }
    const std::string& loadError() const { return m_error; }
    size_t recordCount() const { return m_records.size(); }
    size_t emitterCount() const { return m_edd.size(); }

    // nullptr for index <= 0 (field 145's "none" values) or out of range
    // (spells_us.txt names records up to 3411; the shipped file has 418).
    const SpellsNewEffRecord* getRecord(int32_t index) const;
    const SpellsNewEddEntry* getEmitter(int32_t index) const;

    // The textures of stage `stage` (0 Source, 1 Middle, 2 Target), one per
    // non-empty slot, in slot order (a texture named by two slots appears
    // twice: two emitters draw it). Empty when the record or stage is empty.
    std::vector<std::string> stageTextures(int32_t record, int stage) const;

private:
    std::vector<SpellsNewEffRecord> m_records;
    std::vector<SpellsNewEddEntry> m_edd;
    bool m_loaded = false;
    std::string m_error;
};

}  // namespace EQ

#endif  // EQT_SPELLS_NEW_EFF_READER_H
