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

    void setHeading(float heading) override;
    void jump() override;
    void sit() override;
    void stand() override;
    void toggleAutorun() override;
    void stopAllMovement() override;

    void moveToLocation(float x, float y, float z) override;
    void moveToEntity(uint16_t spawnId) override;
    void followEntity(uint16_t spawnId) override;
    void stopFollow() override;

    // ========== Combat ==========

    void targetEntity(uint16_t spawnId) override;
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
    void interruptCast() override;
    void beginCastFromGem(uint8_t gemSlot, uint16_t targetId) override;

    void useSkill(uint32_t skillId) override;

    // ========== Interaction ==========

    void hail() override;
    void hailTarget() override;

    void clickNearestDoor() override;

    void lootCorpse(uint16_t corpseId) override;
    bool lootItem(uint16_t corpseId, int16_t slot) override;
    bool lootItemByName(const std::string& name) override;
    bool lootAll(uint16_t corpseId) override;
    bool closeLootWindow() override;

    void consider() override;

    // ========== Chat ==========

    void sendChatMessage(action::ChatChannel channel, const std::string& message) override;
    void sendTell(const std::string& target, const std::string& message) override;
    void replyToLastTell(const std::string& message) override;
    bool hasTellToReplyTo() const override;
    std::string getLastTellSender() const override;

    // ========== Group ==========

    void inviteToGroup(const std::string& playerName) override;
    void inviteTarget() override;
    void acceptGroupInvite() override;
    void declineGroupInvite() override;
    void leaveGroup() override;

    // ========== Character State ==========

    void setAFK(bool afk) override;
    void setAnonymous(bool anon) override;

    // ========== Inventory ==========

    void useItem(int16_t slot) override;
    int16_t findItemSlotByItemId(uint32_t itemId) override;  // T230
    void activateHotbarCommandSlot(eqt::HotbarSlotType type, const std::string& text) override;  // T239
    void equip(const std::string& args) override;  // T141
    void readItem(const std::string& args) override;  // T273
    void scribeItem(const std::string& args) override;  // T276

    // ========== Spellbook ==========

    void forgetSpell(uint8_t gemSlot) override;
    void memorizeSpellByName(const std::string& spellName, uint8_t gemSlot) override;
    void listMemorizedSpells() override;

    // ========== Buffs ==========

    void cancelBuff(const std::string& args) override;  // T277

    // ========== Trade ==========

    void requestTrade(uint16_t targetId) override;
    void acceptTrade() override;
    void cancelTrade() override;
    void declineTrade() override;


    // ========== Pet ==========

    void sendPetCommand(uint8_t command, uint16_t targetId) override;
    void dismissPet() override;

    // ========== Tradeskill ==========

    void clickWorldObject(uint32_t dropId) override;
    uint32_t findNearestWorldObject(float& outDistance) const override;

    // T149: recipe requests. object_type is NOT a parameter -- it is
    // resolved here from the open container via
    // resolveOpenContainerObjectType(), see the .cpp.
    void recipeSearch(const std::string& query, uint32_t minTrivial,
                      uint32_t maxTrivial) override;
    void recipeDetails(uint32_t recipeId) override;
    void recipeAutoCombine(uint32_t recipeId) override;
    void recipesFavorite(const std::vector<uint32_t>& recipeIds) override;
    void tradeskillCombine(int16_t containerSlot) override;
    void closeTradeskillContainer() override;

    // T153: /appearance -- merges the named fields over the character's
    // current appearance and calls EverQuest::SendFaceChange().
    void setAppearance(const action::AppearanceChange& change) override;
    // T171: set/clear/show the local inspect message. nullopt = show.
    void setInspectMessage(const std::optional<std::string>& message) override;

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

    // ========== Raid (T179) ==========
    void raidInvite(const std::string& name) override;
    void raidAccept() override;
    void raidLeave() override;
    void raidStatus() override;
    void raidList() override;
    // T181: leader-only ops.
    void raidRemove(const std::string& name) override;
    void raidMakeLeader(const std::string& name) override;
    void raidLootType(const std::string& mode) override;
    void raidLooter(bool add, const std::string& name) override;
    void raidLock(bool lock) override;  // T182
    void raidGroup(const std::string& name, const std::string& group_text) override;  // T184

