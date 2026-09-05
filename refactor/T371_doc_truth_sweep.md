# T371 — documentation truth sweep after T352–T370

Class P (Will: stale docs are a first-class defect). slot2, no server.
`DocCitations` proves paths resolve; it does not prove CLAIMS. Nineteen
units merged since the last orientation rewrite (T352–T370) changed:
command names (`/skills`, `/inventory`), the completion path (three
registries), the vision pipeline (`vision_type.h`, `vision_ambient.cpp`,
the light-culling rule), the send path (all senders via `PacketSender`,
the send-guard bypass on it), the component pattern (the `EverQuest&`
handle is at ZERO, no friend lines), the chr-cache accounting, `/warp`,
the UI toggle keys, willeq.db v6 (`endurance_cost`).

1. For each of `CLAUDE.md`, every `*/CLAUDE.md` under `include/` and
   `src/`, `README.md`, `docs/module_map.md`, `docs/decisions.md` (only
   the orientation parts), `docs/*.md` that name any of the areas above:
   read it against the tree and list every sentence that is now FALSE or
   names a thing that no longer exists (`/newskills`, `/newinv`,
   `setOnSpellLand`, `FindEntityByName`, `StartCombatMovement(name)`,
   `Test_SetBypassSendGuardForSeam` on EverQuest, `friend class` for the
   components, "data-only" zone lights, "Tab covers registries 1+2",
   `kWilleqDbExpectedVersion 5`, `drawZoneLineOverlay`, the collision
   registration API, …). Grep is the start; reading is the proof.
2. Fix each in place with the measured truth (cite the unit). Do not add
   prose; correct it.
3. Add a `DocClaims` hygiene test, next to `DocCitations`: a small list of
   (doc, forbidden phrase) pairs for the retired names above, so a retired
   name reappearing in orientation docs fails ctest. Register under
   `tests/cmake/hygiene.cmake`.
4. Board header suite count (it says 3,760; the tip runs ~3,940) — make
   the header say how to get the number rather than a number, or update
   it and say when.
5. Review: the table of false sentences found, per file.

## Proof

`ctest -j6` (the two hygiene tests + the new one), file sizes. No
graphics change → no gate.

STOP: none expected.
