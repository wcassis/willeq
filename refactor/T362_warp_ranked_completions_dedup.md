# T362 — /warp resolves ranked and the prefix resolver dies (B070); getCompletions de-dup shared (B069)

Class B, slot3. Two disjoint items; one branch, two commits. Read the
B070 and B069 board text.

## Leg A — B070

`CommandsComponent::RequestWarp` (`src/client/commands/commands_movement.cpp`)
is the LAST caller of `EntitiesComponent::FindEntityByName`, the unranked
case-insensitive prefix resolver (lowest spawn id wins). Resolve `/warp
<name>` through the ranked lookup (`EntityManager::findEntityByName(name,
px, py, pz)`) at the dispatcher if `/warp` passes through
`ActionDispatcher` (T356 shape), else at `RequestWarp` itself; then delete
`FindEntityByName` (K12 ff check, comment-filtered grep, the compiler).
Test, pre-fix failing: two same-named mobs, nearer spawned second, `/warp`
picks the nearer. Strike B070.

## Leg B — B069

`CommandProcessor::getCompletions` and `CommandRegistry::getCompletions` are
duplicated-then-drifted: the same prefix idiom over names then aliases,
the same private `toLower`, both sort; only `CommandProcessor` de-duplicates.
Extract ONE shared helper (a free function in a small header under
`include/client/action/` or wherever both can include without a new
dependency edge — say which and why) that both call; the helper sorts and
de-duplicates. Test, pre-fix failing: a `CommandRegistry` command
registered with its own name as an alias completes ONCE. Both existing
completion tests and T357's `ChatTabCompletionTest` stay green. Strike
B069.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, file sizes.
`command_registry.cpp` is under `src/client/graphics/ui` → visual gate
standalone + ARM (rebuild all three trees first; willeq.db v6). Check the
slot's CMakeCache points EQT_GLES2_IRRLICHT_DIR at the slot's own
third_party (protocol).

STOP: `/warp` name resolution is server-side and the client never resolves
(then Leg A is delete-only if the resolver has no other caller; say so).
