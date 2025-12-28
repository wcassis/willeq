/*
 * WillEQ Spell Manager Implementation
 */

#include "client/spell/spell_manager.h"
#include "client/eq.h"
#include "client/combat.h"
#include "common/net/packet.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include <algorithm>
#include <cmath>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/window_manager.h"
#include "client/graphics/spell_visual_fx.h"
#endif

namespace EQ {

// ============================================================================
// Constructor / Destructor
// ============================================================================

SpellManager::SpellManager(EverQuest* eq)
    : m_eq(eq)
{
    // Initialize spell gems to empty
    m_spell_gems.fill(SPELL_UNKNOWN);
    m_gem_states.fill(GemState::Empty);
    m_gem_cooldown_duration.fill(0);

    // Initialize spellbook to empty
    m_spellbook.fill(SPELL_UNKNOWN);
}

SpellManager::~SpellManager() = default;

// ============================================================================
// Initialization
// ============================================================================

bool SpellManager::initialize(const std::string& eq_client_path)
{
    std::string spell_file = eq_client_path + "/spells_us.txt";

    if (!m_spell_db.loadFromFile(spell_file)) {
        LOG_ERROR(MOD_SPELL, "Failed to load spell database: {}", m_spell_db.getLoadError());
        return false;
    }

    LOG_INFO(MOD_SPELL, "Loaded {} spells from database", m_spell_db.getSpellCount());
    m_initialized = true;
    return true;
}

// ============================================================================
// Update
// ============================================================================

void SpellManager::update(float delta_time)
{
    // Update gem cooldowns
    updateGemCooldowns(delta_time);

    // Update memorization progress
    updateMemorization(delta_time);

    // Check if cast has timed out (server didn't respond)
    if (m_is_casting && m_waiting_for_server_confirm) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_cast_start_time).count();

        // If waiting more than 5 seconds for server confirm, something went wrong
        if (elapsed > 5000) {
            LOG_WARN(MOD_SPELL, "Cast timeout waiting for server confirmation");
            m_is_casting = false;
            m_waiting_for_server_confirm = false;
            if (m_current_gem_slot < MAX_SPELL_GEMS) {
                m_gem_states[m_current_gem_slot] = GemState::Ready;
            }
        }
    }
}

void SpellManager::updateGemCooldowns(float delta_time)
{
    auto now = std::chrono::steady_clock::now();

    for (uint8_t i = 0; i < MAX_SPELL_GEMS; ++i) {
        if (m_gem_states[i] == GemState::Refresh) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_gem_cooldown_start[i]).count();

            if (elapsed >= m_gem_cooldown_duration[i]) {
                m_gem_states[i] = GemState::Ready;
                LOG_DEBUG(MOD_SPELL, "Gem {} cooldown complete", i + 1);
            }
        }
    }
}

void SpellManager::updateMemorization(float delta_time)
{
    if (!m_is_memorizing) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_memorize_start_time).count();

    if (elapsed >= m_memorize_duration_ms) {
        // Memorization complete
        m_spell_gems[m_memorize_slot] = m_memorize_spell_id;
        m_gem_states[m_memorize_slot] = GemState::Ready;
        m_is_memorizing = false;

        const SpellData* spell = m_spell_db.getSpell(m_memorize_spell_id);
        LOG_INFO(MOD_SPELL, "Memorized spell '{}' in gem {}",
            spell ? spell->name : "Unknown", m_memorize_slot + 1);
    }
}

// ============================================================================
// Casting - Player Initiated
// ============================================================================

