# T341 — The EverQuest transition-handle residue: retire clusters 1–4 (B038), close B039

Structural, class B. Slot2. Measured at `57aa74c5`: 131 `owner_.` uses
across the twelve `src/client/<family>/` components (K13 left 83; K14–K16
and the T-units added a few), eleven `friend class eqt::…` lines in
`include/client/eq.h`.

## Rulings per cluster (B038)

1. **Renderer-lifetime state** (`m_constrained_config`, `m_graphics_initialized`,
   `m_constrained_preset`, `m_loading_status_ptr`) — bridge-only readers →
   MOVE to `BridgeComponent` (it owns the graphics lifecycle); the four
   `everquest.cpp` bodies that read them go through `Bridge().accessor()`.
2. **The loading-phase state machine** (`SetLoadingPhase`, `m_loading_phase`,
   `IsGraphicsReady`) → MOVE to `BridgeComponent` as `LoadingPhase` state
   with the same publish; `EverQuest` keeps a one-line forwarder ONLY if
   `Application` calls it (name the caller).
3. **Lifecycle entry points** (`RequestQuit`, `TickNetwork`, `Start/StopUpdateLoop`,
   `OnGameStateComplete`, `ConnectToZone`, `SetSneak`, `m_update_running`,
   `m_pending_logout_disconnect`, `m_zone_session_established`) — these are
   `EverQuest`'s own job; components reach them via a small
   `ctx_.lifecycle` service (`ILifecycle`-shaped struct of bound
   `std::function`s or a reference to a `LifecycleFacade` class holding
   them — the K13 `PacketSender` shape). Record the choice.
4. **Members with a public accessor carrying external callers**
   (`m_guild_id`/`m_guild_rank` → `GuildComponent`, with `GetGuildID`/
   `GetGuildRank` forwarding; `m_last_tell_sender` → `ChatComponent`, with
   `GetLastTellSender` forwarding) — the forwarders stay because the ~30
   external call sites are tests and action handlers (name them); count
   them.
5. Whatever remains after 1–4 is re-censused and boarded as the new B038
   text (expect ≤ 40, all lifecycle).

Then the friend census (two axes: `owner_.m_` AND qualified private
names/nested types — K13 ii, L09 38) per component; delete the `friend`
line and the `EverQuest&` ctor parameter wherever BOTH are empty (expect
several: gm already; guild, chat, group after cluster 4?). B039 closes by
citing `refactor/K_component_extraction.md` §K13 corrections (ii), which
already states the two-axis rule — strike it.

Proof: the K/L multiset discipline for moved declarations; every existing
test with assertions untouched; `eq.h` ratchet (no allowlist — it must
simply stay under 1,500; report the count, expect ≈ 850); full bar
(graphics files unchanged → gate/ARM skipped with evidence).

STOP: a lifecycle method needs a thread change; a cluster-4 accessor's
external callers exceed 40 (then leave that member and say so).
