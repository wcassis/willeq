# T432 — per-character persistence of what the user can change (B275)

Class B, no server. Read B275 and Will's direction (docs/decisions.md
2026-09-07): persist per character the user-changeable state — the 15
window open/closed states (`isOpen` in the static panels + the four
toggles T368/T415 added), the camera mode; keybinds stay global.
Positions are static (XML-derived) and are NOT persisted.

1. The official client writes `UI_<char>_<server>.ini` next to its
   executable, `[<Window>] Show=0/1` per window (T368 quoted
   `defaults.ini`; T382's oracle screenshots). Match the shape: fable
   writes `<cache dir>/ui/UI_<char>_<server>.ini` (T402's cache-path
   rule — outside the repo) with the same section/key names for the
   windows the oracle has, plus a `[Camera] Mode=` key; read it at
   zone-in, write it on every change and at camp.
2. Tests pre-fix failing: toggle a window, camp, log in → the state
   holds; a fresh character starts from `defaults.ini`'s rule (shown
   unless `Show=0` there — T368); the file lands in the cache dir, not
   the repo (`git status` clean).
3. Strike B275; annotate B350 if the oracle's ini carries any look
   setting fable now honours.

## Proof

Build (background), `ctest -j6`, pre-fix/post-fix, file sizes; no gate.

STOP: none expected.

---

## Review

Base `a47f31db`, branch `task/T432-per-character-persistence`. No server (T433
held the eqemu lock), no visual gate expected.

### What is persisted, and the census behind it

Will's direction said "the 15 window open/closed states". A census of every
window a user can toggle in this tree found **seven**, and the deviation is
flagged rather than absorbed:

| official section | fable state | default | reached by |
|---|---|---|---|
| `[InventoryWindow]` | `InventoryPanelState::activePopup == PopupType::Inventory` | closed | `I`, `/inventory` |
| `[SpellBookWnd]` | `SpellbookPopupState::isOpen` | closed | `Ctrl+B`, `/newbook` |
| `[SkillsWindow]` | `SkillsPopupState::isOpen` | closed | `K`, `/skills` |
| `[GroupWindow]` | `GroupPanelState::isOpen` | **open** | `Alt+P` |
| `[PetInfoWindow]` | `PetPanelState::isOpen` | **open** | `Alt+F1` |
| `[BuffWindow]` | `BuffBarState::isOpen` | **open** | `Alt+B` |
| `[CombatAbilityWnd]` | `CombatAbilityPanelState::isOpen` | closed | `Alt+C` |
| `[Camera] Mode=` | `PlayerCameraComponent::cameraMode_` | `1` (Follow) | `Ctrl+F5` |

What was deliberately left out, with the reason:

- **Loot, tradeskill container, item display** — the other three UI `isOpen`
  flags. None is a user preference: the server opens the loot window, a click
  on a container opens the tradeskill one, and an item click opens the display.
  Persisting "the loot window was open" would reopen an empty window over a
  corpse that no longer exists.
- **`allUIHidden_` (F11)** — a screenshot convenience. A client that starts
  with its entire UI hidden gives the player nothing to click and no way to
  learn why. Not persisted, on purpose.
