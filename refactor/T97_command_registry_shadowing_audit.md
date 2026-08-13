# T97 — How much of `CommandProcessor` is unreachable?

**Slot: whichever is free. Headless audit. eqemu only if a spot-check
genuinely needs it — lock normally if so.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T97/`, throwaway
analysis scripts. PRODUCTION CODE IS READ-ONLY. This unit audits and reports;
it changes nothing.**

Read T82's Review (the `/who` shadowing finding), then T96's Review (the
`/pet` one), then this plan.

## Why this unit exists

Twice now a bug has been chased in code that never runs:

- **T82** found `/who`, `/follow` and `/moveto` shadowed — the `game` registry
  (`EverQuest::m_command_registry`) is tried before the `action` registry
  (`CommandProcessor`), so where both define a name, `CommandProcessor`'s is
  unreachable.
- **T96** fixed nine drifted `/pet` literals, then discovered at its live leg
  that `ActionDispatcher`'s pet wrappers have **zero live callers** for the
  same reason. The `game` registry's `/pet` was already correct.

Both discoveries were accidents. A third accident is not a strategy.

## The scale, measured while scoping

Static name overlap between the two registries: **124 names in `game`, 65 in
`action`, 33 in both.** Roughly half of `CommandProcessor` is shadowed:

```
auction camp cast debug decline disband emote filter follow forget gems
gsay gu help interrupt invite loc mem move moveto ooc pet quit reply say
shout sit skills stand stopfollow target tell timestamp who
```

**Verify this list rather than trusting it** — it is a static extraction of
registration literals and may miss aliases, conditional registrations, or
names built at runtime.

## Part 1 — Establish the real reachability set

- Confirm the dispatch order empirically, not just from the source. T82
  established that `Application::printCommandResult()` logs `registry=` for
  every dispatch — that is the ground truth.
- **Account for aliases.** A command shadowed under its primary name may still
  be reachable under an alias the other registry does not define. The `game`
  registry has an `aliasMap_`; check whether `action` does too, and whether any
  alias crosses the boundary.
- Note conditional or ordering-dependent registrations. `G02`
  (`refactor/G02_commands_split.md`) documents an intra-`game` shadowing where
  two `who` registrations collide and the later wins — so shadowing exists
  *within* a registry as well as between them. Report both kinds.
- Produce the definitive list: for each of `CommandProcessor`'s ~65 commands,
  **reachable or not, and why**.

## Part 2 — The valuable part: where do the two implementations diverge?

For each shadowed pair, compare behaviour and record it. This is what turns an
inventory into something actionable.

Three cases, and the distinction matters:

- **Equivalent** — the dead one is redundant. Safe to delete later.
- **The live one is better** — as with `/pet`, where `game`'s used named
  enumerators while `action`'s literals had drifted. The dead one is a liability
  that wastes future effort; T96 spent a whole unit fixing one.
- **The dead one is better** — it may contain fixes, validation or features the
  live one lacks. **This is the dangerous case**, because it means users are
  getting the worse implementation and nobody has noticed. Flag any instance
  prominently.

Do not assume the live one is always right because it is live. Check.

## Part 3 — Report the disposition options, do not act on them

State the options with their risks, and stop:

- **Delete the shadowed implementations** — smallest blast radius, but discards
  work and any better behaviour Part 2 found.
- **Reorder the registries** — makes `action` win. **Global and risky**: it
  would silently change the behaviour of all 33 commands at once, and G02
  documents that registration order is already load-bearing.
- **Merge per command** — keep the better implementation of each pair. Most
  work, best outcome, needs a decision per command.
- **Leave it and document** — cheapest, but guarantees a fourth accident.

**This is Will's call, not yours.** Present the evidence and the trade-offs.

## What you may not do

- **No production code changes.** Not one. Do not delete a shadowed command, do
  not reorder registries, do not "tidy" a duplicate.
- Do not modify the dispatch order to test a hypothesis in a way that could be
  committed. If you experiment locally, revert and say so.
- Do not extend into the bridge/event layer. This is about command routing.

## Verification

Full build and suite to prove nothing broke (expect the current baseline; sole
pre-existing failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds`). Since no production code changes, this
is a formality — put the effort into the audit.

