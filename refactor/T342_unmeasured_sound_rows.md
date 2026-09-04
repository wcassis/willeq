# T342 — The five unmeasured UI-sound rows (B036): coin pickup by denomination, buy, sell, combine success/fail

Measurement first (`match-the-oracle`), then the wiring. Slot3; eqemu
lock; the official client under Wine with T328 Leg A's tone-probe rig
(`review/T328/scripts/` — reusable, per its README).

## Leg A — measure (the rows T328 could not reach)

T328's blockers, and how to get past each:
- **Coin from a corpse by denomination:** the Titanium client will not
  drop coin on the ground, and an NPC corpse lands at the player's feet
  where the Xvfb third-person camera cannot right-click it. Use a GM
  route instead: `#zone`/`#goto` beside a pre-placed corpse, or give the
  character coin in the BANK and pick up 1 pp / gp / sp / cp from the
  bank window (the same drag-to-cursor gesture; if the sound is the
  generic coin-move `BuyItem.WAV` as T328 measured for the money slot,
  say so — that closes the four rows as "same sound, all denominations"
  or shows a per-denomination difference).
- **Buy / sell:** `#goto` placed the player inside the merchant; use
  `#summon`-style placement two units away or `/target` + `/say` with the
  merchant window opened via the official client's own click on a visible
  merchant (screenshot the placement). One buy, one sell.
- **Combine success / fail:** the tradeskill grid takes click-pickup /
  click-drop, not drag (T328); the test character has Baking 300, so a
  FAIL needs a trivial > 300 recipe (pick one from the eqemu
  `tradeskill_recipe` table) and a SUCCESS a trivial ≤ 300 one. Place the
  components with `#summonitem`, open the container, click each into
  the grid, press Combine.
- Table event → `.WAV` (via the tone chords), with SILENT recorded where
  measured silent. `review/T342/README.md`.

## Leg B — wire (same slot, after Leg A's table is in the plan Review)

As T328 Leg B: each measured non-silent row → `Bridge().PlaySound(id)` at
its game-side event (`ZoneProcessMoneyUpdate` deltas per denomination if
they differ; `ZoneProcessShopPlayerBuy`/`Sell` replies; the combine reply
handler), one test per row via the `PlaySoundIntent` seam, silent rows
pinned silent. Strike the rows on B036.

STOP: lock held; a gesture cannot be performed in-world after two
attempts (record which, leave that row).

---

# Review (slot3, `task/T342-unmeasured-sound-rows`, 2026-09-04)

Base `0ce3ecc3`. Both legs done in one session. Four of the five rows are
MEASURED and the wiring follows the measurement exactly; the fifth is left,
with the reason, per the plan's STOP rule.

Full write-up, evidence and screenshots: **`/mnt/projects/fable/review/T342/`**.

## Leg A — the table

Method: T328 Leg A's tone probe and ALSA-loopback capture rig, reused. The
probe builder reproduced `tonemap.json` **byte-identical** to T328's
(`md5 0dc0708b…`), so the two units' rows are the same measurement on the same
chord assignment and are directly comparable.

| # | event | .WAV | ID | trials |
|---|---|---|---|---|
| 1 | coin onto cursor — **platinum** | `BuyItem.WAV` | 138 | 1 |
| 2 | coin onto cursor — **gold** | `BuyItem.WAV` | 138 | 1 |
| 3 | coin onto cursor — **silver** | `BuyItem.WAV` | 138 | 1 |
| 4 | coin onto cursor — **copper** | `BuyItem.WAV` | 138 | 1 |
| 5 | **buy** an item from a merchant | `BuyItem.WAV` **+** `wearclth.WAV`, together | 138 + 146 | 2 |
| 6 | **sell** an item to a merchant | `BuyItem.WAV` only | 138 | 2 |
| 7 | tradeskill combine **SUCCESS** | *(SILENT)* | — | 2 |
| 8 | tradeskill combine **FAILURE** | *(SILENT)* | — | 3 |
| — | coin out of a corpse **loot window** | NOT MEASURED | | |

