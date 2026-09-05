#include "client/action/command_processor.h"
#include "client/state/game_state.h"
#include "client/output/output_renderer.h"
#include "client/skill/skill_constants.h"  // T156: /useskill id/name resolution
#include "client/inventory/inventory_constants.h"  // T278: /combine <general slot>
#include "common/logging.h"
#include <fmt/format.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <cctype>

namespace eqt {
namespace action {

// ========== Command Implementations ==========

ActionResult CommandProcessor::cmdShout(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Shout, args);
}

// ========== Chat Commands (T100) ==========
//
// Ported from EverQuest::m_command_registry's Chat-category handlers
// (eq_commands.cpp's RegisterSocialCommands(), deleted from there in this
// same change -- see refactor/T100_migrate_chat_family.md's atomicity
// section). T97's divergence table rated the whole family "Equivalent" --
// both sides terminate in EverQuest::SendChatMessage() either way -- so
// these are direct ports, re-expressed against ActionDispatcher's existing
// sendChatMessage()/sendTell()/replyToLastTell() (already correct, already
// implemented in EqActionHandler; no new ActionDispatcher method needed).

ActionResult CommandProcessor::cmdSay(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Say, args);
}

ActionResult CommandProcessor::cmdOOC(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::OOC, args);
}

ActionResult CommandProcessor::cmdAuction(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Auction, args);
}

ActionResult CommandProcessor::cmdGsay(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Group, args);
}

ActionResult CommandProcessor::cmdGu(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Guild, args);
}

ActionResult CommandProcessor::cmdTell(const std::string& args) {
    // Ported from game's tell.handler verbatim: split on the FIRST space
    // only, so "/tell Name multi word message" -> target="Name",
    // message="multi word message" (the message itself may contain further
    // spaces, unsplit). game's else-if branch ("no space found, args
    // non-empty") showed a usage message and did nothing; requiresArgs=true
    // on this registration already rejects the fully-empty case before this
    // handler is ever called, so only the "single word, no space" case needs
    // handling here.
    size_t spacePos = args.find(' ');
    if (spacePos != std::string::npos) {
        std::string target = args.substr(0, spacePos);
        std::string message = args.substr(spacePos + 1);
        return m_dispatcher.sendTell(target, message);
    }
    return ActionResult::Failure("Usage: /tell <player> <message>");
}

ActionResult CommandProcessor::cmdEmote(const std::string& args) {
    // game's /emote always sends the literal text as a text emote (matches
    // real EQ). T97 flagged this as the "Live is better" case for this
    // family: the old (T98-deleted) action copy silently reinterpreted 9
    // known words as animation triggers instead of text -- NOT ported; this
    // is the real-EQ text-emote behavior only.
    return m_dispatcher.sendChatMessage(ChatChannel::Emote, args);
}

ActionResult CommandProcessor::cmdReply(const std::string& args) {
    // Ported from game's reply_cmd's args-empty branch ("Usage: /reply
    // <message>"). game's OTHER pre-check (m_last_tell_sender.empty() ->
    // "No one to reply to") lives in ActionDispatcher::replyToLastTell()
    // since T222, via IActionHandler::hasTellToReplyTo() -- T100 could not
    // port it because m_last_tell_sender was unreachable from this layer.
    if (args.empty()) {
        return ActionResult::Failure("Usage: /reply <message>");
    }
    return m_dispatcher.replyToLastTell(args);
}

ActionResult CommandProcessor::cmdFace(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /face <name> or /face <x> <y> <z>");
    }

    auto parts = splitArgs(args);
    if (parts.size() >= 3) {
        try {
            float x = std::stof(parts[0]);
            float y = std::stof(parts[1]);
            float z = std::stof(parts[2]);
            return m_dispatcher.faceLocation(x, y, z);
        } catch (const std::exception&) {
            // Fall through to treat as entity name
        }
    }

    return m_dispatcher.faceEntity(args);
}

