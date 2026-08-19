# T150 — Four zero-caller social/find senders

**Slot: slot3. x86. NO eqemu lock (T148 holds it). No Pi.**

**Territory: `src/client/eq_player_info.cpp`, `src/client/eq_group.cpp`,
`include/client/eq.h` (these four declarations only), `src/client/action/`,
`include/client/action/`, `src/client/eq_action_handler.{h,cpp}`, `tests/`,
`refactor/`.**

**T148 (slot1) and T149 (slot2) run in parallel.** T148 owns
`src/client/eq_gm.cpp` and `src/client/eq.cpp`; T149 owns
`src/client/eq_merchant.cpp`. All three edit `src/client/action/*` and
`eq_action_handler.*` — **append your registrations, do not reorder or reflow
existing ones**, and keep `tests/CMakeLists.txt` to a single appended block.

Read T132's Review for the net-new action-stack recipe (~50 minutes measured).

## The four senders

All four have **zero production callers** — declaration and definition only,
verified by a sweep across `src/`, `include/`, `tests/` and `tools/`.

| sender | signature | eq.h | file |
|---|---|---|---|
| `SendAssistGroup` | `(uint32_t target_id)` | :1053 | `eq_group.cpp` |
| `SendTrackTarget` | `(uint32_t target_id)` | :1086 | `eq_player_info.cpp` |
| `SendFindPerson` | `(uint32_t npc_id)` | :1098 | `eq_player_info.cpp` |
| `SendFriendsWho` | `()` | :1102 | `eq_player_info.cpp` |

Receive side already wired: `ZoneProcessTrack` (`eq.h:1447`) and
`ZoneProcessFindPersonReply` (`:1509`). Like T149's recipe parsers, **these have
never been reached in production** — nothing has ever sent the provoking
request — so they are unvalidated by construction. Garbage in a reply is a
finding to report, not a blocker to patch around.

`SendAssistGroup` and `SendFriendsWho` appear to have no reply parser at all;
confirm that and say what the expected server behaviour is, rather than assuming
silence means success.

## The work

Full action stack per T132's recipe: `CommandProcessor` registration →
`ActionDispatcher` method → `EqActionHandler` → existing sender. **Register in
`action::CommandProcessor` only**, never the legacy `game` registry (T97's
shadowing bug).

Three of the four take an entity id. **Take a target name or use the current
target where that is the natural interface** — a command that requires the user
to know a numeric spawn id is technically wired and practically unreachable,
which is the same defect class this whole arc exists to remove. `SendFindPerson`
takes an `npc_id`; check how the existing target/consider commands resolve names
to ids and reuse that path rather than building a second one.

**Check for name collisions before registering.** `/face` already means the
*movement* face (`command_processor.cpp:324`, category "Movement") — the T146
plan conflated it with `OP_FaceChange` and lost a leg to the confusion.
`/track`, `/find`, `/assist` and `/who`-adjacent names are all plausible
collisions; `grep` the registries first and report any you hit.

## Acceptance

You have no eqemu lock — **T148 holds it and you cannot take it.** This unit
ends verified against source and by tests, NOT on the wire, exactly as T145 did.
**Be explicit in the Review; do not write "tracking works now."** The wire leg
is a follow-up unit; name it as owed.

## Verification

