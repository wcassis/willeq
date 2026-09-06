# T407 — `spell_manager.cpp` split before it breaches (structural); the written-never-read field sweep

Class P (structural — Will's first-order rule) + P (a new reachability
class), slot3, no server.

## Leg A — `src/client/spell/spell_manager.cpp` (1,451 of 1,500)

T404 added the gem-update arm and left it 49 lines under the ceiling; the
next spell unit breaches it. Split by FUNCTION with the K mechanism
(read `refactor/K_component_extraction.md` §Mechanism and Batch G's
shape): read the file first and write the seam plan into the Review
BEFORE editing — candidate seams: packet handlers (BeginCast / Action /
Interrupt / MemorizeSpell / buff packets), the cast state machine
(validateCast / checkMana / checkEndurance / timers), the gem bar and
spellbook, the visual/vocabulary publishing (T381). One commit per
extracted TU; headers first; no behaviour change — the spell tests
(`SpellManager*`, `SpellCast*`, `SelfDeath*`, the G07/G08 goldens, the
bridge intent table, T381/T384/T404's tests) pass unedited. Each TU
≤ 1,000 so the family has headroom. Update `docs/module_map.md`
(DocCounts nets component counts — check).

## Leg B — struct fields written by every producer and read by nobody

T404 found `MixChannel::sampleRate` written everywhere and read nowhere
for years, two lines under a TSAN banner. Build a census script
(`scripts/refactor/check_write_only_fields.py`): for every struct/class
member under `include/client` and `src/client`, count reads vs writes
across `src/ include/ tests/ tools/ benchmarks/` (a write is
`.name =`, `.name +=`, `->name =`, an initialiser; a read is any other
occurrence outside its declaration). Report the write-only fields with
their files; classify each (dead → delete; a value that SHOULD be read →
board as a defect, B290+; a serialised wire field → exempt with the
reason). Delete the dead ones (compiler proof), board the defects. If
the list is long, do the deletions in this unit and board the rest with
the script committed so the next unit continues from the table.

## Proof

Build, `ctest -j6` (expected red: B240's gate delta only), the spell
goldens/tests unedited, file sizes (every new TU under the ceiling),
`DocCounts` green.

STOP: a spell seam cannot be cut without changing a golden statement
(learn IN PLACE per correction 32 — that is not a STOP; STOP only if a
behaviour test goes red for a reason you cannot attribute).
