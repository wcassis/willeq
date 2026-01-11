#include "client/action/command_processor.h"
#include "client/state/game_state.h"
#include "client/output/output_renderer.h"
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

void CommandProcessor::displayHelp(const std::string& commandName) {
    if (!commandName.empty()) {
        auto info = getCommandInfo(commandName);
        if (info) {
            displayMessage("/" + info->name + " - " + info->description);
            displayMessage("Usage: " + info->usage);
            if (!info->aliases.empty()) {
                std::string aliases;
                for (size_t i = 0; i < info->aliases.size(); ++i) {
                    if (i > 0) aliases += ", ";
                    aliases += "/" + info->aliases[i];
                }
                displayMessage("Aliases: " + aliases);
            }
        } else {
            displayError("Unknown command: " + commandName);
        }
        return;
    }

    // Display all commands by category
    displayMessage("=== Available Commands ===");
    for (const auto& category : getCategories()) {
        displayMessage("");
        displayMessage("-- " + category + " --");
        for (const auto& cmd : getCommandsByCategory(category)) {
            displayMessage("  /" + cmd.name + " - " + cmd.description);
        }
    }
    displayMessage("");
    displayMessage("Type /help <command> for detailed help");
}

// ========== Register Built-in Commands ==========

void CommandProcessor::registerBuiltinCommands() {
    // Chat commands
    registerCommand({
        "say", {"s"}, "/say <message>", "Say something to nearby players", "Chat", true
    }, [this](const std::string& args) { return cmdSay(args); });

    registerCommand({
        "shout", {"sh"}, "/shout <message>", "Shout to the zone", "Chat", true
    }, [this](const std::string& args) { return cmdShout(args); });

    registerCommand({
        "ooc", {}, "/ooc <message>", "Out of character message", "Chat", true
    }, [this](const std::string& args) { return cmdOOC(args); });

    registerCommand({
        "auction", {"auc"}, "/auction <message>", "Auction message", "Chat", true
    }, [this](const std::string& args) { return cmdAuction(args); });

    registerCommand({
        "tell", {"t"}, "/tell <name> <message>", "Send private message", "Chat", true
    }, [this](const std::string& args) { return cmdTell(args); });

    registerCommand({
        "reply", {"r"}, "/reply <message>", "Reply to last tell", "Chat", true
    }, [this](const std::string& args) { return cmdReply(args); });

    registerCommand({
        "gsay", {"g"}, "/gsay <message>", "Group message", "Chat", true
    }, [this](const std::string& args) { return cmdGroupSay(args); });

    registerCommand({
        "gu", {}, "/gu <message>", "Guild message", "Chat", true
    }, [this](const std::string& args) { return cmdGuildSay(args); });

    registerCommand({
        "emote", {"em", "me"}, "/emote <action>", "Perform an emote", "Chat", true
    }, [this](const std::string& args) { return cmdEmote(args); });

    // Movement commands
    registerCommand({
        "loc", {}, "/loc", "Show current location", "Movement", false
    }, [this](const std::string& args) { return cmdLoc(args); });

    registerCommand({
        "sit", {}, "/sit", "Sit down", "Movement", false
    }, [this](const std::string& args) { return cmdSit(args); });

    registerCommand({
        "stand", {}, "/stand", "Stand up", "Movement", false
    }, [this](const std::string& args) { return cmdStand(args); });

    registerCommand({
        "camp", {}, "/camp", "Sit down and logout after 30 seconds", "Movement", false
    }, [this](const std::string& args) { return cmdCamp(args); });

    registerCommand({
        "move", {}, "/move <x> <y> <z>", "Move to coordinates", "Movement", true
    }, [this](const std::string& args) { return cmdMove(args); });

    registerCommand({
        "moveto", {}, "/moveto <name>", "Move to entity", "Movement", true
    }, [this](const std::string& args) { return cmdMoveTo(args); });

    registerCommand({
        "follow", {}, "/follow [name]", "Follow entity or accept group invite", "Movement", false
    }, [this](const std::string& args) { return cmdFollow(args); });

    registerCommand({
        "stopfollow", {}, "/stopfollow", "Stop following", "Movement", false
    }, [this](const std::string& args) { return cmdStopFollow(args); });

    registerCommand({
        "face", {}, "/face <name|x y z>", "Face entity or location", "Movement", false
    }, [this](const std::string& args) { return cmdFace(args); });

    // Combat commands
    registerCommand({
        "target", {"tar"}, "/target <name>", "Target entity by name", "Combat", false
    }, [this](const std::string& args) { return cmdTarget(args); });

    registerCommand({
        "attack", {"a"}, "/attack", "Start attacking target", "Combat", false
    }, [this](const std::string& args) { return cmdAttack(args); });

    registerCommand({
        "stopattack", {}, "/stopattack", "Stop attacking", "Combat", false
    }, [this](const std::string& args) { return cmdStopAttack(args); });

    registerCommand({
        "aa", {}, "/aa", "Toggle auto-attack", "Combat", false
    }, [this](const std::string& args) { return cmdAutoAttack(args); });

    registerCommand({
        "cast", {}, "/cast <gem#>", "Cast spell from gem slot", "Combat", true
    }, [this](const std::string& args) { return cmdCast(args); });

    registerCommand({
        "interrupt", {}, "/interrupt", "Interrupt current cast", "Combat", false
    }, [this](const std::string& args) { return cmdInterrupt(args); });

    registerCommand({
        "consider", {"con"}, "/consider", "Consider current target", "Combat", false
    }, [this](const std::string& args) { return cmdConsider(args); });

    registerCommand({
        "hail", {}, "/hail", "Hail target or say Hail", "Combat", false
    }, [this](const std::string& args) { return cmdHail(args); });

    // Group commands
    registerCommand({
        "invite", {"inv"}, "/invite [name]", "Invite to group", "Group", false
    }, [this](const std::string& args) { return cmdInvite(args); });

    registerCommand({
        "disband", {}, "/disband", "Leave group", "Group", false
    }, [this](const std::string& args) { return cmdDisband(args); });

    registerCommand({
        "decline", {}, "/decline", "Decline group invite", "Group", false
    }, [this](const std::string& args) { return cmdDecline(args); });

    // Utility commands
    registerCommand({
        "who", {}, "/who", "List nearby entities", "Utility", false
    }, [this](const std::string& args) { return cmdWho(args); });

    registerCommand({
        "help", {"?"}, "/help [command]", "Show help", "Utility", false
    }, [this](const std::string& args) { return cmdHelp(args); });

    registerCommand({
        "quit", {}, "/quit", "Show exit options", "Utility", false
    }, [this](const std::string& args) { return cmdQuit(args); });

    registerCommand({
        "q", {}, "/q", "Exit immediately", "Utility", false
    }, [this](const std::string& args) {
        displayMessage("Exiting...");
        return ActionResult::Success("exit");
    });

    registerCommand({
        "debug", {}, "/debug <level>", "Set debug level (0-6)", "Utility", true
    }, [this](const std::string& args) { return cmdDebug(args); });

    registerCommand({
        "timestamp", {"ts"}, "/timestamp", "Toggle chat timestamps", "Utility", false
    }, [this](const std::string& args) { return cmdTimestamp(args); });

    registerCommand({
        "door", {"u"}, "/door", "Interact with nearest door", "Utility", false
    }, [this](const std::string& args) { return cmdDoor(args); });

    registerCommand({
        "afk", {}, "/afk", "Toggle AFK status", "Utility", false
    }, [this](const std::string& args) { return cmdAFK(args); });

    registerCommand({
        "anon", {}, "/anon", "Toggle anonymous status", "Utility", false
    }, [this](const std::string& args) { return cmdAnon(args); });

    // Spell commands
    registerCommand({
        "skills", {}, "/skills", "Toggle skills window", "Spells", false
    }, [this](const std::string& args) { return cmdSkills(args); });

    registerCommand({
        "gems", {}, "/gems", "Show memorized spells", "Spells", false
    }, [this](const std::string& args) { return cmdGems(args); });

    registerCommand({
        "mem", {}, "/mem <gem#> <spell>", "Memorize spell to gem slot", "Spells", true
    }, [this](const std::string& args) { return cmdMem(args); });

    registerCommand({
        "forget", {}, "/forget <gem#>", "Forget spell from gem slot", "Spells", true
    }, [this](const std::string& args) { return cmdForget(args); });

    // Pet commands
    registerCommand({
        "pet", {}, "/pet <command>", "Issue pet command (attack, back, follow, guard, sit, taunt, hold, focus, health, dismiss)", "Pet", true
    }, [this](const std::string& args) { return cmdPet(args); });

    registerCommand({
        "filter", {}, "/filter [channel]", "Toggle channel display", "Chat", false
    }, [this](const std::string& args) { return cmdFilter(args); });
}

