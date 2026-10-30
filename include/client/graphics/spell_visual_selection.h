/*
 * WillEQ Spell Visual Selection Layer (T72)
 *
 * Pure mapping: spell_affect_index -> spells.eff record -> per-stage
 * resolved texture identity, plus which of the three stages (T70's
 * Source/middle/Target) actually carry content.
 *
 * Deliberately separate from SpellVisualFX so it can be tested without any
 * Irrlicht/rendering dependency -- resolve() is a pure function of its
 * inputs. See refactor/T72_spell_visual_layer.md.
 *
 * B1127 (#447): THE ART COMES FROM spellsnew.eff. The official client draws
 * the textures spellsnew.edd names for the emitters of the spell's
 * spellsnew.eff record (spells_us.txt field 145, `spellAnim`), not the
 * GEN*_SPRITE names of its spells.eff record (review/B1127/README.md: same
 * field-123 record + different field 145 = different art on the oracle).
 * resolve() therefore takes both indices: when field 145 names a populated
 * record, each stage's names/files ARE that record's textures (up to four,
 * one per emitter) and its `present` flag is that stage's; spells.eff then
 * only supplies `spell_affect_index`, which keys the per-record anchors
 * (B1131/B1132). A field-145 record makes a spell Resolved even with no
 * spells.eff record (the oracle draws it).
 *
 * Scope boundary (do not extend past this without re-reading T70's Review):
 * this layer exposes WHICH textures a stage names and WHETHER a stage has
 * content. It does NOT expose timing, scale, colour, particle rate,
 * lifetime, or velocity -- spells_eff_reader.h's `unknown_bytes` region is
 * not touched here, directly or indirectly.
 */

#ifndef EQT_GRAPHICS_SPELL_VISUAL_SELECTION_H
#define EQT_GRAPHICS_SPELL_VISUAL_SELECTION_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "client/spell/spells_eff_reader.h"
#include "client/spell/spells_new_eff_reader.h"

namespace EQ {

// Why a spell resolved (or didn't) to real spells.eff visual data. Kept as
// a 3-way enum, not a bool, specifically so the two distinct fallback
// populations T70 found (~798 spells with no index at all, ~148 spells
// whose referenced index lands on an empty padding record) stay
// distinguishable in logs and tests instead of collapsing into one
// "no visual" bucket.
enum class SpellVisualSource : uint8_t {
    NoAffectIndex = 0,  // spell_affect_index == -1 (or reader unavailable)
    EmptyRecord,         // index resolves to a record with no sprite/tag content
    Resolved,             // index resolves to a populated spells.eff record
};

// One resolved emitter stage (Source, Middle/travel, or Target).
struct SpellVisualStage {
    bool present = false;  // SpellsEffEmitter::hasContent()

    // Raw sprite-name slots as read from the file (may contain empty
    // strings for unused slots -- see spells_eff_reader.h). B1127: four
    // slots, because a spellsnew.eff stage has four emitters; from that
    // file each name IS a texture file name ("spelua.png").
    std::array<std::string, 4> sprite_names{};

    // effectNameToTexture() applied to each non-empty sprite name. Empty
    // string means either the slot was empty or the name had no evidenced
    // texture mapping (T70 Review) -- both are legitimate, not errors.
    std::array<std::string, 4> texture_files{};

    // B1127: true when the names above came from spellsnew.eff/.edd.
    bool from_spell_anim = false;

    // B1190 (#509): the legacy record's per-slot effect types (0..7 or -1),
    // copied from SpellsEffEmitter::effect_types. Empty (all -1) when the
    // stage came from spellsnew.eff (resolveNewStage) or the reader predates
    // the decode.
    std::array<int32_t, 3> effect_types{ -1, -1, -1 };

