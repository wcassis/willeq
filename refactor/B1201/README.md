# B1201 -- INFERENCE LEDGER: one row per kanban run, per-card report, backfill since 2026-10-23

Issue #520 (class B, owner session). Tool-build leg: three python3 scripts (stdlib +
sqlite3, no new dependency), the backfill tables, the phase-signal convention and the
hourly no-agent cron spec. No C++, no board change, read-only on every hermes store.

## 1. What was built

| file | what |
|---|---|
| `scripts/hermes/ledger/build_ledger.py` | the ledger: `--since <UTC date>` `--board willeq` `--out /mnt/scratch/ledger`; writes `runs.tsv` + `cards.tsv` (temp file + rename, whole rewrite = idempotent) |
| `scripts/hermes/ledger/report.py` | `report.py <t_xxxxxxxx or B####>`: per-card consumption by run, by inferred phase, the worker-posted phases and the N longest commands; reads the live stores, so a RUNNING card's open run counts to now |
| `scripts/hermes/ledger/summarize.py` | `runs.tsv` + `cards.tsv` -> the committed summary tables (pure function of its inputs) |
| `scripts/hermes/ledger/ledger_cron.sh` | the no-agent cron wrapper (silent on success, error tail on failure) |
| `refactor/B1201/tables/` | the backfill: `runs.tsv`, `cards.tsv` (snapshot of `/mnt/scratch/ledger/` at 2026-10-29 ~22:53Z) and the five summary tables |

Every script runs ON hermes-ext (the stores live there; it mounts `/mnt/projects` at the
same path, so the scripts run straight from a worktree on NFS). The SQLite files are opened
with `mode=ro` URIs (`ro()`, `scripts/hermes/ledger/build_ledger.py:69`); the only writes are
the output TSVs.

Reproduction (the exact commands that produced the committed tables):

```
ssh hermes-ext
cd /mnt/projects/fable/main/wt/kb-b1201-inference-ledger      # any checkout of this branch
python3 -I scripts/hermes/ledger/build_ledger.py --since 2026-10-23
#   LEDGER board=willeq since=2026-10-23 runs=212 cards=158 with_session=196 -> /mnt/scratch/ledger/runs.tsv /mnt/scratch/ledger/cards.tsv
cp /mnt/scratch/ledger/runs.tsv /mnt/scratch/ledger/cards.tsv refactor/B1201/tables/
python3 -I scripts/hermes/ledger/summarize.py refactor/B1201/tables/runs.tsv refactor/B1201/tables/cards.tsv refactor/B1201/tables
#   SUMMARY worker_runs=192 legs=continuation,dev,diagnose,fix,re-review,review -> refactor/B1201/tables
```

A re-run of `build_ledger.py` later gives MORE rows (new runs) and fills in the four runs that
were still running at the snapshot; `summarize.py` over the committed `runs.tsv`/`cards.tsv`
reproduces the committed summary tables byte for byte.

## 2. Sources and join keys

| source (hermes-ext) | table / record | used for |
|---|---|---|
| `~/.hermes/kanban/boards/willeq/kanban.db` | `task_runs` | one ledger row per row: id, task, profile, started_at/ended_at, status, outcome, error |
| same | `tasks` | title (-> B number, issue, leg type), card status |
| same | `task_events` kind `owner_admitted` | payload `{db, session_id}` = the run's session (present from 2026-10-24 23:52Z) |
| same | `task_events` kind `timed_out`, `heartbeat` | wall-timeout vs iteration-budget stop; heartbeat `note` text for `phase:` lines |
| same | `task_comments` | worker-posted `phase:` lines |
| `~/.hermes/profiles/<profile>/state.db` | `sessions` | model, billing provider, prompt/completion/cache-read/reasoning token totals, api_call_count |
| same | `session_model_usage` | main-model base URL; auxiliary (compression / vision) call count |
| same | `messages` | the command log: every assistant turn (with its `tool_calls`) and every tool result, with timestamps |
| `~/.hermes/profiles/codex*/codex-runtime/sessions/**/rollout-*.jsonl` and `~/.codex/sessions/**/rollout-*.jsonl` | codex rollouts | codex-* runs: model, `token_count`, function/custom tool calls with their outputs' timestamps |

