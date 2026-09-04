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

// ========== Register Built-in Commands ==========
//
// G03 (refactor/G03_register_builtin_commands.md): what used to be one
// 768-line registerBuiltinCommands() is now the ordered list of per-family
// registerXCommands() calls below, cut at the banners that were already in
// this file. Each family is a VERBATIM slice -- no registration moved
// relative to any other.
//
// ORDER IS LOAD-BEARING. registerCommand() does m_commands[name] = ... and
// m_aliases[alias] = name, so the LAST registration of a name or alias wins
// (J04; T097/T098 audited the shadowing that caused). The order is pinned by
// tests/test_command_registration_order.cpp against
// tests/goldens/command_registration_order_g03.txt, extracted from the
// pre-G03 source. Reordering these calls, or moving a registration from one
// family to another, fails that test on purpose.

void CommandProcessor::registerBuiltinCommands() {
    registerChatCommands();
    registerInteractionCommands();
    registerGroupCommands();
    registerCombatCommands();
    registerSocialCommands();
    registerGuildCommands();
    registerCorpseCommands();
    registerTradeskillCommands();
    registerUtilityCommands();
    registerCommerceCommands();
    registerInventoryCommands();
    registerMovementAndUtilityCommands();
    registerSpellAndPetCommands();
    registerExtendedMovementCommands();
    registerEntityQueryCommands();
    registerExtendedCombatCommands();
    registerPathfindingCommands();
    registerEmoteShortcutCommands();
    registerLoggingFilterCommands();
    registerFindCommands();
    registerGMCommands();
}

// Chat (T100)
void CommandProcessor::registerChatCommands() {
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
}

// Face / consider / appearance / inspect message / hail / npcstate
void CommandProcessor::registerInteractionCommands() {
    // T98: loc/sit/stand/camp/move/moveto/follow/stopfollow registrations
    // removed -- shadowed dead code. face stays: no game "face" command
    // exists at all, so it was never shadowed.
    registerCommand({
        "face", {}, "/face <name|x y z>", "Face entity or location", "Movement", false
    }, [this](const std::string& args) { return cmdFace(args); });

    // T98: target/cast/interrupt registrations removed -- shadowed dead code.
    // T51: attack/stopattack/aa registrations removed from this registry.
    // T29's dispatch order (registry 1 -- EverQuest::m_command_registry --
    // tried first, see Application::DispatchSlashCommand in
    // application_commands.cpp) meant these
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
}

// Group + raid (T99, T179-T184)
void CommandProcessor::registerGroupCommands() {
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
}

// Combat (T102) + /useskill (T156)
void CommandProcessor::registerCombatCommands() {
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

    // T347 (B012): the counterpart to /target. ActionDispatcher::clearTarget()
    // existed but was reachable only from the ClearTarget keybinding
    // (input_action_bridge.cpp), so a --script run could acquire a target and
    // never drop one. Registered here, next to /target, so the golden's edit
    // is one insertion at position 22. No alias: "ct" is unclaimed but short
    // aliases are what T097/T098's shadowing audit was about.
    registerCommand({
        "cleartarget", {}, "/cleartarget", "Clear your current target", "Combat", false
    }, [this](const std::string& args) { return cmdClearTarget(args); });

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
}

// Social (T102)
void CommandProcessor::registerSocialCommands() {
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
}

// Guild (T132)
void CommandProcessor::registerGuildCommands() {
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
}

// Corpse + resurrection (T134)
void CommandProcessor::registerCorpseCommands() {
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
}

// World object click (T144) + tradeskill recipes (T149, T278)
void CommandProcessor::registerTradeskillCommands() {
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

    // T278: the MANUAL combine and the container close. /recipecombine above
    // is the AUTO-combine (OP_RecipeAutoCombine, server picks the
    // components for a recipe id); /combine is the "I loaded the forge
    // myself, hit Combine" request (OP_TradeSkillCombine). Neither takes an
    // argument -- the container is the one the server has told us is open.
    // See refactor/T278_tradeskill_combine.md.
    registerCommand({
        "combine", {"docombine"}, "/combine | /combine <general slot>",
        "Combine the items in the open world container, or in a container you carry",
        "Tradeskill", false
    }, [this](const std::string& args) { return cmdCombine(args); });

    registerCommand({
        "closecontainer", {"containerclose"}, "/closecontainer",
        "Close the open world tradeskill container", "Tradeskill", false
    }, [this](const std::string& args) { return cmdCloseContainer(args); });
}

// Utility (T135)
void CommandProcessor::registerUtilityCommands() {
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
}

// Merchant / commerce (T136)
void CommandProcessor::registerCommerceCommands() {
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
}

