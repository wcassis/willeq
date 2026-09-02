/*
 * WillEQ Spell Visual Effects Implementation -- scene nodes
 *
 * Effect removal, spell colour lookup, the scene-node builders for glow,
 * impact, projectile and aura effects, and the simulation-worker result
 * path including evicted-texture swapping.
 *
 * Split from a single 1,517-line translation unit by
 * refactor/J34_spell_visual_fx_split.md (move-only, verbatim relocation).
 * The effect-creation half remains beside this file in spell_visual_fx.cpp.
 */

#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/spell/spell_database.h"
#include "client/spell/spell_constants.h"
#include "common/logging.h"
#ifdef EQT_HAS_GLES2
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_particle_types.h"
#endif
#include <algorithm>
#include <fstream>

namespace EQ {
// ============================================================================
// Effect Removal
// ============================================================================

void SpellVisualFX::removeCastGlow(uint16_t caster_id)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        m_particle_manager->removeSpellEffectsForEntity(caster_id);
        m_particle_manager->removeSpellEffectsForEntity(rightHandEntityId(caster_id));
        m_particle_manager->removeSpellEffectsForEntity(leftHandEntityId(caster_id));
        return;
    }
#endif
    if (m_worker_driven) {
        EQT::Graphics::SpellVFXCommandData cmd;
        cmd.type = EQT::Graphics::SpellVFXCommand::RemoveCastGlow;
        cmd.sourceEntity = caster_id;
        m_pending_commands.push_back(cmd);
    }
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

void SpellVisualFX::removeBuffAura(uint16_t entity_id, uint32_t spell_id)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        m_particle_manager->removeSpellEffectsForEntity(entity_id);
        return;
    }
#endif
    if (m_worker_driven) {
        EQT::Graphics::SpellVFXCommandData cmd;
        cmd.type = EQT::Graphics::SpellVFXCommand::RemoveBuffAura;
        cmd.sourceEntity = entity_id;
        cmd.spellId = spell_id;
        m_pending_commands.push_back(cmd);
    }
    // Also remove local scene nodes immediately
    for (size_t i = 0; i < m_effects.size(); ) {
        if (m_effects[i].type == SpellFXType::AuraPersistent &&
            m_effects[i].source_entity == entity_id &&
            m_effects[i].spell_id == spell_id) {
            removeEffect(i);
        } else {
            ++i;
        }
    }
}

void SpellVisualFX::removeAllForEntity(uint16_t entity_id)
{
#ifdef EQT_HAS_GLES2
    if (m_particle_manager) {
        m_particle_manager->removeSpellEffectsForEntity(entity_id);
        m_particle_manager->removeSpellEffectsForEntity(rightHandEntityId(entity_id));
        m_particle_manager->removeSpellEffectsForEntity(leftHandEntityId(entity_id));
        // Fall through to also clean up any Irrlicht scene nodes
    }
#endif
    if (m_worker_driven) {
        EQT::Graphics::SpellVFXCommandData cmd;
        cmd.type = EQT::Graphics::SpellVFXCommand::RemoveAllForEntity;
        cmd.sourceEntity = entity_id;
        m_pending_commands.push_back(cmd);
    }
    // Also remove local scene nodes immediately
    for (size_t i = 0; i < m_effects.size(); ) {
        if (m_effects[i].source_entity == entity_id ||
            m_effects[i].target_entity == entity_id) {
            removeEffect(i);
        } else {
            ++i;
        }
    }
}

void SpellVisualFX::clearAllEffects()
{
    if (m_worker_driven) {
        EQT::Graphics::SpellVFXCommandData cmd;
        cmd.type = EQT::Graphics::SpellVFXCommand::ClearAll;
        m_pending_commands.push_back(cmd);
    }
    for (auto& effect : m_effects) {
        cleanupEffectNode(effect);
    }
    m_effects.clear();
}

// ============================================================================
// Queries
// ============================================================================

bool SpellVisualFX::hasCastGlow(uint16_t caster_id) const
{
    for (const auto& effect : m_effects) {
        if (effect.type == SpellFXType::CastGlow &&
            effect.source_entity == caster_id &&
            effect.active) {
            return true;
        }
    }
    return false;
}

