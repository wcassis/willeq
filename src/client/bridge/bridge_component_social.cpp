/*
 * B897 (GitLab #213) -- the friends/ignore lists' persistence tick and the
 * ONE publisher of GameEventType::SocialListsChanged. Its own TU (M00); the
 * intent handler (onSocialListIntent) lives in bridge_component_intents.cpp
 * because test_bridge_intent_table pins every intent handler there.
 */

#include "client/bridge/bridge_component.h"

#include "client/bridge/game_state_bridge.h"
#include "client/login_world/login_world_component.h"
#include "client/state/event_bus.h"
#include "client/state/game_state.h"

void eqt::bridge::BridgeComponent::SyncSocialLists() {
	auto& model = ctx_.game_state.social();
	if (!m_social_sync.tick(model, ctx_.character, ctx_.LoginWorld().ServerName())) {
		return;
	}
	if (!ctx_.bridge) return;
	eqt::state::SocialListsChangedData data;
	data.friends = model.friends();
	data.ignored = model.ignored();
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::SocialListsChanged, std::move(data)));
}
