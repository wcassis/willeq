# B945 — the GameEvent "staging buffer" the issue named is an in-flight zone-in burst, not a retained allocation

Issue #263. Timorous `game_state` `misc` is 6.2 MB at `ready` on x86; the issue
identifies the largest single block in it as a 3.1 MB
`std::vector<eqt::state::GameEvent>` buffer "allocated on the game thread
(`Application::gameThreadLoop()` -> the per-tick event staging), alive at
`ready` in one block: a staging/swap vector whose capacity grew during the
zone-in burst and is never given back."

## Conclusion

**The issue's premise ("never given back") is refuted on both platforms.** The
3.1 MB block is the zone-in spawn burst sitting in the render-side staging
queue (`GameStateSyncBridge::m_forwardQueue`) *in flight*: the game thread
stages it in while the loading thread owns the GL context and the render
thread is not forwarding, so it is alive at the `ready` point and is freed at
the **first `forwardStagedEvents()` after the load** — which the base build
already does. It is a transient, not a retained allocation. The Pi number the
acceptance requires confirms it: on the Pi the `misc` row does not contain the
3.1 MB block at either `ready` or `steady_60s`, and is identical between the
base and this build.

The change in this unit is real but **bounded to steady state**: it returns the
small buffer the drain's own (returned) vector keeps after the burst, so a
later burst does not pay for the previous burst's peak again. It is NOT a
fix that drops `misc at ready` by 3.1 MB — that number is the in-flight burst
at the `ready` instant, unchanged by any steady-state capacity rule (see the
x86 and Pi numbers below).

## What was measured

Matched before/after rendered-client captures on BOTH platforms
(`game_state` owner + `game_state:login_world` trace, the issue's exact recipe;
`WILLEQ_BASELINE=1`), plus a matched `[b945-cap]` diagnostic pair (x86) that
prints the staging queue's retained capacity after every
`forwardStagedEvents()` (5 s cadence), plus the B941 baseline that filed the
issue.

| capture | platform | branch / commit | fix | `[b945-cap]` |
|---|---|---|---|---|
| `b941-orig` | x86 | 01b059fe (B941) | none | no |
| `x86-before` / `x86-before-diag` | x86 (timorous) | 7a621b9c (fable tip) | none | diag only |
| `x86-after` / `x86-after-diag` | x86 (timorous) | 7a621b9c + B945 | yes | diag only |
| `pi-before-ej` | Pi (emeraldjungle) | 7a621b9c (fable, ARM build) | none | no |
| `pi-after-ej` | Pi (emeraldjungle) | 7a621b9c + B945 (ARM build eda2ceaa) | yes | no |

