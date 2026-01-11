#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <optional>
#include <glm/glm.hpp>

namespace eqt {

// Forward declarations
namespace state {
class GameState;
}

namespace action {

/**
 * Direction enum for movement actions.
 */
enum class Direction {
    Forward,
    Backward,
    Left,
    Right,
    Up,      // For jump/levitate
    Down     // For descend
};

/**
 * ChatChannel - Types of chat channels.
 */
enum class ChatChannel {
    Say,
    Shout,
    OOC,
    Auction,
    Tell,
    Group,
    Guild,
    Raid,
    Emote
};

/**
 * ActionResult - Result of an action attempt.
 */
struct ActionResult {
    bool success = false;
    std::string message;  // Error or status message

    static ActionResult Success(const std::string& msg = "") {
        return {true, msg};
    }

    static ActionResult Failure(const std::string& msg) {
        return {false, msg};
    }
};

/**
 * IActionHandler - Interface for objects that can execute game actions.
 *
 * This is typically implemented by the EverQuest class or a wrapper around it.
 * The ActionDispatcher delegates action execution to this interface.
 */
class IActionHandler {
public:
    virtual ~IActionHandler() = default;

    // ========== Movement ==========

    virtual void startMoving(Direction dir) = 0;
    virtual void stopMoving(Direction dir) = 0;
    virtual void setHeading(float heading) = 0;
    virtual void jump() = 0;
    virtual void sit() = 0;
    virtual void stand() = 0;
    virtual void toggleAutorun() = 0;
    virtual void stopAllMovement() = 0;

    // Pathfinding movement
    virtual void moveToLocation(float x, float y, float z) = 0;
    virtual void moveToEntity(const std::string& name) = 0;
    virtual void moveToEntityWithinRange(const std::string& name, float distance) = 0;
    virtual void followEntity(const std::string& name) = 0;
    virtual void stopFollow() = 0;

    // ========== Combat ==========

    virtual void targetEntity(uint16_t spawnId) = 0;
    virtual void targetEntityByName(const std::string& name) = 0;
    virtual void targetNearest() = 0;
    virtual void clearTarget() = 0;

    virtual void enableAutoAttack() = 0;
    virtual void disableAutoAttack() = 0;
    virtual void toggleAutoAttack() = 0;

    virtual void castSpell(uint8_t gemSlot) = 0;
    virtual void castSpellOnTarget(uint8_t gemSlot, uint16_t targetId) = 0;
    virtual void interruptCast() = 0;

    virtual void useAbility(uint32_t abilityId) = 0;
    virtual void useSkill(uint32_t skillId) = 0;

    // ========== Interaction ==========

    virtual void hail() = 0;
    virtual void hailTarget() = 0;

    virtual void clickDoor(uint8_t doorId) = 0;
    virtual void clickNearestDoor() = 0;

    virtual void lootCorpse(uint16_t corpseId) = 0;
    virtual void lootItem(uint16_t corpseId, int16_t slot) = 0;
    virtual void lootAll(uint16_t corpseId) = 0;

    virtual void consider() = 0;

    // ========== Chat ==========

    virtual void sendChatMessage(ChatChannel channel, const std::string& message) = 0;
    virtual void sendTell(const std::string& target, const std::string& message) = 0;
    virtual void replyToLastTell(const std::string& message) = 0;

    // ========== Group ==========

    virtual void inviteToGroup(const std::string& playerName) = 0;
    virtual void inviteTarget() = 0;
    virtual void acceptGroupInvite() = 0;
    virtual void declineGroupInvite() = 0;
    virtual void leaveGroup() = 0;

    // ========== Character State ==========

    virtual void setAFK(bool afk) = 0;
    virtual void setAnonymous(bool anon) = 0;
    virtual void toggleSneak() = 0;
    virtual void startCamp() = 0;
    virtual void cancelCamp() = 0;

    // ========== Inventory ==========

    virtual void moveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity = 0) = 0;
    virtual void deleteItem(int16_t slot) = 0;
    virtual void useItem(int16_t slot) = 0;

    // ========== Spellbook ==========

    virtual void memorizeSpell(uint8_t gemSlot, uint32_t spellId) = 0;
    virtual void forgetSpell(uint8_t gemSlot) = 0;
    virtual void openSpellbook() = 0;
    virtual void closeSpellbook() = 0;

    // ========== Trade ==========

    virtual void requestTrade(uint16_t targetId) = 0;
    virtual void acceptTrade() = 0;
    virtual void cancelTrade() = 0;

    // ========== Zone ==========

    virtual void requestZone(const std::string& zoneName) = 0;

    // ========== Pet ==========

    virtual void sendPetCommand(uint8_t command, uint16_t targetId = 0) = 0;
    virtual void dismissPet() = 0;

