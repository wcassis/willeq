#!/usr/bin/env python3
"""B1202 (#521): the card manifest -- parser, validator and derivation rules.

The vocabulary is specified in docs/hermes/card_manifest.md; this module is its
single implementation (lint_manifest.py and the B1204 allocator import it).
python3 stdlib only.

A manifest is a `## Manifest` block in a card body / issue description:

    ## Manifest
    leg: fix
    tier: heavy
    needs: thinkpad-render:2, hermes-measure:1, cpu-build
    model: any
    phases: code:strong, build:none, render:none, measure:none, doc:cheap
    est: turns=62, wall_min=42

and the matching GitLab labels (leg::fix, tier::heavy, model::any,
need:thinkpad-render, need:hermes-measure, need:cpu-build).
"""
import re

LEGS = ("diagnose", "fix", "review", "infra", "tool")
TIERS = ("light", "medium", "heavy")
MODELS = ("any", "strong", "reasoning")
PHASES = ("trace", "code", "build", "render", "measure", "leg", "doc", "review")
QUALITIES = ("none", "cheap", "any", "strong", "reasoning")
# need name -> takes a count (N) / a lane id / nothing
NEEDS = {
    "thinkpad-render": "count",   # N concurrent model_viewer renders on the ThinkPad
    "thinkpad-quiet": "flag",     # the ThinkPad with nothing else rendering (timing legs)
    "lane": "lane",               # lane:5 (the ThinkPad oracle + eqemu) / lane:1..4 / lane:any
    "arm-build": "flag",          # an ARM cross/native build slot
    "cpu-build": "flag",          # an rbuild slot (ThinkPad or runner VM)
    "oracle-vm": "flag",          # an r430/r540 Wine oracle VM
    "hermes-measure": "count",    # N cv2 measure processes on hermes-ext
}
FIELDS = ("leg", "tier", "needs", "model", "phases", "est")
# STEP 1 ledger medians (refactor/B1201/tables/by_leg_type.tsv): leg -> (turns, wall minutes)
EST_DEFAULTS = {
    "review": (72, 41),     # ledger leg types review (72 turns, 2431 s)
    "fix": (62, 42),        # ledger fix (62 turns, 2504 s)
    "diagnose": (262, 192), # ledger diagnose (262 turns, 11520 s)
    "infra": (295, 148),    # ledger dev (295 turns, 8873 s)
    "tool": (295, 148),     # ledger dev
}
HEAVY_NEEDS = ("thinkpad-render", "thinkpad-quiet", "lane", "oracle-vm")
TIER_RANK = {t: i for i, t in enumerate(TIERS)}
MODEL_RANK = {"any": 0, "strong": 1, "reasoning": 2}
QUALITY_TO_MODEL = {"none": "any", "cheap": "any", "any": "any", "strong": "strong",
                    "reasoning": "reasoning"}

BLOCK_RE = re.compile(r"(?ms)^##\s+Manifest\s*$\n(.*?)(?=^#{1,2}\s|\Z)")
LINE_RE = re.compile(r"^\s*[-*]?\s*`?([a-z_]+)`?\s*:\s*(.*?)\s*`?\s*$")


def find_block(text):
    """The raw `key: value` lines of the first `## Manifest` block, or None."""
    m = BLOCK_RE.search(text or "")
    if not m:
        return None
    out = {}
    for line in m.group(1).splitlines():
        line = line.strip().strip("`")
        if not line or line.startswith("```"):
            continue
        lm = LINE_RE.match(line)
        if lm and lm.group(1) in FIELDS:
            out[lm.group(1)] = lm.group(2)
    return out


def parse_needs(s, errors):
    needs = {}
    s = (s or "").strip()
    if s.lower() in ("none", "-", ""):
        return needs
    for tok in [t.strip() for t in s.split(",") if t.strip()]:
        name, _, arg = tok.partition(":")
        kind = NEEDS.get(name)
        if kind is None:
            errors.append("needs: unknown need %r (valid: %s)" % (name, ", ".join(sorted(NEEDS))))
            continue
        if kind == "count":
            if not arg:
                arg = "1"
            if not arg.isdigit() or int(arg) < 1:
                errors.append("needs: %s takes a positive count, got %r" % (name, arg))
                continue
            needs[name] = int(arg)
        elif kind == "lane":
            if arg not in ("1", "2", "3", "4", "5", "any"):
                errors.append("needs: lane takes 1..5 or any, got %r" % arg)
                continue
            needs["lane"] = arg
        else:
            if arg:
                errors.append("needs: %s takes no argument, got %r" % (name, arg))
                continue
            needs[name] = 1
    return needs


def parse_phases(s, errors):
    phases = []
    for tok in [t.strip() for t in (s or "").split(",") if t.strip()]:
        name, _, q = tok.partition(":")
        if name not in PHASES:
            errors.append("phases: unknown phase %r (valid: %s)" % (name, ", ".join(PHASES)))
            continue
        if q not in QUALITIES:
            errors.append("phases: %s needs a quality from %s, got %r" % (name, "|".join(QUALITIES), q))
            continue
        phases.append((name, q))
    if not phases and not errors:
        errors.append("phases: empty (at least one phase:quality)")
    return phases