Full protocol bar. Suite baseline 2457 (tip `359f3647`), sole expected failure
the documented ALSA `AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- A name collision that cannot be resolved without Will's call on which command
  keeps the name.
- Name→id resolution needing new cross-layer state.

## Review

Executed in `/mnt/projects/fable/wt/slot3`, task branch
`task/T150-social-find`, off `fable` tip `72169720`. **No eqemu lock — T148
holds it. The server was never started. Nothing below was observed on the
wire; every claim is against eqemu source or the test suite.**

### The four commands

| command (aliases) | category | `CommandProcessor` | `ActionDispatcher` | `EqActionHandler` | `EverQuest` sender | opcode |
|---|---|---|---|---|---|---|
| `groupassist` (`ga`) | Group | `cmdGroupAssist` | `assistGroup(id)` | `assistGroup(id)` | `SendAssistGroup` | `HC_OP_AssistGroup` 0x5104 |
| `track` | Skills | `cmdTrack` | `trackTarget(id)` | `trackTarget(id)` | `SendTrackTarget` | `HC_OP_TrackTarget` 0x7085 |
| `find` (`findperson`) | Social | `cmdFind` | `findPerson(id)` | `findPerson(id)` | `SendFindPerson` | `HC_OP_FindPersonRequest` 0x3c41 |
| `friendswho` (`friends`) | Social | `cmdFriendsWho` | `friendsWho(list)` | `friendsWho(list)` | `SendFriendsWho` | `HC_OP_FriendsWho` 0x48fe |

All four registered in `action::CommandProcessor` ONLY — the legacy
`EverQuest::m_command_registry` was not touched (T97 shadowing bug). Its
only surviving registrations are `eq_commands_spells.cpp`'s
hotkeys/pmem/music/sound/volume/musicvolume/effectsvolume; none collide.
Registrations were **appended** at the end of `registerBuiltinCommands()`;
nothing above them was reordered or reflowed. `tests/CMakeLists.txt` was
**not modified at all** (no new test binary — the 16 new cases went into
the existing `test_command_processor.cpp`), so there is no block for the
parallel units to conflict with.

### Interface: no command requires a numeric spawn id

Three senders take an entity id. All three commands take a **name or the
current target**, resolved through the **pre-existing**
`CommandProcessor::resolveCombatTarget()` — the exact path `/assist`,
`/shield` and `/duel` already use (`EntityManager::findEntityByName()`,
case-insensitive substring, then `CombatManager`'s live target).
**No second resolution path was built and no new cross-layer state was
needed**, so the plan's second STOP condition was not reached.

`targetId == 0` means "no explicit name given"; the fallback to the live
`CombatManager` target and the `"You don't have a target."` message live in
`EqActionHandler` (new private helper `resolveTargetOrCurrent()`), which is
the same rule `assist()`/`shieldTarget()` implement inline — the live
manager is read there rather than `GameState::combat()` per T28's note that
the latter is never populated in production.

`/friendswho` deliberately does **not** resolve names to ids: eqemu resolves
this opcode in **world**, across zones, by character name
(`ClientList::SendFriendsWho`, `world/clientlist.cpp:921`). An entity-list
lookup would be wrong there, not merely redundant.

### Name collisions — checked before registering, one real one found

Greps run over `action::CommandProcessor`'s table, `eq_commands*.cpp`, and
`IrrlichtRenderer::processSlashCommand()`'s literal `cmd == "/..."` arms.

| candidate | result |
|---|---|
| `assist` / `a` | **COLLISION — real, and it is exactly T146's `/face` trap.** `/assist` (alias `a`) already exists, Combat category, and routes to a **different opcode**: `SendAssist` → `HC_OP_Assist` (0x7709), single-target assist. `SendAssistGroup` is the **group** assist (0x5104). Two different opcodes, two different senders. **Resolved without needing Will**: the new command took `groupassist`/`ga`; `/assist` is untouched. Guarded by a test (`T150_AssistIsUntouchedAndDistinctFromGroupAssist`) that asserts `/assist` still yields `assist(555)` and `/groupassist` yields `assistGroup(555)` in the same fixture. |
| `who` / `whoall` | **Adjacent, not a collision.** `cmdWho` (Social) is `SendWhoAllRequest` → `HC_OP_WhoAllRequest`. `/friendswho` is a different opcode with a different argument shape — even though, see below, its **reply** is the same `OP_WhoAllResponse` packet. Deliberately not merged into `/who`; `T150_FriendsWho_DoesNotShadowWho` asserts `who`/`whoall` still resolve to `who`. |
| `track`, `find`, `findperson`, `friendswho`, `friends`, `groupassist`, `ga` | **All free** in all three registries. |
| `face` | Not used by this unit. (Named in the plan because T146 lost a leg to it; `SendFaceChange` is not in T150's set.) |

**No STOP was required**: the one real collision was resolvable without
deciding which command keeps a name, because the group assist never had a
claim on `/assist`.

### Reply parsers: what each sender actually provokes, per eqemu source

The plan asked specifically about `SendAssistGroup` and `SendFriendsWho`.
Both **do** provoke server traffic — silence would have been wrong.

| sender | eqemu handler | server behaviour | client parser |
|---|---|---|---|
| `SendAssistGroup` | `Handle_OP_AssistGroup` (`zone/client_packet.cpp:3110`) | Size-checks `EntityId_Struct`, looks up the entity, and **if in `TARGETING_RANGE` or the requester is a GM**, replies **`OP_Assist`** carrying the new target's id. Out of range / non-mob ⇒ genuinely silent. | **NONE.** `HC_OP_Assist` (0x7709) has **no `case` in `EverQuest::HandleZonePacket`** — it appears only in `eq_strings.cpp`'s name table and in `SendAssist`. So the reply is received and dropped, and neither `/assist` nor `/groupassist` can currently acquire the assisted target client-side. **Pre-existing, not caused by this unit** (`/assist` has shipped with the same hole). Owed as a follow-up. |
| `SendFriendsWho` | `Handle_OP_FriendsWho` (`:6514`) → `Client::FriendsWho` (`zone/client.cpp:2194`) → `ServerOP_FriendsWho` → `ClientList::SendFriendsWho` (`world/clientlist.cpp:921`) → `ServerOP_WhoAllReply` → `zone/worldserver.cpp:471` | Replies **`OP_WhoAllResponse`** — byte-for-byte the same packet `/who all` produces. Zero matches ⇒ `WHOALL_NO_RESULTS` message instead of a packet. | **YES, shared with `/who`**: `ZoneProcessWhoAllResponse` (`eq.h:1487`) is already dispatched for `HC_OP_WhoAllResponse`. This is the only one of the four whose reply path is both wired **and** already exercised in production (by `/who`). |
| `SendFindPerson` | `Handle_OP_FindPersonRequest` (`:6459`) → `Client::SendPath` (`zone/client.cpp:12114`) | Replies `OP_FindPersonReply`: a `FindPersonResult_Struct` = `dest` point followed by the path, or a **zero-length** packet when the target is null / <2 or >36 points. | `ZoneProcessFindPersonReply` — wired, **never reached in production**, unvalidated by construction. Struct layout verified against `common/patches/titanium_structs.h`: `FindPerson_Point {y,x,z}` 12 bytes and `FindPersonRequest_Struct {npc_id, client_pos}` 16 bytes match `include/common/packet_structs.h` exactly, and the parser reading offset 0 as the destination matches `FindPersonResult_Struct::dest` being the first member. The zero-length reply is handled (length guard returns early). |
| `SendTrackTarget` | `Handle_OP_TrackTarget` (`:15283`) | Gated on `CanThisClassTrack()` (class tracking-distance multiplier > 0 — Ranger/Druid/Bard). Then **`SetTrackingID()` and nothing else. It sends NO reply, by design.** | n/a — and see the finding below. |

### Finding: `ZoneProcessTrack` is still unreachable, and T150 does not fix it

**The plan's premise that `SendTrackTarget` provokes `ZoneProcessTrack` is
wrong.** They are two different opcodes with two different eqemu handlers:

- `OP_Track` (0x5d11) is the **track-list request**. `Handle_OP_Track`
  (`:15266`) checks `CanThisClassTrack()`, bumps the Tracking skill, and
  calls `EntityList::MakeTrackPacket` (`zone/entity.cpp:3696`), which
  replies `OP_Track` with an array of `Track_Struct{entityid, distance}`.
  That is the packet `ZoneProcessTrack` parses. Struct layout verified: 8
  bytes both sides, fields in the same order.
- `OP_TrackTarget` (0x7085) is a **fire-and-forget setter**. No reply.

**This client has no sender for `OP_Track` at all** — grep over `src/`,
`include/`, `tests/`, `tools/` finds `HC_OP_Track` only at its enum
definition, in `eq_strings.cpp`'s name table, and at the `case` in
`eq.cpp:1618`. So `ZoneProcessTrack` remains **structurally unreachable
after this unit**, exactly as it was before it. `/track` sets the
server-side tracking id and produces no visible output; that is honest
eqemu behaviour, not a client defect, but it is not a tracking feature.

**Not fixed here, deliberately.** Adding a fifth sender is net-new network
surface outside the plan's "The four senders" contract, and the arc's
standing instruction (T146) is not to invent callers to make a leg
verifiable. **Owed follow-up: a `SendTrackRequest()` for `OP_Track` plus a
`/track` (no-args) list mode — that is the unit that can first claim
`ZoneProcessTrack` works.** Note `OP_TrackUnknown` (0x6177,
`Handle_OP_TrackUnknown`, a size-0 no-op the real client sends right after
`OP_Track`) exists too and is likewise unsent.

### DEVIATION (flagged, not absorbed): `SendFriendsWho()` signature and payload changed

`SendFriendsWho()` took no arguments and sent
`uint16 opcode | uint32 m_my_spawn_id | m_character`. **That payload is
provably wrong against eqemu source.** `Handle_OP_FriendsWho`
(`zone/client_packet.cpp:6514`) is:

```
char *FriendsString = (char*)app->pBuffer;
FriendsWho(FriendsString);
```

The **entire** application payload is one NUL-terminated, comma-delimited
list of character names. There is no struct and no Titanium encoder for
this opcode (`grep FriendsWho common/patches/` finds only the opcode name),
so nothing strips a prefix. World then `strchr()`s that string for `,` and
`FindCharacter()`s each name; the requester's id and name are taken
server-side from `ServerFriendsWho_Struct::FromID`/`FromName`, which the
**zone** fills from `GetID()`/`GetName()` — the client-supplied spawn id was
never read by anything. So the old form made the server parse a first
"name" beginning with four raw binary bytes, which can never match a
character: guaranteed `WHOALL_NO_RESULTS`.

Changed to `SendFriendsWho(const std::string& friends_list)` writing the
list at offset 2 with **`PutCString`, not `PutString`** — `Packet::PutString`
(`src/common/net/packet.cpp:117`) does **not** write a terminator, and the
server `strchr()`s the buffer, so the NUL is load-bearing.

Why this is a deviation and why it was taken: the plan's table records the
`()` signature as current state, and its territory grant explicitly covers
`include/client/eq.h` "these four declarations". Registering `/friendswho`
over the old payload would have shipped a command that is reachable and
known-broken on the wire — the same defect class this arc exists to remove.
Zero production callers existed, so nothing regressed. **This is a T145-class
struct/wire fix made while wiring a caller, and it is unverified on the
wire like everything else in this unit.**

### What is NOT claimed

**Tracking does not work now. Group assist does not work now.** Nothing in
this unit was observed on the wire. What is established:

- Each command dispatches through `action::CommandProcessor` to the correct
  `EverQuest::Send*()` with a correctly resolved id or argument — by 16 new
  unit tests against `RecordingActionHandler`.
- Each sender's opcode and payload match eqemu's handler expectations — by
  reading eqemu source (`zone/client_packet.cpp`, `zone/client.cpp`,
  `zone/entity.cpp`, `zone/pathing.cpp`, `world/clientlist.cpp`,
  `world/zoneserver.cpp`, `common/patches/titanium_structs.h`).

**Owed wire leg (a follow-up unit, needs the eqemu lock):**
1. `/groupassist` with a group and a target in range — expect `OP_Assist`
   back, and expect the client to **drop** it (no dispatch case). Fixing
   that is part of the same follow-up.
2. `/find <name>` — expect a non-empty `OP_FindPersonReply`; this is the
   **first ever** exercise of `ZoneProcessFindPersonReply`. Garbage in the
   printed destination is a finding to report, not to patch around.
3. `/friendswho Testbuddy` — expect an `OP_WhoAllResponse` listing
   Testbuddy, which validates the payload fix above.
4. `/track` on a Ranger/Druid/Bard — expect **nothing visible**, correctly.
   The list leg cannot be run until `OP_Track` has a sender.

### Verification

| step | result |
|---|---|
| 1. `cmake --build build -j24` | **PASS**, clean |
| 2. `ctest --output-on-failure` | **PASS** — 2473/2474; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| 3. `scripts/build-gles2-x86.sh` | **PASS** |
| 4. Visual gate | **PASS**, goldens untouched (`git status` shows only the 10 source files) |
| 5. `scripts/build-arm-noble.sh` | **PASS** |
| 6. `check_file_sizes.sh fable` | **PASS** — 10 files checked, 5 ok, 5 WARN, **0 FAIL**; every WARN is a pre-existing >1500-line file (`eq.h`, `action_dispatcher.cpp`, `command_processor.cpp`, `eq_player_info.cpp`, `test_command_processor.cpp`) that this unit only appended to |

Suite-count note: the plan's baseline is 2457 at tip `359f3647`; this branch
is off `72169720`, several revs later. 2474 − 16 new cases = 2458, i.e. the
pre-existing base at *this* tip is 2458, one above the plan's figure. The
drift is between the two base revs, not from this unit.

### Files touched

`include/client/eq.h` (one declaration), `src/client/eq_player_info.cpp`
(`SendFriendsWho` only), `include/client/action/action_dispatcher.h` (+4
`IActionHandler` virtuals, +4 `ActionDispatcher` methods),
`src/client/action/action_dispatcher.cpp` (+4, appended),
`include/client/eq_action_handler.h` / `src/client/eq_action_handler.cpp`
(+4 overrides, +1 private helper, appended),
`include/client/action/command_processor.h` /
`src/client/action/command_processor.cpp` (+4 `cmd*`, +4 registrations,
appended), `tests/action_test_fakes.h` (+4 recordings, appended),
`tests/test_command_processor.cpp` (+16 cases, appended).

**Parallel boundary honoured**: `src/client/eq_gm.cpp`, `src/client/eq.cpp`
(T148) and `src/client/eq_merchant.cpp` (T149) were not touched.

**Plan correction**: the plan's table places `SendAssistGroup` in
`src/client/eq_group.cpp`. It is actually in
`src/client/eq_player_info.cpp:1347` (Cluster 9, next to `SendAssist`).
`eq_group.cpp` was never opened. All four senders live in one file.
