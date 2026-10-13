# B1051 XU4 texture investigation (not reproduced)

## Take 2: Will's saved qeynos position, 2026-10-13

Will's `~/willeq/play.sh` logged Testchar into qeynos using the existing
setcap binary and config. The only client additions were `--dump-frames-dir`,
`--dump-every`, `--dump-max-frames`, and `--log-level=INFO`. A temporary copy of
`scripts/pi/uinput_drive.py` created the virtual evdev keyboard before the
client started, dismissed the initially open Tip and Options windows, held D
for 4 seconds (the client's 90 degrees/s turn setting), and typed `/q`. No
movement keys were sent. The first two runs timed out with UI panels still
open and no `/q` command observed in the log; the final burst exited cleanly
with code 0. These deviations
are visible in each `keys.tsv` and `client.log`.

Raw evidence is under
`/home/user/kb-evidence/kb-b1051-panfrost-npc-textures/` on hermes-ext:

| Run | Capture settings | Frames | Persistent dark proxies | High-variance proxies | Validity |
|---|---|---:|---:|---:|---|
| `take2` | 1280x720, every 15, cap 120 | 56 | 132 | 1193 | Options and Tip cover the entity band; one normally textured Gash Flockwalker is visible on the bridge. |
| `take2_1024` | 1024x768, every 15, cap 120 | 49 | 58 | 955 | Tip closed, Options still covers the band; no affected NPC visible. |
| `take2_burst` | 1280x720, every 1, cap 60 | 46 | 74 | 406 | Popups closed, but chat input held focus and the turn did not occur; wall-only view. |
| `take2_burst_retry` | 1280x720, every 1, cap 60 | 12 | 16 | 99 | Popups and chat closed; D turn occurred, but available frames show bridge edge/sky and a building wall, not an affected NPC. `/q` exited cleanly. |

The counts are sums of the per-frame `counts.tsv` columns, **not counts of bad
NPCs**. `scripts/sbc/xu4/count_npc_artifacts.py` uses the fixed central crop
x=20-80 %, y=30-80 % of each PNG: a dark proxy is a connected near-black,
low-chroma blob of 160-18000 pixels, 18+ px high and at most 140 px wide,
with height at least 1.2 times width; persistence means bounding-box IoU >=
0.3 against the preceding frame. A variance proxy is a connected patch of
100-18000 pixels where 7x7 local grayscale variance exceeds 1800. The
`counts.tsv` files contain a row for **every** frame. UI panels, dark windows,
water and wood texture trigger these proxies, so they cannot be called the
reported silhouettes or corrupt textures without a visibly matching NPC.

Six stills for cold review:
`take2/frames/frame_000000.png`, `take2/frames/frame_000030.png`,
`take2_1024/frames/frame_000000.png`, `take2_1024/frames/frame_000012.png`,
`take2_burst_retry/frames/frame_000005.png`, and
`take2_burst_retry/frames/frame_000008.png`. Each directory also contains the
raw `client.log`, `keys.tsv`, and per-frame `counts.tsv`.

**Verdict: NOT REPRODUCED.** The prescribed 1280x720, 1024x768, and burst
capture modes were attempted at Will's saved login position. A normal-looking
bridge NPC appears in the baseline, but no frame establishes either reported
defect. The extra burst was necessary because the first burst's chat focus
prevented the turn. The final burst did turn, but generated only twelve frames
before `/q`, none with a classifiable affected NPC. No one-knob experiments or
Mesa mechanism attribution are justified without a failing baseline.

The XU4 was left as found: only `/tmp/b1051` was used and removed after all
logs/PNGs were transferred; `~/willeq/play.sh`, `~/willeq/willeq`, and
`~/willeq/willeq.json` retained their initial size and mtime; the GPU governor
remained `simple_ondemand`; no client process remained. The per-character UI
cache changed when ESC closed Options, so its sole changed `Show` byte and
original mtime were restored; its SHA-256 again matched the pre-run pin
`e30f4e24fca4b7d2ce1768bf4ba2bf48136574cbc269d2c265255cec0f324be4`.
The input sequence never requested character movement or altered the config or
DB. No reboot, package install, or kernel change was performed.

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

Will's saved qeynos position was tried in take 2, but the reported artifacts
still did not appear in a classifiable NPC view. A follow-up needs a capture
with at least one visibly affected NPC and a way to keep that entity in view
long enough for a before/after comparison. Neither the prior distant NPC nor
the take-2 bridge NPC establishes a failing baseline.
