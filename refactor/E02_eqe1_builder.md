# E02: EQE1 Builder Tool — S3D to ETC1 Flat Pack Converter

## Overview

Offline command-line tool that reads S3D archives and produces `.eqe1` flat pack files
containing ETC1-compressed textures with full mipmap chains, plus any non-texture data
(`.wld` files) as raw entries.

## Prerequisites

- E01 complete (EQE1 format header with structures and helpers)
- etc2comp library (already FetchContent'd in CMakeLists.txt, lines 939-944)
- PfsArchive reader (existing: `include/client/graphics/eq/pfs.h`)
- DDSDecoder (existing: `include/client/graphics/eq/dds_decoder.h`)

## Plan

### Step 1: Create `tools/eqe1_builder.cpp`

Main source file. Model on `tools/zone_atlas_builder.cpp` structure.

### Step 2: Include dependencies

```cpp
#include "client/graphics/eqe1_format.h"    // E01 format definitions
#include "client/graphics/eq/pfs.h"          // S3D archive reader
#include "client/graphics/eq/dds_decoder.h"  // DDS detection + decode
#include <Etc.h>                             // etc2comp ETC1 encoder
#include <EtcImage.h>
```

### Step 3: BMP decode function

Copy `decodeBMP()` from `tools/zone_atlas_builder.cpp:176-256`. This handles:
- 8bpp paletted (BGRA palette at offset 54, 256 entries × 4 bytes)
- 24bpp (BGR → RGB, alpha=255)
- 32bpp (BGRA → RGBA)
- Row alignment to 4-byte boundary
- Negative height (top-down DIB)

Output: `std::vector<uint8_t>` of RGBA pixels, width, height.

### Step 4: Alpha detection function

Copy `detectAlpha()` from `zone_atlas_builder.cpp:168-173`.
Scans RGBA pixels for any `alpha[i*4+3] < 255`.

### Step 5: Float pixel conversion function

`std::vector<float> toFloat(const uint8_t* rgba, int width, int height)`

Converts uint8 RGBA [0,255] to float [0,1] for etc2comp input.
Reference: `zone_atlas_builder.cpp:302-306`.

### Step 6: ETC1 encode function

`std::vector<uint8_t> encodeETC1(const uint8_t* rgba, int width, int height)`

a. Convert to float via Step 5
b. Call `Etc::Encode()` with:
   - `Etc::Image::Format::ETC1`
   - `Etc::ErrorMetric::RGBA`
   - `ETCCOMP_DEFAULT_EFFORT_LEVEL`
   - 1 job, 1024 max jobs
c. Return encoded bytes
d. Free encoding bits buffer after copying

Reference: `zone_atlas_builder.cpp:313-325`.

### Step 7: Downsample function

`std::vector<uint8_t> downsample2x(const uint8_t* rgba, int srcW, int srcH, int& dstW, int& dstH)`

Box filter 2× downsample of RGBA data. Clamp source indices for odd dimensions.
Reference: `zone_atlas_builder.cpp:261-284`.

Output dimensions: `dstW = max(4, srcW/2)`, `dstH = max(4, srcH/2)`.
(Clamp to 4×4 minimum for ETC1 block size.)

### Step 8: Mipmap chain generation function

`std::vector<uint8_t> generateMipChainETC1(const uint8_t* rgba, int width, int height, uint8_t& mipLevels)`

a. Compute mipLevels via `computeMipLevels(width, height)` from E01 header
b. Encode base level (Step 6), append to output
c. For each subsequent level:
   - Downsample previous level (Step 7)
   - Encode to ETC1 (Step 6)
   - Append to output
   - Stop when dimensions reach 4×4
d. Return concatenated ETC1 data for all levels

Reference: `zone_atlas_builder.cpp:378-405`.

### Step 9: Alpha channel extraction function

`std::vector<uint8_t> extractAlphaAsGrayscale(const uint8_t* rgba, int width, int height)`

Creates RGBA where R=G=B=original_alpha, A=255.
Reference: `zone_atlas_builder.cpp:336-348`.

### Step 10: Process single S3D archive function

`bool processArchive(const std::string& archivePath, const std::string& outputPath)`

a. Open S3D via `PfsArchive::open(archivePath)`
b. Get all `.bmp` filenames via `archive.getFilenames(".bmp", bmpFiles)`
c. Get all `.wld` filenames via `archive.getFilenames(".wld", wldFiles)`
d. Get all file data via `archive.getFiles()` (returns `const std::map<std::string, std::vector<char>>&`)

e. **Build index entries** — first pass (compute sizes, no encoding yet):
   - For each BMP: decode to get width/height, detect alpha, compute `etc1MipChainSize()`
   - For alpha BMPs: also add companion `{name}_alpha` entry with same dimensions
   - For each WLD: entry with `entryType=RAW_DATA`, dataSize = raw file size
   - Compute data offsets: header (16) + index (N × 48) + padding to 16-byte align,
     then each entry's data at 16-byte aligned offsets

f. **Second pass — encode and write**:
   - Open output file
   - Write EQE1Header
   - Write index entries (N × 48 bytes)
   - For each BMP texture:
     - Decode BMP to RGBA (Step 3, or DDSDecoder if DDS)
     - Encode full mip chain (Step 8)
     - If alpha: also encode alpha-as-grayscale mip chain (Step 9 + Step 8)
     - Seek to entry's dataOffset, write ETC1 data
     - Pad to 16-byte alignment between entries
   - For each WLD file:
     - Seek to entry's dataOffset, write raw data
     - Pad to 16-byte alignment

g. Log per-archive stats: texture count, alpha count, WLD count, total file size,
   compression ratio vs original S3D size.

### Step 11: CLI argument parsing

```
Usage: eqe1_builder [options]
  --eq-path <dir>       Path to EQ client directory containing .s3d files
  --archive <name>      Process single archive (e.g., qeynos2.s3d)
  --output <dir>        Output directory for .eqe1 files
  --all                 Process all .s3d files in eq-path
  --verbose             Print per-texture stats
```

Validate:
- `--eq-path` is required and must be a directory
- `--output` is required and must be a directory (create if missing)
- Either `--archive` or `--all` is required (not both)
- `--archive` file must exist in `--eq-path`

### Step 12: Main function

a. Parse CLI args (Step 11)
b. If `--all`: glob `*.s3d` in eq-path, call `processArchive()` for each
c. If `--archive`: call `processArchive()` for the single archive
d. Print summary: total archives processed, total textures, total output size

### Step 13: Add to CMakeLists.txt

Model on zone_atlas_builder entry (CMakeLists.txt lines 946-970):

```cmake
add_executable(eqe1_builder
    tools/eqe1_builder.cpp
    src/client/graphics/eq/pfs.cpp
    src/client/graphics/eq/dds_decoder.cpp
)
target_include_directories(eqe1_builder PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${ZLIB_INCLUDE_DIRS}
    ${etc2comp_SOURCE_DIR}/EtcLib/Etc
    ${etc2comp_SOURCE_DIR}/EtcLib/EtcCodec
)
target_link_libraries(eqe1_builder PRIVATE
    ${ZLIB_LIBRARIES}
    fmt::fmt
    EtcLib
)
```

### Step 14: Build and test

a. `cmake --build build -j24` — verify eqe1_builder compiles
b. Run on a single small archive:
   `./build/bin/eqe1_builder --eq-path /home/user/projects/claude/EverQuestP1999 --archive qeynos2_chr.s3d --output /tmp/eqe1_test/ --verbose`
c. Verify output file exists: `/tmp/eqe1_test/qeynos2_chr.eqe1`
d. Verify header magic and version by hex dump: `xxd /tmp/eqe1_test/qeynos2_chr.eqe1 | head -2`
e. Run on a zone with alpha textures:
   `./build/bin/eqe1_builder --eq-path /home/user/projects/claude/EverQuestP1999 --archive qeynos2_obj.s3d --output /tmp/eqe1_test/ --verbose`
f. Verify alpha entries are created (builder should log alpha texture names)

## Acceptance Criteria

- [ ] `tools/eqe1_builder.cpp` compiles and links
- [ ] Processes qeynos2_chr.s3d without errors
- [ ] Output file starts with magic bytes `45 51 45 31` ("EQE1")
- [ ] Version field is 1
- [ ] numEntries matches BMP count + WLD count + alpha companion count
- [ ] Each index entry has correct width/height matching original BMP dimensions
- [ ] mipLevels is correct: 256×256 → 7, 64×32 → 4, 4×4 → 1
- [ ] dataOffset values are all 16-byte aligned
- [ ] dataSize matches `etc1MipChainSize()` for each texture entry
- [ ] Alpha textures have companion `{name}_alpha` entries
- [ ] WLD entries have `entryType=2` and contain the original WLD bytes
- [ ] `--all` flag processes every .s3d in the EQ directory
- [ ] Output file size is smaller than input S3D (ETC1 + no deflate overhead vs deflate'd BMPs)
- [ ] No memory leaks (etc2comp Encode allocates — must free encoding bits)
- [ ] ARM cross-compile succeeds (verify via `scripts/build-arm-noble.sh` after adding to CMakeLists.txt)

## Dependencies

- etc2comp encoding is CPU-intensive. A single 256×256 texture with 7 mip levels takes ~50ms.
  Processing all 9,677 textures will take ~8 minutes single-threaded. Acceptable for offline tool.
  Future optimization: parallelize with OpenMP or std::thread pool.

## Review

All steps completed as planned. One deviation from the zone_atlas_builder's BMP decoder:

**8bpp palette fix**: The original `decodeBMP()` (copied from zone_atlas_builder) assumed
256 palette entries starting at offset 54. Some EQ BMPs have `colorsUsed` < 256 (as few
as 2), with palette at offset `14 + dibHeaderSize` and only `colorsUsed` entries. Fixed
by reading `colorsUsed` from DIB header offset 46 (0 = 256), computing palette start from
`14 + dibHeaderSize`, and clamping pixel indices to `colorsUsed`. This fix resolved 12
failures in qeynos2_chr.s3d.

**Full run results**: 1006/1006 S3D archives converted, 0 skipped textures, 0 failures.
Output: 2.8 GB EQE1 files in `/mnt/projects/EverQuest_ETC1/` from 6.1 GB S3D input.
Covers all Classic, Kunark, Velious, and Luclin-era archives in the Titanium client.

The same palette fix should be backported to `tools/zone_atlas_builder.cpp` (outside E02 scope).

ARM cross-compile not yet verified (deferred to next ARM build).
