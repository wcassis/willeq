# T107 — Paired Pi/x86 memory baseline, measured simultaneously

**Slot: whichever is free. OWNS EQEMU — takes the lock, runs both clients,
tears down. Requires the Orange Pi.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T107/`, throwaway
deployment and capture scripts. PRODUCTION CODE IS READ-ONLY — this unit
measures.**

Read **T106's Review first** — it establishes the x86 attribution method, and
this unit reuses it rather than inventing a second one. Then T105's Review
(mapped-vs-resident, and the working-set numbers).

## Why paired, not sequential

T106 is reasoning about what *would* differ on the Pi — principally whether
the untracked third of RSS is largely **llvmpipe**, the software rasterizer
this dev VM uses in place of a GPU. **The Pi can answer that directly**: it has
a real Mali400 and the native GLES2 driver (`/dev/dri/card0`, `card1`,
`renderD128` confirmed present).

Running both **simultaneously, in the same zone, against the same server
session** removes the confound that makes two sequential runs incomparable:
different spawn populations, different server state. Pair on **zone state**,
not wall-clock — the Pi is four slow cores and will lag; that is expected and
is not a defect.

## The environment, verified while scoping

| | |
|---|---|
| Pi host | `10.0.30.192`, user `user` (see `scripts/pi/env.sh`) |
| Pi memory | **483MB total, ~88MB already in use → ~395MB headroom** |
| Pi `/tmp` | **242MB tmpfs** — see the capture discipline below |
| Pi cores | 4 |
| Pi GPU | real DRM nodes present; native GLES2, **not** llvmpipe |
| Pi assets | `/mnt/projects/EverQuest_ETC1` mounted and populated |
| eqemu | `10.0.30.23`, login `5998` (same subnet as the Pi) |
| existing tooling | `scripts/pi/` — `env.sh`, `bench_pi.sh`, `pi_bench_run.py`, `compare_pi.sh`, `pi_common.py` |

**Reuse `scripts/pi/` rather than writing a new deployment path.** It already
knows the host, the ARM binary directory and the remote paths.

## The decisive question

**What is the Pi's actual untracked share, and how much of x86's is
llvmpipe?**

x86 shows ~34-36% of RSS untracked across three zones. If that is largely the
software rasterizer, the Pi's share should be dramatically smaller — and the
memory picture on the real target is far better than the x86 numbers imply.

If the Pi's untracked share is **similar**, then llvmpipe is not the
explanation and something real is unaccounted on both. Either answer is
valuable; **do not assume the flattering one.**

## Scope

1. Deploy a **stripped** ARM binary (`scripts/build-arm-noble.sh` output).
   T61 already had to strip to make room in the Pi's `/tmp`.
2. Bring up eqemu, take the lock normally.
3. **Both clients into the same zone at the same time**, connected to the same
   server session. The Pi joins as an ordinary client — the lock governs
   server lifecycle, not client connections (T85 established the guest
   pattern).
4. Capture the same category breakdown on both, using **T106's method**.
5. Report side by side, with the delta explained rather than just displayed.

Use a zone with existing baselines — `befallen`, `nektulos` or `qeynos2` — so
the numbers are comparable with T80, T105 and T106.

## Capture discipline — the Pi will bite you here

`/tmp` is a **242MB tmpfs on a 483MB machine**. Writing captures there
competes directly with the process you are measuring, which would corrupt the
result.

**Pull captures off the Pi as you go, or write them to the NFS mount.** Do not
accumulate on tmpfs. Check free space before and after and report it.

## Also worth capturing while you are there

The Pi's **zone-load time**, as a byproduct. T91 and T94 took Nektulos from
246s to 14.4s **on x86** — nobody has measured the Pi, where SD-class storage
and four slow cores may tell a very different story. Note it; do not chase it.

## What you may not do

- **No production code changes.** This unit measures.
- Do not add memory counters — that decision follows T106 and is a separate
  unit.
- Do not tune, strip or reconfigure the Pi to make numbers look better. The
  baseline is what the target actually does today.
- Do not report the flattering interpretation without the evidence for it.
- **If the Pi cannot run the client at all, that is the finding** — report
  precisely where it fails and stop. Do not spend the session fighting
  deployment.

## Verification

Full build and suite on x86 (current total 2247; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`) plus the ARM
cross-compile. Visual gate — goldens must not move, since behaviour is
unchanged.

