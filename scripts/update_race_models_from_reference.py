#!/usr/bin/env python3
"""
Update race_models.json with S3D file locations from s3d_contents_reference.json.

This script:
1. Reads the S3D reference to find which files contain which race models
2. Compares against current race_models.json entries
3. Identifies missing, mismatched, and confirmed entries
4. Creates updated_race_models.json with corrected s3d_file values

Usage: python3 scripts/update_race_models_from_reference.py
"""

import json
from pathlib import Path

PROJECT_DIR = Path(__file__).parent.parent
REFERENCE_FILE = PROJECT_DIR / "data" / "s3d_contents_reference.json"
RACE_MODELS_FILE = PROJECT_DIR / "config" / "race_models.json"
OUTPUT_FILE = PROJECT_DIR / "config" / "updated_race_models.json"


def load_reference():
    """Load the S3D contents reference."""
    with open(REFERENCE_FILE) as f:
        return json.load(f)


def load_race_models():
    """Load current race models config."""
    with open(RACE_MODELS_FILE) as f:
        return json.load(f)


def build_code_to_s3d_mapping(reference):
    """Build a mapping of race codes to S3D files where they're found."""
    code_to_s3d_files = {}

    def process_s3d_file(s3d_info):
        filename = s3d_info.get('filename', '')
        models = s3d_info.get('models', [])

        for model in models:
            if 'race_code' in model:
                code = model['race_code'].upper()
                if code not in code_to_s3d_files:
                    code_to_s3d_files[code] = []
                if filename not in code_to_s3d_files[code]:
                    code_to_s3d_files[code].append(filename)

    # Process global files
    for gfile in reference.get('global_character_files', []):
        process_s3d_file(gfile.get('s3d_file', {}))

    # Process all zones
    for expansion in reference.get('expansions', {}).values():
        for zone in expansion.get('zones', []):
            for s3d_file in zone.get('s3d_files', []):
                process_s3d_file(s3d_file)

    return code_to_s3d_files


def analyze_race_models(race_models, code_to_s3d_files):
    """Analyze race models and categorize by status."""
    missing_s3d = []
    mismatched_s3d = []
    confirmed_s3d = []
    not_found_in_reference = []

    for race_id, info in race_models.get('races', {}).items():
        code = info.get('code', '').upper()
        current_s3d = info.get('s3d_file', '')

        # Check all code variants (male, female, neuter)
        codes_to_check = [code]
        if info.get('female_code'):
            codes_to_check.append(info['female_code'].upper())
        if info.get('neuter_code'):
            codes_to_check.append(info['neuter_code'].upper())
        if info.get('male_code'):
            codes_to_check.append(info['male_code'].upper())

        # Find all S3D files containing this race
        found_in = set()
        for c in codes_to_check:
            if c in code_to_s3d_files:
                found_in.update(code_to_s3d_files[c])

        entry = {
            'race_id': race_id,
            'code': code,
            'name': info.get('name', ''),
            'current_s3d': current_s3d,
            'found_in': sorted(found_in)
        }

        if not found_in:
            not_found_in_reference.append(entry)
        elif not current_s3d:
            missing_s3d.append(entry)
        elif current_s3d not in found_in:
            mismatched_s3d.append(entry)
        else:
            confirmed_s3d.append(entry)

    return {
        'confirmed': confirmed_s3d,
        'missing': missing_s3d,
        'mismatched': mismatched_s3d,
        'not_found': not_found_in_reference
    }


def select_best_s3d_file(found_in):
    """Select the best S3D file from a list of options."""
    if not found_in:
        return None

    sorted_files = sorted(found_in)

    # Prefer global_chr files (main character repository)
    for f in sorted_files:
        if f.startswith('global') and '_chr' in f:
            return f

    # Then prefer zone _chr files
    for f in sorted_files:
        if '_chr' in f and '_chr2' not in f:
            return f

    # Then _chr2 files
    for f in sorted_files:
        if '_chr2' in f:
            return f

    # Otherwise use first available
    return sorted_files[0]


