# P11b — The true composite perf magnitude: animated entities through the production path, on the Pi

Approved by Will 2026-07-27 ("go ahead with P11b").
Slot: wt/slot1, branch task/P11b-composite-magnitude, cut from fable
(2a609ac tip). Measurement unit; Pi non-interactive (P02 protocol).
This retires two bad numbers: P08c's -33.1% (stand-in path with its
own cull bug) and P11's ~0% (static scenegraph route that never builds
composite meshes). Composite meshes build ONLY on the ANIMATED path
(single SMeshBuffer per entity) — measurable at last because P12b
fixed the benchmark's animated rendering (static-HW-buffer freeze).

## Design

### A. x86 preflight (correctness + calibration before any Pi spend)

1. **Animated-path determinism**: animation time is frame-indexed
   (frame*33.3ms) — verify an animated canonical-replay run is
   deterministic (two identical runs → geometry/pose columns
   byte-identical; timing columns exempt). If not, stop and report.
2. **Composite engagement proof**: with --composite-scenegraph +
   --animate, confirm via draw counts / logs that composite entities
   really dispatch as single-buffer meshes (fewer entity draws than
   multibuffer-animated at the same frames) — the thing P11's static
   route couldn't do. Quantify entity draws per frame both ways.
3. **Visible-entity calibration for the ANIMATED path**: P08b's static
   calibration (placed 205 → median 38 visible) may not transfer
   (P08b measured 4.5-5x visible inflation with the OLD --animate; the
   P12b fix may have changed behavior). Check whether entities_visible
   populates on the animated scenegraph path (extend the counter
   additively if not), re-run the calibration curve, and pick placed-N
   for median ~38 visible on the befallen canonical replay. Document
   the curve.
4. Known context, do not re-litigate: the low-magnitude atlas-seam diff
   under animation (P12b Z35 verdict) is a KNOWN visual item — this
   unit measures PERF; no pixel-identity bar beyond confirming no
   structural rendering breakage at 2 spot frames.

### B. Pi measurement (P02 protocol, canonical befallen replay, production config)

5. Variants (all --animate, calibrated placed-N, 1000-frame replay):
   a. multibuffer-animated (baseline for THIS comparison);
   b. composite-animated (--composite-scenegraph) — THE number;
   c. merge + composite-animated (the full honest stack) if budget
      allows — the real KPI-table entry.
   Whole-run avg/p95 + submission phase columns + anim_cpu_ms +
   pinned-pose frame extraction (bf355/472/626/791/917/987).
   Randomized order; <=12 invocations/session, 2 sessions OK.
6. Cross-checks: geometry/pose columns byte-identical across variants
   where expected (a vs b: entity draw counts differ BY DESIGN — zone
   columns identical; document expected deltas); unmerged zone columns
   vs P08b/P09c baselines where comparable (animated visibility may
   legitimately differ from static baselines — compare and explain
   rather than assert).

### C. Deliverables

7. **The corrected number**: composite-animated vs multibuffer-animated
   (avg/p95/fps, entity draws, submission ms) — alongside the two
   retired numbers (P08c -33.1%, P11 ~0%) with one line each on why
   they were wrong.
8. **The honest KPI table update**: where does befallen stand with the
   full stack (merge + real composites, animated) against 33.3ms/30fps;
   the measured residual and its split (entity submission / zone
   submission / GPU / anim CPU).
9. Recommendation: what (if anything) remains between befallen and
   30fps, and which deck item addresses it.

## Verification

- Standard bar: build, ctest (known host exceptions), gles2-x86 build,
  visual gate PASS goldens untouched, ARM build, file-size check.
- A1 determinism proof; A2 engagement proof; replay discipline (pose
  columns vs path file).
- Pi rules: P02 via pi_common.py, /tmp-only I/O, ARM_BUILD_BIN_DIR trap
  (slot1 build — rebuild ARM first so P12b's fix + all flags are in the
  staged binary), drop_caches never reboot, pristine exit.
- CSVs → benchmarks/pi_runs/p11b_composite_magnitude/.

## Out of scope

- Any production change; visual fixes (seam/foliage/pop-in); the
  eviction mystery; preset flips (Will's).

## Review

(executor fills in)