ActionResult CommandProcessor::cmdConsider(const std::string& args) {
    return m_dispatcher.consider();
}

ActionResult CommandProcessor::cmdHail(const std::string& args) {
    // T39: m_state.combat().hasTarget() is now correct as written.
    // CombatManager writes its target into this same state::GameState
    // instance from its own choke points (SetTarget/ClearTarget, combat.cpp)
    // -- T39's per-frame Application::syncCombatState() poll was deleted by
    // T104 and T227 folded the write to the source. That keeps the
    // decoupling T12 established: this file
    // still holds only state::GameState& and ActionDispatcher&, no
    // CombatManager dependency was added. See refactor/deferred.md "Dead
    // parallel state copies in the state layer, WITH LIVE READERS" for the
    // T28/T39 history of this reader.
    if (m_state.combat().hasTarget()) {
        return m_dispatcher.hailTarget();
    }
    return m_dispatcher.hail();
}

ActionResult CommandProcessor::cmdNpcState(const std::string& args) {
    // T49: pure state-layer read, no args, no dispatcher call -- same shape
    // as cmdLoc(). Reports vendor/banker/trainer context fed by
    // GameStateSyncBridge from the six window-open/close bridge events.
    auto& player = m_state.player();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);

    if (player.isWithVendor()) {
        oss << "Vendor: " << player.vendorName() << " (id=" << player.vendorNpcId()
            << ", sellRate=" << player.vendorSellRate() << ")";
    } else {
        oss << "Vendor: none";
    }
    displayMessage(oss.str());

    oss.str("");
    if (player.isAtBank()) {
        oss << "Banker: id=" << player.bankerNpcId();
    } else {
        oss << "Banker: none";
    }
    displayMessage(oss.str());

    oss.str("");
    if (player.isWithTrainer()) {
        oss << "Trainer: " << player.trainerName() << " (id=" << player.trainerNpcId() << ")";
    } else {
        oss << "Trainer: none";
    }
    displayMessage(oss.str());

    return ActionResult::Success();
}

ActionResult CommandProcessor::cmdDoor(const std::string& args) {
    return m_dispatcher.clickNearestDoor();
}

ActionResult CommandProcessor::cmdAFK(const std::string& args) {
    return m_dispatcher.toggleAFK();
}

ActionResult CommandProcessor::cmdAnon(const std::string& args) {
    bool currentState = m_state.player().isAnonymous();
    return m_dispatcher.setAnonymous(!currentState);
}

// ========== Group Commands (T99) ==========
//
// Ported from EverQuest::m_command_registry's Group-category handlers
// (eq_commands.cpp, deleted from there in this same change -- see
// refactor/T99_action_layer_unification_pilot.md's atomicity section).
// T97's divergence table found game's invite/disband/decline better than
// action's old (T98-deleted) copies; these are that better implementation,
// re-expressed against ActionDispatcher/GameState instead of EverQuest's
// private members directly.

ActionResult CommandProcessor::cmdInvite(const std::string& args) {
    // Ported from game's invite_cmd: explicit name wins; otherwise fall back
    // to the current combat target. inviteToGroup()/inviteTarget() are the
    // two ActionDispatcher methods this call gives production callers to --
    // T98 reported both newly caller-free after deleting the old dead copy.
    if (!args.empty()) {
        auto result = m_dispatcher.inviteToGroup(args);
        if (result.success) {
            displayMessage("Inviting " + args + " to group");
        }
        return result;
    }

    if (!m_state.combat().hasTarget()) {
        return ActionResult::Failure("Usage: /invite <name> or target a player");
    }

    auto result = m_dispatcher.inviteTarget();
    if (result.success) {
        displayMessage("Inviting " + m_state.combat().targetName() + " to group");
    }
    return result;
}

