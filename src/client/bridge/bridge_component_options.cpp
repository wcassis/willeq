// bridge_component_options.cpp -- the game side of the Options window's
// server-owned settings (B430/B433).
//
// Three functions, and between them they are the whole game-thread half:
//
//   ApplyGameOption()           - a GameOptionIntent (or /option, through the
//                                 same intent): send it, then publish it
//   PublishGameOption()         - the ONE publisher of GameOptionChanged
//   PublishProfileGameOptions() - the player-profile values, read at zone-in
//
// The wire facts are in include/client/game_options.h. The SERVER is the
// authority for every one of these -- the renderer persists none of them and
// draws only what is published here.

#include "client/eq.h"
#include "client/bridge/bridge_component.h"
#include "client/eq_enums.h"
#include "client/game_options.h"
#include "client/entities/entities_component.h"
#include "client/zone/zone_component.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"

void eqt::bridge::BridgeComponent::PublishGameOption(eqt::GameOption option, int32_t value) {
	if (!ctx_.bridge) return;
	eqt::state::GameOptionChangedData data;
	data.option = option;
	data.value = value;
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::GameOptionChanged, std::move(data)));
}

void eqt::bridge::BridgeComponent::ApplyGameOption(const eqt::events::GameOptionIntent& i) {
	if (i.option == eqt::GameOption::ServerFilters) {
		ctx_.Zone().SetServerFilters(i.filters);
		LOG_INFO(MOD_UI, "Options: server filters updated");
		return;
	}
	const int type = eqt::gameOptionAppearanceType(i.option);
	if (type < 0) return;

	int32_t value = i.value;
	if (i.option == eqt::GameOption::AnonStatus) {
		// 0 normal / 1 anonymous / 2 roleplay -- the server keeps exactly one.
		if (value < 0 || value > 2) return;
		ctx_.game_state.player().setAnonymous(value == 1);
		ctx_.game_state.player().setRoleplay(value == 2);
	} else {
		value = value ? 1 : 0;
	}
	ctx_.Entities().SendSpawnAppearance(static_cast<uint16_t>(type), static_cast<uint32_t>(value));
	LOG_INFO(MOD_UI, "Options: game option {} = {}", static_cast<int>(i.option), value);
	PublishGameOption(i.option, value);
}

void eqt::bridge::BridgeComponent::PublishProfileGameOptions(uint8_t anon, uint32_t autosplit,
                                                              uint8_t consentGroup, uint8_t consentRaid,
                                                              uint8_t consentGuild) {
	PublishGameOption(eqt::GameOption::AnonStatus, anon <= 2 ? anon : 0);
	PublishGameOption(eqt::GameOption::AutoSplit, autosplit ? 1 : 0);
	PublishGameOption(eqt::GameOption::AutoConsentGroup, consentGroup ? 1 : 0);
	PublishGameOption(eqt::GameOption::AutoConsentRaid, consentRaid ? 1 : 0);
	PublishGameOption(eqt::GameOption::AutoConsentGuild, consentGuild ? 1 : 0);
}
