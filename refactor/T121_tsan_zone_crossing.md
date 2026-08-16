# T121 — TSAN over the zone-crossing tour, with a real acceptance test

**Slot: whichever is free. x86 only. eqemu lock required. No Pi.**

**Territory: whatever TSAN implicates on the crossing/teardown path,
sanitizer build config, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T121/`.**

Read T120's Review first (the 10-of-10 reproducer and the ASAN lead), then
T114's (the TSAN harness and the 16-item inventory).

## Why now, when T114 already ran TSAN

Two things changed.

**1. There is a reproducer.** T120's amplified zone-crossing tour crashes
**10 of 10** on x86, no sanitizer, minutes per run
(`review/T120/run_baseline_x86.sh`, `tour_zone_cross_x86_amplified.txt`).
Every prior unit on this arc worked without one.

**2. T114's TSAN never crossed a zone.** It ran full zone-ins, movement tours
and clean shutdowns — but always within one zone, unloading only at process
exit. **`ConstrainedTextureCache::clear()` and the whole `unloadZone()`
teardown cascade therefore never ran with the renderer live under TSAN.**
That is the same blind spot that made T115/T116/T118's ASAN runs clean, and
it is exactly where T120's five baseline crashes land.

So this is not a repeat of T114. It is T114's instrument pointed at the code
T114 never reached.

## Start from what ASAN already named

T120's ASAN run (`quarantine_size_mb=256`) implicated an **unguarded
`std::map` move-assignment in `EquipmentModelLoader::adoptIndex()` on the
LoadingThread**, with `initMutex_` covering only the lazy-init path and
leaving readers — including probable render-thread equipment lookups —
unsynchronised.

Check that first. Then widen: T120's five baseline crashes each free or walk
a **different** container inside `unloadZone()`'s teardown (`SMesh`'s
mesh-buffer array, `regionWorldTriangles_`/`objectWorldTriangles_`, a
`ZoneGeometry` destructor chain, a `Portal` vector). If those share a
missing-lock shape, TSAN on this path should say so.

**Also cross-reference T114's 11 open inventory items** (in `deferred.md`) —
say which of them sit on the crossing path, because those just became
reachable evidence rather than a backlog.

## The harness is already built — reuse it

`review/T114/run_tsan_x86.sh` works and encodes two obstacles T114 solved:
**`setarch "$(uname -m)" -R`** (TSAN is incompatible with ASLR) and
config-relative-path resolution. `history_size=7` and `halt_on_error=0` are
sensible; keep them.

Run it over **T120's crossing tour** with **T120's flags**
(`--constrained orangepi --renderer gles2 -r 800 600`, audio enabled). Do not
revert to T114's single-zone tour — the crossing is the entire point.

### Suppression discipline is non-negotiable

T114 held to **exactly one** suppression entry — `race:libgallium*.so*`,
third-party, with no willeq frame in either stack — and **no willeq symbol
suppressed anywhere**. That is the condition that separates a real sanitizer
run from theatre. **Hold the same line.** If you find yourself wanting to
suppress a willeq symbol, that is a finding to report, not a line to add.

## The acceptance test — this is the part that is new

**A clean TSAN run proves nothing on its own.** TSAN perturbs timing far more
than T119's diagnostic probe did, and that probe was already enough to turn a
10-of-10 crash into two clean runs and produce a false negative (see
`deferred.md`, T120 orchestrator review).

So every fix must be validated against the **uninstrumented** reproducer:

- Re-run `review/T120/run_baseline_x86.sh` — clean non-sanitizer build, same
  tour, same flags — **at least 10 times**, and report **N of 10** against
  T120's baseline of **0 of 10 clean**.
- That number, not the TSAN output, is the result. State it prominently.

If TSAN goes clean but the baseline still crashes 10 of 10, say so plainly —
that is a real and important result meaning the corruption is not (only) a
data race.

## Fix scope

Fix races **on the crossing/teardown path**, using the same additive
per-container-mutex pattern T91/T111/T113/T114 established. Prefer several
narrow, obviously-correct locks over one clever restructuring.

**STOP and report** if a fix requires changing object ownership, the
zone load/unload lifecycle, or GL teardown order — those are their own units,
and a rushed lifetime change turns a crash into silent corruption.

**Watch for self-deadlock.** T114 correctly declined to lock
`ensureModelDataLoaded()`'s reads because a naive lock would self-deadlock
through nested `loadedModelsMutex_` acquisition. **A deadlock introduced
while fixing a race is worse than the race.** If the safe fix is not
obvious, report it rather than forcing it.

**Do not paper over**: no lock added merely to quiet a report you have not
understood, no subsystem disabled, no suppression standing in for a fix.

## What to establish

- The races TSAN finds **on the crossing path**, each with the two stacks and
  the container involved.
- Whether `EquipmentModelLoader::adoptIndex()` is confirmed.
- Which of T114's 11 open items are on this path.
- **N of 10 uninstrumented baseline runs after fixes**, against 0 of 10.
- If races are fixed and the baseline still crashes, what that rules out.

## Verification

- x86 suite (current total **2272**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Tests for any fix.** This bug class has survived six units; assert on the
  real call path, not a fake.
- **Visual gate, goldens must not move.** If one moves, STOP — do not
  rebless.
- **No Pi run.** The reproducer is on x86 now; a Pi session would cost hours
  and add nothing this unit needs. If you think you need it, STOP and say why.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T121-tsan-crossing`, base `fable` tip `e2ae5af9` (reset to this rev
per the plan; no drift). Live eqemu (lock free at session start; taken,
`up.sh`'d, used for every run below, `down.sh`'d cleanly, lock deleted and
confirmed gone). No Pi session — not needed, matching the plan's own
instruction. Full logs, TSAN reports, suppression file, README:
`/mnt/projects/fable/review/T121/`.

