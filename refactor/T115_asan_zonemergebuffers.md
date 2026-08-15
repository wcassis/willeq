# T115 — ASAN: find the write that corrupts `ZoneMergeBuffers::ranges_`

**Slot: whichever is free. x86 ASAN first (fast); Pi ASAN if x86 comes back
clean. eqemu lock as needed.**

**Territory: whatever ASAN implicates, sanitizer build config, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T115/`.**

Read T114's Review first (it proved this is *not* a race), then T113's (the
backtrace and reproduction).

## The defect, and what is already ruled out

T113 captured it: **SIGSEGV in `ZoneMergeBuffers::renderMerged()` at
`ranges_.find(sortedEntries[k].buffer)` (`renderer_zone_merge.cpp:230`),
`__x=0x2601`** — a **corrupted red-black tree**, not a missing key. Mid-play,
Pi-only, near Befallen `x≈-333..-400`.

**T114 ruled out the obvious cause.** TSAN reached this path repeatedly and
found **zero races**; every caller is render-thread-only. So this is not
unsynchronised access. **Something else corrupts memory that later manifests
when this map is walked.**

That is exactly what ASAN is for: it catches the **write** that corrupts, where
TSAN and gdb only show the read that crashes.

Rate: **2 of 3 post-T114-fix Pi runs** still hit it, byte-identical to T113's
signature.

## The lead worth starting from

`ranges_` is declared `std::map<const void*, RegionBufferRange>`
(`renderer_zone_merge.h:131`) — **keyed on raw buffer pointers it does not
own.** Writers are `renderer_zone_merge.cpp:131` (insert), `:167`
(`ranges_.clear()`), and reads at `:230`/`:253`.

A map keyed on pointers whose lifetime is managed elsewhere is a classic
use-after-free shape: if a buffer is destroyed without `ranges_` being cleared,
the key dangles — and a later allocation can reuse that address. That is a
hypothesis, not a conclusion. **ASAN will say.**

Note also `:253` uses `ranges_[...]` (`operator[]`, which inserts on miss)
after `:230` already established presence with `find()`. Probably harmless
given the guard, but it is a mutation on a read path and worth a look.

## Try x86 first — it is much cheaper

The crash is Pi-only, but **the corruption may exist on x86 and simply not
manifest** — the Pi's timing widens the window, as it did for the whole race
class. ASAN detects the bad write whether or not it later crashes.

So: **build ASAN on x86, run a zone-in and the Befallen movement tour, and see
if ASAN fires.** If it does, you have the answer in minutes rather than a
multi-run Pi session.

**If x86 ASAN is clean, move to the Pi.** It fits — client 92.9MB, ASAN roughly
doubles it, ~395MB headroom, and `libasan.so.8` is already installed.

## Practical constraints — both machines

- **ASAN roughly halves speed.** Pi zone-in is already ~34s; expect 70s+. **Pad
  scripts accordingly** — a script running dry exits the client and wastes a run.
- **Pi cores and ASAN logs go to NFS, never `/tmp`.** T113 OOM'd the Pi writing
  a core to its 242MB tmpfs.
- **`--log-level=INFO` on the Pi.** T112 measured DEBUG as destabilising — and
  T114 then found why (a real log-buffer race, now fixed), but do not
  reintroduce the variable while chasing something else.
- **No live gdb** during reproduction; it distorts timing.
- Given a 2-of-3 rate, expect several runs. **Report how many runs produced how
  many ASAN reports.**

## What to establish

- **The corrupting write**: what wrote out of bounds or after free, from where,
  in which thread — reported **verbatim** from ASAN's report, including the
  allocation and free stacks it prints.
- Whether `ranges_`' pointer keys are implicated, or whether the corruption
  merely lands there.
- Whether the same root cause explains T113's third signature — the
  **unexplained Mesa/Lima SIGBUS at teardown**.

## Fix the narrowest thing

Once ASAN names the write, fix minimally.

**STOP and report** if the fix requires changing buffer ownership, the zone
merge/rebuild lifecycle, or the GL resource teardown order. Those are their own
units, and a rushed lifetime change can turn a crash into silent corruption.

**Do not paper over it**: no clamping, no skipping the faulting access, no
`try`/`catch`, no disabling merged drawing. If the honest outcome is "ASAN
names it and the fix is larger than this unit," that is a good result provided
the diagnosis is precise.

## Verification — the bar is the Pi

- x86 suite (current total 2266; sole pre-existing failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Consecutive clean Pi runs, stated as a number.** T113 got 0 of 2; T114 got
  1 of 3. **Beat that**, and say by how much. A clean ASAN report with a
  still-crashing Pi means the corruption has another source.
- Visual gate — goldens must not move; merged drawing is what `ranges_` feeds,
  so a golden change here would be a real signal.

## Review

(executor fills: whether x86 ASAN reproduced it or the Pi was needed; the
verbatim ASAN report with allocation/free stacks; the corrupting write and its
thread; whether the pointer keys are implicated; whether this also explains the
Mesa/Lima SIGBUS; the fix and why it qualified as narrow, or the diagnosis if
it did not; **consecutive clean Pi runs**; runs-to-reports ratio; deviations)

## Board

If this closes, say what remains of T114's 16-item inventory. If it does not,
record what ASAN ruled out — that is worth as much as what it finds.
