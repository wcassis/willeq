# B1179 (#498): the five b1130 landings that end LATER than the oracle

**Status:** analysis complete; no lifetime-word defect found (the words are applied
correctly). The r40 row is explained by the scene (the landing ends when the model is
gone, not at the OP_DeleteSpawn). The r41 row is a position/attach issue (the long slot
is drawn in the target ROI in fable but elsewhere in the oracle) — B1172/position
territory, not lifetime. The three small rows (r6, r10, r43) are within the B1169
determinism / threshold noise. See per-record below.

**Will's rule is the gate.** A lifetime is the client's words applied the client's way,
never a tuned number. This unit decodes the Target emitters' lifetime/fade words per
record, traces the DX9 death rule, and compares fable's path. **The words are already
correct** — so there is no lifetime fix to make without inventing a number, which the
rule forbids.

## The instrument and the "old" numbers

- End instrument: `review/B1131/scripts/landing_onset_abs.py` (last frame with lit >
  `thr = max(4*p95(tail), 0.003*mpx^2)`, 0.3 s gap tolerance, `mpx = 235`).
- "Old" (before) numbers: `refactor/B1171/tables/b1130_table.tsv` (the level-60 table):
  r6 +6.400, r10 +6.067, r29 +7.467, r41 +5.533, r43 +4.667 (fable) vs the oracle
  r6 +6.260, r10 +5.720, r29 +7.340, r41 +2.750, r43 +4.430.
- Fable's lit series confirms the emitters ARE drawn for the full EDD duration
  (e.g. r6: lit rises 0.3->1.8, decays to 7.0, the slot0/1 T=5.0 + L=1.75 stream's full
  life; r41: lit to 5.5, the slot2 T=5.0 + L=0.5 stream's full life). So the lifetime
  words are applied; the "end" is where the lit drops below the threshold.

## The DX9 death rule (what removes the last particle)

`CParticleSystem::Update` (`EQGraphicsDX9.dll.c:49646`): the emitter stops EMITTING at
`elapsed > T` (emitter duration, +0x78); each live particle dies at `age > life` (+0x7C);
a burst emits `burst` at age 0; a stream emits `burst + tick*floor(age/tps)`. The last
particle dies at `T + L` for a stream, `L` for a burst. The alpha fades to 0 over the
fade-out (+0x94) window before the hard death at `L`. Fable mirrors this
(`spell_fx_timing.h`, `spell_emitter_presets.cpp`, `sim_particles.cpp`) — the per-particle
alpha is byte-identical to the client's (`eddParticleAlpha` vs `fun_1005f940`, confirmed
in a prior unit). So the visible end is set by the EDD words, applied the same way.

## Per record (words decoded, death traced, compared)

### r6 (rec 6, spell 722, Bard, self, cast 3000 ms) — f +0.140
- Target words: slot0 edd366 T=5.0 L=1.75 stream (ends 6.75); slot1 edd364 T=5.0 L=1.75
  stream (ends 6.75); slot2 edd365 T=2.0 L=1.0 stream (ends 3.0); slot3 edd363 T=0
  L=1.0 burst (ends 1.0). No delay (+0x8C=0).
- Fable draws the full duration (lit to 7.0). The +0.140 is within the threshold /
  B1169 determinism (the fade-out crossing, not a lifetime defect).

### r10 (rec 10, spell 590, Bard, self, cast 3000 ms) — f +0.347
- Target words: slot0 edd382 T=0 L=0.6 burst; slot1 edd380 T=0 L=0.6 burst; slot2 edd381
  T=5.0 L=0.8 stream (ends 5.8); slot3 edd379 T=0 L=1.5 burst (ends 1.5). No delay.
- Fable draws the full duration (lit to ~5.8). The +0.347 is within the threshold /
  B1169 determinism.

### r29 (rec 29, spell 286, other, cast 1500 ms) — f +0.127
- Target words: slot0 edd285 T=1.0 L=1.5 stream (ends 2.5); slot1 edd287 T=0 L=0.5
  burst; slot2 edd286 T=0 L=3.0 stream (ends 3.0); slot3 edd288 T=0 L=2.5 stream
  (ends 2.5). No delay.