Joins (`find_session()`, `scripts/hermes/ledger/build_ledger.py:353`):
1. `task_runs.id == task_events.run_id` with kind `owner_admitted` -> `payload.db` + `payload.session_id` (192 of the 212 runs in the window: `session_join_admitted` in `tables/headline.tsv`).
2. Older runs (before the owner_admitted event existed): the `source='kanban'` session in the run profile's state.db whose `started_at` is within [run start - 60 s, run start + 600 s] and whose title starts with the card title's first 30 characters (4 runs: `session_join_matched`).
3. codex-* profiles: the rollout whose filename start time lies in [run start - 300 s, run end] and whose first 200 kB contain `work kanban task <id>` (0 runs in this window; the code path was exercised over `--since 2026-10-12`, which yields `rollout` rows for the codex and codex-sol runs of 10-13..10-20).
4. No session: the 16 zero-length runs the orchestrator created when it closed parked cards on 2026-10-29 (runs 1574-1587; `wall_s` 0, `session_source none`) plus blocked-at-claim runs. They stay in `runs.tsv` and are excluded from every median.

Two store facts the code handles (both measured on this data):
- Context compression re-inserts copies of kept messages with their ORIGINAL timestamps (session of run 1411: 1213 message rows, 988 `compacted`). Without the de-duplication and the run-window clip, ordering by row id produced model+tool seconds above the wall clock on 125 of 192 runs; the ledger de-duplicates on (role, timestamp, tool_call_id) and orders by timestamp (`hermes_session()`, `scripts/hermes/ledger/build_ledger.py:169`), and only counts messages inside the run window. After that, model_s + tool_s exceeds wall_s by more than 120 s on 0 runs (one run, 1501, is far BELOW: a crashed worker whose session holds 2 tool calls in 2922 s).
- `cache_read_tokens` is 0 on every hermes-a session: the Inspur llama-swap endpoint does not report cached tokens. The column is filled for codex runs (`cached_input_tokens`).

## 3. Columns

`runs.tsv` (`RUN_COLS`, `scripts/hermes/ledger/build_ledger.py:51`):

| column | meaning |
|---|---|
| run_id, task_id | `task_runs.id`, `task_runs.task_id` |
| b_number, issue | first `B<3-4 digits>` and first `(#N` / `#N` in the card title |
| leg_type | from the title, first rule wins (`leg_type()`, `scripts/hermes/ledger/build_ledger.py:91`): `Re-review` -> re-review; `Review` -> review; `CONTINUATION` -> continuation; `FIX` / `MERGE-FIX` -> fix; `TRACE` / `report-only` / `diagnose` -> diagnose; else dev |
| profile | `task_runs.profile` |
| model, provider, base_url | `sessions.model`, `sessions.billing_provider`, `session_model_usage.billing_base_url` (codex: `world_state.collaboration_mode.model`, `openai-codex`) |
| session_id, session_source | the joined session (or rollout file name) and how it was joined: admitted / matched / rollout / none |
| start_utc, end_utc, wall_s | run start, end (empty while running), end-or-now minus start |
| status, outcome | `task_runs.status`, `task_runs.outcome` |
| stop_reason | outcome, with `timed_out` split into `iter_budget` (error starts "Iteration budget": the 500-iteration cap) and `wall_timeout` (a `timed_out` event: the max_runtime wall limit); `running` while open |
| turns | assistant messages in the run window (compression summaries excluded) |
| tool_calls | tool results in the run window |
| api_calls, aux_calls | `sessions.api_call_count`; calls the session made to auxiliary tasks (compression, vision) per `session_model_usage.task != ''` |
| prompt_tokens, completion_tokens, cached_tokens, reasoning_tokens | session totals (one session per run: no session id repeats across runs in `owner_admitted`); codex: the last `total_token_usage` |
| model_s | sum of the gaps before each assistant message = wall seconds the run spent waiting on the model (includes queueing behind other seats on the same endpoint) |
| tool_s | sum of all command durations. A command's duration = its result's timestamp minus the later of the issuing assistant message and the previous result of the same batch (`timeline_from_events()`, `scripts/hermes/ledger/build_ledger.py:222`) |
| noninference_s | seconds inside commands longer than `LONG_S` = 60 s (`scripts/hermes/ledger/build_ledger.py:47`) |
| inference_frac | 1 - noninference_s / wall_s |
| n_long_cmds, longest_cmd_s, longest_cmd | count of > 60 s commands; the longest one and its text (a leading `cd <dir> &&` stripped) |
| phase_comments, reported_phases | count and timeline (`phase@HH:MM>...`) of worker-posted `phase:` lines (section 5) |
| inferred_phases | `phase=model_s+command_s;...` per inferred phase (section 5), largest first |
| title | card title, 120 chars |

