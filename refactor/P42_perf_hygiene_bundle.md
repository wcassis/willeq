# P42 — Perf-hygiene bundle (audit items 2+4 + measurement honesty)

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Sibling P41 (slot1)
measures on the Pi from its own branch/binaries — territories
disjoint; you own the driver/benchmark/config files below.

Four small, independent fixes from P38/P39 findings. Each lands with
evidence; none changes shipped rendering behavior.

## Work
1. **State-dedup (audit item 4)**: productionize P38's measured
   prototype — dedup redundant per-draw glTexParameteri (and any
   other trivially-dedupable state the P38 counters flagged) in
   COpenGLES2Driver, following the file's existing state-compare
   idioms. P38 measured −99.9% calls / −0.5% time: land as
   defense-in-depth. MUST be behavior-identical: gate PASS, and a
   counter A/B (calls drop, draws/output identical).
2. **enableMipmaps dead flags (audit item 2)**: P38 found the flag(s)
   dead (never generate mips; MIPMAP filter requested on incomplete
   chains Pi-side). Given the measured ~0% win, do NOT enable mipmaps:
   make the config HONEST — either wire the flag to a real working
   implementation kept default-OFF (preferred if cheap — P38's
   prototype exists in its Review) or remove/comment the dead flag
   with the P38 citation. No default behavior change.
3. **Benchmark occluder-budget honesty (P39 finding)**:
   zone_bench_loader hardcodes --occluder-budget default 16 and never
   reads the config it builds. Make the DEFAULT resolve from the
   ConstrainedRendererConfig it already constructs (explicit CLI flag
   still wins). Prevents future measurement drift like P38's.
4. **Three same-pattern JSON keys (P39 audit)**: add parses +
   explicit orangepi JSON entries for totalMemoryBudgetBytes,
   occlusionBufferWidth, occlusionBufferHeight (same precedence
   pattern + extend P39's regression test to assert them).

## Rules
- Goldens untouched (gate PASS mandatory — item 1 especially).
- Each item = separate commit on the branch, so any one can be
  reverted independently.
- STOP per item (not unit): if an item can't be behavior-identical,
  drop it with a note and land the rest.

## Verification
- Full bar (GLES2 ctest baseline ~1418/1420, gate PASS, ARM clean,
  file sizes) + per-item evidence (counter A/B for 1; config-resolve
  log for 3; extended regression test green for 4).

## Review
(executor fills in, per item)
