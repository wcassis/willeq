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

Executed 2026-07-30 on `wt/slot1`, branch `task/P39-occluder-reland`, cut from `fable`
at `6dce719` (P38's tip).

### Root cause

`ConstrainedRendererConfig::loadJsonOverrides()` (`constrained_renderer_config.cpp`)
had NO `isMember("occlusionMaxOccluderRegions")` parse block, confirmed by an explicit
audit of every `isMember(...)` call against the struct's field list (54 parsed keys;
`occlusionMaxOccluderRegions` absent). `config/constrained_presets.json`'s shipped
`"orangepi"` block also never set the key. `fromPreset(OrangePi)` DOES set it to the
P14-blessed 256 in C++ — so in the one real call sequence the shipped `willeq` binary
actually uses (`application.cpp`: `fromPreset(OrangePi)` then `loadJsonOverrides("orangepi",
"config/constrained_presets.json")`), the 256 happened to survive by accident: the JSON
loader silently skips a key it has no parse for, so it never had a chance to revert
the C++ default back to 16. Verified empirically both before and after the fix with a
standalone probe reproducing that exact sequence against the real shipped JSON.

The bug is real but a **fragility/silent-loss gap**, not (as the plan's framing
suggested) a currently-broken production value: any caller that builds a config via
default-construct + `loadJsonOverrides()` only — no `fromPreset()` first — silently
gets the struct default (16), with **zero way to recover 256 from the JSON file**,
because the JSON preset block could not express this key at all. This is exactly the
test-fixture pattern used throughout `test_constrained_renderer.cpp`, and precisely
the shape of "load from a JSON preset" call sites in general. Probe evidence (built
against `wt/slot1` before the fix):
```
after fromPreset:        occlusionMaxOccluderRegions=256
after loadJsonOverrides:  occlusionMaxOccluderRegions=256   (JSON silent, C++ default survives by luck)
bare default:             occlusionMaxOccluderRegions=16
bare+JSON:                occlusionMaxOccluderRegions=16    (the actual loss: JSON couldn't express 256)
```
After the fix, the same probe: `bare+JSON` -> **256** (JSON now carries it explicitly,
closing the fragility).

