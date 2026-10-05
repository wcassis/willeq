# Crowd observation controls

`scripts/b964/pi_crowd.sh` and `scripts/b964/crowd_run.sh` forward `--idle`
and `--hold-after` to the same tour generator. Defaults remain idle 40 seconds,
hold 15 seconds; `--cap` is the finite per-client deadline (Pi default 720,
x86 default 420 seconds). Nonfinite/negative durations and caps that cannot
contain the requested waits fail before acquiring resources. Leave additional
cap headroom for startup, spawning, zone hops and movement.

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
