# T142 — Route `spellreload` and `weatherquality` through the bridge

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**Territory: `src/client/eq_commands_movement.cpp`,
`src/client/graphics/irrlicht_renderer.cpp`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T142/`.**

**Will has made this decision — route them through the bridge.** This plan
implements it; it does not re-open it.

Read T139's Review first (the deletion + alias pattern this reuses) and T138's
`needs-decision` rows for these two.

## What they are

Both call graphics singletons **directly from the game thread**, bypassing the
bridge-only rule (`CLAUDE.md` Key Patterns: *"EverQuest communicates with the
renderer only through the bridge"*). The code **self-flags** them at
`eq_commands_movement.cpp:104-107` as *"the two exceptions that called
graphics-only singletons directly."*

- `spellreload` (`:201-208`) — `SpellEffectsConfig::instance().reload()`,
  aliases `reloadspells`, `reloadspellfx`.
- `weatherquality` (`:216-240`) — `WeatherConfigLoader::instance()`, aliases
  `wq`, `weatherpreset`.

## The real win — the cross-thread mutation disappears

T138 found no lock protecting these config singletons against a concurrent
render-thread read. Today the **game thread mutates** state the **render
thread reads**.

**Routing through the bridge moves the mutation onto the render thread**, so
the race dissolves *by construction* rather than by adding a lock. That is the
substantive reason for this change, not tidiness — and it is the same defect
class T113–T131 spent nineteen units on.

**Say in the Review whether a lock is still needed after the move.** If the
answer is no, say why; that is the result.

## The shape — this is T139's pattern, already proven twice

`reloadeffects` and `reloadweather` already work this way. So: **delete from
the game registry, add a `processSlashCommand()` branch.** Registry-3 fallback
carries it.

## THE ALIAS TRAP APPLIES — four aliases, and they will break silently

This is exactly T139's lesson. **The game registry resolves aliases; registry 3
does not** — it forwards raw text to a renderer matching literal names, **with
a leading slash** (`cmd == "/wq"`, not `"wq"`).

**All four aliases must be taught to `processSlashCommand()`**: `reloadspells`,
`reloadspellfx`, `wq`, `weatherpreset`.

And the breakage would be **invisible**: registry 3 is a catch-all reporting
`recognized=true`/`outcome=queued` for anything, so `/wq` would look exactly
like a working command while doing nothing. **Verify the effect, never the
dispatch line.**

## `weatherquality` has a read-back — handle it deliberately

`spellreload` is fire-and-forget. **`weatherquality` is not**: with no
arguments it *queries* `getQualityPreset()` and reports it; the set path also
reads back to confirm.

Checked while scoping: **that survives the move.** `processSlashCommand()`'s
`chat()` helper (`irrlicht_renderer.cpp:1043`) pushes a `SystemMessage` back
through the bridge, so renderer-side output reaches the same chat window.

**But it becomes asynchronous**, and the dispatch line changes from
`outcome=applied` to `outcome=queued`. That is the accepted consequence of the
decision — **state it in the Review** so anyone scripting against these knows.

## Headless

Both handlers have `#else` branches printing *"requires graphics mode"*.
Registry 3 has no such guard, so headless will report `queued` instead — the
same regression T139 recorded. **Note it**; do not silently widen scope to fix
it (the fix site is `application.cpp`, outside this territory).

## Verification

- x86 suite (current total **2411**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile, **and a
  headless configure** since both handlers have `#ifdef` paths.
- **Live: exercise all six names** — both commands plus all four aliases — and
  verify the **effect**, not `registry=`. For `weatherquality`, confirm the
  query form still reports the preset and the set form still changes it.
- **Visual gate goldens must not move.** These configure spell and weather
  effects; a moved golden means behaviour changed — **STOP and report**.
- Atomic: remove from `eq_commands_movement.cpp` in the same commit as the
  `processSlashCommand()` branches.

## Review

(executor fills: both branches and all four aliases, with the effect verified
per name; **whether a lock is still needed after the move, and why**; how
`weatherquality`'s read-back behaves and the `applied`→`queued` change; the
headless note; the new game-registry count; deviations)

## Board

State the remaining game-registry contents. With these two routed, the
programme should be down to **7 `stay-game`** plus whatever `equip` (T141)
leaves — say the number.
