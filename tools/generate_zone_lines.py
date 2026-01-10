#!/usr/bin/env python3
"""
Generate zone_lines.json from zone_points.json

This script reads zone_points.json and generates trigger boxes for zone lines
using the methodology documented in .agent/zone_line_methodology.md.

Coordinate Systems:
- zone_points.json uses server coordinates (server_x, server_y)
- zone_lines.json uses display coordinates (m_x, m_y)
- Conversion: display_x (m_x) = server_y, display_y (m_y) = server_x
"""

import json
import argparse
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

# Pre-Luclin zones (Classic + Kunark + Velious)
PRE_LUCLIN_ZONES = {
    # Classic
    'qeynos', 'qeynos2', 'qrg', 'qeytoqrg', 'highpass', 'highkeep',
    'freportn', 'freportw', 'freporte', 'runnyeye', 'qey2hh1',
    'northkarana', 'southkarana', 'eastkarana', 'beholder', 'blackburrow',
    'paw', 'rivervale', 'kithicor', 'commons', 'ecommons',
    'erudnint', 'erudnext', 'nektulos', 'lavastorm', 'nektropos',
    'halas', 'everfrost', 'soldunga', 'soldungb', 'misty', 'nro', 'sro',
    'befallen', 'oasis', 'tox', 'hole', 'neriaka', 'neriakb', 'neriakc',
    'najena', 'qcat', 'innothule', 'feerrott', 'cazicthule', 'oggok',
    'rathemtn', 'lakerathe', 'grobb', 'aviak', 'gfaydark', 'akanon',
    'steamfont', 'lfaydark', 'crushbone', 'mistmoore', 'kaladima',
    'felwithea', 'felwitheb', 'unrest', 'kedge', 'guktop', 'gukbottom',
    'kaladimb', 'butcher', 'oot', 'cauldron', 'airplane', 'fearplane',
    'permafrost', 'kerraridge', 'paineel', 'hateplane', 'arena',
    # Kunark
    'fieldofbone', 'warslikswood', 'soltemple', 'droga', 'cabwest',
    'swampofnohope', 'firiona', 'lakeofillomen', 'dreadlands',
    'burningwood', 'kaesora', 'sebilis', 'citymist', 'skyfire',
    'frontiermtns', 'overthere', 'emeraldjungle', 'trakanon',
    'timorous', 'kurn', 'erudsxing', 'stonebrunt', 'warrens',
    'karnor', 'chardok', 'dalnir', 'charasis', 'cabeast', 'nurga',
    'veeshan', 'veksar',
    # Velious
    'iceclad', 'frozenshadow', 'velketor', 'kael', 'skyshrine',
    'thurgadina', 'eastwastes', 'cobaltscar', 'greatdivide', 'wakening',
    'westwastes', 'crystal', 'necropolis', 'templeveeshan', 'sirens',
    'mischiefplane', 'growthplane', 'sleeper', 'thurgadinb', 'erudsxing2'
}