ActionResult CommandProcessor::cmdDisband(const std::string& args) {
    // Ported from game's disband_cmd: leader vs member gets a different
    // local message (matches EQ's own player-facing wording). The network
    // effect is identical either way -- EverQuest::SendLeaveGroup() is
    // itself just a call to SendGroupDisband() (eq_group.cpp) -- so a single
    // ActionDispatcher::leaveGroup() call covers both cases; only the
    // message construction needs the leader/member branch.
    if (!m_state.group().inGroup()) {
        return ActionResult::Failure("You are not in a group");
    }

    bool wasLeader = m_state.group().isLeader();
    auto result = m_dispatcher.leaveGroup();
    if (result.success) {
        displayMessage(wasLeader ? "Group disbanded" : "You have left the group");
    }
    return result;
}

// T179: /raid <subcommand>. A dispatcher family, not a single sender -- accept
// answers the pending invite T172 could only watch arrive. Subcommands route
// to ActionDispatcher::raid*, which reach EverQuest::SendRaidCommand (invite/
// accept/leave) or read raid state (status). Bare /raid or /raid status prints
// the current state. NOTE: raid membership lives on EverQuest (from the decoded
// OP_RaidUpdate stream), NOT in the m_state projection, so these route through
// the handler rather than reading m_state like the group commands above.
ActionResult CommandProcessor::cmdRaid(const std::string& args) {
    // First token = subcommand, remainder = argument. Parsed inline rather than
    // via splitWhitespace() (a helper in command_processor_internal.h, which
    // this TU does not include).
    const size_t tokEnd = args.find_first_of(" \t");
    const std::string sub = toLower(
        tokEnd == std::string::npos ? args : args.substr(0, tokEnd));
    std::string rest;
    if (tokEnd != std::string::npos) {
        const size_t restStart = args.find_first_not_of(" \t", tokEnd);
        if (restStart != std::string::npos) rest = args.substr(restStart);
    }

    if (sub.empty() || sub == "status") {
        return m_dispatcher.raidStatus();
    }
    if (sub == "accept") {
        return m_dispatcher.raidAccept();
    }
    if (sub == "leave") {
        return m_dispatcher.raidLeave();
    }
    if (sub == "list" || sub == "members") {
        return m_dispatcher.raidList();
    }
    // T181 leader-only ops. The argument checks that would need a message to
    // the player (not in a raid, not the leader, not a member, bad loot type)
    // live in the handler, which can reach raid state; only the arity errors
    // are caught here, matching how /raid invite already splits it.
    if (sub == "remove" || sub == "kick") {
        if (rest.empty()) {
            return ActionResult::Failure("Usage: /raid remove <name>");
        }
        return m_dispatcher.raidRemove(rest);
    }
    if (sub == "leader" || sub == "makeleader") {
        if (rest.empty()) {
            return ActionResult::Failure("Usage: /raid leader <name>");
        }
        return m_dispatcher.raidMakeLeader(rest);
    }
    if (sub == "loot" || sub == "loottype") {
        if (rest.empty()) {
            return ActionResult::Failure(
                "Usage: /raid loot <leader|groupleader|selected|all>");
        }
        return m_dispatcher.raidLootType(rest);
    }
    if (sub == "group") {
        // Two words: the member, then the group. Same split as /raid looter.
        const size_t sp = rest.find_first_of(" \t");
        if (sp == std::string::npos) {
            return ActionResult::Failure("Usage: /raid group <name> <1-12|none>");
        }
        const std::string who = rest.substr(0, sp);
        std::string group;
        const size_t start = rest.find_first_not_of(" \t", sp);
        if (start != std::string::npos) group = rest.substr(start);
        if (who.empty() || group.empty()) {
            return ActionResult::Failure("Usage: /raid group <name> <1-12|none>");
        }
        return m_dispatcher.raidGroup(who, group);
    }
    if (sub == "lock") {
        return m_dispatcher.raidLock(true);
    }
    if (sub == "unlock") {
        return m_dispatcher.raidLock(false);
    }
    if (sub == "looter") {
        // Two words: add|remove, then the name.
        const size_t sp = rest.find_first_of(" \t");
        const std::string verb = toLower(
            sp == std::string::npos ? rest : rest.substr(0, sp));
        std::string who;
        if (sp != std::string::npos) {
            const size_t start = rest.find_first_not_of(" \t", sp);
            if (start != std::string::npos) who = rest.substr(start);
        }
        if ((verb != "add" && verb != "remove") || who.empty()) {
            return ActionResult::Failure("Usage: /raid looter <add|remove> <name>");
        }
        return m_dispatcher.raidLooter(verb == "add", who);
    }
    if (sub == "invite") {
        if (rest.empty()) {
            return ActionResult::Failure("Usage: /raid invite <name>");
        }
        return m_dispatcher.raidInvite(rest);
    }
    return ActionResult::Failure(
        "Usage: /raid [status|list|invite <name>|accept|leave|remove <name>|leader <name>|loot <mode>|looter add|remove <name>|lock|unlock|group <name> <1-12|none>]");
}

