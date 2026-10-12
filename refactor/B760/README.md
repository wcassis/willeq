# B760 (issue #45) — what settles a released cast's result

**State (2026-10-12): the wrong-result path is closed by construction and
pinned; it was NOT reproduced live.** The "late" half is inherent to eqemu's
`StopCasting()` and is left open on purpose (see the end).

## The question

T482 releases a confirmed cast (cast bar, glow, gem) at `ManaChange
keepcasting 0` and leaves only the `SpellCastComplete` RESULT pending:
`SpellManager::noteZonePacket()` settled it as Interrupted on the first zone
packet after the keepcasting 0 that was not `OP_MemorizeSpell` /
`OP_LinkedReuse`. B760 asked whether an unrelated packet can arrive inside the
success tail (wrong result) and whether the settle is late.

## Server truth (akk-stack source, `/mnt/projects/akk-stack/code`)

| path | what the caster receives, in queue order |
|---|---|
| success, non-bard (`zone/spells.cpp` `Mob::CastedSpellFinished`, 1803-1824) | SpellFinished's own packets, then `SendSpellBarEnable` = `OP_ManaChange` keepcasting 0 (`Mob::SendSpellBarEnable`, 5752-5766), [`OP_LinkedReuse`], `OP_MemorizeSpell` scribing 3 (`Client::MemorizeSpell`, `zone/client.cpp` 3346-3404) — one function call |
| interrupt (`Mob::InterruptSpell`, 1242-1316) | `OP_InterruptCast` (own copy) then `SendSpellBarEnable` |
| stop after BeginCast (`Mob::StopCasting`, 1351-1377; callers 1423, 1439, 1749) | `OP_ManaChange` keepcasting 0 **and nothing else** |

* All three are RELIABLE: `Client::QueuePacket` / `FastQueuePacket` default
  `ack_req = true` (`zone/client.h` 444-445), and `EQ::Net::EQStream::QueuePacket`
  maps `ack_req` to the reliable stream (`common/net/eqstream.cpp` 116-121).
* The zone is single-threaded, so nothing else is queued to this client
  between the tail's packets: they take consecutive reliable sequence numbers
  (`ReliableStreamConnection::InternalQueuePacket`,
  `common/net/reliable_stream_connection.cpp` 1488-1588) and normally share
  one combined datagram (`InternalBufferedSend`, 1344-1366).
* An unreliable send (`OP_ClientUpdate` broadcast, `zone/client_packet.cpp`
  4865; `OP_SpawnAppearance` to target/self, `zone/mob.cpp` 4142-4144) carries
  no sequence number. The client delivers it the moment it arrives, while a
  reliable packet after a gap waits for the resend — so an unreliable packet
  reaches the client INSIDE the tail only when a tail packet is lost and resent
  (or the server drops it under `Network:ClientDataRate`, default 0 = off,
  `InternalSend`). **The board's original wording ("the reliable stream may
  hold the tail back behind them") had the mechanism the wrong way round.**
* **Terminal signal:** the next RELIABLE packet after the keepcasting 0. It is
  `OP_LinkedReuse`/`OP_MemorizeSpell` iff the cast succeeded; anything else
  means `StopCasting`. An interrupt is already settled earlier by
  `OP_InterruptCast`. There is no terminal packet for `StopCasting` at all.

## Measurements [measured], local akk-stack on dev-u24, 2026-10-12