private:
    // T181 guards. The server answers a non-leader's MakeLeader with silence
    // and never validates a loot type at all -- see the comment block at the
    // implementations.
    bool raidRequireMembership();
    bool raidRequireLeader(const std::string& what);
    bool raidMemberKnown(const std::string& name) const;
    size_t countRaidGroup(uint32_t wire_group) const;  // T184

public:

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
    // T280: thin wrappers over EverQuest::RequestTrainSkill/
    // RequestCloseTrainer (eq_commands_train.cpp), which push the kept
    // TrainSkillIntent / CloseTrainerIntent.
    void trainSkill(const std::string& args) override;
    void closeTrainer() override;
    // T269: thin wrappers over EverQuest::RequestBankCoinMove/Convert
    // (eq_merchant.cpp), which push the kept Bank* intents.
    void bankCoinMove(uint8_t coinType, int32_t amount, bool fromBank) override;
    void bankCoinConvert(uint8_t fromCoinType, uint8_t toCoinType, int32_t amount) override;

    // ========== Social / Find (T150) ==========
    void assistGroup(uint16_t targetId) override;
    void trackTarget(uint16_t targetId) override;
    void findPerson(uint16_t targetId) override;
    void friendsWho(const std::string& friendsList) override;

    // ========== GM (T148) ==========
    // Pure routing to the pre-existing EverQuest::SendGM*() senders
    // (src/client/eq_gm.cpp); isGMMode() is a pure read of
    // EverQuest::GetGMMode() (PlayerProfile.gm @13054, login-time only).
    bool isGMMode() const override;
    void gmZoneRequest(const std::string& charName, uint16_t zoneId) override;
    void gmSummon(const std::string& charName) override;
    void gmGoto(const std::string& charName) override;
    void gmFind(const std::string& charName) override;
    void gmKick(const std::string& charName) override;
    void gmKill(const std::string& charName) override;
    void gmHideMe(bool hide) override;
    void gmEmoteZone(const std::string& text) override;
    void gmLastName(const std::string& charName, const std::string& lastName) override;

private:
    EverQuest& m_eq;

    // Helper to get the combat manager
    CombatManager* getCombatManager();

    // T150: shared targetId==0 -> live CombatManager target fallback for
    // assistGroup/trackTarget/findPerson (same rule assist()/shieldTarget()
    // implement inline). Returns 0 after printing "You don't have a target."
    uint16_t resolveTargetOrCurrent(uint16_t targetId);

    // T162: the ONE skill-possession check in this file that does not go
    // through SkillManager::activateSkill(), and it exists for exactly one
    // command. /track sends HC_OP_TrackTarget, which is NOT skill 53's
    // opcode (that is HC_OP_Track, the "give me the track list" request), so
    // routing /track through activateSkill would change what the command
    // does. eqemu still class-gates it -- Handle_OP_TrackTarget returns early
    // on !CanThisClassTrack() -- so it is gated here by CALLING T159's own
    // predicate, SkillManager::hasSkill(), never by copying it. Returns true
    // (allow) when the skill system is absent or the class can have the
    // skill; on refusal it prints T159's wording and returns false.
    bool skillGateAllows(uint8_t skillId);

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

    // T149: resolve the currently-open tradeskill container's object_type.
    // Returns false (and tells the player why) when no world container is
    // open. The value comes from the WorldObject T144 made correct, keyed by
    // the drop_id ZoneProcessClickObjectAction recorded when the server
    // acknowledged the open -- there is no parallel state store here.
    bool resolveOpenContainerObjectType(uint32_t& objectTypeOut);
};

} // namespace eqt
