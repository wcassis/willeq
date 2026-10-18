#!/usr/bin/env python3
"""B1093 (#413) -- the ARM cross-build container dies with the shell that started it.

Reads the two ARM build scripts, the guard helper and .gitlab-ci.yml as TEXT and
asserts:

  1. scripts/ci/arm_container.sh defines the label, `arm_container_guard` (a
     --name, the label, --init and a trap on EXIT TERM INT HUP that docker-kills
     the name) and `arm_container_sweep` (docker ps by label, then docker kill).
  2. scripts/build-arm-noble.sh and scripts/build-arm64-noble.sh source the helper,
     call `arm_container_guard` and then `arm_container_run` (background docker run
     + wait, so a signal reaches the trap), with no bare `docker run` left.
  3. .gitlab-ci.yml: both `build-arm` and `build-arm64` run
     `bash scripts/ci/arm_container.sh --sweep` before the build script.

A self-test then breaks each rule group and requires a failure.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "scripts" / "ci" / "arm_container.sh"
BUILDS = [ROOT / "scripts" / "build-arm-noble.sh", ROOT / "scripts" / "build-arm64-noble.sh"]
CI = ROOT / ".gitlab-ci.yml"


def job_body(text, name):
    m = re.search(rf"^{re.escape(name)}:\s*$", text, re.M)
    if not m:
        return None
    rest = text[m.end():]
    nxt = re.search(r"^[^\s#][^:\n]*:\s*$", rest, re.M)
    return [l for l in (rest[: nxt.start()] if nxt else rest).splitlines() if not l.strip().startswith("#")]


def check(helper, builds, ci):
    fails = []

    def expect(cond, msg):
        if not cond:
            fails.append(msg)

    # 1. the helper
    expect('ARM_CONTAINER_LABEL="willeq.ci.arm=1"' in helper, "helper: label constant missing")
    expect("arm_container_guard()" in helper, "helper: arm_container_guard missing")
    expect("arm_container_sweep()" in helper, "helper: arm_container_sweep missing")
    expect('--name "$ARM_CONTAINER_NAME"' in helper, "helper: guard sets no --name")
    expect('--label "$ARM_CONTAINER_LABEL"' in helper, "helper: guard sets no --label")
    expect("--init" in helper, "helper: guard passes no --init")
    expect(re.search(r"trap .*docker kill .*EXIT TERM INT HUP", helper), "helper: no kill trap on EXIT TERM INT HUP")
    expect("arm_container_run()" in helper, "helper: arm_container_run missing")
    expect(re.search(r'docker run --rm "\$\{ARM_CONTAINER_ARGS\[@\]\}" "\$@" &\n\s*pid=\$!\n\s*wait "\$pid"', helper),
           "helper: arm_container_run must background docker run and wait (a foreground child defers the trap)")
    expect('docker ps -q --filter "label=$ARM_CONTAINER_LABEL"' in helper, "helper: sweep does not select by label")
    expect(re.search(r"docker kill \$ids", helper), "helper: sweep does not kill what it found")
    expect("--sweep) arm_container_sweep" in helper, "helper: no --sweep entry point")

    # 2. the build scripts
    for name, text in builds:
        expect('source "$PROJECT_DIR/scripts/ci/arm_container.sh"' in text, f"{name}: does not source the helper")
        g = text.find("arm_container_guard ")
        r = text.find("arm_container_run ")
        expect(g >= 0, f"{name}: no arm_container_guard call")
        expect(r >= 0 and g >= 0 and g < r, f"{name}: guard must precede arm_container_run")
        expect("docker run" not in text.replace("`docker run`", ""), f"{name}: a bare docker run bypasses the guard")

    # 3. the CI jobs
    for job, script in (("build-arm", "scripts/build-arm-noble.sh"), ("build-arm64", "scripts/build-arm64-noble.sh")):
        body = job_body(ci, job)
        expect(body is not None, f"{job}: job missing")
        if body is None:
            continue
        sweep = next((i for i, l in enumerate(body) if "bash scripts/ci/arm_container.sh --sweep" in l), None)
        build = next((i for i, l in enumerate(body) if script in l), None)
        expect(sweep is not None, f"{job}: no arm_container.sh --sweep step")
        expect(build is not None, f"{job}: build script line missing")
        expect(sweep is not None and build is not None and sweep < build, f"{job}: sweep must precede the build")
    return fails


def main():
    helper = HELPER.read_text()
    builds = [(p.name, p.read_text()) for p in BUILDS]
    ci = CI.read_text()
    fails = check(helper, builds, ci)
    for f in fails:
        print("FAIL:", f)
    if fails:
        return 1

    # self-test: each broken input must fail
    broken = [
        ("helper trap", check(helper.replace("EXIT TERM INT HUP", "EXIT"), builds, ci)),
        ("helper label", check(helper.replace('--label "$ARM_CONTAINER_LABEL"', ""), builds, ci)),
        ("build guard", check(helper, [(n, t.replace("arm_container_guard ", "true ")) for n, t in builds], ci)),
        ("build run", check(helper, [(n, t.replace("arm_container_run \\", "docker run --rm \\")) for n, t in builds], ci)),
        ("helper wait", check(helper.replace('wait "$pid"', 'true'), builds, ci)),
        ("ci sweep", check(helper, builds, ci.replace("bash scripts/ci/arm_container.sh --sweep", "true"))),
    ]
    for name, f in broken:
        if not f:
            print(f"SELF-TEST FAIL: breaking '{name}' did not fail")
            return 1
    print("ArmContainerGuard: OK (self-test passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
