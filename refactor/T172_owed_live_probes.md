# T172 — The owed live probes: T167, T169, T170/T171, and T162's wire re-check

**Slot: slot1. x86. No Pi.** Orchestrator-direct — no executor, and no plan
committed before the fact; this document is written after the runs and says so.

**eqemu: brought UP by this unit (`scripts/eqemu/up.sh arena`) and taken back
DOWN at the end. `/mnt/projects/fable/.eqemu-lock` was taken by this session and
released with it.** Both are recorded here because the previous three live units
(T155, T158, T161) all ran against a server the orchestrator had already started.

**Territory:** `refactor/T172_owed_live_probes.md`,
`/mnt/projects/fable/review/T172/`, `data/captures/` (gitignored). **No source
was changed.** Two defects were found and are reported, not fixed.

Read the Reviews of T162, T167, T169, T170 and T171 first. Evidence lives at
`/mnt/projects/fable/review/T172/`, start at its `README.md`.

## Why this unit existed

Four separate units had merged on tests alone and left an explicit live debt:

- **T167** — `/appearance` must preserve the six appearance fields the command
  did not name. eqemu's `Handle_OP_FaceChange` copies the whole struct and
  `Save()`s with no rejection path, so a field we fail to re-send is a field we
  set to **zero**.
- **T169** — the raid decode: six actions were decoded wrongly and action 20, a
  raid invite, fell through to "Unknown raid action" so an invited player was
  told nothing at all.
- **T170/T171** — a Titanium client must answer its own inspect requests, and
  T171 bounded what that answer overwrites. Both are two-client by nature: the
  request and the empty window are on different machines.
- **T162** — three "nothing was sent" assertions resting on the **cooldown
  witness**, because the x86 test seam cannot observe a packet (`QueuePacket`
  no-ops with a null zone connection).

## The evidence rule

Unchanged from T161, and it earned its keep twice here. Every probe is
**VERIFIED** (external evidence quoted — server log, DB, or the second client),
**BLOCKED** (with the exact gate) or **FAILED** (with the defect). A local
"sent OK" line counts for nothing, and an assertion that a packet was **not**
sent needs a **positive control on the same run and in the same log file**.

## Cast

- **A = `Oraclerogue`** — `willeq_rogue`, **status 250**, Rogue 10. The only
  account that can `#logs`. See `oraclerogue-test-character` in memory.
- **B = `Testmelee`** — `willeq_test`, Warrior 1.
- **The official Titanium client** as `Testbuddy` (`willeq_buddy`), under
  Wine/Xvfb via the eq-oracle rig — used for one thing only, see probe 2 leg B.

Binary: `wt/slot1/build/bin/willeq`, built 2026-08-22 01:10 at `fable` tip
**`04875b39`**, with no source file newer than it (checked with `find -newer`
before the first run). Clients headless, `--no-graphics --log-level=DEBUG`,
driven by `--script`. **Client logs need `grep -a`.**

## Results

| probe | verdict |
|---|---|
| 1. T167 `/appearance` field preservation | **VERIFIED** |
| 2. T169 raid decode — 7 actions incl. the invite | **VERIFIED** |
| 3. T170/T171 two-client inspect | **VERIFIED** |
| 4. T162's three "nothing was sent" assertions | **VERIFIED, 3 of 3** |

### Probe 1 — T167

**The columns were all already zero, and that is the finding underneath the
probe.** `character_data`'s seven appearance columns read 0 for every test
character — which is precisely the pre-T167 damage, since T155 ran `/appearance`
on `Testmelee` on 2026-08-20. **A probe against all-zero columns cannot fail**,
so the unit first SEEDED six distinct non-zero values by SQL with the character
logged out (`hair_color=2, hair_style=1, beard=1, beard_color=3, eye_color_1=5,
eye_color_2=7`; `face` was already 4).

`Testmelee` then ran **`/appearance face 3`** — one field named, six untouched.
The client merged from the cached PROFILE and said so:

