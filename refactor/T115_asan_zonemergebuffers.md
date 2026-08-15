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

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T115-asan-zonemerge`, base fable tip `3cdc7263`. Live eqemu (lock free
at session start; taken, used, released cleanly — `down.sh` run, lock file
deleted, confirmed gone) and live Orange Pi (deployed 3 times, exercised,
left pristine — governor `ondemand`, `/tmp` 0% used, `core_pattern` restored
to `core`; no leftover `willeq` process). Full logs, ASAN reports, README:
`/mnt/projects/fable/review/T115/`.

**Headline: `ZoneMergeBuffers::ranges_`'s mid-tour corruption (T113/T114's
`__x=0x2601` signature) did NOT reproduce anywhere in this unit — not on
x86 across 7 ASAN runs, not on the Pi across 3 attempts. It is neither
caught nor ruled out. Along the way, x86 ASAN found and this unit fixed a
real, different, reproducible (2 of 3 pre-fix runs) bug: a dangling
`portalVisibleRegions_` pointer into a local `SimulationOutput`, the exact
same class T111 fixed for `regionPvsDepthMap_`, just a sibling field that
missed the same cleanup. Pi verification is honestly incomplete: every Pi
attempt was cut short by the ASAN build's memory footprint (two OOM kills,
one 300s timeout still mid-zone-load) before a full clean/dirty verdict on
the target defect could be reached.**

### x86 ASAN reproduced a bug — but not the target one

Built a parallel `build-asan/` tree (`-fsanitize=address -g -O1
-fno-omit-frame-pointer`, same Irrlicht/CMake reuse pattern as T114's
`build-tsan/`) and ran the same Befallen zone-in + `/move -400`/`/move -700`
tour T113/T114 used, via `review/T115/run_asan_x86.sh`.

**Run 1** hit an unrelated bug immediately: a heap-buffer-overflow READ in
third-party `TinySoundFont` (`tsf.h:1295`, `tsf_voice_render`), with a full
allocation stack (`tsf_load_samples` → `MidiPlayer::init` →
`AudioManager::initialize`). Real, but audio, not graphics — reported in
`review/T115/x86_asan_run1_report_audio_tsf_overflow.txt` and NOT fixed
(out of this unit's territory); worked around for every subsequent run via
`--no-audio`.

**Runs 2 and 4** (pre-fix) both reproduced, byte-for-byte identically, a
SEGV in `IrrlichtRenderer::drawZoneGeometrySorted()`:

```
==2416930==ERROR: AddressSanitizer: SEGV on unknown address 0x713db0d5fa38 (pc 0x5fd66272729a bp 0x7ffc047c60e0 sp 0x7ffc047c3900 T0)
==2416930==The signal is caused by a READ memory access.
    #0 0x5fd66272729a in EQT::Graphics::IrrlichtRenderer::drawZoneGeometrySorted() /mnt/projects/fable/wt/slot1/src/client/graphics/renderer_zone_draw.cpp:385
    #1 0x5fd662737489 in EQT::Graphics::IrrlichtRenderer::RenderPassTimer::OnRenderPassPreRender(irr::scene::E_SCENE_NODE_RENDER_PASS) /mnt/projects/fable/wt/slot1/src/client/graphics/renderer_zone_draw.cpp:260
    #2 0x5fd6638a8143 in irr::scene::CSceneManager::drawAll() (/mnt/projects/fable/wt/slot1/build-asan/bin/willeq+0x237e143)
    #3 0x5fd6627f470c in EQT::Graphics::IrrlichtRenderer::processFrameRender(float) /mnt/projects/fable/wt/slot1/src/client/graphics/renderer_frame.cpp:1169
    #4 0x5fd662821a8a in EQT::Graphics::IrrlichtRenderer::processFrame(float) /mnt/projects/fable/wt/slot1/src/client/graphics/renderer_frame.cpp:210
    #5 0x5fd6622cf539 in eqt::Application::render(float) /mnt/projects/fable/wt/slot1/src/client/application.cpp:1390
    #6 0x5fd6622dc63a in eqt::Application::mainLoop() /mnt/projects/fable/wt/slot1/src/client/application.cpp:875
    #7 0x5fd6622df778 in eqt::Application::run() /mnt/projects/fable/wt/slot1/src/client/application.cpp:654
    #8 0x5fd66191b885 in main /mnt/projects/fable/wt/slot1/src/client/main.cpp:130
