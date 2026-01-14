#!/bin/bash
# Generate a JSON reference of all pre-Luclin S3D file contents
# Usage: ./scripts/generate_s3d_reference_json.sh [eq_client_path]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_FILE="$PROJECT_DIR/data/s3d_contents_reference.json"

# Default EQ client path
EQ_CLIENT_PATH="${1:-/home/user/projects/claude/EverQuestP1999}"

# Check prerequisites
if [ ! -d "$EQ_CLIENT_PATH" ]; then
    echo "Error: EQ client directory not found at $EQ_CLIENT_PATH"
    exit 1
fi

echo "=== Pre-Luclin S3D JSON Reference Generator ==="
echo "EQ Client Path: $EQ_CLIENT_PATH"
echo "Output File: $OUTPUT_FILE"
echo ""

# Create a Python script to do the heavy lifting
python3 << 'PYTHON_SCRIPT'
import os
import sys
import json
import struct
import zlib
from pathlib import Path
from collections import defaultdict

EQ_PATH = os.environ.get('EQ_CLIENT_PATH', '/home/user/projects/claude/EverQuestP1999')
PROJECT_DIR = os.environ.get('PROJECT_DIR', '/home/user/projects/claude/willeq-fix-commonlands')
OUTPUT_FILE = os.path.join(PROJECT_DIR, 'data', 's3d_contents_reference.json')

# PFS/S3D magic
PFS_MAGIC = 0x20534650  # "PFS "

# WLD magic and versions
WLD_MAGIC = 0x54503D02
WLD_OLD_VERSION = 0x00015500
WLD_NEW_VERSION = 0x1000C800

# Hash key for WLD string decoding
HASH_KEY = bytes([0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A])

def decode_wld_string(data):
    """Decode a WLD hash string"""
    result = bytearray(data)
    for i in range(len(result)):
        result[i] ^= HASH_KEY[i % 8]
    return result.decode('latin-1', errors='replace').rstrip('\x00')

def read_pfs_archive(filepath):
    """Read a PFS/S3D archive and return file listing"""
    files = {}
    try:
        with open(filepath, 'rb') as f:
            # Read header
            f.seek(0)
            offset = struct.unpack('<I', f.read(4))[0]
            magic = struct.unpack('<I', f.read(4))[0]

            if magic != PFS_MAGIC:
                return None

            # Read directory
            f.seek(offset)
            count = struct.unpack('<I', f.read(4))[0]

            entries = []
            for _ in range(count):
                crc = struct.unpack('<i', f.read(4))[0]
                data_offset = struct.unpack('<I', f.read(4))[0]
                size = struct.unpack('<I', f.read(4))[0]
                entries.append((crc, data_offset, size))

            # Find filename directory (last entry typically)
            # Read each file
            for crc, data_offset, size in entries:
                f.seek(data_offset)

                # Read compressed blocks
                compressed_data = b''
                remaining = size

                while remaining > 0:
                    deflated_len = struct.unpack('<I', f.read(4))[0]
                    inflated_len = struct.unpack('<I', f.read(4))[0]

                    block_data = f.read(deflated_len)

                    if deflated_len < inflated_len:
                        try:
                            decompressed = zlib.decompress(block_data)
                            compressed_data += decompressed
                        except:
                            compressed_data += block_data
                    else:
                        compressed_data += block_data

                    remaining -= inflated_len

                # Check if this is the filename directory
                if compressed_data and compressed_data[0:1] != b'\x02':
                    # Try to parse as filename list
                    try:
                        if b'\x00' in compressed_data:
                            names = compressed_data.split(b'\x00')
                            for name in names:
                                if name:
                                    name_str = name.decode('latin-1', errors='replace').lower()
                                    if '.' in name_str:
                                        files[name_str] = len(compressed_data)
                    except:
                        pass

            # Simpler approach - just list files by extension patterns
            f.seek(0)
            content = f.read()

            # Find .wld, .bmp, .dds files
            import re
            for match in re.finditer(rb'([a-zA-Z0-9_]+\.(wld|bmp|dds|txt))', content, re.IGNORECASE):
                name = match.group(1).decode('latin-1').lower()
                if name not in files:
                    files[name] = 0

    except Exception as e:
        print(f"Error reading {filepath}: {e}", file=sys.stderr)
        return None

    return files