Session discipline: verify binary freshness against HEAD on **both** machines;
force daylight `/say #set time 11 0`; pad scripts far longer than you think —
longer still for the Pi; `-d 4` plus `--log-level=DEBUG`; restore DB state and
confirm by `SELECT` **after logout**; kill clients by real binary PID on both
hosts; clean teardown, delete the lock.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T107-pi-x86-baseline`, base fable tip `ab0e6c06`. Live eqemu
measurement performed (lock free at session start; taken, used, released
cleanly; both moved test characters restored to their documented resting
positions and confirmed by `SELECT` after logout). **Read-only: no
production code changed** — only new files under `benchmarks/audits/T107/`
(small config/tour/driver-script files) and this plan document; the large
captures live in `/mnt/projects/fable/review/T107/` (README there indexes
everything, cross-references below).

### The Pi ran the full client — no deployment obstacle

Deployed the stripped ARM `willeq` binary (10,242,800 bytes, ~10 MB,
`scripts/build-arm-noble.sh` output stripped with
`arm-linux-gnueabihf-strip`, matching CLAUDE.md's stated ~10 MB) to the
Pi's `/tmp` (tmpfs). It connected to eqemu at `10.0.30.23:5998`, logged in,
selected a character, zoned into Befallen (a real, populated, indoor zone —
`ZoneLightVis`, `EntityVis`, `RegionVis` debug lines all show real
visibility-culling activity against real zone content), and ran a scripted
tour (`/say #set time 11 0`, idle hold, `/pmem`, `/camp`) to a clean
**server-side** logout, all while an x86 client was simultaneously
connected to the same zone, same eqemu session. **The plan's stated risk
("the client may not fit at all") did not materialize** — see the RSS/
headroom finding below.

One genuine, unplanned finding, reproduced twice (a preliminary smoke test
and the paired session itself): **the Pi client SIGSEGVs during its own
local shutdown teardown, after the server has already confirmed a clean
logout.** `Logout confirmed by server` and `Game thread stopped` both
appear in the log before the crash in every run. This does not affect any
measurement in this unit — the gdb capture and the server-side logout both
complete cleanly first — but it is a real, ARM-only defect.
**Recommended, not fixed**, per this unit's "no production code changes"
rule; added to `refactor/deferred.md`.

### Paired capture — same zone, same eqemu session, overlapping windows

Pairing was built on **zone state, not wall-clock**, per the plan: the Pi
was launched first (its own zone-load is ~3.6x slower, measured below),
polled until its log showed `Graphics loading complete`, and only then was
the x86 client launched. Both tour scripts hold their client idle in-zone
for minutes after their own zone-load completes specifically to build a
wide overlap window. Confirmed overlap (UTC, Pi's EDT timestamps converted
— clocks cross-checked via `date -u` on both hosts, exact, no offset
correction needed beyond the timezone conversion): x86 connected
15:50:56–15:53:51, Pi connected 15:48:18–~15:52:3x; the two `mallinfo2()`/
smaps capture instants (x86 15:51:51, Pi 15:51:38) are 13 seconds apart,
both squarely inside the ~3-minute overlap window.

Two accounts were needed (`willeq_test`/`Testchar` for x86,
`willeq_buddy`/`Testbuddy` for the Pi) — eqemu allows only one active
session per account, so the single `willeq_test` account this project
otherwise uses could not run both clients at once. Both characters'
`zone_id`/position were set to Befallen before the session (documented
character-position writes) and restored to their pre-session values
afterward, confirmed by `SELECT` after logout.

