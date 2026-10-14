# B1016 — the nollvm prefix check tests `lib/` itself, and the gate proves the [gldriver] line (GitLab #337)

Branch `task/kb-b1016-b1017-gldriver-prefix-eqe1-verify` (the B1017 half shares
this branch — `refactor/B1017/README.md`).

## The problem

B946 (#264) added the nollvm decision: `glPrefixExpected()` returns the prefix
the publish step baked into the binary's DT_RPATH (it writes
`<prefix>/lib`), and `mesaPrefixMissing(prefix)` decides the ERROR variant.
The decision ran on `fs::is_directory(prefix)`. But
`nollvmPrefixFromRpath()` — which produces the argument — returned the rpath
entry with a trailing `/lib` STRIPPED, so the composed check tested the
PREFIX DIRECTORY, not the directory `ld.so` actually searches. A PARTIAL
deploy (the prefix dir present, its `lib/` lost or never synced — exactly the
`rsync --delete` failure shape) passed the check, no ERROR was logged, and
`ld.so` silently fell through to the system Mesa: the defect B946 existed to
catch, still invisible on the hand-run path.

The CI `gate:` job also never exercised the line: `visual_gate.py` runs
`gles2_zone_benchmark`, which did not emit `[gldriver]` at all (only the client
did), so the one real GLES2 run in CI had no driver line to check — the
parser (`mesa_driver_field`) could drift from the emitter with no net.

## What changed

- **`nollvmPrefixFromRpath()` returns the rpath entry AS-IS**
  (`src/client/core/gl_driver_census.cpp`): the publish bakes in
  `<prefix>/lib`, so the composed check tests the `lib/` directory itself.
  A partial deploy that lost only `lib/` is now caught; a fully-present
  prefix still passes. The existing `NollvmPrefixFromRpath` test was updated
  to the new contract.
- **The ERROR fires before the INFO line**
  (`src/client/graphics/renderer/renderer_init.cpp`): a hand run on a broken
  deploy reads the ERROR first, not the driver line that describes the
  fallback it just fell into.
- **The benchmark emits the same one-time line**
  (`tools/hwcaps/zone_bench_loader.cpp`, `src/client/core/gl_driver_census.cpp`
  linked into `gles2_zone_benchmark`): after `createDeviceEx`, the same
  ERROR-before-INFO sequence the client runs. The gate now has a real GLES2
  run with a real line per zone.
- **`visual_gate.py` asserts it** (`assert_gldriver_line`): every zone's
  benchmark stdout must contain a `[gldriver]` line that
  `scripts/pi/rendered_zone_summarize.py`'s `mesa_driver_field()` parses —
  presence + parse, no threshold (the line's values are evidence, not a
  budget). Missing or unparseable is a gate failure like any other.
- **The emitter and parser are un-divergible** (`tests/gldriver_line_emit.cpp`
  → `GldriverLineFormatContract` ctest): the tool prints
  `makeGlDriverLine()` for CLI-supplied fields; the Python test feeds the
  real emitter output through the real parser and asserts the round trip
  (realistic line, no-LLVM answer, a single-quote renderer that pins the
  format's LIMIT, a log-prefixed line, and the absent line → empty).

## Evidence

- **Red on `origin/fable`** (base worktree at `06852f31`, new tests seeded
  without the fixes): `GlDriverCensus.ComposedNollvmCheckCatchesLibMissingButPrefixPresent`
  **FAILED** — `nollvmPrefixFromRpath` returned the parent (`...mesa-nollvm-9.9`)
  instead of `.../lib`. Output: `fable_red_run.log` section (a), and
  `red_a_composed.txt`.
- **Green (branch tip):** `test_gl_driver_census` 19/19, including the new
  composed-check test and `RealRunPrintsTheGlDriverLine` (spawns the real
  `willeq` with `--renderer gles2 --no-audio` and requires the `[gldriver]`
  line on stdout — the acceptance criterion's literal ask; the real-run leg
  is `#ifdef EQT_HAS_GRAPHICS` because B1038's `build-headless` CI job runs
  this suite against a graphics-OFF `willeq` that can never do GL init and
  emit the line — the gate keeps the leg live in every graphics tree);
  `GldriverLineFormatContract` PASS; the benchmark's own run prints the line
  (line 56 of a 1-frame headless run, per the identical local run) [measured].
- **The gate assertion** was exercised directly against three cases (a real
  parsed line, an absent line, an unparseable line) — `test_gate_gldriver.py`
  in the evidence dir, all pass. A full `visual_gate.py` run over all five
  zones is the CI `gate:` job's first run on this branch.

## Not verified / left open

- **A real `willeq-nollvm` on the Pi** is out of scope (no Pi lane on this
  card): the composed check and its net are x86-tested; the Pi sweep's
  `mesa_mismatch` (exit 67) remains the live-deploy proof.
- `renderer` still carries whatever `GL_RENDERER` says verbatim; a renderer
  string containing an unbalanced single quote would break
  `mesa_driver_field` — the format contract test pins this as a known LIMIT
  rather than a fix (no renderer string observed with a quote).

## FOLLOW-UP:

- Rebuilding the NFS `soldunga.eqe1` (#248 acceptance 2) and any
  Pi/setcap live-deploy run are the orchestrator's, per the card's
  OUT OF SCOPE.
- The root CLAUDE.md's B930 paragraph (line 317 at the branch tip) says
  "`--require-v3` ... (STALE on any v1/v2, exit 1)", which B1017's
  semantics supersede — the protected file is edited by the orchestrator
  at merge from `CLAUDE.md EDITS:` below.

## CLAUDE.md EDITS:

Replace the root CLAUDE.md's line

```
(STALE on any v1/v2, exit 1). Pinned by test_eqe1_stale_pack and
test_eqe1_verify_stale_pack.
```

with

```
(STALE on a v1/v2 pack that HOLDS A 31-CHAR NAME — the truncation
signature of a longer original; a structurally-clean v1/v2 pack whose
names all fit prints OK, and a 31-char `_alpha` companion whose base
entry is present and shorter is a complete name — B1017, #338; exit 1
on any candidate). Pinned by test_eqe1_stale_pack and
test_eqe1_verify_stale_pack.
```