# Zone ID to name mapping
ZONE_ID_TO_NAME = {
    1: "qeynos", 2: "qeynos2", 3: "qrg", 4: "qeytoqrg", 5: "highpass",
    6: "highkeep", 8: "freportn", 9: "freportw", 10: "freporte",
    11: "runnyeye", 12: "qey2hh1", 13: "northkarana", 14: "southkarana",
    15: "eastkarana", 16: "beholder", 17: "blackburrow", 18: "paw",
    19: "rivervale", 20: "kithicor", 21: "commons", 22: "ecommons",
    23: "erudnint", 24: "erudnext", 25: "nektulos", 26: "cshome",
    27: "lavastorm", 28: "nektropos", 29: "halas", 30: "everfrost",
    31: "soldunga", 32: "soldungb", 33: "misty", 34: "nro",
    35: "sro", 36: "befallen", 37: "oasis", 38: "tox",
    39: "hole", 40: "neriaka", 41: "neriakb", 42: "neriakc",
    43: "neriakd", 44: "najena", 45: "qcat", 46: "innothule",
    47: "feerrott", 48: "cazicthule", 49: "oggok", 50: "rathemtn",
    51: "lakerathe", 52: "grobb", 53: "aviak", 54: "gfaydark",
    55: "akanon", 56: "steamfont", 57: "lfaydark", 58: "crushbone",
    59: "mistmoore", 60: "kaladima", 61: "felwithea", 62: "felwitheb",
    63: "unrest", 64: "kedge", 65: "guktop", 66: "gukbottom",
    67: "kaladimb", 68: "butcher", 69: "oot", 70: "cauldron",
    71: "airplane", 72: "fearplane", 73: "permafrost", 74: "kerraridge",
    75: "paineel", 76: "hateplane", 77: "arena", 78: "fieldofbone",
    79: "warslikswood", 80: "soltemple", 81: "droga", 82: "cabwest",
    83: "swampofnohope", 84: "firiona", 85: "lakeofillomen", 86: "dreadlands",
    87: "burningwood", 88: "kaesora", 89: "sebilis", 90: "citymist",
    91: "skyfire", 92: "frontiermtns", 93: "overthere", 94: "emeraldjungle",
    95: "trakanon", 96: "timorous", 97: "kurn", 98: "erudsxing",
    100: "stonebrunt", 101: "warrens", 102: "karnor", 103: "chardok",
    104: "dalnir", 105: "charasis", 106: "cabeast", 107: "nurga",
    108: "veeshan", 109: "veksar", 110: "iceclad", 111: "frozenshadow",
    112: "velketor", 113: "kael", 114: "skyshrine", 115: "thurgadina",
    116: "eastwastes", 117: "cobaltscar", 118: "greatdivide", 119: "wakening",
    120: "westwastes", 121: "crystal", 123: "necropolis", 124: "templeveeshan",
    125: "sirens", 126: "mischiefplane", 127: "growthplane", 128: "sleeper",
    129: "thurgadinb", 130: "erudsxing2",
}

# Build reverse mapping
ZONE_NAME_TO_ID = {v: k for k, v in ZONE_ID_TO_NAME.items()}

# Special marker for "use current coordinate"
SAME_COORD_MARKER = 999999.0

# Configuration
SAFETY_MARGIN = 20.0  # Units BEFORE zone-in point (toward zone interior) for zone-out trigger
TRIGGER_DEPTH = 10.0  # Depth of trigger box in direction of travel
CONFINED_WIDTH = 5.0  # Initial width of confined zone line boxes (will be expanded at runtime)
CONFINED_DEPTH = 5.0  # Initial depth of confined zone line boxes (will be expanded at runtime)
DEFAULT_Z_MIN = -50.0  # Default Z range for trigger boxes
DEFAULT_Z_MAX = 50.0
CONTINUOUS_SPAN = 5000.0  # How far to extend continuous zone lines


@dataclass
class ZonePoint:
    """Represents a zone point from zone_points.json"""
    zone: str
    number: int
    source_x: float  # server coords
    source_y: float
    source_z: float
    target_zone_id: int
    target_x: float  # server coords (or 999999)
    target_y: float
    target_z: float
    target_heading: float

    @property
    def target_zone_name(self) -> str:
        return ZONE_ID_TO_NAME.get(self.target_zone_id, f"unknown_{self.target_zone_id}")

    @property
    def is_continuous_x(self) -> bool:
        return abs(self.target_x) >= SAME_COORD_MARKER - 1

    @property
    def is_continuous_y(self) -> bool:
        return abs(self.target_y) >= SAME_COORD_MARKER - 1


@dataclass
class TriggerBox:
    """Trigger box in display coordinates (m_x, m_y, m_z)"""
    min_x: float
    max_x: float
    min_y: float
    max_y: float
    min_z: float
    max_z: float


@dataclass
class ZoneLine:
    """Generated zone line entry"""
    zone_point_index: int
    destination_zone: str
    destination_zone_id: int
    trigger_box: TriggerBox
    dest_x: float
    dest_y: float
    dest_z: float
    dest_heading: float
    notes: str = ""
    needs_review: bool = False


