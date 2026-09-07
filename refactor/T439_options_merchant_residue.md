# T439 — T434/T436 residue: absolute volume (B432), the zone-in snapshot (B444), Options popup enumerator (B434)

Class B, slot1, no server. Read B432, B444, B434 on the board; T434's
and T436's §Reviews; `AudioManager`'s volume API; `FrameComponent`'s
`optionsApplied*Step_` records; `src/client/core/everquest.cpp`'s
zone-in inventory snapshot and T436's slot-30 / bank 2000..2007
consumers; the `PopupType` enum and its census tests.

1. B432: the volume intents carry an ABSOLUTE step (0..20, the oracle's
   unit) and the audio manager publishes the applied absolute volume
   back (one event both `/music`-style commands and the Options sliders
   consume), deleting the renderer-side shadow records. Pre-fix failing
   test: a `/music` delta after a slider move leaves the slider showing
   the audio manager's real value.
2. B444: the zone-in snapshot publishes the cursor slot and the bank
   slots too (the consumers exist since T436); pre-fix failing test on
   the snapshot with a cursor item and a bank item.
3. B434: delete the unused `PopupType::Options` enumerator (T434 chose
   an independent window per the oracle's `RelativePosition false`) and
   fix every census/doc that lists it. Stale doc = defect.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
G05 in place if a pass changes, visual gate standalone + ARM (no image
golden should move), file sizes. Strike B432/B444/B434; new items B445+.
Live proof of the sliders/music is OWED to a lock unit — say so.

STOP: an image golden moves.

## Review

Branch `task/T439-options-merchant-residue`, base **d1458444** (verified at
checkout). slot1's cache already read `CMAKE_BUILD_TYPE=RelWithDebInfo` and
its own `third_party/irrlicht-gles2-x86`; both checked before anything else.
No eqemu — T428 holds the lock (B306) and nothing here needed one.
**THE LIVE PROOF IS OWED** for both B432 and B444, and is stated as owed in
each board strike as well as here.

### 1. B432 — the volume round trip

The board offered two ways out and they turned out to be two halves of one
thing: an absolute intent is only safe if something publishes the applied
value back, and a published-back value is only useful if the intent is
absolute. Both were done.

**The shapes I settled on.**

| | before | after |
|---|---|---|
| `MusicVolumeChangeIntent` | `float delta` | `int step` — 0..20, clamped by the handler |
| `EffectsVolumeChangeIntent` | `float delta` | `int step` |
| game→renderer | *(nothing)* | `GameEventType::VolumesChanged` carrying `VolumesChangedData{int musicStep; int soundStep;}` |
| renderer's record | `optionsAppliedMusicStep_` / `optionsAppliedSoundStep_` | **deleted** — `optionsState_` is the record |
| hotkey accumulator | `float musicVolumeDelta_`, ±0.1f | `int musicVolumeStepDelta_`, ±2 |

**The unit is the ORACLE'S, and it moved to where both threads can say it.**
T434 measured it (`eqclient.ini` `Music=18` against that client displaying
90 %), but the arithmetic lived in `options_panel.cpp` behind
`options_layout.h`, which is full of `irr::s32` and cannot be included game
side. It is now `include/client/audio/volume_steps.h` — device-free,
dependency-free, `kVolumeSteps = 20` plus three `constexpr` conversions —
and `options_panel.cpp` delegates to it under a
`static_assert(OptionsLayout::SLIDER_STEPS == eqt::audio::kVolumeSteps)`, so
the slider's travel and the wire's unit cannot drift.

**ONE publisher, five call sites.** `BridgeComponent::PublishVolumesChanged()`
(`bridge_component_ui_audio.cpp`) is the only body that pushes the event, and
every site that moves a volume calls it: the config apply in
`InitializeAudio()`, both intent handlers, and `/musicvolume` +
`/effectsvolume`. It **reads `AudioManager` back** rather than echoing the
value it was handed — the audio manager clamps, and the event must state what
it HOLDS, not what it was asked for. `B432Census.EverySiteThatMovesAVolume
PublishesTheAppliedValueBack` counts those call sites, so a sixth site added
without a publish is a red.

**The 0..1 → step conversion is LOSSY, on purpose and once.** `/musicvolume
53` leaves the audio manager on 0.53 and the slider on step 11 (55 %). Twenty
one positions is the control the oracle gives us; the audio manager keeps the
exact float it was told and only the readout rounds. Written out in
`volume_steps.h`.

**The hotkeys came along.** ±0.1 of a 0..1 volume is exactly ±2 of 20 steps,
so the travel is unchanged, but the consumer now adds the delta to the step
the audio manager last PUBLISHED instead of to a private accumulator — which
means a volume hotkey press moves the Options slider too. That was not in
scope and is a free consequence of the round trip.

**Deviation, flagged: `/volume` (MASTER) is NOT in the round trip.** The
oracle's General page declares `OGP_MusicVolumeSlider` and
`OGP_SoundVolumeSlider` and no master control, so there is nothing to keep in
sync and inventing a slider would be the T363 defect. Boarded as **B445** so
the asymmetry is recorded rather than rediscovered.

### 2. B444 — the zone-in snapshot

The loop was `for (int16_t slot = 0; slot < 30; ++slot)`. It now iterates
`eqt::inventory::ZoneInSnapshotSlots()`, a named table in
`inventory_constants.h`: 0..30 (`POSSESSIONS_COUNT`, which is equipment +
general + the cursor in one range) plus `BANK_BEGIN`..`+7`. **39 slots.**

**EIGHT bank slots, not sixteen.** `BANK_SNAPSHOT_COUNT = 8` — the era's
`EQUI_BankWnd.xml` declares `BW_BankSlot0..7` (T436 quoted it) and 2008..2015
have no consumer at all. `BANK_COUNT` stays 16 because that is the WIRE's
range, and the test asserts both facts including the negative (2008 is not
published).

The table exists so the loop and the census cannot drift; a future panel
wanting a slot family at zone-in adds it there and nowhere else.

**`SyncInventorySlotToGameState()` needed no change** — I checked rather than
assumed. It is an if/else over `isEquipmentSlot` / `isGeneralSlot` /
`isBankSlot`, so the bank slots take the correct `setBankOccupied()` arm and
the cursor falls through as a no-op, which is right (the cursor queue is not a
game-state occupancy bit).

### 3. B434 — the enumerator

Deleted, with T434's reading confirmed rather than re-litigated:
`EQUI_OptionsWindow.xml` is `RelativePosition false`, the window is drawn and
hit-tested above every centre popup, and opening it closes nothing. The three
comments that named `PopupType::Options` were rewritten, because a comment
asserting a symbol exists is a claim, not a tombstone.

**ONE CORRECTION to the board entry, measured.** B434 says "`Vendor`, `Bank`
and `Trade` at least appear in tests". They do not. A comment-blind grep over
`src/`, `include/` and `tests/` finds only `None`, `Inventory` and `Vendor`
used anywhere; `Spellbook`, `Bank`, `Loot`, `Trade` and `Skills` are named by
nothing. That is a different question from B434's (those five windows exist
and track their own `isOpen`), so it is boarded as **B446** rather than
absorbed here.

