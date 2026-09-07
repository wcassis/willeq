# T444 — doc truth sweep after T428–T443 (stale docs are a first-class defect)

Class B (docs), slot1, no server. Read `CLAUDE.md` (the doc rules and the
truth nets: `DocClaims`, `DocCounts`, `DocCitations`, `check_doc_*.py`),
the §Reviews of T428, T430, T433–T443, `README.md`, `docs/module_map.md`,
`docs/decisions.md`, `docs/testing_progress.md` (or whatever the
progress/feature docs are called — list them), `docs/future/*.md`, the
per-directory `CLAUDE.md` files touched by those units
(`include/client/graphics/ui/CLAUDE.md` panel table: options, merchant,
bank, cursor rows; `src/client/graphics/CLAUDE.md`: GroundZ, door
collision, deleted fallback mesh node / Irrlicht stubs; audio: volume
steps; spell: colour source).

1. For each doc, every claim that T428–T443 changed is corrected in
   place with the unit cited (feature tables: options window, merchant/
   bank/cursor, item icons, door collision, night base, Froglok vision,
   volume round trip, guild list if T443 lands first — check `fable` at
   checkout and say what you saw). Counts (panels, event types, popup
   enumerators, vision levels, golden sizes G05/G06) re-measured, not
   copied.
2. Stale statements found that no unit fixed (the "implemented" claims
   the T363 rule forbids, features documented but unreached) are
   boarded, not silently rewritten into truth.
3. Extend the nets: every count you corrected by hand gets a row in
   `check_doc_counts.py`; every "X is implemented" claim you verified
   gets a `DocClaims` anchor if the mechanism supports it — so the next
   drift is a red, not a re-read.
4. `docs/future/` entries already delivered (T434 options, T436
   windows, T430 icons…) are struck with the unit, not deleted.

## Proof

`ctest -j6` (doc nets green pre- and post-), the new net rows red on the
pre-fix docs (show it with an exported patch), file sizes (docs are under
the ceiling too), no code changes except the checker tables. Board: new
items B457+ (T443 in slot2 allocates from B455+; say which you used).

STOP: none expected.

## Review

Branch `task/T444-doc-truth-sweep`, base `f072f784`. Docs only plus one checker
table; **no production code changed**. The corrected-claim ledger with every
measurement is `/mnt/projects/fable/review/T444/README.md`.

### The doc set, and which half of it was already true

Listed at checkout: `README.md`, `CLAUDE.md`, `docs/CLAUDE.md`,
`docs/decisions.md`, `docs/module_map.md`, `docs/future/*.md` (8 files), and the
seven per-directory `CLAUDE.md` under `src/`, `include/` and `docker/`.

**T428–T443 largely maintained their own docs, and the plan's expected targets
were mostly clean.** The panel table in `include/client/graphics/ui/CLAUDE.md`
already carried the Options, Merchant, Bank and Cursor-item rows (T434, T436);
`GameEventType` was already 101 and `RendererIntent` 42 (T439, T436);
`src/client/graphics/CLAUDE.md` makes **no** claim about GroundZ, door
collision or the deleted fallback mesh node, so there was nothing there to
correct — its `SpellVisualFX` "no production caller" census was re-taken and is
still exactly right (`createProjectile`, `createBuffAura`, `createRainEffect`,
`createGroundCircle` are reached only from `tools/model_viewer` and tests;
T441 routed sprite names *inside* `createProjectile()` without adding a
caller).

**Where the rot actually was is not where the plan predicted.** Twenty-four
claims were wrong, and the two most valuable had nothing to do with the T428–T443
feature tables:

1. **Both `CLAUDE.md` and `docs/module_map.md` told a new agent that
   `FileSizeCeiling` is RED at this tip**, naming two `tools/hwcaps` files at
   3,280 and 3,060 lines with board B185 open. B185 is struck, those files are
   977 and 992, and `check_file_sizes.sh --all` reports 1,129 files / 0 warn /
   0 fail. This is the worst possible shape of doc rot: the orientation doc
   pre-authorises waiving a real red.
2. **`docs/module_map.md` — the doc `CLAUDE.md` tells agents to consult
   "instead of scanning" — was missing two TUs**,
   `frame/renderer_frame_options.cpp` (T434) and `player/ground_query.cpp`
   (T437/T440), from both the component lists and the TU table.

