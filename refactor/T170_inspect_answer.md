# T170 — `OP_InspectAnswer`: generate the packet nobody generated

**Slot: slot1. x86 only. NO eqemu, NO lock, NO Pi (offline).** Orchestrator
executed directly.

**Territory: `include/client/inspect_answer.h`,
`src/client/inspect_answer.cpp`, `src/client/eq_player_info.cpp` (the three
inspect functions), `include/client/eq.h`, `include/common/packet_structs.h`
(the two inspect structs), `tests/test_inspect_answer.cpp`, CMake,
`refactor/`.**

## The defect

`ZoneProcessInspectAnswer` was wired and `SendInspectRequest` worked, but
**nothing in this client ever produced an `OP_InspectAnswer`** — so inspecting
a WillEQ player showed an empty window, forever.

That is not optional client behaviour. eqemu's `Handle_OP_InspectRequest`
(`zone/client_packet.cpp:9084-9087`) **forwards the request to the target
client** and only builds the answer server-side for **SoF+ clients and bots**.
Titanium is below that cut, so a Titanium client must answer for itself. We
received the request, printed "X is inspecting you", and stopped.

**Why it survived this long is worth naming: the request and the empty window
are on two different machines.** No single-client test could show it — the
standing two-client rule exactly.

## A second defect, found on the way

The two id fields of `InspectResponse_Struct` **mean the opposite of
`Inspect_Struct`'s**, and the receive path had them the request's way:

```
insr->TargetID = requester->GetID();   // who ASKED -- the routing target
insr->playerid = requestee->GetID();   // who is being INSPECTED
```
*(eqemu's own builder, `Client::ProcessInspectRequest`, `zone/client.cpp:6590-6591`)*

`Handle_OP_InspectAnswer` looks `TargetID` up to decide **whom to forward the
answer to**. We only ever receive answers we asked for, so `TargetID` is
**us** — and `ZoneProcessInspectAnswer` was using it to name the inspected
player. Every inspect would have been captioned with the inspector's own name.
Fixed, and both structs now carry the asymmetry in their comments with
`playerid` named as eqemu names it, lower-case included.

## A near-miss worth recording

`Inspect_Struct` is **8 bytes** here, while eqemu's internal one is two
`uint16`s — 4 bytes — and `Handle_OP_InspectRequest` length-checks against
**that**. That looks exactly like T145's and T168's dead-on-arrival senders,
and I nearly reported it as one. **It is correct as it stands:** Titanium's own
`Inspect_Struct` (`titanium_structs.h:1969`) is two `uint32`s and
`DECODE/ENCODE(OP_InspectRequest)` convert 8↔4 at the patch layer. The struct
now says so, so the next reader does not "fix" it and break the wire.

## What was done

`MakeInspectAnswer()` in its own testable TU — same reasoning as T167/T169:
the handler is private, and the field semantics are the part that can be got
backwards. `ZoneProcessInspectRequest` now calls `SendInspectAnswer()`.

**Equipment is deliberately left empty.** The server does not read it:
`Handle_OP_InspectAnswer` (`:9023-9043`) overwrites every itemname and itemicon
from the inspected character's own inventory before forwarding. Filling them
from our client-side inventory would be work whose result is discarded, and
would mislead the next reader into thinking the values matter. Icons are set to
eqemu's own no-item marker `0xFFFFFFFF` rather than 0.

## ⚠ The accepted cost, stated plainly

**Answering an inspect overwrites the character's server-stored inspect
message.** `Handle_OP_InspectAnswer` (`:9045-9049`) memcpy()s our `text` field
over the character's inspect message and calls `SaveCharacterInspectMessage()`
— unconditionally, on every answer — and the packet is length-checked EXACT so
the field cannot be omitted.

**We send it empty, because this client has no honest source for it.** eqemu
loads the inspect message at zone-in (`:1367`) and **never sends it to a
Titanium client**: the real client owns that text, in its own inspect window.
So a character who set an inspect message in another client will have it
cleared the first time a WillEQ session is inspected.

That is accepted deliberately. The alternative is not answering at all, which
leaves inspect entirely broken between two WillEQ clients. **The follow-up is a
client-side inspect message the user can set** (`/inspectmessage` plus
persistence in `willeq.db`), which this sender would then echo. It is NOT done
here: a setter with nothing able to reach it is precisely the trap T168 just
finished removing. `m_inspect_message` exists and is always empty, which is the
truthful state of affairs rather than a half-built feature.

**Will should overrule this if the loss is unacceptable** — the revert is one
line (drop the `SendInspectAnswer` call), and it puts us back to inspect not
working at all.

## Verification

- **8 new tests.** The one that matters most is
  `TheTwoIdsAreNotInterchangeable`: a test that merely checked "both ids appear
  somewhere" would pass on a swapped implementation, which is the exact bug
  that was live on the receive side. Also covered: the no-item icon marker,
  message truncation at the field boundary, the exact 1792-byte body, and the
  sender end-to-end through a real headless `EverQuest`.
- Suite **2627/2628** (2620 + 8), sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`.
- T168's `check_sender_reachability` gate passes at 90 senders — the new
  sender has a production caller, which is the point.
- Build clean; `build-gles2-x86.sh` clean; **visual gate PASS**, goldens
  untouched; `build-arm-noble.sh` clean; `check_file_sizes.sh` 0 FAIL.

## Not verified

- **NOT wire-verified, and this one genuinely needs two clients.** The probe:
  run `Testmelee` and `Oraclerogue` in the same zone, `/inspect` each way, and
  confirm the inspecting client lists the other's equipment — the server fills
  the item names from the DB, so a correct answer produces real gear names, not
  blanks. Also confirm `character_inspect_messages` for the inspected
  character: it will be emptied, which is this unit's known cost and should be
  observed rather than assumed.
- **The receive-path fix (`playerid` instead of `TargetID`) has no test.**
  Naming the inspected player needs entries in `m_entities`, which has no
  public mutator, and I did not add a test seam solely to reach it. It is
  source-verified against eqemu's builder only. The equivalent gap was recorded
  in T167 for the same reason.
