# T412 — three inert knobs from the field census (B291, B292, B294)

Class A ×3, slot3, no server. Read the three board entries, T407's
§Review (the census script and the exemptions), and the consumers.

## Leg A — B291 `skipTextureUpload_`

The constrained-mode knob is stored and never read; entity texture
upload happens regardless. Read what the knob was meant to skip (the
config comment, the orangepi preset), consume it at the upload site, and
test (pre-fix failing): with the flag set, no entity texture upload
happens (count via the texture cache's stats seam); with it clear, it
does. The orangepi preset's value decides what the Pi does — say what it
is and whether the memory gate's coverage run exercises it.

## Leg B — B292 `cacheMaxBytes_`

The SFX cache has no ceiling: `setCacheMaxBytes()` stores a budget nothing
compares against. Implement eviction (LRU — the chr-cache store from
T364/T366 is the shape) when `cacheSizeBytes_` would exceed it; test
(pre-fix failing): load N sounds past the ceiling → the total stays under
it and the least-recently-used is gone; a sound in use is not evicted
(read how "in use" is known — a playing channel).

## Leg C — B294 `forcePvsUpdate_`

Set in four places to force a PVS recalculation, read nowhere: a
render-distance change (and the other three writers) never forces the
recompute. Find the PVS update site and consume the flag (or, if the PVS
now recomputes on a different trigger that already covers the four
writers, delete the flag and the four writes with the proof). Test
(pre-fix failing): after a render-distance change, the next frame
recomputes the PVS (the frame order golden G05 pins statements — learn in
place if one is added).

## Proof

Build, `ctest -j6` (expected reds: only the shared-pack consumers while
T411 is mid-replacement), pre-fix/post-fix, visual gate standalone + ARM
(Leg A/C touch the render path; a moved image golden is a STOP with the
contact sheet), file sizes. Strike B291, B292, B294.

STOP: Leg A's upload site cannot be skipped without breaking entity
rendering (then the knob's intent is wrong — report and board).
