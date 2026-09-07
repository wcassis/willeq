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
