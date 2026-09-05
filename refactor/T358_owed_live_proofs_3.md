# T358 — Owed live proofs 3: Ultravision landing (T348), Dark Elf zone-in (B056), self-death (T346/T349)

Measurement only; slot2; eqemu lock (take it atomically per the protocol —
it must be ABSENT; three units this week found it absent while "held", so
if it exists, read its contents and STOP with them). Reuse T336's driver
(`/mnt/projects/fable/review/T336/t336_live.sh`) and T338's config
(`maps_path` = `/mnt/projects/maps/base`, `--eqe1-path`). Binary rebuilt
from the slot at the current tip (must contain T348, T346, T349 — check
`git log`). Read `refactor/T345_owed_live_proofs_2.md` §Review first: it
records the rig facts (`#castspell 46` needs no class/gem; the account's
`invulnerable` flag must be OFF or the cast refuses; Escape releases the
chat-focus gate; eqemu never sends an "applied" OP_Buff, which is why T348
moved the landing to OP_Action effect_flag 4).

1. **Ultravision landing (B055, T348):** night (`/say #set time 22 0`),
   `/say #set invulnerable off`, `#castspell 46` on self. Capture before,
   after landing, and after `#nukebuffs`. The zone light must change and
   the log must show T348's landing line and the fade line. Record video
   for the transition (never stills for a transient — step the video).
2. **Dark Elf zone-in (B056, T348):** a Dark Elf character (provisioning is
   automated — see the memory note on eq-oracle provisioning / `#race 6`
   on an existing character is acceptable if it takes effect on zone-in).
   Zone in at night: the racial floor must give Ultravision with no buff.
   Capture + log line. Then `#race 1` (Human) and zone again: Normal.
3. **Self-death (T346/T349):** die once (`/say #kill` on self, or let a
   `#npctypespawn`ed mob kill you — it lands on the invoker). Assert on the
   packet log: Lying appearance sent, NO ClientUpdate while dead, OP_Save
   body = death position (192 bytes), then bind re-entry through a FULL
   zone-in with the world visible afterwards. Video + log excerpt.
4. `/mnt/projects/fable/review/T358/README.md` (one section per proof,
   PASS/FAIL stated first, the exact commands, the log lines). Strike the
   "owed" notes on B055/B056 rows and in T346/T348/T349 plans (in-repo text
   edits: docs-only commit). Restore every flag/race/time you changed;
   `down.sh`; delete the lock.

STOP: lock present; a proof FAILS (then it is a defect: record the evidence
in the README, add a board item B066+, do the remaining proofs, do NOT fix
code in this unit).

## Review

Branch `task/T358-owed-live-proofs-3`, cut from `fable` at `6cbf838a` (verified).
Measurement only — **no source file changed**; the only in-repo edits are the
strikes on `refactor/BOARD.md`, `refactor/T346_self_death_sequence.md`,
`refactor/T348_buff_landing_producer.md` and `refactor/T349_op_save_body.md`,
plus this section. `build-gles2` was rebuilt from this slot first
(`scripts/build-gles2-x86.sh`, exit 0) so the client holds `willeq.db` version 6.

**All three proofs PASS.** Full evidence, every log line and every measured
number, is in `review/T358/README.md`; this section is the summary and the
deviations.

| # | proof | verdict | the decisive line |
| --- | --- | --- | --- |
| 1 | Ultravision landing (B055/T348) | **PASS** | `Buff landed (OP_Action): spell=46 caster=282 level=20 ticks=360 slot=0` -> `recomputed vision = 2` -> `Vision upgraded to: Ultravision`, and the zone light steps **+4.9%** at that frame |
| 2 | Racial vision floor (B056/T348) | **PASS** | `Player race 6 -> base vision: Ultravision` at zone-in with no buff (and `race 1 -> Normal`), the line absent from all five T345 logs |
| 3 | Self-death (T346/T349) | **PASS** | the qeynos zone process' own C->S log: `OP_SpawnAppearance Size [10] 0F 01 0E 00 73 00 00 00`, `OP_Save Size [194]`, zero `OP_ClientUpdate` until `OP_ClientReady`, then a full zone-in |

### Three findings, boarded, no code touched (per the contract)

