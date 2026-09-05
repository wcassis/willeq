# Executor protocol (standing rules for unit-executor agents)

Established over Q01..P16 (2026-07-24..27). Every executor prompt
references this file; plans carry only unit-specific content.

## Setup
- Work ONLY in the assigned worktree slot (/mnt/projects/fable/wt/slotN).
- CHECKOUT FIRST: `git -C <slot> checkout -B task/<name> fable` — the
  plan file exists only on the fable tip; verify with `git log -1` that
  the tip hash matches the one named in your dispatch. STOP if not.
- Never rely on the harness `isolation: worktree` — it forks from `main`,
  not `fable`. Always `checkout -B task/... fable` in your slot and verify
  the tip.
- Read the unit plan in refactor/ as the contract, plus the
  prior-unit reviews it names.

⚠ Configure the slot against ITS OWN `third_party/irrlicht-gles2-x86`
(`-DEQT_GLES2_IRRLICHT_DIR=<slot>/third_party/irrlicht-gles2-x86`). Check
`grep EQT_GLES2_IRRLICHT_DIR <slot>/build/CMakeCache.txt` at checkout: a cache
pointing at `main/` breaks the moment main rebuilds its Irrlicht (T357).

## Hard rules
- Shell discipline (hook-enforced): never chain commands with `&&` or
  `;`; never `cd` — pass paths as arguments (script contents and ssh
  remote strings exempt); never pipe build output through
  grep/tail/tee.
- Run long commands FOREGROUND with explicit timeouts (600000 ms max
  per call); re-run on timeout. **A timeout can kill a build MID-LINK and
  leave test binaries silently TRUNCATED — ctest then dies at discovery
  with a shell-parse error, not a test failure (T293). Verify a build by
  its EXIT CODE, never by grepping its log: `cmake --build ... > log 2>&1;
  echo EXIT=$?` — and note `grep ' error '` NEVER matches GCC, which prints
  `error:`.**
  ⚠⚠ **AND THE RE-INVOCATION AFTER A TIMEOUT LIES TO YOU (J07, 2026-09-02).**
  After a build is killed at the 600 s ceiling (exit 143), the second
  `cmake --build` **exits 0 while binaries are still TRUNCATED mid-link** —
  cmake does not rebuild them, because the partial file is NEWER than its
  inputs. J07 had **twelve** truncated binaries behind a clean exit 0. Worse,
  **`ctest` reports only the FIRST discovery failure**, so fixing them one at a
  time costs a full suite run each. **After ANY exit-143 build, scan for
  truncation in one pass before trusting the rebuild** — check every produced
  binary for a valid ELF magic (`\x7fELF`) and non-trivial size, and delete +
  rebuild every one that fails. This is the T293 trap, and it is worse than
  T293 recorded.
  ⚠⚠ **THE SCAN HAS A HOLE: it validates artifacts that EXIST and is blind to one
  that is ABSENT (orchestrator, 2026-09-02).** A killed `ar` DELETES the archive
  outright, so `readelf -h` over `build/bin/*` plus `ar t` over `lib*.a` reported
  **"221 binaries + archives, bad=0" while `build/lib/libwilleq_core.a` was
  missing entirely** — a glob cannot match a file that is gone. **Assert the
  artifacts you EXPECT are present FIRST, then validate them.** Absence and
  corruption are different failures with the same trigger, and only the second
  is caught by a scan over what you find. ⚠ **A THIRD shape (G03, 2026-09-04): the
  archive can be PRESENT and non-empty with ZERO members** — the link then fails with
  dozens of undefined references that look like a source defect. Check
  `ar t <archive> | wc -l` per archive, never just its presence or size.
  ⚠ Related but DIFFERENT: a `ctest` **discovery-stage** failure
  ("Process terminated due to timeout" while listing a test's cases, or exit 8
  in `GoogleTestAddTests.cmake`) can also be plain cold-cache contention with an
  intact binary — J05 and J07 each hit one. Check the binary
  (`--gtest_list_tests`) before concluding truncation. **Two different discovery
  failures, two causes, neither is a test failure.**
  ⚠ **A THIRD cause (T323, 2026-09-02): `ctest` launched on top of a still-running
  `cmake --check-build-system` reads the 174 `*_include.cmake` discovery shims
  MID-WRITE and dies with "Maximum recursion depth of 1000 exceeded" in ~60 s,
  naming an innocent test.** No binary is damaged; waiting for the real exit file
  and re-running is sufficient. Root cause is the same class as the shared-scratchpad
  trap: trusting an exit file that did not exist because the `cat` was chained and
  its stderr lost. **Never start a test run against a build you have not confirmed
  finished by reading a file you know was written by THAT run.** NEVER end a turn waiting on a
  background task or monitor. This failure mode has stalled 5+ units
  (T128 alone stalled 3 times in one unit); it is forbidden. Use
  `scripts/wait_for.sh` instead of writing your own polling loop — it
  is a bounded, re-invokable, dependency-free poller built for exactly
  this (T129):
  ```
  scripts/wait_for.sh --exit-file <path> [--interval 10] [--deadline 240] [--label NAME]
  scripts/wait_for.sh --file <path> --contains <text> [--interval 10] [--deadline 240] [--label NAME]
  ```
  Exit 0 = condition met; exit 2 = deadline reached, still running (not
  an error — just call it again); exit 1 = usage error; exit 3 = target
  path unusable. It prints elapsed time, file size, and a tail on every
  exit, so you can act without a second command. Deadline is capped at
  540s so a single invocation can never approach the Bash tool's 600s
  ceiling. `scripts/wait_for.sh --help` for full usage.
