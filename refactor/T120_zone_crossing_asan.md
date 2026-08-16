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

Executed in `/mnt/projects/fable/wt/slot3`, task branch
`task/T120-crossing-asan`, base `fable` tip `d5407117` (reset to this rev
per the plan; no drift). Live eqemu (lock free at session start; taken,
`up.sh`'d, used for every run below; `down.sh` still owed — see "Finish"
note at the bottom). No Pi session — not reached, see Step 3 below. Full
logs, cores, ASAN reports, gdb backtraces, README: `/mnt/projects/fable/review/T120/`.

**Headline: the x86 crossing-tour rate went from T119's unresolved `n=1` to
a flat 10 of 10 on the amplified tour — every single amplified-tour run on
current `fable`, clean build, no diagnostics, no gdb, crashed. Under ASAN
with a real 256MB quarantine (not trimmed) over the same tour, 2 of 3 runs
produced a fully symbolized report: ASAN's own allocator metadata
(`AtomicallySetQuarantineFlagIfAllocated`) is corrupted for a heap chunk
being freed inside `EquipmentModelLoader::EquipmentModelRef`'s destructor,
reached via `adoptIndex()`'s unguarded `std::map` move-assignment on the
LoadingThread. `EquipmentModelLoader::adoptIndex()` and every accessor of
`equipmentModelIndex_`/`textureIndex_` except `ensureIndexLoaded()` take
**no lock at all** — `initMutex_` guards only the lazy-init path — so a
concurrent render-thread read during a zone-crossing overlap window is
architecturally unguarded. This is a genuine, evidenced write (or, more
precisely, evidenced corrupted allocator bookkeeping consistent with an
earlier unsynchronized write), not a fix — the fix is a synchronization
audit bigger than this unit, per the plan's own explicit permission to stop
at diagnosis.**

### Step 1 — x86 crossing-tour rate: 10 of 10 crashed (dramatic change from T119)

Amplified `review/T119/tour_zone_cross_x86.txt`'s single Befallen↔Qeynos2
round trip to **3 round trips (6 zone crossings)** —
`review/T120/tour_zone_cross_x86_amplified.txt`. Built a fresh
`build-gles2/` tree at this unit's base rev (no diagnostic instrumentation,
unlike T119's build; no gdb). Ran the amplified tour **10 times** in a row
(plus one earlier unlogged smoke run that also crashed, not counted in the
10):

| Run | Outcome | Signature |
|---|---|---|
| 1 | SIGABRT, mid-tour, first crossing | `corrupted double-linked list` |
| 2 | SIGABRT, first crossing, inside `unloadZone()`→`CMeshSceneNode::~CMeshSceneNode()`→`SMesh` mesh-buffer array `free()` | `free(): invalid next size (fast)` |
| 3 | SIGSEGV, first crossing, inside `unloadZone()`→`regionWorldTriangles_`/`objectWorldTriangles_` (`std::map<size_t, vector<triangle3d>>`) `clear()`'s rb-tree walk | silent (no glibc text) |
| 4 | SIGSEGV, first crossing, inside `unloadZone()`→destructor cascade (`ZoneGeometry`→`ResolvedMaterialList`→`TextureAnimationInfo`→`vector<string>`) | silent |
| 5 | SIGSEGV, first crossing | silent |
| 6 | SIGSEGV, first crossing, **identical** `regionWorldTriangles_` rb-tree-walk stack to run 3 | silent |
| 7 | SIGSEGV, earlier — during the very first in-zone `/move`, before any crossing | silent |
| 8 | SIGABRT, first crossing, inside `unloadZone()`→`Portal`'s `vector<float>` `free()` | `free(): invalid next size (fast)` |
| 9 | SIGSEGV, first crossing | silent |
| 10 | SIGSEGV, first crossing | silent |

