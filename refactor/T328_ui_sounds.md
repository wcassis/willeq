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

---

# Review — Leg A (slot1, `task/T328-legA-ui-sounds`, 2026-09-03/04)

**Leg A changed no production code.** Deliverables:
`/mnt/projects/fable/review/T328/` (the table, the trace, the per-event audio
excerpts, the tone map, the driver/analysis scripts, screenshots, and a README
that maps every file), plus this section.

Oracle: the official Titanium client under Wine/Xvfb per H05/T298/T311, account
`willeq_test`. **Deviation:** the dispatch named `Testchar`; `enter-world.sh`
selects character slot 1, which is **Testcaster** (level 25 Wizard, GM 200).
Testcaster was used throughout and its state was restored (see below). Zones:
Befallen, then North Qeynos.

## The table

`ID` = `SoundAssets.txt` id. Every row is from a capture window containing
exactly one non-ambient sound, with an independent duration check.

| event | .WAV the official client plays | ID | in `ui_sounds.h` today |
|---|---|---|---|
| bag open (right-click container in inventory) | **`itemclth.WAV`** | 145 | **no** |
| bag close (container "Done" button) | **`itemclth.WAV`** | 145 | **no** |
| bag close (container "X" button) | **`itemclth.WAV`** | 145 | **no** |
| tradeskill container open | **`itemclth.WAV`** | 145 | **no** |
| tradeskill container close | **`itemclth.WAV`** | 145 | **no** |
| item dropped into an inventory or bag slot | **`wearclth.WAV`** | 146 | **no** |
| item picked up from a slot onto the cursor | **silent** | — | — |
| coin moved from a money slot onto the cursor (1 pp) | **`BuyItem.WAV`** | 138 | yes, `BUY_ITEM` |
| coin returned from cursor to a money slot | **silent** | — | — |
| inventory window open | **silent** | — | — |
| inventory window close | **silent** | — | — |
| UI button click (6 distinct buttons) | **silent** | — | — |
| (control) drinking | `Drink.WAV` | 149 | yes, `DRINK` |

Ambient beds measured incidentally, because they matter to anyone repeating
this: Befallen = `caveloop.wav` (continuous, and loud enough to defeat plain
cross-correlation); North Qeynos daytime = **no steady ambient at all**; North
Qeynos at nightfall = `night.wav`, which starts on its own.

## Measured silent — recorded, not guessed

- **Inventory open and inventory close.**
- **Every plain UI button click.** Six were tested: "Close" on Tip of the Day,
  "Close" on the Inventory Tip window (x2), "Accept" on the Quantity dialog for
  an *item* stack, "Close" on the Tradeskill Window, plus the container window's
  "Done" and "X" — the last two do make a sound, but it is `itemclth.WAV`, i.e.
  the bag closing, since the same button shape is silent everywhere else.
- **Picking an item up** onto the cursor (only the drop makes a sound).
- **Returning coin** from the cursor to a money slot.

## Three findings that contradict `ui_sounds.h` as written

1. **`BUTTON_CLICK` = `Button_1.WAV` (142) is never played.** The client
   *preloads* it and no button tested plays it. Wiring it in Leg B would invent
   a sound the oracle does not make.
2. **`CHEST_OPEN`/`CHEST_CLOSE` (`Chest_Op`/`Chest_Cl`, 134/133) are not the bag
   sounds.** Containers use `itemclth.WAV`. `Chest_*` is presumably world
   chests/doors; that was not measured.
3. **`BuyItem.WAV` (138) is the generic COIN-MOVE sound, not a "purchase"
   sound.** It fired when 1 pp moved from the money slot to the cursor, with no
   merchant anywhere. The same Quantity-dialog "Accept" for an *item* stack was
   silent — so it is keyed to coin, not to the dialog.

**Two IDs Leg B must add** (both absent from `ui_sounds.h`): `itemclth.WAV` = 145
(container open/close) and `wearclth.WAV` = 146 (item into slot).

## Not measured (five rows), and exactly why

The blocker was in-world interaction under Xvfb, never the sound method.

