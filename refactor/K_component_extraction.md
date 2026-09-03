# Batch K — Component extraction from `class EverQuest`

**Approved by Will 2026-09-02 (J tracker §"WILL'S RULING") as a separate
programme; ordered as the priority 2026-09-03.** Functionality first,
delegability second, performance not a criterion. Private single-user
project: no compatibility, deployment or user-base concern exists.

## The problem, measured at `22b85f8c` (2026-09-03)

Batch J put every `.cpp` under 1,500 lines. It did not change what an agent
must hold in its head to touch game logic:

| | `class EverQuest` |
|---|---|
| header | `include/client/eq.h`, 2,025 lines, one class |
| member functions declared | 685 |
| data members | 178 (163 `m_*` parsed) |
| definitions | 572 `EverQuest::` bodies across 38 flat `src/client/eq_*.cpp` files |
| includers | 90 |
| owned subsystems by pointer | 23 (3 connection managers, combat, trade, spell, buff, skill, inventory, hotbar, command registry, audio ×2, pathfinder, BSP/PFS/WLD handles) |

Layer boundaries DID land (one `state::GameState`; the renderer reads only via
`GameStateBridge`; `test_duplicate_state_stores.cpp` pins the overlap to one
documented exception). What never happened is decomposition *inside* the
class. The 38-file split is a god class wearing 38 coats.

### Data-member coupling (measured, `m_*` vs the TU family that references it)

| shape | members |
|---|---|
| used by exactly one family | 66 |
| used by two families | 49 |
| used by three or four | 24 |
| used by five or more (the shared-service core) | 15 |
| unused in any `eq_*.cpp` | 9 |

The 15-member core, in descending breadth: `m_game_state` (13 families),
`m_bridge` (12), `m_zone_connection` (11), `m_entities` (11), `m_character`
(11), `m_combat_manager` (9), `m_audio_manager` (8), `m_trade_manager` (7),
`m_spell_manager` (7), `m_inventory_manager` (7), `m_skill_manager` (6),
`m_zone_connection_manager`, `m_command_registry`, `m_buff_manager`,
`m_active_tradeskill_object_id` (5 each).

That is the extraction shape: **a context of shared services that every
component receives by reference, plus per-family private state that moves
into the component.**

## Families (the existing TU split already names them)

| family | TUs | `EverQuest::` defs | single-family members | notes |
|---|---|---|---|---|
| guild | `eq_guild` | 12 | 3 | pilot; three open guild defects land here later |
| gm | `eq_gm` | 12 | 2 | |
| chat | `eq_chat` (+ `eq_strings` 16, free-function candidates) | 13 | 1 | |
| group | `eq_group` | 25 | 5 | |
| merchant | `eq_merchant`, `_trade`, `_vendor_bank` | 56 | 1 | leans on trade/inventory managers |
| movement | `eq_movement`, `_state`, `_update` | 44 | 4 | heaviest `m_game_state` user (220 refs) |
| entities | `eq_entities`, `_appearance`, `_updates`, `_world` | 28 | 1 | `m_entities` owner candidate |
| player | `eq_player_info`, `_misc`, `_social`, `_state`, `_buffs`, `eq_profile_appearance` | 142 | 11 | largest; split into two components if a 2-cluster cut is obvious |
| zone | `eq_zone`, `_dispatch`, `_entry`, `_handshake` | 41 | 17 | `DispatchZonePacket` 880 lines stays whole (Batch G) |
| login_world | `eq_login_world` | 42 | 9 | owns login/world connection managers |
| bridge | `eq_bridge`, `_intents`, `_ui_audio` | 46 | 8 | belongs to the bridge layer, not a component |
| commands | `eq_commands`, `_movement`, `_spells`, `_train` | 32 | 0 | legacy `/command` bodies; belong in action handlers |
| combat | `eq_combat_actions` | 6 | 1 | fold into whichever component owns attack |
| core | `eq.cpp` | 38 | 3 | ctor/dtor, wiring, `Test_*` seams — stays |
| action_handler | `eq_action_handler*` | 19 | 0 | `EqActionHandler` forwarders; retargeted in K-final only |

## Mechanism (every unit, identical) — REVISED after the K01 pilot (2026-09-03)

