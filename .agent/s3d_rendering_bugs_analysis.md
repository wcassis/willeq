# S3D/WLD Rendering Bugs Analysis

This document compares WillEQ's S3D/WLD processing against the working eqsage implementation to identify rendering issues.

## Observed Problems

1. **Piles of dirt too large/tall** - Scale issue
2. **Doors, chairs, torches in wrong locations** - Position/rotation issue
3. **Terrain geometry incorrect** - Coordinate transformation or normal issue

## Progress

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 1 | **COMPLETE** | Fix Fragment 0x15 Parsing |
| Phase 2 | **COMPLETE** | Fix Normal Parsing |
| Phase 3 | **COMPLETE** | Investigate UV/Coordinate Issues |

---

## Phase 1: Fragment 0x15 Parsing - COMPLETED

### Changes Made

1. **Added flag constants** in `wld_loader.h:235-244`:
   ```cpp
   namespace Fragment15Flags {
       constexpr uint32_t HasCurrentAction = 0x01;
       constexpr uint32_t HasLocation = 0x02;
       constexpr uint32_t HasBoundingRadius = 0x04;
       constexpr uint32_t HasScaleFactor = 0x08;
       constexpr uint32_t HasSound = 0x10;
       constexpr uint32_t Active = 0x20;
       constexpr uint32_t SpriteVolumeOnly = 0x80;
       constexpr uint32_t HasVertexColorReference = 0x100;
   }
   ```

2. **Rewrote `parseFragment15`** (`wld_loader.cpp:694-817`) with proper flag-based parsing:
   - Reads fields sequentially based on flag bits
   - Properly handles variable-length fragment structure
   - Scale is now a single uniform float (defaults to 1.0 if not present)
   - Removed direct struct casting that was accessing invalid memory

3. **Fixed rotation axis transformation**:
   ```cpp
   // EQ rotation values are in 512ths of a circle
   const float rotModifier = 360.0f / 512.0f;
   float finalRotX = 0.0f;                      // Always 0 per eqsage
   float finalRotY = -rawRotX * rotModifier;   // Negated
   float finalRotZ = rawRotY * rotModifier;    // Swapped from Y
   ```

4. **Placeable class unchanged** - Uses existing 3-value scale interface but now receives uniform scale for all axes.

### Bugs Fixed

- **Scale garbage value bug**: Was reading `header->scaleZ` which didn't exist in the struct, causing random Z-scale values. Now correctly reads a single uniform scale factor.
- **Rotation axis bug**: Was using raw rotation values without axis swap/negation. Now applies proper transformation: server rotX -> client rotY (negated), server rotY -> client rotZ.
- **Fixed struct parsing bug**: Was using fixed struct layout on a variable-length fragment. Now uses proper flag-based sequential parsing.

---

## Phase 2: Normal Parsing - COMPLETED

### Changes Made

1. **Changed `WldNormal` struct** in `wld_loader.h:207-211` from unsigned to signed:
   ```cpp
   struct WldNormal {
       int8_t x;   // Signed: range -128 to 127, divide by 128.0 for [-1, 1]
       int8_t y;
       int8_t z;
   };
   ```

2. **Updated normal conversion** in `wld_loader.cpp:397-418` (Fragment 0x36 mesh parsing):
   - Changed divisor from 127.0 to 128.0 (`recip128 = 1.0f / 128.0f`)
   - Added normalization after conversion
   - Added fallback to up vector (0, 0, 1) for degenerate normals

3. **Updated legacy mesh parsing** in `wld_loader.cpp:897-918` (Fragment 0x2C):
   - Added normalization to float normals for consistency
   - Added same degenerate normal handling

### Bugs Fixed

- **Wrong data type**: Changed from `uint8_t` (0-255) to `int8_t` (-128 to 127), so normals now correctly represent negative directions.
- **Wrong divisor**: Changed from 127.0 to 128.0 to match eqsage and produce proper [-1, 1] range.
- **Missing normalization**: Added vector normalization to ensure unit normals for correct lighting.

---

## Phase 3: UV/Coordinate Investigation - COMPLETED

### Analysis Summary

After detailed analysis comparing eqsage and WillEQ, I determined that **no UV or coordinate changes are needed** for zone/object geometry. Here's why:

### Key Finding: eqsage Transformations are for Format Conversion

eqsage exports to **glTF format**, which has different conventions than both EQ and Irrlicht:

| Aspect | EQ/DirectX/Irrlicht | glTF/OpenGL |
|--------|---------------------|-------------|
| Coordinate System | Left-handed | Right-handed |
| UV Origin | Top-left | Bottom-left |
| V Direction | Increases downward | Increases upward |

### eqsage Transformations Explained

**For Zone Geometry:**
```javascript
// Position: X negated
vecs.push(-1 * (v[0] + mesh.center[0]), v[2] + mesh.center[2], v[1] + mesh.center[1]);
// UV: V negated
uv.push(v[0], -1 * v[1]);
// Plus flip matrix at node level: mat4.scale(flipMatrix, flipMatrix, [-1, 1, 1])
```

