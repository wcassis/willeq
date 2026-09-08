#!/usr/bin/env bash
# build_willeq_db.sh -- rebuild the shipped SQLite database `data/willeq.db`
# from its inputs, deterministically and byte-for-byte.
#
# T450 (board B467). `data/willeq.db` is the third derived asset in the tree,
# and it was the only one with no recipe and no pin: `data/textures.sha256`
# covers the BMPs (TexturesReproducible) and `data/willeq.eqe1.sha256` covers
# the shared pack (PackReproducible), while the database was hand-rebuilt by
# whoever changed a row and nothing checked the result. It could not have been
# pinned before this unit either -- `willeq_db_builder` stamped
# `metadata.build_date = __DATE__ " " __TIME__`, so RECOMPILING the tool
# changed the output file with byte-identical inputs (T447 Review). That row
# is now `metadata.input_digest`, a digest of the inputs themselves.
#
# The inputs, in the order the builder reads them (its own `inputs` table in
# tools/willeq_db_builder.cpp is the source of truth):
#   from the EQ client tree  spells_us.txt, eqstr_us.txt, dbstr_us.txt,
#                            SoundAssets.txt, AnimationSounds.txt,
#                            mp3index.txt, eqlsstr_us.txt
#   from data/               item_models.json
#   from config/             race_models.json
# T452 (board B468): there is no longer an optional --s3d-index input to leave
# out. It built an `s3d_assets` table that never shipped, and its only
# reachable consumer had been querying a table that did not exist since
# 2026-03-21. The flag, the loader and tools/s3d_index_builder are gone.
# T456 (board B476) removed a tenth, data/equipment_models.json: the table it
# fed, `equipment_models`, had no reader either (the model index resolves live
# through EquipmentModelLoader::buildEquipmentIndex()). So the NINE inputs
# above are ALL of them, and metadata.input_digest moved when the tenth went.
#
# Usage:
#   scripts/build_willeq_db.sh [--check] [--bin-dir DIR] [--eq-path DIR]
#                              [--data-path DIR] [--config-path DIR]
#                              [--output PATH] [--sha-file PATH] [--keep-temp]
#
#   --check       Build into a temp file and compare against the pinned
#                 sha256 and the checked-in database; do NOT install. Exit 0
#                 only if all three agree. This is what the ctest test runs.
#   --bin-dir     Directory holding `willeq_db_builder` (default: the first of
#                 build/bin, build-gles2/bin that has it).
#   --eq-path     EQ client directory
#                 (default: /mnt/projects/EverQuestP1999).
#   --data-path   WillEQ data directory (default: <repo>/data).
#   --config-path WillEQ config directory (default: <repo>/config).
#   --output      Destination database (default: <repo>/data/willeq.db).
#   --sha-file    Pinned sha256 (default: <repo>/data/willeq.db.sha256).
#   --keep-temp   Leave the temp directory in place for inspection.
#
# The install is ATOMIC: written to a temp file in the SAME directory as the
# destination and `mv`'d over it, so a concurrent reader (a ctest run, the
# visual gate) never opens a half-written database.
#
# A rebuild that changes the bytes must be committed TOGETHER with its new
# pin, exactly like the pack: `data/willeq.db` and `data/willeq.db.sha256` in
# one commit, or DbReproducible goes red.
#
# ⚠ This script never touches `kWilleqDbExpectedVersion`. A version bump
# invalidates every build tree in the workspace (T304/T355) and is a separate,
# deliberate act.

set -u

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

CHECK_ONLY=0
BIN_DIR=""
EQ_PATH="/mnt/projects/EverQuestP1999"
DATA_PATH=""
CONFIG_PATH=""
OUTPUT=""
SHA_FILE=""
KEEP_TEMP=0

while [ $# -gt 0 ]; do
    case "$1" in
        --check)       CHECK_ONLY=1; shift ;;
        --bin-dir)     BIN_DIR="$2"; shift 2 ;;
        --eq-path)     EQ_PATH="$2"; shift 2 ;;
        --data-path)   DATA_PATH="$2"; shift 2 ;;
        --config-path) CONFIG_PATH="$2"; shift 2 ;;
        --output)      OUTPUT="$2"; shift 2 ;;
        --sha-file)    SHA_FILE="$2"; shift 2 ;;
        --keep-temp)   KEEP_TEMP=1; shift ;;
        -h|--help)     sed -n '2,50p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "build_willeq_db.sh: unknown argument '$1'" >&2; exit 2 ;;
    esac
done

[ -n "$DATA_PATH" ]   || DATA_PATH="$REPO_ROOT/data"
[ -n "$CONFIG_PATH" ] || CONFIG_PATH="$REPO_ROOT/config"
[ -n "$OUTPUT" ]      || OUTPUT="$REPO_ROOT/data/willeq.db"
[ -n "$SHA_FILE" ]    || SHA_FILE="$REPO_ROOT/data/willeq.db.sha256"

if [ -z "$BIN_DIR" ]; then
    for cand in "$REPO_ROOT/build/bin" "$REPO_ROOT/build-gles2/bin"; do
        if [ -x "$cand/willeq_db_builder" ]; then
            BIN_DIR="$cand"
            break
        fi
    done
fi

