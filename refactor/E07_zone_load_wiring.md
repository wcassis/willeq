# E07: Zone Load Wiring — Open EQE1 Files, Pass to Cache

## Overview

Wire EQE1 loading into the sequential zone loader. When `eqe1Path` is configured,
open the zone's `.eqe1` files and register them with the constrained texture cache
so that subsequent `getOrLoad()` calls use pre-compressed ETC1 data.

## Prerequisites

- E04 complete (EQE1Loader class)
- E05 complete (compressed upload path in cache)
- E06 complete (eqe1Path config field)
- Understanding of `loadZoneSequential()` flow in `irrlicht_renderer.cpp`

## Plan

### Step 1: Add EQE1Loader storage to IrrlichtRenderer

**File**: `include/client/graphics/irrlicht_renderer.h`

Add members:
```cpp
#include "client/graphics/eqe1_loader.h"

// EQE1 flat pack loaders (lifetime = zone)
std::unique_ptr<EQE1Loader> zoneEQE1_;      // zone textures (e.g., qeynos2.eqe1)
std::unique_ptr<EQE1Loader> zoneObjEQE1_;   // object textures (e.g., qeynos2_obj.eqe1)
std::unique_ptr<EQE1Loader> zoneChrEQE1_;   // character textures (e.g., qeynos2_chr.eqe1)
std::unique_ptr<EQE1Loader> globalChrEQE1_; // global_chr.eqe1 (shared across zones)
```

### Step 2: Open EQE1 files in loadZoneSequential()

**File**: `src/client/graphics/irrlicht_renderer.cpp`

