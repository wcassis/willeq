# K13 — Context services: retire the transition handle where it is measured to be cheap

Batch: `refactor/K_component_extraction.md`. **Runs ALONE after K11 and K12
merge.** Not the end of the programme — it is the unit that turns the
`EverQuest&` transition handle from "everything" into a measured, boarded
residue.

## Measured at `233ff30b` (2026-09-03), after K09

`owner_.` appears **926 times** across the ten components. The targets
concentrate:

| target group | uses | becomes |
|---|---|---|
| `AddChatSystemMessage` / `AddChatCombatMessage` / `AddChatMissMessage` | ~155 | `ctx_.out.System(…)` etc. — a `ChatOutput` class owning the three bodies now stranded in `eq_chat.cpp` (K03 left them; 67 lines) |
| `MySpawnId`, `PosX/Y/Z`, `HeadingDeg`, `IsFullyZonedIn`, `CurrentZoneName/Id`, `SetPosLocal`, `SetHeadingLocal`, `SetMySpawnId`, `SetPosition`, `SetHeading`, `SetLoadingPhase` | ~430 | `ctx_.self` — a `PlayerFacade` class holding the K08 facade bodies (`eq_player_info.cpp`, 46 accessors) plus these; it wraps `game_state` and nothing else |
| `Zone()`, `Player()`, `Entities()`, `Merchant()`, `LoginWorld()`, `Movement()`, `Group()`, `Chat()`, `Guild()`, `Gm()`, `Bridge()`, `Commands()` | ~215 | `ctx_.<family>()` — the context carries a pointer to each sibling component, set by `EverQuest` after construction (components are constructed in dependency order; late-bound pointers avoid the order problem) |
| `QueuePacket` | 22 | `ctx_.net.Queue(op, pkt)` — a `PacketSender` wrapping `m_zone_connection` + the replay guard + `m_replay_suppressed_sends` (which then LEAVES `EverQuest`, closing K09's deviation 1) |
| `owner_.m_*` direct member reaches (`m_replay_suppressed_sends` 24, `m_pending_loot_slots` 14, `m_zone_bsp_tree` 9, `m_zone_map` 8, `m_world_objects` 8, `m_guild_id` 8, `m_eq_client_path` 6, `m_vendor_items` 5, `m_test_bypass_send_guard` 5, …) | ~100 | each is a K13 DECISION: move to the last family using it (§3), or to a context service, one line of rationale per member in the Review |
| the zone-environment set (`m_zone_map`, `m_zone_bsp_tree*`, `m_zone_fog_*`, `m_zone_sky_type`, `m_zone_type`) | ~30 | K11 measured: written by `ZoneComponent::ZoneProcessNewZone`, loaded/torn down inside `ZoneComponent`, read by one bridge body → **re-home into `ZoneComponent`** behind getters; `m_hotbar_model` and `m_zone_audio_manager` stay on `EverQuest` as ctor-wired subsystem handles |
| everything else | ≤ 50 | measured, listed, and BOARDED as the residue — with the friend line kept only for components that still have one |

The unit is done when: `owner_.` count is reported before and after; every
remaining use is in a table with its reason; `friend class` lines exist
only for components with a remaining `owner_.m_` use; the three services
have their own headers under `include/client/` and their own tests.

## Steps

1. `include/client/chat_output.h` + `src/client/chat_output.cpp`: move the
   three `AddChat*` bodies verbatim (they need `m_bridge` and the console
   path — read them). `EverQuest::AddChat*` become forwarders for the
   non-component callers (`application_commands.cpp`, `spell_manager.cpp`,
   `skill_manager.cpp`, `inventory_manager.h`, the action layer) — these
   are OUTSIDE the batch's no-forwarder rule because they are not
   EverQuest-family callers; K14 retargets them if Will wants the surface
   gone. Count them.
2. `include/client/player_facade.h` + `src/client/player_facade.cpp`: the
   46 K08 facade accessors + the setters above, verbatim. Same forwarder
   rule for non-component callers (combat, spells, skills: `GetPosition`
   alone has ~20).
3. `include/client/packet_sender.h`: `QueuePacket` + the replay guard +
   `m_replay_suppressed_sends` + `GetReplaySuppressedSendCount()`; the 49
   test call sites retarget to `eq.Net().ReplaySuppressedSendCount()`
   (call-site only).
4. `ClientContext` gains `out`, `self`, `net` and the twelve component
   pointers; `EverQuest`'s ctor sets the pointers after all components
   exist.
5. Mechanical rewrite in `src/client/*/` and `include/client/*/`:
   `owner_.AddChat` → `ctx_.out.`; the facade names → `ctx_.self.`;
   `owner_.<Family>()` → `ctx_.<family>()`; `owner_.QueuePacket` →
   `ctx_.net.Queue`. Code lines only (K01 correction 4).
6. Per-member decisions for the `owner_.m_*` reaches; execute the moves.
7. Remove `friend class` lines for components with zero remaining
   `owner_.m_` uses; remove the `EverQuest&` ctor parameter ONLY from
   components with zero remaining `owner_.` uses of any kind.
8. Tests: `test_chat_output.cpp`, `test_player_facade.cpp`,
   `test_packet_sender.cpp` — construct via `zonetest::MakeEq()`, one
   behaviour test per service. Existing suites unchanged in assertions.
9. Ratchet `eq.h`; predicted delta ≈ −(46 facade + 3 AddChat + QueuePacket
   + the replay counter) +(3 accessors) ≈ **−50 or better**.

## STOP conditions

- A service needs state that is not `game_state`, the bridge, or the zone
  connection → STOP; that is a design question.
- The `owner_.` residue after step 5 exceeds 150 → STOP and report the
  table; the plan's grouping was wrong.

## Territory

`include/client/client_context.h`, `include/client/chat_output.h`,
`include/client/player_facade.h`, `include/client/packet_sender.h` (new),
their `src/client/` TUs (new), `src/client/eq_chat.cpp` and
`src/client/eq_player_info.cpp` (shrink or vanish), `eq.h`, `eq.cpp`, every
`src/client/*/` component TU and `include/client/*/` component header
(the rewrite), root `CMakeLists.txt`, the three new tests + cmake, the
allowlist, `docs/module_map.md`, `refactor/BOARD.md` (the residue table's
board entry), and CALL-SITE-ONLY edits in the tests that read the replay
counter. Runs alone.

## After K13 (not this unit)

- **K14** — directory move: the remaining flat `src/client/eq_*.cpp`
  (`eq.cpp`, `eq_action_handler*.cpp`, `eq_strings.cpp`) into
  `src/client/core/` and `src/client/action/`; `eq_strings` → free
  functions in `eq_strings.h`.
- **K15** — the S2 leftovers: `InitGraphics(IrrlichtRenderer*, …)` parameter,
  `door_manager.cpp`'s `EverQuest::GetDebugLevel()`; delete `UCSConnection`
  (B005) and the dead members B019/B030/B031/B034/B035; the 14 dead renderer
  declarations B033.
- Then **Batch L**.
