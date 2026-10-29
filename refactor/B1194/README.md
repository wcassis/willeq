# B1194 — rbuild on the idle gitlab-runner VMs (#513)

The ThinkPad is the one saturated host (Will, feedback 2026-10-29, in #513:
Prometheus 24 h — thinkpad 40 % busy with 100 % peaks, every other host under
10 %); headless renders are CPU-bound, one core each. The two x86 gitlab-runner
VMs (gitlab-runner-1 = 10.0.30.81, gitlab-runner-2 = 10.0.30.82; the B1008
provision records under `/mnt/projects/fable/review/B1008/`) sit almost idle.
This unit makes `scripts/hermes/rbuild` able to target them for `hygiene` and
non-GL `configure`/`--target` steps, keeps the ThinkPad as the host for
GL/visual targets (`model_viewer`, `gles2_zone_benchmark`, `visual_gate`) and
the oracle, and proves the runner side with timings and identical ctest
results. All measurements are 2026-10-29, code at `daceefe2b`.

## Why the inventory and the proof run as CI jobs, not over ssh

The runners answer **no inbound port** from either lab200 host — measured
2026-10-29 from both sides of the firewall, one file per side:

- from hermes-ext (172.16.200.100): TCP 22/2049/9090/9252 to 10.0.30.81 and
  10.0.30.82 all `closed/filtered (timeout 5s)` —
  `/mnt/projects/fable/review/B1194/hermes-ext/network_probes_hermes-ext.txt`
- from the ThinkPad (172.16.200.205): the same four ports to both VMs all
  `closed/filtered` —
  `/mnt/projects/fable/review/B1194/hermes-ext/network_probes_thinkpad.txt`
- the only 10.0.30.x port open to both hosts is the NAS's NFS
  (10.0.30.10:2049); GitLab (10.0.30.72:22) answers but `user` has no key
  there (`Permission denied (publickey,password)`, same files).

The direction is one-way: from the VMs themselves, `tcp 172.16.200.100:2049
(NFS): OPEN` (the VM-side probe in each inventory file below), so a runner VM
CAN pull hermes-ext's worktree export — it just cannot be reached for ssh.
The runner accepts the GitLab job because it POLLS GitLab outbound
(`docs/ci.md:127` names the two runner VMs, their addresses and the B1008
provision). Hence: the inventory and the
proof are self-selecting CI jobs (three copies per host, each a ~1 s no-op on
a non-matching host) that tee their output to the NAS share, which every
lab200 host mounts. The no-op path is visible in the pipeline:
`b1194-hyg-r1-b` ran 1.1 s (wrong host) beside `b1194-hyg-r1-a`'s 103 s
(pipeline 2567, all twelve jobs green).

## (A) The inventory

`refactor/B1194/scripts/runner_inventory.sh` (read-only, no sudo) runs from
`b1194-inv-*`; ThinkPad-side rows were taken with the same commands over
`ssh thinkpad`. Fresh outputs per host, quoted in full by these files:

- `/mnt/projects/fable/review/B1194/gitlab-runner-1/inventory-20261029T161829Z.txt`
- `/mnt/projects/fable/review/B1194/gitlab-runner-2/inventory-20261029T161829Z.txt`

