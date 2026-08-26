# T286 — sweep EverQuest's remaining duplicate/stray state stores (T280/T283 pattern)

Slot: slot3. x86. NO eqemu. Class A. Territory: `include/client/eq.h`
members and their `PlayerState`/`GameState` counterparts, the PlayerProfile
parse that feeds them, tests. Do NOT touch graphics (T284 slot2, T285
slot1), inventory.

## Pattern
T283 found `EverQuest::m_practice_points` duplicating
`PlayerState::m_practicePoints` (one stale reader, every user-visible read
going through the other). T280 found the same shape. The 2026-08-25
decision says state is written at the source into ONE instance — this unit
checks whether more of these exist.

## Method (mechanical, like T264's sweep)
1. For every scalar/string member of `EverQuest` that names player state
   (level, class, race, hp, mana, currency, flags, ids, zone, target...),
   find whether `PlayerState`/`CombatState`/`WorldState`/etc. holds the same
   value, and list: member | counterpart | writers of each | readers of each.
   T271 already found `m_class`/`m_level` being read where `GetClass()`/
   `GetLevel()` was meant (T276 fixed one instance) — start there.
2. For each duplicate: delete the `EverQuest` copy and route its readers to
   the state object, UNLESS the wire path genuinely needs a private staging
   value (say which and why).
3. A pinning test in the T264 style: parse the sources and assert the known
   duplicate set is empty (or exactly the justified exceptions), so this
   cannot silently regrow.

## Verification
Full ctest; Review with the table; evidence review/T286/. Commit:
`T286: player state has one store per value`.

---

## Review (executor, 2026-08-26)

Branch `task/T286-duplicate-stores` off fable `6ad62863`. x86, no eqemu.
Evidence: `/mnt/projects/fable/review/T286/` (README.md there indexes it;
`sweep_table.md` is the full row-by-row table this Review summarises).

### Scope: the sweep found 107 duplicate members. 99 deleted, 8 kept.

Method was the plan's: enumerate every scalar/string member of `EverQuest`
declared in `include/client/eq.h`, match it against the members of
`PlayerState` / `WorldState` / `CombatState` / `GroupState` / `PetState`,
then classify each writer and reader by parsing `src/client/eq*.cpp` +
`eq.h`. 107 pairs came out. They are NOT a handful of stragglers — every
stat, every currency and bank counter, the bind point, the flags, the
vendor/banker/trainer/loot ids, the zone-transition state, the whole group
domain and the pet were all held twice, with `// update both local members
and GameState during migration` written over the top of the PlayerProfile
parse. T280/T283's `m_practice_points` and T271/T276's `m_class`/`m_level`
were three samples of this one population.

| domain | members | outcome |
|---|---|---|
| stats, attributes, currency, bank, weight, bind point, last name | 34 | DELETED -> `PlayerState` |
| flags (sneak/AFK/anon/RP), camp timer | 6 | DELETED -> `PlayerState` |
| vendor / banker / trainer / looting corpse | 7 | DELETED -> `PlayerState` |
| target id, combat movement, last slain | 6 | DELETED -> `CombatState` |
| time of day, zone-line + pending-zone + zone-change, zone connected, client ready | 19 | DELETED -> `WorldState` |
| group membership, leader, member array + count, pending invite | 7 | DELETED -> `GroupState` |
| pet spawn id | 1 | DELETED -> `PetState` |
| movement flags: is-moving, animation, move target, speed, follow, WASD, jump, movement mode, position state, sequence, size | 19 | DELETED -> `PlayerState` |
| **position (x/y/z/heading), spawn id, character name, zone name/id** | **8** | **kept — see below** |

Plus, beyond the 107 scalars: `EverQuest::m_group_members` (a whole
`std::array<GroupMember,6>` shadowing `GroupState::m_members`), the second
`struct GroupMember` in `eq.h`, a second `MAX_GROUP_MEMBERS`,
`SyncGroupMemberToGameState()` (a hand-run converter between the two
arrays), `m_pet_button_states[10]`, `SetCurrentTargetId()`, and
`BankCoinPool()` — which handed out a raw `uint32_t*` **into** the duplicate
counter and so could not survive the deletion; it is now a
`BankCoinGet`/`BankCoinSet` pair.

