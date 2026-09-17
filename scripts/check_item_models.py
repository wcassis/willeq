#!/usr/bin/env python3
"""T498 (board B755): compare data/item_models.json against the server's items.idfile.

The server turns a weapon's item row into an appearance material with
    Mob::GetEquipmentMaterial() -> atoi(&item->IDFile[2])
(/mnt/projects/akk-stack/code/zone/mob_appearance.cpp), so the numeric part of
`IT<n>` is the model number every render site ultimately wants.  This script is
PURE PYTHON and OFFLINE: it reads the committed export
`data/item_idfile.tsv` (written by scripts/eqemu/export_item_idfile.sh) and
never touches a live mariadb, so ctest can run it.

Modes:
  --check   exit non-zero if any item_models.json row disagrees with the export
  --diff    write a one-row-per-defect TSV plus the collision census
  --rebuild write a fresh data/item_models.json from the export (task 4)
"""
import argparse
import json
import os
import sys

DEFECT_CLASSES = ("mismatch", "not-on-server", "server-only", "non-numeric-idfile")


def repo_root(explicit):
    if explicit:
        return os.path.abspath(explicit)
    return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def review_root(root):
    """review/ lives BESIDE the checkout, never inside it: <ws>/review for a
    canonical checkout at <ws>/main, and for a task worktree at <ws>/wt/<slot>."""
    parent = os.path.dirname(root)
    if os.path.basename(parent) == "wt":
        parent = os.path.dirname(parent)
    return os.path.join(parent, "review")


def load_export(path):
    """-> {id: (name, idfile, numeric_or_None)}, header_lines"""
    rows = {}
    header = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("#"):
                header.append(line)
                continue
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                continue
            try:
                item_id = int(parts[0])
            except ValueError:
                continue
            name, idfile = parts[1], parts[2]
            num = None
            if idfile.startswith("IT") and idfile[2:].isdigit():
                num = int(idfile[2:])
            rows[item_id] = (name, idfile, num)
    return rows, header


def load_json_map(path):
    """-> ({itemId:int -> modelId:int}, [skipped non-numeric keys])"""
    with open(path, "r", encoding="utf-8") as fh:
        raw = json.load(fh)
    out = {}
    skipped = []
    for key, value in raw.items():
        if not key.isdigit():
            skipped.append(key)
            continue
        out[int(key)] = int(value)
    return out, skipped


def analyse(json_map, export):
    defects = []          # (cls, itemId, name, jsonModel, serverIdfile, serverModel)
    for item_id in sorted(json_map):
        model = json_map[item_id]
        row = export.get(item_id)
        if row is None:
            defects.append(("not-on-server", item_id, "", model, "", ""))
            continue
        name, idfile, num = row
        if num is None:
            defects.append(("non-numeric-idfile", item_id, name, model, idfile, ""))
        elif num != model:
            defects.append(("mismatch", item_id, name, model, idfile, num))
    server_only = [i for i in sorted(export) if i not in json_map]
    return defects, server_only


def collisions(json_map, export):
    """A server MATERIAL (numeric idfile) that is also a JSON KEY: the peer path
    feeds equipment[] a material and the render sites look it up as an item id,
    so such a material is silently replaced by that JSON row's model."""
    hits = {}   # material -> (json model, count of server items carrying it)
    for item_id, (name, idfile, num) in export.items():
        if num is None:
            continue
        if num in json_map:
            m, c = hits.get(num, (json_map[num], 0))
            hits[num] = (m, c + 1)
    return hits


