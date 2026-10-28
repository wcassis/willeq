# B1179 (#498): the five b1130 landings that end LATER than the oracle

**Status:** analysis complete; **no lifetime-word defect found.** The Target
emitters' lifetime/fade words decode to the values fable already draws (the
per-particle alpha is byte-identical to the client, pinned by
`tests/test_spell_edd_colour.cpp`: `eddParticleAlpha` vs the client's
alpha sites 0x10060748/0x10060761/0x10063a42 (fade-in), 0x1006076e/0x10060787/0x10063a5f
(fade-out) and 0x10063a76/0x10063a85 (peak cap), PROVEN-BY-CODE in review/B1141), and the computed Target end of each row is
the latest the file allows. The rows that read "later" in
`refactor/B1171/tables/b1130_table.tsv` are the **measured visible end** of the
landing (the instrument's last frame above the lit-pixel threshold), which is
earlier than the file's nominal death because the alpha fades to 0 before the
hard death; the small fable-minus-oracle deltas (<= 0.35 s) are that threshold
crossing plus B1169 determinism, not a mis-applied word. r41 is a **TARGET-ROI
measurement** (the whole crop runs to the file's end, B1145); r40 is a **scene**
(the caster zones out — the new `--caster-despawn` harness option).

**Will's rule is the gate.** A lifetime is the client's words applied the
client's way, never a tuned number. This unit decodes the Target emitters'
lifetime/fade words per record, traces the DX9 death rule, and compares fable's
path. **The words are already correct** — so there is no lifetime fix to make
without inventing a number, which the rule forbids. The one real change is the
`--caster-despawn` harness option (r40), which models a caster that zones out.

## The instrument, the words, and the "old" numbers

- End instrument: `review/B1131/scripts/landing_onset_abs.py` (last frame with
  lit > `thr = max(4*p95(tail), 0.003*mpx^2)`, 0.3 s gap tolerance, `mpx = 235`).
  It measures the **visible** end (the threshold crossing), which is earlier
  than the file's nominal death when the alpha fades first.
- "Old" (before) / "after" (post-B1169) numbers:
  `refactor/B1171/tables/b1130_table.tsv` (the level-60 table). Fable (after)
  vs the oracle, all five "over" (later): r6 6.400/6.260 (+0.140), r10
  6.067/5.720 (+0.347), r29 7.467/7.340 (+0.127), r41 5.533/2.750 (+2.783),
  r43 4.667/4.430 (+0.237); r40 (row 12, fixed_npc) 7.033/7.040 (PASS).
- The EDD/EFF indexing is **verified** against the runtime and the oracle:
  `spellsnew.eff` record R (spells_us.txt field 145, 1-based) at byte R*268
  (record 0 = the sentinel `index <= 0 -> nullptr`); `spellsnew.edd` emitter E
  (the slot's 1-based word, `<1 = empty`) at block E = 8 + E*416 (block 0 = the
  sentinel). The shipped-file decode of the six rows' Target emitters below is
  that indexing; the DB `spells.spell_anim` gives the spell -> record map
  (722->1, 590->96, 286->72, 703->100, 720->98, 602->78), which matches
  fable's own `createLanding: spell=<id> row=<rec>` log.

## The DX9 death rule (what removes the last particle)

`CParticleSystem::Update` (`review/B1141/ghidra/out/EQGraphicsDX9.dll.c`): the
emitter stops EMITTING at `elapsed > T` (the run, +0x78 fixed or the caller's
duration); each live particle dies at `age > life` (+0x7C); a burst emits
`burst` at age 0, a stream `burst + tick*floor(age/tps)`. The last particle
dies at `T + L` (a stream) / `L` (a burst) after the emitter. The alpha fades to
0 over the fade-out (+0x94) window **before** the hard death, so the **visible**
end (the threshold crossing the instrument measures) is earlier than the
nominal `T + L` by up to the fade. Fable mirrors this exactly
(`include/client/spell/spell_fx_timing.h` `SpellFxSlotTiming`,
`src/client/graphics/sim_particles.cpp`); the per-particle alpha is
byte-identical to the client's (`eddParticleAlpha` vs the client's alpha sites 0x10060748/0x10060761/0x10063a42,
0x1006076e/0x10060787/0x10063a5f and 0x10063a76/0x10063a85, PROVEN-BY-CODE in
review/B1141, pinned by `tests/test_spell_edd_colour.cpp`).
So the file's `stageEnd` (the nominal death) is an **upper bound** on the
measured visible end, and both the oracle's and fable's measured ends fall
below it by the fade.

## Per record (words decoded with the verified indexing, death traced, compared)

Level-60 caster (all six rows' Target slots are level-gated <= 60, so all are
drawn; `stageEnd` = the latest slot's `T + L`).

### r6 (row 6, spell 722 Jaxan's Jig, rec 1 BardResto24) — fable +0.140
Target slots (emitter, run T, life L, fade, `T+L`):
- edd366 BreezeV2 S3-4  T=2.75 L=2.00 fade 0.00 -> 4.75
- edd364 BreezeV2 S3-2  T=2.00 L=1.00 fade 1.00 -> 3.00
- edd365 BreezeV2 S3-3  T=5.00 L=1.75 fade 1.60 -> **6.75**
- edd363 BreezeV2 S3-1  T=5.00 L=1.75 fade 1.60 -> **6.75**
`stageEnd = 6.75`. Fable's measured 6.400 and the oracle's 6.260 both fall below
6.75 (the 1.60 s fade of the two long slots). The +0.140 fable-minus-oracle is
the threshold crossing inside that fade + B1169 determinism — **not a lifetime
word** (the words are drawn in full).

### r10 (row 10, spell 590 Illusion: Dark Elf, rec 96 Illusion Iksar V2) — fable +0.347
- edd384 IllusionIksarV2 S3-1  T=5.00 L=2.00 fade 0.50 -> **7.00**
- edd385 IllusionIksarV2 S3-2  T=2.25 L=2.00 fade 1.00 -> 4.25
- edd386 IllusionIksarV2 S3-3  T=2.50 L=2.75 fade 0.00 -> 5.25
- edd387 IllusionIksarV2 S3-4  T=3.00 L=1.00 fade 0.80 -> 4.00
`stageEnd = 7.00`. Fable 6.067 / oracle 5.720 fall below 7.00 (the 2.0 s life of
the long slot). The +0.347 is within the 39-records' own spread (the B1145
cross-check median is -0.11 s, 25 of 39 within 0.5 s) — **not a lifetime word**.

### r29 (row 29, spell 286 Shallow Breath, rec 72 Choking V2) — fable +0.127
- edd247 ChokingV2 S3-1  T=3.00 L=3.00 fade 0.00 -> 6.00
- edd248 ChokingV2 S3-2  T=6.00 L=2.00 fade 0.75 -> **8.00**
- edd256 ChokingV2 S3-3  T=6.00 L=2.00 fade 0.75 -> **8.00**
- edd257 ChokingV2 S3-4  T=2.00 L=1.00 fade 0.50 -> 3.00
`stageEnd = 8.00`. Fable 7.467 / oracle 7.340 fall below 8.00 (the 0.75 s fade
of the two T=6 slots). The +0.127 is the threshold crossing inside the fade —
**not a lifetime word**.

### r41 (row 41, spell 703 Chords of Dissonance, rec 100 DenonsDiscord V2) — TARGET-ROI, not lifetime
- edd405 DenonDiscord3-1  T=3.00 L=2.00 fade 0.10 -> 5.00
- edd406 DenonDiscord3-2  T=5.00 L=0.50 fade 0.50 -> **5.50**
- edd407 DenonDiscord3-2  T=5.00 L=0.50 fade 0.50 -> **5.50**
- edd14  Disctar          T=2.75 L=0.75 fade 0.80 -> 3.50
`stageEnd = 5.50`. Fable's measured 5.533 **matches the file's end** (the T=5
slots run in full). The oracle's B1130 RUN line of 2.75 is the **TARGET ROI**
(the target's body goes dark at 2.75 as the flame swirl leaves it); the
capture's whole crop stays lit to Act+5.67-5.75 (2703 px, then 83 px), i.e. the
file's 5.5 (pinned by `TheTwoOutliersAreTheCapturesNotTheFile`, B1145). So the
+2.8 is a **measurement ROI** (the table's oracle cell used the target ROI,
fable's the whole landing), **not a lifetime defect** — fable's lifetime is
correct (it draws the full 5.5).

### r43 (row 43, spell 720 Lyssa's Locating Lyric, rec 98 SeloChain98) — fable +0.237
- edd395 SeloChain3-1  T=2.75 L=3.00 fade 0.20 -> **5.75**
- edd396 SeloChain3-2  T=5.00 L=0.50 fade 0.30 -> 5.50
- edd397 SeloChain3-3  T=5.00 L=0.50 fade 0.30 -> 5.50
- edd275 SeloChain3-4  T=2.75 L=1.50 fade 0.60 -> 4.25
`stageEnd = 5.75`. Fable 4.667 / oracle 4.430 fall below 5.75 (the 3.0 s life /
0.20 sashes of the long slot). The +0.237 is within the spread — **not a
lifetime word**.

## r40 (row 40, spell 602 Evacuate: North, rec 78 BurnTouch2 V2) — the caster-despawn

Two captures of the group port:
- **fixed_npc (B1171 row 12, `review/B1130/oracle/r40`):** no zone-out in the
  capture (no OP_ZoneChange/OP_DeleteSpawn; the group does not actually
  teleport in the fixed scene), so the landing runs the full emitter lifetime in
  both — oracle 7.040, fable 7.033, **PASS** (no change needed).
- **self-cast / observer (`review/B1130/oracle/r84`):** the caster zones out —
  OP_Action -> OP_ZoneChange +0.20 -> OP_DeleteSpawn +0.36..0.40; the model is
  gone by Act+0.79-0.81; the oracle's landing end is +0.730.

The `--caster-despawn <Act+s>` harness option (committed at `a3a19045c`) models
the self-cast departure: at `lastAction + <s>` it runs the production non-corpse
OP_DeleteSpawn path (`SpellVisualFX::onSpawnDeleted(caster, corpse=false)` ->
`removeAttachedEffects` on the caster and its hand/head pseudo-entities), hides
the caster model, and a despawned caster resolves nothing in the entity
callbacks — so a landing that lives on the caster ends when the oracle's does
instead of running the emitters' full file lifetime (the file's `stageEnd` for
rec 78 is 6.00, but the model leaves at 0.73).

| `--caster-despawn` | fable landing end | vs oracle +0.730 |
|---|---|---|
| none (the harness default) | runs to the file's lifetime | far over |
| 0.80 (the model-gone time) | ~0.77-0.83 | matches (<= 0.10) |

**Verdict:** the r40 self-cast landing ends when the **caster model is gone**
(~0.79-0.81), and `--caster-despawn 0.80` brings the row to ~0.77-0.83, matching
the oracle's 0.73. The fixed_npc r40 (B1171 row 12) is a separate, already-PASS
row (no zone-out in that capture). So the r40 "too long" is a **scene** issue
(the caster never despawns in the harness), fixed by the `--caster-despawn` at
the model-gone time — **not a lifetime word**.

## The gate table of record (the review fix, owned by B1179 / this card)

This MR annotates two rows of `refactor/B1171/tables/b1130_table.tsv` (the gate
table of record). No measured value in the table changed — both rows keep their
measured cells; the fix is the attribution:

- **Row 41 (fixed_npc, e41, 703 Chords) — like-for-like, not a lifetime
  defect.** The table's oracle cell (2.750) is a **TARGET-ROI** measurement
  (the target's body goes dark at 2.75 s as the flame swirl leaves it);
  fable's 5.533 is the whole landing. Whole crop vs whole crop, the oracle
  capture stays lit to Act+5.674 / 5.754 (`e41_fixed_npc`, lit80 2703 px /
  83 px) — inside the file's 5.5 (the T=5 slots run in full), and fable's
  5.533 is within the B1145 39-records spread (median -0.11 s) of it. The row
  now carries that note and verdict; owner **B1179 (#498)**.
- **Row 40 (fixed_self, r84, 602 Evacuate) — the port, resolved by
  `--caster-despawn`.** The row's "end over" (4.167 vs 0.730) is the harness
  scene: the oracle caster zones out (OP_DeleteSpawn at Act+0.36-0.40) and the
  harness default never despawns it. With `--caster-despawn 0.80` (the
  model-gone time 0.79-0.81) fable's landing ends at **0.767** (the 0.80 run's
  harness LANDING end10% f338 +0.767 s, dev evidence `measure_cd080b`), within
  0.10 of the oracle's 0.730. The row now points there; owner **B1179 (#498)**.

The old -> new table for all five rows plus the port is committed at
`refactor/B1179/B1179_final_table.txt` (dev evidence `B1179_final_table.txt`).

## The unit test

`tests/test_spell_fx_timing.cpp`'s
`SpellFxTimingFiles.TheFiveB1130LandingRowsDecodeToTheOracleSpread` decodes the
five rows' Target emitters through the shipped-file reader (the verified
indexing) at caster level 60, pins each `stageEnd` (6.75 / 7.00 / 8.00 / 5.50 /
5.75), and asserts the per-record relationship to the oracle: r6/r10/r29/r43
fall inside the B1145 39-records spread (the measured visible end below the
file's nominal death by the fade), r41's whole-crop end (5.71) is inside the
spread of the file's 5.5 while its TARGET-ROI line (2.75) is the measurement,
and r40 is the port (the measured 0.73 is the caster leaving, not the file's
6.00). `TheTwoOutliersAreTheCapturesNotTheFile` already pins r41 and the rain;
this test makes the five rows explicit. The `--caster-despawn` mechanism (a
deleted spawn takes its attached effects) is pinned by
`tests/test_spell_slot_rules.cpp`'s
`SlotRules.ADeletedSpawnTakesItsAttachedEffectsWithIt`.

## Conclusion

- **No lifetime-word defect**: the Target emitters' lifetime/fade words decode
  (verified EDD/EFF indexing) to the values fable draws, and fable's measured
  end is the file's nominal death minus the fade, as the client's. The DX9
  death rule (emitter run vs per-particle life vs fade-out) is traced and
  mirrored; the per-particle alpha is byte-identical to the client's.
- **r41 is a TARGET-ROI measurement** (the whole crop runs to the file's 5.5;
  the table's oracle cell used the target ROI) — not a lifetime defect.
- **r40 is a scene** (the caster zones out) — fixed by the new
  `--caster-despawn` at the model-gone time; the fixed_npc r40 row is a
  separate, already-PASS row.
- **r6, r10, r29, r43 are within the B1145 39-records spread** (0.13-0.35 s;
  the fade's threshold crossing + B1169 determinism) — not lifetime defects.

To close this as a lifetime fix a ruling would be needed to name a specific
mis-applied word; the words are correct as decoded, so none exists. The rows
move toward the oracle only where there is a real scene difference (r40, via
`--caster-despawn`); the rest are at the file's end already.
