#!/usr/bin/env bash
# B1134 (#454): one-time setup of the OBSERVER client on a lane's oracle host:
# a second official client that logs in as a second PLAYER character and
# watches the caster (docs/oracle_driver.md "The observer").
#
#   1. ~/eq-oracle/client-observer   = a full copy of ~/eq-oracle/client (the
#      client writes eqclient.ini, its UI_*.ini and Logs/ while it runs, so the
#      two instances must not share a directory);
#   2. ~/eq-oracle/wineprefix-observer = a copy of the Wine prefix (one
#      wineserver per instance);
#   3. the observer's UI layout = the caster's (UI_<Caster>_<server>.ini copied
#      to UI_<Observer>_<server>.ini, only when the observer has none), so its
#      windows sit where the B1118/B1130 survey camera had them;
#   4. ~/eq-oracle/observer-<Observer>.cred (mode 600): OBS_USER=<account>
#      and OBS_PASS=<a random password GENERATED ON THE ORACLE HOST>, written
#      only when absent. The password never leaves that host: it is not on a
#      command line, not in the repo, not printed. The loginserver's
#      auto_create_accounts (on, every lane) creates the account at the
#      observer's first login; the observer CHARACTER is then made by a
#      provisioning leg (docs/oracle_driver.md "The observer").
#
# usage: oracle_observer_setup.sh <lane 1..5> <Observer> --account <name>
#            [--caster Testcaster] [--server 'WillEQ Private Server'] [--refresh]
# --refresh re-copies the client and prefix (only with no eqgame.exe running).
# Exit: 0 ready; 64 usage; 65 host-side failure.
set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../../.." && pwd)
[[ $# -ge 2 && $1 =~ ^[1-5]$ && $2 =~ ^[A-Za-z]+$ ]] || { sed -n '/^# usage:/,/^# Exit/p' "$0" >&2; exit 64; }
LANE=$1 OBS=$2; shift 2
CASTER=Testcaster SERVER='WillEQ Private Server' REFRESH=0 ACCOUNT=
while [[ $# -gt 0 ]]; do
    case $1 in
        --caster) CASTER=$2; shift 2 ;;
        --server) SERVER=$2; shift 2 ;;
        --refresh) REFRESH=1; shift ;;
        --account) ACCOUNT=$2; shift 2 ;;
        *) exit 64 ;;
    esac
done
[[ $CASTER =~ ^[A-Za-z]+$ && $SERVER =~ ^[A-Za-z0-9\ ]+$ && $ACCOUNT =~ ^[a-z][a-z0-9]{3,15}$ ]] || exit 64
export EQEMU_LANE=$LANE
# shellcheck source=/dev/null
source "$REPO/scripts/eqemu/env.sh"
remote() { ssh -o BatchMode=yes -o ConnectTimeout=10 "user@$EQEMU_ORACLE_HOST" "$@"; }
remote bash -s "$OBS" "$CASTER" "'$SERVER'" "$REFRESH" "$ACCOUNT" <<'EOS' || exit 65
set -euo pipefail
OBS=$1 CASTER=$2 SERVER=$3 REFRESH=$4 ACCOUNT=$5
E=/home/user/eq-oracle
if pgrep -x eqgame.exe >/dev/null; then echo "an eqgame.exe is running: refusing" >&2; exit 65; fi
for pair in client:client-observer wineprefix:wineprefix-observer; do
    src=$E/${pair%%:*} dst=$E/${pair##*:}
    if [[ -d $dst && $REFRESH == 1 ]]; then rm -rf "$dst"; fi
    if [[ ! -d $dst ]]; then cp -a "$src" "$dst.partial" && mv "$dst.partial" "$dst"; echo "copied $src -> $dst"; fi
done
ui_src=$E/client-observer/UI_${CASTER}_${SERVER}.ini ui_dst=$E/client-observer/UI_${OBS}_${SERVER}.ini
if [[ -f $ui_src && ! -f $ui_dst ]]; then cp "$ui_src" "$ui_dst"; echo "observer layout = $CASTER's ($ui_dst)"; fi
cred=$E/observer-$OBS.cred
if [[ ! -f $cred ]]; then
    ( umask 077; pw=$(LC_ALL=C tr -dc 'A-Za-z0-9' < <(head -c 256 /dev/urandom)); pw=${pw:0:14}; printf 'OBS_USER=%s\nOBS_PASS=%s\n' "$ACCOUNT" "$pw" >"$cred" )
    echo "wrote $cred (account $ACCOUNT, generated password, mode 600)"
elif ! grep -qx "OBS_USER=$ACCOUNT" "$cred"; then
    echo "$cred names another account: refusing" >&2; exit 65
fi
du -sh "$E/client-observer" "$E/wineprefix-observer"
EOS
echo "observer $OBS (account $ACCOUNT) ready on lane $LANE ($EQEMU_ORACLE_HOST)"
