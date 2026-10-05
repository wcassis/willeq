# Crowd observation controls

`scripts/b964/pi_crowd.sh` and `scripts/b964/crowd_run.sh` forward `--idle`
and `--hold-after` to the same tour generator. Defaults remain idle 40 seconds,
hold 15 seconds; `--cap` is the finite per-client deadline (Pi default 720,
x86 default 420 seconds). Nonfinite/negative durations and caps that cannot
contain the requested waits fail before acquiring resources. Leave additional
cap headroom for startup, spawning, zone hops and movement.

## Accepted duration syntax (B987's QA finding F1)

ONE definition of a valid duration, for `--idle`, `--hold-after`, `--cap`
(both wrappers), `--stall`/`--hold` (`scripts/pi_rendered_zone_sweep.sh`)
and `--build` (`scripts/b973/{crowd_run.sh,pi_crowd.sh}`): a **plain ASCII
decimal in 0..86400** -- `0` or `[1-9][0-9]*` -- the form Bash arithmetic
and `timeout` both accept. That is `scripts/b964/observation_controls.py`'s
`_seconds()` (the Python side) and the `LC_ALL=C grep -qE '^(0|[1-9][0-9]*)$'`
re-check each shell does (the shell side; `LC_ALL=C` because a `[0-9]` range
in a UTF-8 locale matches non-ASCII digits). `0` is only meaningful where it
is today (the test lane); every real workload uses positive values.

Every rejected spelling is a usage error (exit 3 for the wrappers, 64 for
the sweep) **before any lock, stack or Pi resource is acquired**, and the
scripts then consume the validator's CANONICAL printed integers, never the
raw token. The spellings Python's `int()`/`float()` used to accept -- `0_1`,
`1_000`, `0720` (silently octal: 464; the sweep's `timeout $((CAP_S + 120))`
would become 584 while `rendered_zone_run.py` was told 720), `0800` (invalid
octal), a sign, surrounding whitespace, a fraction, scientific notation, a
non-ASCII digit, the empty string, a value over 86400 -- each now fails
closed. Forwarded raw, `0_1` hit the x86 deadline comparison
`[ $((now - start)) -ge "$CAP" ]` with "integer expression expected" and the
overall deadline was silently DISABLED (QA run 306, the executed
reproduction: `--cap 1` stopped in 1.46 s; `--cap 0_1` ran until an external
timeout with 97 comparison errors).

The same validation covers `scripts/b973/crowd_run.sh` and
`scripts/b973/pi_crowd.sh`'s `--build`, which feeds the deadline arithmetic
`$((BUILD_S + 360))` / `cap=$((BUILD_S + 420))` (the same class of defect).

`scripts/pi_rendered_zone_sweep.sh` validates `--cap`/`--stall`/`--hold`
itself for the same reason it must be called directly by other rigs
(`scripts/pi/mesa_ab.sh`, `scripts/b973/pi_crowd.sh`, ...), not only via
`scripts/b964/pi_crowd.sh`.

The regression net is `CrowdObservationControls`
(`scripts/b964/test_observation_controls.py`, ctest label hygiene): every
rejected spelling on both wrappers and on the sweep, the good spellings
accepted, the canonical integer reaching the sweep, the x86 deadline
actually stopping a continuously logging child (the executed form of QA's
probe), and the sweep's self-validation with its real scripts in a sandbox.

For B975-style acceptance, use explicit longer observation on BOTH wrappers:

```sh
EQEMU_HOST=172.16.200.99 bash scripts/b964/pi_crowd.sh \
  --pi-dir /path/to/ci-arm/COMMIT --allow-vm-lane --owner UNIQUE_OWNER \
  --zones qeynos2 --variants imp200far,tagnode200far -r "800 600" \
  --idle 180 --hold-after 75 --cap 720 --frametiming --out /path/to/new/evidence

EQEMU_HOST=172.16.200.99 bash scripts/b964/crowd_run.sh \
  --bin /path/to/ci/willeq --owner UNIQUE_OWNER \
  --zones qeynos2 --variants imp200far,tagnode200far --res 800 600 \
  --char-level 60 --char-exp 616137000 \
  --idle 180 --hold-after 75 --cap 720 --frametiming --out /path/to/new/evidence
```

Since fable ships Lima's BO cache ON by default (B988, #307), a crowd pair
that must be comparable with pre-B988 runs passes the opt-in to the sweep
through `scripts/b964/pi_crowd.sh`'s `--env`:

```sh
  --env WILLEQ_LIMA_NOBOCACHE=1
```

Use the existing lock/guardian/immutable-harness protocol. Never edit scripts
while they run. Neither duration control changes spawn positions, zone hops,
movement or renderer selection. `tagnode` still selects the per-node renderer.

Connection gating does not establish rendered readiness. Every leg writes
`observation.json` and fails unless the FINAL visit reaches the requested
zone's `[memledger] point=ready` and subsequent `steady_60s`. With
`--frametiming`, at least one complete post-ready idle timing block is also
required. Both wrappers propagate failed observation even after client exit 0;
existing Pi memory/GPU/pixel guards are additional, unchanged requirements.

`scripts/b975/frametiming_table.py` restricts timings to the final visit,
after both its last face before movement and rendered readiness, before the
first move. It discards the first overlapping frame/name-tag window. Missing
boundaries or readiness produce empty samples, never whole-run substitutes.
The owner row also comes from the final visit. The verifier can be rerun:

```sh
python3 scripts/b975/frametiming_table.py --check --zone qeynos2 \
  --require-timing /path/to/client.log
```

Longer waits are an observation adjustment, not a waiver. If readiness arrives
too late or never arrives, retain the failure and diagnose it; no indefinite
retry is permitted. Historical B975 run283 failure and run284 recovery remain
in `refactor/B975/README.md`, not new B987 tests.
