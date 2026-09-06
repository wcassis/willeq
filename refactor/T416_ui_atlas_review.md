# T416 — the UI now draws its atlas with alpha: capture every panel and fix what is visibly wrong (B316)

Class B, slot3, no server. Read B316 (77% of `ui_atlas` texels are
non-opaque and 64% fully transparent; the UI drew them opaque until
T411; no gate golden photographs UI), T411's §Review, the headless UI
capture tool (`tools/hwcaps/ui_inventory_capture.cpp` — T393/T409 used
it) and the static-panel list (`static_panels.cpp`: inventory, spellbook,
skills, group, pet, buff bar, casting bar, chat, hotbar, target, …).

1. Extend (or sibling) the capture tool to render EVERY static panel and
   popup headlessly at 800x600 into `review/T416/after/<panel>.png`;
   build the same binary with the pre-T411 shaders? — no: instead render
   the same panels against the PRE-T411 pack (`review/T411/` holds it) via
   `--eqe1-path` for `review/T416/before/`. Contact sheet before/after per
   panel.
2. For each panel, compare against the oracle: the official window's
   look from its XML pieces (`EQUI_*.xml` + `window_pieces01.tga`
   decals) and any oracle screenshot under `review/T3xx/oracle/`
   (T377 has Options/Keys, T386 the inventory, T401 the group window).
   Table: panel → before/after → oracle → verdict (matches / visibly
   wrong / no oracle).
3. Fix the visibly-wrong ones that are mechanical (a sprite region whose
   alpha now punches a hole where the oracle draws a filled frame — pick
   the right atlas region / add a filled backing; a text over a now-
   transparent box that the oracle draws opaque). Each fix pinned by the
   static-panel seam (pre-fix failing) and re-captured. Anything that
   needs a design call → board (B350+) with the crop.
4. Add a `UiPanels` capture to the visual gate? NO — board it as a
   proposal (B350+): the gate is Will's to extend. Strike or annotate
   B316.

## Proof

Build (background + exit file), `ctest -j6`, pre-fix/post-fix, visual
gate standalone + ARM (no image golden should move — the gate opens no
UI), file sizes; the contact sheets in `review/T416/`.

STOP: none expected.

## Review

Branch `task/T416-ui-atlas-review`, base **`6522c387`** (slot3,
RelWithDebInfo). No server touched (T408 held the eqemu lock). `nm` on
slot3's `libIrrlicht.a` showed `_Z22gles2SetAlphaCompanionPvS_` **defined**
(`T`), so T411's driver was already in this worktree and no
`--irrlicht-only` rebuild was needed.

### 1. The harness

`tools/hwcaps/ui_panel_capture.cpp` (401 lines, new, `ui_panel_capture`
target) is a SIBLING of `ui_inventory_capture`, not an extension of it — that
file is already 648 lines and renders only the inventory popup plus one
combined atlas frame. The new tool renders **every** static panel and popup
alone, through a real `UIAtlas`, at 800x600, onto a **16px checkerboard**: the
checker is the whole point, because a region that is now 70% opaque is
indistinguishable from a dark opaque region against black.

Sixteen captures: the fourteen panels in `static_panels.h` (`player_status`,
`target_info`, `chat`, `hotbar`, `spell_gems`, `buff_bar`, `casting_bar`,
`group`, `pet`, `xp_bar`, `inventory`, `spellbook`, `skills`, `loot`), the
group window's no-group shape (`group_nogroup`, T382's title-only strip), and
`hud_all` — everything a player sees at once.

