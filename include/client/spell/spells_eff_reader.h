/*
 * WillEQ spells.eff Reader
 *
 * Reads the Titanium client's spells.eff — the visual effect definitions
 * used by real EverQuest to render spell casting/landing (as opposed to
 * the mechanics data in spells_us.txt / SpellData::effects, which T68
 * plumbed separately).
 *
 * Format (T70, established by direct parse of
 * /mnt/projects/EverQuestP1999/spells.eff — 714,752 bytes = 2792 x 256
 * exactly, no file header, no index):
 *
 *   256 fixed-size records, kRecordSize (2792) bytes each, back to back.
 *   Record N starts at byte N * kRecordSize. THE RECORD INDEX IS THE
 *   EFFECT ID — it is not stored anywhere in the file, it is the
 *   position, and it is exactly what spells_us.txt field 123
 *   (SpellAffectIndex, carried through willeq.db and SpellData by T68)
 *   references.
 *
 *   Per record:
 *     offset 0,  size 8: two int32 header fields. Record 0 is (1, 0);
 *       every other sampled record reads (0, 0). Meaning NOT established —
 *       exposed raw as header_a/header_b, not interpreted.
 *     offset 8,  size 928: emitter block 0 -- Source (caster)
 *     offset 936, size 928: emitter block 1 -- middle / travel
 *     offset 1864, size 928: emitter block 2 -- Target
 *
 *   Each 928-byte emitter block opens with four 32-byte fixed strings:
 *   three sprite-name slots (not all necessarily populated) followed by
 *   an attach-point tag. Observed tag values across the whole file:
 *   "Source", "Target", "Sprite", and empty -- so the tag is exposed as a
 *   raw string, not an enum with only two members.
 *
 *   Bytes 128..928 of each block (800 bytes) are NOT understood. Their
 *   contents are provably non-constant (they differ between records that
 *   share identical, empty name slots -- see records 36 and 60 in the
 *   T70 Review), so they are not simply unused padding, but no field
 *   inside them has been given a name here because no field's meaning has
 *   been demonstrated. They are exposed as raw bytes via unknown_bytes()
 *   rather than guessed at. A later unit may narrow this with real
 *   evidence; until then, do not trust a "particles_per_second"-shaped
 *   name for anything in this range.
 *
 * What this unit deliberately does NOT do: wire into rendering, touch
 * spell_visual_fx.* / the particle system / config/spell_effects.json, or
 * invent semantics for the unknown region. See
 * refactor/T70_spells_eff_reader.md.
 */

#ifndef EQT_SPELLS_EFF_READER_H
#define EQT_SPELLS_EFF_READER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace EQ {

// ============================================================================
// Layout constants
// ============================================================================

constexpr size_t kSpellsEffEmitterSize = 928;
// Four 32-byte fixed strings: 3 sprite-name slots + 1 attach-point tag.
constexpr size_t kSpellsEffEmitterStringBytes = 4 * 32;
constexpr size_t kSpellsEffEmitterUnknownBytes =
    kSpellsEffEmitterSize - kSpellsEffEmitterStringBytes;  // 800
constexpr size_t kSpellsEffEmittersPerRecord = 3;
constexpr size_t kSpellsEffRecordHeaderBytes = 8;
constexpr size_t kSpellsEffRecordSize =
    kSpellsEffRecordHeaderBytes +
    kSpellsEffEmittersPerRecord * kSpellsEffEmitterSize;  // 2792
constexpr size_t kSpellsEffRecordCount = 256;
constexpr size_t kSpellsEffFileSize =
    kSpellsEffRecordSize * kSpellsEffRecordCount;  // 714752

// SpellAffectIndex's own "no visual effect" marker (spell_constants.h /
// spells_us.txt field 123). Not a valid record index.
constexpr int kSpellsEffNoEffectIndex = -1;

// Emitter roles within a record, in file order.
enum class SpellsEffEmitterRole : int {
    Source = 0,  // caster
    Middle = 1,  // travel / mid-flight
    Target = 2,
};

// ============================================================================
// One 928-byte emitter block
// ============================================================================

