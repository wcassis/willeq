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

### The 34 deletions

All 34 `delete`-tagged rows from `review/T138/triage_table.tsv` were removed,
verbatim per the table:

**`eq_commands_movement.cpp`** (26 of 34): `timestamp`, `reloadeffects`,
`reloadweather`, `frametiming`, `simworker`, `sceneprofile`, `dumpscene`,
`sort`, `mergedraws`, `screenshot`, `upload`, `portal`, `stencil`, `governor`,
`placeholder`, `plight`, `olight`, `zlight`, `lightmode`, `shaderdebug`,
`fire`, `renderdist`, `clipdist`, `filter`, `sky`, `skytype`.

**`eq_commands_spells.cpp`** (8 of 34): `skills`, `detail`, `togglegrass`,
`toggleplants`, `togglerocks`, `toggledebris`, `season`, `detailinfo`.

No row outside the `delete` set was touched — the 17 `migrate-action`, 7
`stay-game`, and 3 `needs-decision` rows are untouched in both files.
`rendererCmd()` (the helper that built the `RendererCommand` bridge push) is
now dead in both files — every remaining caller in each file was one of the
34 — so it was deleted too, in both files, with a comment pointing at
`Application::dispatchToActionOrRenderer`'s registry-3 fallback as the
replacement path (nothing to build, that fallback already existed and now
does 100% of the forwarding these 34 names used to do explicitly).

### The alias audit — 20 aliases, 12 commands

