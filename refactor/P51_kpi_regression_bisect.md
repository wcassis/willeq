# P51 — Canonical-KPI regression: attribute, root-cause, and re-baseline honestly

**Slot: slot1. OWNS THE PI (P02 protocol; ≤10 invocations).
Authorized by Will 2026-08-01.**

## Background

P48's KPI guard: canonical befallen e205 replay at tip = 34.3-34.6ms
p95 (blessed band 32.24-32.5, 30fps line 33.3), confirmed on repeat
AND reproduced with a pre-P48 binary — the regression landed in
a805e01 (P45's measurement) .. 9a61ed3 (P48 branch base). Window
contents: P46 `b49337b`, P46b `c40475d`, P46c `4a9b09d`, P46d
`f89e909`, P47 `6b78548`, P49-era? (NO — P49/P50 merged after
9a61ed3), testing-session merges (T15/T16 etc.), docs commits.
Methodology: benchmarks/audits/p48_sheet_alpha/EVIDENCE_MANIFEST.txt.

## PRIME HYPOTHESIS H1 — instrument honesty, not production regression

The canonical roster (races 1-12) contains NO masked races, weakening
the P46d texture-duplication suspect. But P46b replaced the
benchmark's entity handling: PRE-P46b the benchmark rendered entities
on the driver's built-in fixed-function materials (P44b: it never
linked entity_renderer's promotion); POST-P46b
applyEntityShaderMaterialPromotion() gives entities production's
per-pixel-lit shader — strictly more GPU work, and exactly what the
REAL CLIENT has always done. If H1 holds, P45/P39/P14/P11b's blessed
numbers all understated production entity-shading cost, and 34.3ms is
the honest production-representative figure (KPI actually NOT met);
the "regression" is the instrument catching up.

## Work (cheapest test first — H1 may collapse the whole bisect)

0. **H1 probe (1 invocation)**: tip binary, canonical KPI spot, with
   `--no-entity-shader-promotion`. Recovery into the 32.24-32.5 band
   confirms H1 (the delta = entity promotion); no recovery falsifies
   it → Phase A.
1. **Phase A (only if H1 falsified)**: bisect the window — build ARM
   binaries at candidate merge commits, 1 canonical spot each,
   binary-search order. Attribute to a commit, then root-cause the
   mechanism at code level.
2. **Phase B — verdict + deliverable**:
   - If H1/instrument-honesty: NO code change. Deliverable = a
     re-baselining memo (which historical numbers carry the bias and
     roughly how it scales with entity count), an updated
     honest-KPI statement for Will (production-representative p95 vs
     the 33.3ms line — connects directly to the lighting-lever data:
     per-pixel entity shading is part of this cost), and
     deferred.md/perf_truth_audit.md dated updates. The 34.3-34.6
     band becomes the new canonical reference (state it explicitly).
   - If genuine production regression: propose the fix if small and
     behavior-identical (gate + byte-identity bar), else STOP with
     memo. No silent fixes.
3. **Ride-along (P49, ~0 timed cost)**: `--res 400 300` WITHOUT
   --allow-mode-fallback → expect refusal (exit 1) + both ERROR
   lines; 800x600 control = any normal run's log showing no fallback
   line + the "Found matching mode" confirmation. Record in P49's
   deferred entry.

## Rules

- Pi protocol absolute (CmaFree gate, /tmp-only timed writes,
  drop_caches never reboot — one P02-documented reboot-recovery
  exception exists for CMA collapse, use only as last resort;
  pristine exit; DRM mode line recorded per run).
- NO production source changes without the Phase-B conditions above;
  NO preset changes; goldens untouched.
- Bar (measurement unit): builds clean, ctest baseline, gate PASS,
  ARM clean. CSVs → benchmarks/pi_runs/p51_kpi_bisect/.
- Sibling P52 (slot3) is x86-prep only and Pi-GATED behind you — it
  will not touch the Pi; you own it exclusively.

## Review

(executor fills in)
