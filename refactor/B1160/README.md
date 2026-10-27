# B1160 (#479): spell-93 landing density bisect + the B1151 motion-term ablation

**Resolves the "B1150→B1153 chain drift" attribution** (refactor/B1153/README.md
sec. 3(a)): the spell-93 landing falls 4.233 → 2.742 → 2.113 → 1.993 across the
FX-sim merges while the override-free number should converge on the oracle
(3.945, B1130 fixed camera; 3.049, the B1142 b1118 lane reference). This card
bisects the three sim merges (B1150/B1151/B1152) with the same instrument,
ablates the B1151 motion terms on record 80 one at a time, and names the owner.

## Start-of-run merge (operator note, 2026-10-26 20:35Z)

`origin/fable` already carried **B1157 (MR !309, animated caster)** at
c39f49891fad and **B1159** at cc85c331. Per the card body I ran
`git fetch origin && git merge origin/fable --ff-only` into
`task/kb-b1160-density-drift` **before the first build**, so the branch tip and
the current-tip row are cc85c331 and every after-table below compares to
`refactor/B1157/b1118_table.tsv`, which is now in-tree. B1157 moved 93 by
+0.000 (refactor/B1152/README.md), so the animated caster is not in the gap;
the gap is in the particle simulation, as this bisect shows.

## Instrument (the same one every row uses)

`refactor/B1150/scripts/tour7.sh` preset (B1153's): record-rep cast_ms, per-spell
seconds 1.5 + cast + 11.5, targets other except 253 self, `--camera b1118`,
`--fps 30`, `--seed 1`, `--headless`. Measured with
`review/B1139/scripts/harness_measure.py` (PEAK landing coverage = the max
lit-pixel count over the landing window, threshold 45). The four tips are
rendered in THIS worktree (`git checkout --detach <tip>`, `rbuild
kb-b1160-density-drift --target model_viewer`, tour7 on the ThinkPad, measured
here), one run each. SHAs are recorded per row.

## 1. The four-tip bisect (spell 93 + the other six, PEAK landing cov / end)

| tip | sha | spell | land_cov / end |
|---|---|---|---|
| **B1150** (EDD shapes) | `3abd37df` | 93 | 2.742 / +6.433 |
| | | 38 | 2.742 / +6.433 |
| | | 54 | 0.999 / +6.400 |
| | | 656 | 0.999 / +6.400 |
| | | 41 | 2.882 / +6.067 |
| | | 253 | 5.267 / — |
| | | 458 | 0.571 / +5.517 |
| **B1151** (EDD motion) | `c1ea11ce` | 93 | **2.113** / +6.467 |
| | | 38 | 2.113 / +6.467 |
| | | 54 | **1.204** / +6.333 |
| | | 656 | **1.204** / +6.333 |
| | | 41 | 2.478 / +5.667 |
| | | 253 | 5.306 / — |
| | | 458 | 0.499 / +5.183 |
| **B1152** (missile) | `e83a097d` | 93 | **1.993** / +6.633 |
| | | 38 | 1.993 / +6.633 |
| | | 54 | 1.204 / +6.333 |
| | | 656 | 1.204 / +6.333 |
| | | 41 | 2.478 / +5.667 |
| | | 253 | 5.306 / — |
| | | 458 | 0.499 / +5.183 |
| **current** (= origin/fable, B1157+B1159) | `cc85c331` | 93 | 1.993 / +6.633 |
| | | 38 | 1.993 / +6.633 |
| | | 54 | 1.204 / +6.333 |
| | | 656 | 1.204 / +6.333 |
| | | 41 | 2.478 / +5.667 |
| | | 253 | 5.306 / — |
| | | 458 | 0.500 / +5.050 |

Source TSVs: `measure/b1150_tour_<spell>.tsv`, `measure/b1151_tour_<spell>.tsv`,
`measure/b1152_tour_<spell>.tsv`, `measure/cur_tour_<spell>.tsv`. Command:
`scripts/b1160_tables2.py` (this checkout, under
`/home/user/kb-evidence/kb-b1160-density-drift/scripts/`).

**The steps reproduce.** 93 falls 2.742 → 2.113 → 1.993 across the three merges,
exactly the reported chain (B1149 config was 4.233, the pre-B1150 reference).
The fall is **not uniform**: the B1150→B1151 motion switch **drops** 93/38 (−0.629),
41 (−0.404) and 458 (−0.072) but **raises** 54/656 (+0.205) and holds 253
(+0.039). So the drift is record-specific EDD motion, not a global sim change.
The B1151→B1152 step is the missile (93/38 −0.120 land); 458's end moves at the B1152→cur step (+5.183 → +5.050, −0.133), not at the missile step. **The current
tip (cc85c331) matches `refactor/B1157/b1118_table.tsv` for all seven spells
(`cur_vs_B1157` = +0.000 land on every row)** — no row regressed at the tip.

## 2. The B1151 motion-term ablation (record 80, spell 93, at tip c1ea11ce)

The ablation flag `WILLEQ_FX_ABLATE=<term>` is a **no-op when unset**; it zeroes
one B1151 motion term in `applySlotEmitter()` so a record emitter's particles
move on every other word. It is kept as a local patch
(`ablation_b1160.patch`, diff at the bottom) — not committed in-tree, and its
no-op-when-unset is the ablation table's own noise floor (base1 == base2,
Δ 0.000). Record 80 is EFF "Ignite80ct V2" (B1137 xtab); its **Target** (landing)
stage is EDD 280 / 281 / 282 / 9 — 280 carries the radial (+0x110/+0x114/+0x118:
radMin 4 → radMax 5), 282 the up (+0xEC/+0xF0/+0xF4: upMin 8, upMax 3,
upAccel 2), 281 and 9 are frame-animated and carry no velocity words.