def cmd_diff(args, root):
    export, header = load_export(os.path.join(root, "data", "item_idfile.tsv"))
    json_map, skipped = load_json_map(os.path.join(root, "data", "item_models.json"))
    defects, server_only = analyse(json_map, export)
    hits = collisions(json_map, export)

    counts = {c: 0 for c in DEFECT_CLASSES}
    for cls, *_ in defects:
        counts[cls] += 1
    counts["server-only"] = len(server_only)

    out = os.path.abspath(args.out or os.path.join(review_root(root), "T498", "item_model_mismatches.tsv"))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as fh:
        for line in header:
            fh.write("#   export " + line[1:].strip() + "\n")
        fh.write("# json rows\t%d\n" % len(json_map))
        for cls in DEFECT_CLASSES:
            fh.write("# %s\t%d\n" % (cls, counts[cls]))
        fh.write("# collision materials\t%d\n" % len(hits))
        fh.write("# collision server items\t%d\n" % sum(c for _, c in hits.values()))
        fh.write("class\titem_id\tname\tjson_model\tserver_idfile\tserver_model\n")
        for cls, item_id, name, model, idfile, num in defects:
            fh.write("%s\t%d\t%s\t%s\t%s\t%s\n" % (cls, item_id, name, model, idfile, num))
        for item_id in server_only:
            name, idfile, num = export[item_id]
            fh.write("server-only\t%d\t%s\t\t%s\t%s\n" % (item_id, name, idfile, "" if num is None else num))

    census = os.path.join(os.path.dirname(out), "collision_census.tsv")
    with open(census, "w", encoding="utf-8") as fh:
        fh.write("# a server material that is also an item_models.json KEY:\n")
        fh.write("# the peer path puts the material in equipment[], the render sites look it\n")
        fh.write("# up as an item id, and the JSON row silently replaces it.\n")
        fh.write("material\tjson_model_drawn_instead\tserver_items_affected\n")
        for material in sorted(hits):
            model, count = hits[material]
            fh.write("%d\t%d\t%d\n" % (material, model, count))

    print("export items:        %d" % len(export))
    print("json rows:           %d  (non-numeric keys skipped: %d)" % (len(json_map), len(skipped)))
    for cls in DEFECT_CLASSES:
        print("%-20s %d" % (cls + ":", counts[cls]))
    print("collision materials: %d  (server items affected: %d, of which the JSON model differs: %d)"
          % (len(hits), sum(c for _, c in hits.values()),
             sum(c for m, (mod, c) in hits.items() if mod != m)))
    print("wrote %s" % out)
    print("wrote %s" % census)
    return 0


def baseline_path(root):
    return os.path.join(root, "data", "item_models_drift_baseline.txt")


def read_baseline(path):
    known = set()
    if not os.path.exists(path):
        return known
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            cls, item_id = line.split("\t")
            known.add((cls, int(item_id)))
    return known


BASELINE_HEADER = """\
# data/item_models_drift_baseline.txt -- T498 (board B755, B827).
#
# Every row of data/item_models.json that disagrees with the server export
# data/item_idfile.tsv, as measured at T498.  This is a RATCHET, not an
# approval: ItemModelsMatchServer fails when a row NOT listed here drifts, and
# equally when a row listed here has been fixed without being struck, so the
# list cannot outlive the defect.
#
# It exists because the fix is not a code decision.  1,226 of the 3,661 rows
# name a different model from the server and 902 name an item id the server
# does not have, so regenerating the file wholesale is a VISUAL change to every
# offline tool that draws from it -- the plan's own STOP condition, and Will's
# call.  The RUNTIME no longer depends on any of it: T498 task 3 feeds
# appearance.equipment[7|8] from the wire's own IDFile, the same rule the
# server uses, so this file now only serves tools/model_viewer and
# tools/ui_inventory_capture.
#
# Regenerate with:  python3 scripts/check_item_models.py --write-baseline
#
# class\titem_id
"""


def write_baseline(defects, path):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(BASELINE_HEADER)
        for cls, item_id, *_ in sorted(defects, key=lambda d: (d[0], d[1])):
            fh.write("%s\t%d\n" % (cls, item_id))


