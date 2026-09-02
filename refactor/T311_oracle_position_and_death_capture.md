# T311 — Copy the official client's position on the wire (Will's rulings 3 + 4, 2026-09-01)

**Slot: slot1. Branch `task/T311-legA` cut from `fable`. Owns eqemu
(noclobber lock) AND the Wine/Xvfb official client. No Pi.**

## Will's rulings this unit executes

3. **The official client is the golden. Update willeq to match what the
   official client sends for player position.**
4. **Record a packet capture of an official client session that includes a
   DEATH, and check the position packets sent and received.**

Both come from T310 Leg C's finding (deferred.md, "TIER 2 addendum", item 26):
at a qeynos2 spot where the server's own raycast puts collision ground at
`Best Z = -0.00`, the official client's `#loc` reads **3.75** across three
readings in two sessions -- exactly EQEmu's `Mob::GetZOffset()` for a default
size-6 humanoid (`zone/waypoints.cpp:881-1006`, `0.2 * size * k`) -- while
fable transmits **3.00**, feet 0.119 BELOW that ground. `Mob::FixZ`
early-returns for clients and nothing passes `fix_client_z = true`, so
whatever a client sends is what every other player sees.

## Read first

- `refactor/T310_camera_match_against_oracle.md` (Leg B Part 1 = the server
  never adjusts a client's z; Leg C = the 3.75 vs 3.00 measurement)
- `refactor/T298_official_client_fp_camera_baseline.md` and
  `refactor/H05_official_client_oracle.md` -- how the official client is
  driven under Wine/Xvfb, and how captures are taken
- `refactor/R01_recorder.md` / `R02_replay_harness.md` -- the existing pcap
  recorder and replay harness; **reuse them, do not write a new capture path**
- The memory `match-the-oracle-dont-infer-it` and
  `two-client-live-testing` (single-client verification is circular)
- deferred.md item 15 (`AT_DIE` / `POS_DEAD`) -- Leg A's death capture is the
  evidence that item has been missing; it is a product question ONLY once we
  know what the oracle actually does.

## LEG A — capture and characterise (THIS DISPATCH). No production code.

1. **Provision.** Use the automated account/character path (memory
   `eq-oracle-provisioning`; `oraclerogue` acct `willeq_rogue`, char 81 exists).
   A character that can be killed quickly is what matters -- `#kill` from a GM
   session is acceptable and preferred over hunting, but SAY which was used.
2. **Capture.** One official-client session, pcap recorded end to end, that
   contains, in order and timestamped: login/zone-in, standing still at a
   surveyed spot, walking, jumping if reachable, **death**, the corpse state,
   and the respawn/bind-point arrival. Record the server side too
   (`#loc`, zone log) so every packet has ground truth beside it.
3. **Decode and tabulate.** For EVERY position-bearing opcode in both
   directions -- at minimum `OP_ClientUpdate` / `SpawnPositionUpdate_Struct`,
   `OP_SpawnAppearance` (esp. `AT_DIE`), `OP_Death`, `OP_ZoneEntry`'s pose --
   produce a table: opcode, direction, t, raw field values, decoded
   x/y/z/heading/deltas/animation, and the server's own `#loc` at that moment.
4. **Answer these exactly, with the packet bytes as evidence:**
   - What z does the official client PUT ON THE WIRE relative to collision
     ground? Is it `ground + GetZOffset()` (0.2*size*k), or something else?
     Confirm or refute the T310 Leg C hypothesis -- it was inferred from `#loc`,
     which is the SERVER's view, not the wire.
   - Does that offset change with size/race? Take at least two races if the
     provisioning cost is low; if not, say so and mark it unmeasured.
   - **On death: what does the client send, what does it stop sending, and what
     does the server send back?** Specifically -- does the client keep sending
     position updates while dead? Does its z change? Does `AT_DIE` arrive for
     SELF, and with what payload? What ends the dead state on the wire?
   - What does the client send on respawn/zone-in at the bind point?
5. **STOP and report.** Leg A changes no production code. Deliverables: the
   pcap (under `review/T311/legA/`), the decode tables, and a written
   `## Review` in this file. Name the exact constant/expression fable must
   adopt, and the exact sites that must change -- do not change them.

**⚠ Do not sample a transient with stills** (memory
`dont-sample-transients-with-stills`): the death sequence is a transient.
Record video of the oracle's screen alongside the pcap and step it.

## LEG B — implement the match (NOT this dispatch; scoped from Leg A's answer)

The known sites, from T310 Leg B's "found not fixed" note, are three
disagreeing z frames in one subsystem -- fix them together or say why not:
- `src/client/eq_entities.cpp:416,1184` subtract `size/2` from the player's own
  z on a comment claiming `SendPositionUpdate` adds it back. **It does not**
  (`src/client/eq_movement.cpp:1100` sends z directly).
- `src/client/spell/spell_cast_checks.cpp` adds it back for its LOS ray --
  double-counting.
- `src/client/eq_movement.cpp:1741` uses `PosZ() + 6.0` for water detection.

Acceptance for Leg B when it is dispatched: a two-client live session
(memory `two-client-live-testing`) where the OBSERVING instance sees fable's
player at the same height as the official client at the same surveyed spot,
plus a unit test pinning the offset expression against the mesh/size inputs.

## Territory

`review/T311/`, this file, capture tooling under `scripts/`/`tools/` only if
an existing recorder genuinely cannot express the run (justify in the Review).
No `src/` changes in Leg A.

---

# Review — Leg A (slot1, `task/T311-legA`, 2026-09-02)

**Leg A changed no production code.** Deliverables:
`/mnt/projects/fable/review/T311/legA/` (pcap, video, the two zone packet logs,
the decode tables, screenshots, the driver scripts, and a README that maps every
file), plus this section.

The plan's step-4 questions are answered below, each with the packet bytes.

## How the wire was read, and why it is trustworthy

The pcap (`T311A_official_client_session.pcap`, **73,506 UDP packets**,
01:01:40–01:37:02 UTC, `-i any -s 0`, both directions, login+world+zone) is the
raw record. Titanium traffic sits inside EQStream (sequencing, combining,
compression), so the *decode* comes from the zone process' own packet log with
EQEmu's `Logs::PacketClientServer` / `PacketServerClient` at **Detail** level,
which appends `DumpPacketToString` — i.e. the same packets, hex-dumped
server-side, with opcode names attached.

**That log is a faithful record of the client's transmitted values**, because
the Titanium decoder for this opcode does no arithmetic:
`DECODE(OP_ClientUpdate)`, `common/patches/titanium.cpp:2633-2653`, is
`IN(spawn_id) IN(sequence) IN(x_pos) IN(y_pos) IN(z_pos) IN(heading)
IN(delta_*) IN(animation)` and nothing else. `DumpPacketToString`
(`common/eq_packet.cpp:382`) dumps the full `app->size` payload
(`Size [n]` in the log line is `Size()` = payload + 2 opcode bytes).

Logging was enabled by `logsys_categories.log_to_file = 3` on categories 83, 84,
92, 95 plus `#logs reload`, and **all four were restored to 0** at the end
(they were all 0 before; the before/after values are in the Server state section).

## Q1 — What z does the official client PUT ON THE WIRE relative to collision ground?

**`z = groundZ + 0.625 * size`**, i.e. exactly `0.2 * size * 3.125`, plus a
small upward collision epsilon of **+0.001 to +0.003**.

The canonical packet. Human male, size 6, qeynos2 `(-74, 428)`, where the
server's own raycast reports `Best Z for Yourself | -0.00`:

```
[09-01-2026 20:03:43] [Zone] [Packet C->S] [OP_ClientUpdate] [0x14cb] Size [48]
   0: 46 01 11 01 00 00 00 00 - 00 00 00 00 00 00 80 07  | F...............
  16: 00 00 00 00 94 C2 00 00 - 00 00 64 10 70 40 00 00  | ..........d.p@..
  32: D6 43 00 00 00 00 00 00 - 00 00 00 00 00 00        | .C............
```

decoded against `PlayerPositionUpdateClient_Struct`
(`common/eq_packet_structs.h:1419`):

| field | offset | bytes | value |
|---|---|---|---|
| sequence | 0 | `46 01` | 326 |
| spawn_id | 2 | `11 01` | 273 |
| delta_x | 10 | `00 00 00 00` | 0.0 |
| heading (12b) | 14 | `80 07 00 00` | 1920 raw (= `#loc` 480.00, so the field is heading x 4) |
| **x_pos** | 18 | `00 00 94 C2` | **-74.0** |
| delta_z | 22 | `00 00 00 00` | 0.0 |
| **z_pos** | 26 | `64 10 70 40` | **3.75100040435791** |
| **y_pos** | 30 | `00 00 D6 43` | **428.0** |
| animation | 34 | `00 00 00 00` | 0 |
| delta_y | 38 | `00 00 00 00` | 0.0 |
| delta_heading | 42 | `00 00 00 00` | 0 |

The character's stored `character_data.z` was **3.0** before this login. The
client's very first update already carries 3.751. **The client, not the server,
sets this.**

### The sweep that pins the expression

`#size` (`#feature size`) was applied to the same character standing still, and
the transmitted z tracked it exactly. Every row is the settled C→S value with
the raw float bytes; ground truth is `#loc`'s `Best Z` at that moment.

| where | ground | size | wire z (float hex) | wire z (value) | `0.625 * size` | residual |
|---|---|---|---|---|---|---|
| qeynos2 | 0.00 | 6 | `64 10 70 40` | 3.7510004 | 3.750 | +0.0010 |
| qeynos2 | 0.00 | 12 | `2f 08 f0 40` | 7.5009990 | 7.500 | +0.0010 |
| qeynos2 | 0.00 | 3 | `55 62 f0 3f` | 1.8780009 | 1.875 | +0.0030 |
| qeynos2 | 0.00 | 7 (Barbarian) | `31 08 8c 40` | 4.3759999 | 4.375 | +0.0010 |
| qeynos (bind) | 28.00 | 6 | — | 31.7510 | 31.750 | +0.0010 |
| qeynos (bind) | 28.00 | 12 | — | 35.5000 (`#loc`) | 35.500 | 0 |

Three sizes, two races, two ground heights (0.00 and 28.00). The `#loc`
screenshots for each row are `T311A_02/06/07/10/11/17_*.png`.

**The residual is a collision epsilon, not part of the constant.** Distinct
settled z values seen for size 6 across the session were 3.7510, 3.7520,
3.7530 and 3.7509995 — the value creeps up ~0.001 per landing (classic
push-out-of-the-floor resolution) and resets when size changes. Take
`0.625 * size` as the constant and treat ±0.003 as the error bar.

### T310 Leg C's hypothesis: CONFIRMED, and now with the reason

Leg C inferred `ground + GetZOffset()` from `#loc`, which is the server's view.
On the wire it is confirmed: 3.751 at a ground of 0.00. But the agreement with
EQEmu is a **coincidence of EQEmu's default constant**, not a shared rule —
see Q2.

## Q2 — Does the offset change with size/race?

**With SIZE: yes, exactly linearly** (table above; `#size 3 / 6 / 12` gave
1.878 / 3.751 / 7.501).

**With RACE: no, beyond race's effect on default size.** Measured two ways:

1. **A genuinely different-race character.** `Oraclebravo`, race 2 Barbarian
   male, default size 7, moved by DB write to the *same* qeynos2 spot with
   `character_data.z = 3.0`. On zone-in the client settled at **4.376**
   (`31 08 8c 40`) = `0 + 0.625 * 7 + 0.001`. `#loc`:
   `XYZ: -74.00, 428.00, 4.38 | Best Z 0.00 | Fixed Z 4.38`
   (`T311A_17_barbarian_self_loc.png`).
2. **`#feature race N` with size held at 6**, standing still at the bind spot
   (ground 28.00): races **2 (Barbarian), 8 (Dwarf), 12 (Gnome)** all left the
   transmitted z at **31.75**, unchanged. The Dwarf case is the discriminator:
   the server's own `Fixed Z` moved to **28.84** (EQEmu's Dwarf constant is
   `0.7`, so `0.2 * 6 * 0.7 = 0.84`) while **the client kept sending 31.75**.
   `T311A_12_race_8.png`.

