# E09: End-to-End Validation on Orange Pi

## Overview

Full validation of the EQE1 texture system on the target hardware (Orange Pi One,
Mali 400, 512 MB RAM). Verify memory savings, visual correctness, performance, and
stability across multiple zones.

## Prerequisites

- E01-E08 complete (full pipeline: builder → loader → cache → zone/entity rendering)
- EQE1 files generated for test zones via E03 batch script
- ARM cross-compile succeeds with EQE1 support

## Plan

### Step 1: Generate EQE1 files for test zones

Run batch conversion on host machine:
```bash
bash tools/convert_all_to_eqe1.sh \
  --eq-path /home/user/projects/claude/EverQuestP1999 \
  --output /tmp/eqe1_deploy/
```

Verify all files pass validation:
```bash
for f in /tmp/eqe1_deploy/*.eqe1; do
    ./build/bin/verify_eqe1 "$f"
done
```

### Step 2: Deploy to Orange Pi

```bash
scp /tmp/eqe1_deploy/*.eqe1 orangepi:~/willeq/eqe1/
scp build-arm-noble/bin/willeq orangepi:~/willeq/
```

### Step 3: Memory validation

Run on Orange Pi with and without EQE1, compare `/pmem` output:

a. **Baseline (no EQE1)**:
   ```
   ./willeq -c config.json --drm --renderer gles2 --constrained orangepi -r 800 600
   ```
   Log `/pmem` output after zone load: texture memory, mesh memory, total

b. **EQE1 enabled**:
   ```
   ./willeq -c config.json --drm --renderer gles2 --constrained orangepi \
     --eqe1-path ~/willeq/eqe1/ -r 800 600
   ```
   Log `/pmem` output after zone load

c. **Expected result**: texture memory ~8× lower with EQE1
   - Example: 32 MB baseline → ~4 MB with EQE1

d. Test across multiple zones:
   - Small zone (befallen — few textures)
   - Medium zone (qeynos2 — moderate textures + entities)
   - Large zone (commons — many entities, open area)

### Step 4: Visual correctness

Compare rendering quality between BMP and EQE1 for each zone:

a. **Zone geometry**: Correct textures on walls, floors, terrain
b. **Texture tiling**: Repeating textures tile correctly (no seams, no UV artifacts)
c. **Character models**: Skin textures, face textures, body parts correct
d. **Equipment**: Weapons and armor render with correct textures
e. **Armor variants**: Different armor materials (leather/chain/plate) distinguishable
f. **Alpha textures**: Trees, fire, grass render with transparency (not solid rectangles)
g. **Mipmaps**: Distant textures smooth (no shimmer/aliasing)

Take screenshots for comparison. ETC1 is lossy — some quality reduction is expected
(especially on gradients), but should be visually acceptable at EQ's texture resolution.

### Step 5: Performance validation

Compare frame timing with and without EQE1:

a. **Zone load time**: Time from zone start to fully loaded
   - EQE1 should be faster (no BMP decode, no zlib inflate)
b. **Frame time**: Average and 99th percentile during gameplay
   - ETC1 reduces bandwidth → may improve frame time on Mali 400
c. **Memory headroom**: With 8× less texture memory, more room for mesh cache
   - Check if mesh eviction rate decreases

### Step 6: Stability validation

a. **Zone repeatedly**: Zone in and out 10+ times, verify no memory leaks or crashes
b. **Multi-zone traversal**: Walk through zone line from qeynos2 → qeytoqrg → qeynos
   - Verify EQE1 files open/close correctly across zone changes
   - Verify global_chr persists
c. **Missing EQE1 fallback**: Remove one zone's .eqe1 file, verify BMP fallback works
d. **Long run**: Leave running for 30+ minutes, verify stable memory usage

### Step 7: Document results

Record all findings in the Review section:
- Memory numbers (before/after per zone)
- Visual quality assessment
- Performance numbers
- Any issues found and fixes applied

## Acceptance Criteria

- [ ] All test zones load successfully on Orange Pi with EQE1
- [ ] Texture memory reduced by ~8× (measured via /pmem)
- [ ] Zone textures render correctly with tiling
- [ ] Character textures render correctly (not gray)
- [ ] Equipment textures render correctly
- [ ] Alpha textures (trees, fire) render with transparency
- [ ] Mipmaps visible on distant surfaces (smooth, no shimmer)
- [ ] Zone load time equal or faster than BMP baseline
- [ ] No memory leaks across zone changes
- [ ] BMP fallback works when .eqe1 file missing
- [ ] Stable over 30-minute run

## Review

**ARM build**: Succeeded after fixing one linker error — `gles2WrapTexture` extern declaration
was inside `namespace EQT::Graphics` in `constrained_texture_cache.cpp`, causing C++ name
mangling mismatch with the global-scope definition in `COpenGLES2Driver.cpp`. Fixed by moving
the extern to file scope (outside the namespace), before the namespace block.

**Binary ready**: `/mnt/projects/willeq-atlas/build-arm-noble/bin/willeq` (ARM ELF 32-bit)
**EQE1 files ready**: `/mnt/projects/EverQuest_ETC1/` (1006 files, 2.8 GB)

**Deploy commands**:
```bash
scp build-arm-noble/bin/willeq orangepi:~/willeq/
rsync -av /mnt/projects/EverQuest_ETC1/ orangepi:~/willeq/eqe1/
```

**Test command**:
```bash
./willeq -c config.json --drm --renderer gles2 --constrained orangepi \
  --eqe1-path ~/willeq/eqe1/ -r 800 600
```

**On-device validation**: Awaiting deployment to Orange Pi. Steps 3-7 (memory, visual,
performance, stability testing) to be completed on hardware.
