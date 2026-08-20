# T155 — Clear the wire-probe debt (T149, T150, T152, T153)

**Slot: slot1. x86. eqemu: ALREADY UP, lock ALREADY HELD by the orchestrator.
No Pi.**

**Do NOT run `scripts/eqemu/up.sh` or `down.sh`, and do NOT touch
`/mnt/projects/fable/.eqemu-lock`.** The stack was brought up for you
(loginserver:5998, world:9000, zone `arena`) and the orchestrator takes it down.
`scripts/eqemu/status.sh` is yours to read freely. If the zone process dies,
restart just the zone — and **say so in the Review**, because one of the probes
below can legitimately kill it.

**Territory: `refactor/T155_probe_debt.md`, `/mnt/projects/fable/review/T155/`,
`data/captures/` (gitignored), and — ONLY for a sender that a probe proves
broken — that sender's own body. No action-layer restructuring, no new
commands.**

Four units merged without wire verification. This unit converts as much of that
debt as it can into observed evidence, and reports the rest as still owed. Read
each unit's Review first: `refactor/T149_tradeskill_recipes.md`,
`T150_social_find_family.md`, `T152_skill_opcode_audit.md`,
`T153_consider_corpse_and_face.md`.

## The standing evidence rule — this is the whole point of the unit

**Nothing is accepted on a local "sent OK" log line.** Most of these handlers
bare-return on a size mismatch with no server-side complaint, so silence proves
nothing. Every row of your result table names an effect observed by **the server
log, the database, or a second client**.

**A probe in this arc has already faked a result once** (see the resume memory's
INSTRUMENTATION WARNING). Three outcomes are allowed per probe and only three:

- **VERIFIED** — with the external evidence quoted.
- **BLOCKED** — with the exact gate that stopped it (missing class, missing
  item, missing NPC, no second client). A blocked probe is a *good* outcome
  honestly reported; the debt simply stays owed.
- **FAILED** — the packet was rejected or the effect did not happen. This is the
  expected outcome for roughly one in two, per the arc's measured rate. **Report
  the defect; fix it only if it is the sender's own body.**

Do not convert a BLOCKED into a VERIFIED by reasoning from source. Source
reading is what created this debt.

## Server facts, established by the orchestrator — do not re-derive

Characters on `willeq_test` (account status 200): `Testchar` (warrior, L2,
zone 36), `Testmelee` (warrior, L1, zone 77), `Testcaster` (wizard, L1).
`willeq_buddy`: `Testbuddy` (magician, L1, zone 77). **There is no Rogue and no
poison item anywhere in the roster.**

Three handler facts read from eqemu source at
`/mnt/projects/claude/EQEmu/zone/client_packet.cpp`, already checked:

1. **`Handle_OP_ApplyPoison` (:3016) ALWAYS replies** with an `OP_ApplyPoison`
   echo carrying `success` and `inventorySlot`, whatever your class or item —
   the rogue/poison test only decides whether `success` is 1 or 0. **So the
   round trip is probeable TODAY with a warrior and no poison**, and that round
   trip is exactly what T152's fix owes: right size, accepted by the handler,
   echo parsed by `ZoneProcessApplyPoison` at offset 6. A `success=0` echo is a
   PASS for this unit. Do not create a Rogue for it.
2. **`Handle_OP_PickPocket` (:11913) returns early on
   `!HasSkill(SkillPickPockets)`** and sends nothing, so with no Rogue this one
   is **BLOCKED** — expect to report it that way. ⚠ It also writes a
   **`POSSIBLE_HACK` player-event log** against the player for an out-of-range
   or too-fast attempt. Do not brute-force it.
3. **`Handle_OP_Bind_Wound` (:4013) is the T152 upstream hazard**: it logs a
   size mismatch and does **not** return. Our body is now non-empty so `pBuffer`
   exists and the null-deref path is closed — but **if the zone process dies
   during this probe, that is the upstream eqemu bug confirming itself.**
   Record it with the log, restart the zone, move on. Do not chase it, do not
   patch eqemu.

## The probe matrix — run in this order, cheapest and safest first

Order matters: each group is independently reportable, so a blocker late in the
list cannot cost you the earlier evidence.

**Group 1 — T153 (single client, no gates).**
- `/consider` a living NPC — regression check, must still work exactly as before.
- `/consider` a **corpse** — the new route. Make a corpse the cheap way
  (`/say #kill` on an NPC with the GM account, T148's precedent for driving
  server-side `#` commands through `/say`). Evidence: a `CORPSE_DECAY_*` /
  `CORPSE_REZ_*` message from the server. T153 established there is **no reply
  packet** — the message IS the effect, do not look for one.
- `/appearance` — evidence is **`character_data.face` (and the other six fields)
  changing in the DB**, plus the server's `FACE_ACCEPTED` message. T153 already
  established a second client will NOT see it (`OP_SetFace` has no Titanium
  opcode); do not spend a second client proving that again.

**Group 2 — T152's five skills (single client, one is blocked).**
ApplyPoison (echo, per fact 1 above), BindWound (per fact 3), Disarm, Taunt,
PickPocket (expect BLOCKED, per fact 2). Taunt and Disarm need a living NPC
target in range. For each: did the packet clear the size gate, and what did the
server do?

**Group 3 — T150's social/find family.**
`/groupassist`, `/track`, `/find`, `/friendswho`. Several are skill- or
group-gated; `/track` needs a tracking class. **T150 already recorded that
`SendFriendsWho`'s payload can never match** (eqemu `strchr`s the whole buffer)
— probe it anyway and confirm the failure mode on the wire, since a confirmed
negative is evidence too.

**Group 4 — T149's tradeskill recipes.** Needs a tradeskill container (T144's
`Object_Struct` work). `SendRecipesFavorite` is already known to be structurally
impossible (variable-length body vs an exact 2008-byte gate) — same rule: confirm
the failure on the wire rather than restating the source claim.

**Stop when the matrix is done or a group is genuinely blocked.** Do not invent
extra probes; do not go after batch C or D.

## Method

T148's, verbatim where it fits: `build/bin/willeq` headless,
`-d 4 --log-level=DEBUG --script <file>`, driven from a single **foreground**
driver that `wait`s on every client it launched. **Never end your turn on a
background process.** Configs/scripts/logs under `data/captures/` (gitignored).
Annotated evidence — logs, DB before/after `SELECT`s — into
`/mnt/projects/fable/review/T155/` with a README, per the review-media
convention.

**`--log-level=INFO` is a Pi rule, not an x86 one; DEBUG is correct here.**

## Server state — leave as found

`SELECT` the affected `character_data` rows before and after and put both in the
Review. `/appearance` deliberately mutates a character's face — **restore it**,
and show the read-back. Same for anything else you change. Do not create or
delete characters; do not edit account status.

## Verification

Build + full suite in the slot before you start probing (a stale binary
invalidates every result) and again at the end if you changed any source.
Baseline **2542/2543** at tip `a10f3cf1`, sole expected failure the documented
ALSA `AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- A probe would need a character, class, item or account change beyond the
  face restore above — report BLOCKED instead.
- A defect turns out to be outside the probed sender's own body (report it; do
  not fix it here).
- The zone server dies twice in the same probe.

## Review

(executor fills this in)