// ========== Command Implementations ==========

ActionResult CommandProcessor::cmdSay(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Say, args);
}

ActionResult CommandProcessor::cmdShout(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Shout, args);
}

ActionResult CommandProcessor::cmdOOC(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::OOC, args);
}

ActionResult CommandProcessor::cmdAuction(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Auction, args);
}

ActionResult CommandProcessor::cmdTell(const std::string& args) {
    auto parts = splitArgs(args);
    if (parts.size() < 2) {
        return ActionResult::Failure("Usage: /tell <name> <message>");
    }

    std::string target = parts[0];
    std::string message;
    for (size_t i = 1; i < parts.size(); ++i) {
        if (i > 1) message += " ";
        message += parts[i];
    }

    return m_dispatcher.sendTell(target, message);
}

ActionResult CommandProcessor::cmdReply(const std::string& args) {
    return m_dispatcher.replyToLastTell(args);
}

ActionResult CommandProcessor::cmdGroupSay(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Group, args);
}

ActionResult CommandProcessor::cmdGuildSay(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Guild, args);
}

ActionResult CommandProcessor::cmdEmote(const std::string& args) {
    return m_dispatcher.sendChatMessage(ChatChannel::Emote, args);
}

ActionResult CommandProcessor::cmdLoc(const std::string& args) {
    auto& player = m_state.player();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "Your location is " << player.x() << ", " << player.y() << ", " << player.z();
    displayMessage(oss.str());
    return ActionResult::Success();
}

