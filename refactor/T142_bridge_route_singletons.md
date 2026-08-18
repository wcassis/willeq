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

### The move

`spellreload` (aliases `reloadspells`, `reloadspellfx`) and `weatherquality`
(aliases `wq`, `weatherpreset`) were deleted from the game registry
(`eq_commands_movement.cpp`) and both handlers, plus all four aliases as
literal `cmd == "/alias"` arms, added to
`IrrlichtRenderer::processSlashCommand()` (`irrlicht_renderer.cpp`) directly
below the existing `/reloadweather` branch — same shape as T139's
`reloadeffects`/`reloadweather`. Bodies carried over verbatim except
`AddChatSystemMessage(...)` → `chat(...)` (the renderer-local equivalent
already used by every other branch in that function) and dropping the
`#ifdef EQT_HAS_GRAPHICS` guard (unnecessary — `processSlashCommand()` only
exists inside `EQT_HAS_GRAPHICS` builds already). Three now-dead includes
(`weather_config_loader.h`, `weather_quality_preset.h`,
`environment/spell_effects_config.h`) were removed from
`eq_commands_movement.cpp`; the corresponding includes were added to
`irrlicht_renderer.cpp`. Deletion and addition are in the same commit
(atomic, as required).

### Live verification — all six names, effect verified

`/mnt/projects/fable/review/T142/` has the full A/B evidence (README there
walks through it in detail). Summary:

| name | effect verified | how |
|---|---|---|
| `/spellreload` | yes | `SpellEffectsConfig: Loaded settings from 'config/spell_effects.json'` (GRAPHICS log, identical pre/post) |
| `/reloadspells` | yes | same log line |
| `/reloadspellfx` | yes | same log line |
| `/weatherquality <preset>` | yes | `Weather quality preset set to: <preset>` + 3-line cascade (GRAPHICS log, identical pre/post for `low`/`high`) |
| `/wq <preset>` | yes | same cascade, for `medium` |
| `/weatherpreset <preset>` | yes | same cascade, for `ultra` |
| `/weatherquality` (query) | yes | see "read-back" below |
| `/wq` (query) | yes | see "read-back" below |
| `/weatherpreset` (query) | yes | see "read-back" below |

Method: A/B, not spot-check, reusing T139's exact harness
(`review/T139/extract_responses.py`, copied verbatim into
`review/T142/`). Captured the identical `--script` tour against the
identical live eqemu server twice — once with T142's changes `git
stash`ed out (game registry owns all six names), once restored (renderer
owns them) — diffed the extracted per-command `[GRAPHICS]`-module response
maps (`review/T142/ab_diff.txt`). Two categories of difference, both
expected: the `registry=game`→`registry=renderer` header flip on all six
names, and one line-ordering wobble from best-effort attribution against a
fire-and-forget async queue (same caveat T139 documented) — the *set* of
`[GRAPHICS]` lines across the six-name sequence is identical, not just
similar. **No command's renderer-side effect changed.** Verified the effect,
never `outcome=`/`registry=` (which report `queued`/`renderer` for a
silently-broken alias exactly as convincingly as a working one — the plan's
warning, taken literally).

### Is a lock still needed after the move? No.

Both `SpellEffectsConfig` and `WeatherConfigLoader` are plain singletons
with no internal synchronization. Before this unit, the **game thread**
called `.reload()` / `.setQualityPreset()` (mutating writes) while the
**render thread** read the same singletons every frame it needed spell
particle settings or weather effect parameters (`ParticleManager`,
`WeatherEffectsController` consult these each frame) — that is the
unprotected concurrent read/write T138 found no lock guarding.

After the move, **only the render thread touches either singleton** —
mutation now happens inside `processSlashCommand()`, which
`Application::render()` calls synchronously on the render thread
(registry-3's bridge push is drained there), and every read (particle
spawn, weather effect update) already happened on the render thread. There
is no longer a second thread in the picture for these two singletons at
all. A lock protects against *concurrent* access from *different* threads;
with only one thread ever touching the state, there is nothing left to
protect against — adding a lock here would be pure overhead with no safety
benefit. This is the intended outcome the plan named: the race dissolves
by construction, not by adding a mutex.

(Caveat, for completeness: this reasoning covers these two singletons only.
It says nothing about whether some *other*, still-undiscovered caller
touches them from a third thread — none was found in this unit's search,
and none is architecturally expected, since both are `EQT::Graphics`-
namespaced and every other consumer found is renderer-side.)

### `weatherquality`'s read-back — survives, becomes async

Confirmed live, not just read from source: the query form (`/weatherquality`,
`/wq`, `/weatherpreset` with no args) still reports the current preset, and
the set form still changes it and reports the new value — see
`review/T142/README.md`'s "query form" section for the exact log evidence.

The read-back **survives** because `chat()` (the render-thread helper,
`irrlicht_renderer.cpp:1043`) pushes the report back through the same
`SystemMessage`/bridge path `AddChatSystemMessage()` used, reaching the same
UI chat window.

**It becomes asynchronous**, exactly as the plan anticipated: pre-change,
`CMD outcome=applied registry=game ... cmd="/weatherquality low"`;
post-change, `CMD outcome=queued registry=renderer ... cmd="/weatherquality
low"` for the identical command — live-confirmed in
`review/T142/pre_change_full.log` / `post_change_full.log`. Anyone scripting
against these two commands and depending on the `applied` dispatch line (or
a synchronous reply) needs to account for the queue hop now.

