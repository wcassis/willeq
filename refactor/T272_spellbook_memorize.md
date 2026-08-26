# T272 — spellbook popup click cannot memorize (T264 F4)

Slot: slot2. x86. OWNS eqemu (noclobber) for a live memorize. Class A.
Territory: the spellbook popup click handler (static_panels / the UI panel
that draws the spellbook — T264's Review names the LOG stub and
`MemorizeSpellIntent`), the intent arm in eq_bridge.cpp, CommandProcessor
`cmdSpellbook`'s stale "not implemented in UI" message, tests. Do NOT
touch eq_strings race names (T271 slot1), read-item (T273 slot3).

## Gap
The spellbook popup exists and lists spells; its click handler is a LOG
stub that names `MemorizeSpellIntent` as the missing piece (T264 kept the
intent for exactly this). `/mem` works, so the sender is proven — this is
the UI-gesture leg (T48 class), and it is the first one we can actually
close because the popup exists.

## Scope
1. Click on a spellbook entry (and drag-to-gem if the popup supports it —
   check what the panel already tracks) pushes `MemorizeSpellIntent{gem,
   spellId}`; the arm calls the same handler `/mem` uses (one body, T230
   pattern). Update `tests/test_reachability_sweep.cpp`'s expected set.
2. Fix `cmdSpellbook`'s stale message.
3. Tests: static-panel test (T54 added the first ones) that a click at a
   spell's rect pushes the intent with the right ids; handler test that
   the intent memorizes. Red/green.
4. Live: open the spellbook, click a spell into a gem, `/gems` shows it,
   server accepts (OP_MemorizeSpell on the wire), then cast it.

## Verification
Full ctest; visual gate (UI panel touched); Review; evidence review/T272/.
Commit: `T272: spellbook clicks memorize spells`.

---

## Review (executor, 2026-08-26, slot2, branch task/T272-spellbook-memorize off fable 0a379d8a)

**Done. Evidence: `/mnt/projects/fable/review/T272/` (README.md there).**

### What the gap actually was -- BOTH ends were dead

The plan described the producer gap. There was a second one underneath it:

`eq_bridge.cpp`'s `MemorizeSpellIntent` arm called
`SpellManager::memorizeSpell(uint32_t spell_id, uint8_t gem_slot)` as
`memorizeSpell(i.gemSlot, i.spellId)` -- **arguments swapped**. It asked to
memorize "spell <gem number>" into "gem <spell id & 0xFF>", which
`memorizeSpell` rejects at its `gem_slot >= MAX_SPELL_GEMS` guard. Because
nothing produced the intent, the line had never once executed. This is the
T264 dead-arm class biting a second time: an arm that compiles, reads
plausibly, and is wrong. `review/T272/red_consumer_swapped_args.txt` is that
code failing the new consumer test.

### Scope, item by item

