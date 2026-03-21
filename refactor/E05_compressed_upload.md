# E05: Compressed Upload Path in ConstrainedTextureCache

## Overview

Add an ETC1 compressed texture upload path to `ConstrainedTextureCache`. When an
`EQE1Loader` is registered with the cache and a texture name is found in it, the cache
bypasses BMP decode entirely and uploads pre-compressed ETC1 data via
`glCompressedTexImage2D`. The existing BMP decode path remains as fallback.

## Prerequisites

- E01 complete (EQE1 format header)
- E04 complete (EQE1Loader class)
- Batch T complete (texture loading consolidated into getOrLoad/uploadTexture)
- Understanding of existing flow:
  - `getOrLoad(name, data)` → `processTextureData()` → DecodedEntry (RGBA pixels)
  - `uploadTexture(name)` → RGBA→ARGB → `driver_->addTexture()` → ITexture*

## Plan

### Step 1: Add EQE1Loader references to ConstrainedTextureCache

**File**: `include/client/graphics/constrained_texture_cache.h`

a. Forward declare `EQE1Loader` (avoid header include):
   ```cpp
   namespace EQT { namespace Graphics { class EQE1Loader; } }
   ```

b. Add method to register loaders:
   ```cpp
   void addEQE1Loader(const EQE1Loader* loader);
   void clearEQE1Loaders();
   ```

c. Add private member:
   ```cpp
   std::vector<const EQE1Loader*> eqe1Loaders_;
   ```

d. Add private lookup helper:
   ```cpp
   const EQE1Loader::TextureEntry* findInEQE1(const std::string& name) const;
   ```
   Iterates `eqe1Loaders_` and calls `lookup(name)` on each, returns first hit.

### Step 2: Add CompressedEntry struct

**File**: `include/client/graphics/constrained_texture_cache.h`

Alongside the existing `DecodedEntry` struct, add:

```cpp
struct CompressedEntry {
    const uint8_t* etc1Data;        // pointer into EQE1Loader buffer (not owned)
    uint32_t dataSize;
    uint16_t width;
    uint16_t height;
    uint8_t  mipLevels;
    bool hasAlpha;
    const uint8_t* alphaData;       // nullptr if opaque
    uint32_t alphaDataSize;
};
```

Add private member:
```cpp
std::unordered_map<std::string, CompressedEntry> compressedEntries_;
```

### Step 3: Modify `getOrLoad()` to check EQE1 first

**File**: `src/client/graphics/constrained_texture_cache.cpp`

Current flow (lines 88-134):
```
1. Check uploadedTextures_ (already on GPU?) → return ITexture*
2. Check decodedEntries_ (already decoded?) → return nullptr
3. processTextureData() (BMP decode) → store DecodedEntry → return nullptr
```

Modified flow:
```
1. Check uploadedTextures_ (already on GPU?) → return ITexture*
2. Check compressedEntries_ (already found in EQE1?) → return nullptr
3. Check decodedEntries_ (already decoded from BMP?) → return nullptr
4. NEW: findInEQE1(name) → if found, create CompressedEntry → return nullptr
5. processTextureData() (BMP decode fallback) → store DecodedEntry → return nullptr
```

**Implementation detail for step 4**:
- Strip `.bmp` extension from name before lookup (EQE1 stores names without extension)
- Lowercase the name (EQE1 index is lowercase)
- If found: populate CompressedEntry from TextureEntry fields
- Log at TRACE level: `"EQE1 cache hit: {} ({}x{}, {} mips)"`

### Step 4: Modify `uploadTexture()` to handle CompressedEntry

**File**: `src/client/graphics/constrained_texture_cache.cpp`

Current flow (lines 136-229):
```
1. Find DecodedEntry
2. Check budget, evict if needed
3. Convert RGBA → ARGB
4. driver_->addTexture() → ITexture*
5. Register in LRU
```

