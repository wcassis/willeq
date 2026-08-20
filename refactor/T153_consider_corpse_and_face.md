# T153 — Batch A: fix `SendConsiderCorpse`, wire `/appearance` to `SendFaceChange`

**Slot: slot1. x86. NO eqemu lock. No Pi.**

**Territory: `src/client/eq_player_info.cpp` (the `SendConsiderCorpse` body ONLY),
`src/client/combat.cpp`, `include/client/combat.h`, `src/client/action/`,
`include/client/action/`, `src/client/eq_action_handler.{h,cpp}`, `tests/`,
`refactor/`.**

**T154 (slot2) runs in parallel and edits `src/client/eq_player_info.cpp` too** —
it DELETES `SendConfirmDelete`, which sits at `:1253`, directly below the
`SendConsiderCorpse` body you are rewriting at `:1241`. **Keep your hunk inside
the `SendConsiderCorpse` body. Do not reflow, re-comment or move the neighbouring
functions.** T154 touches no action-layer file, so the T148/T149/T150 append
collision does not apply here.

This unit comes from T151's triage (`refactor/T151_zero_caller_triage.md`,
"Proposed batching" → Unit A). Read that section and the per-sender table before
starting.

## The two legs

### Leg 1 — `SendConsiderCorpse` sends a 4-byte body; the opcode needs 28

`eq_player_info.cpp:1241` writes only `PutUInt32(2, corpse_id)`. eqemu's
`Handle_OP_ConsiderCorpse` expects a full `Consider_Struct` — the same 28-byte
body the LIVE, WORKING `CombatManager::ConsiderTarget()` already builds at
`combat.cpp:356` for `HC_OP_Consider`. **Copy the shape from the live path; do
not invent one.** Confirm the size and field order against eqemu source before
committing, and say in the Review which file and line you confirmed it from.

Then make it reachable: `/consider` is already registered
(`action/command_processor.cpp:340`, aliases `con`) and already routes
`EqActionHandler::consider()` (`eq_action_handler.cpp:623`) →
`CombatManager::ConsiderTarget()`. **Route inside `ConsiderTarget()`**: when the
current target is a corpse, send `HC_OP_ConsiderCorpse`; otherwise `HC_OP_Consider`
exactly as today. `CombatManager::IsCorpse(const Entity&)` already exists at
`combat.cpp:1651` — use it, do not write a second corpse test.

**No new command.** A separate `/considercorpse` would be the "technically wired,
practically unreachable" defect this arc exists to remove.

Check whether any parser handles the reply. There is no `ZoneProcessConsiderCorpse`
in the tree. Determine from eqemu source what `Handle_OP_ConsiderCorpse` actually
sends back (it may reply on `OP_Consider`, in which case the existing parser
covers it — or it may not, in which case the send is correct and the reply is
unparsed). **Report which; do not add a parser speculatively.**

### Leg 2 — `/appearance` → `SendFaceChange`

`SendFaceChange` (`eq_player_info.cpp:1842`) has zero production callers. Wire it
through the full T132 action stack: `action::CommandProcessor` registration →
`ActionDispatcher` method → `EqActionHandler` → the existing sender. **Register in
`action::CommandProcessor` only**, never the legacy `game` registry (T97).

**The name is `/appearance`.** `/face` is taken by the *movement* face
(`command_processor.cpp:324`, category "Movement") — T146 lost a leg to exactly
this confusion. Grep both registries for `appearance` before registering and
report anything you hit.

Seven `uint8_t` fields (haircolor, beardcolor, eyecolor1, eyecolor2, hairstyle,
beard, face). Decide a usable interface — named subcommands
(`/appearance face 3`) beat seven positional numbers — and validate ranges rather
than casting whatever the user typed. **Unvalidated int-to-enum/uint8 casts are a
tracked defect class (T95); do not add one.**

T151 recorded that `OP_SetFace` is absent from the copy of `patch_Titanium.conf`
under `/mnt/projects/claude/EQEmu/`. **`HC_OP_FaceChange` is a different opcode —
check it is mapped there before claiming the command works.** If it is unmapped,
that is a finding: report it, keep the wiring, and say plainly that the packet may
never be delivered.

## Acceptance

