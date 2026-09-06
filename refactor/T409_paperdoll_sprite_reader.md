# T409 — the paperdoll at the oracle's proportion (B219); the 0-based sprite reader (B162)

Class C-ruled + B, no server. Read B219 (the oracle: `IW_CharacterView`
85x168 at (166,57) in 428x460), T393's §Review (`inventory_layout.h`,
`paperdollRect()`, `coinRect()`), and for Leg B: B162 (settled: the digit
is 0-based, `GEN<L><d>0_SPRITE` → `spel<l><d+1>.tga`), T389's §Review,
`spells_eff_reader.cpp` `effectNameToTexture()`, T389's name→tile
mapping in `spell_emitter_presets.cpp`.

## Leg A — B219

`paperdollRect()` scales the oracle's character-view rect into the popup
the same way `coinRect()` scales the coins (T393's integer mapping from
428x460). The doll render (T294 ordering) draws into the smaller rect;
the coins no longer overlap it. Test, pre-fix failing: the paperdoll
rect and the four coin rects are disjoint at all five resolutions; the
rect equals the scaled oracle rect. Static-panel seam. Strike B219; the
live screenshot pair is OWED to the next lock unit.

## Leg B — B162

`effectNameToTexture()` adopts the 0-based rule and drops the `'0'`
exclusion: 36 of 40 names resolve (272 of 301 slots). The mapping from
resolved texture name → atlas tile (T389's `applyStageSpriteNames`)
gains the newly resolved names by the same tile-vs-crop comparison T389
used (its sprites/ crops; the atlas now carries alpha — T400 — so shape
matters). Tests, pre-fix failing: `GENN00_SPRITE` → `speln1`,
`GENA10_SPRITE` → `spela2`, the resolved-slot census = 272. Strike B162.

## Proof

Build, `ctest -j6`, pre-fix/post-fix, visual gate standalone + ARM (no
golden should move — the gate opens no inventory and casts nothing),
file sizes.

STOP: none expected.

## Review

Branch `task/T409-paperdoll-sprite-reader`, base `fc4a2071`.
Leg A `87169ed7`, Leg B (this commit). Slot 3, RelWithDebInfo. No eqemu
(T405 holds the lock); the Leg A live pair is OWED.

### Leg A — B219, the paperdoll at the oracle's proportion

`InventoryLayout::paperdollRect()` no longer derives from fable's own
column geometry. It maps the official client's character view by
`coinRect()`'s rule — proportional, integer division, from the 428x460
`InventoryWindow` onto the fixed 400x350 `centerPopup`:

| | oracle (428x460) | popup (400x350) | at 800x600 |
|---|---|---|---|
| paperdoll (`IW_CharacterView`) | 166,57 85x168 | **155,43 79x127** | {355,168,434,295} |
| coin column (`IW_Money0..3`) | 303,121.. 70x24 | 283,92.. 65x18 | {483,217,548,235}.. |

The arithmetic: `166*400/428 = 155`, `57*350/460 = 43`, `85*400/428 = 79`,
`168*350/460 = 127`. The old rect was offset (48,20) size **304x256** — 76%
of the popup width against the oracle's 20%.

**Disjointness.** The doll spans popup-x **155..234**; the coin column
spans popup-x **283..348**. 234 < 283, so they cannot intersect on the X
axis at any resolution — `centerPopup` is a fixed 400x350 everywhere (T351
measured five), so every rect in the header is the same offset from the
popup origin at every resolution, and the 49 px gap is resolution-invariant.
`PaperdollT409.TheRectIsDisjointFromEveryCoinRowAtEveryResolution` asserts
it exhaustively (5 resolutions x 4 rows x {row, decal box}), and the T60
invariants (inside the frame, clear of all 30 slot rects) are unchanged.

**The second defect the narrowing exposed.** The doll's offscreen render
target was a hardcoded **304x256 in two places** — `ensurePaperdollInitialized()`
in `overlays_hud_env.cpp` and `tools/hwcaps/ui_inventory_capture.cpp`, each
with a comment saying it matched `paperdollRect()`. It did; nothing enforced
it. The narrowed rect is 79x127 (aspect 0.62) against the target's 1.19, so
the blit would have squashed the model 1.9x horizontally. Both callers now
take `InventoryLayout::paperdollRenderTargetSize()` — the rect supersampled
2x, 158x254 — so the target cannot drift from the rect again.

| test (`tests/test_static_ui_panels.cpp`) | pre-fix | post-fix |
|---|---|---|
| `PaperdollT409.TheRectIsTheOracleCharacterViewScaledIntoThePopup` | FAIL | PASS |
| `PaperdollT409.TheRectIsIdenticalRelativeToThePopupOriginAtEveryResolution` | FAIL | PASS |
| `PaperdollT409.TheRectIsDisjointFromEveryCoinRowAtEveryResolution` | FAIL (all 5 resolutions, all 4 rows + all 4 decals) | PASS |
| `PaperdollT409.TheRenderTargetCarriesTheRectsAspect` | pass (guard) | PASS |

The fourth is honestly a guard, not a red: pre-fix the target and the rect
were the same 304x256 by construction, so it could only have gone red *as a
consequence* of the rect change. It is what makes the two-place hardcode a
test failure rather than a silent squash next time.

### Leg B — B162, the 0-based sprite reader

`effectNameToTexture()` implements the measured rule:
`GEN<L><d>0_SPRITE -> spel<l><d+1>.tga`, digit range `'0'..'8'`
(`'9'` would construct a two-character index no file uses). The `'0'`
exclusion is gone.

**The census, MEASURED over the real `spells.eff` and the real
`SpellEffects/` directory** by the new
`SpellsEffReaderFileTest.TheResolvedSlotCensusIsTheZeroBasedOne`, which
prints its table:

| | verbatim (shipped) | 0-based (this commit) |
|---|---|---|
| distinct names | 40 | 40 |
| non-empty slots | 301 | 301 |
| names resolved | 18 | **36** |
| slots resolved | 166 | **286** |
| **slots landing on a file that exists** | **157** | **272** |

which is board B162's 157 -> 272 exactly. The four names that never resolve
are the only non-GEN ones: `GEN_Z_SPB`, `I_SNOWFLAKESPRITE`, `I_TFIRE_SPB`,
`I_TSMOKE_SPB`.

**The counterexamples moved.** T70 recorded `GENA30`/`GENY10` as the two
nonzero-digit names with no matching file. Under the correct rule `GENY10`
resolves to the real `spely2.tga`, and the pair that constructs an absent
file is `GENA20` -> `spela3.tga` and `GENA30` -> `spela4.tga`.

#### The name -> tile rows

Ten added, by T389's tile-vs-crop comparison. **A trap first: every
`spel*.tga` is a 4x4 SHEET of one identical frame** (some 3x3 or 5x5), so
any descriptor taken over the whole file is meaningless — the energy is
uniform and every sprite looks like the same annulus. T389's own crops are
whole sheets. The comparison here is on the single repeated frame; crops in
`review/T409/sprites/`.

| name | file | frame | tile | why |
|---|---|---|---|---|
| `GENA00` | `spela1` | pointed star, bright white core, soft glow | `SpellSparkle` | same class as `speld2`, which T389 measured onto it |
| `GENA10` | `spela2` | bright blue EIGHT-pointed star | `SpellSparkle` | **oracle-measured** (record 35, spell 253 Whirling Wind) |
| `GENB00` | `spelb1` | green/yellow multi-point burst | `StarShape` | the only green pointed-star tile |
| `GENC00` | `spelc1` | orange radiant sunburst | `Ember` | the only orange tile |
| `GENE00`, `GENE01` | `spele1` | green/teal sphere | `LeafShape` | the green filled disc |
| `GENJ00` | `spelj1` | pale-blue ribbon swirl | `SmokeWisp` | the only elongated tile |
| `GENU00` | `spelu1` | purple/blue ribbon swirl (T389 saw it at Frost Bolt's caster) | `SmokeWisp` | same |
| `GENN00` | `speln1` | green/orange MOTTLED sphere | `SnowPatch` | **oracle-measured** (record 20, spell 41 Weaken); the only mottled disc — every other round tile is a smooth gradient |
| `GENZ00` | `spelz1` | white/grey swirling smoke ball | `WispyCloud` | |

Two of the ten are oracle measurements; the other eight are shape
comparisons and are stated as such, in the code comment and in
`review/T409/README.md`, so Will can overrule a row from the contact sheet.

**What is NOT mapped is the finding, and it is boarded as B300.** Several
newly resolved sprites are not abstract particles at all: `spelg1.tga` is a
**SWORD**, `speli1.tga` a **flaming SKULL**, `spelk2.tga` a row of skulls,
`spelh1.tga` a lightning-wreathed sphere. `GENI00_SPRITE` is the commonest
name in the entire file (32 of 301 slots) and it is the skull — so the
single most-used sprite in `spells.eff` is one a 16x16 procedural tile can
never approximate. Those keep the `SoftCircle` fallback, pinned by
`SpellSpriteTilesT409.TheFigurativeSpritesKeepTheFallback`. A third group
(`spelm1`, `spelo1`, `spell1`, `spelv1`, `spelp1`) are round soft dots the
fallback already serves correctly, so a row would add nothing.

| test | pre-fix | post-fix |
|---|---|---|
| `SpellsEffReaderMappingTest.TheTwoOracleMeasuredNamesResolveZeroBased` | FAIL | PASS |
| `SpellsEffReaderMappingTest.TheZeroDigitFormNowResolves` | FAIL | PASS |
| `SpellsEffReaderMappingTest.NonzeroDigitNamesShiftUpByOne` | FAIL | PASS |
| `SpellsEffReaderMappingTest.KnownCounterexamplesDocumented` | FAIL | PASS |
| `SpellsEffReaderMappingTest.DigitNineIsOutOfRange` | FAIL | PASS |
| `SpellsEffReaderFileTest.TheResolvedSlotCensusIsTheZeroBasedOne` | FAIL (18/166/157) | PASS (36/286/272) |
| `SpellSpriteTilesT409.*` (7 mapping + routing tests) | FAIL | PASS |
| `SpellSpriteTilesT409.TheFigurativeSpritesKeepTheFallback` | pass (guard) | PASS |

### Deviation

**One test outside the plan's territory had to change.**
`SpellVisualSelectorFileTest.Record0ResolvesWithExactStagingAndTextures`
(`tests/test_spell_visual_selection.cpp`) pinned T70's rule end-to-end
through `SpellVisualSelector`: `GENC10 -> spelc1.tga` and `GENG00 -> ""`.
It went red on the first full `ctest` after the source fix — the only red in
4201 — and both assertions are now the corrected values (`spelc2.tga`,
`spelg1.tga`), each re-checked against `SpellEffects/`. It is flagged rather
than absorbed because it was not pre-fix-red by design: it was correct for
the old rule, and the rule was wrong.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | EXIT=0 |
| `ctest -j6` (after Leg B source fix, before the selector-test update) | **4200 / 4201**, one FAIL: `SpellVisualSelectorFileTest.Record0ResolvesWithExactStagingAndTextures` (the deviation above) |
| `ctest -j6` (final) | **4201 / 4201, 0 failed** |
| `scripts/build-gles2-x86.sh` | EXIT=0 |
| visual gate (standalone) | **PASS**, `=== VISUAL GATE: PASS ===`; no golden file modified (`git status` shows only source/test/board/plan). CPU p95 4.311 / 5.178 / 6.256 ms against ceilings 10.108 / 10.672 / 10.948 for qeynos2 / gfaydark / befallen |
| `scripts/build-arm-noble.sh` | EXIT=0 |
| `scripts/refactor/check_file_sizes.sh fable` | 10 files checked, **10 ok, 0 warn, 0 fail** (largest touched: `tests/test_static_ui_panels.cpp` 1123) |

**One build hit exit 143** (a full rebuild behind `inventory_layout.h`, which
is widely included). The post-143 sweep found two `build/lib/st??????` `ar`
temp files, one of them **3.19 GB** — the killed `ar` was archiving
`libwilleq_core.a` (RelWithDebInfo objects are ~50 MB each). Both deleted.
The scan found no zero-byte files, no non-ELF binaries in `build/bin` (300
checked) and no truncated objects (`objdump -h` over all 337). The archive
itself was deleted and rebuilt; **the first rebuild still failed**, with
`ranlib: error reading movement_update.cpp.o: file truncated` — a *second*
bad `ar` on the same 3.1 GB archive, not a source defect — and succeeded on
the retry with no change to the tree.