```
Requesting appearance: haircolor=2 beardcolor=3 eyecolor1=5 eyecolor2=7 hairstyle=1 beard=1 face=3
Facial features accepted.
```

`character_data` after camp: `face=3`, **all six others unchanged**. On pre-T167
code all six would have been written 0.

**Correction to the board: the syntax is `/appearance face 3`, not `face=N`.**
The owed-probe note carried the wrong form.

### Probe 2 — T169, in two legs

**No WillEQ client can create a raid.** T168 deleted the broken `SendRaidInvite`
rather than park it, T169 deliberately shipped no accept path and no `/raid`
commands, and **eqemu has no `#raid` GM command** (only `#raidloot`). So the two
legs source raid traffic two different ways, and both are genuinely
server-generated.

**Leg A — the server's own zone-entry rejoin.** `raid_details` + `raid_members`
rows were inserted for A (leader) and B; `Client::Handle_OP_ZoneEntry`
(`zone/client_packet.cpp:573`) rejoins a raid from `raid_members` on zone entry
and fires `SendRaidCreate` + `SendRaidAdd` + `SendBulkRaid` + `SendRaidMOTD`.
**Only the state was seeded; every packet was built by eqemu.** Both clients
decoded, with correct names:

```
action=8  (raidCreate)   leader='Oraclerogue'      -> "Oraclerogue has formed a raid."
action=0  (raidAdd)      player='Oraclerogue'      -> "You have joined the raid."
action=0  (raidAdd)      player='Testmelee'        -> "Testmelee has joined the raid."
action=35 (raidSetMotd)                            -> "Raid MOTD: T172 raid decode probe MOTD"
action=14 (raidSetLeaderAbilities)                 -> logged as not handled (correct: out of scope)
action=30 (raidMakeLeader) player='Oraclerogue'    -> "Oraclerogue is now the raid leader."
```

Action 8 is the one that mattered: the 72-byte `RaidCreate_Struct` arriving on
the same wire value as `OP_RaidUpdate` because Titanium maps `OP_RaidJoin` and
`OP_RaidUpdate` to `0x1f21`. T169 pinned that by test; here it decoded a real
server packet.

**Leg B — the invite (action 20), sourced from the official client.** The
invitee must be resolved by `entity_list.GetClientByName` in
`Handle_OP_RaidCommand`'s invite arm (`client_packet.cpp:12356`), which is
**zone-local**, so both clients were in tutorialb.

```
RaidUpdate action=20 (raidSetLeader) player='Testmelee' leader='Testbuddy'
Testbuddy invites you to join a raid. This client cannot accept raid invites yet.
```

That is exactly the case T169 opened on, and the message names the inviter.

### Probe 3 — T170/T171

A set `/inspectmessage Oraclerogue T172 inspect probe`; B targeted A and
inspected. B's client printed:

```
Received inspect data for Oraclerogue
Inspecting Oraclerogue:
  Slot 13: Dagger*
  Slot 14: Gloomingdeep Lantern
Message: Oraclerogue T172 inspect probe
```

Five lines settle three things:

1. **T170's answer path works end to end** — A's log carries
   `Testmelee is inspecting you` + `Answered inspect request from entity 33`.
   Before T170 the window on B could never populate, because no client anywhere
   sends `OP_InspectAnswer` for a Titanium client and eqemu builds it
   server-side only for SoF+ and bots.
2. **The gear names are real and they are the server's** — slots 13/14 match
   `inventory` for charid 81 exactly. We send those fields empty on purpose.
3. **T170's second fix is confirmed on the wire** — the answer names
   **Oraclerogue**, the inspected player (`playerid`). The pre-T170 code read
   `TargetID` here, which in an ANSWER is the requester, and would have printed
   B's own name.

T171: `character_inspect_messages` afterwards holds exactly one row —
`id=81, len=30, 'Oraclerogue T172 inspect probe'` — the value we set, not empty.

### Probe 4 — T162's owed re-check

