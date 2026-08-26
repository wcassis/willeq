# T243 — /emote goes out as OP_ChannelMessage channel 22; Titanium uses OP_Emote

Slot: slot2. x86. OWNS eqemu briefly (noclobber) for the two-client proof.
Class B → A (T239 finding). Territory: the emote command path
(command_processor cmdEmote → whatever EverQuest sender it calls, likely
eq_chat.cpp), the OP_Emote opcode/struct (eq.h / packet_structs.h —
additive), tests. Do NOT touch hotbar/action dispatcher (T239 merged, fine)
or graphics/benchmark loaders (T242 slot1).

## Evidence
T239 live: Testchar `/emote waves at Oraclerogue` → observer saw nothing,
actor got no echo (`review/T239/`). eqemu: grep
/mnt/projects/akk-stack/code/zone/client_packet.cpp for Handle_OP_Emote and
common/eq_packet_structs.h Emote_Struct; patch_Titanium.conf for OP_Emote.
Check whether eqemu treats ChannelMessage channel 22 at all (chat.cpp /
ChannelMessageReceived) — if it silently drops it, that is the whole bug.

## Scope
1. Determine the correct wire form for Titanium (`OP_Emote` +
   `Emote_Struct{unknown01, message[1024]}` is the usual shape — verify).
2. Make `/emote` (and `/em`, `/me` aliases if registered) send that;
   keep `/say` etc. untouched. Pin with an opcode test (T176 pattern) and
   a body test.
3. Live, two clients: `/emote waves` → observer prints "Testchar waves"
   (eqemu broadcasts Emote to the zone as a message). Also confirm `/wave`
   (OP_Animation) is seen by an observer that logs animations at DEBUG.

## Verification
Full ctest; Review; evidence review/T243/. Commit: `T243: /emote sends OP_Emote`.

## Review (executor, 2026-08-26, slot2, branch task/T243-emote-wire off 6d784175)

### What eqemu does with channel 22 vs OP_Emote

- `zone/client.cpp:1651` `case ChatChannel_Emotes:` ("Emotes for Underfoot
  and later") -- eqemu DOES accept a ChannelMessage channel 22 and rebuilds
  it as OP_Emote (`"%s %s", GetName(), message`) then
  `entity_list.QueueCloseClients(this, outapp, true, RuleI(Range, Emote),
  0, true, FilterSocials)`. It is not dropped.
- `zone/client_packet.cpp:6211` `Handle_OP_Emote` -- size must equal
  `sizeof(Emote_Struct)` = 1028 (else LogError + drop); output is
  `memcpy(name) + memcpy(message)` with NO separator, broadcast with the
  same `QueueCloseClients(..., ignore_sender=true, Range:Emote=135
  (ruletypes.h:754), ..., FilterSocials)`.
- Titanium: `OP_Emote=0x547a` (patch_Titanium.conf:189),
  `OP_Animation=0x2acf` (:151). `titanium.cpp:619` ENCODE trims the S->C
  packet to u32 type + NUL string; `:2756` DECODE pads C->S to 1028.
- So the T239 symptom was NOT the server ignoring channel 22. Two client
  bugs combined: (a) inbound OP_Emote 0x547a was unhandled (the client's
  `HC_OP_Emote` was 0x2acf = OP_Animation), so even a correctly rebroadcast
  emote fell through the dispatcher; (b) `ZoneSendSetServerFilter` sent
  0xFFFFFFFF x29, which eqemu's `Client::ServerFilter` (client.cpp:3693
  `Filter0`: shows ONLY for value 1) reads as HIDE for FilterSocials -- and
  for every other filter too (the "0=show" family reads any non-zero as
  hide/mine-only). The observer had told the server to suppress socials.

### Scope delivered

1. `HC_OP_Animation = 0x2acf` (renamed from the misnamed HC_OP_Emote; 3
   references + eq_strings), `HC_OP_Emote = 0x547a` added;
   `ZoneProcessEmote` -> `ZoneProcessAnimation` (rename), new
   `ZoneProcessEmote` for the text form; `EQT::Emote_Struct` (1028,
   static_assert) in packet_structs.h.
