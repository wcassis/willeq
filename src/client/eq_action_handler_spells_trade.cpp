// eq_action_handler_spells_trade.cpp -- EqActionHandler: the Spellbook,
// Trade, Pet and Tradeskill domains, moved verbatim out of
// eq_action_handler.cpp. Note the file's own grouping quirk: setAppearance
// and setInspectMessage sit under the Tradeskill banner and stay there.
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

// ========== Spellbook ==========

void EqActionHandler::forgetSpell(uint8_t gemSlot) {
    // T101: fixed -- ported from game's /forget (eq_commands_spells.cpp).
    // Was a stub ("Would need to send forget spell packet") that always
    // silently no-op'd; T97 flagged this as a false-success two layers
    // down from a dispatcher method that otherwise looked fully wired.
    auto* spellMgr = m_eq.GetSpellManager();
    if (!spellMgr) {
        m_eq.AddChatSystemMessage("Spell system not initialized");
        return;
    }
    // gemSlot arrives 1-indexed (ActionDispatcher::forgetSpell() already
    // validated 1-8); SpellManager's gems are 0-indexed.
    if (spellMgr->forgetSpell(static_cast<uint8_t>(gemSlot - 1))) {
        m_eq.AddChatSystemMessage("Forgot spell in gem " + std::to_string(gemSlot));
    } else {
        m_eq.AddChatSystemMessage("No spell in gem " + std::to_string(gemSlot));
    }
}

void EqActionHandler::memorizeSpellByName(const std::string& spellName, uint8_t gemSlot) {
    // T101: ported from game's /mem (eq_commands_spells.cpp) verbatim --
    // exact match, else partial match with disambiguation (first 5 shown).
    auto* spellMgr = m_eq.GetSpellManager();
    if (!spellMgr) {
        m_eq.AddChatSystemMessage("Spell system not initialized");
        return;
    }

    const EQ::SpellData* spell = spellMgr->getDatabase().getSpellByName(spellName);
    if (!spell) {
        auto matches = spellMgr->getDatabase().findSpellsByName(spellName);
        if (matches.empty()) {
            m_eq.AddChatSystemMessage("Spell '" + spellName + "' not found");
            return;
        }
        if (matches.size() == 1) {
            spell = matches[0];
        } else {
            m_eq.AddChatSystemMessage("Multiple matches found:");
            size_t count = 0;
            for (const auto* s : matches) {
                m_eq.AddChatSystemMessage("  " + s->name + " (ID: " + std::to_string(s->id) + ")");
                if (++count >= 5) {
                    if (matches.size() > 5) {
                        m_eq.AddChatSystemMessage("  ... and " + std::to_string(matches.size() - 5) + " more");
                    }
                    break;
                }
            }
            return;
        }
    }

    // gemSlot arrives 1-indexed (ActionDispatcher::memorizeSpellByName()
    // already validated 1-8); SpellManager's gems are 0-indexed.
    if (spellMgr->memorizeSpell(spell->id, static_cast<uint8_t>(gemSlot - 1))) {
        m_eq.AddChatSystemMessage("Memorizing " + spell->name + " in gem " + std::to_string(gemSlot));
    } else {
        m_eq.AddChatSystemMessage("Cannot memorize " + spell->name + " - check if scribed and level requirement");
    }
}

void EqActionHandler::cancelBuff(const std::string& args) {
    // T277: one body, one resolver. EverQuest::RequestCancelBuff() owns the
    // slot/name lookup because BuffManager is the only place the player's
    // live buffs exist; the buff-bar click reaches the same wire hop
    // (SendBuffCancelRequest) through BuffCancelIntent.
    m_eq.RequestCancelBuff(args);
}

