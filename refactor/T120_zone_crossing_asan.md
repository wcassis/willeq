# T120 — ASAN with a real quarantine, over the zone-crossing tour

**Slot: whichever is free. x86 first and probably x86 only. eqemu lock
required. Pi only for the owed quarantine re-run, if you get that far.**

**Territory: sanitizer/build configuration, `scripts/`, `refactor/`,
`/mnt/projects/fable/review/T120/`. Production `src/` changes only under
"If you find it".**

Read T119's Review first (the `clear()` fix, the 0-of-5 Pi result, and the
fifth signature), then T118's (the `GL_LINEAR` identification and the ASAN
legs).

## Two corrections to carry in — the previous framing was too optimistic

**1. The x86 zone-crossing repro is NOT established as reliable.** It is
`n=1`: one crash on the first pre-fix attempt (`double free or corruption
(out)`, right after `/camp`), and then **two clean post-fix runs of the same
tour**. A repeat under `gdb` did not crash. That is a promising recipe with an
**unknown rate**, not a deterministic reproducer, and this unit must not
build on it as though it were one.

**2. The fifth signature may already be closed by T119's fix.** The one
observed crash was pre-fix; the two clean runs were post-fix. It is entirely
possible that crash *was* the `clear()` use-after-free T119 fixed. Equally
possible it is independent and simply did not recur in two runs. **Nobody
knows, and establishing which is this unit's first job.**

What *is* solid: **zone crossing is a genuinely new ingredient.** Every ASAN
run in T115, T116 and T118 stayed in a single zone and unloaded only at
process exit, so `ConstrainedTextureCache::clear()` never ran with the
renderer still live. That gap is real regardless of the fifth signature's
status.

## The blindfold this unit removes

**T118's Legs 2 and 3 both ran `quarantine_size_mb=0`** (see
`review/T118/run_asan_leg2_m32_lowmem.sh` and
`leg3_pi_asan/run_pi_asan.sh`). That was added to fit memory, and it is
**exactly the option that cripples use-after-free detection**: freed chunks
are recycled immediately instead of held poisoned, so a write into a
reallocated chunk looks legitimate. The two legs that ran at the Pi's bitness
and on the Pi itself had UAF detection largely disabled by the trick used to
make them fit.

Leg 1 (x86-64) did use the default quarantine and was clean — but it never
crossed a zone.

**So no ASAN run in this arc has ever combined a real quarantine with a zone
crossing.** That combination is this unit.

## Step 1 — Establish the x86 rate on current `fable` (no sanitizer)

Before instrumenting anything, find out whether the zone-crossing tour still
crashes at all post-T119, and how often.

- Use `review/T119/tour_zone_cross_x86.txt` as the starting recipe.
- **Amplify the hazard**: extend it to **4-6 zone crossings** per run rather
  than two. Each crossing is another `clear()` with the renderer live, so more
  crossings multiply the per-run hazard without multiplying setup cost. This
  is the single highest-leverage change available to you.
- Run enough times to state a rate — **at least 10**. Report it as N of M.

**This step decides the unit.** If the amplified tour reproduces on x86 at a
usable rate, everything else happens on x86 in minutes per iteration. If it
never reproduces in 10+ runs, say so plainly: that is evidence T119's fix
closed the fifth signature, and it also means the cheap x86 path is weaker
than hoped and the Pi is still the venue.

## Step 2 — ASAN with a real quarantine over the amplified tour

Build x86-64 ASAN and run the amplified crossing tour with **quarantine
enabled** — the default, or explicitly generous (`quarantine_size_mb=256`).
Do **not** trim it for memory on x86; there is no reason to, and trimming it
is what blinded T118.

Also run with `detect_stack_use_after_return=1`, and keep audio **enabled**
(handle the known TinySoundFont overflow with `halt_on_error=0` or the local
uncommitted `nextPos` clamp — **do not disable audio**, that is how the
original blind spot formed).

If ASAN fires, the report with its allocation and free stacks is the answer
this whole arc has been missing. Capture it **verbatim**.

## Step 3 — the owed Pi quarantine re-run (only if x86 stays clean)

T119 left this undone and it is still owed. T118 proved ASAN fits the Pi at
`-g1` (~36.7MB stripped), so rebuild that way and run **with a non-zero
quarantine** — the whole point. Memory will be tighter; trade `malloc_context_size`
and `max_redzone` down before you trade away the quarantine. If it will not
fit with a meaningful quarantine, **say so with the numbers** rather than
silently setting it to zero again.

Use the crossing tour here too, not the single-zone one.

## If you find it

Fix the narrowest thing. **STOP and report** if the fix requires changing
texture or buffer ownership, refcounting semantics, or GL teardown order.

**Do not paper over**: no guard that hides a faulting access, no subsystem
disabled to make a symptom vanish.

**Diagnosis is a valid deliverable.** A precise, evidenced "here is the write,
here is why, the fix is larger than this unit" is a success. This arc's honest
negatives (T115, T117, T118) are what produced every real lead it has.

## What to establish regardless of outcome

- **The x86 crossing-tour rate on current `fable`**, as N of M. This is
  durable: it tells every future unit whether it can work on x86 or must book
  the Pi.
- Whether the fifth signature survives T119's fix, or was closed by it.
- Whether a real quarantine changes ASAN's verdict versus T118's Legs 2/3.
- If ASAN stays clean **with** quarantine **and** zone crossings, say what
  that rules out — that is a much stronger negative than T118's and genuinely
  narrows the field.

## Verification

- x86 suite (current total **2272**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile **if** you
  change production code. If you change none, say so and skip.
- **Visual gate goldens must not move** if you touch rendering. A golden
  change here is a real signal — STOP, do not rebless.
- **Any Pi rate claim uses T117's method**: same tour family, same flags,
  **10 runs**, stated as N of 10 against T117's 2 of 10 and T119's 0 of 5.
  **Do not compare a 3-run or 5-run sample as though it were a rate** — T119
  had to flag exactly this deviation.
- Pi rules: cores and logs to **NFS never `/tmp`**, `--log-level=INFO`, no
  live gdb during reproduction (T119 confirmed again that gdb suppresses this
  defect), teardown by real binary PID (`pgrep -x`), `down.sh`, lock deleted
  and confirmed gone.

## Review

(executor fills: the x86 crossing-tour rate as N of M, pre- and post-
amplification; whether the fifth signature still reproduces post-T119; the
ASAN result **with quarantine stated explicitly**, verbatim if it fired; the
Pi quarantine re-run or precisely why it was not reached; what a clean result
rules out; the fix and why it is narrow, or the diagnosis; deviations)

## Board

State whether the arc now has a cheap x86 reproducer or is still Pi-bound —
that single fact determines how every remaining unit on this arc gets scoped.
If ASAN fired, say which of T117's five signatures the named write explains
and which remain unexplained.
