# T133 — Populate `m_guild_id`: revive the guild feature

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**Territory: `src/client/eq_entities.cpp`, `src/client/eq_guild.cpp`,
`include/client/eq.h`, wherever the player profile is decoded, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T133/`.**

Read T132's Review first — it found this while migrating the Guild family.

## The bug, fully located

`EverQuest::m_guild_id` (`include/client/eq.h:1846`) is declared `= 0`, **read
in six places in `eq_guild.cpp`, and assigned nowhere in the codebase.**
Verified independently at T132's merge.

Every `SendGuild*()` sender guards on `if (m_guild_id == 0) return;`, so
`guildinvite`, `guildremove`, `guilddemote`, `guildleader` and `guildmotd`'s
set-path are **permanently dead on the wire**. Only `guildmotd`'s get-path
does a real round trip. This is pre-existing — the pre-T132 `game` handlers
carried the identical guard — and it is another instance of this codebase's
dominant defect class, implemented-but-unreached.

## The value is already in hand — this should be small

Scoping established the whole chain already exists except the last hop:

- `Spawn_Struct` carries `guildID` at **offset 238**
  (`include/common/packet_structs.h:183`).
- **Both** spawn-processing sites already decode it:
  `entity.guild_id = p.GetUInt32(offset + 238);` at
  `src/client/eq_entities.cpp:318` and `:1128`.
- The player's own spawn is identifiable — `m_my_spawn_id`, exposed as
  `GetMySpawnID()` / `GetEntityID()`.

So the value is parsed, stored per-entity, and simply never propagated to the
player's own `m_guild_id`.

## The two things that decide whether the minimal fix actually works

Do not just add the assignment and declare victory. Establish both:

**1. Ordering.** Is `m_my_spawn_id` already set when the player's own spawn is
processed at these sites? If the id arrives in a *later* packet, an
`if (spawn_id == m_my_spawn_id)` test at parse time silently never fires — and
that failure mode looks exactly like the current bug. **Verify from a live log,
not by reading.** If the ordering is wrong, find the correct hook and say why.

**2. Is there a more authoritative source?** The player profile / character
data may carry the guild id directly, which would be a better hook than
inferring it from a spawn. Check before settling on the spawn path, and say
what you chose and why.

## Scope boundary — say what you did NOT cover

The minimal fix populates the id **at zone-in / spawn**. That unblocks every
guild command for a character who is *already* in a guild.

**It probably does not cover joining or leaving a guild mid-session**, where
the server sends an update rather than a fresh spawn. Establish whether that
path exists and whether it updates `m_guild_id`. **If it does not, say so
explicitly and record it** rather than leaving a half-working feature that
looks fixed — a player who accepts an invite and then finds `/guildinvite`
silently dead has the same bug in a new costume.

Covering it is in scope if it is small. If it needs a new packet handler,
that is a **STOP** — report it as a follow-up.

## Acceptance — the wire, not the guard

The test is **not** "`m_guild_id` is non-zero". It is that a guild command
**reaches the server and gets a response**.

- Get the test character into a guild. A GM account is available
  (`willeq_test` is status 200, per T132), and seeding the guild directly in
  the eqemu DB is a legitimate route — say which you used.
- Then verify live that at least `guildinvite` (two-client, the other
  character receives the invite) and `guildmotd`'s **set**-path now work, where
  T132 proved they could not.
- **Contrast against T132's evidence**: it recorded `registry=action` for all
  seven with no wire effect for six. This unit's evidence should show the wire
  effect.

If the live leg cannot be completed without Will acting in-game, **say so and
bound it honestly** — which commands you verified and which you did not. T132
set that precedent and it held up.

## Do not

- **Do not remove or weaken the `m_guild_id == 0` guards.** They are correct —
  a guildless character genuinely cannot invite. The bug is the missing
  assignment, not the guard. Deleting the guard would turn a dead feature into
  malformed packets.
- **Do not paper over**: no defaulting `m_guild_id` to a non-zero constant, no
  claim of a wire effect you did not observe in a log.

## Verification

- x86 suite (current total **2311**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **A test** that the player's own spawn populates `m_guild_id` and another
  entity's does not — that second half is the one that would rot.
- **Visual gate goldens must not move.**
- Live evidence in `review/T133/` with the log excerpts showing the wire
  effect.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T133-guild-id`,