`cards.tsv` (`CARD_COLS`, `scripts/hermes/ledger/build_ledger.py:60`): one row per card with
runs in the window: n_runs, n_worker_runs (a session was found), n_timeouts (wall + iteration),
n_iter_budget, n_crashed, first start / last end, and sums of wall_s (worker runs), turns,
tool_calls, tokens and noninference_s; inference_frac over the sums; the longest command;
the models used; `codex_record_age_h` = for a card with any codex-* run, the age in hours of
the newest codex rollout (the record `~/kb-bodies/codex_usage.sh` reads; empty otherwise).

## 4. Backfill findings (2026-10-23 .. 2026-10-29 ~22:53Z)

Window facts (`tables/headline.tsv`): 212 runs on 158 cards; the first run in the window
started 2026-10-24T21:55:02Z (nothing ran 10-23 .. 10-24 21:55 on this board); 192 worker
runs that ended; every one ran `hermes-a` (Inspur local model) on profile `dev` (115) or
`review` (77) (`tables/by_profile_model.tsv`); no codex run in the window.

Per leg type (`tables/by_leg_type.tsv`, medians, n = ended worker runs):

| leg | n | turns med/p90 | wall s med/p90 | prompt tok med | completion tok med | noninference s med | pooled inference_frac | seat-h | seat-h in > 60 s commands | wall timeouts | iteration-budget stops |
|---|---|---|---|---|---|---|---|---|---|---|---|
| review | 42 | 72 / 158 | 2431 / 4583 | 8508950 | 96490 | 336 | 0.919 | 31 | 2.542 | 0 | 0 |
| re-review | 35 | 50 / 122 | 1394 / 3758 | 4706662 | 54572 | 410 | 0.845 | 19 | 2.922 | 0 | 0 |
| fix | 30 | 62 / 189 | 2504 / 8658 | 7635810 | 62732 | 753 | 0.657 | 30 | 10 | 0 | 0 |
| dev | 31 | 295 / 496 | 8873 / 14976 | 38808668 | 264485 | 2306 | 0.721 | 83 | 23 | 0 (0 %) | 3 (9.7 %) |
| diagnose | 30 | 262 / 478 | 11520 / 14413 | 35225649 | 271766 | 2974 | 0.707 | 81 | 24 | 7 (23.3 %) | 1 (3.3 %) |
| continuation | 24 | 296 / 443 | 10157 / 14405 | 38158700 | 220492 | 3376 | 0.707 | 60 | 18 | 4 (16.7 %) | 0 |
| ALL | 192 | 122 / 405 | 3794 / 13578 | 15287873 | 118746 | 1080 | 0.736 | 305 | 80 | 11 (5.7 %) | 4 (2.1 %) |

