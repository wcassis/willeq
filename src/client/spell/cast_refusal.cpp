/*
 * T251: the one place a local cast refusal becomes a chat line. See
 * include/client/spell/cast_refusal.h and refactor/T251_missing_component_
 * message.md.
 */

#include "client/spell/cast_refusal.h"
#include "client/client_context.h"
#include "client/inventory/inventory_manager.h"
#include "client/spell/spell_manager.h"

namespace EQ {

std::string castRefusalMessage(eqt::ClientContext& ctx, CastResult result)
{
    const uint32_t string_id = castRefusalStringId(result);
    if (string_id == 0) {
        return "";
    }
    if (result == CastResult::ComponentMissing) {
        const SpellManager* spells = ctx.spell_manager.get();
        const eqt::inventory::InventoryManager* inv = ctx.inventory_manager.get();
        const uint32_t item_id = spells ? spells->getLastMissingReagentId() : 0;
        const eqt::inventory::ItemInstance* item =
            (inv && item_id != 0) ? inv->getItemById(item_id) : nullptr;
        if (item && !item->name.empty()) {
            return ctx.strings.GetFormattedStringMessage(STR_MISSING_NAMED_COMPONENT, {item->name});
        }
    }
    return ctx.strings.GetStringMessage(string_id);
}

} // namespace EQ