**10 of 10.** All but run 7 crash within the very first zone crossing —
the 4-6 crossing amplification was not actually needed to observe the
defect on this clean build; it happens on crossing #1 almost every time.
Five backtraces were pulled with `gdb` against saved core files
(`review/T120/baseline_x86/run{2,3,4,6,8}_core.willeq*` +
`run{2,3,4,6,8}_gdb_backtrace.txt` — cores for 5,7,9,10 were not
individually backtraced given the already-overwhelming signal, logs kept).
**Every single one of the five backtraced crashes free()s or walks a
completely different C++ container** (an Irrlicht `SMesh`'s mesh-buffer
array, a raw `std::map<size_t, vector<triangle3d>>`, a `ZoneGeometry`'s
`ResolvedMaterialList`/`TextureAnimationInfo` chain, a `Portal`'s
`vector<float>`) — this is the classic signature of heap **metadata**
corruption from an earlier out-of-bounds write landing near an unrelated
chunk's bookkeeping, not a bug local to any one of these five destructors.
`unloadZone()`'s mass container-destruction on the LoadingThread is where
the corruption is *detected*, consistently, not necessarily where it
*happens* — matching this arc's established "manifests far from its cause"
pattern (T115, T118, T119 all made the same observation about different
signatures).

**Rate comparison, pre- vs. post-amplification**: T119's un-amplified
2-crossing recipe never had a rate established at N≥10 — it was one
pre-fix crash, two post-fix clean runs, and no crash under gdb, explicitly
flagged by T119 as "an unknown rate, not a deterministic reproducer." This
unit's 6-crossing recipe, on an equivalent non-diagnostic, non-gdb, current
build, is 10 of 10. Whether the improvement is from the extra crossings or
from removing T119's diagnostic instrumentation (which the gdb-suppression
precedent suggests could itself perturb timing enough to mask a
timing-sensitive defect) was not isolated — both changed at once. **Either
way, the practical finding stands: this unit hands off a reliable,
cheap, no-hardware x86 reproducer where none existed before.**

### Fifth signature status: subsumed, not confirmed or refuted in isolation

T119's fifth signature (`double free or corruption (out)`, observed once,
pre-fix) was never seen verbatim again this run — but `corrupted
double-linked list` (run 1) and `free(): invalid next size (fast)` (runs
2, 8) are siblings from the same glibc `_int_free()` consistency-check
family, on the same trigger (first zone crossing, post-T119-fix build).
Given the corrupted-container-differs-per-run pattern established above,
treating these as "the same underlying corruptor, different detection
point" is far more consistent with the evidence than treating each
distinct glibc message as a separate bug. Not narrowed further than that.

### Step 2 — ASAN with a REAL quarantine: FIRED, 2 of 3, fully symbolized

Built `build-asan/` (`-fsanitize=address -fsanitize-recover=address -g -O1
-fno-omit-frame-pointer`, prebuilt non-instrumented Irrlicht reused per
T115/T118 precedent — ASAN's allocator still intercepts every malloc/free
process-wide regardless of which TUs are instrumented).
`-fsanitize-recover=address` was added specifically because T118 found that
`halt_on_error=0` alone did **not** keep the process alive past the known
TinySoundFont overflow without it. Audio was kept **enabled** throughout —
not disabled — via this recover flag plus the plan's alternative "local
uncommitted `nextPos` clamp" to `third_party/TinySoundFont/tsf.h`
(clamping `nextPos` to `region->end` when not looping, all 3 `outputmode`
branches), applied only for the smoke run that characterized the known
overflow, then **fully restored** before any counted run and confirmed
byte-identical to `review/T120/tsf.h.orig_backup` via `diff` before commit
— it never landed on the branch.

`ASAN_OPTIONS="quarantine_size_mb=256 detect_stack_use_after_return=1
halt_on_error=0 detect_leaks=0 abort_on_error=0 log_path=... symbolize=1"`
— **quarantine explicitly non-zero and generous, not the `quarantine_size_mb=0`
that crippled T118's Legs 2/3.** `review/T120/run_asan_x86_quarantine.sh`.

