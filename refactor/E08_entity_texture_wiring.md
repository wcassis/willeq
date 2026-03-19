# E08: Entity Texture Wiring — chr/obj EQE1 Loading During Entity Prep

## Overview

Ensure character model and equipment textures are loaded from EQE1 flat packs during
entity preparation. This covers the texture loading paths in RaceModelLoader,
equipment model loader, and variant loading — all of which ultimately call
`constrainedCache_->getOrLoad()`.

## Prerequisites

- E05 complete (compressed upload path in cache)
- E07 complete (zone EQE1 files opened and registered with cache)
- Understanding of entity texture loading flow (Batch T consolidated all paths to getOrLoad)

## Plan

### Step 1: Verify entity texture flow already works

After Batch T, **all** entity texture loading goes through `getOrLoad()`:
- `RaceModelLoader::preloadModelData()` — calls `getOrLoad()` for body textures
- Step 8c `decodeAndAdd` lambda — calls `getOrLoad()` for variant textures
- Step 8d `prepOneEquip` lambda — calls `getOrLoad()` for equipment textures

Since E05 adds EQE1 lookup to `getOrLoad()`, and E07 registers chr EQE1 loaders with
the cache, entity textures **should work automatically** — no additional wiring needed.

**Test this**: Load a zone with entity textures, verify they load from EQE1.

### Step 2: Verify additional chr archives are covered

Character textures come from multiple archives:
- `global_chr.s3d` → `global_chr.eqe1` (opened in E07, persists across zones)
- `{zone}_chr.s3d` → `{zone}_chr.eqe1` (opened in E07, per-zone)

But some race-specific archives aren't zone-specific:
- `hum_chr.s3d`, `elf_chr.s3d`, `dwf_chr.s3d`, etc.
- `global2_chr.s3d`, `global3_chr.s3d`

**Investigate**: Where does RaceModelLoader open these archives? Does it load from
`eq_client_path` directly, or from the already-cached S3D data?

If RaceModelLoader opens per-race chr archives on demand, we need to:
a. Open corresponding `.eqe1` files when first referenced
b. Register them with the cache

### Step 3: Open per-race EQE1 files on demand

**File**: `src/client/graphics/irrlicht_renderer.cpp` (or wherever RaceModelLoader
is invoked with archive paths)

If race-specific archives are opened individually (not all at zone load time):

a. Add a lazy-open method:
   ```cpp
   const EQE1Loader* IrrlichtRenderer::getOrOpenEQE1(const std::string& archiveName);
   ```
b. Maintains a map of opened EQE1 loaders:
   ```cpp
   std::unordered_map<std::string, std::unique_ptr<EQE1Loader>> openedEQE1_;
   ```
c. On first call: open `{eqe1Path}/{archiveName}.eqe1`, register with cache, cache in map
d. On subsequent calls: return existing loader
e. Cleared on zone unload (or application shutdown)

### Step 4: Equipment textures from gequip archives

Equipment textures come from:
- `gequip.s3d` → `gequip.eqe1`
- `gequip2.s3d` → `gequip2.eqe1`
- `gequip3.s3d` → `gequip3.eqe1`

**Investigate**: Where are gequip archives opened? Same pattern as chr archives.

If opened on demand, the lazy-open method (Step 3) handles them automatically.
If pre-loaded at startup, need to add EQE1 opening alongside.

### Step 5: Verify texture name format consistency

Entity texture names in `getOrLoad()` calls must match EQE1 index entries.

Check:
- Are entity texture names lowercase? (EQE1 index is lowercase)
- Do they include `.bmp` extension? (findInEQE1 strips it, per E07 Step 6)
- Any other naming variations (prefix, suffix)?

Verify by logging the texture names passed to `getOrLoad()` during entity loading
and comparing them against the EQE1 index entries.

### Step 6: Handle missing textures gracefully

Some entity textures may not be in the EQE1 file (e.g., if the conversion script
missed an archive, or a texture was added after conversion). The fallback to S3D/BMP
decode must work seamlessly:

- `findInEQE1()` returns nullptr → `getOrLoad()` falls through to BMP decode
- Entity renders with BMP-decoded texture (higher memory, but functional)
- Log at WARN level: `"Texture {} not found in EQE1, using BMP fallback"`

### Step 7: Build and test

a. `cmake --build build -j24`
b. Run with EQE1 and zone with NPCs:
   ```
   ./build/bin/willeq -c willeq.json --eqe1-path /tmp/eqe1_full/
   ```
c. Verify character models have textures (not gray placeholders)
d. Verify equipment textures render correctly
e. Check log for "EQE1 cache hit" messages for entity textures
f. Check `/pmem` for entity texture memory vs baseline (should be ~8× lower)
g. Verify armor variants render correctly (different armor materials)

## Acceptance Criteria

- [ ] Character body textures load from chr EQE1 files
- [ ] Equipment textures load from gequip EQE1 files
- [ ] Race-specific archives (hum_chr, elf_chr, etc.) loaded on demand
- [ ] global_chr.eqe1 textures shared across all entities
- [ ] Texture name matching works (lowercase, extension handling)
- [ ] Fallback to BMP decode when texture not in EQE1
- [ ] No gray placeholder entities when EQE1 is configured correctly
- [ ] Entity texture memory ~8× lower than BMP baseline (via `/pmem`)
- [ ] Armor appearance variants render correctly
- [ ] No changes to entity loading call sites (transparent via cache)

## Review

Implementation deviated significantly from the original plan. Instead of relying on
entity textures "just working" via E07's zone-only loaders, investigation revealed
many additional archives opened on demand:

- `global2-7_chr.s3d` (numbered global character models)
- `gequip.s3d` through `gequip8.s3d` (equipment models)
- `global17-23_amr.s3d` (armor textures)
- Race-specific `globalhum_chr.s3d`, etc.

**Approach**: Replaced E07's individual `unique_ptr` members with a thread-safe
lazy-open map (`eqe1Loaders_` + `eqe1Mutex_`). Added `getOrOpenEQE1(archiveBaseName)`
method that:
1. Checks if already opened (returns cached loader)
2. Opens `{eqe1Path}/{name}.eqe1` if not
3. Registers with constrained cache via `addEQE1Loader()`
4. Stores nullptr on failure (prevents repeated attempts)

**Pre-opened at zone load (Step 0)**:
- Zone-specific: `{zone}`, `{zone}_obj`, `{zone}_chr`
- Global characters: `global_chr`, `global2-7_chr`
- Equipment: `gequip`, `gequip2-8`
- Armor: `global17-23_amr`

This covers all known entity texture archives at zone load time. The entity prep
worker's `getOrLoad()` calls will find textures in the already-registered EQE1 loaders.
Race-specific archives (globalhum_chr, etc.) are also available since `getOrOpenEQE1()`
is public and thread-safe — it can be called from entity prep if needed in the future.

**Zone unload**: `clearEQE1Loaders()` + `eqe1Loaders_.clear()` replaces the old
individual reset calls.

Build clean, all tests pass. On-device validation deferred to E09.
