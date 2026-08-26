/*
 * T246: SpellManager's local cast pre-checks -- the three that were
 * `return true` stubs from the day SpellManager was written until T246,
 * which left T244's Stunned / Silenced / ComponentMissing / NoLineOfSight
 * refusals unreachable (the server refused instead, one round trip later,
 * and for reagents with no local message at all).
 *
 * Each check is permissive wherever the client has no state to judge from;
 * the comments on each say exactly what is and is not covered. See
 * refactor/T246_cast_check_stubs.md.
 */

#include "client/spell/spell_manager.h"
#include "client/eq.h"
#include "client/hc_map.h"
#include "client/inventory/inventory_manager.h"
#include "client/spell/buff_manager.h"
#include "common/logging.h"

#include <glm/glm.hpp>
#include <memory>

namespace EQ {

// T246. Ray from the player's eye point to the target's, against the zone's
// collision map (the same eqemu .map the server runs CheckLosFN on).
// Permissive when no map is loaded or the target is unknown -- those are
// the server's calls, not a local refusal. Eye height follows eqemu's
// CheckLosFN: server z (model centre) + size/2 * 0.9, size 6 when the
// entity reports none. Entity z IS the server's model-centre z; the
// player's m_z is ground level (eq_bridge.cpp), so it gets +size/2 first.
bool SpellManager::checkLineOfSight(uint16_t target_id) const
{
    std::shared_ptr<const HCMap> map = m_eq->GetZoneMap();
    if (!map || !map->IsLoaded()) {
        return true;
    }

    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(target_id);
    if (it == entities.end()) {
        return true;
    }

    constexpr float kDefaultSize = 6.0f;
    constexpr float kHeadPosition = 0.9f;
    auto eyeZ = [](float z, float size) {
        return z + ((size > 0.0f ? size : kDefaultSize) / 2.0f) * kHeadPosition;
    };

    glm::vec3 from = m_eq->GetPosition();
    float mySize = m_eq->GetSize() > 0.0f ? m_eq->GetSize() : kDefaultSize;
    from.z = eyeZ(from.z + mySize / 2.0f, mySize);
    glm::vec3 to(it->second.x, it->second.y, eyeZ(it->second.z, it->second.size));

    bool clear = map->CheckLOS(from, to);
    if (!clear) {
        LOG_DEBUG(MOD_SPELL, "checkLineOfSight: target {} blocked from ({:.1f},{:.1f},{:.1f}) to ({:.1f},{:.1f},{:.1f})",
            target_id, from.x, from.y, from.z, to.x, to.y, to.z);
    }
    return clear;
}

uint32_t SpellManager::countCarriedItem(uint32_t item_id) const
{
    const auto* inv = m_eq->GetInventoryManager();
    if (!inv || item_id == 0) {
        return 0;
    }

    using namespace eqt::inventory;
    uint32_t total = 0;
    auto countSlot = [&](int16_t slot) {
        const ItemInstance* item = inv->getItem(slot);
        if (item && item->itemId == item_id) {
            total += item->stackable && item->quantity > 0 ? static_cast<uint32_t>(item->quantity) : 1u;
        }
    };
    for (int16_t slot = EQUIPMENT_BEGIN; slot <= EQUIPMENT_END; ++slot) countSlot(slot);
    for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END; ++slot) countSlot(slot);
    for (int16_t slot = GENERAL_BAGS_BEGIN; slot <= GENERAL_BAGS_END; ++slot) countSlot(slot);
    return total;
}

// T246. Every consumed component (reagent_id/reagent_count, populated from
// willeq.db since T246) must be carried in at least the listed count.
// Deliberately NOT checked: noexpend_reagent_id (the held-not-consumed
// focus; carried in SpellData as plumbing only) and a reagent whose count
// is 0 in the data -- both left to the server rather than risk refusing a
// cast it would accept.
bool SpellManager::checkReagents(const SpellData& spell) const
{
    for (size_t i = 0; i < MAX_SPELL_REAGENTS; ++i) {
        uint32_t id = spell.reagent_id[i];
        uint32_t needed = spell.reagent_count[i];
        if (id == 0 || needed == 0) {
            continue;
        }
        uint32_t have = countCarriedItem(id);
        if (have < needed) {
            LOG_DEBUG(MOD_SPELL, "checkReagents: spell '{}' needs {} x item {}, carrying {}",
                spell.name, needed, id, have);
            return false;
        }
    }
    return true;
}

// T246. The only casting-blocker state this client tracks is the player's
// own buff list (BuffManager, fed by the PlayerProfile and OP_Buff), so
// the checks are: an active player buff whose spell carries SE_Stun /
// SE_Mez (-> Stunned, string 175), SE_Silence (-> Silenced, 207) or
// SE_Fear (-> Feared, no official string). NOT covered, by design: the
// instantaneous OP_Stun packet (eq.cpp logs it and stores nothing -- a
// duration-less stun never appears in the buff list) and any server-side
// state the client is never told about; those still fall to the server's
// own refusal.
CastResult SpellManager::checkPlayerState() const
{
    const BuffManager* buffs = m_eq->GetBuffManager();
    if (!buffs) {
        return CastResult::Success;
    }

    for (const ActiveBuff& buff : buffs->getPlayerBuffs()) {
        const SpellData* spell = m_spell_db.getSpell(buff.spell_id);
        if (!spell) {
            continue;
        }
        if (spell->hasEffect(SpellEffect::Stun) || spell->hasEffect(SpellEffect::Mez)) {
            LOG_DEBUG(MOD_SPELL, "checkPlayerState: stunned/mezzed by buff '{}'", spell->name);
            return CastResult::Stunned;
        }
        if (spell->hasEffect(SpellEffect::Silence)) {
            LOG_DEBUG(MOD_SPELL, "checkPlayerState: silenced by buff '{}'", spell->name);
            return CastResult::Silenced;
        }
        if (spell->hasEffect(SpellEffect::Fear)) {
            LOG_DEBUG(MOD_SPELL, "checkPlayerState: feared by buff '{}'", spell->name);
            return CastResult::Feared;
        }
    }
    return CastResult::Success;
}

} // namespace EQ
