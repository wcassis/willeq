#!/bin/bash
# H03: Ensure a test account + character exist for the willeq recording tour.
#
# Seeding path chosen (see refactor/H03_eqemu_recording.md's review for the
# full investigation):
#
#   - Account creation: NOT this script's job at all. login.json has
#     "auto_create_accounts": true (already shipped in akk-stack's default
#     config, unmodified by this unit) — any login attempt with an unknown
#     account name auto-creates it on the login server. Fully sanctioned,
#     zero DB writes.
#
#   - Password: this environment already has an account (willeq_test) and
#     character (Testchar) seeded from prior work, but the login password
#     hash is unknown to us. Rather than touching the DB, this script uses
#     the loginserver binary's own built-in admin subcommand:
#         ./bin/loginserver login-user:update-credentials <user> <pass>
#     (registered in loginserver_command_handler.cpp as
#     "login-user:update-credentials" — a shipped server *tool*, not server
#     *code*; running it modifies no file under akk-stack/code). This is
#     idempotent — safe to run every time seed.sh runs.
#
#   - Character creation: willeq has no client-side implementation of
#     OP_CharacterCreate (confirmed by investigation — no opcode, no struct,
#     no send/process method anywhere in include/client or src/client), and
#     akk-stack exposes no console/telnet/GM command that creates a
#     brand-new character out of thin air (character creation is normally a
#     client-driven UI flow that world/client.cpp's HandleCharacterCreatePacket
#     handles by building a fresh PlayerProfile_Struct row). Since this
#     environment already has a usable character (willeq_test / Testchar,
#     zone_id 77 "arena"), this script does NOT attempt to create one from
#     scratch — it only verifies one exists and STOPS with a clear message
#     if not, rather than guessing at an unverified raw-SQL character
#     creation fallback. See the review doc for what a from-scratch fallback
#     would require if this is ever run against a truly empty database.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

echo "== seed.sh: checking for account '${EQ_TEST_ACCOUNT}' =="
ACCOUNT_ROW="$(mysql_exec "SELECT id FROM account WHERE name='${EQ_TEST_ACCOUNT}';" | tail -n +2)"
if [ -z "${ACCOUNT_ROW}" ]; then
    cat >&2 <<EOF
STOP: no world account named '${EQ_TEST_ACCOUNT}' exists in the peq database.

No sanctioned command path creates a world account + character from a
completely empty database in this stack (see this script's header comment
and refactor/H03_eqemu_recording.md for the full investigation). Options:
  1. Log in once with a real EQ Titanium client using account
     '${EQ_TEST_ACCOUNT}' / any password — auto_create_accounts will create
     the login_accounts + account rows, then create a character through the
     normal in-client character creation screen, naming it
     '${EQ_TEST_CHARACTER}'.
  2. Extend willeq with client-side OP_CharacterCreate support (out of scope
     for this unit) and script that flow instead.
  3. As a last resort, write a documented raw SQL seed mirroring an existing
     working character row (account/login_accounts/character_data) — not
     attempted here since it was not verified against a real fresh install.
EOF
    exit 1
fi
ACCOUNT_ID="$(echo "${ACCOUNT_ROW}" | awk '{print $1}')"
echo "  account '${EQ_TEST_ACCOUNT}' -> id ${ACCOUNT_ID}"

echo "== seed.sh: checking for character '${EQ_TEST_CHARACTER}' =="
CHAR_ROW="$(mysql_exec "SELECT id, zone_id FROM character_data WHERE name='${EQ_TEST_CHARACTER}' AND account_id=${ACCOUNT_ID};" | tail -n +2)"
if [ -z "${CHAR_ROW}" ]; then
    echo "STOP: account '${EQ_TEST_ACCOUNT}' has no character named '${EQ_TEST_CHARACTER}'." >&2
    echo "See this script's header comment for options (same gap as a missing account)." >&2
    exit 1
fi
CHAR_ID="$(echo "${CHAR_ROW}" | awk '{print $1}')"
ZONE_ID="$(echo "${CHAR_ROW}" | awk '{print $2}')"
echo "  character '${EQ_TEST_CHARACTER}' -> id ${CHAR_ID}, zone_id ${ZONE_ID}"

echo "== seed.sh: resetting login password via loginserver's own admin CLI =="
server_exec "./bin/loginserver login-user:update-credentials ${EQ_TEST_ACCOUNT} ${EQ_TEST_PASSWORD}"
server_exec "./bin/loginserver login-user:update-credentials ${EQ_BUDDY_ACCOUNT} ${EQ_BUDDY_PASSWORD}"

# Verify the full character roster (created via official client 2026-07-23)
for char in Testchar Testmelee Testcaster Testbuddy; do
    ROW="$(mysql_exec "SELECT id FROM character_data WHERE name='${char}'" | tail -n +2)"
    if [ -z "${ROW}" ]; then
        echo "seed.sh: MISSING character '${char}' — recreate via official client (see refactor/H03_eqemu_recording.md character-creation gap)" >&2
        exit 1
    fi
done
echo "seed.sh: roster verified (Testchar, Testmelee, Testcaster, Testbuddy)"

# B1047 (#367) / B1025-H5 (#345): no roster character may be STORED in
# timorous (it never finishes loading on the lanes). Idempotent.
echo "== seed.sh: moving any roster character out of timorous =="
mysql_exec "$(cat "${SCRIPT_DIR}/roster_zone_seed.sql")"

echo "== seed.sh: done — account and character ready =="
