// eq_action_handler_extended.cpp -- EqActionHandler: the extended action
// families the file grew after T102 -- Extended Movement / Character State /
// Combat, Entity Query, Utility, Social, Guild, Raid, Merchant, Find and GM.
// Moved verbatim out of eq_action_handler.cpp.
// Split by refactor/J12_eq_action_handler_split.md.
#include "client/eq_action_handler.h"
#include "client/eq.h"
#include "client/combat.h"
#include "client/pet_constants.h"
#include "client/spell/spell_manager.h"
#include "client/spell/cast_refusal.h"  // T244: official refusal strings
#include "client/skill/skill_manager.h"      // T156: useSkill() reaches SkillManager
#include "client/skill/skill_constants.h"
#include "client/inventory/inventory_manager.h"  // T230: item-id -> slot hop
#include "client/trade_manager.h"
#include "client/world_object.h"  // T149: open-container object_type lookup
#include "client/state/game_state.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <fmt/format.h>
#include <cmath>
#include <algorithm>  // T180: /raid list sorts the roster
#include <vector>

namespace eqt {

// ========== Extended Movement ==========

void EqActionHandler::setMovementMode(int mode) {
    // T95: `mode` arrives here as a caller-supplied int from
    // ActionDispatcher::setMovementMode(), which every current call site
    // (CommandProcessor's /run,/walk,/sneak) passes as a compile-time
    // constant -- but the public IActionHandler surface accepts any int,
    // and MovementMode is a plain enum with no fixed underlying type: an
    // out-of-range static_cast into it is undefined behaviour, not merely
    // a wrong result. Validate before casting.
    if (!isValidMovementMode(mode)) {
        LOG_ERROR(MOD_MOVEMENT,
                  "setMovementMode: {} is not a valid movement mode "
                  "(valid: 0=Run, 1=Walk, 2=Sneak); rejected, mode unchanged",
                  mode);
        m_eq.AddChatSystemMessage(
            "Movement mode " + std::to_string(mode) +
            " is not valid (must be 0=Run, 1=Walk, or 2=Sneak).");
        return;
    }
    m_eq.SetMovementMode(static_cast<MovementMode>(mode));
}

void EqActionHandler::setPositionState(int state) {
    // T95: same shape as setMovementMode() above -- PositionState is also
    // a plain enum with no fixed underlying type, so an unvalidated
    // out-of-range cast is undefined behaviour.
    if (!isValidPositionState(state)) {
        LOG_ERROR(MOD_MOVEMENT,
                  "setPositionState: {} is not a valid position state "
                  "(valid: 0=Standing, 1=Sitting, 2=Crouching, "
                  "3=FeignDeath, 4=Dead); rejected, state unchanged",
                  state);
        m_eq.AddChatSystemMessage(
            "Position state " + std::to_string(state) +
            " is not valid (must be 0-4: Standing/Sitting/Crouching/"
            "FeignDeath/Dead).");
        return;
    }
    m_eq.SetPositionState(static_cast<PositionState>(state));
}

// ========== Extended Character State ==========

void EqActionHandler::setRoleplay(bool rp) {
    m_eq.SetRoleplay(rp);
}

void EqActionHandler::setPathfinding(bool enabled) {
    m_eq.SetPathfinding(enabled);
}

// ========== Extended Combat ==========

void EqActionHandler::setAutoHunting(bool enabled) {
    auto* combat = getCombatManager();
    if (combat) {
        combat->SetAutoHunting(enabled);
    }
}

void EqActionHandler::setAutoLoot(bool enabled) {
    auto* combat = getCombatManager();
    if (combat) {
        combat->SetAutoLootEnabled(enabled);
    }
}

void EqActionHandler::listHuntTargets() {
    auto* combat = getCombatManager();
    if (combat) {
        combat->ListHuntTargets();
    }
}

// ========== Entity Query ==========

void EqActionHandler::listEntities(const std::string& filter) {
    m_eq.ListEntities(filter);
}

void EqActionHandler::dumpEntityAppearance(const std::string& name) {
    m_eq.DumpEntityAppearance(name);
}

// ========== Utility ==========

void EqActionHandler::sendAnimation(uint8_t animationId, uint8_t speed) {
    m_eq.SendAnimation(animationId, speed);
}

// ========== Social (T102) ==========
// Ported from game's Social-category handlers (eq_commands.cpp). /who
// (requestWho) ports the Social section's who_cmd -- see
// command_processor_social.cpp's cmdWho for why that one was chosen over
// Movement's later, intra-registry-shadowing-winner `who`.

void EqActionHandler::inspectTarget(uint16_t targetId) {
    uint16_t resolved = targetId;
    if (resolved == 0) {
        auto* combat = getCombatManager();
        if (combat && combat->HasTarget()) {
            resolved = combat->GetTargetId();
        }
    }
    if (resolved == 0) {
        m_eq.AddChatSystemMessage("No target - use /inspect <name> or target a player");
        return;
    }
    m_eq.SendInspectRequest(resolved);
    m_eq.AddChatSystemMessage("Inspecting...");
}

void EqActionHandler::toggleLFG() {
    m_eq.SendLFGCommand(!m_eq.IsLFG());
}

void EqActionHandler::setSurname(const std::string& surname) {
    m_eq.SendSurname(surname);
}

void EqActionHandler::rollRandom(uint32_t low, uint32_t high) {
    if (high < low) high = low;
    m_eq.SendRandom(low, high);
}

void EqActionHandler::yellForHelp() {
    m_eq.SendYellForHelp();
}

void EqActionHandler::requestWho(const std::string& name, int lvlLow, int lvlHigh) {
    m_eq.SendWhoAllRequest(name, lvlLow, lvlHigh);
    m_eq.AddChatSystemMessage("Searching for players...");
}

// ========== Guild (T132) ==========
//
// Pure routing to the pre-existing senders (src/client/eq_guild.cpp) --
// guild-membership checks, pending-invite checks, and player-facing chat
// messages ("Inviting X to join your guild.", etc.) already live inside
// those Send*() calls, so no logic is duplicated here. game's own command
// handlers called these same methods directly.

void EqActionHandler::guildInvite(const std::string& playerName) {
    m_eq.Guild().SendGuildInvite(playerName);
}

void EqActionHandler::guildAcceptInvite() {
    m_eq.Guild().SendGuildInviteAccept(true);
}

void EqActionHandler::guildDeclineInvite() {
    m_eq.Guild().SendGuildInviteAccept(false);
}

void EqActionHandler::guildRemove(const std::string& playerName) {
    m_eq.Guild().SendGuildRemove(playerName);
}

void EqActionHandler::guildDemote(const std::string& playerName) {
    m_eq.Guild().SendGuildDemote(playerName);
}

void EqActionHandler::guildMakeLeader(const std::string& playerName) {
    m_eq.Guild().SendGuildLeader(playerName);
}

void EqActionHandler::requestGuildMOTD() {
    m_eq.Guild().SendGetGuildMOTD();
}

void EqActionHandler::setGuildMOTD(const std::string& motd) {
    m_eq.Guild().SendSetGuildMOTD(motd);
}

void EqActionHandler::corpseDrag(const std::string& corpseName) {
    m_eq.SendCorpseDrag(corpseName);
}

void EqActionHandler::corpseDrop() {
    m_eq.SendCorpseDrop();
}

void EqActionHandler::consent(const std::string& playerName) {
    m_eq.SendConsent(playerName);
}

void EqActionHandler::consentDeny(const std::string& playerName) {
    m_eq.SendConsentDeny(playerName);
}

void EqActionHandler::rezAccept() {
    m_eq.SendRezzAnswer(true);
}

void EqActionHandler::rezDecline() {
    m_eq.SendRezzAnswer(false);
}

// ========== Utility (T135) ==========

void EqActionHandler::split(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
    m_eq.SendSplit(platinum, gold, silver, copper);
}

void EqActionHandler::save() {
    m_eq.SendSave();
}

// Ported from game's bindwound_cmd (eq_commands.cpp) exactly: targetId==0
// resolves to the CombatManager's current target if one exists, otherwise
// falls back to SELF (the player spawn id) -- unlike assist()/shieldTarget()
// above, which report "You don't have a target." instead of self-targeting.
// Reuses the same getCombatManager() helper T102 built for the Combat
// family rather than introducing any new targeting concept.
void EqActionHandler::bandage(uint16_t targetId) {
    // T162: this used to call EverQuest::SendBindWound() directly, which put
    // HC_OP_BindWound on the wire with no possession check at all -- the same
    // bypass shape T161 caught on /taunt, one function away. It now goes
    // through the gated entry point.
    //
    // The target rule is UNCHANGED and is now stated once instead of twice:
    // SkillManager::sendBindWound's case 9 already carries this exact
    // "current target, else self" fallback (skill_manager.cpp), copied from
    // this function when T152 wrote it. activateSkill() zeroes the target for
    // skills with requires_target == false (Bind Wound is one, because it may
    // be cast on yourself), so case 9's copy is the one that runs and the
    // duplicate here is deleted rather than left to drift.
    //
    // Bind Wound has NO animation (getSkillAnimationId(9) == 0 and no
    // animation code), so routing through activateSkill() adds no packet. It
    // does add Bind Wound's 1s recast, which /bandage did not have before;
    // that matches /useskill bind wound and is the shorter of the two rules.
    //
    // Cap data: config/skill_caps.json has a Bind Wound row for ALL SIXTEEN
    // classes, so this gate can refuse no class that could bandage before.
    if (targetId != 0) {
        LOG_DEBUG(MOD_COMBAT,
                  "bandage: explicit target {} is resolved inside the skill "
                  "path (current target, else self); no caller passes one",
                  targetId);
    }
    useSkill(EQ::kSkillBindWound);
}

void EqActionHandler::rewind() {
    m_eq.SendRewind();
}

void EqActionHandler::bug(const std::string& description) {
    m_eq.SendReport(description);
}

void EqActionHandler::petition(const std::string& text) {
    m_eq.SendPetition(text);
}


// ========== Raid (T179) ==========
// Raid command action codes (eqemu common/raid.h): 1=AcceptInvite, 3=Invite,
// 5=Disband. The vendorList() shape -- read EverQuest raid state, report here.
void EqActionHandler::raidInvite(const std::string& name) {
    if (name.empty()) {
        m_eq.AddChatSystemMessage("Usage: /raid invite <name>");
        return;
    }
    // RaidCommandInvite (3): player_name = invitee, leader_name = us.
    m_eq.Group().SendRaidCommand(3, name, m_eq.GetMyName());
    m_eq.AddChatSystemMessage(fmt::format("You invite {} to join your raid.", name));
}

void EqActionHandler::raidAccept() {
    const std::string inviter = m_eq.Group().GetPendingRaidInviter();
    if (inviter.empty()) {
        m_eq.AddChatSystemMessage("You have no pending raid invitation.");
        return;
    }
    // RaidCommandAcceptInvite (1): player_name = the INVITER, leader_name = us.
    m_eq.Group().SendRaidCommand(1, inviter, m_eq.GetMyName());
    m_eq.Group().ClearPendingRaidInvite();
    m_eq.AddChatSystemMessage(fmt::format("You accept {}'s raid invitation.", inviter));
}

void EqActionHandler::raidLeave() {
    if (!m_eq.Group().IsInRaid()) {
        m_eq.AddChatSystemMessage("You are not in a raid.");
        return;
    }
    // RaidCommandDisband (5) removing yourself: both names = us.
    const std::string me = m_eq.GetMyName();
    m_eq.Group().SendRaidCommand(5, me, me);
    m_eq.AddChatSystemMessage("You leave the raid.");
}

void EqActionHandler::raidStatus() {
    if (m_eq.Group().IsInRaid()) {
        const std::string& leader = m_eq.Group().GetRaidLeader();
        if (!leader.empty()) {
            m_eq.AddChatSystemMessage(fmt::format("You are in a raid led by {}.", leader));
        } else {
            m_eq.AddChatSystemMessage("You are in a raid.");
        }
        // T182: only worth saying when it is true -- an unlocked raid is the
        // default and saying so every time is noise.
        if (m_eq.Group().IsRaidLocked()) {
            m_eq.AddChatSystemMessage("The raid is locked.");
        }
    } else {
        m_eq.AddChatSystemMessage("You are not in a raid.");
    }
    const std::string pending = m_eq.Group().GetPendingRaidInviter();
    if (!pending.empty()) {
        m_eq.AddChatSystemMessage(fmt::format(
            "Pending raid invitation from {} -- type /raid accept to join.", pending));
    }
}

// T180. The roster is assembled from the action-0 stream, not read out of a
// roster packet -- Titanium's ENCODE drops eqemu's member list (see
// raid_update.h). Ordered by raid group, then by name, so the printout matches
// how the raid is actually organised rather than the order members happened to
// arrive in. Members in the raid but in no group (kRaidGroupless) sort last,
// under their own heading.
void EqActionHandler::raidList() {
    if (!m_eq.Group().IsInRaid()) {
        m_eq.AddChatSystemMessage("You are not in a raid.");
        return;
    }

    std::vector<EQT::RaidMemberInfo> members = m_eq.Group().GetRaidMembers();
    if (members.empty()) {
        // In a raid but holding no members: either the adds have not arrived
        // yet, or something dropped them. Say which, rather than printing an
        // empty list that reads like an empty raid.
        m_eq.AddChatSystemMessage("Raid member list not received yet.");
        return;
    }

    std::sort(members.begin(), members.end(),
              [](const EQT::RaidMemberInfo& a, const EQT::RaidMemberInfo& b) {
                  if (a.group != b.group) return a.group < b.group;
                  return a.name < b.name;
              });

    const std::string& leader = m_eq.Group().GetRaidLeader();
    m_eq.AddChatSystemMessage(fmt::format("Raid members ({}):", members.size()));

    uint32_t current_group = 0;
    bool have_group = false;
    for (const auto& m : members) {
        if (!have_group || m.group != current_group) {
            current_group = m.group;
            have_group = true;
            m_eq.AddChatSystemMessage(
                m.group == EQT::kRaidGroupless
                    ? std::string("  Not in a group:")
                    : fmt::format("  Group {}:", m.group + 1));  // 0-based on the wire
        }
        // Class/level are absent when the add arrived without the
        // RaidAddMember tail (a 136-byte general); print the name alone rather
        // than "Level 0 Unknown".
        std::string detail;
        if (m.level != 0 || m.class_id != 0) {
            detail = fmt::format(" (Level {} {})", m.level,
                                 EverQuest::GetClassName(m.class_id));
        }
        std::string tags;
        if (!leader.empty() && m.name == leader) tags += " [raid leader]";
        if (m.is_group_leader) tags += " [group leader]";
        m_eq.AddChatSystemMessage(fmt::format("    {}{}{}", m.name, detail, tags));
    }
}
// ========== Raid leader-only ops (T181) ==========
// Command codes (eqemu common/raid.h): 5 = Disband, 20 = LootType,
// 21 = AddLooter, 22 = RemoveLooter, 30 = MakeLeader.
//
// WHAT THE SERVER DOES AND DOES NOT CHECK -- this is the whole reason these
// commands need client-side guards (Handle_OP_RaidCommand, client_packet.cpp):
//   * MakeLeader IS gated: `if (strcmp(raid->leadername, GetName()) == 0)`,
//     and a non-leader's command falls out of the arm with NO reply at all.
//     Silence is the failure mode, so we pre-check and say why.
//   * Disband is NOT gated server-side: any raid member can remove any other.
//     T181 left it ungated here for that reason; T183 gates it, because the
//     real Titanium client offers kicking only to the leader and that is the
//     standard already applied to /raid loot and /raid lock. The gate is our
//     policy, not the server's -- said plainly rather than implied.
//   * LootType is NOT gated and is NOT validated: `parameter` goes straight
//     into Raid::ChangeLootType. A typo'd value would be stored verbatim and
//     match no arm of the switch in attack.cpp, silently disabling looting.
//     So the PARSE is the guard (EQT::ParseRaidLootType).
//
// Membership checks use the T180 roster: naming somebody who is not in the
// raid produces a server no-op, so catching it here turns silence into a
// sentence.

bool EqActionHandler::raidRequireMembership() {
    if (!m_eq.Group().IsInRaid()) {
        m_eq.AddChatSystemMessage("You are not in a raid.");
        return false;
    }
    return true;
}

bool EqActionHandler::raidRequireLeader(const std::string& what) {
    if (!raidRequireMembership()) {
        return false;
    }
    const std::string& leader = m_eq.Group().GetRaidLeader();
    if (leader.empty() || leader != m_eq.GetMyName()) {
        // The server would simply drop this on the floor.
        m_eq.AddChatSystemMessage(fmt::format(
            "Only the raid leader can {}.", what));
        return false;
    }
    return true;
}

// Returns true when `name` is in the roster. The roster is assembled from the
// action-0 stream (T180), so an empty roster means "we have not been told yet",
// which is not the same as "not a member" -- treat it as unknown and let the
// command through rather than refusing on missing information.
bool EqActionHandler::raidMemberKnown(const std::string& name) const {
    const auto& members = m_eq.Group().GetRaidMembers();
    if (members.empty()) {
        return true;
    }
    for (const auto& m : members) {
        if (m.name == name) {
            return true;
        }
    }
    return false;
}

void EqActionHandler::raidRemove(const std::string& name) {
    if (name.empty()) {
        m_eq.AddChatSystemMessage("Usage: /raid remove <name>");
        return;
    }
    if (!raidRequireMembership()) {
        return;
    }
    if (name == m_eq.GetMyName()) {
        // Checked BEFORE the leadership gate: a non-leader typing their own
        // name wants /raid leave, and telling them "only the leader can do
        // that" would send them the wrong way.
        // Identical on the wire to /raid leave; say which command they meant
        // rather than quietly doing something they did not ask for.
        m_eq.AddChatSystemMessage("To leave the raid yourself, use /raid leave.");
        return;
    }
    // T183: gated on leadership, resolving the asymmetry T182 flagged. The
    // SERVER does not gate this -- RaidCommandDisband lets any raid member
    // remove any other -- but the real Titanium client offers kicking only to
    // the leader, and that is the standard already applied to /raid loot and
    // /raid lock. Being permissive here and strict there was the inconsistency.
    if (!raidRequireLeader("remove a raid member")) {
        return;
    }
    if (!raidMemberKnown(name)) {
        m_eq.AddChatSystemMessage(fmt::format("{} is not in your raid.", name));
        return;
    }
    // RaidCommandDisband (5): leader_name = the member being removed,
    // player_name = whoever is doing the removing. Same shape as /raid leave,
    // which sends our own name in both.
    m_eq.Group().SendRaidCommand(5, m_eq.GetMyName(), name);
    m_eq.AddChatSystemMessage(fmt::format("You remove {} from the raid.", name));
}

void EqActionHandler::raidMakeLeader(const std::string& name) {
    if (name.empty()) {
        m_eq.AddChatSystemMessage("Usage: /raid leader <name>");
        return;
    }
    if (!raidRequireLeader("pass raid leadership")) {
        return;
    }
    if (name == m_eq.GetMyName()) {
        m_eq.AddChatSystemMessage("You are already the raid leader.");
        return;
    }
    if (!raidMemberKnown(name)) {
        m_eq.AddChatSystemMessage(fmt::format("{} is not in your raid.", name));
        return;
    }
    // RaidCommandMakeLeader (30): leader_name = the NEW leader.
    m_eq.Group().SendRaidCommand(30, m_eq.GetMyName(), name);
    m_eq.AddChatSystemMessage(fmt::format("You make {} the raid leader.", name));
}

void EqActionHandler::raidLootType(const std::string& mode) {
    if (mode.empty()) {
        m_eq.AddChatSystemMessage(
            "Usage: /raid loot <leader|groupleader|selected|all>");
        return;
    }
    uint32_t loot_type = 0;
    if (!EQT::ParseRaidLootType(mode, loot_type)) {
        // The server would store the bad value verbatim; refuse here instead.
        m_eq.AddChatSystemMessage(fmt::format(
            "'{}' is not a raid loot type. Use leader, groupleader, selected or all.",
            mode));
        return;
    }
    if (!raidRequireLeader("change the raid loot type")) {
        return;
    }
    // RaidCommandLootType (20): parameter = the loot type.
    m_eq.Group().SendRaidCommand(20, m_eq.GetMyName(), m_eq.GetMyName(), loot_type);
    m_eq.AddChatSystemMessage(fmt::format(
        "You set raid looting to {}.", EQT::RaidLootTypeName(loot_type)));
}

void EqActionHandler::raidLooter(bool add, const std::string& name) {
    if (name.empty()) {
        m_eq.AddChatSystemMessage("Usage: /raid looter <add|remove> <name>");
        return;
    }
    if (!raidRequireLeader(add ? "add a raid looter" : "remove a raid looter")) {
        return;
    }
    if (!raidMemberKnown(name)) {
        m_eq.AddChatSystemMessage(fmt::format("{} is not in your raid.", name));
        return;
    }
    // RaidCommandAddLooter (21) / RaidCommandRemoveLooter (22):
    // leader_name = the looter. Only meaningful under loot type 3 (Selected).
    m_eq.Group().SendRaidCommand(add ? 21 : 22, m_eq.GetMyName(), name);
    m_eq.AddChatSystemMessage(add
        ? fmt::format("You add {} as a raid looter.", name)
        : fmt::format("You remove {} as a raid looter.", name));
}

// T184: raid groups. RaidCommandMoveGroup (6) -- leader_name = the member
// being moved, parameter = the target WIRE group (0..11), or anything >= 12
// for the ungrouped bank.
//
// THE SERVER ANSWERS BOTH FAILURE CASES WITH SILENCE, and the roster is what
// lets us do better (Handle_OP_RaidCommand, zone/client_packet.cpp:12804):
//   * `if (group_count < 6)` -- moving into a group that already holds six
//     falls out of the arm with no reply at all.
//   * `if (parameter == old_group) break;` -- an explicit no-op, also silent.
// Both are cheap to detect here because T180's roster carries every member's
// group, so the client says which it was instead of appearing to hang.
//
// eqemu does not validate `parameter` either: anything outside 0..11 is read
// as "ungrouped", so a user typing 0 while thinking in wire numbers would
// silently be moved OUT of their group. ParseRaidGroup rejects it.
void EqActionHandler::raidGroup(const std::string& name, const std::string& group_text) {
    if (name.empty() || group_text.empty()) {
        m_eq.AddChatSystemMessage("Usage: /raid group <name> <1-12|none>");
        return;
    }
    uint32_t wire_group = 0;
    if (!EQT::ParseRaidGroup(group_text, wire_group)) {
        m_eq.AddChatSystemMessage(fmt::format(
            "'{}' is not a raid group. Use 1-{} or none.", group_text, EQT::kMaxRaidGroups));
        return;
    }
    if (!raidRequireLeader("assign raid groups")) {
        return;
    }

    const auto& members = m_eq.Group().GetRaidMembers();
    const EQT::RaidMemberInfo* target = nullptr;
    for (const auto& m : members) {
        if (m.name == name) {
            target = &m;
            break;
        }
    }
    if (!members.empty() && !target) {
        m_eq.AddChatSystemMessage(fmt::format("{} is not in your raid.", name));
        return;
    }

    if (target && target->group == wire_group) {
        // eqemu breaks out of the arm without a word.
        m_eq.AddChatSystemMessage(fmt::format(
            "{} is already in {}.", name, EQT::RaidGroupName(wire_group)));
        return;
    }
    if (wire_group != EQT::kRaidGroupless) {
        // Counted from the roster, which is only as current as the last
        // update we received -- so this can be wrong in the safe direction
        // (we refuse something that would have worked). It cannot be wrong in
        // the dangerous direction: if the roster says there is room and there
        // is not, the server does exactly what it did before, nothing.
        const size_t occupied = countRaidGroup(wire_group);
        if (occupied >= EQT::kMaxRaidGroupSize) {
            m_eq.AddChatSystemMessage(fmt::format(
                "{} is full ({} of {}).", EQT::RaidGroupName(wire_group),
                occupied, EQT::kMaxRaidGroupSize));
            return;
        }
    }

    m_eq.Group().SendRaidCommand(6, m_eq.GetMyName(), name, wire_group);
    m_eq.AddChatSystemMessage(
        wire_group == EQT::kRaidGroupless
            ? fmt::format("You move {} out of their group.", name)
            : fmt::format("You move {} to {}.", name, EQT::RaidGroupName(wire_group)));
}

size_t EqActionHandler::countRaidGroup(uint32_t wire_group) const {
    size_t n = 0;
    for (const auto& m : m_eq.Group().GetRaidMembers()) {
        if (m.group == wire_group) {
            ++n;
        }
    }
    return n;
}

// T182: raid lock / unlock. RaidCommandRaidLock (8) / RaidCommandRaidUnlock
// (9) -- the handler uses no fields from the body at all, it looks the raid up
// from the sender, so the names are ours by convention.
//
// The server does NOT gate these on leadership; the gate here is the same
// judgement as /raid loot, which the real Titanium client also offers only to
// the leader. See the doc for the one remaining asymmetry (/raid remove).
//
// Asking for the state it is already in is NOT an error server-side: eqemu
// answers with the same action code, sent to you alone instead of broadcast.
// We still send it -- the tracked state could be stale, and refusing on stale
// state would block a legitimate command -- and let the reply be the truth.
void EqActionHandler::raidLock(bool lock) {
    if (!raidRequireLeader(lock ? "lock the raid" : "unlock the raid")) {
        return;
    }
    m_eq.Group().SendRaidCommand(lock ? 8 : 9, m_eq.GetMyName(), m_eq.GetMyName());
    m_eq.AddChatSystemMessage(lock ? "You lock the raid." : "You unlock the raid.");
}

// ========== Merchant (T136) ==========
// vendorToggle()/buyFromVendor()/sellToVendor() are thin wrappers over the
// three new EverQuest methods (eq_commands.cpp) that push the same
// eqt::events::Vendor*Intent the renderer's vendor-window gesture pushes --
// EqActionHandler has no access to the private m_bridge, so EverQuest keeps
// owning the push, same as every guard/send in this family.

void EqActionHandler::vendorToggle() {
    m_eq.RequestVendorToggle();
}

// Pure state read + print, ported verbatim from eq_commands.cpp's old
// vendorlist_cmd lambda -- see EverQuest::GetVendorItems()/GetVendorName()
// (eq.h, T136) for why this reads via accessor here rather than through a
// new EverQuest method (this unit's plan asked explicitly what "migrate"
// means for a command that sends nothing; see the plan's Review).
void EqActionHandler::vendorList() {
    if (!m_eq.IsVendorWindowOpen()) {
        m_eq.AddChatSystemMessage("You are not at a vendor.");
        return;
    }
    const auto& items = m_eq.GetVendorItems();
    const auto& vendorName = m_eq.GetVendorName();
    if (items.empty()) {
        m_eq.AddChatSystemMessage(fmt::format("{}'s inventory is empty (or still loading).", vendorName));
        return;
    }
    m_eq.AddChatSystemMessage(fmt::format("{}'s inventory ({} item{}):",
        vendorName, items.size(), items.size() == 1 ? "" : "s"));
    for (const auto& [slot, item] : items) {
        std::string qtyStr = (item.quantity < 0) ? "unlimited" : std::to_string(item.quantity);
        m_eq.AddChatSystemMessage(fmt::format("  [slot {}] {} - {} (qty: {})",
            slot, item.name, EverQuest::FormatVendorPrice(item.price), qtyStr));
    }
}

void EqActionHandler::buyFromVendor(uint32_t slot, uint8_t quantity) {
    m_eq.RequestVendorBuy(slot, quantity);
}

void EqActionHandler::sellToVendor(uint32_t slot, uint8_t quantity) {
    m_eq.RequestVendorSell(slot, quantity);
}

// ========== Movement / Utility / Skills (T140) ==========

// Pure state read + print, ported verbatim from eq_commands_movement.cpp's
// old loc_cmd lambda -- EverQuest::GetPosition() (eq.h) was already public
// and already reads the same client position this used directly, so no new
// EverQuest surface was needed here.
void EqActionHandler::showLocation() {
    auto pos = m_eq.GetPosition();
    m_eq.AddChatSystemMessage(fmt::format("Your location is {:.1f}, {:.1f}, {:.1f}", pos.x, pos.y, pos.z));
}

void EqActionHandler::warp(const std::string& args) {
    m_eq.RequestWarp(args);
}

// Ported verbatim from eq_commands_movement.cpp's old zoning_cmd lambda --
// SetZoningEnabled()/IsZoningEnabled() (eq.h) were already public, so no
// new EverQuest surface was needed here (unlike warp/timeOfDay/bankToggle/
// trainWithTarget below, which do).
void EqActionHandler::showOrSetZoning(const std::string& args) {
    std::string trimmed = args;
    while (!trimmed.empty() && trimmed[0] == ' ') {
        trimmed = trimmed.substr(1);
    }
    if (trimmed.empty()) {
        m_eq.AddChatSystemMessage(fmt::format("Zoning is {}", m_eq.IsZoningEnabled() ? "on" : "off"));
        return;
    }
    if (trimmed == "on") {
        m_eq.SetZoningEnabled(true);
        m_eq.AddChatSystemMessage("Zoning is now on");
    } else if (trimmed == "off") {
        m_eq.SetZoningEnabled(false);
        m_eq.AddChatSystemMessage("Zoning is now off");
    } else {
        m_eq.AddChatSystemMessage("Usage: /zoning [on|off]");
    }
}

void EqActionHandler::requestQuit() {
    m_eq.RequestQuit();  // D20c4: Application checks quit flag
}

// Ported verbatim from eq_commands_movement.cpp's old camp_cmd lambda: sit
// down, ack, then start the 30-second camp timer. SetPositionState()/
// StartCampTimer() were already public.
void EqActionHandler::campNow() {
    m_eq.SetPositionState(POS_SITTING);
    m_eq.AddChatSystemMessage("You have begun to camp. You will log out in 30 seconds.");
    m_eq.StartCampTimer();
}

// Ported verbatim from eq_commands_movement.cpp's old debug_cmd lambda --
// EverQuest::SetDebugLevel()/GetDebugLevel() (eq.h) are static and already
// public, so no new EverQuest surface was needed here.
void EqActionHandler::debugLevel(const std::string& args) {
    if (args.empty()) {
        m_eq.AddChatSystemMessage(fmt::format("Debug level: {}", EverQuest::GetDebugLevel()));
    } else {
        try {
            int level = std::stoi(args);
            EverQuest::SetDebugLevel(level);
            m_eq.AddChatSystemMessage(fmt::format("Debug level set to {}", level));
        } catch (...) {
            m_eq.AddChatSystemMessage("Usage: /debug <0-6>");
        }
    }
}

// Ported verbatim from eq_commands_movement.cpp's old perf_cmd lambda --
// EQT::PerformanceMetrics::instance() is a process-global singleton, no
// EverQuest state involved beyond the AddChatSystemMessage/LOG_INFO calls.
void EqActionHandler::perfReport() {
    std::string report = EQT::PerformanceMetrics::instance().generateReport();
    // Output to console
    LOG_INFO(MOD_MAIN, "{}", report);
    // Also show summary in chat
    auto stats = EQT::PerformanceMetrics::instance().getStats("Frame Time");
    if (stats.count > 0) {
        double avgFps = stats.avgMs() > 0 ? 1000.0 / stats.avgMs() : 0;
        m_eq.AddChatSystemMessage(fmt::format("=== Performance Metrics ==="));
        m_eq.AddChatSystemMessage(fmt::format("Startup: {} ms", EQT::PerformanceMetrics::instance().getCategoryTotalMs(EQT::MetricCategory::Startup)));
        m_eq.AddChatSystemMessage(fmt::format("Zoning: {} ms", EQT::PerformanceMetrics::instance().getCategoryTotalMs(EQT::MetricCategory::Zoning)));
        m_eq.AddChatSystemMessage(fmt::format("Avg FPS: {:.1f} (avg frame: {:.1f} ms)", avgFps, stats.avgMs()));
        m_eq.AddChatSystemMessage(fmt::format("Frame time: min {} ms, max {} ms", stats.minMs, stats.maxMs));
        m_eq.AddChatSystemMessage("Full report written to console.");
    } else {
        m_eq.AddChatSystemMessage("No performance data collected yet.");
    }
}

void EqActionHandler::timeOfDay(const std::string& args) {
    m_eq.RequestTimeOfDay(args);
}

void EqActionHandler::bankToggle() {
    m_eq.RequestBankToggle();
}

void EqActionHandler::trainWithTarget() {
    m_eq.RequestTraining();
}

void EqActionHandler::trainSkill(const std::string& args) {
    m_eq.RequestTrainSkill(args);
}

void EqActionHandler::closeTrainer() {
    m_eq.RequestCloseTrainer();
}

void EqActionHandler::bankCoinMove(uint8_t coinType, int32_t amount, bool fromBank) {
    m_eq.Merchant().RequestBankCoinMove(coinType, amount, fromBank);
}

void EqActionHandler::bankCoinConvert(uint8_t fromCoinType, uint8_t toCoinType, int32_t amount) {
    m_eq.Merchant().RequestBankCoinConvert(fromCoinType, toCoinType, amount);
}

// ========== Social / Find (T150) ==========
// resolveTargetOrCurrent() repeats the targetId==0 fallback assist()/
// shieldTarget()/duel() established at the top of the Combat section: the
// live CombatManager target is read HERE, not from GameState::combat()
// (T28: that copy is never populated in production). Returns 0 and prints
// the same "You don't have a target." message when there is nothing to use.

// T162: see the declaration in eq_action_handler.h for why this exists and
// why it is not a second copy of T159's predicate. SkillManager::hasSkill()
// IS that predicate (isDisplayableSkill() && canClassHaveSkill()), and
// getSkillUseError() supplies the same wording activateSkill() would have
// produced, so a player refused by /track reads the same sentence as a player
// refused by /taunt or /useskill.
//
// Fails OPEN, exactly as T159's gate does: with no SkillManager, or with an
// unloaded cap table (canClassHaveSkill() defaults true), everything is
// permitted and behaviour is what it was before the gate existed. A data
// regression degrades the client to "no gate", never to "no skills".
bool EqActionHandler::skillGateAllows(uint8_t skillId) {
    auto* skills = m_eq.GetSkillManager();
    if (!skills) {
        return true;
    }
    if (skills->hasSkill(skillId)) {
        return true;
    }
    m_eq.AddChatSystemMessage(fmt::format(
        "Cannot use {}: {}", EQ::getSkillName(skillId),
        skills->getSkillUseError(skillId)));
    LOG_DEBUG(MOD_COMBAT, "Skill gate refused skill {} ({})", skillId,
              EQ::getSkillName(skillId));
    return false;
}

uint16_t EqActionHandler::resolveTargetOrCurrent(uint16_t targetId) {
    if (targetId != 0) {
        return targetId;
    }
    auto* combat = getCombatManager();
    if (!combat || !combat->HasTarget()) {
        m_eq.AddChatSystemMessage("You don't have a target.");
        return 0;
    }
    return combat->GetTargetId();
}

void EqActionHandler::assistGroup(uint16_t targetId) {
    uint16_t resolved = resolveTargetOrCurrent(targetId);
    if (resolved == 0) return;
    m_eq.SendAssistGroup(resolved);
}

void EqActionHandler::trackTarget(uint16_t targetId) {
    // T162: /track is the one bypass that CANNOT be routed through
    // activateSkill(). It sends HC_OP_TrackTarget ("make this entity my
    // tracking target"), while skill 53 maps to HC_OP_Track ("send me the
    // track list") -- two different opcodes and two different actions, so
    // activateSkill(53) would not be this command. eqemu class-gates it all
    // the same: Handle_OP_TrackTarget returns immediately on
    // !CanThisClassTrack(), so before this fix a Rogue's /track was
    // discarded in silence exactly like the Rogue's /taunt.
    //
    // Cap data: config/skill_caps.json has a Tracking row for classes 4
    // (Ranger), 6 (Druid) and 8 (Bard) only -- which is precisely eqemu's
    // CanThisClassTrack(). Gating on it refuses nobody who could track.
    if (!skillGateAllows(EQ::kSkillTracking)) return;
    uint16_t resolved = resolveTargetOrCurrent(targetId);
    if (resolved == 0) return;
    m_eq.SendTrackTarget(resolved);
}

void EqActionHandler::findPerson(uint16_t targetId) {
    uint16_t resolved = resolveTargetOrCurrent(targetId);
    if (resolved == 0) return;
    m_eq.SendFindPerson(resolved);
}

void EqActionHandler::friendsWho(const std::string& friendsList) {
    m_eq.SendFriendsWho(friendsList);
}

// ========== GM (T148) ==========
//
// Pure routing to the pre-existing senders (src/client/eq_gm.cpp), the same
// shape as the Guild family (T132): the senders own their own zone-
// connection guard and their own logging, so nothing is duplicated here.
//
// The GM-mode gate is NOT here -- it lives in ActionDispatcher::checkGMMode()
// so a single place owns the refusal string and the tests can drive it
// through RecordingActionHandler without an EverQuest instance.

bool EqActionHandler::isGMMode() const {
    return m_eq.GetGMMode();
}

void EqActionHandler::gmZoneRequest(const std::string& charName, uint16_t zoneId) {
    m_eq.Gm().SendGMZoneRequest(charName, zoneId);
}

void EqActionHandler::gmSummon(const std::string& charName) {
    m_eq.Gm().SendGMSummon(charName);
}

void EqActionHandler::gmGoto(const std::string& charName) {
    m_eq.Gm().SendGMGoto(charName);
}

void EqActionHandler::gmFind(const std::string& charName) {
    m_eq.Gm().SendGMFind(charName);
}

void EqActionHandler::gmKick(const std::string& charName) {
    m_eq.Gm().SendGMKick(charName);
}

void EqActionHandler::gmKill(const std::string& charName) {
    m_eq.Gm().SendGMKill(charName);
}

void EqActionHandler::gmHideMe(bool hide) {
    m_eq.Gm().SendGMHideMe(hide);
}

void EqActionHandler::gmEmoteZone(const std::string& text) {
    m_eq.Gm().SendGMEmoteZone(text);
}

void EqActionHandler::gmLastName(const std::string& charName, const std::string& lastName) {
    m_eq.Gm().SendGMLastName(charName, lastName);
}

} // namespace eqt