ActionResult CommandProcessor::cmdDecline(const std::string& args) {
    // Ported from game's decline_cmd. ActionDispatcher::declineGroupInvite()
    // already performs the same hasPendingInvite() check internally
    // (returning Failure("No pending group invite") itself); the check here
    // is kept so the pre-check short-circuits before touching the dispatcher
    // at all, matching game's exact shape and giving a clean early return
    // for the group-accept unreachability test below.
    if (!m_state.group().hasPendingInvite()) {
        return ActionResult::Failure("No pending group invite");
    }

    auto result = m_dispatcher.declineGroupInvite();
    if (result.success) {
        displayMessage("Group invite declined");
    }
    return result;
}

ActionResult CommandProcessor::cmdGroupAccept(const std::string& args) {
    // T99: NEW command. This is where the group-invite-accept gap gets
    // fixed. game's own Social follow_cmd used to gate accept-on-no-args
    // behind bare /follow, but that was itself dead (shadowed by Movement's
    // later "follow" registration -- T97) even before T98 deleted action's
    // separately-shadowed copy. Reviving bare /follow here would put it back
    // in a head-on collision with Movement's live entity-following /follow,
    // so this pilot uses an explicit spelling instead: /groupaccept (alias
    // /gaccept). Gives ActionDispatcher::acceptGroupInvite() -- orphaned
    // since T98 -- its first live caller.
    if (!m_state.group().hasPendingInvite()) {
        return ActionResult::Failure("No pending group invite");
    }

    std::string inviterName = m_state.group().pendingInviterName();
    auto result = m_dispatcher.acceptGroupInvite();
    if (result.success) {
        displayMessage("You have joined " + inviterName + "'s group");
    }
    return result;
}

// ========== Combat + Social Commands (T102) ==========
//
// Ported from EverQuest::m_command_registry's Combat and Social categories
// (eq_commands.cpp/eq_commands_movement.cpp, deleted from there in this
// same change -- see refactor/T102_migrate_combat_social.md's atomicity
// section). Unlike Group's GameStateSyncBridge gap (T99) or Trade's total
// absence of a GameState projection (see cmdTrade below), combat targeting
// IS reliably live here: m_state.combat() is kept current by CombatManager
// itself, from SetTarget/ClearTarget (combat.cpp) -- the same read path
// cmdHail (T39) already uses. When this comment was written that currency
// came from Application::syncCombatState(), a per-frame poll unrelated to
// GameStateSyncBridge; T104 deleted the poll and T227 moved the write to the
// choke points. See the gravestone comment in application_zone_loading.cpp.

uint16_t CommandProcessor::resolveCombatTarget(const std::string& name, std::string* outError) {
    if (!name.empty()) {
        const auto* entity = m_state.entities().findEntityByName(name);
        if (!entity) {
            if (outError) *outError = "No target found matching '" + name + "'";
            return 0;
        }
        return entity->spawnId;
    }
    if (!m_state.combat().hasTarget()) {
        if (outError) *outError = "You don't have a target.";
        return 0;
    }
    return m_state.combat().targetId();
}

