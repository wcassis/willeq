# P03 — Flatpack payload-format shootout (ETC1 vs RGBA8888 vs RGB565/4444)

Will's directive: evaluate retiring ETC1 encoding while keeping the
flatpack container + offline pre-baking (his file-open cost point: ~200ms
base per open on the Pi — consolidation and pre-decode stay regardless).
This unit produces the DATA for that decision. No retirement happens here —
deliverable is a four-way measurement table + a drafted decision-doc entry.

Goal frame (Will's words): performant client, >30fps, low RAM. P01/P02
context: RGBA path is stable (±2%), EQE1 erratic (>49% spread, cause under
investigation); ETC1's negatives include the alpha-companion architecture,
unrolled-texture workarounds, and the composite/animation friction.

## Part 1 — v2 pack format + builder + loader

1. **Container v2**: extend the EQE1 format (include/common or its format
   header + docs) with a version bump and a per-entry payloadFormat field:
   `ETC1` (v1 semantics), `RGB565` (opaque), `RGBA4444` (alpha textures —
   4-bit alpha is plenty for binary masks), `RGBA8888`. v1 packs remain
   readable (loader dispatches on version).
2. **Builder** (`eqe1_builder`): `--payload-format etc1|rgba8888|rgb565`
   where rgb565 mode emits 565 for opaque textures and 4444 for
   alpha-carrying ones (the builder already decodes and knows alpha
   presence). Mip chains included as v1 does.
3. **Loader/upload**: v2 payload upload via glTexImage2D with the matching
   GL format/type (565: GL_RGB/UNSIGNED_SHORT_5_6_5; 4444:
   GL_RGBA/UNSIGNED_SHORT_4_4_4_4; 8888 as today's RGBA). NO alpha
   companion for v2 entries; `isETC1Texture()` false → H02d's main-alpha
   shader variant applies. ETC1/v1 path byte-for-byte untouched.
4. Build packs (zone + _obj + _chr as the benchmark requires) for the three
   golden zones in all three new/old variants.
5. VM sanity: benchmark under llvmpipe with each v2 variant — rendering
   intact (foliage cutouts present, no magenta/garbage); pixel-exact match
   to goldens is NOT expected for 565/4444 (quantization) — document the
   visual delta qualitatively (and capture comparison PNGs for Will).

## Part 2 — Pi shootout (after P02's protocol: interleaved ordering,
CmaFree telemetry, session caps, scripted reboots, /tmp I/O rule)

Four configurations × three zones: raw S3D decode (no --eqe1-path),
flatpack-ETC1 (v1), flatpack-RGBA8888, flatpack-565/4444. Measure:
- **Cold zone-load time** (the file-open/pre-decode claim): drop caches
  (`echo 3 > /proc/sys/vm/drop_caches` via sudo) before each load-time
  trial; time the load phase (add a load-phase timing line to the benchmark
  output if absent — flag-gated); ALSO record warm-cache load. Note pack
  byte sizes per variant (bytes-over-NFS matters: RGBA is ~6-8x ETC1; 565
  halves RGBA).
- **Steady-state p95 + run-to-run variance** (the stability finding is the
  differentiator — interleave all four configs; 2+ samples each across
  sessions).
- **Memory**: texture cache bytes per config + CmaFree during run.

## Deliverables

- The 4×3 results table (load cold/warm, p95, variance, bytes, CmaFree).
- Comparison PNGs (llvmpipe) of 8888 vs 565/4444 for one texture-rich pose.
- A drafted decision entry (do NOT commit to docs/decisions.md — put the
  draft in this file's review for Will's sign-off): recommendation with
  numbers on whether ETC1 retires, and if so the staged migration
  (feature-flag off → archive tools/companion machinery → remove companion
  shader variant).
- Updated deferred.md entries left to the orchestrator.

## Verification

- v1/ETC1 behavior unchanged (Pi ETC1 runs still work; ctest + visual gate
  green on the VM; full default-build bar since loader/builder are
  production code).
- All Pi work under the P02 protocol; Pi pristine after sessions.

## Review

(executor fills in)
