# T304 — Will's three rulings (2026-08-28): death state, race colours, GraphicalMode

**Status: WRITTEN, QUEUED.** Dispatch as slots free, AFTER the 300-commit branch
audit which Will asked for first. Legs are independent and may run in parallel.

---

## Leg 1 — THE DEATH STATE. Owns eqemu (schedule after T301 Leg A releases the lock).

**Will's ruling, and it corrects the orchestrator's proposal:** Titanium has **NO
"release to bind"**. HP reaching 0 puts the player **unconscious and prone with NO
control of the character**. If HP drops further (exact threshold unconfirmed) the
player **dies and is immediately relocated to the bind point BY THE SERVER**.

Consequences for the design, none of which may be re-litigated:
- A client death/unconscious state must **GATE INPUT SELECTIVELY** (Will,
  2026-08-28, clarifying): **chat STILL WORKS while unconscious** — "strangely",
  but it does. What is blocked is **player movement, inventory, spells and the
  hotbar**. So this is NOT a blanket input block: a gate that kills chat too is
  WRONG, and so is one that only plays an animation. The `AT_DIE` inbound handler being animation-only
  (`eq_entities.cpp:774-782`, pushes `EntityDeathAnimation` keyed by spawnId and
  nothing else) is therefore incomplete for the LOCAL player.
- The relocation is the **server's** to perform. The client never initiates it.

**Measure before implementing.** The unconscious/dead distinction is a wire fact,
not a guess: drive the official client on the oracle rig, take a character to 0 HP
and then past it, and record **what the server actually sends at each transition**
— `AT_DIE`? an HP update? a position/appearance change? a zone/relocate? Include
the exact HP threshold if the capture shows it. Video/packet capture, stepped;
stills are barred in this codebase (`dont-sample-transients-with-stills`).
`scripts/eqemu` + `/mnt/projects/eq-oracle` both honour the same lock.

Then implement:
1. **The non-sending internal setter** T234/T237 flagged (`deferred.md:138`) —
   every existing path that sets local position state also SENDS it, so applying a
   server-originated death would echo `AT_DIE` back to the server as if it were
   news. This same setter is what stun and GM-teleport need; build it once.
   Outbound `POS_DEAD` (`eq_movement.cpp:1522-1527`) must keep working unchanged.
2. Adopt the measured state locally for the LOCAL player and gate control.
3. Pin it: a test that a server-originated death sets the state and sends NOTHING;
   a test that movement, inventory, spells and hotbar are refused in that state;
   and — equally load-bearing — **a test that CHAT STILL WORKS in that state**, so
   a later over-broad gate cannot pass. Reach each the way a USER does, per the
   pin-every-fix rule.
4. Enumerate every input path against that split before writing the gate, and list
   it in the Review. Anything not in Will's four blocked categories (movement,
   inventory, spells, hotbar) stays ALLOWED unless the live capture shows
   otherwise — do not extrapolate the blocklist.

## Leg 2 — RACE COLOUR COLUMN. No server.

**Will RULED: add a colour column and populate it with invented values.** The
standing objection ("no colour column exists; filling it would be inventing data",
T271) is **OVERRULED** — invented placeholder tints are acceptable to him.

- Add the column to the race data (`race_models.json` + the willeq.db build), and
  populate every row. These are **placeholder-cube tints**, reached only when no
  model could be built (T213), so the bar is "plausible and distinguishable", not
  "authentic". Say in the Review that the values are invented, by whose ruling,
  and on what date — a future audit must not mistake them for sourced data.
- **Delete `EntityRenderer::getColorForRace()`'s hand-written switch**
  (`entity_renderer.cpp:113`) and read the column instead. **The stale pre-JSON
  numbering in its case comments dies with it** — 21 is BEH Evil Eye not Skeleton,
  26 FRO not Snake, 33 GHU not Ghost, 35 EEL not Rat, 42 WOL not Rat, 43 BEA not
  Bat, 46 IMP not Goblin, 48/49 KOB/DRA not Spiders, 85 SPE not Dragon. Every
  other copy of that numbering (T267, T270, T271) turned out to be LIVE AND WRONG;
  this is the last one.
- Rebuild the db with row-level proof, as T274 did. Goldens: check whether any
  golden frame contains a placeholder cube before assuming no re-bless is needed.

## Leg 3 — GRAPHICALMODE'S SILENT FALLTHROUGH. No server.

**Will (2026-08-28): "used at one point, not sure it's relevant any more."** The
use case he names is **scripting / non-graphical interaction with a running
client** — legitimate, but **not on the radar now**.