void EqActionHandler::listMemorizedSpells() {
    // T101: ported from game's /gems (eq_commands_spells.cpp) verbatim --
    // fixes the "(Spell data not available in current implementation)"
    // stub T97 found underneath the pre-T101 dispatcher method.
    auto* spellMgr = m_eq.GetSpellManager();
    if (!spellMgr) {
        m_eq.AddChatSystemMessage("Spell system not initialized");
        return;
    }
    m_eq.AddChatSystemMessage("=== Spell Gems ===");
    for (int i = 0; i < 8; i++) {
        uint32_t spellId = spellMgr->getMemorizedSpell(static_cast<uint8_t>(i));
        if (spellId != EQ::SPELL_UNKNOWN && spellId != 0xFFFFFFFF) {
            const EQ::SpellData* spell = spellMgr->getSpell(spellId);
            std::string stateStr;
            EQ::GemState state = spellMgr->getGemState(static_cast<uint8_t>(i));
            switch (state) {
                case EQ::GemState::Ready: stateStr = "Ready"; break;
                case EQ::GemState::Refresh:
                    stateStr = "Refresh (" + std::to_string(spellMgr->getGemCooldownRemaining(static_cast<uint8_t>(i)) / 1000) + "s)";
                    break;
                case EQ::GemState::MemorizeProgress:
                    stateStr = "Memorizing (" + std::to_string(static_cast<int>(spellMgr->getMemorizeProgress(static_cast<uint8_t>(i)) * 100)) + "%)";
                    break;
                case EQ::GemState::Casting: stateStr = "Casting"; break;
                default: stateStr = ""; break;
            }
            std::string spellLabel = spell ? spell->name : ("Unknown(" + std::to_string(spellId) + ")");
            m_eq.AddChatSystemMessage("[" + std::to_string(i + 1) + "] " + spellLabel + " " + stateStr);
        } else {
            m_eq.AddChatSystemMessage("[" + std::to_string(i + 1) + "] <empty>");
        }
    }
}

// ========== Trade ==========

// T102: repointed (not a new method -- T101's rule: repoint when the
// existing method's internals are untested; test_action_dispatcher.cpp's
// coverage of requestTrade/acceptTrade/cancelTrade is fake-handler routing
// only, confirmed before making this change). Ported from game's trade_cmd/
// accept_cmd/canceltrade_cmd (eq_commands.cpp). targetId==0 fallback and
// name resolution follow the same split as assist()/shieldTarget() above.
// The self-trade guard from game's trade_cmd is NOT ported: it needs "my
// own spawn id", which has no public accessor from this layer (unlike
// CombatManager's target, TradeManager/GameState don't expose it) --
// flagged as a deviation in the Review rather than silently dropped.
//
// T103: precondition/display reads below now go through
// m_eq.GetGameState().trade() (state::TradeState, the projection T103
// added) instead of reading TradeManager directly -- closing the layering
// compromise T102 recorded honestly rather than hiding. TradeManager
// itself is still reached for the four calls that actually perform a
// trade action (requestTrade()/acceptTradeRequest()/cancelTrade()/
// rejectTradeRequest() -- these send real packets, nothing else does that)
// -- same shape as CombatManager's calls elsewhere in this file.
void EqActionHandler::requestTrade(uint16_t targetId) {
    auto* trade = m_eq.GetTradeManager();
    if (!trade) {
        m_eq.AddChatSystemMessage("Trade not available");
        return;
    }
    if (m_eq.GetGameState().trade().active()) {
        m_eq.AddChatSystemMessage("You are already trading");
        return;
    }
    auto* combat = getCombatManager();
    if (combat && combat->GetCombatState() == COMBAT_STATE_ENGAGED) {
        m_eq.AddChatSystemMessage("You cannot trade while in combat");
        return;
    }

    uint16_t resolved = targetId;
    if (resolved == 0 && combat && combat->HasTarget()) {
        resolved = combat->GetTargetId();
    }
    if (resolved == 0) {
        m_eq.AddChatSystemMessage("No target - use /trade <name> or target a player");
        return;
    }

    const auto& entities = m_eq.GetGameState().entities();
    const auto* entity = entities.getEntity(resolved);
    bool isNpc = entity && entity->npcType == 1;
    std::string targetName = entity ? entity->name : std::string();

    if (entity) {
        auto& player = m_eq.GetGameState().player();
        float dx = player.x() - entity->x;
        float dy = player.y() - entity->y;
        float dz = player.z() - entity->z;
        float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        constexpr float MAX_TRADE_DISTANCE = 150.0f;
        if (distance > MAX_TRADE_DISTANCE) {
            m_eq.AddChatSystemMessage("You are too far away to trade");
            return;
        }
    }

    trade->requestTrade(resolved, targetName, isNpc);
    m_eq.AddChatSystemMessage(fmt::format("Requesting trade with {}", EQT::toDisplayName(targetName)));
}