**So the client does NOT use EQEmu's per-race table.** It behaves as if every
model used the default constant 3.125. For all races whose EQEmu constant is
3.125 (which is every playable race except Dwarf) the two agree exactly; for a
Dwarf they disagree by `0.625 - 0.14 = 0.485 * size / 6`. Since the server never
writes a client's z (T310 Leg B, `zone/waypoints.cpp:831-834`), that
disagreement only shows up in `#loc`'s `Fixed Z`, never in gameplay.

**Honest limits.** (a) A real Dwarf *character* was not tested — only a Dwarf
illusion on a Human; provisioning one is a `create-character.sh` run and was not
spent. (b) The race-10 (Ogre) and race-128 (Iksar) rows of the illusion sweep
are **VOID**: the packet log shows the character died at 20:20:04 and 20:20:24,
seconds after each of those two commands (C→S `SpawnAppearance type=14
param=115`, plus corpse rows 34 and 35 in `character_corpses`), so the client
re-zoned and the illusion was cleared before the `#loc` was read. Those two
readings measured a plain Human. Races 2, 8 and 12 have no death between the
command and the reading and are valid. The Ogre/Iksar-illusion-kills-a-level-1
behaviour was not chased.

## Q3 — On death: what does the client send, what does it stop sending, and what does the server send back?

