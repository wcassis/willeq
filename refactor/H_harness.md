# Batch H — Automated Verification Harness (llvmpipe + eqemu)

Goal: the VM verifies the REAL production rendering path and full client
lifecycle with zero manual steps, so the Orange Pi is needed only for
frame-timing baselines and milestone sign-off.

Background decisions (from discussion with Will, 2026-07-23):
- GLES2 is the single rendering path going forward; desktop-GL/software
  tiers are slated for removal with Irrlicht.
- The x86 build currently uses desktop GL via system Irrlicht — the real
  GLES2 backend (docker/irrlicht-drm/COpenGLES2Driver + friends) has only
  ever been compiled inside the ARM Docker image. H01 fixes that.
- llvmpipe (mesa 25.2 on this VM) provides deterministic, conformant ES 2.0
  → pixel-exact golden images become CI-viable once the real backend runs
  here.
- akk-stack eqemu server (/mnt/projects/akk-stack, docker) may be
  started/stopped and its DB modified; server CODE must not be modified;
  lifecycle operations use eqemu GM commands where possible.

| Unit | Scope | Status | Slot | Commit |
|---|---|---|---|---|
| H01 | x86 GLES2 build (EGL surfaceless llvmpipe fallback; no X11+GLES2 path exists — finding) | done | slot1 | `9bc8914` |
| H02 | CI gates: golden compare + metric/memory assertions (first golden set rejected by orchestrator review) | done | slot1 | `585d198` |
| H03 | eqemu automation + recording tour (2 real client bugs fixed; credential scrubbed pre-merge; launcher misdiagnosis later corrected) | done | slot2 | `19ba3f7` |
| H02b | Golden re-frame: pose-search cameras, representative scenes; found+fixed ASLR pointer-key draw-order bug in benchmark | done | slot1 | `6bb32ac` |
| H02c | Foliage alpha fix attempt — SUPERSEDED by H02d (use-after-erase made foliage vanish; diagnosis partially correct; WLD flag harvested) | superseded | slot1 | (branch deleted) |
| H02d | Option A alpha-source-aware shader selection + shader-debug diagnostics (Will-approved design; foliage cutouts verified by instrument) | done | slot1 | `d00c8d3` |

**Golden set BLESSED by Will 2026-07-23** (round 2, post-H02d) — the visual
gate is fully armed: any rendering change now requires either byte-identical
frames or a re-blessed golden set through the same review.

**Remaining for Batch H closure:** Pi tier-2 timing script (awaits Pi
power-on + IP); Win10 character-creation session for the two-client tours.
