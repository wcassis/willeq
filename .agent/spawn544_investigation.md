# spawn_id=544 ClientUpdate Investigation

## ABSOLUTE CONSTRAINTS - READ THIS FIRST

**THIS IS A 100% VIRTUAL SYSTEM:**
- No physical networking - everything is local
- No network contention, no dropped packets, no network errors
- EVERY OTHER CLIENT WORKS with the same server on the same infrastructure
- If a packet is in the pcap, it WAS DELIVERED to the client
- The bug is 100% in the willeq CLIENT CODE - not network, not server, not OS

**DO NOT BLAME:**
- Network packet loss
- UDP buffer overflow
- Socket issues
- Server bugs
- Infrastructure problems

**THE PACKET ARRIVED. THE CLIENT FAILED TO PROCESS IT.**

## CONFIRMED FACTS (DO NOT RE-VERIFY)

- **spawn_id=544 ClientUpdate exists in pcap at frame 6500, timestamp 00:31:02.260**
- It was inside an OP_Combined (0x0003) packet with zlib compression
- **Server log confirms send at line 17326 (gfaydark.log)**:
  - `[01-21-2026 18:31:02] [Packet S->C] [OP_ClientUpdate] [0x14cb] Size [24]`
  - Hex: `20 02 00 C0 59 00 E8 F9 07 00 AC 03 00 00 00 C0`
  - spawn_id=544 = 0x0220 = `20 02`
- This is 100% a willeq client bug

## What was delivered in fail.log

ClientUpdate packets delivered: spawn_id=94, 95, 380
ClientUpdate for spawn_id=544: NEVER DELIVERED

## Sequence tracking

- Sequences 31-101 processed correctly (lines 10290-14677)
- FRAG_DROPPED_PAST seq=32 at expected=102 are RETRANSMISSIONS (not the original missing packet)

## Key question

WHY wasn't the ClientUpdate at frame 6500 delivered to the application layer?

## KEY FINDING: Packets inside Combined dropped as PAST

At line 22105-22119 in fail.log:
```
OP_Packet seq=286 is PAST (expected=292), dropped
OP_Packet seq=287 is PAST (expected=292), dropped
OP_Packet seq=288 is PAST (expected=292), dropped
OP_Packet seq=289 is PAST (expected=292), dropped
```

These are OP_Packets INSIDE a Combined packet being treated as protocol-level OP_Packets
and incorrectly sequence-checked.

## The problem

The same Combined packet (RECV[324]) appears to be processed TWICE:
1. First time at line 21948: seq=286-289 are CURRENT, processed correctly
2. Second time at line 22099: Same packet arrives again (retransmit), seq=286-289 now PAST, DROPPED

But the ClientUpdate for spawn_id=544 (`cb 14 20 02 00 c0 59...`) was NEVER received.
It was sent by server at 18:31:02 (line 17326 in gfaydark.log) but never appears in fail.log.

## CRITICAL FINDING: Frame 6500 contents extracted

**File saved: frame_6500.bin (or similar) - user saved the extracted data**

Frame 6500 is an OP_Combined (0x03) with zlib compression containing **20 ClientUpdate packets**:
- Raw packet: 332 bytes
- Decompressed: 500 bytes
- All sub-packets are opcode 0x14cb (OP_ClientUpdate)

**Sub-packet 11 is the missing ClientUpdate for spawn_id=544:**
```
cb 14 20 02 00 c0 59 00 e8 f9 07 00 ac 03 00 00 00 c0 4e 00 00 00 00 00
```
- opcode: 0x14cb
- spawn_id: 0x0220 (544)
- position data follows

## THE MYSTERY: Packet not in RECV log but IS in pcap

**The compressed data pattern `5a 78 01 35 d1 3f` from frame 6500 does NOT appear in fail.log!**

**Frame 6500 details:**
- Time: 5.826196s (00:31:02.260)
- UDP: 172.18.0.3:7000 → 10.0.30.13:42018, 332 bytes
- Content: OP_Combined (0x03) with 20 ClientUpdate sub-packets
- Sub-packet 11: spawn_id=544 ClientUpdate

**Key observation:**
- The packet IS in the pcap (it arrived)
- The packet pattern is NOT in fail.log RECV entries
- Many other OP_Combined packets ARE logged
- **The CLIENT CODE must be dropping/filtering this packet before logging**

## FIX APPLIED (2026-01-22)

**Problem:** OP_Packet and OP_Fragment sub-packets inside OP_Combined were being sequence-checked and dropped as PAST when they had already been processed at the outer protocol level.

**Solution:** Added `bool from_combined` parameter to `ProcessDecodedPacket()`:
- When called from OP_Combined or OP_AppCombined handling, pass `true`
- In OP_Packet handler: if `from_combined`, skip sequence checking and process payload directly
- In OP_Fragment handler: if `from_combined`, skip sequence checking (set order to SequenceCurrent) and skip ACK/sequence increment

**Files changed:**
- `include/common/net/daybreak_connection.h` - Added parameter to declaration
- `src/common/net/daybreak_connection.cpp`:
  - Line 776: Added parameter to definition
  - Line 858: OP_Combined passes `true`
  - Line 909: OP_AppCombined passes `true`
  - Lines 978-984: OP_Packet skips sequence check if from_combined
  - Lines 1024-1049: OP_Fragment skips sequence check and ACK if from_combined

**Build:** Successful

**Testing:** Needs verification - connect to gfaydark and check if spawn_id=544 ClientUpdate is now delivered