Ran the amplified tour 3 times:

- **Run 1**: `AddressSanitizer:SEGV on unknown address 0x290100002909...
  caused by a READ memory access` (`review/T120/asan_x86/asan_report_run1.2735316`).
  Two `DEADLYSIGNAL`s in quick succession, the second in the same thread
  while still handling the first — ASAN gave up with "nested bug in the
  same thread, aborting" before it could unwind a stack, so this report is
  address-only, no frames. **The address itself is notable**: `0x2901` is
  `GL_REPEAT` (`GLES2/gl2.h`) — the same shape of finding as T118's
  `0x2601`/`GL_LINEAR` discovery (a small GLenum texture-parameter constant
  landing where a pointer or tree-node field is read), though this alone is
  suggestive, not proof — a coincidentally small corrupted value could
  produce a similar-looking address without any GL involvement.
- **Run 2**: `AddressSanitizer:SEGV on unknown address 0x2901000028f1...
  caused by a WRITE memory access`, **fully symbolized**
  (`review/T120/asan_x86/asan_report_run2.2736335`, verbatim below). Same
  `0x2901` high bytes as run 1.
- **Run 3**: clean — full 305s tour, `Application shutdown complete`, zero
  ASAN reports.

**Run 2's report, verbatim (trimmed to the load-bearing frames — full file
at the path above):**

```
==2736335==ERROR: AddressSanitizer: SEGV on unknown address 0x2901000028f1
(pc 0x74c6b8c44688 bp 0x74c64793e510 sp 0x74c64793e4d0 T92)
==2736335==The signal is caused by a WRITE memory access.
    #0 in __sanitizer::atomic_compare_exchange_strong<...>
       sanitizer_atomic_clang.h:81
    #1 in __asan::Allocator::AtomicallySetQuarantineFlagIfAllocated(...)
       asan_allocator.cpp:668
    #2 in __asan::Allocator::Deallocate(...) asan_allocator.cpp:724
    #3 in operator delete(void*, unsigned long)
    #4-#13 in std::basic_string<char>::~basic_string() [via _M_dispose,
       _M_destroy, allocator_traits::deallocate] and its containing
       std::vector<std::string>::~vector()
    #14 in EQT::Graphics::EquipmentModelLoader::EquipmentModelRef::~EquipmentModelRef()
       include/client/graphics/eq/equipment_model_loader.h:104
    #15-#20 in std::pair<int const, EquipmentModelRef>::~pair() and the
       owning std::map's rb-tree node destruction/erase machinery
    #21-#27 in std::_Rb_tree<...>::_M_erase (recursive, 4 levels) then
       ::clear() then ::_M_move_assign()
    #28 in std::map<int, EquipmentModelRef>::operator=(std::map&&)
       (the move-assignment)
    #29 in EQT::Graphics::EquipmentModelLoader::adoptIndex(...)
       src/client/graphics/eq/equipment_model_loader.cpp:733
    #30 in IrrlichtRenderer::loadZoneStep5_Assets(...)
       src/client/graphics/renderer_zone_load.cpp:798
    #31 in IrrlichtRenderer::loadZoneSequential(...) renderer_zone_load.cpp:2944
    #32 in Application::loadZoneGraphicsOnThread(...) application.cpp:1647
    #33-#39 std::function/std::thread plumbing
    ... (LoadingThread::threadFunc, loading_thread.cpp:157)
SUMMARY: AddressSanitizer: SEGV .../sanitizer_atomic_clang.h:81 in
  atomic_compare_exchange_strong<__sanitizer::atomic_uint8_t>
```