bool SpellVisualFX::hasBuffAura(uint16_t entity_id, uint32_t spell_id) const
{
    for (const auto& effect : m_effects) {
        if (effect.type == SpellFXType::AuraPersistent &&
            effect.source_entity == entity_id &&
            effect.spell_id == spell_id &&
            effect.active) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Private Helpers
// ============================================================================

irr::video::SColor SpellVisualFX::getSpellColor(uint32_t spell_id) const
{
    if (m_spell_db) {
        const SpellData* spell = m_spell_db->getSpell(spell_id);
        if (spell) {
            return getSpellColor(spell);
        }
    }
    // Default: white/magical blue
    return irr::video::SColor(255, 150, 180, 255);
}

irr::video::SColor SpellVisualFX::getSpellColor(const SpellData* spell) const
{
    if (!spell) {
        return irr::video::SColor(255, 150, 180, 255);
    }

    // Color based on resist type
    switch (spell->resist_type) {
        case ResistType::Fire:
            return irr::video::SColor(255, 255, 120, 50);   // Orange/red
        case ResistType::Cold:
            return irr::video::SColor(255, 100, 180, 255);  // Light blue
        case ResistType::Poison:
            return irr::video::SColor(255, 80, 200, 80);    // Green
        case ResistType::Disease:
            return irr::video::SColor(255, 150, 100, 50);   // Brown/sickly
        case ResistType::Magic:
            return irr::video::SColor(255, 200, 100, 255);  // Purple
        case ResistType::Chromatic:
            return irr::video::SColor(255, 255, 255, 255);  // White/rainbow
        default:
            break;
    }

    // Color based on spell school
    switch (spell->getSchool()) {
        case SpellSchool::Abjuration:
            return irr::video::SColor(255, 255, 255, 200);  // White/gold
        case SpellSchool::Alteration:
            return irr::video::SColor(255, 100, 255, 150);  // Green/teal
        case SpellSchool::Conjuration:
            return irr::video::SColor(255, 150, 100, 200);  // Purple
        case SpellSchool::Divination:
            return irr::video::SColor(255, 200, 200, 255);  // Light purple/white
        case SpellSchool::Evocation:
            return irr::video::SColor(255, 255, 150, 100);  // Orange/yellow
        default:
            break;
    }

    return irr::video::SColor(255, 150, 180, 255);
}

void SpellVisualFX::removeEffect(size_t index)
{
    if (index >= m_effects.size()) return;

    cleanupEffectNode(m_effects[index]);

    // Swap with last and pop for O(1) removal
    if (index != m_effects.size() - 1) {
        m_effects[index] = std::move(m_effects.back());
    }
    m_effects.pop_back();
}

void SpellVisualFX::cleanupEffectNode(SpellFXInstance& effect)
{
    if (effect.particle_system) {
        effect.particle_system->remove();
        effect.particle_system = nullptr;
    }
    if (effect.billboard) {
        effect.billboard->remove();
        effect.billboard = nullptr;
    }
    if (effect.scene_node) {
        effect.scene_node->remove();
        effect.scene_node = nullptr;
    }
}

// ============================================================================
// Node Creation
// ============================================================================

void SpellVisualFX::createGlowNode(SpellFXInstance& effect)
{
    if (!m_smgr) return;

    // Create a pulsing billboard glow effect at entity center
    effect.billboard = m_smgr->addBillboardSceneNode(nullptr,
        irr::core::dimension2df(12.0f, 12.0f),
        effect.source_pos + irr::core::vector3df(0, 3.0f, 0));  // Offset up to entity center

    if (effect.billboard) {
        // Semi-transparent glow
        irr::video::SColor glowColor = effect.color;
        glowColor.setAlpha(120);
        effect.billboard->setColor(glowColor);
        effect.billboard->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        effect.billboard->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        // Set particle texture for better appearance
        irr::video::ITexture* tex = getTextureForSpell(effect.spell_id);
        if (tex) {
            effect.billboard->setMaterialTexture(0, tex);
        }
    }

    // Also create spiraling particles around the caster during casting
    if (m_particle_multiplier > 0.0f) {
        effect.particle_system = m_smgr->addParticleSystemSceneNode(false, nullptr, -1,
            effect.source_pos + irr::core::vector3df(0, 1.0f, 0));

        if (effect.particle_system) {
            // Apply particle multiplier to emission rates
            int minPPS = static_cast<int>(15 * m_particle_multiplier);
            int maxPPS = static_cast<int>(25 * m_particle_multiplier);

            // Create ring emitter for spiraling effect around caster
            irr::scene::IParticleEmitter* emitter = effect.particle_system->createRingEmitter(
                irr::core::vector3df(0, 0, 0),              // Center
                3.0f, 5.0f,                                  // Ring radius (around entity)
                irr::core::vector3df(0, 0.02f, 0),          // Slight upward movement
                minPPS, maxPPS,                              // Particles per second
                effect.color,                                // Min color
                effect.color,                                // Max color
                300, 800,                                    // Min/max lifetime ms
                30,                                          // Max angle (some spread)
                irr::core::dimension2df(0.8f, 0.8f),        // Min size
                irr::core::dimension2df(2.0f, 2.0f)         // Max size
            );

            effect.particle_system->setEmitter(emitter);
            emitter->drop();

            // Add fade out affector
            irr::scene::IParticleAffector* fadeAffector =
                effect.particle_system->createFadeOutParticleAffector();
            effect.particle_system->addAffector(fadeAffector);
            fadeAffector->drop();

            // Add rotation affector to create spiraling effect
            irr::scene::IParticleAffector* rotAffector =
                effect.particle_system->createRotationAffector(
                    irr::core::vector3df(0, 60.0f, 0));  // Rotate around Y axis
            effect.particle_system->addAffector(rotAffector);
            rotAffector->drop();

            // Set particle texture
            irr::video::ITexture* tex = getTextureForSpell(effect.spell_id);
            if (tex) {
                effect.particle_system->setMaterialTexture(0, tex);
            }

            effect.particle_system->setMaterialFlag(irr::video::EMF_LIGHTING, false);
            effect.particle_system->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);
        }
    }
}

void SpellVisualFX::createProjectileNode(SpellFXInstance& effect)
{
    if (!m_smgr) return;

    // Create a billboard for the projectile
    effect.billboard = m_smgr->addBillboardSceneNode(nullptr,
        irr::core::dimension2df(5.0f, 5.0f),
        effect.source_pos);

    if (effect.billboard) {
        effect.billboard->setColor(effect.color);
        effect.billboard->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        effect.billboard->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);
    }
}

