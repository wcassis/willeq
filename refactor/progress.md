# Refactoring Progress

Master tracker for the willeq architecture refactoring effort.

## Process

Each unit follows: **Plan -> Implement -> Review -> Commit**

1. Write plan with numbered steps and acceptance criteria to unit file
2. Implement, re-reading the plan file during work
3. Review: re-read plan, check each step, note deviations in review section
4. Commit only after review passes
5. Update this table

## Completed Batches

All completed batch plans and unit files are in `refactor/completed/`.

| Batch | Description | Status |
|-------|-------------|--------|
| A | Architecture (BackgroundWorkQueue, unified priority, entity pipeline) | done |
| B | Budget/Safety/Cleanup (dead code, budget guards, equipment textures) | done |
| C | Post-audit remaining issues (thread pool, GL safety, mutex, dead code) | done |
| L | Loading Thread Separation (sequential zone loading, GL context transfer) | done |
| S | Startup Validation & Pre-allocation (config validation, fail-fast) | done |
| D | Game State Threading (event bus, bridge, intents, thread separation) | done |
| U | UI and Text Rendering Overhaul (bitmap font, batched UI, static layout) | done |
| T | Texture Loading Consolidation (single decoder, getOrLoad/uploadTexture) | done |

---

## Batch E — ETC1 Flat Pack Textures (GLES2)

Pre-compress all EQ textures to ETC1 offline, package in flat pack container files (`.eqe1`).
Replaces BMP/DDS decode pipeline with direct `glCompressedTexImage2D` upload on GLES2.
Full design: `docs/plans/full_etc1_textures.md`.

### Phase E1 — Offline Tooling

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| E01 | EQE1 binary format definition + header structures | done | |
| E02 | EQE1 builder tool — S3D → ETC1 flat pack converter | done | |
| E03 | Batch conversion script + validation | done | |

### Phase E2 — Runtime Loader

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| E04 | EQE1Loader class — mmap reader with name lookup | done | |
| E05 | Compressed upload path in ConstrainedTextureCache | done | |

### Phase E3 — Integration

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| E06 | Configuration — `--eqe1-path` CLI flag + config field | done | |
| E07 | Zone load wiring — open EQE1 files, pass to cache | done | |
| E08 | Entity texture wiring — chr/obj EQE1 loading during entity prep | done | |

### Phase E4 — Validation and Cleanup

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| E09 | End-to-end validation on Orange Pi (memory, visual, performance) | done | |
| E10 | Remove zone atlas UV workaround path | done | |