def get_s3d_info(filepath):
    """Get basic info about an S3D file"""
    info = {
        'filename': os.path.basename(filepath),
        'size_bytes': os.path.getsize(filepath),
        'files': {
            'wld': [],
            'bmp': [],
            'dds': [],
            'other': []
        }
    }

    try:
        with open(filepath, 'rb') as f:
            # Read PFS header
            f.seek(0)
            dir_offset = struct.unpack('<I', f.read(4))[0]
            magic = struct.unpack('<I', f.read(4))[0]

            if magic != PFS_MAGIC:
                info['error'] = 'Invalid PFS magic'
                return info

            # Read version
            version = struct.unpack('<I', f.read(4))[0]

            # Read directory
            f.seek(dir_offset)
            file_count = struct.unpack('<I', f.read(4))[0]

            entries = []
            for _ in range(file_count):
                crc = struct.unpack('<i', f.read(4))[0]
                offset = struct.unpack('<I', f.read(4))[0]
                size = struct.unpack('<I', f.read(4))[0]
                entries.append((crc, offset, size))

            # Read filename block (last entry in most archives)
            # The filename block contains null-separated filenames
            filename_entry = entries[-1] if entries else None
            filenames = []

            if filename_entry:
                f.seek(filename_entry[1])

                # Read and decompress filename block
                deflated_len = struct.unpack('<I', f.read(4))[0]
                inflated_len = struct.unpack('<I', f.read(4))[0]
                block_data = f.read(deflated_len)

                try:
                    if deflated_len < inflated_len:
                        name_data = zlib.decompress(block_data)
                    else:
                        name_data = block_data

                    # Parse null-separated names
                    names = name_data.split(b'\x00')
                    for name in names:
                        if name:
                            try:
                                name_str = name.decode('latin-1').lower()
                                filenames.append(name_str)
                            except:
                                pass
                except Exception as e:
                    pass

            # Categorize files
            for name in filenames:
                if name.endswith('.wld'):
                    info['files']['wld'].append(name)
                elif name.endswith('.bmp'):
                    info['files']['bmp'].append(name)
                elif name.endswith('.dds'):
                    info['files']['dds'].append(name)
                elif name and '.' in name:
                    info['files']['other'].append(name)

            info['total_files'] = len(filenames)
            info['file_count'] = file_count

    except Exception as e:
        info['error'] = str(e)

    return info

# Zone definitions
CLASSIC_ZONES = [
    "qeynos", "qeynos2", "qrg", "qeytoqrg", "highpass", "highkeep",
    "freportn", "freportw", "freporte", "qey2hh1", "northkarana",
    "southkarana", "eastkarana", "beholder", "blackburrow", "paw",
    "rivervale", "kithicor", "commons", "ecommons", "erudnint", "erudnext",
    "nektulos", "lavastorm", "halas", "everfrost", "soldunga", "soldungb",
    "misty", "nro", "sro", "befallen", "oasis", "tox", "hole",
    "neriaka", "neriakb", "neriakc", "najena", "qcat", "innothule",
    "feerrott", "cazicthule", "oggok", "rathemtn", "lakerathe", "grobb",
    "gfaydark", "akanon", "steamfont", "lfaydark", "crushbone", "mistmoore",
    "kaladima", "kaladimb", "felwithea", "felwitheb", "unrest", "kedge",
    "guktop", "gukbottom", "butcher", "oot", "cauldron", "airplane",
    "fearplane", "permafrost", "kerraridge", "paineel", "hateplane",
    "arena", "erudsxing", "soltemple", "runnyeye"
]

