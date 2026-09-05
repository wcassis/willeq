# T388 — documentation truth sweep 2 (T371–T387) + the FX API decision (B161)

Class P + B, slot1, no server. T371 swept the docs after T352–T370 and
built `DocClaims`; T373 built `DocCounts`. Fourteen units since changed
things the two nets do not see: every client send goes through
`PacketSender` (T376/T378/T379 — `QueueLogin`/`QueueWorld`,
`SafeQueueZone`, the census pin), the pending-click set lives on
`DoorStateManager` (T376), `/help` command-then-category and Tab over
three registries (T365), `/inventory` (T365), window keys `Alt+P` /
`Alt+F1` / `Alt+B` (T382), corpse coin banked (T382), spell visuals keyed
on `SpellVisualVocabulary` with `m_spell_db` gone (T381), willeq.db v7
with `target_anim`/`travel_type` (T384), the ceiling covering `tools/`
and `benchmarks/` with `model_viewer` split into `tools/model_viewer/`
(T385), vision floors per channel (T383), the light-culling rule (T367),
`AudioMixer` atomics (T370).

## Leg A — sweep

1. Same method as T371: for each orientation doc (root `CLAUDE.md`, every
   `*/CLAUDE.md` under `include/`, `src/`, `tools/`, `README.md`,
   `docs/module_map.md`, the orientation parts of `docs/decisions.md`),
   read it against the tree and table every sentence now FALSE or naming
   a thing that no longer exists (`m_spell_db`, `setSpellDatabase`,
   `m_pending_door_clicks`, `ClearPendingDoorClicks`, `zone_connection->QueuePacket`
   as a pattern, the old key bindings, "12 direct sends", "tools/ is not
   checked", `kWilleqDbExpectedVersion 6`, "35x darker", "data-only"
   lights, …). Grep is the start; reading is the proof.
2. Fix in place, citing the unit. Add the retired names to `DocClaims`'
   table and any new count to `DocCounts` (T373 shape: planted fault
   shown).
3. Review: the per-file table.

## Leg B — B161

The seven "dead" `SpellVisualFX` methods: T381 corrected the claim —
`tools/model_viewer` calls six of them (`createProjectile` is the one
with no caller anywhere). After T385's split, re-census with the B166
rule (name the directories). Decision by the board's own rule: a method
whose only caller is a developer tool is not production-reached; but
deleting it deletes the tool's feature. Keep the six (the tool is the
manual FX harness — say so in a comment at the class), and for
`createProjectile`: B165 says the travel gate guards it and 6,250 spells
populate that gate; do NOT delete it — annotate B161 as "six kept for
the harness, one awaits B165/B162" and strike nothing unless the census
contradicts T381.

## Proof

`ctest -j6` (the three hygiene tests + any new pin), file sizes.

STOP: none expected.
