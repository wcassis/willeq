#!/bin/bash
# analyze_load_log.sh — Structured analysis of willeq logs for debugging.
#
# Usage: ./scripts/analyze_load_log.sh <logfile> [--full] [--phase PHASE]
#
# Sorts all log lines by timestamp to handle multi-threaded out-of-order output,
# then walks every line sequentially, extracting structured data for each phase.
#
# Modes:
#   (default)     Summary: milestones, key metrics, warnings/errors (deduped), frame timing
#   --full        Verbose: all INFO/WARN/ERROR lines shown per phase (TRACE aggregated)
#   --phase NAME  Show only the named phase (e.g. --phase "SEQ Step 2" or --phase "Gameplay")
#
# Handles all debug levels (-d 0 through -d 6):
#   -d 4 (INFO):  Full speed, shows milestones and key metrics
#   -d 6 (TRACE): May impact FPS, TRACE lines are counted/aggregated, not dumped

set -uo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <logfile> [--full] [--phase PHASE]"
    exit 1
fi

LOGFILE="$1"
shift
FULL=false
PHASE_FILTER=""

while [ $# -gt 0 ]; do
    case "$1" in
        --full) FULL=true; shift ;;
        --phase) PHASE_FILTER="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [ ! -f "$LOGFILE" ]; then
    echo "Error: $LOGFILE not found"
    exit 1
fi

# Create a timestamp-sorted temp file for ordered processing
SORTED=$(mktemp)
trap "rm -f '$SORTED'" EXIT

# Sort lines that have timestamps; preserve non-timestamped lines in original order
# Timestamp format: [YYYY-MM-DD HH:MM:SS.mmm]
# Lines without timestamps (e.g. raw output, Exit code) get a fake early timestamp to stay in order
{
    line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if echo "$line" | grep -qP '^\[?\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\]?'; then
            # Extract timestamp for sorting
            ts=$(echo "$line" | grep -oP '\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}')
            printf '%s|%08d|%s\n' "$ts" "$line_num" "$line"
        else
            # Non-timestamped lines: use a sort key that keeps them near their original position
            # Map line number to a fake timestamp that preserves order
            printf '0000-00-00 00:00:00.000|%08d|%s\n' "$line_num" "$line"
        fi
    done < "$LOGFILE"
} | sort -t'|' -k1,1 -k2,2n | while IFS='|' read -r _ts _num line; do echo "$line"; done > "$SORTED"

TOTAL_LINES=$(wc -l < "$SORTED")

# ============================================================================
# Utility functions
# ============================================================================

# Extract timestamp from a log line (returns HH:MM:SS.mmm)
get_ts() {
    echo "$1" | grep -oP '\d{2}:\d{2}:\d{2}\.\d{3}' | head -1
}

# Convert HH:MM:SS.mmm to milliseconds
ts_to_ms() {
    local ts="$1"
    if [ -z "$ts" ]; then echo 0; return; fi
    local h m s ms
    h=$(echo "$ts" | cut -d: -f1 | sed 's/^0//')
    m=$(echo "$ts" | cut -d: -f2 | sed 's/^0//')
    s=$(echo "$ts" | cut -d: -f3 | cut -d. -f1 | sed 's/^0//')
    ms=$(echo "$ts" | cut -d. -f2 | sed 's/^0*//')
    [ -z "$h" ] && h=0; [ -z "$m" ] && m=0; [ -z "$s" ] && s=0; [ -z "$ms" ] && ms=0
    echo $(( h * 3600000 + m * 60000 + s * 1000 + ms ))
}

# Duration between two timestamps in ms
duration_ms() {
    local start_ms end_ms
    start_ms=$(ts_to_ms "$1")
    end_ms=$(ts_to_ms "$2")
    echo $(( end_ms - start_ms ))
}

# ============================================================================
# Phase 0: Detect log metadata
# ============================================================================

# Detect debug level
DEBUG_LEVEL=$(grep -oP 'debug level \K\d+' "$SORTED" | head -1)
[ -z "$DEBUG_LEVEL" ] && DEBUG_LEVEL="?"

# Detect zone name
ZONE_NAME=$(grep -oP "zone='?\K[^'\")]+" "$SORTED" | head -1)
[ -z "$ZONE_NAME" ] && ZONE_NAME="unknown"

# Detect resolution
RESOLUTION=$(grep -oP 'Using mode \K\d+x\d+' "$SORTED" | head -1)
[ -z "$RESOLUTION" ] && RESOLUTION=$(grep -oP 'Renderer initialized.*\(\K\d+x\d+' "$SORTED" | head -1)
[ -z "$RESOLUTION" ] && RESOLUTION="?"

# Detect constrained mode
CONSTRAINED=$(grep -oP 'Constrained rendering mode: \K.*' "$SORTED" | head -1)
[ -z "$CONSTRAINED" ] && CONSTRAINED="none"

# Count lines by level
FATAL_COUNT=$(grep -c '\[FATAL\]' "$SORTED" || true)
ERROR_COUNT=$(grep -c '\[ERROR' "$SORTED" || true)
WARN_COUNT=$(grep -c '\[WARN' "$SORTED" || true)
INFO_COUNT=$(grep -c '\[INFO' "$SORTED" || true)
DEBUG_COUNT=$(grep -c '\[DEBUG\]' "$SORTED" || true)
TRACE_COUNT=$(grep -c '\[TRACE\]' "$SORTED" || true)

# Detect if TRACE is present (impacts performance analysis)
HAS_TRACE=false
[ "$TRACE_COUNT" -gt 0 ] && HAS_TRACE=true