CastResult SpellManager::beginCastFromGem(uint8_t gem_slot, uint16_t target_id)
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return CastResult::InvalidSpell;
    }

    uint32_t spell_id = m_spell_gems[gem_slot];
    if (spell_id == SPELL_UNKNOWN) {
        return CastResult::NotMemorized;
    }

    // Check gem state
    GemState state = m_gem_states[gem_slot];
    if (state == GemState::Casting) {
        return CastResult::AlreadyCasting;
    }
    if (state == GemState::Refresh) {
        return CastResult::GemCooldown;
    }
    if (state == GemState::MemorizeProgress) {
        return CastResult::SpellNotReady;
    }
    if (state == GemState::Empty) {
        return CastResult::NotMemorized;
    }

    // Get spell data
    const SpellData* spell = m_spell_db.getSpell(spell_id);
    if (!spell) {
        return CastResult::InvalidSpell;
    }

    // Use current target if none specified
    if (target_id == 0) {
        target_id = m_eq->GetCombatManager()->GetTargetId();
    }

    // Validate the cast
    CastResult result = validateCast(*spell, target_id);
    if (result != CastResult::Success) {
        return result;
    }

    // Set up casting state
    m_is_casting = true;
    m_current_spell_id = spell_id;
    m_current_target_id = target_id;
    m_current_gem_slot = gem_slot;
    m_cast_start_time = std::chrono::steady_clock::now();
    m_cast_duration_ms = spell->cast_time_ms;
    m_waiting_for_server_confirm = true;

    // Update gem state
    m_gem_states[gem_slot] = GemState::Casting;

    // Send cast packet to server
    sendCastSpellPacket(spell_id, gem_slot, target_id);

    LOG_INFO(MOD_SPELL, "Beginning cast of '{}' (ID: {}) on target {}",
        spell->name, spell_id, target_id);

    return CastResult::Success;
}

CastResult SpellManager::beginCastById(uint32_t spell_id, uint16_t target_id)
{
    // Find which gem has this spell
    uint8_t gem_slot = findGemSlotForSpell(spell_id);
    if (gem_slot < MAX_SPELL_GEMS) {
        return beginCastFromGem(gem_slot, target_id);
    }

    // Spell not memorized - check if we can cast it anyway (item/scroll)
    const SpellData* spell = m_spell_db.getSpell(spell_id);
    if (!spell) {
        return CastResult::InvalidSpell;
    }

    // For non-memorized casts, we still need to validate
    if (target_id == 0) {
        target_id = m_eq->GetCombatManager()->GetTargetId();
    }

    CastResult result = validateCast(*spell, target_id);
    if (result != CastResult::Success) {
        return result;
    }

    // Set up casting state (no gem slot)
    m_is_casting = true;
    m_current_spell_id = spell_id;
    m_current_target_id = target_id;
    m_current_gem_slot = 0xFF;  // Invalid slot marker for item/scroll casts
    m_cast_start_time = std::chrono::steady_clock::now();
    m_cast_duration_ms = spell->cast_time_ms;
    m_waiting_for_server_confirm = true;

    // Send cast packet (slot 0xFF or use inventory slot for item casts)
    sendCastSpellPacket(spell_id, 0xFF, target_id);

    LOG_INFO(MOD_SPELL, "Beginning item/scroll cast of '{}' (ID: {})",
        spell->name, spell_id);

    return CastResult::Success;
}

void SpellManager::interruptCast()
{
    if (!m_is_casting) {
        return;
    }

    LOG_DEBUG(MOD_SPELL, "Player interrupting cast of spell {}", m_current_spell_id);

    // Send interrupt packet to server
    sendInterruptPacket();

    // Reset local state
    m_is_casting = false;
    m_waiting_for_server_confirm = false;

    // Reset gem state if it was a gem cast
    if (m_current_gem_slot < MAX_SPELL_GEMS) {
        m_gem_states[m_current_gem_slot] = GemState::Ready;
    }

    // Notify callback
    if (m_on_cast_complete) {
        m_on_cast_complete(CastResult::Interrupted, m_current_spell_id);
    }

    m_current_spell_id = SPELL_UNKNOWN;
    m_current_target_id = 0;
}

float SpellManager::getCastProgress() const
{
    if (!m_is_casting || m_cast_duration_ms == 0) {
        return 0.0f;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_cast_start_time).count();

    return std::min(1.0f, static_cast<float>(elapsed) / m_cast_duration_ms);
}

uint32_t SpellManager::getCastTimeRemaining() const
{
    if (!m_is_casting) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_cast_start_time).count();

    if (elapsed >= m_cast_duration_ms) {
        return 0;
    }

    return m_cast_duration_ms - static_cast<uint32_t>(elapsed);
}

// ============================================================================
// Cast Validation
// ============================================================================

