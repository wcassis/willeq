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
    const int32_t idx = spellVocabulary(spell_id).spellAffectIndex;
    return idx >= 0 ? static_cast<int>(idx) : -1;
}

SpellVisualResolution SpellVisualFX::resolveVisual(uint32_t spell_id) const
{
    static_assert(kNoSpellAffectIndex == kSpellsEffNoEffectIndex,
                  "spell_visual_vocabulary.h's sentinel must equal the reader's");

    // T381 (B160): the index comes from the event payload the game thread
    // filled, not from a SpellDatabase pointer the renderer never had.
    const int32_t affect_index = spellVocabulary(spell_id).spellAffectIndex;

    SpellVisualSelector selector(m_spells_eff_loaded ? &m_spells_eff : nullptr);
    SpellVisualResolution res = selector.resolve(affect_index);

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

void SpellVisualFX::update(float delta_time)
{
    // Worker-driven path: update math runs on SimulationWorker thread
    if (m_worker_driven) return;

    // Collect pending impacts to create after the update loop
    // (to avoid vector invalidation when createImpact adds to m_effects)
    std::vector<std::pair<uint16_t, uint32_t>> pendingImpacts;

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

        // Type-specific updates
        switch (effect.type) {
            case SpellFXType::ProjectileTravel:
                updateProjectile(effect, delta_time, pendingImpacts);
                break;
            case SpellFXType::CastGlow:
                updateGlow(effect, delta_time);
                break;
            case SpellFXType::AuraPersistent:
                updateAura(effect, delta_time);
                break;
            default:
                break;
        }

        ++i;
    }

    // Create pending impacts now that we're done iterating
    for (const auto& impact : pendingImpacts) {
        createImpact(impact.first, impact.second);
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

uint32_t SpellVisualFX::createCastGlow(uint16_t caster_id, uint32_t spell_id, uint32_t duration_ms)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
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
        // B1131 (#451): the Source is drawn per spells.eff record, as the
        // official client draws it (B1130 survey, config `sourceRecords`):
        // hands / feet-ring / orbit / body / head, with the record's onset.
        // A spell whose override gives its Source its own geometry (B1122's
        // 54/656/41/253/458) keeps it on the caster's entity, at its own rate.
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
            return d;
        };
        glm::vec3 centerPos(0.0f);
        const bool hasCenter = m_particle_manager->resolveEntityPosition(caster_id, centerPos);
        // The followed bone's height above the feet (the entity anchor is A above them).
        auto boneAboveFeet = [&](const glm::vec3& bone) {
            return hasCenter ? bone.y - (centerPos.y - A) : A;
        };
        using Anchor = Cfg::SourceAnchor;
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

void SpellVisualFX::createProjectile(uint16_t caster_id, uint16_t target_id, uint32_t spell_id)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        // T72: spells.eff's middle/travel emitter maps onto ProjectileTravel.
        // Same fallback-preserving rule as createCastGlow: only a Resolved
        // record with a provably-absent Middle stage suppresses this.
        SpellVisualResolution visual = resolveVisual(spell_id);
        if (!visual.isFallback() && !visual.hasTravelStage()) {
            LOG_DEBUG(MOD_SPELL, "T72: spell {} spells.eff record {} has no Middle-stage content "
                                  "— suppressing projectile travel (data-driven, not a bug)",
                      spell_id, visual.spell_affect_index);
            return;
        }

        auto scolor = getSpellColor(spell_id);
        glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f,
                     scolor.getBlue() / 255.0f, 1.0f);
        auto def = EQT::Graphics::Environment::SpellPresets::Projectile(c, spell_id);
        // T441 (board B398): the travel trail draws the Middle stage's own
        // sprites, and therefore its own colour. Same rule as the Source and
        // Target stages.
        if (visual.hasTravelStage()) {
            EQT::Graphics::Environment::SpellPresets::applyStageSpriteNames(
                def,
                std::vector<std::string>(visual.middle_stage.sprite_names.begin(),
                                         visual.middle_stage.sprite_names.end()),
                spell_id);
        }

        // Compute travel duration from distance
        irr::core::vector3df srcPos, tgtPos;
        if (m_entity_pos_callback) {
            m_entity_pos_callback(caster_id, srcPos);
            m_entity_pos_callback(target_id, tgtPos);
        }
        float dist = (tgtPos - srcPos).getLength();
        float travelDuration = std::max(0.1f, dist / PROJECTILE_SPEED);

        // Set trail emitter lifetime to match travel duration
        if (!def.emitters.empty()) {
            def.emitters[0].config.emitterLifetime = travelDuration;
        }

        m_particle_manager->createSpellEffect(
            def, caster_id, target_id, travelDuration + 1.0f,
            false, travelDuration);
        return;
    }