No eqemu lock — **this unit ends verified against source and by tests, NOT on the
wire.** Be explicit in the Review. Do not write "considering corpses works now."
The wire leg is owed and must be named as owed.

Per T151's "What only a live capture can settle": whether `HC_OP_FaceChange` is
deliverable to Titanium closes for free the first time a two-client session runs
this. Record it, do not chase it.

## Verification

Full protocol bar. Suite baseline **2531/2532** at tip `c123c7b4`, sole expected
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. New tests are
expected for both legs — corpse-vs-non-corpse opcode selection, and the
`/appearance` parse/validation path.

## STOP conditions

- `/appearance` collides with an existing name you cannot resolve without Will's call.
- The corpse route needs entity state `CombatManager` cannot reach (report it; do
  not build a new cross-layer path).
- eqemu's `Handle_OP_ConsiderCorpse` turns out to expect something other than a
  `Consider_Struct`.

## Review

### Leg 1 — `SendConsiderCorpse`

**Step: confirm the size and field order against eqemu source.** Confirmed, and
the plan's diagnosis was exactly right.

* `zone/client_packet.cpp:5245` — `Client::Handle_OP_ConsiderCorpse` opens with
  `if (app->size != sizeof(Consider_Struct)) { LogDebug(...); return; }`. A
  `LogDebug`, not a `LogError`: the drop was silent on the server too.
* `common/eq_packet_structs.h:1351` — base `Consider_Struct`, 28 bytes:
  `playerid`@0, `targetid`@4, `faction`@8, `level`@12, `cur_hp`@16, `max_hp`@20,
  `pvpcon`@24, `unknown3[3]`@25.
* `common/patches/titanium_structs.h:1160` — `Titanium::structs::Consider_Struct`,
  **byte-identical**. `grep -n "OP_Consider" common/patches/titanium.cpp` returns
  **nothing**, so by the arbiter rule there is no ENCODE/DECODE and the base
  struct goes raw. Either layout gives the same 28 bytes.
* Our shared `EQT::Consider_Struct` (`include/common/packet_structs.h:427`) is
  already that layout with a `static_assert(... == 28)` — T147 corrected it from
  24. `SendConsiderCorpse` now fills it (`playerid` = `GetEntityID()`,
  `targetid` = corpse id, everything else zeroed) instead of writing a bare
  `PutUInt32(2, corpse_id)`.
* The handler reads **only** `conin->targetid` (`:5252`). Zeroing the rest is not
  a guess: it is what the live `CombatManager::ConsiderTarget()` already does for
  `HC_OP_Consider`, and the server fills those fields on the reply.

**Step: make it reachable, routing inside `ConsiderTarget()`.** Done. No new
command; `/consider` (alias `con`) is unchanged at
`action/command_processor.cpp:340`. `CombatManager::ConsiderTarget()`
(`combat.cpp`) now asks the new `CombatManager::GetConsiderOpcodeForTarget()`
first and, on `HC_OP_ConsiderCorpse`, calls
`EverQuest::SendConsiderCorpse(m_current_target_id)` and returns; otherwise the
pre-existing 28-byte `HC_OP_Consider` path runs byte-for-byte as before.
`GetConsiderOpcodeForTarget()` uses the existing
`CombatManager::IsCorpse(const Entity&)` — no second corpse test was written.

**Step: check whether any parser handles the reply. Report which.**
**`Handle_OP_ConsiderCorpse` sends no `OP_Consider` reply at all.** Read end to
end (`client_packet.cpp:5245-5348`), every exit is a `MessageString(...)` or a
`Message(...)`: `CORPSE_DECAY_TIME_MINUTE` / `CORPSE_DECAY_NOW` for an NPC
corpse; `CORPSE_REZ_TIME_HOUR` / `CORPSE_REZ_TIME_MINUTE` / `CORPSE_TOO_OLD` /
"This corpse has already accepted a resurrection." plus a decay line for a
player corpse. Those are `OP_FormattedMessage` / `OP_SpecialMesg`, which this
client already parses and displays. **So the absence of a
`ZoneProcessConsiderCorpse` is correct, and no parser was added.** The
consequence for acceptance is that the observable effect is a chat line, not a
target-window update — the `ProcessConsiderResponse` path is not involved.

