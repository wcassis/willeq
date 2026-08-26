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