ActionResult CommandProcessor::cmdTarget(const std::string& args) {
    // Ported from game's target.handler: no-args is a usage error (unlike
    // assist/shield/duel below, target has no "use current target"
    // fallback -- it IS how you set the current target). Resolution itself
    // (case-insensitive substring match) is EntityManager::findEntityByName(),
    // matching game's own lowercase-both-sides search semantics exactly.
    // T230: this is now the ONE name resolver -- CombatManager::SetTarget(
    // std::string) (T97: case-SENSITIVE; T173: corpse-unaware) was deleted
    // with its unreached targetEntityByName chain; only the resolved-id
    // path (ActionDispatcher::targetEntity()) remains.
    if (args.empty()) {
        return ActionResult::Failure("Usage: /target <name>");
    }
    // T244: nearest-first among equal-rank matches (distance from the
    // player is the secondary key; living-over-corpse stays primary).
    const auto& player = m_state.player();
    const auto* entity = m_state.entities().findEntityByName(
        args, player.x(), player.y(), player.z());
    if (!entity) {
        return ActionResult::Failure("No target found matching '" + args + "'");
    }
    auto result = m_dispatcher.targetEntity(entity->spawnId);
    if (result.success) {
        displayMessage("Targeting: " + entity->name);
    }
    return result;
}

ActionResult CommandProcessor::cmdClearTarget(const std::string& args) {
    // T347 (B012). The dispatcher owns the "is there a handler" check and the
    // message, exactly as it does for the keybinding route
    // (input_action_bridge.cpp -> ActionDispatcher::clearTarget()); this is
    // the same call, reachable from --script and from /help.
    return m_dispatcher.clearTarget();
}

ActionResult CommandProcessor::cmdAttack(const std::string& args) {
    // Ported from game's attack.handler -- enableAutoAttack() already does
    // the "no target" check inside the handler (CombatManager), matching
    // game's own pre-check exactly (T51's comment: "Target check is done by
    // the handler").
    return m_dispatcher.enableAutoAttack();
}

ActionResult CommandProcessor::cmdStopAttack(const std::string& args) {
    return m_dispatcher.disableAutoAttack();
}

ActionResult CommandProcessor::cmdAA(const std::string& args) {
    // Ported from game's aa.handler: disable if enabled, else enable if
    // targeted, else no-op -- toggleAutoAttack() (pre-existing, used
    // directly by InputActionBridge's keyboard gesture too) already
    // implements exactly this.
    return m_dispatcher.toggleAutoAttack();
}

ActionResult CommandProcessor::cmdCombat(const std::string& args) {
    // Ported from game's combat.handler verbatim: on/1/true, off/0/false,
    // empty = toggle, else usage. This is /combat on|off -- T26's
    // historically-unreachable command, reachable since T29's unified
    // command channel; migrating it here keeps it reachable as the sole
    // (migrated) implementation.
    std::string arg = args;
    while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
    while (!arg.empty() && arg.back() == ' ') arg.pop_back();

    if (arg.empty()) {
        return m_dispatcher.toggleCombatManager();
    } else if (arg == "on" || arg == "1" || arg == "true") {
        return m_dispatcher.enableCombatManager();
    } else if (arg == "off" || arg == "0" || arg == "false") {
        return m_dispatcher.disableCombatManager();
    }
    return ActionResult::Failure("Usage: /combat [on|off]");
}

ActionResult CommandProcessor::cmdTaunt(const std::string& args) {
    if (!m_state.combat().hasTarget()) {
        return ActionResult::Failure("No target to taunt");
    }
    return m_dispatcher.taunt();
}