CastResult SpellManager::validateCast(const SpellData& spell, uint16_t target_id)
{
    // Check if already casting
    if (m_is_casting) {
        return CastResult::AlreadyCasting;
    }

    // Check player state
    if (!checkPlayerState()) {
        // Could be stunned, feared, silenced, etc.
        return CastResult::Stunned;  // Simplified - could be more specific
    }

    // Check mana
    if (!checkMana(spell)) {
        return CastResult::NotEnoughMana;
    }

    // Check range (if target required)
    if (spell.target_type != SpellTargetType::Self && target_id != 0) {
        if (!checkRange(spell, target_id)) {
            return CastResult::OutOfRange;
        }

        // Check line of sight
        if (!checkLineOfSight(target_id)) {
            return CastResult::NoLineOfSight;
        }
    }

    // Check for self-only spells with target
    if (spell.target_type == SpellTargetType::Self && target_id != 0 &&
        target_id != m_eq->GetEntityID()) {
        // Self-only spell but targeting someone else - that's fine, just cast on self
    }

    // Check reagents
    if (spell.requiresReagents() && !checkReagents(spell)) {
        return CastResult::ComponentMissing;
    }

    return CastResult::Success;
}

bool SpellManager::checkMana(const SpellData& spell) const
{
    if (spell.mana_cost <= 0) {
        return true;  // Free spell or mana-returning spell
    }

    return m_eq->GetCurrentMana() >= static_cast<uint32_t>(spell.mana_cost);
}

bool SpellManager::checkRange(const SpellData& spell, uint16_t target_id) const
{
    if (spell.range <= 0) {
        return true;  // No range limit
    }

    // Get player and target positions
    glm::vec3 player_pos = m_eq->GetPosition();
    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(target_id);
    if (it == entities.end()) {
        return false;  // Target doesn't exist
    }

    glm::vec3 target_pos(it->second.x, it->second.y, it->second.z);
    float distance = glm::distance(player_pos, target_pos);

    return distance <= spell.range;
}

bool SpellManager::checkLineOfSight(uint16_t target_id) const
{
    // For now, always return true - actual LOS checks would require map data
    // TODO: Implement proper LOS check using raycast
    return true;
}

bool SpellManager::checkReagents(const SpellData& spell) const
{
    // For now, assume reagents are available
    // TODO: Check inventory for required reagents
    return true;
}

bool SpellManager::checkPlayerState() const
{
    // TODO: Check if player is stunned, feared, silenced, etc.
    // For now, always allow casting
    return true;
}

// ============================================================================
// Spell Gems
// ============================================================================

bool SpellManager::memorizeSpell(uint32_t spell_id, uint8_t gem_slot)
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return false;
    }

    const SpellData* spell = m_spell_db.getSpell(spell_id);
    if (!spell) {
        LOG_WARN(MOD_SPELL, "Cannot memorize unknown spell {}", spell_id);
        return false;
    }

    // Check if spell is scribed
    if (!hasSpellScribed(spell_id)) {
        LOG_WARN(MOD_SPELL, "Cannot memorize spell '{}' - not in spellbook", spell->name);
        return false;
    }

    // Check level requirement
    PlayerClass pc = static_cast<PlayerClass>(m_eq->GetClass());
    if (!spell->canClassUse(pc, m_eq->GetLevel())) {
        LOG_WARN(MOD_SPELL, "Cannot memorize spell '{}' - level too low", spell->name);
        return false;
    }

    // Check if already casting or memorizing
    if (m_is_casting || m_is_memorizing) {
        return false;
    }

    // If slot has a spell, forget it first
    if (m_spell_gems[gem_slot] != SPELL_UNKNOWN) {
        forgetSpell(gem_slot);
    }

    // Start memorization
    m_is_memorizing = true;
    m_memorize_slot = gem_slot;
    m_memorize_spell_id = spell_id;
    m_memorize_start_time = std::chrono::steady_clock::now();
    m_memorize_duration_ms = calculateMemorizeTime(spell_id);
    m_gem_states[gem_slot] = GemState::MemorizeProgress;

    // Send memorize packet to server
    sendMemorizePacket(spell_id, gem_slot, true);

    LOG_INFO(MOD_SPELL, "Starting memorization of '{}' in gem {} ({}ms)",
        spell->name, gem_slot + 1, m_memorize_duration_ms);

    return true;
}

