# B582 (GitLab #49, + B561 / GitLab #42) — adopt the official keymap for F10 / F11 / L; the F10/F11 volume bindings go

Class A, RULED by Will 2026-09-18 (`docs/decisions.md` 2026-09-18, item 2:
"adopt the official keymap. F10, F11 and L mean what the official keys mean").
Applied 2026-10-10 on `task/kb-b582-official-keymap`.

## What changed

Measured on the oracle's Options > Keys (T466): **F10 = Toggle UI Display,
F11 = Toggle netstat, L = Toggle Leadership Window, Alt+C = Toggle Combat
Ability Window** (Alt+C already matched). Applied exactly:

| key | before | after |
|---|---|---|
| F10 | `MusicVolumeDown` (Player) | `ToggleAllUI` (Global) — the official "Toggle UI Display" |
| F11 | `ToggleAllUI` (Global) AND `MusicVolumeUp` (Player) — the B561 double binding | UNBOUND — fable has no netstat display, and this card does not build one. A future netstat owns F11. |
| L | `CycleObjectLights` (Player) | UNBOUND — the official Leadership window is post-Velious (LoY/GoD), which Will's feature-gap ruling IGNORES. `CycleObjectLights` keeps its consumer (`DiagnosticsComponent::cycleObjectLights()`, Options > Keys Debug page) but no key; `/olight` is the on/off toggle (`renderer_slash_commands.cpp:240`), not the cycle, so no slash alias was invented. |
| Shift+F10 | `EffectsVolumeDown` | REMOVED — no volume key on the Camera, UI or Commands pages (T466) |
| Shift+F11 | `EffectsVolumeUp` | REMOVED — same |

`MusicVolumeUp`/`MusicVolumeDown`/`EffectsVolumeUp`/`EffectsVolumeDown` keep
their `RendererEventReceiver` arms and Options-slider routes; the volume
intents and `/musicvolume` /`effectsvolume` are the routes (CLAUDE.md "Volumes
round-trip").

B652 (#54, the zoom keys) is NOT this card — no camera binding was touched.

Files: `src/client/input/hotkey_manager.cpp` (setupDefaults), `config/hotkeys.json`,
`tests/test_hotkey_manager.cpp` (B561 exemption deleted; `B582_TheOfficialKeymapIsBound`
+ `B582_APreB582UserFileStillLoads` added), `tests/test_hotkey_dispatch_census.cpp`
(exemption deleted), `tests/test_ui_hide_command.cpp` (F11 → F10), plus doc/comment
updates naming the old meaning (`renderer_command_catalog.cpp`, the `/hideui`
comment in `renderer_slash_commands.cpp`, `renderer_frame_render.cpp`,
`diagnostics_component.h`, `src/client/input/CLAUDE.md`).

## RED — the updated tests fail on the old code [measured]

Baseline tree: fresh worktree of `origin/fable` (16c9cf44) with ONLY the three
updated test files copied in, everything else the old code. Local build
(`-j3`, Release, GLES2 tree), 16:0x 2026-10-10.

`test_hotkey_manager` (63 tests, 2 FAILED):

```
[  FAILED  ] HotkeyShippedFileTest.T464_TheShippedFileCarriesEveryDefaultBinding (0 ms)
[  FAILED  ] HotkeyShippedFileTest.B582_TheOfficialKeymapIsBound (0 ms)
[  PASSED  ] 61 tests.
```

`B582_TheOfficialKeymapIsBound` failures (abridged):

```
test_hotkey_manager.cpp:1194: Failure
Expected equality of these values:
  mgr.getAction(irr::KEY_F10, false, false, false, mode)
    Which is: (nullopt)
  std::optional<HotkeyAction>(HotkeyAction::ToggleAllUI)
config/hotkeys.json: F10 is not ToggleAllUI in Player mode
config/hotkeys.json: F11 must be unbound (the official netstat toggle; fable has no netstat)
defaults: L must be unbound (the official Leadership window is post-Velious)
defaults: MusicVolumeDown is bound -- the official keymap has no volume key
config/hotkeys.json: EffectsVolumeUp is bound -- the official keymap has no volume key
```

`test_hotkey_dispatch_census` (1 FAILED):

```
[  FAILED  ] HotkeyDispatchCensus.EveryDefaultBindingHasADispatchConsumer
```

(fails on the old `MusicVolumeUp = F11` default the deleted exemption used to
carry)

`test_ui_hide_command` (1 FAILED):

```
[  FAILED  ] UIHideCommandTest.F10IsBoundToToggleAllUIInEveryMode
```

Full outputs: `/home/user/kb-evidence/b582-official-keymap/red_*.txt`.

## GREEN — same binaries rebuilt on the fix [measured]

```
test_hotkey_manager          [  PASSED  ] 63 tests.
test_hotkey_dispatch_census  [  PASSED  ] 4 tests.
test_ui_hide_command         [  PASSED  ] 7 tests.  (F10IsBoundToToggleAllUIInEveryMode OK)
```

Outputs: `/home/user/kb-evidence/b582-official-keymap/green_*.txt`.

The full suite (and the visual gate) run on the GitLab runner for the push; a
local run of the untouched suites would only re-prove what CI re-proves.

## Not verified / left open

- ~~`include/client/graphics/ui/CLAUDE.md` (the key-bindings table) and
  `src/client/input/CLAUDE.md` (the B561-exemption sentence) still named the
  pre-B582 keys: the CLAUDE.md write is gated on an interactive approval
  prompt that times out in a headless single-query run.~~ RESOLVED in follow-up
  t_d85121df (commit 90363a63, MR !125 pipeline 748 green): the Global row is
  now F10 (with the move note), the L and volume rows are replaced by one row
  documenting the removal, the two inline mentions and the B561-exemption
  sentence are re-pointed to the B582 outcome, and `ctest -L hygiene`
  (DocCounts/DocClaims/DocCitations/CommentClaims) is green.
- The Options Keys UI page (`options_keys.cpp`) still lists the four volume
  actions under its UI category: a user MAY bind volume keys in a user copy
  (pinned by `B582_APreB582UserFileStillLoads`), and removing them from the
  page would delete live actions from the rebinding surface — that is a
  separate decision, not made here. A saved pre-B582 user copy therefore keeps
  its old volume/F10/F11/L keys until the user hits "Reload defaults" — no
  migration is applied and there was no user base to migrate (T464's shipped
  file was the only table; `loadFromFile()` replaces, never merges).
- Live proof (pressing F10/F11/L in a running zone) was not done: the lane
  rig was not used for a keypress; the hotkey tables are what the receiver
  consults, and both are pinned above.
