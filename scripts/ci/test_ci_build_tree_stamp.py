#!/usr/bin/env python3
"""B1026 (#346) -- pin the host+sha build-tree stamp into .gitlab-ci.yml.

Since gitlab-runner-2 took the `x86-build` tag (2026-10-11) the stages of one
pipeline may land on different hosts, and the stages pass no artifacts: fable
pipeline 809 built on gitlab-runner-1 (job 4006) and tested gitlab-runner-2's
pre-B603 binary (job 4007), a false red. The fix lives in the pipeline file,
so this test reads it as TEXT (no PyYAML on the runners, and `!reference` is
a GitLab-only tag) and asserts:

  1. the `build-test` job verifies the stamp before its first ctest line, and
     the verify block rebuilds with the shared willeq_build_tree on a mismatch
     and verifies again; `client-ledger-live` verifies before it runs the
     client (B1042 folded `build` + `test` into `build-test`, so there the
     verify is a file read that matches -- the net is kept regardless);
  2. the build steps exist once (`.build_tree`), the `build-test` job runs them, and
     inside them the host+sha claim is written AFTER `cmake --build` (a stamp
     must never claim a build that failed), with the only stamp before it
     being the ref-only one that clears an earlier claim;
  3. the `.goldfarm` comment no longer claims a single tagged runner.

Red before B1026 (no verify, no claim stamp, the single-runner comment).

B1038 (#358) adds group 4, the headless net: a `build-headless` job in the
`build` stage runs the SHARED willeq_build_tree (no second copy of the build
steps) with WILLEQ_HEADLESS=1 into its own build-headless/ref tree, the shared
function configures that case with EQT_GRAPHICS=OFF, and the job runs
`ctest -LE hygiene` itself after the build (no stage split, so no verify is
needed); GIT_CLEAN_FLAGS keeps the tree and prune-build-trees expires it.
Red before B1038 (no such job: the EQT_GRAPHICS=OFF C++ build rotted unseen).
"""
import re
import sys
from pathlib import Path

CI = Path(__file__).resolve().parents[2] / ".gitlab-ci.yml"


def job_body(text, name):
    """The lines of a top-level key's block (up to the next top-level key)."""
    m = re.search(rf"^{re.escape(name)}:\s*$", text, re.M)
    assert m, f"no top-level `{name}:` in .gitlab-ci.yml"
    rest = text[m.end():]
    nxt = re.search(r"^[^\s#][^:\n]*:\s*$", rest, re.M)
    return (rest[: nxt.start()] if nxt else rest).splitlines()


def code_lines(body):
    """Drop comment-only lines: a comment that mentions ctest must not count."""
    return [l for l in body if not l.strip().startswith("#")]


def first_index(lines, pred, what):
    for i, l in enumerate(lines):
        if pred(l):
            return i
    raise AssertionError(f"not found: {what}")


