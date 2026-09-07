# T445 — the detail view distance drives the chunk radius (B336); comment and board-header rot (B457, B458)

Class B, slot1, no server. Read B336 (`ZoneDetailConfig::viewDistance`
parses and nothing reads it; `DetailManager` culls by a hardcoded
`viewDistanceChunks_ = 2`; 200/50 = 4 chunks = a 9x9 grid — "changes both
the visual gate and the frame cost"), T414 Leg C's §Review, B457 (three
comments in `vision_ambient.h` still quote the pre-T428 night triple),
B458 (BOARD.md header "confirmed at 3808ebe9"), T444's §Review (no net
reads C++ comments).

1. B336: `viewDistanceChunks_ = ceil(viewDistance / chunkSize)` from the
   configured value; pre-fix failing test on `DetailManager` with a
   config of 200 → 4 chunks, 100 → 2. THEN MEASURE: run the visual gate
   standalone. If any image golden moves, STOP with the contact sheet
   (before/after per zone) and the CPU-p95 delta — Will decides whether
   the 9x9 grid is the intended look (the shipped 200 was never
   exercised; the oracle's own detail draw distance is unmeasured — say
   so). Do NOT change the shipped 200 to keep the gate green; that is
   choosing the golden over the config.
2. B457: rewrite the three comments to name the constant they sit
   beside (no literal), and add the check T444 said does not exist: a
   `check_doc_counts.py`-style row (or a tiny test) that greps
   `vision_ambient.h` comments for the retired triple `0.08` `0.15` —
   pre-fix red.
3. B458: the board header's "confirmed at" line becomes the suite-line
   shape two lines below it (or is deleted as duplicate) — say which.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
visual gate standalone + ARM, file sizes. Strike B336/B457/B458 (B336
only if the gate holds or after the STOP is answered). New items B459+
(T443 in slot2 allocates from B455+ — say which you used).

STOP: an image golden moves (B336) — expected; report, do not bless.

## Review

Branch `task/T445-detail-distance`, base `fa11c2bc`. Slot1, RelWithDebInfo,
no server (T438 held the eqemu lock throughout and was not touched).

### 1. B336 — the view distance drives the chunk radius

`detailChunkRadius(viewDistance, chunkSize)` in
`include/client/graphics/detail/detail_types.h` is `ceil(distance / chunkSize)`
clamped to at least 1 (a zero radius leaves only the camera's own chunk; a
non-positive chunk size has no answer and must not divide by zero).
`DetailManager::setZoneConfig()` is the single place the radius is set, and
both `loadZone()` branches — the JSON config and the hardcoded default — route
through it, so a caller cannot install a config without the radius following
it. `getViewDistanceChunks()` exposes it.

`tests/test_detail_view_distance.cpp` (new, 4 cases, registered in
`tests/cmake/graphics.cmake`):

| case | pre-fix | post-fix |
|---|---|---|
| `RadiusIsTheDistanceInChunksRoundedUp` | PASS | PASS |
| `DegenerateConfigFallsBackToOneChunk` | PASS | PASS |
| `ManagerDerivesItsRadiusFromTheInstalledConfig` | **FAIL** (expected 4, got 2) | PASS |
| `ShippedConfigYieldsASevenBySevenGrid` | **FAIL** (expected 3, got 2) | PASS |

The pre-fix run was taken by reverting the one derivation line via an exported
patch (`git diff` to a scratch file, edit, rebuild, run, restore, `diff` the
patch back to identity — never `git stash`, which is shared across slots).

**Two corrections to B336's own text, both measured:**

- **The shipped value is 150, not 200.** `config/detail_objects.json` has said
  `globalSettings.viewDistance: 150.0` since it was written;
  `git log -S'"viewDistance": 200'` on that file returns nothing. So the grid
  goes 5x5 (25 chunks) -> **7x7 (49)**, not 9x9. `detail_zone_configs.cpp` and
  `DetailConfigLoader::getHardcodedDefault()` both also say 150, so every path
  agrees.
- **"changes both the visual gate and the frame cost" is FALSE on the gate
  half.** The gate runs `build-gles2/bin/gles2_zone_benchmark`, and
  `tools/hwcaps/gles2_zone_benchmark.cpp` + `zone_bench_loader.cpp` contain
  **zero** occurrences of `DetailManager` or `detail`. It never builds a chunk.

