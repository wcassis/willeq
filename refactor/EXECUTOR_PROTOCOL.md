# Executor protocol (standing rules for unit-executor agents)

Established over Q01..P16 (2026-07-24..27). Every executor prompt
references this file; plans carry only unit-specific content.

## Setup
- Work ONLY in the assigned worktree slot (/mnt/projects/fable/wt/slotN).
- CHECKOUT FIRST: `git -C <slot> checkout -b task/<name> fable` — the
  plan file exists only on the fable tip; verify with `git log -1`.
- Read the unit plan in refactor/ as the contract, plus the
  prior-unit reviews it names.

## Hard rules
- Shell discipline (hook-enforced): never chain commands with `&&` or
  `;`; never `cd` — pass paths as arguments (script contents and ssh
  remote strings exempt); never pipe build output through
  grep/tail/tee.
- Run long commands FOREGROUND with explicit timeouts (600000 ms max
  per call); re-run on timeout. NEVER end a turn waiting on a
  background task or monitor — poll output files in bounded foreground
  loops inside a single Bash call (e.g. 60 iterations x 10s). This
  failure mode has stalled 5+ units; it is forbidden.
- Stay out of parallel units' file territory (named in each plan).
- Honor STOP conditions exactly; report narrowed evidence, don't thrash.
- Evidence discipline: garbled/invalid renders void any claim built on
  them; identity-between-paths ≠ correctness; never self-declare
  visual acceptability — Will judges; verify framing/content of any
  visual evidence before calling it deliverable.

## Pi (only if the plan assigns Pi ownership)
- user@10.0.30.192, P02 protocol via scripts/pi/pi_common.py; ALL
  timed-run I/O to Pi-local /tmp only; ARM_BUILD_BIN_DIR must point at
  THIS slot's build-arm-noble/bin (env.sh defaults to main — the trap);
  rebuild ARM before staging; drop_caches never reboot; <=12
  invocations/session (2 sessions OK); leave Pi pristine (governor
  ondemand, /tmp clean); report blocked if unreachable (kmsg log:
  /mnt/projects/fable/pi_console/kmsg_follow.log).
- Canonical replays: all measured moving runs use --path-file with
  benchmarks/paths/*.path. Never --animate-era claims without the
  post-P12b animated path.

## eqemu (only if the plan assigns server ownership)
- LOCK FIRST (cross-session protocol, refactor/TESTING_CHARTER.md):
  before bringing the server up, check `/mnt/projects/fable/.eqemu-lock`
  does not exist; if it does, STOP and report (another session owns the
  server). Otherwise create it with your unit ID + date, and DELETE it
  after clean shutdown. This applies to every server use, no exceptions.
- Sanctioned start only: scripts/eqemu/up.sh / `spire spire:launcher
  start`; NEVER `eqemu-server:launcher` direct. Never modify server
  code. DB reads + documented character-position writes OK (restore
  after: Testchar → zone_id=36, x=-77.9232, y=21.9086, z=3, h=190).
  Clean shutdown (down.sh) + restore configs at end.
- Client: build-gles2/bin/willeq (EGL surfaceless, no X needed);
  account willeq_test / Testchar (GM); EQ data
  /mnt/projects/EverQuestP1999; sheets /mnt/projects/EverQuest_ETC1.
  Assets are read from NFS everywhere (never copy to Pi-local).

## Verification bar (every unit unless the plan narrows it)
1. `cmake --build <slot>/build -j24`
2. `ctest --test-dir <slot>/build --output-on-failure` — known host
   exceptions: AudioManagerTest.PreloadCommonSounds,
   RDPServerFrameTest.UpdateFrameMultipleTimes (intermittent)
3. `bash <slot>/scripts/build-gles2-x86.sh`
4. Visual gate PASS, goldens untouched (unless the plan says a re-bless
   is expected — then STOP for Will, never self-bless)
5. `bash <slot>/scripts/build-arm-noble.sh`
6. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` (no new
   FAILs; put substantial new code in new TUs)

## Finish
- Fill the plan's Review section (evidence, tables, deviations —
  deviations flagged, never silently absorbed).
- Commit on the task branch with the plan's message + trailer
  "Co-Authored-By: Claude Sonnet <noreply@anthropic.com>". Do NOT
  merge, push, or delete branches. Orchestrator reviews and merges.
- Final report: pass/fail per verification step, headline findings,
  commit hashes, deviations; if blocked/STOPped, exactly where and why.