struct SpellsEffEmitter {
    // Three sprite-name slots read directly from the file. Empty entries
    // are legitimate (record 0's middle block has only slot 0 populated).
    std::array<std::string, 3> sprite_names;

    // Fourth 32-byte string. Observed values across the whole file:
    // "Source", "Target", "Sprite", or empty -- kept as a raw string
    // rather than an enum because "Sprite" would not fit a Source/Target
    // dichotomy.
    std::string attach_tag;

    // Bytes 128..928 of the block, raw. See the header comment: this
    // region's structure is not understood and is not guessed at here.
    std::array<uint8_t, kSpellsEffEmitterUnknownBytes> unknown_bytes{};

    // True if any sprite-name slot or the attach tag is non-empty. This
    // is the only "does this emitter carry visual content" signal this
    // reader asserts, and it is provable directly from the string bytes
    // (not from the unknown region).
    bool hasContent() const;
};

// ============================================================================
// One 2792-byte record ( == one spells.eff "effect", indexed by
// SpellAffectIndex )
// ============================================================================

struct SpellsEffRecord {
    // Raw header ints. Record 0 is (1, 0); other sampled records are
    // (0, 0). Meaning not established -- do not assign it significance.
    int32_t header_a = 0;
    int32_t header_b = 0;

    // [0]=Source (caster), [1]=middle/travel, [2]=Target.
    std::array<SpellsEffEmitter, kSpellsEffEmittersPerRecord> emitters;

    const SpellsEffEmitter& emitter(SpellsEffEmitterRole role) const {
        return emitters[static_cast<size_t>(role)];
    }

    // True if any emitter has populated sprite-name/attach-tag content.
    // False for the ~164 padding records that round the file out to 256
    // (record 255 among them) -- see T70 Review for the exact count.
    bool hasContent() const;
};

// ============================================================================
// Reader
// ============================================================================

class SpellsEffReader {
public:
    SpellsEffReader() = default;

    // Loads and fully parses the file. Requires the file to be exactly
    // kSpellsEffFileSize bytes (714,752) -- per the T70 plan's STOP
    // condition, a size mismatch is reported as a load failure rather
    // than tolerated or guessed around.
    bool load(const std::string& path);

    bool isLoaded() const { return m_loaded; }
    const std::string& loadError() const { return m_load_error; }

    static constexpr size_t recordCount() { return kSpellsEffRecordCount; }

    // Returns the record at `index`, or nullptr if index is out of range
    // ([0, 255]) or is kSpellsEffNoEffectIndex (-1, SpellAffectIndex's
    // "no visual effect" marker) -- callers should treat -1 as "nothing
    // to look up" rather than an error.
    const SpellsEffRecord* getRecord(int index) const;

    // Number of records for which SpellsEffRecord::hasContent() is true.
    size_t populatedRecordCount() const;

    // Salvaged from the parked fix-spell-effects branch (48ae032b),
    // verified against the real files in
    // /mnt/projects/EverQuestP1999/SpellEffects/ per the T70 Review
    // rather than trusted as-is:
    //   GEN[A-Z][1-9]0_SPRITE -> spel[a-z][1-9].tga
    // This holds for most but NOT all observed nonzero-digit names (two
    // counterexamples found: GENA30_SPRITE and GENY10_SPRITE have no
    // matching file) and NEVER holds for the "00"-digit form (GENx00,
    // 0/16 observed names resolved to a real file) -- so digit '0'
    // deliberately returns empty rather than a name that is known not to
    // exist. Non-"GEN..." names (e.g. "GEN_Z_SPB", "I_SNOWFLAKESPRITE")
    // also return empty; no evidenced mapping exists for them.
    static std::string effectNameToTexture(const std::string& effect_name);

private:
    bool parseRecord(const std::vector<uint8_t>& data, size_t record_index,
                      SpellsEffRecord& out) const;
    static std::string readFixedString32(const std::vector<uint8_t>& data, size_t offset);

    std::vector<SpellsEffRecord> m_records;
    bool m_loaded = false;
    std::string m_load_error;
};

}  // namespace EQ

#endif  // EQT_SPELLS_EFF_READER_H