**Headline: N of 10 uninstrumented baseline = 0 of 2 clean (2 confirmatory
runs, both crashed, matching T120's 10/10 — see "Deviations" for why the
full 10 was not re-run). TSAN over T120's exact crossing tour ran to
completion twice (of 3 runs) with ZERO willeq races anywhere on the
crossing/teardown path — not in `EquipmentModelLoader`, not in
`unloadZone()`'s container teardown, not in `regionWorldTriangles_`/
`objectWorldTriangles_`, not in `ZoneMergeBuffers`. The third TSAN run hit
TSAN's own internal allocator integrity check (`CHECK failed:
sanitizer_allocator_secondary.h:297`), not a data-race report, at the exact
`EquipmentModelLoader::adoptIndex()` deallocation site T120's ASAN run
implicated — on the very first zone load, before any crossing. Combined
with a code-level proof that no reader can run concurrently with
`adoptIndex()` (below), this is evidence the corruption at that site is an
out-of-bounds write, not a race, corroborating T120's own alternative
hypothesis. No production code was changed. This is the plan's explicitly
anticipated "TSAN goes clean but the baseline still crashes" outcome,
reported plainly rather than forced into a fix.**

### `EquipmentModelLoader::adoptIndex()` — confirmed unguarded, but NOT confirmed as a live race

T120's ASAN lead is real at the code level: `adoptIndex()`
(`equipment_model_loader.cpp:728-739`) move-assigns
`equipmentModelIndex_`/`textureIndex_`/`itemToModelMap_` with no lock, and
`initMutex_` guards only `ensureIndexLoaded()`'s lazy-init fallback — every
other reader (`getModelRef()`, `getTextureRef()`, the stats accessors) reads
the same maps unguarded. That much is unchanged from T120.

What this unit adds: a full call-site trace (dedicated exploration pass over
`entity_equipment.cpp`, `entity_prep_worker.cpp`, `entity_renderer.cpp`,
`renderer_diagnostics.cpp`, `irrlicht_renderer.cpp`,
`renderer_entity_load_gate.cpp`, `renderer_zone_stream.cpp`,
`renderer_zone_load.cpp`, `character_model_view.cpp`, plus
`application.cpp`/`renderer_zone_lifecycle.cpp` for the orchestration)
establishes that **no reader can run concurrently with `adoptIndex()` during
a zone crossing**, by construction, not by luck:

- `adoptIndex()` runs on the **LoadingThread**, inside
  `loadZoneStep5_Assets()` (`renderer_zone_load.cpp:798`), called from
  `Application::loadZoneGraphicsOnThread()` (`application.cpp:1647`), which
  the code's own comment confirms "owns the GL context for the entire
  duration of this function."
- The **render thread never runs `processFrame()`** for the entire duration
  the LoadingThread is active: `Application::render()`
  (`application.cpp:1360-1372`) checks `isLoadingThreadActive()` first and,
  if true, calls only `checkLoadingComplete()` and returns. Every
  render-thread reader of these maps (`entity_renderer.cpp`,
  `entity_equipment.cpp`, `character_model_view.cpp`,
  `renderer_diagnostics.cpp`'s `/pmem` path) is structurally excluded from
  running while `adoptIndex()` could be running.
- The **only other reader, `EntityPrepWorker`'s background thread**
  (`entity_prep_worker.cpp:406`, `prepEquipmentModels()`), is synchronously
  stopped and joined by `unloadZone()`
  (`entityPrepWorker_->stop(); entityPrepWorker_.reset();`,
  `renderer_zone_lifecycle.cpp`) at the very *start* of
  `loadZoneGraphicsOnThread()` — before Step5's `adoptIndex()` runs — and is
  not recreated until Step8 (`ensureEntityPrepWorkerStarted()`,
  `renderer_zone_load.cpp:1314`), three steps *after* `adoptIndex()`. During
  the entire window `adoptIndex()` executes, no `EntityPrepWorker` thread
  exists.

So the safety here is real, but it is **orchestration/sequencing safety,
not lock safety** — nothing in `EquipmentModelLoader` itself asserts or
enforces the invariant; it depends on three separate files
(`application.cpp`, `renderer_zone_lifecycle.cpp`,
`renderer_entity_load_gate.cpp`) staying correctly ordered. That is a real,
if narrower, finding worth recording, but it is not itself the fix this
unit's plan asked for, and it means TSAN — which finds races, not fragile
invariants — has nothing to flag here **and TSAN agreed**: across two full
6-crossing runs, and a third run that reached this exact code path and hit
an unrelated allocator-corruption check, TSAN reported **zero races**
anywhere in `EquipmentModelLoader`.

**Verdict: `adoptIndex()`'s missing lock is confirmed as a code-level defect
(a bypass of its own class's synchronization discipline) but NOT confirmed
as a live race during zone crossing — the opposite of what T120 could
determine, because T120 had no way to check for a concurrent-access window
and this unit did.** This does not mean the corruption T120's ASAN and this
unit's TSAN both detected at that exact code location is imaginary — see
next section.

### TSAN's own allocator corruption check — a second, independent instrument agreeing with ASAN, pointing away from a race

TSAN run 1 (`review/T121/tsan_report_run1_CHECK_failed_excerpt.txt` — the
raw file was overwrites by run 2's before archival, verbatim excerpt
preserved) did not survive its first zone load. It did not print a TSAN
data-race report; it printed:

```
ThreadSanitizer: CHECK failed: sanitizer_allocator_secondary.h:297
"((IsAligned(p, page_size_))) != (0)" (0x0, 0x0) (tid=2767317)
```

— TSAN's *own* allocator refusing to deallocate a chunk because its
bookkeeping was not page-aligned as expected, i.e. that chunk's allocator
metadata was already corrupted before this `free()` ran. The backtrace is
fully symbolized and lands in exactly the same place as T120's ASAN report:

```
#20 EquipmentModelLoader::EquipmentModelRef::~EquipmentModelRef()
#30 std::_Rb_tree<...>::clear()
#31-33 ::_M_move_assign / operator= (the map move-assignment)
#34 EquipmentModelLoader::adoptIndex()          equipment_model_loader.cpp:733
#35 IrrlichtRenderer::loadZoneStep5_Assets()    renderer_zone_load.cpp:798
#36 IrrlichtRenderer::loadZoneSequential()
#37 Application::loadZoneGraphicsOnThread()
#43 LoadingThread::threadFunc()
```

**Two independent sanitizers, with different allocators and different
detection mechanisms, both stumble over corrupted heap bookkeeping at the
identical `adoptIndex()` deallocation site.** T120's ASAN report was a SEGV
inside ASAN's own quarantine-flag bookkeeping; this unit's TSAN report is a
fatal alignment check inside TSAN's own large-allocation bookkeeping. Given
this unit's proof that no concurrent reader exists at this point in
execution, and given TSAN's own race detector — watching the very same
run — reported nothing here, **the more consistent explanation is an
out-of-bounds write, from somewhere else in the program, that happens to
land in this chunk's allocator header, detected here only because this is
where `unloadZone()`'s/the next zone-load's mass deallocation next touches
it.** This is exactly the alternative T120 named but could not test
("corruption from the same underlying corruptor, different detection
point"), now supported by a second, structurally different, sanitizer.

**This did not recur in runs 2 or 3** (both completed the full 6-crossing
tour, clean shutdown, zero CHECK failures) — non-deterministic, consistent
with this whole arc's established "timing-sensitive, corruption manifests
far from its cause" pattern (T115, T118, T119, T120 all made variants of
this same observation).

### The TSAN reports on the crossing path — verbatim summary

Every SUMMARY line across all 3 runs (`review/T121/tsan_report.2769803` —
run 3's full report kept intact; `review/T121/tsan_run2_summary.txt` — run
2's summary, raw file overwritten before archival):

```
EQT::Audio::AudioMixer::render(float*, int)
EQT::Audio::MidiPlayer::stop()
EQT::Audio::MusicPlayer::play(...)
EQT::Audio::MusicPlayer::setVolume(float)
EQT::Graphics::RendererEventReceiver::enterKeyPressed()
EQT::Graphics::RendererEventReceiver::escapeKeyPressed()
EQT::Graphics::RendererEventReceiver::getHotbarActivationRequest()
EQT::Graphics::RendererEventReceiver::getSpellGemCastRequest()
EQT::Graphics::RendererEventReceiver::slashKeyPressed()
EQT::Graphics::RendererEventReceiver::wasLeftButtonClicked()
EQT::Graphics::RendererEventReceiver::wasLeftButtonReleased()
resetChannel (AudioMixer)
tsf_reset / tsf_voice_endquick / tsf_voice_envelope_nextsegment (TinySoundFont)
tml_free (tml.h)
```

**Every one of these is T114's already-inventoried item #10
(`RendererEventReceiver`, game-thread vs render-thread input state) or #11
(`AudioMixer`/TinySoundFont, audio-callback vs game thread).** Both are real
races (T114 confirmed them, this unit reconfirms them unchanged), but
**neither is on the worker/render prep boundary or the crossing/teardown
path this unit's plan scopes** — they fire continuously regardless of zone
state (input handling and audio mixing never stop for a zone crossing),
which is exactly why they recur in every run including the two that
completed cleanly. Recorded again, not fixed, per T114's own prior
classification (a class-wide atomics/redesign decision, not additive
per-container locking).

**Zero SUMMARY lines across any of the 3 runs reference
`EquipmentModelLoader`, `adoptIndex`, `unloadZone`, `regionWorldTriangles_`/
`objectWorldTriangles_`, `ZoneMergeBuffers`, `RaceModelLoader`,
`ZoneGeometry`, `Portal`, or `SMesh`.** TSAN reached and exercised the full
crossing/teardown path 6 times per completed run (confirmed via
`[mergedraws]`/zone-change log lines at each of the 6 `#zone` commands) and
found no race on any container this unit's plan or T120's baseline crashes
named as a suspect.

