# S3D/WLD Loading Cache Analysis

Analysis of opportunities to speed up client startup and zone loading by precalculating/caching data structures derived from S3D/WLD files.

**Created:** 2026-01-06

---

## Executive Summary

Since S3D/WLD files are static game assets that don't change, significant loading time can be saved by caching processed data structures to disk. The biggest opportunities are:

1. **Zone Geometry Cache** - 50-200ms savings per zone (10-30x speedup)
2. **Character Model Cache** - 10-100ms savings per model type
3. **Texture metadata** - Minimal savings, low priority

---

## Current Loading Pipeline

### Stage 1: Archive Extraction (pfs.cpp)
**Files:** `<zone>.s3d`, `<zone>_obj.s3d`, `<zone>_chr.s3d`

- Read PFS archive header and directory
- Decompress filename table (zlib)
- Match CRC32 checksums to filenames
- Extract compressed file blocks on demand

**Time:** 10-50ms (I/O bound)

### Stage 2: WLD Fragment Parsing (wld_loader.cpp)

Key fragment types processed:

| Fragment | Purpose | Expensive? |
|----------|---------|------------|
| 0x03/0x04 | Texture definitions, animation timing | No |
| 0x30/0x31 | Material definitions | No |
| 0x36 | Mesh geometry (vertices, triangles, UVs, bone pieces) | No |
| 0x2C | Legacy mesh format | No |
| 0x10/0x11/0x12/0x13 | Skeleton tracks, bone keyframes | No |
| 0x14/0x15 | Object definitions and placements | No |
| 0x21/0x22/0x29 | BSP tree, regions, zone lines | No |

**Time:** 20-50ms (mostly memory allocation)

### Stage 3: Geometry Combination (wld_loader.cpp - getCombinedGeometry)

**This is the first major bottleneck.**

Processing:
- Iterate through all parsed geometries
- Build global texture name map (deduplication)
- Accumulate vertices with mesh center offsets applied
- Remap triangle indices to combined vertex buffer
- Recalculate bounding box

**Time:** 50-200ms
**Complexity:** O(V + T) where V = 100K-500K vertices, T = triangles

### Stage 4: Character Model Loading (s3d_loader.cpp)

**This is the second major bottleneck.**

Processing:
1. Parse character WLD for skeleton tracks
2. For each skeleton:
   - Flatten bone hierarchy
   - Match geometries to bones
   - **Apply Skinning** - transform vertices by bone matrices
   - Build animated skeleton with keyframes

**applySkinning details:**
- Build 4x4 bone matrices from quaternion rotations
- Transform each vertex using bone-local transforms
- Character models: 100-1000 bones, 5K-50K vertices

**Time:** 10-100ms per character model

### Stage 5: Texture Processing (zone_geometry.cpp)

Processing:
- Check if DDS format (most EQ textures)
- Decompress DXT1/DXT3/DXT5
- Convert RGBA to ARGB for Irrlicht
- Upload to GPU

**Time:** 5-50ms per texture, 200ms-2s total if all loaded at once

### Stage 6: Mesh Building (zone_geometry.cpp)

Processing:
- Group triangles by texture
- Split by 16-bit index limit (65535 vertices max)
- Build Irrlicht SMeshBuffer for each group
- Convert coordinates (EQ Z-up to Irrlicht Y-up)

**Time:** 50-200ms total

---

## Bottleneck Summary

| Operation | Time | Why Expensive |
|-----------|------|---------------|
| **Geometry Combination** | 50-200ms | High memory throughput, cache misses |
| **Character Skinning** | 10-100ms/model | Matrix multiplications per vertex |
| **DDS Decompression** | 5-50ms/texture | Full decompression to RGBA |
| **Mesh Building** | 50-200ms | Memory copies, coordinate conversion |
| **Archive Reading** | 10-50ms | Zlib inflation |

---

## Recommended Caching Strategy

### Priority 1: Zone Geometry Cache (CRITICAL)

**What to cache:** Combined geometry after `getCombinedGeometry()`

```cpp
struct CachedZoneGeometry {
    // Header
    uint32_t magic;           // "WEQC"
    uint32_t version;
    uint64_t sourceHash;      // Hash of source .s3d for invalidation

    // Geometry
    uint32_t vertexCount;
    uint32_t triangleCount;
    std::vector<Vertex3D> vertices;      // Pre-offset, combined
    std::vector<Triangle> triangles;     // Remapped indices

    // Textures
    std::vector<std::string> textureNames;
    std::vector<bool> textureInvisible;
    std::vector<TextureAnimationInfo> textureAnimations;

    // Bounds
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
};
```

**Savings:** 50-200ms per zone load
**Cache location:** `~/.config/willeq/cache/zones/<zone>.geom`
**Invalidation:** Compare hash of source .s3d file

### Priority 2: Character Model Cache (HIGH)

**What to cache:** Skinned models + animation data

