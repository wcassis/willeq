#!/bin/bash
# scripts/eqemu/pose_logger.sh -- capture player pose from client debug output
#
# The will client logs position data when started with -d 2 (debug level 2+):
#   MOD_MOVEMENT: "POS C->S spawn_id=1 server_pos=(1234.56, -1567.89, 10.00) heading=0.0"
#   MOD_MOVEMENT: "POS C->S [SELF] client_pos=(1234.56,-1567.89,10.00) -> server_pos=(1234.56,-1567.89,10.00)"
#   MOD_MOVEMENT: "POS C->S [SELF] heading=90.00deg -> server_heading=90.00deg -> heading_scaled=0"
#
# This script runs the client with -d 2, greps for MOD_MOVEMENT lines, and
# writes them to a TSV file.
#
# Usage:
#   source scripts/eqemu/env.sh
#   pose_logger_start <output.tsv>   # starts background capture to output.tsv
#   pose_logger_stop                 # stops the background capture
#   pose_logger_query <output.tsv>   # shows last pose from TSV file
#
# Output format (TSV):
#   timestamp_ms	frame_index	player_x	player_y	player_z	player_heading_deg	server_pos_x	server_pos_y	server_pos_z	server_heading_deg	delta_x	delta_y	delta_z
#
# Example:
#   pose_logger_start /tmp/pose.tsv
#   # ... run client ...
#   pose_logger_stop
#   pose_logger_query /tmp/pose.tsv

set -euo pipefail

# Source env.sh to get eqemu paths and server_exec function
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

# --- Constants ---
POSE_LOG_DIR="$HOME/.pose_logger"
POSE_LOG_PID_FILE="$POSE_LOG_DIR/willeq_pos.pid"
POSE_LOG_LOGFILE="$POSE_LOG_DIR/willeq_pos.log"

# --- Helper Functions ---

ensure_pose_log_dir() {
    mkdir -p "$POSE_LOG_DIR" 2>/dev/null || true
}

get_server_binary_path() {
    # Resolve the server binary path
    local server_bin="server_exec"
    
    if [[ -n "${EQT_SERVER_BIN:-}" ]]; then
        echo "$EQT_SERVER_BIN"
        return 0
    fi
    
    if [[ -f "/home/eqemu/server/bin/willeq" ]]; then
        echo "/home/eqemu/server/bin/willeq"
        return 0
    fi
    
    echo "server_exec"
}

get_willeq_client_path() {
    # Get the willeq client binary path from build output
    if [[ -n "${EQT_CLIENT_BIN:-}" ]]; then
        echo "$EQT_CLIENT_BIN"
        return 0
    fi
    
    # Try common build locations
    local client_bin="build/bin/willeq"
    
    if [[ -f "$client_bin" ]]; then
        echo "$client_bin"
        return 0
    fi
    
    # Try from source dir
    if [[ -n "${SOURCE_DIR:-}" ]]; then
        if [[ -f "${SOURCE_DIR}/build/bin/willeq" ]]; then
            echo "${SOURCE_DIR}/build/bin/willeq"
            return 0
        fi
    fi
    
    echo "build/bin/willeq"
}

# --- Main Commands ---

pose_logger_start() {
    local output_tsv="${1:-$POSE_LOG_DIR/pose.tsv}"
    
    ensure_pose_log_dir
    
    if [[ -f "$POSE_LOG_PID_FILE" ]]; then
        local pid
        pid=$(cat "$POSE_LOG_PID_FILE")
        if kill -0 "$pid" 2>/dev/null; then
            echo "ERROR: A pose logger instance is already running (PID: $pid)."
            echo "Use 'pose_logger_stop' first or specify a different output file."
            return 1
        else
            # Stale PID file
            rm -f "$POSE_LOG_PID_FILE"
        fi
    fi
    
    echo "Starting pose logger..."
    echo "  Output TSV: $output_tsv"
    echo "  Debug log:  $POSE_LOG_LOGFILE"
    
    # Run the client with -d 2 (debug level 2+) to enable position logging
    # The -d 2 flag enables MOD_MOVEMENT logging which contains position data
    # We pipe stderr to our log file and filter for pose data
    
    local client_bin
    client_bin=$(get_willeq_client_path)
    
    if [[ ! -f "$client_bin" ]]; then
        echo "ERROR: willeq client binary not found at: $client_bin"
        echo "Build the client first with: cmake --build build -j$(nproc)"
        return 1
    fi
    
    # Start the client with debug level 2
    # The client will log position updates to stderr/stdout
    # We capture both to the log file and parse to TSV
    
    # Start the server first
    if [[ -n "${EQT_SERVER_BIN:-}" ]]; then
        # If server_exec is available, use it
        "$EQT_SERVER_BIN" start 2>/dev/null || echo "Note: Server start attempted (may already be running)"
    fi
    
    # Start the client with debug output capture
    (
        "$client_bin" -c willeq.json -d 2 2>&1 | tee "$POSE_LOG_LOGFILE" | \
        bash "$SCRIPT_DIR/parse_pose_log.sh" "$POSE_LOG_LOGFILE" "$output_tsv"
    ) &
    
    local pid=$!
    echo "$pid" > "$POSE_LOG_PID_FILE"
    echo "Pose logger started (PID: $pid)"
    echo "  Watching: $POSE_LOG_LOGFILE"
    echo "  Writing:  $output_tsv"
    echo "  Stop with: pose_logger_stop"
}