Category **84 (`Packet C->S`)** at level 3 to file, enabled by A, both clients in
tutorialb, zone process 391. The container runs 5 h behind the host: client
`18:26:16` == server `13:26:16`.

| client action (host clock) | server C->S line | verdict |
|---|---|---|
| A `/taunt` 18:26:16 — "Your class cannot use Taunt" | **none at 13:26:16** | not sent |
| A `/track Testmelee` 18:26:26 — "Skill gate refused skill 53" | **none at 13:26:26** | not sent |
| A `/bandage` 18:26:36 | `[OP_Bind_Wound] Size [10]` at 13:26:36 | sent |
| B `/taunt` 18:27:12 | `[OP_Taunt] Size [6]` at 13:27:12 | sent — control |

**Whole-file counts are the clean statement: exactly ONE `OP_Taunt` in the entire
session — B's control — and ZERO `OP_Track*` of any kind.** Logging was
demonstrably live through both silences: `OP_ClientUpdate` and
`OP_FloatListThing` land every second, `OP_TargetMouse` anchors each `/target`
five seconds before its command, and the two positive controls straddle the
silences in time (A's own `OP_Bind_Wound` 20 s after its silent taunt, B's
`OP_Taunt` 56 s after it).

Two corroborations that were not asked for:

- **A had a target when its `/taunt` was refused**, so the `"No target to taunt"`
  pre-check is excluded as the cause. The gate is the cause. Without this the
  silence would have been ambiguous.
- B's control emitted `OP_Animation` immediately before `OP_Taunt` — the kick
  animation T162's Review predicted `/taunt` would gain by routing through
  `activateSkill`, now observed rather than argued. A's `/bandage` emitted none,
  as Bind Wound has no animation.

Outside authority for the bandage leg: A's `Bandages*` charges went **18 -> 17**.
The server acted on the packet, not merely received it.

**T162's cooldown-witness caveat is retired. The T152 -> T162 skill arc is now
wire-closed except the null-deref confirmation**, which stays deliberately unrun
because it needs a server nobody else is using.

## Defects found — two, neither fixed

### 1. OURS — action 10 at zone-in is not `raidNoRaid`, and T169's premise about it is wrong

Every zone entry logs:

```
[WARN] [ZONE] RaidUpdate too short for action 10 (raidNoRaid): 138 bytes
```

It fires **with no raid in existence anywhere** — it is in the probe 1 and probe
3 logs too, once per zone entry. The packet is not a raid message at all:
`Client::CompleteConnect` (`zone/client.cpp:859`) sends `OP_RaidUpdate` carrying
a **`ZoneInSendName_Struct`** — `unknown0`, `name[64]`, `name2[64]`,
`unknown132` — with **`unknown0 = 0x0A`**. We read that field as `action` and
land on 10.

- 4 + 64 + 64 + 4 = **136**, plus the 2 opcode bytes = the 138 observed. It is
  not a short `raidNoRaid`; it is a different, complete struct.
- T169's note that "action 10 arrives in eqemu's INTERNAL 140-byte layout" is
  **wrong on the wire**. `EMU_GEN_SIZE = 140` (`src/client/raid_update.cpp:36`)
  is why a complete packet is rejected.
- **Nothing in `zone/` sends a real `raidNoRaid`** — the only references in the
  whole eqemu tree are the six patch encoders, and Titanium's arm is a raw
  passthrough (`common/patches/titanium.cpp:1703`).
- Effect today is a spurious WARN per zone-in, not a user-visible break. **The
  fix is a discrimination between two packets that both present 10, not a size
  bump** — and it is only findable on the wire, which is the point of this unit.

### 2. UPSTREAM eqemu, not ours — `SendRaidMOTD` truncates the name to 7 characters

```
A: action=35 (raidSetMotd) player='Oraclerogue' leader='Oracler'
B: action=35 (raidSetMotd) player='Oraclerogue' leader='Testmel'
```

`Raid::SendRaidMOTD`, `zone/raids.cpp:1566`:

