// ActionDispatcher -- guild, /who, LFG, GM, skill and appearance clusters.
//
// Split out of src/client/action/action_dispatcher.cpp by
// refactor/J25_action_dispatcher_split.md (move-only, 2026-09-02). The
// dispatcher's method definitions now live in three TUs:
// action_dispatcher.cpp, action_dispatcher_social.cpp and
// action_dispatcher_guild_misc.cpp; all are declared in
// include/client/action/action_dispatcher.h.
//
// WARNING: tests/test_reachability_sweep.cpp reads THIS FILE AS TEXT (it
// concatenates all three TUs to parse every ActionDispatcher:: definition,
// and excludes all three by path from its external-caller sweep). Moving a
// method into or out of this file requires updating that test.

#include "client/action/action_dispatcher.h"
#include "client/state/game_state.h"
#include "client/pet_constants.h"
#include <cmath>
#include <limits>
#include <sstream>  // T362: /warp parses its own coordinate form
#include <string>

namespace eqt {
namespace action {
// ========== Guild Actions (T132) ==========
//
// Net-new: EqActionHandler wraps the pre-existing EverQuest::SendGuild*()
// senders, which already implement the guild-membership/pending-invite
// guards and player-facing messages (game's handlers did no additional
// checks of their own -- see refactor/T132_migrate_guild_family.md). The
// empty-name usage checks below mirror game's own pre-dispatch guard so the
// exact usage string is preserved even before reaching the handler.

ActionResult ActionDispatcher::guildInvite(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /guildinvite <name>");
    }