### The eight kept, and why

The plan allows "the wire path genuinely needs a private staging value".
One of the eight is that; the other seven are a size/risk judgement, flagged
as a deviation rather than absorbed.

| kept | counterpart | reason |
|---|---|---|
| `m_character` | `PlayerState::m_name` | **genuinely a different value.** `m_character` is the login-time character name, fixed in the constructor and used by every packet sender (`GroupInvite_Struct::inviter_name` etc.). `PlayerState::m_name` is the SERVER-reported name — `player.setName(name[0] ? name : m_character)` — which does not exist until PlayerProfile arrives. Keeping one would mean sending an empty inviter name before zone-in. |
| `m_x`, `m_y`, `m_z`, `m_heading` | `PlayerState::m_x`.. | 19 direct writes each inside the movement integrator, against grouped `setPosition(x,y,z)` on the state side; converging them is a restructure of `UpdateMovement`, not a substitution. |
| `m_my_spawn_id` | `PlayerState::m_spawnId` | 138 references, and its write order relative to the pet-detection and player-entity-creation paths is load-bearing (three call sites in `eq_entities.cpp` key off "spawn id is now known"). |
| `m_current_zone_name`, `m_current_zone_id` | `WorldState::m_zoneName/m_zoneId` | `WorldState::setZoneName` fires `ZoneChanged`, and `ConsoleRenderer` subscribes to it. `CleanupZone()` clears the EverQuest copy, so converging them makes a console session print a zone change to the empty string on every teardown. That is a behaviour decision, not a mechanical one. |

**Recommend one follow-on unit** covering position + spawn id + zone
name/id together (they interlock: the position writes and the spawn-id
writes are in the same functions). `m_character` should stay, and the
pinning test says so in the source.

### Drift the sweep exposed (these were NOT theoretical)

Nine sites wrote one store and not the other. Each is now a single write:

1. `ZoneOnStatusChangeReconnectEnabled` set `m_zone_connected = true` with
   **no** `world().setZoneConnected(true)` — so `world().isZoneConnected()`
   never became true from the connection callback at all.
2. `WorldState::m_clientReady` had **zero production writers**. Every
   `ClientReady` write went to `EverQuest::m_client_ready_sent`, which made
   `WorldState::isFullyZonedIn()` permanently false and unusable.
3. `CheckZoneLine`'s "update last checked position" wrote only the EverQuest
   copy, so `world().lastZoneCheck*` froze at whatever `LoadZoneLines` set
   and never tracked the player again.
4. The zone-change DENIED path cleared only the EverQuest copies, leaving
   `world()` claiming a zone change was still requested and pending.
5. `WorldProcessZoneServerInfo`'s "clear pending zone info" likewise —
   `world()` kept a stale pending zone across every transition.
6. `/time <h>` wrote only `m_time_hour`/`m_time_minute`; `world().timeHour()`
   kept the server's value and the two disagreed for the rest of the session.
7. `SendPetCommand` wrote `m_pet_button_states[..]` directly, **bypassing**
   `OnPetButtonStateChanged` — so the pet panel never saw a command-driven
   button change and the two arrays drifted on every `/pet` command. Now
   every one of those goes through the one publisher.
8. `EqActionHandler::updateRendererTargetInfo(spawnId)` wrote EverQuest's
   target id **even when the `CombatManager::SetTarget` immediately above it
   had failed** (its return value is ignored). With one store, the id only
   moves when the target actually changes.
9. The vendor/bank/trainer **close** paths published `WindowClosed` with
   `data.npcId = m_vendor_npc_id` read AFTER `player().clearVendor()` — that
   only worked because the duplicate still held the old id. Each close path
   now captures the id into a local before clearing, so the event still
   carries the window we were AT.