bool SpellManager::forgetSpell(uint8_t gem_slot)
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return false;
    }

    if (m_spell_gems[gem_slot] == SPELL_UNKNOWN) {
        return false;  // Already empty
    }

    // Can't forget while casting
    if (m_is_casting && m_current_gem_slot == gem_slot) {
        return false;
    }

    uint32_t spell_id = m_spell_gems[gem_slot];
    m_spell_gems[gem_slot] = SPELL_UNKNOWN;
    m_gem_states[gem_slot] = GemState::Empty;

    // Send forget packet to server
    sendMemorizePacket(spell_id, gem_slot, false);

    LOG_DEBUG(MOD_SPELL, "Forgot spell from gem {}", gem_slot + 1);

    return true;
}

bool SpellManager::swapGems(uint8_t slot1, uint8_t slot2)
{
    if (slot1 >= MAX_SPELL_GEMS || slot2 >= MAX_SPELL_GEMS) {
        return false;
    }

    if (slot1 == slot2) {
        return false;
    }

    // Can't swap while casting from either slot
    if (m_is_casting && (m_current_gem_slot == slot1 || m_current_gem_slot == slot2)) {
        return false;
    }

    // Can't swap if either is on cooldown or memorizing
    if (m_gem_states[slot1] == GemState::Refresh ||
        m_gem_states[slot1] == GemState::MemorizeProgress ||
        m_gem_states[slot2] == GemState::Refresh ||
        m_gem_states[slot2] == GemState::MemorizeProgress) {
        return false;
    }

    // Swap
    std::swap(m_spell_gems[slot1], m_spell_gems[slot2]);
    std::swap(m_gem_states[slot1], m_gem_states[slot2]);

    LOG_DEBUG(MOD_SPELL, "Swapped gems {} and {}", slot1 + 1, slot2 + 1);

    return true;
}

uint32_t SpellManager::getMemorizedSpell(uint8_t gem_slot) const
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return SPELL_UNKNOWN;
    }
    return m_spell_gems[gem_slot];
}

GemState SpellManager::getGemState(uint8_t gem_slot) const
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return GemState::Empty;
    }
    return m_gem_states[gem_slot];
}

float SpellManager::getGemCooldownProgress(uint8_t gem_slot) const
{
    if (gem_slot >= MAX_SPELL_GEMS || m_gem_states[gem_slot] != GemState::Refresh) {
        return 1.0f;  // Ready
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_gem_cooldown_start[gem_slot]).count();

    if (m_gem_cooldown_duration[gem_slot] == 0) {
        return 1.0f;
    }

    return std::min(1.0f, static_cast<float>(elapsed) / m_gem_cooldown_duration[gem_slot]);
}

uint32_t SpellManager::getGemCooldownRemaining(uint8_t gem_slot) const
{
    if (gem_slot >= MAX_SPELL_GEMS || m_gem_states[gem_slot] != GemState::Refresh) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_gem_cooldown_start[gem_slot]).count();

    if (elapsed >= m_gem_cooldown_duration[gem_slot]) {
        return 0;
    }

    return m_gem_cooldown_duration[gem_slot] - static_cast<uint32_t>(elapsed);
}

float SpellManager::getMemorizeProgress(uint8_t gem_slot) const
{
    if (!m_is_memorizing || gem_slot != m_memorize_slot) {
        return 1.0f;  // Not memorizing or different slot
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_memorize_start_time).count();

    if (m_memorize_duration_ms == 0) {
        return 1.0f;
    }

    return std::min(1.0f, static_cast<float>(elapsed) / m_memorize_duration_ms);
}

uint32_t SpellManager::calculateMemorizeTime(uint32_t spell_id) const
{
    // Memorization time decreases with level
    uint8_t level = m_eq->GetLevel();

    if (level >= 50) return 500;   // Nearly instant at 50+
    if (level >= 40) return 1000;  // 1 second
    if (level >= 30) return 2000;  // 2 seconds
    if (level >= 20) return 4000;  // 4 seconds
    return 8000;                    // 8 seconds for low levels
}

uint8_t SpellManager::findGemSlotForSpell(uint32_t spell_id) const
{
    for (uint8_t i = 0; i < MAX_SPELL_GEMS; ++i) {
        if (m_spell_gems[i] == spell_id) {
            return i;
        }
    }
    return 0xFF;  // Not found
}

// ============================================================================
// Spellbook
// ============================================================================