// Inventory: equip (T141), read (T273), scribe (T276)
void CommandProcessor::registerInventoryCommands() {
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

    // T273: /read -- ask the server for a carried book or note's text.
    // T264 listed ReadItemIntent as a blocked intent (consumer arm, no
    // producer); this command is the producer. See
    // refactor/T273_read_book.md.
    registerCommand({
        "read", {}, "/read <slot> | /read <item name>",
        "Read a book or note you are carrying (e.g. /read Tattered Note)",
        "Inventory", false
    }, [this](const std::string& args) { return cmdRead(args); });

    // T276: /scribe -- write a carried spell scroll into the spellbook.
    // T264 listed ScribeSpellIntent as blocked (consumer arm, no producer);
    // this command is the producer. See refactor/T276_scribe_scroll.md.
    registerCommand({
        "scribe", {}, "/scribe <slot> | /scribe <item name>",
        "Scribe a spell scroll you are carrying into your spellbook (e.g. /scribe Spell: Minor Healing)",
        "Inventory", false
    }, [this](const std::string& args) { return cmdScribe(args); });
}

// The final sixteen of the game-registry migration (T140) + /trainclose (T280)
void CommandProcessor::registerMovementAndUtilityCommands() {
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
        "bank", {}, "/bank [deposit|withdraw <amount> <pp|gp|sp|cp> | convert <amount> <from> <to>]",
        "Open/close the bank window with your current target, or move bank coins", "Utility", false
    }, [this](const std::string& args) { return cmdBank(args); });

    registerCommand({
        "train", {}, "/train [skill|id|list]",
        "Open the trainer window with your target, or train a skill at the open one",
        "Skills", false   // argless /train keeps its pre-T280 meaning
    }, [this](const std::string& args) { return cmdTrain(args); });

    // T280: /trainclose -- the scriptable form of a trainer-window close
    // button (there is no trainer panel yet, so the command is the only
    // producer; T156's rule keeps it first either way).
    registerCommand({
        "trainclose", {"traindone"}, "/trainclose", "Close the trainer window", "Skills", false
    }, [this](const std::string& args) { return cmdTrainClose(args); });

    registerCommand({
        "door", {"u"}, "/door", "Interact with nearest door", "Utility", false
    }, [this](const std::string& args) { return cmdDoor(args); });

    registerCommand({
        "afk", {}, "/afk", "Toggle AFK status", "Utility", false
    }, [this](const std::string& args) { return cmdAFK(args); });

    registerCommand({
        "anon", {}, "/anon", "Toggle anonymous status", "Utility", false
    }, [this](const std::string& args) { return cmdAnon(args); });
}

// Spells + pet (T101) + /removebuff (T277)
void CommandProcessor::registerSpellAndPetCommands() {
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

    // T277: /removebuff -- the scriptable form of clicking a buff off the
    // buff bar (T156's rule: a click gesture is not scriptable, so the
    // command comes first and the click reuses its body).
    registerCommand({
        "removebuff", {"cancelbuff"}, "/removebuff <slot# | spell name>",
        "Ask the server to drop one of your buffs", "Spells", true
    }, [this](const std::string& args) { return cmdRemoveBuff(args); });

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
}

// Extended movement
void CommandProcessor::registerExtendedMovementCommands() {
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
}

// Entity query
void CommandProcessor::registerEntityQueryCommands() {
    // Entity query commands
    registerCommand({
        "list", {}, "/list [search]", "List nearby entities", "Utility", false
    }, [this](const std::string& args) { return cmdList(args); });

    registerCommand({
        "dump", {}, "/dump <name>", "Dump entity appearance/equipment info", "Utility", true
    }, [this](const std::string& args) { return cmdDump(args); });
}

// Extended combat + loot window (T268)
void CommandProcessor::registerExtendedCombatCommands() {
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
        "loot", {}, "/loot <corpseId|corpse name> | /loot <slot|item name> (window open)",
        "Open a corpse's loot window; with one open, take one item", "Combat", true
    }, [this](const std::string& args) { return cmdLoot(args); });

    // T268: the rest of the loot window.
    registerCommand({
        "lootall", {}, "/lootall", "Take everything from the open loot window and close it", "Combat", false
    }, [this](const std::string& args) { return cmdLootAll(args); });

    registerCommand({
        "lootclose", {"closeloot"}, "/lootclose", "Close the open loot window", "Combat", false
    }, [this](const std::string& args) { return cmdLootClose(args); });
}

// Pathfinding and roleplay
void CommandProcessor::registerPathfindingCommands() {
    // Pathfinding and roleplay
    registerCommand({
        "pathfinding", {}, "/pathfinding [on|off]", "Toggle pathfinding", "Utility", false
    }, [this](const std::string& args) { return cmdPathfinding(args); });

    registerCommand({
        "roleplay", {"rp"}, "/roleplay [on|off]", "Toggle roleplay status", "Utility", false
    }, [this](const std::string& args) { return cmdRoleplay(args); });
}

// Emote shortcuts
void CommandProcessor::registerEmoteShortcutCommands() {
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
}

// Logging filters
void CommandProcessor::registerLoggingFilterCommands() {
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

// Social / find family (T150)
void CommandProcessor::registerFindCommands() {
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
}

// GM (T148)
void CommandProcessor::registerGMCommands() {
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

} // namespace action
} // namespace eqt
