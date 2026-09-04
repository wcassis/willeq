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
