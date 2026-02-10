#pragma once

#include "client/action/action_dispatcher.h"

// Forward declaration
class EverQuest;
class CombatManager;

namespace eqt {

/**
 * EqActionHandler - Adapter that connects ActionDispatcher to EverQuest.
 *
 * This class implements the IActionHandler interface and delegates all
 * action calls to the appropriate EverQuest methods. It serves as the
 * bridge between the new action system and the existing EverQuest class.
 */
class EqActionHandler : public action::IActionHandler {
public:
    /**
     * Construct an action handler for an EverQuest instance.
     *
     * @param eq The EverQuest client to delegate actions to
     */
    explicit EqActionHandler(EverQuest& eq);
    ~EqActionHandler() override;

    // ========== Movement ==========

    void startMoving(action::Direction dir) override;
    void stopMoving(action::Direction dir) override;
    void setHeading(float heading) override;
    void jump() override;
    void sit() override;
    void stand() override;
    void toggleAutorun() override;
    void stopAllMovement() override;

    void moveToLocation(float x, float y, float z) override;
    void moveToEntity(const std::string& name) override;
    void moveToEntityWithinRange(const std::string& name, float distance) override;
    void followEntity(const std::string& name) override;
    void stopFollow() override;

    // ========== Combat ==========

    void targetEntity(uint16_t spawnId) override;
    void targetEntityByName(const std::string& name) override;
    void targetNearest() override;
    void targetNearestPC() override;
    void targetNearestNPC() override;
    void cycleTargets() override;
    void cycleTargetsReverse() override;
    void clearTarget() override;

    void enableAutoAttack() override;
    void disableAutoAttack() override;
    void toggleAutoAttack() override;

    void castSpell(uint8_t gemSlot) override;
    void castSpellOnTarget(uint8_t gemSlot, uint16_t targetId) override;
    void interruptCast() override;

    void useAbility(uint32_t abilityId) override;
    void useSkill(uint32_t skillId) override;

    // ========== Interaction ==========

    void hail() override;
    void hailTarget() override;

    void clickDoor(uint8_t doorId) override;
    void clickNearestDoor() override;

    void lootCorpse(uint16_t corpseId) override;
    void lootItem(uint16_t corpseId, int16_t slot) override;
    void lootAll(uint16_t corpseId) override;

    void consider() override;

    // ========== Chat ==========

    void sendChatMessage(action::ChatChannel channel, const std::string& message) override;
    void sendTell(const std::string& target, const std::string& message) override;
    void replyToLastTell(const std::string& message) override;

    // ========== Group ==========

    void inviteToGroup(const std::string& playerName) override;
    void inviteTarget() override;
    void acceptGroupInvite() override;
    void declineGroupInvite() override;
    void leaveGroup() override;

    // ========== Character State ==========

    void setAFK(bool afk) override;
    void setAnonymous(bool anon) override;
    void toggleSneak() override;
    void startCamp() override;
    void cancelCamp() override;

    // ========== Inventory ==========

    void moveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity) override;
    void deleteItem(int16_t slot) override;
    void useItem(int16_t slot) override;

    // ========== Spellbook ==========

    void memorizeSpell(uint8_t gemSlot, uint32_t spellId) override;
    void forgetSpell(uint8_t gemSlot) override;
    void openSpellbook() override;
    void closeSpellbook() override;

    // ========== Trade ==========

    void requestTrade(uint16_t targetId) override;
    void acceptTrade() override;
    void cancelTrade() override;

    // ========== Zone ==========

    void requestZone(const std::string& zoneName) override;

    // ========== Pet ==========

    void sendPetCommand(uint8_t command, uint16_t targetId) override;
    void dismissPet() override;

    // ========== Tradeskill ==========

    void clickWorldObject(uint32_t dropId) override;
    void tradeskillCombine() override;

    // ========== Extended Movement ==========

    void setMovementMode(int mode) override;
    void setPositionState(int state) override;

    // ========== Extended Character State ==========

    void setRoleplay(bool rp) override;
    void setPathfinding(bool enabled) override;

    // ========== Extended Combat ==========

    void setAutoHunting(bool enabled) override;
    void setAutoLoot(bool enabled) override;
    void listHuntTargets() override;

    // ========== Entity Query ==========

    void listEntities(const std::string& filter) override;
    void dumpEntityAppearance(const std::string& name) override;

    // ========== Utility ==========

    void sendAnimation(uint8_t animationId, uint8_t speed) override;
    void sendPositionUpdate() override;

private:
    EverQuest& m_eq;

    // Helper to get the combat manager
    CombatManager* getCombatManager();

    // Convert ChatChannel enum to EQ channel string
    std::string channelToString(action::ChatChannel channel);
};

} // namespace eqt
