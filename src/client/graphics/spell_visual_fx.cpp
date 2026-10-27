/*
 * WillEQ Spell Visual Effects Implementation
 *
 * Effect creation: construction, particle-texture loading, the per-frame
 * update, and the cast-glow / projectile / impact / spell-complete /
 * buff-aura / ground-circle creation family.
 *
 * Split from a single 1,517-line translation unit by
 * refactor/J34_spell_visual_fx_split.md (move-only, verbatim relocation).
 * The removal, colour-lookup, scene-node builder and worker-result halves
 * now live beside this file in spell_visual_fx_nodes.cpp.
 */

#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/spell/spell_constants.h"
#include "common/logging.h"
#include "client/data_paths.h"
#ifdef EQT_HAS_GLES2
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_particle_types.h"
#endif
#include <algorithm>
#include <vector>
#include <fstream>

#include <fmt/format.h>

namespace EQ {

SpellVisualFX::SpellVisualFX(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                             const std::string& eqClientPath)
    : m_smgr(smgr)
    , m_driver(driver)
    , m_eq_client_path(eqClientPath)
{
#ifndef EQT_HAS_GLES2
    // Load particle textures from EQ client SpellEffects directory.
    // GLES2 uses ParticleManager with color-only point sprites — no textures needed.
    loadParticleTextures();
#endif
    // T72: independent of the GLES2/desktop split above — loads the raw
    // spells.eff staging data used by resolveVisual() regardless of driver.
    loadSpellsEff();
}

void SpellVisualFX::loadSpellsEff()
{
    if (m_eq_client_path.empty()) {
        LOG_DEBUG(MOD_SPELL, "T72: no eqClientPath given, spells.eff visual selection unavailable "
                              "(all spells will use resist/school fallback)");
        return;
    }
    std::string path = m_eq_client_path;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '/';
    }
    path += "spells.eff";

    m_spells_eff_loaded = m_spells_eff.load(path);
    if (m_spells_eff_loaded) {
        LOG_INFO(MOD_SPELL, "T72: loaded spells.eff ({} records, {} populated) from {}",
                 SpellsEffReader::recordCount(), m_spells_eff.populatedRecordCount(), path);
    } else {
        LOG_WARN(MOD_SPELL, "T72: could not load spells.eff from {} ({}) — all spells will use "
                             "resist/school fallback visuals", path, m_spells_eff.loadError());
    }

    // B1146 (#466): the colour words of the same emitters.
    if (!m_spells_new_edd_colour.load(m_eq_client_path)) {
        LOG_WARN(MOD_SPELL, "B1146: spellsnew.edd colour words not loaded ({}) -- spell particles "
                             "draw their sprite's own colour", m_spells_new_edd_colour.loadError());
    }

    // B1127 (#447): the art the official client actually draws.
    m_spells_new_eff_loaded = m_spells_new_eff.load(m_eq_client_path);
    if (m_spells_new_eff_loaded) {
        LOG_INFO(MOD_SPELL, "B1127: loaded spellsnew.eff ({} records) + spellsnew.edd ({} emitters) from {}",
                 m_spells_new_eff.recordCount(), m_spells_new_eff.emitterCount(), m_eq_client_path);
    } else {
        LOG_WARN(MOD_SPELL, "B1127: could not load spellsnew.eff/.edd from {} ({}) -- spell art falls "
                             "back to spells.eff names", m_eq_client_path, m_spells_new_eff.loadError());
    }
}

void SpellVisualFX::noteSpellVocabulary(uint32_t spell_id, const SpellVisualVocabulary& vocab)
{
    if (!vocab.known) return;  // nothing to learn; keep any earlier entry
    m_spell_vocab[spell_id] = vocab;
}

SpellVisualVocabulary SpellVisualFX::spellVocabulary(uint32_t spell_id) const
{
    auto it = m_spell_vocab.find(spell_id);
    return it == m_spell_vocab.end() ? SpellVisualVocabulary{} : it->second;
}

int SpellVisualFX::sourceRecordOf(uint32_t spell_id) const
{
    // B1137 (#457): the record the oracle draws is the spell's field-145
    // record (spellsnew.eff), not the spells.eff record (field 123). B1153
    // (#473): the measured rows it used to select are retired -- the number
    // identifies the record whose spellsnew.edd emitters draw it (every
    // row-lookup consumer returns the default row).
    return spellVocabulary(spell_id).spellAnim;
}

#ifdef EQT_HAS_GLES2
// B1144: B1146's colour words for a def whose texture regions are these
// emitters' textures, one per region in order (a per-slot def).
void SpellVisualFX::applySlotEddColours(EQT::Graphics::Environment::SpellEffectDef& def,
                                        const std::vector<int32_t>& emitters) const
{
    std::vector<SpellsNewEddColour> colours;
    for (int32_t e : emitters) {
        const SpellsNewEddColour* c = m_spells_new_edd_colour.emitter(e);
        if (!c) return;  // no texture/colour pairing to trust
        colours.push_back(*c);
    }
    EQT::Graphics::Environment::SpellPresets::applyStageEmitterColours(def, colours);
}

void SpellVisualFX::applyStageEddColours(EQT::Graphics::Environment::SpellEffectDef& def,
                                         const SpellVisualResolution& visual, int stage) const
{
    // B1146 (#466): a spellsnew.eff stage's emitters carry their own colour
    // words; a spells.eff (field 123) stage has none.
    const SpellVisualStage& st = stage == 0 ? visual.source_stage
                               : stage == 1 ? visual.middle_stage : visual.target_stage;
    if (visual.isFallback() || !st.present || !st.from_spell_anim) return;
    EQT::Graphics::Environment::SpellPresets::applyStageEmitterColours(
        def, m_spells_new_edd_colour.stageColours(visual.spell_anim, stage));
}

#endif
uint16_t SpellVisualFX::attachPointEntityId(uint16_t entity, int32_t attachPoint)
{
    switch (static_cast<SpellsNewEffAttach>(attachPoint)) {
        case SpellsNewEffAttach::LeftHand:  return leftHandEntityId(entity);
        case SpellsNewEffAttach::RightHand: return rightHandEntityId(entity);
        case SpellsNewEffAttach::Head:
        case SpellsNewEffAttach::LeftEye:
        case SpellsNewEffAttach::RightEye:
        case SpellsNewEffAttach::Mouth:     return headEntityId(entity);
        default:                            return entity;
    }
}

