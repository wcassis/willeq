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

### Leg A — what was done

Five commits, one per extracted TU plus the seam plan. Headers first: the only
edit to `include/client/spell/spell_manager.h` is four banner comments naming
the defining TU (364 lines, up 9 from 355 — all comment). No declaration moved,
so no caller and no test needed a call-site edit.

| commit | TU | lines |
|---|---|---|
| `96212215` | (the seam plan) | — |
| `3a06881c` | `src/client/spell/spell_cast_validation.cpp` | 222 |
| `296e1de3` | `src/client/spell/spell_gems.cpp` | 380 |
| `5031ca9b` | `src/client/spell/spell_packet_handlers.cpp` | 351 |
| `9927d446` | `src/client/spell/spell_publish.cpp` | 148 |

`src/client/spell/spell_manager.cpp` **1,451 → 473**. The family is now six TUs
(the five above plus T246's untouched `spell_cast_checks.cpp`, 171), largest
473, all far under the 1,000 headroom target the plan asked for.

Every body moved VERBATIM. Exactly one edit in the whole leg is not a body
move: `spell_gems.cpp` needed `client/bridge/game_state_bridge.h` and
`client/state/event_bus.h` added, because the `publishGemChanged()` call inside
the moved `startGemCooldown()` dereferences a type the old TU got from a
neighbour's include. The compiler found it on the first build; it is an include
line, not code.

**Zero of the K mechanism's four substitutions were needed** and this is worth
recording, because the plan named the K mechanism. `SpellManager` is a plain
class: the movers stay members of the same class in the same namespace, so
there is no `m_x` → `ctx_.x`, no `Foo()` → `owner_.Foo()`, no `EverQuest::`
scoping and no `this` → `&owner_`. The K mechanism reduces here to its TU-split
half plus the "header first" and "one commit per TU" rules.

### Leg A — tests and goldens

Every spell test passes UNEDITED: `ctest -R 'Spell|spell|Cast|SelfDeath|Buff'`
= **457/457** (6 pre-existing skips). No golden under `tests/goldens/` names a
spell TU; `self_death_outbound_g07` and `op_save_body_g08` are runtime
timelines, which a body move cannot touch, and both pass.

One test WAS edited, and it is the K03-c case:
`tests/test_spell_manager_timing.cpp`'s `SpellLandHookIsGone` (T357, board
B061) slurps `src/client/spell/spell_manager.cpp` as TEXT and asserts
`m_on_spell_land(` is absent. The invocation it pins sat inside
`handleAction()`, which this unit moved to `spell_packet_handlers.cpp`. Left
alone the clause would have stayed green while pinning a file the body had
left — a blunted guard, which is worse than a red one. It now concatenates all
six `SpellManager` TUs.

Re-proved both ways after the edit: the test is green at the tip; planting
`m_on_spell_land(` in `spell_packet_handlers.cpp` turns it red; removing the
plant turns it green again.

### Leg B — the census

`scripts/refactor/check_write_only_fields.py` (commit `a46b40a2`). It parses
every struct/class body under `include/client/` and `src/client/` — **6,414
data members, 4,365 distinct names** — then sweeps `src/ include/ tests/ tools/
benchmarks/`, comment- and string-stripped, classifying each occurrence as a
write (`.name =`, `->name =`, a compound assign, a `: name(...)` / `, name{...}`
constructor initialiser) or a read.

At `4dbeeedb`: **123 write-only** fields and **101 with no occurrence at all**
outside their declaration — 224 rows.

Matching is by identifier, not by receiver type. That is deliberate and
one-directional: a shared name can only ADD reads, so a collision can hide a
write-only field but can never invent one. The output is a floor.

### Leg B — the write-only field table

Eleven deleted (commit `12190da0`), each with its writes, compiler-proven at
build exit 0 with `tools/` in the tree:

| field | classification | action |
|---|---|---|
| `Application::m_lastUpdate` | dead — the live per-frame clock is `m_lastGraphicsUpdate`, the next line | DELETED (+ golden learned in place) |
| `AudioManager::useDbForSounds_` | dead — set true by `loadSoundAssets()`, never consulted | DELETED |
| `CharacterModelView::parentSmgr_` | dead — `init()` stored the parent scene manager; the view uses its own | DELETED |
| `CharacterModelView::modelCenterY_` | dead — "cached model center for zoom"; zoom reads `cameraDistance_` | DELETED |
| `EntityRenderer::nameFont_` | dead — no occurrence at all | DELETED |
| `EntityRenderer::currentCameraRegionIdx_` | dead — redundant sibling of `currentCameraRegion_`, which IS read | DELETED (`setCameraRegion`'s index parameter now named unused) |
| `EquipmentModelLoader::mappingLoaded_` | dead — an `atomic<bool>` set on all three load paths, never checked | DELETED |
| `RenderPassTimer::currentPass_` | dead — the timer reads `passStart_` only | DELETED |
| `PathfinderWaypoint::Implementation::FileName` | dead — the ctor stored the path | DELETED |
| `SkyLoader::wldBuffer_` | dead — no occurrence at all | DELETED |
| `XmiDecoder::ticksPerQuarter_` | dead — no occurrence at all | DELETED |
| `EntityRenderer::skipTextureUpload_` | **should be read** — a real configured value arrives via `setSkipTextureUpload(config_.constrainedConfig.skipEntityTextureUpload)` and does nothing | BOARDED **B291** (A) |
| `SfxManager::cacheMaxBytes_` | **should be read** — `AudioManager` passes the configured sound-cache budget; eviction never compares against it | BOARDED **B292** (A) |
| `ParticleManager::entityDirCallback_` | **should be read** — installed by two callers, invoked by none | BOARDED **B293** (B) |
| `IrrlichtRenderer::forcePvsUpdate_` | **should be read** — set true in four places to force a PVS recompute that never happens | BOARDED **B294** (A) |
| `GraphicsArchiveIndex::lazyMode_` | **should be read** — `buildIndex()`'s `lazyMode` argument is stored and discarded | BOARDED **B295** (B) |
| 10 bridge-event payload fields (`CastingStateChangedData::castTimeRemainingMs`/`castTimeTotalMs`, `SpellMemorizingData::progressMs`, `SpellGemChangedData::memorizeTotalMs`, `SkillActivationFeedbackData::cooldownMs`, `CursorItemChangedData::queueSize`, `TradeskillContainerOpenedEvent::containerName`/`isWorldObject`/`slotCount`, `TradeskillContainerClosedEvent::wasWorldObject`) | **should be read** — the game thread computes them for the renderer to draw; nothing draws them | BOARDED **B296** (A) |
| 22 JSON config keys (`DetailSettings::grassEnabled` …, `BoidsSettings::flockSize*` …, the rain/snow overlay depths and wind tilts, `ConstrainedRendererConfig::enableItemIcons`/`frontToBackSorting`, …) | **should be read** — editing the shipped JSON changes nothing, silently | BOARDED **B297** (B) |
| ~45 loaded-but-never-shown data fields (`ItemInstance` ×21, `SpellData` ×13, `SkillData` ×4, `Entity::guildId`/`anon_status`/`deltaHeading`, `RaceModelInfo` ×2) | **should be read** — item tooltips, spell descriptions, skill gating, the `/who` guild/anon columns | BOARDED **B298** (B) |
| `CombatManager::m_assist_range`/`m_combat_range`/`m_spell_range`/`m_is_resting`/`m_rest_mana_threshold`, `CombatTarget::last_considered` | dead, but `SetAssistRange`/`SetCombatRange` are called only by `test_combat_manager.cpp`, whose own comment says the class has no getters | BOARDED **B299** (B) — deleting them needs a test edit this unit's contract forbade |
| `DDSHeader`/`DDSPixelFormat`, `WldFragment12/36/37Header`, `WldHeader::unk3`, `EQE1Header::reserved`, `Weather_Struct::pad1`/`pad2`, `ActionPacket_Struct` unknowns, `CombatDamage_Struct::unknown06`, `EffSoundEntry::UnkPad*` | **exempt** — serialised wire/file layout must keep its shape | recorded in the board so nobody re-reports them |

The census queue itself is **B290** (P), which also asks for the script to be
wired into ctest as a no-grow ratchet.

### The golden that learned in place (correction 32)

`tests/goldens/application_init_order_g04.txt`: entry 311
`m_lastUpdate = std::chrono::steady_clock::now();` DELETED, entries 312–314
renumbered to 311–313, count **314 → 313**. The change is named at its position
in the golden's `LEARNED EDITS` block and in the guard's `kGoldenSize` comment,
citing T407 and B290. It was NOT regenerated — this is the same shape T347 used
for its own write-only flag two edits above.

Both faults re-planted after the edit, as the rule requires:

* drop a statement from `Application::initialize()` (`m_running.store(true);`)
  → `ComposedStartupOrderMatchesThePreG04Golden` FAILS;
* swap golden entries 310/311 → the same clause FAILS;
* restore both → 4/4 PASS.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 config, RelWithDebInfo) | **exit 0** |
| `ctest -j6` | **4,169 of 4,171** — see below |
| spell family (`-R 'Spell\|spell\|Cast\|SelfDeath\|Buff'`) | **457/457** |
| order/golden/hygiene (`-R 'Order\|Golden\|Registration\|Dispatch\|Reachability\|Duplicate\|FileSize\|Doc'`) | **443/443** |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh fable` | **23 checked, 23 ok, 0 warn, 0 fail** |
| `DocCounts` / `DocCitations` / `DocClaims` | green (8 counts netted, 834 citations, 0 fail) |

The two reds are `visual_gate` and `PackReproducible`, and **both are the
shared-pack drift, not this branch.** This branch touches nothing under `data/`,
`tools/` or `scripts/build_willeq_pack.sh` (`git diff --stat fable..HEAD` over
those paths is empty). While T407 ran, T406 merged onto `fable` (`235b2d40`,
then `7cdf53fa` "Gate: eqe1MappedBytes baselines applied") and installed a new
`/mnt/projects/EverQuest_ETC1/willeq.eqe1` — 2.83 MB → 19.99 MB, written
03:24. So:

* `PackReproducible`: the REBUILD matches this branch's pin exactly
  (`1078463f…`); the INSTALLED pack is `2f76087a…`, which is what the CURRENT
  `fable` tip pins. The branch's pin is stale by one merge, not wrong.
* `visual_gate`: `eqe1MappedBytes` +17,184,047 in all three zones — exactly the
  "+17.2 MB per zone" T406 recorded in `CLAUDE.md` for the RGBA8888 entries,
  and exactly what `7cdf53fa` re-baselined. **Not** B240's +19,343.

Both go green when the orchestrator merges this branch onto the current `fable`,
which already carries T406's pin and the new gate baselines. Nothing here needs
a re-bless.

### Surprises

1. **The K mechanism did not apply, and saying so is the finding.** The plan
   named §Mechanism and its four substitutions; a plain, non-component class
   needs none of them. Worth carrying: check whether the class under a "split by
   function" plan is an `EverQuest`/`IrrlichtRenderer` component BEFORE reaching
   for the context/handle machinery, because the substitution passes are the
   risky part of a K unit and here they would have been pure invention.
2. **The census's first run reported 458 write-only fields; 335 of them were an
   artefact of my own filter.** A heuristic meant to skip the declaration line
   ("identifier followed by `;` with no `=`") also skipped every `return
   field;` — so `SpellManager::m_initialized`, read by a one-line inline
   accessor, came back "write-only", as did `PlayerState::m_name`. The correct
   exclusion is by file+line against the declaration site, which the script
   already did. A field-census tool's failure mode is a plausible-looking list;
   the tell was that obviously-live members appeared in it.
3. **The write-only shape is far commoner than T404's one case suggested, and
   it clusters.** 224 rows, and they are not scattered: bridge event payloads
   (B296), JSON config keys (B297) and DB/wire-loaded record fields (B298)
   account for roughly three quarters. Each cluster is one missing consumer —
   a UI that never drew the payload, a loader whose settings nothing honours —
   not a hundred independent oversights.
4. **`EntityRenderer::currentCameraRegionIdx_` shows how a dead field hides.**
   It is written on the same line as `currentCameraRegion_`, which IS read, in
   all three of its writes. Reading any one of those sites, it looks live.
   Only counting the whole tree separates them.
5. **The exit-143 sweep found a shape the protocol's list already names but
   which is easy to under-run:** four `build/bin` binaries were non-empty and
   non-ELF (the T353 shape), AND three targets were missing outright — the
   absence case. Comparing `ls build/bin` against `main/build/bin` was what
   surfaced the missing three; the ELF scan alone reported them as fine,
   because a glob cannot match a file that is gone.
6. **`ctest -j6`'s first run died at gtest DISCOVERY** on
   `test_entity_size_change` ("Process terminated due to timeout") with an
   intact binary — `--gtest_list_tests` returned its cases in under a second.
   Cold-cache contention, the protocol's second cause; a plain re-run was
   sufficient.

### Deviations

* One test edited, not a golden: `SpellLandHookIsGone` (see Leg A above).
  Flagged rather than absorbed — the plan said the spell tests pass unedited,
  and this one would have, vacuously.
* One golden edited: `application_init_order_g04.txt`, under correction 32,
  which the plan's STOP clause explicitly does not treat as a STOP.
* Leg B deleted 11 of 224 census rows rather than "the dead ones". The plan's
  own escape hatch ("if the list is long, do the deletions in this unit and
  board the rest with the script committed") is what was taken; the remainder is
  a triaged table in B290–B299, not an undifferentiated dump.