Silent gestures measured either side of those, which bound them: clicking a
money slot (opens the Quantity dialog), returning coin from the cursor to a
money slot, selecting an item in the merchant's list (the price quote), and
selecting one of your own items to sell (the offer quote).

Three findings behind the table:

- **The four denominations are ONE sound.** That was the open question. It is
  the generic coin-move `BuyItem.WAV` for platinum, gold, silver and copper
  alike, so `pickupMoney()` needs no per-denomination branch.
- **A purchase makes TWO sounds, a sale ONE.** Buy = the coin (`BuyItem`) plus
  the item landing in a slot (`wearclth`, the same sound T328 measured for a
  local drop), at the same onset. Sell = the coin only, because the item
  *leaves* a slot and T328 row 7 already measured that gesture silent. The
  asymmetry is the oracle's.
- **Both combine outcomes are silent** — including the failure, where
  `LevDn.WAV` sits in the preloaded bank and would have been an entirely
  plausible guess.

Incidentally, two of T328's own rows were **re-measured and reproduced** on a
different character in a different session: item dropped into a slot =
`wearclth.WAV`, tradeskill container opened = `itemclth.WAV`.

## Leg B — the wiring

| row | call site | id(s) | test |
|---|---|---|---|
| coin onto cursor, **all four denominations** | `InventoryManager::pickupMoney()` — already wired by T328 and already denomination-agnostic; **no code change**, the measurement closes it | `BUY_ITEM` 138 | `UISoundEvents.CoinOntoCursor_EveryDenominationAsksForTheSameBuyItem` |
| **buy** | `MerchantComponent::ZoneProcessShopPlayerBuy` (`src/client/merchant/merchant_trade.cpp`), after the length check | `BUY_ITEM` 138 **+** `WEAR_CLOTH` 146 | `UISoundEvents.MerchantBuy_AsksForBuyItemAndWearCloth`, `…MerchantBuy_OfAStackStillAsksOnce` |
| **sell** | `MerchantComponent::ZoneProcessShopPlayerSell` (same file), after the length check | `BUY_ITEM` 138 | `UISoundEvents.MerchantSell_AsksForBuyItemAndNotWearCloth` |
| combine **success** | none — PINNED silent | — | `UISoundEvents.TradeskillCombineSuccessIsSilent` |
| combine **failure** | none — PINNED silent | — | `UISoundEvents.TradeskillCombineFailureIsSilent`, `…TradeskillCombineAcknowledgementIsSilent` |

Same seam as T328: `eqt::events::PlaySoundIntent` pushed onto the
`GameStateBridge`, with the single consumer arm in
`ProcessBridgeIntents()` forwarding to `BridgeComponent::PlaySound()`. The new
tests use the same `RecordingBridge` and need no audio device.

**Why the sounds hang off the server's confirmation and not off the item
placement.** eqemu queues `OP_ShopPlayerBuy` / `OP_ShopPlayerSell` **only** on a
completed transaction — every failure path in `Handle_OP_ShopPlayerBuy` deletes
the outgoing packet and returns — so arriving in these handlers *is* the
purchase or the sale, and no success test is needed. The purchased item itself
arrives separately as an `OP_ItemPacket` -> `InventoryManager::setItem()`,
which is also the zone-in inventory-sync path; hanging `wearclth` there would
make every zone-in play a sound per item. The oracle's two purchase sounds are
simultaneous, so emitting both at this one edge matches its timing.

Both combine reply handlers (`MerchantComponent::ZoneProcessRecipeAutoCombine`
for the client's own recipe/auto-combine path, and
`PlayerComponent::ZoneProcessTradeSkillCombine` for the manual
put-items-in-the-grid path) already played nothing, which the measurement says
is correct; the tests pin both so neither can drift.

## Deviations

- **The plan's fail recipe is impossible on this server.** It asked for
  "a trivial > 300 recipe for the fail" against a Baking-300 character. eqemu's
  formula (`zone/tradeskills.cpp:1071–1107`) is
  `chance = skill − 0.75 × trivial + 51.5` for trivial ≥ 68, floored at 5 and
  capped at 95 — at skill 300 the highest-trivial Baking recipe in the database
  (335) still gives **95 % SUCCESS**, and a failure would need trivial ≥ 462,
  which does not exist. A failure needs a *low-skill* character, not a
  high-trivial recipe. Also `GetGM()` forces success outright
  (`tradeskills.cpp:1124`), so GM mode has to be off.
