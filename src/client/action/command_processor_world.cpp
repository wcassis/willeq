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
#include "client/action/command_processor_internal.h"  // J04: parseU32Strict / splitWhitespace

namespace eqt {
namespace action {

using detail::parseU32Strict;
using detail::splitWhitespace;

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

// T278: bare /combine is the OPEN WORLD container (forge on the ground);
// /combine <slot> is a container you are CARRYING (a Forge/Sewing Kit in a
// general slot). A stray argument that is not a general slot is refused and
// points at /recipecombine -- a player who typed "/combine 4321" meant a
// recipe id, and silently combining a container's real contents instead
// would consume the wrong items.
ActionResult CommandProcessor::cmdCombine(const std::string& args) {
    auto parts = splitWhitespace(args);
    if (parts.empty()) {
        return m_dispatcher.tradeskillCombine(eqt::inventory::SLOT_INVALID);
    }
    uint32_t slot = 0;
    if (parts.size() != 1 || !parseU32Strict(parts[0], slot) ||
        !eqt::inventory::isGeneralSlot(static_cast<int16_t>(slot))) {
        return ActionResult::Failure(
            "Usage: /combine (the open world container) | /combine <general slot "
            + std::to_string(eqt::inventory::GENERAL_BEGIN) + "-"
            + std::to_string(eqt::inventory::GENERAL_END)
            + "> (a container you are carrying) - did you mean /recipecombine <recipe_id>?");
    }
    return m_dispatcher.tradeskillCombine(static_cast<int16_t>(slot));
}

ActionResult CommandProcessor::cmdCloseContainer(const std::string& args) {
    if (!splitWhitespace(args).empty()) {
        return ActionResult::Failure("Usage: /closecontainer (no arguments)");
    }
    return m_dispatcher.closeTradeskillContainer();
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

// T273: raw arg string passed straight through, same idiom as cmdEquip
// above. Slot/name form dispatch, item resolution and the not-a-book
// refusal all live in EverQuest::RequestReadItem(), reached via
// m_dispatcher.readItem().
ActionResult CommandProcessor::cmdRead(const std::string& args) {
    return m_dispatcher.readItem(args);
}

// T276: same idiom again -- everything is in EverQuest::RequestScribeItem().
ActionResult CommandProcessor::cmdScribe(const std::string& args) {
    return m_dispatcher.scribeItem(args);
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

// T269: CommandProcessor::cmdBank (the /bank coin sub-commands) lives in
// command_processor_bank.cpp -- this TU is at check_file_sizes.sh's 3000-line
// FAIL ceiling, so substantial new code goes in a new TU.

// T280: cmdTrain/cmdTrainClose live in command_processor_train.cpp -- this
// TU is at check_file_sizes.sh's 3000-line FAIL ceiling (see the cmdBank
// note above).

} // namespace action
} // namespace eqt
