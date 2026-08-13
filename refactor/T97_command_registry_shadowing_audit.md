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

(executor fills: the verified shadowed list and how it differs from the
scoping estimate; alias handling; any intra-registry shadowing found; the
per-pair divergence table with the three cases separated; any case where the
DEAD implementation is better than the live one; the disposition options with
risks; deviations)

## Board

Record the reachability table in `refactor/deferred.md`. This is the largest
instance of the implemented-but-unreached pattern found so far — the running
count is around ten individual instances, and this one is structural rather
than incidental.
