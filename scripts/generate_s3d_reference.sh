#!/bin/bash
# Generate a comprehensive reference of all pre-Luclin S3D file contents
# Usage: ./scripts/generate_s3d_reference.sh [eq_client_path]
#
# This script reads data/pre_luclin_zones.json and runs s3d_dump on each
# S3D file, consolidating the output into data/s3d_contents_reference.md

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
S3D_DUMP="$PROJECT_DIR/build/bin/s3d_dump"
ZONES_JSON="$PROJECT_DIR/data/pre_luclin_zones.json"
OUTPUT_FILE="$PROJECT_DIR/data/s3d_contents_reference.md"

# Default EQ client path
EQ_CLIENT_PATH="${1:-/home/user/projects/claude/EverQuestP1999}"

# Check prerequisites
if [ ! -f "$S3D_DUMP" ]; then
    echo "Error: s3d_dump not found at $S3D_DUMP"
    echo "Please build the project first: cd build && cmake --build ."
    exit 1
fi

if [ ! -f "$ZONES_JSON" ]; then
    echo "Error: Zone list not found at $ZONES_JSON"
    exit 1
fi

if [ ! -d "$EQ_CLIENT_PATH" ]; then
    echo "Error: EQ client directory not found at $EQ_CLIENT_PATH"
    echo "Usage: $0 [eq_client_path]"
    exit 1
fi

# Extract zone short names from JSON
get_zones() {
    grep '"short_name"' "$ZONES_JSON" | sed 's/.*: "\([^"]*\)".*/\1/'
}

# Count total files to process
count_files() {
    local count=0
    local all_zones="global global2 global3 global4 global5 global6 global7 qeynos qeynos2 qrg qeytoqrg highpass highkeep freportn freportw freporte qey2hh1 northkarana southkarana eastkarana beholder blackburrow paw rivervale kithicor commons ecommons erudnint erudnext nektulos lavastorm halas everfrost soldunga soldungb misty nro sro befallen oasis tox hole neriaka neriakb neriakc najena qcat innothule feerrott cazicthule oggok rathemtn lakerathe grobb gfaydark akanon steamfont lfaydark crushbone mistmoore kaladima kaladimb felwithea felwitheb unrest kedge guktop gukbottom butcher oot cauldron airplane fearplane permafrost kerraridge paineel hateplane arena erudsxing soltemple runnyeye fieldofbone warslikswood droga cabwest cabeast swampofnohope firiona lakeofillomen dreadlands burningwood kaesora sebilis citymist skyfire frontiermtns overthere emeraldjungle trakanon timorous kurn karnor chardok dalnir charasis nurga veeshan veksar iceclad frozenshadow velketor kael skyshrine thurgadina thurgadinb eastwastes cobaltscar greatdivide wakening westwastes crystal necropolis templeveeshan sirens mischiefplane growthplane sleeper"
    for zone in $all_zones; do
        for suffix in "" "_chr" "_obj" "_chr2" "_2_obj"; do
            if [ -f "${EQ_CLIENT_PATH}/${zone}${suffix}.s3d" ]; then
                ((count++))
            fi
        done
    done
    echo $count
}

echo "=== Pre-Luclin S3D Reference Generator ==="
echo "EQ Client Path: $EQ_CLIENT_PATH"
echo "Output File: $OUTPUT_FILE"
echo ""

total_files=$(count_files)
echo "Found $total_files S3D files to process"
echo ""

# Initialize output file
cat > "$OUTPUT_FILE" << 'EOF'
# Pre-Luclin S3D Contents Reference

This file contains the complete contents of all pre-Luclin EverQuest S3D files,
including Classic, Ruins of Kunark, and Scars of Velious zones.

Generated using `s3d_dump` tool.

## Table of Contents