Lane: `cast_tail_repeat_{quiet,traffic}` (`src/client/scenario/scenario_cast_tail_repeat.cpp`)
— GM Testcaster alternates 12 x Blast of Cold (372, let finish) with 12 x
O'Keils Radiation (378, GM `#interrupt` at 1.5 s) at Testbuddy; the traffic
peer sends `/sit` `/stand` `/say` `/move` `/jump` every 0.25 s (~1,100
commands). Every zone packet is logged with its reliability
(`[ZonePacket] <op> rel=0|1`); `scripts/scenario_configs/b760_tail_analyze.py`
builds the per-cast table. Inbound loss is `WILLEQ_SIM_ZONE_IN_LOSS=<n>`
(the Daybreak manager's `simulated_in_packet_loss`, applied once zoned in).

| condition | binary | success tails | unreliable pkts inside a success tail | wrong results | success settle (ms) | dropped datagrams |
|---|---|---|---|---|---|---|
| quiet | pre-fix | 12 | 0 | 0 | 0-1 | 0 |
| traffic | pre-fix | 12 | 0 | 0 | 0-1 | 0 |
| traffic + loss 5 (from connect) | pre-fix | 12 | 0 | 0 | 0-1 | 62 |
| traffic + loss 15 (after zone-in) | pre-fix | 12 | 0 | 0 | 0-1 | 145 |
| traffic + loss 15 (after zone-in) | **fix** | 12 | 0 | 0 | 0-1 | 156 |

(loss 20 from connect never finished zone-in, exit 3 — why the lever is now
applied after zone-in.) All 60 interrupts settled at `OP_InterruptCast`,
before the keepcasting 0, as the source says.

**The gap after a keepcasting 0 with no tail** (the interrupt rows, a proxy for
a `StopCasting` — no live `StopCasting` after BeginCast was produced) [measured]:

| condition | to first packet of any kind (pre-fix settle) | to first RELIABLE packet (post-fix settle) |
|---|---|---|
| quiet | min 0 / median 479 / max 3,837 ms | 464 / 4,602 / 7,554 ms |
| traffic | 33 / 382 / 606 ms | 33 / 382 / 606 ms |
| loss 5 | 131 / 993 / 4,257 ms | 1,146 / 4,487 / 4,749 ms |
| loss 15 (fix binary) | 64 / 739 / 2,088 ms | 64 / 4,503 / 5,044 ms |

In the quiet zone 10 of 12 such gaps had at least one unreliable
`OP_ClientUpdate` before the first reliable packet.

## Verdict

* **Wrong result: not reproduced live** in 60 success tails, including 15 %
  simulated inbound loss with ~1,100 peer commands — the tail shares a
  datagram and is lost and resent as a unit [measured]. The path is real by
  construction: `DaybreakStateMachineTest.UnreliablePacketOvertakesAHeldReliableOneAndIsFlagged`
  shows an unreliable packet delivered ahead of a held reliable one.
* **Late success: no.** A success settles at its `OP_MemorizeSpell`, 0-1 ms
  after the keepcasting 0, in every condition [measured].
* **Late stop: inherent.** `StopCasting` sends no terminal packet, so its
  Interrupted can only be known at the server's next reliable packet.

## The fix (root)

`DaybreakConnection::DeliveringReliable()` (set around every OP_Packet /
reassembled-fragment delivery) reaches `ZoneComponent::DispatchZonePacket(p,
reliable)` and `SpellManager::noteZonePacket(opcode, reliable)`, which now
ignores unreliable packets: the outcome settles on the next RELIABLE packet —
the server's own next word. `handleSpellBarRefresh()`'s recast is unchanged.
Pins: `SpellCastEndWire.UnreliablePacketInsideTheTailDoesNotMakeASuccessInterrupted`,
`…StopCastingSettlesOnTheNextReliablePacketNotAnUnreliableOne` (RED at
7a97557c, green at ba4a51b3) and the four `DaybreakStateMachineTest`
reliability-flag tests.

**Trade-off, left open (Will's call if it ever matters):** a `StopCasting`
result now waits for a reliable packet — quiet-zone proxy median 4.6 s instead
of 0.48 s. Nothing in production reads `SpellCastComplete`
(`SpellParticleEffect::castCompleteSignaled` has no writer; `irrlicht_bridge.cpp`
only traces it; B603), and the cast bar and gem are released at the
keepcasting 0 either way. A time-out fallback would reintroduce the wrong
result under loss, so none was added.

Evidence: `/mnt/projects/fable/review/B760/` (one directory per run, client +
peer logs, tail tables, snapshots).