```cpp
struct CachedCharacterModel {
    uint32_t magic;
    uint32_t version;
    uint64_t sourceHash;

    std::string modelCode;    // "hum", "elf", etc.

    // Skinned geometry
    std::vector<Vertex3D> skinnedVertices;
    std::vector<Triangle> triangles;
    std::vector<std::string> textureNames;

    // For runtime animation
    std::vector<VertexPiece> vertexPieces;
    std::vector<Vertex3D> rawVertices;    // Pre-skinning

    // Skeleton
    std::vector<CachedBone> bones;
    std::map<std::string, CachedAnimation> animations;
};

struct CachedAnimation {
    std::string code;         // "l01", "c01", etc.
    int frameCount;
    int durationMs;
    std::vector<BoneKeyframe> keyframes;
};
```

**Savings:** 10-100ms per character type
**Cache location:** `~/.config/willeq/cache/models/<race>_<gender>.char`
**Invalidation:** Compare hash of source _chr.s3d file

### Priority 3: Texture Metadata (LOW)

**What to cache:** Only metadata, NOT pixel data

```cpp
struct CachedTextureMetadata {
    std::string name;
    uint32_t width, height;
    bool isAnimated;
    int animationDelayMs;
    std::vector<std::string> frameNames;
};
```

**Savings:** 1-5ms (minimal)
**Recommendation:** Keep compressed data in PFS archives - more space efficient

### NOT Worth Caching

- **BSP Trees** - <10ms to parse, complex to serialize
- **Object Geometries** - Only 5-20ms savings
- **Texture Pixel Data** - Archives have better compression

---

## Estimated Performance Impact

### Zone Load Times

| Scenario | Without Cache | With Cache | Speedup |
|----------|--------------|------------|---------|
| Small zone (tutorial) | 110ms | 15ms | 7x |
| Medium zone (city) | 300ms | 30ms | 10x |
| Large zone (outdoor) | 720ms | 55ms | 13x |

### Character Model Load Times

| Scenario | Without Cache | With Cache | Speedup |
|----------|--------------|------------|---------|
| Single model | 50ms | 5ms | 10x |
| Zone with 20 NPCs | 500ms | 30ms | 17x |

---

## Implementation Plan

### Phase 1: Zone Geometry Cache

1. **Add cache infrastructure**
   - Create `CacheManager` class
   - Implement binary serialization for `ZoneGeometry`
   - Add file hash computation for .s3d files

2. **Modify S3DLoader**
   - Check for valid cache before parsing WLD
   - Load from cache if valid
   - Write cache after first successful load

3. **Cache invalidation**
   - Store hash of source .s3d in cache header
   - Compare on load, rebuild if mismatch

### Phase 2: Character Model Cache

1. **Add serialization for CharacterModel**
   - Include skeleton structure
   - Include animation keyframes
   - Include vertex piece assignments

2. **Modify RaceModelLoader**
   - Check cache before loading from _chr.s3d
   - Write cache after first load

### Phase 3: Cache Management

1. **Cache directory structure**
   ```
   ~/.config/willeq/cache/
   ├── zones/
   │   ├── qeynos2.geom
   │   ├── freporte.geom
   │   └── ...
   └── models/
       ├── human_male.char
       ├── elf_female.char
       └── ...
   ```

2. **Cache versioning**
   - Increment version when format changes
   - Auto-invalidate old cache versions

3. **Cache size management** (optional)
   - Track total cache size
   - LRU eviction if over limit

---

## File Format Specification

### Zone Geometry Cache (.geom)

```
Offset  Size    Field
------  ----    -----
0x00    4       Magic ("WEQC")
0x04    4       Version (uint32)
0x08    8       Source S3D hash (uint64)
0x10    4       Zone name length (uint32)
0x14    N       Zone name (char[N])
...     4       Vertex count (uint32)
...     4       Triangle count (uint32)
...     4       Texture count (uint32)
...     V*32    Vertices (x,y,z,nx,ny,nz,u,v as floats)
...     T*16    Triangles (v1,v2,v3,texIdx as uint32)
...     ...     Texture names (length-prefixed strings)
...     Tc      Texture invisible flags (bool[Tc])
...     ...     Texture animations (variable)
...     24      Bounding box (6 floats)
```

### Character Model Cache (.char)

```
Offset  Size    Field
------  ----    -----
0x00    4       Magic ("WEQM")
0x04    4       Version (uint32)
0x08    8       Source S3D hash (uint64)
0x10    4       Model code length
0x14    N       Model code (e.g., "hum")
...     4       Vertex count
...     4       Triangle count
...     4       Bone count
...     4       Animation count
...     V*32    Skinned vertices
...     V*32    Raw vertices (for animation)
...     T*16    Triangles
...     ...     Vertex pieces
...     ...     Bone hierarchy
...     ...     Animation data
```

---

## Notes

- Cache files should be platform-independent (use fixed-size types, handle endianness)
- Consider compression (LZ4) for large caches if disk space is a concern
- First-run experience will be slow (building cache), subsequent runs fast
- Could add progress indication during cache building

---

## References

- `src/client/graphics/eq/s3d_loader.cpp` - S3D archive loading
- `src/client/graphics/eq/wld_loader.cpp` - WLD fragment parsing
- `src/client/graphics/eq/pfs.cpp` - PFS archive format
- `src/client/graphics/eq/race_model_loader.cpp` - Character model loading
- `src/client/graphics/eq/zone_geometry.cpp` - Mesh building