Note also that `OP_ConsiderCorpse=0x773f` and `OP_Consider=0x65ca` are both
present in `utils/patches/patch_Titanium.conf` (`:243`, `:188`) and both match
our `HC_OP_*` values, so the send is deliverable.

### Leg 2 — `/appearance` → `SendFaceChange`

**Step: grep both registries for `appearance` before registering.** Done.
`grep -rn '"appearance"\|/appearance' src/ include/ tests/` at the base rev
returned **nothing** in either registry — no collision. The only `appearance`
hits in the command layer are `dumpEntityAppearance` (`/dump`, a different name)
and unrelated `SpawnAppearance` opcode text. `/face` is untouched and still the
Movement command; a test asserts both facts.

**Step: wire through the full T132 stack, `action::CommandProcessor` only.** Done:

* `action::CommandProcessor` registration `"appearance"`, no aliases, category
  `"Character"` (`command_processor.cpp`, immediately after `/consider`), plus
  `cmdAppearance()`.
* `AppearanceChange` (seven `std::optional<uint8_t>`) + `IActionHandler::
  setAppearance()` + `ActionDispatcher::setAppearance()` in
  `include/client/action/action_dispatcher.h` / `.cpp`.
* `EqActionHandler::setAppearance()` → `EverQuest::SendFaceChange(...)`.
* The legacy `game` registry (`eq_commands*.cpp`) was **not** touched (T97).

**Step: decide a usable interface; validate ranges, do not cast.** Interface is
`/appearance <field> <value> [<field> <value> ...]`, fields
`haircolor|beardcolor|eyecolor1|eyecolor2|hairstyle|beard|face`, case-insensitive.
Values go through the existing `parseU32Strict()` and are then range-checked
against the wire type (each field is a `uint8` in
`Titanium::structs::FaceChange_Struct`): `256`, `-1`, `3x`, `abc` and an odd
token count are all rejected with a usage message and **no dispatch**. There is
no unvalidated int→uint8 cast (T95).

**Step: check `HC_OP_FaceChange` is mapped in `patch_Titanium.conf`.** **It is.**
`utils/patches/patch_Titanium.conf:210` — `OP_FaceChange=0x0f8e`, matching
`HC_OP_FaceChange` at `eq.h:400`. `titanium.cpp:2779` has
`DECODE(OP_FaceChange)` with `DECODE_LENGTH_EXACT(structs::FaceChange_Struct)`
(7 bytes), which is what our 7-byte `EQT::FaceChange_Struct` already sends.
**So the send itself is deliverable** — T151's finding was about `OP_SetFace`,
the server's *broadcast* of the result, which has an ENCODE but no opcode number
in `patch_Titanium.conf` and therefore cannot reach a Titanium client. That
distinction is confirmed and unchanged: the request goes out and is handled; the
broadcast back does not arrive. The observable effects that remain are the
server's `FACE_ACCEPTED` message to the sender
(`client_packet.cpp:6411`) and `Save()` writing the fields to `character_data`
(`:6410`).

### Deviations

1. **New test file instead of growing `test_command_processor.cpp`.** The
   `/appearance` tests were first written into `tests/test_command_processor.cpp`,
   which took it from 3168 to 3245 lines and made
   `check_file_sizes.sh` report `FAIL ... (> 3000)`. The 3168-line baseline is
   **already over the ceiling at tip `c123c7b4`** — this unit did not create the
   failure, but it would have deepened it. Reverted and moved to a new
   `tests/test_appearance_command.cpp` with its own CMake target (same source
   set as `test_command_processor`). The size check now reports **0 fail**. The
   corpse-opcode tests stayed in `tests/test_combat_manager.cpp` (1025 lines,
   ok).
2. **A public `CombatManager::GetConsiderOpcodeForTarget()` was added to make
   the routing assertable.** The plan asked for a test of "corpse-vs-non-corpse
   opcode selection", but the T23 note at the top of `test_combat_manager.cpp`
   records that a CombatManager-initiated send is **not** observable from a test:
   `EverQuest::QueuePacket(opcode, packet)` no-ops with no side effect when there
   is no zone connection, and no counter records the attempt. Rather than build a
   new observation seam, the *decision* was made a named public accessor that
   `ConsiderTarget()` itself consults, and the tests assert on it. This is one
   new public method on `CombatManager`, no behaviour change to the living-target
   path.