### 4. Evidence — pre-fix and post-fix

`tests/test_options_merchant_residue.cpp`, **13 cases**, device-free,
database-free, server-free; registered in `tests/cmake/ui_rendering.cmake`.

The `*Census.*` cases read the tree at runtime through `WILLEQ_REPO_ROOT`, so
one binary measures both trees — T436's recipe, and no stash (the stack is
shared across worktrees). Patch of the eleven changed sources exported,
`git apply -R`, run, `git apply`:

| case | pre-fix | post-fix |
|---|---|---|
| `B432Census.TheVolumeIntentsCarryAnAbsoluteStepNotADelta` | **FAIL** | pass |
| `B432Census.TheRendererKeepsNoShadowOfTheVolumeItLastSent` | **FAIL** | pass |
| `B432Census.EverySiteThatMovesAVolumePublishesTheAppliedValueBack` | **FAIL** | pass |
| `OptionsVolumeT439.AVolumeHotkeyMovesTwoStepsAndSaturates` | **FAIL** | pass |
| `B444Census.TheZoneInSnapshotCoversTheCursorAndTheBank` | **FAIL** | pass |
| `B434Census.PopupTypeHasNoOptionsEnumerator` | **FAIL** | pass |

**7 PASSED / 6 FAILED** reverted; **13 PASSED / 0 FAILED** re-applied.

The other seven are behaviour over the real free functions and cannot exist
pre-fix (they name types the fix creates):
`OptionsVolumeT439.TheWireUnitIsTheOraclesSliderStep` (the measured 18→90 % /
10→50 % pair, both clamp directions, all 21 steps round-tripping),
`TheApplierWritesBothSlidersAndClamps`,
`ASlashCommandThenADragLeavesTheAudioManagerOnTheSlider` — **the board's own
scenario**, composed from the real functions, which ends by asserting the
audio manager is on step 9 and specifically **not** on 12, the answer the
stale-shadow arithmetic produced — plus
`ZoneInSnapshotT439.EverySnapshotSlotReachesAConsumer` (drives
`applyCursorSlotChanged` / `applyBankSlotChanged` with every published slot
and requires each to be claimed by **exactly one** family, so a slot published
with nowhere to land is a red too),
`ZoneInSnapshotT439.ACursorItemAndABankItemLandOnZoneIn`, and two B434 cases.

