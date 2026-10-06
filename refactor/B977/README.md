# B977 — the `audio` ledger budget: measure on the Orange Pi (classic zones, sound ON), then ratchet

Issue #295. Branch `task/B977-audio-budget`. Will, in chat, 2026-10-01:
**"4) perform the test run"** — the run `audio` was held for on 2026-09-30
(**"lower the seven budgets, hold audio"**, B943).

## STATE AT HAND-OFF 2026-10-05 (this unit, complete)

The 2026-10-01 hand-off list below is superseded: steps 1–9 of REMAINS are
done (see "DONE 2026-10-05"). The code fix (attribution + `au.*` owner labels
+ the regression test + the rig's `audio` mode) is unchanged from the WIP
commit `8f229907`; fable merged in at `1b8ebb6f` (pre) and again before the
final push. Raw evidence: `~/kb-evidence/b977-audio/{pi,x86}/` (NOT committed;
the committed artifacts are this README, `audio_ledger_census.tsv` and the
table in it). Both lanes restored their eqemu snapshots BYTE-IDENTICALLY with
the world down; both released the eqemu and Pi locks.

## DONE 2026-10-05 (with evidence)

1. **Irrlicht rebuilt + Release build** (`build/`), `-j3` per the worker
   cap; `--irrlicht-only` first (B959's symbol).
2. **Red/green attribution test** (recorded in `~/kb-evidence/b977-audio/`):
   - red (the two `src/client/audio/*.cpp` hunks reverted): `playMusic
     (velketor.xmi): audio +64 B, game_state +24,228,592 B` — the custom
     SoundFont lands in the CALLER's category; `playSoundByName x6: audio
     +0 B, game_state +3,135,120 B`. Both cases FAIL as written.
   - green (hunks applied, `ctest -R AudioLedgerAttribution`): 2/2 pass.
3. **ARM build** published by CI at full sha
   `1b8ebb6f18b20e26a3dc3b4e730b3a9bfb653ca0`, staged locally
   (`~/ci-arm/1b8ebb6f…`, `binary_identity.txt`: pi_copy_sha256 matches
   published).
4. **Pi run** (`pi/sweep.log`, 13 zones, `--allow-vm-lane`, tour with the
   rat-fight, `--char-level 60 --char-exp 616137000`,
   `WILLEQ_LEDGER_OWNERS=audio WILLEQ_LEDGER_IDLE_EVERY=15`). 10/13 first-pass
   pass, 3 retried: erudnint passed on retry; **rivervale and gfaydark fail
   both attempts on `gpu_unnamed_over_bound` (13.03 / 12.19 MiB unnamed GEM >
   10 MiB) — the KNOWN condition of B988/#307 (Lima BO cache on by default,
   expected red on Pi tours until B972/#290 lands); not an audio finding, the
   guard was not changed.** `#npctypespawn 351043` exists on the VM lane
   (`client.log`: `Spawned a rat (351043)` in every zone that fought).
   Audio confirmation per zone: `Miniaudio backend initialized`, GM font
   loaded, `Preloaded 17/17 common sounds`; zone music actually STARTED
   (log `Playing music: …/…xmi`) in qeynos, qeynos2, befallen, akanon,
   neriaka, rivervale, gfaydark and **velketor** — velketor is the only one
   that also shows `MidiPlayer: loaded custom SoundFont on demand`.
   freportw, halas and erudnint never entered a music region in this tour.
   kael and thurgadina (the other two synthus2 zones) likewise never entered
   a music region: their safe point sits outside it, so their rows are the
   ~3.6 MB startup-only ones.
5. **x86 client, same zones** (`x86/rig.log`, `client_ledger_capture.sh
   --modes audio --bin build-gles2/bin/willeq`, same tour + char level):
   13/13 pass, snapshot restored byte-identically. Same music pattern:
   velketor is the only custom-font load (`au.soundfont_custom`
   24,162,944 B); kael/thurgadina again music-less.
6. **Table + ratchet decision** (below, and in
   `audio_ledger_census.tsv`; `docs/decisions.md` 2026-10-05 entry quotes both
   rulings verbatim).
7. **Gate note:** the benchmark never constructs `AudioManager` (audio row 0
   in the five gate zones), so the gate passes any audio budget trivially —
   the CLIENT figure is what the budget bounds (B923 #242). The full suite +
   gate run on CI (pushed branch; see MR !75 pipeline).
8. **Defects filed:** **B989 (#308)** duplicated float SFX caches
   (`AudioManager::bufferCache_` + `SfxManager::cache_`, each capped at the
   8,192 KB `soundCache` → up to 16 MiB of duplicated PCM) and **B990 (#309)**
   the custom SoundFont held as ~24 MB of float samples. Both class::A,
   lane::pi, on the board's A section. Board numbers re-read just before
   filing (max B-number in use 988), so the new ones land as B989/B990; the
   GitLab iids are the next free, #308/#309 (max iid 307 = B988) — the two
   numberings are not aligned.
9. **Push + MR**: `glab mr update 75` un-draft, `Closes #295`, issue #295 →
   `status::review`. Not merged.

## The measurement (steady_60s, the asserted point; full table in
`audio_ledger_census.tsv`)

Pi client (`pi/zones/<zone>/client.log`, `WILLEQ_LEDGER_OWNERS=audio`):

| zone | zone_load_complete | ready | steady_60s live / peak | notes |
|---|---:|---:|---:|---|
| qeynos | 3,653,144 / 3,667,928 | 3,653,144 / 3,667,928 | 4,239,144 / **4,839,672** | music started |
| qeynos2 | 3,638,160 / 3,638,160 | 3,638,160 / 3,638,160 | 3,811,680 / 4,765,016 | music started |
| freportw | 3,702,488 / 3,702,488 | 3,702,488 / 3,702,488 | 3,702,488 / 3,702,488 | no music region |
| gfaydark | 3,699,816 / 3,699,888 | 3,699,816 / 3,699,888 | 3,752,096 / 4,067,056 | music started; row failed the #307 guard |
| befallen | 4,050,664 / 4,050,712 | 4,050,664 / 4,050,712 | 4,686,944 / 4,789,664 | music started |
| halas | 3,638,856 / 3,638,856 | 3,638,856 / 3,638,856 | 4,123,640 / 4,244,536 | no music region |
| erudnint | 4,050,688 / 4,050,808 | 4,050,688 / 4,050,808 | 4,874,384 / **4,945,872** | classic worst; no music region |
| rivervale | 4,051,752 / 4,051,824 | 4,051,752 / 4,051,824 | 4,153,184 / 4,680,064 | music started; row failed the #307 guard |
| neriaka | 4,051,032 / 4,051,080 | 4,051,032 / 4,051,080 | 4,078,640 / 4,276,032 | music started |
| akanon | 3,644,224 / 3,644,224 | 3,644,224 / 3,644,224 | 3,794,712 / 4,849,504 | music started |
| velketor | 3,646,472 / 3,646,472 | 3,646,472 / 3,646,472 | 28,622,192 / **28,720,320** | music started; **custom font `au.soundfont_custom` 24,160,712 B** |
| thurgadina | 3,682,080 / 3,682,080 | 3,682,080 / 3,682,080 | 3,682,080 / 3,682,080 | no music region |
| kael | 3,678,376 / 3,678,376 | 3,678,376 / 3,678,376 | 3,678,376 / 3,678,376 | no music region |

x86 client (`x86/<zone>/audio/client.log`, same tour, same char level):

| zone | ready live / peak | steady_60s live / peak | notes |
|---|---:|---:|---|
| worst classic (akanon) | 3,616,080 / 3,616,080 | 3,817,120 / **5,690,016** | music started |
| velketor | 3,618,656 / 3,618,656 | 28,614,000 / **28,918,432** | **custom font: `au.soundfont_custom` 24,162,944 B** |
| kael / thurgadina | ~3.66 MB | ~3.66 MB | no music region entered |

The worst on BOTH machines is velketor at steady_60s, and it is the custom
SoundFont: 24.16 MB of 28.7–28.9 MB (84 %). velketor is also the only zone
whose music STARTED in both runs (music is region-based,
`ZoneAudioManager::findMusicEmitter`; kael/thurgadina's seeded safe point is
outside their region).

**Ratchet (the in-tree convention: measured worst × 1.25, rounded UP to a
whole MiB, moves DOWN only):**

| | worst | × 1.25 | rounded up | vs 32 MiB |
|---|---:|---:|---:|---|
| Pi, velketor | 28,720,320 B | 35,900,400 B | 34.25 MiB → **35 MiB** | above |
| x86, velketor | 28,918,432 B | 36,148,040 B | 34.47 MiB → **35 MiB** | above |

35 MiB is above the current 32 MiB, so per the rule the budget **HOLDS at
32 MiB** and the defects are named: the custom SoundFont (B990) moves first —
it is 24.16 MB of the 28.9 MB worst and the 1999 client never held its font
as float — and the duplicated SFX caches (B989) second. When B990 (+B989)
land, the worst falls to the classic-zone figures (~4.9 MiB Pi / 5.7 MiB x86
in this tour): worst 5,690,016 B × 1.25 = 7,112,520 B = 6.79 MiB → **7 MiB**
would be the convention value then. The ratchet NUMBER is Will's to bless
(he held it on 2026-09-30 and released the run on 2026-10-01); this unit
writes no budget change, so `config/constrained_presets.json` (33554432),
`constrained_renderer_config.{h,cpp}`, the test pins and the gate stay as
they are.

## Files changed by this unit (on top of `8f229907`)

- `refactor/B977/README.md` (this), `refactor/B977/audio_ledger_census.tsv`
  (new, 26 rows = 13 zones × Pi/x86 + the velketor owner rows);
- `docs/decisions.md` (2026-10-05 entry, quotes both rulings verbatim);
- `refactor/BOARD.md` (B989 #308 and B990 #309 filed, class::A, lane::pi);
- `src/client/graphics/constrained_renderer_config.cpp` +
  `tests/test_constrained_renderer.cpp`: the audio-budget comments now carry
  the B977 result (the VALUE is unchanged: 32 MiB, both pins untouched).

## Not verified

- **x86 idle_75 / idle_90 points**: the census columns are empty for every
  x86 row — the x86 lane (`client_ledger_capture.sh`) sampled
  steady_60s only, so the idle points stand from the Pi run alone.
- **The "31 Velious/Luclin XMI" count** (why `synthus2.sf2`'s 24 MB first
  load is a worst case, not a one-off) and the **T504 deferral history**
  (B990's `MidiPlayer::selectSoundFont(Custom)` deferral) are quoted from
  the earlier units, not re-verified against the pack in this one.
- **The rivervale and gfaydark steady points** stand in the table although
  their rows failed the `gpu_unnamed_over_bound` guard (the B988/#307
  condition, expected red on Pi tours until B972/#290 lands) on both
  attempts.

## Locks and stacks touched

Pi lock + eqemu VM-lane lock: taken and released by the sweep rig
(`pi/sweep.log` final lines: `snapshot restored BYTE-IDENTICALLY`,
`stack down`, `locks released`). x86 lane: eqemu lock taken and released by
`client_ledger_capture.sh` (`x86/rig.log`: same). No character row changed
(eligibility leg level 60 / exp 616137000 restored from the snapshot).