One deliberate removal fell out of #1/#2: `ZoneProcessClientUpdate` also
called `world().setZoneConnected(true)`, meaning "player confirmed in
world", not "the zone connection is up". With one store that would make
zone-connected true on a ClientUpdate replayed with no socket at all —
`ObservableZoneState.FullHandshake_ProducesCoherentState` caught it
immediately. The DaybreakConnection status callback is now the sole writer;
it fires strictly earlier in a live session, so production is unchanged.
**That test was left exactly as it was** — it passes on its original
assertions.

### Measured red/green

The plan's precedent (T283) applies verbatim: the harnesses were seeding
BOTH stores, which is what let a reader of the wrong one look correct.
`test_eq_bank_coins.cpp`'s helper even said so — "the same two writes
ZoneProcessPlayerProfile ends in for currency". Changing the two harnesses
to seed exactly one store and running them against the **pre-T286** source:

| binary | red (old source, single-store harness) | green (T286) |
|---|---|---|
| `test_eq_bank_coins` | **4 of 13 FAILED** — deposit/withdraw/convert-up/convert-down all sent nothing, because the intent handler mutated `EverQuest::m_platinum`.. while the pool check read `PlayerState` | 13/13 |
| `test_tradeskill_combine` | **4 of 23 FAILED** — `IsFullyZonedIn()` was gated on `m_zone_connected`/`m_client_ready_sent`, not on `WorldState` | 23/23 |

Logs: `review/T286/before_red_*.txt`, `after_green_*.txt`.

### The pinning test

`tests/test_duplicate_state_stores.cpp` (T264's source-parsing pattern):
parses `include/client/eq.h` and the five state headers, normalises member
names across snake_case/camelCase, and asserts the overlap is **exactly**
`kKnownRemaining` — the eight above, each with its reason in the source.
Asserted both ways: a new duplicate fails with its name and counterpart;
deleting one of the eight also fails, with "no longer duplicated: remove it
from kKnownRemaining", so this list and the table above cannot drift apart.

Its blind spot is stated in the file: a duplicate introduced under a
*different* name (the way `m_my_spawn_id` shadows `m_spawnId`) is invisible
to a name-based match. Same-name duplicates — every one T280, T283 and T286
actually found — are caught. Red probe (`m_practicePoints` re-added to
`eq.h` by hand) and green run are in `review/T286/pinning_test_*.txt`.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | PASS |
| `ctest --test-dir build` | **3160/3161**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `bash scripts/build-gles2-x86.sh` | PASS |
| visual gate | **PASS**, goldens untouched |
| `bash scripts/build-arm-noble.sh` | PASS |
| `check_file_sizes.sh fable` | no NEW fails. `src/client/eq.cpp` was already over (3944 -> **3901**, net -43); `include/client/eq.h` WARN pre-existing (2594 -> **2489**, net -105). The run also lists `irrlicht_renderer.h`, `renderer_effects.cpp`, `renderer_zone_load.cpp` and `tools/hwcaps/zone_bench_loader.cpp` — `fable` advanced to `820961e4` (T285/T287) under the slot mid-unit; none of those are this unit's files. |

Net: 893 insertions, 1109 deletions across 22 files.

**One flake worth recording**: the first full `ctest -j12` run failed
`visual_gate` on `befallen: p95 CPU ms 46.712 exceeds ceiling 40.144`. That
is a wall-clock ceiling measured while twelve ctest jobs (including the
three multi-minute `ComprehensiveZone*` tests) shared the box. Re-run alone
it passes in 20s, and the final full run at `-j6` passes it too. Not a
regression; a reminder that the gate's CPU ceiling is not meaningful under
`-j12`.

### Deviations

**One, flagged**: the plan says delete or justify each duplicate, and seven
of the eight kept (position, spawn id, zone name/id) are justified by SIZE
and BEHAVIOUR RISK, not by "the wire path needs a private staging value".
They are named above, the reason is recorded next to each in the pinning
test, and the recommended follow-on unit is stated. Only `m_character` is a
genuine wire-path exception that should never be merged away.