1. **Click produces the intent.** `static_ui_input.cpp`'s spellbook row
   handler replaced the LOG stub with a `pushIntent(MemorizeSpellIntent{gem,
   spellId})`. The popup tracks no drag state, so the destination gem is
   chosen by a new shared resolver:
   - `SpellGemPanelState::memorizeTargetGem` (new, default -1) and
     `resolveMemorizeGem(const SpellGemPanelState&)` in
     `static_panels.h/.cpp` -- explicit pick if set, else the first empty
     gem, else -1 (refuse rather than silently clobber gem 1).
   - The explicit pick is made by **clicking a gem while the spellbook is
     open**, which now sets the target instead of casting (you cannot cast
     out of your spellbook anyway, and it is the only way to say "overwrite
     THIS gem" when all eight are full). Clicking the picked gem again
     clears it; the pick is consumed by the memorize click it targeted.
     With the book CLOSED the gem click still casts -- pinned by
     `GemClickWithSpellbookClosedStillCasts`.
   - The gem panel draws a gold marker on the picked gem (see the deviation
     note below).
   - A free function, not a method, so the tests drive the same body the
     click handler uses with no Irrlicht device (T156's precedent).
   `tests/test_reachability_sweep.cpp`'s `kBlockedIntents` no longer lists
   `MemorizeSpellIntent`.
2. **The arm.** Swapped arguments fixed, plus the same chat feedback the
   `CastSpellIntent` arm directly above it renders. `SpellManager` remains
   the single gem authority (T240) and is the one body `/mem` also reaches
   via `EqActionHandler::memorizeSpellByName`; no second implementation was
   added.
3. **`cmdSpellbook`.** Was `"Spellbook window not yet implemented in UI"`.
   The window has existed for some time (`SpellbookPopupState` /
   `renderSpellbookPopup`, Ctrl+B / `/newbook`) and since this unit its rows
   are clickable, so the text was simply false. It is still a pure message
   with no dispatcher call -- `CommandProcessor` has no channel to the
   renderer's popup state -- but it now names the binding and the gesture.
   `tests/test_command_processor_migration.cpp`'s
   `Spellbook_DisplaysMessageWithoutDispatching` pinned the stale string; it
   is updated and additionally asserts `"not yet implemented"` never
   reappears.
4. **Tests.** New `tests/test_spellbook_memorize.cpp`, 20 tests in three
   legs (resolver policy / real `StaticUIInput` producer / real
   `EverQuest::ProcessBridgeIntents` consumer against a real `SpellManager`
   and the real spell DB, self-skipping without `willeq.db`).

### Measured red/green

| suite | red (pre-fix binary) | green |
|---|---|---|
| `test_spellbook_memorize` | **8 of 20 FAIL** (7 producer + the swapped-args consumer) | 20/20 |
| `test_reachability_sweep` | FAIL: `NEW producer-less intents: MemorizeSpellIntent` | 5/5 |
| `test_command_processor` (`*Spellbook*`) | FAIL on the stale string | 1/1 |

Red was measured by stashing only the three production files and rebuilding;
artifacts `red_*.txt` / `green_*.txt` in `review/T272/`.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | PASS |
| `ctest --output-on-failure` | **3032/3033**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | exit 0 |
| Visual gate | **PASS**, goldens untouched |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | 0 FAIL (2 pre-existing WARNs) |

### Live (nektulos, Testcaster wizard lvl 1) -- a real click was driven

Two Xvfb + `--egl-x11` + xdotool runs, full logs and screenshots in
`review/T272/`. The client renders 800x600 inside the 1024x768 root, so
click coordinates are 800x600 `UILayout` coordinates.

- **Run 1** (plain click, first-empty-gem): click on row 3 ->
  `Spellbook: memorizing 'Numbing Cold' into gem 3` ->
  `Sent MemorizeSpell packet: spell=374, gem=2` ->
  `MemorizeSpell response: slot=2 spell_id=374 scribing=1` ->
  `Memorized spell 'Numbing Cold' in gem 3`; `/gems` `[3] <empty>` becomes
  `[3] Numbing Cold Ready`; the server persisted the gem to
  `character_memmed_spells`.
- **Run 2** (gem-pick, then cast): click gem A6, then row 0 ->
  memorized into **gem 6, not the first empty gem 3** -> `/cast 6` ->
  `Casting Spell #288...` -> `BeginCast: ... 'Minor Shielding' (288)` ->
  `Casting complete.`

All four of the plan's live checkpoints (open, click into a gem, `/gems`,
server accepts on the wire, then cast) are covered.

### Deviations

1. **The plan's scope stopped at the producer; the consumer arm was broken
   too.** Fixed in the same unit -- the producer is untestable end to end
   without it, and it is squarely in the plan's named territory ("the intent
   arm in eq_bridge.cpp"). Flagged rather than absorbed because it is a
   second live defect, not a wiring step.
2. **Gem-click semantics changed while the spellbook is open** (pick a
   destination instead of casting). Not spelled out in the plan; it is the
   answer to the plan's "drag-to-gem if the popup supports it -- check what
   the panel already tracks". The panel tracked nothing, so a two-click
   gesture replaced a drag. Behaviour with the book CLOSED is unchanged and
   pinned by a test.
3. **The new gold destination marker is not visible in the live capture** --
   and neither is anything else `UIRenderer::drawRect()` draws in that panel,
   including the `Ready`-state green gem borders that shipped long before
   T272 and use the identical idiom. `drawRect` routes through the atlas
   `white` sprite; the atlas loads fine (`UIAtlas: loaded from EQE1`). This
   is a pre-existing `ui_atlas`/`ui_renderer` matter, outside territory,
   untouched, and reported for Will rather than self-assessed. See
   `t272_5_gemcolumn_zoom.png`.
4. **eqemu was shut down once before the DB was restored**, so the stack was
   brought back up (`eqemu_up_for_restore.log`), restored, and shut down
   again. Net effect nil -- `db_restored.txt` matches the recorded
   before-state exactly -- but the sequence is recorded rather than hidden.
5. **Testcaster's spellbook was EMPTY** in the DB (gems held Frost Bolt, but
   `character_spells` had zero rows), so six level-1 wizard spells were
   staged there for the runs and deleted afterwards. Recorded in
   `db_before_and_staged.txt` / `db_restored.txt`.

### Also found, not fixed

`resolveMemorizeGem` treats both `0` and `SPELL_UNKNOWN` (0xFFFFFFFF) as an
empty gem because `SpellGemState::spellId` documents both. Only the `0`
convention was observed live. Not a defect, just an ambiguity worth knowing
about if the gem panel's feed is ever tightened.
