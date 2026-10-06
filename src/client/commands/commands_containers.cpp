// B899 (GitLab #215 / #151 Leg B) -- the CARRIED container window's game side.
//
// The official client opens a bag's window locally: there is no server
// session for a carried container (eqemu has no "open bag" opcode; a combine
// in a carried kit is OP_TradeSkillCombine with the bag's own slot, T278).
// So the game thread is the one truth for which bags are open, exactly as the
// server is for the world container:
//
//   /bag <general slot>  -> RequestToggleBag()       opens or closes it
//   Done / close box / ESC -> TradeskillCloseIntent{0, slot}
//                         -> CloseCarriedContainer()
//   the bag leaves its slot -> OnInventorySlotChanged() closes it
//
// Every open publishes TradeskillContainerOpened{isWorldObject=false} -- the
// producer #215 found missing ("a carried container opens no window") --
// plus a snapshot of the bag's cells as enriched InventorySlotChanged events,
// because the zone-in inventory never crossed the bridge for bag sub-slots
// (ZoneInSnapshotSlots() is 0..30 and the bank). Every close publishes
// TradeskillContainerClosed{wasWorldObject=false}. The renderer never opens or
// closes a window on its own (T290's rule).

#include "client/commands/commands_component.h"

#include "client/bridge/game_state_bridge.h"
#include "client/chat_output.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/state/event_bus.h"
#include "common/logging.h"

#ifdef WITH_AUDIO
#include "client/audio/ui_sounds.h"
#endif

#include <fmt/format.h>

namespace inv = eqt::inventory;

namespace {

uint8_t bagBit(int16_t generalSlot) {
	return static_cast<uint8_t>(1u << (generalSlot - inv::GENERAL_BEGIN));
}

}  // namespace

bool eqt::commands::CommandsComponent::IsBagOpen(int16_t generalSlot) const
{
	if (!inv::isGeneralSlot(generalSlot)) return false;
	return (m_open_bag_mask & bagBit(generalSlot)) != 0;
}

void eqt::commands::CommandsComponent::RequestToggleBag(int16_t generalSlot)
{
	if (!inv::isGeneralSlot(generalSlot)) {
		ctx_.out.System(fmt::format("{} is not a general inventory slot.",
			inv::describeSlot(generalSlot)));
		return;
	}
	if (IsBagOpen(generalSlot)) {
		CloseCarriedContainer(generalSlot);
		return;
	}
	if (!ctx_.inventory_manager) {
		ctx_.out.System("Cannot open the container - inventory not available.");
		return;
	}
	const inv::ItemInstance* item = ctx_.inventory_manager->getItem(generalSlot);
	if (!item) {
		ctx_.out.System(fmt::format("Nothing in {}.", inv::describeSlot(generalSlot)));
		return;
	}
	if (!item->isContainer()) {
		ctx_.out.System(fmt::format("{} ({}) is not a container.",
			item->name, inv::describeSlot(generalSlot)));
		return;
	}
	if (!ctx_.bridge) {
		ctx_.out.System("Cannot open the container - no bridge attached.");
		return;
	}

	m_open_bag_mask = static_cast<uint8_t>(m_open_bag_mask | bagBit(generalSlot));

	eqt::state::TradeskillContainerOpenedEvent open;
	open.isWorldObject = false;
	open.objectId = 0;
	open.inventorySlot = generalSlot;
	open.containerName = item->name;
	open.containerType = item->bagType;
	open.slotCount = item->bagSlots;
	open.iconId = item->icon;
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::TradeskillContainerOpened, std::move(open)));

	// The cells' current contents. Every later change to a sub-slot arrives
	// through the existing MoveItem / ItemPacket publishers.
	const int cells = item->bagSlots > inv::BAG_SLOT_COUNT ? inv::BAG_SLOT_COUNT
	                                                       : item->bagSlots;
	for (int i = 0; i < cells; ++i) {
		const int16_t sub = inv::calcBagSlotId(generalSlot, static_cast<int16_t>(i));
		const inv::ItemInstance* in = ctx_.inventory_manager->getItem(sub);
		eqt::state::InventorySlotChangedData data;
		data.slotId = sub;
		data.hasItem = (in != nullptr && in->itemId != 0);
		data.itemId = data.hasItem ? in->itemId : 0;
		data.itemName = data.hasItem ? in->name : "";
		data.iconId = data.hasItem ? in->icon : 0;
		data.quantity = data.hasItem ? static_cast<uint32_t>(in->quantity) : 0;
		data.bagSlots = data.hasItem ? in->bagSlots : 0;  // B442b
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
	}

	// T328 Leg B measured itemclth.WAV (145) on the official client for an
	// inventory bag opening AND closing.
#ifdef WITH_AUDIO
	ctx_.bridge->pushIntent(eqt::events::PlaySoundIntent{EQT::Audio::UISoundId::ITEM_CLOTH});
#endif
	LOG_DEBUG(MOD_INVENTORY, "B899: opened carried container in {} ('{}', {} slots, type {})",
		generalSlot, item->name, static_cast<int>(item->bagSlots), static_cast<int>(item->bagType));
}

void eqt::commands::CommandsComponent::CloseCarriedContainer(int16_t generalSlot)
{
	if (!IsBagOpen(generalSlot)) return;
	m_open_bag_mask = static_cast<uint8_t>(m_open_bag_mask & ~bagBit(generalSlot));
	if (!ctx_.bridge) return;

	eqt::state::TradeskillContainerClosedEvent closed;
	closed.wasWorldObject = false;
	closed.objectId = 0;
	closed.inventorySlot = generalSlot;
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::TradeskillContainerClosed, std::move(closed)));
#ifdef WITH_AUDIO
	ctx_.bridge->pushIntent(eqt::events::PlaySoundIntent{EQT::Audio::UISoundId::ITEM_CLOTH});
#endif
}

// Called from MerchantComponent::SyncInventorySlotToGameState(), which every
// InventorySlotChanged publisher calls with the slot it is about to announce.
// A change to an open bag's OWN general slot means the bag moved, was
// replaced or was deleted; its window describes a container that is no
// longer there, so it closes. A change to one of its sub-slots does not.
void eqt::commands::CommandsComponent::OnInventorySlotChanged(int16_t slot)
{
	if (IsBagOpen(slot)) CloseCarriedContainer(slot);
}

bool eqt::commands::CommandsComponent::ContainerHoldsAStack(int16_t containerSlot) const
{
	if (!ctx_.inventory_manager) return false;
	auto stacked = [this](int16_t slot) {
		const inv::ItemInstance* in = ctx_.inventory_manager->getItem(slot);
		return in != nullptr && in->itemId != 0 && in->quantity > 1;
	};
	if (containerSlot == inv::SLOT_TRADESKILL_EXPERIMENT_COMBINE) {
		for (int16_t s = inv::WORLD_BEGIN; s <= inv::WORLD_END; ++s) {
			if (stacked(s)) return true;
		}
		return false;
	}
	if (!inv::isGeneralSlot(containerSlot)) return false;
	for (int16_t i = 0; i < inv::BAG_SLOT_COUNT; ++i) {
		if (stacked(inv::calcBagSlotId(containerSlot, i))) return true;
	}
	return false;
}
