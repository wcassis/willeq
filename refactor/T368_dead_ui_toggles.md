# T368 — the three dead UI toggle keys get real toggles (B101); consumer-less InputActions (B100)

Class B, slot2. Two legs, one commit each. Read the B101 and B100 board
text and T365's §Review. The eqemu lock is held by T367 — this unit does
NOT take it.

## Leg A — B101

`RA::ToggleGroup` (G, Alt+P alternate), `RA::TogglePet` and
`RA::ToggleBuffWindow` reach EMPTY arms in `renderer_frame_input.cpp`
("visibility is driven by inGroup state"). A bound key that does nothing
is a defect; the official client's windows toggle open/closed on their
key regardless of state.
1. Oracle first, without the server: the official client's default key
   bindings live in its own files (`/mnt/projects/EverQuestP1999` —
   `eqclient.ini` [KeyMaps]/keyboard sections, `defaults/`, or the UI XML
   for the options window; find them and QUOTE the lines) — record the
   official default keys for the group, pet and buff windows and compare
   with `hotkey_manager.cpp`'s bindings. If they differ, fable's binding
   changes to the official one (a binding picked by argument is the
   smell). If the files do not record defaults, say so and leave the
   bindings; a screenshot of the options window is owed when the lock
   frees (board it).
2. Give each of the three the `toggleSkillsWindow()` shape: an `isOpen`
   the panel honours, toggled by the key. Read how each panel decides to
   draw today (`inGroup`, pet exists, buffs present) — the toggle must
   compose with it the way the official client does: a closed window
   stays closed when you join a group; an open window with no group draws
   its empty frame (check what the oracle draws for an empty group window
   — if unknown, the empty frame, and say it is unverified).
3. Tests, pre-fix failing: each key flips its panel's `isOpen`; the panel
   draw honours it (the static-panel UI test seam, T54–T56 shape).
4. Strike B101.

## Leg B — B100

