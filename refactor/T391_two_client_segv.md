# T391 — the two-client render-thread SIGSEGV (B202)

Class A; slot1; eqemu lock (atomic; must be ABSENT — if present read it
and STOP). Read the B202 board text, `review/T386/README.md` §5 and
`review/T386/logs/fable_spells3_caster_gdb.txt` (the backtrace:
`SimBridgeComponent::applySimulationOutput` ← `applySimulationResults` ←
`FrameComponent::processFrame`, worker parked), `src/client/graphics/sim/`
(the sim bridge, `SimulationOutput`, `computeOnce()`), and T386's rig
notes (`--script` is the two-client driver; `#set invulnerable off`).

1. Reproduce under gdb with the SAME recipe (two clients, qeynos2 or
   arena, 20–60 s after the second zones in); get the faulting line and
   the values (which vector/index; which entity id). Three crashes with
   full backtraces + `info locals` in `review/T391/`. If ASAN builds in
   the slot's budget (T352's recipe used TSAN — the flags are analogous),
   one ASAN run is worth more than three gdb runs.
2. Read `applySimulationOutput` against what the worker wrote: the likely
   shape is an index computed from the entity set the worker saw applied
   to the entity set the render thread now has (a second player's spawn
   / despawn between compute and apply). Name the invariant.
3. Fix at the apply side (validate against the current set; skip stale
   entries) — not by widening a lock across the frame. Test, pre-fix
   failing: a unit test that runs `computeOnce()` over N entities, removes
   or adds one, then applies — must not read out of range (drive through
   the public seam T367's `test_zone_light_culling.cpp` used).
4. Live: a two-client session survives 5 minutes in both zones; capture
   the log. Then take T386's item 3 (B160 fire vs frost on the observing
   client) if time remains — do NOT spend more than 30 minutes on it.
5. Strike B202. Restore every server value; `down.sh`; delete the lock.

STOP: lock present; the fault is not in the sim apply (report the real
frame and STOP — do not fix by argument).
