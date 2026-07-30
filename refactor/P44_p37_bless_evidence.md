# P44 — P37 bless-evidence errand: framed masked-race stills for Will

**Slot: slot2. x86 ONLY. Owns eqemu (w/ lock, sanctioned start =
`spire spire:launcher start` — see scripts/eqemu/; akk-stack rules:
GM commands for lifecycle ops, never modify server code). No Pi.**

P37 (merged `e8efd87`) assigned alpha-test to masked entity textures
(non-composite + equipment paths) and excluded ~15 masked races from
composite eligibility. Its numeric trace was solid but the live stills
FAILED to frame a single zombie (bad vantage + night) — Will's visual
bless is still owed evidence. This unit is an evidence ERRAND: no
production source changes.

## Work

1. Live x86 GLES2 client session (llvmpipe; the full live loop is
   clean post-P36), GM char (Testchar / willeq_test). `#time 12` for
   daylight. Frame, close-up and well-lit:
   - befallen: skeletons (SKE — rib gaps are the most dramatic
     alpha-test win) and zombies (ZOM — befallen's own population),
   - qeynos2: `beahe0008` (the bone-helm case from P37's plan).
2. **Same-pose before/after pairs**: "after" = current fable tip
   binary; "before" = pre-P37 rendering via a pre-P37 binary (P37's
   merge parent — build it in a scratch dir inside YOUR slot; restore
   your branch checkout afterwards) or a config/flag toggle if one
   exists that cleanly reverts the P37 behavior — verify whichever
   route you pick actually reproduces pre-P37 rendering (spot-check a
   masked texture renders opaque-quad style). Identical framing by
   construction: fixed `#goto` coordinates + identical camera inputs +
   `/screenshot`; verify pairs align before calling it done.
3. Deliverable: PNG pairs + honest crops of the alpha regions, at
   /mnt/projects/fable/videos/p44_p37_bless/ (NFS, gitignored per P20
   convention) + committed manifest w/ diff stats under
   benchmarks/audits/p44_p37_bless/ + plan Review. Objective captions
   (what changed where); Will judges.

## Rules

- No production source changes. Scripts/scratch only; anything
  committed = manifest, scratch scripts, plan Review.
- Server restored clean at exit (lock released, containers as found).
- If a live-client crash blocks a capture, note it with a backtrace
  and work around via vantage/zone choice — do NOT fix live bugs in
  this unit (report them for deferred.md).
- Build-tree caution: post-crash 0-byte stub binaries possible in
  slot build dirs — verify binary sizes after building.

## Verification

- Errand bar: no source diff vs fable (scripts/docs only), pairs
  aligned (state how verified), server clean exit. Full ctest/gate NOT
  required unless you touched anything that could affect them.

## Review

**P44b ANNOTATION (retake dispatched, see refactor/P44b_bless_retake.md)**:
Will REJECTED this unit's live-client visual pairs. The rejection is
CORRECT, confirmed independently by P44b's own investigation, for two
compounding reasons: (1) the live aggroed mobs ran attack animations and
`/screenshot` timing was not animation-synchronized, so BEFORE/AFTER caught
different POSES -- the pixel diff below is dominated by pose mismatch, not
alpha; (2) the vantage put dark stone/shadow directly behind the ribcages,
where pre-P37 masked-opaque (dark texture fill) and post-P37 alpha-test
(background bleed-through) are visually indistinguishable against a near-
black background. Per the below, specifically VOID the "textbook alpha-test
evidence" characterization of the skeleton pair and the "STRUCTURAL
DIFFERENCE CONFIRMED" framing of the qeynos2 bear pair as VISUAL bless
evidence -- the underlying pixel diffs are real (not fabricated) but do not
demonstrate what the prose around them claims a human viewer can see. The
zombie honest-null finding (zomch0001 citywlivy-class) is NOT void -- it
was independently re-confirmed by P44b via a different method (source-level
numeric trace) and still holds. See refactor/P44b_bless_retake.md for the
retake's own (also inconclusive, for a different and newly-discovered
reason -- a benchmark-tool wiring gap, not a framing problem) result. This
annotation follows the P08d-retake precedent: annotate in place, do not
delete the original prose below.

**Delivered**: three same-pose before/after PNG pairs (befallen
skeletons, befallen zombies, qeynos2 bear/"bone helm") at
`/mnt/projects/fable/videos/p44_p37_bless/stills/` (NFS, gitignored),
full method + diff-stat details + honest per-pair verdicts in the
committed manifest: `benchmarks/audits/p44_p37_bless/EVIDENCE_MANIFEST.txt`.

**Binaries**: AFTER = fable tip (task/P44-bless-evidence, `a805e01`,
`build/bin/willeq`). BEFORE = commit `fda2e34` (P37's actual merge
parent, `0019ba6^1`) built in a scratch tree `build-preP37/` inside this
slot (gitignored via the repo's `build-*/` pattern), then the branch
checkout was restored (`git checkout task/P44-bless-evidence`) before
any further work.

**Hash deviation (self-caught, confirmed by orchestrator)**: the plan
text named "P37's merge parent e8efd87^" — `e8efd87` is actually P36's
merge commit, predating the P37 plan doc entirely. Verified via
`git log --oneline --all | grep P37`: P37's real merge commit is
`0019ba6` (parents `fda2e34` and `018bcdc`). Used `fda2e34` for the
before-binary from the start; the orchestrator's mid-session correction
(prompted by P45's fact-check) confirmed the same hash — no captures
were ever taken against the wrong commit.

**Headline result**: the skeleton pair is clear, dramatic, textbook
alpha-test evidence — BEFORE shows a solid/filled ribcage (opaque-quad
rendering, satisfying the plan's required before-binary spot-check
directly), AFTER shows individual rib gaps with background visible
through the torso. Full-frame pixel diff (ImageMagick `compare -metric
AE`): 20380/480000 (4.2%) nonzero, concentrated on the two skeleton
silhouettes (not background noise) — ~80x P37's own documented
unmasked-race FP-noise floor (~0.05%). The qeynos2 bear pair shows the
same full-silhouette structural diff pattern (3.4% full-frame / 19.2%
in-crop AE) confirming a real rendering change, but the vantage
rendered too dark (sky stayed black despite `/time 12`, an
unroot-caused zone-lighting quirk — flagged for deferred.md, not
chased) for a human viewer to clearly see the cutout. The zombie pair
is an honest null-ish result: full-frame diff is real (2.5%, driven
mostly by a bonus "ice boned skeleton" in frame) but the zombie's own
visible chest/leg crops sit at background-noise-level AE — this
matches P37's own ground-truth table, which named `zomch0001.bmp` (the
zombie's dominant chest texture) as the one confirmed citywlivy-class
case (WLD masked=true, decode-opaque, unchanged BY DESIGN) in the
whole sweep; the legs' real-alpha texture (`zomlg0001.bmp`) didn't read
as a visible change at this mob's screen size. Reported as found, not
re-selected for a flattering crop.

**Method**: fixed camera pose achieved via direct `character_data`
DB writes (x/y/z/heading/zone_id) before each capture rather than
in-session movement/turning, so pose is deterministic by construction;
identical `--script` command sequences (wait/`#set invulnerable
on`/wait-for-Phase-15-Ready/`/time 12`/wait/`/screenshot`/`/camp`)
across every before/after pair. Server restarted (`down.sh`+`up.sh
<zone>`) between each AFTER/BEFORE run so spawn2 mobs (skeleton,
zombie) start at deterministic spawn coordinates. qeynos2's
`beahe0008.bmp` (P37's "bone helm", actually a bear-race masked head
texture per `wld_dump` on `qeynos2_chr.s3d` — `BEA_ACTORDEF`) has no
live spawn2 placement in qeynos2 today (P37's Review already found
this); worked around with GM `#npctypespawn 3003` (`a_brown_bear`) +
GM `#set texture 8`, honestly labelled as GM-spawned, not a natural
qeynos2 encounter.

**Live-client issues hit (not fixed, per plan scope, flagged for
deferred.md)**:
1. One non-deterministic SIGSEGV zoning into befallen at the zombie
   vantage (x=-195,y=-982,z=-68.63), mid zone-object-mesh-build. Did
   not reproduce under `gdb -batch -ex run -ex "thread apply all bt"`
   at the identical position (clean exit), nor on a plain re-run — no
   core file was produced (`ulimit -c` was 0 at first crash; a
   follow-up with `ulimit -c unlimited` simply didn't crash again), so
   no backtrace could be captured. Worked around by re-running; did
   not block the final delivered captures.
2. Pre-existing (not new): dying at this account's original bind point
   (qeynos, zone_id=1) crashes on respawn (`Player BSP region is
   SIZE_MAX!` / missing `./maps/qeynos.map`). Hit once early
   (Testchar was killed by a live skeleton before `#set invulnerable
   on` was added to the script). Worked around for the rest of the
   session by granting invulnerability immediately after every
   zone-in, before any mob could land a hit.

**eqemu**: sanctioned `scripts/eqemu/up.sh <zone>` / `down.sh` only,
lock held for the full session (`/mnt/projects/fable/.eqemu-lock`,
created/removed per protocol). Server visited befallen (twice, fresh
restart between AFTER/BEFORE) then qeynos2 (twice, same pattern).
Testchar restored to the documented point (zone_id=36, x=-77.9232,
y=21.9086, z=3, heading=190) and accumulated test-session corpses
cleaned up (routine Testchar-scoped `character_corpses`/
`character_corpse_items` DB rows, not server code) before final
`down.sh`. `status.sh` confirmed clean exit: no containers, ports
5998/9000 closed.

**Errand bar**: no production source diff vs fable (`git status` on
the branch shows only this plan file + the manifest + captures
scripts under `data/captures/`, the latter gitignored per repo
convention — verify with `git status`/`git diff --stat` before
merge). Pairs aligned (see Alignment verification in the manifest).
Server clean exit (above). Full ctest/gate not run — not required per
this errand's narrowed verification bar (no source changes to gate).
