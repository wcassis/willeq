#include "client/action/command_processor.h"
#include "client/state/game_state.h"
#include "client/output/output_renderer.h"
#include "client/skill/skill_constants.h"  // T156: /useskill id/name resolution
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

    // T153: the appearance change is /appearance, NOT /face -- /face above is
    // the *movement* face (turn to face an entity or a location) and has been
    // since T98. Registered here in the action registry only, never the legacy
    // game registry (T97).
    registerCommand({
        "appearance", {}, "/appearance <field> <0-255> [<field> <0-255> ...]",
        "Change face/hair/beard appearance", "Character", false
    }, [this](const std::string& args) { return cmdAppearance(args); });

    // T171: /inspectmessage -- the text other players see when they inspect
    // us. Local state, no packet; it reaches the server the next time we
    // answer an inspect. See refactor/T171_inspect_message.md.
    registerCommand({
        "inspectmessage", {"inspectmsg"}, "/inspectmessage [text|-clear]",
        "Show or set the message shown when others inspect you", "Social", false
    }, [this](const std::string& args) { return cmdInspectMessage(args); });

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

    // T179-T184: /raid subcommand family.
    registerCommand({
        "raid", {}, "/raid [status|list|invite|accept|leave|remove|leader|loot|looter|lock|unlock|group]",
        "Raid status and commands", "Group", false
    }, [this](const std::string& args) { return cmdRaid(args); });

    // T98: who/help/quit/q/debug/timestamp registrations removed -- shadowed
    // dead code (who/help/quit/q/debug/timestamp; "q" was shadowed via
    // game's alias table, not a game primary name -- see T97's Review).
    // T102 re-adds `who` below (this time deliberately: the Social section's
    // real server request, not Movement's local-scan substitute).

    // T102: Combat (12), migrated from EverQuest::m_command_registry's
    // Combat category -- eq_commands_movement.cpp's target/attack/
    // stopattack/aa/combat/taunt/autofire and eq_commands.cpp's assist/
    // shield/duel/duelaccept/dueldecline, registrations removed from both
    // in the same change. See refactor/T102_migrate_combat_social.md.
    registerCommand({
        "target", {"tar"}, "/target <name>", "Target entity by name", "Combat", false
    }, [this](const std::string& args) { return cmdTarget(args); });

    registerCommand({
        "attack", {"att"}, "/attack", "Begin auto-attack", "Combat", false
    }, [this](const std::string& args) { return cmdAttack(args); });

    registerCommand({
        "stopattack", {}, "/stopattack", "Stop auto-attack", "Combat", false
    }, [this](const std::string& args) { return cmdStopAttack(args); });

    registerCommand({
        "aa", {"~"}, "/aa", "Toggle auto-attack on/off", "Combat", false
    }, [this](const std::string& args) { return cmdAA(args); });

    registerCommand({
        "combat", {}, "/combat [on|off]", "Enable/disable combat manager (auto-attack, hunting, etc.)", "Combat", false
    }, [this](const std::string& args) { return cmdCombat(args); });

    registerCommand({
        "taunt", {}, "/taunt", "Taunt current target (warrior/paladin/shadowknight ability)", "Combat", false
    }, [this](const std::string& args) { return cmdTaunt(args); });

    // T156: /useskill is the only scriptable path to SkillManager::activateSkill
    // — a hotbar click cannot be driven by --script, which is what left T152's
    // five skill senders unprobeable. See refactor/T156_skill_reachability.md.
    registerCommand({
        "useskill", {"skill"}, "/useskill <id|name>",
        "Use an activatable skill on your current target (e.g. /useskill taunt, /useskill 9)",
        "Combat", false
    }, [this](const std::string& args) { return cmdUseSkill(args); });

    registerCommand({
        "autofire", {"af"}, "/autofire", "Toggle ranged auto-attack (archery/throwing)", "Combat", false
    }, [this](const std::string& args) { return cmdAutofire(args); });

    registerCommand({
        "assist", {"a"}, "/assist [target]", "Assist your target or specified player", "Combat", false
    }, [this](const std::string& args) { return cmdAssist(args); });

    registerCommand({
        "shield", {}, "/shield", "Shield your current target (Warrior/Paladin)", "Combat", false
    }, [this](const std::string& args) { return cmdShield(args); });

    registerCommand({
        "duel", {}, "/duel", "Challenge your target to a duel", "Combat", false
    }, [this](const std::string& args) { return cmdDuel(args); });

    registerCommand({
        "duelaccept", {}, "/duelaccept", "Accept a duel challenge", "Combat", false
    }, [this](const std::string& args) { return cmdDuelAccept(args); });

    registerCommand({
        "dueldecline", {}, "/dueldecline", "Decline a duel challenge", "Combat", false
    }, [this](const std::string& args) { return cmdDuelDecline(args); });

    // T102: Social (10 distinct names / 11 game-registry registrations --
    // `who` is duplicated, see cmdWho below), migrated from
    // eq_commands.cpp's RegisterSocialCommands() (trade/accept/
    // tradedecline/canceltrade/who_cmd/inspect/lfg/surname/random/yell) and
    // eq_commands_movement.cpp's second `who` (deleted, not migrated -- see
    // cmdWho).
    registerCommand({
        "trade", {}, "/trade [name]", "Initiate trade with target or named player", "Social", false
    }, [this](const std::string& args) { return cmdTrade(args); });

    registerCommand({
        "accept", {}, "/accept", "Accept pending trade request", "Social", false
    }, [this](const std::string& args) { return cmdTradeAccept(args); });

    registerCommand({
        "tradedecline", {"rejecttrade"}, "/tradedecline", "Decline pending trade request", "Social", false
    }, [this](const std::string& args) { return cmdTradeDecline(args); });

    registerCommand({
        "canceltrade", {"stoptrade"}, "/canceltrade", "Cancel active trade", "Social", false
    }, [this](const std::string& args) { return cmdCancelTrade(args); });

    registerCommand({
        "who", {"whoall"}, "/who [name] [level range]", "Search for players online", "Social", false
    }, [this](const std::string& args) { return cmdWho(args); });

    registerCommand({
        "inspect", {}, "/inspect [name]", "Inspect target or named player", "Social", false
    }, [this](const std::string& args) { return cmdInspect(args); });

    registerCommand({
        "lfg", {}, "/lfg", "Toggle Looking For Group status", "Social", false
    }, [this](const std::string& args) { return cmdLFG(args); });

    registerCommand({
        "surname", {}, "/surname <lastname>", "Request to set your surname", "Social", false
    }, [this](const std::string& args) { return cmdSurname(args); });

    registerCommand({
        "random", {"roll"}, "/random [low] [high]", "Roll a random number", "Social", false
    }, [this](const std::string& args) { return cmdRandom(args); });

    registerCommand({
        "yell", {}, "/yell", "Yell for help", "Social", false
    }, [this](const std::string& args) { return cmdYell(args); });

    // T132: Guild (7), migrated from eq_commands.cpp's Guild block --
    // action::CommandProcessor had zero guild registrations before this
    // unit (net-new plumbing over the existing EverQuest::SendGuild*()
    // senders, not a reconcile-and-delete -- see
    // refactor/T132_migrate_guild_family.md). Registrations removed from
    // eq_commands.cpp's Guild block in the same change.
    registerCommand({
        "guildinvite", {"ginvite"}, "/guildinvite <name>", "Invite player to guild", "Guild", false
    }, [this](const std::string& args) { return cmdGuildInvite(args); });

    registerCommand({
        "guildaccept", {"acceptguild"}, "/guildaccept", "Accept pending guild invite", "Guild", false
    }, [this](const std::string& args) { return cmdGuildAccept(args); });

    registerCommand({
        "guilddecline", {"declineguild"}, "/guilddecline", "Decline pending guild invite", "Guild", false
    }, [this](const std::string& args) { return cmdGuildDecline(args); });

    registerCommand({
        "guildremove", {"gremove", "gkick"}, "/guildremove <name>", "Remove member from guild", "Guild", false
    }, [this](const std::string& args) { return cmdGuildRemove(args); });

    registerCommand({
        "guilddemote", {"gdemote"}, "/guilddemote <name>", "Demote guild member", "Guild", false
    }, [this](const std::string& args) { return cmdGuildDemote(args); });

    registerCommand({
        "guildleader", {"gleader"}, "/guildleader <name>", "Transfer guild leadership", "Guild", false
    }, [this](const std::string& args) { return cmdGuildLeader(args); });

    registerCommand({
        "guildmotd", {"gmotd"}, "/guildmotd [message]", "View or set guild MOTD", "Guild", false
    }, [this](const std::string& args) { return cmdGuildMotd(args); });

    // T134: Corpse (4) + Resurrection (2). Aliases ported verbatim from
    // eq_commands.cpp's Corpse/Resurrection blocks.
    registerCommand({
        "corpsedrag", {"drag"}, "/corpsedrag <corpse_name>", "Start dragging a corpse", "Corpse", false
    }, [this](const std::string& args) { return cmdCorpseDrag(args); });

    registerCommand({
        "corpsedrop", {"drop"}, "/corpsedrop", "Drop the corpse you are dragging", "Corpse", false
    }, [this](const std::string& args) { return cmdCorpseDrop(args); });

    registerCommand({
        "consent", {}, "/consent <player_name>", "Grant permission to drag your corpse", "Corpse", false
    }, [this](const std::string& args) { return cmdConsent(args); });

    registerCommand({
        "deny", {}, "/deny <player_name>", "Revoke permission to drag your corpse", "Corpse", false
    }, [this](const std::string& args) { return cmdConsentDeny(args); });

    registerCommand({
        "rezaccept", {"acceptrez"}, "/rezaccept", "Accept pending resurrection offer", "Utility", false
    }, [this](const std::string& args) { return cmdRezAccept(args); });

    registerCommand({
        "rezdecline", {"declinerez"}, "/rezdecline", "Decline pending resurrection offer", "Utility", false
    }, [this](const std::string& args) { return cmdRezDecline(args); });

    // T144
    registerCommand({
        "clickobject", {"clickobj"}, "/clickobject <drop_id>",
        "Click a world object (forge, oven, ground spawn) by its drop id", "Utility", false
    }, [this](const std::string& args) { return cmdClickObject(args); });

    // T149: tradeskill recipes. object_type is never an argument -- see
    // EqActionHandler::resolveOpenContainerObjectType().
    registerCommand({
        "recipesearch", {"recipes"}, "/recipesearch [--min N] [--max N] [name]",
        "Search the open tradeskill container's recipes", "Tradeskill", false
    }, [this](const std::string& args) { return cmdRecipeSearch(args); });

    registerCommand({
        "recipedetails", {"recipeinfo"}, "/recipedetails <recipe_id>",
        "Show the components of a recipe from a search result", "Tradeskill", false
    }, [this](const std::string& args) { return cmdRecipeDetails(args); });

    registerCommand({
        "recipecombine", {"autocombine"}, "/recipecombine <recipe_id>",
        "Auto-combine a recipe in the open tradeskill container", "Tradeskill", false
    }, [this](const std::string& args) { return cmdRecipeAutoCombine(args); });

    registerCommand({
        "recipefavorites", {"recipefav"}, "/recipefavorites <recipe_id> [recipe_id ...]",
        "Request details for favorite recipes in the open container", "Tradeskill", false
    }, [this](const std::string& args) { return cmdRecipesFavorite(args); });

    // T135: Utility (6) -- categories ported verbatim from eq_commands.cpp's
    // per-command category, not unified, since the plan explicitly treats
    // these as six independent migrations that happen to share a unit.
    registerCommand({
        "split", {}, "/split <platinum> [gold] [silver] [copper]", "Split money with your group", "Group", false
    }, [this](const std::string& args) { return cmdSplit(args); });

    registerCommand({
        "save", {}, "/save", "Request character save", "Utility", false
    }, [this](const std::string& args) { return cmdSave(args); });

    registerCommand({
        "bandage", {"bindwound"}, "/bandage", "Use bind wound skill on target", "Skills", false
    }, [this](const std::string& args) { return cmdBandage(args); });

    registerCommand({
        "rewind", {}, "/rewind", "Attempt to return to a safe position", "Movement", false
    }, [this](const std::string& args) { return cmdRewind(args); });

    registerCommand({
        "bug", {"report"}, "/bug <description>", "Submit a bug report", "Utility", false
    }, [this](const std::string& args) { return cmdBug(args); });

    registerCommand({
        "petition", {}, "/petition <text>", "Submit a petition", "Utility", false
    }, [this](const std::string& args) { return cmdPetition(args); });

    // T136: Merchant (vendor/vendorlist/buy/sell) -- category ported
    // verbatim from eq_commands.cpp ("Commerce"), matching this unit's
    // question-1 decision to keep these four together for discoverability
    // rather than splitting vendorlist into a different category. equip
    // stays out of this family (see refactor/T136_migrate_merchant_family.md).
    registerCommand({
        "vendor", {}, "/vendor", "Open (or close) a vendor window with your current target", "Commerce", false
    }, [this](const std::string& args) { return cmdVendor(args); });

    registerCommand({
        "vendorlist", {}, "/vendorlist", "List the open vendor's inventory (slot, name, price, quantity)", "Commerce", false
    }, [this](const std::string& args) { return cmdVendorList(args); });

    registerCommand({
        "buy", {}, "/buy <slot> [quantity]", "Buy an item from the open vendor (see /vendorlist)", "Commerce", false
    }, [this](const std::string& args) { return cmdBuy(args); });

    registerCommand({
        "sell", {}, "/sell <slot> [quantity]", "Sell an item from your inventory to the open vendor", "Commerce", false
    }, [this](const std::string& args) { return cmdSell(args); });

    // T141: equip -- the one command T136 explicitly excluded from the
    // Merchant family (different mechanism, different category) and the
    // last row of the whole game-registry migration programme. Category
    // ported verbatim from eq_commands.cpp ("Inventory", not "Commerce").
    // See refactor/T141_equip_extraction.md.
    registerCommand({
        "equip", {}, "/equip <fromSlot> <toSlot> | /equip <item name> | /equip list",
        "Move/equip an item by slot numbers or by name (e.g. /equip 22 13, /equip Rusty Mace, /equip list)",
        "Inventory", false
    }, [this](const std::string& args) { return cmdEquip(args); });

    // T140: final 16 of the game-registry migration programme (equip
    // excluded). Registrations removed from eq_commands_movement.cpp/
    // eq_commands_spells.cpp in the same change -- see
    // refactor/T140_migrate_final_seventeen.md.
    registerCommand({
        "loc", {"location"}, "/loc", "Show current coordinates", "Movement", false
    }, [this](const std::string& args) { return cmdLoc(args); });

    registerCommand({
        "sit", {}, "/sit", "Sit down", "Movement", false
    }, [this](const std::string& args) { return cmdSit(args); });

    registerCommand({
        "stand", {}, "/stand", "Stand up", "Movement", false
    }, [this](const std::string& args) { return cmdStand(args); });

    registerCommand({
        "move", {}, "/move <x> <y> <z> (CLIENT frame -- swap x/y if reading a server-frame /loc)",
        "Move to coordinates (client frame; see usage)", "Movement", true
    }, [this](const std::string& args) { return cmdMove(args); });

    registerCommand({
        "moveto", {}, "/moveto <entity_name>", "Move to named entity", "Movement", true
    }, [this](const std::string& args) { return cmdMoveTo(args); });

    registerCommand({
        "warp", {}, "/warp [x y z | entity_name]",
        "Instantly teleport to coordinates, entity, or current target", "Movement", false
    }, [this](const std::string& args) { return cmdWarp(args); });

    registerCommand({
        "zoning", {}, "/zoning [on|off]",
        "Show or set whether crossing a zone line triggers an actual zone change", "Movement", false
    }, [this](const std::string& args) { return cmdZoning(args); });

    registerCommand({
        "follow", {}, "/follow <entity_name>", "Follow an entity", "Movement", true
    }, [this](const std::string& args) { return cmdFollow(args); });

    registerCommand({
        "stopfollow", {"stop"}, "/stopfollow", "Stop following", "Movement", false
    }, [this](const std::string& args) { return cmdStopFollow(args); });

    registerCommand({
        "quit", {"exit", "q"}, "/quit", "Exit the game immediately (aliases: /exit, /q)", "Utility", false
    }, [this](const std::string& args) { return cmdQuit(args); });

    registerCommand({
        "camp", {}, "/camp", "Sit down and camp out (logout)", "Utility", false
    }, [this](const std::string& args) { return cmdCamp(args); });

    registerCommand({
        "debug", {}, "/debug <0-6>", "Set debug level", "Utility", false
    }, [this](const std::string& args) { return cmdDebug(args); });

    registerCommand({
        "perf", {"performance", "metrics"}, "/perf", "Show performance metrics report", "Utility", false
    }, [this](const std::string& args) { return cmdPerf(args); });

    registerCommand({
        "time", {"settime"}, "/time [hour]", "Show or set game time (0-23)", "Utility", false
    }, [this](const std::string& args) { return cmdTime(args); });

    registerCommand({
        "bank", {}, "/bank", "Open (or close) a bank window with your current target", "Utility", false
    }, [this](const std::string& args) { return cmdBank(args); });

    registerCommand({
        "train", {}, "/train", "Open trainer window with current target", "Skills", false
    }, [this](const std::string& args) { return cmdTrain(args); });

    registerCommand({
        "door", {"u"}, "/door", "Interact with nearest door", "Utility", false
    }, [this](const std::string& args) { return cmdDoor(args); });

    registerCommand({
        "afk", {}, "/afk", "Toggle AFK status", "Utility", false
    }, [this](const std::string& args) { return cmdAFK(args); });

    registerCommand({
        "anon", {}, "/anon", "Toggle anonymous status", "Utility", false
    }, [this](const std::string& args) { return cmdAnon(args); });

    // T101: gems/mem/forget/cast/interrupt/spellbook migrated here from
    // EverQuest::m_command_registry's Spells category (removed from
    // eq_commands_spells.cpp in the same change -- see
    // refactor/T101_migrate_spells_pet_family.md). cmdSkills stays NOT
    // migrated: game's /skills is registered category "Utility", outside
    // this unit's confirmed "Spells category (6)" membership.
    registerCommand({
        "cast", {}, "/cast <gem#>", "Cast spell from gem slot (1-8)", "Spells", true
    }, [this](const std::string& args) { return cmdCast(args); });

    registerCommand({
        "mem", {"memspell", "memorize"}, "/mem <gem#> <spell_name>", "Memorize spell to gem slot", "Spells", true
    }, [this](const std::string& args) { return cmdMem(args); });

    registerCommand({
        "forget", {}, "/forget <gem#>", "Forget spell from gem slot", "Spells", true
    }, [this](const std::string& args) { return cmdForget(args); });

    registerCommand({
        "gems", {}, "/gems", "Show memorized spells", "Spells", false
    }, [this](const std::string& args) { return cmdGems(args); });

    registerCommand({
        "interrupt", {"ducking"}, "/interrupt", "Interrupt current spell cast", "Spells", false
    }, [this](const std::string& args) { return cmdInterrupt(args); });

    registerCommand({
        "spellbook", {"book"}, "/spellbook", "Open spellbook window", "Spells", false
    }, [this](const std::string& args) { return cmdSpellbook(args); });

    // T101: pet migrated here from EverQuest::m_command_registry's Pet
    // category (removed from eq_commands_spells.cpp in the same change).
    // game's vocabulary (attack/back/follow/guard/sit/stand/taunt/notaunt/
    // hold/nohold/focus/nofocus/health/dismiss/leader/spellhold/
    // nospellhold) is ported in full -- a strict superset of what the
    // pre-T101 ActionDispatcher wrapper set covered (T97).
    registerCommand({
        "pet", {}, "/pet <command> [target]",
        "Issue commands to your pet (attack, back, follow, guard, sit, taunt, hold, focus, health, dismiss)",
        "Pet", false
    }, [this](const std::string& args) { return cmdPet(args); });

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

    // ===== T150: Social / Find family =====
    // Four net-new registrations over four senders that had zero production
    // callers (refactor/T150_social_find_family.md). APPENDED, per the
    // parallel-unit boundary in that plan -- nothing above is reordered.
    //
    // Name-collision check run against BOTH registries and the renderer's
    // literal-string fallback before choosing these names:
    //   - "assist" (alias "a") is ALREADY TAKEN here, by cmdAssist -> a
    //     DIFFERENT opcode (HC_OP_Assist, single-target assist). This
    //     command is the GROUP assist (HC_OP_AssistGroup), so it is
    //     registered as "groupassist"/"ga" and does not touch /assist.
    //     (T146 lost a leg by conflating two same-named-but-different
    //     commands; this is the same trap, caught this time.)
    //   - "track", "find", "findperson", "friendswho", "groupassist",
    //     "ga", "friends" are all free: absent from CommandProcessor's
    //     table, from EverQuest::m_command_registry (whose only survivors
    //     are eq_commands_spells.cpp's hotkeys/pmem/music/sound/volume/
    //     musicvolume/effectsvolume), and from
    //     IrrlichtRenderer::processSlashCommand()'s literal arms.
    //   - "who"/"whoall" (cmdWho) is deliberately NOT reused: /friendswho
    //     is a different opcode with a different argument shape, even
    //     though its REPLY is the same OP_WhoAllResponse packet.
    registerCommand({
        "groupassist", {"ga"}, "/groupassist [name]",
        "Group assist: acquire your target's (or the named player's) target", "Group", false
    }, [this](const std::string& args) { return cmdGroupAssist(args); });

    registerCommand({
        "track", {}, "/track [name]", "Set your tracking target (Ranger/Druid/Bard)", "Skills", false
    }, [this](const std::string& args) { return cmdTrack(args); });

    registerCommand({
        "find", {"findperson"}, "/find [name]",
        "Ask the server to path you to your target or a named entity", "Social", false
    }, [this](const std::string& args) { return cmdFind(args); });

    registerCommand({
        "friendswho", {"friends"}, "/friendswho <name>[,<name>...]",
        "Ask who from a comma-delimited name list is online", "Social", true
    }, [this](const std::string& args) { return cmdFriendsWho(args); });

    // T148: GM (9), APPENDED (T149/T150 append their own blocks below this
    // one; nothing above was reordered or reflowed).
    //
    // Net-new, Guild's shape (T132): all nine EverQuest::SendGM*() senders
    // (src/client/eq_gm.cpp) had ZERO production callers and were registered
    // in NEITHER registry before this unit -- so atomicity, the concern every
    // migration unit since T97 has had to address, simply does not arise
    // here: there is no legacy `game` registration to remove in the same
    // change, and no shadowing risk, because there was never a second copy.
    // Registered in action::CommandProcessor ONLY, never in the legacy
    // registry (T97's shadowing bug).
    //
    // Registration is UNCONDITIONAL. The GM-mode gate lives one layer down,
    // in ActionDispatcher::checkGMMode(), precisely so a non-GM who types
    // /kick gets a refusal that says the command exists and why it is
    // unavailable -- not "Unknown command", which would be a lie.
    registerCommand({
        "gmzone", {}, "/gmzone <name> <zone_id>", "GM: send a character to a zone", "GM", true
    }, [this](const std::string& args) { return cmdGMZone(args); });

    registerCommand({
        "summon", {}, "/summon <name>", "GM: summon a character to you", "GM", true
    }, [this](const std::string& args) { return cmdGMSummon(args); });

    registerCommand({
        "goto", {}, "/goto <name>", "GM: teleport yourself to a character", "GM", true
    }, [this](const std::string& args) { return cmdGMGoto(args); });

    registerCommand({
        "gmfind", {}, "/gmfind <name>", "GM: locate a character", "GM", true
    }, [this](const std::string& args) { return cmdGMFind(args); });

    registerCommand({
        "kick", {}, "/kick <name>", "GM: disconnect a character", "GM", true
    }, [this](const std::string& args) { return cmdGMKick(args); });

    registerCommand({
        "kill", {}, "/kill <name>", "GM: kill a character", "GM", true
    }, [this](const std::string& args) { return cmdGMKill(args); });

    registerCommand({
        "hideme", {}, "/hideme [on|off]", "GM: hide yourself from other players", "GM", false
    }, [this](const std::string& args) { return cmdGMHideMe(args); });

    registerCommand({
        "emotezone", {}, "/emotezone <message>", "GM: broadcast an emote to the zone", "GM", true
    }, [this](const std::string& args) { return cmdGMEmoteZone(args); });

    registerCommand({
        "gmlastname", {}, "/gmlastname <name> <lastname>", "GM: set a character's last name", "GM", true
    }, [this](const std::string& args) { return cmdGMLastName(args); });
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