void SpellVisualFX::createImpactNode(SpellFXInstance& effect)
{
    if (!m_smgr) return;
    if (m_particle_multiplier <= 0.0f) return;  // Particles disabled

    // Create a particle system for the impact burst
    effect.particle_system = m_smgr->addParticleSystemSceneNode(false, nullptr, -1,
        effect.target_pos);

    if (effect.particle_system) {
        // Apply particle multiplier to emission rates
        int minPPS = static_cast<int>(20 * m_particle_multiplier);
        int maxPPS = static_cast<int>(40 * m_particle_multiplier);

        // Create box emitter for burst effect
        irr::scene::IParticleEmitter* emitter = effect.particle_system->createBoxEmitter(
            irr::core::aabbox3df(-2, -2, -2, 2, 2, 2),  // Box size
            irr::core::vector3df(0, 0.02f, 0),          // Direction (slight upward)
            minPPS, maxPPS,                              // Particles per second (scaled)
            effect.color,                                // Min color
            effect.color,                                // Max color
            100, 300,                                    // Min/max lifetime ms
            0,                                           // Max angle
            irr::core::dimension2df(1.0f, 1.0f),        // Min size
            irr::core::dimension2df(3.0f, 3.0f)         // Max size
        );

        effect.particle_system->setEmitter(emitter);
        emitter->drop();

        // Add fade out affector
        irr::scene::IParticleAffector* fadeAffector =
            effect.particle_system->createFadeOutParticleAffector();
        effect.particle_system->addAffector(fadeAffector);
        fadeAffector->drop();

        // Set particle texture based on spell type
        irr::video::ITexture* tex = getTextureForSpell(effect.spell_id);
        if (tex) {
            effect.particle_system->setMaterialTexture(0, tex);
        }

        effect.particle_system->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        effect.particle_system->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);
    }
}