bool SpellManager::scribeSpell(uint32_t spell_id, uint16_t book_slot)
{
    if (book_slot >= MAX_SPELLBOOK_SLOTS) {
        return false;
    }

    if (!m_spell_db.hasSpell(spell_id)) {
        return false;
    }

    m_spellbook[book_slot] = spell_id;
    return true;
}

bool SpellManager::hasSpellScribed(uint32_t spell_id) const
{
    for (uint16_t i = 0; i < MAX_SPELLBOOK_SLOTS; ++i) {
        if (m_spellbook[i] == spell_id) {
            return true;
        }
    }
    return false;
}

uint32_t SpellManager::getSpellbookSpell(uint16_t slot) const
{
    if (slot >= MAX_SPELLBOOK_SLOTS) {
        return SPELL_UNKNOWN;
    }
    return m_spellbook[slot];
}

std::vector<uint32_t> SpellManager::getScribedSpells() const
{
    std::vector<uint32_t> spells;
    for (uint16_t i = 0; i < MAX_SPELLBOOK_SLOTS; ++i) {
        if (m_spellbook[i] != SPELL_UNKNOWN) {
            spells.push_back(m_spellbook[i]);
        }
    }
    return spells;
}

void SpellManager::setSpellbook(const uint32_t* spells, size_t count)
{
    size_t max_count = std::min(count, static_cast<size_t>(MAX_SPELLBOOK_SLOTS));
    for (size_t i = 0; i < max_count; ++i) {
        m_spellbook[i] = spells[i];
    }

    LOG_DEBUG(MOD_SPELL, "Set {} spells in spellbook", max_count);
}

void SpellManager::setSpellGems(const uint32_t* gems, size_t count)
{
    size_t max_count = std::min(count, static_cast<size_t>(MAX_SPELL_GEMS));
    for (size_t i = 0; i < max_count; ++i) {
        m_spell_gems[i] = gems[i];
        m_gem_states[i] = (gems[i] != SPELL_UNKNOWN && gems[i] != 0)
            ? GemState::Ready : GemState::Empty;
    }

    LOG_DEBUG(MOD_SPELL, "Set {} spell gems", max_count);
}

// ============================================================================
// Packet Handlers
// ============================================================================

void SpellManager::handleBeginCast(uint16_t caster_id, uint16_t spell_id, uint32_t cast_time_ms)
{
    const SpellData* spell = m_spell_db.getSpell(spell_id);
    std::string spell_name = spell ? spell->name : "Unknown";

    LOG_DEBUG(MOD_SPELL, "BeginCast: caster={}, spell='{}' ({}), time={}ms",
        caster_id, spell_name, spell_id, cast_time_ms);

    // If this is our cast, confirm it
    if (caster_id == m_eq->GetEntityID()) {
        if (m_is_casting && m_current_spell_id == spell_id) {
            // Spell gem cast confirmed by server
            m_waiting_for_server_confirm = false;
            m_cast_duration_ms = cast_time_ms;  // Use server's cast time
            m_cast_start_time = std::chrono::steady_clock::now();  // Reset timer

            LOG_INFO(MOD_SPELL, "Server confirmed cast of '{}'", spell_name);
        } else {
            // Item click or other cast source - start tracking it now
            m_is_casting = true;
            m_current_spell_id = spell_id;
            m_cast_duration_ms = cast_time_ms;
            m_cast_start_time = std::chrono::steady_clock::now();
            m_waiting_for_server_confirm = false;
            m_current_gem_slot = 255;  // No gem (item cast)

            LOG_INFO(MOD_SPELL, "Item/other cast started: '{}'", spell_name);
        }

#ifdef EQT_HAS_GRAPHICS
        auto* renderer = m_eq->GetRenderer();
        if (renderer) {
            // Show player's casting bar
            if (renderer->getWindowManager()) {
                renderer->getWindowManager()->startCast(spell_name, cast_time_ms);
            }

            // Create cast glow effect for the player too
            if (renderer->getSpellVisualFX()) {
                renderer->getSpellVisualFX()->createCastGlow(caster_id, spell_id, cast_time_ms);
            }
        }
#endif
    } else {
        // NPC or other player casting
        // Show chat message
        if (spell) {
            showBeginCastMessage(caster_id, spell);
        }

#ifdef EQT_HAS_GRAPHICS
        // Get caster name for target casting bar (converted to display format)
        std::string caster_name = "Unknown";
        const auto& entities = m_eq->GetEntities();
        auto it = entities.find(caster_id);
        if (it != entities.end()) {
            caster_name = EQT::toDisplayName(it->second.name);
        }

        // If this is our current target, show target casting bar
        CombatManager* combat = m_eq->GetCombatManager();
        if (combat && caster_id == combat->GetTargetId()) {
            m_target_caster_id = caster_id;
            m_target_spell_id = spell_id;

            auto* renderer = m_eq->GetRenderer();
            if (renderer && renderer->getWindowManager()) {
                renderer->getWindowManager()->startTargetCast(caster_name, spell_name, cast_time_ms);
            }
        }

        auto* renderer = m_eq->GetRenderer();
        if (renderer) {
            // Don't change animation during cast - entity stays in idle
            // The particle effects and casting bar indicate the cast is in progress

            // Start entity casting bar (shows above the entity's head)
            renderer->getEntityRenderer()->startEntityCast(caster_id, spell_id, spell_name, cast_time_ms);

            // Create cast glow effect with spiraling particles
            if (renderer->getSpellVisualFX()) {
                renderer->getSpellVisualFX()->createCastGlow(caster_id, spell_id, cast_time_ms);
            }
        }
#endif
    }
}