ActionResult CommandProcessor::cmdSit(const std::string& args) {
    return m_dispatcher.sit();
}

ActionResult CommandProcessor::cmdStand(const std::string& args) {
    return m_dispatcher.stand();
}

ActionResult CommandProcessor::cmdCamp(const std::string& args) {
    return m_dispatcher.startCamp();
}

ActionResult CommandProcessor::cmdMove(const std::string& args) {
    auto parts = splitArgs(args);
    if (parts.size() < 3) {
        return ActionResult::Failure("Usage: /move <x> <y> <z>");
    }

    try {
        float x = std::stof(parts[0]);
        float y = std::stof(parts[1]);
        float z = std::stof(parts[2]);
        return m_dispatcher.moveToLocation(x, y, z);
    } catch (const std::exception&) {
        return ActionResult::Failure("Invalid coordinates");
    }
}

ActionResult CommandProcessor::cmdMoveTo(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /moveto <name>");
    }
    return m_dispatcher.moveToEntity(args);
}

ActionResult CommandProcessor::cmdFollow(const std::string& args) {
    if (args.empty()) {
        // Accept group invite
        if (m_state.group().hasPendingInvite()) {
            return m_dispatcher.acceptGroupInvite();
        }
        return ActionResult::Failure("Usage: /follow <name> or use to accept group invite");
    }
    return m_dispatcher.followEntity(args);
}

ActionResult CommandProcessor::cmdStopFollow(const std::string& args) {
    return m_dispatcher.stopFollow();
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

ActionResult CommandProcessor::cmdTarget(const std::string& args) {
    if (args.empty()) {
        return m_dispatcher.targetNearest();
    }
    return m_dispatcher.targetEntityByName(args);
}

ActionResult CommandProcessor::cmdAttack(const std::string& args) {
    return m_dispatcher.enableAutoAttack();
}

ActionResult CommandProcessor::cmdStopAttack(const std::string& args) {
    return m_dispatcher.disableAutoAttack();
}

ActionResult CommandProcessor::cmdAutoAttack(const std::string& args) {
    return m_dispatcher.toggleAutoAttack();
}

ActionResult CommandProcessor::cmdCast(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /cast <gem#>");
    }

    try {
        int gem = std::stoi(args);
        if (gem < 1 || gem > 12) {
            return ActionResult::Failure("Gem slot must be 1-12");
        }
        return m_dispatcher.castSpell(static_cast<uint8_t>(gem));
    } catch (const std::exception&) {
        return ActionResult::Failure("Invalid gem slot");
    }
}

ActionResult CommandProcessor::cmdInterrupt(const std::string& args) {
    return m_dispatcher.interruptCast();
}

ActionResult CommandProcessor::cmdConsider(const std::string& args) {
    return m_dispatcher.consider();
}

ActionResult CommandProcessor::cmdHail(const std::string& args) {
    if (m_state.combat().hasTarget()) {
        return m_dispatcher.hailTarget();
    }
    return m_dispatcher.hail();
}

ActionResult CommandProcessor::cmdInvite(const std::string& args) {
    if (args.empty()) {
        return m_dispatcher.inviteTarget();
    }
    return m_dispatcher.inviteToGroup(args);
}

ActionResult CommandProcessor::cmdDisband(const std::string& args) {
    return m_dispatcher.leaveGroup();
}

ActionResult CommandProcessor::cmdDecline(const std::string& args) {
    return m_dispatcher.declineGroupInvite();
}

ActionResult CommandProcessor::cmdWho(const std::string& args) {
    auto& entities = m_state.entities();
    auto& player = m_state.player();

    auto nearbyEntities = entities.getEntitiesInRange(player.x(), player.y(), player.z(), 200.0f);

    displayMessage("=== Nearby Entities ===");
    for (uint16_t spawnId : nearbyEntities) {
        auto* entity = entities.getEntity(spawnId);
        if (entity) {
            std::ostringstream oss;
            oss << entity->name << " (Level " << static_cast<int>(entity->level) << ")";
            displayMessage(oss.str());
        }
    }
    displayMessage("Total: " + std::to_string(nearbyEntities.size()));

    return ActionResult::Success();
}

