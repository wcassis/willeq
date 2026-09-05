# T387 — the two hwcaps tool files under the ceiling (B167, hwcaps half)

Class P (structural), slot3, no server. Read B167, the K/L mechanism
(`refactor/K_component_extraction.md` §Mechanism), and T385's plan (the
sibling unit decomposing `model_viewer`; T385 also extends
`check_file_sizes.sh --all` to `tools/` — do NOT touch that script or the
allowlist here; if your branch merges first the check still covers only
src/include/tests, and that is fine).

Files: `tools/hwcaps/zone_bench_loader.cpp` (3,280) and
`tools/hwcaps/gles2_zone_benchmark.cpp` (3,060). These are the visual
gate's benchmark (`build-gles2/bin/gles2_zone_benchmark`) — the gate is
the behaviour proof.

1. Read both first; write the split plan into the Review BEFORE editing
   (functional seams: CLI/config, zone loading steps, memory accounting
   (`zone_bench_memory.h` already exists), the frame loop / capture, the
   report writer). Extract by function into `tools/hwcaps/<part>.cpp` +
   headers; each part ≤ 1,500; `main` tiny. One commit per part.
2. Behaviour proof: the visual gate run BEFORE the first edit (record its
   manifest / metrics JSON and the golden comparison) and AFTER the last —
   pixels identical (goldens untouched — a moved golden is a STOP, never
   bless), memory categories identical to the byte, CPU p95 within the
   ceiling (load artefacts re-run standalone). Also `--help` output
   byte-identical.
3. CMake: the tool target lists its new sources; both `build` and
   `build-gles2` build it.
4. Annotate B167 (hwcaps half done, hashes); T385 strikes it when the
   check covers tools/.

## Proof

`build`, `build-gles2` (both trees), `ctest -j6`, the visual gate before
and after, `check_file_sizes.sh --files` on every new file, ARM build if
the benchmark is in the ARM tree (check `scripts/build-arm-noble.sh`).

STOP: a pixel golden moves; a memory category changes by even one byte.
