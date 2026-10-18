#!/usr/bin/env bash
# B1034 (#357): pins the pi_hdmi_force_on / pi_hdmi_restore contract of
# scripts/pi/env.sh against a FAKE device surface -- no device, no ssh,
# no root. The device surface is shaped like the real one AND behaves like
# the real kernel connector, measured on lane 2's RK3328 (2026-10-18):
#   $PI/<di_connector>.status   the kernel connector status (sudo-writable sysfs)
#   $PI_HDMI_RECORD             the pre-force record (root-owned on a device)
#
#   accepted write     reads back
#   ----------------   -----------------------------------------
#   on                 connected        (the kernel FORCES the connector)
#   detected
#   off                disconnected
#   connected          REJECTED (store fails, state unchanged)
#   disconnected       REJECTED (store fails, state unchanged)
#   anything else      REJECTED
#
# That normalization is why pi_hdmi_write_status reports the write's EXIT
# CODE (not the read-back): "on" never reads back "on", "detect" never reads
# back "detect". The fake enforces the same rule in its status store, so a
# regression to a literal read-back comparison (the pre-lane-proof form of
# the B1034 check) fails this selftest exactly as it failed lane 2.
#
# The fake is built by shadowing the two binaries that reach the device --
# `ssh` (pi_remote's transport) and `sudo` (the tee boundary) -- with local
# fakes on PATH, so env.sh runs UNMODIFIED and its own commands execute
# against the sandbox files. The fake `sudo -n <cmd...>` runs <cmd...>
# locally and fails exactly when the scenario's SUDO_OK is 0: that failure
# is the "no passwordless sudo" the B1034 fix must check (the pre-B1034 code
# did not check it at all).
#
# Scenarios (each subshell = a fresh shell on the host, the way a crashed
# run's recovery shell actually is; PI_HDMI_FORCED is always 0 at entry):
#   s1 force on a disconnected lane: record written with the pre-force
#      status, the kernel forced it (status reads connected), flag set
#   s2 crash recovery: fresh shell, PI_HDMI_FORCED=0, the record survives
#      "the crash"; restore writes detect back (accepted), clears the record
#   s3 force on an already-connected lane: no record, no write, no flag
#   s4 a failed sudo: force REFUSES (non-zero, no record, no flag)
#   s5 a record owned by a DIFFERENT host is refused, like a foreign lock
#   s6 the no-record path: clean force, clean restore writes detect back
#   s7 crash recovery then CONTINUE: a stale same-owner record makes
#      force_on restore its pre-force status, clear the record, and THEN
#      proceed with this run's own force (record + flag set again)
#
# Exit 0 = PASS; non-zero = FAIL (the failing scenario is named).
set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TMP="$(mktemp -d)"
# WILLEQ_HDMI_SELFTEST_KEEP=1 keeps the temp dir for inspection (the
# failure lines point at $TMP/<scenario>/stderr).
[ -n "${WILLEQ_HDMI_SELFTEST_KEEP:-}" ] || trap 'rm -rf "$TMP"' EXIT
FAILS=0

# ---- the two faked binaries (once) ------------------------------------------
FAKE_BIN="$TMP/bin"
mkdir -p "$FAKE_BIN"
# Fake ssh: pi_remote calls
#   ssh <opts> user@host "<prelude>; <command>"
# The last argument is the command string (prelude + command, `;`-joined);
# run it with bash. The prelude (export PI_RUN_ENV=... PI_KMSCUBE_ARGS=...)
# is harmless locally and runs too, so the fake stays faithful to the shape.
cat > "$FAKE_BIN/ssh" <<'EOF'
#!/usr/bin/env bash
if [ -n "${SSH_LOG:-}" ]; then
    for a in "$@"; do L="$a"; done
    echo "SSH n=$# last=[$L]" >> "$SSH_LOG"
fi
cmd=""
for a in "$@"; do cmd="$a"; done   # the command string is always the last arg
exec bash -c "$cmd"
EOF
# Fake sudo: `sudo -n <cmd...>`. Run <cmd...> locally; fail (like a device
# without passwordless sudo) when SUDO_OK is 0 in the environment.
cat > "$FAKE_BIN/sudo" <<'EOF'
#!/usr/bin/env bash
if [ "${SUDO_OK:-1}" = 0 ]; then
    echo "sudo: a password is required" >&2
    exit 1
fi
while [ "$#" -gt 0 ] && [ "$1" = -n ]; do shift; done
exec "$@"
EOF
chmod +x "$FAKE_BIN/ssh" "$FAKE_BIN/sudo"

