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

(executor fills this in)