echo "================================================================================"
echo "  WillEQ Log Analysis: $(basename "$LOGFILE")"
echo "================================================================================"
echo ""
echo "  Zone: $ZONE_NAME | Resolution: $RESOLUTION | Debug Level: $DEBUG_LEVEL"
echo "  Constrained: $CONSTRAINED"
echo "  Total lines: $TOTAL_LINES"
echo "  FATAL=$FATAL_COUNT ERROR=$ERROR_COUNT WARN=$WARN_COUNT INFO=$INFO_COUNT DEBUG=$DEBUG_COUNT TRACE=$TRACE_COUNT"
if $HAS_TRACE; then
    echo "  ** TRACE enabled — FPS numbers may be impacted by logging overhead **"
fi
echo ""

# ============================================================================
# Phase 1: Initialization (before connection)
# ============================================================================

show_phase() {
    local name="$1"
    if [ -n "$PHASE_FILTER" ] && [ "$PHASE_FILTER" != "$name" ]; then
        return 1
    fi
    return 0
}

# --- Init Phase ---
if show_phase "Init"; then
    echo "================================================================================"
    echo "  INIT PHASE"
    echo "================================================================================"

    # DRM
    DRM_MODE=$(grep -oP 'Using mode \K.*' "$SORTED" | head -1)
    [ -n "$DRM_MODE" ] && echo "  DRM mode: $DRM_MODE"

    # EGL
    EGL_VER=$(grep -oP 'EGL: Version \K.*' "$SORTED" | head -1)
    [ -n "$EGL_VER" ] && echo "  EGL: $EGL_VER"

    # GLES2 driver
    GLES2_DRIVER=$(grep -oP 'GLES2 Driver: \K.*' "$SORTED" | head -1)
    [ -n "$GLES2_DRIVER" ] && echo "  GPU: $GLES2_DRIVER"

    # Shader cache
    SHADER_CACHE_COUNT=$(grep -c 'Shader loaded from cache' "$SORTED" || true)
    SHADER_COMPILE_COUNT=$(grep -c 'shader compiled' "$SORTED" || true)
    echo "  Shaders: $SHADER_CACHE_COUNT cached, $SHADER_COMPILE_COUNT compiled"

    # GPU upload thread
    GPU_UPLOAD=$(grep -c 'GPU upload thread.*initialized\|GPUUploadThread.*OK' "$SORTED" || true)
    [ "$GPU_UPLOAD" -gt 0 ] && echo "  GPU upload thread: initialized"

    # Renderer init timing
    RENDERER_INIT_TS=$(get_ts "$(grep 'Renderer initialized successfully' "$SORTED" | head -1)")
    STARTUP_TS=$(get_ts "$(grep 'Starting WillEQ' "$SORTED" | head -1)")
    if [ -n "$RENDERER_INIT_TS" ] && [ -n "$STARTUP_TS" ]; then
        echo "  Renderer init: $(duration_ms "$STARTUP_TS" "$RENDERER_INIT_TS")ms"
    fi

    # Texture cache
    TEX_CACHE=$(grep -oP 'Texture cache created \(\K[^)]+' "$SORTED" | head -1)
    [ -n "$TEX_CACHE" ] && echo "  Texture cache: $TEX_CACHE"

    # Spells
    SPELL_COUNT=$(grep -oP 'Loaded \K\d+ spells' "$SORTED" | head -1)
    [ -n "$SPELL_COUNT" ] && echo "  Spells: $SPELL_COUNT"

    echo ""
fi

# --- Connection Phases ---
if show_phase "Connection"; then
    echo "================================================================================"
    echo "  CONNECTION PHASES"
    echo "================================================================================"

    # Extract each [LOADING] phase line
    while IFS= read -r line; do
        ts=$(get_ts "$line")
        phase=$(echo "$line" | grep -oP '\[LOADING\]\K.*')
        printf "  %s  %s\n" "$ts" "$phase"
    done < <(grep '\[LOADING\]' "$SORTED")

    # Key connection data
    PLAYER_POS=$(grep -oP 'PlayerProfile position: \K.*' "$SORTED" | head -1)
    [ -n "$PLAYER_POS" ] && echo "  Player position: $PLAYER_POS"

    SPAWN_COUNT=$(grep -oP 'Loaded \K\d+ spawns' "$SORTED" | head -1)
    [ -n "$SPAWN_COUNT" ] && echo "  Zone spawns: $SPAWN_COUNT"

    DOOR_COUNT=$(grep -oP 'Received \K\d+ doors' "$SORTED" | head -1)
    [ -n "$DOOR_COUNT" ] && echo "  Doors received: $DOOR_COUNT"

    MAP_INFO=$(grep -oP 'Loaded V\d+ map: \K.*' "$SORTED" | head -1)
    [ -n "$MAP_INFO" ] && echo "  Map: $MAP_INFO"

    ZONE_LINES=$(grep -oP 'Loaded \K\d+ zone lines for' "$SORTED" | head -1)
    [ -n "$ZONE_LINES" ] && echo "  Zone lines: $ZONE_LINES"

    # Connection timing
    CONNECT_START=$(get_ts "$(grep 'Connecting to login server' "$SORTED" | head -1)")
    CONNECT_END=$(get_ts "$(grep 'Zone connection complete' "$SORTED" | head -1)")
    if [ -n "$CONNECT_START" ] && [ -n "$CONNECT_END" ]; then
        echo "  Connection time: $(duration_ms "$CONNECT_START" "$CONNECT_END")ms"
    fi

    echo ""
