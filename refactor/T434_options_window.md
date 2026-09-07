# T434 — the options window from the official XML (the `O` key reaches a `break;`)

Class B (feature, era-appropriate — the official client's Options window
is the home of every user-changeable setting), slot1, no server. Read
T432's §Review (fable has NO options window: `O` reaches `break;`,
`PopupType::Options` has zero uses; the oracle's per-character ini keys —
`BGTint`, `Alpha`, `Fades`, `Locked`, `UISkin`, camera distance — live
under `[Options]`), T368's §Review (Options > Keys screenshots under
`review/T377/oracle/`), `/mnt/projects/EverQuestP1999/uifiles/default/EQUI_OptionsWindow.xml`
(the tabs: General, Display, Mouse, Keys, Chat, Colors…), the T413/T415
XML-placement method, T432's `ui_preferences` (one writer), and the
static-panel seam.

1. Draw the window (tabs and their controls at the XML placements; a
   tab strip the player can click; sliders/checkboxes as the XML pieces).
   Wire ONLY controls whose setting exists in fable today: the camera
   distance (T57's camera command), the window toggles (T368), sound
   volume (AudioManager master/effects — read what exists), the
   render-distance slider (`display_settings.json`, T414 wired it),
   mouse look sensitivity if a knob exists. Controls with no backing
   setting are drawn disabled (greyed, per the XML's disabled piece) and
   boarded per control (B430+) so nothing advertises what does not
   exist (the T363 rule).
2. Every wired control persists through T432's `ui_preferences` under
   the oracle's own `[Options]` key names (quote them from a real
   `eqclient-<char>.ini`/`UI_*.ini` under `/mnt/projects/EverQuestP1999/`).
3. The `O` key opens/closes it (T363's toggle shape; the keys page from
   T377 shows the official binding — quote it).
4. Tests pre-fix failing: `O` opens the window; each wired control moves
   its setting and persists; disabled controls are not hit-testable.
   `ui_panel_capture` gains the window (each tab) into `review/T434/`.

## Proof

Build (background), `ctest -j6` (expected reds only while T430 (slot2)
has the pack replaced: `PackReproducible`/`eqe1MappedBytes`),
pre-fix/post-fix, G05 in place if a pass is added, visual gate standalone
+ ARM (no image golden should move), file sizes.

STOP: none expected.

## Review

Branch `task/T434-options-window`, base `bd0f1970` (verified at checkout).
One commit. No server — T433 holds the eqemu lock (B306), and nothing here
needed one: the whole oracle is files on disk plus T377's captures. slot1's
build cache already pointed at its own `third_party/irrlicht-gles2-x86` and
`CMAKE_BUILD_TYPE=RelWithDebInfo` (both checked at checkout).

### The two measured findings, before anything else

**1. The key is `Alt+O`, not `O`.**
`review/T377/oracle/T377_oracle_options_keys_UI_page2.png` — the official
client's Options > Keys > UI page — carries the row

```
Toggle options window      Alt O      -------
```

Keypress `Alt O`, Alternate column empty. fable bound a bare `O`
(`hotkey_manager.cpp`, `config/hotkeys.json`), which is an invention of
exactly the class T382 removed when it took `G` off the group window and `P`
off the pet window. Rebound; `O` is now unbound, as it is in the oracle. The
same screenshot set also carries the window itself
(`T377_oracle_options_alt_o_1024x768.png`), which is the General page and was
used to check the placements below.

**2. The oracle's sliders are integers 0..20, displayed as value x 5 %.**
This was not in the plan and settles how the three wired controls persist.
`/mnt/projects/EverQuestP1999/eqclient.ini` records `Music=18`,
`SoundVolume=10`, `Realism=1`, `Gamma=11`, `ClipPlane=20` — and the live
capture of *that same client* shows Music Volume **90 %**, Sound Volume
**50 %**, Sound Realism **5 %**. 18/20, 10/20 and 1/20. So the storage is a
21-position integer and the readout is `value * 5` with a `%` suffix. fable
uses the same 21 positions, the same readout and the same key names, so a
fable file and an official one mean the same thing by the same number.
Pinned by `OptionsWindowT434.TheReadoutIsTheOraclesValueTimesFive`.

### Placement — the oracle's, unscaled, and why that is not a shortcut

`<Screen item="OptionsWindow">` is `<Size>400x452</Size>`, `Style_Sizable
true`. That flag is the condition under which T415 mapped the tradeskill
window proportionally onto `centerPopup`, so the default reading would be to
scale. It does not apply here, and the reason is measured rather than argued:
`/mnt/projects/EverQuestP1999/defaults.ini` records `[OptionsWindow]`
`Width<res>=400` / `Height<res>=452` at every resolution it lists, and the
real player file `UI_Obian_project1999.ini` carries **both**
`Width1280x1024=400 Height1280x1024=452` **and** `Width1600x900=400
Height1600x900=452`. The shipped size never changes and a real player never
resized it, so the window is drawn at 400x452 with every interior rect at the
oracle's own offset and nothing can round.

The bands are DERIVED, not chosen. Every `<Page>` is
`<Location>(0,22)</Location>` `<Size>387x402</Size>`, so 452 splits as
**28 + 22 + 402**: 28 is the titlebar band, 22 the tab strip, 402 the page.
387 in a 400 window leaves 13px of border, 6 left / 7 right. Pinned by
`OptionsLayoutT434.TheBandsAddUpToTheOraclesHeight`.

**Two deviations, flagged rather than absorbed:**

1. **The window is CENTRED**, not placed at the oracle's absolute
   `<Location>(90,47)</Location>`. fable's top-left is the player status
   panel. Same deviation T415 flagged for the combat-abilities anchor; the
   SIZE and every interior rect are the oracle's, only the screen anchor is
   fable's.
2. **The seven tabs split the window width in an exact integer partition.**
   The oracle's `TabBox` sizes each tab to its `TabText`'s rendered width —
   visibly, in the T377 capture, "General" is wider than "Keys". A layout
   header is device-free and has no font metrics, so it cannot reproduce
   that.

The window is **not** a centre popup. `EQUI_OptionsWindow.xml` is
`RelativePosition false` at its own `<Location>`, so it is drawn and
hit-tested LAST — after even T421's item display window — and ESC dismisses
it first of all. Opening it closes nothing. `PopupType::Options` therefore
stayed unused and is boarded as **B434** rather than pressed into a role the
oracle does not give it.

### The control table, tab by tab

Every control the XML declares is in one `constexpr` table
(`include/client/graphics/ui/options_layout.h`), each row carrying the XML
`item` name and the XML's own `<Location>`/`<Size>` — or, where the XML
anchors instead, the derived absolute rect with the anchor arithmetic written
out beside it. The draw loop and both hit tests all go through
`controlRect()`, so render and hit-test cannot drift (T57's rule).
**114 controls; 6 rows are wired** (three sliders plus the three value labels
that read them back), and every other row is `Setting::None`.

| tab (XML page) | controls | wired | disabled → boarded |
|---|---|---|---|
| General (`OptionsGeneralPage`) | 39 | `OGP_MusicVolumeSlider` + `OGP_MusicVolumeValueLabel` → AudioManager music volume; `OGP_SoundVolumeSlider` + `OGP_SoundVolumeValueLabel` → AudioManager effects volume | the other 35 → **B430** |
| Display (`OptionsDisplayPage`) | 47 | `ODP_ClipPlaneSlider` + `ODP_ClipPlaneValueLabel` → `IrrlichtRenderer::setRenderDistance()` | the other 45 → **B431** |
| Mouse (`OptionsMousePage`) | 8 | none | all 8 → **B433** |
| Keys (`OptionsKeyboardPage`) | 4 | none | all 4 → **B433** |
| Filters (`OptionsChatPage`) | 2 | none | both → **B433** |
| Colors (`OptionsColorPage`) | 3 | none | all 3 → **B433** |
| Mail (`OptionsMailPage`) | 11 | none | all 11 → **B433** |

Per-control detail for the wired six and the near misses:

| XML element | XML Location / Size (or the anchor arithmetic) | wired to |
|---|---|---|
| `<Slider item="OGP_MusicVolumeSlider">` | (200,70) 100x16 | `MusicVolumeChangeIntent`, delta against the last applied step |
| `<Label item="OGP_MusicVolumeValueLabel">` | (300,70) 50x16 | reads it back as `step * 5 %` |
| `<Slider item="OGP_SoundVolumeSlider">` | (200,110) 100x16 | `EffectsVolumeChangeIntent`, same shape |
| `<Label item="OGP_SoundVolumeValueLabel">` | (300,110) 50x16 | reads it back |
| `<Slider item="ODP_ClipPlaneSlider">` | (200,129) 100x16 | `owner_.setRenderDistance()` — the setting `/clipplane` already drives |
| `<Label item="ODP_ClipPlaneValueLabel">` | (300,129) 50x16 | reads it back |
| `<Button item="ODP_PCNamesCheckbox">` | (10,12) 150x18 | **disabled** — fable has ONE nametag toggle (Ctrl+F3), not the oracle's separate PC/NPC flags, so wiring either would silently move the other. B431 |
| `<Slider item="OMP_MouseSensitivitySlider">` | (200,30) 100x16 | **disabled** — mouse-look multiplies by hardcoded `0.5f`/`0.3f` in `renderer_player_physics.cpp`; there is no knob. B433 |
| `<Button item="OGP_CombatMusicCheckbox">` | (200,155) 150x20 | **disabled** — `CombatMusicManager::setEnabled()` exists but has no caller and no intent. B430 |
| `<Listbox item="OKP_KeyboardAssignmentList">` | anchored: Top=60, Left=5, Right=5(from right), Bottom=5(from bottom) → (5,60) 377x337; columns Command 180 / Keypress 75 / Alternate 75 | **disabled** — the cheapest of the five inert pages to close, read-only, from `HotkeyManager`. B433 |
| `<Listbox item="OFP_FilterList">` | Top=10, Left=40, Right=40, Bottom=40 → (40,10) 307x352 | **disabled** — no chat filter model. B433 |
| `<Combobox item="OFP_FilterComboBox">` | `TopAnchorToTop=false`, so Top=34 is from the BOTTOM → y=368; Bottom=10 from the bottom → y=392; (60,368) 267x24 | **disabled**. B433 |
| `<Button item="OCP_UserColorChange">` | both anchors from the bottom: Top=30 → y=372, Bottom=8 → y=394; (126,372) 159x22 | **disabled** — no user colour table (T432 ruled UI colours out of scope). B433 |
| `<Button item="OMP_MailForwardingCheckbox">` | (10,80) 16x16, caption in the separate `OMP_MailForwardingLabel` at (30,80) | **disabled** — no mail feature, and arguably post-Velious. B433 |

Two content notes on the Display page. `ODP_SetFullscreenButton` and
`ODP_SetWindowedButton` occupy the SAME rect (10,106) in the XML — the
official client shows only the applicable one — so only the windowed one is
drawn rather than two frames stacked. And the nested
`<TabBox item="ODP_ParticlesTabs">` (190,174) 190x170 is drawn as a frame
with its three sub-tab captions plus the Spell page's nine rows at
(190,196)+offset; the sub-tabs are **not** clickable, because all nine
controls behind them are disabled anyway. Both flagged in B431.

### The T363 rule, made mechanical

`hitTestOptionsControl()` skips any control whose `Setting` is `None`, so a
greyed control cannot be clicked into doing anything — it is not a matter of
the click handler remembering to check. The window as a whole still consumes
the position, so a click on a greyed control does not fall through to the 3D
scene. Pinned by `OptionsWindowT434.ADisabledControlIsNotHitTestable`, which
walks **every** disabled control on every tab, and by
`AnOpenWindowConsumesEveryClickInsideIt`.

### Persistence — through T432's ONE writer

Four new fields on `UiPreferences`, and the same anti-drift device T432
built: `kWindows` gained `{"OptionsWindow", &UiPreferences::optionsWindow}`,
and a second member-pointer table `kOptions` carries the three integers, both
walked by the parse AND the serialise so they cannot disagree about a key
name. The file a fable client now writes:

```
[InventoryWindow]
Show=0
...
[CombatAbilityWnd]
Show=0
[OptionsWindow]
Show=0
[Options]
Music=14
SoundVolume=20
ClipPlane=5
[Camera]
Mode=1
```

**The keys are quoted from the oracle**, `/mnt/projects/EverQuestP1999/eqclient.ini`:

- `Music=18` and `SoundVolume=10` — its `[Defaults]` section
- `ClipPlane=20` — its `[Options]` section
- `[OptionsWindow] … Show=0` — `UI_Obian_project1999.ini`, which is also the
  measured default (the window ships CLOSED)

**DEVIATION, FLAGGED:** the oracle keeps the sliders in `eqclient.ini` — a
file that is per-character but not per-server — and splits them across two
sections. fable has one persistence writer (T432's rule), so they go in
`UI_<Char>_<server>.ini` in one `[Options]` section under those key names.
This is the same deviation T432 flagged for `[Camera] Mode=`; recorded so
nobody later reads the file as an oracle match.

Defaults are fable's own shipped values expressed on the oracle's scale —
music 0.7 → 14, effects 1.0 → 20, `renderDistance` 300 → 5 — and
`OptionsPreferencesT434.TheDefaultsMatchTheOptionsPanelStruct` plus
`TheDefaultsAreFablesOwnShippedValuesOnTheOracleScale` pin both halves. An
absent key keeps the default (T368's "a default is stated by omission"), and
an out-of-range or non-numeric value is refused on parse AND clamped again on
apply, so a value assembled in code cannot get through either.

### The clip-plane mapping is fable's, and is flagged

The oracle's `ClipPlane=20` is a slider POSITION; the world units it means
are not recoverable from the ini, and its value label is a `%` like the
volume ones. fable maps the 21 positions linearly onto its own
render-distance range in 50-unit steps — `step * 50 + 50`, i.e. 50..1050 —
which puts `config/display_settings.json`'s shipped `renderDistance: 300`
exactly on step 5 and keeps the whole range inside the 50..10000 band
`/renderdist` already enforces. Written out in `options_panel.h` and pinned
round-trip for all 21 steps.

### The one new seam, and why it exists

`FrameComponent::applyOptionsSetting()`
(`src/client/graphics/frame/renderer_frame_options.cpp`) is the single body
that takes a moved control to its setting, and both the click path and the
preferences-load path go through it. It exists because the three wired
controls do NOT reach their settings the same way: two go to the game thread
through `MusicVolumeChangeIntent` / `EffectsVolumeChangeIntent`, which are
the only volume intents in the tree and carry a **delta**, and the third is a
direct renderer call. A delta needs the previous value, and the value the
audio manager holds is not readable from the render thread — so the renderer
records what it last told it (`optionsAppliedMusicStep_` /
`optionsAppliedSoundStep_`) and sends the difference. `StaticUIInput`
therefore resolves a click into "this setting moved" and hands the name back;
it pushes no intent itself.

That delta shape has a consequence, boarded as **B432**: `/music 50` sets the
audio manager ABSOLUTELY and nothing publishes the result back, so the slider
would then show a stale value. Not a live defect until a player mixes the two
paths; the fixes (an absolute set-volume intent, or a volumes-changed event
at the four sites that move a volume) are named in the entry.

`processOptionsInput()` is its own entry point on `StaticUIInput`, not an
eleventh parameter on `processInput()` — the T415/T421 precedent.

### Evidence

`tests/test_options_window.cpp`, **29 cases**, device-free.

**PRE-FIX** — a probe reproducing this tip's behaviour exactly: no window
reachable (`processOptionsInput` consumes nothing, as a tip with no window
does not), a bare `O` binding, and neither `[OptionsWindow]` nor `[Options]`
persisted. **22 passed, 7 FAILED:**

- `OptionsLayoutT434.EveryTabIsReachableByAClickOnItsHeader`
- `OptionsWindowT434.AnOpenWindowConsumesEveryClickInsideIt`
- `OptionsWindowT434.DraggingASliderMovesItsSettingAndNamesIt`
- `OptionsWindowT434.TheKeyIsAltOAndBareOIsUnbound`
- `OptionsPreferencesT434.TheWindowStateAndTheThreeSlidersRoundTrip`
- `OptionsPreferencesT434.TheOfficialFilesOptionsWindowShowIsHonoured`
- `OptionsPreferencesT434.CollectAndApplyCarryTheWholeOptionsState`

The 22 that pass pre-fix describe the GEOMETRY and the scale arithmetic,
which the probe does not disturb — correctly, since the layout header is what
the probe leaves alone.

**POST-FIX: 29/29 pass.**

**G05 learned the edit IN PLACE** (batch-G correction 32; never regenerated).
The composed list was diffed against the pre-edit golden and the only hunks
are **two pure insertions** — no text change and no reorder anywhere:

- entries **264-268**, the input hop: the `OptionsLayout::Setting
  optionsChanged` declaration, the three lines of the
  `staticUIInput_->processOptionsInput(...)` call, and
  `applyOptionsSetting(optionsChanged);`
- entry **291**, the draw: `renderOptionsWindow(...)`, directly after entry
  290 `renderItemDisplayPopup(...)` and before `renderXPBar(...)`

314 → **320**. The comment block in `test_frame_render_order.cpp` gained the
paragraph naming those positions, and `kGoldenSize` follows.
`test_frame_render_order`: **4/4**.

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 tree) | **EXIT 0** |
| `ctest -j6` | **4478 / 4480** |
| `bash scripts/build-gles2-x86.sh` | **EXIT 0** |
| `scripts/ci/visual_gate.sh` standalone | **FAIL — not ours** (below) |
| `bash scripts/build-arm-noble.sh` | **EXIT 0** |
| `check_file_sizes.sh fable` | 25 files checked, **25 ok, 0 warn, 0 fail** |

**The two ctest reds are the ones the plan predicted, and both are the shared
pack.** `PackReproducible` and `visual_gate`. The gate's whole failure is

```
- memory-coverage qeynos2:  eqe1MappedBytes mismatch (expected 124063684, got 126860164)
- memory-coverage gfaydark: eqe1MappedBytes mismatch (expected 122729776, got 125526256)
- memory-coverage befallen: eqe1MappedBytes mismatch (expected 117214272, got 120010752)
```

— **+2,796,480 in all three zones, identically**, which is the T304/T355
signature of a changed pack and never of a code change. T430 installed a new
`/mnt/projects/EverQuest_ETC1/willeq.eqe1` (it added the two item-icon
sheets); this branch touches no texture, no generator and no pack input.
**No image golden moved** — the gate reported exactly those three lines and
no image comparison at all, which is what the plan asked for.

### Captures

`/mnt/projects/fable/review/T434/` — seven PNGs, one per tab, written by
`ui_panel_capture` (which gained seven entries differing only in
`activeTab`), plus a `README.md` saying what each shows, which two placement
deviations to check against `review/T377/oracle/`, and which three controls
are the enabled ones. Not self-assessed.

### Deviations, collected

1. The window is centred, not at the oracle's absolute `(90,47)`.
2. Tab widths are an exact integer partition, not caption-width.
3. The three slider values persist in fable's ONE ini rather than the
   oracle's `eqclient.ini`, under the oracle's key names.
4. The clip-plane slider's step→distance mapping is fable's, since the
   oracle's world units are not in the ini.
5. `ODP_SetFullscreenButton` is not drawn (it shares its rect with the
   windowed button, and the oracle shows one at a time).
6. The nested particles sub-tabs are drawn but not clickable.
7. **The plan named "camera distance (T57's camera command)" as a wiring
   candidate. There is no such setting** — `/camera` takes a MODE
   (Free/Follow/FirstPerson, which T432 already persists as `[Camera]
   Mode=`), and the only `cameraDistance_` in the tree belongs to the
   inventory paperdoll's `CharacterModelView`. Nothing was invented; the
   oracle's `Camera1-Distance` key has no fable counterpart.
8. The plan also named "mouse look sensitivity if a knob exists". It does
   not — the player's mouse-look multiplies by two hardcoded literals.
   Disabled and boarded (B433).

### Owed

The **live proof** — press Alt+O in a real session, drag the two volume
sliders and hear it, drag the clip plane and see the fog move, `/camp` and
log back in to find the values held. T433 held the eqemu lock for the whole
of this unit and B306 forbids opening a connection without it. Everything
below the network is proven by the 29 cases and the seven captures; what is
unproven is only the live round trip.