**New finding beyond what the plan flagged**: the live capture also surfaced
that `AddChatSystemMessage()` (the game-thread helper these handlers used to
call) does more than push to the bridge — it also `LOG_INFO(MOD_MAIN, ...)`
directly to console. `chat()` does not. So the query/report messages that
used to appear in the console log (`[MAIN] Weather quality: low`) no longer
do post-move — only in the UI chat window. Checked this is **not new**:
already-bridge-routed `/reloadeffects`/`/reloadfx` (T139) show the identical
gap. Documented in `refactor/deferred.md`, not fixed (fixing it means
touching every `chat()` call site in `processSlashCommand()`, well outside
this unit's diff).

### Headless

As anticipated: registry-3 has no `#ifdef EQT_HAS_GRAPHICS` guard, so a
headless build now reports `outcome=queued` for all six names instead of
the old `#else` branch's "requires graphics mode" message. Confirmed by a
headless configure + build (`-DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=OFF`,
built clean). Not fixed — fix site is `application.cpp`, outside this
unit's territory. Documented in `refactor/deferred.md` (brings the running
total needing that fix to 36 names, T139's 34 + these 2).

### File-size ceiling — pre-existing FAIL, worsened

`bash scripts/refactor/check_file_sizes.sh fable` reports `irrlicht_
renderer.cpp: 3054 lines (> 3000)`. This file was **already over the 3000-
line FAIL ceiling at the `fable` baseline** (3020 lines) before this unit
touched it — not a new pass→fail transition. This unit's two new branches
add ~34 net lines, worsening an already-flagged file. Splitting it is
tracked under `refactor/F_file_split.md`, out of this unit's territory;
noted here rather than silently absorbed or silently fixed out of scope.

### The new game-registry count

Deleting these two brings the game registry down to **8** registered
commands total: the 7 `stay-game` rows from T138's triage (`hotkeys`,
`pmem`, and the 5 audio commands `music`/`sound`/`volume`/`musicvolume`/
`effectsvolume`) plus `help` (T138's third `needs-decision` row, untouched
here — Will's call, not this unit's, and queued next as T143). T141 left 0
`/equip`-family commands in the game registry (fully migrated), matching
the plan's "7 stay-game plus whatever equip leaves" framing exactly:
7 + 0 + 1 (help, still needs-decision) = 8. Verified by grepping
`m_command_registry->registerCommand(` across both remaining registration
files (`eq_commands_movement.cpp`, `eq_commands_spells.cpp`).

### Deviations

None from the plan's shape. Two things worth flagging as new findings
(both documented above and in `refactor/deferred.md`, neither fixed —
out of territory or out of scope):
1. The console-echo asymmetry (`chat()` vs `AddChatSystemMessage()`),
   surfaced by live A/B evidence rather than anticipated by the plan.
2. The pre-existing (not new) file-size FAIL on `irrlicht_renderer.cpp`,
   worsened by this unit's necessarily-in-territory addition.

## Board

**8** commands remain in the game registry: **7 `stay-game`**
(`hotkeys`, `pmem`, `music`, `sound`, `volume`, `musicvolume`,
`effectsvolume`) plus **1 `needs-decision`** (`help`, T138's third
needs-decision row — Will's call, not resolved by this unit; queued next as
T143). `equip` (T141) left **0** — fully migrated to `action::
CommandProcessor`. `spellreload`/`weatherquality` (T138's other two
needs-decision rows) are now resolved and gone from the game registry
entirely, per this unit.
