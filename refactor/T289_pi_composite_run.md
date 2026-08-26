# T289 — the Pi composite measurement (T287 Part 2; Will gave the go 2026-08-26)

Slot: slot1. OWNS the Pi. NO eqemu unless a zone-in needs it (then noclobber).
Class B, measurement only. Territory: measurement scripts + review/T289/.
**Do NOT change `config/constrained_presets.json` or any source.** Re-enabling
composites on the orangepi is a SEPARATE decision after Will reads these numbers.

## Why this is worth a Pi session
Decision #3 (composites OFF on orangepi) rests on T195/T197/T198: `std::bad_alloc`
on the Pi. T195 proved the cause precisely — `EQE1Loader::open()` **mmaps**, and
the eager loop mapped the whole composite directory: **VmSize peaked at 3,128 MB
against a ~3 GB 32-bit user ceiling, while RSS rose only ~40 MB and MemAvailable
never fell below ~260 MB**. T195's own words: *"the platform difference is 32-bit
vs 64-bit, not 483MB vs 32GB."*
T284 deleted that eager loop (1506 packs → 1 per sheet; 4,676,044 kB → 174,856 B
for a Human), and T287 Part 1 confirmed it live on x86: 7 sheet packs for
befallen's 7 composite races, all via index late-open, and composites ON mapped
**3.2 MB LESS in total** than OFF.

## What is genuinely unknown and only the Pi can answer
1. **VmSize/VmPeak with composites ON** — does it now sit far below the 3 GB
   ceiling? (Expected yes; this is the direct test of the decision's premise.)
2. **RSS** — mapping is cheap but the GPU-side upload of a decoded sheet is not.
   This is the number T195 never had to care about and the one that could still
   argue against composites on a 512 MB board.
3. **Zone-in time** and whether it completes at all (T193's `std::bad_alloc` is
   the failure to watch for).
4. Whether Mali/Lima behaves like llvmpipe for the composite draw path.

## Method
P02 protocol via `scripts/pi/pi_common.py`. `ARM_BUILD_BIN_DIR` must point at
THIS slot's `build-arm-noble/bin` (env.sh defaults to main — the trap). Rebuild
ARM before staging. All timed-run I/O to Pi-local `/tmp`. `drop_caches`, never
reboot. **≤12 invocations.** Leave the Pi pristine (governor ondemand, /tmp
clean). Pi is `user@10.0.30.192` via `scripts/pi/env.sh` — `opi1` in ssh config
is STALE. Report blocked if unreachable (kmsg: `/mnt/projects/fable/pi_console/`).

Run the same zone twice: composites ON (override the preset ON THE COMMAND LINE
or via a run-local config — not by editing the committed preset) and the shipped
OFF baseline. Sample RSS/VmSize/VmPeak/MemAvailable every 0.3 s as T195 did, so
the numbers are directly comparable to its table. Capture `EQT_EQE1_REQUEST_LOG_DIR`
for packs opened and bytes mapped.

## Deliverable
A table in the Review directly comparable to T195's, plus a plain statement:
does the address-space argument still hold, and does RSS give a NEW argument
against composites on this board? **Numbers and a recommendation — no preset
change.**

## Verification
No source change, so no ctest/gate needed unless something is fixed. Review in
this file; evidence review/T289/. Commit: `T289: composites measured on the Pi`.
