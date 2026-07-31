# P49 — DRM mode fallback: fail loud (P43 follow-up)

**Slot: slot2. x86 ONLY — no Pi (P48 owns it), no eqemu. Authorized
by Will 2026-07-31 (item 2).**

## Background

P43 (refactor/P43_res_visibility_root_cause.md): when --res has no
exact DRM mode match, docker/irrlicht-drm/CIrrDeviceFB.cpp:335-382
silently substitutes the connector's preferred mode and overwrites
Width/Height — every historical "400x300" Pi run actually rendered
2560x1440. The trap is still armed for anyone requesting a
non-native resolution.

## Work (split by consumer, keep production robust)

1. **Client (production)**: keep the fallback (a client that can't
   boot on an unknown monitor is worse than one at the wrong res) but
   make it LOUD: ERROR-level log stating requested vs actual mode,
   repeated once post-init (not just buried in device-creation
   spew).
2. **Benchmark**: gles2_zone_benchmark REFUSES to run when the
   achieved mode != requested resolution, unless
   --allow-mode-fallback is passed; either way the ACTUAL WxH goes
   into the CSV header/columns so no dataset can ever be mislabeled
   again.
3. x86 has no DRM path, so behavioral verification is limited here:
   unit-verify the decision logic (extract it into a small testable
   helper if needed), build-verify via the ARM cross-build, and
   register the 1-invocation Pi behavioral check (request 400x300,
   expect refusal + log line) as a RIDE-ALONG item in deferred.md
   for the next Pi-owning unit — do NOT touch the Pi yourself.

## Rules

- Files: docker/irrlicht-drm/CIrrDeviceFB.cpp + benchmark tools +
  tests. Do NOT touch entity/texture code (slot1/slot3 territory).
- Full bar: GLES2 ctest baseline, visual gate PASS (should be
  trivially unaffected — llvmpipe path has no DRM), ARM cross-build
  clean, file sizes.
- Small unit — if it grows beyond the above shape, STOP and report.

## Review

(executor fills in)