ActionResult CommandProcessor::cmdHelp(const std::string& args) {
    displayHelp(args);
    return ActionResult::Success();
}

ActionResult CommandProcessor::cmdQuit(const std::string& args) {
    displayMessage("Use /camp to safely logout, or /q to exit immediately");
    return ActionResult::Success();
}

ActionResult CommandProcessor::cmdDebug(const std::string& args) {
    try {
        int level = std::stoi(args);
        if (level < 0 || level > 6) {
            return ActionResult::Failure("Debug level must be 0-6");
        }
        // Note: This would need to be connected to the logging system
        displayMessage("Debug level set to " + std::to_string(level));
        return ActionResult::Success();
    } catch (const std::exception&) {
        return ActionResult::Failure("Invalid debug level");
    }
}

ActionResult CommandProcessor::cmdTimestamp(const std::string& args) {
    if (m_renderer) {
        m_renderer->setShowTimestamps(!m_renderer->getShowTimestamps());
        displayMessage(m_renderer->getShowTimestamps() ? "Timestamps enabled" : "Timestamps disabled");
    }
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

ActionResult CommandProcessor::cmdSkills(const std::string& args) {
    // This would toggle the skills window in graphical mode
    displayMessage("Skills window toggled");
    return ActionResult::Success();
}

ActionResult CommandProcessor::cmdGems(const std::string& args) {
    // Display memorized spells
    displayMessage("=== Memorized Spells ===");
    // Note: This would need access to spell data
    displayMessage("(Spell data not available in current implementation)");
    return ActionResult::Success();
}

ActionResult CommandProcessor::cmdMem(const std::string& args) {
    auto parts = splitArgs(args);
    if (parts.size() < 2) {
        return ActionResult::Failure("Usage: /mem <gem#> <spell_name>");
    }

    try {
        int gem = std::stoi(parts[0]);
        if (gem < 1 || gem > 12) {
            return ActionResult::Failure("Gem slot must be 1-12");
        }
        // Note: Would need spell ID lookup by name
        displayMessage("Memorizing spell to gem " + std::to_string(gem));
        return ActionResult::Success();
    } catch (const std::exception&) {
        return ActionResult::Failure("Invalid gem slot");
    }
}

ActionResult CommandProcessor::cmdForget(const std::string& args) {
    if (args.empty()) {
        return ActionResult::Failure("Usage: /forget <gem#>");
    }

    try {
        int gem = std::stoi(args);
        if (gem < 1 || gem > 12) {
            return ActionResult::Failure("Gem slot must be 1-12");
        }
        return m_dispatcher.forgetSpell(static_cast<uint8_t>(gem));
    } catch (const std::exception&) {
        return ActionResult::Failure("Invalid gem slot");
    }
}

ActionResult CommandProcessor::cmdPet(const std::string& args) {
    if (args.empty()) {
        displayMessage("Usage: /pet <command>");
        displayMessage("Commands: attack, back, follow, guard, sit, taunt, hold, focus, health, dismiss");
        return ActionResult::Success();
    }

    std::string cmd = toLower(args);

    if (cmd == "attack") {
        return m_dispatcher.petAttack();
    } else if (cmd == "back" || cmd == "backoff") {
        return m_dispatcher.petBackOff();
    } else if (cmd == "follow" || cmd == "followme") {
        return m_dispatcher.petFollow();
    } else if (cmd == "guard" || cmd == "guardhere") {
        return m_dispatcher.petGuard();
    } else if (cmd == "sit") {
        return m_dispatcher.petSit();
    } else if (cmd == "taunt") {
        return m_dispatcher.petTaunt();
    } else if (cmd == "hold") {
        return m_dispatcher.petHold();
    } else if (cmd == "focus") {
        return m_dispatcher.petFocus();
    } else if (cmd == "health") {
        return m_dispatcher.petHealth();
    } else if (cmd == "dismiss" || cmd == "getlost") {
        return m_dispatcher.dismissPet();
    } else {
        return ActionResult::Failure("Unknown pet command: " + args);
    }
}

ActionResult CommandProcessor::cmdFilter(const std::string& args) {
    if (args.empty()) {
        displayMessage("Usage: /filter <channel>");
        displayMessage("Channels: say, tell, group, guild, shout, auction, ooc, emote, combat, exp, loot, npc, all");
        return ActionResult::Success();
    }

    displayMessage("Filter toggled for: " + args);
    return ActionResult::Success();
}

} // namespace action
} // namespace eqt