**How "before" was produced, and why the plan's own suggestion does not work.**
The plan proposed rendering against the pre-T411 pack via `--eqe1-path`. That
alone reproduces nothing: T411 changed the SHADER, and the current `UI2D_FS`
samples the ETC1 `_alpha` companion, which the pre-T411 pack still carries —
so the pre-T411 pack renders with its alpha live. The tool therefore also
takes `--opaque-alpha`, which calls `gles2SetAlphaCompanion(tex, nullptr)`
after `UIAtlas::load()`; unit 1 then falls back to the driver's 1x1
opaque-white and `UI2D_FS` reads alpha 1.0 for an ETC1 texture — exactly the
pre-T411 sampler. Paired with the pre-T411 pack that is the real "before".
Against the post-T411 pack the flag is a documented no-op (`ui_atlas` carries
its alpha in band there, on unit 0). `--eqe1-path` now accepts the pack FILE
as well as its directory, so `review/T411/willeq.eqe1.pre-T411` is named
directly and read-only.

### 2. The panel table

`before` = pre-T411 (alpha ignored). `after` = T411 as merged, at base
`6522c387`. `fixed` = this branch. Media:
`review/T416/{before,after,fixed}/<panel>_800x600.png`, three-way contact
sheets in `review/T416/crops/<panel>_3way.png`. "changed" is the fraction of
the 800x600 frame whose RGB moved by >8 (sum over channels), before→after.