// T179: /raid <subcommand>. A dispatcher family, not a single sender -- accept
// answers the pending invite T172 could only watch arrive. Subcommands route
// to ActionDispatcher::raid*, which reach EverQuest::SendRaidCommand (invite/
// accept/leave) or read raid state (status). Bare /raid or /raid status prints
// the current state. NOTE: raid membership lives on EverQuest (from the decoded
// OP_RaidUpdate stream), NOT in the m_state projection, so these route through
// the handler rather than reading m_state like the group commands above.
ActionResult CommandProcessor::cmdRaid(const std::string& args) {
    // First token = subcommand, remainder = argument. Parsed inline rather than
    // via splitWhitespace() (an anonymous-namespace helper defined later in this
    // TU, not visible here).
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
// IS reliably live here: m_state.combat() is kept current by
// Application::syncCombatState() (application.cpp), a per-frame
// CombatManager poll that predates GameStateSyncBridge and is unrelated to
// it -- the same read path cmdHail (T39) already uses. Checked by reading
// syncCombatState() in full before writing any of the code below.

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
    const auto* entity = m_state.entities().findEntityByName(args);
    if (!entity) {
        return ActionResult::Failure("No target found matching '" + args + "'");
    }
    auto result = m_dispatcher.targetEntity(entity->spawnId);
    if (result.success) {
        displayMessage("Targeting: " + entity->name);
    }
    return result;
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
    // EntityManager, matching FindEntityByName's game-side substring
    // search), else falls back to the current combat target.
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

// ========== Corpse / Resurrection Commands (T134) ==========

ActionResult CommandProcessor::cmdCorpseDrag(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /corpsedrag <corpse_name>");
    }
    return m_dispatcher.corpseDrag(args);
}