**Method reused verbatim from T106** — `mallinfo2()`/`malloc_stats()` via
`gdb`, target launched as `gdb`'s own child (`yama.ptrace_scope=1`
confirmed on the Pi too, same restriction as the x86 dev VM), status+smaps
read at the same ptrace-stopped instant as the allocator calls. Only the
`STOP_AFTER` timer value and output path differ between the x86 driver
script (55s, identical to T106) and the Pi driver script (200s, chosen so
the capture instant falls well inside the x86 client's connected window).

| | x86 (t=55s) | Pi (t=200s) |
|---|---|---|
| VmRSS | 593,836 KB (**579.9 MB**) | 95,100 KB (**92.9 MB**) |
| Tracked total (`/pmem`, in-game) | 63.3 MB | 62.4 MB |
| **Untracked (RSS − Tracked)** | **516.6 MB (89.1%)** | **30.5 MB (32.8%)** |
| llvmpipe (`libgallium`) resident | 11.6 MB | 0.54 MB |
| LLVM JIT (`libLLVM`) resident | 59.0 MB | 0.004 MB (126 MB *mapped*, 4 KB *resident* — linked, never exercised) |
| **llvmpipe + JIT combined** | **69.0 MB (11.9% of RSS)** | **0.55 MB (0.6% of RSS)** |
| `[heap]` (smaps) | 161.5 MB (27.2%) | 3.1 MB (3.4%) |
| `anon-other` (smaps) | 332.5 MB (56.0%) | 86.9 MB (92.4% of a far smaller pie) |
| self-binary resident | 15.3 MB | 2.4 MB |
| other shared libs + EGL/GLES glue | 12.7 MB | 0.9 MB |
| `mallinfo2()` arena (all threads) | 479.5 MB | 198.0 MB (**exceeds Pi RSS** — see caveat below) |
| `mallinfo2()` fordblks (nominal fragmentation) | 125.3 MB (21.6%) | 48.0 MB (nominal 51.7% — **not trusted**, see caveat) |
| New threads spawned this session | 75 | 10 |

Full smaps categorization (T106's `analyze_smaps.py`, reused unmodified):
`review/T107/smaps_categorized_summary.txt`. Raw captures:
`review/T107/x86/`, `review/T107/pi/`.

### The decisive question, answered directly

**llvmpipe is confirmed almost entirely absent on the Pi in absolute
terms** — 0.55 MB vs 69.0 MB, matching T106's prediction closely. But
**llvmpipe explains only ~14% of the total x86→Pi RSS gap.** The gap is
487.0 MB; llvmpipe+JIT accounts for 68.4 MB of it. The remaining ~86%
(~418.6 MB) is dominated by `[heap]` collapsing 157.7 MB→3.1 MB and
`anon-other` collapsing 324.8 MB→84.9 MB (together ~81% of the total
delta), while **Tracked** — the real graphics content, character models,
zone geometry, textures — barely moves (63.3 MB x86 vs 62.4 MB Pi, same
zone, same nearby character types, consistent with T105's finding that the
same subsystems produce similar absolute costs regardless of host).

**Per the plan's explicit instruction not to assume the flattering
answer**: the corrected untracked share is dramatically smaller on the Pi
(89.1%→32.8%), which reads flattering at a glance, but the *cause* is not
what the plan's own hypothesis suggested. It is not primarily the software
rasterizer — it is a much smaller general allocator/thread-bookkeeping
footprint, most plausibly downstream of far fewer threads (75 spawned in
the x86 session, almost certainly llvmpipe's own rasterizer worker pool,
versus 10 on the Pi) and smaller pointer/object sizes on 32-bit ARM.
**Stated as informed hypothesis, not proven attribution** — this unit's
tooling (smaps + aggregate `mallinfo2()`) cannot attribute anonymous bytes
to a specific subsystem, exactly the limit T106 already documented.

### A new methodological limit, found on the Pi (not on x86)

T106 validated, on x86, that `mallinfo2()`'s arena total (502.8 MB in this
session) sits comfortably inside RSS (593.8 MB) — i.e., arena ⊆ RSS holds,
which is what let T106 treat `fordblks` as a real resident-fragmentation
figure. **On the Pi, that self-consistency breaks**: `malloc_stats()`'s own
"Total (incl. mmap)" system bytes is 250.5 MB — **2.7x the entire process
RSS (92.9 MB)**. This means a large fraction of what glibc's allocator
considers "committed" arena space on the Pi is never actually
touched/resident — brk/mmap-reserved, demand-paged, never faulted in — the
same PROT_NONE-style over-reservation pattern T106 found for x86's
per-thread arenas, but proportionally far more extreme here. **The
resulting `fordblks` figure (48.0 MB, nominally 51.7% of RSS) should not
be read as real resident fragmentation the way T106's x86 reading could
be** — it is very likely a substantial overestimate, computed against a
mostly-non-resident base. Reported as a genuine, previously-unknown
methodological limit rather than silently folded into a headline
percentage, per the plan's "do not report a residual as an attribution"
rule.

### RSS against the Pi's ~395 MB headroom

**Under, by a wide margin.** Live paired-session Pi RSS was 92.9 MB against
the plan's stated ~395 MB headroom — **23.5% of headroom used, ~302 MB
free**. The plan's stated concern ("this unit may find the client does not
fit at all") did not materialize on this measurement. This number is a
single-process snapshot at one instant (t=200s of a ~4.5-minute session,
inside the settled idle-hold window, not a peak-load probe) — it is not a
worst-case/peak-load figure, and this unit did not attempt to construct
one (out of scope; the plan asks for a baseline, not a stress test).

One caveat on completeness, not chased further per the plan's scope: the
Pi's smaps shows 13,250 separate `renderD128` (DRM device) mappings
totalling 88.1 MB of *mapped* space but 0 KB *resident* in every one — a
known characteristic of DRM/GEM buffer objects, which the kernel often
does not attribute to the owning process's smaps `Rss` at all. The Pi's
real GPU-side (CMA) memory pressure may not be fully visible in the 92.9 MB
figure. `scripts/pi/pi_common.py`'s `CmaFree` monitor exists for exactly
this but is wired into the benchmark-tool path, not a live-client eqemu
session — flagged as a gap for a future unit.

### Zone-load time (byproduct, not chased)

| | x86 | Pi | Ratio |
|---|---|---|---|
| Zone-request → ready | 10.3s | 37.3s | 3.6x |
| Process-start → ready | 14.6s | 53.2s | 3.6x |

The x86 process-start-to-ready figure (14.6s) closely matches T91/T94's
already-established ~14.4s Nektulos number (different zone, same order of
magnitude), cross-validating the timing method. SD-class storage and four
slow cores make the Pi ~3.6x slower end to end — noted, not investigated
further, per the plan.

### `/tmp` free space

242 MB free (0% used) before staging → 232 MB free (9.8 MB used, the
staged stripped binary) during the session → **242 MB free (0% used)
again** after teardown (`rm -rf` on the staged binary). No capture (smaps
dump, gdb log) ever touched the Pi's tmpfs — all written directly to the
NFS mount per the plan's capture discipline, moved into
`review/T107/` afterward.

### Which x86 findings do NOT apply to the target

- **The ~69-73 MB llvmpipe+JIT cost (T106)**: confirmed absent, as
  predicted — 0.55 MB on the Pi, essentially zero.
- **T106's `mallinfo2()`-based fragmentation reading (~25.6% of RSS on
  x86)**: does NOT transfer even as a *ratio* — the underlying arena
  bookkeeping is not self-consistent with RSS on the Pi (see the
  methodological-limit finding above), so neither the x86 absolute figure
  nor its percentage can be assumed to hold on ARM.