void SpellManager::handleAction(const EQT::Action_Struct& action)
{
    // Type 231 (0xE7) indicates spell effect
    if (action.type != 231) {
        return;
    }

    uint32_t spell_id = action.spell;
    uint16_t caster_id = static_cast<uint16_t>(action.source);
    uint16_t target_id = static_cast<uint16_t>(action.target);

    const SpellData* spell = m_spell_db.getSpell(spell_id);
    std::string spell_name = spell ? spell->name : "Unknown";

    LOG_DEBUG(MOD_SPELL, "SpellAction: caster={}, target={}, spell='{}' ({}), effect_flag={}",
        caster_id, target_id, spell_name, spell_id, action.effect_flag);

    // effect_flag meanings:
    // 0 = initial spell animation packet (first of two action packets)
    // 4 = success flag (second packet, causes buff icon to appear)
    // Resists are communicated via separate message packets, not effect_flag
    bool is_success_packet = (action.effect_flag == 4);

#ifdef EQT_HAS_GRAPHICS
    auto* renderer = m_eq->GetRenderer();
    if (renderer) {
        // Remove cast glow from caster
        if (renderer->getSpellVisualFX()) {
            renderer->getSpellVisualFX()->removeCastGlow(caster_id);
        }

        // Complete entity casting bar (shown above entity's head)
        renderer->getEntityRenderer()->completeEntityCast(caster_id);

        // Create spell completion effect (spiraling particles that burst outward)
        if (is_success_packet && renderer->getSpellVisualFX()) {
            renderer->getSpellVisualFX()->createSpellComplete(caster_id, spell_id);
            // Create impact effect at target
            renderer->getSpellVisualFX()->createImpact(target_id, spell_id);
        }

        // Play completion animation on the caster (only for other entities, not player)
        // Animation depends on spell type: damage spells use combat animation, buffs use prayer
        if (is_success_packet && spell && caster_id != m_eq->GetEntityID()) {
            std::string completion_anim;
            if (spell->isDamageSpell() || spell->isDetrimental()) {
                // Damage/detrimental spells: combat cast animation
                completion_anim = "c03";
            } else if (spell->is_beneficial) {
                // Beneficial spells (buffs, heals): prayer cast animation
                completion_anim = "c04";
            } else {
                // Default: standard cast animation
                completion_anim = "c01";
            }
            // Play as non-looping with playThrough so it completes before returning to idle
            renderer->setEntityAnimation(caster_id, completion_anim, false, true);
        }
    }

    // Complete target casting bar if this was our target
    if (caster_id == m_target_caster_id && spell_id == m_target_spell_id) {
        if (renderer && renderer->getWindowManager()) {
            renderer->getWindowManager()->completeTargetCast();
        }
        m_target_caster_id = 0;
        m_target_spell_id = SPELL_UNKNOWN;
    }
#endif

    // Display spell landing messages only on success packet
    if (spell && is_success_packet) {
        showSpellMessages(spell, caster_id, target_id, false);  // Not resisted
    }

    // If this is our cast completing
    if (caster_id == m_eq->GetEntityID() && m_is_casting) {
        completeCast(true);

#ifdef EQT_HAS_GRAPHICS
        // Complete player's casting bar
        auto* renderer = m_eq->GetRenderer();
        if (renderer && renderer->getWindowManager()) {
            renderer->getWindowManager()->completeCast();
        }
#endif
    }

    // Notify callback
    if (m_on_spell_land) {
        m_on_spell_land(spell_id, caster_id, target_id);
    }
}

