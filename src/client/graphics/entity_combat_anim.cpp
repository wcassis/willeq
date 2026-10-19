#include "client/graphics/entity_renderer.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/animation_constants.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>
#include <chrono>
#include <fstream>
#include <fmt/ranges.h>

namespace EQT {
namespace Graphics {

void EntityRenderer::queueCombatAnimation(uint16_t sourceId, uint16_t targetId,
                                           uint8_t weaponSkill, int32_t damage, float damagePercent) {
    // Find the source entity to buffer attacks
    auto sourceIt = entities_.find(sourceId);
    if (sourceIt == entities_.end()) {
        return;
    }

    EntityVisual& source = sourceIt->second;
    auto now = std::chrono::steady_clock::now();

    // If buffer window has expired (>50ms since start), process the old buffer first
    if (source.combatBufferActive) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - source.combatAnimBufferStart).count();
        if (elapsed > 50) {
            // Process the existing buffer
            processCombatAnimationBufferForEntity(sourceId, targetId);
        }
    }

    // Start a new buffer window if needed
    if (!source.combatBufferActive || source.pendingCombatAnims.empty()) {
        source.combatAnimBufferStart = now;
        source.combatBufferActive = true;
    }

    // Add this damage packet to the buffer
    EntityVisual::PendingCombatAnim anim;
    anim.weaponSkill = weaponSkill;
    anim.damage = damage;
    anim.damagePercent = damagePercent;
    anim.targetId = targetId;
    anim.timestamp = now;
    source.pendingCombatAnims.push_back(anim);

    LOG_DEBUG(MOD_COMBAT, "Queued combat anim: source={} target={} skill={} dmg={} pct={:.1f} buffer_size={}",
              sourceId, targetId, weaponSkill, damage, damagePercent, source.pendingCombatAnims.size());
}

void EntityRenderer::queueReceivedDamageAnimation(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& entity = it->second;
    auto now = std::chrono::steady_clock::now();

    // If buffer window has expired (>50ms since start), process the old buffer first
    if (entity.combatBufferActive) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - entity.combatAnimBufferStart).count();
        if (elapsed > 50) {
            // Process the existing buffer
            processCombatAnimationBufferForEntity(spawnId, 0);
        }
    }

    // Start a new buffer window if needed
    if (!entity.combatBufferActive) {
        entity.combatAnimBufferStart = now;
        entity.combatBufferActive = true;
    }

    // Mark that this entity received damage during the buffer window
    entity.receivedDamageInBuffer = true;

    LOG_DEBUG(MOD_COMBAT, "Queued received damage for spawn={} (bufferActive={} pendingAttacks={})",
              spawnId, entity.combatBufferActive, entity.pendingCombatAnims.size());
}

void EntityRenderer::queueSkillAnimation(uint16_t spawnId, const std::string& animCode) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& entity = it->second;
    auto now = std::chrono::steady_clock::now();

    // If buffer window has expired (>50ms since start), process the old buffer first
    if (entity.combatBufferActive) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - entity.combatAnimBufferStart).count();
        if (elapsed > 50) {
            // Process the existing buffer
            processCombatAnimationBufferForEntity(spawnId, 0);
        }
    }

    // Start a new buffer window if needed
    if (!entity.combatBufferActive) {
        entity.combatAnimBufferStart = now;
        entity.combatBufferActive = true;
    }

    // Store the skill animation (overwrites any previous - only one skill per combat round)
    entity.queuedSkillAnim = animCode;

    LOG_DEBUG(MOD_COMBAT, "Queued skill animation '{}' for spawn={} (bufferActive={} pendingAttacks={})",
              animCode, spawnId, entity.combatBufferActive, entity.pendingCombatAnims.size());
}

