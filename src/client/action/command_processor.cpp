#include "client/action/command_processor.h"
#include "client/state/game_state.h"
#include "client/output/output_renderer.h"
#include "common/logging.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace eqt {
namespace action {

CommandProcessor::CommandProcessor(state::GameState& state, ActionDispatcher& dispatcher)
    : m_state(state)
    , m_dispatcher(dispatcher) {
    registerBuiltinCommands();
}

CommandProcessor::~CommandProcessor() = default;

void CommandProcessor::setOutputRenderer(output::IOutputRenderer* renderer) {
    m_renderer = renderer;
}

ActionResult CommandProcessor::processCommand(const std::string& input) {
    if (input.empty()) {
        return ActionResult::Success();
    }

    // Remove leading '/' if present
    std::string cmdLine = input;
    if (!cmdLine.empty() && cmdLine[0] == '/') {
        cmdLine = cmdLine.substr(1);
    }

    // Parse command and arguments
    auto [cmdName, args] = parseCommandLine(cmdLine);
    if (cmdName.empty()) {
        return ActionResult::Success();
    }

    // Echo the command if enabled
    if (m_echoEnabled && m_renderer) {
        m_renderer->displaySystemMessage("/" + cmdName + (args.empty() ? "" : " " + args));
    }

    // Look up command
    std::string lowerName = toLower(cmdName);

    // Check aliases first
    auto aliasIt = m_aliases.find(lowerName);
    if (aliasIt != m_aliases.end()) {
        lowerName = aliasIt->second;
    }

    // Find command
    auto cmdIt = m_commands.find(lowerName);
    if (cmdIt == m_commands.end()) {
        displayError("Unknown command: " + cmdName);
        return ActionResult::Failure("Unknown command: " + cmdName);
    }

    const auto& cmd = cmdIt->second;

    // Check if arguments are required
    if (cmd.info.requiresArgs && args.empty()) {
        displayError("Usage: " + cmd.info.usage);
        return ActionResult::Failure("Missing arguments");
    }

    // Execute the command
    return cmd.handler(args);
}

ActionResult CommandProcessor::processInput(const std::string& input) {
    if (input.empty()) {
        return ActionResult::Success();
    }

    // If it starts with '/', treat as command
    if (input[0] == '/') {
        return processCommand(input);
    }

    // Otherwise, send as default channel message
    return m_dispatcher.sendChatMessage(m_defaultChannel, input);
}

void CommandProcessor::registerCommand(const CommandInfo& info, CommandHandler handler) {
    std::string lowerName = toLower(info.name);

    RegisteredCommand regCmd;
    regCmd.info = info;
    regCmd.handler = std::move(handler);

    m_commands[lowerName] = std::move(regCmd);

    // Register aliases
    for (const auto& alias : info.aliases) {
        m_aliases[toLower(alias)] = lowerName;
    }
}

void CommandProcessor::unregisterCommand(const std::string& name) {
    std::string lowerName = toLower(name);

    auto it = m_commands.find(lowerName);
    if (it != m_commands.end()) {
        // Remove aliases
        for (const auto& alias : it->second.info.aliases) {
            m_aliases.erase(toLower(alias));
        }
        m_commands.erase(it);
    }
}

bool CommandProcessor::hasCommand(const std::string& name) const {
    std::string lowerName = toLower(name);
    return m_commands.count(lowerName) > 0 || m_aliases.count(lowerName) > 0;
}

std::optional<CommandInfo> CommandProcessor::getCommandInfo(const std::string& name) const {
    std::string lowerName = toLower(name);

    // Check aliases
    auto aliasIt = m_aliases.find(lowerName);
    if (aliasIt != m_aliases.end()) {
        lowerName = aliasIt->second;
    }

    auto it = m_commands.find(lowerName);
    if (it != m_commands.end()) {
        return it->second.info;
    }
    return std::nullopt;
}

std::vector<CommandInfo> CommandProcessor::getAllCommands() const {
    std::vector<CommandInfo> result;
    result.reserve(m_commands.size());
    for (const auto& [name, cmd] : m_commands) {
        result.push_back(cmd.info);
    }
    return result;
}

std::vector<CommandInfo> CommandProcessor::getCommandsByCategory(const std::string& category) const {
    std::vector<CommandInfo> result;
    for (const auto& [name, cmd] : m_commands) {
        if (cmd.info.category == category) {
            result.push_back(cmd.info);
        }
    }
    return result;
}

std::vector<std::string> CommandProcessor::getCategories() const {
    std::vector<std::string> result;
    for (const auto& [name, cmd] : m_commands) {
        if (std::find(result.begin(), result.end(), cmd.info.category) == result.end()) {
            result.push_back(cmd.info.category);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> CommandProcessor::getCompletions(const std::string& partial) const {
    std::vector<std::string> result;
    std::string lowerPartial = toLower(partial);

    for (const auto& [name, cmd] : m_commands) {
        if (name.find(lowerPartial) == 0) {
            result.push_back(cmd.info.name);
        }
    }

    for (const auto& [alias, primary] : m_aliases) {
        if (alias.find(lowerPartial) == 0) {
            result.push_back(alias);
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<std::string> CommandProcessor::getAllCommandNames() const {
    std::vector<std::string> result;

    for (const auto& [name, cmd] : m_commands) {
        result.push_back(cmd.info.name);
        for (const auto& alias : cmd.info.aliases) {
            result.push_back(alias);
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

// ========== Helper Methods ==========

std::string CommandProcessor::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::pair<std::string, std::string> CommandProcessor::parseCommandLine(const std::string& input) {
    if (input.empty()) {
        return {"", ""};
    }

    // Find first space
    size_t spacePos = input.find(' ');
    if (spacePos == std::string::npos) {
        return {input, ""};
    }

    std::string cmd = input.substr(0, spacePos);
    std::string args = input.substr(spacePos + 1);

    // Trim leading whitespace from args
    size_t start = args.find_first_not_of(" \t");
    if (start == std::string::npos) {
        args = "";
    } else {
        args = args.substr(start);
    }

    return {cmd, args};
}

std::vector<std::string> CommandProcessor::splitArgs(const std::string& args) {
    std::vector<std::string> result;
    std::istringstream iss(args);
    std::string token;
    while (iss >> token) {
        result.push_back(token);
    }
    return result;
}

void CommandProcessor::displayMessage(const std::string& message) {
    if (m_renderer) {
        m_renderer->displaySystemMessage(message);
    }
}

void CommandProcessor::displayError(const std::string& message) {
    if (m_renderer) {
        m_renderer->displaySystemMessage("Error: " + message);
    }
}

// T98: displayHelp() removed -- its only caller was cmdHelp (deleted, the
// "help" registration was shadowed dead code). getCommandInfo()/
// getCategories()/getCommandsByCategory() it used are public API used
// elsewhere and are untouched.

// ========== Register Built-in Commands ==========

void CommandProcessor::registerBuiltinCommands() {
    // T98 deleted say/ooc/auction/tell/reply/gsay/gu/emote as shadowed dead
    // code (game's registry always won the dispatch collision for these
    // primary names; see refactor/T97_command_registry_shadowing_audit.md).
    // T100 migrates the whole Chat family upward -- game's registrations
    // (eq_commands.cpp's RegisterSocialCommands()) removed in the same
    // change. shout's alias list grew "sho" (game's own shout alias,
    // reconciled here instead of left duplicated) on top of the pre-existing
    // "sh" escape alias. Aliases/usage/requiresArgs below are ported
    // verbatim from game's registrations.
    registerCommand({
        "shout", {"sh", "sho"}, "/shout <message>", "Shout to the zone", "Chat", true
    }, [this](const std::string& args) { return cmdShout(args); });

    // requiresArgs ported exactly as game had it: false for all of these
    // except tell (game's own tell.requiresArgs = true). CommandProcessor's
    // pre-existing shout registration (above) sets true, a pre-T100
    // deliberate deviation from game's shout (which had no requiresArgs) --
    // not touched here, left as found.
    registerCommand({
        "say", {"s"}, "/say <message>", "Say message to nearby players", "Chat", false
    }, [this](const std::string& args) { return cmdSay(args); });

    registerCommand({
        "ooc", {"o"}, "/ooc <message>", "Out of character message", "Chat", false
    }, [this](const std::string& args) { return cmdOOC(args); });

    registerCommand({
        "auction", {"auc"}, "/auction <message>", "Auction channel message", "Chat", false
    }, [this](const std::string& args) { return cmdAuction(args); });

    registerCommand({
        "gsay", {"g"}, "/gsay <message>", "Group chat message", "Chat", false
    }, [this](const std::string& args) { return cmdGsay(args); });

    registerCommand({
        "gu", {"guildsay"}, "/gu <message>", "Guild chat message", "Chat", false
    }, [this](const std::string& args) { return cmdGu(args); });

    registerCommand({
        "tell", {"t", "msg"}, "/tell <player> <message>", "Send private message to player", "Chat", true
    }, [this](const std::string& args) { return cmdTell(args); });

    registerCommand({
        "emote", {"em", "me"}, "/emote <action>", "Perform custom emote", "Chat", false
    }, [this](const std::string& args) { return cmdEmote(args); });

    registerCommand({
        "reply", {"r"}, "/reply <message>", "Reply to last tell", "Chat", false
    }, [this](const std::string& args) { return cmdReply(args); });

    // T98: loc/sit/stand/camp/move/moveto/follow/stopfollow registrations
    // removed -- shadowed dead code. face stays: no game "face" command
    // exists at all, so it was never shadowed.
    registerCommand({
        "face", {}, "/face <name|x y z>", "Face entity or location", "Movement", false
    }, [this](const std::string& args) { return cmdFace(args); });

    // T98: target/cast/interrupt registrations removed -- shadowed dead code.
    // T51: attack/stopattack/aa registrations removed from this registry.
    // T29's dispatch order (registry 1 -- EverQuest::m_command_registry --
    // tried first, see application.cpp's DispatchSlashCommand) meant these
    // three were always shadowed dead code: registry 1's own /attack (alias
    // "att"), /stopattack, /aa (alias "~") won every collision (T51 Piece
    // 0 live probe, benchmarks/audits/T51/). ActionDispatcher::
    // enableAutoAttack()/disableAutoAttack()/toggleAutoAttack() themselves
    // are untouched and still live -- the keyboard gesture
    // (InputActionBridge) reaches them directly. See
    // refactor/T51_attack_path_consolidation.md.

    registerCommand({
        "consider", {"con"}, "/consider", "Consider current target", "Combat", false
    }, [this](const std::string& args) { return cmdConsider(args); });

    registerCommand({
        "hail", {}, "/hail", "Hail target or say Hail", "Combat", false
    }, [this](const std::string& args) { return cmdHail(args); });

    registerCommand({
        "npcstate", {}, "/npcstate", "Show current vendor/banker/trainer interaction state", "Utility", false
    }, [this](const std::string& args) { return cmdNpcState(args); });

    // T99: invite/disband/decline migrated here from EverQuest::
    // m_command_registry (removed from eq_commands.cpp in the same change).
    // groupaccept is new -- see cmdGroupAccept and
    // refactor/T99_action_layer_unification_pilot.md.
    registerCommand({
        "invite", {"inv"}, "/invite [name]", "Invite player to group", "Group", false
    }, [this](const std::string& args) { return cmdInvite(args); });

    registerCommand({
        "disband", {}, "/disband", "Leave or disband group", "Group", false
    }, [this](const std::string& args) { return cmdDisband(args); });

    registerCommand({
        "decline", {}, "/decline", "Decline pending group invite", "Group", false
    }, [this](const std::string& args) { return cmdDecline(args); });

    registerCommand({
        "groupaccept", {"gaccept"}, "/groupaccept", "Accept pending group invite", "Group", false
    }, [this](const std::string& args) { return cmdGroupAccept(args); });

    // T98: who/help/quit/q/debug/timestamp registrations removed -- shadowed
    // dead code (who/help/quit/q/debug/timestamp; "q" was shadowed via
    // game's alias table, not a game primary name -- see T97's Review).

    registerCommand({
        "door", {"u"}, "/door", "Interact with nearest door", "Utility", false
    }, [this](const std::string& args) { return cmdDoor(args); });

    registerCommand({
        "afk", {}, "/afk", "Toggle AFK status", "Utility", false
    }, [this](const std::string& args) { return cmdAFK(args); });

    registerCommand({
        "anon", {}, "/anon", "Toggle anonymous status", "Utility", false
    }, [this](const std::string& args) { return cmdAnon(args); });

    // T98: skills/gems/mem/forget registrations removed -- shadowed dead
    // code. This empties the "Spells" category entirely.

    // T98: pet registration removed -- shadowed dead code. This empties the
    // "Pet" category entirely. ActionDispatcher's pet wrappers (T96-fixed)
    // are untouched -- see refactor/T98_delete_shadowed_commands.md's
    // cascade list.

    // T98: filter registration removed -- shadowed dead code.

    // Extended movement commands
    registerCommand({
        "walk", {}, "/walk", "Set movement speed to walk", "Movement", false
    }, [this](const std::string& args) { return cmdWalk(args); });

    registerCommand({
        "run", {}, "/run", "Set movement speed to run", "Movement", false
    }, [this](const std::string& args) { return cmdRun(args); });

    registerCommand({
        "sneak", {}, "/sneak", "Set movement speed to sneak", "Movement", false
    }, [this](const std::string& args) { return cmdSneak(args); });

    registerCommand({
        "crouch", {"duck"}, "/crouch", "Crouch/duck", "Movement", false
    }, [this](const std::string& args) { return cmdCrouch(args); });

    registerCommand({
        "feign", {"fd"}, "/feign", "Feign death", "Movement", false
    }, [this](const std::string& args) { return cmdFeign(args); });

    registerCommand({
        "jump", {}, "/jump", "Jump", "Movement", false
    }, [this](const std::string& args) { return cmdJump(args); });

    registerCommand({
        "turn", {}, "/turn <degrees>", "Turn to heading (0=N, 90=E, 180=S, 270=W)", "Movement", true
    }, [this](const std::string& args) { return cmdTurn(args); });

    // Entity query commands
    registerCommand({
        "list", {}, "/list [search]", "List nearby entities", "Utility", false
    }, [this](const std::string& args) { return cmdList(args); });

    registerCommand({
        "dump", {}, "/dump <name>", "Dump entity appearance/equipment info", "Utility", true
    }, [this](const std::string& args) { return cmdDump(args); });

    // Extended combat commands
    registerCommand({
        "hunt", {}, "/hunt [on|off]", "Toggle auto-hunting mode", "Combat", false
    }, [this](const std::string& args) { return cmdHunt(args); });

    registerCommand({
        "autoloot", {}, "/autoloot [on|off]", "Toggle auto-looting", "Combat", false
    }, [this](const std::string& args) { return cmdAutoLoot(args); });

    registerCommand({
        "listtargets", {}, "/listtargets", "List potential hunt targets", "Combat", false
    }, [this](const std::string& args) { return cmdListTargets(args); });

    registerCommand({
        "loot", {}, "/loot <corpseId>", "Loot a corpse by id (see /list)", "Combat", true
    }, [this](const std::string& args) { return cmdLoot(args); });

    // Pathfinding and roleplay
    registerCommand({
        "pathfinding", {}, "/pathfinding [on|off]", "Toggle pathfinding", "Utility", false
    }, [this](const std::string& args) { return cmdPathfinding(args); });

    registerCommand({
        "roleplay", {"rp"}, "/roleplay [on|off]", "Toggle roleplay status", "Utility", false
    }, [this](const std::string& args) { return cmdRoleplay(args); });

    // Emote shortcut commands
    registerCommand({
        "wave", {}, "/wave", "Wave", "Chat", false
    }, [this](const std::string& args) { return cmdWave(args); });

    registerCommand({
        "dance", {}, "/dance", "Dance", "Chat", false
    }, [this](const std::string& args) { return cmdDance(args); });

    registerCommand({
        "cheer", {}, "/cheer", "Cheer", "Chat", false
    }, [this](const std::string& args) { return cmdCheer(args); });

    registerCommand({
        "laugh", {}, "/laugh", "Laugh", "Chat", false
    }, [this](const std::string& args) { return cmdLaugh(args); });

    // Logging filter commands
    registerCommand({
        "logonly", {}, "/logonly [MOD1,MOD2,...]", "Whitelist log modules (suppress all others)", "Utility", false
    }, [this](const std::string& args) { return cmdLogOnly(args); });

    registerCommand({
        "logexclude", {}, "/logexclude [MOD1,MOD2,...]", "Blacklist log modules", "Utility", false
    }, [this](const std::string& args) { return cmdLogExclude(args); });

    registerCommand({
        "logmodule", {}, "/logmodule <MOD:LEVEL>", "Set a module's log level", "Utility", false
    }, [this](const std::string& args) { return cmdLogModule(args); });

    registerCommand({
        "logclear", {}, "/logclear", "Clear all module log filters", "Utility", false
    }, [this](const std::string& args) { return cmdLogClear(args); });
}

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
    // "No one to reply to") is NOT reproduced here: that state lives only on
    // EverQuest (m_last_tell_sender), not in state::GameState or anywhere
    // IActionHandler exposes to the dispatcher, so ActionDispatcher::
    // replyToLastTell() cannot make the same check game's registry could --
    // it already silently no-ops via EqActionHandler::replyToLastTell() when
    // there is no pending sender, returning ActionResult::Success() rather
    // than surfacing an error. This is a pre-existing action-layer gap (the
    // dispatcher/handler shape predates T100), not introduced by this
    // migration, but T100 is what gives it its first live caller. Reported,
    // not fixed -- see refactor/T100_migrate_chat_family.md's Review and
    // refactor/deferred.md.
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
    // Application::syncCombatState() (application.cpp) mirrors
    // CombatManager's target into this same state::GameState instance
    // (Application::m_gameState -- the one CommandProcessor is constructed
    // with) every frame, keeping the decoupling T12 established: this file
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

// ========== Extended Command Implementations ==========

ActionResult CommandProcessor::cmdWalk(const std::string& args) {
    auto result = m_dispatcher.setMovementMode(1);  // MOVE_MODE_WALK
    if (result.success) {
        displayMessage("Movement mode set to walk");
    }
    return result;
}

ActionResult CommandProcessor::cmdRun(const std::string& args) {
    auto result = m_dispatcher.setMovementMode(0);  // MOVE_MODE_RUN
    if (result.success) {
        displayMessage("Movement mode set to run");
    }
    return result;
}

ActionResult CommandProcessor::cmdSneak(const std::string& args) {
    auto result = m_dispatcher.setMovementMode(2);  // MOVE_MODE_SNEAK
    if (result.success) {
        displayMessage("Movement mode set to sneak");
    }
    return result;
}

ActionResult CommandProcessor::cmdCrouch(const std::string& args) {
    auto result = m_dispatcher.setPositionState(2);  // POS_CROUCHING
    if (result.success) {
        displayMessage("Character is now crouching");
    }
    return result;
}

ActionResult CommandProcessor::cmdFeign(const std::string& args) {
    auto result = m_dispatcher.setPositionState(3);  // POS_FEIGN_DEATH
    if (result.success) {
        displayMessage("Character is feigning death");
    }
    return result;
}

ActionResult CommandProcessor::cmdJump(const std::string& args) {
    auto result = m_dispatcher.jump();
    if (result.success) {
        displayMessage("Character jumps!");
    }
    return result;
}

ActionResult CommandProcessor::cmdTurn(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /turn <degrees> (0=North, 90=East, 180=South, 270=West)");
    }

    try {
        float degrees = std::stof(args);
        auto result = m_dispatcher.setHeading(degrees);
        if (result.success) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1);
            oss << "Turned to heading " << degrees << " degrees";
            displayMessage(oss.str());
        }
        return result;
    } catch (const std::exception&) {
        return ActionResult::Failure("Invalid heading value");
    }
}

ActionResult CommandProcessor::cmdList(const std::string& args) {
    return m_dispatcher.listEntities(args);
}

ActionResult CommandProcessor::cmdDump(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /dump <entity_name>");
    }
    return m_dispatcher.dumpEntityAppearance(args);
}

ActionResult CommandProcessor::cmdHunt(const std::string& args) {
    if (args.empty()) {
        // Toggle - we don't track state here, so just default to enabling
        // The action handler will handle the actual toggle
        return m_dispatcher.setAutoHunting(true);
    }

    std::string lower = toLower(args);
    bool enable = (lower == "on" || lower == "true" || lower == "1");
    auto result = m_dispatcher.setAutoHunting(enable);
    if (result.success) {
        displayMessage(enable ? "Auto-hunting mode enabled" : "Auto-hunting mode disabled");
    }
    return result;
}

ActionResult CommandProcessor::cmdAutoLoot(const std::string& args) {
    if (args.empty()) {
        displayMessage("Usage: /autoloot <on|off>");
        return ActionResult::Success();
    }

    std::string lower = toLower(args);
    bool enable = (lower == "on" || lower == "true" || lower == "1");
    auto result = m_dispatcher.setAutoLoot(enable);
    if (result.success) {
        displayMessage(enable ? "Auto-loot enabled" : "Auto-loot disabled");
    }
    return result;
}

ActionResult CommandProcessor::cmdListTargets(const std::string& args) {
    return m_dispatcher.listHuntTargets();
}

ActionResult CommandProcessor::cmdLoot(const std::string& args) {
    // T34: dispatches to m_dispatcher.lootCorpse(corpseId) -- the exact
    // same entry point the renderer's shift+click overlay callback uses
    // (InputActionBridge::processLootRequests -> ActionDispatcher::lootCorpse),
    // so a scripted /loot <id> and a live shift+click enter CombatManager::
    // LootCorpse() through identical last hops. An explicit corpse id is
    // the required form deliberately -- see refactor/T34_shiftclick_loot_verification.md;
    // "nearest corpse" detection is out of scope for this unit.
    if (args.empty()) {
        return ActionResult::Failure("Usage: /loot <corpseId> - use '/list' to find corpse IDs");
    }

    try {
        size_t consumed = 0;
        long corpseId = std::stol(args, &consumed);
        if (corpseId < 0 || corpseId > 0xFFFF || consumed != args.size()) {
            return ActionResult::Failure("Invalid corpse id - use '/list' to find corpse IDs");
        }
        return m_dispatcher.lootCorpse(static_cast<uint16_t>(corpseId));
    } catch (const std::exception&) {
        return ActionResult::Failure("Invalid corpse id - use '/list' to find corpse IDs");
    }
}

ActionResult CommandProcessor::cmdPathfinding(const std::string& args) {
    if (args.empty()) {
        displayMessage("Usage: /pathfinding <on|off>");
        return ActionResult::Success();
    }

    std::string lower = toLower(args);
    bool enable = (lower == "on" || lower == "true" || lower == "1");
    return m_dispatcher.setPathfinding(enable);
}

ActionResult CommandProcessor::cmdRoleplay(const std::string& args) {
    if (args.empty()) {
        // Toggle - default to enabling since we don't track state here
        return m_dispatcher.setRoleplay(true);
    }

    std::string lower = toLower(args);
    bool enable = (lower == "on" || lower == "true" || lower == "1");
    return m_dispatcher.setRoleplay(enable);
}

ActionResult CommandProcessor::cmdWave(const std::string& args) {
    auto result = m_dispatcher.sendAnimation(29);  // ANIM_WAVE
    if (result.success) {
        displayMessage("You wave");
    }
    return result;
}

ActionResult CommandProcessor::cmdDance(const std::string& args) {
    auto result = m_dispatcher.sendAnimation(58);  // ANIM_DANCE
    if (result.success) {
        displayMessage("You dance");
    }
    return result;
}

ActionResult CommandProcessor::cmdCheer(const std::string& args) {
    auto result = m_dispatcher.sendAnimation(27);  // ANIM_CHEER
    if (result.success) {
        displayMessage("You cheer");
    }
    return result;
}

ActionResult CommandProcessor::cmdLaugh(const std::string& args) {
    auto result = m_dispatcher.sendAnimation(63);  // ANIM_LAUGH
    if (result.success) {
        displayMessage("You laugh");
    }
    return result;
}

ActionResult CommandProcessor::cmdLogOnly(const std::string& args) {
    if (args.empty()) {
        displayMessage(GetModuleFilterStatus());
        displayMessage("Modules: " + GetAllModuleNames());
        displayMessage("Usage: /logonly MOD1,MOD2,...");
        return ActionResult::Success();
    }
    ApplyLogOnly(args.c_str());
    displayMessage("Log whitelist applied: " + args);
    displayMessage(GetModuleFilterStatus());
    return ActionResult::Success();
}

ActionResult CommandProcessor::cmdLogExclude(const std::string& args) {
    if (args.empty()) {
        displayMessage(GetModuleFilterStatus());
        displayMessage("Modules: " + GetAllModuleNames());
        displayMessage("Usage: /logexclude MOD1,MOD2,...");
        return ActionResult::Success();
    }
    ApplyLogExclude(args.c_str());
    displayMessage("Log blacklist applied: " + args);
    displayMessage(GetModuleFilterStatus());
    return ActionResult::Success();
}

ActionResult CommandProcessor::cmdLogModule(const std::string& args) {
    if (args.empty()) {
        displayMessage(GetModuleFilterStatus());
        displayMessage("Usage: /logmodule MOD:LEVEL (e.g., /logmodule NET:TRACE)");
        return ActionResult::Success();
    }

    // Parse MOD:LEVEL
    size_t colonPos = args.find(':');
    if (colonPos == std::string::npos) {
        return ActionResult::Failure("Usage: /logmodule MOD:LEVEL (e.g., /logmodule NET:TRACE)");
    }

    std::string modStr = args.substr(0, colonPos);
    std::string levelStr = args.substr(colonPos + 1);

    // Convert to uppercase
    std::transform(modStr.begin(), modStr.end(), modStr.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    std::transform(levelStr.begin(), levelStr.end(), levelStr.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    LogModule mod = ParseModuleName(modStr.c_str());
    LogLevel level = ParseLevelName(levelStr.c_str());
    SetModuleLogLevel(mod, level);

    displayMessage("Module " + modStr + " set to " + GetLevelName(level));
    return ActionResult::Success();
}

ActionResult CommandProcessor::cmdLogClear(const std::string& args) {
    ClearModuleFilters();
    displayMessage("All module filters cleared (using global level)");
    displayMessage(GetModuleFilterStatus());
    return ActionResult::Success();
}

} // namespace action
} // namespace eqt