2. `SendChatMessage("emote")` -> new `ZoneSendEmote()`: OP_Emote + full
   1028-byte Emote_Struct, type 0, text with a LEADING SPACE (eqemu
   concatenates name+message with no separator; verified live: run 2 gave
   `Testcharwaves`), plus a local echo ChatMessage ("Testchar waves") since
   eqemu broadcasts with ignore_sender. `/em`, `/me` are aliases of the
   same registry entry (command_processor.cpp:315) so they follow. `/say`
   etc. untouched (pinned by SendChatMessage_Say_StillGoesOutAsChannelMessage).
3. Live, two clients (review/T243/README.md): observer log
   `run3_B_oraclerogue.log` has `[CHAT] (emote type 0) Testchar waves at
   Oraclerogue`, the `/wave` as `[EMOTE] spawn_id=408 ... anim_id=29 ->
   s03`, and the second emote. Runs 1 and 2 are the two intermediate
   failures that located findings (b) and the leading space.

### Tests (red -> green, files in review/T243/)

| test | red | green |
|---|---|---|
| TitaniumOpcodesTest.EmoteOpcode (+Animation pin, NE) | 1 FAILED (0x2acf) | 12/12 |
| EqActionHandlerTransformsReal.SendChatMessage_Emote_{GoesOutAsOpEmote,BodyIsExactly1028Bytes,EchoesLocallyAsEmoteChat}, _Say_StillGoesOutAsChannelMessage | 3 FAILED / 4 | 28/28 |
| EqActionHandlerTransformsReal.SetServerFilter_ShowAllUsesEqemuPerSlotConvention | 29 slot failures | (in the 28/28) |
| ZoneChatTest.Emote_{PublishesEmoteChatEventWithVerbatimText,UndersizedRejectedWithoutEvent,SmallestValidSizePublishesEmptyMessage} | 2 FAILED / 3 | 12/12 |

Harness note: the 1030-byte OP_Emote is the first C->S packet
`WireHarness` has seen that exceeds Daybreak's 512-byte max, so
`sentPackets()` now reassembles OP_Fragment frames (first frame carries a
BE u32 total). The first green run failed on exactly this and it was the
harness, not the sender.

### Verification bar

1. `cmake --build build -j24` clean (green_build{,2,3}.txt).
2. ctest 2886/2887 on the final code (ctest_full.txt); the one failure is
   AudioManagerTest.PreloadCommonSounds (ALSA). Baseline was 2745/2746 +
   T239..T242's additions; +11 tests here.
3. build-gles2-x86.sh OK. 4. visual_gate PASS inside ctest, goldens
   untouched. 5. build-arm-noble.sh OK.
6. check_file_sizes vs 6d784175: 1 FAIL, src/client/eq.cpp 3968 --
   PRE-EXISTING (3950 at base; +18 here: one dispatcher arm and the
   29-entry filter table). Run against the branch base because fable moved
   to aae1ae65 (T242 merge) mid-unit and the default base dragged
   tools/hwcaps into the check.

### Deviations (flagged)

- **Scope grew by one production function outside the plan's named
  territory: `EverQuest::ZoneSendSetServerFilter` (eq.cpp) + a
  `SetServerFilter_Struct`/`ServerFilterSlot` enum (packet_structs.h).**
  Without it Scope item 3 is unreachable -- the observer's own filter
  payload suppresses the broadcast. Checked against T244's plan (spell
  manager result switch, /target ranking) and T242 (merged): no overlap.
  Side effect worth knowing: this un-hides EVERY server-filtered message
  class for willeq (guild/group/shout/auction/OOC chat relayed through
  filters, other players' hits/misses, pet hits, DoTs, spell crits...).
  The previous payload hid them all; the official client's default is
  show-all, so this is toward parity, but chat volume in busy zones will
  rise. If any class should default to hidden, that is a one-line change
  in the `kShowIsOne` table, not a design question for this unit.
- The `HC_OP_Emote` rename is a correction, not purely additive as the
  plan assumed -- the old symbol was the wrong opcode, and keeping a
  misnamed constant beside the right one would mislead the next reader
  (T176 precedent). 3 code references + 1 comment updated.
- Live leg ran three times (bounded: each run found one concrete defect,
  fixed, re-run). All three logs are kept; run 3 is the deliverable.
