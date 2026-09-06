# T404 — sounds at double rate (B250); the stale self entity (B251); memspell into an occupied gem (B254)

Class A ×3, slot3 (now RelWithDebInfo — the first slot at main's build
type), no server. Read the three board entries and T401's §Review §1d,
§1e, §6.4 (`review/T401/`).

## Leg A — B250

Every PFS sound plays at k = 2.00: the assets are 11,025 Hz 16-bit mono;
the mixer/decoder treats them as 22,050 (or the WAV header's sample rate
is read from the wrong field / a default rate is assumed after the PFS
path skips the header parse). Reproduce offline: decode `BuyItem.WAV`
from the archive through the production path and assert the decoded
sample rate and frame count against the WAV header (pre-fix failing:
rate or duration off by 2). Fix at the decoder, not by halving the rate
(find WHY it is doubled — a `bytesPerSample`/`channels` mix-up or a
hard-coded 22050); a loose-file load of the same WAV must still be
correct (the pre-T392 path). Tests over three assets. Strike B250; the
audible re-check (T401's loopback rig: ncc peak at k = 1.00) is OWED.

## Leg B — B251

The local player's own `Entity` row is never updated when the server
relocates him (`#goto`, zone-in warp, death respawn): sounds are emitted
from the spawn pose and self-targeted range checks fail (`distance=1019`
caster == target). Find the ONE place the player's authoritative
position lives (`PlayerState`/movement) and the places that read the
self entity's position instead; either update the self entity row on
every position change the player component publishes (the K-mechanism
substitution: readers of the entity row for `MySpawnId()` read the
player position), or make the self entity row mirror it on
`OP_ClientUpdate`/`OP_MoveTo`-style relocations. Tests, pre-fix failing:
after a forced relocation event, a combat sound's emit position equals
the player position; a self-targeted cast's distance is 0. Strike B251.

## Leg C — B254

`OP_MemorizeSpell` with `scribing=2` (un-memorise) for slot N followed by
`scribing=1` for the same slot leaves the gem bar casting the OLD spell.
Read the handler: the order/guard bug (does `scribing=2` clear the slot
after the `scribing=1` arm ran? is the gem bar keyed by spell id and the
old id not erased?). Test, pre-fix failing: the two packets in that order
→ `/cast 1` sends the NEW id. Strike B254.

## Proof

Build, `ctest -j6` (no expected failures beyond B240's gate delta and,
until T403 merges, the atlas determinism test), pre-fix/post-fix per
leg, file sizes; no graphics change.

STOP: Leg B's authoritative position has two writers that disagree
(report both; do not pick).

## Review

Branch `task/T404-double-rate-stale-self-memspell`, base `4f5c9125` (verified
at checkout). Three commits, one per leg. No server taken. Slot3's `build/`
was the first at `CMAKE_BUILD_TYPE=RelWithDebInfo`; the first build was a full
rebuild and was green.

| Leg | Board | Commit | Test file | Pre | Post |
|---|---|---|---|---|---|
| A | B250 | `4452b378` | `tests/test_audio_playback_rate.cpp` | 7 of 8 FAIL | 8 of 8 PASS |
| B | B251 | `853bdf2b` | `tests/test_self_entity_position.cpp` | 4 of 7 FAIL | 7 of 7 PASS |
| C | B254 | `b75ac801` | `tests/test_memorize_into_occupied_gem.cpp` | 5 of 6 FAIL | 6 of 6 PASS |

### Leg A — WHY the rate doubles

**The plan and the board both named the wrong code.** `loadSoundDataFromPfs`
is correct, and so is the decoder: `SoundBuffer::loadFromMemory` calls
`drwav_open_memory_and_read_pcm_frames_f32` and stores the rate the RIFF
header carries. The new decode assertions **pass on the pre-fix tree** —
`BuyItem.WAV`, `LevDn.WAV`, `SpelCast.WAV` and `SwordBlk.WAV` all decode at
**11,025 Hz**, straight out of each archive member's own header. There is no
hard-coded 22050 anywhere in the load path and no `bytesPerSample`/`channels`
mix-up.

The doubling is one line further down, in `AudioMixer::render()`
(`src/client/audio/audio_mixer.cpp`), the PCM sample path:

    ch.position++;

stepped once per **output** frame, unconditionally. `MixChannel::sampleRate`
(`include/client/audio/audio_mixer.h:41`) was **written by every producer and
read by nobody**: `SfxManager::play` sets it from the cached `WavData`,
`MusicPlayer::playChannel` sets it, `resetChannel()` defaults it to 22050 —
and `render()` never mentioned it. The miniaudio device is opened at
`AudioMixer::SAMPLE_RATE == 22050` (`audio_manager.cpp:149`), so a channel
always played back at the device rate regardless of what its asset was
recorded at.

That answers the question B250 left open. It is **not** "the decoder assumes
22,050" and **not** "everything is doubled". It is **"every asset plays at the
device rate"** — which happens to double the entire shipped effects set
because every one of them is 11,025 Hz, and which would have played a
44,100 Hz asset at HALF speed by the same line. The new
`DoubleRateAssetLastsHalfAsManyDeviceFrames` test asserts that direction, so
the fix cannot be a hard-coded halving.

Fix: `MixChannel::position` becomes a fractional **source**-frame cursor
advanced by `sampleRate / SAMPLE_RATE` per output frame, with linear
interpolation between the two bracketing source frames. A device-rate asset
steps by exactly 1.0 with `frac == 0`, so those channels are unchanged. A
looping channel keeps its fractional remainder across the wrap instead of
accumulating a phase error. `position` is touched by nothing but `render()`
and three `= 0` initialisations, so the type change is contained.

Pre-fix numbers, against each asset's own header duration:

| Asset | Header | Mixer played | Ratio |
|---|---|---|---|
| `BuyItem.WAV` | 0.5155 s | 0.2586 s | 1.993 |
| `LevDn.WAV` | 0.6741 s | 0.3396 s | 1.985 |
| `SpelCast.WAV` | 9.3576 s | 4.6817 s | 1.999 |
| `SwordBlk.WAV` | 0.7909 s | 0.3976 s | 1.989 |

T401's live measurement was k = 2.00 on the first three. A 1 kHz tone stored
at 11,025 Hz rendered **800** zero crossings in 0.2 s pre-fix and **400**
post-fix.

**OWED: the audible re-check on T401's loopback rig (ncc peak at k = 1.00).**
This unit took no server, as dispatched.

Also fixed for free, unmeasured: `MusicPlayer`'s PCM-WAV music path sets
`ch->sampleRate` from the file too — though it currently routes through the
render callback, so nothing observable changed there.

### Leg B — where the self entity row is updated from now

**STOP condition NOT hit.** The plan warned that the authoritative position
might have two writers that disagree. It does not. There is ONE store and it
is `m_game_state.player()` — T288 deleted `EverQuest`'s duplicate members for
exactly this reason, and `eqt::PlayerFacade` is an accessor into it, not a
copy. The disagreement is between that store and the **projection** of it in
`EverQuest::m_entities[MySpawnId()]`, which is what every positioned sound and
every range check reads.

Before this unit the projection was written by exactly four sites: the two
`ZoneProcessClientUpdate` branches (`entities_component_updates.cpp`),
`WarpToPosition` (`/warp`, `commands_movement.cpp`) and the spawn that created
it. **Never** the movement integrator, **never** the renderer's
`PlayerPositionChanged` intent (`bridge_component.cpp:726`), **never** a
zone-in warp or a death respawn. And the server does not routinely echo our
own `OP_ClientUpdate` — the handler logs `*** RECEIVED OUR OWN ClientUpdate!`
as a notable event — so in a normal session the row held the spawn pose for
the whole session, which is exactly T401's `pos=(428,-74,3)`.

Every position change the player undergoes goes through one of two funnels,
both on `PlayerFacade`: `SetPosLocal()` (the per-tick integrator and the
renderer intent, no event) and `SetPosition()` (discrete relocation, fires
`PlayerMoved`). The facade now takes `m_entities` by reference — declared at
`eq.h:656`, long before `m_player_facade` at `eq.h:868`, so the binding order
is safe — and refreshes the projection from both, and nowhere else. It writes
the values it was just handed; no new store.

**POSITION ONLY, deliberately.** `ZoneProcessClientUpdate` writes
`it->second.heading = h_player` where `h_player == server_h` verbatim, while
writing `90 - server_h` into `PlayerState` — the player-only flip that
`docs/CLAUDE.md`'s Heading/Rotation Conventions calls load-bearing. Mirroring
`HeadingDeg()` into the row would rotate every self row by 90 degrees plus a
mirror. `HeadingIsNotMirroredIntoTheSelfEntityRow` pins that so a later unit
does not "finish the job".

Pre-fix, the self-targeted range check computed **977.78** with caster and
target the same character (T401 read **1019.0** live for the same shape — the
difference is the test's synthetic spawn pose, not the mechanism). Post-fix it
is 0.

**OWED: the live proof** — a melee sound audible at the player's real position
after a `#goto`.

### Leg C — the memspell ordering bug

There isn't one. **The arms did not exist.** The handler is
`eqt::zone::ZoneComponent::OnMemorizeSpell` in
`src/client/zone/zone_dispatch.cpp` — not `spell_manager.cpp`, as B254 and the
plan both said — and it had two arms, **both guarded on `scribing == 0`**:

* the spellbook-scribe confirmation (`m_pending_scribe_spell_id` matches);
* an `else if (slot < EQ::MAX_SPELL_GEMS && scribing == 0)` whose entire body
  is a `LOG_DEBUG` reading *"Existing gem memorization - SpellManager handles
  this via other mechanisms"*.

It does not. `SpellManager::m_spell_gems` moved on exactly two things: our own
local memorise timer (`updateMemorization`) and the `PlayerProfile` bulk load
(`setSpellGems`). Nothing the server said about a **gem** ever reached it, so
`scribing=2` and `scribing=1` both fell off the end of the handler. The gem
bar showed — and cast — whatever the profile had put there.

That also corrects T401's reading of the control case: `#memspell 54 1` into
"an empty slot 1" did not work because the `scribing=1` arm works; gem 1 was
holding what the profile gave it and 54 happened to match. The new
`MemoriseIntoAnEmptyGemStillWorks` test is red pre-fix.

New `SpellManager::applyServerGemUpdate(gem_slot, spell_id, scribing)` plus
the dispatch arm for `scribing` 1 or 2 with `slot < MAX_SPELL_GEMS`. One
packet, one edit, applied in arrival order, so the un-memorise that precedes
the memorise for the same slot cannot undo it — and
`MemoriseThenUnmemoriseLeavesTheGemEmpty` pins that the order is honoured in
the other direction too. Two deliberate choices, both commented at the code:

* the un-memorise is **unconditional**, unlike `forgetSpell()` which refuses
  while casting — the server has already emptied the slot, and refusing would
  leave the client holding a spell the server says is gone;
* a response matching our **own** in-flight memorise (same slot, same spell)
  is ignored, so the `MemorizeProgress` animation is not cut short by the echo.

A `slot >= MAX_SPELL_GEMS` is a spellbook slot and is not clamped into gem 7
(`ASpellbookSlotIsNotAGem`).

### Verification

1. `cmake --build build -j24` — **EXIT=0** (full rebuild at RelWithDebInfo).
2. `ctest --test-dir build -j6 --output-on-failure` — **4159 / 4162**, exit 8.
   Three failures, all accounted for:
   * `visual_gate` — `eqe1MappedBytes` mismatch in qeynos2, gfaydark and
     befallen, **+19,343 bytes in each of the three**, i.e. B240 exactly,
     awaiting Will. Not this unit.
   * `ParticleAtlasAlpha.ShippedFileMatchesTheGeneratorPixelForPixel` — T403's
     generator determinism at -O2, in flight in slot1. Not this unit.
   * `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` — a
     **load artefact of `-j6`**, the class the protocol's closing note names.
     Re-run standalone: `2/2 Passed`, 100%. Not a real red.
3. `bash scripts/build-gles2-x86.sh` — **EXIT=0**, run as a compile check on
   the shared core (this unit changed no graphics code).
4. Goldens untouched. No visual change.
5. `bash scripts/refactor/check_file_sizes.sh fable` — **30 checked, 30 ok, 0
   warn, 0 fail.** Largest touched file `src/client/spell/spell_manager.cpp`
   at 1,451 lines (ceiling 1,500) — worth knowing before the next unit adds
   to it.

### Surprises

1. **All three board entries named the wrong code.** B250 pointed at the PFS
   loader and the decoder; both are correct and the new decode assertions pass
   pre-fix. B254 pointed at `spell_manager.cpp` and at an ordering/guard bug;
   the handler is in `zone_dispatch.cpp` and the arms were simply absent. Only
   B251 pointed at a real reader, and even there the fix belongs at the
   writer, not the reader. The measurements in all three were exact; the code
   attributions were guesses.
2. **A field written by every producer and read by nobody survived years.**
   `MixChannel::sampleRate` has a T370 TSAN banner two lines above it
   discussing the atomicity of its neighbours. Nobody noticed the field itself
   was inert. A "zero readers" sweep over struct fields would have found it.
3. **The self row and `PlayerState` disagree about the heading convention**,
   and that is correct — `server_h` verbatim in one, `90 - server_h` in the
   other. Mirroring position was safe; mirroring the obvious neighbouring
   field would have introduced a new defect. Pinned by a test.
4. `Spawn_Struct` positions are **eighths**, so a test that writes the pose
   directly lands at one eighth of it. The first pre-fix run failed in `SetUp`
   for that reason and proved nothing until it was fixed.
5. **B036's clause 3 mentions both B250 and B251** and is annotated rather
   than struck — it stays open on its own unmeasured rows.