Findings:
1. **Inference-heavy vs wait-heavy.** Reviews are inference-heavy (pooled inference_frac 0.919 review, 0.845 re-review; 2.5-2.9 seat-hours in long commands over 50 h of review seats). The implementation legs are wait-heavy: fix 0.657, diagnose 0.707, continuation 0.707, dev 0.721. Fix legs are short (median 2504 s) but a third of their seat time sits in commands (rbuild test/hygiene and the sleep loops around it).
2. **Seat-hours held waiting on commands: 80 of 305** worker seat-hours (26 %) were spent inside single commands longer than 60 s (`seat_hours_inside_commands_over_60s`), 1287 such commands. By inferred phase (`tables/by_phase.tsv`, ALL rows, command seconds): `wait` (sleep / poll loops) 122450 s = 34 h, `build` 47227 s = 13 h, `measure` 46090 s = 13 h, `ci-wait` 21403 s = 5.9 h, `leg` 21101 s = 5.9 h, `render` 19824 s = 5.5 h. The largest single block is workers SLEEPING on their own background jobs: a scheduled no-agent job (STEP 3) frees exactly that seat time.
3. **No command runs longer than ~420 s** (`longest_single_command_s` 421): long work is backgrounded and polled with `sleep N; tail` calls of up to 420 s, so a render or rbuild shows up as a chain of `wait` commands, not as one long command. noninference_s is therefore a LOWER bound on the time a run waits on non-inference work (it misses the model turns spent polling between sleeps).
4. **Model time dominates the wall clock: 211 of 305 seat-hours** are gaps before assistant messages (`seat_hours_model_time`). On one shared Inspur endpoint with 4 seats this includes queueing; the ledger cannot separate generation from queueing (the store has no per-call latency).
5. **The 4 h wall timeout hits diagnose (7/30 = 23.3 %) and continuation (4/24 = 16.7 %) legs only**; dev legs instead hit the 500-iteration budget (3/31 = 9.7 %). Review, re-review and fix legs never timed out. Crashes: 22 (`n_crashed`), in every leg type except re-review.
6. **Context re-send is the token cost**: median prompt tokens per run 15.3 M vs 119 k completion tokens (`tables/by_leg_type.tsv` ALL), ratio ~129:1; a dev/diagnose/continuation run re-sends 35-39 M prompt tokens.
7. **Phase signals: 0 runs** carry a `phase:` line yet (`runs_with_phase_comments` 0) -- the convention below is new.

Defaults for STEP 2's `est` field (median turns / median wall minutes, from the table above):
review 72 / 41, re-review 50 / 23, fix 62 / 42, dev 295 / 148, diagnose 262 / 192, continuation 296 / 169.

## 5. Phase signalling convention (ask 3) and the orchestrator audit

A worker posts ONE line when it enters a phase, as a kanban comment or a heartbeat note:

```
phase: <trace|code|build|render|measure|leg|doc|review>
```

(`PHASE_RE`, `scripts/hermes/ledger/build_ledger.py:49`: case-insensitive, at line start,
anywhere in the comment/note body; other text may follow on the same line.) The ledger lists
them per run in `reported_phases` with their UTC time.

Inferred phases (`CMD_RULES` + `cmd_phase()`, `scripts/hermes/ledger/build_ledger.py:114` and
`:127`) classify every tool call: terminal commands by the first matching regex (render:
render_gate / model_viewer / ffmpeg; build: rbuild / cmake / ninja / ctest; leg: run_leg /
oracle_class_leg / EQEMU_LANE / eqemu_lock; measure: harness_measure / build_tables / measure
/ cv2; ci-wait: pipelines / jobs / glab; copy: rsync / scp; wait: sleep / wait / until / pgrep
loops / tail -f; trace: ghidra / .dis / eqgame; code: git commit/diff/add/push; else shell);
`patch`/`write_file` = doc for .md/.txt/.tsv, code otherwise; `read_file`/`search_files` =
trace on decompile paths, read otherwise; kanban_* = board; vision_* = vision. The model
seconds before an assistant message are split evenly over the phases of the calls it issued;
a message with no call is `answer`.