- **T105's "34-36% untracked" / T106's corrected "75-90% untracked"
  figures**: do NOT transfer to the Pi. This unit's own measurement (same
  method, same zone, same live session) finds 32.8% on the Pi versus 89.1%
  on x86 in the identical paired session — a completely different
  headline number on the real target.
- **The `[heap]`/`anon-other` smaps split (T106: heap 27.2%, anon-other
  56.0% of RSS)**: collapses on the Pi (heap 3.4%, anon-other 92.4% of a
  ~6x smaller pie) — the *shape* of where RSS goes is different, not just
  the scale.
- **What DOES transfer**: the Tracked-category numbers (character models,
  zone geometry, textures) — 63.3 MB x86 vs 62.4 MB Pi, essentially
  identical for the same zone and nearby entities, consistent with T105's
  finding that these subsystems cost similar absolute bytes regardless of
  host. Self-binary resident size also scales sensibly (15.3 MB unstripped-
  debug x86 build vs 2.4 MB stripped ARM build).

### Deviations from the plan

- **Two separate accounts/characters were needed**, not stated explicitly
  in the plan (which assumed a single paired session without addressing
  eqemu's one-session-per-account limit). `willeq_buddy`/`Testbuddy` was
  used for the Pi client alongside `willeq_test`/`Testchar` for x86 — both
  already existed per `scripts/eqemu/env.sh`'s documented character roster,
  no new accounts created. Both restored to their pre-session DB state,
  confirmed by `SELECT` after logout.
- **A private `data/`/`config/` working directory was built for the Pi
  client** (`benchmarks/audits/T107/pi_run/`, ~19 MB, not committed —
  regenerable, session-specific), rather than pointing the Pi at the
  shared slot1 checkout directly, because `data/willeq.db` is opened
  read-write and two processes opening the same SQLite file concurrently
  over NFS is a real corruption risk the plan did not call out. Not a
  scope change — just an implementation detail needed to make "both
  clients in the same zone at the same time" safe.
- **A preliminary smoke-test session** (short tour, no gdb wrapping) was
  run before the full paired capture, to confirm the Pi could connect and
  zone in at all before committing session time to the more complex
  gdb-wrapped paired capture — same discipline `EXECUTOR_PROTOCOL.md`
  prescribes for visual captures (T75), applied here to deployment risk.
  Its raw log was not preserved (deleted from the Pi's tmpfs as part of
  capture-discipline hygiene) — the identical shutdown-SIGSEGV finding it
  surfaced is independently reproduced, with full log context preserved,
  in the paired session itself, so nothing is lost.
- **Scope grew, flagged rather than absorbed**: the `mallinfo2()`
  self-consistency breakdown on the Pi (arena total exceeding RSS) was not
  anticipated by the plan and is a genuine methodological finding in its
  own right, not just a number to report. Follows directly from applying
  T106's method faithfully rather than adapting it, per the plan's "reuse
  it, do not invent a second one" instruction.
- No production code changed — confirmed via `git status --short` (only
  new files under `benchmarks/audits/T107/` and this plan document).

### Verification

1. `cmake --build build -j24` — PASS (no source changes).
2. `ctest --test-dir build --output-on-failure` — **2246/2247 passed**,
   sole failure the documented pre-existing ALSA
   `AudioManagerTest.PreloadCommonSounds`, matching baseline exactly.
3. `bash scripts/build-gles2-x86.sh` — PASS, `build-gles2/bin/{willeq,gles2_zone_benchmark}`
   rebuilt cleanly; this run's own `visual_gate.sh` invocation (embedded in
   the script) reported `=== VISUAL GATE: PASS ===`, goldens untouched.