off `fable` tip `75b530ed`. Full evidence:
`/mnt/projects/fable/review/T133/` (README + client evidence logs + the
eqemu zone server's own log).

### The hook chosen, and why

`EverQuest::m_guild_id` is now assigned in two places:

1. **Zone-in** (`ZoneProcessPlayerProfile`, `eq.cpp`): the function already
   decoded a `guild_id` local from the player's own `PlayerProfile` packet
   at offset 13036, but only ever wrote it into `self_entity.guild_id` — a
   display-only field on the per-entity `Entity` struct
   (`eq.cpp:2378`), never into `EverQuest::m_guild_id`. Added the missing
   assignment, grouped with the function's other `m_*` stat assignments.
   **This is a more authoritative source than `Spawn_Struct`'s `guildID`**
   (offset 238, decoded per-entity in `eq_entities.cpp:318`/`:1128`, which
   the plan flagged as the alternative to check): the profile packet
   unconditionally describes the player — no spawn-id matching, no
   ordering hazard. Using the spawn path instead would have been actively
   *wrong* for other entities' spawns (their `guildID` describes THAT
   entity, not the player) — proved by a dedicated test,
   `GuildIdPopulation.OtherEntitySpawn_DoesNotPopulateOwnGuildId`.
2. **Mid-session** (join/leave/kick, `ZoneProcessSpawnAppearance`,
   `eq_entities.cpp`): added an `AT_GUILD_ID` case to the existing
   `OP_SpawnAppearance` switch (see "mid-session join/leave" below).

### Ordering evidence, from a live log

The plan's ordering worry — "if `m_my_spawn_id` isn't set yet when a spawn
is matched against it, that failure mode looks exactly like the current
bug" — **does not apply to the hook actually used**, because the
`PlayerProfile` assignment needs no spawn-id match at all. Confirmed by
reading the parse order (`guild_id` is read at the top of
`ZoneProcessPlayerProfile`, offset 13036; `m_my_spawn_id` is only set later
in the same function, offset 14384 — no dependency between them) **and**
empirically from a live log: `review/T133/t133_c_zonein_only_evidence.log`
is a brand-new client session with **zero mid-session guild events**
(guild membership already existed in the DB from an earlier step), and
`/guildmotd`'s set-path succeeds immediately after zone-in:

```
[ZONE] [ZONE-IN] PlayerProfile position: ...
CMD outcome=applied registry=action recognized=true cmd="/guildmotd Zone-in path check!"
[MAIN] Guild MOTD updated.
```

No `AT_GUILD_ID`/`SpawnAppearance` line appears in that log — proving the
zone-in path alone, independent of the mid-session path, does the job.

### Mid-session join/leave — covered, not left as a silent gap