def check(text):
    failures = []

    def expect(cond, msg):
        if not cond:
            failures.append(msg)

    # 2. one copy of the build steps; the claim comes after cmake --build.
    bt = code_lines(job_body(text, ".build_tree"))
    i_build = first_index(bt, lambda l: "cmake --build" in l, ".build_tree cmake --build")
    claims = [i for i, l in enumerate(bt) if "prune_ref_build_trees.py --stamp" in l and "--sha" in l]
    expect(len(claims) == 1, f".build_tree must write exactly one host+sha claim, found {len(claims)}")
    expect(all(i > i_build for i in claims), ".build_tree writes the host+sha claim before cmake --build")
    expect(all("--host" in bt[i] and "$CI_COMMIT_SHA" in bt[i] for i in claims),
           ".build_tree's claim must record --host and $CI_COMMIT_SHA")
    pre = [l for l in bt[:i_build] if "--stamp" in l]
    expect(pre and all("--sha" not in l for l in pre),
           ".build_tree needs a ref-only stamp before the build (it clears an earlier claim)")
    i_conf = first_index(bt, lambda l: re.search(r"cmake -S \. -B \"\$BUILD_TREE\"", l), ".build_tree configure")
    i_pre = first_index(bt, lambda l: "--stamp" in l and "--sha" not in l, ".build_tree ref-only stamp")
    expect(i_pre < i_conf, ".build_tree's ref-only stamp must precede the configure")
    expect(any("set -eo pipefail" in l for l in bt[:i_build]),
           ".build_tree must run under errexit+pipefail so a failed build never stamps")

    build = code_lines(job_body(text, "build-test"))
    expect(any("!reference [.build_tree, script]" in l for l in build), "`build-test` must use !reference [.build_tree, script]")
    expect(any(l.strip() == "- willeq_build_tree" for l in build), "`build-test` must run willeq_build_tree")
    expect(not any("cmake --build" in l for l in build), "`build-test` keeps its own copy of cmake --build (use .build_tree)")
    i_wbt = [i for i, l in enumerate(build) if l.strip() == "- willeq_build_tree"]
    i_ct = [i for i, l in enumerate(build) if "ctest" in l]
    expect(bool(i_wbt) and bool(i_ct) and i_wbt[0] < i_ct[0], "`build-test` must build before its first ctest line")

    # 1. the verify-or-rebuild block, and its use before the first ctest.
    vb = code_lines(job_body(text, ".verify_build_tree"))
    expect(any("!reference [.build_tree, script]" in l for l in vb), ".verify_build_tree must define willeq_build_tree")
    verifies = [i for i, l in enumerate(vb) if "prune_ref_build_trees.py --verify" in l]
    expect(len(verifies) == 2, f".verify_build_tree must verify before and after the rebuild, found {len(verifies)}")
    i_reb = first_index(vb, lambda l: l.strip() == "willeq_build_tree", ".verify_build_tree rebuild")
    expect(verifies and verifies[0] < i_reb < verifies[-1], ".verify_build_tree order must be verify, rebuild, verify")
    expect(any("STAMP MISMATCH" in l for l in vb), ".verify_build_tree must print a loud STAMP MISMATCH line")
    expect(all('--host "$(hostname)"' in vb[i] and '--sha "$CI_COMMIT_SHA"' in vb[i] for i in verifies),
           ".verify_build_tree must check this host and $CI_COMMIT_SHA")

    for job, consumer in (("build-test", "ctest"), ("client-ledger-live", "client_ledger_capture.sh")):
        body = code_lines(job_body(text, job))
        i_c = first_index(body, lambda l: consumer in l, f"{job} {consumer}")
        i_v = [i for i, l in enumerate(body) if "!reference [.verify_build_tree, script]" in l]
        expect(bool(i_v) and i_v[0] < i_c, f"`{job}` must verify the stamp before its first {consumer} line")

    # 3. the stale single-runner premise is gone from .goldfarm.
    gold = "\n".join(job_body(text, ".goldfarm"))
    for stale in ("ONE runner carries", "x86-build-2", "idles until"):
        expect(stale not in gold, f".goldfarm comment still says {stale!r}")
    expect("BOTH runners carry `x86-build`" in gold, ".goldfarm comment must say both runners carry x86-build")
    expect("tags: [x86-build]" in gold, ".goldfarm must still tag x86-build")
    # 4. B1038: the headless configuration is built and tested in ONE job.
    if not re.search(r"^build-headless:\s*$", text, re.M):
        failures.append("no `build-headless` job: the EQT_GRAPHICS=OFF build is never compiled (B1038)")
        return failures
    raw = job_body(text, "build-headless")
    hl = code_lines(raw)
    expect(any(l.strip() == "stage: build" for l in hl), "`build-headless` must be in the build stage")
    expect(any(l.strip() == "extends: .goldfarm" for l in hl), "`build-headless` must extend .goldfarm")
    expect(any(re.match(r"\s*BUILD_TREE: build-headless/ref/\$CI_COMMIT_REF_SLUG\s*$", l) for l in hl),
           "`build-headless` must build into its own build-headless/ref/$CI_COMMIT_REF_SLUG tree")
    expect(any(re.match(r'\s*WILLEQ_HEADLESS: "1"\s*$', l) for l in hl), "`build-headless` must set WILLEQ_HEADLESS: \"1\"")
    expect(any("!reference [.build_tree, script]" in l for l in hl), "`build-headless` must use the shared .build_tree")
    expect(not any("cmake --build" in l or "cmake -S" in l for l in hl),
           "`build-headless` keeps its own copy of the build steps (use .build_tree)")
    i_b = [i for i, l in enumerate(hl) if l.strip() == "- willeq_build_tree"]
    i_t = [i for i, l in enumerate(hl) if "ctest" in l and '--test-dir "$BUILD_TREE"' in l and "-LE hygiene" in l]
    expect(bool(i_b) and bool(i_t) and i_b[0] < i_t[0],
           "`build-headless` must run willeq_build_tree and then ctest -LE hygiene in the same job")
    off = [i for i, l in enumerate(bt) if re.search(r'cmake -S \. -B "\$BUILD_TREE".*-DEQT_GRAPHICS=OFF', l)]
    gate = [i for i, l in enumerate(bt) if "WILLEQ_HEADLESS" in l]
    expect(len(off) == 1 and bool(gate) and gate[0] < off[0],
           ".build_tree must configure EQT_GRAPHICS=OFF (once) under WILLEQ_HEADLESS")
    clean = re.search(r"^\s*GIT_CLEAN_FLAGS:([^\n]*)$", text, re.M)
    expect(bool(clean) and "-e build-headless" in clean.group(1), "GIT_CLEAN_FLAGS must keep build-headless")
    prune = "\n".join(code_lines(job_body(text, "prune-build-trees")))
    expect("--root build-headless/ref" in prune, "prune-build-trees must expire build-headless/ref trees")
    return failures


