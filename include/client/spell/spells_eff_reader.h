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
 *
 * T73 addendum: a recovered reverse-engineering document (archive/
 * fix-spell-effects-20260811:.agent/spells_eff_format.md) claimed to decode
 * exactly this unknown region -- duration/fade-in/fade-out (int32 ms),
 * min/max scale (float), particles-per-second (int32), particle lifetime
 * (float), and a 12-float colour-keyframe array, at fixed block-relative
 * offsets 0x274-0x298 and 0x370. T73 tested it directly against the real
 * file and data/willeq.db (refactor/T73_spells_eff_parameter_decode.md
 * Review has the full evidence):
 *
 *   - REFUTED: min/max-scale ordering (0x280/0x288). 102 of 151
 *     name-populated emitters have min_scale > max_scale (68%), and the
 *     value set includes negatives (down to -10.0), far outside the
 *     document's own claimed 0.01-100.0 range. Pinned by
 *     T73_ClaimedMinMaxScaleOrderingIsRefutedByMajorityViolation.
 *   - REFUTED (as "colour"): the 0x370 float[12] array. Real values reach
 *     32.0, over 12x the document's own claimed [0.0, 2.0] range, and
 *     populated records show a decay-from-burst-to-1.0 pattern more
 *     consistent with a scale/intensity keyframe curve than RGBA colour.
 *     The array's LOCATION and SIZE are corroborated (ends exactly at the
 *     928-byte block boundary; padding records read a flat (1,1,...,1)
 *     template; populated records differ systematically) -- only the name
 *     "colour" is refuted, not the field's existence. Pinned by
 *     T73_ClaimedColorKeyframeRangeIsRefutedByOutOfRangeValues.
 *   - UNDETERMINED (real, unnamed): duration/fade-in/fade-out/
 *     particles-per-second/particle-lifetime (0x274-0x298). No internal
 *     contradiction found (always non-negative, cluster at round values,
 *     read as exactly zero in both confirmed padding records 88 and 255)
 *     -- but the semantic-correlation test found no relationship between
 *     the claimed duration_ms and either the referencing spells'
 *     duration_formula/is_beneficial classification (buff- vs
 *     nuke-referenced records overlap 33/38 and show no systematic
 *     separation) or their cast_time_ms (Pearson r = -0.09 across 57
 *     distinct records). Real, structured, non-padding data lives at these
 *     offsets; what it means is not established. NOT added as named
 *     fields for exactly the reason T70 gave for the region as a whole.
 *
 * None of the eight claimed fields were added to SpellsEffEmitter --
 * unknown_bytes() is unchanged. See
 * tests/test_spells_eff_reader.cpp's "T73" section for the pinning tests
 * (reading directly out of unknown_bytes, no named accessors) and
 * refactor/T73_spells_eff_parameter_decode.md's Review for full
 * distributions.
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

// B1190 (#509): the per-slot effect-type integers. The oracle's legacy
// dispatcher (0x49ab78, review/B1141/ghidra/out/eqgame.exe.c:121698) reads
// slot `s` of stage `st` at byte (st*0xe8 + 0x25 + s)*4 of the RUNTIME record
// (:121738) -- and 0xe8*4 == 0x3a0 == 928, the emitter-block size, so the
// runtime and the raw file share the 928-byte block stride. The loader
// (0x44d773, :68688) is a plain _fread(&DAT_008513b8, 0xae8, 0x100) (:68705)
// -- no re-layout: the runtime table IS the raw file, so the record-relative
// 0x94 + s*4 the dispatcher reads (0x94 = the 8-byte record header + 0x8C)
// are exactly the bytes at block offset 0x8C + s*4 (140/144/148) in the raw
// file (verified against /mnt/projects/EverQuestP1999/spells.eff,
// review/B1190). Each is 0..7 (the type that 0x49ab78 dispatches: 0
// hand-glow, 1 attached, 2 world, 3/4 ground, 5 ground + second attached,
// 6/7 skip) or -1 (no slot). See refactor/B1190/README.md.
constexpr size_t kSpellsEffEmitterEffectTypeOffset = 0x8C;
constexpr size_t kSpellsEffEffectTypeSlots = 3;
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

    // B1190 (#509): the three per-slot effect-type ints at block offset
    // kSpellsEffEmitterEffectTypeOffset + s*4. 0..7 dispatch in 0x49ab78;
    // -1 means "no slot" (the common case for padding emitters). This is
    // the ONE field in the unknown region whose meaning is demonstrated by
    // the oracle decompile, so it is named here (the rest stay raw).
    std::array<int32_t, kSpellsEffEffectTypeSlots> effect_types{ -1, -1, -1 };

    int32_t effectType(size_t slot) const {
        return slot < kSpellsEffEffectTypeSlots ? effect_types[slot] : -1;
    }

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

    // B1190: per-slot drawable-ness is the select rule itself
    // (legacyStageDrawnSlots in spell_visual_selection.h); a record-level
    // "any slot drawable" aggregate has no production caller, so it is not
    // declared -- the per-slot effectType() is the observation window.
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

    // THE DIGIT IS 0-BASED (T409, board B162):
    //   GEN<L><d>0_SPRITE -> spel<lowercase L><d+1>.tga
    //
    // T70 salvaged the verbatim reading (GEN[A-Z][1-9]0 -> spel[a-z][1-9])
    // from the parked fix-spell-effects branch and excluded digit '0'
    // because none of the sixteen observed "00" names resolved to a file.
    // T389 MEASURED the rule against the official Titanium client and
    // refuted it. Two spells.eff records whose every non-empty sprite slot
    // in every stage holds ONE name settle it, because the drawn particle
    // can only have come from that name:
    //   record 20, every slot GENN00_SPRITE, cast as spell 41 Weaken, drew
    //     speln1.tga's green/orange mottled spheres -- the verbatim rule
    //     resolves that name to nothing at all;
    //   record 35, every slot GENA10_SPRITE, cast as spell 253 Whirling
    //     Wind, drew spela2.tga's bright blue EIGHT-pointed stars, NOT
    //     spela1.tga, which is what the verbatim rule gives.
    // Corroborated by spelo1.tga (285 Pendril's Animation, record 11) and
    // spelu1.tga (54 Frost Bolt's GENU00 Source stage). Evidence:
    // review/T389/, refactor/T389_spell_fx_oracle.md Review S3.
    //
    // Census over the shipped spells.eff and SpellEffects/ (pinned by
    // SpellsEffReaderFileTest.TheResolvedSlotCensusIsTheZeroBasedOne):
    // the verbatim rule resolved 18 of the 40 distinct names and 157 of
    // the 301 non-empty slots to a file present on disk; the 0-based rule
    // resolves 36 names and 272 slots.
    //
    // It is still not 100%: GENA20_SPRITE and GENA30_SPRITE construct
    // spela3.tga / spela4.tga, neither of which exists. The reader returns
    // the constructed name anyway -- texture existence is a rendering
    // concern. Digit '9' would construct a two-character index no file
    // uses, so the accepted digit range is '0'..'8'. Non-"GEN..." names
    // (GEN_Z_SPB, I_SNOWFLAKESPRITE, I_TFIRE_SPB, I_TSMOKE_SPB -- the only
    // four in the file) return empty; no evidenced mapping exists for them.
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