| panel | before | after (T411) | changed | oracle | verdict |
|---|---|---|---|---|---|
| `player_status` | opaque plate + 3 bars | plate and bar troughs 70–78% opaque | 1.02% | none for fable's own HUD shape | look changed, nothing broken → B350 |
| `target_info` | opaque plate + bar | same, translucent | 0.67% | none | look changed; **"63%%" label** → fixed |
| `chat` | opaque black box | box 70% opaque, text unaffected | 9.33% | official chat window IS translucent | look changed, arguably closer to oracle → B350 |
| `hotbar` | 10 slots, opaque | slots keep 78% backing, 32x32 borders crisp | 2.42% | none | matches; 1:1 sprite, no mip loss |
| `spell_gems` | 8 gems, opaque column | column 70%, gem borders unchanged | 1.67% | none | matches; pinned by the 3 T272/T275 gem cases |
| `buff_bar` | opaque strip + icons | strip 70%, icons unchanged (they are `drawRect`) | 2.29% | none | matches |
| `casting_bar` | opaque trough + fill + frame | trough 78%, frame intact | 0.31% | `EQUI_CastingWindow.xml` gauge | matches |
| `group` | opaque plate + rows | plate 70% | 2.68% | `review/T377,T386/oracle` | look changed; **"100%%" on every row** → fixed |
| `group_nogroup` | opaque title strip | strip 70% | 0.53% | T377 oracle: title bar only | matches (T382's shape held) |
| `pet` | opaque plate | plate 70% | 1.00% | `EQUI_PetInfoWindow.xml` | **"88%%"** → fixed; button row overflows the frame and the 10th button is clipped (pre-dates T411) → B352 |
| `xp_bar` | opaque trough + purple fill | trough 78% | 0.51% | none | matches |
| `inventory` | opaque popup, slots opaque | popup 70%, slots 78% | 28.01% | `T401_oracle_inventory_window_crop.png` — OPAQUE | **coin-row frames collapsed** → FIXED; popup opacity → B350 |
| `spellbook` | opaque popup + rows | popup 70% | 19.73% | `EQUI_SpellBookWnd.xml` | look changed; memorise gauge intact |
| `skills` | opaque popup + rows | popup 70%, rows readable | 28.37% | none | look changed → B350 |
| `loot` | opaque popup + rows + buttons | popup 70% | 28.29% | `T377_oracle_corpse_loot_coin` | look changed → B350; buttons are `drawRect`, not the atlas's `button_*` → B351 |
| `hud_all` | — | — | 21.80% | — | the composite of the above |

### 3. What was fixed, and what it was pinned by

**(a) The purse's coin-row frames — the one thing the alpha actually broke.**
`renderInventoryPopup` drew each coin row's frame as
`drawSprite(row, SlotBorderNormal)`. That sprite is a 1px ring inside a 32x32
tile, and a coin row is **65x18** at 800x600 (`ORACLE_COIN_W/H` 70x24 scaled
into a 400x350 popup) — the **only** place in the whole UI where a border
sprite is drawn onto a rect SMALLER than its tile. The 32→18 minification
selects mip level 1, where the ring has averaged into its own transparent
interior. While the atlas drew opaque that still read as a box (a washed-out
ring on an opaque black tile, edge 49 vs interior 0); with the alpha live it
collapsed to **edge 37 vs interior 18** — a 17/255 step, and the row stopped
reading as a box at all. The oracle's IW_Money buttons are framed plates
(`review/T401/oracle/T401_oracle_purse_coinrows_crop4x.png`).

Fixed by drawing the frame from the uniform 4x4 `white` sprite — every mip of
which is still white, which is why `drawRect` has never had this problem —
through a new `UIRenderer::drawFrame(rect, colour, thickness=1)`, in the same
`{80,80,80}` grey `ui_atlas_builder` gives the ring.

`drawFrame` is also the one body for something **five** call sites had each
open-coded as the same four `drawRect`s in the same order: `drawPanel`, the
casting-bar border, a buff icon's border, a spell gem's state border and its
2px memorize marker. The migration is provably pixel-identical — `after` vs
`fixed` differs by **0.000%** on `spell_gems`, `casting_bar`, `buff_bar`,
`group`, `pet`, `player_status`, `target_info`, `skills`, `spellbook`, `loot`,
`hotbar`, `xp_bar` and `group_nogroup`, and by 0.151% on `inventory` (the coin
rows, i.e. the fix). `chat`/`hud_all` differ by 0.002%/0.008% because the chat
input cursor blinks on wall-clock time.

Pinned by **`UIAtlasCoinRowFrameTest`** in `tests/test_ui_atlas_pixels.cpp`
(the existing real-GLES2-device harness):

| test | pre-fix | post-fix |
|---|---|---|
| `EveryCoinRowPaintsAFullBrightFrame` (all 4 rows, all 4 edges > 60) | **FAILED** — every edge 37 | PASSED |
| `TheFrameStandsOutAgainstTheRowInterior` (edge − interior > 30) | **FAILED** — 37 vs 18, delta 19 | PASSED |

Pre-fix proof taken by exporting the `static_panels.cpp` diff to a patch,
`git apply -R`, building `--target test_ui_atlas_pixels`, running, then
`git apply` (never `git stash` — it is shared across worktrees).

**(b) `fmt::format("{}%%", pct)` — the health label printed the sign twice.**
Found by eye in the captures (`review/T416/crops/group_3way.png`): every group
member row read **"100%%"**, the pet panel **"88%%"**, the target panel the
same. `fmt` is not `printf` — a literal `%` is copied through verbatim. Three
call sites in `static_panels.cpp` now go through one `percentText(uint8_t)`,
the `coinRowText()` idiom this file already uses so a test can assert the
drawn string with no device. The same bug in the same shape was in
`src/client/bridge/console_bridge.cpp`'s `TargetChanged` arm
(`"HP={}%%"`, a `LOG_INFO`, which also formats through fmt) and is fixed
there too — **a deviation from the plan's file territory**, flagged rather
than absorbed: one line, no other unit is in that file, and leaving a known
one-character defect behind would have needed its own board entry.

Pinned by **`StaticPanelPercentLabelT416`** in `tests/test_static_ui_panels.cpp`
(device-free): `PercentTextPrintsExactlyOneSign` and
`PercentTextNeverDoublesTheSign` (all 256 values). Both **FAILED** with the
body reverted to `"{}%%"` (`percentText(255) = '255%%'`), both pass now.

### 4. Boarded, not fixed

- **B350** (C) — the whole UI is now 70–78% opaque and the oracle's windows
  are opaque. A LOOK decision (one alpha per sprite in `ui_atlas_builder` plus
  a pack rebuild and two sha256 pins), so nothing was changed.
- **B351** (B) — six of twenty-one `UISprite` regions have no consumer:
  `ButtonNormal`, `ButtonPressed`, `ButtonDisabled`, `PanelBorder`,
  `ScrollIndicator`, `SlotBorderSelected`. The loot buttons and the chat
  scrollbar thumb are hand-drawn `drawRect`s that should be two of them.
- **B352** (B) — the pet button row hangs 8px below its panel and the tenth
  button is silently clipped. Visible in all three columns, so it pre-dates
  T411; the honest fix is the oracle's `EQUI_PetInfoWindow.xml` grid.
- **B353** (P) — the gate should photograph the UI (plan step 4 said board it,
  not do it: the gate is Will's).

### 5. Verification

| step | result |
|---|---|
| `cmake --build build -j24` (background + exit file) | **exit 0**, no exit-143 anywhere in the unit |
| `ctest --test-dir build -j6` (first run) | **4,272 / 4,273** — `WriteOnlyFields`, red at the tip, see below |
| `ctest --test-dir build -j6` (final run) | **4,271 / 4,273** — `WriteOnlyFields` green; `PackReproducible` and `visual_gate` red because **another slot replaced the shared pack at 13:32**, see below |
| `bash scripts/build-gles2-x86.sh` | exit 0 |
| `bash scripts/ci/visual_gate.sh` (standalone) | **PASS** — qeynos2 5.337/10.108, gfaydark 6.411/10.672, befallen 5.491/10.948; `git status tests/goldens/` empty, **no image golden moved** (as expected: the gate opens no UI) |
| `bash scripts/build-arm-noble.sh` | exit 0, `build-arm-noble/bin/willeq` ELF 32-bit ARM |
| `bash scripts/refactor/check_file_sizes.sh fable` | 26 checked, **26 ok, 0 warn, 0 fail** (largest touched: `test_static_ui_panels.cpp` 1,149; `static_panels.cpp` 1,098; the new tool 401) |

**`WriteOnlyFields` was RED at the tip `6522c387`, and it is not T416's.**
`scripts/refactor/check_write_only_fields.py`'s `EXEMPT` table still carried
four rows T413 had made stale — `CastingStateChangedData::castTimeRemainingMs`
and `::castTimeTotalMs`, `CursorItemChangedData::queueSize`,
`SpellMemorizingData::progressMs` — and the checker fails on an exemption
whose field is no longer write-only, by design ("the table is not allowed to
outlive its rows"). Proof that it pre-dates this branch:
`git show fable:src/client/graphics/ui/static_panels.cpp` already contains the
reads (`applyCastingStateChanged`, `applySpellMemorizing`), and nothing in
T416's diff touches `event_bus.h` or those bodies. The four rows are DELETED
here — the tool's own instruction — and the checker then reports
`write-only census clean: 79 fields, all 79 classified in EXEMPT`.

**`PackReproducible` and `visual_gate` went red MID-UNIT because another slot
replaced the shared pack, and neither is T416's.**
`/mnt/projects/EverQuest_ETC1/willeq.eqe1` was **5,532,254 bytes /
`aca03e7f…`** for the whole of this unit's work and was overwritten at
**13:32:58** with **5,881,918 bytes / `584ba1fe…`** — 349,664 bytes larger.
The evidence:

- `PackReproducible` reports `built == pinned == aca03e7f…` and
  `shipped == 584ba1fe…`; slot3's inputs still rebuild the pinned pack
  bit-for-bit, so nothing on this branch changed a pack input.
- `visual_gate` fails ONLY on `eqe1MappedBytes`, by **+349,664 in all three
  zones** — the pack's size delta exactly, and the identical-deficit-in-every-
  zone shape `refactor/BOARD.md` already names as a pack/tree mismatch rather
  than a defect. Every CPU p95 is inside its ceiling in that same run.
- Both are timestamps, not opinions: every capture in `review/T416/` was
  written at 12:03–12:46 and the **standalone** `visual_gate` PASS was taken
  before 13:00, all against `aca03e7f…`. `git status tests/goldens/` is empty.

Whoever owns that pack owes the `data/willeq.eqe1.sha256` pin in the same
commit. T416 changed no pack input and re-pinned nothing.

STOP conditions: none hit. No image golden moved, so no bless is requested.