**The dead state on the wire is effectively zero-length.** There is no interval
in which the client is alive-but-dead and transmitting. Everything below happens
inside ONE second.

Death #1, `#damage 100000` on a self (F1) target, qeynos2, 20:12:31, in order:

```
S->C OP_CancelTrade      11 01 00 00 07 00 00 00
S->C OP_PreLogoutReply   (empty)
S->C OP_BecomeCorpse     11 01 00 00 | 1e 9d 1e 44 | 0f 10 20 c3 | 60 10 70 40
S->C OP_Death            11 01 00 00 11 01 00 00 11 01 00 00 01 00 00 00
                         ff ff ff ff 04 00 00 00 a0 86 01 00 00 00 00 00
S->C OP_ZonePlayerToBind 01 00 00 00 | 00 00 ff c3 | 00 00 64 42 | 00 00 fe 41
                         | 00 00 00 00 | "South Qeynos\0"
C->S OP_FloatListThing   1e 9d 1e 44 0f 10 20 c3 fc d4 b4 40 02 fc 95 3d 0f 1e
C->S OP_Save             3f 05 4d 1b 15 | 1e 9d 1e 44 | 0f 10 20 c3 | 60 10 70 40 ...
C->S OP_SpawnAppearance  11 01 | 0e 00 | 73 00 00 00
C->S OP_ZoneChange       "Oraclealpha" ...
S->C OP_ZoneChange x2, OP_LogoutReply, OP_DeleteSpawn
```