Deliverables to `/mnt/projects/fable/review/T97/` with a README: the
reachability table for all ~65 `CommandProcessor` commands, and the divergence
comparison for the 33 shadowed pairs.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T97-shadowing-audit`, at `fable` tip `8f15a8b0`. Read-only audit — no
production code changed anywhere (`git status`/`git diff --stat` empty
against `fable` throughout). Full evidence, machine-readable tables, and the
attempted live probe log are in `/mnt/projects/fable/review/T97/` (README +
`reachability_table.tsv` + `divergence_table.tsv` + `dispatch_probe.log`);
this section summarizes.

### Verified reachability, and how it differs from the scoping estimate

`action::CommandProcessor` registers exactly **65** commands (confirmed by
counting `registerCommand(...)` calls, `command_processor.cpp`), matching
the plan's "~65" estimate. The `game` registry (`EverQuest::m_command_registry`,
split across `eq_commands.cpp`/`eq_commands_movement.cpp`/
`eq_commands_spells.cpp` since G02) registers **125 primary names + 78
aliases**.

Checking every one of `action`'s 65 commands against `game`'s FULL name
space (primary names ∪ aliases, in both directions):

- **34 are unreachable under every spelling they have** — primary name and
  every alias all claimed by `game`.
- **1 (`shout`) is reachable only through a non-obvious alias**: its
  primary name is shadowed, but `action`'s alias `sh` escapes because
  `game`'s own `shout` alias is spelled `sho`, not `sh`.
- **30 are fully reachable** — no collision on any of their names/aliases.

**This differs from the plan's static list in composition, not count.** The
plan's list (34 words, though its own prose said "33" — an off-by-one in
the plan's count, not a data error) was built from primary-name comparison
only. Checking aliases in both directions swaps one entry: `shout` (in the
plan's list) turns out reachable via `/sh`; `q` (missing from the plan's
list) turns out shadowed — not by a `game` primary name, but by `game`'s
own alias table (`"q"` is an alias of `quit`, registered
`eq_commands_movement.cpp:558-584`; `CommandRegistry::findCommand` checks
`commands_` first, then `aliasMap_`, so `/q` resolves to `game`'s live
`/quit`, never reaching `action`'s separate `"q"` command). Full detail in
`review/T97/README.md`'s "Verifying the static list".

### Alias handling

Both registries have alias tables (`game`'s `aliasMap_`,
`command_registry.cpp`; `action`'s `m_aliases`, `command_processor.cpp`) and
both resolve primary names before aliases in lookup
(`CommandRegistry::findCommand`, `CommandProcessor::processCommand`/
`getCommandInfo`) — plain deterministic `std::map` lookups, read in full.
`Application::dispatchToActionOrRenderer()` calls
`m_commandProcessor->getCommandInfo(name)` (which itself resolves `action`
aliases) to decide whether to even attempt registry 2, so an alias-only
match in `action` is correctly attempted. The two alias-crossing findings
above (`shout`/`sh` escaping, `q` newly shadowed via `game`'s alias table)
are exactly the kind of case a primary-name-only static extraction misses,
as the plan anticipated.

### Intra-registry shadowing — TWO found, not one

G02 documented (and T82 confirmed live) that `game` itself has two
colliding `"who"` registrations (`who_cmd`, Social section vs `who`,
Movement section) where the later registration wins. A full sweep of every
`.name = "..."` assignment across all three `game`-registry files found
**exactly two duplicated primary names in the whole registry: `who` (known)
and `follow` (new, not previously documented anywhere)**. No duplicated
aliases, and no alias colliding with a different command's primary name,
were found.

**The `follow` case is consequential, not just structurally analogous.**
`game`'s Social-section `follow_cmd` (`eq_commands.cpp:607-638`, alias
`fol`) accepts a pending group invite when called with no arguments;
Movement's later `follow` (`eq_commands_movement.cpp:317-335`, no aliases)
unconditionally requires a target name and has no invite-accept branch at
all. Because `RegisterMovementCommands()` runs after
`RegisterSocialCommands()` (G02's driver order), the Movement version wins
`commands_["follow"]` — and since the `"fol"` alias only maps to whatever
`commands_["follow"]` currently holds, `/fol` inherits the same fate.
`action::CommandProcessor::cmdFollow` has the identical invite-accept
fallback, but `action`'s `"follow"` is separately shadowed by `game`'s.
**Net: no reachable spelling of `/follow`, in either registry, under any
alias, can accept a pending group invite today** — a real, user-facing
gameplay gap discovered as a side effect of this audit, not previously
flagged anywhere. T82's live capture (`"Following: X"`, colon, matching the
Movement handler's exact format string) already corroborates the Movement
version is what's live, without having named the mechanism.

### Divergence table — three cases, and where the dead one is better

Full table with file:line evidence per row: `review/T97/divergence_table.tsv`.
Summary by case:

- **Equivalent** (dead is redundant, not a liability): the eight chat
  commands (say/shout/ooc/auction/gsay/gu/tell/reply — all terminate in the
  same `EverQuest::SendChatMessage()` call either way), loc/sit/stand,
  move/moveto/stopfollow, invite/decline, target (case-sensitivity aside),
  who (different scope/format by design, no defect in either), help (both
  sides share the SAME defect — neither registry's `/help` can see the
  other's commands), debug and timestamp (cosmetic differences only), and
  `q` (not really a divergence — resolves through `game`'s alias table to
  the same live `/quit`).
- **Live is better**: `emote` (dead reinterprets 9 words as animations
  instead of real EQ's plain-text emote), `cast`/`interrupt` (dead routes
  through an older `CombatManager`-based casting path, not the live
  `SpellManager` system `/cast` actually uses), `mem`/`gems`/`skills`/
  `forget` (dead is a literal code stub in each — `mem` and `forget` return
  a **false success** without doing anything), `pet` (T96-confirmed;
  this audit adds that even post-T96-fix, dead's vocabulary is a strict
  subset of live's — leader/spellhold/notaunt/nohold/standup never existed
  on the dead side), `filter` (dead is 100% cosmetic, live pushes a real
  renderer-side toggle), `camp` (dead never sits the character, a real if
  minor visual gap), `disband` (dead always sends `SendLeaveGroup()` with
  no leader/member branch — a group leader using it would get the wrong
  packet), and `quit`/`q` (dead is the exact pre-T30 broken advisory-only
  behavior the live registry's own code comment documents having fixed).
- **DEAD IS BETTER**: **none found**, checked deliberately rather than
  assumed — every one of the 34 shadowed pairs was traced to its terminal
  call (or stub) before being classified. This is itself a finding: the
  live registry, whatever its other faults, was never caught holding back
  a strictly worse implementation than the one nobody can reach.

### Disposition options (not acted on)

Per the plan, this is Will's call. Options and risks — see
`review/T97/README.md`'s "Disposition options" for the full writeup:

- **Delete the shadowed `action` implementations** — smallest blast radius;
  loses nothing live users currently get (no dead-is-better case exists),
  but does not by itself fix the `/follow` invite-accept gap (present in
  `game`'s own dead Social handler too) or `game`'s own stub commands.
- **Reorder the registries** — global and risky, exactly as the plan
  warned; G02 already showed order is load-bearing for `who`, this audit
  adds it's load-bearing for `follow` too, and flipping order would
  regress the "live is better" rows (`mem`, `gems`, `skills`, `forget`,
  `pet`, `filter`, `quit`) unless each is re-verified first.
- **Merge per command** — most work, best outcome; this unit's divergence
  table is the direct input such a pass needs. Only `follow`'s invite-accept
  gap needs actual new design work; every other row's "merge" is just
  "delete the dead side."
- **Leave it and document** — cheapest, defers the same discovery cost to
  whichever future unit stumbles into the next shadowed command by
  accident, as T82 and T96 both did.

### Verification

1. `cmake --build build -j24` — clean, no source changes (audit only).
2. `ctest --test-dir build --output-on-failure` — **2102/2103 passed**,
   sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
   First attempt was disrupted by this executor's own stray `pkill -f
   build/bin/willeq` (aimed at a stuck dummy-config probe process)
   incidentally killing a live `HeadlessShutdownTest` subprocess mid-run;
   caught (abrupt truncation, no summary line) and not used — re-run
   cleanly from a fully detached background process with no interference,
   and that clean run is the reported result.
3. `bash scripts/build-gles2-x86.sh` — clean.
4. Visual gate (`visual_gate`, test #2103 above) — PASS, 6.19s; goldens
   untouched (`git status` clean throughout).
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `scripts/refactor/check_file_sizes.sh fable` — "nothing was checked,
   this is not a pass" — correct and expected (zero production-code
   changes this unit, same precedent T82 documented).

### Deviations

1. Stray `pkill` disrupted the first `ctest` run (see Verification #2) —
   caught, not used as evidence, re-run cleanly.
2. **No live eqemu leg was taken.** The plan permits eqemu "only if a
   spot-check genuinely needs it." The one thing needing empirical
   confirmation (dispatch order) was already independently proven live by
   T82 for the identical mechanism (unchanged since), and every relevant
   function (`DispatchSlashCommand`, `dispatchToActionOrRenderer`,
   `CommandRegistry::findCommand`/`executeCommand`,
   `CommandProcessor::processCommand`/`getCommandInfo`) was read in full —
   deterministic map/set lookups, no runtime dynamism. A probe attempt
   without a real server was made (blocked by the pre-connection command
   gate, `Application::processPreConnectionLifecycleCommands()`) and is
   logged in `review/T97/dispatch_probe.log` for the record, but is NOT
   cited as evidence for any claim in this Review — every reachability and
   divergence claim is sourced to a specific file:line read this session.
3. No other deviations. No production code touched. No scope extension
   into the bridge/event layer.

## Board

Record the reachability table in `refactor/deferred.md`. This is the largest
instance of the implemented-but-unreached pattern found so far — the running
count is around ten individual instances, and this one is structural rather
than incidental.