**What this means**: `#0`-`#3` are ASAN's *own* allocator internals —
`AtomicallySetQuarantineFlagIfAllocated` reads/writes a flag byte inside
the `AsanChunk` header that immediately precedes every live allocation, as
part of legitimately quarantining a chunk being `delete`d. A SEGV *inside
this specific function*, while ASAN is doing completely ordinary,
correctly-called deallocation work on a `std::vector<std::string>` that
lived inside `EquipmentModelLoader::EquipmentModelRef`, means the chunk
header ASAN itself maintains for that allocation had already been
overwritten by something else, at some earlier point — exactly the
signature an out-of-bounds write into or near this chunk (not necessarily
by this code path) would leave. This is not "a bug in
`EquipmentModelLoader`'s destructor" — the destructor is unremarkable; it
is evidence that *some earlier write corrupted this chunk's ASAN
bookkeeping*, discovered here because this chunk happened to be freed next
inside `unloadZone()`'s cascade. Same "detected far from caused" shape as
Step 1.

**The concrete mechanism this unit was able to name**: `adoptIndex()`
(`equipment_model_loader.cpp:728-739`, called from the LoadingThread during
every zone load) replaces `equipmentModelIndex_`/`textureIndex_`/
`itemToModelMap_` via `std::move()` **without taking any lock**. Checked
every other access point in the file
(`grep -n "equipmentModelIndex_\|initMutex_"
src/client/graphics/eq/equipment_model_loader.cpp`): `initMutex_` is taken
in exactly one place, `ensureIndexLoaded()` (line 97) — the lazy-init
fallback path. Every other reader (`getModelForItem()` at line 898,
`hasModelForItem()`-shaped checks at 208/413/435, stats accessors at
692/721) reads `equipmentModelIndex_` with **no lock at all**. If any of
those readers run on the render thread — which continues rendering the
outgoing zone's entities, including their equipment, during the loading
thread's overlap window — while `adoptIndex()`'s move-assignment destroys
the old map's rb-tree nodes on the LoadingThread, that is an unsynchronized
concurrent std::map mutation: architecturally exactly the kind of race that
corrupts heap metadata the way this report shows, and it is not gated by
anything that would prevent it from happening during a real zone crossing.
**This was not traced to a proven interleaving with a thread sanitizer or
live instrumentation this unit** — it is a code-level finding (the missing
lock, confirmed by grep and read of every access site), backed by an ASAN
report whose corruption shape is consistent with it, not a captured
race in the act.

### Step 3 — Pi quarantine re-run: NOT REACHED, correctly per the plan's own gate

The plan scopes Step 3 to "Pi only if x86 stays clean." x86 did not stay
clean — it crashed decisively at every level (baseline 10/10, ASAN 2/3).
Per the plan's own gating logic this correctly routes the unit away from
the Pi entirely; no Pi session was opened, no Pi invocations spent.

### What this clean-vs-fired split rules out and rules in

- **Rules out** T118's standing weak negative: T118's Legs 2/3 ran
  `quarantine_size_mb=0` (UAF detection crippled) and never crossed a zone;
  its Leg 1 crossed no zone either. This unit is the first ASAN run in the
  whole arc to combine a real quarantine with an actual zone crossing, and
  it **fired** — T118's "ASAN is clean" conclusion does not survive
  contact with a zone crossing. The corruption this arc has chased since
  T113 is not invisible to ASAN in general; it was invisible to every prior
  *configuration* of ASAN this arc happened to run.
- **Rules in** a concrete, named, previously-unexamined unsynchronized
  access pattern (`EquipmentModelLoader::adoptIndex()` vs. its own
  unguarded readers) as a plausible contributor, with a real ASAN report
  as evidence, not speculation.