- ⚠⚠ **THE SESSION SCRATCHPAD IS SHARED ACROSS SLOTS AND ACROSS EARLIER
  SESSIONS — same hazard class as the git stash (J20, 2026-09-02).** J20's
  `wait_for.sh --exit-file .../gles2.exit` returned **MET, exit 0, after 0
  seconds** against a file written at 01:56 by an UNRELATED earlier run, while
  its own GLES2 build was still compiling — so it launched the visual gate on
  top of an unfinished build and had to redo the leg. **Generic names
  (`build.log`, `gate.exit`, `ctest.log`) are guaranteed collisions in a
  three-slot wave.** Always `rm -f` the exit file immediately before launching
  the thing that writes it, or use a unit-private subdirectory, AND sanity-check
  the elapsed time `wait_for.sh` reports — a 0-second MET is a lie, not a fast
  build.
- Stay out of parallel units' file territory (named in each plan).
- **NEVER use `git stash`. THE STASH STACK IS SHARED ACROSS ALL WORKTREES** —
  it lives in the common `.git`, exactly like the eqemu lock, and slots are NOT
  isolated from each other on it. T306 (2026-08-28) pushed a stash, another slot
  pushed one in between, and T306's `git stash pop` pulled the OTHER unit's work
  into its tree and dropped that entry: race_models.json, willeq.db, race_codes,
  entity_renderer, plus a stray 644->755 mode change on `data/willeq.db`. It was
  recovered by hand (`git stash store <sha>`) and cost most of an hour. **A pop
  can silently take another agent's uncommitted work.** To stage a
  revert-and-prove step, export a patch (`git diff > /tmp/.../x.patch`,
  `git apply -R`, `git apply`) or write your own patch script — never the stash.
  ⚠ **A FOURTH shape (T353, 2026-09-04): a binary can be NON-EMPTY yet truncated
  non-ELF, newer than its inputs, so the incremental rebuild keeps it and ctest
  aborts at gtest DISCOVERY with a shell syntax error.** The zero-byte sweep does
  not see it. After any exit 143 scan `build/bin` for the ELF magic (`head -c4 |
  od`; anything not `7f 45 4c 46` is deleted and rebuilt).

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
  take `/mnt/projects/fable/.eqemu-lock` ATOMICALLY — check-then-create is
  a race (T224 clobbered T222's lock 90s after seeing it absent). Use:
  `bash -c 'set -o noclobber; echo "T### $(date -I)" > /mnt/projects/fable/.eqemu-lock'`
  — exit 0 means you own it; non-zero means another session does: STOP and
  report, never overwrite. DELETE it after clean shutdown. This applies to every server use, no exceptions.
- Sanctioned start only: scripts/eqemu/up.sh / `spire spire:launcher
  start`; NEVER `eqemu-server:launcher` direct. Never modify server
  code. DB reads + documented character-position writes OK (restore
  after: Testchar → zone_id=36, x=-77.9232, y=21.9086, z=3, h=190).
  **⚠ DO NOT ASSUME Testchar IS AT THAT POSE — SELECT IT FIRST.** T311 Leg A
  (2026-09-02) found it at `zone_id=2, x=-74, y=428, z=3.751, h=160`
  (T310 Leg C's qeynos2 pose) on arrival, and left it as found. The line
  above is the pose to restore to when YOU move it, not a guarantee of the
  pose you inherit. Boarded in `refactor/BOARD.md`.
  Clean shutdown (down.sh) + restore configs at end.
- Client: build-gles2/bin/willeq (EGL surfaceless, no X needed); ALWAYS pass
  `--eqe1-path` on any run whose pixels will be judged — the S3D path thrashes
  the constrained texture budget and paints placeholders (T226, not a defect);
  account willeq_test / Testchar (GM); EQ data
  /mnt/projects/EverQuestP1999; sheets /mnt/projects/EverQuest_ETC1.
  Assets are read from NFS everywhere (never copy to Pi-local).

## Verification bar (every unit unless the plan narrows it)
1. `cmake --build <slot>/build -j24` — `<slot>/build` is the GLES2-config
   tree (P30, `refactor/desktop_gl_elimination_plan.md`): configured with
   `-DEQT_GRAPHICS=ON -DEQT_GLES2=ON -DEQT_DRM=OFF
   -DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86
   -DEQT_BUILD_TESTS=ON` (bootstrap the patched Irrlicht once per worktree
   with `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only` if
   `third_party/irrlicht-gles2-x86/` is absent). Desktop-GL Irrlicht
   (system `libirrlicht-dev`/X11-GLX) is no longer a supported configure —
   there is no fallback.
2. `ctest --test-dir <slot>/build --output-on-failure` (same GLES2-config
   tree as step 1) — known host exceptions:
   AudioManagerTest.PreloadCommonSounds. (T35, 2026-08-03: the
   `RDPServerFrameTest.UpdateFrameMultipleTimes` "intermittent" waiver is
   RETIRED — it was masking a real use-after-free in `RDPServer::stop()`
   [`Close()` before `join()`; T03b root-caused it, 12 kernel GP faults at
   an identical libwinpr3 offset]. Fixed by reordering to join-then-close;
   see `refactor/T35_rdp_stop_uaf.md` Review for the repro/verification
   evidence. A second, unrelated, non-crash flake in the same test family
   [parallel `ctest -j` processes racing on a fixed RDP listener port] was
   found and fixed in the same unit. A future RDP segfault is a real bug,
   not a flake — do not re-waive without root-causing it.) **The suite
   grows constantly — 3208 at T293 (2026-08-27), 2745 at T214, 1234 at P27.
   Do NOT treat any count written here as current: read the header block of
   `refactor/BOARD.md`, or simply run it. The only invariant is that the
   ALSA quirk above is the sole expected failure.**
3. `bash <slot>/scripts/build-gles2-x86.sh`
   ⚠ A `kWilleqDbExpectedVersion` bump (a rebuilt `data/willeq.db`) invalidates
   EVERY build tree, not just `build/`: a stale `build-gles2` binary refuses the
   new db silently and the gate fails with `characterModelsBytes mismatch`
   (identical deficit in both zones — T304, T355). Rebuild `build-gles2` AND
   `build-arm-noble` before reading the gate.
4. Visual gate PASS, goldens untouched (unless the plan says a re-bless
   is expected — then STOP for Will, never self-bless)
5. `bash <slot>/scripts/build-arm-noble.sh`
6. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` (no new
   FAILs; put substantial new code in new TUs). **Run it from YOUR slot
   on your task branch** — it checks what changed vs the base rev, so
   from `main` or a slot detached at `fable` there is nothing to check.
   Since T37 that case says so out loud (`nothing was checked, this is
   not a pass`) instead of printing nothing and exiting 0; if you see
   that line, your run proved nothing — re-run it correctly or use
   `--files` with absolute paths. A bad base rev is now an error, not a
   silent pass.

## Media for Will's review (screenshots, video, contact sheets)
- **FORCE DAYLIGHT BEFORE CAPTURING ANYTHING IN A LIVE SESSION.** Send the
  GM command **`/say #set time 11 0`** — space-separated hour and minute.
  Three units (T63, T74, T75's first attempt) shipped captures too dark to
  judge, each costing a re-run; this is one command.
  - The syntax matters. `#set time 11:00` does **not** parse —
    eqemu's handler reads two separate numeric tokens and `"11:00"` fails
    `IsNumber()`, silently printing usage instead. `#settime 12` is a
    different, **nonexistent** command spelling; a 2026-07 session used it,
    saw a crash, and wrongly concluded the whole approach was unsafe.
  - T75 verified the correct form early in a throwaway run — 112 frames
    over ~10s, no crash — then used it for every capture. **Do the same:
    confirm the client survives a few seconds of rendering after the
    command BEFORE building a capture sequence on it.** Discovering a
    problem with half your captures made wastes the session.
- **Everything Will has to look at goes to
  `/mnt/projects/fable/review/<UNIT>/`** — e.g. `review/T62/`. Not
  `refactor/artifacts/`, not your worktree, not `/tmp`. This is an
  explicitly sanctioned path outside the worktree, the same way the Pi
  paths are; it is the ONE exception to stay-in-your-slot for writes.
- It is outside the git checkout on purpose: media does not belong in the
  repo. Do not commit captures unless they are a golden or a regression
  fixture that genuinely needs versioning. Findings and conclusions go in
  the plan's Review section; the pixels go in `review/`.
- **Write a `README.md` in your unit's review directory**: what each file
  shows, what changed, and what specifically to look at. A folder of bare
  PNGs makes Will do the archaeology.
- Self-describing filenames (`inventory_paperdoll_equipped_800x600.png`,
  not `capture3.png`); `pi/` subdirectory for real-hardware captures;
  `before/` + `after/` when the unit changes something visual, captured
  identically — a pair that differs in two ways proves nothing.
- Never self-assess visuals. Capture it, say what it is, stop. Will
  judges. Garbled or unverified renders void any claim built on them —
  check the file is what you think it is before calling it deliverable
  (identical byte sizes across supposedly-different captures is the
  classic tell).
- Name the review paths in your final report so the orchestrator can
  hand Will the link directly.
- **When the thing under review is temporal** (a weapon appearing mid-swing,
  a cast, a zone transition, particle behavior) **record video rather than
  burst-capturing stills and hoping a frame lands.** T64 productised this:
  `--dump-frames-dir <dir> --dump-every <N> --dump-max-frames <N>` (off by
  default, bounded, `Application::render()`) + `scripts/ci/encode_capture.sh`
  (frame dir -> mp4, fps **computed from frame mtimes, never assumed**).
  Every capture video must carry its measured fps/frame-count/real-time span
  (the encode script writes this to a `.txt` next to the video) and the
  honesty note that capture fps is an artifact of the capture, not a
  performance measurement — see `CLAUDE.md`'s "Video capture" section and
  `review/T64/README.md` for the worked example.

## Finish
- Fill the plan's Review section (evidence, tables, deviations —
  deviations flagged, never silently absorbed).
- Commit on the task branch with the plan's message + trailer
  "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>". Never put
  backticks in a heredoc'd commit message — bash expands them and
  silently mangles the commit. Do NOT
  merge, push, or delete branches. Orchestrator reviews and merges.
- Final report: pass/fail per verification step, headline findings,
  commit hashes, deviations; if blocked/STOPped, exactly where and why.