void SpellVisualFX::createAuraNode(SpellFXInstance& effect)
{
    if (!m_smgr) return;
    if (m_particle_multiplier <= 0.0f) return;  // Particles disabled

    // Create a particle system for the persistent aura
    effect.particle_system = m_smgr->addParticleSystemSceneNode(false, nullptr, -1,
        effect.source_pos);

    if (effect.particle_system) {
        // Apply particle multiplier to emission rates
        int minPPS = static_cast<int>(5 * m_particle_multiplier);
        int maxPPS = static_cast<int>(10 * m_particle_multiplier);

        // Create ring emitter for aura effect
        irr::scene::IParticleEmitter* emitter = effect.particle_system->createRingEmitter(
            irr::core::vector3df(0, 0, 0),              // Center
            5.0f, 7.0f,                                  // Ring radius min/max
            irr::core::vector3df(0, 0.01f, 0),          // Direction (slight upward)
            minPPS, maxPPS,                              // Particles per second (scaled)
            effect.color,                                // Min color
            effect.color,                                // Max color
            500, 1000,                                   // Min/max lifetime ms
            0,                                           // Max angle
            irr::core::dimension2df(0.5f, 0.5f),        // Min size
            irr::core::dimension2df(1.5f, 1.5f)         // Max size
        );

        effect.particle_system->setEmitter(emitter);
        emitter->drop();

        // Add fade out affector
        irr::scene::IParticleAffector* fadeAffector =
            effect.particle_system->createFadeOutParticleAffector();
        effect.particle_system->addAffector(fadeAffector);
        fadeAffector->drop();

        // Set particle texture based on spell type
        irr::video::ITexture* tex = getTextureForSpell(effect.spell_id);
        if (tex) {
            effect.particle_system->setMaterialTexture(0, tex);
        }

        effect.particle_system->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        effect.particle_system->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);
    }
}

void SpellVisualFX::createRainNode(SpellFXInstance& effect)
{
    if (!m_smgr) return;
    if (m_particle_multiplier <= 0.0f) return;  // Particles disabled

    // Create particle system for rain effect
    effect.particle_system = m_smgr->addParticleSystemSceneNode(false, nullptr, -1,
        effect.target_pos + irr::core::vector3df(0, 30, 0));  // Start above target

    if (effect.particle_system) {
        float radius = effect.scale;

        // Apply particle multiplier to emission rates
        int minPPS = static_cast<int>(30 * m_particle_multiplier);
        int maxPPS = static_cast<int>(60 * m_particle_multiplier);

        irr::scene::IParticleEmitter* emitter = effect.particle_system->createBoxEmitter(
            irr::core::aabbox3df(-radius, 0, -radius, radius, 5, radius),
            irr::core::vector3df(0, -0.1f, 0),          // Falling direction
            minPPS, maxPPS,                              // Particles per second (scaled)
            effect.color,
            effect.color,
            500, 1500,                                   // Lifetime
            0,
            irr::core::dimension2df(0.3f, 0.3f),
            irr::core::dimension2df(0.8f, 0.8f)
        );

        effect.particle_system->setEmitter(emitter);
        emitter->drop();

        // Set particle texture based on spell type
        irr::video::ITexture* tex = getTextureForSpell(effect.spell_id);
        if (tex) {
            effect.particle_system->setMaterialTexture(0, tex);
        }

        effect.particle_system->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        effect.particle_system->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);
    }
}