SpellVisualFX::StageSlots SpellVisualFX::stageSlots(uint32_t spell_id, int stage, int casterLevel) const
{
    StageSlots out;
    const int32_t record = spellVocabulary(spell_id).spellAnim;
    if (!m_spells_new_eff_loaded) return out;
    const SpellsNewEffRecord* rec = m_spells_new_eff.getRecord(record);
    // The record the selector draws the art from (B1127): one with content.
    if (!rec || !rec->hasContent()) return out;
    out.fromFile = true;
    out.drawn = m_spells_new_eff.drawnSlots(record, stage, casterLevel);
    out.measured = m_spells_new_eff.emitterSlotCount(record, stage);
    return out;
}

bool SpellVisualFX::slotTiming(int32_t emitter, int32_t callerMs, SpellFxSlotTiming& out) const
{
    const SpellsNewEddTiming* w = m_spells_new_eff.edd().timing(emitter);
    if (!w) return false;
    // B1165 (#484): the Spell page's density, applied as CreateEmitter does
    // (0x1005a430: at creation, to the record's own +0x80/+0x84/+0x88).
    out = SpellFxSlotTimingAtDensity(SpellFxSlotTimingFor(*w, callerMs), m_fx_options.density);
    return true;
}

bool SpellVisualFX::stageDraws(uint32_t spell_id, int stage, const SpellFxActor& caster,
                               const SpellFxActor& target) const
{
    // B1165 (#484): FUN_0049b260's gate. `hasRecord` = the spell has a
    // spellsnew.eff record with content (stageSlots' fromFile), eqgame's
    // param_2[0x8d] != 0 arm.
    const bool hasRecord = stageSlots(spell_id, kSpellsNewEffSource, kMeasuredCasterLevel).fromFile;
    return SpellFxStageDraws(m_fx_options, hasRecord, stage, caster, target);
}

SpellVisualResolution SpellVisualFX::resolveVisual(uint32_t spell_id) const
{
    static_assert(kNoSpellAffectIndex == kSpellsEffNoEffectIndex,
                  "spell_visual_vocabulary.h's sentinel must equal the reader's");

    // T381 (B160): the index comes from the event payload the game thread
    // filled, not from a SpellDatabase pointer the renderer never had.
    const SpellVisualVocabulary vocab = spellVocabulary(spell_id);
    const int32_t affect_index = vocab.spellAffectIndex;

    SpellVisualSelector selector(m_spells_eff_loaded ? &m_spells_eff : nullptr,
                                 m_spells_new_eff_loaded ? &m_spells_new_eff : nullptr);
    SpellVisualResolution res = selector.resolve(affect_index, vocab.spellAnim);

    // Distinguish the two fallback populations in logs (T72 requirement) --
    // "no visual" for a spell must be diagnosable as "by design (-1)" vs.
    // "referenced index but the record is empty padding" vs. a genuine
    // resolved record, not one silently-ordinary bucket.
    switch (res.source) {
        case SpellVisualSource::NoAffectIndex:
            LOG_TRACE(MOD_SPELL, "T72 visual: spell {} index={} -> NoAffectIndex fallback "
                                  "(no spell_affect_index or reader unavailable)",
                      spell_id, affect_index);
            break;
        case SpellVisualSource::EmptyRecord:
            LOG_DEBUG(MOD_SPELL, "T72 visual: spell {} index={} -> EmptyRecord fallback "
                                  "(referenced record has no sprite/tag content)",
                      spell_id, affect_index);
            break;
        case SpellVisualSource::Resolved:
            LOG_TRACE(MOD_SPELL, "T72 visual: spell {} index={} -> Resolved "
                                  "(source={} middle={} target={})",
                      spell_id, affect_index, res.hasSourceStage(), res.hasTravelStage(),
                      res.hasTargetStage());
            break;
    }
    return res;
}

SpellVisualFX::~SpellVisualFX()
{
    clearAllEffects();
    // Note: texture is managed by the driver, don't delete it
}

void SpellVisualFX::loadParticleTextures()
{
    if (!m_driver) return;

    std::string spellEffectsPath = m_eq_client_path;
    if (!spellEffectsPath.empty() && spellEffectsPath.back() != '/' && spellEffectsPath.back() != '\\') {
        spellEffectsPath += '/';
    }
    spellEffectsPath += "SpellEffects/";

    // Load particle textures for each resist type
    // Try multiple options in case some don't exist

    // Default particle (for None/Physical/unresistable)
    m_particle_texture = loadDDSTexture(spellEffectsPath + "Glowflare03.dds", "spell_particle_default");
    if (!m_particle_texture) {
        m_particle_texture = loadDDSTexture(spellEffectsPath + "glittersp501.dds", "spell_particle_default");
    }
    if (!m_particle_texture) {
        m_particle_texture = loadDDSTexture(spellEffectsPath + "Corona1.dds", "spell_particle_default");
    }

    // Fire resist spells (red/orange)
    m_fire_texture = loadDDSTexture(spellEffectsPath + "Firesp501.dds", "spell_particle_fire");
    if (!m_fire_texture) {
        m_fire_texture = loadDDSTexture(spellEffectsPath + "FireC.dds", "spell_particle_fire");
    }
    if (!m_fire_texture) {
        m_fire_texture = loadDDSTexture(spellEffectsPath + "flamesp501.dds", "spell_particle_fire");
    }

    // Cold resist spells (blue/white)
    m_frost_texture = loadDDSTexture(spellEffectsPath + "frostsp501.dds", "spell_particle_frost");
    if (!m_frost_texture) {
        m_frost_texture = loadDDSTexture(spellEffectsPath + "frostsp502.dds", "spell_particle_frost");
    }

    // Magic resist spells (purple/electric)
    m_magic_texture = loadDDSTexture(spellEffectsPath + "ElectricA.dds", "spell_particle_magic");
    if (!m_magic_texture) {
        m_magic_texture = loadDDSTexture(spellEffectsPath + "ElectricB.dds", "spell_particle_magic");
    }
    if (!m_magic_texture) {
        m_magic_texture = loadDDSTexture(spellEffectsPath + "ElectricE.dds", "spell_particle_magic");
    }

    // Poison resist spells (green)
    m_poison_texture = loadDDSTexture(spellEffectsPath + "PoisonC.dds", "spell_particle_poison");
    if (!m_poison_texture) {
        m_poison_texture = loadDDSTexture(spellEffectsPath + "Acid1.dds", "spell_particle_poison");
    }
    if (!m_poison_texture) {
        m_poison_texture = loadDDSTexture(spellEffectsPath + "Acid2.dds", "spell_particle_poison");
    }

    // Disease resist spells (brown/sickly)
    m_disease_texture = loadDDSTexture(spellEffectsPath + "diseasesp501.dds", "spell_particle_disease");
    if (!m_disease_texture) {
        m_disease_texture = loadDDSTexture(spellEffectsPath + "genbrownA.dds", "spell_particle_disease");
    }

    // Chromatic/Prismatic resist spells (rainbow/multi)
    m_chromatic_texture = loadDDSTexture(spellEffectsPath + "Corona3.dds", "spell_particle_chromatic");
    if (!m_chromatic_texture) {
        m_chromatic_texture = loadDDSTexture(spellEffectsPath + "8starsp501.dds", "spell_particle_chromatic");
    }

    // Corruption resist spells (dark purple/black)
    m_corruption_texture = loadDDSTexture(spellEffectsPath + "darknesssp501.dds", "spell_particle_corruption");
    if (!m_corruption_texture) {
        m_corruption_texture = loadDDSTexture(spellEffectsPath + "darknesssp502.dds", "spell_particle_corruption");
    }

    // If we couldn't load any textures, load pre-generated fallback
    if (!m_particle_texture) {
        LOG_WARN(MOD_SPELL, "Could not load spell effect textures from {}, loading fallback", spellEffectsPath);
        loadFallbackTexture();
    } else {
        LOG_INFO(MOD_SPELL, "Loaded spell particle textures from {}", spellEffectsPath);
    }

    // Use default texture for any that failed to load
    if (!m_fire_texture) m_fire_texture = m_particle_texture;
    if (!m_frost_texture) m_frost_texture = m_particle_texture;
    if (!m_magic_texture) m_magic_texture = m_particle_texture;
    if (!m_poison_texture) m_poison_texture = m_particle_texture;
    if (!m_disease_texture) m_disease_texture = m_particle_texture;
    if (!m_chromatic_texture) m_chromatic_texture = m_particle_texture;
    if (!m_corruption_texture) m_corruption_texture = m_particle_texture;
}