def load_zone_points(filepath: str) -> List[ZonePoint]:
    """Load zone points from JSON file"""
    with open(filepath, 'r') as f:
        data = json.load(f)

    zone_points = []
    for entry in data:
        zp = ZonePoint(
            zone=entry['zone'],
            number=entry['number'],
            source_x=entry['source_x'],
            source_y=entry['source_y'],
            source_z=entry['source_z'],
            target_zone_id=entry['target_zone_id'],
            target_x=entry['target_x'],
            target_y=entry['target_y'],
            target_z=entry['target_z'],
            target_heading=entry['target_heading']
        )
        zone_points.append(zp)

    return zone_points


def build_zone_graph(zone_points: List[ZonePoint]) -> Dict[str, List[ZonePoint]]:
    """Build a graph of zone points indexed by source zone"""
    graph = defaultdict(list)
    for zp in zone_points:
        graph[zp.zone].append(zp)
    return graph


def find_reverse_connection(
    zone_points: List[ZonePoint],
    source_zone: str,
    target_zone_id: int,
    source_zp: Optional[ZonePoint] = None
) -> Optional[ZonePoint]:
    """
    Find the reverse zone point (from target zone back to source zone).
    Returns the zone point in target_zone that goes to source_zone.

    If source_zp is provided, tries to find the best match based on:
    1. Matching zone point index (number)
    2. Position proximity (target of reverse should be near source of forward)
    """
    source_zone_id = ZONE_NAME_TO_ID.get(source_zone)
    if source_zone_id is None:
        return None

    target_zone_name = ZONE_ID_TO_NAME.get(target_zone_id)
    if target_zone_name is None:
        return None

    # Find zone points in target zone that go to source zone
    candidates = [
        zp for zp in zone_points
        if zp.zone == target_zone_name and zp.target_zone_id == source_zone_id
    ]

    if not candidates:
        return None

    if len(candidates) == 1:
        return candidates[0]

    # Multiple candidates - try to find best match
    if source_zp:
        # Try matching by zone point index first
        for c in candidates:
            if c.number == source_zp.number:
                return c

        # Try matching by position proximity
        # The reverse target should be close to our source position
        def distance_sq(c: ZonePoint) -> float:
            # Handle 999999 markers
            tx = c.target_x if abs(c.target_x) < SAME_COORD_MARKER - 1 else source_zp.source_x
            ty = c.target_y if abs(c.target_y) < SAME_COORD_MARKER - 1 else source_zp.source_y
            dx = tx - source_zp.source_x
            dy = ty - source_zp.source_y
            return dx * dx + dy * dy

        # Sort by distance and return closest
        candidates.sort(key=distance_sq)

    return candidates[0]


def server_to_display(server_x: float, server_y: float) -> Tuple[float, float]:
    """Convert server coordinates to display coordinates (m_x, m_y)"""
    # m_x = server_y, m_y = server_x (swapped)
    return server_y, server_x