ActionResult CommandProcessor::cmdCorpseDrop(const std::string& args) {
    return m_dispatcher.corpseDrop();
}

ActionResult CommandProcessor::cmdConsent(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /consent <player_name>");
    }
    return m_dispatcher.consent(args);
}

ActionResult CommandProcessor::cmdClickObject(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /clickobject <drop_id>");
    }
    uint32_t dropId = 0;
    size_t consumed = 0;
    try {
        unsigned long parsed = std::stoul(args, &consumed);
        if (parsed > std::numeric_limits<uint32_t>::max()) {
            return ActionResult::Failure("Usage: /clickobject <drop_id>");
        }
        dropId = static_cast<uint32_t>(parsed);
    } catch (const std::exception&) {
        return ActionResult::Failure("Usage: /clickobject <drop_id>");
    }
    // Reject trailing junk ("12abc"), but allow trailing whitespace.
    if (args.find_first_not_of(" \t", consumed) != std::string::npos) {
        return ActionResult::Failure("Usage: /clickobject <drop_id>");
    }
    return m_dispatcher.clickWorldObject(dropId);
}

// T149 -----------------------------------------------------------------
//
// Deliberately absent from all four: an object_type argument. The container
// type is resolved from the open container inside EqActionHandler. What the
// user types here is only what the user can actually know -- a recipe name
// to search for, or a recipe id that a prior search reply printed.