irr::video::ITexture* SpellVisualFX::loadDDSTexture(const std::string& path, const std::string& name)
{
    if (!m_driver || !m_constrained_cache) return nullptr;

    // Read file contents
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        LOG_TRACE(MOD_SPELL, "Could not open texture: {}", path);
        return nullptr;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> data(fileSize);
    if (!file.read(data.data(), fileSize)) {
        LOG_WARN(MOD_SPELL, "Failed to read texture: {}", path);
        return nullptr;
    }

    // Route through constrained cache
    const EQT::Graphics::TextureOrigin origin = EQT::Graphics::TextureOrigin::plainFile(path);  // B947: restore origin
    irr::video::ITexture* texture = m_constrained_cache->getOrLoad(name, data, "uncategorized", "", false, &origin);
    if (!texture) {
        texture = m_constrained_cache->uploadTexture(name);
    }

    if (texture) {
        LOG_DEBUG(MOD_SPELL, "Loaded particle texture: {} via constrained cache", path);
    }

    return texture;
}

void SpellVisualFX::loadFallbackTexture()
{
    if (!m_driver || !m_constrained_cache) return;

    // Load pre-generated fallback texture from BMP via constrained cache
    std::string path = eqt::ResolveDataPath("data/textures/spell_particle_fallback.bmp");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        LOG_FATAL(MOD_SPELL, "Required texture missing: {} — run generate_textures first", path);
        std::exit(1);
    }
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> data(fileSize);
    file.read(data.data(), fileSize);

    const EQT::Graphics::TextureOrigin origin = EQT::Graphics::TextureOrigin::plainFile(path);  // B947: restore origin
    m_particle_texture = m_constrained_cache->getOrLoad("spell_particle_fallback", data, "uncategorized", "",
                                                        false, &origin);
    if (!m_particle_texture) m_particle_texture = m_constrained_cache->uploadTexture("spell_particle_fallback");
    if (!m_particle_texture) {
        LOG_FATAL(MOD_SPELL, "Failed to load required texture: {}", path);
        std::exit(1);
    }

    LOG_DEBUG(MOD_SPELL, "Loaded fallback particle texture via constrained cache");
}

irr::video::ITexture* SpellVisualFX::getTextureForSpell(uint32_t spell_id) const
{
    // Get appropriate texture based on the spell's noted resist type (T381)
    const SpellVisualVocabulary vocab = spellVocabulary(spell_id);
    if (vocab.known) {
        switch (vocab.resistType) {
            case ResistType::Fire:
                return m_fire_texture;
            case ResistType::Cold:
                return m_frost_texture;
            case ResistType::Magic:
                return m_magic_texture;
            case ResistType::Poison:
                return m_poison_texture;
            case ResistType::Disease:
                return m_disease_texture;
            case ResistType::Chromatic:
            case ResistType::Prismatic:
                return m_chromatic_texture;
            case ResistType::Corruption:
                return m_corruption_texture;
            case ResistType::None:
            case ResistType::Physical:
            default:
                break;
        }
    }
    return m_particle_texture;
}

void SpellVisualFX::onSpawnDeleted(uint16_t spawn_id, bool corpse, uint16_t race_id)
{
    if (spawn_id == 0) return;
    // 0x4849b2: the deferred (corpse-fade) path is taken for spawn type >= 2
    // unless 0x47bfd0 says race 216 or 472.
    const bool fades = corpse && race_id != 216 && race_id != 472;
    if (!fades) {
        removeAttachedEffects(spawn_id);
        return;
    }
    for (auto& p : m_pending_actor_deletes)
        if (p.first == spawn_id) return;  // already queued
    m_pending_actor_deletes.emplace_back(spawn_id, kCorpseActorDeleteSeconds);
}

void SpellVisualFX::removeAttachedEffects(uint16_t spawn_id)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        // remove-by-actor matches the actor and every bone of it: the entity
        // and fable's hand / head pseudo-entities.
        for (uint16_t id : {spawn_id, rightHandEntityId(spawn_id), leftHandEntityId(spawn_id),
                            headEntityId(spawn_id)})
            m_particle_manager->removeSpellEffectsForEntity(id);
        LOG_DEBUG(MOD_SPELL, "spawn {} actor deleted: its attached spell emitters removed", spawn_id);
    }
#endif
    (void)spawn_id;
}