Decoded:

* **`OP_BecomeCorpse`** (`BecomeCorpse_Struct`, `eq_packet_structs.h:1378`):
  spawn 273, y `1e 9d 1e 44` = 634.455, x `0f 10 20 c3` = -160.0627,
  **z `60 10 70 40` = 3.7509995**. **The corpse is spawned at exactly the z the
  client was transmitting — the +0.625*size is NOT removed.** Confirmed
  independently in the DB: `character_corpses` row 33 is
  `zone 2, (-160.063, 634.455, 3.751)`.
* **`OP_Death`** (`Death_Struct`, `eq_packet_structs.h:1366`): spawn_id 273,
  killer_id 273, corpseid 273, bindzoneid **1**, spell_id `0xFFFFFFFF`,
  attack_skill 4, **damage 0x000186a0 = 100000**, unknown 0. It carries **no
  position at all.**
* **`OP_ZonePlayerToBind`** (`eq_packet_structs.h:5227`): bind_zone_id 1,
  instance 0, x `00 00 ff c3` = **-510.0**, y `00 00 64 42` = **57.0**,
  z `00 00 fe 41` = **31.75**, heading 0.0, name `"South Qeynos"`. The z is the
  `character_bind` row verbatim (which itself is a ground+3.75 value, ground
  there being 28.00).