namespace {

// Parse a decimal uint32 with no trailing junk. Returns false on any
// malformed input (empty, negative, non-numeric, overflow, trailing chars).
bool parseU32Strict(const std::string& text, uint32_t& out) {
    if (text.empty() || text[0] == '-') {
        return false;
    }
    size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(text, &consumed);
    } catch (const std::exception&) {
        return false;
    }
    if (consumed != text.size()) {
        return false;
    }
    if (parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    return true;
}

std::vector<std::string> splitWhitespace(const std::string& text) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < text.size()) {
        size_t start = text.find_first_not_of(" \t", i);
        if (start == std::string::npos) break;
        size_t end = text.find_first_of(" \t", start);
        if (end == std::string::npos) end = text.size();
        parts.push_back(text.substr(start, end - start));
        i = end;
    }
    return parts;
}

// eqemu's search SQL filters `tr.trivial >= mintrivial AND tr.trivial <=
// maxtrivial` (client_packet.cpp Handle_OP_RecipesSearch). A maxtrivial of 0
// therefore matches NOTHING -- the naive "pass 0 for both" call silently
// returns an empty result set. Titanium's own tradeskill window ships a
// trivial slider; this is the equivalent default, wide enough to cover every
// live recipe.
constexpr uint32_t kDefaultMinTrivial = 0;
constexpr uint32_t kDefaultMaxTrivial = 1000;

} // namespace

