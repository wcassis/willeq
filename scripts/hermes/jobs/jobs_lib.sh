# B1203 (#522): shared helpers for the no-agent jobs (sourced, not run).
# Job state lives on /mnt/scratch (hermes-ext, NFS) under $JOBS_ROOT/<slug>/:
#   <job>.running  -- "<pid> <host> <start iso>" while the job runs
#   <job>.done     -- ONE line "JOB <job> <slug> status=<s> ..." when it finished (any status)
#   <job>.log      -- the job's own log
# A done-file is written exactly once per run and removed only by `--force`.
JOBS_ROOT=${JOBS_ROOT:-/mnt/scratch/jobs}
TP=${TP:-thinkpad}
RUNNER=${RUNNER:-/mnt/projects/fable/review/GATE-BASELINE/render_gate.sh}
RBUILD=${RBUILD:-$HOME/.local/bin/rbuild}

job_now() { date -u +%FT%TZ; }

# job_begin <slug> <job> [--force] -> 0 = go, 10 = already done, 11 = running elsewhere
job_begin() {
  local slug=$1 job=$2 force=${3:-}
  JOB_DIR="$JOBS_ROOT/$slug"; mkdir -p "$JOB_DIR"
  JOB_DONE="$JOB_DIR/$job.done"; JOB_RUN="$JOB_DIR/$job.running"; JOB_LOG="$JOB_DIR/$job.log"
  if [ "$force" = --force ]; then rm -f "$JOB_DONE"; fi
  if [ -s "$JOB_DONE" ]; then echo "already done: $(cat "$JOB_DONE")"; return 10; fi
  if [ -s "$JOB_RUN" ]; then
    local pid host; read -r pid host _ < "$JOB_RUN"
    if [ "$host" = "$(hostname)" ] && kill -0 "$pid" 2>/dev/null; then
      echo "running: pid $pid since $(cut -d' ' -f3 "$JOB_RUN")"; return 11
    fi
    echo "stale running-file (pid $pid gone) -- taking over" >> "$JOB_LOG"
  fi
  echo "$$ $(hostname) $(job_now)" > "$JOB_RUN"
  trap 'rm -f "$JOB_RUN"' EXIT
  echo "== $job $slug start $(job_now) pid $$" >> "$JOB_LOG"
  return 0
}

# job_end <status> <free text> -- writes the done-file line and prints it
job_end() {
  local line="JOB $JOB_NAME $JOB_SLUG status=$1 $2 finished=$(job_now)"
  echo "$line" > "$JOB_DONE.tmp" && mv -f "$JOB_DONE.tmp" "$JOB_DONE"
  echo "$line" | tee -a "$JOB_LOG"
  rm -f "$JOB_RUN"
}

# judge <ref-checkout> <measure dir> <out dir> -> runs the B1188 judge with WT pointed at the
# checkout (the committed script hard-codes its own worktree path; a copy with WT replaced
# is written next to the tables, as /mnt/scratch/gate-baseline/8c2bfe7c-sl/tables/judge.py was)
judge() {
  local ref=$1 meas=$2 out=$3 src="${JUDGE:-$1/refactor/B1188/scripts/build_tables_b1188.py}"
  mkdir -p "$out"
  [ -f "$src" ] || { echo "judge: $src missing" >> "$JOB_LOG"; return 2; }
  sed "s#^WT = .*#WT = '$ref'#" "$src" > "$out/judge.py"
  B1188_MEASURE="$meas" B1188_OUT="$out" python3 -I "$out/judge.py" > "$out/judge.log" 2>&1
}

# measured_rows <ev> -- rows with a summary.tsv or b1130_rows.tsv
measured_rows() {
  find "$1/measure" -maxdepth 2 \( -name summary.tsv -o -name b1130_rows.tsv \) 2>/dev/null | wc -l
}
