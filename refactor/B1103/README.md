# B1103 — the spellbook draws the SAME icon in every slot while the gem bar is right

Will's XU4 observation (testcaster, 2026-10-19/20): every spellbook icon was the
same wrong one; the gem bar's icons were correct. This card proves the
mechanism, fixes it at the source, and pins it device-free.

## Hypothesis table (written BEFORE any run, per the card)

| # | Hypothesis (from #423) | Verdict | Proof |
|---|---|---|---|
| H1 | Publish order: the book is published at zone-in before the spell DB is loaded, so `getSpell()` misses for every row and every entry gets `iconId = 0` (one and the same cell). The gem bar publishes later, when the DB is loaded. | **DEAD — same call, same struct, same DB** | The book block and the gem block are NOT two separate publish moments. `PublishFullStateSnapshot()` (src/client/core/everquest.cpp:640) publishes the gems at :704 (`spell->gem_icon`) and the book at :722 (`spell->spell_icon`) in ONE function, in one call, reading the SAME `SpellData` structs from the SAME loaded `SpellManager`. If the DB were not loaded, the gems would be wrong too — they are not. H1 cannot produce "gems right, book wrong" by construction. (There is a second book publish, the scribe-completion arm, zone_dispatch.cpp:600 — same expression, same struct, same moment-class; it fails for the same reason as :722.) |
| H2 | Spells-sheet index mapping: an index past the sheet, or the Spells family keyed on the wrong id, resolves every slot to one fallback cell. | **DEAD for the geometry, ALIVE for the id** | The mapping in include/client/graphics/ui/spell_icon_atlas.h is the oracle's own (row-major, zero-based, 7x36 = 252 cells) and is pinned by test_spell_icon_atlas.cpp. The index arithmetic itself is not the defect. WHAT the book passes in is the defect: the book's id comes from `spells.spell_icon`, and in the shipped Titanium export that column is **0 on all 8,066 rows** (measured: `SELECT count(*), min(spell_icon), max(spell_icon) FROM spells` → `8066|0|0`; 1 distinct value). Every slot therefore addresses the SAME cell — cell 0 of the Spells family — regardless of spell. That is exactly "the same icon in every slot". |
| H2b | (the card's H2, kept separate) the family is the wrong one for the id | DEAD | The book draws through the Spells family (static_panels.cpp:962, B1077), which is precisely the family field 144 / NEW_ICON addresses (Spells01..07.tga, `uifiles/default/spells??.tga` per the oracle's own field comment, eqemu common/spdat.h:1662). The id space is right; the VALUE in the column is not. |
| H3 | XU4/Panfrost-only (pack/ETC1 issue, B1051 family) — only if x86 does not reproduce | DEAD | The mechanism (H2, wrong column) is driver-independent data plumbing; it reproduces identically on x86 in the device-free test (below), so no GPU leg is needed to name it. |

## The mechanism, at field level

`spells_us.txt` carries two icon-looking columns. The shipped loader
(tools/willeq_db/db_loaders_text.cpp:171-172) binds them:

- field **143** → `spells.spell_icon` — eqemu's comment on 143 (spdat.h:1661):
  `//int ai_pt_bonus; // 143: always set to 0, client doesn't save this`.
  It is NOT an icon column in the Titanium export: 0 on all 8,066 rows.
- field **144** → `spells.gem_icon` — spdat.h:1662: `int16 new_icon; // Spell
  icon used by the client in uifiles/default/spells??.tga, both for spell gems
  & buff window.` 170 distinct ids, min -1 max 215.

So the ONE icon column the official client actually uses — NEW_ICON, field 144
— lives in `gem_icon`, and the `spell_icon` column is a phantom: a faithful
transcription of a non-icon field that the Titanium export never populated.

The consumers split on that split:

| consumer | file:line | id read | family drawn | result in the shipped export |
|---|---|---|---|---|
| gem bar | everquest.cpp:704 (publish) → static_panels.cpp:495 | `gem_icon` (144) | Gems (gemicons*) | **correct** — distinct art per spell |
| spellbook (snapshot) | everquest.cpp:722 → static_panels.cpp:961 | `spell_icon` (143) | Spells (spells*) | **one icon in every slot** — cell 0 of the Spells family |
| spellbook (scribe) | zone_dispatch.cpp:600 → irrlicht_bridge.cpp:1014 | `spell_icon` (143) | Spells | same wrong cell for a freshly scribed spell |
| buff bar (snapshot) | everquest.cpp:761 | `spell_icon` (143) | Buff (Spell_Icons.tga) | cell 0 of the Buff family for every buff (B1077 already documented this as "0 everywhere in this export") |
| buff bar (live) | player_buffs.cpp:386 | `spell_icon` (143) | Buff | same |
| shift-click spell display | bridge_component_intents.cpp:1059 | `spell_icon` (143) | item panel | 0 for every spell |

