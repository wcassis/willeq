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
| H01 | x86 GLES2 build: patched Irrlicht 1.8.5 built locally for x86_64 with EGL/X11 winsys; willeq + gles2_zone_benchmark running the real GLES2 driver under Xvfb/llvmpipe | | | |
| H02 | CI gates (needs H01): golden-frame capture + pixel-exact compare, draw/state/CPU-ms metric assertions, memory-ceiling assertions; contact sheet for Will's blessing | | | |
| H03 | eqemu automation: akk-stack up/down/health/seed scripts (GM-command-based lifecycle) + scripted headless recording tour producing data/captures/auto_session.sleq, arming the replay oracle golden | | | |
