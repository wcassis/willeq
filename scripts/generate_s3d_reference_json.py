#!/usr/bin/env python3
"""
Generate a JSON reference of all pre-Luclin S3D file contents.
Uses the s3d_dump tool to extract file listings.

Usage: python3 scripts/generate_s3d_reference_json.py [eq_client_path]
"""

import os
import sys
import json
import subprocess
import re
from pathlib import Path

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_DIR = SCRIPT_DIR.parent
S3D_DUMP = PROJECT_DIR / "build" / "bin" / "s3d_dump"
OUTPUT_FILE = PROJECT_DIR / "data" / "s3d_contents_reference.json"
RACE_MODELS_FILE = PROJECT_DIR / "config" / "race_models.json"

# Default EQ client path
EQ_CLIENT_PATH = Path(sys.argv[1] if len(sys.argv) > 1 else "/home/user/projects/claude/EverQuestP1999")

# Load race models mapping
def load_race_models():
    """Load race_models.json and build mappings."""
    race_by_code = {}  # code -> race info with id
    races_by_s3d = {}  # s3d_file -> list of race ids

    if RACE_MODELS_FILE.exists():
        with open(RACE_MODELS_FILE) as f:
            data = json.load(f)
            for race_id, info in data.get("races", {}).items():
                code = info.get("code", "")
                info["race_id"] = int(race_id)
                race_by_code[code.upper()] = info

                # Also add female/neuter codes
                if info.get("female_code"):
                    race_by_code[info["female_code"].upper()] = info
                if info.get("neuter_code"):
                    race_by_code[info["neuter_code"].upper()] = info

                # Build s3d -> races mapping
                s3d_file = info.get("s3d_file", "")
                if s3d_file:
                    if s3d_file not in races_by_s3d:
                        races_by_s3d[s3d_file] = []
                    races_by_s3d[s3d_file].append({
                        "race_id": int(race_id),
                        "code": code,
                        "name": info.get("name", ""),
                        "scale": info.get("scale", 1.0)
                    })

    return race_by_code, races_by_s3d

RACE_BY_CODE, RACES_BY_S3D = load_race_models()

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


def extract_race_code_from_model(model_name):
    """Extract 3-letter race code from model name like 'HUM_HS_DEF' or 'HUMHE_DMSPRITEDEF'."""
    # Model names are like: HUM_HS_DEF, BAM_HS_DEF, ELE_HS_DEF
    # Or mesh names like: HUMHE_DMSPRITEDEF, HUMCH_DMSPRITEDEF
    if not model_name:
        return None

    # Try to extract first 3 characters as race code
    name_upper = model_name.upper()

    # Remove common suffixes to get base name
    for suffix in ["_HS_DEF", "_DMSPRITEDEF", "_TRACKDEF", "_ACTORDEF"]:
        if suffix in name_upper:
            name_upper = name_upper.split(suffix)[0]
            break

    # Extract first 3 chars (race code)
    if len(name_upper) >= 3:
        code = name_upper[:3]
        if code in RACE_BY_CODE:
            return code

    return None


def get_race_info_for_model(model_name):
    """Get race info (id, name, scale) for a model name."""
    code = extract_race_code_from_model(model_name)
    if code and code in RACE_BY_CODE:
        info = RACE_BY_CODE[code]
        return {
            "race_id": info.get("race_id"),
            "race_code": code,
            "race_name": info.get("name", "")
        }
    return None


def parse_s3d_dump_output(output, s3d_filename=""):
    """Parse the markdown output from s3d_dump into structured data."""
    info = {
        "wld_files": [],
        "bmp_textures": [],
        "dds_textures": [],
        "other_files": [],
        "models": [],
        "race_ids": [],  # Race IDs found in this file
        "total_files": 0
    }

    # Add races from the s3d -> races mapping
    if s3d_filename in RACES_BY_S3D:
        info["race_ids"] = RACES_BY_S3D[s3d_filename]

    found_race_ids = set()  # Track race IDs found from model names

    lines = output.split('\n')
    current_section = None
    current_model = None

    for line in lines:
        # Parse archive summary
        if '| WLD Files' in line:
            match = re.search(r'\| (\d+) \|', line)
            if match:
                info['wld_count'] = int(match.group(1))
        elif '| BMP Textures' in line:
            match = re.search(r'\| (\d+) \|', line)
            if match:
                info['bmp_count'] = int(match.group(1))
        elif '| DDS Textures' in line:
            match = re.search(r'\| (\d+) \|', line)
            if match:
                info['dds_count'] = int(match.group(1))
        elif '| **Total Files**' in line:
            match = re.search(r'\*\*(\d+)\*\*', line)
            if match:
                info['total_files'] = int(match.group(1))

        # Parse WLD info
        elif line.startswith('### ') and '.wld' in line:
            wld_name = line.replace('### ', '').strip()
            info['wld_files'].append(wld_name)
        elif '**Format**:' in line:
            info['wld_format'] = 'old' if 'Old' in line else 'new'
        elif '**Fragment Count**:' in line:
            match = re.search(r'(\d+)', line)
            if match:
                info['fragment_count'] = int(match.group(1))

        # Parse character models
        elif line.startswith('### ') and '_HS_DEF' in line:
            model_name = line.replace('### ', '').strip()
            current_model = {
                'name': model_name,
                'bones': 0,
                'mesh_parts': 0,
                'vertices': 0,
                'triangles': 0,
                'textures': []
            }
            # Try to identify race from model name
            race_info = get_race_info_for_model(model_name)
            if race_info:
                current_model['race_id'] = race_info['race_id']
                current_model['race_code'] = race_info['race_code']
                current_model['race_name'] = race_info['race_name']
                found_race_ids.add(race_info['race_id'])
            info['models'].append(current_model)
        elif current_model and '**Bones**:' in line:
            match = re.search(r'(\d+)', line)
            if match:
                current_model['bones'] = int(match.group(1))
        elif current_model and '**Mesh Parts**:' in line:
            match = re.search(r'(\d+)', line)
            if match:
                current_model['mesh_parts'] = int(match.group(1))
        elif current_model and '**Total Vertices**:' in line:
            match = re.search(r'(\d+)', line)
            if match:
                current_model['vertices'] = int(match.group(1))
        elif current_model and '**Total Triangles**:' in line:
            match = re.search(r'(\d+)', line)
            if match:
                current_model['triangles'] = int(match.group(1))
        elif current_model and '`' in line and '.bmp`' in line:
            # Extract texture names
            textures = re.findall(r'`([^`]+\.(?:bmp|dds))`', line)
            current_model['textures'].extend(textures)

        # Parse texture listings
        elif line.strip().startswith('├── ') or line.strip().startswith('└── '):
            filename = line.split('── ')[-1].split(' →')[0].strip()
            if filename.endswith('.bmp'):
                if filename not in info['bmp_textures']:
                    info['bmp_textures'].append(filename)
            elif filename.endswith('.dds'):
                if filename not in info['dds_textures']:
                    info['dds_textures'].append(filename)

    # Merge found race IDs with pre-configured ones
    if found_race_ids:
        existing_ids = {r['race_id'] for r in info['race_ids']}
        for race_id in found_race_ids:
            if race_id not in existing_ids:
                # Look up race info
                for code, race_info in RACE_BY_CODE.items():
                    if race_info.get('race_id') == race_id:
                        info['race_ids'].append({
                            "race_id": race_id,
                            "code": race_info.get('code', code),
                            "name": race_info.get('name', ''),
                            "scale": race_info.get('scale', 1.0)
                        })
                        break

    return info


