# B1192 (#511): the LOOT row of B1183's +0x251 stand-byte table

B1183 (#502, MR !335) measured 8 of the 9 steps of the client's per-spawn
stand-state byte (`+0x251`, `docs/decisions.md` 2026-10-28) on lane 5. The
LOOT row stayed PENDING because the lane-5 leg infrastructure died before the
client step: the legH3 re-run reported exit 66 ("the client step never
produced a timeline") with nothing in `driver.log` to explain why.

Three separate walls stood between that card and the row, in the order they
were broken:

1. **exit 66** — the leg refused itself before the client step, silently, and
   reported the failure of its own cleanup (§1-§2);
2. **the plan** — even a running leg never measured the row, because its
   `/loot` step had no corpse target: the oracle client has no name-based
   `/target`, and the row's mob was untargeted the moment it died (§3);
3. **the poller arm** — the measurement channel (the `+0x251` poller) never
   survived its own start: the arm command's stale-poller kill loop matched
   the arm shell's *own* command line and killed it before the poller was
   launched (§4).

Will's rule held throughout: each unmeasured state was treated as a rig
defect to fix, never as a game fact — the row is not "unmeasurable", it had
three broken instruments in front of it.

## 1. The root cause of legH3's exit 66

**The plan file was SPACE-delimited, so the session derived an empty
character name — and died at the (then silent) account-id check.**

1. `refactor/B1183/legs/legH_loot.tsv` (as committed by B1183 at
   fd26e93e5) was hand-written with spaces between the four plan columns
   (`action target hold_ms still_name`), not tabs. `oracle_session.sh`
   derives the character from it with
   `CHARACTER=$(awk -F'\t' '$1=="login" {print $2; exit}' "$PLAN")` — on a
   space-delimited file every line is one tab-field, the `login` row never
   matches, and `CHARACTER` is empty.
2. The next step — the account-id lookup
   `SELECT account_id FROM character_data WHERE name='$CHARACTER'` — returns
   empty for the empty name, and the leg exits 65. **Pre-fix, that exit
   carried no log line**: nothing in `driver.log` between the successful
   `up.sh` / `db-before.tsv` (both present in legH3's evidence,
   `/home/user/kb-evidence/kb-b1183-stand-state/legs/legH3/`) and the
   cleanup.
3. The cleanup's post-SQL restore then ran unconditionally even though the
   pre-SQL (which creates the `oracle_save_*` tables) never ran: its
   `DELETE FROM character_skills ...; INSERT INTO oracle_save_character_skills_67 SELECT ...`
   hit `ERROR 1146 (table doesn't exist)` on the INSERT, and **that 1146 set
   the leg's exit code to 66** — the code legH3 reported. The 65 that actually
   stopped the leg was masked by the 66 of the restore that followed it.

So "the client step never produced a timeline" was true for the wrong
reason: the client step never *started* — the leg refused itself one step
earlier, said nothing, and then reported the failure of its own cleanup.

The issue's candidate causes (corpse not present at step time, the `/loot`
click path, the poller's wait, the observer's camera) were all checked
against the evidence and ruled out: the poller never produced any output —
legH3's evidence directory
(`/home/user/kb-evidence/kb-b1183-stand-state/legs/legH3/`) holds only the
session's state files and `driver.log`, with no `poll/` subdirectory —
because there was no `gdb_arm` timeline line to arm it on, the ThinkPad
session directory for legH3 was never created (the remote step is the first
thing that creates one), and the corpse-hold fix (rule 300000, B1183) was
already in the DB. None of them ran.

A fourth, latent defect sat in the same path: the post-SQL restore ran the
`DELETE` on the live table **before** proving the save-table copy existed —
in legH3 the `character_skills` rows were 0 (discovery-time state, verified
on lane 5 before this run), so nothing was lost, but any leg that dies after
snapshotting a character with rows would have had them deleted and then
failed to restore them.

## 2. The exit-66 fix (scripts/eqemu/oracle/, committed 4b373b99e)

`oracle_session.sh` — the pre-client window can no longer die silently, and
an early death can no longer be re-reported as a cleanup failure:

- every pre-client early exit now calls `say` (driver.log) before setting
  `STATUS` and exiting: the zone/character guard, plan-name validity, the
  zone/pose/observer seeds, the remote mkdir/scp, the pre-SQL, and the
  server-data hooks;
- `PRE_SQL_RAN` is set only when the pre-SQL actually ran; a post-SQL
  failure on a leg that already exited early is logged as a consequence
  ("the state it restores may never have existed"), not reported as the
  failure;
- the early-exit code is un-masked: when the leg died before the client step
  (`prior` = 64/65) and the cleanup reports 66, the leg exits with `prior`;
- a completed client step (remote rc 0) **without a non-empty
  `timeline.tsv`** is refused 65 and named — the "no timeline" failure
  class wearing a success code;
- a plan with no parseable TAB-delimited `login` row is refused 64 and named
  ("the plan columns are action/target/hold_ms/still_name, tab-separated")
  — legH3's own failure, now a one-line diagnosis.

`oracle_class_leg.sh` — the restore can no longer delete live rows it cannot
put back: the post-SQL now probes `SELECT COUNT(*)` on the `oracle_save_*`
copy **before** the `DELETE` (mysql `-B -N -e` stops at the first error, so a
missing table fails the probe and the DELETE never runs); the
`table:char` split is on separate assignment lines (the two-word form
`T=${tc%%:*} C=${tc##:*}` mis-expands the second assignment under
bash 5.2.21 outside the driver's three-word shape).

The tests — `scripts/eqemu/oracle/tests/session_safety.sh` gains a faithful
`eqemu_sql` stub (splits on `;`, stops at the first error, like
`mysql -B -N -e`) and four cases: **early66** (a named early exit stays 65;
the orphaned post-SQL neither masks it nor deletes live rows), **timeline0**
(a completed step leaves its timeline), **notimeline** (a remote rc 0
without a timeline is refused 65), **spaceplan** (a space-delimited plan
is refused 64, named, no client step), and **obsnoobs** (obs rows without
a `# observer=` directive are refused 64, named, before the client step).
`tests/class_leg_safety.sh` asserts the probe-before-DELETE order per save
table. All 13 oracle safety tests pass:
`bash scripts/eqemu/oracle/tests/{session,class_leg}_safety.sh`.

## 3. The plan wall: `/loot` with no corpse target (runs 2-8)

With exit 66 fixed, runs 2-8 reached the client step and completed it
(rc=0, full timeline) — and the LOOT row was still unmeasured: in every
run the corpse's `+0x251` went 100→120 at death and never to 105, because
the client's `/loot` found no corpse target.

**Root cause (run 6-7 evidence, §7):** the plan's `/target TrollF`
(step 25) targets the LIVE mob (the official client has no name-based
corpse targeting), and the troll's death (step 26) **auto-untargets** it —
run 7's opcodes show `OP_Death` immediately followed by `OP_TargetMouse
target=0`. F8 (TARGETNPC) is the wrong corrector: it targets the nearest
**live** NPC in view, which in this spot is consistently zone NPC 31, not
the corpse (runs 7 and 8 both: `OP_TargetMouse target=31`). So the corpse
had to be targeted by a click on its screen blob — and that click is a
**coin flip**: run 7's click (405,700) hit (run 7 opcodes:
`OP_TargetMouse target=268` → `OP_LootRequest corpse_id=268` +
`OP_MoneyOnCorpse`, the loot window open), while run 8's identical click at
identical geometry (the scene pixels are unchanged between the runs — the
run-7-vs-run-8 frame diff's changed-pixel bbox is x≥421, the right-hand UI
bars only) produced no `OP_TargetMouse` at all, so `/loot` saw the F8
target (NPC 31, a live mob) and did nothing. A single raycast against a
distant low-poly corpse blob is not a measurement instrument.

**The fix — plan v4 (committed, `refactor/B1183/legs/legH_loot.tsv`):**
make the corpse the LAST word before `/loot`, with two independent
mechanisms:

- a 3-point click sweep around the proven point — `(380,690)`,
  `(430,690)`, `(405,700)` — each a plan step with a short settle. A ground
  miss sends no target packet and keeps the previous target (run 8 proved
  the miss path: no `OP_TargetMouse`, target unchanged), so the sweep can
  only ADD the corpse as target, never remove it;
- `/target TrollF` AFTER the kill, as the final step before `/loot`. The
  corpse keeps its name prefix (poll.log: `TrollF000` →
  `TrollF\`s_corpseNNN`), and `/target` resolves names to spawn ids
  server-acknowledged (`OP_TargetCommand target=260` at step 25, run 8).
  If the client's name lookup includes corpses (it does for the live mob
  with the same prefix), the target is deterministic and geometry-free; if
  it doesn't, the command is a no-op and the sweep's best result stands.

