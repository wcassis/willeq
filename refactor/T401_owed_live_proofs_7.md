# T401 — Owed live proofs 7 (sound audible, coins on screen, fire vs frost observed, vision with weather pinned)

Measurement only; slot3; eqemu lock (atomic; must be ABSENT — if present
read it and STOP). Reuse the T377/T386/T391 drivers under
`review/T377|T386|T391/scripts/`; `--script` two-client rig; `/camp`
cleanly (a hard-killed client leaves a linkdead session that overrides
`character_data` writes — T391); verify every chat line against the
client's `Chat submit:` line; video for transients. Rebuild the slot's
`build` and `build-gles2` from the tip (T392 sounds, T393 purse, T395
impact, T396 weather log, T397 removal, T398 coins).

1. **Sound audible (B200/B036/B172).** T328's tone-probe rig on fable:
   loot a corpse (T377's arena recipe) → `BuyItem.WAV` (138) actually
   heard; one combat round → `SwordBlk` (120); one gem cast → `Spell_*`.
   The log must show `Loaded sound from PFS` lines and zero `not found`.
   Fill the rows on B036/B172/B200.
2. **Coins on screen (B201/T393).** Open the inventory after the loot:
   fable screenshot with the four coin rows and the decals (T398) vs the
   official client's inventory window at the same purse — side by side.
   Note B219 (the rows land on fable's oversized paperdoll) with the
   crop so Will can rule.
3. **Fire vs frost observed (B160/B204/B220).** Two clients; the caster
   gem-casts (NOT `#castspell` — no OP_BeginCast) Burst of Flame (93) and
   Frost Bolt (54) on itself with the observer 15 units away, caster in
   frame; the observer's video must show two DIFFERENT-coloured impacts
   (T395 keyed the impact on effect_flag=0; T397 stopped the wipe; T389
   mapped the tiles; the atlas alpha (T400) may or may not have merged —
   say which tip). If nothing draws, that is B226 (style too small) — crop
   at 4x and report the changed-pixel count.
4. **Vision with weather pinned (B203).** Repeat T377 §2 exactly but
   FIRST prove weather clear from the log (T396's per-zone-in line and
   the modifier line must read 1.000 across the whole window; if rain
   arrives, `#weather 0 0` and wait for the 1.000 line). Per-channel and
   whole-frame ratios for Ultravision and Infravision vs the oracle's
   1.561 / 1.256 (oracle side re-captured in the same session, same
   weather proof). This is the baseline the per-channel re-solve starts
   from; do NOT tune anything here.
5. `review/T401/README.md` (PASS/FAIL first per item); strike/annotate
   rows; restore every server value (world clock too) with the world
   DOWN; `down.sh`; delete the lock.

STOP: lock present; a proof FAILS (record, board B250+, continue).