Evidence: `~/kb-evidence/b945-event-staging/{b941-orig,x86-before,x86-before-diag,x86-after,x86-after-diag,pi-before-ej,pi-after-ej}/.../client.log`.
Timorous never reaches `ready` on the Pi (both attempts: phase 11/15, the zone
link dies ~20 s into "Entering world" — the known B925/B940 timorous Pi-lane
class, matching B941's eight failed Pi timorous attempts), so emeraldjungle is
the Pi discriminator (it reaches `ready` + `steady_60s` cleanly on both legs).

### The 3.1 MB block is the in-flight zone-in burst, freed at the first forward

The block (single allocation, `blocks=1`, alloc site
`GameStateSyncBridge::stagePendingEvents()` -> `Application::gameThreadLoop()`,
which is `drainEvents()`'s returned vector) lives in the **render-side staging
queue** `GameStateSyncBridge::m_forwardQueue`: the game thread stages the
zone-in burst into it, and the render thread does not call
`forwardStagedEvents()` while the loading thread owns the GL context
(`Application::render()` returns early), so the burst sits staged for the whole
load window. It is freed at the first `forwardStagedEvents()` after the load.

Timeline, `x86-before-diag` (base, `[b945-cap]` diagnostic):

```
16:39:43.5  [b945-cap] after_forward staged_live=82 retained_cap=0   <- pre-load forward
16:39:43.5  startLoadingThread: zone load #1 for 'timorous'          <- load begins, render returns early
16:39:45.8  [LOADING] Phase 15/15 (100%) Ready!
16:39:45.8  [memledger] point=ready  (misc=6.23 MB; the 3.1 MB block is ALIVE here)
16:39:48.5  [b945-cap] after_forward staged_live=0 retained_cap=0    <- FIRST forward after the load: burst freed
```

The burst (the ~30k spawn events) accumulates in `m_forwardQueue` between the
43.5 s and 48.5 s samples; it is alive at `ready` (45.8 s, inside the load
window) and gone at the first forward (48.5 s), ~2.7 s after `ready` is
recorded.

### The base ALREADY returns it; the fix does not change `misc at ready`

The `[b945-cap]` diagnostic shows the staging queue's retained capacity is
**0 after every forward in BOTH the base and the fixed build** — the swap
`events.swap(m_forwardQueue)` already hands the buffer back at the first
forward. So the 3.1 MB block is a **transient** (in-flight during the load
window), and it is freed by the base already. The capacity this unit returns is
the **steady-state** buffer the drain's own (returned) vector keeps: the base
retains the burst-sized returned vector (probe: 131072 events = 37.7 MB
allocated, re-shrunk to ~128 events = 37 KB per drain); the fix's halving rule
returns it (probe: 128 -> 0 in steady state).

Measured `startup.misc` (the `misc` row the issue names) at the two ledger
points, x86:

| point | b941-orig (B941) | x86-before (fable, no fix) | x86-after (fable + fix) |
|---|---|---|---|
| `ready` | 6,211,520 | 6,226,960 | 6,229,152 |
| `steady_60s`* | 3,907,760 | 3,092,624 | 3,093,552 |

\* `steady_60s` on the x86 VM lane is confounded by the known timorous
zone-link drop (#244/#258): every x86 capture here (including the B941
baseline) hit a `zone_link_lost` re-zone just before `steady_60s`, and the
re-zone frees the staging buffer regardless of the fix. The B941 baseline ran
with audio on (`audio=1526480`); the before/after pair ran `--no-audio`
(`audio=0`), so the two rows are not directly comparable on the audio line.

So on x86 the `misc at ready` number does **not** drop by the buffer's size
(6.23 -> 6.23 MB): the block is the in-flight burst, still alive at the `ready`
instant in both builds, and the base already frees it at the first forward.
The fix's effect is the steady-state capacity (returned vector 37 MB -> 37 KB
per drain), which the ledger does not surface as a separate `misc` delta at
these points because the queue itself is empty at every forward.

## The required Pi number — `misc` is identical, base vs fixed

The acceptance required a Pi (ARM) number. Timorous cannot be measured on the
Pi (it does not reach `ready` — see the capture table), so the Pi leg uses
**emeraldjungle** (the B941 Pi A/B's primary zone), one leg per side, matched
env (nollvm Mesa, 1280×720, `game_state` owner + `login_world` trace,
`WILLEQ_BASELINE=1`):

| point | Pi base (7a621b9c) | Pi fixed (eda2ceaa) | delta |
|---|---|---|---|
| `misc` @ `ready` | 316,944 (0.30 MB) | 317,824 (0.30 MB) | +880 (noise) |
| `misc` @ `steady_60s` | 1,243,736 (1.19 MB) | 1,244,768 (1.19 MB) | +1,032 (noise) |
| `game_state` total @ `steady_60s` | 28,450,392 | 28,457,392 | +7,000 (noise) |

Two readings from the Pi:

1. **The `misc` row does not drop by the buffer's size** — base and fixed are
   identical to within measurement noise. The change does what it was scoped
   to do (bound the retained steady-state capacity of the two staging
   vectors) and nothing measurable more: on the Pi the burst is already
   forwarded — and the buffer freed — at the first render tick after the load
   completes, and emeraldjungle's burst is much smaller than timorous's (its
   `misc` never grows past ~1.2 MB at any point, base or fixed).
2. **There is no persistent retention to fix.** At `steady_60s` the largest
   `game_state` block on the Pi is the 16.4 MB recast navmesh (labeled
   `gs.navmesh`, owned — not `misc`); no multi-MB single-block `GameEvent`
   buffer appears at either point in either build. The issue's x86 "3.1 MB
   misc at ready" was the in-flight burst caught mid-life by the `ready`
   sampling (the B941 timorous trace, where the first post-load forward lands
   ~2.7 s after `ready`), not a steady-state leak. The x86 matched
   `[b945-cap]` diagnostic pair shows `retained_cap=0` after every forward in
   BOTH the base and the fixed build, and the block is gone by
   `steady_60s` in both.

## The change itself (what it does and does not do)

The unit is a bounded hygiene fix, kept because it closes the last theoretical
retention path (a drain that hands back a burst-sized buffer that outlives the
burst) and because the test pins the property:

- `GameStateBridge::drainEvents()` and `GameStateSyncBridge::forwardStagedEvents()`
  each carry a halving rule: a high-water mark of the queue's live capacity,
  read before the swap; when a drain leaves the queue under half of that mark
  and the buffer genuinely retains more than the drained count, the drained
  events move off into an exact-size vector and the old buffer frees. A live
  burst can never force its own shrink (the mark grows with the burst), and
  steady state keeps one allocation tracking the live queue (no re-alloc per
  frame).
- In the live client the two persistent vectors (`eventQueue_`,
  `m_forwardQueue`) already end up holding the small local's buffer after each
  swap, which is exactly why the measured `misc` delta is noise on both
  platforms. The code comment in `drainEvents()` says this plainly instead of
  claiming the 3.1 MB drop.
- The two capacity accessors
  (`eventQueueCapacityBytes()` / `forwardQueueCapacityBytes()`) are test/ledger
  observation windows — classified `observer` in `check_zero_callers.py`.

## Repeatable test

`tests/test_game_state_sync_bridge.cpp` — four B945 cases (the issue's "burst N
events, drain, assert retained capacity bounded") plus the 7 pre-existing:
- `BurstDrainReturnsEventQueueCapacity` — 100k-event burst, drain, trickle; the base queue's retained capacity is bounded by the steady state, not the burst.
- `BurstDrainReturnsStagingQueueCapacity` — the render-side queue (`m_forwardQueue`): burst staged, forwarded, trickle; `forwardQueueCapacityBytes()` returns to the steady state.
- `ASecondBurstStillGrowsAndDrainsAgain` — a second, larger burst still grows to its own size and drains back (no mid-burst shrink, no dropped events).
- `SteadyStateDrainsStopReallocating` — the queue's retained capacity stays at the steady state across further cycles (no re-alloc per frame), and the returned vector keeps its buffer.

11/11 pass (`build/bin/test_game_state_sync_bridge`); the pipeline on this
branch is green — CI pipeline #531 on the code head 428f0ff3 (MR ref
refs/merge-requests/85/head): hygiene, build, test and gate all success;
build-arm and client-ledger-live are manual (`allow_failure`) jobs. (The
citation names the head whose full suite ran; the final head's delta is this
README plus the BOARD entry — docs-only — re-verified by the final head's
pipeline, recorded on the card.)

## Verdict on #263

The 3.1 MB "staging buffer never given back" does not exist as a persistent
allocation: it is the zone-in spawn burst in flight, freed by the base at the
first post-load forward (x86 timorous: ~2.7 s after `ready`; Pi emeraldjungle:
at the first render tick after the load — and its burst never reaches a
multi-MB block). `misc at ready` (x86 6.23 MB → 6.23 MB) and
`misc at steady_60s` (Pi 1.19 MB → 1.19 MB) are unchanged by this unit, by
design. Recommend closing #263 as **not a retention defect**, with the
in-flight burst at the `ready` point recorded as the measurement artifact
(the `ready` ledger point is taken inside the load window on x86, where the
burst is still staged); the bounded steady-state capacity fix and its tests
are merged regardless.

## Not verified

1. **No ARM ctest lane.** The sysroot has no gtest, so the suite (including the
   four new B945 cases in `tests/test_game_state_sync_bridge.cpp`) is never
   executed on ARM: the only ARM evidence in this unit is CI's `build-arm`
   compile plus the rendered Pi captures, and the 11/11 pass above is the x86
   lane.
2. **The x86 `steady_60s` numbers are confounded** by the timorous zone-link
   drop (#244/#258) — footnoted inline in the x86 table above.
3. **The `[b945-cap]` probe + its `probe.cpp` are uncommitted
   working-tree evidence.** Per the no-bulk-logs rule they live only under
   `~/kb-evidence/b945-event-staging/` (probe, probe.cpp, and the seven capture
   directories named in the "What was measured" section) — not on the branch,
   not in CI. The committed tables above rest on those local files; a reader
   who cannot see them cannot re-check the cited log lines.
