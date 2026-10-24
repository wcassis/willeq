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
#include "client/spell/buff_manager.h"
#include "common/logging.h"

#include "client/position_frames.h"
#include <glm/glm.hpp>
#include <memory>

namespace EQ {

// T246. Ray from the player's eye point to the target's, against the zone's
// collision map (the same eqemu .map the server runs CheckLosFN on).
// Permissive when no map is loaded or the target is unknown -- those are
// the server's calls, not a local refusal. Eye height follows eqemu's
// CheckLosFN: server z (model centre) + size/2 * 0.9, size 6 when the
// entity reports none.
//
// T311 (closes T310 Leg B's note): the old +size/2 applied to the PLAYER here
// existed only to undo the unmatched size/2 that eq_entities.cpp subtracted on
// zone-in. Both are gone. GetPosition().z is now the RENDER frame (the model
// node's origin), while every OTHER entity's z is the WIRE frame straight off
// the packet, so the player has to be lifted into the wire frame before the two
// can be compared -- one call to renderZToWireZ, the same expression the send
// path uses (include/client/position_frames.h). After that both endpoints of
// the ray are in the server's own frame, which is the frame eqemu's CheckLosFN
// runs in, and eyeZ() applies identically to both.
bool SpellManager::checkLineOfSight(uint16_t target_id) const
{
    std::shared_ptr<const HCMap> map = m_eq->Zone().GetZoneMap();
    if (!map || !map->IsLoaded()) {
        return true;
    }

    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(target_id);
    if (it == entities.end()) {
        return true;
    }

    static constexpr float kDefaultSize = 6.0f;  // static: gcc 10 (lane 4) wants a capture for a plain constexpr local
    static constexpr float kHeadPosition = 0.9f;
    auto eyeZ = [](float z, float size) {
        return z + ((size > 0.0f ? size : kDefaultSize) / 2.0f) * kHeadPosition;
    };

    glm::vec3 from = m_eq->GetPosition();
    float mySize = m_eq->GetSize() > 0.0f ? m_eq->GetSize() : kDefaultSize;
    from.z = eyeZ(eqt::renderZToWireZ(from.z, m_eq->GetPlayerCollisionZOffset(), mySize), mySize);
    glm::vec3 to(it->second.x, it->second.y, eyeZ(it->second.z, it->second.size));

    bool clear = map->CheckLOS(from, to);
    if (!clear) {
        LOG_DEBUG(MOD_SPELL, "checkLineOfSight: target {} blocked from ({:.1f},{:.1f},{:.1f}) to ({:.1f},{:.1f},{:.1f})",
            target_id, from.x, from.y, from.z, to.x, to.y, to.z);
    }
    return clear;
}

// B1140: countCarriedItem() / checkReagents() (T246/T251) are gone. The
// official client refuses nothing for a missing component (oracle leg
// review/B1140/oracle/O1_wiz); the server judges components when the cast
// finishes. See validateCast() and refactor/B1140/README.md.

// T246/T247. Two sources of casting-blocker state: the instantaneous
// OP_Stun packet (T247: EverQuest::IsStunned(), a duration the server
// sends in ms, cleared by OP_Stun{0} or expiry -- checked first, it is the
// common melee-bash stun) and the player's own buff list (BuffManager, fed
// by the PlayerProfile and OP_Buff): an active player buff whose spell
// carries SE_Stun / SE_Mez (-> Stunned, string 175), SE_Silence
// (-> Silenced, 207) or SE_Fear (-> Feared, no official string). Any
// server-side state the client is never told about still falls to the
// server's own refusal.
CastResult SpellManager::checkPlayerState() const
{
    if (m_eq->Commands().IsStunned()) {
        LOG_DEBUG(MOD_SPELL, "checkPlayerState: stunned (OP_Stun in force)");
        return CastResult::Stunned;
    }

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