void EqActionHandler::acceptTrade() {
    auto* trade = m_eq.GetTradeManager();
    if (!trade) {
        m_eq.AddChatSystemMessage("Trade not available");
        return;
    }
    // T259: during an ACTIVE trade, /accept is the trade window's Accept
    // button (TradeManager::clickAccept -> OP_TradeAcceptClick). That path
    // was reachable only via a bridge intent nothing produced (deleted T264), so
    // no trade could ever be completed from a headless/scripted client.
    if (m_eq.GetGameState().trade().active()) {
        if (trade->isOwnAccepted()) {
            m_eq.AddChatSystemMessage("You have already accepted this trade");
            return;
        }
        trade->clickAccept();
        m_eq.AddChatSystemMessage(fmt::format("You accept the trade with {}",
            EQT::toDisplayName(m_eq.GetGameState().trade().partnerName())));
        return;
    }
    if (!m_eq.GetGameState().trade().hasPendingRequest()) {
        m_eq.AddChatSystemMessage("No pending trade request");
        return;
    }
    auto* combat = getCombatManager();
    if (combat && combat->GetCombatState() == COMBAT_STATE_ENGAGED) {
        m_eq.AddChatSystemMessage("You cannot trade while in combat");
        return;
    }
    // T103: acceptTradeRequest() drives TradeManager's setState(Active),
    // which synchronously (same thread, same call) fires
    // EverQuest::SetupTradeWindowCallbacks()'s Active branch and populates
    // m_eq.GetGameState().trade() BEFORE this call returns -- so reading the
    // projection's partnerName() after the call is safe and correct, and
    // sidesteps the read-before-populate aliasing class of bug entirely
    // (the pre-T103 fix here read TradeManager::getPartnerName() after
    // acceptTradeRequest() too, for the same reason -- see T102's Review --
    // this is the same ordering, just through the projection instead of
    // TradeManager directly).
    trade->acceptTradeRequest();
    m_eq.AddChatSystemMessage(fmt::format("Accepting trade with {}",
        EQT::toDisplayName(m_eq.GetGameState().trade().partnerName())));
}

void EqActionHandler::cancelTrade() {
    auto* trade = m_eq.GetTradeManager();
    if (!trade) {
        m_eq.AddChatSystemMessage("Trade not available");
        return;
    }
    if (!m_eq.GetGameState().trade().active()) {
        m_eq.AddChatSystemMessage("You are not trading");
        return;
    }
    trade->cancelTrade();
    m_eq.AddChatSystemMessage("Trade cancelled");
}

void EqActionHandler::declineTrade() {
    auto* trade = m_eq.GetTradeManager();
    if (!trade) {
        m_eq.AddChatSystemMessage("Trade not available");
        return;
    }
    auto& tradeState = m_eq.GetGameState().trade();
    if (!tradeState.hasPendingRequest()) {
        m_eq.AddChatSystemMessage("No pending trade request");
        return;
    }
    // T222: TradeManager::rejectTradeRequest() now fires m_on_cancelled()
    // with TradeEndReason::DeclinedLocal; the setOnCancelled lambda in
    // eq_merchant.cpp prints the single "Declined trade request from X."
    // line, resets the projection and publishes TradeCancelled. T103's
    // in-territory workaround (an explicit trade().reset() here) and the
    // duplicate print are gone -- DeclineTrade_ClearsProjectionViaCancelledCallback
    // fails without the TradeManager fix.
    trade->rejectTradeRequest();
}

// ========== Pet ==========

