# T328 — UI sounds: measure the official client, then wire (B036, Will 2026-09-03)

Will's ruling: UI sounds are wanted — bag open/close, coin pickup by
denomination (pp/gp/sp/cp), possibly tradeskill combine, "there may be
other UI sounds". This is a FEATURE unit with a measurement leg first
(`match-the-oracle`): we do not guess which `.WAV` the Titanium client
plays for each event.

## What exists (measured at `d6cbf730`)

`include/client/audio/ui_sounds.h` (123 lines) already catalogues UI sound
IDs by name — `BUY_ITEM` 138, `BUTTON_CLICK` 142, `CHEST_OPEN` 134 /
`CHEST_CLOSE` 133, `LEVEL_UP` 139, `TELEPORT` 137, `DRINK` 149, the door
set, … — with `getSoundFile`/`getSoundId` helpers and `tests/test_ui_sounds.cpp`.
**Its only non-audio includer is `bridge_component.h`; no production code
plays any of them** — the T48/T264 reachability pattern, one more instance.
No coin sound is catalogued. The sound archives are at
`/mnt/projects/EverQuestP1999/*_sounds.eff` / `*_sndbnk.eff` plus the
global `snd*.pfs` (find them; `eff_loader.h` reads the per-zone ones).

## Leg A — measure (eqemu + official client under Wine/Xvfb, per `eq-oracle-provisioning`)

1. Take the eqemu lock (noclobber). Start the official client with audio
   capture (the Wine prefix's ALSA/Pulse output → a WAV, or the client's
   own sound log if it has one — check what H05 set up).
2. Drive, one at a time, with a screen-recorded timestamp for each: open a
   bag, close a bag, pick up 1 pp, 1 gp, 1 sp, 1 cp from the ground or a
   corpse, a successful tradeskill combine, a failed combine, buy an item,
   sell an item, click a UI button, open/close inventory. Log the
   wall-clock of each action.
3. Match each captured sound to a file in the global sound archive by
   waveform (a short `python` cross-correlation against every `.WAV` in
   `snd*.pfs`; the archive has a few hundred files — feasible) or by the
   client's file-access trace (`strace -e openat` on the Wine process is
   simpler and exact: which `.WAV` was opened at which timestamp).
4. Deliverable: a table event → `.WAV` file → existing `ui_sounds.h` ID or
   "not catalogued", with the trace evidence in `review/T328/`.

## Leg B — wire (after Leg A merges)

For each measured event: the game-side publisher
(`InventoryManager` bag open/close intent; `MerchantComponent::ZoneProcessMoneyUpdate`
deltas per denomination; the combine reply handler; buy/sell replies;
button clicks in `static_ui_input.cpp`) calls
`ctx_.audio_manager->playSound(<id>)` through `Bridge().PlaySound()` — the
one path movement already uses for `WaterIn`. Add the missing IDs to
`ui_sounds.h` with the measured filenames. One test per event asserting the
sound ID is requested (the audio manager already has a test seam — read
`test_ui_sounds.cpp` and `player_sounds.h`'s pattern).

## STOP

- The official client plays NO sound for an event → record it, do not
  invent one (fable stays silent there too).
- The archive lacks a file the trace names → record it; Will decides.
