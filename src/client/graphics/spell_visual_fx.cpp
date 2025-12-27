/*
 * WillEQ Spell Visual Effects Implementation
 */

#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/spell/spell_database.h"
#include "client/spell/spell_constants.h"
#include "common/logging.h"
#include <algorithm>
#include <fstream>

namespace EQ {

SpellVisualFX::SpellVisualFX(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                             const std::string& eqClientPath)
    : m_smgr(smgr)
    , m_driver(driver)
    , m_eq_client_path(eqClientPath)
{
    // Load particle textures from EQ client SpellEffects directory
    loadParticleTextures();
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

    // If we couldn't load any textures, create a fallback
    if (!m_particle_texture) {
        LOG_WARN(MOD_SPELL, "Could not load spell effect textures from {}, creating fallback", spellEffectsPath);
        createFallbackTexture();
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
    if (!m_driver) return nullptr;

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

    // Check if it's a DDS file
    if (!EQT::Graphics::DDSDecoder::isDDS(data)) {
        LOG_WARN(MOD_SPELL, "Not a valid DDS file: {}", path);
        return nullptr;
    }

    // Decode DDS
    EQT::Graphics::DecodedImage decoded = EQT::Graphics::DDSDecoder::decode(data);
    if (!decoded.isValid()) {
        LOG_WARN(MOD_SPELL, "Failed to decode DDS: {}", path);
        return nullptr;
    }

    // Create Irrlicht image from decoded RGBA data
    irr::core::dimension2du dim(decoded.width, decoded.height);
    irr::video::IImage* image = m_driver->createImage(irr::video::ECF_A8R8G8B8, dim);
    if (!image) {
        LOG_WARN(MOD_SPELL, "Failed to create image for: {}", path);
        return nullptr;
    }

    // Copy pixels (RGBA to ARGB)
    for (uint32_t y = 0; y < decoded.height; ++y) {
        for (uint32_t x = 0; x < decoded.width; ++x) {
            size_t idx = (y * decoded.width + x) * 4;
            uint8_t r = decoded.pixels[idx + 0];
            uint8_t g = decoded.pixels[idx + 1];
            uint8_t b = decoded.pixels[idx + 2];
            uint8_t a = decoded.pixels[idx + 3];
            image->setPixel(x, y, irr::video::SColor(a, r, g, b));
        }
    }

    // Create texture
    irr::video::ITexture* texture = m_driver->addTexture(name.c_str(), image);
    image->drop();

    if (texture) {
        LOG_DEBUG(MOD_SPELL, "Loaded particle texture: {} ({}x{})", path, decoded.width, decoded.height);
    }

    return texture;
}

void SpellVisualFX::createFallbackTexture()
{
    if (!m_driver) return;

    // Create a 32x32 soft circle texture as fallback
    const int size = 32;
    irr::core::dimension2du dim(size, size);

    irr::video::IImage* image = m_driver->createImage(irr::video::ECF_A8R8G8B8, dim);
    if (!image) return;

    float center = size / 2.0f;
    float maxDist = center;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float dx = x - center + 0.5f;
            float dy = y - center + 0.5f;
            float dist = std::sqrt(dx * dx + dy * dy);

            float normalizedDist = dist / maxDist;
            int alpha = 0;
            if (normalizedDist < 1.0f) {
                float falloff = std::cos(normalizedDist * 3.14159f * 0.5f);
                alpha = static_cast<int>(falloff * 255.0f);
            }

            image->setPixel(x, y, irr::video::SColor(alpha, 255, 255, 255));
        }
    }

    m_particle_texture = m_driver->addTexture("spell_particle_fallback", image);
    image->drop();

    if (m_particle_texture) {
        LOG_DEBUG(MOD_SPELL, "Created fallback particle texture");
    }
}

irr::video::ITexture* SpellVisualFX::getTextureForSpell(uint32_t spell_id) const
{
    // Get appropriate texture based on spell's resist type
    if (m_spell_db) {
        const SpellData* spell = m_spell_db->getSpell(spell_id);
        if (spell) {
            switch (spell->resist_type) {
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
    }
    return m_particle_texture;
}

void SpellVisualFX::update(float delta_time)
{
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

uint32_t SpellVisualFX::createCastGlow(uint16_t caster_id, uint32_t spell_id, uint32_t duration_ms)
{
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

void SpellVisualFX::createSpellComplete(uint16_t caster_id, uint32_t spell_id)
{
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

// ============================================================================
// Effect Removal
// ============================================================================

void SpellVisualFX::removeCastGlow(uint16_t caster_id)
{
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
        printf("[SpellFX] Particle multiplier: %.1fx\n", display_mult);
    }
}

} // namespace EQ
