# E06: Configuration — `--eqe1-path` CLI Flag + Config Field

## Overview

Add configuration support for the EQE1 flat pack directory path. Follows the existing
`--atlas-path` pattern: CLI flag overrides config file, config file overrides preset default.

## Prerequisites

- E01 complete (EQE1 format header — needed for the config field name to be consistent)

## Plan

### Step 1: Add config field to ConstrainedRendererConfig

**File**: `include/client/graphics/constrained_renderer_config.h`

Add after `atlasPath` (line 113):
```cpp
std::string eqe1Path;    // Directory containing .eqe1 flat pack files
```

### Step 2: Add CLI flag parsing

**File**: `src/client/application.cpp`

In the CLI parsing section (near line 1483 where `--atlas-path` is handled):

```cpp
} else if (arg == "--eqe1-path" && i + 1 < argc) {
    config.eqe1Path = argv[++i];
```

Add to the config override section (near line 325 where atlas path is applied):
```cpp
if (!config.eqe1Path.empty()) {
    builtConfig.eqe1Path = config.eqe1Path;
    LOG_DEBUG(MOD_GRAPHICS, "CLI override: eqe1 path -> '{}'", config.eqe1Path);
}
```

### Step 3: Add JSON config support

**File**: `src/client/application.cpp` (or wherever preset JSON is parsed)

In the preset JSON reading section, add alongside `atlasPath`:
```cpp
if (preset.isMember("eqe1Path")) {
    config.eqe1Path = preset["eqe1Path"].asString();
}
```

### Step 4: Add to constrained preset definitions

**File**: Check where Orange Pi / constrained presets are defined (likely in
`include/client/graphics/constrained_renderer_config.h` or a preset JSON file).

The `orangepi` preset should include `eqe1Path` with a sensible default
(empty string = disabled, use S3D/BMP path).

### Step 5: Log at startup

**File**: `src/client/graphics/irrlicht_renderer.cpp`

In `loadZoneSequential()` where config is logged (near line 3180):
```cpp
LOG_DEBUG(MOD_GRAPHICS, "SEQ Setup: constrainedConfig: ... eqe1Path='{}'",
          constrainedConfig.eqe1Path);
```

### Step 6: Validation

**File**: `src/client/application.cpp` (or wherever startup validation happens — Batch S)

If `eqe1Path` is non-empty:
- Verify directory exists (FATAL if not)
- Log at INFO: `"EQE1 texture path: {} ({} .eqe1 files found)"`

### Step 7: Build and test

a. `cmake --build build -j24`
b. Verify `--eqe1-path /tmp/eqe1_test` is accepted without error
c. Verify path appears in startup log output
d. Verify FATAL on nonexistent path: `--eqe1-path /nonexistent`
e. Verify empty path (no flag) = no error, feature disabled

## Acceptance Criteria

- [ ] `--eqe1-path <dir>` CLI flag is parsed and stored in config
- [ ] JSON config `"eqe1Path"` field is read from preset
- [ ] CLI overrides JSON config
- [ ] Path logged at startup (DEBUG level)
- [ ] FATAL error if path is set but directory doesn't exist
- [ ] Empty path = feature disabled, no errors
- [ ] No behavioral change when eqe1Path is empty (all existing paths work unchanged)
- [ ] ARM cross-compile succeeds

## Review

All steps completed. One deviation: `eqe1Path` needed to be added to both
`ConstrainedRendererConfig` (line 114) and `ApplicationConfig` (application.h line 90),
since CLI parsing stores to `ApplicationConfig` first, then overrides into the
constrained config.

**Files modified**:
- `include/client/graphics/constrained_renderer_config.h` — added `eqe1Path` field
- `include/client/application.h` — added `eqe1Path` field to ApplicationConfig
- `src/client/application.cpp` — CLI parsing (`--eqe1-path`), config override, help text,
  startup validation (FATAL on nonexistent directory, INFO log with file count)
- `src/client/graphics/constrained_renderer_config.cpp` — JSON preset `"eqe1Path"` parsing
- `src/client/graphics/irrlicht_renderer.cpp` — added eqe1Path to SEQ Setup log line

**Startup validation**: If `--eqe1-path` is set, verifies directory exists (FATAL if not)
and logs the number of .eqe1 files found. Uses `<filesystem>` (added to application.cpp includes).

Build clean, all tests pass.