void SpellVisualFX::createGroundCircleNode(SpellFXInstance& effect)
{
    if (!m_smgr) return;

    // Simple billboard on ground for now
    effect.billboard = m_smgr->addBillboardSceneNode(nullptr,
        irr::core::dimension2df(effect.scale * 2, effect.scale * 2),
        effect.target_pos + irr::core::vector3df(0, 0.5f, 0));  // Slightly above ground

    if (effect.billboard) {
        irr::video::SColor circleColor = effect.color;
        circleColor.setAlpha(100);
        effect.billboard->setColor(circleColor);
        effect.billboard->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        effect.billboard->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);
    }
}

// ============================================================================
// Update Functions
// ============================================================================

void SpellVisualFX::updateProjectile(SpellFXInstance& effect, float delta_time,
                                      std::vector<std::pair<uint16_t, uint32_t>>& pendingImpacts)
{
    if (!effect.billboard) return;

    // Get current and target positions
    irr::core::vector3df current = effect.billboard->getPosition();

    // Update target position if tracking an entity
    if (effect.target_entity != 0 && m_entity_pos_callback) {
        m_entity_pos_callback(effect.target_entity, effect.target_pos);
    }

    irr::core::vector3df direction = effect.target_pos - current;
    float distance = direction.getLength();

    if (distance < 3.0f) {
        // Reached target - queue impact creation and remove projectile
        // Don't create impact directly to avoid vector invalidation
        pendingImpacts.emplace_back(effect.target_entity, effect.spell_id);
        effect.active = false;
        return;
    }

    // Move toward target
    direction.normalize();
    float move_distance = PROJECTILE_SPEED * delta_time;
    if (move_distance > distance) {
        move_distance = distance;
    }

    effect.billboard->setPosition(current + direction * move_distance);
}

void SpellVisualFX::updateGlow(SpellFXInstance& effect, float delta_time)
{
    // Update position to follow caster
    if (effect.source_entity != 0 && m_entity_pos_callback) {
        irr::core::vector3df pos;
        if (m_entity_pos_callback(effect.source_entity, pos)) {
            // Position glow at caster's chest height
            if (effect.billboard) {
                effect.billboard->setPosition(pos + irr::core::vector3df(0, 3.0f, 0));
            }
            // Position particle system at caster's feet/base
            if (effect.particle_system) {
                effect.particle_system->setPosition(pos + irr::core::vector3df(0, 1.0f, 0));
            }
        }
    }

    // Pulsing effect for billboard glow
    if (effect.billboard) {
        float pulse = 0.7f + 0.3f * std::sin(effect.elapsed * 6.0f);
        effect.billboard->setSize(irr::core::dimension2df(12.0f * pulse, 12.0f * pulse));

        // Also pulse the alpha
        irr::video::SColor glowColor = effect.color;
        glowColor.setAlpha(static_cast<uint32_t>(80 + 40 * std::sin(effect.elapsed * 6.0f)));
        effect.billboard->setColor(glowColor);
    }
}

void SpellVisualFX::updateAura(SpellFXInstance& effect, float delta_time)
{
    if (!effect.particle_system) return;

    // Update position to follow entity
    if (effect.source_entity != 0 && m_entity_pos_callback) {
        irr::core::vector3df pos;
        if (m_entity_pos_callback(effect.source_entity, pos)) {
            effect.particle_system->setPosition(pos);
        }
    }
}

void SpellVisualFX::adjustParticleMultiplier(float delta)
{
    float old_mult = m_particle_multiplier;
    m_particle_multiplier = std::max(0.0f, std::min(m_particle_multiplier + delta, 3.0f));

    // Log the change if it actually changed
    if (old_mult != m_particle_multiplier) {
        // Round to 1 decimal for display
        float display_mult = std::round(m_particle_multiplier * 10.0f) / 10.0f;
        LOG_DEBUG(MOD_GRAPHICS, "Particle multiplier: {:.1f}x", display_mult);
    }
}

// ============================================================================
// Worker-Driven Facade
// ============================================================================

std::vector<EQT::Graphics::SpellVFXCommandData> SpellVisualFX::drainCommands()
{
    std::vector<EQT::Graphics::SpellVFXCommandData> result;
    result.swap(m_pending_commands);
    return result;
}

