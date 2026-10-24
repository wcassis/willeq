# B1142 (#462) FX-LANE-RIG-OCCLUDER -- the Tip of the Day over lane 5's FX field

Branch `task/B1142-fx-lane-occluder`, cut from fable dacd9534. Evidence: `/mnt/projects/fable/review/B1142/`
(README, `progress.log`, `gate/`, `audit/`, `verify/`, `scripts/`, `plans/`, `tests/`). Lane 5, two leases,
2026-10-24 04:56-05:33 and 05:41-05:57 UTC; both legs sets ran fable dacd9534 with pack 1cdd31e7.

## Prior work found

- `refactor/B1139/README.md` sec. 2 (the window as a harness occluder), sec. 3 (Source ROI residue).
- `refactor/T499_tip_dialog_audit.md` + BOARD B795/B833/B834: the OFFICIAL client pops a Tip of the Day after
  zone-in; T492 Leg I first dismissed it; `scripts/oracle/find_tip_dialog.py` swept the archive in T499.
- B834 (#202) gave fable the same window: `src/client/graphics/frame/renderer_frame_tips.cpp`,
  `include/client/graphics/ui/tip_window.h` (`shouldPopTipOfTheDay`: `ShowTOTD` alone gates it).
- The oracle driver has no Tip-specific code; every oracle FX plan carries a `click 1229,555 close_tip` row after
  an 8 s settle (B1118 onward; B1134 one per client). `oracle_remote.sh` seeds only `# ini-set=` / `# ini-unset=`.
- The fable lane rig (`review/B1132/scripts/fable_leg_remote.sh`, copied by B1127) runs a slash-command
  `--script` tour; nothing in it, and no slash command, closes a tip window.

## 2. Why the window was up, and what the oracle captures carry

**Fable (code + file).** `FrameComponent::updateTipWindows()` opens the Tip of the Day once per session as soon
as the zone is ready, unless `[Tips] ShowTOTD=0` is in `UI_<char>_<server>.ini`
(`<cache dir>/ui/`, `eqt::ResolveCachePathForRead`). The lane client runs `~/src/willeq/build-gles2/bin/willeq`
with no `WILLEQ_CACHE_DIR`, so its file is `build-gles2/bin/cache/ui/UI_Testcaster_WillEQ.ini`, which carries
`ShowTips=1`, `ShowTOTD=1` (fable's defaults, written back by the client). The tour never closes the window, so
it stood at x 920..1279, rows 243..542 for the whole leg (`review/B1127/gate_final/fable_B/video.mkv` at 60 s;
`review/B1142/verify/fable93_landing_peak_oldrig_Bon_top_newrig_B_bottom.png`). Fable behaves as the official
client does here (the oracle's `03_settle.png` stills show the same window at 920..1279 x 250..549); the defect
was the rig's.

**Oracle (frames).** `review/B1142/audit/oracle_tip_audit.tsv` (`scripts/oracle_tip_audit.py`): the B1058
title-bar template matched position-free on every step still and on the video sampled once a second after
the close step, threshold 0.80 (every pre-close still scores 0.923-0.924; the best non-tip match, the Main Chat
title bar, 0.624-0.627). 68 videos (B1118 4, B1127 5, B1130 47, B1134 12): **zero tip frames after the close
step in every FX leg**; the window is up throughout only in `B1118/oracle_calib` (gem-pitch calibration, no close
row) and `B1134/p1_provision` (character creation). B1134's caster display carries it from its zone-in until its
own `07_close_tip`, before any cast (`oracle_tip_audit_b1134_lastclose.tsv`: 0 hits after it).
Other UI inside the oracle measurement crops: for B1118's 93 (`crop 150,60,900,540`, the source of the gate
tables' oracle column) only the nameplates ("Testcaster", "zone controller"); Effects/Group/Main Chat lie outside
(`review/B1142/verify/oracle93_B1118_druid_landing_peak_crop_red_tiprect_cyan.png`). B1130's geometry excludes the
nameplate rectangle; B1134's `geom/rig.json` excludes six observer UI windows that DO sit inside its crop.
On the fixed fable rig the 170,0..1230,650 crop holds the nameplates and the "FPS: 60" text (x 625..660, rows
5..15); the gem bar starts at x 1230.

## 3. The fixed rig and the re-baselined gate

`review/B1142/scripts/fable_leg_remote.sh`: the client runs with a per-leg `WILLEQ_CACHE_DIR` (a copy of
`build-gles2/bin/cache`), whose ini gets `ShowTOTD=0` (`FABLE_TIP=on` keeps the copy as found = the old rig).
The shared ini is never written (sha 32b993d1 before and after). Every leg's `tips-seeded.txt`, `ui-after.ini`.

Tour A/B/C (`review/B1142/gate/regression_table.tsv`; land_cov in caster heights^2):

| spell | B1132 old rig | B1127 old rig | dacd9534 old rig (Bon) | **new rig (baseline)** | new, window rect masked | peak px in window | new vs B1127 | masked vs B1127 |
|---|---|---|---|---|---|---|---|---|
| 93 | 2.938 | 2.929 | 2.942 | **3.049** | 2.934 | 3.8 % | +4.1 % | +0.2 % |
| 38 | 2.541 | 2.477 | - | **2.650** | 2.541 | 4.1 % | +7.0 % | +2.6 % |
| 54 | 0.998 | 1.615 | - | **1.696** | 1.625 | 4.2 % | +5.0 % | +0.6 % |
| 656 | 0.705 | 0.936 | - | **0.957** | 0.932 | 2.6 % | +2.2 % | -0.4 % |
| 41 | 2.229 | 2.273 | - | **2.666** | 2.285 | 14.3 % | +17.3 % | +0.5 % |
| 253 | 3.647 | 4.473 | 4.156 | **5.028** | 4.215 | 16.2 % | +12.4 % | -5.8 % |
| 458 | 1.520 | 1.729 | - | **1.732** | 1.714 | 1.1 % | +0.2 % | -0.9 % |

With the window's rectangle masked, the new captures land within -0.9..+2.6 % of B1127 except 253, whose
same-code window-open leg (Bon) reads 4.156, -7.1 % from B1127 and -1.4 % from the masked new value: 253's
run-to-run spread, not the window. Ends, ROI p90 and hues are in the table (ends within 0.04 s of B1127;
roi_l 0.034..0.105 all inside the oracle bands; roi_s 93 0.064 and 253 0.135 stay "short", as on B1127).

## Harness default and parity row for 93

`--camera b1118` no longer draws an occluder (`model_viewer_fx_harness.cpp`); `--ui-occluder 920,243,1280,543`
reproduces a pre-B1142 capture. Seeds 1-5 (`--target-death 0 --target-despawn 1.027`, the new leg B's pcap:
OP_DeleteSpawn(260) at Act+1.030): land_cov 3.111..3.191 (mean 3.151), end +4.967..+5.033, roi_l 0.065..0.073,
roi_s 0.069..0.074. Four unoccluded lane captures of 93: B 3.049, N93 2.990 (30 fps x264), G93 3.136, G93N 3.140
(60 fps lossless) -- mean 3.079, spread 5 %. Harness mean +2.3 % over the lane mean, each seed -0.9..+6.7 % of
each capture. Seed 1 with the window drawn back: 2.963 against Bon's 2.942 (+0.7 %). Written into
`tools/model_viewer/README.md` and `refactor/EXECUTOR_PROTOCOL.md`.

## 4. Source ROI p90 residue

- **The fable frame-dump path cannot record 60 fps**: leg D93 (`--dump-frames-dir --dump-every 1`) wrote 279
  frames in 80.4 s, one per ~290 ms (`gate/fable_D93/dump_frames.tsv`); the client rendered at that rate, so the
  cast was simulated at ~3.5 fps. Not usable; the 17 frames around the cast are kept (`dump_window/`). The 60 fps
  legs were instead x11grab at 60 fps through `libx264rgb -qp 0` (bit-exact RGB), no dump: G93 (tour B's 93) and
  G93N (no outline): 0 stale grabs, max frame gap 17 ms.
- **The no-outline legs** (`plans/tour_93_nooutline.txt`): `/target`, `/cast 1`, `/cleartarget` on consecutive
  frames (fable.log: 05:23:06.687 / .705 / .723), so the outline exists ~36 ms around BC and never in the
  pre-cast baseline (<= BC-0.2) the Source mask subtracts.
- **Attribution** (`review/B1142/source_density_summary.txt`, mean warm px per frame in the Source ROI;
  `verify/source_early_spray_B_N93_G93N_harness_s1.png`):

| capture | BC+0.5..0.85 | BC+1.0..1.45 | onset | roi_s | warm-blob p90 |
|---|---|---|---|---|---|
| B (outline in baseline, 30 fps x264) | 386 | 2659 | late | 0.064 | 0.076 |
| G93 (outline in baseline, 60 fps lossless) | 506 | 3289 | BC+0.856 | 0.069 | 0.080 |
| N93 (no outline, 30 fps x264) | 667 | 3607 | BC+0.778 | 0.066 | 0.076 |
| G93N (no outline, 60 fps lossless) | 654 | 3610 | BC+0.776 | 0.064 | 0.074 |
| harness seeds 1-5 | 704..977 | 3105..3585 | BC+0.700..0.800 | 0.069..0.074 | 0.081..0.090 |

  The colour-cycling target outline in the pre-cast median raises the baseline exactly where the hand spray
  starts, so a sprite there rises less than 80: it cut the lane's early warm pixels by 24..42 % (B, G93 against
  N93, G93N). That is most of B1139's "2x denser early spray": without it the harness reads +5..+49 % early
  (mean +23 %) and the lane reads +1..+16 % MORE once the spray is established. Frame rate and encoder move
  roi_s by 0.002 (N93 0.066, G93N 0.064). **Remaining**: roi_s 0.069..0.074 against 0.064..0.066, the
  harness's sprite-colour blobs merging larger (p90 0.081..0.090 against 0.074..0.076). Not attributed to a
  measured cause. **Not a fable defect**: both sides run fable's Source code; the oracle's 93 roi_s reference
  is 0.084 (band 0.071..0.097), which the lane (fable) reads short of on every capture since B1132 -- a
  fable-vs-oracle row that already stands in the gate table, not this residue.

## Code

- `tools/model_viewer/model_viewer_fx_harness.{h,cpp}`: the b1118 preset's occluder default removed.
- Docs: `tools/model_viewer/README.md` (defaults table, parity, lane-only), `refactor/EXECUTOR_PROTOCOL.md`.

## Builds and tests (`review/B1142/tests/`)

See `review/B1142/README.md` "Builds and tests" for the logs and counts.

## FOLLOW-UP

- Source ROI p90: harness 0.069..0.074 against lane 0.064..0.066 with no outline -- unattributed (sec. 4).
- The fable dump path's ~290 ms per frame (`IrrlichtRenderer::saveScreenshot`, synchronous readback + PNG) makes
  `--dump-frames-dir` unusable for timing-sensitive evidence; x11grab 60 fps lossless is the measured substitute.
- `review/B1127/gate_final/regression_table.tsv` is 0 bytes as B1127 left it; `SUPERSEDED_BY_B1142.txt` beside
  it and in `review/B1132/gate/` points at the new baseline.
