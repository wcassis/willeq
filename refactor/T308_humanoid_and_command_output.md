# T308 — two class-A defects T304 Leg 2 found on its way past

Both were reported, deliberately unfixed, in `refactor/T304_wills_three_rulings.md`'s
Review. No server, no Pi.

## Leg 1 — `CombatManager::IsHumanoid()` carried a FIFTH live copy of the pre-JSON race numbering

`src/client/combat.cpp:1765-1816` at the base rev, reached by hunt-target filtering
(`IsTargetSuitableForHunt`, `:1626`) and by `ListHuntTargets()` (`:1873`). Same shift
as the four copies T267, T270, T271 and T304 killed. Fix db-driven, not by writing a
sixth table; if the race data cannot answer "is this humanoid", say so and propose the
smallest honest alternative. Pin through a caller-reachable path, not the predicate.

## Leg 2 — ~60 user-facing messages printed where a player never sees them

Registry 2 (`action::CommandProcessor`) wrote only to the `ConsoleRenderer` that
`GraphicalMode` hands `Application` (`application.cpp:382-385` →
`command_processor.cpp:253-263` → `console_renderer.cpp:343`, a bare `std::cout`),
while registry 1 and the merged `/help` reach the chat window and registry 3 reaches
the render thread. Get the routing shape right rather than patching call sites; do not
break `--script` or the console mode T304 Leg 3 preserved.

---

# Review (T308, 2026-08-28)

## Leg 1 — the race data cannot answer it; the SERVER already does

**Answer to the question the plan asked: no.** `config/race_models.json` carries
`code`, `name`, gendered codes, `s3d_file`, `also_in`, `fallback_code`,
`animation_source`, `animation_source_s3d`, `scale`, `invisible`, `alias_of` and (since
T304) `color`. There is no creature-class, body-type or humanoid field on any of the
190 rows, and `RaceModelInfo` mirrors exactly those columns. Adding one would have been
a sixth hand-curated table keyed on race id — the thing this defect family IS.

**The smallest honest alternative turned out not to be a table at all.** The server
sends the answer already: `Spawn_Struct` carries `bodytype` at offset 335,
`eq_entities.cpp:341` and `:1148` parse it into `Entity::bodytype` for every spawn, and
eqemu stamps it from `common/bodytypes.h` where `Humanoid == 1`. So:

```cpp
bool CombatManager::IsHumanoid(const Entity& entity) const {
	return entity.bodytype == EQT::BodyType::Humanoid;
}
```

The signature moved from `uint16_t race_id` to `const Entity&` (all three call sites
already had the entity in hand). `bodytype 0` is not a valid eqemu value — it means no
spawn data was recorded — and answering `false` there preserves the deleted switch's
own default for an unknown race.

**A NEW header, and it is a deletion not an addition.** `include/client/eq_body_types.h`
holds the numbering, transcribed from eqemu's `common/bodytypes.h`. It exists because
the client already had **three** hand-written copies of that numbering and **all three
were wrong**:

| copy | error |
|---|---|
| `EverQuest::GetBodyTypeName()` (`eq_strings.cpp`) | **off by one for every value** — `case 0` returned "Humanoid", so a real Humanoid printed as "Lycanthrope", an Undead as "Giant", an Animal as "Luggald". User-visible on `/entityinfo`'s "Body Type:" line. **FIXED here**, it is now a one-line delegate to `EQT::BodyType::Name()`. |
| `SpellManager::CheckTargetBodyType()`'s local `BT_*` constants (`spell/spell_manager.cpp:456-469`) | off by one the same way (`BT_HUMANOID = 0`, `BT_UNDEAD = 2`, `BT_ANIMAL = 20`). **NOT fixed** — see below. |
| `SpellTypeProcessor::` (`spell/spell_type_processor.cpp:585-591`) | a THIRD, differently-wrong set: `21` for Plant (really Insect; Plant is 25) and `22` for Giant (really Monster; Giant is 4). Only its `26` for Dragon is right. **NOT fixed.** |

**The two spell copies are reported, not fixed, and they are their own class-A item.**
They decide which creatures a creature-restricted spell may target — an undead nuke,
an animal charm, a Plant/Giant/Dragon-only line. Correcting them changes spell
targeting behaviour in a live session and needs its own pins and its own live check;
folding it into this unit would have been a silent behavioural change ridden in on a
combat fix. Both should be repointed at `client/eq_body_types.h`, which is why the
header carries their names in its comment.

**What the dead switch was actually saying.** Spot-checked against the audited
`race_models` rows: 44 "Dark Elf (NPC)" is FPM Freeport Guard, 55 "Freeport Guard" is
BGM Human Beggar, 71 "Human (NPC)" is QCM Qeynos Citizen, 77 "Skeleton" is NGM Neriak
Citizen, 78 "Ghoul" is EGM Erudite Citizen, 81/82/106/110/111/112 likewise shifted,
117 "Iksar Citizen" is GDM Ghost Dwarf, 118 "Forest Giant" is GEM Erudite Ghost,
120/122/123-127/137/139 shifted, 140 "Velious Dragon" is FGI Forest Giant, 145
"Zombie" is GOO Goo, 146 "Mummy" is SSN Spectral Sarnak. Only 1-12, 85 and 128 were
right. The corrections survive only as a historical note in the new comment; nothing
in the shipped code is keyed on a race id any more.