**For Object Geometry:**
```javascript
// Position: X NOT negated (no flip matrix either)
vecs.push(v[0] + mesh.center[0], v[2] + mesh.center[2], v[1] + mesh.center[1]);
// UV: V NOT negated
sharedPrimitive.uv.push(...uvs[k]);
```

### Why WillEQ Doesn't Need These Changes

1. **X-axis negation**: Not needed because Irrlicht uses left-handed coordinates, same as EQ. The X negation in eqsage is specifically to convert to glTF's right-handed system.

2. **V-coordinate negation**: Not needed because Irrlicht uses DirectX-style UV conventions (origin at top-left), same as EQ. The V negation in eqsage is specifically to convert to glTF/OpenGL conventions (origin at bottom-left).

3. **Character model flipV**: Already correctly implemented in WillEQ. Character models in EQ files apparently use a different UV convention than zone/object geometry, which is why `flipV=true` is used for character models but not for zones/objects.

### Changes Made

1. **Added documentation** to `zone_geometry.cpp` explaining the UV and coordinate system decisions
2. **No functional changes** to UV or coordinate handling - current implementation is correct

### Documentation Added

```cpp
// ===========================================================================
// UV and Coordinate System Notes
// ===========================================================================
// EQ uses a left-handed coordinate system (Z-up), same as DirectX/Irrlicht.
// UV coordinates in EQ follow DirectX convention (origin at top-left, V increases downward).
//
// When comparing to eqsage (which exports to glTF):
// - eqsage negates V coordinates because glTF uses OpenGL convention (origin at bottom-left)
// - eqsage negates X positions because glTF uses right-handed coordinates
// - These transformations are for FORMAT CONVERSION, not bug fixes
//
// For Irrlicht rendering, we do NOT need these transformations because:
// - Irrlicht uses the same UV convention as EQ (DirectX-style, origin top-left)
// - Irrlicht uses the same coordinate handedness as EQ (left-handed)
//
// Character models use flipV because the character model UV data in EQ files
// is stored with a different convention than zone/object geometry.
// ===========================================================================
```

---

## Summary of All Changes

### Files Modified

| File | Changes | Phase |
|------|---------|-------|
| `include/client/graphics/eq/wld_loader.h` | Added Fragment15Flags namespace, changed WldNormal to int8_t | 1, 2 |
| `src/client/graphics/eq/wld_loader.cpp` | Rewrote parseFragment15 with flag-based parsing, fixed normal parsing | 1, 2 |
| `src/client/graphics/eq/zone_geometry.cpp` | Added documentation explaining UV/coordinate decisions | 3 |

### Bugs Fixed

1. **Fragment 0x15 scale bug** (Phase 1): Was reading garbage memory for scaleZ
2. **Fragment 0x15 rotation bug** (Phase 1): Missing axis swap and negation
3. **Fragment 0x15 struct parsing bug** (Phase 1): Using fixed struct on variable-length data
4. **Normal data type bug** (Phase 2): Using uint8_t instead of int8_t
5. **Normal divisor bug** (Phase 2): Dividing by 127 instead of 128
6. **Normal normalization bug** (Phase 2): Missing vector normalization
7. **Fragment 0x15 nameRef parameter bug** (Regression fix): parseFragment15 was trying to read nameRef from the buffer, but the WLD parser already extracts nameRef before calling fragment parsers. Fixed by adding nameRef as a parameter to parseFragment15.
8. **Fragment 0x15 name source bug** (Regression fix): Was checking if nameRef < 0 to validate placeables, but Fragment 0x15 typically has nameRef == 0. The actual name comes from objectRef (first field in fragment body), which is a negative string hash reference pointing to the _ACTORDEF name. Fixed by using objectRef instead of nameRef to get the placeable name and validate the fragment.
9. **Object definition name mismatch bug** (Regression fix): parseFragment14 was keeping the `_ACTORDEF` suffix in object definition names (e.g., "TORCH_ACTORDEF"), but parseFragment15 was stripping it from placeable names (e.g., "TORCH"). This caused the matching in s3d_loader.cpp to fail since `objectDefs.find("TORCH")` couldn't find "TORCH_ACTORDEF". Fixed by also stripping `_ACTORDEF` suffix in parseFragment14.
10. **Rotation axis mapping bug**: Was putting primary yaw rotation (rawRotX) into placeable rotY, which the renderer maps to Irrlicht Z (roll). This caused objects like trees to be rolled sideways instead of rotated horizontally. Fixed by putting rawRotX into placeable rotZ (which the renderer maps to Irrlicht Y/yaw) and rawRotY into placeable rotY.

### Confirmed Correct (No Changes Needed)

1. **UV V-flip for zone/object geometry**: Not needed (same UV convention as Irrlicht)
2. **X-axis negation**: Not needed (same coordinate handedness as Irrlicht)
3. **Character model flipV**: Already correctly implemented

---

## Testing Recommendations

The fixes in Phase 1 and Phase 2 should resolve the observed issues:

1. **Dirt piles too large/tall** → Fixed by Phase 1 (correct uniform scale)
2. **Objects in wrong locations** → Fixed by Phase 1 (correct rotation transformation)
3. **Terrain geometry incorrect** → Fixed by Phase 2 (correct normals for lighting)

Visual testing with commonlands and other zones should confirm these fixes are working correctly.