void SpellVisualFX::update(float delta_time)
{
    // B1145: the corpse actors the client deletes 3.5 s after OP_DeleteSpawn.
    for (size_t i = 0; i < m_pending_actor_deletes.size();) {
        m_pending_actor_deletes[i].second -= delta_time;
        if (m_pending_actor_deletes[i].second <= 0.0f) {
            removeAttachedEffects(m_pending_actor_deletes[i].first);
            m_pending_actor_deletes.erase(m_pending_actor_deletes.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }

    // B1170 (#489): the 0x4033 missile objects. FX-layer state, so they run
    // in both worker-driven and direct modes (spell_visual_fx_missile.cpp).
    updateMissileObjects(delta_time);

    // Worker-driven path: update math runs on SimulationWorker thread
    if (m_worker_driven) return;

    // Update each effect and remove expired ones
    for (size_t i = 0; i < m_effects.size(); ) {
        auto& effect = m_effects[i];

        if (!effect.active) {
            removeEffect(i);
            continue;
        }

        effect.elapsed += delta_time;

        // Check for expiration
        if (effect.lifetime > 0 && effect.elapsed >= effect.lifetime) {
            removeEffect(i);
            continue;
        }

        // Type-specific updates (B1152: ProjectileTravel's node path is gone).
        switch (effect.type) {
            case SpellFXType::CastGlow:
                updateGlow(effect, delta_time);
                break;
            default:
                break;
        }

        ++i;
    }
}

// ============================================================================
// Effect Creation
// ============================================================================

// Determine cast effect style from spell school:
//   Spray  = Alteration, Abjuration (protective/healing — outward hand sparks)
//   Smolder = Evocation, Conjuration, Divination (offensive/summoning — orbital glow)
bool SpellVisualFX::isSprayCastStyle(uint32_t spell_id) const
{
    const SpellVisualVocabulary vocab = spellVocabulary(spell_id);
    if (!vocab.known) return false;  // unknown spell id: pre-T381 fallback
    return vocab.school == SpellSchool::Alteration ||
           vocab.school == SpellSchool::Abjuration;
}

int32_t SpellVisualFX::castAnimationMs(uint16_t caster_id, int castAnim) const
{
    // B1148 (#468): 0x47ddd4 -- the caster actor's track for the spell's
    // CastingAnim id (eqgame's code table, spell_fx_timing.h).
    const char* code = SpellCastAnimationCode(castAnim);
    if (!code || !m_cast_anim_length_cb) return -1;
    return m_cast_anim_length_cb(caster_id, code);
}

uint32_t SpellVisualFX::createCastGlow(uint16_t caster_id, uint32_t spell_id, uint32_t duration_ms,
                                      int casterLevel, int castAnim, const SpellFxActor& casterFx)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        // B1165 (#484): the Spell page's gate per stage (FUN_0049b260): the
        // Source on (caster, caster) -- BeginCast passes the caster twice,
        // eqgame.exe.c:122270. (B1170: no Middle here; the 0x4033 object's
        // stage fires are gated in spell_visual_fx_missile.cpp.)
        const bool sourceDraws = stageDraws(spell_id, 0, casterFx, casterFx);
        // Remove any existing cast glow for this caster (center, both hands, head)
        m_particle_manager->removeSpellEffectsForEntity(caster_id);
        m_particle_manager->removeSpellEffectsForEntity(rightHandEntityId(caster_id));
        m_particle_manager->removeSpellEffectsForEntity(leftHandEntityId(caster_id));
        m_particle_manager->removeSpellEffectsForEntity(headEntityId(caster_id));

        // T72: spells.eff's Source stage maps onto CastGlow/CastSpray — the
        // caster-side emitter block. Only a *Resolved* record (a real
        // spells.eff record, not the -1/empty-padding fallback) can
        // suppress this; the resist/school fallback always still shows a
        // cast glow exactly as before, per the plan's "fallback must stay
        // good" requirement.
        SpellVisualResolution visual = resolveVisual(spell_id);
        if (!visual.isFallback() && !visual.hasSourceStage()) {
            LOG_DEBUG(MOD_SPELL, "T72: spell {} spells.eff record {} has no Source-stage content "
                                  "— suppressing cast glow (data-driven, not a bug)",
                      spell_id, visual.spell_affect_index);
            return 1;
        }

        auto scolor = getSpellColor(spell_id);
        glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f,
                     scolor.getBlue() / 255.0f, 1.0f);
        const bool spray = isSprayCastStyle(spell_id);
        // B1131 (#451): the Source is drawn per measured row (B1137: the row of
        // the spell's field-145 record, sourceRecordOf()), as the
        // official client draws it (B1130 survey, config `sourceRecords`):
        // hands / feet-ring / orbit / body / head, with the record's onset.
        // A spell whose override gives its Source its own geometry (B1122's
        // 54/656/41/458; the 253 cloud was retired by B1161) keeps it on the
        // caster's entity, at its own rate.
        // Every Source emits until releaseCastGlow() (OP_Action) ends it after
        // the record's hold; the cap is the cast plus the longest hold, for a
        // cast whose OP_Action never arrives.
        namespace Presets = EQT::Graphics::Environment::SpellPresets;
        using Cfg = EQT::Graphics::Environment::SpellEffectsConfig;
        const auto& cfg = Cfg::instance();
        const int record = sourceRecordOf(spell_id);
        const float durSec = duration_ms / 1000.0f;
        const float cap = durSec + cfg.sourceRecord(record).maxHold();
        const float A = cfg.sourceScale().anchorAboveFeet;
        // B1170 (#489): no Middle stage here. The client draws a record's
        // Middle (stage 1) only on a missile object (eqgame 00495d8e /
        // 00495d93: an anchor-2 slot with no object is skipped), and the
        // only object is the one a server 0x4033 message builds (00460737);
        // the record-driven B1152 missile that flew here is removed.
        if (!sourceDraws) {
            LOG_INFO(MOD_SPELL, "createCastGlow: caster={} spell={}: Source not drawn (On For {}, density {}, "
                                "opacity {}, ShowSpellEffects {})", caster_id, spell_id, m_fx_options.castFilter,
                     m_fx_options.density, m_fx_options.opacity, m_fx_options.showSpellEffects ? 1 : 0);
            return 1;
        }
        // T441 (board B398): the Source draws its resolved stage's own sprites
        // (and so its own colour); applyStageSpriteNames() drops the tint.
        const std::vector<std::string> stageNames =
            visual.hasSourceStage()
                ? std::vector<std::string>(visual.source_stage.sprite_names.begin(),
                                           visual.source_stage.sprite_names.end())
                : std::vector<std::string>{};
        auto build = [&](float anchorAboveFeet, float share) {
            auto d = Presets::SourceForSpell(c, spell_id, record, spray, anchorAboveFeet, share);
            if (!stageNames.empty()) Presets::applyStageSpriteNames(d, stageNames, spell_id);
            applyStageEddColours(d, visual, 0);  // B1146
            return d;
        };
        glm::vec3 centerPos(0.0f);
        const bool hasCenter = m_particle_manager->resolveEntityPosition(caster_id, centerPos);
        // The followed bone's height above the feet (the entity anchor is A above them).
        auto boneAboveFeet = [&](const glm::vec3& bone) {
            return hasCenter ? bone.y - (centerPos.y - A) : A;
        };
        using Anchor = Cfg::SourceAnchor;
        // B1144 (#464): a spellsnew.eff record's Source is its slots -- one
        // emitter per slot the caster's level passes (eqgame 0x495cc3), on the
        // slot's anchor (the caster: every Source slot is anchor 1 or 0) at its
        // attach point. The row keeps the shape, sizes, onset and hold; each
        // slot draws 1/measured of the row's population and its own emitter's
        // art. A spell whose override sets its own Source spawnShape (B1122's
        // measured 54/656/41/458 clouds; the 253 one was retired by B1161)
        // keeps that geometry on the caster's entity for every slot.
        const StageSlots slots = stageSlots(spell_id, kSpellsNewEffSource, casterLevel);
        // B1145 (#465): each slot's timing is its emitter's (spellsnew.edd
        // +0x78/+0x7C/+0x80..+0x8C) for the duration eqgame passes a Source:
        // the cast time + the cast-animation term (spell_fx_timing.h), from
        // the stage's measured start; the row keeps shape and size only.
        // The cast-animation term was MEASURED as the emission's end after
        // OP_Action (kSpellFxCastAnimationSeconds); fable's emitter starts
        // at the stage's measured latency after OP_BeginCast, so its run is
        // that much shorter for the emission to end where the oracle's did.
        // B1148 (#468): the term is the caster model's CastingAnim track
        // (0x47ddd4), the measured 4.35 s only when that does not resolve.
        const float sourceLatency = cfg.sourceStageLatency();
        const int32_t castAnimMs = castAnimationMs(caster_id, castAnim);
        const int32_t sourceCallerMs = SpellFxCallerDurationMs(
            SpellFxStage::Source, static_cast<int32_t>(duration_ms) - static_cast<int32_t>(sourceLatency * 1000.0f + 0.5f), 0,
            castAnimMs);
        LOG_DEBUG(MOD_SPELL, "createCastGlow: caster={} spell={} castAnim={} track={} ms (term {} ms)", caster_id,
                  spell_id, castAnim, castAnimMs,
                  castAnimMs >= 0 ? castAnimMs : static_cast<int32_t>(kSpellFxCastAnimationSeconds * 1000.0f + 0.5f));
        if (slots.fromFile) {
            if (slots.drawn.empty()) {
                LOG_INFO(MOD_SPELL, "createCastGlow: caster={} spell={} level={}: no Source slot passes "
                                    "the level gate -- nothing drawn", caster_id, spell_id, casterLevel);
                return 1;
            }
            const bool ownShape =
                cfg.hasSourceShapeOverride(spell_id, spray ? "castSpray" : "castGlow");
            std::string made;
            // B1149 (#469): each slot is ONE emitter of the file's own
            // population (spellsnew.edd +0x80/+0x84/+0x88), sprite size
            // (+0xCC/+0x184 x +0x17C/+0x180 x +0x16C) and the client's alpha;
            // the row gives only its shape (B1150's).
            struct Pending { EQT::Graphics::Environment::SpellEffectDef def; uint16_t id; float cap; };
            std::vector<Pending> pending;
            for (const auto& d : slots.drawn) {
                if (d.slot.anchorKind() == SpellsNewEffAnchor::Missile ||
                    d.slot.anchorKind() == SpellsNewEffAnchor::Target)
                    continue;  // no Source slot in the file is either (reader test)
                // World (anchor 0): the caller's fixed position; the caster's
                // own, not attached to a point.
                uint16_t id = (ownShape || d.slot.anchorKind() == SpellsNewEffAnchor::World)
                                  ? caster_id : attachPointEntityId(caster_id, d.slot.attachPoint);
                glm::vec3 bone(0.0f);
                if (id != caster_id && !m_particle_manager->resolveEntityPosition(id, bone)) id = caster_id;
                auto def = Presets::SourceForSpell(c, spell_id, record, spray,
                                                   id == caster_id ? A : boneAboveFeet(bone), 1.0f);
                if (!d.texture.empty()) {
                    Presets::applyStageSpriteNames(def, {d.texture}, spell_id);
                    applySlotEddColours(def, {d.slot.emitter});  // B1146's colour, this slot's emitter
                }
                else if (!stageNames.empty()) {
                    Presets::applyStageSpriteNames(def, stageNames, spell_id);
                    applyStageEddColours(def, visual, 0);  // B1146
                }
                float slotCap = cap;
                SpellFxSlotTiming timing;
                if (slotTiming(d.slot.emitter, sourceCallerMs, timing)) {
                    Presets::applySlotEmitter({&def}, timing, m_spells_new_eff.edd().sprite(d.slot.emitter),
                                              sourceLatency, kSpellFxAttachScale,
                                              m_spells_new_eff.edd().shape(d.slot.emitter),  // B1150
                                              m_spells_new_eff.edd().motion(d.slot.emitter));  // B1151
                    slotCap = sourceLatency + timing.end() + 0.1f;
                }
                pending.push_back({std::move(def), id, slotCap});
                made += fmt::format(" {}:{}@{}+{:.2f}..{:.2f}", d.index, d.slot.attachPoint, id,
                                    sourceLatency + timing.start, sourceLatency + timing.end());
            }
            for (auto& p : pending) m_particle_manager->createSpellEffect(p.def, p.id, 0, p.cap, false);
            LOG_INFO(MOD_SPELL, "createCastGlow: caster={} spell={} anim={} level={} row={} slots{} (of {})",
                     caster_id, spell_id, spellVocabulary(spell_id).spellAnim, casterLevel, record, made,
                     slots.measured);
            return 1;
        }
        const Anchor anchor = Presets::SourceAnchorForSpell(spell_id, record, spray);
        if (anchor == Anchor::Hands) {
            const uint16_t ids[2] = {rightHandEntityId(caster_id), leftHandEntityId(caster_id)};
            int made = 0;
            for (uint16_t id : ids) {
                glm::vec3 hand;
                if (!m_particle_manager->resolveEntityPosition(id, hand)) continue;
                m_particle_manager->createSpellEffect(build(boneAboveFeet(hand), 0.5f), id, 0, cap, false);
                ++made;
            }
            LOG_INFO(MOD_SPELL, "createCastGlow: caster={} spell={} record={} hands ({} bones)",
                     caster_id, spell_id, record, made);
            if (made > 0) return 1;
            // No hand bones (placeholder model, non-humanoid): the same spray from the entity.
            m_particle_manager->createSpellEffect(build(A, 1.0f), caster_id, 0, cap, false);
            return 1;
        }
        if (anchor == Anchor::Head) {
            const uint16_t id = headEntityId(caster_id);
            glm::vec3 head;
            const bool hasHead = m_particle_manager->resolveEntityPosition(id, head);
            LOG_INFO(MOD_SPELL, "createCastGlow: caster={} spell={} record={} head (bone={})",
                     caster_id, spell_id, record, hasHead);
            m_particle_manager->createSpellEffect(build(hasHead ? boneAboveFeet(head) : A, 1.0f),
                                                  hasHead ? id : caster_id, 0, cap, false);
            return 1;
        }
        LOG_INFO(MOD_SPELL, "createCastGlow: caster={} spell={} spray={} record={} anchor={}",
                 caster_id, spell_id, spray, record, static_cast<int>(anchor));
        m_particle_manager->createSpellEffect(build(A, 1.0f), caster_id, 0, cap, false);
        return 1;
    }