ActionResult CommandProcessor::cmdRecipeSearch(const std::string& args) {
    uint32_t minTrivial = kDefaultMinTrivial;
    uint32_t maxTrivial = kDefaultMaxTrivial;

    // Leading --min/--max flags; everything after them is the name query,
    // which may legitimately be empty (the server then omits the name clause
    // and lists everything makeable in the container).
    std::string rest = args;
    for (;;) {
        size_t start = rest.find_first_not_of(" \t");
        if (start == std::string::npos) { rest.clear(); break; }
        rest = rest.substr(start);
        if (rest.rfind("--min", 0) != 0 && rest.rfind("--max", 0) != 0) {
            break;
        }
        bool isMin = (rest.rfind("--min", 0) == 0);
        std::string tail = rest.substr(5);
        size_t vstart = tail.find_first_not_of(" \t");
        if (vstart == std::string::npos) {
            return ActionResult::Failure("Usage: /recipesearch [--min N] [--max N] [name]");
        }
        tail = tail.substr(vstart);
        size_t vend = tail.find_first_of(" \t");
        std::string value = (vend == std::string::npos) ? tail : tail.substr(0, vend);
        uint32_t parsed = 0;
        if (!parseU32Strict(value, parsed)) {
            return ActionResult::Failure("Usage: /recipesearch [--min N] [--max N] [name]");
        }
        if (isMin) minTrivial = parsed; else maxTrivial = parsed;
        rest = (vend == std::string::npos) ? std::string() : tail.substr(vend);
    }

    // Trim trailing whitespace off the query.
    size_t last = rest.find_last_not_of(" \t");
    rest = (last == std::string::npos) ? std::string() : rest.substr(0, last + 1);

    // RecipesSearch_Struct::query is char[56] and eqemu forces query[55]=0,
    // so anything longer is silently truncated on the wire. Reject instead.
    if (rest.size() > 55) {
        return ActionResult::Failure("Search text must be 55 characters or fewer");
    }

    return m_dispatcher.recipeSearch(rest, minTrivial, maxTrivial);
}

ActionResult CommandProcessor::cmdRecipeDetails(const std::string& args) {
    uint32_t recipeId = 0;
    auto parts = splitWhitespace(args);
    if (parts.size() != 1 || !parseU32Strict(parts[0], recipeId)) {
        return ActionResult::Failure("Usage: /recipedetails <recipe_id>");
    }
    return m_dispatcher.recipeDetails(recipeId);
}

ActionResult CommandProcessor::cmdRecipeAutoCombine(const std::string& args) {
    uint32_t recipeId = 0;
    auto parts = splitWhitespace(args);
    if (parts.size() != 1 || !parseU32Strict(parts[0], recipeId)) {
        return ActionResult::Failure("Usage: /recipecombine <recipe_id>");
    }
    return m_dispatcher.recipeAutoCombine(recipeId);
}

ActionResult CommandProcessor::cmdRecipesFavorite(const std::string& args) {
    auto parts = splitWhitespace(args);
    if (parts.empty()) {
        return ActionResult::Failure("Usage: /recipefavorites <recipe_id> [recipe_id ...]");
    }
    std::vector<uint32_t> ids;
    ids.reserve(parts.size());
    for (const auto& part : parts) {
        uint32_t id = 0;
        if (!parseU32Strict(part, id)) {
            return ActionResult::Failure("Usage: /recipefavorites <recipe_id> [recipe_id ...]");
        }
        ids.push_back(id);
    }
    return m_dispatcher.recipesFavorite(ids);
}

ActionResult CommandProcessor::cmdConsentDeny(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /deny <player_name>");
    }
    return m_dispatcher.consentDeny(args);
}

ActionResult CommandProcessor::cmdRezAccept(const std::string& args) {
    return m_dispatcher.rezAccept();
}

ActionResult CommandProcessor::cmdRezDecline(const std::string& args) {
    return m_dispatcher.rezDecline();
}

// ========== Utility Commands (T135) ==========

// Ported from game's split_cmd exactly: usage guard on empty args, then
// parse up to four whitespace-separated numeric tokens (platinum required,
// gold/silver/copper default to 0 if omitted). This is a formatting concern
// that belongs at this layer, matching who_cmd's lvllow/lvlhigh parsing
// above -- ActionDispatcher::split() takes four already-typed uint32_t and
// does no string parsing.
ActionResult CommandProcessor::cmdSplit(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /split <platinum> [gold] [silver] [copper]");
    }
    uint32_t p = 0, g = 0, s = 0, c = 0;
    std::istringstream iss(args);
    iss >> p >> g >> s >> c;
    return m_dispatcher.split(p, g, s, c);
}

ActionResult CommandProcessor::cmdSave(const std::string& args) {
    return m_dispatcher.save();
}

// No usage guard: game's bindwound_cmd took no arguments and always
// resolved a target itself. targetId=0 tells ActionDispatcher::bandage()
// (via EqActionHandler) to resolve current-combat-target-or-self.
ActionResult CommandProcessor::cmdBandage(const std::string& args) {
    return m_dispatcher.bandage(0);
}

ActionResult CommandProcessor::cmdRewind(const std::string& args) {
    return m_dispatcher.rewind();
}

ActionResult CommandProcessor::cmdBug(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /bug <description>");
    }
    return m_dispatcher.bug(args);
}

ActionResult CommandProcessor::cmdPetition(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /petition <text>");
    }
    return m_dispatcher.petition(args);
}

// ========== Merchant Commands (T136) ==========
// No usage guard on /vendor or /vendorlist: game's originals took no
// arguments either. The "You don't have a target"/"You are not at a
// vendor." messages are stateful guards -- they live inside
// EverQuest::RequestVendorToggle() / EqActionHandler::vendorList(), not
// duplicated here (T134's rule).

ActionResult CommandProcessor::cmdVendor(const std::string& args) {
    return m_dispatcher.vendorToggle();
}

ActionResult CommandProcessor::cmdVendorList(const std::string& args) {
    return m_dispatcher.vendorList();
}

