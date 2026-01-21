#!/bin/bash
# tools/generate_all_surface_maps.sh
# Generate surface maps for all Classic, Kunark, and Velious zones
#
# Usage: ./tools/generate_all_surface_maps.sh [eq_client_path] [output_dir]
# Example: ./tools/generate_all_surface_maps.sh /home/user/projects/claude/EverQuestP1999 data/detail/zones

set -e

EQ_PATH="${1:-/home/user/projects/claude/EverQuestP1999}"
OUTPUT_DIR="${2:-data/detail/zones}"
TOOL="./build/bin/generate_surface_map"
CELL_SIZE="2.0"

# Verify tool exists
if [[ ! -x "$TOOL" ]]; then
    echo "ERROR: Tool not found at $TOOL"
    echo "Please build the project first: cmake --build build"
    exit 1
fi

# Verify EQ path exists
if [[ ! -d "$EQ_PATH" ]]; then
    echo "ERROR: EQ client path not found: $EQ_PATH"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

echo "========================================"
echo "Surface Map Generation"
echo "========================================"
echo "EQ Client Path: $EQ_PATH"
echo "Output Directory: $OUTPUT_DIR"
echo "Cell Size: $CELL_SIZE"
echo "========================================"
echo ""

# All zones by expansion
# Classic Antonica (35 zones)
CLASSIC_ANTONICA="qeynos qeynos2 qcat qey2hh1 northqeynos southqeynos sro nro oasis highkeep highpass blackburrow eastkarana northkarana southkarana westkarana lakerathe lavastorm nektulos commons ecommons freportw freportn freporte innothule feerrott cazicthule befallen everfrost permafrost grobb halas rivervale misty arena"

# Classic Faydwer (13 zones)
CLASSIC_FAYDWER="gfaydark lfaydark crushbone felwithea felwitheb kaladima kaladimb akanon steamfont butcher kedge mistmoore unrest"

# Classic Odus (7 zones)
CLASSIC_ODUS="erudnext erudnint erudsxing paineel tox kerraridge stonebrunt"

# Classic Planes (3 zones)
CLASSIC_PLANES="airplane fearplane hateplane"

# Classic Dungeons (7 zones)
CLASSIC_DUNGEONS="soldunga soldungb najena beholder guktop gukbottom runnyeye"

# Kunark (27 zones)
KUNARK="burningwood cabeast cabwest charasis chardok chardokb citymist dalnir dreadlands droga emeraldjungle fieldofbone firiona frontiermtns kaesora karnor kurn lakeofillomen nurga overthere sebilis skyfire swampofnohope timorous trakanon veeshan warslikswood"

# Velious (18 zones)
VELIOUS="thurgadina thurgadinb kael wakening skyshrine cobaltscar crystal eastwastes greatdivide iceclad necropolis sirens sleeper templeveeshan velketor westwastes frozenshadow growthplane"

# Combine all zones
ALL_ZONES="$CLASSIC_ANTONICA $CLASSIC_FAYDWER $CLASSIC_ODUS $CLASSIC_PLANES $CLASSIC_DUNGEONS $KUNARK $VELIOUS"

# Counters
total=0
success=0
skipped=0
failed=0

# Failed zones list
failed_zones=""

for zone in $ALL_ZONES; do
    total=$((total + 1))
    s3d="$EQ_PATH/${zone}.s3d"
    output="$OUTPUT_DIR/${zone}_surface.map"

    # Check if S3D file exists
    if [[ ! -f "$s3d" ]]; then
        echo "[$total] SKIP: $zone (no S3D file)"
        skipped=$((skipped + 1))
        continue
    fi

    echo -n "[$total] Generating: $zone ... "

    # Run the tool
    if "$TOOL" "$EQ_PATH" "$zone" "$output" "$CELL_SIZE" 2>/dev/null; then
        # Verify output file was created
        if [[ -f "$output" ]]; then
            size=$(stat -c%s "$output" 2>/dev/null || echo "0")
            echo "OK (${size} bytes)"
            success=$((success + 1))
        else
            echo "FAILED (no output file)"
            failed=$((failed + 1))
            failed_zones="$failed_zones $zone"
        fi
    else
        echo "FAILED (tool error)"
        failed=$((failed + 1))
        failed_zones="$failed_zones $zone"
    fi
done

echo ""
echo "========================================"
echo "Generation Complete"
echo "========================================"
echo "Total zones:   $total"
echo "Success:       $success"
echo "Skipped:       $skipped (no S3D file)"
echo "Failed:        $failed"
echo "========================================"

if [[ -n "$failed_zones" ]]; then
    echo ""
    echo "Failed zones:$failed_zones"
fi

# Exit with error if any failures
if [[ $failed -gt 0 ]]; then
    exit 1
fi

exit 0