#endif
    // B1165 (#484): the same stage-0 gate on the worker / scene-node paths.
    if (!stageDraws(spell_id, 0, casterFx, casterFx)) return 1;
    if (m_worker_driven) {
        // Remove existing cast glow via worker
        if (hasCastGlow(caster_id)) {
            EQT::Graphics::SpellVFXCommandData rmCmd;
            rmCmd.type = EQT::Graphics::SpellVFXCommand::RemoveCastGlow;
            rmCmd.sourceEntity = caster_id;
            m_pending_commands.push_back(rmCmd);
            // Also remove local scene nodes immediately
            for (size_t i = 0; i < m_effects.size(); ) {
                if (m_effects[i].type == SpellFXType::CastGlow &&
                    m_effects[i].source_entity == caster_id) {
                    removeEffect(i);
                } else {
                    ++i;
                }
            }
        }

        auto color = getSpellColor(spell_id);
        uint32_t eid = m_next_effect_id++;
        EQT::Graphics::SpellVFXCommandData cmd;
        cmd.type = EQT::Graphics::SpellVFXCommand::CreateEffect;
        cmd.effectId = eid;
        cmd.fxType = static_cast<uint8_t>(SpellFXType::CastGlow);
        cmd.spellId = spell_id;
        cmd.sourceEntity = caster_id;
        cmd.lifetime = duration_ms / 1000.0f;
        cmd.colorA = color.getAlpha();
        cmd.colorR = color.getRed();
        cmd.colorG = color.getGreen();
        cmd.colorB = color.getBlue();
        // Position resolved by worker from entity data
        m_pending_commands.push_back(cmd);
        return eid;
    }

    // Check if already has a cast glow
    if (hasCastGlow(caster_id)) {
        removeCastGlow(caster_id);
    }

    SpellFXInstance effect;
    effect.type = SpellFXType::CastGlow;
    effect.spell_id = spell_id;
    effect.source_entity = caster_id;
    effect.color = getSpellColor(spell_id);
    effect.lifetime = duration_ms / 1000.0f;
    effect.active = true;

    // Get caster position
    if (m_entity_pos_callback) {
        m_entity_pos_callback(caster_id, effect.source_pos);
    }

    createGlowNode(effect);
    m_effects.push_back(effect);

    LOG_TRACE(MOD_SPELL, "Created cast glow for entity {} spell {}", caster_id, spell_id);
    return m_next_effect_id++;
}

