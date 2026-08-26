# T285 — applyPaletteMask plumbing gaps (P46d, still open at the tip)

Slot: slot1. x86. NO eqemu (offline asset + pixel work). Class A.
Territory: `src/client/graphics/eq/mesh_building.cpp`'s
`loadTextureFromBMP()` fallback and `animated_mesh_creation.cpp`'s
equipment-variant swap (the two sites P46d named), the palette-mask helper
itself, tests. Do NOT touch the composite scan (T284 slot2) or inventory.

## Gap (deferred.md index, P46d)
`applyPaletteMask` is not threaded through two paths: `mesh_building.cpp`'s
`loadTextureFromBMP()` fallback and `animated_mesh_creation.cpp`'s
equipment-variant swap. Both were listed with "0 hits" at T53 and no unit
has touched them since. Verify that is still true at the tip before doing
anything (T215's lesson: the board can be stale).

## Scope
1. Confirm the two sites still bypass the mask, and establish what the mask
   DOES for the paths that do call it (which textures, which palette, what
   the visible difference is) — P21/P37/P46 are the prior art.
2. Thread it through both, matching the existing callers exactly (no second
   implementation — T230/T239 one-body rule).
3. Prove the difference in pixels: a texture that the mask changes, loaded
   through each of the two paths, before and after. If the mask makes NO
   difference on those paths (e.g. they only ever see already-masked
   textures), say so with the evidence and close the item as not-a-defect
   rather than adding dead plumbing.
4. Tests: red/green on the pixel assertion.

## Verification
Full ctest; visual gate (goldens must stay untouched unless the mask
genuinely changes a golden texture — if it does, STOP and report, that is a
re-bless decision); Review; evidence review/T285/. Commit:
`T285: palette mask reaches the BMP fallback and the equipment swap` or the
not-a-defect form.