* **`OP_SpawnAppearance` C→S, `11 01 | 0e 00 | 73 00 00 00`** = spawn 273,
  **type 14 = `AppearanceType::Animation`, parameter 115 = `Animation::Lying`**
  (`common/eq_constants.h:30,74`). **This is the only appearance the client
  sends on death.**

### Does the client keep sending position updates while dead? NO.

Last C→S `OP_ClientUpdate` in qeynos2: **20:12:30**, z = 3.750999 (a normal live
value, unchanged). Next C→S `OP_ClientUpdate` in that zone: **20:28:46** — the
*other* character's login sixteen minutes later. **1102 logged records span the
gap and not one is a position update.** Its z therefore never changes on death,
because it never sends another one.

### Does AT_DIE arrive for SELF? NO — it does not appear at all.

`AppearanceType::Die` is **0**, and its own comment in `common/eq_constants.h:24`
reads *"Causes the client to keel over and zone to bind point (default action)"* —
it is a server→client **command**, not a notification. Across **both** zone logs
(the whole session, both directions, all 71 `OP_SpawnAppearance` records) there
are **ZERO with type 0**. EQEmu drove this death with `OP_Death` +
`OP_ZonePlayerToBind` instead. So `AT_DIE` for self is **not** part of the
observed death sequence; **deferred.md item 15's `AT_DIE`/`POS_DEAD` question is
answered as "the official Titanium death sequence does not use it"** — at least
for a death the server initiates this way.

### What ends the dead state on the wire?

**The client itself, immediately: it sends `OP_ZoneChange` and does a full
zone-in handshake.** It does this **even when the bind point is the zone it is
already in** — death #2 (`#kill`, in the bind zone, 20:22:43) produced the exact
same sequence, an `OP_ZoneChange`, a loading screen and a fresh `OP_ZoneEntry`
two seconds later. (This also explains T310 Leg C's note that `#goto` timed the
oracle client out: a same-zone move still costs the client a full zone-in.)

Death #2's packets are identical in shape; the only differences are
`OP_Death.damage = 0` (`Mob::Kill()` bypasses the damage path) and
`OP_BecomeCorpse` z = `08 02 fe 41` = **31.750992** (ground 28.00 + 3.751).

### The corpse and the visual transient

`T311A_18_death1_contactsheet.png` steps the recorded video at 4 fps across the
death. There is **no keel-over animation of the player's own model**: on one
frame the model is labelled `Oraclealpha`, on the next it is gone and a corpse
object labelled `Oraclealpha's corpse` (green) is lying on the ground, with
`Entering South Qeynos...` already on screen. That matches the packets exactly —
`OP_BecomeCorpse` spawns a *separate* corpse entity and the player's own spawn is
deleted. Level 1 leaves a corpse (`character_corpses` rows 33-36) but no items:
the client's own Death Tip says *"if your character is level 9 or under, you have
revived with all of your items and with no experience loss"*, and `/corpse` in
the bind zone answered *"You do not have any corpses in this zone."*

## Q4 — What does the client send on respawn / zone-in at the bind point?

**The same rule, applied within about one second of `OP_ClientReady`.** The
client first echoes whatever pose the server handed it, then overrides it with
its own `ground + 0.625 * size`.

Bind arrival after death #1 (South Qeynos, ground 28.00, Human size 6):

```
20:12:40  C->S OP_ClientUpdate   x=-510.000 y=57.000 z=32.5000    <- echoes the server's spawn pose
20:12:41  C->S OP_ClientUpdate   x=-510.000 y=57.000 z=31.7510    <- the client's own value; holds from here
```

Oraclebravo's fresh login (Barbarian size 7, qeynos2, ground 0.00,
`character_data.z` deliberately left at **3.0**):