Checked against the eqemu server source (`zone/guild.cpp`,
`zone/client_packet.cpp`), not guessed: `Client::SendGuildSpawnAppearance()`
calls `SendAppearancePacket(AppearanceType::GuildID, ..., true)` —
`whole_zone=true` — and is invoked from `Handle_OP_GuildInviteAccept` and
generally from `Client::SetGuild()`, so the server DOES push a live update
to the affected client's own connection on every guild join/leave/kick.
Before this unit, `ZoneProcessSpawnAppearance`'s switch had **no**
`AT_GUILD_ID` case — the packet arrived and was silently dropped ("Unhandled
SpawnAppearance type" DEBUG log). Fixed by adding one case to the existing
switch: updates the entity map's display copy and, if the spawn is our own
(`spawn_id == m_my_spawn_id`), `m_guild_id` too. `GUILD_NONE` (server-side
`0xFFFFFFFF`, `common/guilds.h`) is normalized to 0 so a guild-leave can't
latch a bogus nonzero id that would defeat the `m_guild_id == 0` guards.
This is a small addition to an **existing** handler, not a new packet
handler, so covered here per the plan's "small" carve-out rather than
reported as a STOP.

Live-confirmed twice in the two-client session
(`review/T133/t133_b_testbuddy_evidence.log`):
```
[MAIN] You have accepted the guild invite.
[ENTITY] Our guild id changed to 1 via SpawnAppearance
```
— once immediately after `#guild create` (unexpectedly also live-pushed to
the connected leader), and once from the real `/guildaccept` flow on the
invitee, who was guildless at zone-in.

### How the test character was put in a guild

GM command: `/say #guild create Testmelee TestGuild133`, same route T132
confirmed reachable (`willeq_test` is account status 200, above the
`Steward` (10) gate `#guild` requires). Chosen over seeding the guild
directly in the DB because it's the sanctioned, already-precedented path
and required no server-code changes.

### The live wire evidence, contrasted against T132's

T132 (before this unit), every guarded command:
```
CMD outcome=applied registry=action recognized=true cmd="/guildinvite Testbuddy"
[MAIN] You are not in a guild.
```
`registry=action` (dispatch worked) but zero network effect.

This unit, same commands, full round trip with server confirmation:
```
# Client A (Testmelee, leader)
CMD outcome=applied registry=action recognized=true cmd="/guildinvite Testbuddy"
[MAIN] Inviting Testbuddy to join your guild.
CMD outcome=applied registry=action recognized=true cmd="/guildmotd Welcome to TestGuild133!"
[MAIN] Guild MOTD updated.
[MAIN] Testbuddy has joined the guild.

# Client B (Testbuddy, guildless invitee)
[MAIN] Testmelee has invited you to join a guild.
CMD outcome=applied registry=action recognized=true cmd="/guildaccept"
[MAIN] You have accepted the guild invite.
CMD outcome=applied registry=action recognized=true cmd="/guildmotd Hello from Testbuddy!"
[MAIN] Guild MOTD updated.
```
`/loc` (client A, untouched) still shows `registry=game` in the same
session — the dispatch-order control, same as T132's.

### A second, independent bug this unit found and had to fix to meet the acceptance bar

Populating `m_guild_id` was necessary but **not sufficient**: the first
two-client attempt (with the population fix already in place) still failed
— `Testbuddy: "You don't have a pending guild invite."` The eqemu zone
log (`review/T133/eqemu_zone_arena_server_log.txt`) explained why:
```
Wrong size: OP_GuildInvite, size=138, expected 136
Error: app size of 650 != size of GuildMOTD_Struct of 648
```
Every `SendGuild*()` sender in `eq_guild.cpp` built its own opcode-prefixed
packet (`packet.Resize(2 + sizeof(Struct)); packet.PutUInt16(0, opcode);
memcpy(...+2, ...)`) and then called `EverQuest::QueuePacket(opcode,
&packet)`, which **also** prepends its own 2-byte opcode
(`eq.cpp:3850`) — a double opcode prefix that made every size-checked
guild struct arrive 2 bytes over what the server expected, and get
rejected. Confirmed against `QueuePacket()`'s own correctly-working callers
elsewhere (e.g. `combat.cpp`'s `ConsiderTarget`), which pass a body-only
packet with no self-written opcode. This bug predates T133 and was
invisible before it: every guarded sender was blocked by `m_guild_id == 0`
first, and the one unguarded sender (`SendGetGuildMOTD`) has no
size-checked struct. Fixed in the same commit (`src/client/eq_guild.cpp`,
this unit's territory, not a new packet handler) by removing the redundant
opcode-write and `+2` offset from all six affected senders. Confirmed
fixed: zero `Wrong size`/`app size` lines anywhere in the zone log after
the fix + rebuild, across three separate client sessions.

### Tests

`tests/test_zone_connection_flow.cpp`, five new cases in a new
`GuildIdPopulation` suite (all passing, part of the 2316-test x86 suite):

- `OwnPlayerProfile_PopulatesGuildId` — the player's own `PlayerProfile`
  (guildID at offset 13036) reaches `GetGuildID()`.
- `GuildlessPlayerProfile_LeavesGuildIdZero` — an explicit-zero guild id in
  the profile is not misread as "unset".
- `OtherEntitySpawn_DoesNotPopulateOwnGuildId` — **the plan's required
  "second half"**: a `ZoneSpawns` entry for a different spawn, carrying its
  own nonzero `guildID` (offset 238), is recorded on that entity's display
  copy but must not, and does not, leak into the player's own
  `m_guild_id`.
- `MidSessionSpawnAppearance_UpdatesOwnGuildId` — an `AT_GUILD_ID`
  `SpawnAppearance` for our own spawn updates `m_guild_id`; `GUILD_NONE`
  (`0xFFFFFFFF`) normalizes to 0.
- `MidSessionSpawnAppearance_OtherEntity_DoesNotAffectOwnGuildId` — same
  packet for a different spawn must not touch our own `m_guild_id`.

x86 suite: 2316/2316 minus the documented ALSA
`AudioManagerTest.PreloadCommonSounds` (2315 passing, same sole exception
as the plan's stated baseline — total grew by 5 for this unit's new
tests). GLES2 x86 build: clean. Visual gate: **PASS**, goldens untouched.
ARM cross-compile: clean. `check_file_sizes.sh fable`: `src/client/eq.cpp`
shows `FAIL` at 3898 lines, but this is **pre-existing** — it was already
3884 lines (already over the 3000 ceiling) on the `fable` baseline before
this unit's 14-line addition; not a new fail, and the addition itself
(one assignment + a doc comment) is not "substantial new code" warranting
a new TU. `src/client/eq_guild.cpp` (net-larger due to the file-header
comment explaining the double-opcode bug) and the new test file are both
`ok`. `include/client/eq.h` and `eq_entities.cpp` remain at their
pre-existing `WARN` levels, unchanged in kind.

### Deviations from the plan

- **Found and fixed a second, independent bug** (the double-opcode-prefix
  packet-sizing issue above) that was not anticipated by the plan but was
  required to meet its own acceptance bar ("a guild command reaches the
  server and gets a response"). In territory (`eq_guild.cpp`), not a new
  packet handler, so fixed rather than reported as a STOP — flagged here
  per "deviations flagged, never silently absorbed."
- Mid-session coverage went further than "establish whether it exists": it
  does exist (confirmed from server source), so it was fixed rather than
  merely reported — the plan explicitly scoped this in if small, and it
  was (one `switch` case).
- `guilddecline`, `guildremove`, `guilddemote`, `guildleader` got the same
  double-opcode-prefix fix but were not separately live-exercised this
  session (see Board) — narrower live coverage than the full seven, called
  out explicitly rather than folded into "guild works now."

## Board

**Genuinely functional end-to-end, verified live this unit**:
`guildinvite`, `guildaccept`, `guildmotd` (both get, pre-existing, and set,
new — verified on both a zone-in guild leader and a mid-session joiner).

**Same fix applied, mechanically identical, but NOT separately verified
live this session**: `guilddecline` (same code path as accept,
`SendGuildInviteAccept(false)`, same double-opcode fix), `guildremove`,
`guilddemote`, `guildleader` (all three: same double-opcode fix, guard
unchanged). No reason to expect divergence given the identical fix shape
applied to all of them, but not directly observed — flagged so "guild
works now" doesn't stand unqualified for these four. A small follow-up
(second two-client script exercising just these four) would close this
cheaply.

**Mid-session guild-state changes are covered**, as their own fix distinct
from the zone-in fix (`AT_GUILD_ID` `SpawnAppearance` case in
`ZoneProcessSpawnAppearance`) — not folded into "zone-in fix, done." Both
the zone-in path and the mid-session path were independently verified live
in this session (see "ordering evidence" and "mid-session join/leave"
above).