void SpellManager::handleInterrupt(uint16_t caster_id, uint16_t spell_id, uint8_t message_type)
{
    LOG_DEBUG(MOD_SPELL, "Interrupt: caster={}, spell={}, type={}",
        caster_id, spell_id, message_type);

#ifdef EQT_HAS_GRAPHICS
    auto* renderer = m_eq->GetRenderer();
    if (renderer) {
        // Remove cast glow from caster
        if (renderer->getSpellVisualFX()) {
            renderer->getSpellVisualFX()->removeCastGlow(caster_id);
        }

        // Cancel entity casting bar (shown above entity's head)
        renderer->getEntityRenderer()->cancelEntityCast(caster_id);
    }

    // Cancel target casting bar if this was our target
    if (caster_id == m_target_caster_id && spell_id == m_target_spell_id) {
        if (renderer && renderer->getWindowManager()) {
            renderer->getWindowManager()->cancelTargetCast();
        }
        m_target_caster_id = 0;
        m_target_spell_id = SPELL_UNKNOWN;
    }
#endif

    // If this is our cast being interrupted
    if (caster_id == m_eq->GetEntityID() && m_is_casting) {
        m_is_casting = false;
        m_waiting_for_server_confirm = false;

        // Reset gem state
        if (m_current_gem_slot < MAX_SPELL_GEMS) {
            m_gem_states[m_current_gem_slot] = GemState::Ready;
        }

        // Determine interrupt type
        CastResult result = CastResult::Interrupted;
        if (message_type == 2) {
            result = CastResult::Fizzle;
        }

#ifdef EQT_HAS_GRAPHICS
        // Cancel player's casting bar
        auto* renderer = m_eq->GetRenderer();
        if (renderer && renderer->getWindowManager()) {
            renderer->getWindowManager()->cancelCast();
        }
#endif

        // Notify callback
        if (m_on_cast_complete) {
            m_on_cast_complete(result, spell_id);
        }

        const SpellData* spell = m_spell_db.getSpell(spell_id);
        LOG_INFO(MOD_SPELL, "Cast of '{}' {} ",
            spell ? spell->name : "Unknown",
            result == CastResult::Fizzle ? "fizzled" : "was interrupted");

        m_current_spell_id = SPELL_UNKNOWN;
        m_current_target_id = 0;
    }
}

void SpellManager::handleManaChange(uint32_t new_mana, uint32_t stamina, uint32_t spell_id)
{
    LOG_DEBUG(MOD_SPELL, "ManaChange: mana={}, stamina={}, spell={}",
        new_mana, stamina, spell_id);

    // Mana is updated in EverQuest class, we just log it here
}

// ============================================================================
// Packet Sending
// ============================================================================

void SpellManager::sendCastSpellPacket(uint32_t spell_id, uint8_t gem_slot, uint16_t target_id)
{
    EQ::Net::DynamicPacket packet;
    packet.Resize(sizeof(EQT::CastSpell_Struct));

    EQT::CastSpell_Struct* cs = reinterpret_cast<EQT::CastSpell_Struct*>(packet.Data());
    cs->slot = gem_slot;
    cs->spell_id = spell_id;
    cs->inventoryslot = 0xFFFFFFFF;  // 0xFFFFFFFF for normal cast
    cs->target_id = target_id;
    std::memset(cs->cs_unknown, 0, sizeof(cs->cs_unknown));

    m_eq->QueuePacket(HC_OP_CastSpell, &packet);

    LOG_DEBUG(MOD_SPELL, "Sent CastSpell packet: spell={}, gem={}, target={}",
        spell_id, gem_slot, target_id);
}

void SpellManager::sendInterruptPacket()
{
    EQ::Net::DynamicPacket packet;
    packet.Resize(4);
    packet.PutUInt16(0, m_eq->GetEntityID());
    packet.PutUInt16(2, 0x01);  // Interrupt type

    m_eq->QueuePacket(HC_OP_InterruptCast, &packet);

    LOG_DEBUG(MOD_SPELL, "Sent InterruptCast packet");
}