// Slot/quantity token parsing, ported verbatim (same consumed-length +
// range-check idiom eq_commands.cpp's old ParseVendorSlot/
// ParseVendorQuantity used) from buy_cmd/sell_cmd's istringstream parsing
// -- a formatting concern, matching cmdSplit's precedent, not a guard.
// Slot range is uint16_t (0-0xFFFF, matches VendorBuyIntent/
// VendorSellIntent's own uint16_t slot field); quantity is 1-255,
// defaulting to 1 when omitted, exactly as before.
namespace {
bool ParseMerchantSlot(const std::string& tok, uint32_t& out) {
    if (tok.empty()) return false;
    try {
        size_t consumed = 0;
        long v = std::stol(tok, &consumed);
        if (v < 0 || v > 0xFFFF || consumed != tok.size()) return false;
        out = static_cast<uint32_t>(v);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ParseMerchantQuantity(const std::string& tok, uint8_t& out) {
    if (tok.empty()) return false;
    try {
        size_t consumed = 0;
        long v = std::stol(tok, &consumed);
        if (v < 1 || v > 0xFF || consumed != tok.size()) return false;
        out = static_cast<uint8_t>(v);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
}  // namespace

ActionResult CommandProcessor::cmdBuy(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /buy <slot> [quantity] - see /vendorlist");
    }
    std::istringstream iss(args);
    std::string slotTok, qtyTok;
    iss >> slotTok;
    iss >> qtyTok;  // optional; empty if not present

    uint32_t slot = 0;
    if (!ParseMerchantSlot(slotTok, slot)) {
        return ActionResult::Failure("Invalid slot - see /vendorlist");
    }
    uint8_t quantity = 1;  // default, per the plan ("optional quantity")
    if (!qtyTok.empty() && !ParseMerchantQuantity(qtyTok, quantity)) {
        return ActionResult::Failure("Invalid quantity (must be 1-255)");
    }
    return m_dispatcher.buyFromVendor(slot, quantity);
}

// T141: raw arg string passed straight through -- same idiom as cmdWarp()
// above. All three input-form parsing (slot-pair / item-name / list) and
// all legality/execution happens in EverQuest::RequestEquip() ->
// InventoryManager, reached via m_dispatcher.equip().
ActionResult CommandProcessor::cmdEquip(const std::string& args) {
    return m_dispatcher.equip(args);
}

ActionResult CommandProcessor::cmdSell(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /sell <slot> [quantity] - <slot> is your inventory slot");
    }
    std::istringstream iss(args);
    std::string slotTok, qtyTok;
    iss >> slotTok;
    iss >> qtyTok;

    uint32_t slot = 0;
    if (!ParseMerchantSlot(slotTok, slot)) {
        return ActionResult::Failure("Invalid slot");
    }
    uint8_t quantity = 1;
    if (!qtyTok.empty() && !ParseMerchantQuantity(qtyTok, quantity)) {
        return ActionResult::Failure("Invalid quantity (must be 1-255)");
    }
    return m_dispatcher.sellToVendor(slot, quantity);
}

// ========== Movement / Utility / Skills Commands (T140) ==========
//
// Final 16 of the game-registry migration programme (equip excluded --
// see refactor/T140_migrate_final_seventeen.md). Registrations removed
// from eq_commands_movement.cpp/eq_commands_spells.cpp in the same change.

ActionResult CommandProcessor::cmdLoc(const std::string& args) {
    return m_dispatcher.showLocation();
}

// sit/stand REUSE the pre-existing ActionDispatcher::sit()/stand() (no new
// plumbing), but override the dispatcher's generic empty-message Success()
// with game's original acknowledgement text.
ActionResult CommandProcessor::cmdSit(const std::string& args) {
    auto result = m_dispatcher.sit();
    if (result.success) return ActionResult::Success("You sit down.");
    return result;
}

ActionResult CommandProcessor::cmdStand(const std::string& args) {
    auto result = m_dispatcher.stand();
    if (result.success) return ActionResult::Success("You stand up.");
    return result;
}

// T88: FRAME CONTRACT -- x, y, z are CLIENT frame (see
// ActionDispatcher::moveToLocation()/EverQuest::Move()'s own comment for
// the full swap relationship to server/wire frame). Echoes BOTH frames,
// same as game's original /move -- this dual-frame echo is load-bearing
// (cost T85's live capture session once already; see
// refactor/T88_position_frame_determination.md), so it is preserved here
// rather than left to ActionDispatcher::moveToLocation()'s generic
// "Moving to location" message. requiresArgs=true on this command's
// registration handles the empty-args usage message (T134's rule: the
// usage-string guard moved up; the registration's own `usage` string is
// byte-identical to game's original, so the auto-generated "Usage: ..."
// text is unchanged).
ActionResult CommandProcessor::cmdMove(const std::string& args) {
    std::istringstream iss(args);
    float x, y, z;
    if (!(iss >> x >> y >> z)) {
        return ActionResult::Failure("Usage: /move <x> <y> <z> (CLIENT frame -- swap x/y if reading a server-frame /loc)");
    }
    auto result = m_dispatcher.moveToLocation(x, y, z);
    if (!result.success) return result;
    return ActionResult::Success(fmt::format(
        "Moving to client=({:.1f}, {:.1f}, {:.1f}) -> server=({:.1f}, {:.1f}, {:.1f})",
        x, y, z, /*server_x=*/y, /*server_y=*/x, z));
}

ActionResult CommandProcessor::cmdMoveTo(const std::string& args) {
    std::string entity = args;
    while (!entity.empty() && entity[0] == ' ') {
        entity = entity.substr(1);
    }
    auto result = m_dispatcher.moveToEntity(entity);
    if (!result.success) return result;
    return ActionResult::Success(fmt::format("Moving to entity: {}", entity));
}

// Raw arg string passed straight through -- all parsing (coords vs. entity
// name vs. no-args-use-target) happens inside EverQuest::RequestWarp(),
// which m_dispatcher.warp() routes to. See that method for why (guard
// state is private to EverQuest).
ActionResult CommandProcessor::cmdWarp(const std::string& args) {
    return m_dispatcher.warp(args);
}

ActionResult CommandProcessor::cmdZoning(const std::string& args) {
    return m_dispatcher.showOrSetZoning(args);
}

// follow REUSES the pre-existing ActionDispatcher::followEntity() (no new
// plumbing), but overrides the dispatcher's message ("Following " + name)
// with game's original wording ("Following: " + name).
ActionResult CommandProcessor::cmdFollow(const std::string& args) {
    std::string entity = args;
    while (!entity.empty() && entity[0] == ' ') {
        entity = entity.substr(1);
    }
    auto result = m_dispatcher.followEntity(entity);
    if (!result.success) return result;
    return ActionResult::Success(fmt::format("Following: {}", entity));
}

// stopfollow REUSES the pre-existing ActionDispatcher::stopFollow(), whose
// own message ("Stopped following") already matches game's original
// wording verbatim -- no override needed.
ActionResult CommandProcessor::cmdStopFollow(const std::string& args) {
    return m_dispatcher.stopFollow();
}

ActionResult CommandProcessor::cmdQuit(const std::string& args) {
    return m_dispatcher.quit();
}

ActionResult CommandProcessor::cmdCamp(const std::string& args) {
    return m_dispatcher.camp();
}

ActionResult CommandProcessor::cmdDebug(const std::string& args) {
    return m_dispatcher.debugLevel(args);
}

ActionResult CommandProcessor::cmdPerf(const std::string& args) {
    return m_dispatcher.perfReport();
}

ActionResult CommandProcessor::cmdTime(const std::string& args) {
    return m_dispatcher.timeOfDay(args);
}

ActionResult CommandProcessor::cmdBank(const std::string& args) {
    return m_dispatcher.bankToggle();
}

ActionResult CommandProcessor::cmdTrain(const std::string& args) {
    return m_dispatcher.trainWithTarget();
}

// ========== Spells Commands (T101) ==========
//
// Ported from EverQuest::m_command_registry's Spells-category handlers
// (eq_commands_spells.cpp, deleted from there in this same change -- see
// refactor/T101_migrate_spells_pet_family.md's atomicity section). T97's
// divergence table found game's cast/interrupt/mem/forget/gems all better
// than action's old (T98-deleted) copies -- cast/interrupt because game
// uses the real EQ::SpellManager path (the old action copies routed
// through an unconnected CombatManager-based subsystem); mem/forget/gems
// because the old action copies were literal stubs. The name-lookup/
// disambiguation and SpellManager-touching logic lives in
// EqActionHandler (below the action layer's own state-reading surface --
// it needs EverQuest::GetSpellManager()); this layer stays thin parsing +
// dispatch, matching every other migrated family.

ActionResult CommandProcessor::cmdCast(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /cast <gem# 1-8>");
    }
    int gem = 0;
    try {
        gem = std::stoi(args);
    } catch (const std::exception&) {
        return ActionResult::Failure("Usage: /cast <gem# 1-8>");
    }
    if (gem < 1 || gem > 8) {
        return ActionResult::Failure("Gem slot must be 1-8");
    }
    uint16_t targetId = m_state.combat().hasTarget() ? m_state.combat().targetId() : 0;
    return m_dispatcher.beginCastFromGem(static_cast<uint8_t>(gem), targetId);
}

ActionResult CommandProcessor::cmdMem(const std::string& args) {
    std::istringstream iss(args);
    int gem = 0;
    std::string spellName;
    if (!(iss >> gem)) {
        return ActionResult::Failure("Usage: /mem <gem# 1-8> <spell_name>");
    }
    std::getline(iss >> std::ws, spellName);
    if (spellName.empty()) {
        return ActionResult::Failure("Usage: /mem <gem# 1-8> <spell_name>");
    }
    if (gem < 1 || gem > 8) {
        return ActionResult::Failure("Gem slot must be 1-8");
    }
    return m_dispatcher.memorizeSpellByName(spellName, static_cast<uint8_t>(gem));
}

ActionResult CommandProcessor::cmdForget(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /forget <gem# 1-8>");
    }
    int gem = 0;
    try {
        gem = std::stoi(args);
    } catch (const std::exception&) {
        return ActionResult::Failure("Usage: /forget <gem# 1-8>");
    }
    if (gem < 1 || gem > 8) {
        return ActionResult::Failure("Gem slot must be 1-8");
    }
    return m_dispatcher.forgetSpell(static_cast<uint8_t>(gem));
}

