# T497 — B797/B798: readiness that docker-proxy cannot fake, and a pkill that cannot kill itself

Class A (B797) + B (B798). Lane: **LOCAL eqemu** (`export EQEMU_HOST=local`; clients use
`scripts/scenario_configs/peer.local.json`). Do NOT touch the VM at 172.16.200.99.
Board entries start at **B820**.

## The facts this plan rests on

- [measured] `/mnt/projects/akk-stack/docker-compose.yml:35-46` publishes `9000:9000/tcp+udp`,
  `5998:5998/tcp+udp` and `7000-…/udp` on `${IP_ADDRESS}`. Published ⇒ `docker-proxy` binds the
  host port for as long as the CONTAINER runs, whatever is (not) listening inside it.
- [measured, T493] With the eqemu-server container up and `world`/`zone`/`loginserver` at zero
  processes, `eqemu_port_open 5998` and `… 9000` both return 0.
- [measured] `eqemu_port_open` (`scripts/eqemu/env.sh:71`) is a host-side `/dev/tcp` connect.
  Readiness callers: `scripts/llmbench/record.sh:200`, `scripts/llmbench/f9_cycle.sh:48` (NOT
  named in B797), `scripts/eqemu/status.sh:21`. `scripts/eqemu/up.sh:82,86,110,120` does NOT
  call it — it inlines the same `bash -c "exec 3<>/dev/tcp/…"` four times, so fixing `env.sh`
  alone leaves `up.sh` broken. `scenario.sh:147` runs `up.sh` unconditionally.
- [measured] TCP 5998 on the loginserver is the **servertalk listener for world servers**
  (`/mnt/projects/akk-stack/code/loginserver/world_server_manager.cpp:31-38`); the client's login
  path is UDP 5998 (`client_manager.cpp`). Same split at world: TCP 9000 = zone links, UDP 9000 =
  clients. So the current probe tests a transport no client uses.
- [assumed] Servertalk's first exchange is client-initiated, so a "banner read" on TCP 5998 would
  hang. **Do not build readiness on a protocol handshake.**

## Design — one readiness definition, in `scripts/eqemu/env.sh`

