#!/usr/bin/env python3
"""
B824 (issue #86, kanban t_20a88e62) -- the deleted item-to-model JSON map,
ratcheted.

The defect: EquipmentModelLoader parsed all 3,661 rows of
data/item_models.json into the in-memory `itemToModelMap_` on every startup
(client via EntityRenderer::loadEquipmentModels(), tools/model_viewer via
model_viewer_startup.cpp, tools/hwcaps/ui_inventory_capture, and the
loading-thread handoff through adoptIndex()'s third argument) -- and read
it back from NOTHING. The only lookup, getModelIdForItem(), queries
WilleqDatabase::getItemModel() (the item_models table, which willeq.db is
built from that JSON; see tools/willeq_db/db_loaders_json.cpp). T498 had
measured this and stopped the loader from THROWING on a non-digit key; this
unit (the orchestrator's B824 ruling: the db table is the live lookup, the
in-memory JSON map goes) deleted the map and its plumbing:

  * EquipmentModelLoader::itemToModelMap_        (member)
  * EquipmentModelLoader::loadItemModelMapping   (the JSON parse)
  * EquipmentModelLoader::loadItemModelMappingStatic (B491: zero callers)
  * EquipmentModelLoader::getMappingCount        (B491: zero callers)
  * MemoryStats::mappingCount + the 16 B/entry indexBytes term
  * adoptIndex()'s third (map) argument; the caller passed an empty map
  * IrrlichtRenderer::itemToModelMap_ -- a second copy of the map the
    WriteOnlyFields census itself had missed: zero references tree-wide.

This is the ratchet half of the pin (the GREEN/GREEN half). It fails on
two things, exactly like check_write_only_fields.py / check_zero_callers.py
(B840's method):

  1. DELETED -- any identifier of the map reappearing in the tree;
  2. DB-ONLY -- getModelIdForItem() no longer querying
     WilleqDatabase::getItemModel() (if the lookup stopped reading the
     database, the map's deletion has no surviving source of truth).

Pure source scan: no build artefact, no server, no display. Wired as the
`ItemModelMapDeletion` hygiene ctest test (tests/cmake/hygiene.cmake); the
C++ half of the pin is tests/test_item_model_mapping.cpp.

Scanned directories: src/ include/ tools/ benchmarks/ docker/ -- the
tree-wide rule (board B166: a census that skips tools/ is not tree-wide).
tests/ is deliberately NOT swept: test_item_model_mapping.cpp's header
comment names the deleted identifiers so future readers see what went, and
that file's C++ assertions already fail if the map comes back in the
loader itself.
"""

import argparse
import os
import re
import sys

EXTS = (".h", ".hpp", ".cpp", ".cc", ".inl")
SWEEP_DIRS = ("src", "include", "tools", "benchmarks", "docker")

# The deleted map. Each pattern is an identifier (no word boundary needed:
# the names are distinctive enough that a boundary would only hide a
# longer-name reuse of the same concept, which is exactly what should fail).
#   itemToModelMap   the member, both copies (EquipmentModelLoader's and
#                    the never-referenced IrrlichtRenderer's)
#   loadItemModelMapping  the JSON parse and its Static variant
#   MappingCount     getMappingCount() and MemoryStats::mappingCount
DELETED_PATTERNS = [
    re.compile(r"itemToModelMap"),
    re.compile(r"loadItemModelMapping"),
    re.compile(r"MappingCount"),
]

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")
STRINGS = re.compile(r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'")


def strip_noncode(text):
    """Blank out comments and string literals, preserving line numbers."""
    text = BLOCK_COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = LINE_COMMENT.sub("", text)
    text = STRINGS.sub('""', text)
    return text


def walk(root, dirs):
    for d in dirs:
        base = os.path.join(root, d)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [x for x in dirnames
                           if x not in ("third_party", "build", ".git")
                           and not x.startswith("build")]
            for fn in sorted(filenames):
                if fn.endswith(EXTS):
                    yield os.path.join(dirpath, fn)


def check(root):
    failures = []

    # Half 1: the deleted map must stay deleted.
    for path in walk(root, SWEEP_DIRS):
        with open(path, encoding="utf-8", errors="replace") as f:
            code = strip_noncode(f.read())
        lines = code.split("\n")
        for i, line in enumerate(lines, 1):
            for pat in DELETED_PATTERNS:
                if pat.search(line):
                    failures.append(
                        "DELETED-REAPPEARED %s:%d: %s"
                        % (os.path.relpath(path, root), i, line.strip()))

    # Half 2: the surviving lookup must still query the database. The
    # definition is in src/client/graphics/eq/equipment_model_loader.cpp;
    # if it stops calling WilleqDatabase::getItemModel(), the deleted map's
    # job has no source of truth left and something else has to be answering
    # -- which is exactly the defect this unit closed.
    loader = os.path.join(root, "src", "client", "graphics", "eq",
                          "equipment_model_loader.cpp")
    if not os.path.isfile(loader):
        failures.append("DB-ONLY: %s is missing" % os.path.relpath(loader, root))
    else:
        with open(loader, encoding="utf-8", errors="replace") as f:
            code = strip_noncode(f.read())
        m = re.search(r"int EquipmentModelLoader::getModelIdForItem\([^;]*?\)\s*const\s*\{",
                      code)
        if not m:
            failures.append(
                "DB-ONLY: EquipmentModelLoader::getModelIdForItem() definition not found")
        else:
            body = code[m.end():m.end() + 400]
            if "getItemModel(" not in body:
                failures.append(
                    "DB-ONLY: getModelIdForItem() no longer queries "
                    "WilleqDatabase::getItemModel() -- the item->model "
                    "lookup lost its (only) source of truth")

    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".", help="repository root")
    args = ap.parse_args()

    failures = check(args.repo)
    if failures:
        for f in failures:
            print(f)
        print("ItemModelMapDeletion: %d failure(s). The B824 deletion "
              "(db-only item->model lookup) has regressed." % len(failures))
        sys.exit(1)
    print("ItemModelMapDeletion: the B824 item-to-model JSON map stays "
          "deleted; getModelIdForItem() stays db-only.")


if __name__ == "__main__":
    main()
