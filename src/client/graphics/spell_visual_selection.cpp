/*
 * WillEQ Spell Visual Selection Layer (T72) -- implementation.
 * See include/client/graphics/spell_visual_selection.h for scope.
 */

#include "client/graphics/spell_visual_selection.h"

namespace EQ {

// B1190: the legacy record's drawn slots for a stage under the 0x49ab78 rule.
std::vector<LegacySlotDraw> SpellVisualResolution::legacySlots(int stage, int caster_level) const {
    const SpellVisualStage* st =
        stage == 0 ? &source_stage : (stage == 1 ? &middle_stage : &target_stage);
    return legacyStageDrawnSlots(stage, caster_level, st->effect_types);
}

SpellVisualStage SpellVisualSelector::resolveStage(const SpellsEffEmitter& emitter) {
    SpellVisualStage stage;
    stage.present = emitter.hasContent();
    for (size_t i = 0; i < emitter.sprite_names.size(); ++i) stage.sprite_names[i] = emitter.sprite_names[i];
    stage.attach_tag = emitter.attach_tag;
    for (size_t i = 0; i < stage.sprite_names.size(); ++i) {
        if (!stage.sprite_names[i].empty()) {
            stage.texture_files[i] = SpellsEffReader::effectNameToTexture(stage.sprite_names[i]);
        }
    }
    // B1190: the per-slot effect types the legacy dispatcher dispatches on.
    for (size_t i = 0; i < 3; ++i) stage.effect_types[i] = emitter.effectType(i);
    return stage;
}

// B1127: one spellsnew.eff stage -> its emitters' textures, in slot order.
SpellVisualStage SpellVisualSelector::resolveNewStage(const SpellsNewEffReader& reader,
                                                      int32_t record, int stage) {
    SpellVisualStage out;
    out.from_spell_anim = true;
    const std::vector<std::string> tex = reader.stageTextures(record, stage);
    for (size_t i = 0; i < tex.size() && i < out.sprite_names.size(); ++i) {
        out.sprite_names[i] = tex[i];
        out.texture_files[i] = tex[i];
    }
    out.present = !tex.empty();
    return out;
}

SpellVisualResolution SpellVisualSelector::resolve(int32_t spell_affect_index,
                                                   int32_t spell_anim) const {
    SpellVisualResolution result = resolveSpellsEff(spell_affect_index);
    result.spell_anim = spell_anim;
    const SpellsNewEffRecord* rec = m_new ? m_new->getRecord(spell_anim) : nullptr;
    if (!rec || !rec->hasContent()) return result;
    // The oracle draws this record's art for every stage (B1127).
    const std::string sourceTag = result.source_stage.attach_tag;
    const std::string middleTag = result.middle_stage.attach_tag;
    const std::string targetTag = result.target_stage.attach_tag;
    result.source = SpellVisualSource::Resolved;
    result.source_stage = resolveNewStage(*m_new, spell_anim, 0);
    result.middle_stage = resolveNewStage(*m_new, spell_anim, 1);
    result.target_stage = resolveNewStage(*m_new, spell_anim, 2);
    result.source_stage.attach_tag = sourceTag;
    result.middle_stage.attach_tag = middleTag;
    result.target_stage.attach_tag = targetTag;
    return result;
}

SpellVisualResolution SpellVisualSelector::resolveSpellsEff(int32_t spell_affect_index) const {
    SpellVisualResolution result;
    result.spell_affect_index = spell_affect_index;

    if (spell_affect_index == kSpellsEffNoEffectIndex || !m_reader) {
        result.source = SpellVisualSource::NoAffectIndex;
        return result;
    }

    const SpellsEffRecord* record = m_reader->getRecord(spell_affect_index);
    if (!record) {
        // Out-of-range index (shouldn't happen for a real spell_affect_index
        // per T70's finding that every referenced index resolves in-range,
        // but a corrupt/unexpected value must still degrade to fallback
        // rather than crash or fabricate content).
        result.source = SpellVisualSource::NoAffectIndex;
        return result;
    }

    if (!record->hasContent()) {
        result.source = SpellVisualSource::EmptyRecord;
        return result;
    }

    result.source = SpellVisualSource::Resolved;
    result.source_stage = resolveStage(record->emitter(SpellsEffEmitterRole::Source));
    result.middle_stage = resolveStage(record->emitter(SpellsEffEmitterRole::Middle));
    result.target_stage = resolveStage(record->emitter(SpellsEffEmitterRole::Target));
    return result;
}

}  // namespace EQ
