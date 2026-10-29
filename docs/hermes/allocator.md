# The allocator -- deterministic admission of kanban cards against measured resources

B1204 (#523), step 4 of the deterministic allocation stack (Will, 2026-10-29): B1201 ledger
(costs), B1202 manifest (what a card needs), B1203 no-agent jobs (script phases off the
seats), and this allocator, which decides every 5 minutes which `ready` cards may run. No
model is in the loop: every decision is a comparison of declared needs against capacities and
live probes, and every decision is logged.

Files: `scripts/hermes/alloc/alloc.py`, `scripts/hermes/alloc/capacity.yaml`,
`scripts/hermes/alloc/test_alloc.py`, `scripts/hermes/manifest/manifest.py` (byte-identical
to B1202's).

## One pass

1. **Board** (kanban.db, read-only): candidates = `ready` cards + cards the allocator itself
   holds (`scheduled`, last schedule reason starting `awaiting `); `running` cards are usage
   only. `blocked` (worker/human blocks), `todo` (dependency waits), `triage`, `done` and
   anybody else's `scheduled` cards are never touched.
2. **Manifests**: each card body's `## Manifest` block. Missing or invalid -> `tier heavy`,
   `model strong`, `needs cpu-build` (`unmanifested` in capacity.yaml) and the decision row
   is flagged `NO_MANIFEST` / `BAD_MANIFEST`.
3. **Usage**: running cards' declared needs; the ThinkPad census (`pgrep -c model_viewer`,
   `pgrep -c eqgame`, load1, runq/core over ssh) -- the render usage is max(declared,
   live), because a no-agent baseline render holds a slot without a card; `pgrep -fc
   harness_measure` on hermes-ext likewise; lane leases from `scripts/eqemu/leases` (HELD
   closes the lane, EXPIRED/FREE do not); per-endpoint seats from each running run's
   session model (`owner_admitted` -> profile state.db), else its model override, else its
   profile's default.
4. **Guards** (capacity.yaml `guards`): load1 > 12 or runq/core > 2 closes every render slot;
   `thinkpad-quiet` needs load1 <= 2 and no model_viewer; a running `eqgame` closes
   `thinkpad-quiet` and `lane-5`; no ThinkPad answer = renders closed.
5. **Jobs**: a card whose parent's latest summary has a B1203 `JOB:` line whose done-file
   is missing is held `awaiting job:<job>:<slug>`.
6. **Admission**: candidates in priority order (then creation time), GREEDY with out-of-order
   admission: a card is admitted when every need fits and a seat is free, otherwise held and
   the next card is tried. Admitted cards reserve their needs for the rest of the pass;
   `thinkpad-quiet` reserves every render slot. Cards that fit while the board is at
   `max_in_progress` are `queue` (they stay `ready`, resources reserved).
7. **Model route** (route class: `strong` if manifest model is strong/reasoning, `light` if
   tier light or leg review, else `medium`): `routes` in capacity.yaml (light/medium ->
   hermes-a = q38-d; strong -> codex, then hermes-a). Codex is used only when
   `codex.enabled`, the usage record is at most 6 h old (else `CODEX_USAGE_STALE`, local
   fallback), `codex_usage.sh` says `verdict=GO` (the pace line to 90 % by the reset stays the
   ceiling) and the one codex seat is free. Will's window-end rule: when `resets_in < 72 h`
   and `used < 60 %`, medium cards also prefer codex. Every codex decision carries the
   per-run budget (400 tool calls / 10 window points, solo prompt 20261018a rule 14 -- the
   budget is enforced by the card body, not by the allocator).
8. **Actions** (`--apply` only; the default is a dry run): admit/queue -> `kanban assign
   <id> codex` for a codex route, `kanban set-model <id> none` when a local route meets a
   stale override, `kanban unblock <id>` for a held card; hold -> `kanban schedule <id>
   "awaiting <resource>"` on a `ready` card (a held card stays held; hermes refuses
   `schedule` on a scheduled card). The card's status is re-read right before acting; a
   card that moved (e.g. the gateway dispatched it) is not touched.
9. **Log**: one TSV row per decision appended to `/mnt/scratch/ledger/alloc_decisions.tsv`
   (`ts_utc mode task_id status priority decision route reason tier model_class needs flag
   action applied title`).

## Why `schedule`, not `block`

The issue named `block` / `unblock` as the control surface. Hermes's `block_task` counts
repeated blocks of the same kind: `BLOCK_RECURRENCE_LIMIT = 2` in
`~/.hermes/hermes-agent/hermes_cli/kanban_db.py` on hermes-ext (line 114), and
`_route_block` sends a card blocked -> unblocked -> blocked again with the same kind (an
untyped block compares equal to an untyped block) to `triage` with a `block_loop_detected`
event; `block_recurrences` is reset only by a successful completion. An allocator that holds
a card on one pass, admits it, and holds it again later would park it in triage on the
second hold -- the state the solo prompt has to rescue by hand. `schedule` ("waiting on time,
not human input") parks a card with no recurrence counter and `unblock` returns it to
`ready` (or `todo` while parents are open). `schedule` with a reason also appends a comment
and a zero-length run; the allocator schedules a card only on the transition ready -> held,
so a card waiting many passes gets one comment.

## Known limits

- The gateway dispatches a `ready` card the moment a board slot is free; the allocator acts
  every 5 minutes. Until the orchestrator creates new cards held (`kanban schedule <id>
  "awaiting allocator"` right after `kanban create`), a new card can start before its first
  allocator pass. The plan below closes this.
- Declared needs are only as good as the manifests: today no card carries one, so every card
  is `NO_MANIFEST` (heavy, strong, cpu-build) and the 2 cpu-build slots are always full
  with 4 running cards (see refactor/B1204/README.md, the live dry run).
- Runner-VM build slots, ARM build slots and oracle VMs have no live probe yet (capacity +
  declared usage only).

## Install (orchestrator; NOT done by the MR)

```
ssh hermes-ext
mkdir -p ~/.hermes/scripts/alloc ~/.hermes/scripts/manifest ~/.hermes/scripts/eqemu && git -C ~/src/willeq fetch -q origin
for f in alloc/alloc.py alloc/capacity.yaml alloc/test_alloc.py manifest/manifest.py; do
  git -C ~/src/willeq show origin/fable:scripts/hermes/$f > ~/.hermes/scripts/$f; done
for f in leases lease_core.sh; do git -C ~/src/willeq show origin/fable:scripts/eqemu/$f > ~/.hermes/scripts/eqemu/$f; done
python3 -I ~/.hermes/scripts/alloc/test_alloc.py          # ALLOC TESTS PASS
python3 -I ~/.hermes/scripts/alloc/alloc.py               # dry run: read the table
cat > ~/.hermes/scripts/alloc/alloc_cron.sh <<'SH'
#!/usr/bin/env bash
# silent unless a kanban action was taken (a --no-agent job delivers stdout verbatim)
out=$(python3 -I "$HOME/.hermes/scripts/alloc/alloc.py" --apply 2>&1) || { echo "$out" | tail -5; exit 1; }
echo "$out" | grep -E '^(APPLIED|CODEX_USAGE_STALE)' || true
SH
chmod +x ~/.hermes/scripts/alloc/alloc_cron.sh
H="$HOME/.hermes/hermes-agent/venv/bin/python $HOME/.hermes/hermes-agent/hermes"
$H cron create 'every 5m' --name willeq-allocator --no-agent --script alloc/alloc_cron.sh --deliver local --paused
```

(`alloc.py` finds the leases script at `../../eqemu/leases` relative to itself, hence the
`~/.hermes/scripts/eqemu/` copy; created `--paused`: resume it after a dry-run pass reads
right.)

## Plan: raise kanban.max_in_progress to 8 (NOT done here)

1. Live for a day at max_in_progress 4 with manifests on every new card (B1202 linter in the
   solo prompt) and new cards created `scheduled "awaiting allocator"`.
2. Read `alloc_decisions.tsv` + the B1201 ledger: no render/lane collision, no card in
   triage, seats never over-subscribed.
3. Then `kanban.max_in_progress: 8` in `~/.hermes/config.yaml` (+ `max_in_progress_per_profile`)
   and `board.max_in_progress: 8` in capacity.yaml, gateway restart. The binding limits become
   the per-endpoint seats (hermes-a 4 + codex 1) and the resources, not the card count: four
   wait-heavy cards (B1201: fix/diagnose/dev legs spend 28-34 % of their seat time in
   commands) no longer block a light review.