The observation is fully explained: the gem bar is the ONLY spell-icon surface
fed from the column the client actually uses, so it is the only one that looks
right. The "same wrong icon" is cell 0 of the Spells family (the B1077 census
says the family is full, so cell 0 is a real, visible picture — the first
spell sheet cell — not a blank).

## The fix

`spell_icon` is the oracle's own id space for the Spells and Buff families,
and B1077's settled rule is that each id space resolves against its own
family. The value the oracle's spellbook uses in a Titanium session is
NEW_ICON (field 144) — the eqemu field comment says it explicitly ("both for
spell gems & buff window"), and the oracle's own spellbook buttons decal
against the BuffIcons template, i.e. the NEW_ICON id, not field 143.

Two candidate fixes:

1. **Rebind the loader**: `db_loaders_text.cpp:171` bind field 144 to
   `spell_icon` (leaving `gem_icon` on 144 as today). Every consumer
   (`spell_icon` readers) then resolves to the real icon; the `gem_icon`
   readers are untouched. One line at the source, plus the shipped DB
   rebuild + sha256 pin (the normal T450 cascade).
2. Point the five consumers at `gem_icon`. Five call sites, and it would
   collapse the book/buff id space into the gem id space, breaking the B1077
   invariant "a BuffIcons id is never mixed into a spell_icon and vice versa"
   — and it would be wrong for any export that DOES populate field 143.

**Chosen: (1), the rebind**, because it is the source of the wrong value, it
is one line, it keeps every consumer and every family mapping exactly as B1077
settled them, and it makes the column honest in BOTH directions: a future
export that populates field 143 would still be the wrong feed (the client
never reads 143 for an icon) — so the loader comment must say the binding is
deliberately the NEW_ICON column, with the phantom column documented. The DB
rebuild is the standard recipe: `scripts/build_willeq_db.sh` + the sha256 pin,
one commit.

`spell_data.h`'s field comment ("Spellbook icon") stays true; no consumer
code, no golden, no preset, no manifest changes.

## The test (device-free)

`tests/test_spellbook_icon_source.cpp`: drive the REAL
`EverQuest::PublishFullStateSnapshot()` with the REAL shipped DB:

- scribe two spells whose NEW_ICON ids differ (Summon Corpse = 35, Cloak =
  138 — rows 3 and 5, measured from data/willeq.db);
- read the `SpellbookSnapshot` off a capture bridge;
- assert each entry's `iconId` equals the DB's NEW_ICON id for that spell,
  and that the two entries' ids DIFFER — "no all-the-same";
- assert `indexForId(Spells, id)` resolves in-sheet for both (the Spells-sheet
  cell for id 35 is not the cell for id 138).

Against the pre-fix DB this FAILS (both entries carry iconId 0 = one and the
same cell); against the rebind it PASSES. The negative run is logged.

## BLESS NEEDED

None. The diff touches `tools/willeq_db/db_loaders_text.cpp`,
`data/willeq.db`, `data/willeq.db.sha256`, and the new test. No
`tests/goldens/*`, no `config/constrained_presets.json`, no manifest, no
third_party. (Static-UI golden frames with the spellbook open would only
change if one exists with scribed spells — none does: the goldens' books are
empty, so the icon path is not exercised.)

## FOLLOW-UP (for the orchestrator)

- XU4 live confirmation: open testcaster's book on the XU4 after this lands —
  expect per-spell icons, matching the gem bar. Not a gate (x86 reproduction
  is the proof, per #423).
- The `gem_icon` column now duplicates field 144 (the rebind puts 144 in BOTH
  columns). A follow-up can drop the duplicate column and re-point the gem
  bar at `spell_icon`, or keep both and document the duplication — a Will
  ruling, out of scope here (touching the gem path is explicitly off-limits on
  this card).
