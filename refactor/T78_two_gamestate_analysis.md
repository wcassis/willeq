# T78 — Two `state::GameState` instances: produce the analysis that makes the decision possible

**Slot: t2. x86 only. No Pi. NO EQEMU.**

**This is an ANALYSIS unit. Production code is READ-ONLY.** Territory is
`refactor/` and a throwaway analysis script under a scratch path. You will
change no behaviour. If you find yourself editing `src/`, you have
misread the unit.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `refactor/deferred.md`'s entry
**"TWO `state::GameState` instances exist, unconnected"**, then
`refactor/T55_position_state_propagation.md` and
`refactor/T58_playerprofile_propagation.md`'s Reviews — T58's Step 1 trace
is where the consequence was first proven.

## Why this unit exists

The board has carried this for months as "needs a decision, not a
drive-by", and it is now the highest-leverage open item — but nobody can
decide it, because nobody has laid out what the decision actually costs.

The facts established so far, which you should verify rather than assume:

- `EverQuest::m_game_state` (`include/client/eq.h:1210`) is a **value
  member**.
- `Application::m_gameState` (`include/client/application.h:437`) is a
  **separate `unique_ptr`**, constructed at `application.cpp:180`.
- **Both production `subscribe()` sites attach to Application's**:
  `console_renderer.cpp` and `automated_mode.cpp`. Nothing subscribes to
  EverQuest's.
- Consequence, proven by T58: `SetPosition`/`SetHeading` fire `PlayerMoved`
  on EverQuest's private EventBus, **which has no production listener**.
  T55 and T58 made the state-layer position writes correct and they still
  have no reader.
- `application.cpp:1793-1797` already carries a comment acknowledging the
  split.

## What to produce

A written analysis, in the plan's Review, that a decision can be made
from. Specifically:

**1. A complete census of both instances.** For each: who constructs it,
who holds references, every reader and every writer. Distinguish
*production* paths from tests and tools. This is the part nobody has
done, and it is most of the unit's value.

**2. What each instance is actually used for today.** They are not
symmetric — EverQuest's appears to back a handful of "Phase 7.x" getters,
while Application's backs the mode/dispatcher/command layers. Say what
each genuinely serves.

**3. The options, with costs.** At minimum:
   - **Converge on one instance** — which one, what has to move, and what
     breaks. Note that `EverQuest` is deliberately graphics-free and
     communicates only via the bridge (`CLAUDE.md`'s "Key separation");
     any convergence must not violate that.
   - **Connect them** — forward events from one bus to the other, or share
     the bus. Cheaper, but say honestly whether it creates two sources of
     truth rather than resolving them.
   - **Delete one** — if one is genuinely vestigial, that is the cleanest
     outcome. Establish whether it is.
   - **Leave them split deliberately**, documented, with the state layer's
     unread writes accepted as cost. This is a legitimate answer and must
     be presented fairly, not as a straw man.

For each: what changes, roughly how much, what regression risk, and what
it unblocks.

**4. What is currently blocked by the split.** T55 and T58's writes are
the known case. Find the others — code written against the state layer
that silently does nothing today. That list is the real argument for
acting, and it may be longer than anyone expects.

## Discipline

**Do not recommend by assertion.** Every claim needs a file:line. This
item has survived months precisely because it has been easier to restate
than to investigate.

**Do not pick for Will.** Lay the options out with costs and say which you
would choose and why — but the decision is his, and a unit that quietly
forecloses options by framing is worse than one that presents them
plainly.

If the census turns up something that changes the shape of the question —
for instance that one instance has no production readers at all — **say so
prominently**. That would make this a much smaller decision than it looks.

## STOP conditions

- Any edit to `src/` or `include/`.
- Any urge to prototype a fix "to see if it works".

## Verification

Build and test only to prove you changed nothing (`git status` should show
only `refactor/`). The deliverable is the analysis.

## Review

(executor fills: the two censuses; what each instance serves; the options
with costs and risks; the blocked-work list; your recommendation and
reasoning; anything that changes the shape of the question)

Board: replace the standing "needs a decision, not a drive-by" entry's
body with a pointer to this analysis, so the next person reads evidence
rather than a restatement of the problem.