ActionResult CommandProcessor::cmdGems(const std::string& args) {
    return m_dispatcher.listMemorizedSpells();
}

ActionResult CommandProcessor::cmdInterrupt(const std::string& args) {
    return m_dispatcher.interruptCast();
}

ActionResult CommandProcessor::cmdSpellbook(const std::string& args) {
    // Matches game's stub exactly -- no spellbook UI window exists on
    // either side of this migration; a pure message, no dispatcher call.
    displayMessage("Spellbook window not yet implemented in UI");
    return ActionResult::Success();
}

// ========== Pet Commands (T101) ==========
//
// Ported from EverQuest::m_command_registry's pet_cmd handler
// (eq_commands_spells.cpp). T97 found game's vocabulary is a strict
// superset of the pre-T101 ActionDispatcher wrapper set even after T96's
// literal fixes (leader/spellhold/nospellhold/notaunt/nohold/standup were
// all missing); this port carries the full vocabulary, using 5 new
// ActionDispatcher wrappers for the subcommands that had none.

ActionResult CommandProcessor::cmdPet(const std::string& args) {
    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You do not have a pet.");
    }

    std::istringstream iss(args);
    std::string subcommand;
    std::string targetName;
    iss >> subcommand;
    std::getline(iss >> std::ws, targetName);
    std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (subcommand == "attack") {
        uint16_t targetId = 0;
        if (!targetName.empty()) {
            const auto* entity = m_state.entities().findEntityByName(targetName);
            if (!entity) {
                return ActionResult::Failure("Could not find target: " + targetName);
            }
            targetId = entity->spawnId;
        } else if (m_state.combat().hasTarget()) {
            targetId = m_state.combat().targetId();
        } else {
            return ActionResult::Failure("You must specify a target or have one selected.");
        }
        auto result = m_dispatcher.petAttack(targetId);
        if (result.success) displayMessage("Commanding pet to attack.");
        return result;
    }
    if (subcommand == "back" || subcommand == "stop") {
        auto result = m_dispatcher.petBackOff();
        if (result.success) displayMessage("Commanding pet to back off.");
        return result;
    }
    if (subcommand == "follow") {
        auto result = m_dispatcher.petFollow();
        if (result.success) displayMessage("Commanding pet to follow.");
        return result;
    }
    if (subcommand == "guard") {
        auto result = m_dispatcher.petGuard();
        if (result.success) displayMessage("Commanding pet to guard here.");
        return result;
    }
    if (subcommand == "sit" || subcommand == "sitdown") {
        auto result = m_dispatcher.petSit();
        if (result.success) displayMessage("Commanding pet to sit.");
        return result;
    }
    if (subcommand == "stand" || subcommand == "standup") {
        auto result = m_dispatcher.petStand();
        if (result.success) displayMessage("Commanding pet to stand.");
        return result;
    }
    if (subcommand == "taunt") {
        auto result = m_dispatcher.petTaunt();
        if (result.success) displayMessage("Toggling pet taunt.");
        return result;
    }
    if (subcommand == "notaunt") {
        auto result = m_dispatcher.petTauntOff();
        if (result.success) displayMessage("Disabling pet taunt.");
        return result;
    }
    if (subcommand == "hold") {
        auto result = m_dispatcher.petHold();
        if (result.success) displayMessage("Toggling pet hold.");
        return result;
    }
    if (subcommand == "nohold" || subcommand == "unhold") {
        auto result = m_dispatcher.petHoldOff();
        if (result.success) displayMessage("Disabling pet hold.");
        return result;
    }
    if (subcommand == "focus") {
        auto result = m_dispatcher.petFocus();
        if (result.success) displayMessage("Toggling pet focus.");
        return result;
    }
    if (subcommand == "nofocus") {
        // Titanium only has toggle, no explicit off -- use toggle (matches game).
        auto result = m_dispatcher.petFocus();
        if (result.success) displayMessage("Toggling pet focus off.");
        return result;
    }
    if (subcommand == "health" || subcommand == "report") {
        auto result = m_dispatcher.petHealth();
        if (result.success) displayMessage("Requesting pet health report.");
        return result;
    }
    if (subcommand == "dismiss" || subcommand == "getlost" || subcommand == "leave") {
        return m_dispatcher.dismissPet();
    }
    if (subcommand == "leader") {
        auto result = m_dispatcher.petLeader();
        if (result.success) displayMessage("Commanding pet to become leader.");
        return result;
    }
    if (subcommand == "spellhold" || subcommand == "ghold") {
        auto result = m_dispatcher.petSpellHold();
        if (result.success) displayMessage("Toggling pet spell hold.");
        return result;
    }
    if (subcommand == "nospellhold" || subcommand == "noghold") {
        // Titanium only has toggle, no explicit off -- use toggle (matches game).
        auto result = m_dispatcher.petSpellHold();
        if (result.success) displayMessage("Toggling pet spell hold off.");
        return result;
    }
    if (subcommand.empty()) {
        displayMessage("Pet commands: attack, back, follow, guard, sit, stand, taunt, hold, focus, health, dismiss");
        return ActionResult::Success();
    }
    return ActionResult::Failure("Unknown pet command: " + subcommand + ". Use /pet for a list of commands.");
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

    // T178: every refusal echoes the offending id. The server's own failure
    // ("Error: OP_LootRequest: Corpse not found (ent = 0)") hardcodes "0" and
    // never names the id we asked for, so a decayed corpse and a bad id look
    // identical. The client cannot change that string, but it can (a) name the
    // id in its OWN validation refusals and (b) echo the id it is requesting,
    // so the server's generic error is at least attributable.
    try {
        size_t consumed = 0;
        long corpseId = std::stol(args, &consumed);
        if (corpseId < 0 || corpseId > 0xFFFF || consumed != args.size()) {
            return ActionResult::Failure(
                "Invalid corpse id '" + args + "' - use '/list' to find corpse IDs");
        }
        auto result = m_dispatcher.lootCorpse(static_cast<uint16_t>(corpseId));
        if (result.success) {
            displayMessage("Requesting loot from corpse " + std::to_string(corpseId) + "...");
        }
        return result;
    } catch (const std::exception&) {
        return ActionResult::Failure(
            "Invalid corpse id '" + args + "' - use '/list' to find corpse IDs");
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

// ========== Social / Find (T150) ==========
//
// Interface note, and the point of the unit: three of the four underlying
// senders take a numeric entity id, and a command that makes the user type a
// spawn id is unreachable in practice. All three therefore take a NAME or
// fall back to the current target, resolved through the SAME
// resolveCombatTarget() path /assist, /shield and /duel already use --
// EntityManager::findEntityByName() (case-insensitive substring) plus
// CombatManager's live target. No second resolution path was built and no
// new cross-layer state was needed.

ActionResult CommandProcessor::cmdGroupAssist(const std::string& args) {
    std::string error;
    uint16_t targetId = resolveCombatTarget(args, &error);
    if (targetId == 0) {
        return ActionResult::Failure(error.empty() ? ("Could not find '" + args + "'") : error);
    }
    return m_dispatcher.assistGroup(targetId);
}

ActionResult CommandProcessor::cmdTrack(const std::string& args) {
    std::string error;
    uint16_t targetId = resolveCombatTarget(args, &error);
    if (targetId == 0) {
        return ActionResult::Failure(error.empty() ? ("Could not find '" + args + "'") : error);
    }
    return m_dispatcher.trackTarget(targetId);
}

ActionResult CommandProcessor::cmdFind(const std::string& args) {
    std::string error;
    uint16_t targetId = resolveCombatTarget(args, &error);
    if (targetId == 0) {
        return ActionResult::Failure(error.empty() ? ("Could not find '" + args + "'") : error);
    }
    return m_dispatcher.findPerson(targetId);
}

ActionResult CommandProcessor::cmdFriendsWho(const std::string& args) {
    // No name resolution here: the server takes NAMES, not ids, for this
    // opcode, and deliberately reports on characters who may be in other
    // zones -- so an entity-list lookup would be wrong, not just redundant.
    if (args.empty()) {
        return ActionResult::Failure("Usage: /friendswho <name>[,<name>...]");
    }
    return m_dispatcher.friendsWho(args);
}

ActionResult CommandProcessor::cmdLogClear(const std::string& args) {
    ClearModuleFilters();
    displayMessage("All module filters cleared (using global level)");
    displayMessage(GetModuleFilterStatus());
    return ActionResult::Success();
}

// ========== GM Commands (T148) ==========
//
// Thin parsing + dispatch, the Guild family's shape (T132). Nothing here is
// a guard: the login-time GM-mode gate is ActionDispatcher::checkGMMode()
// and the zone-connection guard lives in the senders, exactly as they did
// for Guild. What IS owned here is argument shape -- splitting the two-token
// commands and rejecting a non-numeric zone id -- which is a formatting
// concern, the same precedent cmdSplit and cmdClickObject set.

ActionResult CommandProcessor::cmdGMZone(const std::string& args) {
    std::istringstream iss(args);
    std::string name;
    std::string zoneToken;
    if (!(iss >> name) || !(iss >> zoneToken)) {
        return ActionResult::Failure("Usage: /gmzone <name> <zone_id>");
    }

    unsigned long zoneId = 0;
    size_t consumed = 0;
    try {
        zoneId = std::stoul(zoneToken, &consumed);
    } catch (const std::exception&) {
        return ActionResult::Failure("Usage: /gmzone <name> <zone_id>");
    }
    if (consumed != zoneToken.size() || zoneId > std::numeric_limits<uint16_t>::max()) {
        return ActionResult::Failure("Usage: /gmzone <name> <zone_id>");
    }

    return m_dispatcher.gmZoneRequest(name, static_cast<uint16_t>(zoneId));
}

ActionResult CommandProcessor::cmdGMSummon(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /summon <name>");
    }
    return m_dispatcher.gmSummon(args);
}

