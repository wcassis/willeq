# B1008 — four parallel eqemu + official-client oracle lanes, and two x86 CI runners (GitLab #329)

**Class P/B (infrastructure).** **Status: DONE 2026-10-08 — all four lanes proven
with both clients; runner-1 proven on hygiene, build/test/gate result in the
MR.** Companion: `docs/decisions.md` 2026-10-08, the memory note for the next
orchestrator session.

## Why

Four local-model worker chains were to run overnight, and everything live was
single: one eqemu VM with one lock, one Wine oracle on hermes-ext, one CI
runner on hermes-ext (which the 2026-10-06 local-gate rule wanted free for the
board). Will deployed seven Ubuntu 24.04 VMs; this unit provisioned them from
the existing recipes and gave the scripts a lane switch so a card names a lane,
never an address.

## The lanes

| lane | eqemu (akk-stack) | official-client oracle |
|---|---|---|
| 1 | 172.16.200.99 (the original VM) | eq-oracle-1 172.16.200.91 |
| 2 | eqemu-2 172.16.200.98 | eq-oracle-2 172.16.200.92 |
| 3 | eqemu-3 172.16.200.97 | eq-oracle-3 172.16.200.93 |
| 4 | eqemu-4 172.16.200.96 | eq-oracle-4 172.16.200.94 |

- `scripts/eqemu/env.sh`: `EQEMU_LANE=N` resolves `EQEMU_HOST` and
  `EQEMU_ORACLE_HOST`; an explicit `EQEMU_HOST` wins and the lane id follows
  it, so `EQEMU_HOST=local` and an unknown host behave as before.
- `scripts/scenario.sh`: output dir `review/<UNIT>/scenario-lane<N>` for lanes
  2-4 (lane 1 keeps `scenario`), and the client configs are DERIVED from the
  shipped lane-1 files by substituting every `"host"` value, into that dir.
  The first parallel attempt (three lanes, one unit id, shared `scenario/`)
  failed lane 2 with exit 66: its restore compared against another lane's
  snapshot. An explicit `--config` is taken as given and lane-checked.
- `/mnt/projects/eq-oracle/env.sh` takes `EQEMU_LANE` too; each oracle VM's
  copy pins its lane on line 2.

## How the VMs were built (`review/B1008/provision/`)

- **eqemu-2/3/4**: `docker.io` + `docker-compose-v2` from Ubuntu, the
  akk-stack tree streamed from the original VM with `sudo tar --numeric-owner`
  (6.8 GB, mariadb data root-owned), the four images via `docker save` /
  `docker load`, `localaddress` set to the VM, `compose up -d mariadb
  eqemu-server`. The Spire launcher does not start the loginserver;
  `scripts/eqemu/up.sh` does, so a fresh clone reads not-ready until up.sh has
  run once. Stacks are left up; `scenario.sh` takes a lane down at its end.
- **eq-oracle-1..4**: the hermes-ext `wine-oracle.md` recipe (i386 wine32,
  Xvfb, llvmpipe, tesseract, imagemagick), a writable Titanium copy under
  `~/eq-oracle/client` with `eqhost.txt` = the lane and `WindowedMode=TRUE`, a
  win32 wineprefix, the eq-oracle repo at `/mnt/projects/eq-oracle` and
  hermes-ext's step scripts in `~/eq-oracle`. hermes-ext's ssh key is on all
  seven VMs.
- **gitlab-runner-1/2** (10.0.30.81/.82, 16 cores, 15 GB): the x86 Dockerfile
  dependency list plus `freerdp3-dev`, `libwinpr3-dev`, `libasound2-dev`;
  gitlab-runner 19.4.1 with the GitLab CA chain installed; shell executor,
  `concurrent = 1`; registered through the API (runner 5 = `x86-build`,
  runner 6 = `x86-build-2`). Local copies at the NFS paths the jobs expect:
  `EverQuestP1999`, `EverQuest_ETC1`, `irrlicht-1.8.5`, `maps`. No NFS mount.

## What was proven (2026-10-08)

| check | lane 1 | lane 2 | lane 3 | lane 4 |
|---|---|---|---|---|
| `eqemu_stack_ready` after up.sh | yes | yes | yes | yes |
| native `zone_in` scenario (`scripts/scenario.sh`, exit 0, byte-identical restore) | prior art | **0** (after the per-lane fix; 66 before) | **0** | **0** |
| official client under Wine → character select (`drive-login.sh` then `step2,3,4,6`) | **CHARSELECT** | **CHARSELECT** | **CHARSELECT** | **CHARSELECT** |

CI: pipeline 664 on `task/B1008-eqemu-lanes` ran `hygiene` on
gitlab-runner-1 (PASS, 91 s). Its first `build` died in 4 s because the
Irrlicht bootstrap wanted `/mnt/projects/irrlicht-1.8.5`; copied, retried —
the result is on the MR.

## Not verified / left open

- Only runner-1 carries `x86-build`. The stages share one warm tree and pass
  no artifacts, so a second tagged runner would split a pipeline across two
  trees. Folding build+test+gate into one job is the follow-up that lets
  runner-2 work.
- `scripts/eqemu/ready_selftest.sh` was not run on the clones (zone_in exit 0
  and a byte-identical restore were taken as the proof for tonight).
- The oracle VMs reach character select; entering the world and the capture
  scripts' geometry (coordinates in the step scripts are for 1280x1024 Xvfb
  with the 1280x800 window) were not re-measured on the new VMs beyond that.
- Hermes profile caps (4 in progress, 2 per profile) still gate how many dev
  cards run at once; Will raises the per-profile cap and restarts the gateway.
- Trap recorded: `pkill -f eqgame.exe` over ssh kills the ssh shell itself
  (exit 255); use `pkill -x`.