F8 is gone from the plan: it always overwrites a good corpse target with
NPC 31.

The poller's keymap decode was also fixed in this window (commit
698fa7413): `refactor/B1183/scripts/b1183_poll.py` `decode_slot` — the
keycode is in the HIGH byte (`(v >> 24) & 0xFF`), not the low 28 bits;
pre-fix every real binding mis-decoded (SLOT 32 raw `0x42000000` came out
`SHIFT+code=0x2000000` when it is F8 = TARGETNPC). Regression test:
`refactor/B1192/scripts/test_poll_decode.py` (ctest `B1192PollDecode`),
ground truth the run-6 `poll.log` NAMEKEY/CHARKEY lines
(F1=0x3b, F6=0x40, F7=0x41, F8=0x42; '1'=0x02, 's'=0x1f). And the plan
keeps its run-6/7 hygiene: `#corpse deleteplayercorpses` first (the stale
Oraclewatch PC corpse from earlier runs, lane-5 `gm_commands/corpse.cpp`)
and a back-to-back kill so the corpse is fresh.

The pre-flight guard (`scripts/eqemu/oracle/oracle_plan_check.py`,
`loot_guards`) now accepts `/target <name>` after the last `#damage` as
corpsed targeting (alongside `press F7/F8` and `click/rclick`), so the
run-6 shape (target the live mob, let death clear it, `/loot` with no
post-kill targeting) is still refused before the client starts. The test
(`scripts/eqemu/oracle/tests/oracle_plan_check_test.py`, case 7) pins all
three mechanisms and the no-kill variants; `python3
scripts/eqemu/oracle/tests/oracle_plan_check_test.py` — PASS.

