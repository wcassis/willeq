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
    void targetSelf() override;
    void targetGroupMember(int index) override;

    void enableAutoAttack() override;
    void disableAutoAttack() override;
    void toggleAutoAttack() override;

    // T102
    void assist(uint16_t targetId) override;
    void shieldTarget(uint16_t targetId) override;
    void taunt() override;
    void toggleAutoFire() override;
    void enableCombatManager() override;
    void disableCombatManager() override;
    void toggleCombatManager() override;
    void duelRequest(uint16_t targetId) override;
    void duelAccept() override;
    void duelDecline() override;

    void castSpell(uint8_t gemSlot) override;
    void castSpellOnTarget(uint8_t gemSlot, uint16_t targetId) override;
    void interruptCast() override;
    void beginCastFromGem(uint8_t gemSlot, uint16_t targetId) override;

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
    void memorizeSpellByName(const std::string& spellName, uint8_t gemSlot) override;
    void listMemorizedSpells() override;

    // ========== Trade ==========

    void requestTrade(uint16_t targetId) override;
    void acceptTrade() override;
    void cancelTrade() override;
    void declineTrade() override;

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

    // ========== Social (T102) ==========

    void inspectTarget(uint16_t targetId) override;
    void toggleLFG() override;
    void setSurname(const std::string& surname) override;
    void rollRandom(uint32_t low, uint32_t high) override;
    void yellForHelp() override;
    void requestWho(const std::string& name, int lvlLow, int lvlHigh) override;

    // ========== Guild (T132) ==========

    void guildInvite(const std::string& playerName) override;
    void guildAcceptInvite() override;
    void guildDeclineInvite() override;
    void guildRemove(const std::string& playerName) override;
    void guildDemote(const std::string& playerName) override;
    void guildMakeLeader(const std::string& playerName) override;
    void requestGuildMOTD() override;
    void setGuildMOTD(const std::string& motd) override;

    // Corpse / Resurrection (T134)
    void corpseDrag(const std::string& corpseName) override;
    void corpseDrop() override;
    void consent(const std::string& playerName) override;
    void consentDeny(const std::string& playerName) override;
    void rezAccept() override;
    void rezDecline() override;

    // ========== Utility (T135) ==========
    void split(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) override;
    void save() override;
    void bandage(uint16_t targetId) override;
    void rewind() override;
    void bug(const std::string& description) override;
    void petition(const std::string& text) override;

    // ========== Merchant (T136) ==========
    void vendorToggle() override;
    void vendorList() override;
    void buyFromVendor(uint32_t slot, uint8_t quantity) override;
    void sellToVendor(uint32_t slot, uint8_t quantity) override;

    // ========== Movement / Utility / Skills (T140) ==========
    void showLocation() override;
    void warp(const std::string& args) override;
    void showOrSetZoning(const std::string& args) override;
    void requestQuit() override;
    void campNow() override;
    void debugLevel(const std::string& args) override;
    void perfReport() override;
    void timeOfDay(const std::string& args) override;
    void bankToggle() override;
    void trainWithTarget() override;

private:
    EverQuest& m_eq;

    // Helper to get the combat manager
    CombatManager* getCombatManager();

    // Update renderer's target display after targeting via bridge
    void updateRendererTargetInfo(uint16_t spawnId);

    // Convert ChatChannel enum to EQ channel string
    std::string channelToString(action::ChatChannel channel);

    // T95: bounds-check helpers for the caller-supplied ints that
    // setMovementMode()/setPositionState()/sendPetCommand() cast into
    // enums. Reject-and-report, never clamp -- see eq_action_handler.cpp
    // for the rationale and refactor/T95_unvalidated_enum_casts.md.
    static bool isValidMovementMode(int mode);
    static bool isValidPositionState(int state);
    static bool isValidPetCommand(uint8_t command);
};

} // namespace eqt
