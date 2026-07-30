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

(executor fills in)
