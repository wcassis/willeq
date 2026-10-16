# B1079 (#399): every zone-in loading phase is bounded, and the play box keeps its log

Will, 2026-10-15, XU4 play sessions (quoted in #399): after a cold login the
client zoned to tutorialb and then sat on the loading screen until it was
killed over ssh. The SESSION leg (fable, #399 note 5330) showed a COLD
zone-in works fine on x86 and the XU4 -- Will's hang was an IN-SESSION change
(qeynos2 -> death -> zone-to-bind tutorialb = zone load #2 after an unload)
crossed with a B1072 retry, so cold logins never reach it. B1072's watchdog
disarmed the moment the loading phase left `ZONE_CONNECTING`, which is exactly
the seam: once the session is up, nothing bounds the phase.

## Mechanism (before this unit)

B1072 (`refactor/B1072/README.md`) put a 30 s / 1-retry bound on
`LoadingPhase::ZONE_CONNECTING` only. `ZoneConnectWatchdog::Tick(bool
connecting, ...)` disarmed on the first tick where the phase was not
`ZONE_CONNECTING` (`zone_connect_watchdog.h`, pre-B1079). So:

- The session comes up, `OP_ZoneEntry` goes out (`everquest.cpp`), the zone
  process answers it and then dies / wedges -- no `PlayerProfile`, no
  `ReqClientSpawn`, nothing. The phase is `ZONE_RECEIVING_PROFILE` and the
  bound is OFF. Forever.
- The same is true for every phase after it (`RECEIVING_SPAWNS`,
  `REQUEST_PHASE`, `PLAYER_READY`, `AWAITING_CONFIRM`, and the four
  `GRAPHICS_*` phases the loading thread drives). A zone load #2 (the XU4
  shape) lands on a tail B1072 never covered.

## The fix

- `include/client/zone/zone_connect_watchdog.h`: the watchdog walks the whole
  loading span. `Arm(host, port, now, timeoutsMs[10], retries)` takes ONE
  deadline per phase (`phase_bound::kZone*Ms`, one constant per phase, in
  `phase_index` order); `Tick(phase, now)` walks the client's current loading
  phase (mapped by `ZonePhaseIndex`, `zone_connect.cpp`) and re-baselines the
  deadline ONLY on a forward transition -- a 60 Hz tick at the same phase never
  extends the budget (that re-baseline defect is the whole reason the phase
  tracking exists). The four `GRAPHICS_*` rows share ONE budget (set on the
  first graphics phase's entry; later graphics phases do not extend it), because
  they are one contiguous span owned by the loading thread. The ONE retry stays
  limited to `ZONE_CONNECTING`: at the deadline, `Retry` re-opens the session
  while still connecting, else `GiveUp`.
- `src/client/core/zone_connect.cpp`: `TickZoneConnect()` ticks the phase-aware
  watchdog every game tick; `Action::Retry` re-opens the connect (the B1072
  shape); `Action::GiveUp` -> `AbandonZoneConnect()`, which now NAMES the stuck
  phase and the LAST opcode the zone sent (`m_last_zone_opcode`, recorded in
  `ZoneOnPacketRecv`) -- the one fact a play box without a log file still
  leaves (the XU4 zone logged `OP_ZoneEntry` and nothing else; now the client
  says so too). Same landing as B1072: a fresh world login (character select on
  a run that can show it, auto Enter World headless), never a hang, never a
  process exit.
- The escalation is a **no-retry give-up once the session is up**: past
  `ZONE_CONNECTING` the zone has already seen our `OP_ZoneEntry`, so
  re-opening the session would duplicate it. That is the B1079 distinction from
  B1072's shape (session never came up -> one connect retry).

## The deadline table (source of every value, in the header)

`phase_bound` in `zone_connect_watchdog.h`, restated here:

| phase | bound | source |
|---|---|---|
| `ZONE_CONNECTING` | 30 s | **measured** (B1072): `review/B1072/zone_connect_interval_census.txt`, 1,663 intervals, max 14.4 s; 2x. |
| `ZONE_RECEIVING_PROFILE` .. `ZONE_AWAITING_CONFIRM` | 10 s each | **assumed** (no per-phase census in-tree; ~2.2 B1072 p99s, above the x86 whole-load 7.98/12.1 s, T186). |
| `GRAPHICS_LOADING_ZONE` .. `GRAPHICS_FINALIZING` | 80 s, ONE budget | **measured**: 2x the slowest blessed Pi COLD load, refactor/T186 (orangepi cold befallen 38.65/36.66 s, warm re-zone 23.39 s). A re-zone is faster, so the same bound covers load #N with margin. |

Worst case from a fresh Arm: 30 s + 30 s (the one retry) + 5 x 10 s + 80 s =
230 s, under four minutes. In practice a stalled load sits in ONE phase and
costs that phase's own budget.

## `--log-file` / `WILLEQ_LOG_FILE` (the play box)

`LogManager` (include/common/logging.h) gains a line-flushed file sink: the SAME
lines the drain thread writes to stdout/stderr are appended to the file,
flushed every drain cycle (~50 ms), so a kill mid-zone-in keeps the log (the
XU4's play.sh teed stdout today; a killed process lost its tail). Set from
`main()` BEFORE `InitLogging` (`main.cpp`) so the level lines land in the file
too. The file is ADDITIVE -- stdout/stderr keep everything; an unwritable path
is one stderr line and it continues. The drain cadence is 50 ms
(`logging.h:330`, `drainLoop`'s `sleep_for(milliseconds(50))`).
`docs/standards/debug_logging_standards.md`
documents the flag, the env var, and both `--log-file`/`WILLEQ_LOG_FILE` forms.

Two small logging fixes in the same area (fable, #399 note 5330, "if cheap"):

- **`--log-level LEVEL` (space form)** was silently ignored -- only
  `--log-level=LEVEL` was parsed (`main.cpp:214`), and two misread reproductions
  cost Will a night over it. The space form is now rewritten onto the `=` form
  so the parse sees one shape; an unparseable value is a loud startup `ERROR`,
  never a silent no-op.
- **`eqe1_loader.cpp` ordering**: it compiled into `willeq_core` WITHOUT
  `EQT_HAS_LOGGING`, so its lines were raw `fprintf` (stderr ERROR immediately,
  stdout INFO only at flush -- the "last line is an ERROR" misread was that
  buffering, #399 note 3). `willeq_core` now carries `EQT_HAS_LOGGING`
  (CMakeLists.txt), so every message goes through the logger: one ordered
  stream, and the `--log-file` sink sees them. The standalone test tools
  (`test_eqe1_loader`, `test_eqe1_stale_pack`) do not link `willeq_core` and
  keep the `fprintf` path by definition.

## The silent-zone scenario arm

`tests/test_zone_connect_timeout.cpp` + `src/client/scenario/
scenario_zone_connect_timeout.cpp`: the `zone_connect_timeout` scenario gains a
"zone answered the session and `OP_ZoneEntry`, then went silent" arm
(`--scenario-silent-zone`). `/camp` to character select, arm ONE dead-port
connect (stable silence -- the stale connect is erased without a
`StatusDisconnected`, so nothing reconnects) PLUS the client's
`EverQuest::SimulateZoneSilentAfterHandshake()` lever (pins the handshake to
`ZONE_RECEIVING_PROFILE`, the phase `PlayerProfile` sets). The bound must walk
that phase to its own 10 s deadline and escalate WITHOUT a connect retry,
back at character select within the profile bound and inside the 30 s connect
bound. The plain dead-port arm (B1072: 2 connects) is unchanged.

## Pins

- `tests/test_zone_connect_timeout.cpp` (25 cases): the per-phase table pin;
  the pure state-machine (forward re-baseline, backward no-extend, graphics
  shared budget, complete disarms, silent-after-entry escalates without a
  retry); a headless `EverQuest` (the real 10 s profile budget, one connect, no
  retry); the scenario over a fake world (plain arm pass / no-retry / early
  give-up / hang / lever-not-taken; silent arm pass / one-connect / no-retry /
  wrong-budget). 25/25 green.
- `docs/standards/debug_logging_standards.md`: `--log-file`, `WILLEQ_LOG_FILE`,
  `--log-level` space form.

## Not verified / left open

- **LIVE run on lanes 1-4 (acceptance B): NOT done in this run.** All four
  lanes were held by sibling runs (`b1080_oracle`, then `B1025-H5
  hotbar_lifecycle`, per `eqemu_lock_show`) throughout; the live leg -- run
  `zone_connect_timeout --scenario-silent-zone` cold (SIGSTOP the zone process
  after ZoneEntry, SIGCONT after) and confirm the time-to-character-select
  against the table -- is left to the QA card (t_4fbaa1fd, step 4/5). The
  mechanism is proven at the unit and pure state-machine level above (25/25
  green); the live run is the remaining leg.
- **`--log-file` (acceptance C): smoke-tested in this run, live line-for-line
  left to QA.** `willeq --no-graphics --log-file <path> --log-level INFO`
  wrote the same lines to the file (evidence:
  `/home/user/kb-evidence/kb-b1079-zone-in-bound/logfile_smoke.log`), and
  `--log-level BOGUSLEVEL` (the space form) produced the loud startup ERROR
  rather than being silently ignored. The line-for-line equality check on a
  REAL zone-in (where the file and the merged stdout/stderr can only differ in
  the position of a stderr line that drains in a separate 50 ms cycle --
  content is preserved and flushed before exit) is QA's step 4, on a lane.
- The **official client's** own per-phase bound and message are not measured
  (assumed conservative, table above). An oracle capture that shows a
  different value changes one constant.
- **Graphics stall in `GRAPHICS_LOADING_ZONE`**: the bound covers it (the 80 s
  shared budget) and the loading-thread failure reaches `checkLoadingComplete`
  -> `quitRequested` -> non-zero exit (B918) rather than sitting on the screen,
  but a live "pause the loading thread in GRAPHICS_LOADING_ZONE and confirm the
  bound trips" run is left to QA (t_4fbaa1fd, step 5).
- The in-session `#zone tutorialb` (zone load #2) live run is QA's leg
  (t_4fbaa1fd, step 5a), not this card's.
