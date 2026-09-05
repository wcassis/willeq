# T375 — `/report` sends what eqemu reads (B023)

Class B, slot2, no server (the oracle is eqemu's source). Read the B023
board text.

`PlayerComponent::SendReport` (`src/client/player/player_misc.cpp`) sends a
pipe-delimited reported/reporter/message body; eqemu's
`Client::Handle_OP_Report` (`/mnt/projects/akk-stack/code/zone/client_packet.cpp`)
reads a different shape, so `AddReport()` is never reached.

1. Read the handler and the struct it casts to (quote both, with the
   size check the handler performs). That is the wire shape.
2. Make `SendReport` build exactly that body. If the official client
   sends fields fable cannot know (a category id), pick the value the
   handler treats as the plain-text report and say why from the handler.
3. Test, pre-fix failing: the queued packet's bytes decode with a
   struct mirroring eqemu's and pass the handler's size check; the
   pre-fix body fails it. A recorded-session assertion through
   `PacketSender` (T360 shape) that `/report` emits one OP_Report.
4. The G08 / self-death goldens are untouched (different opcode); the
   bridge intent table golden if `/report` passes through an intent.
5. Strike B023. Live proof (a `/report` landing in eqemu's `reports`
   table) is OWED to the next lock unit — note it on the board.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, file sizes.

STOP: the handler's shape needs a field fable has no source for and no
default the handler accepts (report the struct).