# Fake tee: env.sh stores a connector value as
#   printf %s 'VALUE' | sudo -n tee $PI_HDMI_CONN/status
# so the store happens INSIDE tee, and a plain state file would accept every
# value the kernel rejects. The fake tee enforces the kernel's write rules
# when its target is the status file, and behaves like tee otherwise (the
# record file goes through it too).
cat > "$FAKE_BIN/tee" <<'EOF'
#!/usr/bin/env bash
val="$(cat)"
files=()
for a in "$@"; do case "$a" in -*) ;; *) files+=("$a");; esac; done
ok=1
for f in "${files[@]}"; do
    if [ -n "${PI_HDMI_CONN:-}" ] && [ "$f" = "$PI_HDMI_CONN/status" ]; then
        case "$val" in
            on) printf 'connected\n' > "$PI_HDMI_CONN/status.data" ;;
            detect|detected|off) printf 'disconnected\n' > "$PI_HDMI_CONN/status.data" ;;
            *) echo "bash: $f: cannot write" >&2; ok=0 ;;
        esac
    else
        printf '%s' "$val" > "$f"
    fi
done
[ "$ok" = 1 ]
EOF
chmod +x "$FAKE_BIN/ssh" "$FAKE_BIN/sudo" "$FAKE_BIN/tee"

# setup_scenario <name> <initial-status> <initial-record|->
# PI_HDMI_CONN is a DIRECTORY on a real device (env.sh reads
# $PI_HDMI_CONN/status and $PI_HDMI_CONN/modes), so the surface is shaped
# like /sys/class/drm/<connector>. The status file is a symlink to
# status.data: the fake tee writes the NORMALIZED value to status.data
# (kernel rules, see the fake tee), every read follows the symlink.
setup_scenario() {
    local name="$1" init_status="$2" init_record="$3"
    local di="$TMP/$name"
    mkdir -p "$di"
    case "$init_status" in on|connected) init_status=connected ;; *) init_status=disconnected ;; esac
    printf '%s\n' "$init_status" > "$di/status.data"
    ln -sf status.data "$di/status"
    printf '1024x768 800x600 640x480\n' > "$di/modes"
    [ "$init_record" = "none" ] || printf '%s\n' "$init_record" > "$di/record"
}

# The inner runner script, written once so the per-scenario `bash` needs no
# nested quoting. Its inputs ride in VARIABLES, not positional params: a
# `source` rebinds "$@" to the caller's own positional parameters, so
# `source "$1"` after a source would glob the CWD if "$1" were empty
# (measured: running from a /tmp with files in it sourced the FIRST FILE).
cat > "$TMP/inner.sh" <<'EOF'
set -u
[ -n "${HDMI_ST_TRACE:-}" ] && set -x
source "$HDMI_ST_ENV"   # env.sh, unmodified
PI_HDMI_FORCED=0
eval "$HDMI_ST_BODY"
EOF

# run_scenario <name> <initial-status> <initial-record|-> <owner> <body>
# PASS is a zero exit of the body (which may assert on PI_HDMI_FORCED).
run_scenario() {
    local name="$1" init_status="$2" init_record="$3" owner="$4" body="$5"
    setup_scenario "$name" "$init_status" "$init_record"
    local di="$TMP/$name"
    PI_LANE=1 PI_HDMI_OWNER="$owner" \
    PI_HDMI_CONN="$di" PI_HDMI_RECORD="$di/record" \
    HDMI_ST_ENV="$SCRIPT_DIR/env.sh" HDMI_ST_BODY="$body" \
    SSH_LOG="$di/ssh.log" \
    SUDO_OK=1 PATH="$FAKE_BIN:$PATH" \
    bash "$TMP/inner.sh" </dev/null \
        > "$di/stdout" 2> "$di/stderr"
    local rc=$?
    [ "$rc" -eq 0 ] || {
        echo "FAIL[$name]: expected exit 0, got $rc"
        sed 's/^/    /' "$di/stderr"
        FAILS=$((FAILS + 1))
    }
}