void EqActionHandler::sendPetCommand(uint8_t command, uint16_t targetId) {
    // T95: `command` arrives here as a caller-supplied raw byte (currently
    // only ever a compile-time constant from ActionDispatcher's /pet
    // convenience wrappers -- see the plan's Review for the reachability
    // finding). EQT::PetCommand has a fixed uint32_t underlying type, so an
    // out-of-range static_cast is not technically UB the way the plain
    // MovementMode/PositionState casts below are -- but it still silently
    // sends a meaningless command byte to the server, which is exactly the
    // caller-bug-hiding failure mode this unit exists to close. Validate
    // against the actual (non-contiguous) enumerator set, not a min/max
    // range: PetCommand has gaps (no 0, no 15, no 22+).
    if (!isValidPetCommand(command)) {
        LOG_ERROR(MOD_COMBAT,
                  "sendPetCommand: {} is not a valid pet command byte "
                  "(valid: 1-14, 16-21 -- see EQT::PetCommand in "
                  "pet_constants.h); command rejected, nothing sent",
                  static_cast<int>(command));
        m_eq.AddChatSystemMessage(
            "Pet command " + std::to_string(static_cast<int>(command)) +
            " is not valid and was not sent.");
        return;
    }
    m_eq.SendPetCommand(static_cast<EQT::PetCommand>(command), targetId);
}

void EqActionHandler::dismissPet() {
    // PET_GETLOST = 29 from pet_constants.h
    m_eq.SendPetCommand(EQT::PET_GETLOST, 0);
}

// ========== Tradeskill ==========

void EqActionHandler::clickWorldObject(uint32_t dropId) {
    m_eq.SendClickObject(dropId);
}

// T316: ActionDispatcher::interactNearestWorldObject() used to return
// Failure("Use clickWorldObject with drop ID") unconditionally, so the
// interact-object keybind could never open a forge, a chest or any ground
// container. The dispatcher could not fix this itself: world objects are
// held in EverQuest::m_world_objects, not in GameState, so unlike doors
// there is nothing on m_state to query. This is that query.
//
// Range: kWorldObjectInteractRange mirrors the render-side default in
// IrrlichtRenderer::getNearestWorldObject() (50 units) rather than inventing
// a new number -- that is the client's existing notion of "the object you
// are standing at". The server re-validates the click regardless.
//
// Returns 0 for "nothing in range". eqemu allocates ground-spawn drop ids
// from 1, and WorldObject::drop_id defaults to 0, so 0 is never a real id.
uint32_t EqActionHandler::findNearestWorldObject(float& outDistance) const {
    constexpr float kWorldObjectInteractRange = 50.0f;

    const auto& player = m_eq.GetGameState().player();
    const float px = player.x();
    const float py = player.y();
    const float pz = player.z();

    uint32_t nearestId = 0;
    float nearestDistSq = kWorldObjectInteractRange * kWorldObjectInteractRange;

    for (const auto& [dropId, obj] : m_eq.GetWorldObjects()) {
        if (dropId == 0) continue;
        const float dx = obj.x - px;
        const float dy = obj.y - py;
        const float dz = obj.z - pz;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= nearestDistSq) {
            nearestDistSq = distSq;
            nearestId = dropId;
        }
    }
    if (nearestId != 0) {
        outDistance = std::sqrt(nearestDistSq);
    }
    return nearestId;
}

// T149 -- THE design point of this unit.
//
// object_type is the tradeskill container's type (17 = forge, 15 = oven, ...).
// It is NOT typed by the user: it is read off the container the server has
// told us is open. EverQuest::ZoneProcessClickObjectAction() records the
// container's drop_id in m_active_tradeskill_object_id when the server sends
// OP_ClickObjectAction with open==1, and T144 made
// WorldObject::object_type carry the real Titanium value (before T144 it was
// read past the end of an 8-byte-wrong struct and was 0 for every object in
// the world). Both accessors already existed on EverQuest --
// GetActiveTradeskillObjectId() and GetWorldObject() -- so no new cross-layer
// state was needed and none was invented.
//
// Scope note: m_active_tradeskill_object_id tracks WORLD containers only
// (forges, ovens, looms). Tradeskill containers carried in inventory
// (mixing bowls, sewing kits) open through a different path and would need
// the wire's `some_id` set to the container's item id; every sender here
// leaves some_id at 0, which is exactly what eqemu requires for a world
// combiner and wrong for an inventory one. Inventory containers are out of
// scope for this unit and are named as owed in the plan's Review.
bool EqActionHandler::resolveOpenContainerObjectType(uint32_t& objectTypeOut) {
    uint32_t dropId = m_eq.GetActiveTradeskillObjectId();
    if (dropId == 0) {
        m_eq.AddChatSystemMessage("You must have a tradeskill container open.");
        return false;
    }

    const eqt::WorldObject* obj = m_eq.GetWorldObject(dropId);
    if (obj == nullptr) {
        // The container is open per the server but we have no ground-spawn
        // record for it -- report rather than guessing a type.
        LOG_WARN(MOD_UI, "Open tradeskill container {} has no world object record", dropId);
        m_eq.AddChatSystemMessage("The open container is not recognized.");
        return false;
    }

    objectTypeOut = obj->object_type;
    return true;
}

