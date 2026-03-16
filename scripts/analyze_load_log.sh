#!/bin/bash
# analyze_load_log.sh — Extract a structured summary of willeq zone loading from a log file.
#
# Usage: ./scripts/analyze_load_log.sh <logfile> [--full]
#
# Default: shows milestone lines + INFO/WARN/FATAL/summary lines between milestones.
# --full: also includes DEBUG lines (verbose, for deep-dive into a specific step).
#
# The sequential loader reports progress at specific percentages:
#   0%   Loading screen init
#   2%   Connecting to login server
#   5%   Authenticating
#  10%   Connecting to world server
#  15%   Loading characters
#  20%   Connecting to zone
#  25%   Receiving player data
#  30%   Receiving zone data
#  35%   Synchronizing
#  40%   Finalizing connection
#  45%   Player confirmed (OnGameStateComplete)
#  50%   Sequential loader Step 1: S3D parse
#  55%   Step 2: BSP compute + install
#  60%   Step 3: Atlas
#  63%   Step 4: Region meshes
#  71%   Step 5: Asset indexes
#  75%   Step 6: Objects
#  78%   Step 7: Doors
#  80%   Step 8: Entities
#  88%   Step 9: Collision
#  90%   Step 10: Sky/weather
#  93%   Step 11: Environment
#  96%   Step 12: Lighting
#  99%   Step 12b: SimulationWorker
# 100%   Step 13: Cleanup + finalize

set -uo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <logfile> [--full]"
    exit 1
fi

LOGFILE="$1"
FULL=false
if [ "${2:-}" = "--full" ]; then
    FULL=true
fi

if [ ! -f "$LOGFILE" ]; then
    echo "Error: $LOGFILE not found"
    exit 1
fi

# Milestone percentages in order
MILESTONES=(0 2 5 10 15 20 25 30 35 40 45 50 55 60 63 71 75 78 80 88 90 93 96 99 100)

# Step names for display
declare -A STEP_NAMES
STEP_NAMES[0]="Loading screen init"
STEP_NAMES[2]="Phase 1: Login connect"
STEP_NAMES[5]="Phase 2: Authenticate"
STEP_NAMES[10]="Phase 3: World connect"
STEP_NAMES[15]="Phase 4: Characters"
STEP_NAMES[20]="Phase 5: Zone connect"
STEP_NAMES[25]="Phase 6: Player data"
STEP_NAMES[30]="Phase 7: Zone data"
STEP_NAMES[35]="Phase 8: Synchronize"
STEP_NAMES[40]="Phase 9: Finalize connection"
STEP_NAMES[45]="Phase 10: Player confirmed"
STEP_NAMES[50]="SEQ Step 1: S3D parse"
STEP_NAMES[55]="SEQ Step 2: BSP"
STEP_NAMES[60]="SEQ Step 3: Atlas"
STEP_NAMES[63]="SEQ Step 4: Region meshes"
STEP_NAMES[71]="SEQ Step 5: Asset indexes"
STEP_NAMES[75]="SEQ Step 6: Objects"
STEP_NAMES[78]="SEQ Step 7: Doors"
STEP_NAMES[80]="SEQ Step 8: Entities"
STEP_NAMES[88]="SEQ Step 9: Collision"
STEP_NAMES[90]="SEQ Step 10: Sky/weather"
STEP_NAMES[93]="SEQ Step 11: Environment"
STEP_NAMES[96]="SEQ Step 12: Lighting"
STEP_NAMES[99]="SEQ Step 12b: Simulation"
STEP_NAMES[100]="SEQ Step 13: Cleanup"

# Find the line number of each milestone percentage in the log.
# Milestones appear as "($PCT%)" in GRAPHICS_LOAD lines or "[$PCT%]" in loadZoneSequential lines.
declare -A MILESTONE_LINES

for pct in "${MILESTONES[@]}"; do
    # Search for the percentage marker — first occurrence
    line=$(grep -n -m1 "(${pct}%)\|\\[${pct}%\\]" "$LOGFILE" 2>/dev/null | head -1 | cut -d: -f1)
    if [ -n "$line" ]; then
        MILESTONE_LINES[$pct]=$line
    fi
done

# Also find key events that aren't percentage-based
LOADER_COMPLETE=$(grep -n -m1 "loadZoneSequential: complete" "$LOGFILE" 2>/dev/null | head -1 | cut -d: -f1)
CHECK_LOADING=$(grep -n -m1 "checkLoadingComplete.*setting" "$LOGFILE" 2>/dev/null | head -1 | cut -d: -f1)
SCREEN_HIDDEN=$(grep -n -m1 "Loading screen hidden" "$LOGFILE" 2>/dev/null | head -1 | cut -d: -f1)
FIRST_FRAME=$(grep -n -m1 "sceneDrawAll" "$LOGFILE" 2>/dev/null | head -1 | cut -d: -f1)
GRAPHICS_COMPLETE=$(grep -n -m1 "Graphics loading complete" "$LOGFILE" 2>/dev/null | head -1 | cut -d: -f1)
PUBLISH_STATE=$(grep -n -m1 "Publishing full state" "$LOGFILE" 2>/dev/null | head -1 | cut -d: -f1)
EXIT_CODE=$(grep -n "Exit code:" "$LOGFILE" 2>/dev/null | tail -1 | cut -d: -f1)

echo "================================================================================"
echo "  WillEQ Load Log Analysis: $LOGFILE"
echo "================================================================================"
echo ""