1. Rename `eqemu_port_open` → `eqemu_host_port_reachable`, same body, comment: proves
   REACHABILITY ONLY (docker-proxy answers with the fleet at zero — B797) and must never gate
   readiness. The rename forces every call site to be revisited (4: status.sh, record.sh,
   f9_cycle.sh, restore_selftest.sh's comment).
2. Add `eqemu_container_port_open <port>`: the same `/dev/tcp` probe INSIDE the container's netns
   — `eq_docker exec "${EQEMU_CONTAINER}" bash -c "exec 3<>/dev/tcp/127.0.0.1/$1"`. docker-proxy
   lives in the HOST netns and cannot answer it. [assumed] the image's bash has net redirections;
   fallback `grep -qi ":$(printf '%04X' $1) " /proc/net/tcp` inside the container.
3. Add `eqemu_service_ready <basename> <port>` = `server_process_running <basename> &&
   eqemu_container_port_open <port>`.
4. Add `eqemu_stack_ready` = `eqemu_service_ready loginserver ${LOGIN_PORT} &&
   eqemu_service_ready world ${WORLD_PORT}` (zone stays on `server_process_running zone`; its port
   is UDP and assigned by world).

Call sites: `up.sh` — replace all four inline `/dev/tcp` `wait_for`s (direct and `--launcher`)
with `wait_for "… ready" 30 eqemu_service_ready …`; fix the header's step-5 wording.
`record.sh:200`, `f9_cycle.sh:48`: `if ! eqemu_stack_ready; then …up.sh…`.
`restore_selftest.sh:~212`: its hand-rolled post-up pair becomes `eqemu_stack_ready`.
`status.sh`: per service print `running (N) + listening` vs `DOWN — host port answers
(docker-proxy), nothing is listening inside the container`.

## Tasks

1. **Red first.** New `scripts/eqemu/ready_selftest.sh`, modelled on `restore_selftest.sh`
   (lock `eqemu_lock_take T497`, `--no-lock`, `trap cleanup EXIT INT TERM` running `down.sh` +
   release, outputs to `review/T497/ready_selftest/`): (a) `up.sh`, assert `eqemu_stack_ready`;
   (b) kill the fleet AND the supervisor inside the container via the new `eq_container_pkill`,
   poll `server_process_count` until world+loginserver+zone are all 0 (exit 65 if the launcher
   wins the race — setup failure, not a verdict); (c) assert `compose ps` still shows eqemu-server
   Up and re-measure `eqemu_host_port_reachable 5998/9000` = open (the B797 premise; exit 71 if
   not reproducible); (d) **verdict**: `eqemu_stack_ready` must be FALSE — exit **70** if true;
   (e) re-run `up.sh` and assert ready again. `--legacy` evaluates the OLD port-only definition
   and must exit 70 — the recorded red proof, taken BEFORE task 2 is written.
2. **Implement** the design and re-point every call site (grep must show zero inline `/dev/tcp`
   probes outside `env.sh`). `ready_selftest.sh` then exits 0.
3. **B798.** In `env.sh`, `eq_container_pkill <signal> <ere>`: build the command and guard
   `printf '%s' "$cmd" | grep -Eq -- "$pat" && { echo "REFUSING self-matching pkill pattern:
   ${pat}" >&2; return 2; }` before `eq_docker exec`. Comment names the safe shapes: `down.sh`'s
   `/bin/(loginserver|world|zone)( |$)` and the bracket trick `[s]pire-launcher`. Re-point
   `down.sh`'s two pkills, `restore_selftest.sh`'s step-7 SIGTERM and the new selftest at it.
   [assumed] the guard approximates pkill's per-process argv match with a match against the
   command string — catches exactly the T493 shape; deliberately conservative.
4. **Docs.** CLAUDE.md scenario-lane section: "a port probe is not liveness" (the zero-process
   fact, the TCP-vs-UDP split, the `eqemu_stack_ready` rule, the selftest command, "never write a
   container pkill by hand"). Append one line to T493's Task 5 pointing B797/B798 at T497.
5. **Board.** Strike B797 and B798 with the measured exit codes. New: **B820** (B) every
   readiness probe tests TCP while the CLIENT uses UDP on 5998 and 9000 — even the fixed check
   proves "server links up", not "a client can log in"; **B821** (C) `f9_cycle.sh:48` carried the
   same gate and was missed by B797; **B822** (C) there is no shell test lane — propose
   `scripts/eqemu/*_selftest.sh` as the convention.

## Rules
- LOCAL lock (`eqemu_lock_take T497`); exit 64 = stop; release on every path; `down.sh` at the
  end. No Testchar. No edits to akk-stack server code or the shared oracle client. Private scratch
  `scratchpad/T497/`. Never the VM.
- Commit only scripts, CLAUDE.md, BOARD.md, this plan's Review; evidence to `review/T497/`.

## Review
(executor, 2026-09-17, local lane, branch `task/T497-readiness-probe`)

**The red proof.** [measured] `ready_selftest.sh --legacy` exits **70**: with
the eqemu-server container Up and `world`/`loginserver`/`zone` all at ZERO
processes, the old port-only definition (a host-side `connect()` on 5998 and
9000) still said READY, and both host ports still answered. Recorded BEFORE the
readiness definition was written --
`review/T497/ready_selftest/result_legacy.txt`. The same net against
`eqemu_stack_ready` exits **0** (`result_fixed.txt`): ready after `up.sh`, NOT
ready with the fleet killed, ready again after the second `up.sh`.

**The in-container probe method.** [measured] The eqemu-server image's `bash`
HAS network redirections -- `docker exec ... bash -c 'exec 3<>/dev/tcp/…'`
answers `Connection refused` for a dead port, not "no such file" -- so
`eqemu_container_port_open` uses `/dev/tcp/127.0.0.1/<port>` and falls back to
`grep ':<HEX> .* 0A ' /proc/net/tcp` only if that fails. Both were checked in
the container; the fallback was never needed on this image. [measured] With the
fleet at zero: host 5998/9000 OPEN, container 5998/9000 CLOSED -- the whole
finding in one line.

**What the fix moved.** [measured] `up.sh` now waits a real **2 s** for world's
in-container port after starting it, where it printed `ready (0s)` before.
`grep -rn '/dev/tcp' scripts/` outside `env.sh` returns NOTHING; `eqemu_port_open`
survives only inside comments that say what it was renamed from.

**B798.** [measured] `eq_container_pkill` refuses `/tmp/spire-launcher` with
rc 2 and the message `REFUSING self-matching pkill pattern:`, and accepts
`'/bin/(loginserver|world|zone)( |$)'`, `'[s]pire-launcher'`,
`'[e]qemu-server:launcher'` and `'/bin/zone( |$)'`. Two call sites the plan did
not name -- `camera_match_pose.sh` and `camera_match_sweep.sh`, both bouncing
the zone -- were re-pointed too; `restore_selftest.sh`'s `--legacy` branch was
deliberately left hand-written (it reproduces the defect, and the guard would
refuse it).

**Both selftests coexist, and a real run passed.** [measured]
`scripts/eqemu/restore_selftest.sh --unit T497` exits **0** (restore
byte-identical in 17 s, 0 world/zone processes alive, budget 40 s), and
`scripts/scenario.sh zone_in --config scripts/scenario_configs/zone_in.local.json`
exits **0** (`zoned in to 'qeynos2'` after 3.7 s) through `up.sh`'s new
readiness wait, with the snapshot restored byte-identically.

**Deviations / order.** `eq_container_pkill` and the `eqemu_port_open` rename
(task 3 and half of task 2) were written BEFORE the red run, because the
selftest needs the helper to kill the fleet and needs a name for the old
definition. The readiness definition itself -- `eqemu_container_port_open`,
`eqemu_service_ready`, `eqemu_stack_ready` -- was written only after the red 70
was recorded, which is what the red proof is about. No C++ changed.

**[assumed]** (a) that the guard's command-string match approximates pkill's
per-process argv match closely enough -- it catches the T493 shape and is
deliberately conservative, and a legitimate pattern it refuses is a refusal,
not a kill; (b) that killing `[s]pire-launcher` plus the fleet leaves the
container Up -- it did on every run here (`compose ps` checked each time, exit
71 if not), but it is the image's behaviour, not a contract; (c) that the
zone's readiness is adequately covered by its bootup log line, since its port
is UDP; (d) that 60 s / 120 s are enough for loginserver / world to listen
after a cold start -- measured 0 s and 2 s locally, so the margin is large, but
the VM lane was not exercised (this unit is local-only).

**Also found.** `status.sh` on a live stack reports `loginserver: running (2)`
and `world: running (2)` -- the container supervisor's copy plus `up.sh`'s --
which is B483's territory, not this unit's, and did not affect any result here.
`scripts/scenario_configs/*.local.json` are gitignored and absent in a fresh
worktree; `zone_in.local.json` and `peer.local.json` were created locally
(127.0.0.1) and are deliberately NOT committed.