- **Coin pickup by denomination (pp/gp/sp/cp).** Also a finding in its own right:
  **the Titanium client will not drop coin on the ground** — with 1 pp on the
  cursor, clicking open terrain twice left it on the cursor and spawned nothing.
  The corpse route was then tried: `a_dusty_box` (npctype 238320, cash loot
  table) spawned, reached with `#goto`, killed three times — but the corpse lands
  at the player's own feet and the third-person camera could not be positioned to
  right-click it (walking backwards drives the camera inside the player model).
- **Buy / sell.** The merchant window opens only by right-clicking the NPC in 3D.
  `Nerissa_Clothspinner` (2036) was spawned, reached and correctly targeted, but
  `#goto` puts the player *inside* the NPC, so her model is occluded by the
  player's own and the right-click never landed on her.
- **Combine success / failure.** The Tradeskill Window opened fine on a summoned
  Mixing Bowl, but the component would not stay in a container slot (EQ moves
  items click-pickup/click-drop, not by dragging, and the drop into the
  tradeskill grid did not take). **No combine was ever submitted, so no combine
  sound is claimed.** Note also that the test character has **Baking 300**, so a
  *failed* combine needs a recipe with trivial > 300, not merely a `nofail=0` one.

None of these hit a plan STOP condition. Closing them needs a reliable
camera/interaction helper for the oracle client; the expensive parts — the
real-time capture rig and the tone probe — are built, proven and checked in
under `review/T328/scripts/`.

## Method, and why the plan's first option does not work

**`strace -e openat` cannot attribute a sound to an event, and this is now
measured.** The client loads sounds as *loose files* from `<client>/sounds/*.wav`
by name (so a trace does name real files), but it **preloads the whole UI bank in
one contiguous burst** in `SoundAssets.txt` id order — IDs 116..149, `bowdraw`
through `drink`, each opened exactly once — and opens nothing when a sound is
subsequently played. Evidence: `review/T328/opens_run1_stock_client.log`.
This is the fallback condition the plan anticipated.

Plain audio capture plus cross-correlation was then tried and **also measured
insufficient**: Befallen's `caveloop.wav` ambient sits at the same level as the
UI one-shots and no UI sound ranked above the ambient.

So the identification was made exact by **tagging every asset with a unique
signature**:

- A **separate probe copy** of the client (`~/eq-oracle/client-t328`): every
  entry symlinked from the real client except `sounds/`, which is a real
  directory of 2604 generated `.wav`s — one per asset name in the union of the
  install's loose `sounds/` and all 17 `snd*.pfs` archives.
- Each generated file keeps the original's **rate, sample width, channel count
  and exact frame count**, and carries a **unique 3-tone chord** from a 60 Hz grid
  spanning 300–3900 Hz (below the Nyquist of the lowest-rate asset, 8 kHz).
  **2604 files, 2604 chords, zero collisions.**
- Real-time capture through the ALSA loopback (`snd-aloop`): `~/.asoundrc` routed
  the client to `hw:1,0,0`, `ffmpeg` recorded `hw:1,1,0`. Bit-exact, verified
  against a reference tone. (The host has no sound card, no PulseAudio and no
  `alsa-utils`; a `pcm.file` sink was tried first and rejected — with no timing
  source it re-writes the same buffer and drops transients.)
- Every action is bracketed by capture byte offsets and wall clock
  (`T328_events.tsv`), so each sound belongs to exactly one action.
- `chord.py` short-time-FFTs the window, drops the zone's steady ambient tones,
  and reports whose chord the rest spells — with the **measured on-air duration**
  checked against the asset's real duration as an independent second factor.

**A probe is not a patch.** Only asset *data* in a directory we created was
generated; the client binary and every decision it makes are untouched, and
`~/eq-oracle/client` was never modified. The probe directory is deleted.

**Three independent corroborations** that the identification is real:
(1) the ambient resolved to `caveloop.wav` in Befallen and `night.wav` at
nightfall — correct for those zones and not anticipated by the analysis;
(2) the first identified event, `drink.wav`, is confirmed by the game's own chat
log ("You take a drink from an Skin of Milk"), which the audio pipeline knows
nothing about; (3) 11025 Hz assets show their upsampling aliases at `11025 - f`,
confirming the sample rate of the file that played matches the asset named.