# expect_reject <name> <initial-status> <initial-record|-> <owner> <body>
# PASS is a NON-ZERO exit (the refusal itself is the last command).
expect_reject() {
    local name="$1" init_status="$2" init_record="$3" owner="$4" body="$5"
    setup_scenario "$name" "$init_status" "$init_record"
    local di="$TMP/$name"
    PI_LANE=1 PI_HDMI_OWNER="$owner" \
    PI_HDMI_CONN="$di" PI_HDMI_RECORD="$di/record" \
    HDMI_ST_ENV="$SCRIPT_DIR/env.sh" HDMI_ST_BODY="$body" \
    SSH_LOG="$di/ssh.log" \
    SUDO_OK=1 PATH="$FAKE_BIN:$PATH" \
    bash "$TMP/inner.sh" </dev/null \
        > "$di/stdout" 2> "$di/stderr"
    local rc=$?
    [ "$rc" -ne 0 ] || {
        echo "FAIL[$name]: expected a refusal, got exit 0"
        sed 's/^/    /' "$di/stderr"
        FAILS=$((FAILS + 1))
    }
}

# check <scenario> <description> <command-that-must-succeed>
check() {
    local name="$1" desc="$2" cmd="$3"
    if eval "$cmd"; then
        echo "ok[$name]: $desc"
    else
        echo "FAIL[$name]: $desc"
        FAILS=$((FAILS + 1))
    fi
}

# ---- s1: the normal force ---------------------------------------------------
run_scenario s1 disconnected none self 'pi_hdmi_force_on; [ "$PI_HDMI_FORCED" = 1 ]'
check s1 "the kernel forced the connector (reads connected)" 'grep -qx connected "$TMP/s1/status.data"'
check s1 "record holds the pre-force status" 'grep -qx "self disconnected" "$TMP/s1/record"'

# ---- s2: crash recovery in a fresh shell ------------------------------------
# The record survives "the crash"; the connector still reads connected
# (forced). Restore writes detect back -- the kernel accepts it and, with no
# monitor, reads disconnected.
run_scenario s2 disconnected "self disconnected" self 'pi_hdmi_restore'
check s2 "detect restored (hotplug back, reads disconnected)" 'grep -qx disconnected "$TMP/s2/status.data"'
check s2 "record cleared" '[ ! -e "$TMP/s2/record" ]'

# ---- s3: an already-connected lane ------------------------------------------
run_scenario s3 connected none self 'pi_hdmi_force_on; [ "$PI_HDMI_FORCED" = 0 ]'
check s3 "no record written" '[ ! -e "$TMP/s3/record" ]'
check s3 "status untouched" 'grep -qx connected "$TMP/s3/status.data"'

# ---- s4: a failed sudo is a refusal, not a forced lane ----------------------
expect_reject s4 disconnected none self 'export SUDO_OK=0; pi_hdmi_force_on'
check s4 "no record written on a failed force" '[ ! -e "$TMP/s4/record" ]'
check s4 "status still disconnected" 'grep -qx disconnected "$TMP/s4/status.data"'

# ---- s5: a foreign owner's record is refused --------------------------------
expect_reject s5 disconnected "other-host disconnected" self 'pi_hdmi_force_on'
check s5 "connector left alone" 'grep -qx disconnected "$TMP/s5/status.data"'
check s5 "foreign record left in place" 'grep -qx "other-host disconnected" "$TMP/s5/record"'

# ---- s6: clean force, clean restore (the no-record path) ---------------- --------
run_scenario s6 disconnected none self 'pi_hdmi_force_on; pi_hdmi_restore; [ "$PI_HDMI_FORCED" = 0 ]'
check s6 "detect restored" 'grep -qx disconnected "$TMP/s6/status.data"'
check s6 "no record left" '[ ! -e "$TMP/s6/record" ]'

# ---- s7: a stale same-owner record is recovered, then the run proceeds ------
# The record ("self disconnected") and the forced-on connector survive "the
# crash"; the fresh shell's force_on restores (detect, accepted), clears,
# and re-forces (on, accepted -> connected).
run_scenario s7 connected "self disconnected" self 'pi_hdmi_force_on; [ "$PI_HDMI_FORCED" = 1 ]'
check s7 "status back to the forced state after the restore+force" 'grep -qx connected "$TMP/s7/status.data"'
check s7 "record carries this run's pre-force status" 'grep -qx "self disconnected" "$TMP/s7/record"'

echo
if [ "$FAILS" -eq 0 ]; then
    echo "PASS: B1034 HDMI force/restore contract (fake ssh/sudo, kernel-faithful store, 7 scenarios)"
    exit 0
fi
echo "FAIL: $FAILS assertion(s) failed (state in $TMP; WILLEQ_HDMI_SELFTEST_KEEP=1 to keep it)"
exit 1