4. `bash scripts/build-arm-noble.sh` — PASS, ARM cross-build succeeded
   (`build-arm-noble/bin/willeq`, 366,901,548 bytes unstripped, stripped to
   10,242,800 bytes for Pi deployment). Binary freshness verified against
   HEAD (`ab0e6c06`, committed 15:16:34) on both hosts: x86
   `build-gles2/bin/willeq` built 15:35:45, ARM `build-arm-noble/bin/willeq`
   built 15:25:46 — both after HEAD.
5. `scripts/refactor/check_file_sizes.sh fable` — "no C++ files to check
   ... nothing was checked" — correct and expected, no `.cpp`/`.h` files
   changed vs `fable` in this slot.

### Teardown

Both clients confirmed logged out server-side (`Logout confirmed by
server`) before either process fully exited. `down.sh` run, `status.sh`
confirms no containers/ports remain, lock file deleted. `Testchar`
restored to `zone_id=36, x=-77.9232, y=21.9086, z=3, h=190` (the project's
documented resting position); `Testbuddy` restored to its own pre-session
state (`zone_id=77, x=156, y=-1009, z=51, h=77`); both confirmed by
`SELECT` after logout. Pi left pristine: `/tmp` at 0% used, no `willeq`/
`gdb` processes remain, CPU governor never touched by this unit (no
`GovernorPin` used — this was a memory measurement, not a timing
benchmark).

## Board

This decides whether the x86 memory numbers this project has been reasoning
from are representative of the target at all. Record it plainly — several
earlier conclusions rest on them.