#endif
    if (m_worker_driven) {
        auto color = getSpellColor(spell_id);
        uint32_t eid = m_next_effect_id++;
        EQT::Graphics::SpellVFXCommandData cmd;
        cmd.type = EQT::Graphics::SpellVFXCommand::CreateEffect;
        cmd.effectId = eid;
        cmd.fxType = static_cast<uint8_t>(SpellFXType::ProjectileTravel);
        cmd.spellId = spell_id;
        cmd.sourceEntity = caster_id;
        cmd.targetEntity = target_id;
        cmd.lifetime = DEFAULT_PROJECTILE_DURATION;
        cmd.colorA = color.getAlpha();
        cmd.colorR = color.getRed();
        cmd.colorG = color.getGreen();
        cmd.colorB = color.getBlue();
        // Positions resolved by worker from entity data
        m_pending_commands.push_back(cmd);
        return;
    }

    SpellFXInstance effect;
    effect.type = SpellFXType::ProjectileTravel;
    effect.spell_id = spell_id;
    effect.source_entity = caster_id;
    effect.target_entity = target_id;
    effect.color = getSpellColor(spell_id);
    effect.lifetime = DEFAULT_PROJECTILE_DURATION;
    effect.active = true;

    // Get positions
    if (m_entity_pos_callback) {
        m_entity_pos_callback(caster_id, effect.source_pos);
        m_entity_pos_callback(target_id, effect.target_pos);
    }

    createProjectileNode(effect);
    m_effects.push_back(effect);

    LOG_TRACE(MOD_SPELL, "Created projectile from {} to {} spell {}",
              caster_id, target_id, spell_id);
}

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
        m_particle_manager->createSpellEffect(def, 0, target_id);
        return;
    }
#endif
    // Impacts are fire-and-forget (Irrlicht particle system drives them)
    // No worker tracking needed — create directly even in worker-driven mode
    SpellFXInstance effect;
    effect.type = SpellFXType::ImpactBurst;
    effect.spell_id = spell_id;
    effect.target_entity = target_id;
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

void SpellVisualFX::createImpactAtPosition(const irr::core::vector3df& pos, uint32_t spell_id)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        // T72: same Target-stage gate as createImpact() above.
        SpellVisualResolution visual = resolveVisual(spell_id);
        if (!visual.isFallback() && !visual.hasTargetStage()) {
            LOG_DEBUG(MOD_SPELL, "T72: spell {} spells.eff record {} has no Target-stage content "
                                  "— suppressing impact burst at position (data-driven, not a bug)",
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
        glm::vec3 worldPos(pos.X, pos.Y, pos.Z);
        m_particle_manager->createSpellEffectAtPosition(def, worldPos);
        return;
    }
#endif
    SpellFXInstance effect;
    effect.type = SpellFXType::ImpactBurst;
    effect.spell_id = spell_id;
    effect.target_pos = pos;
    effect.color = getSpellColor(spell_id);
    effect.lifetime = DEFAULT_IMPACT_DURATION;
    effect.active = true;

    createImpactNode(effect);
    m_effects.push_back(effect);
}

uint16_t SpellVisualFX::createLanding(uint16_t caster_id, uint16_t target_id, uint32_t spell_id)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        namespace Presets = EQT::Graphics::Environment::SpellPresets;
        using Cfg = EQT::Graphics::Environment::SpellEffectsConfig;
        using Anchor = Cfg::TargetAnchor;
        const int record = sourceRecordOf(spell_id);
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
            m_particle_manager->createSpellEffect(impact, 0, follow);
        }
        LOG_INFO(MOD_SPELL, "createLanding: caster={} target={} spell={} record={} anchor={} on={}",
                 caster_id, target_id, spell_id, record, static_cast<int>(anchor), follow);
        return follow;
    }
#endif
    // The legacy scene-node path: no per-record rows, the landing on the target.
    const uint16_t entity = target_id != 0 ? target_id : caster_id;
    createSpellComplete(entity, spell_id);
    createImpact(entity, spell_id);
    return entity;
}

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

