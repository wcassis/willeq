# P39 — Re-land the blessed occluder-256 (lost under JSON preset load)

**Slot: slot1. OWNS THE PI (P02 protocol, small usage). x86 for
builds. No eqemu.** Sibling: P40 (slot2, visibility investigation) —
do not touch its occlusion-culling ALGORITHM files; yours is config
plumbing.

P38's audit found P14's BLESSED `occlusionMaxOccluderRegions = 256`
(Will: "bless occluder", landed as the C++ orangepi preset default at
`constrained_renderer_config.cpp` ~284, commit 9c5617b) is NOT active
when the shipped `config/constrained_presets.json` preset is loaded —
the JSON load path reverts/never-carries it. The blessed KPI stack
(befallen p95 32.3ms) silently isn't shipping.

## Work

1. Root-cause the exact plumbing: how JSON preset load interacts with
   the C++ preset defaults for this key (P14's Review notes NO
   `isMember("occlusionMaxOccluderRegions")` parse existed). Name why
   the value is lost.
2. Fix properly: add the JSON key parse AND set the key in the shipped
   orangepi preset JSON (both, so config files can tune it and the
   shipped default is explicit + version-controlled). Ensure C++
   default vs JSON precedence is coherent and documented in a comment.
3. Prove active: a config-dump/log line showing 256 reaching the
   renderer under the exact shipped invocation, plus a Pi spot run
   (canonical befallen replay, e205 — the P14 scenario) confirming the
   p95 moves toward the blessed ~32.3ms (vs ~35.3 without). ≤6 Pi
   invocations.
4. Audit for OTHER preset keys with the same C++-default-vs-JSON-load
   loss pattern (compare struct fields vs JSON parse list) — report
   any found, fix only this one.

## Rules
- Territory: constrained_renderer_config parse/preset code +
  constrained_presets.json + a log line. Nothing else.
- This value is ALREADY blessed by Will — no re-bless needed; goldens
  untouched (gate must PASS — occluder count affects culling, and P14
  landed with goldens untouched; verify).
- Pi protocol absolute; pristine exit.

## Verification
- Full bar (GLES2 tree ctest baseline ~1415/1416, gate PASS, ARM
  clean, file sizes) + the active-proof + Pi spot numbers + the
  same-pattern key audit list.

## Review
(executor fills in)
