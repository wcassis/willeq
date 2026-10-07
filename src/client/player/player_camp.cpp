// B1005 (GitLab #324): /camp -- PlayerComponent's half of the camp. The rule,
// measured on the official client, and its oracle rows are in
// include/client/player/camp_timer.h; this TU only turns CampTimer's effects
// into the sit, the packets and the eqstr lines, and routes the server's
// OP_LogoutReply (Character Select, or exit).
//
// Server authoritative: nothing here leaves the zone. The client sends
// OP_Logout at +30 s; the zone session ends only on the server's
// OP_LogoutReply, and the teardown runs deferred, outside the packet callback
// (MovementComponent::UpdateMovement, H03).

#include "client/player/player_component.h"

#include "client/display_eqstr.h"
#include "client/eq.h"
#include "client/core/memory_ledger.h"
#include "client/movement/movement_component.h"
#include "client/zone/zone_component.h"
#include "common/logging.h"
#include "common/packet_structs.h"

#include <chrono>

namespace {

double NowSeconds() {
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

namespace eqt {
namespace player {

namespace {

void ShowLines(ClientContext& ctx, const std::vector<CampLine>& lines) {
	for (const CampLine& l : lines) {
		const std::string text = l.arg.empty() ? eqt::EqStrLine(ctx.strings.Db(), l.eqstr)
		                                       : eqt::EqStrLine(ctx.strings.Db(), l.eqstr, {l.arg});
		if (!text.empty()) ctx.out.System(text);
	}
}

}  // namespace

void PlayerComponent::StartCampTimer(CampMode mode)
{
	const bool sitting = ctx_.game_state.player().positionState() == eqt::state::PositionState::Sitting;
	const CampEffects fx = m_camp.command(mode, NowSeconds(), sitting);
	// camp.tsv [1]: the client sits itself, then OP_Camp, in the same instant.
	if (fx.sit) ctx_.Movement().SetPositionState(POS_SITTING);
	if (fx.sendCamp) {
		eqt::memory::recordPoint("camp");   // T491 lifecycle point: the user typed /camp
		SendCamp(fx.campBody);
		LOG_DEBUG(MOD_MAIN, "Camp started (mode {}), OP_Logout in {} s", fx.campBody, kCampSeconds);
	}
	ShowLines(ctx_, fx.lines);
	ctx_.game_state.player().setCamping(m_camp.counting());   // the IsCamping() facade
}

void PlayerComponent::CancelCamp()
{
	// The character stood up (ApplyPositionState(POS_STANDING): /stand, or
	// the stand a move causes -- the latter unmeasured, camp_timer.h).
	ShowLines(ctx_, m_camp.stood().lines);
	ctx_.game_state.player().setCamping(m_camp.counting());
}

void PlayerComponent::UpdateCampTimer()
{
	if (!m_camp.counting()) return;
	const CampEffects fx = m_camp.tick(NowSeconds());
	ShowLines(ctx_, fx.lines);
	if (fx.sendLogout) {
		ctx_.game_state.player().setCamping(false);
		SendLogout();   // the server answers OP_LogoutReply (camp.tsv [7])
		LOG_INFO(MOD_MAIN, "Camp complete, OP_Logout sent");
	}
}

void PlayerComponent::SendCamp(uint32_t mode)
{
	// camp.tsv [3]: the body is a u32 -- 0 /camp, 1 /camp desktop, 2 /camp
	// server. (Before B1005 fable sent an EMPTY body; eqemu's Handle_OP_Camp
	// reads nothing from it either way.)
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt32(0, mode);
	ctx_.net.Queue(HC_OP_Camp, &packet);
}

void PlayerComponent::SendLogout()
{
	// OP_Logout is an empty packet -- body-only convention: QueuePacket(opcode,
	// pkt) prepends the opcode itself.
	EQ::Net::DynamicPacket packet;
	ctx_.net.Queue(HC_OP_Logout, &packet);
}

bool PlayerComponent::ConsumeReturnToCharacterSelect()
{
	const bool r = m_pending_return_to_select;
	m_pending_return_to_select = false;
	if (r) m_camp.reset();
	return r;
}

void PlayerComponent::ZoneProcessLogoutReply(const EQ::Net::Packet &p)
{
	(void)p;
	// T322: on a zone change the source zone answers our graceful zone-out
	// (OP_SaveOnZoneReq + OP_DeleteSpawn) with this same OP_LogoutReply --
	// eqemu Handle_OP_DeleteSpawn queues it before calling Disconnect().
	// That is an acknowledgement that we may now tear the session down, NOT
	// a logout. The flag is only ever set by BeginGracefulZoneOut().
	if (ctx_.Zone().ZoneOutHandshakePending()) {
		LOG_DEBUG(MOD_ZONE, "Source zone acknowledged graceful zone-out");
		ctx_.Zone().ZoneOutAcknowledged() = true;
		return;
	}

	// T322: a logout is not a zone transition; make sure a stale latch can
	// never make the next world login claim to be one.
	ctx_.Zone().ZoneToZoneInProgress() = false;
	ctx_.game_state.player().setCamping(false);

	// B1005 (#324): where the camp lands. /camp (and /camp server: fable has
	// no server-select screen) -> Character Select on a run that can show it;
	// /camp desktop, a headless run, or a reply that answers no camp at all
	// -> exit, exactly as before. A GM's camp is answered here straight away
	// (camp.tsv [16]; the official client shows "YOU HAVE BEEN DISCONNECTED."
	// for ~19.5 s first -- fable has no such screen and goes on at once).
	const CampPhase where = m_camp.logoutReply(m_camp_to_select);
	if (where == CampPhase::ToSelect) {
		LOG_INFO(MOD_MAIN, "Logout confirmed by server: back to character select");
		m_pending_return_to_select = true;
	} else {
		LOG_INFO(MOD_MAIN, "Logout confirmed by server");
		// D20c4: Signal quit via flag -- Application checks and exits
		ctx_.lifecycle.RequestQuit();
	}

	// H03: Don't call DisconnectFromZone() here -- this handler runs from inside
	// DaybreakConnectionManager::Tick()'s packet-processing loop; resetting the
	// zone connection manager here destroys the object whose Tick() is still on
	// the call stack (use-after-free, reproduced via GM fast-camp). Deferred to
	// UpdateMovement(), which also performs the return to character select.
	ctx_.lifecycle.m_pending_logout_disconnect = true;
}

}  // namespace player
}  // namespace eqt