The rest: five stale asset counts in root `CLAUDE.md` (pack size 6,930,116 →
**9,726,596**; entry breakdown 68/63 → **67/64**; "60 of the 64" 32-bit → **63
of the 67**; "65 shipped BMPs" → **67**), a wrong load-timing claim in **two**
docs (the item icon sheets load EAGERLY, and T430 wrote out the measured reason
in the header), nine README claims, and two superseded numbers in
`docs/decisions.md` (T428 settled Froglok vision and closed B060, and re-solved
every vision gain that paragraph quotes from T383).

### Three findings worth carrying forward

**(a) Four README claims of drag-and-drop, and there is no drag.** "Inventory
popup … drag-and-drop", "Memorize spells … drag-to-gem", "Spell book popup …
drag-to-gem" and "Move items | Done | Drag and drop". A comment-filtered grep
for `drag` across `src/client/graphics/ui/` and
`include/client/graphics/ui/` returns exactly one mechanism: the inventory
paperdoll's rotation in `character_model_view.cpp`. `static_ui_input.cpp:839`
says so in its own words — "static UI latches no drag; board B442". All four
are corrected with B442 cited (memorising is a CLICK: `resolveMemorizeGem()`
picks the first free gem, and with all eight full it asks you to click one).

**(b) The doc nets were all GREEN while twenty-four claims were false**, and
that is the useful result, not an embarrassment — it says precisely what the
nets do not cover. `DocCitations` proves a cited path resolves; `DocClaims`
proves no retired NAME appears; `DocCounts` proves a stated NUMBER matches.
None of the three can see a doc that states a *fact* ("loads lazily",
"FileSizeCeiling is RED"), and none can see a *map* whose defect is a file it
omits, because a map states no number. Item 3 of the plan is answered against
both gaps below.

**(c) The board's own header is four days and ~60 units stale** (B458) — it
still says "Tree confirmed at `3808ebe9`, 2026-09-03" while its newest entries
cite 2026-09-07 work, and it asserts in its own words that items are confirmed
against that tip. Two lines above, the suite line already solves the identical
problem correctly by refusing to state a number and naming the measuring
command instead.

### The nets, extended (plan item 3)

`check_doc_counts.py`: **9 counts → 16**, over 32 doc sites in 12 docs.

Seven new rows, every one a count corrected by hand in this unit or one whose
neighbour drifted: `VisionType` enumerators (**4**), `kVolumeSteps` (**20**),
`OptionsLayout::kControls` rows (**114**), `kSheetCount` (**34**),
`kPopulatedIconCount` (**1200**), shipped BMPs (**67**), shipped 32-bit BMPs
(**63**). Three new measurers: `count_constexpr_int`, `count_array_rows`
(marker-based, because the control rows nest rect initialisers and a `{` count
reads 124 for 114 rows), and `count_shipped_bmps` (BMP header bpp at offset 28).
`count_constexpr_int` resolves an expression over other constants in the same
header — `kPopulatedIconCount = 33 * kCellsPerSheet - 1 + 13` — and refuses
anything that is not integer arithmetic before evaluating it.

**One new table, `COVERAGE`, for the third shape of rot.** Eight renderer
component directories now assert that every `.cpp` in them is NAMED in
`docs/module_map.md`. A count row could not have caught either missing TU
because the map states no count; a name row could not, because the name is
absent rather than retired. Both of this unit's map gaps are red under it.

**Deliberately NOT netted:** the `wc -l` figures in `module_map.md`'s TU table.
Several have drifted (physics 990 → 997, `renderer_zone_draw` 1,156 → 1,143),
but the doc's own header says they are `wc -l` at `3808ebe9` and drift with
normal work. Pinning a number that changes every unit is what
`NAMED_COMMAND` exists to prevent; the drift is disclosed, so it is not a
defect. Same reasoning left the `owner_` census alone.

### Pre-fix red / post-fix green

Doc fixes exported to a patch and reversed with `git apply -R` (never
`git stash`), leaving the new checker rows against the old docs:
**8 failures, exit 1** — four `MISSING` (the checker refuses to pass on an
absent sentence, the T37 rule), two `FAIL` on the BMP count, two `FAIL` from
the coverage table naming the two missing TUs. Patch re-applied: **exit 0**,
"OK: every stated count matches the tree." Both runs are quoted verbatim in
`review/T444/README.md`.