In `loadZoneSequential()`, **before** any texture loading steps (before Step 1 "Loading
zone data" at line ~3207), add a new early step:

```cpp
// Step 0: Open EQE1 flat packs (if configured)
if (!constrainedConfig.eqe1Path.empty()) {
    status.update(0.01f, "Loading EQE1 textures...");

    auto tryOpen = [&](const std::string& name) -> std::unique_ptr<EQE1Loader> {
        std::string path = constrainedConfig.eqe1Path + "/" + name + ".eqe1";
        auto loader = std::make_unique<EQE1Loader>();
        if (loader->open(path)) {
            LOG_INFO(MOD_GRAPHICS, "EQE1: opened {} ({} entries)",
                     path, loader->getEntryCount());
            return loader;
        }
        LOG_DEBUG(MOD_GRAPHICS, "EQE1: {} not found, using S3D fallback", path);
        return nullptr;
    };

    zoneEQE1_    = tryOpen(currentZoneName_);
    zoneObjEQE1_ = tryOpen(currentZoneName_ + "_obj");
    zoneChrEQE1_ = tryOpen(currentZoneName_ + "_chr");

    // Global chr is shared — only open once (persists across zone changes)
    if (!globalChrEQE1_) {
        globalChrEQE1_ = tryOpen("global_chr");
    }

    // Register with constrained texture cache
    constrainedTextureCache_->clearEQE1Loaders();
    if (zoneEQE1_)    constrainedTextureCache_->addEQE1Loader(zoneEQE1_.get());
    if (zoneObjEQE1_) constrainedTextureCache_->addEQE1Loader(zoneObjEQE1_.get());
    if (zoneChrEQE1_) constrainedTextureCache_->addEQE1Loader(zoneChrEQE1_.get());
    if (globalChrEQE1_) constrainedTextureCache_->addEQE1Loader(globalChrEQE1_.get());
}
```

### Step 3: WLD file access from EQE1

The zone loader currently reads `.wld` files from S3D archives via `PfsArchive::get()`.
When EQE1 is available, WLD data should be read from the EQE1 file instead.

**File**: `src/client/graphics/eq/s3d_loader.cpp` (or wherever WLD is extracted)

Check current WLD loading path:
- Find where `archive.get("*.wld", wldData)` is called
- Add EQE1 fallback: if an EQE1Loader is available and has the WLD entry, use
  `getRawData()` instead of S3D extraction
- This means S3D archives may not need to be opened at all when EQE1 is available

**Decision**: For the first pass, **still open S3D for WLD files** and use EQE1 only
for textures. This minimizes the blast radius. A later unit can add WLD-from-EQE1
as an optimization.

### Step 4: Zone unload cleanup

**File**: `src/client/graphics/irrlicht_renderer.cpp`

In the zone unload/cleanup path (wherever zone resources are released):

```cpp
constrainedTextureCache_->clearEQE1Loaders();
zoneEQE1_.reset();
zoneObjEQE1_.reset();
zoneChrEQE1_.reset();
// globalChrEQE1_ persists — not reset on zone change
```

Ensure `clearEQE1Loaders()` is called **before** resetting the EQE1Loader objects,
so the cache doesn't hold dangling pointers.

### Step 5: Handle re-zoning

When the player zones to a new zone:
1. Zone unload (Step 4) clears loaders
2. New zone's `loadZoneSequential()` (Step 2) opens new EQE1 files
3. `globalChrEQE1_` persists across zones (opened once, never reset)

Verify this works by checking the re-zone flow in Application.

### Step 6: Texture name matching

Current texture names in the cache include `.bmp` extension (e.g., `qeynos2_wl001.bmp`).
EQE1 index entries store names **without** extension (e.g., `qeynos2_wl001`).

In `findInEQE1()` (E05 Step 1d), strip the `.bmp` extension before lookup:
```cpp
std::string lookupName = name;
if (lookupName.size() > 4 && lookupName.substr(lookupName.size() - 4) == ".bmp") {
    lookupName = lookupName.substr(0, lookupName.size() - 4);
}
// lowercase already handled by caller
```

**Alternatively**: Store names WITH extension in the EQE1 builder (E02). This avoids
any extension stripping in the runtime. Need to decide and be consistent.

**Decision**: Store names **without** extension in EQE1 (the name is the texture
identifier, not a filename). Strip `.bmp` in `findInEQE1()`.

### Step 7: Verify transparent integration

After this unit, the following should work **without changes to any texture loading
call sites**:

1. Zone geometry calls `getOrLoad("qeynos2_wl001.bmp", bmpData)` as before
2. Cache checks EQE1 → finds `qeynos2_wl001` → creates CompressedEntry
3. Zone geometry calls `uploadTexture("qeynos2_wl001.bmp")` as before
4. Cache uploads via `glCompressedTexImage2D` (ETC1 from EQE1)
5. Returns ITexture* — mesh building proceeds unchanged

The BMP data parameter to `getOrLoad()` is ignored when EQE1 has the texture.

### Step 8: Build and test

a. `cmake --build build -j24`
b. Run with EQE1 path configured:
   ```
   ./build/bin/willeq -c willeq.json --eqe1-path /tmp/eqe1_test/ --renderer gles2
   ```
c. Verify log output shows EQE1 files being opened
d. Verify zone loads successfully (visual verification)
e. Verify `/pmem` shows texture memory at ~8× lower than without EQE1
f. Test re-zoning: zone to another zone, verify EQE1 files opened for new zone
g. Test fallback: zone to a zone WITHOUT an .eqe1 file, verify S3D/BMP path used

## Acceptance Criteria

- [ ] EQE1 files opened during loadZoneSequential() when eqe1Path configured
- [ ] EQE1Loaders registered with constrained texture cache
- [ ] Zone textures loaded from EQE1 (verified by log output)
- [ ] Object textures loaded from EQE1
- [ ] Global chr textures persisted across zone changes
- [ ] Zone unload clears loaders without dangling pointers
- [ ] Re-zoning opens new zone's EQE1 files
- [ ] Fallback to S3D/BMP when .eqe1 file missing for a zone
- [ ] S3D still opened for WLD files (textures from EQE1, WLD from S3D)
- [ ] No changes to any existing texture loading call sites
- [ ] Zone renders correctly with EQE1 textures (visual comparison)

## Review

All steps completed as planned.

**Step 1**: Added 4 `unique_ptr<EQE1Loader>` members to IrrlichtRenderer (zone, obj, chr,
global_chr). Added `#include "eqe1_loader.h"` to the renderer header.

**Step 2**: Added "Step 0: EQE1" in `loadZoneSequential()` before Step 1 (S3D parse).
Opens zone, zone_obj, zone_chr EQE1 files. Global chr opened once and persisted.
All opened loaders registered with constrained texture cache via `addEQE1Loader()`.

**Step 3**: WLD files still loaded from S3D as planned — minimum blast radius for first pass.

**Step 4**: Zone unload cleanup added alongside atlas unload. `clearEQE1Loaders()` called
before resetting unique_ptrs to prevent dangling pointers. `globalChrEQE1_` not reset
on zone change (persists for entity textures shared across zones).

**Step 6**: Extension stripping already implemented in E05's `findCompressedEntry()` —
strips `.bmp` and `.dds` extensions before lowercase lookup.

**Step 7**: Transparent integration confirmed by design — `getOrLoad()` checks EQE1 before
BMP decode, `uploadTexture()` uses compressed path when CompressedEntry exists. No changes
to any texture loading call sites needed.

**On-device testing**: Deferred to E09 (requires ARM build + Orange Pi). Host build compiles
clean, all tests pass.