- **`[OptionsWindow]`** — the oracle persists a `Show=` for it and fable has
  the keybind (`O` → `RA::ToggleOptions`), but the arm is `break;` with the
  comment "WindowManager removed — options window not available", and
  `PopupType::Options` has zero uses in the tree. There is no window to
  persist. (Unchanged by this unit; it is B115's territory.)
- **Chat timestamps, chat scrollback, UI colours** — named in B275, ruled out
  of scope by Will's 2026-09-07 direction, which narrowed the unit to window
  states and the camera. Still unpersisted.

Window POSITIONS are not persisted because `UILayout` computes every rect from
the resolution; key bindings are not, because they are global
(`config/hotkeys.json`).

### The file, measured against the oracle

The official client writes `UI_<Char>_<server>.ini` next to its executable and
records visibility as `Show=0`/`Show=1` at the END of that window's own
section. Verified in `/mnt/projects/EverQuestP1999/`:
`UI_Obian_project1999.ini` has `[ActionsWindow] … Show=1`, `[OptionsWindow] …
Show=0`, `[MapViewWnd] Show=0`, `[TicketWindow] Show=0`, and no `Show=` at all
under `[InventoryWindow]`, `[GroupWindow]` or `[BuffWindow]` — i.e. **a default
is stated by OMISSION** (T368's rule). fable matches the filename, the section
names and the key, and `ParseUiPreferences()` starts from the defaults and
overwrites only what the file states, so an absent key is "leave it alone", not
"false".

fable's file (a real one, written by `SaveUiPreferences` in the test):

```
; WillEQ per-character UI state. Section and key names are the
; official client's own (UI_<char>_<server>.ini); fable persists
; only the user-changeable open/closed state and camera mode.
[InventoryWindow]
Show=0
[SpellBookWnd]
Show=0
[SkillsWindow]
Show=0
[GroupWindow]
Show=0
[PetInfoWindow]
Show=1
[BuffWindow]
Show=1
[CombatAbilityWnd]
Show=0
[Camera]
Mode=2
```

It lands at `<cache dir>/ui/UI_<Char>_<server>.ini` through
`eqt::ResolveCachePathForWrite()` — T402's rule, outside the checkout, so a run
never dirties the working tree. `ResolveCachePathForWrite()` grew four lines to
create the `ui/` subdirectory: a `name` containing a slash previously came back
naming a directory that did not exist, and the caller's `open()` would fail.

### One persistence path, by construction

There is deliberately **no per-toggle save hook**. Nine toggles could each have
called `save()`; the tenth would have forgotten, and the file would silently
stop tracking one window. Instead
`FrameComponent::syncUiPreferences()` (`renderer_frame_preferences.cpp`)
COMPARES the whole collected value against the last one it wrote:

- **read** — first call for an identity: load, apply, set the baseline. A fresh
  character with no file keeps the panel structs' defaults and writes nothing
  until it changes something.
- **write** — every later call: collect, compare eight scalars, save on a
  difference. Called once per frame from `processFrame()` right after
  `processFrameInput()`, and once from `IrrlichtRenderer::shutdown()` — the
  `/camp` path, which window-close and SIGTERM also unwind through — so a
  toggle in the final frame is not lost.

The identity comes from `Application::startLoadingThread()`, the one place on
the render thread where the character name (out of the zone-load snapshot,
under `m_zoneLoadMutex`) and the server name (`m_config.server`) are both
known. `IrrlichtRenderer::setUiPreferencesIdentity()` is a pass-through so
`Application` never learns `FrameComponent` exists (Batch L's rule).

A newly persisted field therefore goes in three places that all live in one
header/TU pair — `UiPreferences`, the `kWindows` table, the binding — and the
compare picks it up everywhere. Both the write and the read walk `kWindows`, so
serialise and parse cannot drift.

### Deviations and hardening, flagged

1. **Seven windows, not fifteen** (above). The direction's count did not match
   the tree; the census is written out rather than padded.
2. **Camera mode goes in the UI ini, per the plan** — the official client keeps
   its camera keys in `eqclient-<char>.ini` (`[Options] Camera1-Distance=…`),
   a per-character but not per-server file, and records no "which camera is
   active" key at all. The plan asked for `[Camera] Mode=` in the UI file;
   that is what shipped. Noted so nobody later reads it as an oracle match.
3. **Filename parts are sanitised.** The character name arrives in a server
   packet and the server name from user config; anything outside `[A-Za-z0-9_-]`
   becomes `_` and an empty part becomes `unknown`, so neither can walk out of
   the cache directory. Pinned.
4. **`Mode=` is clamped** to 0..2 before the `CameraMode` cast, so a
   hand-edited file cannot produce an out-of-range enumerator.
5. **Apply keeps the three centre popups mutually exclusive** with a fixed
   priority (skills > spellbook > inventory), because a hand-edited file could
   name two of them open and the live toggles cannot.
6. **A failed save advances the baseline anyway.** A failure means no cache
   directory is writable at all — not a transient — and retrying it every frame
   would turn that into a 60 Hz `open()` storm.
7. **A save is temp-file-then-rename**, so an interrupted write never leaves a
   half-file for the next start to parse.

### Evidence

`tests/test_ui_preferences.cpp`, 18 cases, device-free (the disk cases point
`WILLEQ_CACHE_DIR` at a private temp directory, so they touch neither the
developer's cache nor the checkout).

PRE-FIX (a probe reproducing this tip's behaviour exactly — `LoadUiPreferences`
and `SaveUiPreferences` returning false, i.e. nothing read and nothing
written): **14 passed, 4 FAILED** —
`UiPreferencesDiskTest.AToggleSurvivesACampAndARelogin`,
`.TheFileLandsInTheCacheDirectory`, `.EachCharacterServerPairGetsItsOwnFile`,
`.ASecondSaveReplacesTheFile`. The fourteen that pass pre-fix describe the file
FORMAT, which the probe does not disturb — correctly.

POST-FIX: **18/18 pass.**

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 tree) | EXIT 0 |
| `ctest -j6` | **4426/4429**, 551 s |
| `check_file_sizes.sh fable` | 26 files checked, 26 ok, 0 warn, 0 fail |

ctest reds, all three re-run standalone:

- `PackReproducible` — **not ours, and predicted.** The rebuild matches the
  pinned hash exactly (`ac78f333…`, 6,930,116 bytes); the SHIPPED pack at
  `/mnt/projects/EverQuest_ETC1/willeq.eqe1` is a different one
  (`6549a368…`), installed by T430 in slot2 while this ran.
- `visual_gate` — same cause, same run. `eqe1MappedBytes` is over the pin by
  **+2,796,480 in all three zones, identically** (qeynos2, gfaydark,
  befallen), which is the T304/T355 signature of a changed pack, not of a code
  change. This branch touches no texture, no generator and no pack input.
- `check_build_tree_orphans` — **a stale slot artefact, now fixed.** Two
  binaries in `build/bin` (`test_caster_own_landing`,
  `test_self_relocation_heading`) were left by T426's sources, which no longer
  exist on `fable`; cmake never prunes `bin/`. Deleted them (build tree only)
  and the test PASSES. Nothing in the repo changed for this.

### Owed

The **live proof** — toggle a window, `/camp`, log back in, see it hold — is
owed to a lock unit. T433 held the eqemu lock for the whole of this unit, and
B306 forbids opening a connection without it. Everything below the network is
proven by the 18 cases; what is unproven is only that
`Application::startLoadingThread()` hands over a non-empty character name in a
real session.
