# B1050 hotfix: lost DRM page-flip event

`docker/irrlicht-drm/drm_flip_tracker.h` owns the pending, retired, and
current front-buffer state shared by the ARM and x86 device forks. An event
releases the retired buffer only after completion. On three consecutive missed
events, `drmPageFlip()` synchronously sets the CRTC to the current front
buffer, then releases the retired buffer and resumes normal swapping. A failed
SetCrtc retains both buffers and retries. Flip serials prevent a late event
from completing a newer flip after recovery.

The Linux-only `DRMFlipTracker` tests cover normal completion, synchronous
fallback, repeated lost-event recovery, stale events, and no double release.
Both device sources were syntax-checked locally; the device path itself needs
the ARM build job and hardware verification.

## Not verified / left open

- Will's XU4 session: tearing, partial UI windows, and recovery after a real
  lost page-flip event remain device-only checks. An internal frame dump
  cannot show a scanout artifact.