// B1152 (#472): createProjectile() is REMOVED, and B1170 (#489) removed the
// record-driven missile that replaced it: a Middle stage is drawn only on a
// server 0x4033 missile object (refactor/B1170/README.md).

void SpellVisualFX::createImpact(uint16_t target_id, uint32_t spell_id)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        // T72: spells.eff's Target emitter maps onto ImpactBurst. Same
        // fallback-preserving rule as createCastGlow/createProjectile.
        SpellVisualResolution visual = resolveVisual(spell_id);
        if (!visual.isFallback() && !visual.hasTargetStage()) {
            LOG_DEBUG(MOD_SPELL, "T72: spell {} spells.eff record {} has no Target-stage content "
                                  "— suppressing impact burst (data-driven, not a bug)",
                      spell_id, visual.spell_affect_index);
            return;
        }

        auto scolor = getSpellColor(spell_id);
        glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f,
                     scolor.getBlue() / 255.0f, 1.0f);
        auto def = EQT::Graphics::Environment::SpellPresets::Impact(c, spell_id, sourceRecordOf(spell_id));
        // T389 (board B162, T380 Leg B2): the impact burst draws the tiles
        // the spell's spells.eff Target stage names, not the style default.
        // Frost Bolt (54) names GEND10_SPRITE + GENU10_SPRITE x2 there.
        if (visual.hasTargetStage()) {
            EQT::Graphics::Environment::SpellPresets::applyStageSpriteNames(
                def,
                std::vector<std::string>(visual.target_stage.sprite_names.begin(),
                                         visual.target_stage.sprite_names.end()),
                spell_id);
        }
        applyStageEddColours(def, visual, 2);  // B1146
        m_particle_manager->createSpellEffect(def, 0, target_id);
        return;
    }
#endif
    // Impacts are fire-and-forget (Irrlicht particle system drives them)
    // No worker tracking needed — create directly even in worker-driven mode
    SpellFXInstance effect;
    effect.type = SpellFXType::ImpactBurst;
    effect.spell_id = spell_id;
    effect.color = getSpellColor(spell_id);
    effect.lifetime = DEFAULT_IMPACT_DURATION;
    effect.active = true;

    if (m_entity_pos_callback) {
        m_entity_pos_callback(target_id, effect.target_pos);
    }

    createImpactNode(effect);
    m_effects.push_back(effect);

    LOG_TRACE(MOD_SPELL, "Created impact on entity {} spell {}", target_id, spell_id);
}

