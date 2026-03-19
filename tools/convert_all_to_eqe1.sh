#!/bin/bash
set -euo pipefail

# Convert an entire EQ Titanium client directory to EQE1 flat pack format.
# Usage: convert_all_to_eqe1.sh --eq-path <dir> --output <dir> [--builder <path>]

EQ_PATH=""
OUTPUT=""
BUILDER="./build/bin/eqe1_builder"
VERIFIER="./build/bin/verify_eqe1"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --eq-path) EQ_PATH="$2"; shift 2 ;;
        --output)  OUTPUT="$2"; shift 2 ;;
        --builder) BUILDER="$2"; shift 2 ;;
        --help|-h)
            echo "Usage: $0 --eq-path <dir> --output <dir> [--builder <path>]"
            exit 0 ;;
        *) echo "Unknown argument: $1"; exit 1 ;;
    esac
done

if [[ -z "$EQ_PATH" ]]; then echo "Error: --eq-path is required"; exit 1; fi
if [[ -z "$OUTPUT" ]]; then echo "Error: --output is required"; exit 1; fi
if [[ ! -d "$EQ_PATH" ]]; then echo "Error: EQ path does not exist: $EQ_PATH"; exit 1; fi
if [[ ! -x "$BUILDER" ]]; then echo "Error: Builder not found: $BUILDER"; exit 1; fi

mkdir -p "$OUTPUT"

TOTAL=$(find "$EQ_PATH" -maxdepth 1 -name "*.s3d" -type f | wc -l)
echo "Converting $TOTAL S3D archives from $EQ_PATH to $OUTPUT"
echo ""

START_TIME=$(date +%s)
SUCCESS=0
FAILED=0

"$BUILDER" --eq-path "$EQ_PATH" --all --output "$OUTPUT"
if [[ $? -eq 0 ]]; then
    SUCCESS=$TOTAL
else
    FAILED=1
fi

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

echo ""
echo "=== Conversion Complete ==="
echo "  Time: ${ELAPSED}s"

# Verify if verifier is available
if [[ -x "$VERIFIER" ]]; then
    echo ""
    echo "Verifying..."
    VERIFY_FAIL=0
    VERIFY_PASS=0
    for eqe1 in "$OUTPUT"/*.eqe1; do
        if ! "$VERIFIER" "$eqe1" > /dev/null 2>&1; then
            echo "  FAIL: $(basename "$eqe1")"
            VERIFY_FAIL=$((VERIFY_FAIL + 1))
        else
            VERIFY_PASS=$((VERIFY_PASS + 1))
        fi
    done
    echo "  Verified: $VERIFY_PASS passed, $VERIFY_FAIL failed"
else
    echo "  (verify_eqe1 not found, skipping verification)"
fi

# Size comparison
INPUT_SIZE=$(du -sb "$EQ_PATH"/*.s3d 2>/dev/null | awk '{sum+=$1} END {print sum}')
OUTPUT_SIZE=$(du -sb "$OUTPUT"/*.eqe1 2>/dev/null | awk '{sum+=$1} END {print sum}')
echo ""
echo "  Input:  $((INPUT_SIZE / 1024 / 1024)) MB (S3D)"
echo "  Output: $((OUTPUT_SIZE / 1024 / 1024)) MB (EQE1)"