```
20:28:46  C->S OP_ClientUpdate   z=3.7500   <- the z the server put in OP_ZoneEntry
20:28:46  C->S OP_ClientUpdate   z=3.0000   <- the z from the PlayerProfile / DB
20:28:47  C->S OP_ClientUpdate   z=4.3760   <- the client's own value; holds from here
20:28:48  C->S OP_ClientUpdate   z=4.3760
```

That third line is the whole finding in one packet: **handed 3.0, the official
client transmits 4.376 one second later, unprompted.**

The zone-in handshake itself carries no position from the client: C→S
`OP_ZoneEntry` is `ClientZoneEntry_Struct` (checksum + character name, 68 bytes);
`OP_ReqClientSpawn`, `OP_SetServerFilter` and `OP_ClientReady` are all
position-free. The pose flows S→C only, in `OP_PlayerProfile` and the self
`OP_ZoneEntry` spawn struct.

**One loose end, reported not chased:** the z in the S→C self `OP_ZoneEntry`
spawn struct was **+0.75 above the stored value in every case** — 3.0 → 3.75 for
Oraclebravo, and 31.75 → 32.5 at the bind point on all four arrivals. A grep for
an obvious `+= 0.75` in the zone source did not find it. It is server-side, it is
overwritten by the client within a second, and it does not affect anything in
this leg's answers.

## Also measured, because they were free

* **Walking (5 s hold, flat ground):** z holds at exactly 3.7520 for the whole
  traverse; `animation` reads **27** while running and 0 at rest; `delta_x` /
  `delta_y` are populated and `delta_z` stays 0. Table 1 rows 20:08:28–20:08:44.
* **Jumping:** the client sends **one** mid-air sample per jump — 3.753 →
  **6.8037** (`delta_z` 1.017) → 3.753. The apex is +3.05 above the standing z.
  It also sends `OP_Jump`, and brackets each jump with C→S
  `SpawnAppearance type=14 param=111 (Crouching)` then `param=100 (Standing)`.
* **Heading:** the 12-bit `heading` field carried **1920** where `#loc` read
  **480.00** — the field is `heading * 4`, not the bare 0-512 value. Noted
  because fable packs this field itself (`eq_movement.cpp:1113`); **not
  verified against fable's own encoding in this leg.**

## THE EXPRESSION fable MUST ADOPT

```
    wireZ = groundZ + 0.625f * size          // == 0.2f * size * 3.125f
```

where `groundZ` is the collision ground under the player and `size` is the
player's current size (the value the server sends in the spawn struct and in an
`OP_ChangeSize`/illusion, i.e. `GameState::player().size()`). No race term. The
+0.001..0.003 the oracle adds is collision noise and must **not** be reproduced.

Today fable transmits `groundZ + collisionZOffset`, where `collisionZOffset` is
the mesh's own origin-to-feet distance (`-bbox.MinEdge.Y * scale`, **3.004** for
a Human male, T310 Leg B). **The two are different quantities and they differ by
0.746 for a Human male** — which is the 3.00-vs-3.75 gap T310 Leg C measured.

The important structural point for Leg B: **`0.625 * size` is not any landmark of
the shipped mesh.** It is neither the origin-to-feet distance (3.004) nor half the
bbox height (3.339) nor the crown (3.674). So the wire z and the render node's
origin cannot remain the same variable — Leg B has to separate them:

```
    render node origin z = groundZ + collisionZOffset      // unchanged, feet on ground
    wire z               = groundZ + 0.625f * size
                         = nodeOriginZ - collisionZOffset + 0.625f * size
```

and, symmetrically, **inbound**:

```
    other entity's node origin z = wireZ - 0.625f * size + collisionZOffset
```

**The inbound half is an INFERENCE, not a measurement.** It follows from the
outbound rule plus the requirement that the two clients draw the same body at the
same height, and it is exactly what the plan's Leg B acceptance test (a two-client
session where the observing instance sees fable's player at the official client's
height) will discriminate. Do not land it as settled without that test.

## THE EXACT SITES

