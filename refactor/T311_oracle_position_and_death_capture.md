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