### 2. The gate: no golden moved, and the STOP did not fire

**No image golden moved — `tests/goldens/` is untouched** (`git status
--porcelain -- tests/goldens/` is empty). The plan's STOP condition
("an image golden moves") did not trigger, so nothing was blessed and nothing
needs Will's decision on the pixels.

The proof is stronger than nine unchanged PNGs: the gate's per-frame CSV
records `draws`, `polys` and `regions_drawn`, and those are **byte-identical
before and after in all three zones and all nine frames** (table in
`review/T445/README.md`; raw CSVs in `review/T445/csv_before` and `csv_after`).

**CPU-p95: the requested delta could not be taken honestly and I am reporting
that rather than inventing a number.** Three executor slots ran in parallel
all unit and host load moved between 12 and 47 on 24 cores. Six standalone
gate runs, three per binary, alternating rebuilds:

| binary | run | load | qeynos2 | gfaydark | befallen | verdict |
|---|---|---|---|---|---|---|
| pre-fix 5x5 | 1 | 28.8 | 5.233 | 9.157 | 7.007 | PASS |
| pre-fix 5x5 | 2 | 19.6 | 4.885 | 7.566 | 5.475 | PASS |
| pre-fix 5x5 | 3 | 17.6 | 4.420 | 7.375 | 7.565 | PASS |
| post-fix 7x7 | 1 | 17.0 | 6.802 | 6.713 | 7.798 | PASS |
| post-fix 7x7 | 2 | 30.7 | 13.218 | 8.824 | 8.936 | **FAIL** |
| post-fix 7x7 | 3 | 43.5 | 7.458 | 9.055 | 6.460 | PASS |

The same binary spans 4.42–13.22 ms on qeynos2 against a 10.108 ceiling. That
spread tracks host load, not the code, and it dwarfs anything this change
could do. Combined with bit-identical geometry and a harness that never
constructs a chunk, **the true gate CPU delta is zero** and every FAIL above is
the load artefact `EXECUTOR_PROTOCOL.md` warns about — reproduced here with a
STANDALONE run rather than under `ctest -j6`, which is worth recording: the
protocol's "re-run standalone" remedy is not sufficient when the contention is
other slots rather than the test harness.

**Final gate: PASS** — qeynos2 7.229, gfaydark 8.180, befallen 6.296, all
under ceiling, against a `build-gles2` rebuilt from the committed tip.

**Deviation / gap flagged:** the real frame cost of the 7x7 grid is
**UNMEASURED**, and no instrument in the tree can measure it. Boarded as
**B460**. The shipped 150 was NOT changed to keep anything green (there was no
green to keep), and the oracle's own detail draw distance is still unmeasured
— 150 is fable's number, not EverQuest's.

**`WriteOnlyFields` caught the other half of the close.** The suite went red on
`STALE exemption (no longer write-only): ZoneDetailConfig::viewDistance`
— T414 Leg C's ratchet doing exactly its job. The row was removed from
`check_write_only_fields.py`; the census is now 66 fields, all classified.

### 3. B457 — the rotted vision comments, and the net that did not exist

All three comments (lines 35, 177, 195) now name `kNightAmbient` /
`kDayAmbient` instead of the triple, and the file carries a
DO-NOT-WRITE-THE-LITERAL banner explaining why, with the T428/T405 history
that makes it load-bearing.

The check T444 said did not exist is
`scripts/refactor/check_comment_claims.py`, registered as ctest
**`CommentClaims`** (label `hygiene`) in `tests/cmake/hygiene.cmake`. It is the
C++-comment analogue of `DocClaims`: a table of (file, retired pattern, truth,
unit) rows matched against **comment text only**, via a hand-rolled scanner
rather than a regex so a string literal cannot open a comment and the
constant's own definition is never a hit. Same `RETIRED-OK` line exemption;
also fails if a row names a file that no longer exists, so an exemption cannot
outlive its file.

