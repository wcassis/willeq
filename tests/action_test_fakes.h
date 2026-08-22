#pragma once

// Shared test fakes for the action layer (ActionDispatcher, CommandProcessor,
// InputActionBridge). See refactor/T12_action_layer.md.
//
// RecordingActionHandler implements every pure virtual of IActionHandler by
// appending a canonical "methodName(args)" string to a call log. Because it
// must implement all of them to compile, it can never silently drift out of
// sync with the interface — a new pure virtual will fail to compile here,
// not fail silently in a test.

#include "client/action/action_dispatcher.h"
#include "client/input/input_handler.h"
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace eqt {
namespace action {
namespace test {

// Wrapper marking a value as an already-formatted bare token (an enum
// name) that should NOT be quoted like a std::string argument would be.
struct Sym {
    std::string text;
};

inline std::string directionName(Direction dir) {
    switch (dir) {
        case Direction::Forward:  return "Forward";
        case Direction::Backward: return "Backward";
        case Direction::Left:     return "Left";
        case Direction::Right:    return "Right";
        case Direction::Up:       return "Up";
        case Direction::Down:     return "Down";
    }
    return "?";
}

inline std::string chatChannelName(ChatChannel ch) {
    switch (ch) {
        case ChatChannel::Say:     return "Say";
        case ChatChannel::Shout:   return "Shout";
        case ChatChannel::OOC:     return "OOC";
        case ChatChannel::Auction: return "Auction";
        case ChatChannel::Tell:    return "Tell";
        case ChatChannel::Group:   return "Group";
        case ChatChannel::Guild:   return "Guild";
        case ChatChannel::Raid:    return "Raid";
        case ChatChannel::Emote:   return "Emote";
    }
    return "?";
}

class RecordingActionHandler : public IActionHandler {
public:
    std::vector<std::string> calls;

    // Returns true if the log contains exactly one call equal to `expected`.
    bool calledOnceWith(const std::string& expected) const {
        int count = 0;
        for (const auto& c : calls) {
            if (c == expected) ++count;
        }
        return count == 1;
    }

    bool empty() const { return calls.empty(); }
    size_t count() const { return calls.size(); }

    void clear() { calls.clear(); }

    // ========== Movement ==========
    void startMoving(Direction dir) override {
        record("startMoving", Sym{directionName(dir)});
    }
    void stopMoving(Direction dir) override {
        record("stopMoving", Sym{directionName(dir)});
    }
    void setHeading(float heading) override {
        record("setHeading", heading);
    }
    void jump() override { record("jump"); }
    void sit() override { record("sit"); }
    void stand() override { record("stand"); }
    void toggleAutorun() override { record("toggleAutorun"); }
    void stopAllMovement() override { record("stopAllMovement"); }

    void moveToLocation(float x, float y, float z) override {
        record("moveToLocation", x, y, z);
    }
    void moveToEntity(const std::string& name) override {
        record("moveToEntity", name);
    }
    void moveToEntityWithinRange(const std::string& name, float distance) override {
        record("moveToEntityWithinRange", name, distance);
    }
    void followEntity(const std::string& name) override {
        record("followEntity", name);
    }
    void stopFollow() override { record("stopFollow"); }

    // ========== Combat ==========
    void targetEntity(uint16_t spawnId) override {
        record("targetEntity", spawnId);
    }
    void targetEntityByName(const std::string& name) override {
        record("targetEntityByName", name);
    }
    void targetNearest() override { record("targetNearest"); }
    void targetNearestPC() override { record("targetNearestPC"); }
    void targetNearestNPC() override { record("targetNearestNPC"); }
    void cycleTargets() override { record("cycleTargets"); }
    void cycleTargetsReverse() override { record("cycleTargetsReverse"); }
    void clearTarget() override { record("clearTarget"); }
    void targetSelf() override { record("targetSelf"); }
    void targetGroupMember(int index) override {
        record("targetGroupMember", index);
    }

    void enableAutoAttack() override { record("enableAutoAttack"); }
    void disableAutoAttack() override { record("disableAutoAttack"); }
    void toggleAutoAttack() override { record("toggleAutoAttack"); }

    // T102
    void assist(uint16_t targetId) override { record("assist", targetId); }
    void shieldTarget(uint16_t targetId) override { record("shieldTarget", targetId); }
    void taunt() override { record("taunt"); }
    void toggleAutoFire() override { record("toggleAutoFire"); }
    void enableCombatManager() override { record("enableCombatManager"); }
    void disableCombatManager() override { record("disableCombatManager"); }
    void toggleCombatManager() override { record("toggleCombatManager"); }
    void duelRequest(uint16_t targetId) override { record("duelRequest", targetId); }
    void duelAccept() override { record("duelAccept"); }
    void duelDecline() override { record("duelDecline"); }

    void castSpell(uint8_t gemSlot) override {
        record("castSpell", static_cast<int>(gemSlot));
    }
    void castSpellOnTarget(uint8_t gemSlot, uint16_t targetId) override {
        record("castSpellOnTarget", static_cast<int>(gemSlot), targetId);
    }
    void interruptCast() override { record("interruptCast"); }
    void beginCastFromGem(uint8_t gemSlot, uint16_t targetId) override {
        record("beginCastFromGem", static_cast<int>(gemSlot), targetId);
    }

    void useAbility(uint32_t abilityId) override {
        record("useAbility", abilityId);
    }
    void useSkill(uint32_t skillId) override {
        record("useSkill", skillId);
    }

    // ========== Interaction ==========
    void hail() override { record("hail"); }
    void hailTarget() override { record("hailTarget"); }

    void clickDoor(uint8_t doorId) override {
        record("clickDoor", static_cast<int>(doorId));
    }
    void clickNearestDoor() override { record("clickNearestDoor"); }

    void lootCorpse(uint16_t corpseId) override {
        record("lootCorpse", corpseId);
    }
    void lootItem(uint16_t corpseId, int16_t slot) override {
        record("lootItem", corpseId, slot);
    }
    void lootAll(uint16_t corpseId) override {
        record("lootAll", corpseId);
    }

    void consider() override { record("consider"); }

    // ========== Chat ==========
    void sendChatMessage(ChatChannel channel, const std::string& message) override {
        record("sendChatMessage", Sym{chatChannelName(channel)}, message);
    }
    void sendTell(const std::string& target, const std::string& message) override {
        record("sendTell", target, message);
    }
    void replyToLastTell(const std::string& message) override {
        record("replyToLastTell", message);
    }

    // ========== Group ==========
    void inviteToGroup(const std::string& playerName) override {
        record("inviteToGroup", playerName);
    }
    void inviteTarget() override { record("inviteTarget"); }
    void acceptGroupInvite() override { record("acceptGroupInvite"); }
    void declineGroupInvite() override { record("declineGroupInvite"); }
    void leaveGroup() override { record("leaveGroup"); }

    // ========== Character State ==========
    void setAFK(bool afk) override { record("setAFK", afk); }
    void setAnonymous(bool anon) override { record("setAnonymous", anon); }
    void toggleSneak() override { record("toggleSneak"); }
    void startCamp() override { record("startCamp"); }
    void cancelCamp() override { record("cancelCamp"); }

    // ========== Inventory ==========
    void moveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity = 0) override {
        record("moveItem", fromSlot, toSlot, quantity);
    }
    void deleteItem(int16_t slot) override { record("deleteItem", slot); }
    void useItem(int16_t slot) override { record("useItem", slot); }
    // T141
    void equip(const std::string& args) override { record("equip", args); }

    // ========== Spellbook ==========
    void memorizeSpell(uint8_t gemSlot, uint32_t spellId) override {
        record("memorizeSpell", static_cast<int>(gemSlot), spellId);
    }
    void forgetSpell(uint8_t gemSlot) override {
        record("forgetSpell", static_cast<int>(gemSlot));
    }
    void openSpellbook() override { record("openSpellbook"); }
    void closeSpellbook() override { record("closeSpellbook"); }
    void memorizeSpellByName(const std::string& spellName, uint8_t gemSlot) override {
        record("memorizeSpellByName", spellName, static_cast<int>(gemSlot));
    }
    void listMemorizedSpells() override { record("listMemorizedSpells"); }

    // ========== Trade ==========
    void requestTrade(uint16_t targetId) override {
        record("requestTrade", targetId);
    }
    void acceptTrade() override { record("acceptTrade"); }
    void cancelTrade() override { record("cancelTrade"); }
    void declineTrade() override { record("declineTrade"); }

    // ========== Zone ==========
    void requestZone(const std::string& zoneName) override {
        record("requestZone", zoneName);
    }

    // ========== Pet ==========
    void sendPetCommand(uint8_t command, uint16_t targetId = 0) override {
        record("sendPetCommand", static_cast<int>(command), targetId);
    }
    void dismissPet() override { record("dismissPet"); }

    // ========== Tradeskill ==========
    void clickWorldObject(uint32_t dropId) override {
        record("clickWorldObject", dropId);
    }
    void tradeskillCombine() override { record("tradeskillCombine"); }

    // T149: no object_type in any of these -- it is resolved from the open
    // container inside EqActionHandler, below this interface.
    void recipeSearch(const std::string& query, uint32_t minTrivial,
                      uint32_t maxTrivial) override {
        record("recipeSearch", query, minTrivial, maxTrivial);
    }
    void recipeDetails(uint32_t recipeId) override {
        record("recipeDetails", recipeId);
    }
    void recipeAutoCombine(uint32_t recipeId) override {
        record("recipeAutoCombine", recipeId);
    }
    void recipesFavorite(const std::vector<uint32_t>& recipeIds) override {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < recipeIds.size(); ++i) {
            if (i) oss << ",";
            oss << recipeIds[i];
        }
        oss << "]";
        record("recipesFavorite", Sym{oss.str()});
    }

    // T153: record only the fields the user actually named, in wire order.
    // The merge with the character's current appearance is EqActionHandler's
    // job, so it is deliberately invisible here.
    void setAppearance(const AppearanceChange& change) override {
        std::ostringstream oss;
        bool first = true;
        auto add = [&](const char* name, const std::optional<uint8_t>& v) {
            if (!v) return;
            if (!first) oss << " ";
            first = false;
            oss << name << "=" << static_cast<int>(*v);
        };
        add("haircolor", change.haircolor);
        add("beardcolor", change.beardcolor);
        add("eyecolor1", change.eyecolor1);
        add("eyecolor2", change.eyecolor2);
        add("hairstyle", change.hairstyle);
        add("beard", change.beard);
        add("face", change.face);
        record("setAppearance", Sym{oss.str()});
    }

    // T171
    void setInspectMessage(const std::optional<std::string>& message) override {
        if (!message) {
            record("setInspectMessage", Sym{"<show>"});
        } else {
            record("setInspectMessage", Sym{"\"" + *message + "\""});
        }
    }

    // ========== Extended Movement ==========
    void setMovementMode(int mode) override { record("setMovementMode", mode); }
    void setPositionState(int state) override { record("setPositionState", state); }

    // ========== Extended Character State ==========
    void setRoleplay(bool rp) override { record("setRoleplay", rp); }
    void setPathfinding(bool enabled) override { record("setPathfinding", enabled); }

    // ========== Extended Combat ==========
    void setAutoHunting(bool enabled) override { record("setAutoHunting", enabled); }
    void setAutoLoot(bool enabled) override { record("setAutoLoot", enabled); }
    void listHuntTargets() override { record("listHuntTargets"); }

    // ========== Entity Query ==========
    void listEntities(const std::string& filter) override {
        record("listEntities", filter);
    }
    void dumpEntityAppearance(const std::string& name) override {
        record("dumpEntityAppearance", name);
    }

    // ========== Utility ==========
    void sendAnimation(uint8_t animationId, uint8_t speed = 10) override {
        record("sendAnimation", static_cast<int>(animationId), static_cast<int>(speed));
    }
    void sendPositionUpdate() override { record("sendPositionUpdate"); }

    // ========== Social (T102) ==========
    void inspectTarget(uint16_t targetId) override { record("inspectTarget", targetId); }
    void toggleLFG() override { record("toggleLFG"); }
    void setSurname(const std::string& surname) override { record("setSurname", surname); }
    void rollRandom(uint32_t low, uint32_t high) override { record("rollRandom", low, high); }
    void yellForHelp() override { record("yellForHelp"); }
    void requestWho(const std::string& name, int lvlLow, int lvlHigh) override {
        record("requestWho", name, lvlLow, lvlHigh);
    }

    // ========== Guild (T132) ==========
    void guildInvite(const std::string& playerName) override { record("guildInvite", playerName); }
    void guildAcceptInvite() override { record("guildAcceptInvite"); }
    void guildDeclineInvite() override { record("guildDeclineInvite"); }
    void guildRemove(const std::string& playerName) override { record("guildRemove", playerName); }
    void guildDemote(const std::string& playerName) override { record("guildDemote", playerName); }
    void guildMakeLeader(const std::string& playerName) override { record("guildMakeLeader", playerName); }
    void requestGuildMOTD() override { record("requestGuildMOTD"); }
    void setGuildMOTD(const std::string& motd) override { record("setGuildMOTD", motd); }

    // ========== Corpse / Resurrection (T134) ==========
    void corpseDrag(const std::string& corpseName) override { record("corpseDrag", corpseName); }
    void corpseDrop() override { record("corpseDrop"); }
    void consent(const std::string& playerName) override { record("consent", playerName); }
    void consentDeny(const std::string& playerName) override { record("consentDeny", playerName); }
    void rezAccept() override { record("rezAccept"); }
    void rezDecline() override { record("rezDecline"); }

    // ========== Utility (T135) ==========
    void split(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) override {
        record("split", platinum, gold, silver, copper);
    }
    void save() override { record("save"); }
    void bandage(uint16_t targetId) override { record("bandage", targetId); }
    void rewind() override { record("rewind"); }
    void bug(const std::string& description) override { record("bug", description); }
    void petition(const std::string& text) override { record("petition", text); }

    // ========== Merchant (T136) ==========
    void vendorToggle() override { record("vendorToggle"); }
    void vendorList() override { record("vendorList"); }
    void buyFromVendor(uint32_t slot, uint8_t quantity) override {
        record("buyFromVendor", slot, static_cast<uint32_t>(quantity));
    }
    void sellToVendor(uint32_t slot, uint8_t quantity) override {
        record("sellToVendor", slot, static_cast<uint32_t>(quantity));
    }

    // ========== Movement / Utility / Skills (T140) ==========
    void showLocation() override { record("showLocation"); }
    void warp(const std::string& args) override { record("warp", args); }
    void showOrSetZoning(const std::string& args) override { record("showOrSetZoning", args); }
    void requestQuit() override { record("requestQuit"); }
    void campNow() override { record("campNow"); }
    void debugLevel(const std::string& args) override { record("debugLevel", args); }
    void perfReport() override { record("perfReport"); }
    void timeOfDay(const std::string& args) override { record("timeOfDay", args); }
    void bankToggle() override { record("bankToggle"); }
    void trainWithTarget() override { record("trainWithTarget"); }

    // ========== Social / Find (T150) ==========
    void assistGroup(uint16_t targetId) override {
        record("assistGroup", static_cast<uint32_t>(targetId));
    }
    void trackTarget(uint16_t targetId) override {
        record("trackTarget", static_cast<uint32_t>(targetId));
    }
    void findPerson(uint16_t targetId) override {
        record("findPerson", static_cast<uint32_t>(targetId));
    }
    void friendsWho(const std::string& friendsList) override {
        record("friendsWho", friendsList);
    }

    // ========== GM (T148) ==========
    // gmMode is the login-time PlayerProfile.gm @13054 flag as
    // ActionDispatcher sees it. Default FALSE so every pre-existing test
    // keeps its meaning and so a test that wants the GM path has to say so.
    bool gmMode = false;
    bool isGMMode() const override { return gmMode; }

    void gmZoneRequest(const std::string& charName, uint16_t zoneId) override {
        record("gmZoneRequest", charName, zoneId);
    }
    void gmSummon(const std::string& charName) override { record("gmSummon", charName); }
    void gmGoto(const std::string& charName) override { record("gmGoto", charName); }
    void gmFind(const std::string& charName) override { record("gmFind", charName); }
    void gmKick(const std::string& charName) override { record("gmKick", charName); }
    void gmKill(const std::string& charName) override { record("gmKill", charName); }
    void gmHideMe(bool hide) override { record("gmHideMe", hide); }
    void gmEmoteZone(const std::string& text) override { record("gmEmoteZone", text); }
    void gmLastName(const std::string& charName, const std::string& lastName) override {
        record("gmLastName", charName, lastName);
    }

private:
    template <typename... Args>
    void record(const char* name, Args&&... args) {
        std::ostringstream oss;
        oss << name << "(";
        appendArgs(oss, std::forward<Args>(args)...);
        oss << ")";
        calls.push_back(oss.str());
    }

    void appendArgs(std::ostringstream&) {}

    template <typename T>
    void appendArgs(std::ostringstream& oss, T&& first) {
        oss << toStr(std::forward<T>(first));
    }

    template <typename T, typename... Rest>
    void appendArgs(std::ostringstream& oss, T&& first, Rest&&... rest) {
        oss << toStr(std::forward<T>(first)) << ", ";
        appendArgs(oss, std::forward<Rest>(rest)...);
    }

    static std::string toStr(const std::string& s) { return "\"" + s + "\""; }
    static std::string toStr(const Sym& s) { return s.text; }
    static std::string toStr(bool b) { return b ? "true" : "false"; }
    static std::string toStr(float f) {
        std::ostringstream oss;
        oss << f;
        return oss.str();
    }
    template <typename T>
    static std::string toStr(T v) {
        return std::to_string(v);
    }
};

// T15: scriptable IInputHandler fake for InputActionBridge tests. Like
// RecordingActionHandler, it implements every pure virtual so interface
// drift breaks the compile, not the tests. Test code loads the public
// members directly; the bridge drains them through the interface.
class FakeInputHandler : public input::IInputHandler {
public:
    // Scriptable state
    std::set<input::InputAction> pendingActions;
    input::InputState state;
    std::queue<input::SpellCastRequest> spellCasts;
    std::queue<input::HotbarRequest> hotbarRequests;
    std::queue<input::TargetRequest> targetRequests;
    std::queue<input::LootRequest> lootRequests;
    std::queue<input::ChatMessage> chatMessages;
    std::queue<input::MoveCommand> moveCommands;
    std::queue<std::string> rawCommands;

    // Recording
    int updateCalls = 0;
    int resetMouseDeltasCalls = 0;
    std::vector<input::InputAction> consumedActions;  // every consumeAction hit

    void update() override { ++updateCalls; }
    bool isActive() const override { return true; }

    bool hasAction(input::InputAction action) const override {
        return pendingActions.count(action) > 0;
    }
    bool consumeAction(input::InputAction action) override {
        if (pendingActions.erase(action) > 0) {
            consumedActions.push_back(action);
            return true;
        }
        return false;
    }

    const input::InputState& getState() const override { return state; }
    void resetMouseDeltas() override { ++resetMouseDeltasCalls; }

    std::optional<input::SpellCastRequest> consumeSpellCastRequest() override {
        return pop(spellCasts);
    }
    std::optional<input::HotbarRequest> consumeHotbarRequest() override {
        return pop(hotbarRequests);
    }
    std::optional<input::TargetRequest> consumeTargetRequest() override {
        return pop(targetRequests);
    }
    std::optional<input::LootRequest> consumeLootRequest() override {
        return pop(lootRequests);
    }

    bool hasPendingKeyEvents() const override { return false; }
    std::optional<input::KeyEvent> popKeyEvent() override { return std::nullopt; }
    void clearPendingKeyEvents() override {}

    std::optional<input::ChatMessage> consumeChatMessage() override {
        return pop(chatMessages);
    }
    std::optional<input::MoveCommand> consumeMoveCommand() override {
        return pop(moveCommands);
    }
    std::optional<std::string> consumeRawCommand() override {
        return pop(rawCommands);
    }
    std::optional<std::string> peekRawCommand() const override {
        if (rawCommands.empty()) {
            return std::nullopt;
        }
        return rawCommands.front();
    }

private:
    template <typename T>
    static std::optional<T> pop(std::queue<T>& q) {
        if (q.empty()) return std::nullopt;
        T v = q.front();
        q.pop();
        return v;
    }
};

} // namespace test
} // namespace action
} // namespace eqt
