#!/usr/bin/env python3
"""B1153 (#473) census: every key the six spellOverrides carry at this tip,
and the top-level key census of config/spell_effects.json (post-retirement).

usage: census_keys.py <config/spell_effects.json>
"""
import json
import sys

cfg = json.load(open(sys.argv[1]))
print("top-level keys:", ", ".join(cfg.keys()))
ov = cfg.get("spellOverrides", {})
for k in sorted(ov, key=int):
    sub = ov[k]
    print(f"override {k}: " + ", ".join(sub.keys()))
    for stage, words in sub.items():
        if isinstance(words, dict):
            print(f"  {stage}: " + ", ".join(words.keys()))