def main():
    text = CI.read_text()
    failures = check(text)

    # Self-test: the pre-B1026 shape must fail every rule group.
    old = text.replace("    - !reference [.verify_build_tree, script]\n", "")
    old = old.replace("BOTH runners carry `x86-build`", "ONE runner carries the tag")
    expect_red = check(old)
    if not any("test" in f and "verify" in f for f in expect_red) or not any(".goldfarm" in f for f in expect_red):
        failures.append(f"self-test: a pipeline without the verify still passes ({expect_red})")
    # (B1042: the ccache report line may sit between the build and the claim.)
    stamp_first = re.sub(r"(\n\s*nice -n 10 cmake --build[^\n]*)((?:\n\s*ccache_report[^\n]*)?)(\n\s*(?:#[^\n]*\n\s*)*(?:if [^\n]*then )?python3 scripts/ci/prune_ref_build_trees.py --stamp[^\n]*--sha[^\n]*)",
                         r"\3\1\2", text)
    if stamp_first == text or not any("before cmake --build" in f for f in check(stamp_first)):
        failures.append("self-test: a claim written before cmake --build still passes")

    no_hl = re.sub(r"\nbuild-headless:\n(?:[ #][^\n]*\n|\n)*", "\n", text)
    if no_hl == text or not any("build-headless" in f for f in check(no_hl)):
        failures.append("self-test: a pipeline without build-headless still passes")
    no_ct = re.sub(r"\n[^\n]*ctest --test-dir \"\$BUILD_TREE\" -LE hygiene[^\n]*", "", text)
    if no_ct == text or not any("ctest -LE hygiene" in f for f in check(no_ct)):
        failures.append("self-test: a build-headless job without its in-job ctest still passes")

    for f in failures:
        print("FAIL:", f)
    if failures:
        return 1
    print("OK: build-test/client-ledger-live verify the host+sha stamp first; build claims only after cmake --build; "
          ".goldfarm names both x86-build runners; build-headless builds EQT_GRAPHICS=OFF and runs ctest in-job")
    return 0


if __name__ == "__main__":
    sys.exit(main())