ActionResult CommandProcessor::cmdGMGoto(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /goto <name>");
    }
    return m_dispatcher.gmGoto(args);
}

ActionResult CommandProcessor::cmdGMFind(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /gmfind <name>");
    }
    return m_dispatcher.gmFind(args);
}

ActionResult CommandProcessor::cmdGMKick(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /kick <name>");
    }
    return m_dispatcher.gmKick(args);
}

ActionResult CommandProcessor::cmdGMKill(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /kill <name>");
    }
    return m_dispatcher.gmKill(args);
}

ActionResult CommandProcessor::cmdGMHideMe(const std::string& args) {
    // Explicit on|off only -- deliberately NOT a toggle. SendGMHideMe(bool)
    // sends AT_GM via SpawnAppearance and the client keeps no hidden-state of
    // its own, so a toggle would have to guess at the current value and would
    // desync the moment the server disagreed. Say what you mean.
    std::string mode = toLower(args);
    // trim
    size_t b = mode.find_first_not_of(" \t");
    size_t e = mode.find_last_not_of(" \t");
    mode = (b == std::string::npos) ? std::string() : mode.substr(b, e - b + 1);

    if (mode == "on" || mode == "1" || mode == "true") {
        return m_dispatcher.gmHideMe(true);
    }
    if (mode == "off" || mode == "0" || mode == "false") {
        return m_dispatcher.gmHideMe(false);
    }
    return ActionResult::Failure("Usage: /hideme <on|off>");
}

ActionResult CommandProcessor::cmdGMEmoteZone(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /emotezone <message>");
    }
    return m_dispatcher.gmEmoteZone(args);
}

ActionResult CommandProcessor::cmdGMLastName(const std::string& args) {
    std::istringstream iss(args);
    std::string name;
    std::string lastName;
    if (!(iss >> name) || !(iss >> lastName)) {
        return ActionResult::Failure("Usage: /gmlastname <name> <lastname>");
    }
    return m_dispatcher.gmLastName(name, lastName);
}

// T153: /appearance <field> <value> [<field> <value> ...]
//
// Named fields, not seven positional numbers: a bare "/appearance 1 0 0 0 2 0 3"
// is unreadable and unverifiable by the person typing it. Values are parsed
// strictly and range-checked against the wire type (each field is a uint8 in
// Titanium::structs::FaceChange_Struct) rather than cast -- unvalidated
// int-to-uint8 casts are a tracked defect class (T95).
//
// Fields the user does not name keep their current value; that merge needs the
// character's live appearance and therefore happens in EqActionHandler.
// T171: /inspectmessage [text|-clear]
//
// No argument SHOWS the current message rather than clearing it. Clearing
// needs the explicit -clear, so the destructive form is never the accidental
// one -- a bare /inspectmessage after a typo would otherwise silently wipe
// text the user cannot recover from anywhere (the server never sends it back).
//
// The text is taken RAW, not split into tokens: it is prose, and internal
// whitespace is the user's. Only the leading separator space is dropped.
ActionResult CommandProcessor::cmdInspectMessage(const std::string& args) {
    const size_t first = args.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return m_dispatcher.setInspectMessage(std::nullopt);  // show
    }

    std::string text = args.substr(first);
    if (text == "-clear") {
        return m_dispatcher.setInspectMessage(std::string());
    }

    return m_dispatcher.setInspectMessage(text);
}

ActionResult CommandProcessor::cmdAppearance(const std::string& args) {
    static const char* kUsage =
        "Usage: /appearance <field> <0-255> [<field> <0-255> ...]  "
        "(fields: haircolor, beardcolor, eyecolor1, eyecolor2, hairstyle, "
        "beard, face)";

    std::vector<std::string> parts = splitWhitespace(args);
    if (parts.empty() || (parts.size() % 2) != 0) {
        return ActionResult::Failure(kUsage);
    }

    AppearanceChange change;
    for (size_t i = 0; i < parts.size(); i += 2) {
        const std::string field = toLower(parts[i]);
        uint32_t value = 0;
        if (!parseU32Strict(parts[i + 1], value)) {
            return ActionResult::Failure(
                "Appearance value must be a number 0-255, got: " + parts[i + 1]);
        }
        if (value > 255) {
            return ActionResult::Failure(
                "Appearance value out of range (0-255): " + parts[i + 1]);
        }
        const uint8_t v = static_cast<uint8_t>(value);

        if (field == "haircolor") {
            change.haircolor = v;
        } else if (field == "beardcolor") {
            change.beardcolor = v;
        } else if (field == "eyecolor1") {
            change.eyecolor1 = v;
        } else if (field == "eyecolor2") {
            change.eyecolor2 = v;
        } else if (field == "hairstyle") {
            change.hairstyle = v;
        } else if (field == "beard") {
            change.beard = v;
        } else if (field == "face") {
            change.face = v;
        } else {
            return ActionResult::Failure("Unknown appearance field: " + parts[i] +
                                         ". " + kUsage);
        }
    }

    return m_dispatcher.setAppearance(change);
}

} // namespace action
} // namespace eqt