AddressSanitizer can not provide additional info.
SUMMARY: AddressSanitizer: SEGV /mnt/projects/fable/wt/slot1/src/client/graphics/renderer_zone_draw.cpp:385 in EQT::Graphics::IrrlichtRenderer::drawZoneGeometrySorted()
```

**No allocation/free stack is printed** — not an omission, but the finding
itself: ASAN says "can not provide additional info" because the faulting
address (`0x713db0d5fa38`/`0x70f7...` across the two runs) isn't inside any
allocation ASAN's shadow memory knows about at all. That rules out a
heap-buffer-overflow or use-after-free-of-a-heap-object shape for this
specific crash and points instead at a wild/uninitialized pointer or a
reference into memory that was never heap-tracked (e.g. a torn-down thread
stack).

**Root cause, found by reading the disassembly then the source**:
objdump'ing the crashing PC (`renderer_zone_draw.cpp:385`, discriminator 1)
shows `rax = *(this + 0x578)` — i.e. `this->portalVisibleRegions_` — loaded,
null-checked (passes), then `cmpq $0x0, 0x18(%rax)` — libstdc++'s
`unordered_set::empty()` reading `_M_element_count` — faults. So
`portalVisibleRegions_` itself is confirmed to hold a **non-null, wild
pointer value**.

`portalVisibleRegions_` is set in exactly one place
(`renderer_sim_bridge.cpp:640`, `applySimulationOutput()`):
`portalVisibleRegions_ = &results->portalVisibleRegions;`. This function has
**two** callers: the runtime path (`renderer_sim_bridge.cpp:583`, `results`
= `simulationWorker_->swapAndGetResults()`, a pointer into
`SimulationWorker::output_[2]`, a renderer-owned member with the renderer's
own lifetime — safe), and `renderer_zone_load.cpp:2804`
(`loadZoneSequential()`'s Step12b, on the **loading thread**): `SimulationOutput
output; ... applySimulationOutput(output);` — `output` is a **local
variable of `loadZoneSequential()`**. `applySimulationOutput()` sets
`portalVisibleRegions_` to point at `output.portalVisibleRegions` — but
`output` is destroyed when Step12b's scope ends. Once
`loadZoneSequential()` returns and the loading thread's stack is
reused/torn down, `portalVisibleRegions_` dangles into now-unmapped or
reused memory. The very next frame's `drawZoneGeometrySorted()` (main/render
thread) reads it and, ~2 of 3 times, faults.

**This is not a new bug class — it is the exact hazard T111 already fixed**
for `regionPvsDepthMap_` and `particleRenderBuffer_`, both of which are
already nulled immediately after this same call
(`renderer_zone_load.cpp`, with a comment explaining why). `portalVisibleRegions_`
was added later (portal occlusion) and simply missed the same cleanup — the
comment right above it ("T111: same dangling-local hazard") describes
`portalVisibleRegions_`'s exact situation, just not by name.

### The fix — narrow, additive, same pattern as T111

`src/client/graphics/renderer_zone_load.cpp`: added
`portalVisibleRegions_ = nullptr;` immediately alongside
`particleRenderBuffer_ = nullptr;` / `regionPvsDepthMap_ = nullptr;` in
Step12b, right after `applySimulationOutput(output)` returns. Purely
additive; changes no other behavior. The runtime `SimulationWorker` path
sets `portalVisibleRegions_` correctly (into its persistent double-buffered
`output_[2]`) on every real frame via `applySimulationResults()` — this only
closes the first-frame window where the loading-thread's temporary value
would otherwise be read. (One deviation: the first version of this comment
was verbose enough to push `renderer_zone_load.cpp` from exactly 3000 lines
to 3015, a NEW `check_file_sizes.sh` FAIL against the fable baseline —
condensed to a one-line note matching T111's own style, landing back at
3000/WARN, not a new FAIL. Committed as a separate small follow-up commit,
not silently folded in.)

**Verified fixed**: 3 consecutive clean x86 ASAN runs post-fix (runs 5, 6,
7 — `review/T115/x86_asan_run5.log`/`run6.log`/`run7.log`), each a full
zone-in + tour + `/pmem` + `/camp` + clean shutdown, zero ASAN reports of
any kind. 0 recurrences in 3 tries where the pre-fix rate was 2 of 3.

### The target defect — not reproduced, not ruled out

Across all 7 x86 ASAN runs (3 pre-fix, plus the audio-bug run, plus 3
post-fix), **zero ASAN reports implicated `ZoneMergeBuffers::ranges_` or
anything in `renderer_zone_merge.cpp`/`.h`** — consistent with T114's TSAN
finding that this code path runs correctly and is reached (build() ran, 9
texture buckets, merged draws succeeded every run per the
`[mergedraws] ... reduction=91.7%` log lines) but never flagged. This
matches the plan's own framing: the corruption may be Pi-timing-dependent
and simply not manifest on x86 even under ASAN.

### Pi — three attempts, all cut short by ASAN's own memory cost, not by the target bug

Cross-compiled `build-arm-noble-asan/` (custom `docker run` against the
already-built `willeq-arm-noble` image, `-fsanitize=address -O1 -g
-fno-omit-frame-pointer`, linked `-fsanitize=address`). **292MB** — larger
than the Pi's 242MB `/tmp` tmpfs, so it is executed **directly off NFS**
(confirmed working: `--help` returns in 2.3s). ASAN's `log_path` also
targets NFS (`review/T115/`), never `/tmp`, for the same OOM-hazard reason
T113 established for core dumps. `ulimit -c` confirmed 0 on the Pi (no
coredump risk from `abort()` regardless of `ASAN_OPTIONS`).

| Run | Tour | Outcome |
|---|---|---|
| 1 | full 15-step incl. `/camp` | Both `/move` waypoints crossed the hazard box cleanly, **zero ASAN report**, then **OOM-killed** by the kernel (`dmesg`: `Out of memory: Killed process ... (willeq) anon-rss:267324kB`) during the post-camp reconnect/re-zone |
| 2 | 14-step, `/camp` dropped (to remove the double-zone-load OOM confound) | Both `/move` waypoints crossed the hazard box cleanly again, **zero ASAN report**, then **OOM-killed again** during the final idle wait after the script finished (`anon-rss:270848kB`) — proving the OOM isn't specific to the re-zone transient; ASAN's steady-state overhead alone exceeds the Pi's budget on a multi-minute run |
| 3 | same as run 2, with `ASAN_OPTIONS` bounding memory (`quarantine_size_mb=16 max_redzone=64`) | Zone load itself became abnormally slow under the tighter quarantine and was still at 65% ("Building zone meshes") when the 300s outer timeout fired — never reached the tour/move phase. **Inconclusive, discounted** |

**Consecutive clean Pi runs: 0.** This does not beat T114's 1-of-3 in the
strict "completed cleanly" sense — but the reason is different from T113's
and T114's: those 0/2 and 1/3 counts were runs that reached `/camp` and then
crashed on the target defect's own signature. Here, the two usable runs
(1 and 2) reached and traversed the hazard box **twice each** (four `/move`
legs total across the two runs) with **zero ASAN reports of any kind**, and
were killed by an unrelated resource constraint (documented via `dmesg` OOM
messages, not a signal in the target code) after the hazard-box portion of
the tour had already completed. **Runs-to-reports ratio for the target
defect: 0 reports in 2 usable runs** (run 3 discounted as inconclusive) —
a real but incomplete negative result, not a clean bill of health. Given
the session's Pi budget (3 of the protocol's 12-invocation cap used) and the
coordinator's explicit direction to stop blocking on further attempts, Pi
verification of the target defect is reported as **outstanding**, not
claimed as either fixed or ruled out.

### Answering the plan's questions explicitly

- **Did x86 ASAN reproduce it, or was the Pi needed?** Neither reproduced
  T113's `ranges_.find()` signature. x86 ASAN reproduced a *different*,
  real bug instead (`portalVisibleRegions_`), now fixed. The Pi was
  attempted per the plan's fallback but every attempt was cut short by
  ASAN's memory cost before conclusively exercising the target path to a
  clean/dirty verdict.
- **The corrupting write and its thread** — for `ranges_`: still unknown,
  never captured. For `portalVisibleRegions_` (the bug this unit did find
  and fix): not a corrupting *write* in the memory-safety sense — it's a
  pointer *write* on the **loading thread**
  (`renderer_sim_bridge.cpp:640`, inside `applySimulationOutput()` called
  from `loadZoneSequential()`) that stores the address of a soon-to-be-
  destroyed local, later read on the **main/render thread**
  (`drawZoneGeometrySorted()`).
- **Are `ranges_`'s pointer keys implicated, or does the corruption merely
  land there?** No new evidence either way. This unit's code reading (not
  ASAN) confirmed `regionMeshNodes_` is genuinely stable for a whole zone
  visit ("regions are always built eagerly at zone load... no streaming
  concept to reconcile with" — `renderer_zone_draw.cpp` comment,
  cross-checked against every write site to `regionMeshNodes_`: only
  `renderer_zone_load.cpp` populates it, during loading, before
  `ZoneMergeBuffers::build()` ever runs) — no code path was found that
  destroys/replaces a region's `IMeshBuffer*` after a successful merge
  build within a single zone visit. That weakens (does not disprove) the
  T113-named "dangling pointer key" hypothesis specifically, but ASAN never
  got the chance to confirm or refute it directly since the corruption
  never fired.
- **Does this also explain T113's unexplained Mesa/Lima SIGBUS at
  teardown?** No evidence of a connection. The `portalVisibleRegions_' fix
  is a CPU-side dangling-pointer bug entirely unrelated to GL/DRM resource
  teardown ordering; nothing in this unit's investigation touched the
  Mesa/Lima driver path. Still unexplained.

