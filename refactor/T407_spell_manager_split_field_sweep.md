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

---

## Review

### Leg A — the seam plan (written BEFORE the first edit)

`src/client/spell/spell_manager.cpp` is 1,451 lines / 44 `SpellManager::`
bodies, already organised by banner into eleven sections. `SpellManager` is a
plain class, not an `EverQuest` component: there is no `ClientContext`, no
`owner_`, no friendship. So the K mechanism reduces to its TU-split half —
bodies move VERBATIM into sibling TUs of the same class, the header is not
touched, and **none of the four substitutions is needed** (no `m_x` →
`ctx_.x`, no `Foo()` → `owner_.Foo()`, no `EverQuest::` scoping, no
`this` → `&owner_`): every mover stays a member of the same class in the same
namespace. This is exactly the shape `src/client/spell/spell_cast_checks.cpp`
(T246) already established — four `SpellManager::` bodies in their own TU with
a one-line pointer comment left at the seam.

Header first: `include/client/spell/spell_manager.h` gets only comment edits
naming which TU holds which body. No declaration moves, so every caller and
every test compiles unchanged.

| # | new TU | bodies moved (source lines at 4dbeeedb) | why this seam |
|---|---|---|---|
| — | `spell_manager.cpp` (stays) | ctor/dtor, `initialize`, `update`, `updateGemCooldowns`, `updateMemorization`, `beginCastFromGem`, `beginCastById`, `interruptCast`, `getCastProgress`, `getCastTimeRemaining`, `completeCast` | lifecycle + the per-frame tick + the cast state machine's own transitions; the entry point a reader looks for first |
| 1 | `spell_cast_validation.cpp` | `adjustTargetForSpellType`, `isValidCreatureTarget` (411–474), `validateCast`, `checkMana`, `checkEndurance`, `checkRange` (476–604) | the pre-cast decision layer. Sits beside its existing sibling `spell_cast_checks.cpp` (`checkLineOfSight` / `checkReagents` / `checkPlayerState` / `countCarriedItem`), which is the same seam already half-cut |
| 2 | `spell_gems.cpp` | `memorizeSpell`, `forgetSpell`, `swapGems`, `getMemorizedSpell`, `getGemState`, `getGemCooldownProgress`, `getGemCooldownRemaining`, `getMemorizeProgress`, `calculateMemorizeTime`, `findGemSlotForSpell` (609–822), `startGemCooldown` (1282–1299), `scribeSpell`, `hasSpellScribed`, `getSpellbookSpell`, `findFreeSpellbookSlot`, `getScribedSpells`, `setSpellbook`, `setSpellGems`, `applyServerGemUpdate` (824–942) | the gem bar and the spellbook — one storage family (`m_spell_gems`, `m_gem_states`, `m_gem_cooldown_*`, `m_spellbook`, `m_memorize_*`). `startGemCooldown` joins them: it is a gem-array writer, not cast flow |
| 3 | `spell_packet_handlers.cpp` | `handleBeginCast`, `handleAction`, `handleInterrupt`, `handleManaChange` (944–1188), `showSpellMessages`, `showBeginCastMessage` (1374–1449) | the four inbound-packet arms. The two message helpers move with them because the handlers are their only callers |
| 4 | `spell_publish.cpp` | `sendCastSpellPacket`, `sendInterruptPacket`, `sendMemorizePacket` (1190–1239), `publishCastStarted`, `publishCastComplete`, `publishGemChanged` (1301–1372) | everything that leaves the class: three outbound packets to the server and three bridge events to the renderer (T44 / U06b / T221) |

Predicted sizes: `spell_manager.cpp` ~460, and the four new TUs ~215 / ~370 /
~345 / ~150 — every one under the 1,000 headroom target the plan asks for.

**Test/golden exposure, checked before editing:**

* No file under `tests/goldens/` mentions a spell TU. `self_death_outbound_g07`
  and `op_save_body_g08` are runtime timelines, not statement-text goldens, and
  a pure body move cannot touch them.
* Exactly one test reads `src/client/spell/spell_manager.cpp` AS TEXT:
  `test_spell_manager_timing.cpp`'s `SpellLandHookIsGone` (T357, B061). Its
  `m_on_spell_land(` clause pins the invocation site inside `handleAction()` —
  which this unit MOVES. The assertion would stay green while pinning a file
  the body left; that is a blunted guard, so the pin follows the body (K03
  correction c). See "Deviations" for what was edited and how both faults were
  re-planted.
* `spell_manager.cpp:NNN` line citations appear in comments in five test TUs.
  Those are prose citations (K05 correction l) and are repointed where the body
  moved.