1. **Context.** `include/client/client_context.h` (created by K01):
   `struct eqt::ClientContext` holding a reference to each core member, named
   without the `m_` prefix (`m_character` → `character`). **14 unconditional
   members + `audio_manager` under `#ifdef WITH_AUDIO`** (K01 correction 1).
   `EverQuest` owns `ClientContext m_ctx`, built in the ctor initialiser list
   (the ctor had none before K01; reference members force one) and declared
   AFTER every member it references. Accessor `Context()`.
2. **Component class.** `include/client/<family>/<family>_component.h`,
   `class eqt::<family>::<Family>Component`, ctor `(ClientContext&, EverQuest&)`.
   The `EverQuest&` is the **transition handle** for cross-family calls and for
   shared non-core members; `EverQuest` declares `friend class` for it. K-final
   removes both. `EverQuest` exposes `<Family>Component& <Family>()`.
3. **Members move.** Single-family `m_*` members move into the component.
   Core members are `ctx_.foo`. A member shared with another family but outside
   the core stays in `EverQuest` as `owner_.m_foo`; the unit extracting the
   LAST family using it moves it. A member with ZERO references tree-wide is
   not moved: it is boarded as dead (K01 found `m_guild_name`).
4. **Methods move.** Bodies move verbatim into `src/client/<family>/…` (`git mv`
   of the existing TU). Exactly THREE substitutions are allowed inside a body:
   `m_x` → `ctx_.x` / `owner_.m_x`; `Foo()` → `owner_.Foo()` for a method
   that stayed behind; `s_debug_level` (and any other `EverQuest` static) →
   `EverQuest::s_debug_level` (friendship grants access, not scope — K01
   correction 2). **Apply substitutions to code lines only; a regex pass over
   the whole file rewrites historical comments that quote old code** (K01
   correction 4 — it falsified a 30-line note before being caught).
5. **No forwarders.** Every caller of a moved method is retargeted to the
   component through the accessor (`eq.Guild().SendGuildInvite(…)`), and the
   declaration is deleted from `eq.h`. Callers live mostly in
   `eq_action_handler*.cpp` and `eq_zone_dispatch.cpp`; those call sites, and
   the call sites in `tests/`, are in every unit's territory FOR CALL-SITE
   EDITS ONLY (assertions untouched). A method with zero callers is deleted
   outright (§5 wins over any plan table that says otherwise). Forwarders were
   the K01 pilot's first shape and cost +8 lines: they keep `eq.h` as the API
   surface, which is the thing this batch exists to remove.
   **From Wave 2 on, callers are found by the COMPILER, not by grep:** delete
   the declarations from `eq.h`, build, retarget every site the error list
   names, rebuild until clean, then grep `tests/` once for stragglers. Word
   greps are inflated by name collisions (`Move`, `Face`, `Jump`, `Follow`
   exist on four other classes) and are only a lead.
6. **Ratchet.** `scripts/refactor/file_size_allowlist.txt`'s `eq.h` entry is
   lowered to the new `wc -l` in the same commit. **The plan must PREDICT the
   delta before dispatch** from: −(declarations deleted) −(members moved)
   −(comment banners that go with them) +2 (accessor + member) +1 (friend)
   +1 (include). K01 paid the one-time scaffolding (`m_ctx`, `Context()`,
   `private:` reopen). Stop condition: the prediction is ≤ 0 or the unit does
   not dispatch.
7. **Tests.** `tests/test_<family>_component.cpp` constructs via
   `zonetest::MakeEq()` and exercises the component directly; existing tests
   are retargeted at the call site. `test_duplicate_state_stores.cpp` and
   `test_reachability_sweep.cpp` scan `eq.h` as text — run them; K01 needed no
   file-list edit, but a later family might (STOP and report if so). Any test
   that reads the moved TU by filename is repointed in-unit (K03 c).
**K03 corrections (2026-09-03):** (a) "zero callers" means zero INCLUDING
   in-family callers — `ZoneSendEmote` had one in-family caller and a plan
   table headed "callers outside the family" nearly deleted `/emote`;
   (b) a caller table must record the RECEIVER EXPRESSION, not the name —
   half of K03's lead sites were `UCSConnection::SendChatMessage` and
   `Application::DispatchSlashCommand`; (c) **grep `tests/` for the moved
   TU's FILENAME as a string before moving it** — `test_chat_channel_names`
   read `eq_chat.cpp` as text and would have kept passing over an empty
   guard; (d) an inline `eq.h` accessor on a moving member is part of the
   move and appears in no method table; (e) re-derive the include set for
   BOTH sides of a split TU.
