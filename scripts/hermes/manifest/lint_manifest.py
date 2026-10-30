#!/usr/bin/env python3
"""B1202 (#521): lint_manifest.py -- check a card body or a GitLab issue for a
complete, valid card manifest (docs/hermes/card_manifest.md).

  python3 -I scripts/hermes/manifest/lint_manifest.py <body.md>        # a card body file
  python3 -I scripts/hermes/manifest/lint_manifest.py -                # body on stdin
  python3 -I scripts/hermes/manifest/lint_manifest.py --issue 521      # a GitLab issue: block AND labels
  python3 -I scripts/hermes/manifest/lint_manifest.py --self-test      # fixture tests, no network

Prints `MANIFEST OK <source> <labels>` (exit 0) or `MANIFEST BAD <source>` followed by
one line per missing/invalid field (exit 1). Read-only; `--issue` runs
`glab issue view <N> -F json -R <repo>` (glab on PATH, or $GLAB).
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import manifest as M  # noqa: E402

REPO = os.environ.get("WILLEQ_REPO", "gitlab.goldfarm.local/willeq/willeq")


def lint_text(text, labels=None):
    """-> (manifest or None, list of problems). labels=None skips the label check."""
    m, errs = M.parse(text)
    if m is not None and not errs:
        errs += M.consistency(m)
    if m is not None and labels is not None and not errs:
        errs += M.label_mismatch(m, labels)
    return m, errs


def issue(n):
    glab = os.environ.get("GLAB", "glab")
    out = subprocess.run([glab, "issue", "view", str(n), "-F", "json", "-R", REPO],
                         capture_output=True, text=True, env=dict(os.environ, GITLAB_HOST=REPO.split("/")[0]))
    if out.returncode != 0:
        raise SystemExit("MANIFEST BAD issue#%s\n  glab failed: %s" % (n, out.stderr.strip()[:200]))
    d = json.loads(out.stdout)
    return d.get("description") or "", d.get("labels") or []


def report(source, m, errs):
    if errs:
        print("MANIFEST BAD %s" % source)
        for e in errs:
            print("  " + e)
        return 1
    print("MANIFEST OK %s %s" % (source, " ".join(M.labels_for(m))))
    return 0


GOOD = """# B9999: x
## Manifest
leg: fix
tier: heavy
needs: thinkpad-render:2, hermes-measure:2, cpu-build
model: strong
phases: code:strong, build:none, render:none, measure:none, doc:cheap
est: turns=62, wall_min=42

## Asks
"""


def self_test():
    cases = [
        ("complete", GOOD, None, []),
        ("no block", "# x\n## Asks\n", None, ["no `## Manifest` block"]),
        ("missing est", GOOD.replace("est: turns=62, wall_min=42\n", ""), None, ["est: missing"]),
        ("bad need", GOOD.replace("cpu-build", "gpu-build"), None, ["needs: unknown need 'gpu-build'"]),
        ("tier under rules", GOOD.replace("tier: heavy", "tier: medium"), None,
         ["tier: declared medium but the rules derive heavy"]),
        ("model under phases", GOOD.replace("model: strong", "model: any"), None,
         ["model: declared any but a phase asks for strong"]),
        ("light review", "## Manifest\nleg: review\ntier: light\nneeds: none\nmodel: any\n"
         "phases: review:any\nest: turns=72, wall_min=41\n", None, []),
        ("doc-only fix may be light", "## Manifest\nleg: fix\ntier: light\nneeds: none\nmodel: any\n"
         "phases: doc:cheap, review:any\nest: turns=62, wall_min=42\n", None, []),
        ("code fix is medium", "## Manifest\nleg: fix\ntier: light\nneeds: cpu-build\nmodel: strong\n"
         "phases: code:strong, build:none\nest: turns=62, wall_min=42\n", None,
         ["tier: declared light but the rules derive medium"]),
        ("lane id", GOOD.replace("cpu-build", "lane:7"), None, ["needs: lane takes 1..5 or any"]),
        ("labels agree", GOOD, ["class::B", "leg::fix", "tier::heavy", "model::strong", "need:thinkpad-render",
                                "need:hermes-measure", "need:cpu-build"], []),
        ("labels missing", GOOD, ["leg::fix", "tier::heavy"], ["labels: missing model::strong"]),
        ("label extra", GOOD, ["leg::fix", "tier::heavy", "model::strong", "need:thinkpad-render",
                               "need:hermes-measure", "need:cpu-build", "need:lane-5"],
         ["labels: need:lane-5 is not in the manifest block"]),
        ("bulleted + backticks", "## Manifest\n- `leg: review`\n- `tier: light`\n- `needs: none`\n"
         "- `model: any`\n- `phases: review:any`\n- `est: turns=50, wall_min=23`\n", None, []),
    ]
    fails = 0
    for name, text, labels, want in cases:
        _, errs = lint_text(text, labels)
        ok = (not want and not errs) or (want and all(any(e.startswith(w) for e in errs) for w in want))
        print("%s %s%s" % ("ok  " if ok else "FAIL", name, "" if ok else "  got %r want %r" % (errs, want)))
        fails += not ok
    m, _ = M.parse(GOOD)
    lab = M.from_labels(M.labels_for(m))
    ok = lab["needs"] == {"thinkpad-render": 1, "hermes-measure": 1, "cpu-build": 1} and lab["leg"] == "fix"
    print("%s from_labels round trip" % ("ok  " if ok else "FAIL"))
    fails += not ok
    print("SELF-TEST %s (%d cases)" % ("PASS" if not fails else "FAIL", len(cases) + 1))
    return 1 if fails else 0


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    if argv[0] == "--self-test":
        return self_test()
    if argv[0] == "--issue":
        desc, labels = issue(argv[1])
        m, errs = lint_text(desc, labels)
        return report("issue#%s" % argv[1], m, errs)
    text = sys.stdin.read() if argv[0] == "-" else open(argv[0]).read()
    m, errs = lint_text(text)
    return report(argv[0], m, errs)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
