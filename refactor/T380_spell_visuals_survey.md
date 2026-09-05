# T380 — spell visuals: the measured gap (B022 survey, no server)

Class P (a survey that turns B022's ruling into numbers); slot3.
B022 says the data-driven spell visual layer "is still entirely ahead":
nothing consumes the `spells.eff` reader in place of the hardcoded
resist/school vocabulary, and spells whose `spell_affect_index` hits an
empty padding record need a rendering ruling. Read the B022 board text,
`src/client/graphics/spell_visual_fx.cpp`, the `spells.eff` reader (grep
`spells.eff` / `SpellsEff` under src/ include/ tools/), and the willeq.db
`spells` table (`spell_affect_index`, `casting_anim`, `target_anim`,
`spell_visual` columns — whatever the builder loads).

1. Reachability census: which production path plays a spell visual today
   (cast start, cast land, buff tick?), what it keys on (resist/school
   vocabulary — quote the table), and whether the `spells.eff` reader has
   ANY production caller (T48/T264 pattern). Report as a table.
2. Data census over `/mnt/projects/EverQuestP1999/spells.eff` and the db:
   record count; for every spell in `spells_us.txt`, does its
   `spell_affect_index` land on a populated record or a padding record?
   Counts + the list of the padding-hit spells by class/level (are they
   the ones a level-1 test character will cast?).
3. What the official client does for a padding-hit spell is an oracle
   question (video under Wine — needs the lock; NOT this unit): name the
   three spells that would settle it (one populated, one padding-hit, one
   with no affect index) and the capture recipe (T361's rig).
4. The shape of Leg B (the wiring): which existing render path a
   data-driven effect would enter (the particle system? the `spell_visual_fx`
   sprites?), what the reader yields per record (fields), and the smallest
   end-to-end slice (one spell, one effect, matched to a capture) — a plan
   section written for a follow-up unit, with the STOPs.
5. Review: the tables; annotate B022 with the numbers (do not strike);
   board anything found dead along the way.

## Proof

Docs-only + any census script under `scripts/refactor/` (commit it);
`ctest -R Doc`. No build needed unless you add a test.

STOP: none expected.