**Known confound:** two simultaneous sounds contribute six tones, whose subsets
can spell a third file's chord. Visible on the NPC-death windows in
`final_analysis.txt` (`death_m` + `drown_m` are the minimal explanation;
`drip_dry05`/`ddm_spl` are artefacts). Every table row above comes from a window
with exactly one non-ambient chord.

## Server and host state

- eqemu: `up.sh qeynos2`, noclobber lock taken and deleted, `down.sh` at the end.
- **Testcaster (id 67) restored**: `zone_id=36, x=-259, y=-1201, z=-5.75, h=0`;
  currency `0/0/0/0`; the three summoned items (17005, 17906, 58154) deleted;
  the Skin of Milk returned to a general slot with charges restored to 18 (one
  charge was consumed by the incidental drink). **Deviation:** its original
  general slot was not recorded before the session, so it was returned to slot 23.
- `~/.asoundrc` deleted, `snd-aloop` unloaded, `~/eq-oracle/client-t328` deleted.

## Leg B — wire what Leg A measured (plan, 2026-09-04)

Runs in slot2 alongside L05 (renderer; disjoint territory). Source of
truth: `review/T328/README.md` and `opens.log` (the official client's
preload order is the authoritative ID → `.WAV` map).

### Steps

1. **Reconcile the catalogues.** `include/client/audio/ui_sounds.h` and
   `include/client/audio/sound_assets.h` disagree with each other and with
   the measurement (`sound_assets.h` says `BUY_ITEM = 145`; the official
   client's preload order says 145 = `itemclth.WAV`, 138 = `BuyItem.WAV`).
   Rebuild the UI-bank ID table (116–149) from `opens.log` verbatim, in ONE
   place; the other header includes or aliases it. Add `ITEM_CLOTH = 145`
   (`itemclth.WAV`) and `WEAR_CLOTH = 146` (`wearclth.WAV`). Correct
   `BUY_ITEM`'s comment: it is the generic coin-move sound. Mark
   `BUTTON_CLICK` as "preloaded, never played by the official client — do
   not wire" (measured). `test_ui_sounds.cpp` and any test asserting the
   old IDs are updated to the measured values (assertions change ONLY where
   the old value was wrong against the oracle — cite the row).
2. **Wire, through `Bridge().PlaySound(id)`** (the path movement already
   uses for `WaterIn`):
   - bag open and bag close → `ITEM_CLOTH`. The game-side event is the
     inventory container open/close (find the intent/handler in
     `InventoryManager` / `bridge_component_intents.cpp` — `OpenContainer`
     / `SendCloseContainer` shape); tradeskill container open/close → the
     same sound.
   - item dropped into an inventory or bag slot → `WEAR_CLOTH` (the
     `SendMoveItem`-completed / `ZoneProcessMoveItem` path — play on the
     LOCAL drop, not the server echo, to match the oracle's timing; record
     which you chose and why).
   - coin moved from the money slot to the cursor → `BUY_ITEM` (the
     `RequestBankCoinMove` / coin-to-cursor intent).
   - measured SILENT, so NOT wired: item pickup to cursor, coin returned to
     slot, inventory open/close, all button clicks.
3. Tests: one per wired event asserting the sound ID is requested (read
   how `test_water_sounds`/`player_sounds.h` seam the audio manager;
   `Bridge().PlaySound` may need a recording seam — add the smallest one).
4. The five unmeasured rows (coin pickup pp/gp/sp/cp from a corpse, buy,
   sell, combine success/fail) stay on B036 as "unmeasured — needs a
   corpse-loot or merchant session" with Leg A's blockers quoted; do NOT
   guess a sound for them.
5. **T329's owed live leg** (the lock is free now): one `fable` session in
   a navmesh-enabled zone, Ctrl+N, screenshot to `review/T329/` with a
   README — protocol eqemu rules, force daylight, `--eqe1-path`. Then
   strike the "owed" note on B041.
6. BOARD: update B036 with what is wired and what remains.

### Territory

`include/client/audio/ui_sounds.h`, `include/client/audio/sound_assets.h`,
the inventory/bridge/merchant call sites the events live in (call-site
additions only), `tests/test_ui_sounds.cpp` + new tests + cmake,
`refactor/BOARD.md`, `review/T329/`. Nothing under `src/client/graphics`
(L05 owns it).
