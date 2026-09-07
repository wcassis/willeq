# T441 — the per-spell colour comes from the spell data (B398); measured per-spell styles (B397)

Class A/B, slot1, no server (T438 holds the lock). Read B398 and B397,
T422's §Review (the measured populations/sizes/colours per spell on the
OBSERVING client: 93 Burst of Flame 715 sprites, 41 Weaken 561, 253
Whirling Wind 25–36; the normalised RGB means), T389/T417/T420 §Reviews
(the sprite sheets, styles, atlas), `config/spell_effects.json`
(`resistColors`, `spellOverrides` = `{}`), `SpellEffectsConfig::
getStyleSettings(spellId)`, `getSpellColor`, and the data the official
client itself reads: `/mnt/projects/EverQuestP1999/spells_us.txt` (the
record for 41/93/54/253 — which columns select the particle sheet and
the colour; quote the column indices from an existing parser in the
tree or from the eqemu `spells_new` schema at
`/mnt/projects/akk-stack/code/common/spdat.h`, cited absolutely) and
`SpellEffects/*.eff` if those carry colour.

1. B398: find WHERE the official client's colour for a spell comes from
   (spell record field(s) → sheet/colour), and make fable's
   `getSpellColor` take the same input. Prove on the four measured
   spells: fable's normalised RGB (the T422 method, headless render of
   the burst on an EDT_NULL/gate device if the capture rig allows;
   otherwise the colour computed for each spell) lands on the oracle's
   ordering (41 green-dominant, 93 red, 54 blue, 253 not blue-heavy).
   If the data does not carry colour and the mapping is a table in the
   client binary, say so and add MEASURED per-spell overrides for the
   four spells only (never a guessed one) — the board says either closes.
2. B397: populate `spellOverrides` for 93, 41, 253 with T422's measured
   population and size (spawnRate/lifetime/size per the T420 model: the
   oracle ramps to N sprites with no deaths for ~2 s), leaving unmeasured
   spells on the style default. Test pins each override and that an
   unmeasured spell still resolves to the style.
3. Tests pre-fix failing: the colour test for 41; the override test.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
G05 in place if a pass changes, visual gate standalone + ARM (no image
golden should move — bursts are not in the gate frames; if one moves,
STOP with the sheet), file sizes. Strike B398/B397 or say exactly what
remains (the live observer re-capture is OWED to a lock unit — the duel
rig T422 built). New items B453+ (T442 in slot2 allocates from B453 too;
say which you used).

STOP: an image golden moves.
