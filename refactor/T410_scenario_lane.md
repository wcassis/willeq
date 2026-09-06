# T410 — a live-server scenario lane with exit codes (B138, B139; Will 2026-09-06: build NEW)

Class B (verification infrastructure), the slot that frees; eqemu lock
for the proof run. Read B138 (what the May branch built: `--scenario
<name> --timeout <n>`, a `ScenarioRunner` + registry, an exit-code
taxonomy replacing `main.cpp`'s unconditional `return 0`, a `MOD_SCENARIO`
log module, a precheck→reset→seed→normalize harness) and B139
(`AutomatedMode::setAutomationCallback` — the unreached per-tick hook it
plugs into); T372's §Review; the existing `--script` driver and
`AutomatedMode`; the live rigs under `review/T377|T386|T391|T401/scripts`
(what every live unit re-implements by hand).

Build it NEW (not ported):
1. `src/client/scenario/`: a `Scenario` interface (name, `step(tick)` →
   Running/Pass/Fail with a reason), a registry, a runner driven from the
   automated mode's callback (B139 wired), `--scenario <name>` and
   `--timeout <s>` flags on `Application`; `main.cpp` returns a taxonomy:
   0 pass, 1 fail, 2 timeout, 3 could-not-start (no server/zone), with the
   reason on stderr and in the log under `MOD_SCENARIO`.
2. Three scenarios that replace hand rigs used this week: `zone_in`
   (login → zone → `IsFullyZonedIn`), `two_client_survive` (T391's
   five-minute pair — needs the second client as a peer scenario or a
   flag), `cast_lands` (gem-cast a spell, expect the OP_Action landing —
   T395's keying). Each has a headless unit test on its state machine
   (pre-fix: no scenario exists) and one live PASS run recorded in
   `review/T410/` with the exit code.
3. A harness script `scripts/scenario.sh <name>` that takes the lock,
   `up.sh`, runs, restores the server snapshot (T401's snapshot/restore
   with the world down), `down.sh`, releases — the pieces every live unit
   re-implements.
4. Docs: `CLAUDE.md` gains the lane; DocClaims/DocCounts green. Strike
   B138, B139.

## Proof

Build, `ctest -j6`, the state-machine tests, three live runs with exit
codes, file sizes (every new TU under the ceiling).

STOP: the automated mode's tick cannot host a runner without restructuring
the main loop (report the shape).
