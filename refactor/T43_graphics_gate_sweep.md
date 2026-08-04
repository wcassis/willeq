# T43 — Sweep the `EQT_HAS_GRAPHICS` guards, and pin headless with a test that would have caught them

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg.**
No sibling unit.

**Territory: every `EQT_HAS_GRAPHICS` / `EQT_GRAPHICS` guard outside
`src/client/graphics/`, plus tests.** `src/client/graphics/` guards are
presumed legitimate — spot-check, don't sweep them.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred entry
**"PATTERN: `EQT_HAS_GRAPHICS` gates around non-graphics code — five
instances and counting"**, then P28's Review (it set the rule).

## Why this unit exists

Five separate units have each found one of these by accident, and the
severity escalated every time:

| unit | wrongly gated | headless effect |
|---|---|---|
| P28 | `eq.h` guard blocks | ~33k lines in no test binary |
| T29 | ~2,500 lines of command registration | no slash commands at all |
| T41 | `m_inventory_manager`'s **construction** | manager never built |
| T42 | the `HC_OP_ItemPacket` dispatch branch (+ a larger gate) | inventory packets dropped |
| T42 | `Application::initialize()` never calling `SetEQClientPath()` | **headless could not stay connected to a server** |

Nobody noticed the last one because nothing had tried. That is the shape
of the whole problem: these do not fail loudly, they make headless
quietly less functional than it appears — and headless is the mode the
automation work and the testing campaign run in.

**The rule is already settled** (P28): *only renderer-facing API stays
guarded.* What is missing is that nobody has applied it exhaustively.
This unit does that, and pins it so instance six cannot happen silently.

## Scope

### 1. Inventory and classify every guard

There are ~97 occurrences across 19 files. Concentration is in the game
and boundary layer — `application.cpp` (16), `eq.cpp` (13),
`eq_entities.cpp` (10), `eq_bridge.cpp` (10) — which is exactly where
misclassification bites.

Classify each as:
- **Legitimate** — genuinely renderer-facing API, or code that cannot
  compile/run without the renderer.
- **Misclassified** — non-graphics logic (game state, network, packet
  dispatch, command registration, construction of non-graphics members)
  that merely happens to sit near graphics code.

Produce the classification as a table in your Review. **A guard you
leave in place needs a one-line reason** — that is the artifact that
stops the next person re-auditing from scratch.

Note `eq_bridge.cpp` is the designated boundary TU (CLAUDE.md: "the
graphics-facing game code is concentrated in `eq_bridge.cpp`"), so guards
there are more likely legitimate. More likely, not automatically.

### 2. Fix the misclassified ones

Un-guard them, following P28's direction. Two oracles will catch
mistakes: the `EQT_GRAPHICS=OFF` build (compile errors surface anything
genuinely graphics-dependent) and item 3's test.

**The subtler risk the build will NOT catch**: code that compiles
headless but should not *run* there — most obviously anything pushing to
a bridge or renderer pointer that is null in headless. Null-check rather
than re-guard, and say which sites needed it.

Include the known residual: **all three `OnPetCreated()` call sites in
`eq_entities.cpp` are guarded**, so pet deltas never publish in an
`EQT_GRAPHICS=OFF` build (T41's snapshot fix covers only a pet present at
zone-in).

### 3. Pin it — the part that prevents instance six

Add a regression test that exercises a **headless zone-in end to end and
asserts game state actually populated** — inventory slots, pet, spell
gems, player stats. Three of the five instances above would have failed
such a test immediately.

It must run in the `EQT_GRAPHICS=OFF` tree, so it cannot depend on a
live server. The existing replay seam is the way in:
`EverQuest::ReplayZonePacket` already drives zone packets server-free and
is used by `test_session_replay` and `test_zone_connection_flow` (T41
added snapshot coverage there). Extend that approach rather than
inventing a new harness.

Assert **state populated**, not merely "did not crash" — the whole point
is that these failures are silent.

## STOP conditions

- The misclassified set turns out to be far larger than the ~49
  game/boundary-layer occurrences suggest, such that fixing them all in
  one unit would be unreviewable → STOP, deliver the classification
  table plus the test, and propose a phased split. **The audit alone has
  standalone value**; do not turn this into an unbounded refactor.
- Un-guarding something requires restructuring (not just null-checking) →
  leave it, classify it as "needs its own unit", and say why.
- A guard turns out to protect against something real that neither oracle
  catches → STOP and report; that is a finding worth more than the fix.
- Any behaviour change in the graphics build → STOP. This unit must be
  a no-op for graphics mode; that is the safety property.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest` in **both** trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35),
   the golden replay does not skip (T36); a failure or `Skipped` on
   either is a real signal.
3. Golden replay unchanged and confirmed to have RUN, in both trees.
4. **Graphics mode must be provably unaffected.** Say how you established
   that — visual gate PASS with goldens untouched is the obvious
   instrument, and this is exactly what it is for.
5. **Live headless acceptance**: a real eqemu session in an
   `EQT_GRAPHICS=OFF` build that zones in, stays connected, and shows
   state populated — the scenario that was broken until T42. eqemu LOCK
   FIRST; `up.sh` only; restore Testchar to zone_id=36, x=-77.9232,
   y=21.9086, z=3, h=190; `down.sh` after. Evidence under
   `benchmarks/audits/T43/`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

(Executor fills: the classification table with a reason for every guard
left in place, what was un-guarded, which sites needed null-checks
instead, the pet-delta fix, the regression test and which of the five
historical instances it would have caught, how graphics-mode
no-op-ness was established, and anything deferred to its own unit.)