Line numbers are on `fable` at `e68f8aa5`.

**Outbound (what fable puts on the wire) — the change Will's ruling 3 asks for:**

| # | site | what is there now |
|---|---|---|
| 1 | `src/client/graphics/renderer_player_physics.cpp:315, 365, 382, 406, 450, 469, 487` | every ground snap does `newZ = groundZ + modelYOffset`, where `modelYOffset = entityRenderer_->getPlayerCollisionZOffset()` (`:247-250`). This is the RENDER frame and should stay as it is. |
| 2 | `src/client/graphics/renderer_player_physics.cpp:768` | `intent.z = playerZ_` — the render-frame z leaves the renderer here |
| 3 | `src/client/eq_bridge.cpp:754` | `SetPosLocal(update.x, update.y, update.z)` — the render-frame z becomes the game-state z. **This is the cleanest single place to convert**, or to start carrying wire-z separately. |
| 4 | `src/client/eq_movement.cpp:1101` | `update.z_pos = PosZ();` — the actual send. Comment at `:1100` says "Send Z position directly without offset"; that comment becomes wrong the moment site 3 changes, so fix it in the same edit. |

**Inbound (the three disagreeing frames the plan names) — all three, together:**

| # | site | what is there now |
|---|---|---|
| 5 | `src/client/eq_entities.cpp:416` (ZoneSpawns path) | `SetPosLocal(entity.x, entity.y, entity.z - (entity.size / 2.0f))` under the T310 Leg B warning block at `:412-424`. `size/2` = 3.0 for a size-6 humanoid; the correct term is `0.625 * size` = 3.75, and it must be paired with `+ collisionZOffset` if the result is a render-frame z. |
| 6 | `src/client/eq_entities.cpp:1195` (NewSpawn path) | the identical subtraction, comment at `:1192-1194` |
| 7 | `src/client/spell/spell_cast_checks.cpp:53-54` | `eyeZ = z + (size/2) * kHeadPosition` with the comment at `:32-34` saying the `+size/2` exists only to undo site 5's subtraction. Once 5/6 are correct this double-count has to be re-derived, not just deleted — the server's own LOS uses `GetZ() + size/2 * 0.9` (`zone/aggro.cpp:1283`) and fable should say which frame it is in. |
| 8 | `src/client/eq_movement.cpp:1741-1742` | `constexpr float HEAD_HEIGHT_OFFSET = 6.0f; float headZ = PosZ() + 6.0f;` for water detection, i.e. it treats `PosZ()` as the feet. With wire z at `ground + 0.625*size` the head sits about `0.375 * size` above it (2.25 for size 6), not 6.0. **Also note `headZ` is computed and then never used** — `findRegionForPoint` on the next line is called with `PosZ()`. That is a separate live defect, found here and NOT fixed. |
| 9 | `src/client/graphics/entity_renderer.cpp:2227` | `float targetZ = groundZ + visual.collisionZOffset;` — the NPC interpolation ground snap, and the one place that already states the render-frame rule correctly. It is the model for what sites 5/6 should produce. |
| 10 | `include/client/graphics/entity_renderer.h:24-52` | THE Z CONVENTION note. It is right about the server (T310 Leg B) but says nothing about what the *official client* puts there; it needs the `0.625 * size` measurement and the render-origin/wire-z split written into it. |

**Do not** change `collisionZOffsetFromBounds` or `getPlayerCollisionZOffset` —
T310 Leg B measured those against the meshes and they are the render frame,
which this measurement does not touch.

## Found and deliberately NOT fixed

1. **`eq_movement.cpp:1741` computes `headZ` and never uses it** (site 8). A
   dead local in a live water-detection path. Leg A changes no code.
2. **`#feature race 10` (Ogre) and `#feature race 128` (Iksar) killed a level-1
   character** within ~5 s, twice, reproducibly. Two extra corpses and two extra
   re-zones in the capture; the two illusion readings they contaminated are
   marked VOID above. Not chased.
