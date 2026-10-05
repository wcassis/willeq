# B975 resumed verification — CPU acceptance is blocked

Raw evidence on hermes-ext:
`/home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-279/`.

## Provenance

- The crowd/normal and live-session captures use integrated B986 baseline
  `fd8b002b2a3ecb4f1b7d1a896cad20b9812f4d5d`, before index reuse.
- The optimized fixed-pose and microbenchmark captures use
  `c81e226090f2960bcd21eac8abee41b2f38ed72b`, pipeline 490.
- The pinned capture harness is separately identified as
  `b0bb555ee8d2cdc8f25cb16202a8237e2d237eeb`; that is not the client revision.
- Exact terminal HEAD, final CI/micro checks and cleanup are recorded in
  the raw run's `result.json` and `final-report.txt`. Documentation-only
  descendants do not silently inherit acceptance: the full optimized-head
  matrix remains unverified after the CPU stop condition.

`micro-summary.md` separates the old tool's cached default from production
policy. Its apparent pass is not production acceptance. The corrected tool
uses the renderer's policy and fails the unchanged CPU bar. Component
medians need not sum to the total median. `pi-pixel.csv` and `x86-pixel.csv`
are optimized primary fixed-pose comparisons; the separately named tie-order
column remains the documented experiment, not the primary comparison.

`pi-summary.md` joins idle frame timing and the verifier's final-visit memory
values; `pi-verification.csv` retains source paths and net-of-PNG values.
The raw crowd-table GPU values include captured PNG charges. Do not mix
those definitions. Separate live runs have different dynamic visibility and
governor choices; total transparent-pass time is not a controlled per-tag
comparison when visible counts differ. X86/llvmpipe is integration evidence,
not Pi performance or a waiver of its known B974 memory-growth finding.

`pi-timing.md` / `x86-timing.md` and `pi-crowd.md` / `x86-crowd.md` are the
repository parsers' baseline tables; `x86-verification.csv` records the
final-visit/exit checks. These are not optimized-head matrix acceptance.
`live-comparison.json` retains the fixed rectangles and measured mask counts:
white NPC masks match before/after death, and blue player masks match after
death. The living player mask differs by six pixels in the overlapping scene;
its cause is not established here. Both screenshot pairs were inspected,
but neither whole-frame identity nor human approval is claimed.

No threshold, golden or baseline was changed. No human visual approval was
granted, and no dependent task is released by these measurements.
