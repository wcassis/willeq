# T376 — SendClickDoor through PacketSender (B140); one pending-click set (B141)

Class B, slot3, no server. Read the B140 and B141 board text and T374's
§Review.

## Leg A — B140

`EntitiesComponent::SendClickDoor` ends in an unguarded
`ctx_.zone_connection->QueuePacket(...)` — segfaults with no zone
(measured exit 139) and bypasses `PacketSender::SafeQueueZone`, so the
session recorder and the T268 seam never see a door/world-object click.
Route it through `ctx_.net.SafeQueueZone` (T360's shape — the one call
that guards, records, and orders). Then census: comment-filtered grep for
`zone_connection->QueuePacket` / `->QueuePacket(` outside `PacketSender`
under `src/client`; every remaining direct sender is either routed in
this leg (same shape, one commit each if more than two) or boarded with
its reason. Tests, pre-fix failing: (a) `SendClickDoor` with no zone
connection returns without crashing (the T360 harness); (b) a recorded
session contains the click. Strike B140.

## Leg B — B141

`EQT::DoorStateManager` carries a complete, correct, zero-caller
duplicate of the pending-click set (`pendingClicks_` + four accessors,
with the erase-on-update and clear-on-zone the reached copy lacked until
T374). Duplicated-then-drifted, and the reached copy was the broken one.
Decide by ownership: the manager owns door state → the manager's set is
the one copy; `EntitiesComponent` calls it (`markPendingClick` on send,
the manager consumes on `OP_MoveDoor`, clears on zone) and its own
`m_pending_door_clicks` + `ClearPendingDoorClicks()` are deleted. T374's
`DoorClickZoneReset` tests must stay green unedited (they test behaviour,
not the member); if they name the member, that is the test reading
implementation — fix the test to the behaviour. K12 ff check. Strike
B141.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, the G01 golden
unchanged, file sizes. No graphics change.

STOP: a direct sender in the census cannot go through PacketSender
because it runs before the zone connection exists (login/world — those
belong to a different sender; board them, do not route).
