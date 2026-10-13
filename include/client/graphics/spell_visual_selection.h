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

#include "client/spell/spells_eff_reader.h"

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
    // strings for unused slots -- see spells_eff_reader.h).
    std::array<std::string, 3> sprite_names{};

    // effectNameToTexture() applied to each non-empty sprite name. Empty
    // string means either the slot was empty or the name had no evidenced
    // texture mapping (T70 Review) -- both are legitimate, not errors.
    std::array<std::string, 3> texture_files{};

    std::string attach_tag;  // "Source" / "Target" / "Sprite" / empty
};

// The fully-resolved visual definition for one spell, keyed by its
// spell_affect_index.
struct SpellVisualResolution {
    SpellVisualSource source = SpellVisualSource::NoAffectIndex;
    int32_t spell_affect_index = kSpellsEffNoEffectIndex;

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
    explicit SpellVisualSelector(const SpellsEffReader* reader) : m_reader(reader) {}

    SpellVisualResolution resolve(int32_t spell_affect_index) const;

private:
    static SpellVisualStage resolveStage(const SpellsEffEmitter& emitter);

    const SpellsEffReader* m_reader;
};

}  // namespace EQ

#endif  // EQT_GRAPHICS_SPELL_VISUAL_SELECTION_H
