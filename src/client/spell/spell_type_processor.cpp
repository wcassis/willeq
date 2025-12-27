/*
 * WillEQ Spell Type Processor Implementation
 */

#include <client/spell/spell_type_processor.h>
#include <client/spell/spell_database.h>
#include <client/spell/spell_effects.h>
#include <client/eq.h>
#include <common/logging.h>
#include <algorithm>
#include <cmath>

namespace EQ {

// ============================================================================
// Constructor / Destructor
// ============================================================================

SpellTypeProcessor::SpellTypeProcessor(EverQuest* eq, SpellDatabase* spell_db,
                                       SpellEffects* spell_effects)
    : m_eq(eq)
    , m_spell_db(spell_db)
    , m_spell_effects(spell_effects)
    , m_rng(std::random_device{}())
{
    LOG_DEBUG(MOD_SPELL, "SpellTypeProcessor initialized");
}

SpellTypeProcessor::~SpellTypeProcessor() = default;

// ============================================================================
// Update
// ============================================================================

void SpellTypeProcessor::update(float delta_time)
{
    if (m_active_rains.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // Process active rain spells
    for (auto& rain : m_active_rains) {
        if (rain.waves_remaining > 0 && now >= rain.next_wave_time) {
            processRainWave(rain);
        }
    }

    // Remove completed rain spells
    m_active_rains.erase(
        std::remove_if(m_active_rains.begin(), m_active_rains.end(),
            [](const RainSpellInstance& r) { return r.waves_remaining == 0; }),
        m_active_rains.end());
}

// ============================================================================
// Main Spell Processing
// ============================================================================

void SpellTypeProcessor::processSpell(uint32_t spell_id, uint16_t caster_id,
                                       uint16_t target_id, uint8_t caster_level,
                                       bool from_server)
{
    if (!m_spell_db) {
        LOG_WARN(MOD_SPELL, "SpellTypeProcessor::processSpell - no spell database");
        return;
    }

    const SpellData* spell = m_spell_db->getSpell(spell_id);
    if (!spell) {
        LOG_WARN(MOD_SPELL, "SpellTypeProcessor::processSpell - unknown spell ID {}", spell_id);
        return;
    }

    LOG_DEBUG(MOD_SPELL, "Processing spell '{}' (ID:{}) type {} from caster {} to target {}",
              spell->name, spell_id, static_cast<int>(spell->target_type), caster_id, target_id);

    // Route to appropriate handler based on target type
    switch (spell->target_type) {
        case SpellTargetType::Self:
            processSelfSpell(*spell, caster_id, caster_level);
            break;

        case SpellTargetType::AECaster:
            // Point Blank AE - centered on caster
            processPBAE(*spell, caster_id, caster_level);
            break;

        case SpellTargetType::AETarget:
        case SpellTargetType::TargetAETap:
        case SpellTargetType::TargetRing:
        case SpellTargetType::FreeTarget:
        case SpellTargetType::TargetAENoNPCs:
        case SpellTargetType::TargetAEUndeadPC:
            // Targeted AE - centered on target location
            processTargetedAE(*spell, caster_id, target_id, caster_level);
            break;

        case SpellTargetType::GroupV1:
        case SpellTargetType::GroupV2:
        case SpellTargetType::GroupNoPets:
        case SpellTargetType::GroupedClients:
        case SpellTargetType::GroupClientsPets:
        case SpellTargetType::GroupTeleport:
            // Group spells
            processGroupSpell(*spell, caster_id, caster_level);
            break;

        case SpellTargetType::AEClientV1:
        case SpellTargetType::AEBard:
        case SpellTargetType::DirectionalAE:
        case SpellTargetType::UndeadAE:
        case SpellTargetType::SummonedAE:
            // Various AE types - treat as targeted AE for now
            if (target_id != 0) {
                processTargetedAE(*spell, caster_id, target_id, caster_level);
            } else {
                processPBAE(*spell, caster_id, caster_level);
            }
            break;

        case SpellTargetType::Single:
        case SpellTargetType::TargetOptional:
        case SpellTargetType::Animal:
        case SpellTargetType::Undead:
        case SpellTargetType::Summoned:
        case SpellTargetType::Pet:
        case SpellTargetType::Corpse:
        case SpellTargetType::Plant:
        case SpellTargetType::Giant:
        case SpellTargetType::Dragon:
        case SpellTargetType::SummonPC:
        case SpellTargetType::SingleInGroup:
        case SpellTargetType::SingleFriendly:
        case SpellTargetType::NoBuffSelf:
        case SpellTargetType::InstrumentBard:
        case SpellTargetType::BeanTarget:
        default:
            // Single target spells
            processSingleTarget(*spell, caster_id, target_id, caster_level);
            break;
    }
}

// ============================================================================
// Target Finding
// ============================================================================

std::vector<uint16_t> SpellTypeProcessor::findTargetsInRadius(
    float center_x, float center_y, float center_z,
    float radius, bool beneficial_only)
{
    std::vector<uint16_t> targets;

    if (!m_eq) {
        return targets;
    }

    float radius_sq = radius * radius;

    for (const auto& [entity_id, entity] : m_eq->GetEntities()) {
        // Skip corpses
        if (entity.is_corpse) {
            continue;
        }

        // Calculate distance
        float dx = entity.x - center_x;
        float dy = entity.y - center_y;
        float dz = entity.z - center_z;
        float dist_sq = dx * dx + dy * dy + dz * dz;

        if (dist_sq <= radius_sq) {
            targets.push_back(entity_id);
        }
    }

    return targets;
}

std::vector<uint16_t> SpellTypeProcessor::findPBAETargets(
    uint16_t caster_id, float radius, bool beneficial)
{
    std::vector<uint16_t> targets;

    if (!m_eq) {
        return targets;
    }

    float caster_x, caster_y, caster_z;
    if (!getEntityPosition(caster_id, caster_x, caster_y, caster_z)) {
        return targets;
    }

    float radius_sq = radius * radius;

    for (const auto& [entity_id, entity] : m_eq->GetEntities()) {
        // Skip self for detrimental spells
        if (entity_id == caster_id && !beneficial) {
            continue;
        }

        // Skip corpses
        if (entity.is_corpse) {
            continue;
        }

        // For beneficial spells, only target players
        // For detrimental spells, only target NPCs
        if (beneficial) {
            if (entity.npc_type != 0) {  // 0 = player
                continue;
            }
        } else {
            if (entity.npc_type == 0) {  // 0 = player
                continue;
            }
        }

        // Calculate distance
        float dx = entity.x - caster_x;
        float dy = entity.y - caster_y;
        float dz = entity.z - caster_z;
        float dist_sq = dx * dx + dy * dy + dz * dz;

        if (dist_sq <= radius_sq) {
            targets.push_back(entity_id);
        }
    }

    return targets;
}

std::vector<uint16_t> SpellTypeProcessor::findGroupTargets(uint16_t caster_id, float range)
{
    std::vector<uint16_t> targets;

    if (!m_eq) {
        return targets;
    }

    // Always include caster for group spells
    targets.push_back(caster_id);

    // TODO: When group system is implemented, get actual group member IDs
    // For now, find nearby players within range as a placeholder
    float caster_x, caster_y, caster_z;
    if (!getEntityPosition(caster_id, caster_x, caster_y, caster_z)) {
        return targets;
    }

    float range_sq = range * range;

    for (const auto& [entity_id, entity] : m_eq->GetEntities()) {
        // Skip self (already added)
        if (entity_id == caster_id) {
            continue;
        }

        // Skip non-players
        if (entity.npc_type != 0) {
            continue;
        }

        // Skip corpses
        if (entity.is_corpse) {
            continue;
        }

        // Check range
        float dx = entity.x - caster_x;
        float dy = entity.y - caster_y;
        float dz = entity.z - caster_z;
        float dist_sq = dx * dx + dy * dy + dz * dz;

        if (dist_sq <= range_sq) {
            // TODO: Check if actually in group
            // For now, just check range
            targets.push_back(entity_id);
        }
    }

    return targets;
}

// ============================================================================
// Rain Spell Management
// ============================================================================

void SpellTypeProcessor::startRainSpell(uint32_t spell_id, uint16_t caster_id,
                                         uint16_t target_id, uint8_t caster_level)
{
    if (!m_spell_db) {
        return;
    }

    const SpellData* spell = m_spell_db->getSpell(spell_id);
    if (!spell) {
        LOG_WARN(MOD_SPELL, "startRainSpell - unknown spell ID {}", spell_id);
        return;
    }

    // Get target location as rain center
    float center_x, center_y, center_z;
    if (!getEntityPosition(target_id, center_x, center_y, center_z)) {
        // If no target, use caster position
        if (!getEntityPosition(caster_id, center_x, center_y, center_z)) {
            return;
        }
    }

    // Create rain instance
    RainSpellInstance rain;
    rain.spell_id = spell_id;
    rain.caster_id = caster_id;
    rain.caster_level = caster_level;
    rain.center_x = center_x;
    rain.center_y = center_y;
    rain.center_z = center_z;
    rain.radius = spell->aoe_range;
    rain.waves_remaining = RainSpellInstance::DEFAULT_RAIN_WAVES;
    rain.wave_interval_ms = 3000;  // 3 seconds between waves
    rain.next_wave_time = std::chrono::steady_clock::now();  // First wave immediately

    m_active_rains.push_back(rain);

    LOG_DEBUG(MOD_SPELL, "Started rain spell '{}' at ({:.1f}, {:.1f}, {:.1f}) radius {:.1f}",
              spell->name, center_x, center_y, center_z, spell->aoe_range);

    // Process first wave immediately
    processRainWave(m_active_rains.back());
}

void SpellTypeProcessor::processRainWave(RainSpellInstance& rain)
{
    if (!m_spell_db || !m_spell_effects) {
        return;
    }

    const SpellData* spell = m_spell_db->getSpell(rain.spell_id);
    if (!spell) {
        rain.waves_remaining = 0;
        return;
    }

    LOG_DEBUG(MOD_SPELL, "Processing rain wave {} for '{}' ({} waves remaining)",
              RainSpellInstance::DEFAULT_RAIN_WAVES - rain.waves_remaining + 1,
              spell->name, rain.waves_remaining);

    // Find targets in radius
    std::vector<uint16_t> targets = findTargetsInRadius(
        rain.center_x, rain.center_y, rain.center_z,
        rain.radius, spell->is_beneficial);

    // Remove caster from detrimental rain targets
    if (!spell->is_beneficial) {
        targets.erase(
            std::remove(targets.begin(), targets.end(), rain.caster_id),
            targets.end());
    }

    if (!targets.empty()) {
        // Shuffle targets for random selection
        std::shuffle(targets.begin(), targets.end(), m_rng);

        // Hit up to MAX_TARGETS_PER_WAVE targets
        size_t hit_count = std::min(targets.size(),
                                    static_cast<size_t>(RainSpellInstance::MAX_TARGETS_PER_WAVE));

        for (size_t i = 0; i < hit_count; ++i) {
            // Process spell effects on target
            m_spell_effects->processSpell(rain.spell_id, rain.caster_id,
                                          targets[i], rain.caster_level, false);

            LOG_TRACE(MOD_SPELL, "Rain hit target {} (wave {})",
                      targets[i], RainSpellInstance::DEFAULT_RAIN_WAVES - rain.waves_remaining + 1);
        }
    }

    // Decrement waves and schedule next
    rain.waves_remaining--;

    if (rain.waves_remaining > 0) {
        rain.next_wave_time = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(rain.wave_interval_ms);
    }
}

// ============================================================================
// Spell Type Handlers
// ============================================================================

void SpellTypeProcessor::processSingleTarget(const SpellData& spell, uint16_t caster_id,
                                              uint16_t target_id, uint8_t caster_level)
{
    if (!m_spell_effects) {
        return;
    }

    // If no target specified, use caster for beneficial spells
    if (target_id == 0 && spell.is_beneficial) {
        target_id = caster_id;
    }

    if (target_id == 0) {
        LOG_DEBUG(MOD_SPELL, "processSingleTarget - no target for spell '{}'", spell.name);
        return;
    }

    // Process spell effects
    m_spell_effects->processSpell(spell.id, caster_id, target_id, caster_level, false);

    LOG_DEBUG(MOD_SPELL, "Processed single target spell '{}' on target {}", spell.name, target_id);
}

void SpellTypeProcessor::processSelfSpell(const SpellData& spell, uint16_t caster_id,
                                           uint8_t caster_level)
{
    if (!m_spell_effects) {
        return;
    }

    // Self spells always target the caster
    m_spell_effects->processSpell(spell.id, caster_id, caster_id, caster_level, false);

    LOG_DEBUG(MOD_SPELL, "Processed self spell '{}' on caster {}", spell.name, caster_id);
}

void SpellTypeProcessor::processPBAE(const SpellData& spell, uint16_t caster_id,
                                      uint8_t caster_level)
{
    if (!m_spell_effects) {
        return;
    }

    // Find all targets in radius around caster
    std::vector<uint16_t> targets = findPBAETargets(
        caster_id, spell.aoe_range, spell.is_beneficial);

    LOG_DEBUG(MOD_SPELL, "PBAE spell '{}' found {} targets in radius {:.1f}",
              spell.name, targets.size(), spell.aoe_range);

    // Apply spell to all targets
    for (uint16_t target_id : targets) {
        m_spell_effects->processSpell(spell.id, caster_id, target_id, caster_level, false);
    }

    // For beneficial PBAEs, also include caster
    if (spell.is_beneficial) {
        if (std::find(targets.begin(), targets.end(), caster_id) == targets.end()) {
            m_spell_effects->processSpell(spell.id, caster_id, caster_id, caster_level, false);
        }
    }
}

void SpellTypeProcessor::processTargetedAE(const SpellData& spell, uint16_t caster_id,
                                            uint16_t target_id, uint8_t caster_level)
{
    if (!m_spell_effects) {
        return;
    }

    // Get target location as AE center
    float center_x, center_y, center_z;
    if (!getEntityPosition(target_id, center_x, center_y, center_z)) {
        LOG_DEBUG(MOD_SPELL, "processTargetedAE - target {} not found", target_id);
        return;
    }

    // Find all targets in radius around target location
    std::vector<uint16_t> targets = findTargetsInRadius(
        center_x, center_y, center_z, spell.aoe_range, spell.is_beneficial);

    // For detrimental spells, remove caster from targets
    if (!spell.is_beneficial) {
        targets.erase(
            std::remove(targets.begin(), targets.end(), caster_id),
            targets.end());
    }

    LOG_DEBUG(MOD_SPELL, "Targeted AE spell '{}' found {} targets at ({:.1f}, {:.1f}, {:.1f}) radius {:.1f}",
              spell.name, targets.size(), center_x, center_y, center_z, spell.aoe_range);

    // Apply spell to all targets
    for (uint16_t affected_id : targets) {
        m_spell_effects->processSpell(spell.id, caster_id, affected_id, caster_level, false);
    }
}

void SpellTypeProcessor::processGroupSpell(const SpellData& spell, uint16_t caster_id,
                                            uint8_t caster_level)
{
    if (!m_spell_effects) {
        return;
    }

    // Find group members in range
    std::vector<uint16_t> targets = findGroupTargets(caster_id, spell.range);

    LOG_DEBUG(MOD_SPELL, "Group spell '{}' affecting {} targets", spell.name, targets.size());

    // Apply spell to all group members in range
    for (uint16_t member_id : targets) {
        m_spell_effects->processSpell(spell.id, caster_id, member_id, caster_level, false);
    }
}

// ============================================================================
// Helper Methods
// ============================================================================

bool SpellTypeProcessor::getEntityPosition(uint16_t entity_id, float& x, float& y, float& z) const
{
    if (!m_eq) {
        return false;
    }

    // Check if this is the player
    if (entity_id == m_eq->GetEntityID()) {
        glm::vec3 pos = m_eq->GetPosition();
        x = pos.x;
        y = pos.y;
        z = pos.z;
        return true;
    }

    // Look up entity
    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(entity_id);
    if (it == entities.end()) {
        return false;
    }

    x = it->second.x;
    y = it->second.y;
    z = it->second.z;
    return true;
}

float SpellTypeProcessor::calculateDistance(float x1, float y1, float z1,
                                             float x2, float y2, float z2) const
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool SpellTypeProcessor::isValidTarget(uint16_t entity_id, const SpellData& spell,
                                        uint16_t caster_id) const
{
    if (!m_eq) {
        return false;
    }

    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(entity_id);
    if (it == entities.end()) {
        return false;
    }

    const Entity& entity = it->second;

    // Can't target corpses (except corpse-only spells)
    if (entity.is_corpse && spell.target_type != SpellTargetType::Corpse) {
        return false;
    }

    // Check target type restrictions
    switch (spell.target_type) {
        case SpellTargetType::Undead:
            // Would need body type check - placeholder
            return true;

        case SpellTargetType::Summoned:
            // Would need summoned check - placeholder
            return true;

        case SpellTargetType::Animal:
            // Would need body type check - placeholder
            return true;

        case SpellTargetType::Plant:
            return entity.bodytype == 21;  // Plant body type

        case SpellTargetType::Giant:
            return entity.bodytype == 22;  // Giant body type

        case SpellTargetType::Dragon:
            return entity.bodytype == 26;  // Dragon body type

        default:
            return true;
    }
}

bool SpellTypeProcessor::isNPC(uint16_t entity_id) const
{
    if (!m_eq) {
        return false;
    }

    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(entity_id);
    if (it == entities.end()) {
        return false;
    }

    return it->second.npc_type == 1;  // 1 = NPC
}

bool SpellTypeProcessor::isPlayer(uint16_t entity_id) const
{
    if (!m_eq) {
        return false;
    }

    // Check if it's our player
    if (entity_id == m_eq->GetEntityID()) {
        return true;
    }

    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(entity_id);
    if (it == entities.end()) {
        return false;
    }

    return it->second.npc_type == 0;  // 0 = player
}

} // namespace EQ
