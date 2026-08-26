# T292 — remove protectedRegions_ and the dead progressive paths (Will's ruling 2026-08-26)

Slot: slot1. x86. NO eqemu. Class A. Territory:
`src/client/graphics/renderer_zone_stream.cpp` (Priority 3/4 progressive
paths, ~:492 and ~:543), `renderer_zone_lifecycle.cpp` (~:200),
`renderer_sim_bridge.cpp` (~:690), the `protectedRegions_` member and its
two dead counters, tests. Do NOT touch the loot panel (T290 merged) or the
gate metric (T291 slot3).

## The ruling — and its condition
Will, 2026-08-26: **"For protectedRegions_ and the progressive path, if this
was for progressive terrain loading, it can be removed."**
**VERIFY THE CONDITION FIRST.** T79's census says the bitvector has been
permanently all-false since T67 deleted `ConstrainedMeshCache`. Establish
from the code and history what the Priority 3/4 paths were actually FOR:
- If they are progressive terrain/zone-geometry loading (the expected
  answer, given T67's cache removal): **remove them**, with the grep proof
  that nothing else reads the signal.
- If they turn out to serve something else (door culling? PVS?), **STOP and
  report** — the ruling was explicitly conditional, and `getDoorsInRegions`
  at :503 takes `protectedRegions_` as an argument, so check what that call
  degrades to when every bit is false and whether removing it changes door
  behaviour at all.

## Scope
1. Establish the purpose with evidence (T67's commit, T79's census, the
   comments at the four sites).
2. Remove the member, the two dead counters, and the Priority 3/4 branches
   that only exist to consume it. Keep anything with a live consumer.
3. Prove the removal is behaviour-neutral: the branches were dead because
   the signal was all-false, so the kept path is the one that already ran.
   A test or a before/after frame that shows identical output.
4. Line-count delta in the Review.

## Verification
Full ctest (`-j6`); **visual gate PASS with goldens untouched** — this is
renderer code, so a golden change would mean the removal was NOT
behaviour-neutral: STOP and report rather than re-bless. Review; evidence
review/T292/. Commit: `T292: protectedRegions_ and the dead progressive paths removed`.
