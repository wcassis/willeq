# B1204 -- THE ALLOCATOR: capacity.yaml + alloc.py (greedy admission, model routing, dry-run default)

Issue #523 (class B, owner session). Step 4 of 4 of Will's 2026-10-29 deterministic
allocation stack (B1201 #520 MR !350 ledger, B1202 #521 MR !351 manifest, B1203 #522 MR !352
no-agent jobs). python3 stdlib + sqlite3 only; no C++; never run with `--apply`; no cron
installed; no card touched. The spec is `docs/hermes/allocator.md`.

## What landed

| file | what |
|---|---|
| `scripts/hermes/alloc/capacity.yaml` | static capacities (render 3, quiet 1 exclusive, lanes 1-5 = 1 each, cpu-build 2, arm-build 1, oracle-vm 2, hermes-measure 4), guards (load1 12, runq/core 2, quiet load1 2, eqgame closes quiet + lane 5, HELD lease closes a lane), seats (hermes-a 4, hermes-b 0 = crons, codex 1), routes, the codex gate (record age 6 h, window-end 72 h / 60 %, per-run budget 400 calls / 10 points), paths |
| `scripts/hermes/alloc/alloc.py` | the allocator: `decide()` is the pure admission core (`scripts/hermes/alloc/alloc.py:134`), `codex_gate()` (`:243`), `actions_for()` (`:257`), board read `gather_board()` (`:286`) with `job_wait()` (`:315`) and `running_endpoint()` (`:330`), live probes `gather_census()` (`:380`); `--dry-run` is the default, `--apply` acts; the capacity.yaml reader `load_yaml_subset()` (`:66`, no PyYAML) |
| `scripts/hermes/alloc/test_alloc.py` | 34 plain-python asserts over fixture cards (greedy out-of-order, cpu-build full, load/runq guards, HELD vs EXPIRED lease, eqgame, quiet exclusivity, codex seat, CODEX_USAGE_STALE, STOP, the window-end rule, seats, board slots -> queue, no manifest, job gating, lane:any, no census, every action shape, both parsers) |
| `scripts/hermes/manifest/manifest.py` | byte-identical copy of B1202's (`cmp` clean against MR !351's file), so this MR runs on its own and the two adds merge cleanly |
| `docs/hermes/allocator.md` | the pass, the hold verb, limits, install + the 5-minute no-agent cron spec, the plan to raise max_in_progress to 8 |

## Findings that shaped the design (measured on hermes-ext, 2026-10-29)

1. **`block` would triage cards.** hermes `kanban_db.py` (line 114 on hermes-ext) has
   `BLOCK_RECURRENCE_LIMIT = 2`; `_route_block` sends a card that is blocked, unblocked and
   blocked again for the same kind to `triage` (`block_loop_detected`), and only a successful
   completion resets the counter. The board already shows the effect (1 card in `triage` at
   the time of the dry run). The allocator therefore holds with `kanban schedule <id>
   "awaiting <resource>"` and releases with `kanban unblock` (scheduled -> ready), and only
   ever releases its own holds (reason prefix `awaiting `).
2. **The codex record and the pace line.** `bash ~/kb-bodies/codex_usage.sh` at the dry run:
   `used=0.0% line=0.3% ... verdict=STOP (ahead of the line) ... resets_in=167.5h ...
   record_age=0.5h` (a fresh 7-day window, refreshed by the orchestrator's codex one-shot).
   At the start of a window the line starts at 0, so `used <= line - 1.0` fails until the line
   passes 1 % (next_go_at 00:42Z): codex routing is legitimately off for the first ~2 h of a
   window. A record older than 6 h is refused with `CODEX_USAGE_STALE` (the record had been
   228 h old before the refresh; only a codex rollout refreshes it -- see B1201 README §7).
3. **No card carries a manifest yet**, so the dry run below holds everything: the unmanifested
   default (`needs: cpu-build`) times 4 running cards exceeds the 2 cpu-build slots. That is
   the conservative behaviour the issue asked for; it becomes useful once B1202's manifests
   are on the cards.
4. **Rule 14** (the codex per-run budget) is in `~/kb-bodies/solo_prompt_20261018a.txt`, not
   in the current `solo_prompt_20261029c.txt` (which has no numbered rule 14); the allocator
   carries the numbers in every codex decision row, the enforcement stays in the card body.

## Dry run over the live board (2026-10-29 23:22:28Z; `refactor/B1204/dryrun/`)

`python3 -I scripts/hermes/alloc/alloc.py` on hermes-ext (default config) ->
`refactor/B1204/dryrun/live_default.txt`:

```
CODEX NO -- verdict STOP (used 0.0% line 0.3%)
CENSUS thinkpad load1=6.39 runq/core=0.25 model_viewer=1 eqgame=0 measure=2 leases=1:FREE,2:FREE,3:FREE,4:FREE,5:EXPIRED
RUNNING 4: t_3d392c18@hermes-a t_1e04932f@hermes-a t_36bce0df@hermes-a t_86c81d8e@hermes-a
```

| card | prio | status | tier / class (flag) | decision | reason | action it would take |
|---|---|---|---|---|---|---|
| t_a74670e3 (B1162 /disc record leg) | 4 | ready | heavy / strong (NO_MANIFEST) | hold | cpu-build (4/2 used) | `schedule t_a74670e3 'awaiting cpu-build'` |
| t_a35f1f1e (B1183 FIX) | 3 | ready | heavy / strong (NO_MANIFEST) | hold | cpu-build (4/2 used) | `schedule t_a35f1f1e 'awaiting cpu-build'` |
| t_a24d468e (B1190 FIX) | 3 | ready | heavy / strong (NO_MANIFEST) | hold | cpu-build (4/2 used) | `schedule t_a24d468e 'awaiting cpu-build'` |
| t_d404af6b (#524 P12 flags census) | 3 | ready | heavy / strong (NO_MANIFEST) | hold | cpu-build (4/2 used) | `schedule t_d404af6b 'awaiting cpu-build'` |

Variant with `unmanifested.needs: none`
(`refactor/B1204/dryrun/capacity_unmanifested_needs_none.yaml`, output
`refactor/B1204/dryrun/live_unmanifested_needs_none.txt`, run right after the default one): the
same four cards are held one step later, on seats --
`seat: codex verdict STOP (used 0.0% line 0.3%); seat hermes-a 4/4` -> `schedule <id>
'awaiting seat'`.
Either way nothing would be admitted: the board is at max_in_progress 4 with 4 running
hermes-a cards. The decision rows appended to `/mnt/scratch/ledger/alloc_decisions.tsv`
by the two default runs at 23:21:52Z (before the `awaiting seat` key change; same decisions) and 23:22:28Z (= `live_default.txt`) are copied in `refactor/B1204/dryrun/alloc_decisions_tail.tsv`.

## For the orchestrator (NOT done by this MR)

- Install + the paused 5-minute no-agent cron: `docs/hermes/allocator.md` "Install".
- Before `--apply`: B1202 manifests on new cards (lint in the solo prompt), new cards created
  `scheduled "awaiting allocator"`, `codex.enabled` set to match the CODEX RESUME rule on #297.
- Raise max_in_progress to 8 only per the plan in `docs/hermes/allocator.md`.

## Checks

- `python3 -I scripts/hermes/alloc/test_alloc.py` -> `ALLOC TESTS PASS (34)`.
- `python3 -I scripts/refactor/cite_check.py refactor/B1204/README.md --tree . --extra /mnt/projects/fable` -> `checked 1 cites, 0 problems, 0 address warnings`