    std::string attach_tag;  // "Source" / "Target" / "Sprite" / empty
};

// B1190 (#509): the legacy dispatcher's select rule (0x49ab78). Pure -- no
// reader, no rendering -- so it is testable in isolation. Given one stage's
// three effect types and the caster's level, which slots draw.
//
// The level gate (eqgame 0x49ab78). Ground truth is the disassembly
// (review/B1141/ghidra/out/eqgame.dis 0049abea-0049abfb), read together with
// the C (review/B1141/ghidra/out/eqgame.exe.c:121740-121754):
//   level >= 39          -> every slot draws (0049abee, the jge bypass)
//   slot 1, level < 24   -> skipped in EVERY stage (0049abfd-0049ac01)
//   slot 0, level < 10   -> skipped ONLY in the Middle stage: the slot-0
//                           path falls into the shared branch with the
//                           STAGE in the compared register (set at
//                           :121742; disasm 0049ac09-0049ac13:
//                           `cmp level,10; jge draw; cmp stage,1; je skip`)
//   slot 2, level < 39   -> skipped in EVERY stage. The C shows this as the
//                           same `if (iVar3 == 1) goto skip` as the slot-0
//                           stage check (LAB_0049ac13, :121752), but iVar3 is
//                           REUSED: on the slot-0 path it holds the stage
//                           (param_1), on the slot-2 path it is the constant
//                           1 (iVar8-1, :121746) -- so the condition is
//                           always true there. The disassembly confirms it:
//                           the slot-2 fall-through `dec eax; dec eax;
//                           jmp 0x49ac13` (0049abf7-0049abfb) reaches the
//                           `je 0x49b23f` with ZF = 1, so the skip always
//                           fires; there is no stage check on that path.
// Net effect for a caster below level 39: Source and Target draw slot 0
// from level 1 and slot 2 from level 39; the Middle stage draws slot 0 from
// level 10; slot 1 draws from level 24 in every stage; slot 2 never.
// A slot's type must be 0..5 to draw: -1 is "no slot" (the `!= -1` guard,
// :121739), and 6/7 are parsed but skipped by the type dispatch
// (the `goto LAB_0049b23f` arm). Types 0..5 are: 0 hand-glow, 1 attached,
// 2 world, 3/4 ground, 5 ground + a second attached emitter.
struct LegacySlotDraw {
    int slot;  // 0, 1 or 2 (within the stage)
    int type;  // 0..5
};

// Which slots of `stage` draw for a caster at `caster_level`, from that
// stage's three effect types (the 0x49ab78 gate documented above).
inline std::vector<LegacySlotDraw>
legacyStageDrawnSlots(int stage, int caster_level, const std::array<int32_t, 3>& types) {
    std::vector<LegacySlotDraw> out;
    for (int s = 0; s < 3; ++s) {
        const int t = types[s];
        if (t < 0 || t > 5) continue;                    // -1 absent; 6,7 skipped
        if (caster_level < 39) {
            if (s == 0) {
                if (caster_level < 10 && stage == 1) continue;  // Middle only
            } else if (s == 1) {
                if (caster_level < 24) continue;             // every stage
            } else {
                continue;                                    // every stage
            }
        }
        out.push_back(LegacySlotDraw{s, t});
    }
    return out;
}

// B1190 fix (review note 8631 f1): how many emitters the legacy dispatcher
// draws for one DRAWABLE slot. 0x49ab78's type-0 arm makes two emitters (the
// L and R hand pair, the two FUN_00495ad4 calls at
// review/B1141/ghidra/out/eqgame.exe.c:121771/:121785) and its type-5 arm
// makes a ground emitter (:121936) and, on a free cast (no object), a second
// mode-6 emitter attached to the anchor model (:121937-:121940 into
// :121866-:121876); types 1..4 make one each. A non-drawable type (-1/6/7)
// makes none.
inline int legacySlotEmitterCount(int type) {
    if (type == 0 || type == 5) return 2;
    return (type >= 1 && type <= 4) ? 1 : 0;
}

// The fully-resolved visual definition for one spell, keyed by its
// spell_affect_index.
struct SpellVisualResolution {
    SpellVisualSource source = SpellVisualSource::NoAffectIndex;
    int32_t spell_affect_index = kSpellsEffNoEffectIndex;
    int32_t spell_anim = 0;  // B1127: spells_us.txt field 145

    SpellVisualStage source_stage;  // caster
    SpellVisualStage middle_stage;  // travel
    SpellVisualStage target_stage;  // impact

    bool isFallback() const { return source != SpellVisualSource::Resolved; }

    // Convenience: does the resolved record carry a stage with content?
    // Always false for fallback resolutions -- callers must not
    // synthesize staging for spells with no real record.
    bool hasSourceStage() const { return source == SpellVisualSource::Resolved && source_stage.present; }
    bool hasTravelStage() const { return source == SpellVisualSource::Resolved && middle_stage.present; }
    bool hasTargetStage() const { return source == SpellVisualSource::Resolved && target_stage.present; }

    // B1190 (#509): the legacy record's drawn slots for `stage` (0=Source,
    // 1=Middle, 2=Target) under the 0x49ab78 select rule. Empty when the
    // stage carries no legacy effect types (a spellsnew.eff stage, or a
    // legacy record whose slots are all -1/6/7) -- callers then keep the
    // T72 generic behaviour.
    std::vector<LegacySlotDraw> legacySlots(int stage, int caster_level) const;
};

// Resolves spell_affect_index -> SpellVisualResolution against a loaded
// SpellsEffReader. Stateless beyond holding the reader pointer -- safe to
// construct cheaply and call resolve() repeatedly; does not own the
// reader.
class SpellVisualSelector {
public:
    // reader may be nullptr (e.g. spells.eff missing) -- resolve() then
    // always returns NoAffectIndex/EmptyRecord-equivalent fallback
    // resolutions, never crashes.
    explicit SpellVisualSelector(const SpellsEffReader* reader,
                                 const SpellsNewEffReader* newReader = nullptr)
        : m_reader(reader), m_new(newReader) {}

    // B1127: `spell_anim` is spells_us.txt field 145; 0 (or no new reader)
    // keeps the T72 spells.eff-only resolution.
    SpellVisualResolution resolve(int32_t spell_affect_index, int32_t spell_anim = 0) const;

private:
    // The T72 spells.eff-only resolution.
    SpellVisualResolution resolveSpellsEff(int32_t spell_affect_index) const;
    static SpellVisualStage resolveStage(const SpellsEffEmitter& emitter);
    static SpellVisualStage resolveNewStage(const SpellsNewEffReader& reader, int32_t record,
                                            int stage);

    const SpellsEffReader* m_reader;
    const SpellsNewEffReader* m_new;
};

}  // namespace EQ

#endif  // EQT_GRAPHICS_SPELL_VISUAL_SELECTION_H