    // ========== Tradeskill ==========

    virtual void clickWorldObject(uint32_t dropId) = 0;
    virtual void tradeskillCombine() = 0;

    // ========== Utility ==========

    virtual void sendAnimation(uint8_t animationId, uint8_t speed = 10) = 0;
    virtual void sendPositionUpdate() = 0;
};

/**
 * ActionDispatcher - Central dispatcher for all game actions.
 *
 * This class provides a unified interface for executing game actions from any
 * input source (keyboard, console, automation scripts). It validates actions
 * before execution and provides consistent error handling.
 *
 * Usage:
 * 1. Create an ActionDispatcher with a GameState reference
 * 2. Set the action handler (typically the EverQuest instance or adapter)
 * 3. Call action methods to execute game actions
 *
 * The dispatcher can be used by:
 * - InputActionBridge for translating input events to actions
 * - CommandProcessor for slash command handling
 * - AutomatedMode for scripted actions
 */
class ActionDispatcher {
public:
    explicit ActionDispatcher(state::GameState& state);
    ~ActionDispatcher();

    /**
     * Set the action handler that will execute the actions.
     * Must be called before any actions can be dispatched.
     */
    void setActionHandler(IActionHandler* handler);

    /**
     * Check if the dispatcher is ready to execute actions.
     */
    bool isReady() const { return m_handler != nullptr; }

    // ========== Movement Actions ==========

    /**
     * Start moving in a direction.
     */
    ActionResult startMoving(Direction dir);

    /**
     * Stop moving in a direction.
     */
    ActionResult stopMoving(Direction dir);

    /**
     * Stop all movement.
     */
    ActionResult stopAllMovement();

    /**
     * Set player heading in degrees (0-360, 0 = north).
     */
    ActionResult setHeading(float heading);

    /**
     * Face a specific location.
     */
    ActionResult faceLocation(float x, float y, float z);

    /**
     * Face an entity by name.
     */
    ActionResult faceEntity(const std::string& name);

    /**
     * Jump.
     */
    ActionResult jump();

    /**
     * Sit down.
     */
    ActionResult sit();

    /**
     * Stand up.
     */
    ActionResult stand();

    /**
     * Toggle autorun.
     */
    ActionResult toggleAutorun();

    /**
     * Move to a specific location using pathfinding.
     */
    ActionResult moveToLocation(float x, float y, float z);

    /**
     * Move to an entity by name.
     */
    ActionResult moveToEntity(const std::string& name);

    /**
     * Move to an entity and stop at specified distance.
     */
    ActionResult moveToEntityWithinRange(const std::string& name, float distance);

    /**
     * Follow an entity.
     */
    ActionResult followEntity(const std::string& name);

    /**
     * Stop following.
     */
    ActionResult stopFollow();

    // ========== Combat Actions ==========

    /**
     * Target an entity by spawn ID.
     */
    ActionResult targetEntity(uint16_t spawnId);

    /**
     * Target an entity by name.
     */
    ActionResult targetEntityByName(const std::string& name);

    /**
     * Target the nearest entity.
     */
    ActionResult targetNearest();

    /**
     * Clear current target.
     */
    ActionResult clearTarget();

    /**
     * Enable auto-attack.
     */
    ActionResult enableAutoAttack();

    /**
     * Disable auto-attack.
     */
    ActionResult disableAutoAttack();

    /**
     * Toggle auto-attack.
     */
    ActionResult toggleAutoAttack();

    /**
     * Cast spell from gem slot (1-8).
     */
    ActionResult castSpell(uint8_t gemSlot);

    /**
     * Cast spell on a specific target.
     */
    ActionResult castSpellOnTarget(uint8_t gemSlot, uint16_t targetId);

    /**
     * Interrupt current spellcast.
     */
    ActionResult interruptCast();

    /**
     * Use an ability by ID.
     */
    ActionResult useAbility(uint32_t abilityId);

    /**
     * Use a skill by ID.
     */
    ActionResult useSkill(uint32_t skillId);

    /**
     * Consider current target.
     */
    ActionResult consider();

    // ========== Interaction Actions ==========

    /**
     * Hail (say "Hail").
     */
    ActionResult hail();

    /**
     * Hail current target.
     */
    ActionResult hailTarget();

    /**
     * Click a door by ID.
     */
    ActionResult clickDoor(uint8_t doorId);

    /**
     * Click the nearest door.
     */
    ActionResult clickNearestDoor();

    /**
     * Open loot window for a corpse.
     */
    ActionResult lootCorpse(uint16_t corpseId);

    /**
     * Loot a specific item from a corpse.
     */
    ActionResult lootItem(uint16_t corpseId, int16_t slot);