**Pin — `tests/test_hunt_target_body_type.cpp`, 8 tests.** Every hunt assertion drives
the user's own sequence: `/pathfinding on` (`SetPathfinding`), `/combat on`
(`Enable`), `/hunt on` (`SetAutoHunting`), a game tick, the server's consider answers,
another tick — and asserts on the target the client ENGAGES, read from
`EverQuest::GetGameState().combat().targetName()`, the public projection `SetTarget()`
publishes. `IsHumanoid()` is never called by the test. Spawns are real `NewSpawn`
packets (`zone_stream_test_utils.h`, extended with the race id and the offset-335
body type). Clock gates are backdated, the technique `test_combat_manager.cpp`
established, so nothing sleeps.

**RED verified, not assumed.** With the old race switch pasted back in and everything
else unchanged: **5 of the 8 fail** — the Goo, the Ghost Dwarf, the Forest Giant, the
two-mob preference test, and the "same race id, different body type" test. The two
"still refused" cases (Freeport Guard 44, Neriak Citizen 77) pass in both, which is the
point: the table got those right by luck and must keep getting them right.

## Leg 2 — registry 2 answers into the chat window, and still into stdout

**Shape: a second sink, not a swapped one.** `CommandProcessor` gains
`setMessageSink(std::function<void(const std::string&)>)`. `displayMessage()`,
`displayError()` and the echo path all funnel through one new private
`emitMessage()`, which writes the `IOutputRenderer` (unchanged, first) **and** the
sink. `Application::wireUnifiedCommandDispatch()` points the sink at
`EverQuest::AddChatSystemMessage()` — the same call registry 1 and the merged `/help`
already use to reach the on-screen chat window.

The wiring lives in `wireUnifiedCommandDispatch()` on purpose: that function is
already the place that connects the chat window's typed input TO registry 2 (the
unrecognized-command fallback). Both halves of that conversation are now set up in one
production function, which is also what makes the pin drivable without a live
`initialize()`.

**Nothing was taken away from stdout.** `setOutputRenderer()` is untouched and is still
written first, so `--script` runs, piped-stdin runs and the console mode T304 Leg 3
deliberately preserved emit exactly the lines they emitted before. `printCommandResult()`
was left alone deliberately: `CMD outcome=... registry=... recognized=...` is T29's
machine-readable automation contract, not a message to a player, and moving it into
chat would break the thing it exists for.

**Known, accepted duplication:** `AddChatSystemMessage()` also does a `LOG_INFO`, so in
a headless run a registry-2 message now appears once from the ConsoleRenderer and once
from the log. Registry 1 has always behaved that way; the alternative was making the
sink conditional on graphics, which would have made the defect untestable without a
window.

**Pin — `tests/test_command_output_routing.cpp`, 4 tests.** The user path, not an
internal one: text goes into `EverQuest::ProcessChatInput()` — the chat window's own
entry point — misses registry 1, reaches registry 2 through the production fallback,
and the assertions are on the `SystemMessage` events on the bridge, i.e. what the chat
window would render.

- `/tell` typed bare → the "Usage:" error is in the chat window, and is NOT reported as
  an unknown command;
- `/spellbook` → the success family (`displayMessage`, ~60 strings: movement-mode acks,
  "Targeting: X", group/pet acks, this hint) lands in chat too;
- with a real `eqt::output::ConsoleRenderer` attached, ONE `/tell` produces the "Usage:"
  line on captured stdout **and** in the chat feed — the no-regression half;
- a genuinely unknown name still gets `ProcessChatInput`'s own "Unknown command"
  message, unchanged.

A `friend struct ApplicationCommandOutputTestPeer` builds the four objects
`Application::init()` builds between its `EverQuest` and its `CommandProcessor` and
then calls the real `wireUnifiedCommandDispatch()`; `init()` itself cannot run under
ctest because it connects.

**RED verified.** With the sink wiring disabled and nothing else changed, 3 of the 4
fail (both chat assertions and the both-sinks-in-one-call test); only the "Unknown
command" test passes, since that path never involved registry 2.

## Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0** |
| `ctest --test-dir build -j6` | **EXIT 8 — 3244/3245**, sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host exception). 12 new tests, all passing. |
| `scripts/build-gles2-x86.sh` | **EXIT 0** |
| visual gate | **PASS**, goldens untouched (`git status` clean under `tests/goldens/`) |
| `scripts/build-arm-noble.sh` | **EXIT 0** |
| `check_file_sizes.sh fable` | 5 ok, 2 warn, **1 FAIL** — `src/client/action/command_processor.cpp` 3024 lines. **Pre-existing**: the file is 3011 lines at the base rev, already over 3000. This unit adds 13. Flagged, not absorbed. |
| `check_build_tree_orphans` | PASS (200/200; no stale binaries in this slot) |

`build-gles2/` was rebuilt before the gate, per the trap T304 recorded.

Deviation, flagged: the file-size FAIL above. Also flagged: `EverQuest::GetBodyTypeName()`
was corrected in this unit although it is not in either leg's scope — it is the third
copy of the same numbering, it shares the new header, and the change is a display string
with no behavioural reach. The two spell copies, which DO have behavioural reach, were
left alone and written up above.