if [ -z "$BIN_DIR" ]; then
    echo "ERROR: no --bin-dir given and neither build/bin nor build-gles2/bin" >&2
    echo "       holds willeq_db_builder. Build it first:" >&2
    echo "       cmake --build $REPO_ROOT/build --target willeq_db_builder" >&2
    exit 3
fi

if [ ! -x "$BIN_DIR/willeq_db_builder" ]; then
    echo "ERROR: $BIN_DIR/willeq_db_builder is missing or not executable" >&2
    exit 3
fi

# The client's *.txt files are seven of the nine inputs. A missing tree is
# not a degraded build -- it is a database with seven empty tables, which
# would silently replace a good one -- so refuse rather than build it.
if [ ! -r "$EQ_PATH/spells_us.txt" ]; then
    echo "ERROR: $EQ_PATH/spells_us.txt is not readable." >&2
    echo "       Seven of the nine inputs come out of the EQ client tree;" >&2
    echo "       building without it would ship empty spells/strings/sound" >&2
    echo "       tables. Point --eq-path at the client directory." >&2
    exit 3
fi

for d in "$DATA_PATH" "$CONFIG_PATH"; do
    if [ ! -d "$d" ]; then
        echo "ERROR: input directory $d does not exist" >&2
        exit 3
    fi
done

WORK="$(mktemp -d "${TMPDIR:-/tmp}/willeq_db.XXXXXXXX")"
cleanup() {
    if [ "$KEEP_TEMP" -eq 1 ]; then
        echo "  (temp kept at $WORK)"
    else
        rm -rf "$WORK"
    fi
}
trap cleanup EXIT

BUILT="$WORK/willeq.db"
if ! "$BIN_DIR/willeq_db_builder" \
        --eq-path "$EQ_PATH" \
        --data-path "$DATA_PATH" \
        --config-path "$CONFIG_PATH" \
        --output "$BUILT" > "$WORK/build.log" 2>&1; then
    echo "ERROR: willeq_db_builder failed:" >&2
    cat "$WORK/build.log" >&2
    exit 4
fi

if [ ! -s "$BUILT" ]; then
    echo "ERROR: willeq_db_builder produced no database at $BUILT" >&2
    cat "$WORK/build.log" >&2
    exit 4
fi

BUILT_SHA="$(sha256sum "$BUILT" | awk '{print $1}')"
BUILT_SIZE="$(stat -c %s "$BUILT")"

PINNED_SHA=""
if [ -f "$SHA_FILE" ]; then
    PINNED_SHA="$(awk 'NF && $1 !~ /^#/ {print $1; exit}' "$SHA_FILE")"
fi

echo "built:  $BUILT_SHA  ($BUILT_SIZE bytes)"
[ -n "$PINNED_SHA" ] && echo "pinned: $PINNED_SHA  ($SHA_FILE)"

if [ "$CHECK_ONLY" -eq 1 ]; then
    rc=0
    if [ -z "$PINNED_SHA" ]; then
        echo "FAIL: no pinned sha256 at $SHA_FILE" >&2
        rc=1
    elif [ "$BUILT_SHA" != "$PINNED_SHA" ]; then
        echo "FAIL: rebuild does not match the pinned sha256." >&2
        echo "      Either an input changed (the client *.txt files," >&2
        echo "      data/item_models.json, config/race_models.json) or" >&2
        echo "      willeq_db_builder did." >&2
        echo "      If that was deliberate, rerun this script without --check" >&2
        echo "      to reinstall data/willeq.db and update $SHA_FILE in the" >&2
        echo "      SAME commit. If it was NOT, the builder has become" >&2
        echo "      non-deterministic again -- see its input-digest comment." >&2
        rc=1
    fi
    if [ ! -f "$OUTPUT" ]; then
        echo "FAIL: shipped database $OUTPUT is missing" >&2
        rc=1
    else
        SHIPPED_SHA="$(sha256sum "$OUTPUT" | awk '{print $1}')"
        echo "shipped: $SHIPPED_SHA  ($OUTPUT)"
        if [ "$SHIPPED_SHA" != "$BUILT_SHA" ]; then
            echo "FAIL: the checked-in database differs from the rebuild." >&2
            rc=1
        fi
    fi
    if [ "$rc" -eq 0 ]; then
        echo "PASS: rebuild is byte-identical to the pinned hash and the checked-in database."
    fi
    exit "$rc"
fi

OUT_DIR="$(dirname -- "$OUTPUT")"
if [ ! -d "$OUT_DIR" ]; then
    echo "ERROR: destination directory $OUT_DIR does not exist" >&2
    exit 5
fi

TMP_OUT="$(mktemp "$OUT_DIR/.willeq.db.XXXXXXXX")"
cp "$BUILT" "$TMP_OUT"
chmod --reference="$OUTPUT" "$TMP_OUT" 2>/dev/null || chmod 0664 "$TMP_OUT"
mv -f "$TMP_OUT" "$OUTPUT"
echo "installed: $OUTPUT ($BUILT_SIZE bytes, sha256 $BUILT_SHA)"

if [ -n "$PINNED_SHA" ] && [ "$PINNED_SHA" != "$BUILT_SHA" ]; then
    echo
    echo "NOTE: $SHA_FILE still pins $PINNED_SHA."
    echo "      Update it to $BUILT_SHA in the same commit as the input change,"
    echo "      or the DbReproducible ctest test will fail."
fi