def calculate_trigger_box(
    zone_point: ZonePoint,
    reverse_zp: Optional[ZonePoint]
) -> Tuple[TriggerBox, str]:
    """
    Calculate the trigger box for a zone point.
    Returns (TriggerBox, notes_string)

    Key insight: The zone-out trigger must be PAST the zone-in point.
    - Zone-in point: where players arrive when coming FROM the destination zone
    - Zone-out trigger: must be beyond zone-in, in the direction AWAY from zone interior
    - Direction: from source (interior) toward zone-in (near border), then past it
    """
    notes_parts = []

    # Determine if this is a continuous or confined zone line
    is_continuous = zone_point.is_continuous_x or zone_point.is_continuous_y

    if reverse_zp:
        # We have the reverse connection - use its target as zone-in point
        # reverse_zp.target_x/y is where players zone INTO our source zone
        zone_in_server_x = reverse_zp.target_x
        zone_in_server_y = reverse_zp.target_y
        zone_in_z = reverse_zp.target_z

        # Handle 999999 markers in reverse target
        if abs(zone_in_server_x) >= SAME_COORD_MARKER - 1:
            zone_in_server_x = zone_point.source_x
        if abs(zone_in_server_y) >= SAME_COORD_MARKER - 1:
            zone_in_server_y = zone_point.source_y

        # Convert to display coords
        zone_in_mx, zone_in_my = server_to_display(zone_in_server_x, zone_in_server_y)

        # Source position in display coords (this is inside the zone, near the zone line)
        source_mx, source_my = server_to_display(zone_point.source_x, zone_point.source_y)

        # Direction vector from source (interior) toward zone-in (near border)
        # The trigger should be PAST zone-in in this same direction
        dx = zone_in_mx - source_mx  # positive if zone-in is in +X direction from source
        dy = zone_in_my - source_my  # positive if zone-in is in +Y direction from source

        # Handle case where source ≈ zone-in (portal-like zone lines)
        # These are typically gates/portals where you step on a spot and zone
        # Without clear direction info, create a centered box and flag for review
        is_portal_like = abs(dx) < 10 and abs(dy) < 10
        if is_portal_like:
            notes_parts.append("PORTAL-LIKE (needs review)")

        notes_parts.append(f"Zone-in at m_x={zone_in_mx:.0f}, m_y={zone_in_my:.0f}")

        if is_continuous:
            # Continuous zone line - extends across terrain
            # IMPORTANT: Trigger placed BEFORE zone-in (toward zone interior)
            # so that when player zones in, they appear PAST the trigger
            if zone_point.is_continuous_x:
                # X (server) is continuous -> m_y (display) spans full range
                # Trigger on m_x axis
                if dx >= 0:
                    # Zone-in is in +X direction, trigger BEFORE it (toward -X / interior)
                    trigger_mx = zone_in_mx - SAFETY_MARGIN
                    max_x = trigger_mx
                    min_x = trigger_mx - TRIGGER_DEPTH
                    notes_parts.append(f"Continuous m_y, trigger at m_x<{trigger_mx:.0f}")
                else:
                    # Zone-in is in -X direction, trigger BEFORE it (toward +X / interior)
                    trigger_mx = zone_in_mx + SAFETY_MARGIN
                    min_x = trigger_mx
                    max_x = trigger_mx + TRIGGER_DEPTH
                    notes_parts.append(f"Continuous m_y, trigger at m_x>{trigger_mx:.0f}")

                min_y = -CONTINUOUS_SPAN
                max_y = CONTINUOUS_SPAN

            else:  # zone_point.is_continuous_y
                # Y (server) is continuous -> m_x (display) spans full range
                # Trigger on m_y axis
                if dy >= 0:
                    # Zone-in is in +Y direction, trigger BEFORE it (toward -Y / interior)
                    trigger_my = zone_in_my - SAFETY_MARGIN
                    max_y = trigger_my
                    min_y = trigger_my - TRIGGER_DEPTH
                    notes_parts.append(f"Continuous m_x, trigger at m_y<{trigger_my:.0f}")
                else:
                    # Zone-in is in -Y direction, trigger BEFORE it (toward +Y / interior)
                    trigger_my = zone_in_my + SAFETY_MARGIN
                    min_y = trigger_my
                    max_y = trigger_my + TRIGGER_DEPTH
                    notes_parts.append(f"Continuous m_x, trigger at m_y>{trigger_my:.0f}")

                min_x = -CONTINUOUS_SPAN
                max_x = CONTINUOUS_SPAN
        else:
            # Confined zone line - small seed box (will be expanded at runtime)
            # IMPORTANT: Trigger placed BEFORE zone-in (toward zone interior)
            if is_portal_like:
                # Portal-like: center box on zone-in position
                # Use a square box since we don't know direction
                half_size = CONFINED_WIDTH / 2
                min_x = zone_in_mx - half_size
                max_x = zone_in_mx + half_size
                min_y = zone_in_my - half_size
                max_y = zone_in_my + half_size
                notes_parts.append("Centered box (needs direction review)")
            elif abs(dx) > abs(dy):
                # Primary movement is in X direction
                if dx >= 0:
                    # Zone-in in +X, trigger BEFORE it (toward -X)
                    max_x = zone_in_mx - SAFETY_MARGIN
                    min_x = max_x - CONFINED_DEPTH
                else:
                    # Zone-in in -X, trigger BEFORE it (toward +X)
                    min_x = zone_in_mx + SAFETY_MARGIN
                    max_x = min_x + CONFINED_DEPTH
                min_y = zone_in_my - CONFINED_WIDTH / 2
                max_y = zone_in_my + CONFINED_WIDTH / 2
                notes_parts.append("Confined zone line (seed box)")
            else:
                # Primary movement is in Y direction
                if dy >= 0:
                    # Zone-in in +Y, trigger BEFORE it (toward -Y)
                    max_y = zone_in_my - SAFETY_MARGIN
                    min_y = max_y - CONFINED_DEPTH
                else:
                    # Zone-in in -Y, trigger BEFORE it (toward +Y)
                    min_y = zone_in_my + SAFETY_MARGIN
                    max_y = min_y + CONFINED_DEPTH
                min_x = zone_in_mx - CONFINED_WIDTH / 2
                max_x = zone_in_mx + CONFINED_WIDTH / 2
                notes_parts.append("Confined zone line (seed box)")

        # Z range based on zone-in Z
        min_z = zone_in_z - 30
        max_z = zone_in_z + 30

    else:
        # No reverse connection - use source position with default box
        source_mx, source_my = server_to_display(zone_point.source_x, zone_point.source_y)

        if is_continuous:
            if zone_point.is_continuous_x:
                min_x = source_mx - TRIGGER_DEPTH / 2
                max_x = source_mx + TRIGGER_DEPTH / 2
                min_y = -CONTINUOUS_SPAN
                max_y = CONTINUOUS_SPAN
            else:
                min_x = -CONTINUOUS_SPAN
                max_x = CONTINUOUS_SPAN
                min_y = source_my - TRIGGER_DEPTH / 2
                max_y = source_my + TRIGGER_DEPTH / 2
        else:
            min_x = source_mx - CONFINED_WIDTH / 2
            max_x = source_mx + CONFINED_WIDTH / 2
            min_y = source_my - CONFINED_WIDTH / 2
            max_y = source_my + CONFINED_WIDTH / 2

        min_z = zone_point.source_z - 30
        max_z = zone_point.source_z + 30

        notes_parts.append("NO REVERSE CONNECTION - needs review")

    trigger_box = TriggerBox(
        min_x=min_x,
        max_x=max_x,
        min_y=min_y,
        max_y=max_y,
        min_z=min_z,
        max_z=max_z
    )

    return trigger_box, "; ".join(notes_parts)


