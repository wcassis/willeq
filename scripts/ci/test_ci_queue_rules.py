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
  5. B1208 (#527) rbuild pipelines: `.not_rbuild` is `if: $RBUILD_TARGET` +
     `when: never`; EVERY job but `rbuild` (this file and the B1194 include)
     starts its rules with it, directly or through `.code_rules`; `hygiene`'s
     rules are exactly that reference + an unconditional `when: on_success`;
     the `rbuild` job runs only on an RBUILD_TARGET push off `fable`, keeps
     its artifacts 2 days and runs ctest only when RBUILD_TEST is set; the
     workflow admits the RBUILD_TARGET push ABOVE the open-MR suppression and
     drops the MR pipeline of an `rbuild: ci build` commit.

A self-test then breaks each rule group and requires a failure.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CI = ROOT / ".gitlab-ci.yml"
GLES2 = ROOT / "scripts" / "build-gles2-x86.sh"
B1194_INCLUDE = ROOT / "refactor" / "B1194" / "ci" / "b1194.yml"
RESERVED = {"stages", "variables", "workflow", "include", "default"}
NOT_RBUILD = "- !reference [.not_rbuild, rules]"


def job_body(text, name):
    m = re.search(rf"^{re.escape(name)}:\s*$", text, re.M)
    if not m:
        return None
    rest = text[m.end():]
    nxt = re.search(r"^[^\s#][^:\n]*:\s*$", rest, re.M)
    return [l for l in (rest[: nxt.start()] if nxt else rest).splitlines() if not l.strip().startswith("#")]


def check(text, gles2_text, include_text=""):
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

    # 2. rules (B1208: hygiene's only rule beyond "always" is the rbuild exclusion)
    hyg = [l.strip() for l in (job_body(text, "hygiene") or [])]
    expect(not any(l.startswith("rules: ") for l in hyg), "`hygiene` must not take another job's rules (it always runs)")
    if "rules:" in hyg:
        i = hyg.index("rules:")
        rules = hyg[i + 1:i + 3]
        tail = hyg[i + 3] if len(hyg) > i + 3 else ""
        expect(rules == [NOT_RBUILD, "- when: on_success"] and not tail.startswith("-"),
               "`hygiene` must always run: its rules are exactly the .not_rbuild reference + `- when: on_success`")
    else:
        expect(not any(l.startswith("rules:") for l in hyg), "`hygiene` must always run")
        expect(False, "`hygiene` must carry the .not_rbuild reference (B1208)")
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
    # 5. B1208 rbuild pipelines
    nr = "\n".join(l.strip() for l in (job_body(text, ".not_rbuild") or []))
    expect("- if: $RBUILD_TARGET\nwhen: never" in nr, "`.not_rbuild` must be `- if: $RBUILD_TARGET` + `when: never`")
    expect([l.strip() for l in code if l.strip().startswith("- ")][:1] == [NOT_RBUILD],
           "`.code_rules` must start with the .not_rbuild reference")
    for src, t in (("CI", text), ("B1194 include", include_text)):
        for name in re.findall(r"^([A-Za-z0-9][\w-]*):\s*$", t, re.M):
            if name in RESERVED or name == "rbuild":
                continue
            body = [l.strip() for l in (job_body(t, name) or [])]
            if "rules: !reference [.code_rules, rules]" in body:
                continue
            ext = [l for l in body if l.startswith("extends:")]
            if ext and ".b1194_" in ext[0]:
                continue  # inherits .b1194_rules, checked below
            ok = "rules:" in body and body[body.index("rules:") + 1] == NOT_RBUILD
            expect(ok, f"{src}: job `{name}` must start its rules with the .not_rbuild reference (B1208)")
    b = [l.strip() for l in (job_body(include_text, ".b1194_rules") or [])]
    expect("rules:" in b and b[b.index("rules:") + 1] == NOT_RBUILD,
           "`.b1194_rules` must start with the .not_rbuild reference (B1208)")
    rb = [l.strip() for l in (job_body(text, "rbuild") or [])]
    rbt = "\n".join(rb)
    expect(bool(rb), "the `rbuild` job is missing (B1208)")
    expect('- if: $RBUILD_TARGET && $CI_PIPELINE_SOURCE == "push" && $CI_COMMIT_BRANCH && $CI_COMMIT_BRANCH != "fable"' in rb,
           "`rbuild` must run only on an RBUILD_TARGET push off fable")
    expect(sum(1 for l in rb if l.startswith("- if:") or l.startswith("- when:")) == 1, "`rbuild` must have exactly one rule")
    expect("extends: .goldfarm" in rb and "expire_in: 2 days" in rbt and "junit: rbuild-junit.xml" in rbt,
           "`rbuild` extends .goldfarm (x86-build, interruptible) and keeps ctest log + junit for 2 days")
    expect("- !reference [.build_tree, script]" in rb and "willeq_build_tree" in rbt,
           "`rbuild` must reuse the shared willeq_build_tree, not a copy")
    expect('if [ -n "${RBUILD_TEST:-}" ]; then' in rb, "`rbuild` runs ctest only when RBUILD_TEST is set")
    wf = [l.strip() for l in (job_body(text, "workflow") or [])]
    i_rb = next((i for i, l in enumerate(wf) if l.startswith("- if: $RBUILD_TARGET")), None)
    i_mr = next((i for i, l in enumerate(wf) if "$CI_OPEN_MERGE_REQUESTS" in l), None)
    expect(i_rb is not None and i_mr is not None and i_rb < i_mr,
           "workflow: the RBUILD_TARGET push rule must sit above the open-MR suppression")
    expect(any("/^rbuild: ci build/" in l for l in wf), "workflow: an `rbuild: ci build` commit must not start an MR pipeline")
    return fails


def main():
    text = CI.read_text()
    gles2 = GLES2.read_text()
    inc = B1194_INCLUDE.read_text() if B1194_INCLUDE.exists() else ""
    fails = check(text, gles2, inc)

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
        "build-arm without the rbuild exclusion": (text.replace(
            "  rules:\n    - !reference [.not_rbuild, rules]\n    - if: $CI_COMMIT_BRANCH == \"fable\"\n    - when: manual\n      allow_failure: true\n  script:\n    - set -o pipefail",
            "  rules:\n    - if: $CI_COMMIT_BRANCH == \"fable\"\n    - when: manual\n      allow_failure: true\n  script:\n    - set -o pipefail", 1), gles2),
        "hygiene without the rbuild exclusion": (text.replace(
            "  rules:\n    - !reference [.not_rbuild, rules]\n    - when: on_success\n", ""), gles2),
        "rbuild rule below the open-MR rule": (text.replace(
            "    - if: $RBUILD_TARGET && $CI_PIPELINE_SOURCE", "    - if: $XRBUILD_TARGET && $CI_PIPELINE_SOURCE"), gles2),
        "rbuild job gone": (text.replace("\nrbuild:\n", "\nrbuildx:\n"), gles2),
    }
    for what, (t, g) in breaks.items():
        if t == text and g == gles2:
            fails.append(f"self-test: '{what}' changed nothing (the break no longer applies)")
        elif not check(t, g, inc):
            fails.append(f"self-test: '{what}' still passes")

    for f in fails:
        print("FAIL:", f)
    if fails:
        return 1
    print("OK: ccache launcher guarded on every configure + reported after every build; "
          "docs-only diffs skip build-test/gate/client-ledger-live; hygiene always runs; build+test are one job; "
          "an rbuild pipeline (B1208) runs the rbuild job alone")
    return 0


if __name__ == "__main__":
    sys.exit(main())
