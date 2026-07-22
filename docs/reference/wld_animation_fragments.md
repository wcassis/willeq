> **HISTORICAL SNAPSHOT** — point-in-time document from a completed batch or past audit; findings inside may be superseded and are NOT kept current. Current truth lives in `docs/decisions.md`, `docs/module_map.md`, and `refactor/deferred.md`.

# WLD Animation Fragment Analysis

Comparison of animation-related WLD fragment parsing between zone-utilities (eqemu reference) and WillEQ.

## Fragment Types (Reference List from zone-utilities)

| Fragment | Name | Purpose | zone-utilities | WillEQ |
|----------|------|---------|----------------|--------|
| 0x10 | Track Set / Skeleton Hierarchy | Bone hierarchy + references to bone tracks and meshes | Parsed | Parsed |
| 0x11 | Track Set Reference | Reference to 0x10 | Parsed | Parsed |
| 0x12 | Mob Track / Track Def | Per-bone animation keyframes (rotation + translation per frame) | Parsed (1 frame only) | Parsed (N frames) |
| 0x13 | Mob Track Reference | Reference to 0x12 | Parsed | Parsed |
| 0x17 | Polygon Animation | Unknown — possibly per-polygon animation data | **NOT PARSED** | **NOT PARSED** |
| 0x18 | Polygon Animation Reference | Reference to 0x17 | **NOT PARSED** | **NOT PARSED** |
| 0x2F | Mesh Animated Vertices Reference | Reference to 0x37 vertex animation data | Not parsed | Recognized in type table |
| 0x37 | Mesh Animated Vertices (DMTRACKDEF) | Pre-computed vertex position frames (flags, banners) | Not parsed | Parsed (zone objects only) |

## Fragment 0x10 — Track Set / Skeleton Hierarchy

Defines the bone hierarchy for a character model.

### zone-utilities struct (`wld_structs.h`)
```c
struct wld_fragment10 {
    uint32_t flag;
    uint32_t track_ref_count;
    uint32_t polygon_anim_frag;   // Reference to 0x17 polygon animation — UNUSED
};

struct wld_fragment10_track_ref_entry {
    int32_t name_ref;
    uint32_t flag;
    int32_t frag_ref;       // Reference to 0x13 (bone orientation track)
    int32_t frag_ref2;      // Reference to 0x2D (mesh reference for this bone's geometry)
    uint32_t tree_piece_count;  // Number of child bone indices following this entry
};
```

### WillEQ struct (`wld_loader.h`)
```c
struct WldFragment10Header {
    uint32_t flags;
    uint32_t trackRefCount;
    uint32_t polygonAnimFrag;    // Reference to 0x17 polygon animation — UNUSED
};

struct WldFragment10BoneEntry {
    int32_t nameRef;
    uint32_t flags;
    int32_t orientationRef;      // Same as zone-utilities frag_ref
    int32_t modelRef;            // Same as zone-utilities frag_ref2
    uint32_t childCount;         // Same as zone-utilities tree_piece_count
};
```

### Differences
- **Struct layout**: Identical binary layout, different field names.
- **`polygon_anim_frag`**: Both codebases read this field but neither uses it. This references a Fragment 0x17 (Polygon Animation) which is never parsed by either codebase.
- **Flag handling**: Both handle flags 1 (skip 3 int32s) and 2 (skip 1 float). zone-utilities also handles flag 512 (additional mesh references). WillEQ does not handle flag 512.

## Fragment 0x12 — Mob Track / Track Def

Contains animation keyframe data for a single bone.

### zone-utilities struct (`wld_structs.h`)
```c
struct wld_fragment12 {
    uint32_t flag;
    uint32_t size;            // Interpreted as header field
    int16_t rot_denom;        // Quaternion W
    int16_t rot_x_num;        // Quaternion X
    int16_t rot_y_num;        // Quaternion Y
    int16_t rot_z_num;        // Quaternion Z
    int16_t shift_x_num;      // Translation X
    int16_t shift_y_num;      // Translation Y
    int16_t shift_z_num;      // Translation Z
    int16_t shift_denom;      // Scale
};
```

### zone-utilities parsing (`wld_fragment.cpp` line 165-179)
Reads **ONE** orientation from the header struct and stores it. The `size` field is not used as a frame count.

### WillEQ parsing (`wld_loader.cpp` line 1657-1736)
Reads the first 4 bytes as `flags`, the next 4 bytes as `frameCount`, then reads `frameCount` frames of 8 x int16 (16 bytes each):
```
For each frame:
    int16_t rotDenom (quaternion W)
    int16_t rotX, rotY, rotZ (quaternion X, Y, Z)
    int16_t shiftX, shiftY, shiftZ (translation)
    int16_t shiftDenom (scale)
```

### CRITICAL DIFFERENCE
**zone-utilities reads 1 frame per Fragment 0x12. WillEQ reads N frames per Fragment 0x12.**

zone-utilities interprets the second uint32 as `size` (unused). WillEQ interprets it as `frameCount` and reads that many 16-byte keyframes. This means:

- In zone-utilities, each bone has ONE orientation (a static pose)
- In WillEQ, each bone has N keyframes (an animation timeline)

The high elf female (`globalhif_chr.s3d`) has 109 Fragment 0x12 entries. If each contains multiple frames, the total animation data could be substantial.

**Question**: Is WillEQ's multi-frame interpretation correct, or is it reading past the fragment boundary? The `size` field in zone-utilities' struct could be the frame count that zone-utilities ignores. This needs verification by checking actual frame counts in the data.