### Boarded, not rewritten (plan item 2)

- **B457** (P) — `include/client/graphics/vision_ambient.h` lines 35, 177, 195
  still name `0.08/0.08/0.15` as the night ambient; line 148 of the same file
  defines `kNightAmbient = {0.26244, 0.31147, 0.24003}` (T428). Solving a floor
  against those sentences repeats the mistake T428 found in T405's floors.
  Left as a board item because this unit may not change code outside the
  checker tables. **The entry also records why it survived: no doc net in the
  tree reads C++ comments.**
- **B458** (P) — the board header's stale tip, above.

Both numbers are free: T443 (slot2) allocates from B455+ and T441 took B455 and
B456, so B457 is the first unused. If T443 lands first and collides, renumber
at merge.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, slot-local Irrlicht) | exit **0** |
| `ctest --test-dir build -j6 --output-on-failure` | **4610 / 4610, 0 failed**, exit **0** |
| `DocClaims`, `DocCounts`, `DocCitations`, `FileSizeCeiling` | all four Passed inside that run |
| doc nets BEFORE any edit | all three green — which is the finding in (b) |
| `check_file_sizes.sh fable` | "no C++ files to check" — correct, nothing but docs and one Python checker changed |
| docs vs the 1,500 ceiling | all touched docs under it; largest `README.md` 859 |
| visual gate / build-gles2 / ARM | not run — no code, shader, asset or generator changed |
| eqemu | never contacted; T438 holds the lock |

**Deviation (flagged, not absorbed):** `refactor/BOARD.md` is 7,668 lines, over
the 1,500 doc ceiling. It is outside `check_file_sizes.sh --all`'s scan (which
covers `src/ include/ tests/ tools/ benchmarks/`) and is an append-only ledger
that the plan's "docs are under the ceiling too" was not aimed at. T444 added
two entries and did not split it; if the ceiling is meant to bind the board,
that is a unit of its own.

**Deviation:** two ctest runs died at gtest DISCOVERY on two DIFFERENT binaries
(`test_daybreak_harness`, then `test_spell_database`) under load average 27 on
24 cores. Both are valid ELFs that list their tests; a listing pass over all
291 test binaries found no truncation (the only two that do not list are
`test_gl_points`, which needs EGL, and `test_music_loopback_compare`, which has
its own CLI — neither is a gtest discovery target). Two different victims in
two runs is contention, not damage. Re-run after warming: 4610/4610.

**Deviation:** the plan expected corrections in `src/client/graphics/CLAUDE.md`
(GroundZ, door collision, deleted fallback mesh node) and volume claims in
`src/client/audio/CLAUDE.md`. Neither doc made those claims at all. The
graphics doc was re-verified and left untouched; the audio doc's silence about
T439's round trip was a documented GAP, so a verified section was added rather
than a claim corrected.

### Docs touched

| doc | claims corrected |
|---|---|
| `README.md` | 9 |
| `CLAUDE.md` | 7 |
| `docs/future/ui_fixes.md` | 4 struck as delivered (T290/T430/T434/T436) + 1 marked NOT delivered |
| `docs/module_map.md` | 4 |
| `docs/decisions.md` | 2 |
| `include/client/graphics/ui/CLAUDE.md` | 1 |
| `src/client/audio/CLAUDE.md` | 1 documented gap filled |
| `refactor/BOARD.md` | 2 new entries |
| `scripts/refactor/check_doc_counts.py` | 7 rows + 3 measurers + the COVERAGE table |

Read, verified and left unchanged because they were already true:
`docs/CLAUDE.md`, `docker/CLAUDE.md`, `src/client/input/CLAUDE.md`,
`include/client/state/CLAUDE.md`, `include/client/bridge/CLAUDE.md`,
`src/client/graphics/CLAUDE.md`, and the seven other `docs/future/*.md`.

**T443 had not landed at this tip** (`f072f784` is the T444 plan commit; T443
and T438 are plan-only), so no guild-list claim was written — the plan's
"if T443 lands first" arm did not fire.