namespace {

// T156: normalise a skill name for comparison — lowercase, and drop every
// character that is not a letter or digit, so "Bind Wound", "bindwound" and
// "bind-wound" all collapse to "bindwound". EQ::getSkillName()'s table is the
// only name source; there is no separate skill DB to consult.
std::string normalizeSkillName(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

// Resolve a skill NAME to its id. Returns false if no entry matches.
bool findSkillIdByName(const std::string& name, uint8_t& outId) {
    const std::string wanted = normalizeSkillName(name);
    if (wanted.empty()) return false;
    for (uint32_t id = 0; id < EQ::MAX_SKILL_ID; ++id) {
        const char* skillName = EQ::getSkillName(static_cast<uint8_t>(id));
        if (normalizeSkillName(skillName) == wanted) {
            outId = static_cast<uint8_t>(id);
            return true;
        }
    }
    return false;
}

} // namespace

ActionResult CommandProcessor::cmdUseSkill(const std::string& args) {
    // T156. Accepts an id or a name; a bare numeric id alone is the
    // "technically wired, practically unreachable" interface this arc keeps
    // deleting, so the name form is the one a human will actually type.
    std::string arg = args;
    while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.back()))) arg.pop_back();
    if (arg.empty()) {
        return ActionResult::Failure("Usage: /useskill <id|name>  (e.g. /useskill taunt)");
    }

    uint8_t skillId = 0;
    const bool numeric = std::all_of(arg.begin(), arg.end(),
        [](unsigned char c) { return std::isdigit(c) != 0; });

    if (numeric) {
        // T95: validate against the skill table before it becomes a skill id.
        // No unchecked int-to-enum / int-to-index cast.
        unsigned long parsed = 0;
        try {
            parsed = std::stoul(arg);
        } catch (const std::exception&) {
            return ActionResult::Failure("Unknown skill '" + arg + "'");
        }
        if (parsed >= EQ::MAX_SKILL_ID) {
            return ActionResult::Failure(fmt::format(
                "Unknown skill id {} (valid ids are 0-{})", parsed, EQ::MAX_SKILL_ID - 1));
        }
        skillId = static_cast<uint8_t>(parsed);
        if (std::string(EQ::getSkillName(skillId)) == "Unknown") {
            return ActionResult::Failure(fmt::format("Unknown skill id {}", parsed));
        }
    } else if (!findSkillIdByName(arg, skillId)) {
        return ActionResult::Failure("Unknown skill '" + arg + "'");
    }

    if (!EQ::isActivatableSkill(skillId)) {
        return ActionResult::Failure(fmt::format(
            "{} is not a skill you can activate", EQ::getSkillName(skillId)));
    }

    // Target resolution itself lives in EqActionHandler::useSkill(), which has
    // the live CombatManager; the dispatcher signature stays id-only.
    return m_dispatcher.useSkill(skillId);
}

ActionResult CommandProcessor::cmdAutofire(const std::string& args) {
    // Ported from game's autofire.handler: same enable/disable-toggle shape
    // as /aa, over CombatManager's auto-fire flag instead of auto-attack's.
    return m_dispatcher.toggleAutoFire();
}

ActionResult CommandProcessor::cmdAssist(const std::string& args) {
    // Ported from game's assist_cmd: explicit name wins (resolved here via
    // EntityManager, whose substring search matched the game-side
    // FindEntityByName the port replaced -- T362 deleted that one), else
    // falls back to the current combat target.
    std::string error;
    uint16_t targetId = resolveCombatTarget(args, &error);
    if (targetId == 0) {
        return ActionResult::Failure(error.empty() ? ("Could not find '" + args + "'") : error);
    }
    return m_dispatcher.assist(targetId);
}

ActionResult CommandProcessor::cmdShield(const std::string& args) {
    // Ported from game's shield_cmd: current target only, no name argument.
    if (!m_state.combat().hasTarget()) {
        return ActionResult::Failure("You don't have a target to shield.");
    }
    return m_dispatcher.shieldTarget(m_state.combat().targetId());
}

