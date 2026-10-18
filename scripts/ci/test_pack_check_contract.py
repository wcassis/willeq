#!/usr/bin/env python3
"""B1094 (#414) -- the pack-check contract: --check is pin-only, --check-installed
is installed-vs-pin.

scripts/build_willeq_pack.sh used to compare the temp rebuild to the pin AND
to the installed shared pack, so one mutable file (/mnt/projects/EverQuest_ETC1/
willeq.eqe1) was the oracle for N branches: the B1076 card installed its branch
pack over the shared file and every other pipeline at the fable tip went red on
PackReproducible (MR !230 pipeline 1626, 2026-10-18). The fix splits the two
properties:

  --check            exit 0 iff the rebuild matches the PIN. The pack at
                     --output (the installed pack by default) is printed as
                     INFO, never as a verdict.
  --check-installed  exit 0 iff the installed pack matches the pin. No build.
                     Its ctest gate (scripts/ci/installed_pack_matches_pin.sh)
                     fails hard only on fable.

This test proves --check without ever building a real pack: two stub
`ui_atlas_builder` / `eqe1_builder` binaries in a temp bin-dir (the script's
own documented flag) make the "rebuild" a known temp file, and --output +
--sha-file point at temp files too -- the installed shared pack is never read
or written by this test. Every clause is broken in a self-test that requires
a failure.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "build_willeq_pack.sh"
WRAPPER = ROOT / "scripts" / "ci" / "installed_pack_matches_pin.sh"

STUB_UI = """#!/usr/bin/env bash
exit 0
"""

# Stub eqe1_builder: ignore every collected input, copy the content file
# named by $B1094_STUB_CONTENT to the last --output value.
STUB_EQE1 = """#!/usr/bin/env bash
out=""
for a in "$@"; do
    if [ "$prev" = "--output" ]; then out="$a"; fi
    prev="$a"