void EntityRenderer::processCombatAnimationBufferForEntity(uint16_t sourceId, uint16_t /*targetIdHint*/) {
    auto sourceIt = entities_.find(sourceId);
    if (sourceIt == entities_.end()) {
        return;
    }

    EntityVisual& source = sourceIt->second;

    // Check if entity has no outgoing attacks (only skill animation and/or received damage)
    if (source.pendingCombatAnims.empty()) {
        // Play skill animation if queued (takes priority over damage)
        if (!source.queuedSkillAnim.empty() && source.isAnimated && source.animatedNode) {
            setEntityAnimation(sourceId, source.queuedSkillAnim, false, true);
            LOG_DEBUG(MOD_COMBAT, "Playing skill animation '{}' on source={} (no weapon attacks)",
                      source.queuedSkillAnim, sourceId);
        } else if (source.receivedDamageInBuffer && source.isAnimated && source.animatedNode) {
            // No attacks or skills, but received damage - play damage animation
            const char* damageAnim = EQ::ANIM_DAMAGE_MINOR;  // d01
            setEntityAnimation(sourceId, damageAnim, false, true);
            LOG_DEBUG(MOD_COMBAT, "Playing received damage animation '{}' on source={} (no outgoing attacks)",
                      damageAnim, sourceId);
        }
        source.combatBufferActive = false;
        source.receivedDamageInBuffer = false;
        source.queuedSkillAnim.clear();
        return;
    }

    // Entity has outgoing attacks - skill animation plays first, then weapon attacks
    // Received damage is skipped (attack animations take priority)
    if (source.receivedDamageInBuffer) {
        LOG_DEBUG(MOD_COMBAT, "Skipping received damage animation on source={} (has {} outgoing attacks)",
                  sourceId, source.pendingCombatAnims.size());
    }

    // Play skill animation first if queued (bash, kick, etc. before weapon swings)
    if (!source.queuedSkillAnim.empty() && source.isAnimated && source.animatedNode) {
        setEntityAnimation(sourceId, source.queuedSkillAnim, false, true);
        LOG_DEBUG(MOD_COMBAT, "Playing skill animation '{}' on source={} (before {} weapon attacks)",
                  source.queuedSkillAnim, sourceId, source.pendingCombatAnims.size());
    }

    // Get target ID from the first packet (all packets in burst should have same target)
    uint16_t targetId = source.pendingCombatAnims[0].targetId;

    // Count packets by weapon skill type to detect double/triple attack and dual wield
    std::map<uint8_t, int> skillCounts;
    float totalDamagePercent = 0.0f;
    int hitCount = 0;

    for (const auto& anim : source.pendingCombatAnims) {
        skillCounts[anim.weaponSkill]++;
        if (anim.damage > 0) {
            totalDamagePercent += anim.damagePercent;
            hitCount++;
        }
    }

    // Determine if this is dual wield (different weapon types in batch)
    bool isDualWield = skillCounts.size() > 1;

    // Separate main hand and offhand attacks
    uint8_t primarySkill = source.primaryWeaponSkill;
    uint8_t secondarySkill = source.secondaryWeaponSkill;

    // If we don't know weapon skills, use the first skill type seen as primary
    if (primarySkill == 255 && !skillCounts.empty()) {
        primarySkill = skillCounts.begin()->first;
    }

    int mainHandCount = 0;
    int offHandCount = 0;

    for (const auto& [skill, count] : skillCounts) {
        if (skill == primarySkill || !isDualWield) {
            mainHandCount += count;
        } else {
            offHandCount += count;
        }
    }

    LOG_DEBUG(MOD_COMBAT, "Processing combat buffer: source={} packets={} mainHand={} offHand={} dualWield={}",
              sourceId, source.pendingCombatAnims.size(), mainHandCount, offHandCount, isDualWield);

    // B1022 (#342): the swing rate is the server's OP_Animation / combat-burst
    // cadence (100 x item delay ms); each swing plays at the model's NATIVE
    // animation length (speed byte 10 = 1.0x). There is no per-entity weapon
    // delay to stretch to anymore -- see the tombstone on
    // setEntityWeaponDelay in entity_renderer.cpp. A double/triple burst plays
    // its queued swings back to back at native (playThrough), so nothing here
    // recomputes a duration from a delay.

    // Play attack animations on source
    if (source.isAnimated && source.animatedNode) {
        // Determine animation code based on primary weapon skill
        const char* attackAnim = EQ::getWeaponAttackAnimation(primarySkill, false, false);

        // Queue main hand attack(s)
        for (int i = 0; i < mainHandCount; i++) {
            // First animation plays immediately, subsequent ones queue
            bool playThrough = true;
            if (setEntityAnimation(sourceId, attackAnim, false, playThrough)) {
                LOG_DEBUG(MOD_COMBAT, "Playing main hand attack '{}' #{}/{} (native duration) on source={}",
                          attackAnim, i + 1, mainHandCount, sourceId);
            }
        }

        // Queue offhand attacks if dual wielding
        if (isDualWield && offHandCount > 0) {
            const char* offhandAnim = EQ::getWeaponAttackAnimation(secondarySkill, true, false);

            for (int i = 0; i < offHandCount; i++) {
                if (setEntityAnimation(sourceId, offhandAnim, false, true)) {
                    LOG_DEBUG(MOD_COMBAT, "Playing offhand attack '{}' #{}/{} (native duration) on source={}",
                              offhandAnim, i + 1, offHandCount, sourceId);
                }
            }
        }
    }

    // Play damage reaction on target if total damage >= 1% HP
    auto targetIt = entities_.find(targetId);
    if (targetIt != entities_.end() && hitCount > 0 && totalDamagePercent >= 1.0f) {
        EntityVisual& target = targetIt->second;
        if (target.isAnimated && target.animatedNode) {
            // Average damage percentage for animation selection
            float avgDamagePct = totalDamagePercent / hitCount;
            const char* damageAnim = EQ::getDamageAnimation(avgDamagePct, false, false);

            // Queue damage reaction to play after attack animations
            setEntityAnimation(targetId, damageAnim, false, true);
            LOG_DEBUG(MOD_COMBAT, "Playing damage reaction '{}' on target={} (total {:.1f}% damage from {} hits)",
                      damageAnim, targetId, totalDamagePercent, hitCount);
        }
    } else if (hitCount > 0) {
        LOG_DEBUG(MOD_COMBAT, "Skipping damage reaction: target={} totalDmg={:.1f}% < 1%",
                  targetId, totalDamagePercent);
    }

    // Clear the buffer
    source.pendingCombatAnims.clear();
    source.combatBufferActive = false;
    source.receivedDamageInBuffer = false;
    source.queuedSkillAnim.clear();
}

