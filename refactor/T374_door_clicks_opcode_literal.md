# T374 — pending door clicks cleared at zone change (B136); opcode literal (B137)

Class A + B, slot3, no server. Read the B136 and B137 board text (T372
wrote them so they can be worked without the surveyed branch).

## Leg A — B136

`EntitiesComponent::m_pending_door_clicks` is never cleared at zone
change; door IDs are `uint8_t` reissued per zone, so a click sent just
before zoning flags the next zone's first matching `OP_MoveDoor` as
user-initiated (`DoorStateChanged.userInitiated` wrong). Clear the set on
the zone-cleanup path of `EntitiesComponent` (find the existing per-zone
reset — the same place entity maps are cleared; do not add a second
cleanup site). Test, pre-fix failing: click door 3, zone, receive
`OP_MoveDoor` for door 3 in the new zone → `userInitiated == false`.
Strike B136.

## Leg B — B137

`HC_OP_TargetCommandReply` is a bare `0x61f9` literal in
`kZoneOpcodeTable` while every sibling uses an enumerator. Add the
enumerator in `include/client/eq_opcodes.h`, use it in the table and in
`eq_strings.cpp`; the G01 zone-opcode golden pins the table — it must
stay byte-identical (the value is unchanged), and `test_zone_dispatch_table`
must pass unedited. A source-reader pin that no bare hex literal remains
in the table. Strike B137.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, G01 golden
unchanged, file sizes. No graphics change.

STOP: none expected.

## Review

Executed 2026-09-05 in `wt/slot3`, branch
`task/T374-door-clicks-opcode-literal` off `fable` at `672e3c3a`. Two legs,
two commits. No STOP condition hit. No graphics change, no server, no
re-bless.

### Leg A — B136, pending door clicks cleared at zone change (`3ef7826c`)

**Where the set is now cleared.** `EntitiesComponent::ClearPendingDoorClicks()`
(`src/client/entities/entities_component_world.cpp`, declared public in
`include/client/entities/entities_component.h`) is called from
`ZoneComponent::CleanupZone()` (`src/client/zone/zone_component.cpp`) **one
line after `ctx_.game_state.doors().clear()`** — the same single per-zone
cleanup site the plan named, beside the door-state-manager clear and the
entity-map clear. **No second cleanup site was added**; `ClearPendingDoorClicks`
has exactly one caller.

The member's occurrence count goes 4 → 6 (declaration, `insert` in
`SendClickDoor`, `erase` in the MoveDoor handler, the declaration comment, and
now the clear plus its `empty()` guard for the debug line).

**Test:** `tests/test_door_click_zone_reset.cpp`, two cases.

| test | pre-fix | post-fix |
|---|---|---|
| `DoorClickZoneReset.PendingClickDoesNotSurviveAZoneChange` | **FAIL** — `h.LastUserInitiated()` was `true`: "a door click made in the PREVIOUS zone was carried across CleanupZone() and credited to the new zone's door 3" | **PASS** |
| `DoorClickZoneReset.ClickThenMoveDoorInTheSameZoneIsUserInitiated` (preservation) | PASS | PASS |

Pre-fix binary run: exit 1, 1 passed / 1 failed. Post-fix: exit 0, 2 passed.

The failing case is exactly the board's shape: spawn door 3, `SendClickDoor(3)`,
`CleanupZone()`, re-enter (all three `IsFullyZonedIn()` terms re-set), spawn a
*different* door 3, replay `HC_OP_MoveDoor{3, open}`, read the
`DoorStateChanged.userInitiated` off the bridge queue. The preservation case
exists so the fix cannot degrade into "never report a user click again".

**Deviation (harness, deliberate).** The plan implies a plain component test.
`SendClickDoor` is gated on `IsFullyZonedIn()`, whose third term
(`EverQuest::m_update_running`) has no public setter, so the file uses the T17
`#define private public` idiom. More importantly, the first draft **segfaulted
(exit 139)**: `SendClickDoor` ends in `ctx_.zone_connection->QueuePacket(...)`,
an unguarded `shared_ptr` deref, so a harness with no connection crashes before
reaching the code under test. The file therefore installs a real
`DaybreakConnection` over `FakeUdpTransport` — the established
test_forced_pose_no_echo / test_self_death_sequence seam, no extra link deps.
That crash is a finding in its own right; **boarded as B140**.

### Leg B — B137, the opcode literal (`7d5e1bed`)

- `HC_OP_TargetCommandReply = 0x61f9` added to `include/client/eq_opcodes.h`
  directly beside `HC_OP_TargetCommand`, with the provenance comment.
- `kZoneOpcodeTable`'s `OnTargetCommandResponse` entry keyed by the enumerator;
  `zone_component.h`'s trailing `// case 0x61f9` comment retyped to the name.
- `src/client/core/eq_strings.cpp` `GetOpcodeName` names it — **and
  `HC_OP_TargetCommand` itself, which was also absent.**

**Two surprises, both taken.**