    m_handler->guildInvite(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::guildAcceptInvite() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->guildAcceptInvite();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::guildDeclineInvite() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->guildDeclineInvite();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::guildRemove(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /guildremove <name>");
    }

    m_handler->guildRemove(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::guildDemote(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /guilddemote <name>");
    }

    m_handler->guildDemote(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::guildMakeLeader(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /guildleader <name>");
    }

    m_handler->guildMakeLeader(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::requestGuildMOTD() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->requestGuildMOTD();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::setGuildMOTD(const std::string& motd) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->setGuildMOTD(motd);
    return ActionResult::Success();
}

// ========== Corpse / Resurrection (T134) ==========
// Usage-string guards below mirror game's original corpsedrag/consent/deny
// handlers verbatim (eq_commands.cpp), matching the Guild family's
// belt-and-suspenders shape -- the empty-argument check is duplicated here
// AND inside game's ported usage strings would have been if game still
// registered them; corpsedrop/rezAccept/rezDecline take no argument so have
// no usage guard, matching game's shape for those too. The stateful guards
// (m_is_dragging_corpse, m_has_pending_rezz) are NOT duplicated here -- they
// live in the senders (SendCorpseDrop, SendRezzAnswer) and are preserved
// there unchanged.

ActionResult ActionDispatcher::corpseDrag(const std::string& corpseName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (corpseName.empty()) {
        return ActionResult::Failure("Usage: /corpsedrag <corpse_name>");
    }

    m_handler->corpseDrag(corpseName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::corpseDrop() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->corpseDrop();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::consent(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /consent <player_name>");
    }

    m_handler->consent(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::consentDeny(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /deny <player_name>");
    }

    m_handler->consentDeny(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::rezAccept() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->rezAccept();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::rezDecline() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->rezDecline();
    return ActionResult::Success();
}

// ========== Utility (T135) ==========
// Net-new, same shape as Guild (T132) / Corpse-Rez (T134). split/save/
// rewind/bandage take no usage-string guard here because game's original
// handlers had none for them either (only split_cmd/bug_cmd/petition_cmd
// had one, and split's is a parsing concern handled by
// CommandProcessor::cmdSplit -- see its comment). bug()/petition() port
// game's empty-argument usage guard verbatim, matching corpsedrag/consent's
// belt-and-suspenders shape. The stateful guards (m_in_group + affordability
// in SendSplit) are NOT duplicated here -- they stay in the sender.

ActionResult ActionDispatcher::split(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->split(platinum, gold, silver, copper);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::save() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->save();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::bandage(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->bandage(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::rewind() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->rewind();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::bug(const std::string& description) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (description.empty()) {
        return ActionResult::Failure("Usage: /bug <description>");
    }

    m_handler->bug(description);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petition(const std::string& text) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (text.empty()) {
        return ActionResult::Failure("Usage: /petition <text>");
    }

    m_handler->petition(text);
    return ActionResult::Success();
}

// ========== Merchant (T136) ==========
// No usage-string guards ported here beyond checkZoneConnection() -- none
// of game's original vendor_cmd/vendorlist_cmd existed at the dispatcher
// level either (their "You are not at a vendor."/"no target" messages are
// stateful guards, so they live inside the new EverQuest methods /
// EqActionHandler::vendorList(), per T134's rule). buy/sell's slot and
// quantity ARGUMENT PARSING (numeric-token validation, matching
// ParseVendorSlot/ParseVendorQuantity's old idiom) moved up to
// CommandProcessor::cmdBuy/cmdSell -- a formatting concern, not a guard,
// same precedent as cmdSplit/cmdWho's own token parsing.

ActionResult ActionDispatcher::vendorToggle() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->vendorToggle();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::vendorList() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->vendorList();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::buyFromVendor(uint32_t slot, uint8_t quantity) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->buyFromVendor(slot, quantity);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::sellToVendor(uint32_t slot, uint8_t quantity) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->sellToVendor(slot, quantity);
    return ActionResult::Success();
}

// ========== Equip (T141) ==========
// No usage-string guard here either, same precedent as vendorToggle()/
// vendorList() above -- /equip's own "Usage: ..." message is a stateful
// (args-shape-dependent) reply that lives inside EverQuest::RequestEquip(),
// not duplicated at the dispatcher level. checkZoneConnection() is added
// here (equip had no equivalent guard in the pre-migration game registry)
// because every other migrated inventory/vendor action in this file gates
// on it, and a disconnected EverQuest has no meaningful inventory state to
// move items through -- see refactor/T141_equip_extraction.md's Review for
// the full argument.

ActionResult ActionDispatcher::equip(const std::string& args) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->equip(args);
    return ActionResult::Success();
}

// ========== Read book/note (T273) ==========
// Zone-connection gate for the same reason equip() has one: OP_ReadBook is
// a zone-server packet and a disconnected client has no inventory to read
// from. The empty-argument usage string is asserted HERE (not inside
// RequestReadItem) so /read with no argument is refused without needing a
// live EverQuest -- RequestReadItem keeps its own identical guard for the
// intent-path callers that do not come through the dispatcher.
ActionResult ActionDispatcher::readItem(const std::string& args) {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    if (args.empty()) {
        return ActionResult::Failure(
            "Usage: /read <slot> | /read <item name> (a book or note you are carrying)");
    }

    m_handler->readItem(args);
    return ActionResult::Success();
}

// ========== Scribe a spell scroll (T276) ==========
// Same shape as readItem() above: OP_MemorizeSpell is a zone-server packet
// and a disconnected client has no inventory to scribe from. The
// empty-argument usage string is asserted HERE so /scribe with no argument
// is refused without a live EverQuest; RequestScribeItem keeps its own
// identical guard for intent-path callers.
ActionResult ActionDispatcher::scribeItem(const std::string& args) {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    if (args.empty()) {
        return ActionResult::Failure(
            "Usage: /scribe <slot> | /scribe <item name> (a spell scroll you are carrying)");
    }

    m_handler->scribeItem(args);
    return ActionResult::Success();
}

// ========== Cursor gesture commands (B442) ==========
// Same gate as equip(): a disconnected client has no inventory to move.
ActionResult ActionDispatcher::cursorCommand(const std::string& verb, const std::string& args) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->cursorCommand(verb, args);
    return ActionResult::Success();
}

// ========== Movement / Utility / Skills Actions (T140) ==========

ActionResult ActionDispatcher::showLocation() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->showLocation();
    return ActionResult::Success();
}

// T362 (B070). /warp took a user-typed name and shipped it to the game side,
// which resolved it with EntitiesComponent::FindEntityByName -- an unranked
// case-insensitive prefix match where the LOWEST SPAWN ID wins. With two
// same-named mobs in the zone that teleported the player to a different one
// than /target had picked. Same defect shape as T350's /face and T356's
// /moveto and /follow, one command later, and the last caller of that
// resolver (now deleted).
//
// The arg string is still parsed HERE rather than pushed down, because only
// the name form needs resolving: an empty arg means "use the current target"
// and three floats mean literal coordinates, and both of those stay the
// handler's warp(args) exactly as before.
ActionResult ActionDispatcher::warp(const std::string& args) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    std::string trimmed = args;
    while (!trimmed.empty() && trimmed[0] == ' ') {
        trimmed = trimmed.substr(1);
    }

    float x, y, z;
    std::istringstream coords(trimmed);
    const bool is_coordinates = static_cast<bool>(coords >> x >> y >> z);

    if (trimmed.empty() || is_coordinates) {
        m_handler->warp(args);
        return ActionResult::Success();
    }

    // Resolve ONCE, with the T244 nearest-first ranked overload -- see
    // moveToEntity() in action_dispatcher.cpp for the full note.
    auto& entities = m_state.entities();
    const auto& player = m_state.player();
    const auto* entity =
        entities.findEntityByName(trimmed, player.x(), player.y(), player.z());
    if (!entity) {
        return ActionResult::Failure("Entity not found: " + trimmed);
    }

    m_handler->warpToEntity(entity->spawnId);
    return ActionResult::Success("Warping to " + entity->name);
}

ActionResult ActionDispatcher::showOrSetZoning(const std::string& args) {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->showOrSetZoning(args);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::quit() {
    // T30: quit must work whether connected or not -- checkHandler() only,
    // deliberately not checkZoneConnection().
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->requestQuit();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::camp() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->campNow();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::debugLevel(const std::string& args) {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->debugLevel(args);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::perfReport() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->perfReport();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::timeOfDay(const std::string& args) {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->timeOfDay(args);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::bankToggle() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->bankToggle();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::bankCoinMove(uint8_t coinType, int32_t amount, bool fromBank) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->bankCoinMove(coinType, amount, fromBank);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::bankCoinConvert(uint8_t fromCoinType, uint8_t toCoinType, int32_t amount) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->bankCoinConvert(fromCoinType, toCoinType, amount);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::trainWithTarget() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->trainWithTarget();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::trainSkill(const std::string& args) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (args.empty()) {
        return ActionResult::Failure("Usage: /train <skill name> | /train <skill id> | /train list");
    }

    // T280: no skill lookup here on purpose. "1H Slash", "20" and "list" are
    // all resolved against the OPEN trainer session's caps, which live on
    // the game side (EverQuest::m_trainer_skill_caps, filled by the
    // server's OP_GMTraining reply). GameState carries no such list, so a
    // check here would be a second, weaker copy of the one
    // EverQuest::RequestTrainSkill() has to do anyway -- T277's rule.
    m_handler->trainSkill(args);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::closeTrainer() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->closeTrainer();
    return ActionResult::Success();
}

// ===== T150: Social / Find family =====
// Same shape as assist()/shieldTarget() above: guard the zone connection,
// route, return a generic Success(). All user-visible reporting (the
// no-target message, and the eventual server reply) happens elsewhere --
// EqActionHandler for the target fallback, the ZoneProcess* parsers for
// the replies. See refactor/T150_social_find_family.md.

ActionResult ActionDispatcher::assistGroup(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->assistGroup(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::trackTarget(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->trackTarget(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::trackingCommand(eqt::tracking::TrackingCommand cmd,
                                               const std::string& arg) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->trackingCommand(cmd, arg);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::findPerson(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->findPerson(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::friendsWho(const std::string& friendsList) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (friendsList.empty()) {
        return ActionResult::Failure("Usage: /friendswho <name>[,<name>...]");
    }

    m_handler->friendsWho(friendsList);
    return ActionResult::Success();
}

// ========== GM Actions (T148) ==========
//
// Net-new plumbing over the pre-existing EverQuest::SendGM*() senders
// (src/client/eq_gm.cpp), all nine of which had zero production callers
// until this unit. The senders already own their own zone-connection guard
// (`if (m_zone_connection && m_zone_connected)`), exactly like the Guild
// family's did (T132); the wrappers below are the gate plus routing.

ActionResult ActionDispatcher::checkGMMode() const {
    auto result = checkHandler();
    if (!result.success) return result;

    if (!m_handler->isGMMode()) {
        // Deliberately specific. "Unknown command" is what a player gets for
        // a command that does not exist; this one exists and is refused, and
        // the message says both why and what to do about it. The relog
        // instruction is not a workaround for a bug -- the check is
        // login-time BY DESIGN (Will, 2026-08-19), and the AT_GM live-update
        // path was explicitly declined.
        return ActionResult::Failure(
            "That command is a GM command and you were not in GM mode when you "
            "logged in. Use #gm on, then log out and back in.");
    }
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmZoneRequest(const std::string& charName, uint16_t zoneId) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /gmzone <name> <zone_id>");
    }

    m_handler->gmZoneRequest(charName, zoneId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmSummon(const std::string& charName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /summon <name>");
    }

    m_handler->gmSummon(charName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmGoto(const std::string& charName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /goto <name>");
    }

    m_handler->gmGoto(charName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmFind(const std::string& charName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /gmfind <name>");
    }

    m_handler->gmFind(charName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmKick(const std::string& charName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /kick <name>");
    }

    m_handler->gmKick(charName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmKill(const std::string& charName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /kill <name>");
    }

    m_handler->gmKill(charName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmHideMe(bool hide) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->gmHideMe(hide);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmEmoteZone(const std::string& text) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (text.empty()) {
        return ActionResult::Failure("Usage: /emotezone <message>");
    }

    m_handler->gmEmoteZone(text);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmLastName(const std::string& charName, const std::string& lastName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty() || lastName.empty()) {
        return ActionResult::Failure("Usage: /gmlastname <name> <lastname>");
    }

    m_handler->gmLastName(charName, lastName);
    return ActionResult::Success();
}

// T153: /appearance -> EverQuest::SendFaceChange(). Zone-gated like every
// other zone-scoped send; the empty-change guard is here rather than in
// CommandProcessor so a programmatic caller cannot send a no-op packet either.
ActionResult ActionDispatcher::setAppearance(const AppearanceChange& change) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (change.empty()) {
        return ActionResult::Failure(
            "Usage: /appearance <field> <0-255> [<field> <0-255> ...]");
    }

    m_handler->setAppearance(change);
    return ActionResult::Success();
}

// T171: /inspectmessage. Deliberately NOT zone-gated, unlike every sender
// above it: this changes local state and writes the config file, and a player
// should be able to set it at a character-select or loading screen. The value
// reaches the server on its own schedule -- the next time somebody inspects
// us -- so there is nothing here that needs a zone connection.
ActionResult ActionDispatcher::setInspectMessage(const std::optional<std::string>& message) {
    if (!m_handler) {
        return ActionResult::Failure("No action handler");
    }
    m_handler->setInspectMessage(message);
    return ActionResult::Success();
}

} // namespace action
} // namespace eqt
