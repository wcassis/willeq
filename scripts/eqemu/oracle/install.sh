#!/usr/bin/env bash
# Install the repo driver beside the hermes-ext UIGAP helpers.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
DEST="$HOME/eq-oracle/uigap-scripts"
mkdir -p "$DEST"
install -m 755 "$ROOT/scripts/eqemu/oracle/oracle_session.sh" "$DEST/oracle_session.sh"
install -m 755 "$ROOT/scripts/eqemu/oracle/oracle_remote.sh" "$DEST/oracle_remote.sh"
install -m 755 "$ROOT/scripts/eqemu/oracle/oracle_login_patch.py" "$DEST/oracle_login_patch.py"
install -m 755 "$ROOT/scripts/eqemu/oracle/oracle_ini_seed.py" "$DEST/oracle_ini_seed.py"
install -m 755 "$ROOT/scripts/eqemu/oracle/set_facepick_luclin.py" "$DEST/set_facepick_luclin.py"
install -m 755 "$ROOT/scripts/eqemu/oracle/oracle_report.py" "$DEST/oracle_report.py"
install -m 644 "$ROOT/scripts/eqemu/oracle/oracle_create.py" "$DEST/oracle_create.py"
printf '%s\n' "$ROOT" >"$DEST/.oracle-repo"
echo "Installed driver in $DEST; the installed copy resolves this checkout via .oracle-repo."