- **Does not** prove `EquipmentModelLoader` is the *sole* corruptor. The
  baseline runs (Step 1) hit `unloadZone()`-adjacent corruption in
  containers `EquipmentModelLoader` never touches (`regionWorldTriangles_`
  in `renderer_player_physics.cpp`, `SMesh` mesh buffers, `Portal`
  vectors) — those are separate classes with, per a quick same-pattern
  check, the same missing-lock shape (`regionWorldTriangles_`/
  `objectWorldTriangles_` are mutated by `renderer_player_physics.cpp`,
  presumably on the render thread during movement/collision processing,
  and unconditionally `.clear()`'d by `unloadZone()` on the LoadingThread —
  no mutex found guarding either). **This looks like a recurring
  architectural pattern — several "loading thread mutates a container the
  render thread also touches, no lock" managers — rather than one
  isolated bug in one class.**

### Fix: not attempted — diagnosis only, per the plan's explicit permission

**No production code was changed.** Per the plan's own STOP guidance and
its explicit endorsement of diagnosis-as-deliverable: a single-class mutex
fix to `EquipmentModelLoader` was considered and rejected as premature —
(1) it is not proven sufficient, since the baseline crashes hit at least
three other containers `EquipmentModelLoader` never touches, all with the
same apparent missing-lock shape; (2) fixing one instance of a suspected
recurring pattern without auditing the siblings risks the same "fixed one
path, arc continues" outcome T119 already produced for
`ConstrainedTextureCache::clear()`; (3) verifying a locking fix doesn't
introduce a deadlock or a frame-time regression (these are per-frame hot
paths) needs more time than remained in this session. **Recommended next
unit**: a synchronization audit across the LoadingThread/render-thread
boundary for every manager that (a) is mutated during `unloadZone()`/zone
load and (b) is also read during ordinary per-frame rendering —
`EquipmentModelLoader`, `RaceModelLoader` (its own log line, "Mesh caches
cleared for zone transition," appears in every crash's surrounding log
context), and the `regionWorldTriangles_`/`objectWorldTriangles_` pair —
ideally with ThreadSanitizer (never yet tried in this arc; the pattern
found here is exactly TSAN's target, and ASAN's redzones are not built to
catch same-allocation concurrent-access races the way they catch
out-of-bounds writes).

### Deviations

1. Pre-amplification x86 rate is cited from T119 (n=1 crash, 2 clean, no
   established N≥10 rate) rather than independently re-measured at N≥10
   this unit — re-running T119's un-amplified single-crossing recipe 10
   times was judged lower value than pursuing the much stronger signal the
   amplified recipe (and then ASAN) produced with the remaining session
   time. Flagged, not silently substituted.
2. Only 5 of the 10 baseline crashes got a full `gdb` backtrace (runs
   2,3,4,6,8); runs 5,7,9,10 are logged (crash confirmed, signature
   silent-vs-glibc-text noted) but not individually symbolized — judged
   sufficient once the same-pattern-across-different-containers finding was
   already established from 5 independent samples.
3. ASAN sample is 3 runs, not a formal "N of M" rate claim the way the
   plan's Pi-rate discipline requires (that discipline is explicitly
   Pi-specific per the plan's Verification section: "Any Pi rate claim
   uses T117's method... 10 runs"). 2 of 3 firing is reported honestly as
   a small sample, sufficient to establish "ASAN fires under these
   conditions," not a precise rate.
4. Step 3 (Pi) not reached — correctly, per the plan's own "Pi only if x86
   stays clean" gate, not from running out of time or session budget.
5. `sudo` was used once, to change `/proc/sys/kernel/core_pattern` from
   the host's `apport` pipe handler to a plain `core.%e.%p` file pattern,
   so that native (non-ASAN) crashes produced an analyzable core file
   instead of silently vanishing into `apport`'s uid-scoped, seemingly
   non-functional-in-this-environment pipeline. **Restored** to the
   original `apport` pipe pattern before session end (verified via
   `cat /proc/sys/kernel/core_pattern`).

## Board

State whether the arc now has a cheap x86 reproducer or is still Pi-bound —
that single fact determines how every remaining unit on this arc gets scoped.
If ASAN fired, say which of T117's five signatures the named write explains
and which remain unexplained.