### Fix scope check against the plan's STOP conditions

The `portalVisibleRegions_` fix is purely additive synchronization-adjacent
cleanup (nulling a pointer at the exact spot its siblings already null
theirs) — it changes no buffer ownership, no zone merge/rebuild lifecycle,
no GL resource teardown order. It did not require stopping.

The target defect (`ranges_`) was **not fixed** — nothing to check against
"narrowest thing" for it, since it was never diagnosed further than T113/
T114 already left it. No clamping, skipping, try/catch, or disabling of
merged drawing was applied anywhere.

### Build/test

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | **2265/2266**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. `visual_gate` PASS within the suite (18.91s / 17.57s across two runs — one before, one after the comment-trim follow-up commit). |
| 3. `bash scripts/build-gles2-x86.sh` | PASS (run twice, before/after the follow-up commit) |
| 4. Visual gate | PASS both times, goldens untouched (no rendering-visible change — the fix is pointer bookkeeping only, not on any path the visual gate's zone benchmark exercises differently) |
| 5. `bash scripts/build-arm-noble.sh` | PASS (run twice) |
| 6. `scripts/refactor/check_file_sizes.sh fable` | 0 FAIL, 1 pre-existing-shape WARN (`renderer_zone_load.cpp`, exactly 3000 lines — see the comment-trim deviation above) |

### Deviations

1. **The verbose first-draft fix comment pushed `renderer_zone_load.cpp` to
   3015 lines, a new `check_file_sizes.sh` FAIL** (the file was exactly at
   the 3000 threshold, WARN-not-FAIL, before this unit). Caught by running
   the check, not assumed clean; condensed to a one-line note in a separate
   follow-up commit, back to 3000/WARN. Not silently absorbed.
2. **Pi ASAN verification of the target defect is incomplete**, per the
   table above — reported as outstanding rather than rounded up to either
   "fixed" or "ruled out." All three attempts are preserved in
   `review/T115/` with `dmesg` evidence for the OOM calls.
3. **`--no-audio` added to every x86 ASAN run after run 1** to work around
   the unrelated `TinySoundFont` heap-buffer-overflow — flagged, not fixed,
   reported in `review/T115/x86_asan_run1_report_audio_tsf_overflow.txt`.
4. Per the coordinator's explicit mid-session direction, further Pi retries
   were stopped after run 3 (background-notification polling had stalled
   twice) in favor of finishing verification and teardown with an honest
   partial result rather than continuing to block.

## Board

**`ZoneMergeBuffers::ranges_` (T113's original named target, carried through
T114) remains OPEN.** This unit neither closed it nor produced new direct
evidence about its mechanism — ASAN was applied as the plan specified, but
the specific conditions that trigger it on the Pi were not sustained long
enough (OOM cut every Pi attempt short) to give ASAN a real chance at the
crash window. What IS new: the "stale pointer key" hypothesis is somewhat
weakened by this unit's `regionMeshNodes_` lifecycle audit (no code path
found that replaces a region's mesh buffer mid-visit), and a real sibling of
T111's dangling-local-`SimulationOutput` class was found and fixed
(`portalVisibleRegions_`) — though it demonstrably does NOT explain the
`ranges_` crash (different symptom: hard SEGV on a wild non-heap pointer vs.
a corrupted-but-present heap `std::map` tree; different code path;
`portalVisibleRegions_`'s fix held clean for 3/3 x86 runs while `ranges_`
was never reached in a state to test either way).

**Recommended follow-up**: the Pi's ASAN memory ceiling needs to be solved
before ASAN can give this specific defect a fair trial — either a
smaller/stripped ASAN build (trade off some symbolization), a shorter
targeted tour that reaches the hazard box faster (skip the 45s dead-air
warmup and the `/pmem` calls), or accept that ASAN cannot run long enough
on this 512MB device and fall back to a different technique (a custom
`ranges_`-only bounds/liveness check compiled into a debug build, or a
longer plain-gdb Pi session budgeted specifically for this, per T113's
original alternative). Either way, the next unit should NOT repeat this
unit's exact ASAN Pi invocation unmodified — it has now failed to complete
a full tour 3 times in a row for a resource reason, not a code reason.

T114's inventory (16 items) is otherwise unchanged by this unit — see
`refactor/deferred.md` for the current state of items #10-#12 (still
open, different subsystem boundary) and #7 (still open, deadlock hazard
documented). This unit's `portalVisibleRegions_` finding is a NEW, 17th item
in that same general "dangling pointer into a local/temporary object" family
(more precisely T111's own sub-class, not T114's worker/render race class)
— now fixed, added to `deferred.md`'s history rather than its open list.
