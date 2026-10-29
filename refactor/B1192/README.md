# B1192 (#511): the LOOT row of B1183's +0x251 stand-byte table

B1183 (#502, MR !335) measured 8 of the 9 steps of the client's per-spawn
stand-state byte (`+0x251`, `docs/decisions.md` 2026-10-28) on lane 5. The
LOOT row stayed PENDING because the lane-5 leg infrastructure died before the
client step: the legH3 re-run reported exit 66 ("the client step never
produced a timeline") with nothing in `driver.log` to explain why. This card
root-caused that, fixed the leg scripts with tests, and measured the row.

## 1. The root cause of legH3's exit 66

**The plan file was SPACE-delimited, so the session derived an empty
character name — and died at the (then silent) account-id check.**

1. `refactor/B1183/legs/legH_loot.tsv` (as committed by B1183 at
   fd26e93e5) was hand-written with spaces between the four plan columns
   (`action target hold_ms still_name`), not tabs. `oracle_session.sh`
   derives the character from it with
   `CHARACTER=$(awk -F'\t' '$1=="login" {print $2; exit}' "$PLAN")` — on a
   space-delimited file every line is one tab-field, the `login` row never
   matches, and `CHARACTER` is empty. `awk -F'\t' 'NF'` over the original
   file prints `1 fields` for every row (the pre-fix file is cited in the
   git history of `refactor/B1183/legs/legH_loot.tsv`; the fix commit's
   diff is the record).
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

## 2. The fix (scripts/eqemu/oracle/, committed 4b373b99e)

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

`refactor/B1183/legs/legH_loot.tsv` — the same plan with real tabs
(identical content; its header records what happened). Run 2 then reached
the client step and exposed a second, independent plan defect: B1183's plan
also carried **neither of the two directives its `obs` rows require** —
`# observer=<Character>` (parsed at `oracle_session.sh:244` and
`oracle_remote.sh:876`) and `# obs-ini-set=Defaults.LastCharSel=1` (the
observer account holds two characters; without it `obs zone` would click
slot 0 and the wrong character would enter). With no `# observer=`, the
remote step died at its first `obs` row on
`oracle_remote.sh`'s `[[ -n $OBSERVER ]] || exit 65` — again silently:
empty `remote.log`, timeline stuck at `01_char_select`
(`/home/user/kb-evidence/kb-b1192-loot-row/legH-run3/`). The plan now
carries both directives (B1176's canonical form,
`refactor/B1176/leg/o_human.tsv:32-38`), and the session refuses the
obs-rows-without-directive shape with a named 64 before the client starts.

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

## 3. The measured row

The leg ran on lane 5 with the fixed scripts (poller armed per B1183's
protocol, `TMAX=240` for the slow click; corpse held at 300000 ms via
B1183's server-SQL hook):

| step | feed | fable | oracle (+0x251) |
|---|---|---:|---:|
| loot | loot open (28061) | 105 | _(pending run)_ |

fable side: `test_stand_state`'s `TheLane5FullSequenceTable` — the loot row
is 105 (`/home/user/kb-evidence/kb-b1192-loot-row/fable_step_table.tsv`,
command in §5). oracle side: the poller's XWR line at `use_click`
(`/home/user/kb-evidence/kb-b1192-loot-row/legs/legH/poll/poll.log`).

## 4. Where this leaves B1183

The row completes B1183's nine-step table (9/9 measured). **B1183's MR !335
stays 8/9 by design** — the row is reported here, not folded into that MR,
and B1183's README loot line (`| loot | 105 | PENDING | BLOCKED`) keeps its
BLOCKED reading for that MR's own tip.

## 5. Evidence

`/home/user/kb-evidence/kb-b1192-loot-row/`:

- `fable_step_table.tsv` — fable's byte per step (14/14 stand-state tests
  pass on this tip);
  `ssh thinkpad '/home/user/willeq-builds/kb-b1192-loot-row/build/bin/test_stand_state --gtest_filter=StandState.TheLane5FullSequenceTable'`;
- `legH-run2/` — the measured leg: `driver.log`, `timeline.tsv`, `steps.tsv`,
  the stills, `server-data-{before,leg,after}.tsv`;
- `legs/legH/poll/poll.log` — the poller's arm/dump/XWR lines.