- [Global Character Files](#global-character-files)
- [Classic Zones](#classic-zones)
- [Kunark Zones](#kunark-zones)
- [Velious Zones](#velious-zones)

---

EOF

processed=0
failed=0
failed_files=""

# Process a single S3D file
process_s3d() {
    local s3d_file="$1"
    local zone_name="$2"

    if [ ! -f "$s3d_file" ]; then
        return 1
    fi

    local basename=$(basename "$s3d_file")
    echo "Processing: $basename"

    # Run s3d_dump and capture output
    local output
    if output=$("$S3D_DUMP" "$s3d_file" 2>&1); then
        echo "$output" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
        echo "---" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
        return 0
    else
        echo "  Warning: Failed to process $basename"
        return 1
    fi
}

# Process global character files first
echo "## Global Character Files" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

for i in "" 2 3 4 5 6 7; do
    s3d_file="${EQ_CLIENT_PATH}/global${i}_chr.s3d"
    if [ -f "$s3d_file" ]; then
        if process_s3d "$s3d_file" "global${i}_chr"; then
            ((processed++))
        else
            ((failed++))
            failed_files="$failed_files global${i}_chr.s3d"
        fi
    fi
done

# Process zones by expansion
process_expansion() {
    local expansion_name="$1"
    local header="$2"

    echo "" >> "$OUTPUT_FILE"
    echo "## $header" >> "$OUTPUT_FILE"
    echo "" >> "$OUTPUT_FILE"

    # Get zones for this expansion from JSON
    # This is a simplified approach - we process all zones and let the file check filter
}

echo ""
echo "Processing Classic zones..."
echo "" >> "$OUTPUT_FILE"
echo "## Classic Zones" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Classic zones (from the JSON)
classic_zones="qeynos qeynos2 qrg qeytoqrg highpass highkeep freportn freportw freporte qey2hh1 northkarana southkarana eastkarana beholder blackburrow paw rivervale kithicor commons ecommons erudnint erudnext nektulos lavastorm halas everfrost soldunga soldungb misty nro sro befallen oasis tox hole neriaka neriakb neriakc najena qcat innothule feerrott cazicthule oggok rathemtn lakerathe grobb gfaydark akanon steamfont lfaydark crushbone mistmoore kaladima kaladimb felwithea felwitheb unrest kedge guktop gukbottom butcher oot cauldron airplane fearplane permafrost kerraridge paineel hateplane arena erudsxing soltemple runnyeye"

for zone in $classic_zones; do
    for suffix in "" "_chr" "_obj" "_chr2" "_2_obj"; do
        s3d_file="${EQ_CLIENT_PATH}/${zone}${suffix}.s3d"
        if [ -f "$s3d_file" ]; then
            if process_s3d "$s3d_file" "$zone"; then
                ((processed++))
            else
                ((failed++))
                failed_files="$failed_files ${zone}${suffix}.s3d"
            fi
        fi
    done
done

echo ""
echo "Processing Kunark zones..."
echo "" >> "$OUTPUT_FILE"
echo "## Kunark Zones" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

kunark_zones="fieldofbone warslikswood droga cabwest cabeast swampofnohope firiona lakeofillomen dreadlands burningwood kaesora sebilis citymist skyfire frontiermtns overthere emeraldjungle trakanon timorous kurn karnor chardok dalnir charasis nurga veeshan veksar"

for zone in $kunark_zones; do
    for suffix in "" "_chr" "_obj" "_chr2" "_2_obj"; do
        s3d_file="${EQ_CLIENT_PATH}/${zone}${suffix}.s3d"
        if [ -f "$s3d_file" ]; then
            if process_s3d "$s3d_file" "$zone"; then
                ((processed++))
            else
                ((failed++))
                failed_files="$failed_files ${zone}${suffix}.s3d"
            fi
        fi
    done
done

echo ""
echo "Processing Velious zones..."
echo "" >> "$OUTPUT_FILE"
echo "## Velious Zones" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

velious_zones="iceclad frozenshadow velketor kael skyshrine thurgadina thurgadinb eastwastes cobaltscar greatdivide wakening westwastes crystal necropolis templeveeshan sirens mischiefplane growthplane sleeper"

for zone in $velious_zones; do
    for suffix in "" "_chr" "_obj" "_chr2" "_2_obj"; do
        s3d_file="${EQ_CLIENT_PATH}/${zone}${suffix}.s3d"
        if [ -f "$s3d_file" ]; then
            if process_s3d "$s3d_file" "$zone"; then
                ((processed++))
            else
                ((failed++))
                failed_files="$failed_files ${zone}${suffix}.s3d"
            fi
        fi
    done
done

echo ""
echo "=== Complete ==="
echo "Processed: $processed files"
echo "Failed: $failed files"
if [ -n "$failed_files" ]; then
    echo "Failed files:$failed_files"
fi
echo ""
echo "Output written to: $OUTPUT_FILE"
echo "File size: $(du -h "$OUTPUT_FILE" | cut -f1)"