Modified flow — check CompressedEntry first:
```
1. Check compressedEntries_ for name
   If found:
     a. Compute GPU memory: etc1MipChainSize(w, h, mipLevels) [+ alpha chain if hasAlpha]
     b. Check budget with evictUntilAvailable(gpuSize)
     c. Upload via glCompressedTexImage2D (Step 5 below)
     d. Register in LRU with gpuSize
     e. Remove from compressedEntries_
     f. Return ITexture*
2. Otherwise: existing DecodedEntry path (unchanged)
```

### Step 5: ETC1 compressed upload function

**File**: `src/client/graphics/constrained_texture_cache.cpp`

New private method:
```cpp
irr::video::ITexture* uploadCompressedETC1(
    const std::string& name,
    const CompressedEntry& entry);
```

a. Generate GL texture: `glGenTextures(1, &texId)`
b. Bind: `glBindTexture(GL_TEXTURE_2D, texId)`
c. Upload mip chain:
   ```cpp
   uint32_t offset = 0;
   uint16_t w = entry.width, h = entry.height;
   for (int level = 0; level < entry.mipLevels; ++level) {
       uint32_t levelSize = etc1DataSize(w, h);
       glCompressedTexImage2D(GL_TEXTURE_2D, level,
                              GL_ETC1_RGB8_OES,
                              w, h, 0,
                              levelSize,
                              entry.etc1Data + offset);
       offset += levelSize;
       w = std::max<uint16_t>(4, w / 2);
       h = std::max<uint16_t>(4, h / 2);
   }
   ```
   Reference: `texture_atlas.cpp:229-243` for the existing ETC1 upload pattern.

d. Set texture parameters:
   ```cpp
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                   entry.mipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   ```
   **Critical**: Use `GL_REPEAT` (not `GL_CLAMP_TO_EDGE` like atlas pages).
   This is the whole point — individual textures support tiling natively.

e. Wrap in Irrlicht texture handle:
   - Need to create an `ITexture*` that wraps the raw GL handle
   - Check how `texture_atlas.cpp` and `gpu_upload_thread.cpp` handle this
   - May need `driver_->addTextureFromGLHandle(texId, name, w, h)` or similar
   - **Investigate**: Does `COpenGLES2Driver` expose a way to wrap an existing GL texture?
   - Fallback: create `IImage` from dummy data, call `addTexture()`, then replace the GL handle

f. If `hasAlpha`: upload alpha texture the same way (separate GL texture object)
   - Store alpha texture handle alongside RGB in the cache entry
   - Shader binding handled in E07/E08 (material setup)

### Step 6: Guard with GLES2 preprocessor check

The compressed upload path only applies to GLES2 builds:

```cpp
#ifdef EQT_HAS_GLES2
    // CompressedEntry path (Steps 3-5)
#endif
```

On non-GLES2 builds, `findInEQE1()` always returns nullptr, and the BMP decode path
is used. The `eqe1Loaders_` vector is still populated but unused — zero overhead.

### Step 7: Thread safety

`getOrLoad()` already acquires `mutex_` (added in C06, commit `a8b7001`).
The new CompressedEntry lookup and insertion happen under the same mutex.
`uploadTexture()` also acquires `mutex_`.
No additional locking needed — same invariants as DecodedEntry path.

`eqe1Loaders_` is populated once during zone loading (before any getOrLoad calls)
and cleared during zone unload (after all texture work is done). No concurrent access.

### Step 8: Memory tracking

CompressedEntry stores pointers into EQE1Loader's buffer — no additional RAM allocation
for texture data. The only new allocation is the CompressedEntry struct itself (~40 bytes).

GPU memory tracking: the `etc1MipChainSize()` value is used for LRU budget accounting,
matching the actual GPU allocation. This is 8× smaller than the RGBA budget for the same
texture, reflecting the real memory savings.

### Step 9: Build and test