Verified the plan's alias table myself against both `eq_commands_movement.cpp`
and `eq_commands_spells.cpp`'s `Command::aliases` fields before deleting
anything: 20 aliases across 12 of the 34 commands, matching the plan exactly.
`IrrlichtRenderer::processSlashCommand()` (`irrlicht_renderer.cpp`) already
recognized 2 of the 20 (`timestamps` on `/timestamp`'s branch, `sd` on
`/shaderdebug`'s branch) — the other 18 were resolved **only** by the
game-registry `Command::aliases` field being deleted, so I added each as a
literal `cmd == "/alias"` arm on the existing branch (additive only — no
existing branch body was touched, per the plan's "do not fix a renderer
implementation" instruction):

| command | aliases | disposition |
|---|---|---|
| `timestamp` | `timestamps`, `ts` | `timestamps` pre-existing; `ts` added (see "dead-both-ends" below for why this is aliasing a no-op) |
| `reloadeffects` | `reloadfx`, `reloadparticles` | both added (renderer already had its own separate `particlereload` name — kept, now 3 literal names all reach the same branch) |
| `reloadweather` | `weather_reload` | added (renderer already had `weatherreload`, no underscore — T138 flagged this mismatch as "latent, not live" since `rendererCmd()` only ever sent the primary name; now live via registry-3, so the underscore form needed adding for real, not just to satisfy the letter of "don't drop aliases") |
| `frametiming` | `frametime`, `ft` | both added |
| `simworker` | `sw` | added |
| `sceneprofile` | `sp`, `scenebreak` | both added |
| `dumpscene` | `ds` | added |
| `governor` | `gov`, `budget` | both added |
| `shaderdebug` | `sd` | pre-existing, no change needed |
| `renderdist` | `clipplane`, `viewdist` | both added |
| `clipdist` | `clip`, `farplane` | both added |
| `detail` | `detaildensity`, `grass` | both added (`grass` as an alias for `/detail` is the game registry's own pre-existing design choice, not something introduced here — preserved faithfully even though it reads oddly next to the separate `/togglegrass` command) |

No alias was dropped. Each addition is a single extra `cmd == "/x"` in an
existing `else if` chain in `irrlicht_renderer.cpp` — no new branches, no
behaviour change to what an existing name already did.

### A/B diff result

Built a harness (`review/T139/extract_responses.py`) that walks a captured
log, pairs each scripted `CMD ...` line with the renderer's own
`[GRAPHICS]`-tagged log lines that follow it, and writes a normalized
per-command response map — deliberately **not** the `CMD outcome=`/`registry=`
line itself, per the plan's warning that line lies for a registry-3 catch-all.

Two real problems surfaced building this, both worth recording since they
shaped how much to trust the result:

1. **Zoning-in noise.** The first capture attempt fired commands before the
   client had finished the `befallen` zone load (4046 mesh regions), so early
   commands' response windows were full of unrelated zone-load log spam.
   Fixed by giving the tour script (`review/T139/ab_script.txt`) a 20s
   startup wait plus a `wait 1` between every command, confirmed against the
   actual `OnGraphicsComplete`/`Ready!` timestamp in a throwaway run.
2. **Render-thread attribution is racy, not exact — flagged mid-unit by the
   orchestrator, and confirmed by source reading.** Registry-3 is
   fire-and-forget (no ack path back to the game thread); the renderer drains
   its queue on its own thread on its own schedule. Two periodic,
   command-independent diagnostics were bleeding into unrelated commands'
   response windows purely by proximity: `logFrameTimings()`
   (`renderer_diagnostics.cpp`), a `=== FRAME TIMING BREAKDOWN ===` block
   that fires every ~60 frames once `/frametiming` has been toggled on
   anywhere earlier in the tour, and a `BUDGET: ...ms` frame-budget watchdog
   warning (`renderer_frame.cpp`) — neither is any specific command's own
   reply. Confirmed by reading both call sites (neither is called from
   `processSlashCommand()`). The extractor now filters both out (the frame-
   timing block statefully, from its header line to its `Governor:` closing
   line). **This means per-command attribution in the response maps is best-
   effort, not exact** — a genuinely silent command and one whose reply
   landed one command late look the same to this tool. Where that mattered
   (see "no observable signal" below), I resolved it by checking whether the
   *same* name showed a real signal in *either* run, and by reading the
   underlying handler source directly rather than trusting the window alone.

With both fixed, `review/T139/ab_diff.txt` (668 pre-deletion lines vs 666
post-deletion lines) reduces to exactly three categories of difference:

1. **The expected header change** — `registry=game` → `registry=renderer` on
   all 34 deleted names (`review/T139/pre_deletion_full.log` /
   `post_deletion_full.log` have the full `CMD` lines). This is the deletion
   working as intended, not a finding.
2. **`/skills`'s one real, and harmless, behaviour change.** Pre-deletion:
   silent — the dead `ToggleSkillsWindow` event has zero consumer
   (`irrlicht_bridge.cpp`'s case is `LOG_TRACE(...); break;`), so nothing
   was ever logged anywhere. Post-deletion: `processSlashCommand()` has no
   `/skills` branch (the renderer's real skills-window toggle lives under
   the unrelated name `/newskills` — a separate discoverability gap,
   already on record in `deferred.md` from T138) — the registry-3 fallback
   now reaches the renderer's own `else` arm and logs `Unknown renderer
   command: /skills` (`irrlicht_renderer.cpp:1390`, `LOG_WARN(MOD_GRAPHICS,
   ...)`). Both before and after, `/skills` does **nothing** — the only
   change is going from total silence to a logged "I don't know this
   command," which is strictly more honest, not a regression.
3. **Live-server noise, not code-caused.** Everything else in the diff —
   `Testchar`'s spawn id (218 vs 220, a fresh login each run), NPC
   `placeholder`/`inGraph` flags and positions in the `/dumpscene`/`/ds`
   entity dump, entity/texture/composite-adoption counts, and the
   `/sceneprofile` perf numbers — comes from running two separate captures
   against the same **live**, stateful eqemu server ~3 minutes apart. Mobs
   act, textures stream progressively, and a scene profile measures whatever
   happens to be rendering at that instant; none of this is influenced by
   which registry claimed the command name. The *mechanism* is identical
   either way (same `dumpScene()`/`runSceneProfile()`/etc. call, same
   string reaching `processSlashCommand()`) — only the live data it's
   reporting on moved between runs, which it would do even re-running the
   *same* build twice.

**No command's renderer-side response changed in a way attributable to this
unit's deletions**, other than `/skills`'s harmless silence-to-logged-unknown
shift explained above.

### Commands with no observable log signal

27 of the 55 tour entries (34 names + 20 aliases + duplicates) show
`(no GRAPHICS-module log line observed)` in **both** captures:
`/budget`, `/clip`, `/clipdist`, `/clipplane`, `/detail`, `/detaildensity`,
`/detailinfo`, `/farplane`, `/filter`, `/gov`, `/governor`, `/grass`,
`/lightmode`, `/placeholder`, `/renderdist`, `/sd`, `/season`, `/shaderdebug`,
`/simworker`, `/sw`, `/timestamp`, `/timestamps`, `/toggledebris`,
`/togglegrass`, `/toggleplants`, `/togglerocks`, `/ts`, `/viewdist`.

Read every one of these handlers directly in `irrlicht_renderer.cpp` to
confirm why: each replies **only** via the `chat()` lambda (pushed to the
UI chat buffer via a `SystemMessage` bridge event,
`irrlicht_bridge.cpp:232-238`, which has no `LOG_` call anywhere in its
path) — none of them call `LOG_INFO`/`LOG_WARN` on `MOD_GRAPHICS` or any
other module. This was already true before this unit touched anything; it
is a pre-existing property of these handlers' implementations, not something
the deletions could have changed. The load-bearing fact making silence safe
here: this unit **never modified the body of any existing branch** in
`processSlashCommand()` — only widened the `cmd ==` match on 10 of these 27
branches to accept their aliases (additive), and the other 17 are untouched
byte-for-byte. Since both the pre- and post-deletion runs invoke the
*identical* function with the *identical* string (`rendererCmd()` and
registry-3 both send `"/" + name [+ " " + args]`), a handler that only wrote
to the chat buffer before deletion writes to the same chat buffer, through
the same code, after deletion. I did not find a way to capture the chat
buffer's contents from this console-log-based harness; source identity is
the evidence for these 27, not a captured transcript line.

### `timestamp` / `filter` (dead on both ends)

Left the renderer-side dead stubs (`irrlicht_renderer.cpp:1259-1262`)
**unchanged** rather than deleting them. Reasoning: `Application::
dispatchToActionOrRenderer`'s registry-3 fallback (`application.cpp`) always
reports `outcome=queued` for **any** unrecognized name that reaches it,
regardless of whether the renderer has real behaviour behind that name —
that mislabeling is a structural property of the catch-all itself, not
something specific to `timestamp`/`filter`. Deleting the stub would not fix
the `outcome=` lie (the fix, if wanted, lives in `application.cpp`, which is
outside this unit's declared territory) — it would only delete the one
useful thing these stubs still do, replying with an accurate "not available
(WindowManager removed)" chat message instead of nothing at all. So the
honest-and-cheap move was to keep the stub's message and just stop letting
the game registry hand out a `applied`-labeled false positive for it. Added
the `ts` alias to `/timestamp`'s dead branch anyway (see alias table above),
for consistency with "no alias silently dropped" even though the feature
behind it does nothing — cheap, and avoids leaving a documented alias
unhandled.

### Headless `#ifdef EQT_HAS_GRAPHICS` consequence

Confirmed and **not fixed** — disclosing per the plan's instruction rather
than absorbing it. `rendererCmd()` printed `"/x requires graphics mode (not
available in this headless build)."` in a headless build; registry-3's
fallback (`Application::dispatchToActionOrRenderer`, `application.cpp`) has
no such guard and reports `queued` unconditionally, because `m_bridge` is
always non-null headless too (attached to `ConsoleBridge` instead of
`IrrlichtBridge`, per `application.cpp`'s own comment at line ~31-32) — the
push just lands in a bridge with no renderer behind it to act on it. The
only place this could be fixed is `application.cpp`, which is **not** in
this unit's declared territory (`eq_commands_movement.cpp`,
`eq_commands_spells.cpp`, `irrlicht_renderer.cpp` alias handling only,
`tests/`, `refactor/`, `review/T139/`) — and it would also be a broader
behaviour change than this unit's scope, since registry-3's fallback forwards
*any* unrecognized name, not just these 34, so guarding it headless would
affect commands well outside this unit. Flagging for Will / a future unit
rather than fixing here. Built and confirmed the headless configure step
below to document the current (unguarded) behaviour, not to fix it.

### Verification

- **x86 suite**: 2360/2361 passed after the deletions and alias additions;
  sole failure is the documented ALSA host quirk
  (`AudioManagerTest.PreloadCommonSounds`) — matches the plan's stated
  baseline exactly, no new failures.
- **`visual_gate` test**: PASSED (19.83s) inside the same `ctest` run — no
  golden moved. Confirms none of the toggle-state commands' removal changed
  rendered output (expected, since the same renderer functions still run,
  just reached via a different registry).
- **`build-gles2-x86.sh`**: clean build, used for the A/B capture binary.
- **`build-arm-noble.sh`**: clean cross-compile (see command output in this
  unit's session; no errors).
- **Headless configure**: built clean (see below) — confirms the file split
  still compiles with `EQT_HAS_GRAPHICS` off, exercising the `#else` arm of
  every deleted-then-restored branch this unit's territory touches.
- **A/B log diff**: `review/T139/ab_diff.txt`, `pre_deletion_responses.txt`,
  `post_deletion_responses.txt`, full raw logs, and the extraction/harness
  script are all checked into `review/T139/` — see its `README.md`.

### Deviations from the plan

- Recommended fix ("teach `processSlashCommand()` the aliases") was
  followed as-is — no deviation there.
- The plan asked whether removing the renderer-side `timestamp`/`filter`
  stub "may be the honest answer" — decided **against** removing it, for
  the reason given above (removing it doesn't fix the structural mislabel,
  and only deletes a still-useful message). Flagging this as a judgment
  call rather than a strict plan-follow, since the plan left it open.
- The headless `#ifdef` consequence was disclosed but not fixed, because
  the fix site (`application.cpp`) is out of this unit's declared
  territory — not a deviation, but calling it out per the plan's own
  instruction to say so plainly rather than paper over it.

## Board

**New `eq_commands_*.cpp` command count**: 27 registered commands across
`eq_commands_movement.cpp` (17, down from 43) + `eq_commands_spells.cpp` (9,
down from 17) + `eq_commands.cpp` (1, unchanged) — down from 61 to 27, a
drop of exactly 34, matching the table.

**T138 worklist remaining**: 17 `migrate-action` rows, 7 `stay-game` rows,
and 3 `needs-decision` rows (`help`, `spellreload`, `weatherquality`) are
all still open — none were in scope for this unit and none were touched.

**Nothing new for Will from the alias question** — the plan's "teach
`processSlashCommand()` the aliases" recommendation was followed without
needing a design call; the `timestamp`/`filter` stub-removal question was
resolved as documented above (kept, not removed) with reasoning, not
deferred as an open decision. The one item that *is* a genuine open question,
already on the board from T138 and unchanged by this unit: `/newskills` is a
fully working skills-window toggle reachable only by a user who already
knows to type it (registered in neither the `game` nor `action` registry) —
still a separate discoverability gap, not resolved by deleting the dead
`/skills` here.
