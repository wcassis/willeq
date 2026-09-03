/*
 * T251: the one place a local cast refusal becomes a chat line. See
 * include/client/spell/cast_refusal.h and refactor/T251_missing_component_
 * message.md.
 */

#include "client/spell/cast_refusal.h"
#include "client/eq.h"
#include "client/inventory/inventory_manager.h"
#include "client/spell/spell_manager.h"

namespace EQ {

std::string castRefusalMessage(EverQuest& eq, CastResult result)
{
    const uint32_t string_id = castRefusalStringId(result);
    if (string_id == 0) {
        return "";
    }
    if (result == CastResult::ComponentMissing) {
        const SpellManager* spells = eq.GetSpellManager();
        const eqt::inventory::InventoryManager* inv = eq.GetInventoryManager();
        const uint32_t item_id = spells ? spells->getLastMissingReagentId() : 0;
        const eqt::inventory::ItemInstance* item =
            (inv && item_id != 0) ? inv->getItemById(item_id) : nullptr;
        if (item && !item->name.empty()) {
            return eq.Strings().GetFormattedStringMessage(STR_MISSING_NAMED_COMPONENT, {item->name});
        }
    }
    return eq.Strings().GetStringMessage(string_id);
}

} // namespace EQ
