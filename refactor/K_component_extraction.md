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

## Mechanism (every unit, identical)

1. **Context.** K01 creates a new header, `client_context.h`, under
   `include/client/`: `struct ClientContext` holding a reference (or pointer, matching the
   current member type) to each of the 15 core members. `EverQuest` owns one
   `ClientContext m_ctx` built in its ctor from its own members. No behaviour.
2. **Component class.** `include/client/<family>/<family>_component.h`,
   `class <Family>Component`, constructed with `(ClientContext&, EverQuest&)`.
   The `EverQuest&` is the **transition handle**: cross-family calls the
   moved code makes go through it, unchanged in spelling. `EverQuest` declares
   `friend class <Family>Component` for the transition. K-final removes both.
3. **Members move.** Every single-family `m_*` member moves into the
   component as-is. Core members (the 15) are reached as `ctx_.foo`. A member
   shared with another family but outside the core stays in `EverQuest` and
   is reached as `owner_.m_foo` through the transition friendship; the unit
   that extracts the LAST family using it moves it, and records the decision.
4. **Methods move.** Bodies move verbatim into `src/client/<family>/…` (the
   existing TU files `git mv`'d into the directory). The only edits inside a
   body are `m_x` → `ctx_.x` or `owner_.x`, and `Foo()` → `owner_.Foo()` for
   a method that stayed behind. **No other change to a moved body. No
   drive-by fixes.**
5. **Forwarders.** For each moved method that has a caller outside the family
   (measure with grep at the unit tip: `tests/`, `src/`, `include/`),
   `EverQuest` keeps a one-line inline forwarder `return m_<family>->Foo(…);`.
   Methods with no external caller are removed from `eq.h` outright.
6. **Ratchet.** `scripts/refactor/file_size_allowlist.txt` entry for `eq.h`
   is lowered to the new line count in the same commit. It must go down every
   unit.
7. **Tests.** Existing tests keep passing through the forwarders. Add
   `tests/test_<family>_component.cpp` with at least: constructs against a
   real `EverQuest` in headless mode, and one behaviour test per public
   method group that exercises the component directly (not via the
   forwarder). `test_duplicate_state_stores.cpp` scans `eq.h` as text: run it
   and, if it must also scan the new component header, extend its file list
   — that is territory, not scope creep (correction 27 shape).
8. **Proof.** `verify_move_only.py` will exit 1 by construction (the `ctx_.`
   / `owner_.` rewrites). Use the J-batch line-multiset check with those two
   substitutions normalised (`sed 's/ctx_\.\|owner_\.//g'` on both sides)
   and report the residual, which must be only the forwarders, the class
   scaffolding and the friend line.

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
| K11 | bridge-intents → bridge layer | K10 | different mechanism: these become `GameStateBridge`/intent handlers, not a component |
| K12 | commands → action handlers | K11 | different mechanism: each `/command` body becomes an `IActionHandler` method or is deleted if the registry already has it (T139/T223 deleted duplicates before — measure) |
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

- A moved body needs an edit beyond the two substitutions → STOP, report the
  method, do not improvise.
- A test asserts on `EverQuest` having a member that moved and cannot be
  repointed by name → STOP and report (that test may be a stale guard).
- `eq.h` does not shrink → STOP; the unit did nothing.

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