**K04 corrections (2026-09-03):** (f) **INLINE ACCESSORS in `eq.h` that read
   a moving member are part of the move and are invisible to every
   "count `EverQuest::` bodies" measurement** — K04 found six (`IsInRaid`,
   `GetRaidLeader`, …) carrying 20 external call sites the plan never saw.
   Every plan from K05 on must list them: grep `eq.h` for each moving member
   and collect the inline functions that touch it; (g) caller tables must be
   comment-filtered — four of K04's leads were prose; (h) a member the plan
   names may no longer exist (two of K04's seven survived only in comments);
   (i) order the normalisation sed longest-name-first when names share a
   prefix.
**K05 corrections (2026-09-03):** (j) the third substitution covers ANY
   `EverQuest`-scoped name — statics AND nested types (`GameLootItem` →
   `EverQuest::GameLootItem`); (k) `ReachabilitySweep.EverySenderHasACaller`
   parses `eq.h` for `Send*` and has a floor of 50 — it is a cumulative
   Batch-K budget and K05 exhausted it; the test now also walks
   `include/client/*/*_component.h`, so later units need no edit but WILL
   conflict with that fix if they branched before `a8886739`; (l) a
   "(was …)" note naming an old file path in `module_map.md` is a live citation —
   write bare filenames in prose.
**K06/K07 corrections (2026-09-03):** (m) a component pointer may not
   reuse an existing member name (`m_entities` is the map, so the component
   is `m_entities_component`); (n) **fourth substitution: `this` →
   `&owner_`** where a moved body uses `this` as an identity key
   (function-static `std::map<EverQuest*,…>` throttle tables — 20 sites in
   movement); (o) a recorded keep decision (the T316 `Start*` note) overrides
   §5's zero-caller delete — cite it in the Review; (p) member greps must be
   string-filtered as well as comment-filtered; (q) `EverQuest::` body counts
   miss overloads; (r) **cross-family retargets in a three-wide wave
   CONFLICT at merge by construction** — unit A retargets a call inside a TU
   that unit B is moving. The orchestrator resolves every such hunk to
   `owner_.<Family>().Foo(…)`; executors should write that form themselves
   whenever the target family is already on `fable`, and `owner_.Foo()` only
   when it is not.
**K10 corrections (2026-09-03):** (s) grep `eq.h` for the family's NAME
   PREFIX, not just for bodies — K10 found two declarations defined nowhere
   (a dead API promise invisible to body counts and member greps); a
   tree-wide check after K10 found zero more; (t) `ucs.cpp`'s
   `m_host/m_port/m_key/m_dbid` are `UCSConnection`'s own — the third unit
   in a row where a "reader outside the family" was a same-named member of
   another class: a member lead is only real if the receiver is `EverQuest`;
   (u) a body moved from a TU OTHER than the family's (K10 took
   `ConnectToLogin`/`ConnectToWorld` from `eq.cpp`) must be proved by a
   residual run on that TU too.