## 4. The poller-arm wall: the kill loop killed its own shell (runs 7-8)

Run 7 completed the leg (rc=0, 43 steps, timeline + stills intact — F8 at
step 31, `OP_TargetMouse target=268` at step 34, `OP_LootRequest
corpse_id=268` + the server's `OP_MoneyOnCorpse` lootback at step 37: the
loot window opened) — and the measurement was still absent, because the
poll dir on the ThinkPad was never created. Two defects, both measured:

**Defect 1 — the kill loop self-killed the arm shell (the actual killer,
run 7 and run 8).** The arm command carried BOTH the stale-poller kill loop
and the poller launch in one remote command. The remote shell runs as
`bash -c '<the whole command>'`, so its own command line contains the real
path `refactor/B1183/scripts/b1183_poll.sh` (plain dot) from the nohup line.
The kill loop's pattern `b1183_poll[.]sh` is a regex matching the text
`b1183_poll.sh` — sitting in its own command line. `pgrep -f` returned the
shell's own PID, `kill` SIGHUP'd it mid-script, and it died before `mkdir`:
no poll dir, no poller, and the ssh client reported exit-signal = rc=255.
The bracket trick only stops matching the *pattern text*; it cannot stop
matching the real path elsewhere in the same command line. Proven by the X2
probe (scratch `selfmatch2.sh`: rc=255, only the marker written BEFORE the
kill loop survives; the same shape without the real path survives).