void EntityRenderer::processExpiredCombatBuffers() {
    auto now = std::chrono::steady_clock::now();

    // Track entities that need processing (can't modify during iteration)
    std::vector<std::pair<uint16_t, uint16_t>> toProcess;  // (sourceId, targetId)

    for (auto& [spawnId, visual] : entities_) {
        // Check entities with active combat buffer that have:
        // - pending outgoing attacks, OR
        // - received damage that needs processing, OR
        // - queued skill animation
        if (visual.combatBufferActive &&
            (!visual.pendingCombatAnims.empty() || visual.receivedDamageInBuffer ||
             !visual.queuedSkillAnim.empty())) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - visual.combatAnimBufferStart).count();
            if (elapsed > 50) {
                // Buffer window expired - we need to process it
                toProcess.push_back({spawnId, 0});
            }
        }
    }

    // Process expired buffers
    for (const auto& [sourceId, targetId] : toProcess) {
        processCombatAnimationBufferForEntity(sourceId, targetId);
    }
}

bool EntityRenderer::hasEntityPendingCombatAnims(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return false;
    }
    return it->second.combatBufferActive && !it->second.pendingCombatAnims.empty();
}

uint8_t EntityRenderer::getEntityPrimaryWeaponSkill(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return 255;  // WEAPON_NONE
    }
    return it->second.primaryWeaponSkill;
}

uint8_t EntityRenderer::getEntitySecondaryWeaponSkill(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return 255;  // WEAPON_NONE
    }
    return it->second.secondaryWeaponSkill;
}

bool EntityRenderer::isEntityPlayingThrough(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return false;
    }

    const EntityVisual& visual = it->second;
    if (!visual.isAnimated || !visual.animatedNode) {
        return false;
    }

    return visual.animatedNode->isPlayingThrough();
}

void EntityRenderer::stopEntityAnimation(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;
    if (visual.isAnimated && visual.animatedNode) {
        visual.animatedNode->stopAnimation();
        visual.currentAnimation.clear();
    }
}

std::string EntityRenderer::getEntityAnimation(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return "";
    }

    const EntityVisual& visual = it->second;
    if (visual.isAnimated && visual.animatedNode) {
        return visual.animatedNode->getCurrentAnimation();
    }
    return "";
}

bool EntityRenderer::hasEntityAnimation(uint16_t spawnId, const std::string& animCode) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return false;
    }

    const EntityVisual& visual = it->second;
    if (visual.isAnimated && visual.animatedNode) {
        return visual.animatedNode->hasAnimation(animCode);
    }
    return false;
}

std::vector<std::string> EntityRenderer::getEntityAnimationList(uint16_t spawnId) const {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return {};
    }

    const EntityVisual& visual = it->second;
    if (visual.isAnimated && visual.animatedNode) {
        return visual.animatedNode->getAnimationList();
    }
    return {};
}

void EntityRenderer::setGlobalAnimationSpeed(float speed) {
    // Clamp to reasonable range
    globalAnimationSpeed_ = std::max(0.1f, std::min(5.0f, speed));

    // Apply to all animated entities
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isAnimated && visual.animatedNode) {
            // setAnimationSpeed expects fps where 10 fps = 1.0x speed multiplier
            visual.animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed_);
        }
    }
}

} // namespace Graphics
} // namespace EQT
