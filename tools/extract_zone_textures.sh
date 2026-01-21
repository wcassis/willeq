#!/bin/bash
# Extract textures from EQ zone S3D files, organized by expansion
# Usage: ./tools/extract_zone_textures.sh [EQ_PATH] [OUTPUT_DIR]

set -e

EQ_PATH="${1:-/home/user/projects/claude/EverQuestP1999}"
OUTPUT_DIR="${2:-docs/zone_textures}"
S3D_DUMP="./build/bin/s3d_dump"

# Ensure output directory exists
mkdir -p "$OUTPUT_DIR"

# Zone lists by expansion
# Classic Antonica
CLASSIC_ANTONICA=(
    qeynos qeynos2 qcat qey2hh1
    northqeynos southqeynos
    sro nro oasis
    highkeep highpass
    blackburrow
    eastkarana northkarana southkarana westkarana lakerathe
    lavastorm nektulos
    commons ecommons
    freportw freportn freporte
    innothule feerrott cazicthule
    befallen everfrost permafrost
    grobb halas rivervale
    misty arena
)

# Classic Faydwer
CLASSIC_FAYDWER=(
    gfaydark lfaydark
    crushbone
    felwithea felwitheb
    kaladima kaladimb
    akanon steamfont
    butcher
    kedge
    mistmoore
    unrest
)

# Classic Odus
CLASSIC_ODUS=(
    erudnext erudnint
    erudsxing
    paineel
    tox
    kerraridge
    stonebrunt
)

# Classic Planar
CLASSIC_PLANES=(
    airplane
    fearplane
    hateplane
)

# Classic Dungeons
CLASSIC_DUNGEONS=(
    soldunga soldungb
    najena
    beholder
    guktop gukbottom
    runnyeye
)

# Kunark
KUNARK=(
    burningwood
    cabeast cabwest
    charasis
    chardok chardokb
    citymist
    dalnir
    dreadlands
    droga
    emeraldjungle
    fieldofbone
    firiona
    frontiermtns
    kaesora
    karnor
    kurn
    lakeofillomen
    nurga
    overthere
    sebilis
    skyfire
    swampofnohope
    timorous
    trakanon
    veeshan
    warslikswood
)

# Velious
VELIOUS=(
    thurgadina thurgadinb
    kael
    wakening
    skyshrine
    cobaltscar
    crystal
    eastwastes
    greatdivide
    iceclad
    necropolis
    sirens
    sleeper
    templeveeshan
    velketor
    westwastes
    frozenshadow
    growthplane
)

# Function to extract textures from a zone
extract_textures() {
    local zone="$1"
    local s3d_file="$EQ_PATH/${zone}.s3d"

    if [[ ! -f "$s3d_file" ]]; then
        echo "# Zone not found: $zone" >&2
        return 1
    fi

    # Extract just BMP texture names using s3d_dump
    "$S3D_DUMP" "$s3d_file" 2>/dev/null | \
        sed -n '/### BMP Textures/,/### Unreferenced\|### DDS\|## Full/p' | \
        grep '\.bmp' | \
        sed 's/.*[│├└]── //' | \
        sort -u
}

# Function to process a zone group
process_zone_group() {
    local group_name="$1"
    local output_file="$OUTPUT_DIR/${group_name}.txt"
    shift
    local zones=("$@")

    echo "Processing $group_name (${#zones[@]} zones)..."

    {
        echo "# $group_name Zone Textures"
        echo "# Generated: $(date)"
        echo "# Zones: ${zones[*]}"
        echo ""

        for zone in "${zones[@]}"; do
            if [[ -f "$EQ_PATH/${zone}.s3d" ]]; then
                echo ""
                echo "## $zone"
                extract_textures "$zone" | sed 's/^/  /'
            fi
        done
    } > "$output_file"

    echo "  Written to $output_file"
}

# Function to create combined texture list with counts
create_combined_list() {
    local output_file="$OUTPUT_DIR/all_textures_combined.txt"

    echo "Creating combined texture list..."

    {
        echo "# Combined Texture List (Classic + Kunark + Velious)"
        echo "# Generated: $(date)"
        echo "# Format: count texture_name"
        echo ""

        cat "$OUTPUT_DIR"/*.txt 2>/dev/null | \
            grep '\.bmp' | \
            sed 's/^  //' | \
            sort | uniq -c | sort -rn
    } > "$output_file"

    echo "  Written to $output_file"
}

# Main execution
echo "=== EQ Zone Texture Extraction ==="
echo "EQ Path: $EQ_PATH"
echo "Output: $OUTPUT_DIR"
echo ""

# Check for s3d_dump tool
if [[ ! -x "$S3D_DUMP" ]]; then
    echo "ERROR: s3d_dump not found at $S3D_DUMP"
    echo "Build the project first: cmake --build build"
    exit 1
fi

# Process each zone group
process_zone_group "classic_antonica" "${CLASSIC_ANTONICA[@]}"
process_zone_group "classic_faydwer" "${CLASSIC_FAYDWER[@]}"
process_zone_group "classic_odus" "${CLASSIC_ODUS[@]}"
process_zone_group "classic_planes" "${CLASSIC_PLANES[@]}"
process_zone_group "classic_dungeons" "${CLASSIC_DUNGEONS[@]}"
process_zone_group "kunark" "${KUNARK[@]}"
process_zone_group "velious" "${VELIOUS[@]}"

# Create combined list
create_combined_list

echo ""
echo "=== Extraction Complete ==="
echo "Files created in $OUTPUT_DIR:"
ls -la "$OUTPUT_DIR"