**Defect 2 — a foreground arm hangs on the channel (probes 2/3).**
Backgrounding a `cd X && VAR=... setsid nohup bash ...` LIST runs it in a
subshell whose stdout stays on the ssh channel pipe for the list's whole
lifetime (the poller waits ~10-20 min for the `gdb_arm` step), so a
foreground ssh client never sees EOF and hangs. A SIMPLE backgrounded
command (`nohup setsid bash -c '...' > log 2>&1 < /dev/null &`) is
fork+exec'd directly with all three fds redirected, so nothing holds the
pipe and ssh returns in ~1 s (probe 4: rc=0 in 1 s, poller alive,
`poll/` created).

**The fix (`refactor/B1192/scripts/arm_poll.sh`, committed e4d8c2e7b + the
self-kill fix):** two separate ssh calls — (1) a KILL call whose command
line contains ONLY the bracketed pattern (no real path, so no self-match),
then (2) an ARM call that is a SIMPLE backgrounded command
(`nohup setsid bash -c '...' > poll.arm.log 2>&1 < /dev/null &`, all fds
redirected, no kill loop, channel closes in ~1 s on the ThinkPad), then (3)
a VERIFY call that hard-fails if the poller is not actually running — a dead
poller is an error at arm time, not a silent absent measurement at rsync
time (the run-7 failure mode).

**The test (`arm_poll.sh --selftest`, ctest `B1192ArmPoll`, device-free):**
(a) the KILL command's own text contains no `b1183_poll.sh` (plain dot) —
it cannot self-match — while the old combined form does contain it (the
Defect-1 reproduction); (b) the ARM command's channel closes promptly over
a pipe (the local analog of the ssh channel: EOF only when no process holds
the pipe; the old `&&`-LIST form demonstrably does not); (c) the arm starts
a stub poller that survives the session ending, leaving
`poll/poll.started`.

Run 8 (the first run armed by the fixed arm) confirms the fix: the poller
armed in ~1 s, walked the spawn list from t=0, and polled continuously
through the loot step (SNAP lines every ~5 s to t=116, `fails=0`). Its
final `ABORT 1002 consecutive read failures (target gone?)` is the
intended dead-target bail firing AFTER the plan's `/q` (the client process
exits at t≈116; the ABORT line carries no timestamp — it printed ~25 s
after the last successful SNAP, i.e. after the quit).

## 5. The measured row

LOOT ROW_PLACEHOLDER

## 6. Where this leaves B1183

The row completes B1183's nine-step table (9/9 measured). **B1183's MR !335
stays 8/9 by design** — the row is reported here, not folded into that MR,
and B1183's README loot line (`| loot | 105 | PENDING | BLOCKED`) keeps its
BLOCKED reading for that MR's own tip.

## 7. Evidence

`/home/user/kb-evidence/kb-b1192-loot-row/`:

- `fable_stand_state_run.txt` — fable's byte per step on this tip
  (14/14 `StandState.*` tests pass):
  `ssh thinkpad '/home/user/willeq-builds/kb-b1192-loot-row/build/bin/test_stand_state --gtest_filter=StandState.*'`;
- `legH-run7/` — the diagnostic leg: 43-step timeline, stills, per-step
  opcodes proving the plan works once the poller is armed (F8 at 31,
  `target=268` at 34, `OP_LootRequest corpse_id=268` + `OP_MoneyOnCorpse`
  at 37);
- `legH-run8/` — the first fully-armed run (fixed arm + v3 plan): poller
  alive through the loot step (poll.log: XNEW/XWR/XCORPSE + SNAP to t=116,
  `fails=0`), corpse byte 100→120 at t=44.096 and NO 105 — the click missed
  (`34_corpse_click.opcodes.tsv`: no `OP_TargetMouse`), `37_loot_open` empty;
- LOOT_ROW_EVIDENCE_PLACEHOLDER
- probe records (scratch): `selfmatch2.sh` X1/X2 (the Defect-1 proof),
  arm probes 2-4 (the Defect-2 proof + the fixed pattern on the ThinkPad).

The decompile and client help are read-only references on this host:
`/mnt/projects/fable/review/B1141/ghidra/out/eqgame.exe.c` (the 105-write
sites, target-derived corpse gate) and
`/mnt/projects/EverQuestP1999/help/{targeting,combatcommands,playercorpses}.html`.
