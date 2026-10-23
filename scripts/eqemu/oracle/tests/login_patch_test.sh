#!/usr/bin/env bash
# B1134 (#454): offline net for oracle_login_patch.py. The default copy is the
# B1048 one (DISPLAY :99, the VM helper's own credentials); --display retargets
# it; --credentials makes it log in as the observer by SOURCING the file, so
# the session copy holds the file's path and never a typed secret.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
F=$(mktemp -d "${TMPDIR:-/tmp}/oracle-login.XXXXXX"); trap 'rm -rf "$F"' EXIT
P=$REPO/scripts/eqemu/oracle/oracle_login_patch.py
cat >"$F/helper.sh" <<'H'
export DISPLAY=:99
        xdotool mousemove 514 497; sleep 0.6
        xdotool mousemove 515 498; sleep 0.3
    key shift+Tab; sleep 1
    key End; for _ in $(seq 1 25); do key BackSpace; done
    xdotool type --delay 90 'helperaccount'
    key Tab; sleep 1
    key End; for _ in $(seq 1 25); do key BackSpace; done
    xdotool type --delay 90 'helpersecret'
key Return

echo "== waiting for character select =="
H
python3 "$P" "$F/helper.sh" "$F/plain.sh"
grep -qx 'export DISPLAY=:99' "$F/plain.sh" && grep -q "helpersecret" "$F/plain.sh" || { echo 'default copy changed'; exit 1; }
printf 'OBS_USER=a\nOBS_PASS=b\n' >"$F/observer-Oraclewatch.cred"
python3 "$P" "$F/helper.sh" "$F/obs.sh" --display :98 --credentials "$F/observer-Oraclewatch.cred"
grep -qx 'export DISPLAY=:98' "$F/obs.sh" || { echo 'display not retargeted'; exit 1; }
grep -qx "source $F/observer-Oraclewatch.cred" "$F/obs.sh" || { echo 'credentials file not sourced'; exit 1; }
if grep -q 'helperaccount\|helpersecret' "$F/obs.sh"; then echo 'observer copy still types the helper credentials'; exit 1; fi
[[ $(grep -c 'xdotool type --delay 90 "$OBS_' "$F/obs.sh") == 2 ]] || { echo 'observer copy does not type $OBS_USER/$OBS_PASS'; exit 1; }
if python3 "$P" "$F/helper.sh" "$F/bad.sh" --credentials "$F/helper.sh" 2>/dev/null; then echo 'accepted a non-observer credentials file'; exit 1; fi
if python3 "$P" "$F/helper.sh" "$F/bad.sh" --display 99 2>/dev/null; then echo 'accepted a bad display'; exit 1; fi
echo 'PASS: default login copy unchanged; observer copy retargets the display and sources its credentials file (no typed secret); bad credentials/display refused'