**One first-run correction of my own.** Three census cases matched the
TOMBSTONE comments this unit wrote (a deleted symbol's tombstone names it on
purpose). Fixed by adding `codeOnly()`, which strips `//` and `/* */` before
searching — the same device `test_read_and_reset_ownership.cpp` already uses.

**`B434Census.NothingInTheTreeNamesPopupTypeOptions` passes in BOTH runs**,
and that is honest rather than a mistake: pre-fix, every occurrence of the
literal `PopupType::Options` was in a comment, and the case is comment-blind.
`PopupTypeHasNoOptionsEnumerator` is the real red for that item.

### 5. One real red the suite found, and it was mine

`ReadAndResetSweepGuard.NoAccessorGrowsASecondCaller` went red on the first
full run: `kSweep` in `tests/test_read_and_reset_ownership.cpp` still named
`getMusicVolumeDelta` / `getEffectsVolumeDelta`, which I had renamed, and the
guard correctly reported **0 call sites where 1 was expected**. The table is
that sweep's documentation, so it was updated with the reason for the rename
rather than waived. This is the guard working exactly as T315 designed it.

### 6. Doc rot fixed, and netted

Four sites said "**100** event types" (root `CLAUDE.md` x3,
`include/client/state/CLAUDE.md` x2) and adding one enumerator would have made
all five stale silently. They now say 101 — **and `GameEventType` is now a
`DocCounts` row**, so the next enumerator makes a red test instead of drifting.
That enum was simply not on `check_doc_counts.py`'s list, which is the exact
drift T373 built the file for. `DocCounts` reports 9 counts over 20 sites, all
matching.

Two stale prose claims went with it: root `CLAUDE.md`'s persistence paragraph
said "the volume intents carry a delta", and
`include/client/bridge/CLAUDE.md`'s Audio bullet described them unqualified.
`RendererIntent` stays at **42** — this unit changed two payloads and added no
alternative.

### 7. Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 config) | **exit 0** |
| `ctest -j6` | **4558 / 4558, 0 failures** |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` standalone | **PASS** (`=== VISUAL GATE: PASS ===`) |
| `bash scripts/build-arm-noble.sh` | **exit 0**, `ELF 32-bit LSB pie executable, ARM, EABI5` |
| `check_file_sizes.sh fable` | 25 files checked, **25 ok, 0 warn, 0 fail** |
| goldens | `git status tests/goldens/` **clean** — nothing moved, no image golden touched |

**G05: unchanged, and that is the correct result.** `test_frame_render_order`
is **4/4** with `frame_render_order_g05.txt` byte-identical, because nothing
this unit did altered `renderer_frame_render.cpp`'s statement list — the
Options hop and the applier call are exactly where T434 put them. G05 learns
an edit in place only when there IS an edit for it to learn.

The other order/coverage goldens for TUs this branch touched were run too:
`test_bridge_intent_table` **6/6** (`bridge_component_intents.cpp`),
`test_reachability_sweep` **5/5** (which is what proves `VolumesChanged` has
both a publisher and a bridge consumer),
plus `test_options_window` 29/29, `test_merchant_bank_cursor` 32/32,
`test_ui_preferences` 18/18 and `test_static_ui_panels` 59/59.

### 8. Two ctest artefacts, neither a defect

1. **A discovery timeout on `test_login_world_flow`** — "Process terminated
   due to timeout" in `GoogleTestAddTests.cmake`. The documented cold-cache
   shape: `--gtest_list_tests` on that binary exits 0. I then ran the T436
   sweep over **all 287** `build/bin/test_*` binaries; two "failed" and
   neither is a gtest binary at all (`test_gl_points` is an EGL tool that
   prints its own banner, `test_music_loopback_compare` says "Unknown mode"),
   and neither is ctest-registered. `libwilleq_core.a` holds 356 members and
   there are no `st??????` `ar` temps. Clean.
2. **A `visual_gate` CPU-p95 ceiling miss on befallen** (13.259 vs 10.948)
   while the ARM cross-build ran concurrently. Standalone: **PASS**. The
   documented load artefact; the failing line named the CPU ceiling only and
   no image comparison.

### 9. Board

Struck **B432**, **B434**, **B444**. Two new items, allocated from B445 (T437
in slot2 may also allocate from B437+; renumber at merge if it collided):
**B445** (master volume is deliberately outside the round trip) and **B446**
(five more `PopupType` enumerators with no use — the corrected census B434
got wrong).

### 10. What surprised me

`PopupType`'s dead enumerators are not one item, they are six, and B434 named
the wrong five as live. Worth noting because the entry's parenthetical read
like a measurement and was not one.

And the `DocCounts` gap: the file exists precisely to net hand-maintained
numbers, and the single most-repeated number in the orientation docs — the
event-type count, stated at five sites — was not in its table. Anything adding
a `GameEventType` since T373 would have left all five stale with a green suite.
