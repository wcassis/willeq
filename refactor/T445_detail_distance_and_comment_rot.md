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