def get_s3d_info(s3d_path):
    """Get info about an S3D file using s3d_dump."""
    filename = s3d_path.name
    info = {
        "filename": filename,
        "size_bytes": s3d_path.stat().st_size,
    }

    try:
        result = subprocess.run(
            [str(S3D_DUMP), str(s3d_path)],
            capture_output=True,
            text=True,
            timeout=60
        )

        if result.returncode == 0:
            parsed = parse_s3d_dump_output(result.stdout, filename)
            info.update(parsed)
        else:
            info['error'] = result.stderr[:200] if result.stderr else 'Unknown error'
    except subprocess.TimeoutExpired:
        info['error'] = 'Timeout'
    except Exception as e:
        info['error'] = str(e)

    return info


def process_zones(zones, expansion_name):
    """Process a list of zones and return their S3D info."""
    results = []
    suffixes = ["", "_chr", "_obj", "_chr2", "_2_obj"]

    for zone in zones:
        zone_data = {
            "zone": zone,
            "s3d_files": []
        }

        for suffix in suffixes:
            s3d_path = EQ_CLIENT_PATH / f"{zone}{suffix}.s3d"
            if s3d_path.exists():
                print(f"  Processing: {zone}{suffix}.s3d")
                info = get_s3d_info(s3d_path)
                zone_data["s3d_files"].append(info)

        if zone_data["s3d_files"]:
            results.append(zone_data)

    return results


def main():
    if not S3D_DUMP.exists():
        print(f"Error: s3d_dump not found at {S3D_DUMP}")
        print("Please build the project first: cd build && cmake --build .")
        sys.exit(1)

    if not EQ_CLIENT_PATH.exists():
        print(f"Error: EQ client directory not found at {EQ_CLIENT_PATH}")
        sys.exit(1)

    print("=== Pre-Luclin S3D JSON Reference Generator ===")
    print(f"EQ Client Path: {EQ_CLIENT_PATH}")
    print(f"Output File: {OUTPUT_FILE}")
    print()

    reference = {
        "description": "Pre-Luclin EverQuest S3D file contents reference",
        "eq_client_path": str(EQ_CLIENT_PATH),
        "global_character_files": [],
        "expansions": {}
    }

    # Process global files
    print("Processing global character files...")
    for gfile in GLOBAL_FILES:
        s3d_path = EQ_CLIENT_PATH / f"{gfile}_chr.s3d"
        if s3d_path.exists():
            print(f"  Processing: {gfile}_chr.s3d")
            info = get_s3d_info(s3d_path)
            reference["global_character_files"].append({
                "name": gfile,
                "s3d_file": info
            })

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
    total_s3d = len(reference["global_character_files"])
    total_models = sum(len(g["s3d_file"].get("models", [])) for g in reference["global_character_files"])

    for exp in reference["expansions"].values():
        for zone in exp["zones"]:
            total_s3d += len(zone["s3d_files"])
            for s3d in zone["s3d_files"]:
                total_models += len(s3d.get("models", []))

    reference["summary"] = {
        "total_s3d_files": total_s3d,
        "total_models": total_models,
        "global_files": len(reference["global_character_files"]),
        "classic_zones": len(reference["expansions"]["classic"]["zones"]),
        "kunark_zones": len(reference["expansions"]["kunark"]["zones"]),
        "velious_zones": len(reference["expansions"]["velious"]["zones"])
    }

    # Write output
    print(f"\nWriting to {OUTPUT_FILE}...")
    with open(OUTPUT_FILE, 'w') as f:
        json.dump(reference, f, indent=2)

    file_size = OUTPUT_FILE.stat().st_size / 1024
    print(f"Done! File size: {file_size:.1f} KB")
    print(f"Total S3D files: {total_s3d}")
    print(f"Total character models: {total_models}")


if __name__ == '__main__':
    main()