uint16_t SpellVisualFX::createLanding(uint16_t caster_id, uint16_t target_id, uint32_t spell_id,
                                      int casterLevel, const SpellFxActor& casterFx,
                                      const SpellFxActor& targetFx)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        // B1165 (#484): stage 2 on (caster, target), eqgame.exe.c:101960.
        if (!stageDraws(spell_id, 2, casterFx, targetFx)) {
            LOG_INFO(MOD_SPELL, "createLanding: caster={} spell={}: Target stage not drawn (On For {})",
                     caster_id, spell_id, m_fx_options.castFilter);
            return 0;
        }
        namespace Presets = EQT::Graphics::Environment::SpellPresets;
        using Cfg = EQT::Graphics::Environment::SpellEffectsConfig;
        using Anchor = Cfg::TargetAnchor;
        const int record = sourceRecordOf(spell_id);
        // B1144 (#464): a spellsnew.eff record lands as its Target slots.
        const StageSlots slots = stageSlots(spell_id, kSpellsNewEffTarget, casterLevel);
        if (slots.fromFile) return createLandingFromSlots(caster_id, target_id, spell_id, record, slots);
        const Anchor anchor = Presets::TargetAnchorForSpell(spell_id, record);
        const uint16_t entity = (anchor == Anchor::Caster || target_id == 0) ? caster_id : target_id;
        const auto& cfg = Cfg::instance();
        const float A = cfg.sourceScale().anchorAboveFeet;
        uint16_t follow = entity;
        float anchorAboveFeet = A;
        if (anchor == Anchor::Head) {
            // The target's head bone (the pseudo-entity B1131 made for the
            // caster's halo resolves any animated entity's head).
            glm::vec3 center(0.0f), head(0.0f);
            if (m_particle_manager->resolveEntityPosition(headEntityId(entity), head) &&
                m_particle_manager->resolveEntityPosition(entity, center)) {
                follow = headEntityId(entity);
                anchorAboveFeet = head.y - (center.y - A);
            }
        }
        auto scolor = getSpellColor(spell_id);
        glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f,
                     scolor.getBlue() / 255.0f, 1.0f);
        SpellVisualResolution visual = resolveVisual(spell_id);
        // spellComplete half: no stage gate (T441), the Source stage's art.
        auto complete = Presets::TargetForSpell(c, spell_id, record, true, anchorAboveFeet);
        if (visual.hasSourceStage())
            Presets::applyStageSpriteNames(
                complete,
                std::vector<std::string>(visual.source_stage.sprite_names.begin(),
                                         visual.source_stage.sprite_names.end()),
                spell_id);
        applyStageEddColours(complete, visual, 0);  // B1146
        m_particle_manager->createSpellEffect(complete, follow, 0);
        // impact half: T72's Target-stage gate, the Target stage's art (T389).
        if (visual.isFallback() || visual.hasTargetStage()) {
            auto impact = Presets::TargetForSpell(c, spell_id, record, false, anchorAboveFeet);
            if (visual.hasTargetStage())
                Presets::applyStageSpriteNames(
                    impact,
                    std::vector<std::string>(visual.target_stage.sprite_names.begin(),
                                             visual.target_stage.sprite_names.end()),
                    spell_id);
            applyStageEddColours(impact, visual, 2);  // B1146
            m_particle_manager->createSpellEffect(impact, 0, follow);
        }
        LOG_INFO(MOD_SPELL, "createLanding: caster={} target={} spell={} record={} anchor={} on={}",
                 caster_id, target_id, spell_id, record, static_cast<int>(anchor), follow);
        return follow;
    }
#endif
    // B1165 (#484): the same stage-2 gate on the scene-node path.
    if (!stageDraws(spell_id, 2, casterFx, targetFx)) return 0;
    (void)casterLevel;
    // The legacy scene-node path: no per-record rows, the landing on the target.
    const uint16_t entity = target_id != 0 ? target_id : caster_id;
    createSpellComplete(entity, spell_id);
    createImpact(entity, spell_id);
    return entity;
}

#ifdef EQT_HAS_GLES2
// B1144 (#464): the landing of a spellsnew.eff record. The client plays the
// record's Target stage (eqgame 0x47f5e1 / 0x45448b) slot by slot (0x495cc3):
// each slot whose minLevel the caster's level passes puts its emitter on its
// anchor actor -- 3 the target, 1 the caster, 0 a fixed position (the
// landing entity's, unattached) -- at its attach point. A stage with no slot
// drawn draws nothing: no Target emitters = no landing (record 326, B1134's
// /disc). fable draws each slot as one pair of its two landing halves
// (B1120's spellComplete + impact volumes, the row's B1132 shape class,
// sizes, lifetimes and onset), each carrying 1/measured of the row's
// population; an override's own landing geometry (B1120/B1122) stays on the
// landing entity. Returns the point the first slot follows (0 when nothing is
// drawn).
uint16_t SpellVisualFX::createLandingFromSlots(uint16_t caster_id, uint16_t target_id, uint32_t spell_id,
                                               int record, const StageSlots& slots, const StageArgs* args)
{
    namespace Presets = EQT::Graphics::Environment::SpellPresets;
    using Cfg = EQT::Graphics::Environment::SpellEffectsConfig;
    if (slots.drawn.empty()) {
        LOG_INFO(MOD_SPELL, "createLanding: caster={} target={} spell={}: no Target slot drawn -- no landing",
                 caster_id, target_id, spell_id);
        return 0;
    }
    const auto& cfg = Cfg::instance();
    const float A = cfg.sourceScale().anchorAboveFeet;
    const bool ownShape = Presets::TargetHasOverrideShape(spell_id);
    const float targetLatency = cfg.targetStageLatency();
    auto scolor = getSpellColor(spell_id);
    const glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f, scolor.getBlue() / 255.0f, 1.0f);
    uint16_t first = 0;
    std::string made;
    struct Pending { EQT::Graphics::Environment::SpellEffectDef def; uint16_t caster; uint16_t target; };
    std::vector<Pending> pending;
    for (const auto& d : slots.drawn) {
        const auto anchor = d.slot.anchorKind();
        uint16_t entity = 0;
        uint16_t follow = 0;
        if (args) {
            // B1170 (#489): an explicit FUN_0049b260 call's arguments
            // (00495d4c .. 00495dc3): a slot whose actor argument is 0 is skipped.
            switch (anchor) {
                case SpellsNewEffAnchor::World:   entity = args->world; break;
                case SpellsNewEffAnchor::Caster:  entity = args->caster; break;
                case SpellsNewEffAnchor::Missile: entity = args->missile; break;
                case SpellsNewEffAnchor::Target:  entity = args->target; break;
            }
            if (entity == 0) continue;
            follow = (ownShape || anchor == SpellsNewEffAnchor::World || anchor == SpellsNewEffAnchor::Missile)
                         ? entity : attachPointEntityId(entity, d.slot.attachPoint);
        } else {
            // B1170 (#489): an anchor-2 (missile) Target slot is drawn by the
            // client only with a missile object (00495d8e / 00495d93); on
            // OP_Action there is none, so it is skipped (no Target slot in the
            // shipped file is anchor 2:
            // SpellsNewEffFileTest.EveryRecordsSlotWordsAreInTheDecodedRanges).
            if (anchor == SpellsNewEffAnchor::Missile) continue;
            entity = (anchor == SpellsNewEffAnchor::Caster || target_id == 0) ? caster_id : target_id;
            follow = (ownShape || anchor == SpellsNewEffAnchor::World)
                         ? entity : attachPointEntityId(entity, d.slot.attachPoint);
        }
        float aboveFeet = A;
        if (follow != entity) {
            glm::vec3 center(0.0f), bone(0.0f);
            if (m_particle_manager->resolveEntityPosition(follow, bone) &&
                m_particle_manager->resolveEntityPosition(entity, center))
                aboveFeet = bone.y - (center.y - A);
            else
                follow = entity;
        }
        const bool onHead = isHeadBoneId(follow);
        auto complete = Presets::TargetForSpell(c, spell_id, record, true, aboveFeet, 1.0f, onHead);
        auto impact = Presets::TargetForSpell(c, spell_id, record, false, aboveFeet, 1.0f, onHead);
        // Art: B1149 -- both halves are the slot's ONE emitter, so both draw
        // its own texture and colour words (B1144 gave the spellComplete half
        // the Source slots' art: B1123's populations were calibrated on it).
        if (!d.texture.empty()) {
            for (auto* half : {&complete, &impact}) {
                Presets::applyStageSpriteNames(*half, {d.texture}, spell_id);
                applySlotEddColours(*half, {d.slot.emitter});  // B1146, on the client's alpha
            }
        }
        // B1145 (#465): the slot's timing from its emitter; eqgame passes a
        // Target stage no duration (0x47f5e1), so +0x78 alone sets the run.
        // B1149 (#469): the slot's file population is shared by the two
        // halves (the row's measured geometry split), with its sprite size.
        SpellFxSlotTiming timing;
        const int32_t callerMs = args ? args->callerMs : SpellFxCallerDurationMs(SpellFxStage::Target, 0, 0);
        if (slotTiming(d.slot.emitter, callerMs, timing))
            Presets::applySlotEmitter({&complete, &impact}, timing, m_spells_new_eff.edd().sprite(d.slot.emitter),
                                      targetLatency, kSpellFxAttachScale,
                                      m_spells_new_eff.edd().shape(d.slot.emitter),  // B1150
                                      m_spells_new_eff.edd().motion(d.slot.emitter));  // B1151
        pending.push_back({std::move(complete), follow, 0});
        pending.push_back({std::move(impact), 0, follow});
        if (first == 0) first = follow;
        made += fmt::format(" {}:{}/{}@{}+{:.2f}..{:.2f}", d.index, static_cast<int>(anchor), d.slot.attachPoint,
                            follow, targetLatency + timing.start, targetLatency + timing.end());
    }
    for (auto& p : pending) m_particle_manager->createSpellEffect(p.def, p.caster, p.target);
    LOG_INFO(MOD_SPELL, "createLanding: caster={} target={} spell={} row={} slots{} (of {})",
             caster_id, target_id, spell_id, record, made, slots.measured);
    return first;
}
#endif