# Print milestone summary
echo "--- Milestone Timeline ---"
for pct in "${MILESTONES[@]}"; do
    line="${MILESTONE_LINES[$pct]:-}"
    name="${STEP_NAMES[$pct]:-???}"
    if [ -n "$line" ]; then
        # Extract the timestamp from that line
        ts=$(sed -n "${line}p" "$LOGFILE" | grep -oP '\d{2}:\d{2}:\d{2}\.\d{3}' | head -1)
        printf "  %3d%%  %-35s  line %-8s  %s\n" "$pct" "$name" "$line" "${ts:-???}"
    else
        printf "  %3d%%  %-35s  ** NOT FOUND **\n" "$pct" "$name"
    fi
done

echo ""
echo "--- Key Events ---"
[ -n "$LOADER_COMPLETE" ] && echo "  Sequential loader complete:  line $LOADER_COMPLETE" || echo "  Sequential loader complete:  ** NOT FOUND **"
[ -n "$CHECK_LOADING" ] && echo "  checkLoadingComplete:        line $CHECK_LOADING" || echo "  checkLoadingComplete:        ** NOT FOUND **"
[ -n "$SCREEN_HIDDEN" ] && echo "  Loading screen hidden:       line $SCREEN_HIDDEN" || echo "  Loading screen hidden:       ** NOT FOUND **"
[ -n "$FIRST_FRAME" ] && echo "  First sceneDrawAll:          line $FIRST_FRAME" || echo "  First sceneDrawAll:          ** NOT FOUND **"
[ -n "$GRAPHICS_COMPLETE" ] && echo "  OnGraphicsComplete:          line $GRAPHICS_COMPLETE" || echo "  OnGraphicsComplete:          ** NOT FOUND **"
[ -n "$PUBLISH_STATE" ] && echo "  PublishFullStateSnapshot:    line $PUBLISH_STATE" || echo "  PublishFullStateSnapshot:    ** NOT FOUND **"
[ -n "$EXIT_CODE" ] && echo "  Exit:                        line $EXIT_CODE  $(sed -n "${EXIT_CODE}p" "$LOGFILE")" || echo "  Exit:                        ** NOT FOUND **"

# Count FATALs and errors
FATAL_COUNT=$(grep -c "FATAL" "$LOGFILE" 2>/dev/null || true)
ERROR_COUNT=$(grep -c "\[ERROR" "$LOGFILE" 2>/dev/null || true)
BLOCKED_COUNT=$(grep -c "BLOCKED unconstrained" "$LOGFILE" 2>/dev/null || true)
CONSTRAINED_COUNT=$(grep -c "\[CONSTRAINED\]" "$LOGFILE" 2>/dev/null || true)
MISSING_TEX_COUNT=$(grep -c "missing texture" "$LOGFILE" 2>/dev/null || true)

echo ""
echo "--- Counts ---"
echo "  FATAL:                $FATAL_COUNT"
echo "  ERROR:                $ERROR_COUNT"
echo "  BLOCKED unconstrained: $BLOCKED_COUNT"
echo "  [CONSTRAINED] loads:  $CONSTRAINED_COUNT"
echo "  Missing textures:     $MISSING_TEX_COUNT"

# Print per-step detail
echo ""
echo "================================================================================"
echo "  Per-Step Detail"
echo "================================================================================"

for i in $(seq 0 $((${#MILESTONES[@]} - 1))); do
    pct=${MILESTONES[$i]}
    start_line="${MILESTONE_LINES[$pct]:-}"
    name="${STEP_NAMES[$pct]:-???}"

    if [ -z "$start_line" ]; then
        continue
    fi

    # Find end line (next milestone or EOF)
    end_line=""
    for j in $(seq $((i + 1)) $((${#MILESTONES[@]} - 1))); do
        next_pct=${MILESTONES[$j]}
        next_line="${MILESTONE_LINES[$next_pct]:-}"
        if [ -n "$next_line" ]; then
            end_line=$next_line
            break
        fi
    done

    if [ -z "$end_line" ]; then
        # Use end of file
        end_line=$(wc -l < "$LOGFILE")
    fi

    # Count lines in this section
    section_lines=$((end_line - start_line))

    echo ""
    echo "--- ${pct}% ${name} (lines ${start_line}-${end_line}, ${section_lines} lines) ---"

    if [ "$FULL" = true ]; then
        # Full mode: show all INFO/WARN/FATAL/DEBUG (skip TRACE)
        sed -n "${start_line},${end_line}p" "$LOGFILE" | grep -v "\[TRACE\]" | head -100
        if [ "$section_lines" -gt 100 ]; then
            echo "  ... ($(( section_lines - 100 )) more lines, use grep for detail)"
        fi
    else
        # Summary mode: show FATAL/ERROR/WARN + key INFO lines (Sequential:, CONSTRAINED, BLOCKED, loaded, built, complete)
        sed -n "${start_line},${end_line}p" "$LOGFILE" | grep -E "FATAL|ERROR|\[WARN|\[INFO.*Sequential:|\[INFO.*CONSTRAINED|\[INFO.*BLOCKED|\[INFO.*loaded|\[INFO.*built|\[INFO.*complete|\[INFO.*installed|\[INFO.*uploaded|\[INFO.*released|\[INFO.*created|\[INFO.*enabled|\[INFO.*disabled|\[INFO.*skipped|logAssetBuildTime|seq_" | head -50
        if [ "$FATAL_COUNT" -gt 0 ] || [ "$BLOCKED_COUNT" -gt 0 ]; then
            # Also show any FATALs or BLOCKEDs in this section
            sed -n "${start_line},${end_line}p" "$LOGFILE" | grep -E "FATAL|BLOCKED" | head -20
        fi
    fi
done

echo ""
echo "================================================================================"
echo "  Analysis complete"
echo "================================================================================"