So the ruling is neither "delete the capability" nor "wire the adapter":
- **Do NOT delete console/headless interaction**, and do NOT build a roadmap for
  it. Out of scope.
- **Remove the misleading part**: `graphical_mode.cpp:36-50` is a switch where
  `IrrlichtSoftware`/`IrrlichtGPU` `[[fallthrough]]` to the console renderer
  behind a `// TODO: Create IrrlichtRendererAdapter when available`, so a
  GRAPHICAL run silently produces a `ConsoleRenderer` as the command processor's
  sink (`application.cpp:381-385`) while the real Irrlicht renderer is
  constructed and owned by `Application` separately. Make what happens explicit
  at the site instead of implied by a fallthrough, and delete the commented-out
  adapter construction — T241 already deleted its sibling
  `connectRendererCallbacks()`, so it is not coming back in this shape.
- **First establish where slash-command output actually goes in a graphical run**
  and say so in the Review. If it genuinely reaches only a ConsoleRenderer while
  the user is looking at an Irrlicht window, that is a LIVE user-visible defect
  and it is bigger than this leg — report it, do not silently fix or silently
  leave it.

---

## Review — Legs 2 and 3 (T304-leg23, 2026-08-28)

Leg 1 (the death state) is NOT in this unit — it owns eqemu, which another leg
held while this ran.

### Leg 2 — the race colour column

**THE COLUMN'S VALUES ARE INVENTED. They are not sourced from EverQuest data.**
Will ruled on **2026-08-28** that inventing them is acceptable, overruling T271's
standing objection; the bar he set is "plausible and distinguishable", not
authenticity, because the tint is reached only when no model could be built for
a race (T213). That sentence is written, in those terms and with that date, in
**six** places a future audit can hit:

| where | form |
|---|---|
| `config/race_models.json` `_notes` (and `_version` 1.3 -> 1.4) | the data file itself says its colours are invented |
| `include/client/graphics/eq/race_codes.h`, on `getRaceColorARGB()` | the public accessor |
| `src/client/graphics/eq/race_codes.cpp`, on the implementation | the db read |
| `include/client/willeq_database.h`, on `RaceModelInfo::colorARGB` | the struct field |
| `src/client/graphics/entity_renderer.cpp`, on `getColorForRace()` | the consumer |
| `tests/test_race_names.cpp`, above the new colour block | plus the reason the assertions are relational |