3. **`eyecolor1` / `eyecolor2` cannot be preserved across an `/appearance` call,
   and this is surfaced rather than hidden.** `OP_FaceChange` is all-or-nothing —
   eqemu copies all seven fields into the PlayerProfile and `Save()`s — so
   unnamed fields must be re-sent at their current value. This client tracks
   `face`/`haircolor`/`hairstyle`/`beardcolor`/`beard` on the player's own
   `Entity` (spawn parse) and `EqActionHandler::setAppearance()` seeds from
   there, but **it tracks the two eye colours nowhere**: `EverQuest`'s
   PlayerProfile read parses them into locals that are never stored
   (`eq.cpp:2097-2098`) and `Entity` has no eyecolor fields. Fixing that is
   outside this unit's territory. The mitigation is that the handler echoes the
   full seven-field body to the player before sending it, so an eye colour reset
   to 0 is visible, not silent. See "Not claimed" below.
4. **`docs/module_map.md` was not updated.** Its `eq_player_info.cpp` row already
   carried a stale line count (2,006 vs 2,072 at the base rev) before this unit;
   correcting it is not in scope and was left alone rather than half-fixed.

### Findings outside scope (reported, not fixed)

* **`EverQuest::ZoneProcessPlayerProfile` discards the character's own
  appearance.** `eq.cpp:2095-2101` parses `haircolor`, `beardcolor`, `eyecolor1`,
  `eyecolor2`, `hairstyle`, `beard`, `face` out of the PlayerProfile into local
  variables that are **never read again** — nothing stores them. This is the
  reason deviation 3 exists. It is also the reachability pattern this arc keeps
  finding, in a receive path rather than a sender: parsed, correct, unreached.
  A small unit could store them on the player and let `/appearance` merge
  faithfully. `eq.cpp` is outside this unit's territory.

### Verification

| step | result |
|---|---|
| 1. `cmake --build build -j24` | **PASS** — clean, unfiltered, no new warnings |
| 2. `ctest --output-on-failure` | **2542/2543 PASS**; sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. Baseline was 2531/2532 at `c123c7b4`; +11 tests = 7 `AppearanceCommandTest` + 4 `CombatManagerConsiderOpcode` |
| 3. `check_file_sizes.sh fable` | **0 fail**, 6 ok, 5 warn — all five WARNs are files that were already over 1500 lines at the base rev |

New tests:

* `tests/test_appearance_command.cpp` (7) — registration under `Character` and
  `/face` still Movement; only-named-fields; all seven names; case
  insensitivity; the eight rejected inputs with no dispatch; 0/255 boundaries;
  not-zone-connected fails without dispatching.
* `tests/test_combat_manager.cpp` (4) — `CombatManagerConsiderOpcode`: no
  target → 0; living target → `HC_OP_Consider`; corpse target →
  `HC_OP_ConsiderCorpse`; target missing from the entity list → falls back to
  `HC_OP_Consider`.

### What is NOT claimed

**There was no eqemu lock for this unit. Nothing here has been observed on the
wire.** This unit ends verified against eqemu source and by tests only.

Specifically **not** claimed:

* **Not claimed: "considering corpses works now."** What is established is that
  the body is now the size and layout the handler demands, and that `/consider`
  reaches it for a corpse target. Whether the server answers is unproven.
* **Not claimed: "`/appearance` changes your appearance."** The opcode is mapped
  for Titanium and the 7-byte body matches `DECODE_LENGTH_EXACT`, but no packet
  has been sent.

**Owed wire leg** — one live session, no second client strictly required:

1. Zone in, kill something (or find a corpse), target it, type `/consider`, and
   confirm a `CORPSE_DECAY_TIME_MINUTE` / `CORPSE_DECAY_NOW` chat line appears.
   Confirm `/consider` on a *living* target still produces the normal con
   message — the regression risk of this change.
2. Type `/appearance face 3` and confirm the `FACE_ACCEPTED` message comes back,
   then confirm `character_data.face` changed in the DB (the T144
   outside-the-client authority). **Do not expect a visual change on a second
   client**: `OP_SetFace` has no Titanium opcode number, so the server's
   broadcast of the result cannot be delivered. Per T151's "what only a live
   capture can settle", recording that observation closes that question for
   free; do not chase it separately.