KUNARK_ZONES = [
    "fieldofbone", "warslikswood", "droga", "cabwest", "cabeast",
    "swampofnohope", "firiona", "lakeofillomen", "dreadlands",
    "burningwood", "kaesora", "sebilis", "citymist", "skyfire",
    "frontiermtns", "overthere", "emeraldjungle", "trakanon",
    "timorous", "kurn", "karnor", "chardok", "dalnir", "charasis",
    "nurga", "veeshan", "veksar"
]

VELIOUS_ZONES = [
    "iceclad", "frozenshadow", "velketor", "kael", "skyshrine",
    "thurgadina", "thurgadinb", "eastwastes", "cobaltscar", "greatdivide",
    "wakening", "westwastes", "crystal", "necropolis", "templeveeshan",
    "sirens", "mischiefplane", "growthplane", "sleeper"
]

GLOBAL_FILES = ["global", "global2", "global3", "global4", "global5", "global6", "global7"]

def process_zones(zones, expansion_name):
    """Process a list of zones and return their S3D info"""
    results = []
    suffixes = ["", "_chr", "_obj", "_chr2", "_2_obj"]

    for zone in zones:
        zone_data = {
            "zone": zone,
            "expansion": expansion_name,
            "s3d_files": []
        }

        for suffix in suffixes:
            s3d_path = os.path.join(EQ_PATH, f"{zone}{suffix}.s3d")
            if os.path.exists(s3d_path):
                print(f"  Processing: {zone}{suffix}.s3d")
                info = get_s3d_info(s3d_path)
                zone_data["s3d_files"].append(info)

        if zone_data["s3d_files"]:
            results.append(zone_data)

    return results

def main():
    reference = {
        "description": "Pre-Luclin EverQuest S3D file contents reference",
        "eq_client_path": EQ_PATH,
        "expansions": {}
    }

    # Process global files
    print("Processing global character files...")
    global_data = []
    for gfile in GLOBAL_FILES:
        s3d_path = os.path.join(EQ_PATH, f"{gfile}_chr.s3d")
        if os.path.exists(s3d_path):
            print(f"  Processing: {gfile}_chr.s3d")
            info = get_s3d_info(s3d_path)
            global_data.append({
                "name": gfile,
                "s3d_file": info
            })
    reference["global_character_files"] = global_data

    # Process each expansion
    print("\nProcessing Classic zones...")
    reference["expansions"]["classic"] = {
        "name": "Original EverQuest",
        "release_date": "1999-03-16",
        "zones": process_zones(CLASSIC_ZONES, "classic")
    }

    print("\nProcessing Kunark zones...")
    reference["expansions"]["kunark"] = {
        "name": "The Ruins of Kunark",
        "release_date": "2000-04-24",
        "zones": process_zones(KUNARK_ZONES, "kunark")
    }

    print("\nProcessing Velious zones...")
    reference["expansions"]["velious"] = {
        "name": "The Scars of Velious",
        "release_date": "2000-12-05",
        "zones": process_zones(VELIOUS_ZONES, "velious")
    }

    # Calculate totals
    total_files = len(global_data)
    total_s3d = sum(1 for g in global_data if g.get("s3d_file"))
    for exp in reference["expansions"].values():
        for zone in exp["zones"]:
            total_s3d += len(zone["s3d_files"])

    reference["summary"] = {
        "total_s3d_files": total_s3d,
        "global_files": len(global_data),
        "classic_zones": len(reference["expansions"]["classic"]["zones"]),
        "kunark_zones": len(reference["expansions"]["kunark"]["zones"]),
        "velious_zones": len(reference["expansions"]["velious"]["zones"])
    }

    # Write output
    print(f"\nWriting to {OUTPUT_FILE}...")
    with open(OUTPUT_FILE, 'w') as f:
        json.dump(reference, f, indent=2)

    print(f"Done! File size: {os.path.getsize(OUTPUT_FILE) / 1024:.1f} KB")
    print(f"Total S3D files processed: {total_s3d}")

if __name__ == '__main__':
    main()
PYTHON_SCRIPT