```cpp
strn0cpy(data->general.leader_name, c->GetName(), sizeof(c->GetName()));
```

`GetName()` returns `const char*` (`zone/mob.h:597`), so `sizeof` is the
**pointer width, 8** — the copy is bounded at 7 chars + NUL. `Oraclerogue` ->
`Oracler`, `Testmelee` -> `Testmel`, exactly as observed. Line 2295 of the same
file does the identical copy correctly with `sizeof(c.member_name)`.

**Our decoder is right and is faithfully reporting what the server sent.** Note
also that for this action eqemu puts the RECIPIENT in `leader_name` and the real
leader in `player_name`, which is why A and B see different values. **Report
upstream alongside T152's `OP_Bind_Wound` null-deref.**

## Ops findings worth keeping

- **The corpse substring trap hit THREE times in one session** — `/target
  Testmelee` resolved to `Testmelee's corpse26` / `corpse237`. It cost probe 3's
  reverse leg outright and would have cost probe 4 had the target's identity
  mattered. T160 fixed corpse *detection*; **targeting still prefers a corpse
  over the living player by substring.** This now deserves its own small unit.
- **`eq-oracle/scripts/enter-world.sh` does not work as written.** Its four
  `jclick` attempts on "Enter World" all failed silently while the client sat at
  character select; a focused **`xdotool key Return`** entered the world first
  try. This is the same keyboard-over-mouse lesson `login-as.sh` documents in its
  own header — `enter-world.sh` never got it.
- Entering the world sends the character to its **bind point**
  (`ZoneToBindPoint`), not to where it logged out.
- The raid-invite handler resolves the invitee with
  `entity_list.GetClientByName` — **zone-local**. A cross-zone `/raidinvite`
  would have silently done nothing and looked like our bug.
- `pkill -f <script>` **matches the agent's own shell** when the pattern appears
  in its command line, and kills it. Cost one command here.
- A client run from a cwd other than the slot root dies at startup on
  `data/willeq.db: unable to open database file`. The driver must `cd "$SLOT"`.

## Server state — changed and left

- `character_data` for `Testmelee`: appearance columns seeded by probe 1, `face`
  4 -> 3 by the probe itself. **Not restored** — the seeded values are more
  useful than the zeros that were there, and both characters are scratch.
- `character_inspect_messages`: one new row, charid 81. That is T171 working as
  designed and is probe 3's evidence.
- `raid_details` / `raid_members`: raidid 1 inserted for leg A and **deleted
  afterwards** — both tables are empty again.
- `Oraclerogue`: one bandage charge spent (18 -> 17), finished in arena.
  `Testmelee` finished in tutorialb, the zone it started the session in.
  `Testbuddy` was moved arena -> tutorialb by the official client's own
  `ZoneToBindPoint`.
- Two `Testmelee` corpses (`character_corpses` 22, 23) predate this session and
  were left alone.
- **eqemu taken down, lock released, official client and its Xvfb shut down.**

## Deviations

1. **No plan was committed before the runs.** This unit was executed directly
   from the owed-probe list in memory. The probe designs are in this document
   and the evidence is complete, but nothing was pre-registered, so the designs
   could in principle have been shaped by what the runs showed. They were not —
   probe 1's seeding decision is the only design change made mid-unit, and it
   made the probe stricter, not weaker.
2. **Probe 3's reverse leg (A inspects B) did not run** — the corpse substring
   trap. It would have exercised the same code with an empty-equipment answer.
   The forward leg proves the path, so it was not re-run.
3. **Probe 2 leg A seeded raid state in the DB.** The packets are the server's
   own, but the raid they describe never arose from gameplay. An invite-and-
   accept raid is impossible from this client today by design (T168, T169).

## Still owed

- **T152's null-deref confirmation** — a zero-length `OP_Bind_Wound` null-derefs
  inside the zone process. Needs a server nobody else is using, and is worth
  reporting upstream either way. **This is now the only live debt in the arc.**
- The two defects above, neither fixed here.
- A targeting fix for the corpse substring trap.