- **B066 (A)** — the vision->light switch in `sim_animation.cpp` is **swapped**
  against `VisionType`/`computeVisionType` (`case 1: // Ultravision`,
  `case 2: // Infravision`, where the enum is Infravision=1, Ultravision=2). An
  Ultravision landing therefore gets Infravision's 0.75 intensity + 0.3 red
  shift. Measured live, not inferred: R +5.5%, G +5.2%, **B +0.4%** across the
  landing (R/B 1.3618 -> 1.4303).
- **B067 (A)** — vision reaches only the 218 data-only zone point lights and
  never the global ambient, so a full Ultravision buff moves an outdoor night
  frame **4.9%** while the day/night cycle moves it **4x**.
- **B068 (B)** — `SendSpawnAppearance`/`SendAnimation` bypass `PacketSender`, so
  `--record-session` is a **false negative** for them (a `.sleq` of a death that
  ran T346 step 1 has zero C->S `OP_SpawnAppearance`), and the bypass reorders
  the death burst so the server receives the appearance BEFORE `OP_Save`, against
  T349's stated oracle order.

### Deviations from the contract, stated

1. **`#race 6` was not used.** `Testcaster` (character 67) is already a Dark Elf
   (race 6) and sits on the same account as `Testchar` (Human, race 1), so proof
   2 is two logins and **zero server race writes**. Strictly better than the
   contract's option.
2. **`/say #kill` on self does not work as written.** Bare `#kill` needs a target
   and prints only its usage; the id form (`#kill <own spawn id>`, taken from the
   client's `Zone connection complete! ... (spawn_id=NNN)` line) is what kills
   you. `account.invulnerable = 1` also refuses the kill, exactly as T345 found
   it refuses `#castspell`.
3. **The packet-log assertion was taken from the SERVER, not from
   `--record-session`.** The contract says "assert on the packet log"; the
   client's own recorder cannot see the Lying appearance at all (B068), so
   asserting on it would have produced a false FAIL. Server-side packet logging
   (`logsys_categories` 83/84 `log_to_file = 3`, restored to 0) was enabled
   instead — the same non-circular observer T311 Leg A used, and the same
   preference the two-client memory expresses.
4. **`up.sh --launcher` (the ~25-process fleet) rather than a single zone**, because
   the death path zones to the bind point (qeynos) and proof 2 uses Befallen.
   The fleet was restarted once mid-unit so every zone process would boot with
   packet logging on (`#logs reload` did not take in-game).
5. **A like-for-like pixel A/B for the racial floor was attempted and failed.**
   Both characters were `#zone`d to the same Befallen safe point at the same
   forced 22:00; the two frames differ in camera facing as well as race, so the
   pair proves nothing and is reported as a failed measurement rather than as
   evidence. Proof 2 rests on the log line.
6. **One driver grab was discarded rather than shipped.** The death session's
   `at_death` `import` landed during the loading-screen swap and produced a
   913-byte blank; it was deleted and replaced by three frames pulled from the
   frame dump at known timestamps.

### The rig lesson worth carrying forward

**T345's blind `chatline` silently loses its text.** T358's first session
submitted `/` instead of `/say #set time 22 0`, so the whole "night" measurement
ran in **daylight** (grey mean 0.187 against 0.045 at real 22:00) and every
conclusion from it would have been wrong. `review/T358/t358_live.sh`'s `chatline`
now verifies each line against the client's own `Chat submit: '<exact text>'` log
line and retries; every session in the README reports `chat OK`. **A live driver
must not send a chat command without checking that it landed.**

And, a third time: **stills could not resolve the vision change** (2-3% spread
against a 1.3% still-to-still noise floor) while the stepped video resolved it
cleanly with non-overlapping bands. The state was persistent; it was the *effect
size* that was below the still-to-still noise.

### Server state

Recorded on arrival and restored by hand, each verified by SELECT: `Testchar`
back to `zone_id=2, x=-74, y=428.04, z=3.75, h=160`; `Testcaster` back to
`zone_id=36, x=-259, y=-1201, z=-5.75, h=0`; `account willeq_test` back to
`invulnerable=1`; `logsys_categories` 83/84 back to `log_to_file=0` (92/95 never
touched). No race changed. `down.sh` exit 0; **lock deleted**.

### STOPs

None fired. No proof failed; the three findings above are defects found on the
way, boarded and left alone as the contract requires.