- **Character changed accordingly.** Testcaster (the plan's implied character,
  Baking 300, and the one T328 used) was not used at all. The coin rows were
  measured on **Testmelee**; the merchant and combine rows on **Testchar**
  (Baking 0), with a `nofail=1` recipe for the guaranteed success and a
  trivial-46 recipe for a ~79 % failure. A second reason forced the split:
  Testmelee is a **Dark Elf** and every Qeynos merchant refuses to trade with
  her ("I don't like to speak to Dark Elves much less sell to them!"), which
  is itself a measured fact — that refusal is the *only* silent-looking
  outcome that still proves the client's `OP_ShopRequest` reached the server.
- **The coin rows were measured from the INVENTORY money slots, not a bank
  window.** The plan offered the bank as the route; the inventory money area
  carries all four denominations directly, so no banker was needed. (There is
  no class-40 banker in `qeynos2` at all.)
- **The manual put-items-in-the-grid combine could not be driven**, exactly as
  T328 found. With an item on the cursor, clicking a Tradeskill Window grid slot
  does nothing — in four click forms, from both a `#summonitem` cursor and a
  normal inventory pickup, with the windows overlapping and side by side. The
  combines were performed through the client's own recipe search + Combine,
  which auto-populates the grid from inventory. Both combine reply paths are
  pinned silent regardless of which one the player reaches.
- **Row 9 (corpse loot) left, per the STOP rule.** Two attempts; the corpse
  could not be right-clicked because `#npctypespawn` drops the NPC among North
  Qeynos' training dummies and eight verified right-clicks reached a dummy's
  Consider window instead. Needs a zone with no clutter at the spawn point, not
  a new technique. Stays on B036.
- **Interaction technique that unblocked the rest, recorded because it is the
  reusable part**: a click in the 3D view must have **no** pointer motion (the
  1 px in `ui-lib.sh`'s `jclick` is read as a camera look-drag there), while a
  click on a UI widget still needs it; `#goto` must target an **offset** from
  the NPC, never the NPC's own coordinates; and every click needs a verified
  retry loop — opening the merchant window took three identical right-clicks.

## Verification

- `cmake --build build -j24` — **exit 0**, read from a per-run exit file written
  by that build. The tool call hit its 600 s ceiling and the build was moved to
  the background rather than killed, so there is no exit-143 truncation risk;
  checked anyway — `libwilleq_core.a` 321 members, `libEtcLib.a` 15 members,
  262 binaries in `build/bin`, all valid ELF, 0 bad.
- `ctest --test-dir build -j6 --output-on-failure` — **3802 / 3803 passed**. The
  single failure is `AudioManagerTest.PreloadCommonSounds`, the known ALSA host
  exception. All 7 new tests pass.
- `check_file_sizes.sh --all` — **873 checked, 873 ok, 0 warn, 0 FAIL.**
- gles2 / visual gate / ARM **skipped, per the contract**: no file under
  `src/client/graphics` or `include/client/graphics` was touched. Evidence,
  `git diff --name-only` — exactly two files:
  `src/client/merchant/merchant_trade.cpp`, `tests/test_ui_sound_events.cpp`
  (plus `refactor/BOARD.md` and this plan).
- Territory: the Leg B call sites are in `src/client/merchant/`, as the dispatch
  anticipated. slot2's T341 had not touched `merchant_trade.cpp` at this tip.

## Server and host state

eqemu locked atomically, `up.sh qeynos2`, `down.sh` (exit 0), **lock deleted
before the Leg B build**. Every character write is listed in
`review/T342/prestate.txt` / `poststate.txt` and was reversed and re-read:
Testmelee and Testchar back to the exact positions, currency, inventory rows and
stack charges they were found with, the Baking skill row the combine created on
Testchar deleted, Testcaster never logged in. `~/.asoundrc` deleted, `snd-aloop`
unloaded, the probe client deleted; `~/eq-oracle/client` was never modified.
