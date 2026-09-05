# T382 — corpse coin banked and sounded (B172); window keys match the oracle (B170)

Class A + A, slot2, no server (both oracles are measured in
`review/T377/`). Read the B172 and B170 board text and T377's §Review
items 1 and 3.

## Leg A — B172

`CommandsComponent::ZoneProcessMoneyOnCorpse` (`src/client/commands/combat_actions.cpp`)
parses the four denominations, logs at debug level, and drops them. The
official client, the instant the loot window opens: plays `BuyItem.WAV`
(UI sound id 138) and prints `You receive 1 platinum, 8 gold, 3 silver
and 4 copper from the corpse.` (eqstr — find the id in `eqstr_us.txt`; it
takes the four counts; check what the official client prints when a
denomination is zero — T377's capture has one line; if the zero form is
unknown, print all four as the capture shows and say so).
1. Bank the four denominations into the player's purse through the
   existing money path (`InventoryManager` — find how `OP_MoneyUpdate`
   credits and reuse it; do not add a second purse writer).
2. Push `PlaySoundIntent{BUY_ITEM}` (T328 Leg B's intent) and the chat
   line.
3. Tests, pre-fix failing: after the packet, the purse holds +1/+8/+3/+4;
   one PlaySoundIntent with id 138 was published; the chat line matches.
   The bridge intent golden learns the reviewed statement IN PLACE.
4. `InventoryManager::pickupMoney()`'s BUY_ITEM (T328) still has NO
   production caller — leave it, note it on B036 clause 2.
5. Strike B172; fill B036's corpse-coin row.

## Leg B — B170

Official defaults (Options > Keys > UI, T377 screenshots): group window
`Alt+P`, buff window `Alt+B`, pet info window `Alt+F1`, no alternates.
Fable: group `G` (+ `Alt+P` alt), pet `P` (= the official Customer
Service key), buff `Alt+B`.
1. `registerDefaultBindings` (`src/client/input/hotkey_manager.cpp`):
   group → `Alt+P` only (delete `G`), pet → `Alt+F1` (delete `P`), buff
   unchanged. Fix every text mention (`README.md`, the CLAUDE.md key
   tables, help strings, T368's tests that press G/P). `DocCounts` nets the
   `HotkeyAction` count — the enum does not change, only the bindings.
2. Also the empty group window: the oracle paints ONLY its title bar
   (`Group`), no body — T368's titled frame draws a body; match the
   oracle (the static-panel test seam).
3. Tests, pre-fix failing: the binding table maps `Alt+F1` → pet, `Alt+P`
   → group, and `G`/`P` → nothing; the empty group panel draws title only.
4. Strike B170 (and B115: the screenshot exists now — strike it citing
   `review/T377/oracle/`).

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, goldens for the
TUs touched, file sizes; Leg B touches `src/client/graphics` (the panel)
→ visual gate standalone + ARM. The live proof (loot a corpse on fable,
hear 138, see the purse move) is OWED to the next lock unit — note it.

STOP: the money path has no single credit function (report the writers).

## Review

Branch `task/T382-corpse-coin-key-bindings`, base `f202c362`. Two commits:

| Leg | Commit | Subject |
|---|---|---|
| A (B172) | `0d7751fe` | corpse coin is banked, sounded and announced |
| B (B170) | `7f6128f8` | the window keys and the empty group window match the oracle |

(plus a third commit for the board strikes and this Review.)

No server, no eqemu lock. The live proof is OWED — see "Owed" below.

---

### Leg A — B172, the corpse-coin grant

**The STOP condition did not fire, but the plan's premise needed correcting.**
The plan says "find how `OP_MoneyUpdate` credits and reuse it (`InventoryManager`)".
`OP_MoneyUpdate` is not in `InventoryManager` at all — it is
`MerchantComponent::ZoneProcessMoneyUpdate` (`src/client/merchant/merchant_trade.cpp`),
and what it does is `PlayerState::setCurrency(...)` followed by a
`CurrencyChanged` publish. The purse writers, as asked for:

| Writer | Callers |
|---|---|
| `PlayerState::setCurrency` (`src/client/state/player_state.cpp`) | `ZoneProcessMoneyUpdate` (absolute set from the wire), `ZoneProcessShopPlayerBuy` (copper-total round trip), `zone_entry.cpp` (zone-in sync) |
| `PlayerState::setPlatinum/setGold/setSilver/setCopper` | one caller, `BridgeComponent::BankCoinSet`, the bank-window coin mover |

So there IS a single credit function for the on-hand purse — `setCurrency` —
and the leg reused it rather than stopping: `PlayerState::addCurrency` reads the
four counters and hands the sums to `setCurrency`. It assigns no member itself,
so `setCurrency` remains the one writer, which is what the plan's "do not add a
second purse writer" was protecting.

**How the coin is credited.** `ZoneProcessMoneyOnCorpse` →
`GameState::player().addCurrency(pp, gp, sp, cp)` → `PlayerState::setCurrency` →
`CurrencyChanged` pushed on the bridge (the same projection `OP_MoneyUpdate`
publishes; D18a makes it the only way the on-screen counters move). Addition is
per-denomination with no carry: the oracle's purse holds 8 gold as 8 gold, and
the server is authoritative for any normalisation.

**The eqstr id: 12072**, `You receive %1 from the corpse.` — an exact match for
the captured sentence's outer form. `%1` is built in the handler as
`"{} platinum, {} gold, {} silver and {} copper"`.

**A finding worth flagging: eqstr 13108 does NOT match the oracle's pixels.**
The table's own coin-list template is `13108 %1 platinum, %2 gold, %3 silver,
and %4 copper` — with an Oxford comma. The captured line
(`review/T377/oracle/T377_oracle_corpse_loot_coin_1024x768.png`, cropped and
scaled 8x to read it) is:

```
You receive 1 platinum, 8 gold, 3 silver and 4 copper from the corpse.
```

— **no comma before "and"**. Per `match-the-oracle-dont-infer-it`, the render is
the golden, so 13108 is deliberately not used and the list is spelled to match
what the client actually printed. Recorded in the handler's comment and in the
test's.

**Zero-denomination handling.** Two different cases, handled differently:

* **All four zero → nothing at all.** No credit, no sound, no line, early
  return. This is not a guess: every `OP_MoneyOnCorpse` that is not a coin grant
  carries four zeros — eqemu's six `SendLootReqErrorPacket` responses
  (`LootResponse::SomeoneElse` / `NotAtThisTime` / `Hostiles` / `TooFar`), a
  corpse with no money (`Corpse::MakeLootRequestPackets` zeroes the struct and
  says "This corpse contains no money"), and the autosplit path (which zeroes it
  and pays the group through its own `OP_MoneyUpdate`). Guarding on the counts
  rather than on `response` keeps every failure shape silent without this client
  having to model eqemu's response enum.
* **ONE denomination zero → all four are still printed** (`You receive 0
  platinum, 0 gold, 3 silver and 4 copper from the corpse.`). This is
  **UNMEASURED** — T377's single capture had all four non-zero — and the plan
  says to print all four as the capture shows and say so. Said so: in the
  handler's comment, in the test's comment, in the test name
  (`AZeroDenominationIsStillPrinted`), and on B036.

**Also in this leg:** the handler's local re-declaration of
`MoneyOnCorpse_Struct` was replaced by the canonical `EQT::MoneyOnCorpse_Struct`
(`include/common/packets/combat.h`, `static_assert`ed at 20 bytes against
eqemu's `moneyOnCorpseStruct`); the length check was already the same 22 bytes,
spelled as a literal. `12072` was added to `StringDb::GetStringMessage`'s
hardcoded fallback table, alongside the existing `12442` / `5805` / `303`, so
the line is right in a test with no `willeq.db` loaded.

`InventoryManager::pickupMoney()`'s `BUY_ITEM` (T328) was left alone and still
has NO production caller — noted on B036 clause 2, with the reason: the corpse
grant is a different gesture with a different publisher, so wiring one did not
reach the other.

#### Tests — Leg A

`tests/test_corpse_coin.cpp` (new, 8 cases, registered in
`tests/cmake/commands.cmake`) and 3 cases added to
`tests/test_ui_sound_events.cpp` for B036's corpse row.

| Test | Pre-fix | Post-fix |
|---|---|---|
| `CorpseCoin.GrantIsBankedOntoTheExistingPurse` | **FAIL** | PASS |
| `CorpseCoin.GrantOntoAnEmptyPurseIsTheGrant` | **FAIL** | PASS |
| `CorpseCoin.GrantPublishesCurrencyChangedWithTheNewTotals` | **FAIL** | PASS |
| `CorpseCoin.GrantPrintsTheMeasuredOracleLine` | **FAIL** | PASS |
| `CorpseCoin.AZeroDenominationIsStillPrinted` | **FAIL** | PASS |
| `CorpseCoin.AnAllZeroPacketGrantsNothingAndSaysNothing` | pass (vacuous) | PASS |
| `CorpseCoin.ARefusedLootResponseGrantsNothing` | pass (vacuous) | PASS |
| `CorpseCoin.AShortPacketIsIgnored` | pass (vacuous) | PASS |
| `UISoundEvents.CorpseCoinGrant_AsksForBuyItem` | **FAIL** | PASS |
| `UISoundEvents.CorpseCoinGrant_MakesExactlyOneSound` | **FAIL** | PASS |
| `UISoundEvents.CorpseWithNoCoinIsSilent` | pass (vacuous) | PASS |

7 of 11 fail pre-fix; all 11 pass post-fix. The four that pass either way are
the negative cases — pre-fix the handler did nothing, which is trivially "no
coin, no line, no sound"; they exist so the guard cannot be removed later.

Pre-fix run method: `git diff` of the four production files exported to a patch,
`git apply -R`, rebuild, run, `git apply` to restore (never `git stash` — it is
cross-slot shared state).

---

### Leg B — B170, the window keys and the empty group window

**The new bindings** (`HotkeyManager::registerDefaultBindings`):

| Action | Was | Now | Oracle |
|---|---|---|---|
| `ToggleGroup` | `G` (primary) + `Alt+P` (alternate) | **`Alt+P`** only | Toggle party window = `Alt P`, Alternate empty |
| `TogglePetWindow` | `P` | **`Alt+F1`** | Toggle pet info window = `Alt F1`, Alternate empty |
| `ToggleBuffWindow` | `Alt+B` | `Alt+B` (unchanged) | Toggle buff window = `Alt B`, Alternate empty |

`G` and `P` are now bound to nothing in Player mode. `P` is the oracle's
**Toggle Customer Service Win**; fable has no petition window, so the right
answer is that `P` does nothing rather than that it opens a pet window a
real-client player never asked for. Bare `F1` is still `TargetSelf` — pinned by
a test, since `Alt+F1` sits next to it.

The `HotkeyAction` enum is untouched (only bindings changed), so `DocCounts`
nets the same count and needed no edit.

**Every text mention fixed:**

| File | What changed |
|---|---|
| `src/client/input/hotkey_manager.cpp` | the two `addBinding` lines, + a comment recording the measurement and its provenance |
| `config/hotkeys.json` | `"ToggleGroup": ["G","Alt+P"]` → `"Alt+P"`; `"TogglePetWindow": "P"` → `"Alt+F1"` (a shipped default config — it would have overridden the code) |
| `README.md` | Windows key table: `G / Alt+P \| Group` → `Alt+P`; `P \| Pet` → `Alt+F1` |
| `include/client/graphics/ui/CLAUDE.md` | the key table's two rows; the T368 paragraph's "G, Alt+P, P and Alt+B did nothing"; the panel table's "empty titled frame" → "bare title bar"; and the whole "the official default KEYS are not recorded in any file on disk … see B115" paragraph replaced by the measured row and its screenshot path |
| `include/client/graphics/frame/frame_component.h` | the T368 block's "G (and the Alt+P alternate), P and Alt+B"; the "fable's G / P / Alt+B are left as they were rather than re-picked by argument. See B115" paragraph (B115 is now closed); the group row of the composition list |
| `include/client/graphics/ui/static_panels.h` | `GroupPanelState::isOpen`'s "G / Alt+P key" comment; the whole `groupPanelIsVisible` doc block, whose "UNVERIFIED against the oracle's pixels … a screenshot is owed — board B115" is now the opposite of true |
| `src/client/graphics/ui/static_panels.cpp` | the "G / Alt+P toggle" comment; the "with no group the frame and its title are all that draws" comment |
| `src/client/bridge/irrlicht_bridge.cpp` | "a player who closed the pet window with P" |
| `tests/test_frame_component.cpp` | the fixture comment naming the keys behind each `RendererAction` |

**How the empty group panel draws now.** `renderGroupPanel` paints
`groupPanelFrameRect(layout, state)` instead of `layout.groupPanel`. That new
free function returns the full panel when `state.inGroup`, and otherwise
collapses it to `BAR_HEIGHT + MARGIN` (18px) tall — which is not an invented
number: it is exactly the title band the panel's own layout already reserves
before the first member row (`y = groupPanel.Y + BAR_H + PAD`). The strip keeps
the full panel's origin and width, so joining a group grows the frame downward
and moves nothing. The title text is unchanged and still drawn at the panel
origin + 2, inside the strip.

It is a **free function, not a lambda or an inline branch**, because that is the
static-panel test seam: the shape is assertable with no Irrlicht device, which
is how the six shape tests run.

`groupPanelIsVisible` is unchanged — the window is still SHOWN with no group
(`defaults.ini` writes no `Show=0` under `[GroupWindow]`, and T377 saw it open).
Only its body is gone.

#### Tests — Leg B

| Test | Pre-fix | Post-fix |
|---|---|---|
| `HotkeyOracleWindowKeysTest.AltPTogglesTheGroupWindow` | pass | PASS |
| `HotkeyOracleWindowKeysTest.AltF1TogglesThePetWindow` | **FAIL** | PASS |
| `HotkeyOracleWindowKeysTest.AltBTogglesTheBuffWindow` | pass | PASS |
| `HotkeyOracleWindowKeysTest.BareGIsBoundToNothing` | **FAIL** | PASS |
| `HotkeyOracleWindowKeysTest.BarePIsBoundToNothing` | **FAIL** | PASS |
| `HotkeyOracleWindowKeysTest.TheThreeWindowActionsHaveOneBindingEach` | **FAIL** | PASS |
| `HotkeyOracleWindowKeysTest.BareF1StillTargetsSelf` | pass | PASS |
| `GroupPanelShapeTest.WithNoGroupOnlyTheTitleBarIsPainted` | **FAIL** | PASS |
| `GroupPanelShapeTest.WithAGroupTheFullPanelIsPainted` | pass | PASS |
| `GroupPanelShapeTest.TheStripSharesTheFullPanelsOriginAndWidth` | pass | PASS |
| `GroupPanelShapeTest.TheStripContainsTheTitleTextRow` | pass | PASS |
| `GroupPanelShapeTest.AnEmptyGroupWindowIsStillVisible` | pass | PASS |
| `GroupPanelShapeAtEveryResolution.TheEmptyWindowIsOneTitleStripEverywhere` | **FAIL** | PASS |

6 of 13 fail pre-fix; all 13 pass post-fix.

Pre-fix run method: `hotkey_manager.cpp` reverted by exported patch; for the
panel, `groupPanelFrameRect` was temporarily stubbed to `return
layout.groupPanel;` (T368's shape) rather than deleted, because deleting a
function the test calls is a compile error, not a red test. Both restored
before the commit.

---

### Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` (GLES2 config, tests on) | **EXIT 0** |
| `ctest --test-dir build -j6` | **4004 / 4005**; sole failure `AudioManagerTest.PreloadCommonSounds` (the waived ALSA host quirk). ctest exits 8 with only it failing — summary line read, per protocol |
| Statement-text goldens | none moved: no golden file changed, `FileSizeCeiling` / `DocCounts` / `test_zone_dispatch_table` / `test_bridge_intent_table` / `test_reachability_sweep` / `test_command_registration_order` all green in the run above. No new intent alternative, no new event type, no dispatch-table row |
| Pixel goldens | none moved (`tests/goldens/*.png` untouched; `test_frustum_debug_draw` and the visual gate green) |
| `scripts/build-gles2-x86.sh` | **EXIT 0** |
| Visual gate (standalone) | **PASS** (`=== VISUAL GATE: PASS ===`), run standalone after the GLES2 build; CPU p95 qeynos2/gfaydark/befallen all inside ceiling |
| `scripts/build-arm-noble.sh` | **EXIT 0** |
| `scripts/refactor/check_file_sizes.sh fable` | 36 ok / 0 warn / **1 pre-existing FAIL** — `tools/model_viewer.cpp` 3242 lines (> 3000), untouched by this unit (last written by T270, `aaa24324`, and not in `git diff fable...HEAD`). No NEW FAILs; the two files this unit grew are `tests/test_hotkey_manager.cpp` (939) and `tests/test_static_ui_panels.cpp` (772), both well under |

### Owed

**The live proof of Leg A is OWED to the next unit that takes the eqemu lock**:
loot a corpse on fable, hear sound 138, watch the purse counters move and the
chat line appear. T377 left the recipe on B036 — zone `arena`, `#npctypespawn
113423` (`Gargalek_Daemado`, loottable 13487, a giant so the corpse is big),
`/target Gargalek`, `#damage 100000` (not `#kill` — that calls `Mob::Kill()`
with no killer and credits nobody), step 20 units back, F9 for third person.

Leg B's keys are worth pressing in the same session (`Alt+P`, `Alt+F1`), and the
empty group window's new shape is worth one screenshot next to
`review/T377/oracle/T377_oracle_group_window_nogroup_open_1024x768.png`.

### Surprises

1. **eqstr 13108 disagrees with the client's own output** (the Oxford comma).
   The table is not the oracle; the render is.
2. **`OP_MoneyUpdate` is not in `InventoryManager`.** The plan's pointer was
   off by a component; the purse lives in `PlayerState` and is written from
   `MerchantComponent` and `zone_entry`.
3. **`config/hotkeys.json` ships the old bindings.** Fixing only
   `registerDefaultBindings` would have left `G` and `P` live for anyone whose
   config directory is the shipped one — the code default is the fallback, not
   the authority.
4. **B036's clause 1 is now fully closed**, not just its last row: with the
   corpse row wired, every measured coin/container/buy/sell/combine row is
   either wired or pinned silent. What remains is narrower than a row — the
   zero-denomination spelling.
