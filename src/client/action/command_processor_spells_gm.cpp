#include "client/action/command_processor.h"
#include "client/state/game_state.h"
#include "client/output/output_renderer.h"
#include "client/skill/skill_constants.h"  // T156: /useskill id/name resolution
#include "client/inventory/inventory_constants.h"  // T278: /combine <general slot>
#include "common/logging.h"
#include "common/packets/common.h"  // T483: the gem counts
#include <fmt/format.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <cctype>
#include "client/action/command_processor_internal.h"  // J04: parseU32Strict / splitWhitespace

namespace eqt {
namespace action {

using detail::parseU32Strict;
using detail::splitWhitespace;

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
    if (gem < 1 || gem > static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT)) {
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
    if (gem < 1 || gem > static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT)) {
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
    if (gem < 1 || gem > static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT)) {
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

ActionResult CommandProcessor::cmdRemoveBuff(const std::string& args) {
    // T277: no parsing here on purpose. "slot 3" and "Minor Shielding" are
    // both resolved against the live buff model, which lives on the game
    // side (BuffManager); GameState has no buff list to validate against,
    // so inventing a range check here would just be a second, weaker copy
    // of the one EverQuest::RequestCancelBuff() has to do anyway.
    if (args.empty()) {
        return ActionResult::Failure("Usage: /removebuff <slot# | spell name>");
    }
    return m_dispatcher.cancelBuff(args);
}

ActionResult CommandProcessor::cmdSpellbook(const std::string& args) {
    // T272: the "not yet implemented in UI" text this printed went stale --
    // the spellbook popup exists (SpellbookPopupState / renderSpellbookPopup)
    // and, since T272, clicking a spell in it memorizes that spell. There is
    // no game-thread channel from CommandProcessor to the renderer's popup
    // state (no dispatcher call, no bridge here), so this stays a pure
    // message; what it must not do any longer is claim the window is missing.
    displayMessage("Spellbook: press Ctrl+B to open it (or /newbook).");
    displayMessage("Click a spell to memorize it into the first empty gem;");
    displayMessage("click a gem first to choose which gem to overwrite.");
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
