# T217 -- name the inviter on group accept; send a real OP_GMHideMe

Branch `task/T217-group-name-gmhideme` off fable `e70e3347`, slot3.
Evidence: `/mnt/projects/fable/review/T217/` (README there).

## Defect A -- "You have joined 's group"

`EverQuest::AcceptGroupInvite()` passed `m_pending_inviter_name` by const
reference into `SendGroupFollow()`, whose last act is
`m_pending_inviter_name.clear()`. Both the follow's own log line and the
accept's chat line then read the cleared member (T99 live evidence,
`review/T99/`). `SendGroupDecline()` had the same aliasing hazard in its log.

Fix (`src/client/eq_group.cpp`): `AcceptGroupInvite()` copies the name before
the send and uses the copy for the chat line; `SendGroupFollow()` and
`SendGroupDecline()` copy their parameter before touching the member. Wire
bytes and state transitions are unchanged.

Test seam: the clear only runs past the zone-connection guard, which is why no
headless test had ever seen the bug. Both senders now honour the existing T88
`m_test_bypass_send_guard` (default false; production guard bit-identical) and
null-check `m_zone_connection` before `QueuePacket` so a headless test can run
the clear-then-read path.

## Defect B -- /hideme misrouted

`SendGMHideMe()` called `SendSpawnAppearance(AT_GM, ...)`; eqemu has no case
for SpawnAppearance type 20 and dropped it (`Unknown SpawnAppearance type:
0x0014`, T148). `Handle_OP_GMHideMe` (eqemu zone/client_packet.cpp:6712)
listens on `OP_GMHideMe = 0x15b2` (patch_Titanium.conf:132; already in eq.h as
`HC_OP_GMHideMe`), size-checks an 8-byte `SpawnAppearance_Struct`, and reads
only `parameter`: `SetHideMe(!sa->parameter)` -- a VISIBILITY flag, 0 hides.

Fix (`src/client/eq_gm.cpp`): build `OP_GMHideMe` + `SpawnAppearance_Struct
{spawn_id=ours, type=AT_GM, parameter = hide ? 0 : 1}`; connection-gated send
as the other GM senders. A T167-style seam (`Test_GetLastGMHideMe()`,
`include/client/eq.h`) captures the raw packet bytes before the gate.

## Tests

- `tests/test_titanium_opcodes.cpp`: `TitaniumOpcodesTest.GMHideMeOpcode` pins
  0x15b2 and asserts it is not `HC_OP_SpawnAppearance`.
- `tests/test_group_accept_gm_hideme.cpp` (new, registered in tests/CMakeLists):
  4 GroupAcceptTest + 4 GMHideMeTest cases (opcode, 10-byte size, parameter
  polarity both ways, sent-flag).

Measured red/green: with only the `AcceptGroupInvite()` ordering line reverted
(seam plumbing kept), `AcceptNamesTheInviterEvenThoughFollowClearsThePendingName`
fails with `Which is: "You have joined 's group"` -- the live symptom verbatim
(`review/T217/unit_test_red_with_fix_reverted.txt`). Restored: 8/8
(`unit_test_green_with_fix.txt`).

## Verification

- `cmake --build build -j24`: clean.
- `ctest`: 2753/2755 -- ALSA `AudioManagerTest.PreloadCommonSounds` (documented)
  and `check_build_tree_orphans`, which flagged three stale binaries in this
  slot's bin/ left over from its previous task/T150 checkout
  (`test_eq_commands_assist/attack/vendor`, sources deleted by earlier units).
  Deleted them, re-ran: pass. Not caused by this unit.
- `check_file_sizes.sh fable`: 0 FAIL (eq.h WARN is pre-existing, +12 lines).

## Live (eqemu, lock held T217 2026-08-25, arena zone, two clients)

Leg A (Oraclerogue invites, Testchar accepts): Testchar logged
`Accepted group invite from Oraclerogue` and `You have joined Oraclerogue's
group`; the server handed out group id 1001 and both clients saw
`Group update: 2 members`; disband propagated to both. PASS.

Leg B (Testchar, GM, `/hideme on` ... `/hideme off`): zero `Unknown
SpawnAppearance` and zero "reported for hacking" lines in either server log
(T148 had the former here). eqemu logs nothing on a good OP_GMHideMe, so the
proof is `account.hideme` for willeq_test: 0 before, **1** queried mid-run
while hidden, 0 after `/hideme off` -- written only by `Client::SetHideMe`,
reached only via `Handle_OP_GMHideMe`. PASS.

Note: the observer did not see a despawn at "on" because eqemu sends the
hide/unhide spawn packet only to clients with status below the GM's
(`QueueClientsStatus(..., 0, Admin()-1)`); Oraclerogue is status 250 vs
Testchar's 200. Server behaviour as written, not a client gap.

Still open, seen again: the group LEADER's own client shows a blank
`leader:` on its own GroupUpdate (T99 finding #2) -- out of scope.

Server restored: Testchar zone 36 (-77.9232, 21.9086, 3, h190) gm=0;
Oraclerogue zone 2 (167, 30, -29.25, h384); eqemu down; lock deleted.