    /**
     * Loot all items from a corpse.
     */
    ActionResult lootAll(uint16_t corpseId);

    // ========== Chat Actions ==========

    /**
     * Send a chat message to a channel.
     */
    ActionResult sendChatMessage(ChatChannel channel, const std::string& message);

    /**
     * Send a tell to a player.
     */
    ActionResult sendTell(const std::string& target, const std::string& message);

    /**
     * Reply to the last tell received.
     */
    ActionResult replyToLastTell(const std::string& message);

    // ========== Group Actions ==========

    /**
     * Invite a player to group.
     */
    ActionResult inviteToGroup(const std::string& playerName);

    /**
     * Invite current target to group.
     */
    ActionResult inviteTarget();

    /**
     * Accept pending group invite.
     */
    ActionResult acceptGroupInvite();

    /**
     * Decline pending group invite.
     */
    ActionResult declineGroupInvite();

    /**
     * Leave current group.
     */
    ActionResult leaveGroup();

    // ========== Character State Actions ==========

    /**
     * Set AFK status.
     */
    ActionResult setAFK(bool afk);

    /**
     * Toggle AFK status.
     */
    ActionResult toggleAFK();

    /**
     * Set anonymous status.
     */
    ActionResult setAnonymous(bool anon);

    /**
     * Toggle sneak.
     */
    ActionResult toggleSneak();

    /**
     * Start camp timer (sit and logout after 30 seconds).
     */
    ActionResult startCamp();

    /**
     * Cancel camp timer.
     */
    ActionResult cancelCamp();

    // ========== Inventory Actions ==========

    /**
     * Move an item between slots.
     */
    ActionResult moveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity = 0);

    /**
     * Delete an item.
     */
    ActionResult deleteItem(int16_t slot);

    /**
     * Use an item (right-click).
     */
    ActionResult useItem(int16_t slot);

    // ========== Spellbook Actions ==========

    /**
     * Memorize a spell to a gem slot.
     */
    ActionResult memorizeSpell(uint8_t gemSlot, uint32_t spellId);

    /**
     * Forget a spell from a gem slot.
     */
    ActionResult forgetSpell(uint8_t gemSlot);

    /**
     * Open spellbook.
     */
    ActionResult openSpellbook();

    /**
     * Close spellbook.
     */
    ActionResult closeSpellbook();

    // ========== Trade Actions ==========

    /**
     * Request trade with target.
     */
    ActionResult requestTrade(uint16_t targetId);

    /**
     * Accept trade.
     */
    ActionResult acceptTrade();

    /**
     * Cancel trade.
     */
    ActionResult cancelTrade();

    // ========== Zone Actions ==========

    /**
     * Request zone transition.
     */
    ActionResult requestZone(const std::string& zoneName);

    // ========== Pet Actions ==========

    /**
     * Send a pet command.
     * @param command Pet command ID (see pet_constants.h PetCommand enum)
     * @param targetId Optional target for attack commands
     */
    ActionResult sendPetCommand(uint8_t command, uint16_t targetId = 0);

    /**
     * Dismiss the current pet.
     */
    ActionResult dismissPet();

    /**
     * Command pet to attack current target.
     */
    ActionResult petAttack();

    /**
     * Command pet to back off (stop attacking).
     */
    ActionResult petBackOff();

    /**
     * Command pet to follow owner.
     */
    ActionResult petFollow();

    /**
     * Command pet to guard current location.
     */
    ActionResult petGuard();

    /**
     * Toggle pet sit.
     */
    ActionResult petSit();

    /**
     * Toggle pet taunt.
     */
    ActionResult petTaunt();

    /**
     * Toggle pet hold (don't add to hate list).
     */
    ActionResult petHold();

    /**
     * Toggle pet focus.
     */
    ActionResult petFocus();

    /**
     * Get pet health report.
     */
    ActionResult petHealth();

    // ========== Tradeskill Actions ==========

    /**
     * Click/interact with a world object (forge, loom, etc.).
     */
    ActionResult clickWorldObject(uint32_t dropId);

    /**
     * Combine items in the current tradeskill container.
     */
    ActionResult tradeskillCombine();

    // ========== Utility Actions ==========

    /**
     * Play an animation.
     */
    ActionResult sendAnimation(uint8_t animationId, uint8_t speed = 10);

    /**
     * Force send position update to server.
     */
    ActionResult sendPositionUpdate();

private:
    state::GameState& m_state;
    IActionHandler* m_handler = nullptr;

    // Helper to check if handler is available
    ActionResult checkHandler() const;

    // Helper to validate zone connection
    ActionResult checkZoneConnection() const;

    // Calculate heading to face a position
    float calculateHeadingTo(float x, float y) const;
};

} // namespace action
} // namespace eqt