**Shape.** JSON key `"color": "#AARRGGBB"` on every one of the 190 race rows;
db column `race_models.color INTEGER` holding the same value packed ARGB;
`RaceModelInfo::colorARGB` (`uint32_t`, 0 = "no colour recorded");
`EQT::Graphics::getRaceColorARGB(uint16_t)` serving it db-only, with
`kDefaultRaceColorARGB = 0xFF969696` (the deleted switch's own generic grey)
when there is no db or no row. `EntityRenderer::getColorForRace()` is now two
lines over that. `kWilleqDbExpectedVersion` 4 -> 5.

**How the values were produced** (`scripts/generate_race_colors.py`, committed so
the method is inspectable and the result reproducible): a curated keyword table
keyed on **each row's own `name`** -- never on a race id -- plus a deterministic
per-id jitter so siblings sharing a keyword stay apart, plus a de-collision pass.
Result: 190 rows, **189 distinct tints**; the single sanctioned duplicate is the
fully transparent `#00000000` shared by 127 IVM Invisible Man and 240 IVM
Teleport Man, the two rows flagged `invisible`.

**Row-level proof** (rebuilt with `willeq_db_builder --eq-path
/mnt/projects/EverQuestP1999 --data-path data --config-path config --output
data/willeq.db`, exit 0, `race_models: 190 entries`):

```
version                         5
count/ non-zero / distinct      190 / 188 / 189
color=0 rows                    127 IVM Invisible Man, 240 IVM Teleport Man
```

Every other table in the rebuilt db was diffed against the committed one
(`.dump` per table, all twelve): **byte-identical**. `race_models` itself was
diffed on all thirteen pre-existing columns: **identical apart from `color`**.
So the db change is exactly the column and nothing else.

**The stale numbering is dead here.** The deleted switch was the LAST surviving
copy of the pre-JSON numbering (T267/T270/T271 killed the other three, all live
and wrong). Checked against the real rows, its case comments were wrong at:
21 "Skeleton" (row 21 is BEH Evil Eye), 26 "Snake" (FRO Froglok), 33 "Ghost"
(GHU Ghoul), 34/43 "Giant Bat"/"Bat" (43 is BEA Bear), 35/36/42 "Rat" (35 is EEL
Eel, 42 is WOL Wolf), 46 "Goblin" (IMP Imp), 48/49 "Spider" (KOB Kobold / DRA
Lava Dragon), 85 "Dragon" (SPE Spectre), 28/77 "Werewolf" (28 is FUN Fungusman;
14 is the Werewolf row), 37 "Giant Snake" (SNA Snake), 39 "Gnoll Pup" / 44 / 87
"Gnoll" (87 is ARM Armadillo), 60 "Skeleton (variant)" (correct), 63 "Ghost
(variant)" (TIG Tiger), 22 "Beetle" (correct), 24 "Fish" (correct). None of that
numbering survives in what this leg leaves behind: the replacement carries the
corrections only as a historical note, and the column is keyed on names.

**BUT ONE MORE COPY OF THAT NUMBERING IS STILL LIVE, OUTSIDE THIS LEG.**
`CombatManager::IsHumanoid()` (`src/client/combat.cpp:1765-1816`) is a hand-
written race-id switch with the same shift, and it is reached by hunt-target
filtering (`combat.cpp:1626`, `:1873`). Spot-checked against the audited rows:
44 "Dark Elf (NPC)" is FPM Freeport Guard, 55 "Freeport Guard" is BGM Human
Beggar, 71 "Human (NPC)" is QCM Qeynos Citizen, 77 "Skeleton" is NGM Neriak
Citizen, 78 "Ghoul" is EGM Erudite Citizen, 81 "Qeynos Citizen" is RIM Rivervale
Citizen, 82 "Erudin Citizen" is SCA Scarecrow, 106 "Fiend" is FEM Felguard,
110 "Erudite Ghost" is MER Mermaid, 111 "Human Ghost" is HAR Harpy, 112 "Iksar
Ghost" is GFM Fayguard, 117 "Iksar Citizen" is GDM Ghost Dwarf, 118 "Forest
Giant" is GEM Erudite Ghost, 120 "Pirate" is WOE Wolf Elemental, 122 "Kerran" is
DRU Dragon Skeleton, 123-127 "Barbarian/Erudite/Troll/Ogre/Dwarf (NPC)" are
INN/UNI/PEG/DJI/IVM, 137 "Yeti" is KGO Kunark Goblin, 139 "Coldain" is ICM Iksar
Citizen, 140 "Velious Dragon" is FGI Forest Giant, 145 "Zombie" is GOO Goo,
146 "Mummy" is SSN Spectral Sarnak. Only 1-12, 85 and 128 happen to be right.
**Reported, deliberately NOT fixed here** -- it is a behavioural change to combat
target selection, outside this leg's scope. It should be its own class-B item.
**CLOSED by T308 Leg 1 (2026-08-28)**: the race data cannot answer "is this humanoid"
(no creature-class field on any of the 190 rows), but the SERVER already does --
`Spawn_Struct` offset 335, eqemu `common/bodytypes.h` `Humanoid == 1`. `IsHumanoid()`
now reads `Entity::bodytype`, no table at all. Pinned by
`tests/test_hunt_target_body_type.cpp` through /pathfinding+/combat+/hunt and the
engaged target. See `refactor/T308_humanoid_and_command_output.md`.

**Goldens: no re-bless, and it was checked rather than assumed.** The gate's
benchmark binary does not link `entity_renderer.cpp` at all (P44b root-caused
this: `nm` shows no EntityRenderer symbols in `gles2_zone_benchmark`;
`zone_bench_loader.cpp:48-79` is a deliberate benchmark-side mirror precisely
because of that), so `getColorForRace()` -- the sole caller of the placeholder
cube path -- is unreachable in every golden and memory-coverage run. **No golden
frame can contain an EntityRenderer placeholder cube.** The gate passes with the
goldens untouched.

**Trap worth recording** (cost one bisect): the visual gate runs
`build-gles2/bin/gles2_zone_benchmark`, a DIFFERENT build tree from `build`.
Bumping `kWilleqDbExpectedVersion` and rebuilding `data/willeq.db` while only
`build` had been rebuilt left the stale `build-gles2` binary expecting version 4;
it silently refused the version-5 db, loaded no character models, and the gate
failed as `characterModelsBytes mismatch` (-1,035,817 bytes, identically in all
three zones) -- which reads exactly like a real memory regression and is not one.
Verification step 3 (`scripts/build-gles2-x86.sh`) must run BEFORE the gate
whenever the db schema version moves.

**Pins** (`tests/test_race_names.cpp`, 6 new tests, all reached the way the
production accessor is): every row serves its own tint and no non-invisible row
is transparent; every tint is distinct; each mis-tinted id differs from the race
the switch borrowed its colour from; the invisible pair stays `#00000000`; no db
and no row both answer the neutral grey. The assertions are **relational, never
on a particular RGB triple** -- so nobody can later cite a test as evidence that
a specific colour is sourced.

### Leg 3 — GraphicalMode's silent fallthrough

**Where slash-command output actually goes in a graphical run, established
first:** `Application::init()` does `m_commandProcessor->setOutputRenderer(
m_gameMode->getRenderer())` (`application.cpp:382-385`), and
`GraphicalMode::initialize()` hands back a `ConsoleRenderer` for every renderer
type. `CommandProcessor::displayMessage()/displayError()` go straight to it
(`command_processor.cpp:253-263`), and `ConsoleRenderer::displaySystemMessage()`
is a `std::cout` write (`console_renderer.cpp:343`).

The three dispatch registries do NOT share a sink:
- registry 1 (`EverQuest::m_command_registry`) and the merged `/help` view use
  `EverQuest::AddChatSystemMessage()`, which pushes a `SystemMessage` event onto
  the bridge -- **it reaches the on-screen chat window**;
- registry 3 (renderer commands) goes to the render thread over the bridge;
- **registry 2 (`action::CommandProcessor`) goes to stdout only.**

So in a graphical run, roughly 60 registry-2 messages -- "Movement mode set to
walk", "Targeting: <name>", "Group disbanded", "You have joined X's group", the
whole pet-command ack family, the `/spellbook` hint, and every `Unknown command`
/ `Usage:` error -- are printed to the terminal while the user is looking at the
Irrlicht window and sees nothing. Registry 2 owns real user-facing commands
(`/say`, `/shout`, `/tell`, `/gu`, `/emote`, `/face`, `/pet`, ...), and
`printCommandResult()` is likewise a stdout ack line, so there is no second path
carrying this text to the window.

**This is a LIVE, USER-VISIBLE DEFECT and it is bigger than this leg. It is
reported here and deliberately NOT fixed** (**CLOSED by T308 Leg 2, 2026-08-28**:
`CommandProcessor` gained a second sink, wired to `AddChatSystemMessage()` in
`Application::wireUnifiedCommandDispatch()`; stdout unchanged; pinned by
`tests/test_command_output_routing.cpp`)**:** the fix is a routing decision
(point `CommandProcessor` at `AddChatSystemMessage()`, or give it a sink that
fans out) that would change what every registry-2 command prints in both
graphical and headless runs, and it needs its own unit and its own pins. Nothing
in this leg makes it better or worse.

**What changed in `graphical_mode.cpp`:** only the misleading part, per Will's
ruling -- the console/headless interaction capability is kept, and no roadmap for
it is implied.
- The `switch (m_rendererType)` with `IrrlichtSoftware`/`IrrlichtGPU`
  `[[fallthrough]]`-ing into the console branch is gone. Every arm produced the
  same two objects, so the construction is now a plain unconditional pair of
  statements with a comment that says out loud that this is a `ConsoleRenderer`
  in every case, that it is NOT the window you see (Application owns the real
  Irrlicht renderer; `m_rendererType` only records which one it asked for), and
  that the console path is deliberately kept and deliberately not built out.
- The commented-out `IrrlichtRendererAdapter` construction and the stale
  `// TODO: Create IrrlichtRendererAdapter when available` are deleted, citing
  T241's deletion of the sibling `connectRendererCallbacks()`.
- The `#ifdef EQT_HAS_GRAPHICS` include of `graphics_input_handler.h` went with
  it -- this mode has never constructed a `GraphicsInputHandler` and nothing in
  the file names one. The `#else` arm disappeared with the `#ifdef`, since both
  arms now build the same thing.
No behaviour change: the object graph a graphical run produces is byte-for-byte
what it produced before.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0** |
| `ctest --test-dir build -j6` | **EXIT 8 -- 3232/3233**, sole failure `AudioManagerTest.PreloadCommonSounds` (the documented ALSA host exception). 6 new tests present and passing. |
| `scripts/build-gles2-x86.sh` | **EXIT 0** |
| visual gate | **PASS**, goldens untouched, no re-bless (see the reachability argument above) |
| `scripts/build-arm-noble.sh` | **EXIT 0** |
| `check_file_sizes.sh fable` | 7 ok, 1 FAIL -- `entity_renderer.cpp` 3211 lines. **Pre-existing**: the file was 3284 lines at the base rev, i.e. already over 3000; this leg REMOVES 73 lines from it. No new FAIL. |

Deviation, flagged: `check_build_tree_orphans` failed on the first full run over
two stale binaries in `build/bin` (`test_first_person_weapon_attachment`,
`test_ui_hide_command`) left by an earlier branch built in this slot -- worktree
contamination, not produced by this unit. Deleted; the test passes.