pose_logger_stop() {
    if [[ ! -f "$POSE_LOG_PID_FILE" ]]; then
        echo "No pose logger instance found."
        return 0
    fi
    
    local pid
    pid=$(cat "$POSE_LOG_PID_FILE")
    
    if kill -0 "$pid" 2>/dev/null; then
        echo "Stopping pose logger (PID: $pid)..."
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        echo "Pose logger stopped."
    else
        echo "Pose logger process (PID: $pid) is not running."
    fi
    
    rm -f "$POSE_LOG_PID_FILE"
}

pose_logger_query() {
    local output_tsv="${1:-$POSE_LOG_DIR/pose.tsv}"
    
    if [[ ! -f "$output_tsv" ]]; then
        echo "ERROR: Output file not found: $output_tsv"
        return 1
    fi
    
    local line_count
    line_count=$(wc -l < "$output_tsv")
    
    if [[ "$line_count" -eq 0 ]]; then
        echo "No pose data recorded yet."
        return 0
    fi
    
    echo "Pose logger output: $output_tsv ($line_count rows)"
    echo ""
    
    # Show last 10 rows (or fewer if total < 10)
    local show_count=10
    if [[ "$line_count" -lt 10 ]]; then
        show_count=$line_count
    fi
    
    echo "Last $show_count rows:"
    tail -n "$show_count" "$output_tsv"
    
    # Show summary stats
    local total_poses
    total_poses=$(grep -c 'POS C->S \[' "$output_tsv" 2>/dev/null || echo "0")
    local unique_positions
    unique_positions=$(awk -F'\t' '{print $3","$4","$5}' "$output_tsv" | sort -u | wc -l)
    
    echo ""
    echo "Summary:"
    echo "  Total pose records: $total_poses"
    echo "  Unique positions: $unique_positions"
}

# Show usage help
pose_logger_help() {
    cat <<EOF
Usage: pose_logger.sh [command] [options]

Commands:
  pose_logger_start [output.tsv]   Start pose logging to TSV file
                                    Default: $POSE_LOG_DIR/pose.tsv
                                    (client runs with -d 2 for debug output)
  pose_logger_stop                 Stop the running pose logger
  pose_logger_query [output.tsv]   Show last pose data from TSV file
  pose_logger_help                 Show this help message

Output format (TSV columns):
  timestamp_ms    frame_index     player_x      player_y      player_z
  player_heading_deg  server_pos_x  server_pos_y  server_pos_z  server_heading_deg
  delta_x         delta_y         delta_z

Examples:
  # Start logging to /tmp/pose.tsv
  pose_logger_start /tmp/pose.tsv
  
  # Stop logging
  pose_logger_stop
  
  # Query last poses
  pose_logger_query /tmp/pose.tsv

Requirements:
  - Server must be running (eqemu or local)
  - Client binary must be available at: $(get_willeq_client_path)
  - Debug level 2+ enabled (-d 2)
EOF
}

# Main entry point
case "${1:-help}" in
    start)
        shift
        pose_logger_start "$@"
        ;;
    stop)
        pose_logger_stop
        ;;
    query)
        shift
        pose_logger_query "$@"
        ;;
    help|--help|-h)
        pose_logger_help
        ;;
    *)
        echo "ERROR: Unknown command: $1"
        pose_logger_help
        exit 1
        ;;
esac