| run | result |
|---|---|
| pre-fix (`git show fa11c2bc:…vision_ambient.h`) | **FAIL, 3 hits: lines 35, 177, 195** |
| post-fix | PASS (1 file, 227 comment lines, 0 exempted) |

The table deliberately holds **one row**, and the script says so: it is a
ratchet for constants that have actually moved and been caught, not a sweep
over every literal in a comment. Pinning all of them would fail on legitimate
tombstones — this same file has two (the pre-T428 dawn/dusk step, the pre-T405
Infravision floor).

**A finding while rewriting them, boarded as B459.** The line-33 claim that
the day ambient is "already above every floor here" is **wrong**. The floor in
force is gain x tint (`vision_ambient.cpp`), which puts Ultravision at
(0.5132, 0.5141, 0.4922) and Froglok at (0.5059, 0.5134, 0.4677) against
`kDayAmbient` 0.5 — R and G are ABOVE it, so both buffs lift a noon frame by
~0.014 (~2.6% on R). Only Infravision (0.4481, 0.4320, 0.3639) is genuinely
inert by day. The sentence is corrected in place; no constant was touched,
because the file's own rule is that a constant moves only when a live session
says so, and whether the oracle has that daylight lift is unmeasured.

### 4. B458 — the board header

**Which option: the hash is GONE, replaced by the measuring command.** The
"Tree confirmed at" line now reads like the suite line two lines below it —
"DO NOT QUOTE A HASH FROM THIS LINE", naming
`git -C <slot> log --oneline -1` — and records that a hash sat there unchanged
for roughly sixty units. Restamping was rejected: restamping is exactly what
sixty units did not do, so it is a rule with a demonstrated 0% compliance rate.

Two consequential edits fell out. The Date line now says built-then-amended
instead of pretending to one stamp. And the "items are confirmed against the
tree at the tip above" bullet was rewritten, because with no tip above it, it
pointed at nothing — each item names the unit that confirmed it, and that
unit's merge is its tip.

Ratcheted in `check_doc_counts.py` two ways: a `NAMED_COMMAND` row for the
command, and a `FORBIDDEN` row whose regex matches
`**Tree confirmed at:** `<hash>``.

| run | `DocCounts` |
|---|---|
| pre-fix board | **FAIL: `refactor/BOARD.md:3` restates a count that must not be pinned** |
| post-fix | PASS (16 counts, 32 sites, 12 docs, 5 not-a-number, 2 forbidden-restatement) |

Honest note on the `NAMED_COMMAND` row: it was ALSO green pre-fix, because the
command string appeared inside B458's own body. The `FORBIDDEN` row is the one
doing the work; the `NAMED_COMMAND` row guards against the line being deleted
outright.

### 5. Incidental: a miscited board id

`check_write_only_fields.py`'s exemption for
`TradeskillContainerClosedEvent::wasWorldObject` cited **B336** — the detail
view distance — for a claim about tradeskill containers. The board has exactly
one B336 and it is the detail one; the container work is **B321**. Corrected,
because leaving a dangling wrong citation to an item this unit just struck is a
trap for the next reader.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 (background + exit file; no exit-143 at any point, so no truncation scan needed) |
| `ctest -j6` | **4,614 / 4,615**. The single red was `WriteOnlyFields`, the stale B336 exemption above; fixed and re-run green standalone (66 fields, all classified). No other failure. |
| `scripts/build-gles2-x86.sh` | exit 0 |
| Visual gate, standalone | **PASS**, goldens untouched |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | 46 files checked, 46 ok, 0 warn, 0 fail |
| `CommentClaims` (new) | pre-fix FAIL (3), post-fix PASS |
| `DocCounts` B458 ratchet (new) | pre-fix FAIL, post-fix PASS |

### Board

Struck: **B336**, **B457**, **B458**. Added: **B459** (C — daylight
Ultravision/Froglok lift, needs a live A/B), **B460** (B — the detail system
has no coverage in any performance lane). T443 in slot2 allocates from B455+,
so if it also used 459/460 they are renumbered at merge.

Review media: `/mnt/projects/fable/review/T445/` (README, both gate logs, the
three-repeat load tables, before/after per-frame CSVs). No screenshots — the
frames are byte-identical to the committed goldens, so a before/after image
pair would be two copies of the same picture.