void EqActionHandler::recipeSearch(const std::string& query, uint32_t minTrivial,
                                   uint32_t maxTrivial) {
    uint32_t objectType = 0;
    if (!resolveOpenContainerObjectType(objectType)) {
        return;
    }
    LOG_DEBUG(MOD_UI, "Recipe search in container type {} query='{}' trivial {}..{}",
              objectType, query, minTrivial, maxTrivial);
    m_eq.SendRecipesSearch(objectType, query, minTrivial, maxTrivial);
}

void EqActionHandler::recipeDetails(uint32_t recipeId) {
    // OP_RecipeDetails carries only the recipe id -- no object_type on the
    // wire (eqemu Handle_OP_RecipeDetails reads a bare uint32). Still gated
    // on an open container, because a details lookup outside one is
    // meaningless to the player and the ids come from a search reply.
    uint32_t objectType = 0;
    if (!resolveOpenContainerObjectType(objectType)) {
        return;
    }
    m_eq.SendRecipeDetails(recipeId);
}

void EqActionHandler::recipeAutoCombine(uint32_t recipeId) {
    uint32_t objectType = 0;
    if (!resolveOpenContainerObjectType(objectType)) {
        return;
    }
    m_eq.SendRecipeAutoCombine(objectType, recipeId);
}

void EqActionHandler::recipesFavorite(const std::vector<uint32_t>& recipeIds) {
    uint32_t objectType = 0;
    if (!resolveOpenContainerObjectType(objectType)) {
        return;
    }
    m_eq.SendRecipesFavorite(objectType, recipeIds);
}

// T278: unlike the four recipe requests above, neither of these carries an
// object_type on the wire, so neither goes through
// resolveOpenContainerObjectType() -- a combine or a close against a
// container we have no ground-spawn record for is still a legitimate
// request, and refusing it here would strand the player with a window they
// cannot close. The open-container gate they DO need lives in EverQuest
// (RequestTradeskillCombine / RequestCloseTradeskillContainer), which owns
// m_active_tradeskill_object_id and pushes the kept intents.
void EqActionHandler::tradeskillCombine(int16_t containerSlot) {
    m_eq.RequestTradeskillCombine(containerSlot);
}

void EqActionHandler::closeTradeskillContainer() {
    m_eq.RequestCloseTradeskillContainer();
}

