# T88 — Is the position frame actually broken, or did we mis-drive it?

**Slot: slot1. Headless first. Live leg only if Part 3 requires it — and
eqemu must be free and locked normally if so.**

**Territory: `src/client/eq_movement.cpp`, `src/client/eq_action_handler.cpp`,
the `/move` command surface, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T88/`.**

Read T86's Review, then T87's Review **including the merge commit's
qualification of it** (`git log 381a8bd1`), then this plan in full before
touching anything.

## Do not assume there is a bug

T86 concluded WillEQ reports a position ~1300 units from where it believes it
is, axis-mirrored, and that this is "a real WillEQ-side defect." **That
conclusion is not established, and your first job is to determine whether it
is true at all.**

Here is the competing explanation, which fits every observed number without
any production defect:

- `/move` → `EqActionHandler::moveToLocation()` → `EverQuest::Move()` →
  `MoveToWithPath()` operates in **client** frame — the frame of `m_x`/`m_y`.
- The T85 join spec gave `Oracledelta`'s position as
  `zone 1 (qeynos) x = -653 y = 314 z = 3.75`, and T86 confirmed those numbers
  match `Oracledelta`'s own **wire** self-report — i.e. **server** frame.
- T85's executor issued `/move -650 310 3.75`, feeding **server-frame numbers
  to a client-frame command**.
- Inbound convention is `m_x = server_y`, `m_y = server_x`. Outbound
  (`eq_movement.cpp:1064-1068`) sends `x_pos = m_y`, `y_pos = m_x` — a correct
  inverse that round-trips cleanly.
- So `/move -650 310` sets client `(-650, 310)` and correctly transmits server
  `(310, -650)`. Observed wire value: `(306.462, -646.949)`. **That is exactly
  what a correct client does when handed mirrored input.**

Under that reading WillEQ did precisely what it was told, and the fault is a
coordinate-frame confusion in the harness.

**Establish which it is before changing a single line.** Both outcomes are
valuable and neither is embarrassing; shipping a "fix" for a non-bug is.

Note also a known EQ trap worth checking while you are here: the official
client's `/loc` conventionally reports **Y, X, Z** order, not X, Y, Z. If the
oracle session read those coordinates off a client UI, the frame labelling in
the join spec may itself be muddled. Do not treat any documented coordinate
as frame-labelled unless you can show where it came from.

## Part 1 — Prove the round trip, headlessly

Before any live work, establish with a test what the code actually does:

- Feed a known server-frame position through the inbound
  `ZoneProcessPlayerProfile` path, then read what `SendPositionUpdate()` would
  transmit. **Assert identity.** If it round-trips, the encoder and decoder
  are a matched pair and the defect is not there.
- Do the same through `Move()`/`MoveToWithPath()`: set a client-frame target,
  run the movement update, and check the transmitted position corresponds to
  that target under the documented convention.

This is cheap, deterministic, and it settles Part 1 without a server. **These
tests should survive as regression tests regardless of the outcome** — nothing
currently pins this round trip, which is why the ambiguity persisted.

## Part 2 — The real defect may be the interface, not the math

Even if the math is correct, **the frame is undocumented at the command
surface, and that ambiguity is what actually cost us a live session**. `/move
-650 310` is accepted silently whichever frame the caller meant.

If Part 1 shows the math is sound, treat this as the finding and fix *that*:

- Document the frame explicitly at `EverQuest::Move()`, `moveToLocation()`,
  and the `/move` command registration — client frame, with the relationship
  to server frame stated.
- **Log the frame conversion** when a move is issued: target in client frame,
  and the server-frame position it will transmit. One log line would have made
  T85's problem obvious within seconds instead of costing a capture session
  and two analysis units.
- Consider whether `/move` should echo back where it thinks it is going in
  both frames. Propose; do not build a new command surface unasked.

## Part 3 — Only if Part 1 finds a genuine defect

If the round trip does *not* hold, you have found a real bug. Then:

- State it in one sentence with the failing round-trip test as evidence.
- Fix the narrowest thing that makes the test pass.
- **STOP and report instead of fixing** if the fix requires changing the
  coordinate convention broadly. That convention is threaded through every
  consumer in the client — renderer, pathfinding, BSP, audio, navmesh (see
  `docs/CLAUDE.md`'s coordinate table) — and a broad change is its own unit
  with its own review, not a side effect of this one.
- A live leg to confirm against the server is warranted **only** in this
  branch. Take the eqemu lock normally if so; it is free now.

## What you may not do

- **Do not touch heading or rotation code.** The `90 - server_heading_deg`
  player offset is deliberate and hard won — see the load-bearing comment in
  `src/client/eq.cpp` and `docs/CLAUDE.md`'s Heading/Rotation Conventions.
  This unit is about **position**. T87's apparent ~252° heading offset is
  **an artifact** of exactly the frame confusion under investigation here —
  its Pass A cardinals are spaced 59°/74°/101° where the official client's are
  spaced 92°/90°/89°, and a constant offset preserves spacing. **Heading is
  re-measured after this unit, not during it.**
- Do not "fix" T87's 252°. It is not a real transformation.
- Do not change the coordinate convention broadly (see Part 3 STOP).

## Verification

- Full build and suite (expect 2066/2067; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`). New round-trip tests must pass and
  must fail if the conversion is inverted — **prove that by temporarily
  inverting it and showing the test goes red**, then restore. A test that
  cannot fail is not a test.
- Visual gate; if goldens change, say so explicitly and explain why.
- Findings to `/mnt/projects/fable/review/T88/` with a README.

## Review

(executor fills: whether the round trip holds; the one-sentence verdict on
whether T86 found a production defect or a harness frame confusion; what was
documented or logged; what was fixed if anything and why it qualified as
narrow; proof the new tests can fail; whether the `/loc` Y,X ordering trap is
implicated; deviations)

## Board

Record the verdict against T86's entry in `refactor/deferred.md` — it
currently states this as a confirmed WillEQ defect, and if that is wrong it
must not be left standing. Also record that heading re-measurement is blocked
on this unit's outcome.
