# P50 — Close the two remaining applyPaletteMask plumbing gaps (P46d audit)

**Slot: slot3. x86 ONLY — no Pi (P48 owns it), no eqemu. Authorized
by Will 2026-07-31 (item 3).**

## Background

P46d's production ordering audit (refactor/P46d_cache_key_fix.md
Review, Part B) found two pre-existing paths that never thread
applyPaletteMask — masked textures loaded through them decode opaque:
1. Composite-deferred on-demand loads: mesh_building.cpp's
   buildMeshFromGeometry() resolves textures via its own
   getOrLoadTexture()/loadTextureFromBMP() fallback; the
   isMaskedMaterial flag it computes is used only for MaterialType,
   never passed to the texture load. Live-client exposure: any
   composite-eligible masked race whose per-part decode was deferred
   (P13b) and later needed multi-buffer on demand.
2. Equipment texture-variant overrides:
   animated_mesh_creation.cpp's per-slot swap (materialId != 0)
   calls loadTextureFromBMP() with no mask flag — a masked base body
   texture with a non-default equipped variant loses its mask on
   that slot.

## Work

1. Thread applyPaletteMask through both call sites (P46d's
   established pattern: the flag exists at getOrLoad/uploadTexture;
   compute isMaskedMaterial BEFORE the texture-load block where
   needed). Respect the (name, mask) cache keying — no new keying
   schemes.
2. Evidence per gap:
   - Gap 1: construct the on-demand scenario (composite-eligible
     masked race + a consumer forcing the multi-buffer path — the
     P13b deferral machinery; benchmark or test harness) and show
     the loaded texture now carries mask alpha (hasAlpha/log or the
     P47 viewer if reachable).
   - Gap 2: masked race + equipped variant swap — show the swapped
     slot's texture carries alpha (the P44b bear finding means no
     viewer CLI hook for variants; a targeted unit test or logged
     hash evidence is acceptable — say which and why).
3. Unmasked/variant-0 behavior byte-identical (md5/hash evidence).

## Rules

- Files expected: src/client/graphics/eq/mesh_building.cpp,
  src/client/graphics/eq/animated_mesh_creation.cpp (+ tests). Do
  NOT touch composite builder/eligibility (slot1 territory) or DRM
  device (slot2). If the fix genuinely requires touching slot1's
  files, STOP and report instead.
- Full bar: GLES2 ctest baseline, visual gate PASS, ARM clean, file
  sizes. Separate commits per gap.

## Review

(executor fills in)