// T153: /appearance. SendFaceChange() had zero production callers, so nothing
// had ever exercised it; this is the wiring, and the merge below is the reason
// it needs a handler rather than going straight from CommandProcessor.
//
// The wire struct is all-or-nothing -- eqemu's Handle_OP_FaceChange
// (zone/client_packet.cpp:6387) copies all seven fields into the PlayerProfile
// and Save()s -- so any field the user did not name must be re-sent at its
// current value or it is silently reset.
//
// T167: the base is now the PLAYER PROFILE, which is the same storage eqemu
// is about to overwrite, rather than the player's Entity.
//   * It is the only source for eyecolor1/eyecolor2 at all. Entity has no
//     eyecolor fields, so before T167 both eyes were reset to 0 by every
//     /appearance -- the bug that motivated this unit.
//   * It is also the CORRECT source for the other five. Entity's copies are
//     the current VISUAL, which an illusion overwrites
//     (EverQuest::ZoneProcessIllusion, eq_entities.cpp). Merging from Entity
//     while illusioned would
//     persist the illusion's hair and beard into the character's saved
//     profile; merging from the profile cannot.
// Entity remains the fallback for its five fields when no profile has been
// parsed, which preserves T153's behaviour on that path.
//
// If NEITHER source is available the request is REFUSED rather than sent as a
// fabricated all-zero body. Zero is a legal value for every field, so an
// unknown appearance and a real all-zero one are indistinguishable, and
// guessing wrong here permanently rewrites the character on the server.
void EqActionHandler::setAppearance(const action::AppearanceChange& change) {
    EQT::ProfileAppearance base = m_eq.GetProfileAppearance();

    if (!base.valid) {
        const auto& entities = m_eq.GetEntities();
        auto self = entities.find(m_eq.GetEntityID());
        if (self == entities.end()) {
            LOG_WARN(MOD_MAIN,
                     "setAppearance: no PlayerProfile parsed and no self "
                     "entity -- refusing to send a fabricated appearance");
            m_eq.AddChatSystemMessage(
                "Your appearance is not known yet -- try again after you have "
                "fully zoned in.");
            return;
        }
        base.haircolor = self->second.haircolor;
        base.beardcolor = self->second.beardcolor;
        base.hairstyle = self->second.hairstyle;
        base.beard = self->second.beard;
        base.face = self->second.face;
        // eyecolor1/eyecolor2 stay 0: nothing on this path knows them.
        LOG_WARN(MOD_MAIN,
                 "setAppearance: no PlayerProfile parsed; merging from the "
                 "self entity, so eye colors will be sent as 0");
    }

    if (change.haircolor) base.haircolor = *change.haircolor;
    if (change.beardcolor) base.beardcolor = *change.beardcolor;
    if (change.eyecolor1) base.eyecolor1 = *change.eyecolor1;
    if (change.eyecolor2) base.eyecolor2 = *change.eyecolor2;
    if (change.hairstyle) base.hairstyle = *change.hairstyle;
    if (change.beard) base.beard = *change.beard;
    if (change.face) base.face = *change.face;

    m_eq.AddChatSystemMessage(fmt::format(
        "Requesting appearance: haircolor={} beardcolor={} eyecolor1={} "
        "eyecolor2={} hairstyle={} beard={} face={}",
        base.haircolor, base.beardcolor, base.eyecolor1, base.eyecolor2,
        base.hairstyle, base.beard, base.face));

    m_eq.SendFaceChange(base.haircolor, base.beardcolor, base.eyecolor1,
                        base.eyecolor2, base.hairstyle, base.beard, base.face);

    // Handle_OP_FaceChange applies all seven fields and Save()s with no
    // rejection path for a correctly-sized body (verified in eqemu source;
    // T155 confirmed the DB write live), so what we just sent IS the profile
    // now. Recording it keeps a SECOND /appearance from reverting the first
    // -- without this, the eyecolors this unit exists to preserve would only
    // survive one command per zone-in.
    base.valid = true;
    m_eq.SetProfileAppearance(base);
}

// T171: /inspectmessage. Three shapes, one entry point: show (nullopt), clear
// (empty string), set (anything else).
//
// The showing is done here rather than in CommandProcessor because only the
// handler can see EverQuest's copy -- the same reason setAppearance() needs a
// handler at all.
void EqActionHandler::setInspectMessage(const std::optional<std::string>& message) {
    if (!message) {
        const std::string& current = m_eq.GetInspectMessage();
        if (current.empty()) {
            m_eq.AddChatSystemMessage(
                "Your inspect message is empty. Set one with "
                "/inspectmessage <text>.");
        } else {
            m_eq.AddChatSystemMessage(
                fmt::format("Your inspect message: {}", current));
        }
        return;
    }

    m_eq.SetInspectMessage(*message);
    m_eq.SaveInspectMessage();

    // Report what was actually stored, not what was asked for: SetInspectMessage
    // truncates at the server's 255-byte limit and the user should see that.
    const std::string& stored = m_eq.GetInspectMessage();
    if (stored.empty()) {
        m_eq.AddChatSystemMessage("Inspect message cleared.");
    } else {
        m_eq.AddChatSystemMessage(fmt::format("Inspect message set: {}", stored));
    }
}

} // namespace eqt
