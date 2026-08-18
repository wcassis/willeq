# T139 — Delete the 34 redundant game-registry commands

**Slot: whichever is free. x86. eqemu lock required (live A/B). No Pi.**

**Territory: `src/client/eq_commands_movement.cpp`,
`src/client/eq_commands_spells.cpp`,
`src/client/graphics/irrlicht_renderer.cpp` (alias handling only — see below),
`tests/`, `refactor/`, `/mnt/projects/fable/review/T139/`.**

Read T138's Review and `review/T138/triage_table.tsv` first — the 34 rows
marked `delete` are this unit's worklist.

## Why this is safe in principle

T138 established, and I verified at its merge, that 32 of the 34 are redundant
duplicates: `IrrlichtRenderer::processSlashCommand()` already natively
implements those names, and `Application::dispatchToActionOrRenderer()`'s
registry-3 fallback reaches it via
`m_bridge->pushEvent(RendererCommand)` → `IrrlichtBridge` →
`renderer_->processSlashCommand()` (`irrlicht_bridge.cpp:720`).

The two pushes are **string-identical** — `rendererCmd()`
(`eq_commands_movement.cpp:470-484`) builds `"/" + cmdName [+ " " + args]`, and
registry 3 builds `"/" + cmdBody`. So the renderer receives the same string
either way.

## THE TRAP — 12 of the 34 carry aliases the renderer does not know

This is the thing that will silently break, and it is why this unit is not a
mechanical delete.

The **game registry** resolves aliases (the `Command::aliases` field). **Registry
3 does not** — it forwards raw text to a renderer that matches on literal
names. I checked `processSlashCommand()`: it knows **none** of these:

| command | aliases lost on deletion |
|---|---|
| `timestamp` | `timestamps`, `ts` |
| `reloadeffects` | `reloadfx`, `reloadparticles` |
| `reloadweather` | `weather_reload` |
| `frametiming` | `frametime`, `ft` |
| `simworker` | `sw` |
| `sceneprofile` | `sp`, `scenebreak` |
| `dumpscene` | `ds` |
| `governor` | `gov`, `budget` |
| `shaderdebug` | `sd` |
| `renderdist` | `clipplane`, `viewdist` |
| `clipdist` | `clip`, `farplane` |
| `detail` | `detaildensity`, `grass` |

**20 aliases across 12 commands.** Verify that list yourself before acting on
it — I derived it by grep and it is the load-bearing input to this unit.

**And the breakage would be invisible.** Registry 3 is a **catch-all**: it
claims *any* unrecognized command and reports `recognized=true`,
`registry=renderer`, `outcome=queued`. Its own doc comment says so — "we cannot
know here whether the renderer recognized it either." So `/sd` after deletion
would report exactly what a working command reports, and do nothing.

**Recommended fix: teach `processSlashCommand()` the aliases**, since that is
where the implementation already lives, and it keeps registry 3's forwarding
dumb. If you find a reason to prefer alias-only stubs in the game registry, say
why. **Dropping the aliases silently is not an option** — if you think any
should be dropped, list them and leave the decision to Will.

## Verification CANNOT use `registry=` or `outcome=`

Those lines will report success for a command that does nothing. **They are
exactly the wrong instrument here.** Verify the **effect**.

**Do an A/B, not a spot check.** Because both paths push a string-identical
event, the renderer's observable response should be **identical before and
after**:

1. **Pre-deletion**: with the current build, drive all 34 command names **and
   all 20 aliases** through a script, and capture the renderer's log output.
2. **Delete**, plus whatever alias handling you land.
3. **Post-deletion**: replay the identical script, capture again.
4. **Diff.** Any name whose renderer-side response changes is a regression —
   investigate rather than accept.

That single before/after pair is far stronger than 34 individual checks and
much cheaper. **Commands with no observable log signal must be listed**, with
how you satisfied yourself about each — a silent command is exactly where a
regression hides.

## A known, smaller consequence — disclose it

`rendererCmd()` is `#ifdef EQT_HAS_GRAPHICS`-guarded: in a **headless** build
it prints *"/x requires graphics mode (not available in this headless build)."*
Registry 3 has no such guard, so post-deletion a headless build will report
`queued` instead of that helpful message.

That is a real, small regression in headless ergonomics. **Say so in the
Review**; do not paper over it. If preserving it is cheap, do it and say how.

## The two dead-on-both-ends commands

`timestamp` and `filter` are dead on **both** sides per T138 (`filter`
correcting a T97 claim). Deleting the game copy leaves the name claimed forever
by registry 3, reporting `queued` for something nothing implements.

**That is arguably worse than the status quo** — it converts a no-op that
reports `applied` into a no-op that reports `queued`, and both lie. Say what
you did and why. Removing the renderer-side remnant too, if there is one, may
be the honest answer.

## Do not

- **Do not delete anything not marked `delete` in T138's table.** The 17
  migrations, 7 stay-game and 3 needs-decision rows are out of scope.
- **Do not "fix" a renderer implementation** you find lacking while in here —
  record it. This unit removes duplication; it does not change behaviour.
- **No papering over**: no alias dropped silently, no `outcome=queued` cited as
  evidence a command works.

## Verification

- x86 suite (current total **2361**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile — note the
  `#ifdef EQT_HAS_GRAPHICS` path means a **headless** configure is worth
  building too, since that is where `rendererCmd`'s guard mattered.
- **Visual gate goldens must not move.** Several of these toggle rendering
  state; if a golden moves, **STOP and report** — that means a deletion changed
  behaviour.
- The A/B log diff, checked into `review/T139/`.

## Review

(executor fills: the 34 deleted with the table row each came from; **the alias
audit — the 20 you found, and what you did with each**; the A/B diff result and
any name whose response changed; commands with no observable signal and how you
satisfied yourself; what you did about `timestamp`/`filter`; the headless
consequence; deviations)

## Board

State the new `eq_commands_*.cpp` command count and what remains of T138's
worklist (17 migrate-action, 7 stay-game, 3 needs-decision). If the alias
question turned into a decision for Will, put it on the board plainly rather
than resolving it by default.