def generate_zone_lines(
    zone_points: List[ZonePoint],
    zones_to_process: Optional[Set[str]] = None
) -> Dict[str, List[ZoneLine]]:
    """Generate zone lines for all specified zones"""

    if zones_to_process is None:
        zones_to_process = PRE_LUCLIN_ZONES

    # Filter to requested zones
    filtered_zps = [zp for zp in zone_points if zp.zone in zones_to_process]

    # Build graph
    graph = build_zone_graph(filtered_zps)

    result = {}

    for zone_name in sorted(zones_to_process):
        if zone_name not in graph:
            continue

        zone_lines = []

        for zp in graph[zone_name]:
            # Skip if target zone is not in our pre-Luclin list
            target_name = zp.target_zone_name
            if target_name.startswith("unknown_"):
                continue

            # Find reverse connection
            reverse_zp = find_reverse_connection(zone_points, zone_name, zp.target_zone_id, zp)

            # Calculate trigger box
            trigger_box, notes = calculate_trigger_box(zp, reverse_zp)

            # Flag for review if no reverse connection or if notes indicate issues
            needs_review = (reverse_zp is None) or ("needs review" in notes.lower())

            zone_line = ZoneLine(
                zone_point_index=zp.number,
                destination_zone=target_name,
                destination_zone_id=zp.target_zone_id,
                trigger_box=trigger_box,
                dest_x=zp.target_x,
                dest_y=zp.target_y,
                dest_z=zp.target_z,
                dest_heading=zp.target_heading,
                notes=notes,
                needs_review=needs_review
            )

            zone_lines.append(zone_line)

        if zone_lines:
            result[zone_name] = zone_lines

    return result