fi

# ============================================================================
# Sequential Loading Steps
# ============================================================================

if show_phase "Sequential Loading"; then
    echo "================================================================================"
    echo "  SEQUENTIAL LOADING"
    echo "================================================================================"

    SEQ_START=$(get_ts "$(grep 'loadZoneSequential: starting' "$SORTED" | head -1)")
    SEQ_END=$(get_ts "$(grep 'loadZoneSequential: complete' "$SORTED" | head -1)")
    SEQ_TOTAL=$(grep -oP 'loadZoneSequential: complete.*in \K\d+ms' "$SORTED" | head -1)
    echo "  Total: ${SEQ_TOTAL:-?}"
    echo ""

    # --- Step 1: S3D Parse (50%) ---
    echo "  --- Step 1: S3D Parse (50%) ---"
    S3D_TIME=$(grep -oP 'seq_s3d_parse.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$S3D_TIME" ] && echo "    Time: $S3D_TIME"

    # --- Step 2: BSP Compute (55%) ---
    echo "  --- Step 2: BSP Compute (55%) ---"
    PORTAL_INFO=$(grep -oP 'Portal system: \K.*' "$SORTED" | head -1)
    [ -n "$PORTAL_INFO" ] && echo "    Portals: $PORTAL_INFO"
    BSP_TIME=$(grep -oP 'seq_bsp_compute.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$BSP_TIME" ] && echo "    BSP compute: $BSP_TIME"
    BSP_INSTALL=$(grep -oP 'seq_bsp_install.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$BSP_INSTALL" ] && echo "    BSP install: $BSP_INSTALL"

    # --- Step 3: Atlas (60%) ---
    echo "  --- Step 3: Atlas (60%) ---"
    ATLAS_TIME=$(grep -oP 'seq_atlas.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$ATLAS_TIME" ] && echo "    Time: $ATLAS_TIME" || echo "    (no atlas timing logged)"

    # --- Step 4: Region Meshes (63%) ---
    echo "  --- Step 4: Region Meshes (63%) ---"
    REGION_BUILD=$(grep -oP 'built \K\d+ region meshes eagerly \([^)]+\)' "$SORTED" | head -1)
    [ -n "$REGION_BUILD" ] && echo "    Built: $REGION_BUILD"
    SORT_ENABLED=$(grep 'front-to-back sorting' "$SORTED" | head -1)
    [ -n "$SORT_ENABLED" ] && echo "    Sorting: enabled"
    REGION_TIME=$(grep -oP 'seq_regions.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$REGION_TIME" ] && echo "    Time: $REGION_TIME"

    # --- Step 5: Asset Indexes (71%) ---
    echo "  --- Step 5: Asset Indexes (71%) ---"
    ARCHIVE_INDEX=$(grep -oP 'GraphicsArchiveIndex: \K.*' "$SORTED" | head -1)
    [ -n "$ARCHIVE_INDEX" ] && echo "    Archive: $ARCHIVE_INDEX"
    EQUIP_SUMMARY=$(grep -oP 'Sequential: equipment index \K.*' "$SORTED" | head -1)
    [ -n "$EQUIP_SUMMARY" ] && echo "    Equipment: $EQUIP_SUMMARY"
    ASSET_TIME=$(grep -oP 'seq_assets.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$ASSET_TIME" ] && echo "    Time: $ASSET_TIME"
    # Frag2C warnings (deduped)
    FRAG2C_COUNT=$(grep -c 'Frag2C.*NOT FOUND' "$SORTED" || true)
    [ "$FRAG2C_COUNT" -gt 0 ] && echo "    WARN: $FRAG2C_COUNT Frag2C 'NOT FOUND' warnings (brush set reference errors)"

    # --- Step 6: Objects (75%) ---
    echo "  --- Step 6: Objects (75%) ---"
    OBJ_BUILD=$(grep -oP 'Sequential: built all \K\d+ object' "$SORTED" | head -1)
    [ -n "$OBJ_BUILD" ] && echo "    Built: $OBJ_BUILD meshes"

    # --- Step 7: Doors (78%) ---
    echo "  --- Step 7: Doors (78%) ---"
    DOOR_BUILD=$(grep -oP 'Sequential: rebuilt \K[^$]+' "$SORTED" | head -1)
    [ -n "$DOOR_BUILD" ] && echo "    Built: $DOOR_BUILD"
    DOOR_TIME=$(grep -oP 'seq_doors.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$DOOR_TIME" ] && echo "    Time: $DOOR_TIME"

    # --- Step 8: Entities (80%) ---
    echo "  --- Step 8: Entities (80%) ---"
    ENT_PREP=$(grep -oP 'Sequential: prepped \K[^$]+' "$SORTED" | head -1)
    [ -n "$ENT_PREP" ] && echo "    Prepped: $ENT_PREP"
    ENT_UPLOAD=$(grep -oP 'SEQ Step8: uploadAllPending uploaded \K[^$]+' "$SORTED" | head -1)
    [ -n "$ENT_UPLOAD" ] && echo "    Textures: uploaded $ENT_UPLOAD"
    ENT_TIME=$(grep -oP 'seq_entities.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$ENT_TIME" ] && echo "    Time: $ENT_TIME"
    # Slow entity builds
    SLOW_ENT_COUNT=$(grep -c 'PERF: buildEntityMesh' "$SORTED" || true)
    if [ "$SLOW_ENT_COUNT" -gt 0 ]; then
        echo "    Slow entity builds: $SLOW_ENT_COUNT"
        # Show top 3 slowest
        grep 'PERF: buildEntityMesh' "$SORTED" | grep -oP 'for \K\S+ race \d+ took \d+ ms' | sort -t' ' -k5 -rn | head -3 | while read -r line; do
            echo "      $line"
        done
    fi
    # Race preloads
    RACE_LOADS=$(grep -c 'Sequential: preload race' "$SORTED" || true)
    RACE_CACHE=$(grep -c 'animation merge using cached' "$SORTED" || true)
    RACE_DISK=$(grep -c 'animation merge loading.*from disk' "$SORTED" || true)
    echo "    Race preloads: $RACE_LOADS total ($RACE_CACHE cached, $RACE_DISK disk)"

    # --- Step 9: Collision (88%) ---
    echo "  --- Step 9: Collision (88%) ---"
    COLL_INFO=$(grep -oP 'Collision setup: \K.*' "$SORTED" | head -1)
    [ -n "$COLL_INFO" ] && echo "    Setup: $COLL_INFO"
    COLL_TIME=$(grep -oP 'seq_collision.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$COLL_TIME" ] && echo "    Time: $COLL_TIME"

    # --- Step 10: Sky/Weather (90%) ---
    echo "  --- Step 10: Sky/Weather (90%) ---"
    SKY_INFO=$(grep -oP 'Sky loaded: \K.*' "$SORTED" | tail -1)
    [ -n "$SKY_INFO" ] && echo "    Sky: $SKY_INFO"
    SKY_TIME=$(grep -oP 'seq_sky.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$SKY_TIME" ] && echo "    Time: $SKY_TIME"

    # --- Step 11: Environment (93%) ---
    echo "  --- Step 11: Environment (93%) ---"
    SURFACE_MAP=$(grep -oP "Loaded surface map from \K[^$]+" "$SORTED" | head -1)
    [ -n "$SURFACE_MAP" ] && echo "    Surface map: $SURFACE_MAP"
    DETAIL_BSP=$(grep -oP 'DetailManager: BSP tree loaded with \K.*' "$SORTED" | head -1)
    [ -n "$DETAIL_BSP" ] && echo "    Detail BSP: $DETAIL_BSP"
    FIRE_SOURCES=$(grep -oP 'Enqueued CreateFireEmitters for \K\d+' "$SORTED" | head -1)
    [ -n "$FIRE_SOURCES" ] && echo "    Fire sources: $FIRE_SOURCES"
    FIRE_LIGHTS=$(grep -oP 'Collected \K\d+ fire glow lights' "$SORTED" | head -1)
    [ -n "$FIRE_LIGHTS" ] && echo "    Fire lights: $FIRE_LIGHTS"
    ENV_TIME=$(grep -oP 'seq_env.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$ENV_TIME" ] && echo "    Time: $ENV_TIME"

    # --- Step 12: Lighting (96%) ---
    echo "  --- Step 12: Lighting (96%) ---"
    # (usually fast, no dedicated timing)
    echo "    (usually < 1ms, no dedicated log)"

    # --- Step 12b: Simulation (99%) ---
    echo "  --- Step 12b: Simulation (99%) ---"
    PVS_INFO=$(grep -oP 'PVS lookup tables built: \K.*' "$SORTED" | head -1)
    [ -n "$PVS_INFO" ] && echo "    PVS: $PVS_INFO"
    SIM_ZONE=$(grep -oP 'SimulationWorker zone data set: \K.*' "$SORTED" | head -1)
    [ -n "$SIM_ZONE" ] && echo "    SimWorker data: $SIM_ZONE"
    SIM_FIRST=$(grep -oP 'SimulationWorker first-frame.*\K\(.*\)' "$SORTED" | head -1)
    [ -n "$SIM_FIRST" ] && echo "    First frame: $SIM_FIRST"
    SIM_TIME=$(grep -oP 'seq_simulation.*took \K[0-9.]+ms' "$SORTED" | head -1)
    [ -n "$SIM_TIME" ] && echo "    Time: $SIM_TIME"
    CAMERA_BSP=$(grep -oP 'Camera BSP collision initialized for region \K\d+' "$SORTED" | head -1)
    [ -n "$CAMERA_BSP" ] && echo "    Camera BSP region: $CAMERA_BSP"

    # --- Step 13: Finalize (100%) ---
    echo "  --- Step 13: Finalize (100%) ---"
    TEX_RELEASE=$(grep -oP 'Sequential: released \K[0-9.]+MB texture' "$SORTED" | head -1)
    [ -n "$TEX_RELEASE" ] && echo "    Released: ${TEX_RELEASE} data"
    CHAR_RELEASE=$(grep -c 'released zone character data\|released WLD post-load' "$SORTED" || true)
    [ "$CHAR_RELEASE" -gt 0 ] && echo "    Cleanup: character + WLD data released"

    echo ""
fi

# ============================================================================
# Scene Dump (dumpscene)
# ============================================================================

if show_phase "Scene Dump"; then
    SCENE_DUMP=$(grep -c 'SCENE DUMP' "$SORTED" || true)
    if [ "$SCENE_DUMP" -gt 0 ]; then
        echo "================================================================================"
        echo "  SCENE DUMP (post-load snapshot)"
        echo "================================================================================"

        PVS_REGION=$(grep -oP 'currentPvsRegion_=\K\d+' "$SORTED" | head -1)
        PVS_ENABLED=$(grep -oP 'usePvsCulling_=\K\w+' "$SORTED" | head -1)
        echo "  PVS region: ${PVS_REGION:-?} | PVS culling: ${PVS_ENABLED:-?}"

        SCENE_NODES=$(grep -oP 'Total Irrlicht scene graph nodes: \K\d+' "$SORTED" | head -1)
        echo "  Scene graph nodes: ${SCENE_NODES:-?}"

        # Entity summary
        ENT_SUMMARY=$(grep -oP 'ENTITIES: \K.*' "$SORTED" | head -1)
        echo "  Entities: ${ENT_SUMMARY:-?}"

        # BSP region assignment analysis
        ENT_WITH_BSP=$(grep '\[NPC\]\|PLAYER' "$SORTED" | grep -v 'bspRegion=-1' | grep -c 'bspRegion=' || true)
        ENT_NO_BSP=$(grep '\[NPC\]\|PLAYER' "$SORTED" | grep -c 'bspRegion=-1' || true)
        ENT_IN_GRAPH=$(grep '\[NPC\]\|PLAYER' "$SORTED" | grep -c 'inGraph=true' || true)
        echo "    With BSP region: $ENT_WITH_BSP | No BSP region (=-1): $ENT_NO_BSP | In scene graph: $ENT_IN_GRAPH"

        # Player specifically
        PLAYER_LINE=$(grep '\[PLAYER\]' "$SORTED" | head -1)
        if [ -n "$PLAYER_LINE" ]; then
            PLAYER_POS=$(echo "$PLAYER_LINE" | grep -oP "pos=\K\([^)]+\)")
            PLAYER_BSP=$(echo "$PLAYER_LINE" | grep -oP "bspRegion=\K-?\d+")
            PLAYER_INGRAPH=$(echo "$PLAYER_LINE" | grep -oP "inGraph=\K\w+")
            echo "    PLAYER: pos=$PLAYER_POS bspRegion=$PLAYER_BSP inGraph=$PLAYER_INGRAPH"
        fi

        # Door summary
        DOOR_SUMMARY=$(grep -oP 'DOORS: \K.*' "$SORTED" | head -1)
        echo "  Doors: ${DOOR_SUMMARY:-?}"

        # Object summary
        OBJ_SUMMARY=$(grep -oP 'OBJECTS: \K.*' "$SORTED" | head -1)
        echo "  Objects: ${OBJ_SUMMARY:-?}"

        # Zone lights
        LIGHT_SUMMARY=$(grep -oP 'ZONE LIGHTS: \K.*' "$SORTED" | head -1)
        echo "  Zone lights: ${LIGHT_SUMMARY:-?}"

        # Zone meshes
        MESH_SUMMARY=$(grep -oP 'ZONE MESHES: \K.*' "$SORTED" | head -1)
        echo "  Zone meshes: ${MESH_SUMMARY:-?}"

        # Trees
        TREE_SUMMARY=$(grep -oP 'ANIMATED TREES: \K.*' "$SORTED" | head -1)
        echo "  Trees: ${TREE_SUMMARY:-?}"

        # Sky/Weather/Other
        SKY_NODES=$(grep -oP 'SKY NODES: \K\d+' "$SORTED" | head -1)
        WEATHER_NODES=$(grep -oP 'WEATHER NODES: \K\d+' "$SORTED" | head -1)
        echo "  Sky nodes: ${SKY_NODES:-0} | Weather nodes: ${WEATHER_NODES:-0}"

        # Fire glow
        FIRE_GLOW=$(grep -oP 'FIRE GLOW: \K.*' "$SORTED" | head -1)
        [ -n "$FIRE_GLOW" ] && echo "  Fire glow: $FIRE_GLOW"

        # VISIBLE line — critical for culling analysis
        VISIBLE=$(grep -oP 'VISIBLE: \K.*' "$SORTED" | head -1)
        echo ""
        echo "  >>> VISIBLE: ${VISIBLE:-NOT FOUND} <<<"
        echo ""
    fi
fi

# ============================================================================
# Post-Load Transition
# ============================================================================

if show_phase "Post-Load"; then
    echo "================================================================================"
    echo "  POST-LOAD TRANSITION"
    echo "================================================================================"

    # Loading thread completion
    LOAD_DONE_TS=$(get_ts "$(grep 'LoadingThread: complete' "$SORTED" | head -1)")
    [ -n "$LOAD_DONE_TS" ] && echo "  Loading thread done: $LOAD_DONE_TS"

    # GL context reacquired
    GL_REACQUIRE=$(grep -c 'GL context reacquired' "$SORTED" || true)
    [ "$GL_REACQUIRE" -gt 0 ] && echo "  GL context: reacquired"

    # BSP tree extraction
    BSP_EXTRACT=$(grep -c 'extracting BSP tree' "$SORTED" || true)
    [ "$BSP_EXTRACT" -gt 0 ] && echo "  BSP tree: extracted from loader"

    # Graphics complete
    GFX_COMPLETE_TS=$(get_ts "$(grep 'graphicsCompleteReady=true' "$SORTED" | head -1)")
    [ -n "$GFX_COMPLETE_TS" ] && echo "  Graphics complete: $GFX_COMPLETE_TS"

    # SimulationWorker started
    SIM_STARTED=$(grep -c 'SimulationWorker started$' "$SORTED" || true)
    [ "$SIM_STARTED" -gt 0 ] && echo "  SimulationWorker: started (runtime thread)"

    # Progressive loading
    PROG_COMPLETE=$(grep -oP 'Progressive loading complete: \K.*' "$SORTED" | head -1)
    [ -n "$PROG_COMPLETE" ] && echo "  Progressive load: $PROG_COMPLETE"

    # Placeholder removal
    PLACEHOLDER=$(grep -c 'Zone placeholder removed' "$SORTED" || true)
    [ "$PLACEHOLDER" -gt 0 ] && echo "  Placeholder: removed"

    # State snapshot published
    STATE_PUB=$(grep -c 'State snapshot published' "$SORTED" || true)
    [ "$STATE_PUB" -gt 0 ] && echo "  State snapshot: published to bridge"

    # Raw texture release
    RAW_TEX=$(grep -oP 'Released \K.*raw texture data' "$SORTED" | head -1)
    [ -n "$RAW_TEX" ] && echo "  Released: $RAW_TEX after progressive load"

    # First slow frame
    FIRST_SLOW_TS=$(get_ts "$(grep 'PERF: Previous frame took\|PERF: smgr->drawAll' "$SORTED" | head -1)")
    [ -n "$FIRST_SLOW_TS" ] && echo "  First slow frame: $FIRST_SLOW_TS"

    echo ""
fi

# ============================================================================
# Frame Timing Analysis (all FRAME TIMING BREAKDOWN blocks)
# ============================================================================

if show_phase "Frame Timing"; then
    FRAME_TIMING_COUNT=$(grep -c 'FRAME TIMING BREAKDOWN' "$SORTED" || true)
    if [ "$FRAME_TIMING_COUNT" -gt 0 ]; then
        echo "================================================================================"
        echo "  FRAME TIMING ($FRAME_TIMING_COUNT reports)"
        echo "================================================================================"

        REPORT_NUM=0
        # Process each frame timing block
        while IFS= read -r header_line; do
            REPORT_NUM=$((REPORT_NUM + 1))
            ts=$(get_ts "$header_line")
            fps=$(echo "$header_line" | grep -oP '\d+\.\d+ fps')
            echo ""
            echo "  --- Report #$REPORT_NUM ($ts, $fps) ---"

            # Extract key timing lines from the block (next ~70 lines after header)
            header_line_num=$(grep -n "FRAME TIMING BREAKDOWN" "$SORTED" | sed -n "${REPORT_NUM}p" | cut -d: -f1)
            if [ -n "$header_line_num" ]; then
                end_line=$((header_line_num + 75))
                block=$(sed -n "${header_line_num},${end_line}p" "$SORTED")

                # Key metrics
                TOTAL_FRAME=$(echo "$block" | grep -oP 'Total Frame:\s+\K\d+')
                SCENE_DRAW=$(echo "$block" | grep -oP 'Scene Draw All:\s+\K\d+ us.*')
                MANUAL_ZONE=$(echo "$block" | grep -oP 'Manual Zone Draw:\s+\K\d+ us.*')
                SOLID=$(echo "$block" | grep -oP 'Solid Pass:\s+\K\d+ us.*')
                TRANSPARENT=$(echo "$block" | grep -oP 'Transparent Pass:\s+\K\d+ us.*')
                END_SCENE=$(echo "$block" | grep -oP 'End Scene:\s+\K\d+ us.*')
                ENTITY_UPD=$(echo "$block" | grep -oP 'Entity Update:\s+\K\d+ us.*')
                DOOR_UPD=$(echo "$block" | grep -oP 'Door Update:\s+\K\d+ us.*')
                PVS_VIS=$(echo "$block" | grep -oP 'PVS Visibility:\s+\K\d+ us.*')
                OCC_CULL=$(echo "$block" | grep -oP 'Occlusion Cull:\s+\K\d+ us.*')
                ZONE_OVERLAY=$(echo "$block" | grep -oP 'Zone Line Overlay:\s+\K\d+ us.*')
                SIM_APPLY=$(echo "$block" | grep -oP 'SimWorker Apply:\s+\K\d+ us.*')
                MESH_LOAD=$(echo "$block" | grep -oP 'Mesh Loading:\s+\K\d+ us.*')
                PARTICLES=$(echo "$block" | grep -oP 'Particles:\s+\K\d+ us.*')
                GUI=$(echo "$block" | grep -oP 'GUI Draw All:\s+\K\d+ us.*')
                WM=$(echo "$block" | grep -oP 'Window Manager:\s+\K\d+ us.*')
                PORTAL_CULL=$(echo "$block" | grep -oP 'Portal Entity Cull: \K.*')
                MANUAL_STATS=$(echo "$block" | grep -oP 'Manual Zone Draw:\s+\K\d+ regions.*')

                # Print frame breakdown
                [ -n "$TOTAL_FRAME" ] && printf "    Total Frame:       %s us\n" "$TOTAL_FRAME"
                [ -n "$SCENE_DRAW" ] && printf "    Scene Draw All:    %s\n" "$SCENE_DRAW"
                [ -n "$SOLID" ] && printf "      Solid Pass:      %s\n" "$SOLID"
                [ -n "$MANUAL_ZONE" ] && printf "      Manual Zone:     %s\n" "$MANUAL_ZONE"
                [ -n "$TRANSPARENT" ] && printf "      Transparent:     %s\n" "$TRANSPARENT"
                [ -n "$END_SCENE" ] && printf "    End Scene:         %s\n" "$END_SCENE"
                [ -n "$ZONE_OVERLAY" ] && printf "    Zone Line Overlay: %s\n" "$ZONE_OVERLAY"
                [ -n "$SIM_APPLY" ] && printf "    SimWorker Apply:   %s\n" "$SIM_APPLY"
                [ -n "$MESH_LOAD" ] && printf "    Mesh Loading:      %s\n" "$MESH_LOAD"
                [ -n "$PVS_VIS" ] && printf "    PVS Visibility:    %s\n" "$PVS_VIS"
                [ -n "$OCC_CULL" ] && printf "    Occlusion Cull:    %s\n" "$OCC_CULL"
                [ -n "$ENTITY_UPD" ] && printf "    Entity Update:     %s\n" "$ENTITY_UPD"
                [ -n "$DOOR_UPD" ] && printf "    Door Update:       %s\n" "$DOOR_UPD"
                [ -n "$PARTICLES" ] && printf "    Particles:         %s\n" "$PARTICLES"
                [ -n "$GUI" ] && printf "    GUI Draw All:      %s\n" "$GUI"
                [ -n "$WM" ] && printf "    Window Manager:    %s\n" "$WM"
                [ -n "$PORTAL_CULL" ] && printf "    Portal Cull:       %s\n" "$PORTAL_CULL"
                [ -n "$MANUAL_STATS" ] && printf "    Manual Draw Stats: %s\n" "$MANUAL_STATS"

                # Governor state
                GOV=$(echo "$block" | grep -oP 'Governor: \K.*')
                [ -n "$GOV" ] && printf "    Governor:          %s\n" "$GOV"

                # Render EMA
                EMA=$(echo "$block" | grep -oP 'Render EMA:\s+\K.*')
                [ -n "$EMA" ] && printf "    Render EMA:        %s\n" "$EMA"
            fi
        done < <(grep 'FRAME TIMING BREAKDOWN' "$SORTED")

        echo ""
    fi
fi

# ============================================================================
# RED State / Performance Warnings
# ============================================================================

if show_phase "Performance"; then
    RED_COUNT=$(grep -c 'RED STATE:' "$SORTED" || true)
    SPIKE_COUNT=$(grep -c 'SPIKE:' "$SORTED" || true)
    BUDGET_COUNT=$(grep -c 'BUDGET:' "$SORTED" || true)

    if [ "$RED_COUNT" -gt 0 ] || [ "$SPIKE_COUNT" -gt 0 ] || [ "$BUDGET_COUNT" -gt 0 ]; then
        echo "================================================================================"
        echo "  PERFORMANCE WARNINGS"
        echo "================================================================================"
        echo "  RED state frames: $RED_COUNT"
        echo "  Spike frames: $SPIKE_COUNT"
        echo "  Budget warnings: $BUDGET_COUNT"

        # Show unique RED SCENE lines (they tend to repeat)
        echo ""
        echo "  Unique RED SCENE signatures:"
        grep 'RED SCENE:' "$SORTED" | grep -oP 'RED SCENE: \K.*' | sort -u | while IFS= read -r line; do
            count=$(grep -cF "$line" "$SORTED" || true)
            [ -z "$count" ] && count=1
            echo "    ($count occurrences) $line"
        done

        # Show unique SPIKE top contributors
        echo ""
        echo "  Spike top contributors (first occurrence):"
        grep 'SPIKE:' "$SORTED" | head -1 | grep -oP 'top: \K.*'| while IFS= read -r line; do
            echo "    $line"
        done

        echo ""
    fi
fi

# ============================================================================
# Comparison with reference (if PVS/culling data available)
# ============================================================================

if show_phase "Culling Analysis"; then
    echo "================================================================================"
    echo "  CULLING ANALYSIS"
    echo "================================================================================"

    # PVS setup
    PVS_CAMERAS=$(grep -oP 'PVS lookup tables built: \K\d+ camera regions' "$SORTED" | head -1)
    PVS_BOUNDS=$(grep -oP 'PVS lookup.*\K\d+ total region bounds' "$SORTED" | head -1)
    echo "  PVS tables: ${PVS_CAMERAS:-NOT BUILT} with PVS data, ${PVS_BOUNDS:-?}"

    # Current PVS region at dumpscene
    echo "  Camera PVS region: ${PVS_REGION:-?}"

    # Visible regions from dumpscene
    VIS_REGIONS=$(echo "$VISIBLE" | grep -oP 'regions=\K\d+/\d+')
    echo "  Visible regions: ${VIS_REGIONS:-?}"

    # Visible from frame timing
    MANUAL_REGIONS=$(grep -oP 'Manual Zone Draw:\s+\K\d+ regions' "$SORTED" | tail -1)
    MANUAL_DRAWS=$(grep -oP 'Manual Zone Draw:\s+\d+ regions avg, \K\d+ drawCalls' "$SORTED" | tail -1)
    echo "  Manual draw: ${MANUAL_REGIONS:-?} avg, ${MANUAL_DRAWS:-?} avg"

    # Scene nodes from frame timing vs dumpscene
    FT_NODES=$(grep -oP 'Scene Draw All:.*\[\K\d+ nodes' "$SORTED" | tail -1)
    echo "  Scene nodes (frame timing): ${FT_NODES:-?}"
    echo "  Scene nodes (dumpscene): ${SCENE_NODES:-?}"

    # PVS Visibility timing
    PVS_TIME=$(grep -oP 'PVS Visibility:\s+\K\d+' "$SORTED" | tail -1)
    echo "  PVS Visibility time: ${PVS_TIME:-?} us"

    # Portal Entity Cull presence
    PORTAL_CULL_PRESENT=$(grep -c 'Portal Entity Cull' "$SORTED" || true)
    if [ "$PORTAL_CULL_PRESENT" -gt 0 ]; then
        PORTAL_CULL_DATA=$(grep -oP 'Portal Entity Cull: \K.*' "$SORTED" | head -1)
        echo "  Portal Entity Cull: $PORTAL_CULL_DATA"
    else
        echo "  Portal Entity Cull: ** NOT PRESENT IN LOG **"
    fi

    # Entities visible
    VIS_ENTITIES=$(echo "$VISIBLE" | grep -oP 'entities=\K\d+')
    VIS_LIGHTS=$(echo "$VISIBLE" | grep -oP 'lights=\K\d+')
    echo "  Visible entities: ${VIS_ENTITIES:-?} | Visible lights: ${VIS_LIGHTS:-?}"

    # Entity BSP region distribution
    echo ""
    echo "  Entity BSP region assignments:"
    echo "    With valid region: $ENT_WITH_BSP"
    echo "    No region (=-1):  $ENT_NO_BSP"
    echo "    In scene graph:   $ENT_IN_GRAPH"

    echo ""
fi

# ============================================================================
# Shutdown
# ============================================================================

if show_phase "Shutdown"; then
    SHUTDOWN=$(grep -c 'Shutting down application' "$SORTED" || true)
    if [ "$SHUTDOWN" -gt 0 ]; then
        echo "================================================================================"
        echo "  SHUTDOWN"
        echo "================================================================================"

        # SimulationWorker stats
        SIM_STATS=$(grep -oP 'SimulationWorker stopped \(\K[^)]+' "$SORTED" | head -1)
        [ -n "$SIM_STATS" ] && echo "  SimWorker: $SIM_STATS"

        # Texture cache at shutdown
        TEX_SHUTDOWN=$(grep -oP 'constrained texture cache has \K[^$]+' "$SORTED" | head -1)
        [ -n "$TEX_SHUTDOWN" ] && echo "  Texture cache: $TEX_SHUTDOWN"

        # Performance report
        AVG_FPS=$(grep -oP 'Average FPS:\s+\K[0-9.]+' "$SORTED" | head -1)
        TOTAL_TIME=$(grep -oP 'Total elapsed time:\s+\K[^$]+' "$SORTED" | head -1)
        FRAME_COUNT=$(grep -oP 'Frame Time\s+\K\d+' "$SORTED" | head -1)
        FRAME_AVG=$(grep 'Frame Time' "$SORTED" | grep -oP '\d+\s+\K[0-9.]+' | head -1)

        [ -n "$AVG_FPS" ] && echo "  Average FPS: $AVG_FPS"
        [ -n "$TOTAL_TIME" ] && echo "  Total time: $TOTAL_TIME"
        [ -n "$FRAME_COUNT" ] && echo "  Frames rendered: $FRAME_COUNT (avg ${FRAME_AVG:-?}ms)"

        # Exit code
        EXIT_LINE=$(grep 'Exit code:' "$SORTED" | tail -1)
        [ -n "$EXIT_LINE" ] && echo "  $EXIT_LINE"

        echo ""
    fi
fi

# ============================================================================
# TRACE summary (if present)
# ============================================================================

if $HAS_TRACE && show_phase "TRACE Summary"; then
    echo "================================================================================"
    echo "  TRACE SUMMARY ($TRACE_COUNT lines)"
    echo "================================================================================"
    echo ""
    echo "  Lines by module:"
    grep '\[TRACE\]' "$SORTED" | grep -oP '\[TRACE\] \[\K[^\]]+' | sort | uniq -c | sort -rn | while read -r count module; do
        printf "    %-20s %d\n" "$module" "$count"
    done
    echo ""
fi

# ============================================================================
# Error/Warning Summary (deduped)
# ============================================================================

if show_phase "Errors"; then
    if [ "$FATAL_COUNT" -gt 0 ] || [ "$ERROR_COUNT" -gt 0 ]; then
        echo "================================================================================"
        echo "  ERRORS AND FATALS"
        echo "================================================================================"

        if [ "$FATAL_COUNT" -gt 0 ]; then
            echo ""
            echo "  FATAL ($FATAL_COUNT):"
            grep '\[FATAL\]' "$SORTED" | while IFS= read -r line; do
                echo "    $line"
            done
        fi

        if [ "$ERROR_COUNT" -gt 0 ]; then
            echo ""
            echo "  ERRORS ($ERROR_COUNT, deduped):"
            grep '\[ERROR' "$SORTED" | sed 's/\[.*\] //' | sort | uniq -c | sort -rn | head -20 | while read -r count msg; do
                printf "    (%d) %s\n" "$count" "$msg"
            done
        fi
        echo ""
    fi
fi

# ============================================================================
# Warnings Summary (deduped, excluding RED/SPIKE/BUDGET which are shown above)
# ============================================================================

if show_phase "Warnings"; then
    # Filter out performance warnings already shown
    OTHER_WARNS=$(grep '\[WARN' "$SORTED" | grep -v 'RED STATE:\|RED SCENE:\|RED:\|SPIKE:\|BUDGET:\|PERF:' | wc -l)
    if [ "$OTHER_WARNS" -gt 0 ]; then
        echo "================================================================================"
        echo "  WARNINGS ($OTHER_WARNS non-performance)"
        echo "================================================================================"

        grep '\[WARN' "$SORTED" | grep -v 'RED STATE:\|RED SCENE:\|RED:\|SPIKE:\|BUDGET:\|PERF:' | \
            sed 's/^\[.*\] \[WARN *\] //' | sort | uniq -c | sort -rn | head -20 | while read -r count msg; do
            printf "    (%d) %s\n" "$count" "$msg"
        done
        echo ""
    fi
fi

echo "================================================================================"
echo "  Analysis complete"
echo "================================================================================"
