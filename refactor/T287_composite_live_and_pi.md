# T287 — live composite zone-in on x86, then the Pi measurement (T284 hand-back; bears on decision #3)

Slot: whichever is free. OWNS eqemu (noclobber) and, for part 2 ONLY if
Will says so, the Pi. Class B (measurement). Territory: none in src/ unless
part 1 fails; measurement scripts + review/T287/.

## Part 1 — close T284's x86 gap (do this now, no permission needed)
`loadZoneStep0_EQE1` is only reachable through a real zone load. With
composites ON on x86 (`--eqe1-path`, composites enabled in the preset used
for the run — NOT the orangepi preset), zone in and record from
`EQT_EQE1_REQUEST_LOG_DIR`: packs opened, bytes mapped, and that the
composite sheets came in via `late-open`/index rather than a scan. Compare
against T284's benchmark numbers (1 pack, 174,856 B for a Human). Confirm
entities render (screenshot, 0 magenta) and nothing regressed vs T191/T192's
composite-adoption findings.

## Part 2 — the Pi run: DO NOT START WITHOUT WILL'S GO
Decision #3 (composites OFF on orangepi) was justified by address-space
exhaustion, which T284 removed. Whether to re-enable is Will's call. If he
says go: P02 protocol via `scripts/pi/pi_common.py`, ARM_BUILD_BIN_DIR
pointed at THIS slot, `<=12` invocations, drop_caches not reboot, leave the
Pi pristine. Measure with composites ON: VmPeak, VmRSS, packs opened, bytes
mapped, zone-in time, and whether it completes at all (T193's `std::bad_alloc`
is the failure to watch for). Report numbers only — the preset change, if
any, is a separate unit after Will reads them.

## Verification
Part 1: Review + evidence review/T287/ with the request-log TSVs and the
screenshot. Commit (docs/measurement only unless part 1 finds a defect):
`T287: composite zone-in verified live on x86` (+ ` and measured on the Pi`
if part 2 ran).