ActionResult CommandProcessor::cmdDuel(const std::string& args) {
    // Ported from game's duel_cmd: current target only, no name argument.
    if (!m_state.combat().hasTarget()) {
        return ActionResult::Failure("You don't have a target.");
    }
    return m_dispatcher.duelRequest(m_state.combat().targetId());
}

ActionResult CommandProcessor::cmdDuelAccept(const std::string& args) {
    // Ported from game's duelaccept_cmd. The pending-duel pre-check itself
    // (EverQuest::HasPendingDuel(), GameState has no duel domain) lives in
    // EqActionHandler::duelAccept() -- there is nothing for this layer to
    // check first, unlike Group's hasPendingInvite() which IS projected
    // into GameState.
    return m_dispatcher.duelAccept();
}

ActionResult CommandProcessor::cmdDuelDecline(const std::string& args) {
    return m_dispatcher.duelDecline();
}

// ---------- Social ----------

ActionResult CommandProcessor::cmdTrade(const std::string& args) {
    // Ported from game's trade_cmd. Unlike Combat above, Trade has NO
    // GameState projection at all (checked: no GameStateSyncBridge case,
    // no per-frame poll analogous to syncCombatState() -- see this unit's
    // Review). So this command's full precondition chain (already trading?
    // in combat? too far away? self-trade?) lives in
    // EqActionHandler::requestTrade(), which reads EverQuest::
    // GetTradeManager()/GetCombatManager() directly (both public, live,
    // authoritative) instead of a GameState field. This layer only does
    // name resolution (when a name is given) and passes 0 through
    // otherwise, letting the handler fall back to the live combat target
    // exactly like game's own code did.
    uint16_t targetId = 0;
    if (!args.empty()) {
        const auto* entity = m_state.entities().findEntityByName(args);
        if (!entity) {
            return ActionResult::Failure("No player found matching '" + args + "'");
        }
        targetId = entity->spawnId;
    }
    return m_dispatcher.requestTrade(targetId);
}

ActionResult CommandProcessor::cmdTradeAccept(const std::string& args) {
    return m_dispatcher.acceptTrade();
}

ActionResult CommandProcessor::cmdTradeDecline(const std::string& args) {
    return m_dispatcher.declineTrade();
}

ActionResult CommandProcessor::cmdCancelTrade(const std::string& args) {
    return m_dispatcher.cancelTrade();
}

ActionResult CommandProcessor::cmdWho(const std::string& args) {
    // T97/T82: `who` is G02's intra-registry duplicate -- `game` itself has
    // TWO "who" registrations (this one, Social's who_cmd; and Movement's
    // `who`), and because RegisterMovementCommands() runs after
    // RegisterSocialCommands(), Movement's has been the one silently
    // winning every dispatch since G02, with Social's who_cmd dead by
    // shadowing. This port CHOSE Social's who_cmd -- the real server /who
    // request (SendWhoAllRequest(), with name/level-range parsing, a real
    // OP_WhoAllResponse round trip) -- over Movement's `who` (a client-side
    // scan of locally-known m_entities, format "[id] Name (Lvl N)",
    // "...and N more"), because Social's is what real EQ's /who actually
    // does; Movement's is a client-only substitute. Movement's `who`
    // registration is deleted (not migrated) in the same change -- see
    // eq_commands_movement.cpp. Argument parsing ported verbatim from
    // who_cmd.handler.
    std::string name;
    int lvllow = -1, lvlhigh = -1;

    std::istringstream iss(args);
    std::string token;
    std::vector<std::string> tokens;
    while (iss >> token) {
        tokens.push_back(token);
    }

    // T224 live leg: "/who all" was sending whom="all" as a NAME filter and
    // the server answered "no players ... match those who filters". On
    // Titanium the request is the same packet either way (eqemu's
    // DECODE(OP_WhoAllRequest) forces type=3, world-wide), so "all" is a
    // keyword to drop, not a name.
    if (!tokens.empty()) {
        std::string lowered = tokens[0];
        for (auto& ch : lowered) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lowered == "all") {
            tokens.erase(tokens.begin());
        }
    }

    if (!tokens.empty()) {
        bool firstIsLevel = false;
        try {
            lvllow = std::stoi(tokens[0]);
            firstIsLevel = true;
        } catch (...) {
            name = tokens[0];
        }

        if (firstIsLevel && tokens.size() >= 2) {
            try {
                lvlhigh = std::stoi(tokens[1]);
            } catch (...) {
                lvlhigh = lvllow;
            }
        } else if (!firstIsLevel && tokens.size() >= 2) {
            try {
                lvllow = std::stoi(tokens[1]);
                lvlhigh = (tokens.size() >= 3) ? std::stoi(tokens[2]) : lvllow;
            } catch (...) {
                // Ignore level parsing errors, matching game's handler.
            }
        }
    }

    return m_dispatcher.requestWho(name, lvllow, lvlhigh);
}

