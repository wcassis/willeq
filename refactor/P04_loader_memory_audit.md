# P04 — EQE1 loader fix + full sequential-loader memory audit

Will's directive (2026-07-24, decision recorded in docs/decisions.md):
ETC1 stays; fix the loader; and audit the ENTIRE sequential loader path so
no unneeded resources persist in memory. Historical signal: `/pmem`
(runPmemDiagnostics, eq_commands.cpp) has shown a large amount of UNTRACKED
RSS — memory not attributable to the known pools.

## Part 1 — Loader fix (the P03 finding)

The v1 EQE1 loader heap-loads entire pack files (eqe1_loader.cpp),
including global archives never sampled in the zone (P03 measured 139MB of
never-referenced archive bytes resident on a 494MB board — the confirmed
cause of the EQE1 stall bursts and the leading explanation for the P01
panic via memory pressure).

Fix design (verify feasibility against the format first — E01's format doc
and eqe1_format.h):
1. **mmap the pack files** instead of whole-file heap reads; entry payloads
   are consumed via pointers into the mapping (upload reads them once;
   page cache handles residency; madvise(MADV_DONTNEED/SEQUENTIAL) after
   upload of each entry, or unmap per-archive after its entries upload).
2. **Open/map only referenced archives**: resolve which archives the zone
   actually references before mapping (resolveEQE1Archives + the
   name-lookup index); global archives open lazily on first lookup miss.
3. Preserve v1 format compatibility and the v2 (P03) payload paths; ETC1
   upload semantics byte-for-byte unchanged.

## Part 2 — Sequential loader memory audit (loadZoneSequential steps 0-13)

The G01 decomposition gives a clean audit surface: 20 step methods +
ZoneLoadContext in renderer_zone_load.cpp.

1. **Instrument**: flag-gated per-step memory sampling (RSS via
   /proc/self/status VmRSS, plus the known pools: ConstrainedTextureCache/
   MeshCache bytes, EQE1 mapped bytes, entity prep buffers) logged at each
   step boundary — a `--load-mem-audit` mode on willeq/benchmark. Also
   extend /pmem (runPmemDiagnostics) to print the same accounted-vs-RSS
   breakdown so the "untracked RSS" gap becomes a number with named
   suspects.
2. **Audit each step** for resources that persist past their need:
   decode/staging buffers not freed after GPU upload, WLD/S3D parse
   artifacts retained, ZoneLoadSnapshot lifetime, prep-worker results kept
   after distribution, pendingZoneComputations_ contents,
   RaceModelLoader/EquipmentModelLoader caches vs their budgets, glibc
   arena bloat (malloc_trim consideration — measure before adding).
3. **Fix what the audit finds** — each fix separately justified in the
   review with before/after RSS numbers. Behavior bar: full suite + visual
   gate + replay oracle green; zone loads still work end-to-end (benchmark
   load phase + willeq headless zone-in via the replay/tour machinery).
4. **Quantify the win on the Pi**: post-fix load-phase RSS curve + steady
   RSS for the three golden zones, EQE1 stall-burst check (expect 8/8
   burst-free now even with the full production pack dir — THE acceptance
   test: P03's confound becomes the proof), CmaFree/page-cache behavior.
   P02 protocol applies (interleaving, caps, /tmp I/O, reboots).

## Deliverables

- The accounted-vs-untracked RSS breakdown, before and after, per step.
- Loader fix with Pi-verified stall elimination on the FULL pack dir.
- Fix list with per-fix RSS deltas.
- Updated /pmem output documented (it's a user-facing diagnostic Will uses).

## Review

(executor fills in)