**Audit (the orchestrator, not the worker, owns it):** each pass the orchestrator spot-checks
one running card with `report.py <id>`: the `reported phases:` line is printed under each run
next to the inferred per-phase split, so a run that says `phase: code` while its commands are
`wait`/`render` is visible at a glance. A mismatch is a comment on the card, never a block.

Template lines for the orchestrator to add (shipped here as text; the orchestrator installs):

- `~/kb-bodies/ENV_BLOCK_dev.md`, new bullet after "ONE LEG PER CARD":
  `- PHASE SIGNAL (B1201): when you START a phase, post one line as a kanban comment or heartbeat note: "phase: <trace|code|build|render|measure|leg|doc|review>" (one word, lower case). The orchestrator audits these against your command log (scripts/hermes/ledger/report.py).`
- `~/kb-bodies/ENV_BLOCK_review.md`, new bullet after the first:
  `- PHASE SIGNAL (B1201): post "phase: review" as a kanban comment when you start; if you re-run a build/test or a render, post "phase: build" / "phase: render" before it and "phase: review" after.`

## 6. Cron (ask 5) -- spec only, NOT installed

```
mkdir -p ~/.hermes/scripts/ledger
git -C ~/src/willeq fetch -q origin
for f in build_ledger.py report.py summarize.py ledger_cron.sh; do
  git -C ~/src/willeq show origin/fable:scripts/hermes/ledger/$f > ~/.hermes/scripts/ledger/$f; done
chmod +x ~/.hermes/scripts/ledger/*
~/.hermes/hermes-agent/venv/bin/python ~/.hermes/hermes-agent/hermes cron create 'every 1h' \
    --name willeq-inference-ledger --no-agent --script ledger/ledger_cron.sh --deliver local
```

`ledger_cron.sh` prints nothing on success (a `--no-agent` job with empty stdout is silent)
and the error tail on failure; `LEDGER_SINCE` / `LEDGER_OUT` override the defaults
(2026-10-23, `/mnt/scratch/ledger`). One run over the window takes ~8 s on hermes-ext.

## 7. Codex usage record (for STEP 4)

`~/kb-bodies/codex_usage.sh` reads the LAST `rate_limits` record of the newest codex rollout
(its line 16 globs `~/.hermes/profiles/codex*/codex-runtime/sessions` and, since the
orchestrator's 2026-10-30 patch, `~/.codex/sessions`). Nothing refreshes that record except a
codex run writing a rollout: with no codex card running since 10-20 the record was 228 h old.
The orchestrator refreshed it on 2026-10-29 with a one-shot on the `codex` profile (record:
used 0.0 %, 7-day window, resets 2026-11-05 22:49Z). Fix for staleness: a cheap no-agent
probe (one minimal codex request) at least every 6 h, or treat `record_age > 6 h` as unknown
-- the STEP 4 allocator does the latter (`CODEX_USAGE_STALE`). The ledger's `cards.tsv` shows
the record age on every codex card (`codex_record_age_h`).

## 8. Not covered / limits

- The solo-orchestrator cron's own sessions (hermes-b) are not kanban runs and are not in the ledger.
- Background jobs a worker starts (`nohup`, `&`) are invisible except through the polls that wait on them (finding 3).
- The pre-admitted title+time join (4 runs) is a heuristic; the 192 admitted runs are exact.

## 9. Checks

- `python3 -I scripts/refactor/cite_check.py refactor/B1201/README.md --tree . --extra /mnt/projects/fable`: `checked 10 cites, 0 problems, 0 address warnings`
- `python3 -m py_compile` on the three scripts; `bash -n scripts/hermes/ledger/ledger_cron.sh`.