void SpellManager::sendMemorizePacket(uint32_t spell_id, uint8_t gem_slot, bool memorize)
{
    // TODO: Determine correct packet format for memorize/forget
    // For now, this is handled server-side via sitting and time

    LOG_DEBUG(MOD_SPELL, "Would send {} packet for spell {} in gem {}",
        memorize ? "memorize" : "forget", spell_id, gem_slot);
}

// ============================================================================
// Internal State Management
// ============================================================================

void SpellManager::completeCast(bool success)
{
    if (!m_is_casting) {
        return;
    }

    const SpellData* spell = m_spell_db.getSpell(m_current_spell_id);

    m_is_casting = false;
    m_waiting_for_server_confirm = false;

    if (success) {
        // Start gem cooldown
        if (m_current_gem_slot < MAX_SPELL_GEMS && spell) {
            startGemCooldown(m_current_gem_slot, spell->recast_time_ms);
        }

        // Notify callback
        if (m_on_cast_complete) {
            m_on_cast_complete(CastResult::Success, m_current_spell_id);
        }

        LOG_INFO(MOD_SPELL, "Cast of '{}' completed successfully",
            spell ? spell->name : "Unknown");
    }

    m_current_spell_id = SPELL_UNKNOWN;
    m_current_target_id = 0;
}

void SpellManager::startGemCooldown(uint8_t gem_slot, uint32_t duration_ms)
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return;
    }

    // Minimum cooldown (global cooldown equivalent)
    if (duration_ms < 1500) {
        duration_ms = 1500;
    }

    m_gem_states[gem_slot] = GemState::Refresh;
    m_gem_cooldown_start[gem_slot] = std::chrono::steady_clock::now();
    m_gem_cooldown_duration[gem_slot] = duration_ms;

    LOG_DEBUG(MOD_SPELL, "Gem {} on cooldown for {}ms", gem_slot + 1, duration_ms);
}

// ============================================================================
// Spell Message Display
// ============================================================================

void SpellManager::showSpellMessages(const SpellData* spell, uint16_t caster_id,
                                      uint16_t target_id, bool resisted)
{
    if (!spell) {
        return;
    }

    // Get entity names (converted to display format)
    std::string caster_name = "Unknown";
    std::string target_name = "Unknown";
    bool target_is_player = (target_id == m_eq->GetEntityID());
    bool caster_is_player = (caster_id == m_eq->GetEntityID());

    const auto& entities = m_eq->GetEntities();

    auto caster_it = entities.find(caster_id);
    if (caster_it != entities.end()) {
        caster_name = EQT::toDisplayName(caster_it->second.name);
    }

    auto target_it = entities.find(target_id);
    if (target_it != entities.end()) {
        target_name = EQT::toDisplayName(target_it->second.name);
    }

    // Handle resist message
    if (resisted) {
        if (target_is_player) {
            m_eq->AddChatSystemMessage("You resist the spell!");
        } else if (!target_name.empty()) {
            m_eq->AddChatSystemMessage(target_name + " resists the spell!");
        }
        return;
    }

    // Display landing message
    if (target_is_player) {
        // "You" message (cast_on_you)
        if (!spell->cast_on_you.empty()) {
            m_eq->AddChatSystemMessage(spell->cast_on_you);
        }
    } else {
        // "Other" message with name substitution (cast_on_other)
        if (!spell->cast_on_other.empty()) {
            std::string msg = spell->cast_on_other;
            size_t pos = msg.find("%s");
            if (pos != std::string::npos) {
                msg.replace(pos, 2, target_name);
            }
            m_eq->AddChatSystemMessage(msg);
        }
    }
}

void SpellManager::showBeginCastMessage(uint16_t caster_id, const SpellData* spell)
{
    if (!spell) {
        return;
    }

    // Get caster name
    std::string caster_name = "Unknown";
    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(caster_id);
    if (it != entities.end()) {
        caster_name = EQT::toDisplayName(it->second.name);
    }

    // Display "X begins casting Y" message
    std::string message = caster_name + " begins casting " + spell->name + ".";
    m_eq->AddChatSystemMessage(message);
}

} // namespace EQ