def create_updated_race_models(race_models, code_to_s3d_files):
    """Create updated race models with corrected s3d_file entries."""
    updated_races = {}

    for race_id, info in race_models.get('races', {}).items():
        updated_info = info.copy()
        code = info.get('code', '').upper()

        # Check all code variants
        codes_to_check = [code]
        if info.get('female_code'):
            codes_to_check.append(info['female_code'].upper())
        if info.get('neuter_code'):
            codes_to_check.append(info['neuter_code'].upper())
        if info.get('male_code'):
            codes_to_check.append(info['male_code'].upper())

        # Find all S3D files containing this race
        found_in = set()
        for c in codes_to_check:
            if c in code_to_s3d_files:
                found_in.update(code_to_s3d_files[c])

        if found_in:
            best_file = select_best_s3d_file(found_in)
            if best_file:
                updated_info['s3d_file'] = best_file
                # Track additional locations
                other_files = [f for f in sorted(found_in) if f != best_file]
                if other_files:
                    updated_info['also_in'] = other_files

        updated_races[race_id] = updated_info

    return updated_races


def print_analysis(analysis):
    """Print the analysis results."""
    print("=== Race Model S3D File Analysis ===\n")
    print(f"Confirmed correct: {len(analysis['confirmed'])}")
    print(f"Missing s3d_file: {len(analysis['missing'])}")
    print(f"Mismatched s3d_file: {len(analysis['mismatched'])}")
    print(f"Not found in reference: {len(analysis['not_found'])}")

    if analysis['missing']:
        print("\n=== Missing s3d_file entries ===")
        for item in analysis['missing'][:25]:
            print(f"  Race {item['race_id']:>3} ({item['code']:>3}) {item['name']:<20}: {item['found_in']}")
        if len(analysis['missing']) > 25:
            print(f"  ... and {len(analysis['missing']) - 25} more")

    if analysis['mismatched']:
        print("\n=== Mismatched s3d_file entries ===")
        for item in analysis['mismatched']:
            print(f"  Race {item['race_id']:>3} ({item['code']:>3}) {item['name']}:")
            print(f"    Listed:   {item['current_s3d']}")
            print(f"    Found in: {item['found_in']}")

    if analysis['not_found']:
        print("\n=== Not found in any pre-Luclin S3D ===")
        for item in analysis['not_found'][:25]:
            s3d = item['current_s3d'] or '(none)'
            print(f"  Race {item['race_id']:>3} ({item['code']:>3}) {item['name']:<20}: listed as {s3d}")
        if len(analysis['not_found']) > 25:
            print(f"  ... and {len(analysis['not_found']) - 25} more")


def main():
    print("Loading S3D reference...")
    reference = load_reference()

    print("Loading race models...")
    race_models = load_race_models()

    print("Building race code to S3D mapping...")
    code_to_s3d_files = build_code_to_s3d_mapping(reference)
    print(f"Found {len(code_to_s3d_files)} unique race codes in S3D files\n")

    # Analyze current state
    analysis = analyze_race_models(race_models, code_to_s3d_files)
    print_analysis(analysis)

    # Create updated race models
    print("\n=== Creating updated race models ===")
    updated_races = create_updated_race_models(race_models, code_to_s3d_files)

    updated_race_models = {
        "_comment": race_models.get("_comment", "Pre-Luclin race ID to model code mappings"),
        "_version": "1.2",
        "_notes": "Updated with s3d_file locations verified against s3d_contents_reference.json",
        "races": updated_races
    }

    with open(OUTPUT_FILE, 'w') as f:
        json.dump(updated_race_models, f, indent=2)

    print(f"Written to: {OUTPUT_FILE}")

    # Count changes
    added_s3d = 0
    changed_s3d = 0
    added_also_in = 0

    for race_id, new_info in updated_races.items():
        old_info = race_models.get('races', {}).get(race_id, {})
        old_s3d = old_info.get('s3d_file', '')
        new_s3d = new_info.get('s3d_file', '')

        if new_s3d and not old_s3d:
            added_s3d += 1
        elif new_s3d != old_s3d and old_s3d:
            changed_s3d += 1

        if 'also_in' in new_info and 'also_in' not in old_info:
            added_also_in += 1

    print(f"\nChanges made:")
    print(f"  Added s3d_file: {added_s3d}")
    print(f"  Changed s3d_file: {changed_s3d}")
    print(f"  Added also_in: {added_also_in}")


if __name__ == '__main__':
    main()