ActionResult CommandProcessor::cmdInspect(const std::string& args) {
    // Ported from game's inspect_cmd: explicit name wins, else current target.
    std::string error;
    uint16_t targetId = resolveCombatTarget(args, &error);
    if (targetId == 0) {
        std::string usageError = args.empty()
            ? "No target - use /inspect <name> or target a player"
            : ("Player '" + args + "' not found");
        return ActionResult::Failure(usageError);
    }
    return m_dispatcher.inspectTarget(targetId);
}

ActionResult CommandProcessor::cmdLFG(const std::string& args) {
    return m_dispatcher.toggleLFG();
}

ActionResult CommandProcessor::cmdSurname(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /surname <lastname>");
    }
    return m_dispatcher.setSurname(args);
}

ActionResult CommandProcessor::cmdRandom(const std::string& args) {
    uint32_t low = 0, high = 100;
    std::istringstream iss(args);
    iss >> low >> high;
    return m_dispatcher.rollRandom(low, high);
}

ActionResult CommandProcessor::cmdYell(const std::string& args) {
    return m_dispatcher.yellForHelp();
}

// ========== Guild Commands (T132) ==========
//
// Ported from eq_commands.cpp's Guild block (deleted from there in this
// same change -- see refactor/T132_migrate_guild_family.md's atomicity
// section). Net-new: game's handlers already do all their own guarding
// inside EverQuest::SendGuild*() (guild membership, pending-invite state),
// so these are thin parsing + dispatch, matching every other migrated
// family's shape. The empty-name usage checks below are ported verbatim
// from game's pre-dispatch guards (identical usage strings).

ActionResult CommandProcessor::cmdGuildInvite(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /guildinvite <name>");
    }
    return m_dispatcher.guildInvite(args);
}

ActionResult CommandProcessor::cmdGuildAccept(const std::string& args) {
    return m_dispatcher.guildAcceptInvite();
}

ActionResult CommandProcessor::cmdGuildDecline(const std::string& args) {
    return m_dispatcher.guildDeclineInvite();
}

ActionResult CommandProcessor::cmdGuildRemove(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /guildremove <name>");
    }
    return m_dispatcher.guildRemove(args);
}

ActionResult CommandProcessor::cmdGuildDemote(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /guilddemote <name>");
    }
    return m_dispatcher.guildDemote(args);
}

ActionResult CommandProcessor::cmdGuildLeader(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /guildleader <name>");
    }
    return m_dispatcher.guildMakeLeader(args);
}

ActionResult CommandProcessor::cmdGuildMotd(const std::string& args) {
    // Ported from game's guildmotd_cmd: empty args requests the current
    // MOTD, non-empty args sets a new one.
    if (args.empty()) {
        return m_dispatcher.requestGuildMOTD();
    }
    return m_dispatcher.setGuildMOTD(args);
}

} // namespace action
} // namespace eqt
