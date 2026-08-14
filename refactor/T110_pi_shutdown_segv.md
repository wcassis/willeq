# T110 — The Pi client SIGSEGVs on shutdown

**Slot: whichever is free. Needs the Orange Pi. eqemu lock only if a live
session is required to reproduce.**

**Territory: whatever the backtrace implicates — likely shutdown/teardown
paths. `tests/`, `refactor/`, `/mnt/projects/fable/review/T110/`.**

Read T107's Review first — it found and reproduced this.

## The defect

T107 observed, and **reproduced twice** (a preliminary smoke test and the
paired session), that the ARM client **SIGSEGVs during its own local shutdown
teardown**. In every run, `Logout confirmed by server` and `Game thread
stopped` both appear in the log **before** the crash.

So the server-side logout is clean; the crash is purely local teardown, and it
is **ARM-only** — x86 has never shown it.

## Why it matters, and why it is last of the three

It is after a clean logout, so no game state is lost and it did not affect
T107's measurements. That is why this is the third priority rather than the
first.

But a crash on exit is not cosmetic: it can leave temporary files, skip
flushes that a clean path would perform, and it makes every future Pi session
end in noise that masks real failures. On a device where captures live on a
242MB tmpfs, a process that does not unwind cleanly is a liability.

## Part 1 — Get a backtrace before theorising

Do not reason about likely causes from the source first. **Reproduce under
`gdb` on the Pi and get the actual stack.** T107 already established the
deployment path and that `gdb` works there.

Establish:
- the faulting frame and the call chain into it
- which thread — the render thread, the game thread (already stopped per the
  log), the loading thread, or a library thread
- what is being touched: freed memory, a null, a partially-destroyed object
- whether it is deterministic or timing-dependent across several runs

**Report the backtrace verbatim.** It is the evidence, and a paraphrase is not.

## Part 2 — Why ARM only?

x86 never crashes here, so something differs. Candidates worth checking rather
than assuming: destruction order across translation units; a thread still
running that x86 happens to join first; different driver teardown (native
Mali/Lima GLES2 versus llvmpipe — T107 measured llvmpipe at 69MB on x86 and
0.55MB on the Pi, so these are genuinely different code paths); alignment or
size assumptions that hold on x86_64 and not on 32-bit ARM; or simply a race
that four slow cores expose and twenty-four fast ones hide.

**Say which it is, with evidence.** "Probably a race" is not a diagnosis.

## Part 3 — Fix the narrowest thing

Once the cause is known, fix it minimally and re-verify on the Pi across
several runs — a timing-dependent crash that passes once has not been fixed.

**STOP and report** if the fix requires restructuring shutdown ordering,
the threading model, or the GL context handoff. Those are their own units, and
a rushed change to teardown ordering can turn a crash-on-exit into a hang or a
corrupted save.

## Do not paper over it

- Do not suppress the signal, `_exit()` past the teardown, or wrap it in a
  handler to make the log look clean. The process must unwind correctly.
- Do not "fix" it by skipping the destructor that faults.

If the honest outcome is "diagnosed, fix is larger than this unit," that is a
good result — write the diagnosis down precisely enough that the next unit does
not start from scratch.

## Verification

Full build and suite on x86 (current total 2247; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`) plus the ARM
cross-compile. **Several clean Pi shutdowns in a row** — state how many.

If the cause admits a headless test, add one and **prove it fails in both
directions**. If it does not, say why.

Session discipline: binary freshness against HEAD on both machines; Pi `/tmp`
is a 242MB tmpfs, so pull captures off as you go; if a live session is needed,
take the eqemu lock normally and tear down cleanly; kill clients by real binary
PID; leave the Pi pristine.

## Review

(executor fills: the verbatim backtrace; the faulting thread and frame;
why ARM and not x86, with evidence; the fix and why it qualified as narrow, or
the diagnosis if it did not; how many consecutive clean Pi shutdowns were
observed; any test added and its failability proof; deviations)