| | ThinkPad | gitlab-runner-1 | gitlab-runner-2 |
|---|---|---|---|
| CPU | i7-9850H @ 2.60 GHz, 12 threads | Xeon E5-2680 v3 @ 2.50 GHz, 16 cores | Xeon E5-2680 v3 @ 2.50 GHz, 16 cores |
| RAM | 46 GB | 15 GB | 15 GB |
| disk `/` | NVMe 915 G (60 % used) | 246 G (74 % used) | 246 G (53 % used) |
| gcc / g++ | 13.3.0-6ubuntu2~24.04.1 | identical | identical |
| clang | absent | absent | absent |
| cmake / ninja | 3.28.3 / 1.11.1 | identical | identical |
| ccache | 4.9.1 (worker-scope dir under `/home/user/willeq-builds/_ccache`) | 4.9.1, 2.9 G of 20 G, 95.42 % lifetime hit rate | 4.9.1, 2.6 G, 95.61 % |
| python3 | 3.12.3 | 3.12.3 | 3.12.3 |
| gitlab-runner | 19.4.1 (runs CI's `hygiene`/`build-test`/`gate`) | 19.4.1, `CI_JOBS=6` | 19.4.1, `CI_JOBS=6` |
| EGL (surfaceless) | llvmpipe (`docs/ci.md`) | llvmpipe (LLVM 20.1.2, 256 bits) | llvmpipe (LLVM 20.1.2, 256 bits) |
| GLES2/Irrlicht deps | host build deps per `docs/thinkpad.md` | every package the script lists is installed (19/19 dpkg rows, quoted in the file) | every package the script lists is installed |
| pkg-config | — | egl 1.5, glesv2 3.2, gbm 25.2.8; `drm` has no `.pc` (CI configures `-DEQT_DRM=OFF`, `docs/ci.md`) | same |
| NFS mounts | NAS `10.0.30.10:/volume2/projects` → `/mnt/projects` (vers=3) + the hermes-ext worktree (nfs4.2, the fstab line below) | NAS → `/mnt/projects` (vers=3, 11 T, 85 % used); the worktree export is NOT mounted yet | same |
| `/home/user/src/willeq-wt` | mounted (same path, B1087) | **absent** — one root action per VM, below | **absent** |
| `/home/user/willeq-builds` | present | present (B1008's provision created it) | present |
| `sudo -n` as the CI user | n/a (workers ssh in as `user`) | NOT allowed for `gitlab-runner` (`sudo: a password is required`) | NOT allowed |

The toolchain match is byte-identical on the compiler side (same gcc build
string, same cmake/ninja/ccache versions), so a `-j6` non-GL build is
like-for-like. `CI_JOBS=6` is the runners' own parallelism, which the proof
script mirrors with `JOBS=6` (rbuild's `RBUILD_JOBS` default).

## (B) rbuild host selection + the mounts

### What changed in `scripts/hermes/rbuild`

- `RBUILD_HOSTS` (env, with a default at
  `scripts/hermes/rbuild:79`): a comma list of
  `name=ssh-target[:builds-root]` — the ThinkPad plus `runner-1` /
  `runner-2`, each with its own builds root (`/home/user/willeq-builds`
  everywhere, so the per-host tree layout is identical).
- `--host <name|auto>` (accepted anywhere in the arguments; `RBUILD_HOST`
  env, default `thinkpad`): explicit selection. `auto` probes every host's
  `/proc/loadavg` over ssh in parallel (15 s budget) and picks the LEAST
  1-minute load among the REACHABLE hosts; a filtered host is skipped, never
  waited on, and ties go to the earlier entry (`scripts/hermes/rbuild:345`,
  `resolve_host`).
- **GL/visual pin** (`scripts/hermes/rbuild:87`): `model_viewer`,
  `gles2_zone_benchmark` and `visual_gate` build on the ThinkPad only — the
  script checks the resolved ssh target, refuses elsewhere with a message
  naming the pin. The oracle is untouched (it is not an rbuild target).
- `hygiene` is now timed like `build` (both print `… exit N in S s on <host>`).

Measured on 2026-10-29: `rbuild --host auto --status` probed all three
hosts, skipped the two filtered runners and selected the ThinkPad in 8.4 s
(the reachable-host path of `auto`); `rbuild <wt> --host runner-1 --target
model_viewer` dies with the GL-pin message before doing any work; a raw
`RBUILD_HOSTS=` fallback to a bare ssh target (the pre-B1194 shape) still
works.

### The verbatim config lines

**hermes-ext** — `/etc/exports.d/willeq-wt.exports` (CHANGED 2026-10-29, the
only shared-host export touched; the previous one-client line is quoted under
it). Active since, verified by `exportfs -v` showing the three client entries:

```
/home/user/src/willeq-wt 172.16.200.205(rw,sync,no_subtree_check,all_squash,anonuid=1000,anongid=1000) 10.0.30.81(rw,sync,no_subtree_check,all_squash,anonuid=1000,anongid=1000) 10.0.30.82(rw,sync,no_subtree_check,all_squash,anonuid=1000,anongid=1000)
# previous line, before B1194:
# /home/user/src/willeq-wt 172.16.200.205(rw,sync,no_subtree_check,all_squash,anonuid=1000,anongid=1000)
```

**each runner VM** — `/etc/fstab` line to be ADDED by
`refactor/B1194/scripts/setup_rbuild_host.sh` (idempotent; the ThinkPad's own
line, captured verbatim 2026-10-29 via `ssh thinkpad 'grep willeq-wt
/etc/fstab'`, so the mount path and options are byte-identical to the
existing B1087 mount):

```
172.16.200.100:/home/user/src/willeq-wt /home/user/src/willeq-wt nfs vers=4.2,proto=tcp,soft,timeo=150,retrans=3,actimeo=1,noatime,nofail,_netdev,x-systemd.automount,x-systemd.idle-timeout=0 0 0
```

plus `loginctl enable-linger user` and
`mkdir -p /home/user/willeq-builds/_ccache /home/user/willeq-builds/_irrlicht`
(same script). **NOT applied yet**: the VMs answer no inbound ssh from either
lab200 host (above) and the CI user cannot `sudo -n` (inventory files), so the
script is staged in this MR for a one-time root run per VM — see the RULING
REQUEST. The hermes-ext export is already live for both VM IPs, and the VMs
can already reach it (`tcp 172.16.200.100:2049: OPEN`, quoted above).

### The CI include

`.gitlab-ci.yml` gained one line, `include: - local:
'refactor/B1194/ci/b1194.yml'`. That file defines the self-selecting jobs
(`b1194-inv-*`, `b1194-hyg-*`, `b1194-proof-*`): each job no-ops in ~1 s unless
its hostname matches, is `allow_failure: true` (a bad proof never sinks the
pipeline; the real `hygiene` job stays the gate), and is `rules`-pinned to
pushes on THIS branch only, so the include is inert on `fable` and every
other branch. The CI-shape tests (`tests/cmake/hygiene.cmake`'s
`CiQueueRules`/`CiBuildTreeStamp`) anchor on the existing jobs only and pass
unchanged (pipeline 2567 green).

## (C) The proof: hygiene + `test_packet` on a runner VM, with timings

Runner side: `b1194-hyg-*` runs `refactor/B1194/scripts/hygiene_proof.sh`
(rbuild's exact non-GL lines: same configure flags, `ctest -L hygiene -j 6`)
and `b1194-proof-*` runs `refactor/B1194/scripts/build_proof.sh` (Irrlicht
bootstrap if needed, same configure, `cmake --build -j 6 --target
test_packet`, `ctest -R PacketTest -j 6`), both teed to the share:

- runner-1: `/mnt/projects/fable/review/B1194/gitlab-runner-1/hygiene_proof-20261029T161833Z.txt`,
  `/mnt/projects/fable/review/B1194/gitlab-runner-1/build_proof-20261029T162020Z.txt`
- runner-2: `/mnt/projects/fable/review/B1194/gitlab-runner-2/hygiene_proof-20261029T161834Z.txt`,
  `/mnt/projects/fable/review/B1194/gitlab-runner-2/build_proof-20261029T162021Z.txt`
- ThinkPad: `/mnt/projects/fable/review/B1194/hermes-ext/thinkpad_rbuild_proof.txt`
  (the `rbuild` runs themselves) and
  `/mnt/projects/fable/review/B1194/hermes-ext/thinkpad_ab_fresh_tree.txt`
  (the fresh-tree A/B, same steps as the runner jobs, so the numbers below are
  like-for-like)

| step (-j6, ccache, `daceefe2b`) | ThinkPad (fresh tree) | runner-1 (CI job) | runner-2 (CI job) |
|---|---|---|---|
| Irrlicht bootstrap | 48 s (user-scope ccache mostly cold) | 9 s (stamp warm) | 9 s (cold tree, CI ccache 95.61 % hits) |
| configure (Release GLES2) | 6 s | 9 s | 10 s |
| `--target test_packet` | 14 s | 16 s | 15 s |
| `ctest -R PacketTest` | **24/24 pass**, 1 s | **24/24 pass**, 0 s | **24/24 pass**, 0 s |
| `ctest -L hygiene` | **49 tests, 0 failed**, 279 s wall (1 skipped) | **49 tests, 0 failed**, 94 s wall (2 skipped) | **49 tests, 0 failed**, 99 s wall (2 skipped) |

**Same ctest results, with the skips accounted for.** All three sides: 0
failures. The skip SETS differ by host, and every difference is a documented
host pre-condition, not a result difference:

- the runners skip `OracleLocateTemplates` and `OracleGlyphReader` — both
  `SKIP_RETURN_CODE 125` without the B1058/B1059 review corpus or
  NumPy/Pillow, and `tests/cmake/hygiene.cmake:576-593` says exactly that the
  CI runners have neither;
- the ThinkPad skip (in the `rbuild hygiene` run) is
  `MemoryCapHuntScript` — `tests/cmake/hygiene.cmake:396-398` SKIPs (125) when
  `systemd-run --user --scope` is not usable, which is the case inside the
  rbuild ssh session; the runners, full VMs, run it (`Passed 0.39 sec`);
- the ThinkPad's 279 s hygiene wall is `OracleLocateTemplates` alone
  (`274.59 sec`); without it the three sides are within seconds of each other.

The runner totals (hygiene 101 s / 103 s; build proof 25 s / 34 s) are the
whole job, including the no-op host checks and the tee.

## Acceptance

- (A) inventory table — above, with the three source files.
- (B) host selection + mounts — the `scripts/hermes/rbuild` diff, the
  verbatim export (applied) and fstab (staged in
  `refactor/B1194/scripts/setup_rbuild_host.sh`, awaiting root per the
  RULING REQUEST) lines above.
- (C) hygiene + one non-GL target on a runner VM, timed, identical results —
  above.
- (D) this README; the docs touched are `docs/thinkpad.md` (the export is no
  longer "to this box only" + a host-selection bullet under the B1087
  section).
- (E) this MR closes #513; scripts + docs only, no C++.

## RULING REQUEST

1. **One-time root setup per VM**: run
   `refactor/B1194/scripts/setup_rbuild_host.sh` as root on gitlab-runner-1
   and gitlab-runner-2 (adds the fstab line + the two build dirs + linger;
   touches nothing of the gitlab-runner CI config). The CI user cannot
   `sudo -n` (measured), and there is no inbound ssh, so this has to be a
   human action (or a key + sudoers grant).
2. **Firewall**: allow inbound TCP 22 from 172.16.200.100 (hermes-ext) to
   10.0.30.81/.82 (optionally 2049 for direct NFS pulls) so that
   `rbuild --host runner-N` and `--host auto` reach the VMs directly and
   `--host auto` can actually balance work onto them. Until then `auto`
   skips them (measured: the ThinkPad is picked in 8.4 s) and the CI-job
   path above covers the proof. Nothing in this MR depends on the firewall
   change; it only makes the selector's runner entries reachable.

## CLAUDE.md EDITS

None.

## FOLLOW-UP

- after the firewall opens: re-run `rbuild --host auto --status` (the runners
  should then appear in the probe) and one `rbuild --host runner-1 hygiene`
  end-to-end over ssh, and retire the `b1194-*` CI copies (or keep the
  hygiene proof as a standing cross-host canary).
- the worker cards' BUILD RULE names the ThinkPad specifically; wiring
  `--host auto` into them is a separate card once the RULING lands.
- `/home/user/willeq-builds/b1194-ab-291626/` on the ThinkPad (the A/B tree)
  is prunable.
