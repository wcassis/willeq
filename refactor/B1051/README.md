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

## Not verified / left open

- No reproduction still containing a corrupted or black NPC; no scripted NPC
  artefact count.
- No valid one-knob table or Mesa mechanism citation: without a failing
  baseline, a knob comparison would not isolate the defect.
- No fix, `xu4` preset proposal, or after-still. Neither candidate mechanism
  has been established on the device.
- No ARM build proof for a change; this attempt changes documentation only.

Next run needs a deterministic camera/character position at the reported
qeynos2 gate, or a live capture with visible affected NPCs, before modifying
client code or proposing preset values.
