# P34 — ParticleManager atlasTexture_ use-after-free (CRITICAL)

**Slot: slot1. x86 only — no Pi. OWNS EQEMU (honor the .eqemu-lock
protocol). Sole agent this dispatch.**

The top-priority item on the deck (deferred.md CRITICAL entry,
2026-07-29): live qeynos2 sessions SIGSEGV ~1-2s after zone-in — a
use-after-free on a dangling `atlasTexture_` inside
`ParticleManager::renderUnified()`. Found and gdb-isolated by P33b;
both P33b and P33c had to gag the call site locally to do any live
work. Plausibly crashes Pi gameplay identically (renderUnified is
GLES2-only, runs whenever zoneReady && have3DTransforms).

## Trusted context (do not re-derive)

- P23 fixed the renderUnified/SimulationWorker RESULT-BUFFER race
  (TSAN-proven, tools/particle_race_stress.cpp) and removed the
  P10b-era isEnabled() gate — renderUnified had never actually run in
  production before that. This crash is a DIFFERENT bug (texture
  lifetime, not buffer lifetime) that the gate removal exposed.
- The bug class is established: P10b (region nodes) and P17 (entity
  nodes + RaceModelLoader cache) both fixed dangling-ITexture* holds
  via the two-layer pattern — an eviction/unload listener that clears
  cached pointers + a defensive isLiveTexture() check at use sites.
- P17 made zone unload SYNCHRONOUS on the loading thread — textures
  of the outgoing zone are now released DURING zone transition. The
  ~1-2s-after-zone-in timing smells like the particle atlas being
  created once (init or first zone) and freed by a later unload/evict
  while ParticleManager retains the raw pointer. VERIFY, don't assume.
- P33b's gdb backtrace + disassembly notes are in
  benchmarks/audits/p33_tree_wind/EVIDENCE_MANIFEST.txt.

## Work

1. Reproduce (eqemu, lock first): login → befallen → #zone qeynos2 —
   the crash was 100% within ~1-2s for P33b/P33c. Capture your own
   backtrace with a debug build if the existing notes don't pin the
   exact free site.
2. Root-cause the atlasTexture_ lifecycle: where it's created, every
   place it can be freed (zone unload via P17's synchronous path?
   ConstrainedTextureCache eviction? driver texture removal?), and
   which path frees it out from under renderUnified. Name the exact
   sequence.
3. Fix with the established pattern, matching whichever layer owns
   the atlas: clear/re-acquire on unload via the existing listener
   mechanisms + defensive liveness check before use (mirror P10b/P17
   idioms — do not invent a new lifetime scheme). If the atlas should
   simply be re-created per zone, that's acceptable if it matches how
   it's built today — explain the choice.
4. Prove it: (a) the exact repro survives — a live qeynos2 session
   running ≥3 minutes with particles enabled, plus a multi-hop
   (befallen↔qeynos2×2) session, no SIGSEGV; (b)
   tools/particle_race_stress still clean (don't regress P23);
   (c) if feasible cheaply, a targeted unit/stress test for the
   texture-lifetime path — manual tool acceptable, document the
   invocation.

## Rules

- Territory: particle system files (particle_manager.*,
  renderer_sim_bridge/renderer_frame call sites if needed), the
  texture-cache listener wiring for the atlas. NOT the driver, NOT
  zone loaders beyond listener wiring, NOT the SimulationWorker (P23's
  fix stays as-is).
- No behavior changes beyond crash removal: particles render as
  before when alive; goldens untouched (particles aren't in goldens,
  but run the gate).
- eqemu per protocol (lock, sanctioned scripts, Testchar restore to
  zone_id=36, x=-77.9232, y=21.9086, z=3, h=190, clean shutdown, lock
  removed).
- STOP: the root cause lands in driver texture-removal semantics
  (report with evidence), or the fix requires reworking atlas
  ownership across threads (report the design need).

## Verification

- Full current bar: GLES2 tree build + ctest at current baseline
  (~1376/1377, ALSA exception; testing session may have moved the
  count — report actual), build-gles2-x86.sh clean, visual gate PASS
  goldens untouched, ARM build clean, file sizes clean.
- The step-4 survival proofs (session durations, hop counts, crash-free
  logs cited).

## Review

(executor fills in: backtrace, lifecycle map, the freeing sequence,
fix description + why it matches the established pattern, survival
evidence, deviations)
