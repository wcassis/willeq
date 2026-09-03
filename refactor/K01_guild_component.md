# K01 — Pilot: `GuildComponent` + `ClientContext`

Batch: `refactor/K_component_extraction.md` (read §Mechanism first; this
plan only carries what is specific to the guild family). Runs ALONE — no
other K unit is dispatched until this Review is folded into the batch doc.
**Dispatch after K00 Leg B has merged** (the allowlist ratchet in step 6
needs `scripts/refactor/file_size_allowlist.txt` to exist).

## Measured at `3808ebe9` (2026-09-03)

`src/client/eq_guild.cpp` is 266 lines, 11 `EverQuest::` bodies, one CMake
site (root `CMakeLists.txt`, the `src/client/eq_guild.cpp` line in the core
source list — grep for it, never cite the line).

| method | callers outside `eq_guild.cpp` (src/include) | tests |
|---|---|---|
| `SendGuildInvite` | `eq_action_handler_extended.cpp` | `test_guild_wire_rank.cpp` |
| `SendGuildInviteAccept` | `eq_action_handler_extended.cpp`, `action/action_handler.h` (a comment or signature — read it) | — |
| `SendGuildRemove` | `eq_action_handler_extended.cpp` | `test_guild_wire_rank.cpp` |
| `SendGuildDemote` | `eq_action_handler_extended.cpp` | — |
| `SendGuildLeader` | `eq_action_handler_extended.cpp` | — |
| `SendGetGuildMOTD` | `eq_action_handler_extended.cpp` | `test_wire_layout_t145.cpp` |
| `SendSetGuildMOTD` | `eq_action_handler_extended.cpp` | — |
| `ZoneProcessGuildInvite` | `eq_zone_dispatch.cpp` | — |
| `ZoneProcessGuildMOTDReply` | `eq_zone_dispatch.cpp` | — |
| `ZoneProcessGuildMemberUpdate` | `eq_zone_dispatch.cpp` | — |
| `ZoneProcessGuildMemberAdd` | `eq_zone_dispatch.cpp` | — |

Every one has an external caller, so every one keeps a one-line forwarder in
`eq.h`. Re-measure with `grep -rw` at your tip; the table is a lead.

Members referenced by `eq_guild.cpp`:

| member | refs | other families | decision |
|---|---|---|---|
| `m_has_pending_guild_invite` | 3 (+ inline `HasPendingGuildInvite()` in `eq.h`) | none | **moves** into the component; `HasPendingGuildInvite()` becomes a forwarder |
| `m_guild_invite_from` | 3 | none | **moves** |
| `m_guild_invite_id` | 3 | none | **moves** |
| `m_guild_id` | 9 | written by `eq_zone_entry.cpp` (`ZoneProcessPlayerProfile`) and `ZoneProcessSpawnAppearance`; read by `GetGuildID()` | **stays** in `EverQuest`; reached as `owner_.m_guild_id` |
| `m_guild_rank` | 1 | written by profile parse | **stays**; `owner_.m_guild_rank` |
| `m_character` | 7 | 11 families (core) | `ctx_.character` |
| `m_guild_name` | 0 in this TU | measure who reads it | stays; note it in the Review |

`QueuePacket` is an `EverQuest` method with 40+ callers — stays; the moved
bodies call `owner_.QueuePacket(...)`.

Text readers of `eq.h` in `tests/`: ten files (grep `eq\.h` in `tests/*.cpp`);
the one that parses it is `test_duplicate_state_stores.cpp`. The three moved
members are not state-module names, so its `kKnownRemaining` should be
unaffected — run it and say so.

## Steps

1. `include/client/client_context.h` — `struct ClientContext` with one
   reference member per core service, named without the `m_` prefix:
   `game_state`, `bridge`, `zone_connection`, `entities`, `character`,
   `combat_manager`, `audio_manager`, `trade_manager`, `spell_manager`,
   `inventory_manager`, `skill_manager`, `zone_connection_manager`,
   `command_registry`, `buff_manager`, `active_tradeskill_object_id`.
   Match each member's type to the `EverQuest` member exactly (a
   `std::unique_ptr<X>&` stays a reference to the unique_ptr; do not
   dereference in the context). `EverQuest` gains `ClientContext m_ctx;`
   constructed in the ctor initialiser list from its own members — read the
   ctor in `eq.cpp` and put `m_ctx` AFTER every member it references in the
   declaration order, or it dangles. `ClientContext& Context()` accessor.
2. `include/client/guild/guild_component.h` — `class GuildComponent`, ctor
   `(ClientContext&, EverQuest&)`, the 11 methods as public members with the
   same signatures, the 3 moved data members private. Include only what the
   moved bodies need.
3. `git mv src/client/eq_guild.cpp src/client/guild/guild_component.cpp`;
   bodies verbatim, `EverQuest::` → `GuildComponent::`, the substitutions
   from §Mechanism 4 only. Update the one CMake line.
4. `eq.h`: `friend class GuildComponent;`, `std::unique_ptr<GuildComponent>
   m_guild;` (constructed in the ctor after `m_ctx`), delete the 3 moved
   members, replace the 11 declarations + `HasPendingGuildInvite()` with
   one-line inline forwarders. The `// Guild functions` and `// Guild state`
   comment banners go with what they described.
5. `tests/test_guild_component.cpp` + its `add_executable` in
   `tests/cmake/packets_skills.cmake` next to `test_guild_wire_rank`:
   construct via `zonetest::MakeEq()` (see `zone_stream_test_utils.h`),
   reach the component through the accessor you add (`GuildComponent&
   Guild()`), and port TWO of `test_guild_wire_rank.cpp`'s cases to call the
   component directly (invite grants member rank; senders refuse when not in a
   guild). Leave `test_guild_wire_rank.cpp` untouched — it proves the
   forwarders.
6. Ratchet: set the `include/client/eq.h` entry in
   `scripts/refactor/file_size_allowlist.txt` to the new `wc -l` of `eq.h`.
   It must be lower than before; record both numbers.
7. Proof: multiset check per §Mechanism 8 with `ctx_.` / `owner_.` /
   `GuildComponent::` / `EverQuest::` normalised. Paste the residual.

## Territory

`include/client/client_context.h` (new), `include/client/guild/` (new),
`src/client/guild/` (new), `src/client/eq_guild.cpp` (moves),
`include/client/eq.h`, `src/client/eq.cpp` (ctor only), root
`CMakeLists.txt` (one line), `tests/test_guild_component.cpp` (new),
`tests/cmake/packets_skills.cmake`, `scripts/refactor/file_size_allowlist.txt`
(one number), `docs/module_map.md` (the guild row). Nothing else. If a moved
body needs anything not covered by §Mechanism 4, STOP.

## Verification

Full protocol bar. Step 6 is `check_file_sizes.sh --all` (K00 Leg B).
`test_duplicate_state_stores` and `test_reachability_sweep` must pass
unchanged; if either needs a file-list edit, STOP and report — the
orchestrator decides.

## Review

(executor: measured table vs the lead above with every difference named;
before/after `eq.h` line counts; multiset residual; ctest counts; what in
§Mechanism was wrong or missing — this is the pilot, corrections are the
deliverable.)
