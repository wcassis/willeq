#!/bin/bash
# parse_pose_log.sh -- parse will client debug output to TSV pose data
#
# Reads lines from a log file and extracts position data from MOD_MOVEMENT
# debug output. Writes a TSV file.
#
# Usage: parse_pose_log.sh <logfile> <tsvfile>
#
# Expected log lines from will client with -d 2:
#   [HH:MM:SS.mmm] [DEBUG] [MOD_MOVEMENT] POS C->S spawn_id=X server_pos=(X.XX, Y.YY, Z.ZZ) heading=HH.H anim=A server_delta=(Dx, Dy, Dz)
#   [HH:MM:SS.mmm] [DEBUG] [MOD_MOVEMENT] POS C->S [SELF] client_pos=(X.XX,Y.YY,Z.ZZ) -> server_pos=(X.XX,Y.YY,Z.ZZ)
#   [HH:MM:SS.mmm] [DEBUG] [MOD_MOVEMENT] POS C->S [SELF] heading=HH.HHdeg -> server_heading=HH.HHdeg -> heading_scaled=S

set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <logfile> <tsvfile>" >&2
    exit 1
fi

LOGFILE="$1"
TSVFILE="$2"

# Write TSV header
echo -e "timestamp_ms\tframe_index\tplayer_x\tplayer_y\tplayer_z\tplayer_heading_deg\tserver_pos_x\tserver_pos_y\tserver_pos_z\tserver_heading_deg\tdelta_x\tdelta_y\tdelta_z" > "$TSVFILE"

# Use gawk for 3-argument match(). Buffer the last client_pos row and flush
# it (with heading) when the heading line arrives, or after a delay if no
# heading follows.
gawk '
function parse_ts(line,   tarr, h, m, s, ms_str, ms) {
    if (match(line, /\[([0-9]+):([0-9]+):([0-9]+)\.([0-9]+)\]/, tarr)) {
        h = int(tarr[1]); m = int(tarr[2]); s = int(tarr[3])
        ms_str = tarr[4]
        while (length(ms_str) < 3) ms_str = ms_str "0"
        ms = int(substr(ms_str, 1, 3))
        return h * 3600000 + m * 60000 + s * 1000 + ms
    }
    return 0
}

function flush_buffer() {
    if (!buf_ts) return
    printf "%d\t0\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t\t\t\n",
        buf_ts, buf_cx, buf_cy, buf_cz, buf_ch,
        buf_sx, buf_sy, buf_sz, buf_sh >> outfile
    fflush(outfile)
    buf_ts = ""
}

function emit_spawn(ts, sx, sy, sz, heading, dx, dy, dz) {
    printf "%d\t0\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\n",
        ts, sx, sy, sz, heading,
        sx, sy, sz, heading, dx, dy, dz >> outfile
    fflush(outfile)
}

BEGIN { buf_ts = 0; buf_cx = 0; buf_cy = 0; buf_cz = 0; buf_sx = 0; buf_sy = 0; buf_sz = 0; buf_ch = 0; buf_sh = 0 }

/POS C->S \[SELF\] client_pos=/ {
    flush_buffer()
    buf_ts = parse_ts($0)
    if (match($0, /client_pos=\(([^)]+)\)/, ca)) {
        split(ca[1], cp, ",")
        buf_cx = cp[1]+0; buf_cy = cp[2]+0; buf_cz = cp[3]+0
    }
    if (match($0, /server_pos=\(([^)]+)\)/, sa)) {
        split(sa[1], sp, ",")
        buf_sx = sp[1]+0; buf_sy = sp[2]+0; buf_sz = sp[3]+0
    }
    buf_ch = 0; buf_sh = 0
    next
}

/POS C->S spawn_id=/ {
    flush_buffer()
    ts = parse_ts($0)
    sx = 0; sy = 0; sz = 0; heading = 0; dx = 0; dy = 0; dz = 0
    if (match($0, /server_pos=\(([^)]+)\)/, sa)) {
        split(sa[1], sp, ",")
        gsub(/^ +/, "", sp[1]); gsub(/ +$/, "", sp[1])
        gsub(/^ +/, "", sp[2]); gsub(/ +$/, "", sp[2])
        gsub(/^ +/, "", sp[3]); gsub(/ +$/, "", sp[3])
        sx = sp[1]+0; sy = sp[2]+0; sz = sp[3]+0
    }
    if (match($0, /heading=([0-9.-]+)/, ha)) heading = ha[1]+0
    if (match($0, /server_delta=\(([^)]+)\)/, da)) {
        split(da[1], dp, ",")
        gsub(/^ +/, "", dp[1]); gsub(/ +$/, "", dp[1])
        gsub(/^ +/, "", dp[2]); gsub(/ +$/, "", dp[2])
        gsub(/^ +/, "", dp[3]); gsub(/ +$/, "", dp[3])
        dx = dp[1]+0; dy = dp[2]+0; dz = dp[3]+0
    }
    emit_spawn(ts, sx, sy, sz, heading, dx, dy, dz)
    next
}

/POS C->S \[SELF\] heading=/ {
    if (match($0, /heading=([0-9.-]+)deg.*server_heading=([0-9.-]+)deg/, ha)) {
        buf_ch = ha[1]+0; buf_sh = ha[2]+0
    }
    flush_buffer()
    next
}

END { flush_buffer() }
' outfile="$TSVFILE" "$LOGFILE"

echo "Pose data written to: $TSVFILE"
echo "Rows parsed: $(tail -n +2 "$TSVFILE" | wc -l)"
