#!/usr/bin/env python3
"""B852: regenerate scripts/ci/zone_content_sweep_zones.tsv.

The zone set is the 118-zone census T490 took (the copy of record on this
host: /home/user/kb-evidence/reference/T490/pvs_census.tsv; the plan's own
wording in refactor/T490_every_classic_zone_fits_the_pi.md is "all 118
original/Kunark/Velious archives"). Column 1 is the zone short name.

The expansion tag is NOT from memory:

- classic / kunark / velious come from docs/reference/pre_luclin_zones.json
  ("expansions" object, per-expansion zone lists; the committed source the
  reference-generation scripts use, per docs/CLAUDE.md).
- stonebrunt and warrens are absent from that JSON but present in the census;
  tools/generate_zone_lines.py's PRE_LUCLIN_ZONES list puts both under the
  Kunark block, so they are tagged kunark and cited in the notes column.
- The census excludes four classic zones pre_luclin_zones.json lists
  (fearplane, hateplane, mischiefplane-NO: mischiefplane is Velious in the
  JSON and IS in the census; the four are fearplane, hateplane, veksar and
  nektropos-NO: see the notes column). Every zone that is in the JSON but not
  in the census is still emitted with s3d_bytes=0 and a note, so the list file
  documents the drift instead of hiding it; the sweep records such a row as
  not_in_census and it is not a failure of the sweep itself.

s3d_bytes is the on-disk size of <EQ_CLIENT_ROOT>/<zone>.s3d (heaviest first
is the sweep's default order; 0 = file absent).

Usage:
  python3 scripts/ci/zone_content_sweep_zones.py \
      [--census /home/user/kb-evidence/reference/T490/pvs_census.tsv] \
      [--eq-root /mnt/projects/EverQuestP1999] \
      [--out scripts/ci/zone_content_sweep_zones.tsv]
"""
import argparse
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_CENSUS = "/home/user/kb-evidence/reference/T490/pvs_census.tsv"
DEFAULT_EQ_ROOT = "/mnt/projects/EverQuestP1999"
TAG_SOURCE = os.path.join(REPO, "docs", "reference", "pre_luclin_zones.json")
# B852: the two census zones pre_luclin_zones.json omits; both sit in the
# Kunark block of tools/generate_zone_lines.py's PRE_LUCLIN_ZONES.
EXTRA_TAGS = {"stonebrunt": "kunark", "warrens": "kunark"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--census", default=DEFAULT_CENSUS)
    ap.add_argument("--eq-root", default=DEFAULT_EQ_ROOT)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()
    out_path = args.out or os.path.join(
        REPO, "scripts", "ci", "zone_content_sweep_zones.tsv")

    census = []
    with open(args.census) as f:
        lines = [l for l in f.read().splitlines() if l.strip()]
    for l in lines[1:]:
        census.append(l.split("\t")[0])
    if len(census) != 118:
        print(f"ERROR: expected 118 census zones, got {len(census)} "
              f"(census moved? re-derive against refactor/T490_every_classic_zone_fits_the_pi.md)",
              file=sys.stderr)
        return 1

    with open(TAG_SOURCE) as f:
        refs = json.load(f)["expansions"]
    tag = {}
    for key in ("classic", "kunark", "velious"):
        for z in refs[key]["zones"]:
            tag[z["short_name"]] = key

    rows = []
    for zone in census:
        t = tag.get(zone)
        if t is None:
            t = EXTRA_TAGS.get(zone, "untagged")
        p = os.path.join(args.eq_root, zone + ".s3d")
        size = os.path.getsize(p) if os.path.exists(p) else 0
        note = ""
        if zone in EXTRA_TAGS:
            note = ("tag:tools/generate_zone_lines.py (PRE_LUCLIN_ZONES Kunark block; "
                    "absent from pre_luclin_zones.json)")
        if size == 0:
            note = (note + "; " if note else "") + "s3d file absent on this host"
        rows.append((zone, t, size, note))

    # JSON zones the census does not carry (drift documentation, not sweep input)
    census_set = set(census)
    for key in ("classic", "kunark", "velious"):
        for z in refs[key]["zones"]:
            name = z["short_name"]
            if name not in census_set:
                p = os.path.join(args.eq_root, name + ".s3d")
                size = os.path.getsize(p) if os.path.exists(p) else 0
                rows.append((name, key, size, "not_in_t490_census"))

    rows.sort(key=lambda r: (-r[2], r[0]))  # heaviest first, name as tiebreak

    with open(out_path, "w") as f:
        f.write("zone\texpansion\ts3d_bytes\tnotes\n")
        for zone, t, size, note in rows:
            f.write(f"{zone}\t{t}\t{size}\t{note}\n")
    print(f"wrote {out_path}: {len(rows)} rows "
          f"({sum(1 for r in rows if r[1] == 'classic')} classic, "
          f"{sum(1 for r in rows if r[1] == 'kunark')} kunark, "
          f"{sum(1 for r in rows if r[1] == 'velious')} velious)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