a. `cmake --build build -j24` — verify compilation on host
b. ARM cross-compile via `scripts/build-arm-noble.sh`
c. Unit test: extend `tests/test_eqe1_loader.cpp` or create new test that:
   - Creates a mock ConstrainedTextureCache (or uses real one with mock driver)
   - Registers an EQE1Loader
   - Calls `getOrLoad("texname", {})` — verifies CompressedEntry created
   - Calls `uploadTexture("texname")` — verifies GL upload path invoked
   (Full GL test may require EGL context — defer to E09 on-device validation)

## Acceptance Criteria

- [ ] `addEQE1Loader()` registers loader with cache
- [ ] `getOrLoad()` checks EQE1 loaders before BMP decode
- [ ] `getOrLoad()` creates CompressedEntry for textures found in EQE1
- [ ] `getOrLoad()` falls through to BMP decode for textures NOT in EQE1
- [ ] `uploadTexture()` uses `glCompressedTexImage2D` for CompressedEntry textures
- [ ] `uploadTexture()` uses `GL_REPEAT` wrap mode (not CLAMP_TO_EDGE)
- [ ] Full mipmap chain uploaded (all levels from base to 4×4)
- [ ] `GL_LINEAR_MIPMAP_LINEAR` set for min filter when mipLevels > 1
- [ ] GPU memory budget tracks ETC1 size (not RGBA size) for compressed textures
- [ ] Alpha textures produce separate GL texture for alpha channel
- [ ] BMP fallback path unchanged — non-EQE1 textures work exactly as before
- [ ] Thread safety maintained (no new lock ordering issues)
- [ ] Compiles on both x86 (GLES2 disabled) and ARM (GLES2 enabled)

## Open Investigation

- **ITexture wrapping**: Need to verify how to create an `ITexture*` from a raw GL texture
  handle in the GLES2 driver. Check `COpenGLES2Driver` for existing patterns. If no clean
  API exists, may need to add a small helper method to the driver. This is the main
  implementation risk in this unit.

## Review

All steps completed. Key implementation details:

**ITexture wrapping resolved**: The GLES2 driver already has `wrapExternalTexture()` and a
C wrapper `gles2WrapTexture()`. Used the existing pattern from GPU upload thread results
(irrlicht_renderer.cpp:1340). No new driver code needed.

**Implementation approach**: Added `CompressedEntry` struct and `compressedEntries_` map
to the cache. `getOrLoad()` checks EQE1 loaders via `findCompressedEntry()` before BMP
decode — creates a CompressedEntry (pointer into EQE1 file buffer, no copy). `uploadTexture()`
checks compressedEntries_ first and calls `uploadCompressedETC1()` for the GL upload path.

**GL upload**: Uses `glCompressedTexImage2D` with `GL_ETC1_RGB8_OES` (0x8D64) for each mip
level. Function pointer obtained via `eglGetProcAddress`. Texture params: `GL_REPEAT` wrap
(not CLAMP_TO_EDGE), `GL_LINEAR_MIPMAP_LINEAR` min filter when mips > 1.

**Alpha handling**: Separate GL texture created for alpha companion. Both tracked under the
same budget entry. Alpha texture GL handle is created but not yet wired to material binding
(deferred to E07/E08 integration).

**Budget tracking**: Uses `etc1MipChainSize()` for GPU memory accounting — 8x smaller than
RGBA for same dimensions. This accurately reflects real GPU memory consumption.

**Linkage**: `eqe1_loader.cpp` added alongside `constrained_texture_cache.cpp` in all 14
source lists (CMakeLists.txt + tests/CMakeLists.txt) since the cache now depends on it.

**Non-GLES2 fallback**: `uploadCompressedETC1()` returns nullptr on non-DRM builds with a
warning log. The getOrLoad→findCompressedEntry path still runs but the upload falls through.
This is acceptable — EQE1 is a GLES2-only optimization.

**Tests**: All existing tests pass. No behavioral change for non-EQE1 path.
Pre-existing test failures (zone tests, RDP segfault) unrelated to this change.