3. **The unexplained +0.75** between the stored/bind z and the z in the S→C self
   `OP_ZoneEntry` spawn struct (four arrivals plus one fresh login, always
   +0.75). Server-side, overwritten by the client within a second.
4. **`Testchar` was NOT in its documented restore state** when this unit started:
   the executor protocol records `zone_id=36, x=-77.9232, y=21.9086, z=3, h=190`,
   but the DB had `zone_id=2, x=-74, y=428, z=3.751, h=160` — i.e. T310 Leg C's
   qeynos2 pose. **This unit did not touch `Testchar`** and left it as found.
   (Its z of 3.751 is itself corroborating evidence: it is the official client's
   settled value at that exact spot.)
5. **The heading field is `heading * 4`** on the wire (1920 for `#loc` 480).
   fable packs this field at `eq_movement.cpp:1113`; whether it packs the same
   scaling was not checked in this leg.

## Server state

eqemu was **already up** and was left up (no `down.sh`/`up.sh` bounce was needed;
none was taken).

**Lock.** `/mnt/projects/fable/.eqemu-lock` was **already held** — by
`claude orchestrator session, 2026-08-31T01:41:38+00:00, purpose: Will manual
login test from another client`, i.e. ~2 days stale and belonging to the session
that dispatched this unit with explicit eqemu ownership. The atomic noclobber
take failed as designed. Rather than delete another session's lock, this unit
**appended** a takeover line naming T311-legA and preserving the original line
verbatim. The file is left in place at the end of this report for the
orchestrator to clear, because removing it would also remove the orchestrator's
own record.

**Restored:**

| thing | before | during | after |
|---|---|---|---|
| `logsys_categories.log_to_file` for 83/84/92/95 | 0/0/0/0 | 3/3/3/3 | **0/0/0/0** (verified by SELECT) |
| `account.status` for `willeq_b` (id 73) | 0 | 250 | **0** (verified) |
| `character_data` id 74 `Oraclealpha` | zone 2, (-74, 428, 3), h=0 | moved to bind zone 1 by the deaths | **zone 2, (-74, 428, 3), h=0** (verified) |
| `character_data` id 75 `Oraclebravo` | zone 29, (-456, 560, -26.62), h=192 | qeynos2 | **zone 29, (-456, 560, -26.62), h=192** (verified) |
| account `willeq_a` status | 250 | 250 | 250 (untouched) |

**NOT restored, on purpose:** four `character_corpses` rows (ids 33-36, all
`Oraclealpha`, all empty level-1 corpses) created by the deaths this unit was
dispatched to capture. They are evidence — row 33's z of 3.751 is quoted above —
and they decay on their own. Deleting corpse rows risks the
`character_corpse_items` FK for no benefit. `Testchar`, `Oraclerogue` and every
other character were untouched.

## Verification

Leg A is capture-and-characterise: **no `src/` change, so the build/test/visual
bar does not apply and none of it was run.** What was verified instead:

| check | result |
|---|---|
| pcap is real and covers the death | **PASS** — 73,506 packets, 01:01:40–01:37:02 UTC, 54 packets inside the death second 01:12:31 |
| packet logging actually captured both directions | **PASS** — 36,410 records in the qeynos2 log alone; 904 C→S `OP_ClientUpdate` |
| the decode is of the wire, not of a transform | **PASS by source** — `DECODE(OP_ClientUpdate)` is `IN()`-only (`titanium.cpp:2633-2653`) |
| the z measurement is not single-sample | **PASS** — 3 sizes x 2 races x 2 ground heights, plus every packet in table 1 |
| the death transient was recorded as video, not sampled with stills | **PASS** — 10 fps continuous recording of the whole session + two clips + a 4 fps contact sheet |
| server state restored | **PASS** — every row above re-SELECTed after the write |

**Single-client caveat, stated plainly.** This leg is one official client. That
is correct for its question — "what does the official client PUT ON THE WIRE" is
a property of the sender, and the server-side log is an independent observer of
it. But nothing here measures what a *second* client renders from those bytes,
which is why the inbound half of the expression is flagged as an inference and
why Leg B's acceptance is a two-client session.