1. **A SECOND bare literal in the same table**, which the board text did not
   mention: `{0x65ca, …}`, the Consider response — and `HC_OP_Consider = 0x65ca`
   already existed in `eq_opcodes.h` and simply was not used. The plan's pin is
   "no bare hex literal remains in the table", so it was fixed with the first.
2. **The optional self-check was taken, and it found 23 dispatched opcodes with
   no `GetOpcodeName` entry at all** — `HC_OP_Action`, `HC_OP_Track`,
   `HC_OP_GroupUpdate`, `HC_OP_GroupInvite`/`GroupInvite2`/`GroupDisband`/
   `GroupFollow`/`GroupCancelInvite`, `HC_OP_ReadBook`, `HC_OP_PetBuffWindow`,
   `HC_OP_ApplyPoison`, and the twelve skill responses (`Hide`, `Sneak`,
   `Forage`, `Fishing`, `Mend`, `FeignDeath`, `PickPocket`, `SenseHeading`,
   `SenseTraps`, `DisarmTraps`, `Begging`, `InstillDoubt`). Every
   unhandled-opcode WARN and every `DumpPacket` header for these read
   `OP_Unknown_0x…`. All 23 already had enumerators; all 23 cases were added.

**Test:** `tests/test_zone_opcode_literals.cpp`, three cases. It is a **source
reader** by design: the opcode VALUES are unchanged, so nothing observable at
runtime distinguishes `0x61f9` from `HC_OP_TargetCommandReply`, and only the
text can pin the spelling.

| test | pre-fix | post-fix |
|---|---|---|
| `ZoneOpcodeLiterals.EveryTableKeyIsAnHcOpEnumeratorNotAHexLiteral` | **FAIL** — 2 keys: `zone_dispatch.cpp:748` `'0x65ca'`, `zone_dispatch.cpp:773` `'0x61f9'` | **PASS** (145 keys read) |
| `ZoneOpcodeLiterals.EveryDispatchedOpcodeResolvesToAName` | **FAIL** — 23 opcodes reported `OP_Unknown_0x…` | **PASS** |
| `ZoneOpcodeLiterals.TargetCommandReplyHasAnEnumeratorAtTheMeasuredValue` | PASS (added with the enumerator) | PASS |

Pre-fix binary run: exit 1, 1 passed / 2 failed. Post-fix: exit 0, 3 passed.
The reader asserts `keys.size() > 100` first, so a regex that stops matching
fails loudly rather than passing vacuously.

**G01 golden: byte-identical.** `git status --porcelain tests/goldens/` is
empty across both legs — `tests/goldens/zone_opcodes_g01.txt` is untouched
(its line for this opcode is `0x61f9 0x61f9`: the golden pins the VALUE, and
the value did not move). `test_zone_dispatch_table` passes **unedited**, 6/6.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 tree) | EXIT=0 |
| `ctest -j6` after Leg A | **3955/3956**, exit 8, sole failure `AudioManagerTest.PreloadCommonSounds` (the waived ALSA quirk) |
| `ctest -j6` after Leg B | **3958/3959**, exit 8, sole failure `AudioManagerTest.PreloadCommonSounds` |
| G01 golden | byte-identical, `test_zone_dispatch_table` 6/6 |
| `check_file_sizes.sh fable` | 8 files checked, **8 ok, 0 warn, 0 fail** (largest touched: `zone_component.cpp` 1110) |
| visual gate / ARM | not run — the plan narrows the bar ("No graphics change") |

The suite grew 3956 → 3959: +2 (Leg A) and +3 (Leg B) new cases against the
3954 on the base tip.

**One exit-143 build**, on the Leg B full build (`eq_opcodes.h` is included
almost tree-wide, so the edit rebuilt ~everything). Post-143 sweep run per
protocol: no `build/lib/st??????` ar temps, both archives non-empty with member
counts intact (`libwilleq_core.a` 322, `libEtcLib.a` 15), zero non-ELF or
sub-10 KB binaries. The 121 binaries the killed build had touched were deleted
by hand before the rebuild (gmake's own `Deleting file` lines covered only some
of them), then rebuilt: EXIT=0, 276 binaries, `missing=0`, `bad=0`.

### New board items

- **B140** (B) `SendClickDoor` / `SendClickObject` dereference
  `ctx_.zone_connection` unguarded and bypass `PacketSender::SafeQueueZone` —
  the B068 pair, still open for these two senders: the session recorder and the
  T268 seam are blind to every door/world-object click, and a null connection
  segfaults. Measured in this unit (exit 139).
- **B141** (B) `EQT::DoorStateManager` carries an unreached duplicate of the
  pending-click set (`pendingClicks_` + four accessors, zero callers tree-wide)
  — and that copy already had both the erase-on-state-update and the
  clear-on-zone-change that B136 was missing. Duplicated-then-drifted; the copy
  that was reached was the broken one. This is the root cause behind B136 and
  is the more surprising finding of the unit.