uint32_t SpellVisualFX::createBuffAura(uint16_t entity_id, uint32_t spell_id)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        auto scolor = getSpellColor(spell_id);
        glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f,
                     scolor.getBlue() / 255.0f, 1.0f);
        auto def = EQT::Graphics::Environment::SpellPresets::BuffAura(c, spell_id);
        // Permanent — no duration, entity-attached
        return m_particle_manager->createSpellEffect(def, entity_id, 0);
    }
#endif
    if (m_worker_driven) {
        if (hasBuffAura(entity_id, spell_id)) return 0;

        auto color = getSpellColor(spell_id);
        uint32_t eid = m_next_effect_id++;
        EQT::Graphics::SpellVFXCommandData cmd;
        cmd.type = EQT::Graphics::SpellVFXCommand::CreateEffect;
        cmd.effectId = eid;
        cmd.fxType = static_cast<uint8_t>(SpellFXType::AuraPersistent);
        cmd.spellId = spell_id;
        cmd.sourceEntity = entity_id;
        cmd.lifetime = 0;  // Permanent
        cmd.colorA = color.getAlpha();
        cmd.colorR = color.getRed();
        cmd.colorG = color.getGreen();
        cmd.colorB = color.getBlue();
        m_pending_commands.push_back(cmd);
        return eid;
    }

    // Don't create duplicate auras
    if (hasBuffAura(entity_id, spell_id)) {
        return 0;
    }

    SpellFXInstance effect;
    effect.type = SpellFXType::AuraPersistent;
    effect.spell_id = spell_id;
    effect.source_entity = entity_id;
    effect.color = getSpellColor(spell_id);
    effect.lifetime = 0;  // Permanent until removed
    effect.active = true;

    if (m_entity_pos_callback) {
        m_entity_pos_callback(entity_id, effect.source_pos);
    }

    createAuraNode(effect);
    m_effects.push_back(effect);

    LOG_TRACE(MOD_SPELL, "Created buff aura for entity {} spell {}", entity_id, spell_id);
    return m_next_effect_id++;
}

void SpellVisualFX::createRainEffect(const irr::core::vector3df& center, float radius,
                                     uint32_t spell_id, float duration)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        auto scolor = getSpellColor(spell_id);
        glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f,
                     scolor.getBlue() / 255.0f, 1.0f);
        auto def = EQT::Graphics::Environment::SpellPresets::SpellRain(c, radius, spell_id);
        for (auto& e : def.emitters) e.config.emitterLifetime = duration;
        glm::vec3 worldPos(center.X, center.Y, center.Z);
        m_particle_manager->createSpellEffectAtPosition(def, worldPos, duration);
        return;
    }
#endif
    // Rain effects are fire-and-forget (Irrlicht particle system drives them)
    // Create directly even in worker-driven mode
    SpellFXInstance effect;
    effect.type = SpellFXType::RainEffect;
    effect.spell_id = spell_id;
    effect.target_pos = center;
    effect.scale = radius;
    effect.color = getSpellColor(spell_id);
    effect.lifetime = duration;
    effect.active = true;

    createRainNode(effect);
    m_effects.push_back(effect);
}

void SpellVisualFX::createGroundCircle(const irr::core::vector3df& center, float radius,
                                       uint32_t spell_id, float duration)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        auto scolor = getSpellColor(spell_id);
        glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f,
                     scolor.getBlue() / 255.0f, 1.0f);
        auto def = EQT::Graphics::Environment::SpellPresets::GroundCircle(c, radius, spell_id);
        for (auto& e : def.emitters) e.config.emitterLifetime = duration;
        glm::vec3 worldPos(center.X, center.Y, center.Z);
        m_particle_manager->createSpellEffectAtPosition(def, worldPos, duration);
        return;
    }
#endif
    // Ground circles are fire-and-forget (Irrlicht particle system drives them)
    // Create directly even in worker-driven mode
    SpellFXInstance effect;
    effect.type = SpellFXType::GroundCircle;
    effect.spell_id = spell_id;
    effect.target_pos = center;
    effect.scale = radius;
    effect.color = getSpellColor(spell_id);
    effect.lifetime = duration;
    effect.active = true;

    createGroundCircleNode(effect);
    m_effects.push_back(effect);
}

} // namespace EQ
