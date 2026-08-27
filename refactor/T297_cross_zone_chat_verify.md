# T297 — is zone-wide and cross-zone chat actually working? (Will's UCS deferral is conditional on it)

Slot: first free. x86. OWNS eqemu (noclobber) — needs TWO clients in TWO
DIFFERENT zones. Class B, verification. **No client code change expected**;
if a channel is broken, report it as a defect with evidence and STOP — the
fix is a separate unit.

## Why this unit exists
Will deferred the UCS/chat-server work *"as long as zone-wide and cross-zone
communication is working."* **That condition has never been tested.** Every
two-client chat probe in the record put both clients in the SAME zone:
T222 (`/reply`, arena), T243 (`/emote`, befallen), T239 (hotbar macro `/say`,
befallen), T256 (`/who`, befallen). Cross-zone delivery is unverified.

Context that matters: `ConnectToUCS(...)` is commented out
(`eq_login_world.cpp:834`), so anything eqemu routes through UCS cannot work.
In eqemu, zone-local channels (say/shout/ooc/auction/emote) are handled by the
zone server, and cross-zone tells are routed by WORLD — but which channels
depend on UCS on THIS server build is a question to answer from the source
(`/mnt/projects/akk-stack/code`: `zone/client_packet.cpp`
Handle_OP_ChannelMessage, `world/`, and the UCS service), not from assumption.

Also relevant: **T243 fixed `ZoneSendSetServerFilter`, which had been telling
the server to HIDE every filtered message class** — so any pre-T243 chat
observation is void. Re-establish from scratch.

## Scope
1. **Read the server first.** For each channel — say, shout, ooc, auction,
   emote, tell, guildsay, group — determine from eqemu's source who routes it
   (zone / world / UCS) and therefore which SHOULD work cross-zone with UCS
   absent. Write that table before running anything; it is the prediction the
   run tests.
2. **Same-zone control leg.** Both clients in one zone, every channel, confirm
   each is received. Anything failing here is a plain defect, not a UCS issue.
3. **Cross-zone leg.** Testchar and a second character in DIFFERENT zones
   (use `/say #zone <zone>` per the standing trap — a DB `zone_id` edit leaves
   COORDINATES stale). Every channel again. Record received / not received per
   channel per leg, verbatim from the OBSERVER's log — never from the sender's
   self-echo (T172's lesson: an ack line is not delivery).
4. **Guild and group cross-zone**: a guild needs `guild_members` rows (T256
   staged some — document and restore); a group needs both characters grouped
   before they separate.
5. Report the matrix against the step-1 prediction. Where they disagree, say
   so — a channel that works when the source says it should not is as
   interesting as the reverse.

## Deliverable
A table: channel | routed by | same-zone | cross-zone | matches prediction.
Plus a plain verdict on **Will's condition**: is zone-wide and cross-zone
communication working? If any channel fails, that failure is the finding —
do not fix it here.

## Verification
No source change expected, so no ctest/gate unless something is fixed. Review
in this file; evidence review/T297/ with both clients' logs. Commit:
`T297: cross-zone chat matrix`.