**K08 corrections (2026-09-03):** (v) a non-`m_` file-scope or class
   constant a mover uses (`CAMP_TIMER_SECONDS`) travels with it — widen the
   member grep to every identifier the moved bodies reference that is
   declared in `eq.h`; (w) comment filters must strip TRAILING `//`
   comments, not only full-line ones; (x) **compute the whole retarget
   mechanically before the first build** — parse the `eq.h` declarations
   and the component headers, scan the movers for bare `identifier(`, and
   emit the substitution list; K08's 2,700-line move reached a clean build
   on the second compile. K09 should reuse that script (see K08's Review).
**K09 corrections (2026-09-03):** (y) **resolve zero-caller deletions
   BEFORE the member single/shared decision, then re-measure** — a deleted
   sender strands its only member (`m_zone_sequence`) and it would otherwise
   move into the component as dead state; (z) an unguarded dereference in a
   sender that every live sender guards is proof the sender is unreachable
   under the replay harness; (aa) `DuplicateStateStores` has the same
   cumulative-floor shape as `ReachabilitySweep` (K05 k) — it now also walks
   `include/client/**/*_component.h`; K11+ inherit it; (bb) a member whose
   test-facing accessor has dozens of call sites (`m_replay_suppressed_sends`,
   49 sites) stays on `EverQuest` under §3 — the K13 decision list now holds
   `m_inspect_message`, `m_zone_server_host/port`, `m_replay_suppressed_sends`,
   the player-state facade and the three `AddChat*` primitives.
**K11 corrections (2026-09-03):** (cc) a receiver-aware caller scan misses
   two-hop chains (`h.eq.Foo()`) — 22 sites in K11; only the compiler is
   complete; (dd) `this` → `&owner_` (correction n) must NOT be applied to a
   lambda capture `[this]` — three of K11's five `this` sites were captures,
   including the `std::visit` visitor that IS `ProcessBridgeIntents`;
   (ee) `test_reachability_sweep.cpp` reads TUs BY PATH and `readFile` on a
   missing path returns an empty buffer that passes — the K03 (c) grep of
   `tests/` for the TU filename is mandatory, not advisory.
**K12 corrections (2026-09-03):** (ff) **a zero-caller method may still be
   the SOLE PUBLISHER of a `GameEventType`** — deleting `runLoadDiagnostics`
   broke `ReachabilitySweep.EveryEventIsPublishedAndConsumed`; before
   deleting, grep the body for `pushEvent`/`GameEventType::` and keep it
   (private) if it publishes; (gg) tests may reach members through
   `#define private public` — the compiler catches it, retarget the site;
   (hh) a sonnet executor ended its turn waiting on a background build once
   — the dispatch prompt must repeat the protocol's "never end a turn on a
   wait" line verbatim and name the foreground timeout.
**K13 corrections (2026-09-03):** (ii) **the `friend class` test is TWO-AXIS.**
   §Mechanism 7's "remove the friend line when zero `owner_.m_` remain" is
   necessary but not sufficient: a component also names `EverQuest`'s PRIVATE
   STATICS by qualification (`EverQuest::FormatPrice`, `::DumpPacket`,
   `::s_debug_level` -- the third substitution, K05 (j)), which friendship
   grants and which no `owner_.` census can see. K13 removed merchant's and
   commands' lines on the one-axis rule and took 12 build errors. Remove the
   line only when BOTH axes are zero; `eqt::gm::GmComponent` is the first and
   so far only component that passes both, and it dropped the `EverQuest&`
   ctor parameter with it; (jj) **a FIFTH substitution shape, for a
   destination that is NOT a friend.** Every substitution in §Mechanism 4
   assumes the body lands in a friend class. A CONTEXT SERVICE
   (`eqt::ChatOutput`/`PlayerFacade`/`PacketSender`) is not one, so a private
   `EverQuest` name it uses must become a public equivalent:
   `s_debug_level` -> `EverQuest::GetDebugLevel()`, and
   `GetOpcodeName(` -> `EverQuest::GetOpcodeName(` with the declaration moved
   to `public:` (the T180/T271 standing for a pure static lookup). Name both
   in the destination file's banner; (kk) **name every service member after
   the `EverQuest` member it references** -- ChatOutput's `m_bridge`,
   PlayerFacade's `m_game_state`, PacketSender's five -- and the moved bodies
   need ZERO substitutions, which makes the §Mechanism 8 residual proof
   trivial and exact (K13's three sets came back 0/0/0 with no name mapping at
   all); (ll) `#define private public` test hacks (K12 (gg)) are now a
   STANDING caller class, not a curiosity -- K13 hit two more
   (`h.eq.m_world_objects`, `h.eq.m_zone_map`); (mm) an aggregate
   `ClientContext` may not have private data members, so late-bound component
   pointers are public with a comment saying why -- `EverQuest` brace-
   initialises `m_ctx` in its initialiser list.
**K13 corrections (2026-09-03):** (ii) the `friend` rule is TWO-AXIS —
   a component may name a private `EverQuest` static by qualification
   (`EverQuest::FormatPrice`, `s_debug_level`) with no `owner_.` in sight;
   the census that decides whether a friend line can go must count
   qualified private names too (BOARD B039); (jj) a non-friend destination
   reaches `s_debug_level` through `EverQuest::GetDebugLevel()`; (kk) a
   service that needs more than `game_state`/bridge/zone-connection is a
   recorded design fact, not a STOP, when the extra members are bound by
   reference under their own names exactly as `ClientContext` binds its
   core; (ll) `#define private public` test hacks are a standing caller
   class — only the compiler finds them.
**K14 corrections (2026-09-03):** (mm) a "pure lookup" lead must be
   checked for state reads before it becomes a free function
   (`DumpEntityAppearance` reads `m_entities`); (nn) the K13 forwarder
   files (`eq_chat.cpp`, `eq_player_info.cpp`) were NOT empty — a plan
   must not guess a file's state after a unit it has not read.
8. **Proof.** `verify_move_only.py` exits 1 by construction. Line-multiset
   check with the substitutions normalised BY NAME MAPPING (`ctx_.character`
   ↔ `m_character`, etc. — stripping the prefix alone leaves 7 false
   residuals, K01 correction 3). The scaffolding residual lands in `eq.h`,
   `eq.cpp` and the new header, NOT in the moved TU. Paste the residual.

## Unit table (one slot each; territory = the family's TUs + its new
directory + `eq.h` + `eq.cpp` ctor wiring + `tests/CMakeLists.txt`/`tests/cmake`)