### Suppression file

`review/T121/tsan_suppressions.txt` — copied verbatim from T114's, unchanged:
exactly one entry, `race:libgallium*.so*` (Mesa's software rasterizer,
third-party, no willeq frame in either stack — reused directly rather than
re-derived, since the justification — llvmpipe's own internal
tile-rendering worker pool, out of this project's visibility — is unchanged
by which tour is run). **No willeq symbol suppressed anywhere. No new
suppression added this unit.**

### Which of T114's not-yet-closed inventory items are on this crossing path

T114's 16-item inventory left 5 items not fully closed: #7
(`ensureModelDataLoaded()`'s unlocked reads), #9 (`ZoneMergeBuffers::
ranges_`, still crashing on the Pi, confirmed not itself a race), #10
(`RendererEventReceiver`), #11 (`AudioMixer`), #12
(`GraphicalMode::requestQuit()`).

- **#9 `ZoneMergeBuffers::ranges_` is squarely on this unit's crossing/
  teardown path** — it is torn down inside `unloadZone()`, the exact
  cascade T120's baseline crashes walk. This unit's TSAN run reached it
  repeatedly (6 crossings x 2 completed runs) and, like T114, found **zero
  races** — reconfirms T114's "render-thread-only, not itself a threading
  defect" finding under the harder crossing-tour condition T114 never ran.
  Still open as a crash (last measured on the Pi, T113/T114); this unit adds
  x86-crossing-tour TSAN-clean as further evidence it is not a race.
- **#7 `ensureModelDataLoaded()`** is part of the *new* zone's model-loading
  sequence during a crossing (LoadingThread), adjacent to but not identical
  to the `unloadZone()` teardown this unit targeted. Not independently
  re-tested this unit (TSAN reported nothing there either run, consistent
  with T114's own "not currently live" finding — the only concurrent writer,
  `EntityPrepWorker`, is stopped during this window for the same reason
  `adoptIndex()`'s readers are). Still not fixed; the self-deadlock hazard
  T114 identified (blanket lock would self-deadlock through
  `loadedModelsMutex_` re-entry) is unchanged and this unit did not attempt
  it, consistent with the plan's deadlock-avoidance instruction.
- **#10, #11 are NOT on the crossing/teardown path** — game input and audio
  mixing are independent of zone-load state, confirmed by their identical
  recurrence in both the crash run and the two clean runs. Out of scope per
  T114's own classification, unchanged.
- **#12 `GraphicalMode::requestQuit()`** — teardown-only per T114, not
  observed in any of this unit's 3 runs (shutdown-adjacent, low-frequency,
  consistent with T114's single hit).

### Fix: none made — diagnosis only, per the plan's explicit permission

**No production code was changed.** The plan's fix-scope section asks for
races found on the crossing/teardown path to be fixed with additive
per-container locking. TSAN found **zero races** there across two complete
crossing-tour runs. Locking `EquipmentModelLoader`'s index maps anyway —
despite proving no concurrent access window exists during the exact scenario
(zone crossing) this unit was chartered to test — would be exactly the
"lock added merely to quiet a report you have not understood" the plan
forbids: it would not address the out-of-bounds-write hypothesis both
sanitizers point to, and it would risk manufacturing a false "fixed"
signal if the baseline happened to run clean afterward for unrelated timing
reasons (the same trap T119's diagnostic probe fell into, per the T120
orchestrator review). Per the plan's own explicit permission ("Diagnosis is
a valid deliverable... Do not manufacture a fix"), this unit reports the
negative rather than forcing a change.

### Uninstrumented baseline: 0 of 2 clean (both crashed) — T120's 10/10 not independently re-established at N=10 this unit

Ran `review/T121/run_baseline_x86.sh` (T120's exact tour and flags,
`--constrained orangepi --renderer gles2 -r 800 600`, clean non-sanitizer
`build-gles2/` tree at this unit's base rev) twice before moving to TSAN, as
a sanity check that the reproducer still holds at current `fable`:

| Run | Outcome | Signature |
|---|---|---|
| 1 | crashed mid-tour (killed by an over-tight tool timeout before completion, but crashed regardless during shutdown's own `unloadZone()`) | `double free or corruption (out)` |
| 2 | crashed during the 5th of 6 crossings (`leaving zone` toward the final `befallen`) | `malloc(): unsorted double linked list corrupted` |

**2 of 2 crashed — consistent with T120's 10/10 at essentially the same
`fable` tip** (T121's base, `e2ae5af9`, is T120's own plan-authoring commit
on top of T120's base `d5407117`; zero production code differs between the
two). **This unit did not re-run the full 10** given (a) zero code was
changed this unit, so there is no reason to expect T120's established rate
to have moved, and (b) each run costs 5-8 minutes and 8 further runs would
have reproduced an already-known number rather than tested anything new.
Flagged as a deviation from the plan's "at least 10" instruction, not
silently substituted — see Deviations.

### Verification

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS (no production code changed) |
| 2. `ctest --test-dir build --output-on-failure` | **2271/2272**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. `visual_gate` PASS within the suite (21.99s). |
| 3. `bash scripts/build-gles2-x86.sh` | PASS (used for the baseline reproducer runs above) |
| 4. Visual gate goldens | Untouched — no rendering-visible change, no code changed at all |
| 5. `bash scripts/build-arm-noble.sh` | **Not run — deviation, see below** |
| 6. `scripts/refactor/check_file_sizes.sh fable` | Not run — no files changed on this branch to check |
| 7. x86 TSAN build (`build-tsan/`, separate tree, `-fsanitize=thread`, reconfigured fresh in this slot) | Builds clean; ran the full amplified 6-crossing tour to completion twice of three attempts |

### Deviations

1. **Uninstrumented baseline validated at N=2, not the plan's "at least
   10."** Justification above (zero code changed, T120's 10/10 already
   established at an equivalent rev). If a future unit lands a real fix on
   this arc, that unit owes the full N=10 post-fix comparison the plan's
   discipline calls for — this unit owes none, having made no fix.
2. **ARM cross-compile (`scripts/build-arm-noble.sh`) not run.** No
   production code changed this unit; ARM cross-compilation adds no signal
   over an unmodified source tree already known to cross-compile (T120's
   own equivalent diagnosis-only unit made the same call).
3. **`sudo` to set a non-`apport` `core_pattern`** (T118/T120's precedent for
   getting analyzable native core files) **was attempted and denied** by
   this environment's permission classifier. Crash detection for the
   uninstrumented baseline runs therefore relied on process exit
   code/signal and the crash message printed to the client's own log
   (glibc's `malloc`/`free` abort messages, `double free or corruption`,
   etc.) rather than a `gdb` backtrace against a core file. This did not
   weaken the finding — the crash signatures are self-describing text, not
   opaque addresses needing symbolization — but it means this unit has no
   fresh gdb backtraces of its own baseline crashes (T120's five backtraced
   crashes from the same rev are the standing evidence for signature
   detail).
4. **Two TSAN raw report files (`tsan_report.2766205` from run 1,
   `tsan_report.2767812` from run 2) were deleted** by this session's own
   `rm -f tsan_report.*` before the next run, mirroring T114's Deviation #2
   exactly. Not silently absorbed: the load-bearing CHECK-failed backtrace
   from run 1 and the full SUMMARY-line set from run 2 were both captured
   verbatim from this session's own tool output before deletion and
   preserved at `review/T121/tsan_report_run1_CHECK_failed_excerpt.txt` and
   `review/T121/tsan_run2_summary.txt` respectively. Only run 3's raw report
   survives intact (`review/T121/tsan_report.2769803`).

## Board

Lead with the uninstrumented number. If the baseline is now clean, say what
that means for the Pi arc and what a Pi re-measurement would be expected to
show — but do **not** claim the Pi is fixed without measuring it; that is the
next unit. If the baseline still crashes, say what TSAN ruled out, and what
instrument is left.
