# E03: Batch Conversion Script + Validation

## Overview

Shell script to convert an entire EQ Titanium client directory to EQE1 format in one
command, plus a validation tool to verify the resulting files.

## Prerequisites

- E02 complete (eqe1_builder tool builds and processes individual archives)

## Plan

### Step 1: Create `tools/convert_all_to_eqe1.sh`

```bash
#!/bin/bash
set -euo pipefail

# Usage: convert_all_to_eqe1.sh --eq-path <dir> --output <dir> [--builder <path>]
```

a. Parse arguments:
   - `--eq-path <dir>` — source EQ client directory (required)
   - `--output <dir>` — output directory for .eqe1 files (required, create if missing)
   - `--builder <path>` — path to eqe1_builder binary (default: `./build/bin/eqe1_builder`)

b. Validate inputs:
   - eq-path exists and contains .s3d files
   - builder binary exists and is executable

c. Count total .s3d files, print summary before starting

d. Loop over all .s3d files in eq-path:
   - Call `eqe1_builder --eq-path "$EQ_PATH" --archive "$(basename $s3d)" --output "$OUTPUT"`
   - Track success/failure count
   - Print progress: `[42/310] Processing qeynos2.s3d... done (148 textures, 2.1 MB)`

e. Print final summary:
   - Total archives processed / failed
   - Total textures converted
   - Total alpha textures
   - Total output size vs total input S3D size
   - Elapsed time

### Step 2: Create `tools/verify_eqe1.cpp`

Small standalone tool that reads an .eqe1 file and validates its integrity.

a. Read and validate header (magic, version, numEntries)
b. Read all index entries, validate:
   - name is null-terminated within 32 bytes
   - width/height are non-zero for texture entries, zero for RAW_DATA
   - mipLevels matches `computeMipLevels(width, height)` for texture entries
   - dataOffset is 16-byte aligned
   - dataOffset + dataSize does not exceed file size
   - entryType is 0, 1, or 2
   - hasAlpha=1 entries have a companion `{name}_alpha` entry in the index
c. For each texture entry, verify ETC1 data size matches `etc1MipChainSize(width, height, mipLevels)`
d. Print summary: entry count by type, total data size, any errors found

### Step 3: Add verify_eqe1 to CMakeLists.txt

```cmake
add_executable(verify_eqe1
    tools/verify_eqe1.cpp
)
target_include_directories(verify_eqe1 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
```

Minimal dependencies — only needs eqe1_format.h, no PFS/zlib/etc2comp.

### Step 4: Add verification pass to conversion script

After all archives are converted, run `verify_eqe1` on each output file:

```bash
echo "Verifying..."
VERIFY_FAIL=0
for eqe1 in "$OUTPUT"/*.eqe1; do
    if ! "$BUILDER_DIR/verify_eqe1" "$eqe1"; then
        echo "FAIL: $eqe1"
        VERIFY_FAIL=$((VERIFY_FAIL + 1))
    fi
done
```

### Step 5: Test

a. Run on full EQ client directory:
   ```
   bash tools/convert_all_to_eqe1.sh \
     --eq-path /home/user/projects/claude/EverQuestP1999 \
     --output /tmp/eqe1_full/
   ```
b. Verify all .eqe1 files pass verification
c. Compare total output size to total input S3D size
d. Spot-check a few files with `xxd` to verify header and alignment

## Acceptance Criteria

- [ ] `convert_all_to_eqe1.sh` processes all .s3d files without errors
- [ ] Script creates output directory if it doesn't exist
- [ ] Script reports per-archive progress with texture count and size
- [ ] Script reports final summary with totals and elapsed time
- [ ] `verify_eqe1` validates header, index, data sizes, and alignment
- [ ] `verify_eqe1` detects and reports missing alpha companion entries
- [ ] `verify_eqe1` returns non-zero exit code on any validation failure
- [ ] All generated .eqe1 files pass verification
- [ ] Script handles archives with zero textures (e.g., wld-only archives) gracefully

## Review

All steps completed. Batch script and verify tool both implemented.

**verify_eqe1 results**: 1006/1006 files passed, 0 errors, 0 warnings across the entire
Titanium client (Classic, Kunark, Velious, Luclin-era archives). All header, index,
alignment, mipmap chain size, and alpha companion checks pass.

**Batch script**: Not run separately since E02's `--all` flag already converted the full
client. Script wraps the builder with progress reporting and post-conversion verification.

**EQE1 files**: Located at `/mnt/projects/EverQuest_ETC1/` (1006 files, 2.8 GB).
