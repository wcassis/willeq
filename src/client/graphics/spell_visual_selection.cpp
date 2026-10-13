/*
 * WillEQ Spell Visual Selection Layer (T72) -- implementation.
 * See include/client/graphics/spell_visual_selection.h for scope.
 */

#include "client/graphics/spell_visual_selection.h"

namespace EQ {

SpellVisualStage SpellVisualSelector::resolveStage(const SpellsEffEmitter& emitter) {
    SpellVisualStage stage;
    stage.present = emitter.hasContent();
    stage.sprite_names = emitter.sprite_names;
    stage.attach_tag = emitter.attach_tag;
    for (size_t i = 0; i < stage.sprite_names.size(); ++i) {
        if (!stage.sprite_names[i].empty()) {
            stage.texture_files[i] = SpellsEffReader::effectNameToTexture(stage.sprite_names[i]);
        }
    }
    return stage;
}

SpellVisualResolution SpellVisualSelector::resolve(int32_t spell_affect_index) const {
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