| unit | family | serialise against | why this order |
|---|---|---|---|
| K01 | guild + `ClientContext` | — (pilot, run ALONE) | smallest family; establishes the mechanism and the executor corrections |
| K02 | gm | K01 | second-smallest; proves the mechanism on a family with `m_zone_connection` traffic |
| K03 | chat (+ decide `eq_strings` → free functions) | K01 | |
| K04 | group | K01 | |
| K05 | merchant | K04 (loot members shared with zone/entities: decide once) | |
| K06 | entities | K05 | `m_entities` ownership decision |
| K07 | movement | K06 | |
| K08 | player (may be two units) | K07 | largest; plan file must show the cut |
| K09 | zone | K08 | 17 private members; `DispatchZonePacket` moves whole |
| K10 | login_world | K09 | connection managers move with it |
| K11 | bridge → `BridgeComponent` | K09 | SAME mechanism (revised 2026-09-03: extract first, fold into the bridge layer later); keeps ≤5 lifecycle forwarders for `Application` |
| K12 | commands + combat_actions → `CommandsComponent` | K09 | SAME mechanism (revised 2026-09-03); the registry-merge into the action layer is a later measured unit. Runs alongside K11 |
| K13 | final: `EqActionHandler` retargets to components, forwarders with a single caller are inlined at the caller, `friend` lines and the `EverQuest&` handle removed, `src/client/eq_*.cpp` directory move completed, `module_map.md` regenerated | K12 | |

**After K01 merges, K02–K04 have disjoint territory and can run three-wide.**
Every plan after K01 is written AFTER reading K01's Review — the pilot's
corrections are the template, exactly as J01's were for Batch J.

The S2 leftovers (eight vestigial `irrlicht_renderer.h` includes in
`eq_player_*.cpp` / `eq_bridge_*.cpp` / `application_*.cpp`; the
`InitGraphics(IrrlichtRenderer*, …)` parameter; `door_manager.cpp`'s
`EverQuest::GetDebugLevel()`) are removed by whichever K unit owns the file
they sit in; the plan for that unit names them.

## Stop conditions (every unit)

- A moved body needs an edit beyond the three substitutions → STOP, report the
  method, do not improvise.
- A test asserts on `EverQuest` having a member that moved and cannot be
  repointed by name → STOP and report (that test may be a stale guard).
- The predicted `eq.h` delta is > 0 → the plan is wrong; do not dispatch. A measured delta > 0 after execution → STOP and report the arithmetic; never compress code to fit.

## Per-unit plan files

`refactor/K01_guild_component.md` … each carries: measured method list with
external-caller counts, member list with the single/shared decision per
member, the exact new file names, CMake add-sites (grep, never a line
number), text readers of `eq.h` in `tests/` and `scripts/` that must be
re-measured, predicted multiset residual, and the Review section.

## Batch L (not planned yet)

The same programme for `class IrrlichtRenderer` (441 methods, 282 data
members, 30 TUs). Its family cut is not as clean as EverQuest's because the
zone-load lifecycle is the largest cluster and is spread across many TUs.
Plan it after K04 has proven the mechanism three-wide.
