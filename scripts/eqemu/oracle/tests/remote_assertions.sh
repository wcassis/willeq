#!/usr/bin/env bash
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../../.." && pwd)
OUT=$(mktemp -d "${TMPDIR:-/tmp}/oracle-remote-test.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
bash "${ORACLE_REMOTE_SOURCE:-$REPO/scripts/eqemu/oracle/oracle_remote.sh}" selftest "$OUT"
