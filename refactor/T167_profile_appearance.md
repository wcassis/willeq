# T167 — Store the PlayerProfile appearance fields; make /appearance merge faithfully

**Slot: slot1. x86 only. NO eqemu, NO lock, NO Pi (offline).** Orchestrator
executed directly.

**Territory: `include/client/eq_profile_appearance.h`,
`src/client/eq_profile_appearance.cpp`, `include/client/eq.h`,
`src/client/eq.cpp` (the profile parse site only),
`src/client/eq_player_info.cpp` (`SendFaceChange` test seam only),
`src/client/eq_action_handler.cpp` (`setAppearance` only),
`tests/test_profile_appearance.cpp`, CMake, `refactor/`.**

## The defect

`ZoneProcessPlayerProfile` parsed all seven appearance fields into locals and
read **none** of them (`eq.cpp:2091-2097` on `fable`) — the
implemented-but-unreached pattern, and the first instance found on a
**RECEIVE** path rather than a send path. T153 found it while wiring
`/appearance` and recorded it as out of its territory.

It was not cosmetic. eqemu's `Handle_OP_FaceChange`
(`zone/client_packet.cpp:6387`) copies **all seven** fields into the
PlayerProfile and `Save()`s, with no rejection path for a correctly-sized
body — **a field you do not re-send is a field you have set to zero.** Since
`Entity` has no eyecolor fields and the profile's copies were discarded,
**every `/appearance` reset both of the character's eye colors to 0**, and
saved that to the database, however small the change the user actually asked
for.

## What was done

**1. The parse is stored, and it lives somewhere testable.**
`ParseProfileAppearance()` + `ProfileAppearance` in a new small TU rather than
more of `ZoneProcessPlayerProfile`: `eq.cpp` is at its accepted size ceiling,
and a private method cannot be tested at all. The result is stored on
`EverQuest` in the same once-per-profile shape as `m_guild_id` (T133) and
`m_gm_mode` (T148). `eq.cpp` is 3 lines **shorter** than before.

**2. The offsets were re-verified from ground truth, not from our own
comments.** All seven were checked field-by-field against eqemu's
`common/patches/titanium_structs.h` annotated offset column (struct at :814):
`haircolor`..`beard` are six consecutive bytes at **172–177**, `face` is at
**2264**, immediately after `WIS`. Our pre-existing comments happened to be
right; re-deriving them from the authority is what makes that a fact rather
than a copy.

**3. `/appearance` now merges from the PROFILE, not the Entity.** Beyond
being the only source of the eyecolors, it is the correct source for the other
five: `Entity`'s copies are the current **visual**, which an illusion
overwrites (`eq_entities.cpp:2131-2134`), so merging from `Entity` while
illusioned would **persist the illusion's hair and beard into the character's
saved profile**. Merging from the profile — the same storage eqemu is about to
overwrite — cannot. Entity remains the fallback for its five fields when no
profile has been parsed, preserving T153's behaviour on that path.

**4. A second `/appearance` no longer reverts the first.** eqemu applies the
whole body unconditionally, so what we sent IS the profile afterwards; the
cached copy advances to match. Without this the eyecolors this unit exists to
preserve would have survived exactly one command per zone-in.

**5. With no profile AND no self entity, the request is REFUSED** rather than
sent as a fabricated all-zero body. Zero is legal for every field, so an
unknown appearance and a real all-zero one are indistinguishable, and guessing
wrong permanently rewrites the character server-side.

## Verification

- **8 new tests, and the headline set was MEASURED against the old code, not
  argued.** Reverting only `eq_action_handler.cpp` to `fable` and rebuilding:
  **4 of the 5 merge tests FAIL** (unnamed-fields-preserved, zero-is-a-value,
  no-revert, and the refusal). The fifth passes by design — it names all seven
  fields, so no merge is involved; it is the control.
- The three parse tests pin the offsets with **distinct non-zero,
  non-consecutive sentinels**, so an off-by-one inside the six-byte run lands
  on another field's value and is caught rather than masked. Zero is never
  used as an expected value.
- Suite **2603/2604** (2596 + 8), sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`.
- Build clean; `build-gles2-x86.sh` clean; **visual gate PASS**, goldens
  untouched; `build-arm-noble.sh` clean.
- `check_file_sizes.sh fable`: no new FAILs. `eq.cpp` stays over the accepted
  legacy ceiling and is **3 lines shorter** than before this unit.

## Not verified, and honestly

- **Not wire-verified.** The eqemu behaviour it depends on is read from
  server source (`Handle_OP_FaceChange`, and `SETUP_DIRECT_DECODE`'s
  `new unsigned char[...]{}` value-initialisation), plus T155's live evidence
  that `/appearance` reaches the DB. **A live probe is owed**: log in, read
  `character_data.eye_color_1/2`, run `/appearance face=N`, and confirm the
  eye colors are unchanged. On `fable` they would go to 0.
- **The entity-fallback path is untested by construction.** `m_entities` has
  no public mutator and I did not add a test seam purely to reach it. The
  profile path and the refusal path are both covered.
- **Upstream note, not a bug of ours:** `Handle_OP_FaceChange` also writes
  `drakkin_heritage`/`drakkin_tattoo`/`drakkin_details` from the decoded
  struct. Titanium's `FaceChange_Struct` has no such fields, and
  `SETUP_DIRECT_DECODE` zero-fills the emu struct, so **every `/appearance`
  from any Titanium client zeroes those three profile fields**. Harmless in
  this era (no Drakkin race), recorded so nobody re-derives it later.

## Still open in this area

`OP_SetFace` — the server's **broadcast** of an accepted change — is not
mapped for Titanium (T151/T153), so **a second client never sees an appearance
change**. That is protocol, not our bug, and it is why this unit's acceptance
cannot be "watch the other client's face change".
