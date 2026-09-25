#!/usr/bin/env bash
# Behavioral proof for B854 round-2 finding 1 (MR !30), WITHOUT the VM lane.
#
# Extracts the ACTUAL cleanup() body and the ACTUAL trap line from
# scripts/scenario_zone_sweep.sh, wires them into a stubbed environment
# (say / eqemu_restore_snapshot / pi_lock_release / eqemu_lock_release /
# down.sh), and proves:
#   A. early exit (exit 65 path) with ZONE_TMPFILE/ZONE_PROCESSED_FILE set to
#      "" (the round-2 init): cleanup runs to completion under set -u, every
#      restore/down/lock-release step fires, exit code 65 is preserved.
#   B. normal completion: the single 'trap cleanup EXIT INT TERM' fires on a
#      plain exit 0 (nothing clobbers it), both real temp files are removed,
#      all lane steps fire, exit code 0 is preserved.
set -u

# The proof script sits at the repo root, next to scripts/ and refactor/B854/.
SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scripts/scenario_zone_sweep.sh"
WORK=$(mktemp -d)
PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "  ok: $*"; }
bad()  { FAIL=$((FAIL+1)); echo "  FAIL: $*"; }
check(){ if eval "$1"; then ok "$2"; else bad "$2"; fi; }

# ---- pull the real cleanup() (from 'cleanup() {' to the first bare '}') ----
sed -n '/^cleanup() {/,/^}/p' "$SCRIPT" > "$WORK/cleanup.sh"
check '[ -s "$WORK/cleanup.sh" ] && grep -q "eqemu_restore_snapshot" "$WORK/cleanup.sh"' "extracted real cleanup() ($(wc -l < "$WORK/cleanup.sh") lines)"

# ---- exactly one real trap line in the whole script, and it is cleanup ----
ntraps=$(grep -c '^trap ' "$SCRIPT")
check "[ $ntraps -eq 1 ]" "exactly one 'trap ...' statement in the script (found $ntraps)"
check "grep -q '^trap cleanup EXIT INT TERM$' \"$SCRIPT\"" "that trap is 'trap cleanup EXIT INT TERM'"
check "! grep -q \"^trap 'rm\" \"$SCRIPT\"" "no bare 'trap rm ... EXIT' clobberers remain"

# ---- stub environment (mirrors what the real script provides by exit time) ----
cat > "$WORK/down.sh" <<'EOF'
#!/usr/bin/env bash
echo "SAY: stack down (stub down.sh)"
exit 0
EOF
chmod +x "$WORK/down.sh"
cat > "$WORK/stubs.sh" <<EOF
REPO="$WORK"
OUT_DIR="$WORK"
PI=0
PI_LOCK_TAKEN=1
SNAPSHOT="$WORK/snapshot_before.tsv"
AFTER="$WORK/snapshot_after.tsv"
say() { echo "SAY: \$*"; }
eqemu_restore_snapshot() { echo "RESTORE_CALLED (snapshot=\$(basename "\$1"))"; return 0; }
pi_lock_release() { echo "PI_LOCK_RELEASED"; }
eqemu_lock_release() { echo "EQEMU_LOCK_RELEASED"; }
EOF

run_case() {
  # $1 = case name, $2 = var-init snippet, $3 = final exit code
  local name="$1" init="$2" code="$3"
  echo "== $name (exit $code) =="
  cat > "$WORK/case.sh" <<EOF
#!/usr/bin/env bash
set -u
source "$WORK/stubs.sh"
source "$WORK/cleanup.sh"
$init
CLEANED=0
CLIENT_PID=""
trap cleanup EXIT INT TERM
exit $code
EOF
  out=$(bash "$WORK/case.sh" 2>&1); rc=$?
  echo "$out" | sed 's/^/    /'
  check "[ $rc -eq $code ]" "exit code preserved ($rc == $code)"
  for step in RESTORE_CALLED "stack down" PI_LOCK_RELEASED EQEMU_LOCK_RELEASED; do
    check "echo \"\$out\" | grep -q \"$step\"" "cleanup reached: $step"
  done
  check "! echo \"\$out\" | grep -qi 'unbound variable\|not set'" "no set -u abort inside cleanup"
  echo
}

# Case A: the early-exit path (exit 65/68) — vars initialized empty as the fix does.
run_case "A: early exit before zone loop (vars empty)" \
  'ZONE_TMPFILE=""; ZONE_PROCESSED_FILE=""' 65

# Negative control: prove the guard + init actually matter — with the vars
# UNSET (as if the init lines were missing), the old regression reproduces
# (set -u aborts cleanup before restore/down/lock-release).
echo "== A-neg: negative control, vars unset (old regression, expected to abort) =="
cat > "$WORK/case_neg.sh" <<EOF
#!/usr/bin/env bash
set -u
source "$WORK/stubs.sh"
source "$WORK/cleanup.sh"
CLEANED=0
CLIENT_PID=""
trap cleanup EXIT INT TERM
exit 68
EOF
neg_out=$(bash "$WORK/case_neg.sh" 2>&1); neg_rc=$?
echo "$neg_out" | sed 's/^/    /'
check "echo \"\$neg_out\" | grep -q 'unbound variable'" \
  "control reproduces the set -u abort without the init (test has sensitivity)"
# and the real script does not have that exposure: both init lines precede the trap
check "awk '/^ZONE_TMPFILE=\\\"\\\"/{a=NR} /^trap cleanup EXIT INT TERM/{b=NR} END{exit !(a && b && a < b)}' \"$SCRIPT\"" \
  "ZONE_TMPFILE init (line precedes trap) present in the real script"
check "awk '/^ZONE_PROCESSED_FILE=\\\"\\\"/{a=NR} /^trap cleanup EXIT INT TERM/{b=NR} END{exit !(a && b && a < b)}' \"$SCRIPT\"" \
  "ZONE_PROCESSED_FILE init (line precedes trap) present in the real script"
echo

# Case B: normal completion — real temp files exist, trap fires on plain exit.
TMP1=$(mktemp "$WORK/tmpfile.XXXXXX"); TMP2=$(mktemp "$WORK/processed.XXXXXX")
echo x > "$TMP1"; echo x > "$TMP2"
echo "== B: normal completion (real temp files $TMP1 / $TMP2) =="
cat > "$WORK/case_b.sh" <<EOF
#!/usr/bin/env bash
set -u
source "$WORK/stubs.sh"
source "$WORK/cleanup.sh"
ZONE_TMPFILE="$TMP1"
ZONE_PROCESSED_FILE="$TMP2"
CLEANED=0
CLIENT_PID=""
trap cleanup EXIT INT TERM
exit 0
EOF
out=$(bash "$WORK/case_b.sh" 2>&1); rc=$?
echo "$out" | sed 's/^/    /'
check "[ $rc -eq 0 ]" "exit code 0 preserved on normal completion"
for step in RESTORE_CALLED "stack down" PI_LOCK_RELEASED EQEMU_LOCK_RELEASED; do
  check "echo \"\$out\" | grep -q \"$step\"" "cleanup reached: $step"
done
check "! echo \"\$out\" | grep -qi 'unbound variable\|not set'" "no set -u abort inside cleanup"
check "[ ! -e '$TMP1' ] && [ ! -e '$TMP2' ]" "both real temp files removed by cleanup()"

echo "=============================="
echo "PASS=$PASS FAIL=$FAIL"
[ $FAIL -eq 0 ]
