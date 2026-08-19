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

(executor fills)