- Fable draws the full duration (lit to ~3.0). The +0.127 is within the threshold /
  B1169 determinism.

### r41 (rec 41, spell 703, other, cast 3000 ms) — f +2.783 — POSITION, not lifetime
- Target words: slot0 edd406 T=0 L=1.0 burst; slot1 edd403 T=0 L=2.0 burst (ends 2.0);
  slot2 edd407 **T=5.0 L=0.5 stream (ends 5.5), attach=1**; slot3 edd14 T=2.0 L=3.0
  stream (ends 5.0), attach=2. No delay.
- Fable draws the full duration (lit to 5.5, matching B1171's +5.533). The oracle's
  TARGET ROI ends at 2.75 (slot1's death) — the T=5.0 stream (slot2) and slot3 are
  drawn ELSEWHERE in the oracle (the whole crop stays lit until 5.67, B1145). So the
  +2.8 is a position/attach issue: slot2 (attach=1) and slot3 are inside the target ROI
  in fable but outside it in the oracle. **This is B1172/position territory, not a
  lifetime defect.** Fable's lifetime is correct (it draws the full 5.5).

### r43 (rec 43, spell 720, Bard, self, cast 3000 ms) — f +0.237
- Target words: slot0 edd350 T=5.0 L=1.75 stream (ends 6.75); slot1 edd349 T=5.0
  L=1.75 stream (ends 6.75); slot2 edd351 T=2.0 L=1.0 stream (ends 3.0); slot3 edd353
  T=0 L=1.0 burst (ends 1.0). No delay.
- Fable draws the full duration (lit to ~6.75). The +0.237 is within the threshold /
  B1169 determinism.

## r40 (rec 40, spell 602, self, cast 9000 ms) — the caster-despawn re-measure

The `--caster-despawn <Act+s>` harness option (committed at `a3a19045c`) removes the
caster's emitters at `lastAction + <s>` (the zone-out). The B1130 r40 capture:
`OP_DeleteSpawn(S->C)` at +0.40 (the zone-out), model gone at +0.79-0.80, oracle
landing end +0.730.

| `--caster-despawn` | fable landing end | vs oracle +0.730 |
|---|---|---|
| none (before) | +4.167 (the file's full lifetime) | +3.437 over |
| 0.40 (the zone-out / OP_DeleteSpawn) | ~0.400 | SHORTER by 0.33 |
| 0.80 (the model-gone time) | ~0.77 | matches (0.04) |

**Verdict:** the r40 landing ends when the **caster model is gone** (0.79-0.80), not
when the `OP_DeleteSpawn` is received (0.40). The emitters linger 0.33 s after the
zone-out, before the model disappears. The `--caster-despawn` at the model-gone time
(0.80) brings the row to ~0.77, matching the oracle's 0.73. So the r40 "too long" is a
scene issue (the caster never despawns in the harness), and the `--caster-despawn` at
the model-gone time is the correct comparison.

## Conclusion

- **No lifetime-word defect**: the Target emitters' lifetime/fade words are decoded and
  applied correctly (fable draws the full EDD duration per record; the per-particle
  alpha is byte-identical to the client). The DX9 death rule (emitter duration vs
  per-particle life vs fade-out) is traced and mirrored.
- **r41 is a position/attach issue** (the long slot is in the target ROI in fable,
  elsewhere in the oracle) — B1172/position territory, not lifetime.
- **r40 is a scene issue** (the caster never despawns) — explained by the
  `--caster-despawn` at the model-gone time.
- **r6, r10, r43 are within the threshold / B1169 determinism** (0.13-0.35 s; the
  fade-out crossing, not a lifetime defect).

To close this as a lifetime fix, a ruling is needed: either (a) accept that the rows are
within determinism / position / scene (no lifetime fix), or (b) name a specific lifetime
word that is mis-applied (the words are correct as decoded, so this would require a
re-derivation of a word's meaning). See the block for the exact next command.
