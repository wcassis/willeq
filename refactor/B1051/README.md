# B1051 XU4 texture investigation (incomplete)

## Attempt on 2026-10-13

The XU4 was reachable and its GPU governor was `simple_ondemand` before and
after the measurements. The runs used the existing fable ARM publish and
`~/willeq/play.sh`; they did not change `~/willeq/*`, packages, kernel modules,
or the governor. Raw logs and PNGs are under
`/home/user/kb-evidence/kb-b1051-panfrost-npc-textures/` on hermes-ext.

The published `gles2_zone_benchmark --help` has neither `--constrained` nor
`--dump-frames-dir`; its supported screenshot flags are `--dump-frames` and
`--dump-png-prefix`. It defaults to zero entities. The qeynos2 and ecommons
attempts explicitly placed 24 synthetic entities at 1280x720 and captured
frames. The qeynos2 run logged `[texture-cache] ... evictions=0` and
`[impostor-timing] ... created=0` (`qeynos2/baseline.log`); ecommons also
logged zero evictions and zero impostors (`baseline.log`). The synthetic
entities in those captures use diagnostic rainbow coloring, so their pixels
cannot be scored as the reported live NPC texture defect.

An explicit impostor scene on qeynos2 (`--adaptive-lod
--adaptive-tier-dist 80 --impostor-scene far`) created 24 impostors with
`incomplete_fbo=0` and zero texture-cache evictions (`baseline.log` in the
evidence root). Its synthetic rainbow-colored entities still cannot establish
whether real NPC textures are correct. A textured race-1 ETC1 entity-viewer
capture (`etc1_1_front.png`, `run.log`) showed a populated texture, but that
single model is not the affected live scene.

The live `--scenario zone_in` run passed in qeynos, but the process exited before
it wrote a captured frame (`live/run.log`). A subsequent 35-second run through
`~/willeq/play.sh --dump-frames-dir /tmp/b1051/live --dump-every 30
--dump-max-frames 20` wrote ten frames. Each sampled frame looks straight at
a nearby wall; no NPC is visible (`live/frame_000000.png`,
`live/frame_000005.png`, `live/frame_000009.png`). The client entered qeynos,
not qeynos2 (`live/standing.log`). A pixel-counting script over these frames
would measure the wall, not silhouettes, so no artefact count is claimed.

## Retry on 2026-10-13

A scripted `/say #zone qeynos2` reached qeynos2 (`retry/run.log`), but the
safe-point capture again contained no NPC in view (`retry/frames/`). A second
script set daylight, hid the UI, and swept four headings from the fixed
`#goto -74 428 3.751` pose (`sweep/tour.txt`, `sweep/run.log`). One sampled
view contains the named NPC Grennet Stormhand
(`sweep/frames/frame_000007.png`); the other sampled views face a wall or
contain no clearly visible NPC. That one distant NPC is not a reproduction of
Will's reported *many* corrupted and all-black NPCs. No pixel threshold or
crop-based count from this sample can classify the reported defect reliably.
No knob comparison was run against this inconclusive baseline.

The sweep sent `/say #zone qeynos` before exit. A separate verification run
entered qeynos and reported location `10.0, 0.0, 5.0` (`retry/verify.log`).
All retry logs and frames are under the same evidence root named above. Scratch
files on the XU4 were removed after transfer; `~/willeq/*` and the GPU
governor were not changed.

## Not verified / left open

- No reproduction still containing a corrupted or black NPC; no scripted NPC
  artefact count.
- No valid one-knob table or Mesa mechanism citation: without a failing
  baseline, a knob comparison would not isolate the defect.
- No fix, `xu4` preset proposal, or after-still. Neither candidate mechanism
  has been established on the device.
- No ARM build proof for a change; this attempt changes documentation only.

Next run needs the exact affected NPC group and camera pose from Will's play
session, or another deterministic capture containing visibly affected NPCs,
before modifying client code or proposing preset values. The retry's one
distant NPC does not establish a failing baseline.