def zone_lines_to_json(zone_lines: Dict[str, List[ZoneLine]]) -> dict:
    """Convert zone lines to JSON-serializable format"""
    output = {}

    for zone_name, lines in zone_lines.items():
        zone_data = {
            "zone_name": zone_name,
            "zone_lines": []
        }

        for zl in lines:
            entry = {
                "zone_point_index": zl.zone_point_index,
                "destination_zone": zl.destination_zone,
                "destination_zone_id": zl.destination_zone_id,
                "trigger_box": {
                    "min_x": round(zl.trigger_box.min_x, 1),
                    "max_x": round(zl.trigger_box.max_x, 1),
                    "min_y": round(zl.trigger_box.min_y, 1),
                    "max_y": round(zl.trigger_box.max_y, 1),
                    "min_z": round(zl.trigger_box.min_z, 1),
                    "max_z": round(zl.trigger_box.max_z, 1),
                },
                "destination": {
                    "x": zl.dest_x,
                    "y": zl.dest_y,
                    "z": zl.dest_z,
                    "heading": zl.dest_heading
                },
                "notes": zl.notes
            }

            if zl.needs_review:
                entry["needs_review"] = True

            zone_data["zone_lines"].append(entry)

        output[zone_name] = zone_data

    return output


def main():
    parser = argparse.ArgumentParser(description='Generate zone_lines.json from zone_points.json')
    parser.add_argument('--input', '-i', default='data/zone_points.json',
                        help='Input zone_points.json file')
    parser.add_argument('--output', '-o', default='data/zone_lines.json',
                        help='Output zone_lines.json file')
    parser.add_argument('--zones', '-z', nargs='*',
                        help='Specific zones to process (default: all pre-Luclin)')
    parser.add_argument('--dry-run', '-n', action='store_true',
                        help='Print output instead of writing to file')
    parser.add_argument('--stats', '-s', action='store_true',
                        help='Print statistics about generation')

    args = parser.parse_args()

    # Load zone points
    print(f"Loading zone points from {args.input}...")
    zone_points = load_zone_points(args.input)
    print(f"Loaded {len(zone_points)} zone points")

    # Determine zones to process
    if args.zones:
        zones_to_process = set(args.zones)
    else:
        zones_to_process = PRE_LUCLIN_ZONES

    print(f"Processing {len(zones_to_process)} zones...")

    # Generate zone lines
    zone_lines = generate_zone_lines(zone_points, zones_to_process)

    # Convert to JSON
    output = zone_lines_to_json(zone_lines)

    # Statistics
    total_zones = len(output)
    total_lines = sum(len(zd["zone_lines"]) for zd in output.values())
    needs_review = sum(
        1 for zd in output.values()
        for zl in zd["zone_lines"]
        if zl.get("needs_review")
    )

    print(f"\nGenerated {total_lines} zone lines across {total_zones} zones")
    print(f"Zone lines needing review: {needs_review}")

    if args.stats:
        print("\nZones with zone lines:")
        for zone_name in sorted(output.keys()):
            count = len(output[zone_name]["zone_lines"])
            review_count = sum(1 for zl in output[zone_name]["zone_lines"] if zl.get("needs_review"))
            review_str = f" ({review_count} need review)" if review_count > 0 else ""
            print(f"  {zone_name}: {count}{review_str}")

    # Output
    json_str = json.dumps(output, indent=2)

    if args.dry_run:
        print("\n--- Generated JSON ---")
        print(json_str)
    else:
        with open(args.output, 'w') as f:
            f.write(json_str)
        print(f"\nWritten to {args.output}")


if __name__ == '__main__':
    main()