done
[ -n "$out" ] || exit 1
cp "$B1094_STUB_CONTENT" "$out/willeq.eqe1"
"""


def run(args, **env):
    e = dict(os.environ)
    e.update(env)
    return subprocess.run(args, capture_output=True, text=True, env=e)


def sha256(path):
    h = __import__("hashlib").sha256(Path(path).read_bytes()).hexdigest()
    return h


def write_pin(path, sha, extra_comments=0):
    lines = ["# pinned by the B1094 contract test"] * max(extra_comments, 0)
    lines.append(sha + "  willeq.eqe1")
    Path(path).write_text("\n".join(lines) + "\n")


def make_stubs(bindir, content_file):
    for name, stub in (("ui_atlas_builder", STUB_UI), ("eqe1_builder", STUB_EQE1)):
        p = Path(bindir) / name
        p.write_text(stub)
        p.chmod(0o755)
    return {"B1094_STUB_CONTENT": str(content_file)}


def pin_only(tmp):
    """--check: verdict from the pin alone; the installed pack is INFO."""
    fails = []

    def expect(cond, msg):
        if not cond:
            fails.append(msg)

    d = Path(tmp) / "pin"
    d.mkdir()
    content = d / "content.bin"
    content.write_bytes(b"B1094 stub pack bytes\n")
    # The script refuses to run without this readable, whatever the builder is.
    (d / "window_pieces01.tga").write_bytes(b"stub tga")
    built_sha = sha256(content)

    # Case 1: built == pinned, installed DELIBERATELY DIFFERENT -> exit 0.
    out1 = d / "installed_differs.eqe1"
    out1.write_bytes(b"some other branch's pack\n")
    pin1 = d / "pin1.sha256"
    write_pin(pin1, built_sha)
    r = run([
        "bash", str(SCRIPT), "--check",
        "--bin-dir", str(d),
        "--output", str(out1),
        "--sha-file", str(pin1),
        "--uifiles-path", str(d),
        "--textures-dir", str(d),
    ], **make_stubs(str(d), content))
    expect(r.returncode == 0,
           f"--check must pass when built == pinned even though the "
           f"installed pack differs (rc={r.returncode}, "
           f"{r.stdout.strip()[-300:]})")
    expect("INFO" in r.stdout, f"--check must label the installed sha INFO, got:\n{r.stdout}")
    expect(sha256(out1) in r.stdout, "--check must print the installed sha as info")

    # Case 2: built != pinned, installed == built (the old oracle would pass)
    # -> exit 1. This is the regression #414's title is about.
    pin2 = d / "pin2.sha256"
    write_pin(pin2, "f" * 64)
    r = run([
        "bash", str(SCRIPT), "--check",
        "--bin-dir", str(d),
        "--output", str(out1),
        "--sha-file", str(pin2),
        "--uifiles-path", str(d),
        "--textures-dir", str(d),
    ], **make_stubs(str(d), content))
    expect(r.returncode == 1,
           f"--check must fail when built != pinned even though the "
           f"installed pack matches the rebuild (rc={r.returncode})")
    expect("FAIL" in r.stderr, f"--check mismatch must print FAIL, got:\n{r.stderr}")

    # Case 3: built == pinned, installed missing -> still exit 0.
    pin3 = d / "pin3.sha256"
    write_pin(pin3, built_sha)
    r = run([
        "bash", str(SCRIPT), "--check",
        "--bin-dir", str(d),
        "--output", str(d / "does-not-exist.eqe1"),
        "--sha-file", str(pin3),
        "--uifiles-path", str(d),
        "--textures-dir", str(d),
    ], **make_stubs(str(d), content))
    expect(r.returncode == 0,
           f"--check must not gate on the installed pack's existence "
           f"(rc={r.returncode}, {r.stderr.strip()[-300:]})")

    # Case 4: no pin at all -> exit 1 (the pin stays the verdict's source).
    r = run([
        "bash", str(SCRIPT), "--check",
        "--bin-dir", str(d),
        "--output", str(out1),
        "--sha-file", str(d / "no-pin.sha256"),
        "--uifiles-path", str(d),
        "--textures-dir", str(d),
    ], **make_stubs(str(d), content))
    expect(r.returncode == 1, "--check with a missing pin file must fail")

    # The verdict block: --check must not read the installed sha into rc.
    text = SCRIPT.read_text()
    check_block = text[text.index("if [ \"$CHECK_ONLY\" -eq 1 ]"):
                       text.index("# ── Stage 4: atomic install")]
    expect(not re.search(r'INSTALLED_SHA.*rc\s*=\s*1', check_block),
           "--check block must not turn the installed sha into a failure")
    expect("shipped" not in check_block.replace("shared", ""),
           "--check must not use the old 'shipped' verdict wording")
    return fails


def check_installed(tmp):
    """--check-installed: installed-vs-pin, no build at all."""
    fails = []

    def expect(cond, msg):
        if not cond:
            fails.append(msg)

    d = Path(tmp) / "installed"
    d.mkdir()
    installed = d / "willeq.eqe1"
    installed.write_bytes(b"the installed pack\n")
    inst_sha = sha256(installed)
    pin = d / "pin.sha256"

    # match -> exit 0
    write_pin(pin, inst_sha)
    r = run(["bash", str(SCRIPT), "--check-installed",
             "--output", str(installed), "--sha-file", str(pin)])
    expect(r.returncode == 0,
           f"--check-installed must pass on a match (rc={r.returncode}, {r.stderr.strip()[-300:]})")
    expect("PASS" in r.stdout, "--check-installed must print PASS on a match")
    expect("ui_atlas_builder" not in r.stdout + r.stderr,
           "--check-installed must not run the builders")

    # mismatch -> exit 1
    write_pin(pin, "0" * 64)
    r = run(["bash", str(SCRIPT), "--check-installed",
             "--output", str(installed), "--sha-file", str(pin)])
    expect(r.returncode == 1,
           f"--check-installed must fail on a mismatch (rc={r.returncode}, {r.stdout.strip()[-300:]})")
    expect("FAIL" in r.stderr, "--check-installed mismatch must print FAIL")

    # missing installed pack -> exit 1
    write_pin(pin, inst_sha)
    r = run(["bash", str(SCRIPT), "--check-installed",
             "--output", str(d / "missing.eqe1"), "--sha-file", str(pin)])
    expect(r.returncode == 1, "--check-installed must fail when the pack is missing")

    # missing pin -> exit 1
    r = run(["bash", str(SCRIPT), "--check-installed",
             "--output", str(installed), "--sha-file", str(d / "no-pin.sha256")])
    expect(r.returncode == 1, "--check-installed must fail when the pin is missing")

    # The wrapper: hard only on fable.
    base = [
        "bash", str(WRAPPER),
        "--check-installed", "--output", str(installed), "--sha-file", str(pin),
    ]
    write_pin(pin, "0" * 64)  # mismatch for the next three
    r = run(["bash", str(WRAPPER)] + base[2:], CI_COMMIT_BRANCH="fable")
    expect(r.returncode == 1, "wrapper must hard-fail on fable")
    r = run(["bash", str(WRAPPER)] + base[2:], CI_COMMIT_BRANCH="task/kb-b1094-pack-coupling")
    expect(r.returncode == 0,
           "wrapper must print-and-pass on a task branch (rc=%s, %s)" % (
               r.returncode, r.stdout.strip()[-200:]))
    r = run(["bash", str(WRAPPER)] + base[2:], CI_COMMIT_BRANCH="")
    expect(r.returncode == 0, "wrapper must print-and-pass outside CI")
    write_pin(pin, inst_sha)  # match everywhere
    r = run(["bash", str(WRAPPER)] + base[2:], CI_COMMIT_BRANCH="fable")
    expect(r.returncode == 0,
           f"wrapper must pass on fable when it matches (rc={r.returncode}, {r.stdout.strip()[-200:]})")
    return fails


def main():
    with tempfile.TemporaryDirectory(prefix="b1094_pack_contract.") as tmp:
        fails = pin_only(tmp) + check_installed(tmp)
        for f in fails:
            print("FAIL:", f)
        if fails:
            return 1

        # Self-test: each clause, broken, must be caught.
        def probe(name, broken):
            got = []
            with tempfile.TemporaryDirectory(prefix="b1094_probe.") as t:
                got = broken(t)
            if not got:
                print(f"SELF-TEST FAIL: breaking '{name}' did not fail")
                return 1
            return 0

        orig = SCRIPT.read_text()
        # a --check that still gates on the installed pack must be caught:
        # pin == built, installed DELIBERATELY different (case 1). The old
        # installed leg flips the exit code there; pin-only does not.
        broken_check = orig.replace(
            'echo "installed (INFO only): $INSTALLED_SHA  ($OUTPUT)"',
            'echo "installed (INFO only): $INSTALLED_SHA  ($OUTPUT)"\n'
            '        if [ "$INSTALLED_SHA" != "$BUILT_SHA" ]; then rc=1; fi')
        rc = probe("check installed leg",
                   lambda t: pin_only_with(t, broken_check))
        if rc:
            return rc
        # a --check-installed that passes on a mismatch must be caught
        broken_ci = orig.replace(
            'echo "      the pack (this script without --check/--check-installed)." >&2\n'
            '            rc=1',
            'echo "      the pack (this script without --check/--check-installed)." >&2')
        rc = probe("check-installed mismatch",
                   lambda t: check_installed_with(t, broken_ci))
        if rc:
            return rc
    print("PackCheckContract: OK (self-test passed)")
    return 0


def pin_only_with(tmp, broken_script):
    """Same core case as pin_only() case 1, but against a patched script."""
    d = Path(tmp) / "script"
    d.mkdir(exist_ok=True)
    (d / "build_willeq_pack.sh").write_text(broken_script)
    return pin_only_with_dir(str(d))


def pin_only_with_dir(sd):
    """Case 1 of pin_only() against a patched script: pin matches the rebuild,
    the installed pack DELIBERATELY differs. A script that still gates on the
    installed pack fails here; the pin-only --check passes."""
    fails = []

    def expect(cond, msg):
        if not cond:
            fails.append(msg)

    d = Path(sd) / "case"
    d.mkdir()
    content = d / "content.bin"
    content.write_bytes(b"B1094 stub pack bytes\n")
    (d / "window_pieces01.tga").write_bytes(b"stub tga")
    built_sha = sha256(content)
    out1 = d / "installed_differs.eqe1"
    out1.write_bytes(b"some other branch's pack\n")  # installed != built
    pin1 = d / "pin1.sha256"
    write_pin(pin1, built_sha)  # pin == built
    r = run([
        "bash", str(Path(sd) / "build_willeq_pack.sh"), "--check",
        "--bin-dir", str(d),
        "--output", str(out1),
        "--sha-file", str(pin1),
        "--uifiles-path", str(d),
        "--textures-dir", str(d),
    ], **make_stubs(str(d), content))
    expect(r.returncode == 0,
           "broken --check (installed leg) did not flip the verdict")
    return fails


def check_installed_with(tmp, broken_script):
    # A fake repo in the PRODUCTION layout: the wrapper resolves REPO_ROOT as
    # <its dir>/../.., then execs $REPO_ROOT/scripts/build_willeq_pack.sh, so
    # the script under test must sit at <tmp>/scripts/build_willeq_pack.sh and
    # the wrapper copy at <tmp>/scripts/ci/. The first version of this helper
    # put both under <tmp>/script/ (no s): the wrapper then exec'd a path that
    # did not exist, bash returned 127, the fable leg turned that into the
    # exit 1 the probe expected, and the self-test could never see a broken
    # script (every fable pipeline from 1645 on printed SELF-TEST FAIL).
    d = Path(tmp)
    sd = d / "scripts"
    (sd / "ci").mkdir(parents=True, exist_ok=True)
    (sd / "build_willeq_pack.sh").write_text(broken_script)
    shutil.copy(WRAPPER, sd / "ci" / "installed_pack_matches_pin.sh")
    fails = []

    def expect(cond, msg):
        if not cond:
            fails.append(msg)

    inst = d / "willeq.eqe1"
    inst.write_bytes(b"the installed pack\n")
    pin = d / "pin.sha256"
    write_pin(pin, "0" * 64)
    # The probe is about the HARD leg, so it names the branch itself instead
    # of inheriting whatever CI_COMMIT_BRANCH the pipeline happens to carry.
    r = run(["bash", str(sd / "ci" / "installed_pack_matches_pin.sh"),
             "--check-installed", "--output", str(inst), "--sha-file", str(pin)],
            CI_COMMIT_BRANCH="fable")
    expect("No such file" not in r.stderr, "the wrapper did not find the script under test: " + r.stderr.strip())
    expect(r.returncode == 1, "broken --check-installed passed on a mismatch")
    return fails


if __name__ == "__main__":
    sys.exit(main())