`InputAction::ToggleVendor`, `TogglePetWindow`, `ToggleTrainer`,
`ToggleSpellbook` — no producer, no consumer. Delete them; restate or
delete `UIToggleActions_AreNotBridgeTerritory` (with all seven gone the
case has no subject — delete it and say so). Compiler confirms no reader.
Strike B100.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, file sizes;
`src/client/graphics/frame` changes → visual gate standalone + ARM
(slot2's trees are at willeq.db v6).

STOP: the official default binding for one of the three is a key fable
already binds to something else (report the collision; do not rebind by
argument).

## Review

Branch `task/T368-dead-ui-toggles`, base `0a4257fd` (verified at checkout).
Three commits, no merge, no push. **The eqemu lock was never taken** — nothing
here needed a server; the whole oracle is files on disk. slot2's build cache
already pointed at its own `third_party/irrlicht-gles2-x86` (checked before the
first build).

| Commit | Leg |
|---|---|
| `466bcadc` | Leg A — the three dead toggle keys (B101) |
| `8afdc6d7` | Leg B — the four consumer-less InputActions (B100) |
| this one | board strikes + this Review |

### The oracle, without the server

Step 1 asked for the official default key bindings. **The official ACTION names
are recorded; the official default KEYS are not.**

**What IS on disk.** `eqgame.exe` builds its ini keys from the literal
`KEYMAPPING_` plus a name out of a **247-entry `char*` table** (file offset
`0x2945a8`, VA `0x6945a8`, walked to its ends by validating every DWORD as an
`.rdata` string pointer). The three actions this unit is about are in it:

```
index 152  TOGGLE_PARTYWIN        (group window)
index 155  TOGGLE_BUFFWIN         (buff window)
index 168  TOGGLE_PETINFOWIN      (pet window)
```

(for orientation, the same table holds `index 2 FORWARD`, `3 BACK`, `4 RIGHT`,
`5 LEFT`, `12 DUCK`, `14 INVENTORY`, `154 TOGGLE_SPELLSWIN`,
`228 TOGGLE_SKILLS_WND`.)

**What is NOT on disk.** No default-key values. The ini stores a
`KEYMAPPING_<NAME>_<n>` line **only for a binding the user changed**, and the
only such lines anywhere under `/mnt/projects/EverQuestP1999` are five movement
overrides, repeated across seven files. Quoted verbatim from
`/mnt/projects/EverQuestP1999/eqclient.ini`:

```
526:[KeyMaps]
527:KEYMAPPING_FORWARD_2=17
528:KEYMAPPING_BACK_2=31
529:KEYMAPPING_RIGHT_2=32
530:KEYMAPPING_LEFT_2=30
531:KEYMAPPING_DUCK_1=208
```

`eqclient-obian.ini`, `eqclient-seth.ini`, `eqclient-bilblow.ini`,
`p1999-obian.ini`, `p1999-obian2.ini` and `p1999-bilblow.ini` carry the same
five (`p1999-obian.ini` adds `FORWARD_1=200 BACK_1=208 RIGHT_1=205 LEFT_1=203`,
`DUCK_1=45`). `keyboard.txt` is a scancode-to-character map, not a binding file.
`defaults.ini` has no `[KeyMaps]` section at all. And the exe's table is a bare
pointer array with no parallel scancode array: `.data`/`.rdata` were scanned for
a 247-long window of values ≤ `0xDF` at strides 1, 2, 4, 8, 12 and 16, and every
hit was either `.text` zero padding or ASCII noise.

**So the bindings are left exactly as they were**, per the plan. fable's, from
`src/client/input/hotkey_manager.cpp`:

| Action | fable's binding | Official action name | Official default key |
|---|---|---|---|
| Group window | `KEY_KEY_G`, plus `Alt+KEY_KEY_P` | `TOGGLE_PARTYWIN` | not recorded on disk |
| Pet window | `KEY_KEY_P` | `TOGGLE_PETINFOWIN` | not recorded on disk |
| Buff window | `Alt+KEY_KEY_B` | `TOGGLE_BUFFWIN` | not recorded on disk |

**No STOP fired** — no collision to report, because there is no official default
to collide with. The owed screenshot of the options window is boarded as
**B115**.

### The oracle DID settle the default OPEN STATE, which the plan did not ask for

`defaults.ini` carries a `Show=` key **only under a section whose window ships
closed**. Grepping every section header immediately preceding a `Show=` line in
the whole file returns exactly three:

```
258:[HotButtonWnd3]
259:Show=0
260:[HotButtonWnd4]
261:Show=0
307:[SelectorWindow]
308:Show=0
```

`[GroupWindow]` (line 277), `[PetInfoWindow]` (323) and `[BuffWindow]` (111)
have position and size keys and **no `Show`** — they ship SHOWN. Corroborated by
real players' persisted per-character UI files: `UI_Obian_project1999.ini` has
`[GroupWindow] Show=1` and `[InventoryWindow] Show=0`;
`UI_Bilblow_project1999.ini` has `[GroupWindow] Show=1`, `[SpellBookWnd] Show=0`
and **`[PetInfoWindow] Show=0`** — a pet window closed by hand, which proves the
official flag is a user toggle held independently of whether a pet exists. That
is the measured basis for `isOpen = true` and for composing it the way below,
rather than picking a default by argument.

### How each panel's draw now composes the toggle with its auto-driven state

Every render function early-returns on a free predicate in
`include/client/graphics/ui/static_panels.h`, so the renderer and a device-free
test read the same body (the `buffBarHasContent()` idiom T277 established):

| Panel | Predicate | Was |
|---|---|---|
| Group | `groupPanelIsVisible(s)` → `s.isOpen` | `if (!state.inGroup) return;` |
| Pet | `petPanelIsVisible(s)` → `s.isOpen && s.hasPet` | `if (!state.hasPet) return;` |
| Buff bar | `buffBarIsVisible(s)` → `s.isOpen && buffBarHasContent(s)` | `if (!buffBarHasContent(state)) return;` |

- **Group** — the toggle alone. An OPEN window with no group draws its frame and
  its "Group" title and then returns *before* the member loop: the member array
  is not consulted at all, because a disband leaves stale rows in it until the
  next `GroupUpdate` and an open window must not show them. A CLOSED window
  stays closed when you join a group. **The empty frame's CONTENT is UNVERIFIED**
  — `defaults.ini` proves the official window is shown, not what it paints with
  no group. Said out loud in the header comment, in the test name and in B115.
- **Pet** — `isOpen && hasPet`. With no pet there is no name, no health bar and
  no button row, and unlike the group panel this one has no title of its own, so
  an "empty frame" would be a blank box. `P` therefore hides a pet panel that
  has a pet, and does nothing visible with no pet.
- **Buff bar** — `isOpen && at least one buff`, for the same reason (it is a HUD
  strip, not a window). `hitTestBuffBar()` reads the *same* predicate, so a bar
  hidden by `Alt+B` is unclickable as well as invisible — an icon you cannot see
  can never be clicked off.

`toggleGroupWindow()`, `togglePetWindow()` and `toggleBuffWindow()` live in
`src/client/graphics/frame/renderer_frame_input.cpp` next to
`toggleSkillsWindow()`, return the new state, and — deliberately unlike it — do
**not** close the centre popups: these are HUD panels, and the official client
keeps them up alongside everything else. Pinned.

### A second defect found while wiring it

`IrrlichtBridge`'s `PetRemoved` arm was `renderer_->Frame().petPanelState_ =
{};`. With `isOpen` on the struct that wholesale reset put the flag back to its
default `true`, so a player who closed the pet window with `P` would have had it
silently reopened by the next charm break or pet death. It now preserves the
flag across the clear. Not in the plan; found by reading every writer of the
three states before adding a field to them.

### Tests — 11 new, 10 failing before the change

Proved by mutating the production behaviour back in place (never `git stash` —
it is shared across slots), rebuilding, running, then restoring from saved
copies. For Leg A the new fields and predicates were KEPT and only their bodies
reverted (`isOpen` dropped from each predicate, the three switch arms returned
to bare `break;`), so the cases could compile and FAIL rather than not build.
For Leg B the four enumerators were textually restored.

| Test | Pre | Post |
|---|---|---|
| `FrameComponentTest.T368_GroupKeyFlipsTheGroupWindowOpenState` | FAIL | PASS |
| `FrameComponentTest.T368_PetKeyFlipsThePetWindowOpenState` | FAIL | PASS |
| `FrameComponentTest.T368_BuffKeyFlipsTheBuffBarOpenState` | FAIL | PASS |
| `FrameComponentTest.T368_ClosingAPanelHonoursTheDrawGateItsRendererReads` | FAIL | PASS |
| `FrameComponentTest.T368_TheThreeTogglesDoNotCloseTheCentrePopups` | pass | PASS |
| `T368PanelVisibility.GroupPanelDefaultsOpenAndDrawsItsEmptyFrameWithNoGroup` | FAIL | PASS |
| `T368PanelVisibility.ClosedGroupPanelStaysClosedWhenYouJoinAGroup` | FAIL | PASS |
| `T368PanelVisibility.PetPanelNeedsBothTheToggleAndAPet` | FAIL | PASS |
| `T368PanelVisibility.BuffBarNeedsBothTheToggleAndABuff` | FAIL | PASS |
| `T368PanelVisibility.AClosedBuffBarIsUnclickableAsWellAsInvisible` | FAIL | PASS |
| `InputActionEnumTest.T368_NoConsumerlessUITogglesRemain` | FAIL | PASS |

Pre-fix binaries: `test_frame_component` 4 of 5 T368 cases FAILED,
`test_static_ui_panels` 5 of 5 FAILED, `test_input_action_bridge` 1 FAILED
("4 consumer-less UI toggle(s) are enumerators … again (first: ToggleVendor)").
Post-fix: 5/5, 5/5 and 41/41.

`T368_TheThreeTogglesDoNotCloseTheCentrePopups` is in the table with `pass` in
both columns on purpose: it documents a non-regression (the HUD toggles must not
inherit `toggleSkillsWindow()`'s mutual exclusion), so it could never have
failed pre-fix. Called out rather than smuggled in.

### Deviation

**The plan's Leg B instruction to DELETE
`UIToggleActions_AreNotBridgeTerritory` was wrong and was not followed.** "With
all seven gone the case has no subject" is true only of the UI half of its set;
the set also held `InputAction::ToggleWireframe` and `ToggleHUD`, which are
REAL, with a real consumer that is simply not this bridge —
`GraphicalMode::…` drains them with `consumeAction()`
(`src/client/mode/graphical_mode.cpp:138,141`). The assertion "the bridge leaves
these pending rather than consuming and dropping them" therefore has a stronger
subject after the deletion than before it. The case was **restated** over four
surviving graphics toggles (`ToggleWireframe`, `ToggleHUD`, `ToggleNameTags`,
`ToggleZoneLights`) and renamed `GraphicsToggleActions_AreNotBridgeTerritory`.

### Verification bar

| Step | Result |
|---|---|
| `cmake --build build -j24` | **exit 0**. The FIRST attempt hit **exit 143** at the 600 s ceiling, mid-`ar` on `libwilleq_core.a`; post-143 sweep ran (a 3.2 GB `build/lib/stsa7zxn` ar temp deleted — the volume is at 93%; both archives re-checked with `ar t`, 322 and 15 members; all 270 `build/bin` binaries checked for the `7f454c46` ELF magic, bad=0), the suspect archive deleted and rebuilt from scratch. |
| `ctest --test-dir build -j6` | **3940 / 3941**, ctest exit 8 — sole failure `AudioManagerTest.PreloadCommonSounds` (the waived ALSA host quirk). No load artefacts, no re-runs needed. |
| `scripts/build-gles2-x86.sh` | exit 0 |
| `scripts/ci/visual_gate.sh` | **PASS** (standalone; qeynos2 p95 4.901 / ceiling 10.108, gfaydark 6.439 / 10.672, befallen 10.898 / 10.948) |
| goldens | untouched — `git status tests/goldens/` empty. G05 pins `renderer_frame_render.cpp`, which this unit does not touch. |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | 51 checked, **51 ok, 0 warn, 0 fail** (largest touched: `static_panels.cpp` 959, `renderer_frame_input.cpp` 879 — was 859) |

### Surprises

1. **The exe records the action NAMES and nothing else.** A 247-entry name table
   is right there and the default keys are not beside it — they are assigned in
   code. The oracle answered a question the plan did not ask (default OPEN state,
   from `defaults.ini`'s `Show=` convention) and could not answer the one it did.
2. **`defaults.ini` states a default by OMISSION.** `Show=0` appears under three
   sections and nowhere else; every other window is shown by not saying so. A
   grep for `Show=1` finds nothing in that file and would have concluded the
   opposite.
3. `PetRemoved`'s `= {}` would have eaten the new flag. A wholesale
   value-reset is a trap for any field added to a state struct later.
4. The plan's Leg B claim that the test case would lose its subject was checked
   and was false — two of its four entries were live all along.