def parse_est(s, errors):
    est = {}
    for tok in [t.strip() for t in (s or "").split(",") if t.strip()]:
        k, _, v = tok.partition("=")
        if k not in ("turns", "wall_min") or not v.isdigit():
            errors.append("est: expected turns=<int>, wall_min=<int>, got %r" % tok)
            continue
        est[k] = int(v)
    for k in ("turns", "wall_min"):
        if k not in est and not any(e.startswith("est:") for e in errors):
            errors.append("est: missing %s" % k)
    return est


def parse(text):
    """-> (manifest dict or None, errors list). A manifest has every FIELD."""
    raw = find_block(text)
    if raw is None:
        return None, ["no `## Manifest` block"]
    errors = []
    for f in FIELDS:
        if f not in raw or raw[f] == "":
            errors.append("%s: missing" % f)
    m = {"leg": raw.get("leg", ""), "tier": raw.get("tier", ""), "model": raw.get("model", "")}
    if m["leg"] and m["leg"] not in LEGS:
        errors.append("leg: %r not in %s" % (m["leg"], "|".join(LEGS)))
    if m["tier"] and m["tier"] not in TIERS:
        errors.append("tier: %r not in %s" % (m["tier"], "|".join(TIERS)))
    if m["model"] and m["model"] not in MODELS:
        errors.append("model: %r not in %s" % (m["model"], "|".join(MODELS)))
    m["needs"] = parse_needs(raw.get("needs"), errors) if "needs" in raw else {}
    m["phases"] = parse_phases(raw.get("phases"), errors) if raw.get("phases") else []
    m["est"] = parse_est(raw.get("est"), errors) if raw.get("est") else {}
    return m, errors


def derived_tier(m):
    """The minimum tier the rules allow (docs/hermes/card_manifest.md, Derivation)."""
    if any(n in m["needs"] for n in HEAVY_NEEDS):
        return "heavy"
    if m["leg"] == "review":
        return "light"
    if m["leg"] == "fix" and all(p in ("doc", "review", "trace") for p, _ in m["phases"]):
        return "light"   # a fix that only edits documents (refactor/, docs/)
    return "medium"


def derived_model(m):
    """The minimum model class the phases ask for (max over phase qualities)."""
    best = "any"
    for _, q in m["phases"]:
        cand = QUALITY_TO_MODEL[q]
        if MODEL_RANK[cand] > MODEL_RANK[best]:
            best = cand
    return best


def consistency(m):
    """Rule violations of a parsed manifest (declared below what the rules derive)."""
    errs = []
    if m["tier"] in TIER_RANK and TIER_RANK[m["tier"]] < TIER_RANK[derived_tier(m)]:
        errs.append("tier: declared %s but the rules derive %s (needs %s, leg %s)"
                    % (m["tier"], derived_tier(m), ",".join(sorted(m["needs"])) or "none", m["leg"]))
    if m["model"] in MODEL_RANK and MODEL_RANK[m["model"]] < MODEL_RANK[derived_model(m)]:
        errs.append("model: declared %s but a phase asks for %s" % (m["model"], derived_model(m)))
    if "thinkpad-quiet" in m["needs"] and "thinkpad-render" not in m["needs"]:
        errs.append("needs: thinkpad-quiet without thinkpad-render:N (a quiet ThinkPad is for a timed render)")
    return errs


def labels_for(m):
    """The GitLab labels a manifest implies."""
    out = ["leg::" + m["leg"], "tier::" + m["tier"], "model::" + m["model"]]
    for n, v in sorted(m["needs"].items()):
        out.append("need:lane-%s" % v if n == "lane" else "need:" + n)
    return out


def label_mismatch(m, labels):
    """Differences between a manifest and an issue's label list (manifest-relevant labels only)."""
    want = set(labels_for(m))
    have = {l for l in labels if l.startswith(("leg::", "tier::", "model::", "need:"))}
    errs = []
    for l in sorted(want - have):
        errs.append("labels: missing %s" % l)
    for l in sorted(have - want):
        errs.append("labels: %s is not in the manifest block" % l)
    return errs


def from_labels(labels):
    """A partial manifest from labels alone (no phases/est): what the allocator falls back to."""
    m = {"leg": "", "tier": "", "model": "", "needs": {}, "phases": [], "est": {}}
    for l in labels:
        for k in ("leg", "tier", "model"):
            if l.startswith(k + "::"):
                m[k] = l.split("::", 1)[1]
        if l.startswith("need:lane-"):
            m["needs"]["lane"] = l[len("need:lane-"):]
        elif l.startswith("need:"):
            m["needs"][l[5:]] = 1
    return m