| term | land_cov | Δ vs base | reading |
|---|---|---|---|
| base1 | 2.113 | — | noise floor |
| base2 | 2.113 | +0.000 | noise floor (Δ 0.000 → the instrument is stable) |
| **up** | **1.256** | **−0.857** | **OWNER** — removing it drops the landing by 0.857 |
| ax2 | 2.113 | +0.000 | no-op |
| ax3 | 2.113 | +0.000 | no-op |
| radial | 2.079 | −0.034 | minor (EDD 280's 4→5 fly-out) |
| swirl | 2.113 | +0.000 | no-op |
| gravity | 2.113 | +0.000 | no-op |
| drift | 2.113 | +0.000 | no-op |
| spin | 2.113 | +0.000 | no-op |
| frames | 2.111 | −0.002 | no-op |
| timescale | 2.113 | +0.000 | no-op (the record's config velocity is not integrated) |

Source TSVs: `measure/abl_<term>_93.tsv`. **UP is the owner** (Δ −0.857, far above
the 0.000 noise floor); radial is a minor −0.034; every other term is a no-op.

## 3. The UP rule is read correctly — this is case (b), not a misread

Re-reading the owner term against `refactor/B1151/TRACE.md` + the cited
eqgame.dis / EQGraphicsDX9.dis lines + the record-80 EDD words
(`review/B1141/edd_fields_from_code.tsv`, `docs/spellsnew_format.md`): the up
word is PROVEN-BY-CODE as `v1 = rand[+0xEC,+0xF0]; v1 += dt*+0xF4;
h += v1*s*dt` (axis-1 / up). `updateEddMotion()` (sim_particles.cpp) implements
exactly this: `p.eddV1 = particleRandomFloat(upMin, upMax); p.eddV1 += dt*upAccel;
p.eddH += p.eddV1*s*dt`. The attach scale `s = entityScaleForServerSize(raceScale,
serverSize)` is **1.0** for the harness caster (a level-60 caster,
raceScale 1.0, server size 6/6 — TRACE.md line 700), so the harness up-velocity
is the raw EDD word. **The rule is read correctly; there is no misread to fix.**

## 4. So the column is short for a different reason — the next quantity

The landing has enough particles and the right geometry; it is simply sparser
in lit pixels than the oracle:

- **Population is high, not low.** A gated `WILLEQ_FX_COUNT` print of
  `ParticleOutput::activeCount` (local patch, reverted before commit) over the
  93 run at cc85c331 peaks at **1550 particles** (frame 154, ≈5.1 s, the impact);
  the landing window (frames 140–320) averages 349.5. The EDD words alone
  predict ~100 landing particles (280: 30/s×1.5 s=45, 281: 1/s×1 s=1,
  282: 28/s×1.5 s=42, 9: 10/s×1.25 s=12) — the sim spawns well above that. So
  the sparseness is **not** a population shortfall. Source: `popcount_93.txt`.
- **The geometry matches the oracle.** PEAK landing end +6.633 (d_end +0.166 vs
  the B1157 baseline; the oracle end is +6.67, d_end −0.04) and the LANDING bbox is
  771×627 px = 3.28×2.67 model (B1151: 791×612 px = 3.37×2.60; `measure_harness.txt`
  of `b1160_cur_tour_93` / `b1160_b1151_tour_93`) — the column is the right length and footprint.
- **The per-particle pixel area is the gap.** 1550 particles, right length, right
  footprint, yet n_px 1.993 (0.505× the 3.945 oracle, 0.654× the 3.049 b1118 lane
  reference) means each particle contributes too few lit pixels. The B1149
  sprite-size/depth-bias words (+0xD0 depth bias, +0x19C scale) are the
  next quantity to check: the EDD sprite size vs willeq's rendered per-particle
  pixel area, and the additive-blend saturation at threshold 45.

This is the card's case (b): the owner rule (UP) is read correctly and the
oracle is still denser. The next quantity is **per-particle pixel area
(sprite size / depth bias / blend)**, named with the 1550-particle + right-geometry
measurement above.

## 5. The 93 ledger clip (before/after, visible)

Regenerated from the best run (current tip cc85c331) with
`refactor/B1158/scripts/make_clip.py` against the B1143 p3_rig93 oracle leg:
oracle LEFT (a dense tall flame column that fully envelops the target) vs
willeq RIGHT (a thinner, sparser spray that leaves the target visible) — the
difference is obvious at impact (cells 5–6). Clip + 8-cell strip:
`clips/b1160_93_before_after.mp4` / `_strip.png` (attached to the task).
Command: `python3 refactor/B1158/scripts/make_clip.py
specs/b1160_93_before_after.json clips` (spec written under
`/home/user/kb-evidence/kb-b1160-density-drift/specs/`).

## 6. The ablation flag (local patch, no-op when unset)

`WILLEQ_FX_ABLATE=<term>` (`ablation_b1160.patch` below). It is a no-op when
unset (base1/base2 = 2.113, Δ 0.000) and is **not** committed in-tree, so no
ctest is required to gate it. Full diff:

```
diff --git a/src/client/graphics/environment/spell_emitter_presets.cpp b/src/client/graphics/environment/spell_emitter_presets.cpp
index fdb08230..cb7340ae 100644
--- a/src/client/graphics/environment/spell_emitter_presets.cpp
+++ b/src/client/graphics/environment/spell_emitter_presets.cpp
@@ -5,6 +5,8 @@
 
 #include <algorithm>
 #include <cmath>
+#include <cstdlib>
+#include <string>
 
 namespace EQT {
 namespace Graphics {
@@ -590,6 +592,25 @@ void applySlotEmitter(const std::vector<SpellEffectDef*>& slotDefs, const EQ::Sp
             em.frameRate = motion->frameRate;             // +0x134
             em.drawNegativeRadius = motion->drawNegativeRadius;  // +0x198
             em.scale = sprite && sprite->scaleSizes ? attachScale : 1.0f;  // s
+            // B1160: ablation of ONE B1151 motion term, env WILLEQ_FX_ABLATE
+            // (up/ax2/ax3/radial/swirl/gravity/drift/spin/frames); a no-op
+            // when the variable is unset. A record emitter's particles move on
+            // exactly these words (sim_particles.cpp updateEddMotion re-derives
+            // position from them, never from the config velocity), so zeroing a
+            // term removes that term's contribution to the landing and nothing
+            // else. "timescale" is handled at the B1145 guard below.
+            if (const char* abl = std::getenv("WILLEQ_FX_ABLATE")) {
+                std::string a = abl;
+                if (a == "up") { em.upMin = em.upMax = em.upAccel = 0.0f; }
+                else if (a == "ax2") { em.axis2Min = em.axis2Max = em.axis2Accel = 0.0f; }
+                else if (a == "ax3") { em.axis3Min = em.axis3Max = em.axis3Accel = 0.0f; }
+                else if (a == "radial") { em.radialMin = em.radialMax = em.radialAccel = 0.0f; }
+                else if (a == "swirl") { em.swirlMin = em.swirlMax = em.swirlAccel = 0.0f; }
+                else if (a == "gravity") { em.gravity = 0.0f; }
+                else if (a == "drift") { em.driftX = 0.0f; }
+                else if (a == "spin") { em.spinMin = em.spinMax = 0.0f; em.randomStartSpin = false; }
+                else if (a == "frames") { em.frameCount = 1; em.frameRate = 0.0f; }
+            }
         }
         const float oldLife = 0.5f * (c.lifetimeMin + c.lifetimeMax);
         const float delay = std::max(stageLatency, 0.0f) + t.start;
@@ -609,7 +630,16 @@ void applySlotEmitter(const std::vector<SpellEffectDef*>& slotDefs, const EQ::Sp
         // the file's own motion words (eddMotion, B1151), which carry the
         // extents -- scaling the config motion under them would double-drive
         // anything that still reads it. Non-record spells keep it.
-        if (!shape && oldLife > 0.0f) {
+        // B1160 ablation: "timescale" re-applies B1145's config-velocity
+        // scaling to a record emitter (reverting B1151's !shape guard) to
+        // measure whether the removal of that scaling moved the landing
+        // density. (For a record the config velocity is not integrated, so
+        // this is expected to be a no-op -- the measurement confirms it.)
+        const bool ablTimescale = [] {
+            const char* e = std::getenv("WILLEQ_FX_ABLATE");
+            return e && std::string(e) == "timescale";
+        }();
+        if ((!shape || ablTimescale) && oldLife > 0.0f) {
             const float k = oldLife / L;
             c.velocityBase *= k;
             c.velocitySpread *= k;
```

## 7. FOLLOW-UP (named owner for the next quantity — no card, per the card's "never create cards" rule)

**FOLLOW-UP: spell-93 landing per-particle pixel area (sprite size / depth bias /
additive-blend saturation) on record 80.** The owner motion term (UP, EDD 282) is
read correctly, so the remaining gap to the oracle's flame column (3.945) is the
per-particle pixel area, measured here: 1550 active particles at the impact
(frame 154), PEAK end +6.633 (d_end +0.166 vs B1157) and a 771×627 px = 3.28×2.67 model
LANDING bbox (B1151: 3.37×2.60) — the column is the right population, length and footprint,
yet n_px 1.993 (0.505× the 3.945 fixed-camera oracle, 0.654× the 3.049 b1118 lane reference). Next measurement:
the EDD sprite-size/depth-bias words (+0xD0, +0x19C scale) vs willeq's rendered
per-particle pixel area in the impact ROI, and the additive-blend saturation at
threshold 45. Owner: the FX-sim sprite-size lane (B1149's sprite-size/depth-bias
wiring). Evidence: `refactor/B1160/popcount_93.txt`, `refactor/B1160/measure/`,
`refactor/B1160/clips/` (the 93 before/after strip).

## 8. Acceptance

- (A) four-tip bisect table, spell 93 + the other six, SHAs recorded — §1.
- (B) per-term ablation table with the noise floor (base1 == base2, Δ 0.000) — §2.
- (C) owner term UP re-read against TRACE.md + .dis + EDD words → case (b),
  next quantity named with the 1550-particle / right-geometry measurement — §3–4.
- (D) tour-7 tabled vs `refactor/B1157/b1118_table.tsv`: `cur_vs_B1157` +0.000
  on all seven rows (no row regressed; no fix was made, so no row worsened).
- (E) every number beside its command — §1/§2/§4/§5.
- (F) targeted ctest + hygiene + whole-tree CI build — see the card run.
- (G) MR opens a FOLLOWUP (no in-tree fix), so it does not "Closes #479" — the
  gap is deferred to the per-particle-pixel-area FOLLOWUP with its measurement.