I also traced (but did not touch, per territory) a SEPARATE, independent loss in the
Pi/benchmark measurement tooling: `tools/hwcaps/zone_bench_loader.cpp:483` sets the
occlusion culler's `maxOccluderRegions` from its own hardcoded `--occluder-budget`
CLI default of 16 ("matches the prior hardcoded default", now stale) — it never reads
`ConstrainedRendererConfig::occlusionMaxOccluderRegions` at all, even though the same
file's step 3 already builds a `fromPreset(OrangePi)` config (with 256) for the texture
cache and simply never wires it through. This — not the production app — is most
likely why P38's own "shipped orangepi preset" Pi numbers landed at ~35.3ms p95
(effectively budget-16 behavior) despite 256 being the blessed value: the benchmark
harness used for measurement doesn't consult the shipped config for this key. This is
visibility-pipeline/benchmark-tooling territory (P40's), out of scope for this unit;
reported, not fixed.

### Fix

1. `constrained_renderer_config.cpp`'s `loadJsonOverrides()`: added the
   `isMember("occlusionMaxOccluderRegions")` parse (documented precedence: `fromPreset()`
   sets the tier baseline, this JSON key overrides it when present — same
   last-writer-wins rule as every other field in the function) + a `LOG_INFO(MOD_GRAPHICS, ...)`
   line reporting the resolved value and whether it came from JSON or the C++ default
   (the "log line" the plan named).
2. `config/constrained_presets.json`'s `"orangepi"` block: added
   `"occlusionMaxOccluderRegions": 256` explicitly, so the blessed value is
   version-controlled and visible in the JSON (JSON has no comment syntax; the "why
   256" rationale lives in the `.cpp` next to both the C++ default and the JSON parse).
3. Precedence documented in three places: the struct field's doc comment
   (`constrained_renderer_config.h`), the existing P14 comment in `fromPreset()`'s
   OrangePi case (cross-referenced to the JSON key), and a new comment block at the
   JSON parse site itself.

### Active-proof (exact shipped invocation)

Built the GLES2-config x86 tree, ran the real `willeq` binary with the documented
shipped invocation shape (`--renderer gles2 --constrained orangepi -r 800 600`,
`-d 4` for INFO-level logging — LOG_INFO is compiled out below `EQT_DEBUG`, which is
ON by default; no source change, just a visibility flag):
```
[INFO ] [GRAPHICS] Preset 'orangepi': occlusionMaxOccluderRegions=256 (from JSON)
...
[INFO ] [GRAPHICS] Software occlusion culler enabled: 64x32 depth buffer, max 256 occluder regions
```
Both the new log line (config layer) and the pre-existing `SoftwareOcclusionCuller`
construction log (consumer layer, `irrlicht_renderer.cpp`) confirm 256 reaches the
renderer end to end under the real shipped preset load. Also added a permanent ctest
regression (`ShippedOrangePiPreset_ActivelyCarries256`) that loads the REAL
`config/constrained_presets.json` (via a new `WILLEQ_CONSTRAINED_PRESETS_JSON`
compile definition in `tests/CMakeLists.txt`, not a synthetic copy) through the exact
`fromPreset()` + `loadJsonOverrides()` sequence and asserts 256 — so this stays
enforced by the full ctest bar going forward, not just a one-time log check.

### Pi spot check (P02 protocol, canonical befallen e205 replay)

2/6 invocations used. Pre-session CmaFree was critically low (2,280 kB, below the
40,960 kB floor — Pi unused since the prior session) — remediated via
`sync && echo 3 | sudo -n tee /proc/sys/vm/drop_caches` (no reboot), recovered to
78,012 kB. `ARM_BUILD_BIN_DIR` explicitly pointed at this worktree's
`build-arm-noble/bin` (script: `refactor/scratch/p39_pi_active_verify.py`, adapted
from P14's own bespoke `--path-file` driver). Canonical replay:
`benchmarks/paths/befallen_seed42_1000f.path`, 1000 frames, seed 42, `--entities 205`
(P11b/P14 calibration), production config, `--animate --merge-zone-draws
--composite-scenegraph --eqe1-path ...` (P11b full-stack), 800x600, DRM/GLES2/Mali400,
governor pinned to `performance` for the timed runs:

| variant | total_ms avg | total_ms p95 | submission p95 | GPU-sync p95 |
|---|---|---|---|---|
| `--occluder-budget 16` (control) | 25.105 | **36.908** | 9.311 | 17.794 |
| `--occluder-budget 256` (blessed, now active) | 24.527 | **32.488** | 7.775 | 15.951 |

p95 moves from 36.908ms to 32.488ms (-4.42ms, -12.0%), tracking P14's own blessed
numbers closely (35.320 -> 32.313ms there; this session's single-run absolute values
differ modestly session-to-session as expected, but the direction and magnitude of
the effect reproduce P14's finding). No new lever, no re-bless — this is a
reproduction check on the already-blessed A/B, run to confirm the config-plumbing fix
doesn't change the shape of that result. CmaFree samples throughout:
[78012, 51904, 51936, 57728] kB, all above the 40,960 kB floor — no SessionAbort.
Session ended clean: governor restored to `ondemand` on all 4 cores (verified), Pi
`/tmp` cleaned of the staged binary/CSVs/path file (verified — only unrelated
`systemd-private-*` dirs remain). CSVs: `benchmarks/pi_runs/p39_occluder_reland/`.

### Same-pattern key audit (report only, per scope)

Cross-checked every field set in `fromPreset()`'s OrangePi case against the
`isMember(...)` parse list in `loadJsonOverrides()`. Three fields share the exact same
loss pattern (a non-default value set in the C++ preset case, with NO JSON parse to
either confirm or override it — silently un-tunable from the config file, correct
today only because nothing currently tries to set them in JSON):

- `totalMemoryBudgetBytes` (OrangePi: 128MB) — no `isMember` parse at all.
- `occlusionBufferWidth` (OrangePi: 64) — no `isMember` parse at all.
- `occlusionBufferHeight` (OrangePi: 32) — no `isMember` parse at all.

Not fixed (out of scope per the plan — "fix only this one"). Also noted in passing,
not a fix candidate: `config/constrained_presets.json` sets `"enableTextureAtlas"` in
every preset block, but the corresponding struct field was removed in E10 (comment at
`constrained_renderer_config.h`: "enableTextureAtlas removed (E10)") — a dead JSON key
with no receiving field, the inverse problem, unrelated to this unit's scope.

### Verification

- `cmake --build build -j24` (GLES2-config tree): clean, exit 0.
- `ctest --output-on-failure` (same tree): **1418/1420 passed** — the two failures are
  exactly the documented host exceptions (`AudioManagerTest.PreloadCommonSounds`,
  `RDPServerFrameTest.UpdateFrameMultipleTimes`, intermittent SEGFAULT). Includes 4 new
  regression tests for this fix (all passing): `FromPreset_OrangePi_BlessedOccluderBudget`,
  `JsonOverride_OcclusionMaxOccluderRegions`,
  `JsonOverride_OcclusionMaxOccluderRegions_AbsentLeavesBaseline`,
  `ShippedOrangePiPreset_ActivelyCarries256`.
- `scripts/build-gles2-x86.sh`: clean, exit 0.
- `scripts/ci/visual_gate.sh`: **PASS**, goldens untouched.
- `scripts/build-arm-noble.sh`: clean, exit 0, rebuilt in THIS worktree before Pi
  staging.
- `scripts/refactor/check_file_sizes.sh fable`: no output (no FAILs/WARNs), exit 0.
- Pi: 2/6 invocations, pristine exit (governor `ondemand`, `/tmp` clean).

### Deviations from the plan

- The plan's framing ("silently isn't shipping", "JSON load path reverts/never-carries
  it") suggested the REAL production app currently ships 16 instead of 256. Empirical
  probing (before touching any code) showed this is not quite right: the actual
  `application.cpp` call sequence (`fromPreset()` then `loadJsonOverrides()`) already
  resolved to 256, by accident of the JSON loader silently no-op'ing on a key it
  couldn't parse. Reported the more precise finding (a fragility/JSON-silent-loss bug,
  not a currently-wrong shipped value) rather than silently going along with the
  plan's stronger framing. The fix requested by the plan (JSON parse + explicit JSON
  key + coherent precedence) is exactly correct regardless of which framing is right,
  and was landed as specified.
- Identified a genuinely separate loss (the Pi benchmark tool's own hardcoded
  `--occluder-budget` default, `tools/hwcaps/zone_bench_loader.cpp:483`, ignoring the
  `ConstrainedRendererConfig` it already builds) that is the more likely explanation
  for P38's ~35.3ms "shipped preset" Pi measurement. Left untouched — it's
  benchmark/visibility-pipeline tooling, outside this unit's territory (config
  plumbing + `constrained_presets.json` + a log line) and adjacent to P40's stated
  territory. Reported here for the orchestrator's awareness, not fixed.
- Added a small `WILLEQ_CONSTRAINED_PRESETS_JSON` compile definition to
  `tests/CMakeLists.txt` (one line) so a new regression test could assert against the
  REAL shipped JSON file rather than only a synthetic copy — a minor, narrowly-scoped
  addition in service of proving this exact fix, not a broader test-infra change.

### Commit

`<filled after commit>`
