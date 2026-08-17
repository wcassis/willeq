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

(executor fills: the hook chosen and why, including whether a more
authoritative source than the spawn exists; the **ordering evidence** that
`m_my_spawn_id` is set in time, from a live log; the mid-session
join/leave answer — covered, or explicitly not and why; how the test character
was put in a guild; the live wire evidence contrasted against T132's; the
tests; deviations)

## Board

Say which of the seven guild commands are now genuinely functional end-to-end,
and which remain unverified and why. If mid-session guild changes are not
covered, put that on the board as its own item — do not let "guild works now"
stand unqualified.
