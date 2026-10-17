#!/usr/bin/env bash
# One official-client capture, with all server state returned to its starting point.
set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../../.." && pwd)
if [[ ! -f $REPO/scripts/eqemu/env.sh && -f $SCRIPT_DIR/.oracle-repo ]]; then
    REPO=$(cat "$SCRIPT_DIR/.oracle-repo")
fi
[[ -f $REPO/scripts/eqemu/env.sh ]] || { echo "oracle repo not found; rerun install.sh" >&2; exit 65; }
if [[ $# != 3 || ! $1 =~ ^[1-5]$ ]]; then
    echo "usage: $0 <lane 1..5> <plan.tsv> <out_dir>" >&2
    exit 64
fi
LANE=$1 PLAN=$(realpath "$2") OUT=$(realpath -m "$3")
mkdir -p "$OUT"
if [[ -e $OUT/session.started ]]; then echo "output already used: $OUT" >&2; exit 64; fi
export EQEMU_LANE=$LANE
source "$REPO/scripts/eqemu/env.sh"
source "$REPO/scripts/eqemu/restore_lib.sh"
OWNER="${ORACLE_OWNER:-oracle_session}_$(date -u +%Y%m%dT%H%M%S)_$$"
[[ $OWNER =~ ^[A-Za-z0-9_-]+$ ]] || { echo "invalid ORACLE_OWNER" >&2; exit 64; }
STATUS=0 LOCKED=0 SNAPSHOT=0 REMOTE_STARTED=0 CREATE_ACCOUNT_ID= BROKER_PID=
REMOTE_DIR="/home/user/eq-oracle/sessions/$OWNER"
say() { printf '%s\n' "$*" | tee -a "$OUT/driver.log"; }
remote() { ssh -n -o BatchMode=yes -o ConnectTimeout=10 "user@$EQEMU_ORACLE_HOST" "$@"; }
stop_broker() {
    if [[ -n $BROKER_PID ]]; then
        kill "$BROKER_PID" 2>/dev/null || :
        wait "$BROKER_PID" 2>/dev/null || :
        BROKER_PID=
    fi
}
cleanup() {
    local prior=$STATUS
    stop_broker
    if (( REMOTE_STARTED )); then
        remote "bash ~/eq-oracle/uigap-scripts/oracle_remote.sh stop '$REMOTE_DIR'" >>"$OUT/driver.log" 2>&1 || :
        scp -q -r "user@$EQEMU_ORACLE_HOST:$REMOTE_DIR/." "$OUT/" 2>>"$OUT/driver.log" || STATUS=65
    fi
    if (( LOCKED )); then
        if (( SNAPSHOT )); then
            # Only remove a plan name that was absent before this locked run,
            # exists now with an id, and belongs to the login character's account.
            : >"$OUT/created-ids.tsv"
            while IFS= read -r name; do
                [[ -n $name ]] || continue
                eqemu_sql "SELECT id,name FROM character_data WHERE name='$name' AND account_id=$CREATE_ACCOUNT_ID;" >>"$OUT/created-ids.tsv" || STATUS=66
            done <"$OUT/absent-names.tsv"
            if [[ -s $OUT/created-ids.tsv ]]; then
                eqemu_world_stop >>"$OUT/driver.log" 2>&1 || STATUS=66
                while IFS=$'\t' read -r id name; do
                    [[ $id =~ ^[0-9]+$ ]] || { STATUS=66; continue; }
                    eqemu_sql "DELETE FROM character_data WHERE id=$id AND name='$name' AND account_id=$CREATE_ACCOUNT_ID;" >/dev/null || STATUS=66
                done <"$OUT/created-ids.tsv"
            fi
            eqemu_restore_snapshot "$OUT/db-before.tsv" "$OUT/db-after.tsv" say >>"$OUT/driver.log" 2>&1 || STATUS=66
            eqemu_inventory_restore "$OUT/inventory-before.tsv" "$CHAR_IDS" >>"$OUT/driver.log" 2>&1 || STATUS=66
            eqemu_inventory_snapshot "$OUT/inventory-after.tsv" "$CHAR_IDS" >>"$OUT/driver.log" 2>&1 || STATUS=66
            diff -q "$OUT/inventory-before.tsv" "$OUT/inventory-after.tsv" >>"$OUT/driver.log" 2>&1 || STATUS=66
            # A mistyped Wine login can auto-create account rows. Remove only
            # new, unowned test accounts, then require the complete censuses to
            # round-trip; never delete a row present before this session.
            account_ids=$(cut -f1 "$OUT/accounts-before.tsv" | paste -sd, -)
            login_ids=$(cut -f1 "$OUT/login-accounts-before.tsv" | paste -sd, -)
            eqemu_sql "DELETE a FROM account a LEFT JOIN character_data c ON c.account_id=a.id WHERE a.id NOT IN ($account_ids) AND a.name LIKE 'willeq_test%' AND c.id IS NULL;" >/dev/null || STATUS=66
            eqemu_sql "DELETE FROM login_accounts WHERE id NOT IN ($login_ids) AND account_name LIKE 'willeq_test%';" >/dev/null || STATUS=66
            eqemu_sql "SELECT id,name FROM account ORDER BY id;" >"$OUT/accounts-after.tsv" || STATUS=66
            eqemu_sql "SELECT id,account_name FROM login_accounts ORDER BY id;" >"$OUT/login-accounts-after.tsv" || STATUS=66
            diff -q "$OUT/accounts-before.tsv" "$OUT/accounts-after.tsv" >>"$OUT/driver.log" 2>&1 || STATUS=66
            diff -q "$OUT/login-accounts-before.tsv" "$OUT/login-accounts-after.tsv" >>"$OUT/driver.log" 2>&1 || STATUS=66
        fi
        bash "$REPO/scripts/eqemu/down.sh" >>"$OUT/driver.log" 2>&1 || STATUS=66
        eqemu_lease_stop_renewer
        # B1089 review (finding 1): eqemu_lock_release is already owner-checked
        # by the lease's identity (card/run/pid; a takeover is refused with
        # 63 -> STATUS=66), so the free-text gate that compared $OWNER to a
        # space-terminated prefix of eqemu_lock_show was both broken (the
        # lease's key=value record never matched) and redundant. A genuine
        # takeover still surfaces through the release's own refusal.
        eqemu_lock_release || STATUS=66
        if eqemu_lock_show >/dev/null 2>&1 || eqemu_stack_ready; then STATUS=66; fi
    fi
    if (( prior == 67 && STATUS != 66 )); then STATUS=67; fi
    say "cleanup exit=$STATUS lane=$LANE lock=$(eqemu_lock_show 2>/dev/null || echo free)"
    trap - EXIT
    exit "$STATUS"
}
trap 'cleanup' EXIT
trap 'STATUS=65; exit 65' INT TERM
if ! eqemu_lock_take "$OWNER"; then say "lane lease held: $(eqemu_lock_show || :)"; STATUS=64; exit "$STATUS"; fi
LOCKED=1
# B1089: a capture can run 20+ min; the renewer keeps the lease live while
# the oracle leg runs, and dies with this script (never after it).
eqemu_lease_start_renewer
printf '%s\n' "$OWNER" >"$OUT/session.started"
if ! bash "$REPO/scripts/eqemu/up.sh" >>"$OUT/driver.log" 2>&1 || ! eqemu_stack_ready; then STATUS=65; exit "$STATUS"; fi
eqemu_snapshot "$OUT/db-before.tsv" || { STATUS=65; exit "$STATUS"; }
CHAR_IDS=$(eqemu_sql "SELECT id FROM character_data WHERE name IN ('Testcaster','Testchar') ORDER BY id;" | paste -sd, -)
[[ -n $CHAR_IDS ]] || { STATUS=65; exit "$STATUS"; }
eqemu_inventory_snapshot "$OUT/inventory-before.tsv" "$CHAR_IDS" || { STATUS=65; exit "$STATUS"; }
eqemu_sql "SELECT id,name FROM account ORDER BY id;" >"$OUT/accounts-before.tsv" || { STATUS=65; exit "$STATUS"; }
eqemu_sql "SELECT id,account_name FROM login_accounts ORDER BY id;" >"$OUT/login-accounts-before.tsv" || { STATUS=65; exit "$STATUS"; }
SNAPSHOT=1
# A plan's zone row is an explicit pre-login seed, never a persistent change.
ZONE=$(awk -F'\t' '$1=="zone" {print $2; exit}' "$PLAN")
CHARACTER=$(awk -F'\t' '$1=="login" {print $2; exit}' "$PLAN")
awk -F'\t' '$1=="create" {print $2}' "$PLAN" >"$OUT/plan-names.tsv"
: >"$OUT/absent-names.tsv"
: >"$OUT/validated-names.tsv"
CREATE_ACCOUNT_ID=$(eqemu_sql "SELECT account_id FROM character_data WHERE name='$CHARACTER' LIMIT 1;")
[[ $CREATE_ACCOUNT_ID =~ ^[0-9]+$ ]] || { STATUS=65; exit "$STATUS"; }
while IFS= read -r name; do
    [[ $name =~ ^[A-Za-z]{3,15}$ ]] || { STATUS=65; exit "$STATUS"; }
    if ! existing_id=$(eqemu_sql "SELECT id FROM character_data WHERE name='$name';"); then
        say "name lookup failed for $name; refusing to arm cleanup"
        STATUS=65; exit "$STATUS"
    fi
    [[ -z $existing_id ]] || { STATUS=65; exit "$STATUS"; }
    if grep -Fxq "$name" "$OUT/validated-names.tsv"; then STATUS=65; exit "$STATUS"; fi
    printf '%s\n' "$name" >>"$OUT/validated-names.tsv"
done <"$OUT/plan-names.tsv"
cp "$OUT/validated-names.tsv" "$OUT/absent-names.tsv"
if [[ -n $ZONE && -n $CHARACTER ]]; then
    [[ $ZONE =~ ^[a-z0-9_]+$ && $CHARACTER =~ ^[A-Za-z]+$ ]] || { STATUS=65; exit "$STATUS"; }
    ZONE_ID=$(eqemu_sql "SELECT zoneidnumber FROM zone WHERE short_name='$ZONE' LIMIT 1;")
    [[ $ZONE_ID =~ ^[0-9]+$ ]] || { STATUS=65; exit "$STATUS"; }
    eqemu_sql "UPDATE character_data SET zone_id=$ZONE_ID WHERE name='$CHARACTER';" >/dev/null || { STATUS=65; exit "$STATUS"; }
    # B1065: `# pose=x,y,z,heading` (heading in EQ units, 0-512) seeds the
    # login character's position with the zone, so a still is taken from a
    # pose willeq can be put at byte-for-byte. x/y/z/heading are in
    # eqemu_snapshot, so the restore puts the old row back.
    POSE=$(awk '/^# pose=/ {sub(/^# pose=/, ""); print; exit}' "$PLAN")
    if [[ -n $POSE ]]; then
        [[ $POSE =~ ^-?[0-9.]+,-?[0-9.]+,-?[0-9.]+,[0-9.]+$ ]] || { STATUS=64; exit "$STATUS"; }
        IFS=, read -r PX PY PZ PH <<<"$POSE"
        eqemu_sql "UPDATE character_data SET x=$PX, y=$PY, z=$PZ, heading=$PH WHERE name='$CHARACTER';" >/dev/null || { STATUS=65; exit "$STATUS"; }
    fi
fi
remote "mkdir -p ~/eq-oracle/uigap-scripts '$REMOTE_DIR'" || { STATUS=65; exit "$STATUS"; }
scp -q "$REPO/scripts/eqemu/oracle/oracle_remote.sh" "user@$EQEMU_ORACLE_HOST:/home/user/eq-oracle/uigap-scripts/oracle_remote.sh" || { STATUS=65; exit "$STATUS"; }
scp -q "$REPO/scripts/eqemu/oracle/oracle_login_patch.py" "user@$EQEMU_ORACLE_HOST:/home/user/eq-oracle/uigap-scripts/oracle_login_patch.py" || { STATUS=65; exit "$STATUS"; }
scp -q "$REPO/scripts/eqemu/oracle/oracle_ini_seed.py" "user@$EQEMU_ORACLE_HOST:/home/user/eq-oracle/uigap-scripts/oracle_ini_seed.py" || { STATUS=65; exit "$STATUS"; }
scp -q "$PLAN" "user@$EQEMU_ORACLE_HOST:$REMOTE_DIR/plan.tsv" || { STATUS=65; exit "$STATUS"; }
REMOTE_STARTED=1
# B1058: a plan with template clicks needs the locate broker on THIS host (the
# oracle box has no NumPy/OpenCV): one ssh exchange per locate, shots and the
# per-locate table in $OUT/broker/ (docs/oracle_driver.md "Template clicks").
# B1059: the same broker answers text reads with lane$LANE's glyph atlas
# (glyphs/rigs.tsv; none listed = tesseract / the legacy checks only).
if grep -qE '^# create-clicks=template$|^(click|wait)-template'$'\t' "$PLAN"; then
    python3 "$REPO/scripts/eqemu/oracle/oracle_locate.py" broker --remote "user@$EQEMU_ORACLE_HOST" \
        --remote-dir "$REMOTE_DIR" --out "$OUT/broker" --rig "lane$LANE" 2>>"$OUT/broker.log" &
    BROKER_PID=$!
fi
remote "bash ~/eq-oracle/uigap-scripts/oracle_remote.sh run '$REMOTE_DIR' $LANE" >"$OUT/remote.log" 2>&1
RC=$?
stop_broker
if (( RC == 67 )); then STATUS=67; else STATUS=$RC; fi
remote "bash ~/eq-oracle/uigap-scripts/oracle_remote.sh stop '$REMOTE_DIR'" >>"$OUT/remote.log" 2>&1 || STATUS=65
scp -q -r "user@$EQEMU_ORACLE_HOST:$REMOTE_DIR/." "$OUT/" 2>>"$OUT/driver.log" || STATUS=65
REMOTE_STARTED=0
if (( STATUS == 67 )); then
    printf '# Official-client disconnect\n\nPlan: `%s`. Partial timeline: [timeline.tsv](timeline.tsv).\nRaw capture: [capture.pcap](capture.pcap); video: [video.mp4](video.mp4).\n' "$PLAN" >"$OUT/summary.md"
elif [[ -s $OUT/capture.pcap ]]; then
    python3 "$REPO/scripts/eqemu/oracle/oracle_report.py" "$OUT" "$PLAN" "$REPO" || STATUS=65
fi
if (( STATUS == 67 )); then
    eq_remote bash -lc "for kind in world zone; do file=\$(ls -t '$AKK_DIR/server/logs'/\${kind}_*.log '$AKK_DIR/server/logs/zone'/\${kind}_*.log 2>/dev/null | head -1); if [[ -n \$file ]]; then echo \"== \$file ==\"; tail -n 40 \"\$file\"; fi; done" >"$OUT/disconnect-logs.txt" 2>&1 || :
    printf '\n## Disconnect server log\n\n' >>"$OUT/summary.md"
    if [[ -s $OUT/disconnect-logs.txt ]]; then
        sed -n '1,82p' "$OUT/disconnect-logs.txt" >>"$OUT/summary.md"
    else
        printf 'No world/zone log lines were found.\n' >>"$OUT/summary.md"
        STATUS=65
    fi
fi
exit "$STATUS"
