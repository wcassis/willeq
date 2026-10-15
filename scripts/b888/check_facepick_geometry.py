#!/usr/bin/env python3
"""Headless FacePick XML/layout parity check (no graphics binary required)."""

import argparse
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--xml", type=Path, required=True)
    args = parser.parse_args()
    header = (args.source / "include/client/graphics/ui/face_pick_layout.h").read_text()
    assert "kWindowW = 175, kWindowH = 215" in header
    assert "kXmlX = 0, kXmlY = 80" in header
    assert "kClientX = 4, kClientY = 16" in header
    assert "kRows = 7" in header
    expressions = {
        "downRect": "15, 4 + 20 * row, 22, 12",
        "textRect": "row == 5 ? 40 : 45, 2 + 20 * row,",
        "upRect": "133, 4 + 20 * row, 22, 12",
        "randomRect": "50, 144, 64, 20",
        "acceptRect": "15, 170, 64, 20",
        "cancelRect": "89, 170, 64, 20",
    }
    for name, expression in expressions.items():
        assert expression in header, f"{name} layout drift"
    root = ET.parse(args.xml).getroot()
    by_name = {element.get("item"): element for element in root if element.get("item")}

    def check(name, rect, label=None):
        element = by_name[name]
        actual = tuple(int(element.findtext(path)) for path in
                       ("Location/X", "Location/Y", "Size/CX", "Size/CY"))
        assert actual == rect, f"{name}: {actual} != {rect}"
        if label is not None:
            assert element.findtext("Text") == label, name

    check("FacePickWindow", (0, 80, 175, 215), "Choose Your Appearance")
    names = ("Face", "Hair", "Hair_Color", "Eyes", "Beard", "Beard_Color", "Wode")
    labels = ("FACE", "HAIR", "HAIR COLOR", "EYES", "BEARD", "BEARD COLOR", "WOAD")
    for row, (name, label) in enumerate(zip(names, labels)):
        stem = "FPW_" + name
        check(stem + "_Down_Button", (15, 4 + 20 * row, 22, 12))
        check(stem + "_Text", (40 if row == 5 else 45, 2 + 20 * row,
                               90 if row == 5 else 80, 16), label)
        check(stem + "_Up_Button", (133, 4 + 20 * row, 22, 12))
    for name, rect, label in (("Random", (50, 144, 64, 20), "Random"),
                              ("Accept", (15, 170, 64, 20), "Accept"),
                              ("Cancel", (89, 170, 64, 20), "Cancel")):
        check("FPW_" + name + "_Button", rect, label)
    controls = [name for name in by_name if re.fullmatch(r"FPW_.*(?:Button|Text)", name)]
    assert len(controls) == 24, f"unexpected control count: {len(controls)}"
    print("FacePick XML geometry: 25 rectangles and labels match source")


if __name__ == "__main__":
    main()
