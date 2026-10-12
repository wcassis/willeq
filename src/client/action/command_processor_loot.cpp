// T268 -- the loot window commands, split out of command_processor.cpp
// (which was at check_file_sizes.sh's 3000-line FAIL ceiling; the ceiling is
// a per-file rule, so a new TU satisfies it without a new binary).
//
// /loot reads its argument in the context of the loot window -- with one
// open a number is a SLOT and a word is an ITEM NAME; with none open a
// number is a corpse id (T34's form) and a word resolves the nearest corpse.
// The window-open state is PlayerState::isLooting(), written by
// EverQuest::RequestLootCorpse on every open path, so a scripted /loot and a
// shift+click gesture see the same thing.

#include "client/action/command_processor.h"
#include "client/state/game_state.h"
#include "client/corpse_predicate.h"  // T268: the ONE corpse predicate (T173)
#include "client/inventory/inventory_constants.h"  // B810 (#199): the corpse table

#include <algorithm>
#include <cctype>
#include <string>

namespace eqt {
namespace action {

ActionResult CommandProcessor::cmdLoot(const std::string& args) {
    // T34: opening dispatches to m_dispatcher.lootCorpse(corpseId) -- the exact
    // same entry point the renderer's shift+click overlay callback uses
    // (InputActionBridge::processLootRequests -> ActionDispatcher::lootCorpse),
    // so a scripted /loot and a live shift+click enter CombatManager::
    // LootCorpse() through identical last hops.
    //
    // T268: the argument is read in the context of the loot window:
    //   - window OPEN:  /loot <slot>       takes that corpse slot
    //                   /loot <item name>  takes the first item matching
    //   - no window:    /loot <corpseId>   opens that corpse (T34 form)
    //                   /loot <name>       opens the nearest corpse whose
    //                                      name contains <name>
    // The window-open state is PlayerState::isLooting(), written by
    // EverQuest::RequestLootCorpse for every open path.
    if (args.empty()) {
        return ActionResult::Failure(
            "Usage: /loot <corpseId|corpse name>; with a loot window open: /loot <slot|item name>");
    }

    // T178: every refusal echoes the offending token. The server's own failure
    // ("Error: OP_LootRequest: Corpse not found (ent = 0)") hardcodes "0" and
    // never names the id we asked for, so a decayed corpse and a bad id look
    // identical. The client cannot change that string, but it can (a) name the
    // id in its OWN validation refusals and (b) echo the id it is requesting,
    // so the server's generic error is at least attributable.
    long number = -1;
    try {
        size_t consumed = 0;
        long parsed = std::stol(args, &consumed);
        if (parsed >= 0 && consumed == args.size()) {
            number = parsed;
        }
    } catch (const std::exception&) {
        number = -1;
    }

    const auto& player = m_state.player();
    if (player.isLooting()) {
        if (number >= 0) {
            if (number > 0x7FFF) {
                return ActionResult::Failure("Invalid loot slot '" + args + "'");
            }
            // B810 (#199): with a window open the number is a SERVER slot
            // (inventory.slot_id, B796's rule), converted to the model's
            // corpse slot over the CORPSE table -- not the possessions
            // parseUserSlot(), which would also convert the looter's own
            // gear (server 0-22) that a corpse never holds.
            int16_t corpseSlot = eqt::inventory::SLOT_INVALID;
            const auto parse = eqt::inventory::parseCorpseSlot(args, corpseSlot);
            if (parse == eqt::inventory::UserSlotResult::NotANumber) {
                return ActionResult::Failure("Invalid loot slot '" + args + "'");
            }
            if (parse == eqt::inventory::UserSlotResult::NotAddressable) {
                return ActionResult::Failure(
                    eqt::inventory::describeUnaddressableCorpseSlot(args));
            }
            return m_dispatcher.lootItem(player.lootingCorpseId(), corpseSlot);
        }
        return m_dispatcher.lootItemByName(args);
    }

    if (number >= 0) {
        if (number > 0xFFFF) {
            return ActionResult::Failure(
                "Invalid corpse id '" + args + "' - use '/list' to find corpse IDs");
        }
        auto result = m_dispatcher.lootCorpse(static_cast<uint16_t>(number));
        if (result.success) {
            displayMessage("Requesting loot from corpse " + std::to_string(number) + "...");
        }
        return result;
    }

    // Name form: corpses only (the /target resolver ranks LIVING entities
    // above their own corpse, the opposite of what looting wants), nearest
    // first, case-insensitive substring like /target.
    std::string want = args;
    for (auto& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::replace(want.begin(), want.end(), ' ', '_');
    const eqt::state::Entity* best = nullptr;
    float bestDist = 0.0f;
    for (const auto& [id, e] : m_state.entities().getAllEntities()) {
        // T173's ONE corpse predicate (flag, npc type, or the name form).
        if (!IsCorpseByFields(e.isCorpse, e.npcType, e.name)) continue;
        std::string have = e.name;
        for (auto& c : have) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (have.find(want) == std::string::npos) continue;
        float dx = e.x - player.x(), dy = e.y - player.y(), dz = e.z - player.z();
        float d = dx * dx + dy * dy + dz * dz;
        if (!best || d < bestDist) {
            best = &e;
            bestDist = d;
        }
    }
    if (!best) {
        return ActionResult::Failure(
            "No corpse found matching '" + args + "' - use '/list' to find corpse IDs");
    }
    auto result = m_dispatcher.lootCorpse(best->spawnId);
    if (result.success) {
        displayMessage("Requesting loot from " + best->name + " (" + std::to_string(best->spawnId) + ")...");
    }
    return result;
}

ActionResult CommandProcessor::cmdLootAll(const std::string&) {
    const auto& player = m_state.player();
    if (!player.isLooting()) {
        return ActionResult::Failure("You are not looting a corpse - /loot <corpseId|name> first");
    }
    return m_dispatcher.lootAll(player.lootingCorpseId());
}

ActionResult CommandProcessor::cmdLootClose(const std::string&) {
    if (!m_state.player().isLooting()) {
        return ActionResult::Failure("You are not looting a corpse");
    }
    return m_dispatcher.closeLootWindow();
}

}  // namespace action
}  // namespace eqt