def cmd_check(args, root):
    export_path = os.path.join(root, "data", "item_idfile.tsv")
    json_path = os.path.join(root, "data", "item_models.json")
    if not os.path.exists(export_path):
        print("FAIL: missing %s (scripts/eqemu/export_item_idfile.sh)" % export_path)
        return 1
    export, _ = load_export(export_path)
    json_map, _ = load_json_map(json_path)
    defects, _ = analyse(json_map, export)

    if args.write_baseline:
        write_baseline(defects, baseline_path(root))
        print("wrote %s (%d rows)" % (baseline_path(root), len(defects)))
        return 0

    if args.strict:
        known = set()
    else:
        known = read_baseline(baseline_path(root))

    current = {(cls, item_id): (name, model, idfile) for cls, item_id, name, model, idfile, _ in defects}
    new = sorted(k for k in current if k not in known)
    fixed = sorted(known - set(current))

    if not new and not fixed:
        if known:
            print("OK: %d of %d item_models.json rows match the server's items.idfile; "
                  "the other %d are the T498 baseline (data/item_models_drift_baseline.txt)"
                  % (len(json_map) - len(defects), len(json_map), len(defects)))
        else:
            print("OK: all %d item_models.json rows match the server's items.idfile" % len(json_map))
        return 0

    print("FAIL: %d of %d item_models.json rows disagree with data/item_idfile.tsv"
          % (len(defects), len(json_map)))
    for want in args.highlight:
        for cls, item_id, name, model, idfile, num in defects:
            if item_id == want:
                print("  the boarded example -- %s: item %d (%s): json %s, server %s"
                      % (cls, item_id, name, model, idfile or "<absent>"))
    if new:
        print("  %d NEW defect(s) not in data/item_models_drift_baseline.txt:" % len(new))
        for cls, item_id in new[:args.max_report]:
            name, model, idfile = current[(cls, item_id)]
            print("    %s: item %d (%s): json %s, server %s"
                  % (cls, item_id, name, model, idfile or "<absent>"))
        if len(new) > args.max_report:
            print("    ... and %d more" % (len(new) - args.max_report))
    if fixed:
        print("  %d baseline row(s) are no longer defects -- strike them "
              "(--write-baseline):" % len(fixed))
        for cls, item_id in fixed[:args.max_report]:
            print("    %s: item %d" % (cls, item_id))
        if len(fixed) > args.max_report:
            print("    ... and %d more" % (len(fixed) - args.max_report))
    if args.strict:
        by_class = {}
        for d in defects:
            by_class.setdefault(d[0], []).append(d)
        for cls in DEFECT_CLASSES:
            rows = by_class.get(cls)
            if not rows:
                continue
            print("  %s: %d" % (cls, len(rows)))
            for _, item_id, name, model, idfile, num in rows[:args.max_report]:
                print("    item %d (%s): json %s, server %s"
                      % (item_id, name, model, idfile or "<absent>"))
            if len(rows) > args.max_report:
                print("    ... and %d more" % (len(rows) - args.max_report))
    return 1


def cmd_rebuild(args, root):
    export_path = os.path.join(root, "data", "item_idfile.tsv")
    json_path = os.path.join(root, "data", "item_models.json")
    export, header = load_export(export_path)
    old_map, _ = load_json_map(json_path)

    keep = set(old_map) if args.keep_coverage else set(export)
    new = {}
    dropped_no_row, dropped_non_numeric = [], []
    for item_id in sorted(keep):
        row = export.get(item_id)
        if row is None:
            dropped_no_row.append(item_id)
            continue
        if row[2] is None:
            dropped_non_numeric.append(item_id)
            continue
        new[str(item_id)] = row[2]

    src = dict(line[1:].strip().split("\t", 1) for line in header if "\t" in line)
    doc = {
        "_provenance": {
            "generator": "scripts/check_item_models.py --rebuild",
            "source": "data/item_idfile.tsv",
            "source_host": src.get("source_host", ""),
            "source_database": src.get("database", ""),
            "source_items_rows": src.get("items_rows", ""),
            "source_generated_utc": src.get("generated_utc", ""),
            "rule": "modelId = atoi(&items.idfile[2]), the server's Mob::GetEquipmentMaterial()",
            "coverage": "item ids that were already in this file" if args.keep_coverage
                        else "every server item with a numeric IT<n> idfile",
            "rows": len(new),
        }
    }
    doc.update(new)
    with open(json_path, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=1, sort_keys=False)
        fh.write("\n")
    changed = sum(1 for k, v in new.items() if int(k) in old_map and old_map[int(k)] != v)
    print("rebuilt %s: %d rows (was %d); %d values changed, %d added, %d dropped (%d not on server, %d non-numeric idfile)"
          % (json_path, len(new), len(old_map), changed,
             len(set(int(k) for k in new) - set(old_map)),
             len(dropped_no_row) + len(dropped_non_numeric),
             len(dropped_no_row), len(dropped_non_numeric)))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=None, help="repo root (default: this script's parent)")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--diff", action="store_true")
    ap.add_argument("--rebuild", action="store_true")
    ap.add_argument("--keep-coverage", action="store_true",
                    help="--rebuild: keep exactly the item ids the file already had")
    ap.add_argument("--out", default=None)
    ap.add_argument("--max-report", type=int, default=25)
    ap.add_argument("--highlight", type=int, action="append", default=[],
                    help="--check: always name this item id in the report (B755's 2894)")
    ap.add_argument("--strict", action="store_true",
                    help="--check: ignore the baseline; every disagreement is a failure")
    ap.add_argument("--write-baseline", action="store_true",
                    help="--check: rewrite data/item_models_drift_baseline.txt from the tree")
    args = ap.parse_args()
    root = repo_root(args.repo)
    if args.diff:
        return cmd_diff(args, root)
    if args.rebuild:
        return cmd_rebuild(args, root)
    if args.check:
        return cmd_check(args, root)
    ap.error("one of --check / --diff / --rebuild is required")


if __name__ == "__main__":
    sys.exit(main())