void SpellVisualFX::applyWorkerResults(const EQT::Graphics::SimulationOutput& results)
{
    const auto& svo = results.spellVfxOutput;
    if (!svo.valid) return;

    // Process create events — build scene nodes for new effects
    for (const auto& ce : svo.createEvents) {
        SpellFXInstance effect;
        effect.effect_id = ce.effectId;
        effect.type = static_cast<SpellFXType>(ce.fxType);
        effect.spell_id = ce.spellId;
        effect.source_entity = ce.sourceEntity;
        effect.target_entity = ce.targetEntity;
        effect.source_pos = irr::core::vector3df(ce.posX, ce.posY, ce.posZ);
        effect.target_pos = irr::core::vector3df(ce.targetPosX, ce.targetPosY, ce.targetPosZ);
        effect.color = irr::video::SColor(ce.colorA, ce.colorR, ce.colorG, ce.colorB);
        effect.lifetime = ce.lifetime;
        effect.scale = ce.scale;
        effect.active = true;

        switch (effect.type) {
            case SpellFXType::CastGlow:
                createGlowNode(effect);
                break;
            case SpellFXType::ProjectileTravel:
                createProjectileNode(effect);
                break;
            case SpellFXType::AuraPersistent:
                createAuraNode(effect);
                break;
            default:
                break;
        }

        m_effects.push_back(effect);
    }

    // Process remove events — find by effect_id and remove scene nodes
    for (const auto& re : svo.removeEvents) {
        for (size_t i = 0; i < m_effects.size(); ) {
            if (m_effects[i].effect_id == re.effectId) {
                removeEffect(i);
            } else {
                ++i;
            }
        }
    }

    // Process effect updates — apply positions/sizes/colors to scene nodes
    for (const auto& eu : svo.effectUpdates) {
        for (auto& effect : m_effects) {
            if (effect.effect_id != eu.effectId) continue;

            if (eu.hasBillboardUpdate && effect.billboard) {
                effect.billboard->setPosition(irr::core::vector3df(eu.posX, eu.posY, eu.posZ));
                if (eu.billboardWidth > 0 && eu.billboardHeight > 0) {
                    effect.billboard->setSize(
                        irr::core::dimension2df(eu.billboardWidth, eu.billboardHeight));
                }
            }

            if (eu.hasParticleUpdate && effect.particle_system) {
                effect.particle_system->setPosition(
                    irr::core::vector3df(eu.particlePosX, eu.particlePosY, eu.particlePosZ));
            }

            if (eu.hasColorUpdate && effect.billboard) {
                irr::video::SColor c(eu.colorA, eu.colorR, eu.colorG, eu.colorB);
                effect.billboard->setColor(c);
            }

            break;  // Found matching effect
        }
    }

    // Process impact events — create fire-and-forget impacts on main thread
    for (const auto& ie : svo.impactEvents) {
        // Temporarily disable worker-driven to avoid re-queuing command
        bool wasWorkerDriven = m_worker_driven;
        m_worker_driven = false;
        createImpact(ie.targetEntity, ie.spellId);
        m_worker_driven = wasWorkerDriven;
    }
}

void SpellVisualFX::swapEvictedTexture(irr::video::ITexture* from, irr::video::ITexture* to) {
    if (!from) return;
    irr::video::ITexture** slots[] = {
        &m_particle_texture, &m_fire_texture, &m_frost_texture,   &m_magic_texture,
        &m_poison_texture,   &m_disease_texture, &m_chromatic_texture, &m_corruption_texture,
    };
    int swapped = 0;
    for (irr::video::ITexture** slot : slots) {
        if (*slot == from) {
            *slot = to;
            ++swapped;
        }
    }
    if (swapped > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "[spellfxtex] SpellVisualFX::swapEvictedTexture: swapped {} cached "
                  "particle-texture member(s) to evicted texture -> placeholder (T124 — prevents a "
                  "future createXNode() from baking a dangling ITexture* into a brand new node's "
                  "material)", swapped);
    }
}

} // namespace EQ
