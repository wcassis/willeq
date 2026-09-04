#pragma once

// T341 (refactor/T341_everquest_residue_pass.md), B038 cluster 3.
//
// LifecycleFacade is the FIFTH context service: the process/session lifecycle
// entry points that are EverQuest's own job and belong to no family, plus the
// three lifecycle FLAGS its update loop owns.
//
// K13 left twenty `owner_.` reaches here -- RequestQuit (7), TickNetwork (3),
// m_pending_logout_disconnect (3), StartUpdateLoop / StopUpdateLoop /
// OnGameStateComplete / ConnectToZone / SetSneak / m_update_running /
// m_zone_session_established (1 each) -- spread across five families that all
// reached them through the transition handle. They are not a family's state
// and their bodies are entangled with EverQuest's connection setup, its
// callback wiring and its destructor, so this service does NOT hold the
// bodies (unlike eqt::ChatOutput / PlayerFacade / PacketSender / StringDb,
// which do). It holds a BOUND CALL to each, and a reference to each flag.
//
// The plan (T341 ruling 3) offered two shapes: "an ILifecycle-shaped struct
// of bound std::functions, or a reference to a LifecycleFacade class holding
// them -- the K13 PacketSender shape". The FIRST was chosen, for one reason:
// moving these seven bodies is not a mechanical move. ConnectToZone builds a
// DaybreakConnectionManager and installs four EverQuest member-function
// callbacks; StopUpdateLoop is called from ~EverQuest; TickNetwork pumps the
// login, world and zone connections that live on three different owners. A
// bound call changes no body at all, so this cluster costs ZERO behaviour
// risk and the whole unit's proof obligation for it is the call-site rewrite.
//
// Naming follows K13 correction (kk): each member is spelled exactly as the
// EverQuest method or member it stands for, so every retargeted call site is
// `owner_.` -> `ctx_.lifecycle.` and nothing else.
//
// This is an AGGREGATE for the same reason eqt::ClientContext is one
// (K13 correction (mm)): EverQuest brace-initialises it in its constructor's
// initialiser list, before any component exists.

#include <atomic>
#include <functional>

namespace eqt {

struct LifecycleFacade {
	// D20c4: sets EverQuest::m_quit_requested; Application polls
	// IsQuitRequested().
	std::function<void()> RequestQuit;
	// The poll-based network tick (login + world + zone connections).
	std::function<void()> TickNetwork;
	// T245: the update-loop flag's writers. Set at the first ClientUpdate,
	// cleared by CleanupZone / ~EverQuest.
	std::function<void()> StartUpdateLoop;
	std::function<void()> StopUpdateLoop;
	// The game-lifecycle "state ready" transition.
	std::function<void()> OnGameStateComplete;
	// EverQuest's own zone-connection setup (the ZoneOn* callbacks are its
	// private member functions).
	std::function<void()> ConnectToZone;
	// A public EverQuest body that itself drives the entities family's
	// SendSpawnAppearance.
	std::function<void(bool)> SetSneak;

	// The three lifecycle FLAGS, bound by reference under their own names.
	// m_update_running is also PlayerFacade's third IsFullyZonedIn() term --
	// same store, two readers, no copy (T288).
	std::atomic<bool>& m_update_running;
	// H03: server confirmed our OP_Logout; the disconnect is deferred to the
	// top of UpdateMovement().
	bool& m_pending_logout_disconnect;
	bool& m_zone_session_established;
};

}  // namespace eqt