void SpellVisualFX::createSpellComplete(uint16_t caster_id, uint32_t spell_id)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        auto scolor = getSpellColor(spell_id);
        glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f,
                     scolor.getBlue() / 255.0f, 1.0f);
        auto def = EQT::Graphics::Environment::SpellPresets::SpellComplete(c, spell_id, sourceRecordOf(spell_id));
        // T441 (board B398). spellComplete is the caster-side half of a
        // landing, so it takes the Source stage's sprites -- and with them
        // its colour. NO suppression gate is added here: unlike the cast
        // glow, this burst has never been staged, and adding a gate would
        // silently remove it for every spell whose record has an empty
        // Source block. Only the art (and hence the colour) changes.
        SpellVisualResolution visual = resolveVisual(spell_id);
        if (visual.hasSourceStage()) {
            EQT::Graphics::Environment::SpellPresets::applyStageSpriteNames(
                def,
                std::vector<std::string>(visual.source_stage.sprite_names.begin(),
                                         visual.source_stage.sprite_names.end()),
                spell_id);
        }
        applyStageEddColours(def, visual, 0);  // B1146
        m_particle_manager->createSpellEffect(def, caster_id, 0);
        return;
    }
#endif
    if (!m_smgr) return;
    if (m_particle_multiplier <= 0.0f) return;  // Particles disabled

    // Get caster position
    irr::core::vector3df caster_pos;
    if (m_entity_pos_callback) {
        if (!m_entity_pos_callback(caster_id, caster_pos)) {
            return;  // Entity not found
        }
    }

    // Create a spell completion burst effect - particles spiral outward and upward
    SpellFXInstance effect;
    effect.type = SpellFXType::ImpactBurst;  // Use impact burst type for the particle system
    effect.spell_id = spell_id;
    effect.source_entity = caster_id;
    effect.target_pos = caster_pos + irr::core::vector3df(0, 3.0f, 0);  // Slightly above entity center
    effect.color = getSpellColor(spell_id);
    effect.lifetime = 1.0f;  // 1 second burst effect
    effect.active = true;

    // Create a particle system for the spell completion burst
    effect.particle_system = m_smgr->addParticleSystemSceneNode(false, nullptr, -1,
        effect.target_pos);

    if (effect.particle_system) {
        // Apply particle multiplier to emission rates
        int minPPS = static_cast<int>(50 * m_particle_multiplier);
        int maxPPS = static_cast<int>(80 * m_particle_multiplier);

        // Create ring emitter for spiraling outward burst
        irr::scene::IParticleEmitter* emitter = effect.particle_system->createRingEmitter(
            irr::core::vector3df(0, 0, 0),              // Center
            0.5f, 2.0f,                                  // Ring radius min/max (start small)
            irr::core::vector3df(0, 0.05f, 0),          // Direction (upward spiral)
            minPPS, maxPPS,                              // Particles per second
            effect.color,                                // Min color
            effect.color,                                // Max color
            200, 500,                                    // Min/max lifetime ms
            60,                                          // Max angle (spread outward)
            irr::core::dimension2df(1.5f, 1.5f),        // Min size
            irr::core::dimension2df(3.0f, 3.0f)         // Max size
        );

        effect.particle_system->setEmitter(emitter);
        emitter->drop();

        // Add fade out affector for smooth fade
        irr::scene::IParticleAffector* fadeAffector =
            effect.particle_system->createFadeOutParticleAffector();
        effect.particle_system->addAffector(fadeAffector);
        fadeAffector->drop();

        // Add attraction affector to create outward movement
        irr::scene::IParticleAffector* attractAffector =
            effect.particle_system->createAttractionAffector(
                effect.target_pos + irr::core::vector3df(0, 10.0f, 0),  // Attract upward
                -20.0f);  // Negative = repel/push outward and up
        effect.particle_system->addAffector(attractAffector);
        attractAffector->drop();

        // Set particle texture based on spell type
        irr::video::ITexture* tex = getTextureForSpell(effect.spell_id);
        if (tex) {
            effect.particle_system->setMaterialTexture(0, tex);
        }

        effect.particle_system->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        effect.particle_system->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);
    }

    m_effects.push_back(effect);

    LOG_TRACE(MOD_SPELL, "Created spell complete effect for entity {} spell {}", caster_id, spell_id);
}

} // namespace EQ
