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

(executor fills: whether the Pi ran the client and any deployment obstacles;
the paired category table, x86 versus Pi, same zone same session; **the Pi's
untracked share and how much of x86's is llvmpipe**; RSS against the Pi's
~395MB headroom — over or under, and by how much; the Pi's zone-load time;
`/tmp` free space before and after; which x86 findings do NOT apply to the
target; deviations)

## Board

This decides whether the x86 memory numbers this project has been reasoning
from are representative of the target at all. Record it plainly — several
earlier conclusions rest on them.
