#!/usr/bin/env python3
"""B1042 (#362) -- pin the CI queue savings into .gitlab-ci.yml.

Reads the pipeline file as TEXT (no PyYAML on the runners; `!reference` is a
GitLab-only tag) and asserts:

  1. ccache launcher: `.goldfarm` sets it up only behind `command -v ccache`;
     CCACHE_BASEDIR is a pipeline variable; every x86 `cmake -S` configure in
     the file passes $CCACHE_CMAKE_ARGS; every `cmake --build` in `.build_tree`
     and `.visual_gate_prepare` is followed by a `ccache_report` line;
     scripts/build-gles2-x86.sh takes the launcher env vars.
  2. docs-only pipelines: `hygiene` has NO rules (always runs); the C++ jobs
     (build-test, gate, and build-headless when present) take
     `.code_rules`; client-ledger-live carries the same regexp; `.code_rules`
     has the regexp on the MR, fable and branch rules, and compare_to fable
     ONLY on the generic branch rule (on fable it would compare fable with
     itself and skip every fable build).
  3. the regexp's meaning: Python's `re` with `\\z` spelled `\\Z` (Ruby's
     `\\z` is Python's `\\Z`) -- an `*.md` path does not match, anything else
     does. GitLab adds a job when ANY changed path matches.
  4. build+test are one job: no top-level `build:` or `test:` job, no `test`
     stage, and client-ledger-live needs `build-test`.

A self-test then breaks each rule group and requires a failure.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CI = ROOT / ".gitlab-ci.yml"
GLES2 = ROOT / "scripts" / "build-gles2-x86.sh"


def job_body(text, name):
    m = re.search(rf"^{re.escape(name)}:\s*$", text, re.M)
    if not m:
        return None
    rest = text[m.end():]
    nxt = re.search(r"^[^\s#][^:\n]*:\s*$", rest, re.M)
    return [l for l in (rest[: nxt.start()] if nxt else rest).splitlines() if not l.strip().startswith("#")]


def check(text, gles2_text):
    fails = []

    def expect(cond, msg):
        if not cond:
            fails.append(msg)

    # 1. ccache
    gold = job_body(text, ".goldfarm") or []
    gtext = "\n".join(gold)
    expect("command -v ccache" in gtext, ".goldfarm must guard the launcher with `command -v ccache`")
    expect("CCACHE_CMAKE_ARGS=" in gtext and "ccache_report()" in gtext,
           ".goldfarm must define CCACHE_CMAKE_ARGS and ccache_report")
    expect(re.search(r"^\s+CCACHE_BASEDIR: \$CI_PROJECT_DIR\s*$", text, re.M) is not None,
           "variables must set CCACHE_BASEDIR: $CI_PROJECT_DIR")
    confs = [l for l in text.splitlines() if re.search(r"\bcmake -S \.", l) and not l.strip().startswith("#")]
    expect(len(confs) >= 3, f"expected the hygiene, .build_tree and gate configures, found {len(confs)}")
    for l in confs:
        expect("$CCACHE_CMAKE_ARGS" in l, f"configure without $CCACHE_CMAKE_ARGS: {l.strip()[:90]}")
    for anchor in (".build_tree", ".visual_gate_prepare"):
        body = job_body(text, anchor) or []
        builds = [i for i, l in enumerate(body) if "cmake --build" in l]
        reports = [i for i, l in enumerate(body) if "ccache_report" in l]
        expect(bool(builds) and bool(reports) and reports[-1] > builds[-1],
               f"{anchor}: a ccache_report line must follow its last `cmake --build`")
    expect("CMAKE_CXX_COMPILER_LAUNCHER" in gles2_text and "IRR_CXX" in gles2_text,
           "scripts/build-gles2-x86.sh must honour CMAKE_CXX_COMPILER_LAUNCHER (configure + Irrlicht make)")

    # 2. rules
    hyg = job_body(text, "hygiene") or []
    expect(not any(l.strip().startswith("rules:") for l in hyg), "`hygiene` must have no rules (it always runs)")
    code = job_body(text, ".code_rules") or []
    ctext = "\n".join(code)
    regexps = re.findall(r"regexp: (?:&\w+ )?'([^']*)'", ctext)
    expect(len(regexps) == 1, f".code_rules must spell the docs-only regexp once (anchored), found {regexps}")
    ifs = [l.strip() for l in code if l.strip().startswith("- if:")]
    expect('- if: $CI_PIPELINE_SOURCE == "merge_request_event"' in ifs, ".code_rules needs the MR rule")
    expect('- if: $CI_COMMIT_BRANCH == "fable"' in ifs, ".code_rules needs a fable rule")
    expect("- if: $CI_COMMIT_BRANCH" in ifs, ".code_rules needs the generic branch rule")
    # compare_to only inside the generic branch rule, never inside the fable one
    rules = re.split(r"\n\s+- if:", "\n" + ctext)
    for r in rules:
        head = r.strip().splitlines()[0] if r.strip() else ""
        if head.startswith('$CI_COMMIT_BRANCH == "fable"'):
            expect("compare_to" not in r, ".code_rules: the fable rule must not use compare_to (fable vs fable = no change)")
        elif head == "$CI_COMMIT_BRANCH":
            expect("compare_to: refs/heads/fable" in r, ".code_rules: the branch rule must compare_to refs/heads/fable")
    jobs = ["build-test", "gate"] + (["build-headless"] if job_body(text, "build-headless") is not None else [])
    for j in jobs:
        body = job_body(text, j)
        expect(body is not None and any("rules: !reference [.code_rules, rules]" in l for l in body),
               f"`{j}` must take rules: !reference [.code_rules, rules]")
    cll = "\n".join(job_body(text, "client-ledger-live") or [])
    expect(cll.count("regexp: *code_change_re") >= 3, "client-ledger-live must carry the docs-only regexp on each rule")

    # 3. regexp meaning
    if regexps:
        rx = re.compile(regexps[0].replace("\\z", "\\Z"))
        for path, want in (("docs/ci.md", False), ("README.md", False), ("refactor/B1042/README.md", False),
                           ("src/client/core/everquest.cpp", True), (".gitlab-ci.yml", True),
                           ("refactor/B1042/timings.tsv", True), ("scripts/ci/x.mdx", True), ("x.cmd", True)):
            expect(bool(rx.search(path)) == want, f"docs-only regexp: {path} should {'' if want else 'not '}match")

    # 4. one build-test job
    expect(job_body(text, "build") is None, "a top-level `build:` job is back (B1042 folded it into build-test)")
    expect(job_body(text, "test") is None, "a top-level `test:` job is back (B1042 folded it into build-test)")
    stages = re.search(r"^stages:\n((?:\s+.*\n)+?)\S", text, re.M)
    expect(stages is not None and not re.search(r"^\s+- test\s*$", stages.group(1), re.M), "the `test` stage is back")
    expect("needs: [build-test]" in cll, "client-ledger-live must need build-test")
    return fails


def main():
    text = CI.read_text()
    gles2 = GLES2.read_text()
    fails = check(text, gles2)

    breaks = {
        "no ccache guard": (text.replace("command -v ccache", "true"), gles2),
        "configure without launcher": (text.replace(" $CCACHE_CMAKE_ARGS\n", "\n", 1), gles2),
        "hygiene with rules": (text.replace("hygiene:\n  extends: .goldfarm\n",
                                            "hygiene:\n  extends: .goldfarm\n  rules: !reference [.code_rules, rules]\n"), gles2),
        "gate without rules": (text.replace("  rules: !reference [.code_rules, rules]\n  script:\n    - !reference [.visual_gate_prepare",
                                            "  script:\n    - !reference [.visual_gate_prepare"), gles2),
        "regexp that matches md": (text.replace("(?!.*\\.md\\z)", "(?!.*\\.txt\\z)"), gles2),
        "test job back": (text + "\ntest:\n  stage: build\n", gles2),
        "gles2 script without launcher": (text, gles2.replace("IRR_CXX", "IRR_X")),
    }
    for what, (t, g) in breaks.items():
        if not check(t, g):
            fails.append(f"self-test: '{what}' still passes")

    for f in fails:
        print("FAIL:", f)
    if fails:
        return 1
    print("OK: ccache launcher guarded on every configure + reported after every build; "
          "docs-only diffs skip build-test/gate/client-ledger-live; hygiene always runs; build+test are one job")
    return 0


if __name__ == "__main__":
    sys.exit(main())