## Fragment 0x17 / 0x18 — Polygon Hierarchy Definition (POLYHDEF)

### Status: DECODED (not animation)

**Previously misidentified as "Polygon Animation".** Full dump of 1515 WLD files across the Titanium client reveals this is a standalone polygon mesh definition — NOT animation data.

Found in 64 files (zone `_obj.s3d` and some `_chr.s3d` files). Named with `_POLYHDEF` suffix (e.g., `SPIKES_POLYHDEF`, `LADDER14_POLYHDEF`, `prepe_POLYHDEF`).

### Structure (Fragment 0x17)
```
uint32_t flags          // Always 1 in practice
uint32_t vertexCount    // Number of float vertices
uint32_t polygonCount   // Number of variable-length polygon faces
float    maxDist        // Bounding radius
float    scale          // Usually 1.0
float    vertices[vertexCount * 3]  // XYZ positions as floats
// Then per polygon:
//   uint32_t indexCount       // 3 = triangle, 4 = quad
//   uint32_t indices[indexCount]  // Vertex indices
```

Example: befallen SPIKES_POLYHDEF = 8 vertices + 6 quads = a box.

Fragment 0x10 has a `polygonAnimFrag` field that references 0x17, but the purpose is collision/interaction geometry, not animation.

### Structure (Fragment 0x18)
Simple reference: `int32_t ref` (fragment index) + `uint32_t flags`.

## Fragment 0x37 — Mesh Animated Vertices (DMTRACKDEF)

### Status: Parsed by WillEQ, NOT found in character S3Ds

WillEQ parses Fragment 0x37 as pre-computed vertex position frames:
```
uint32_t flags
uint16_t vertexCount
uint16_t frameCount
uint16_t delayMs
uint16_t param2
int16_t  scale          // Power of 2
// Then vertexCount * frameCount * 6 bytes:
//   int16_t x, y, z (scaled by 1/(1<<scale))
```

**Confirmed absent from character models**: all `global*_chr.s3d` files contain zero Fragment 0x37 entries. This fragment type is used exclusively for zone object animations (flags, banners, cloth).

## Character Animation: Where It Actually Lives

**Skeletal animation keyframes are in zone-specific and model-specific `_chr.s3d` files, not the global ones.**

Full dump of 1515 WLDs confirms:
- `global*_chr.s3d` files: ALL 0x12 bone tracks have exactly **1 frame** (bind pose only)
- Zone and model `_chr.s3d` files: 0x12 tracks with **up to 241 frames** per bone
- **471 files** contain multi-frame bone tracks

### Loading Model

The EQ client uses a dependency chain to load character animations:

1. **`Resources/GlobalLoad.txt`** — lists S3D archives loaded globally at startup (all `global*_chr.s3d`, equipment files, etc.). These provide skeleton definitions, meshes, textures, and bind poses for PC races and globally-available models.

2. **`<zone>_chr.s3d`** — zone-specific character archive. Contains models and animations for NPCs unique to that zone.

3. **`<zone>_chr.txt`** — dependency file listing additional S3D archives that must be loaded alongside the zone's `_chr.s3d`. Format:
   ```
   <count>
   <model_code>,<s3d_name>
   <model_code>,<s3d_name>
   ...
   ```
   Example (`befallen_chr.txt`):
   ```
   8
   cst,cst_chr
   cpt,cpt_chr
   wil,stonebrunt_chr
   rtn,rtn
   btn,btn
   udk,udk_chr
   mmy,mmy_chr
   mmm,mmm_chr
   ```
   This means befallen loads shared model archives like `cst_chr.s3d` (skeleton model `cst`) that provide animations reused across many zones. Common shared models (`cst`, `rtn`, `btn`) appear in most zones' dependency lists.

4. **Model-specific S3Ds** (e.g., `cst_chr.s3d`, `rtn.s3d`) — contain animation keyframes for a specific model code, loaded by any zone that lists them as a dependency.

The 0x13 (TrackRef) fragment name encodes the animation code (3 chars), model code (3 chars), and bone name, allowing the client to match animation tracks to the correct skeleton across all loaded archives.

## Previously Unknown Fragment Types (discovered via full dump)

| Type | Name | Count | Files | Purpose |
|------|------|-------|-------|---------|
| 0x06 | Sprite2DDef | 11 | gequip only | 2D billboard sprite definition (item sprites) |
| 0x07 | Sprite2DDefRef | 11 | gequip only | Reference to 0x06 |
| 0x26 | ParticleSpriteDef | 1120 | 158 files | Particle sprite base (`_SPB` suffix) |
| 0x27 | ParticleSpriteDefRef | 54 | 1 file | Reference to 0x26 |
| 0x34 | ParticleCloudDef | 1084 | 158 files | Particle emitter params (`_PCD` suffix) |

## Remaining Gaps

1. **Fragment 0x12 frame count discrepancy**: zone-utilities reads 1 frame, WillEQ reads N frames. WillEQ's interpretation is confirmed correct by the dump data — multi-frame tracks have size = 8 + frameCount*16 bytes, with 16 bytes/frame consistently.

2. **Fragment 0x10 flag 512**: zone-utilities handles additional mesh references under this flag. WillEQ does not. Could contain alternative mesh data for LOD or equipment swaps.

3. **Fragment 0x26/0x34 (Particle System)**: Structure partially decoded from hex analysis but not yet fully parsed. These define particle effects (smoke, spell visuals, etc.).
