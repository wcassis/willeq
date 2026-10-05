#!/usr/bin/env python3
"""Exercise the actual wrappers with inert services; never contact EQEmu/Pi.

B987's QA finding F1: the accepted duration SPELLINGS (plain ASCII decimal
0..86400, scripts/b964/observation_controls.py) must be the same ones every
consumer accepts -- the x86 watchdog comparison, the sweep's `timeout
$((CAP_S + 120))`, the runner's --cap/--stall, the generated tour's wait.
These tests run the real scripts with inert services and prove: each
rejected spelling is a usage error BEFORE any lock/stack/Pi resource, the
canonical integers (not the raw tokens) are what the scripts consume, the
x86 overall deadline actually stops a continuously logging child, the Pi
wrapper forwards --env to the sweep, and the sweep validates its own
durations when called directly (as other rigs call it).
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

REPO = Path(__file__).resolve().parents[2]

# Every spelling that used to reach a Bash comparison or arithmetic as a raw
# token (QA's cap_probe.py and the Pi sibling), plus a range violation.
BAD_DURATIONS = ["0_1", "1_000", "0720", "0800", " 5", "+5", "-1", "5.0",
                 "1e3", "\u0661", "", "99999999", "0x10"]
# The validator and both wrappers agree on these spellings.
GOOD_DURATIONS = ["0", "5", "86400"]


class ControlsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("scripts/b964", "scripts/b975"):
            shutil.copytree(REPO / name, self.root / name)
        self.fixture = self.root / "fixture.log"
        self.fixture.write_text("[memledger] point=game_zone_begin zone=qeynos2 \n"
                                "[memledger] point=ready zone=qeynos2 \n"
                                "Script: executing '/face 0 1 0'\n" + self.block() * 2
                                + "Script: executing '/move 0 30 0'\n"
                                "[memledger] point=steady_60s zone=qeynos2 \n"
                                "LoadZoneSources: Loaded zone 'qeynos2'\n")
        self.write("scripts/pi_rendered_zone_sweep.sh",
                   "#!/bin/bash\n"
                   "printf '%s\\n' \"$@\" > \"$TEST_ROOT/sweep.args\"\n"
                   "touch \"$TEST_ROOT/sweep_started\"\n"
                   "while [ $# -gt 0 ]; do\n"
                   "    case \"$1\" in --out) out=\"$2\"; shift 2 ;; *) shift ;; esac\n"
                   "done\n"
                   "mkdir -p \"$out/zones/qeynos2\"\n"
                   "cp \"$TEST_ROOT/fixture.log\" \"$out/zones/qeynos2/client.log\"\n")
        self.write("scripts/eqemu/env.sh", '''EQ_TEST_CHARACTER=Test
EQ_TEST_ACCOUNT=test
EQ_TEST_PASSWORD=test
EQ_TEST_SERVER_LONGNAME=test
LOGIN_PORT=1
eqemu_is_local() { return 1; }
eqemu_port_host() { echo localhost; }
eqemu_lock_take() { touch "$TEST_ROOT/acquired"; }
eqemu_lock_show() { :; }
eqemu_lock_release() { :; }
eqemu_stack_ready() { return 0; }
eqemu_snapshot() { echo test > "$1"; }
eqemu_sql() { echo 0; }
''')
        self.write("scripts/eqemu/restore_lib.sh", 'eqemu_restore_snapshot() { cp "$1" "$2"; }\n')
        self.write("scripts/eqemu/zone_place_lib.sh", 'boot_zone() { :; }\nseed_char() { :; }\nkill_zone() { :; }\n')
        for name in ("scripts/eqemu/up.sh", "scripts/eqemu/down.sh", "bin/sudo"):
            self.write(name, "#!/bin/bash\nexit 0\n")
        self.write("bin/sleep", "#!/bin/bash\nif [ \"${1:-}\" = real ]; then shift; fi\n"
                                "/bin/sleep 0.01\n")
        self.write("client", '#!/bin/bash\ncat "$TEST_ROOT/fixture.log"\n')
        self.env = dict(os.environ, TEST_ROOT=str(self.root),
                        PATH=str(self.root / "bin") + os.pathsep + os.environ["PATH"])

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        path.chmod(0o755)

    @staticmethod
    def block():
        return "FRAME TIMING BREAKDOWN (60 frames, 30.0 fps estimate)\n" + "".join(
            f"[INFO] {name}: 1600 us\n" for name in
            ("Total Frame", "Scene Draw All", "Solid Pass", "Transparent Pass"))

    def run_wrapper(self, pi, extra=(), timeout=15):
        out = self.root / ("pi" if pi else "x86")
        args = ["--pi-dir", "unused"] if pi else ["--bin", str(self.root / "client")]
        return subprocess.run(["bash", str(self.root / "scripts/b964" /
                               ("pi_crowd.sh" if pi else "crowd_run.sh")),
                               "--zones", "qeynos2", "--variants", "tagnode200far",
                               "--out", str(out), *args, *extra],
                              env=self.env, capture_output=True, text=True, timeout=timeout)

    def assert_rejected_before_resources(self, pi, option, value):
        result = self.run_wrapper(pi, [option, value])
        self.assertEqual(result.returncode, 3,
                         f"pi={pi} {option}={value!r}: {result.stdout}{result.stderr}")
        self.assertFalse((self.root / "acquired").exists(),
                         f"pi={pi} {option}={value!r}: the lock was taken")
        self.assertFalse((self.root / "sweep.args").exists(),
                         f"pi={pi} {option}={value!r}: the sweep started")

    def test_defaults_and_explicit_controls_preserve_workload(self):
        for pi in (True, False):
            result = self.run_wrapper(pi)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            out = self.root / ("pi" if pi else "x86") / "qeynos2/tagnode200far"
            default = (out / "tour.txt").read_text()
            self.assertIn("# phase: idle\nwait 40", default)
            self.assertIn("# phase: hold\nwait 15", default)
            if pi:
                args = (self.root / "sweep.args").read_text().splitlines()
                self.assertEqual(args[args.index("--cap") + 1], "720")
            result = self.run_wrapper(pi, ["--idle", "180", "--hold-after", "75", "--cap", "800", "--frametiming"])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            changed = (out / "tour.txt").read_text()
            self.assertEqual(changed.replace("wait 180", "wait 40").replace(
                "wait 75", "wait 15").replace("/frametiming\n", ""), default)
            self.assertEqual(changed.count("/say #spawn Crowd "), 200)
            self.assertTrue(json.loads((out / "observation.json").read_text())["pass"])
            if pi:
                args = (self.root / "sweep.args").read_text().splitlines()
                self.assertEqual(args[args.index("--cap") + 1], "800")
                self.assertIn("WILLEQ_NAMETAG_BATCH=0", args)
                self.assertIn("--final-visit", args)
            else:
                self.assertIn("WILLEQ_NAMETAG_BATCH=0", (out / "env.txt").read_text())

    def test_no_ready_or_final_steady_is_failure_even_with_clean_exit(self):
        original = self.fixture.read_text()
        for marker in ("point=ready", "point=steady_60s"):
            self.fixture.write_text("\n".join(line for line in original.splitlines() if marker not in line))
            for pi in (True, False):
                self.assertEqual(self.run_wrapper(pi, ["--frametiming"]).returncode, 1)

    def test_rejected_spellings_fail_before_resources(self):
        # F1: each of these used to be ACCEPTED by Python int()/float() and
        # forwarded raw to a Bash comparison/arithmetic.
        for pi in (True, False):
            for option in ("--idle", "--hold-after", "--cap"):
                for value in BAD_DURATIONS:
                    self.assert_rejected_before_resources(pi, option, value)
            self.assert_rejected_before_resources(pi, "--cap", "55")  # < idle+hold
        # The good spellings are ACCEPTED with a consistent workload (idle +
        # hold-after must stay below cap, the validator's own rule).
        self.assertEqual(self.run_wrapper(False, ["--idle", "5", "--hold-after", "0",
                                                  "--cap", "86400"]).returncode, 0)
        self.assertEqual(self.run_wrapper(True, ["--idle", "5", "--hold-after", "0",
                                                 "--cap", "86400"]).returncode, 0)

    def test_canonical_integer_reaches_the_sweep(self):
        # The sweep's own validation is the guard; this proves the wrapper
        # forwards the validated canonical form (both already accept 86400).
        result = self.run_wrapper(True, ["--cap", "86400"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        args = (self.root / "sweep.args").read_text().splitlines()
        self.assertEqual(args[args.index("--cap") + 1], "86400")

    def test_good_spellings_are_accepted(self):
        # Every spelling the validator accepts (0..86400) must be accepted
        # by the wrappers too -- no path may reject a valid token. --cap is
        # the strictest position (it must exceed idle + hold-after).
        for pi in (True, False):
            # A mid-range value at the strictest position.
            result = self.run_wrapper(pi, ["--cap", "100"])
            self.assertEqual(result.returncode, 0,
                             f"pi={pi} --cap 100: {result.stdout}{result.stderr}")
            # --idle just below the range boundary (cap must exceed it).
            result = self.run_wrapper(pi, ["--idle", "86399", "--hold-after", "0",
                                           "--cap", "86400"])
            self.assertEqual(result.returncode, 0,
                             f"pi={pi} --idle 86399: {result.stdout}{result.stderr}")

    def test_pi_env_passthrough_reaches_the_sweep(self):
        # B988 lane: the pair must run the client with WILLEQ_LIMA_NOBOCACHE=1.
        # The variant's own --env (WILLEQ_NAMETAG_BATCH=0) is forwarded first;
        # the user's --env must ALSO reach the sweep (the last --env is it).
        result = self.run_wrapper(True, ["--env", "WILLEQ_LIMA_NOBOCACHE=1"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        args = (self.root / "sweep.args").read_text().splitlines()
        idx = len(args) - 1 - args[::-1].index("--env")
        self.assertEqual(args[idx + 1], "WILLEQ_LIMA_NOBOCACHE=1")
        self.assertIn("WILLEQ_NAMETAG_BATCH=0", args)

    def test_x86_deadline_stops_a_continuously_logging_child(self):
        # THE executed regression QA's probe would have been (run 306): a
        # child that logs forever is stopped by the cap's deadline, not by
        # the silence watchdog, with zero Bash comparison errors.
        self.write("client", "#!/bin/bash\nwhile true; do echo watchdog-progress; sleep real 0.05; done\n")
        out = self.root / "deadline"
        start = time.monotonic()
        result = subprocess.run(["bash", str(self.root / "scripts/b964/crowd_run.sh"),
                                 "--zones", "qeynos2", "--variants", "tagnode200far",
                                 "--bin", str(self.root / "client"), "--out", str(out),
                                 "--idle", "0", "--hold-after", "0", "--cap", "2"],
                                env=self.env, capture_output=True, text=True, timeout=60)
        elapsed = time.monotonic() - start
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertLess(elapsed, 15, f"the deadline did not bound the run ({elapsed:.1f} s)")
        self.assertIn("2 s overall deadline", result.stderr)
        self.assertNotIn("integer expression expected", result.stderr)
        result_txt = (out / "qeynos2/tagnode200far/result.txt").read_text()
        self.assertIn("watchdog=2 s overall deadline", result_txt)

    def test_sweep_validates_durations_itself_before_any_resource(self):
        # The sweep is called directly by other rigs, so its own --cap /
        # --stall / --hold must be validated BEFORE any lock, stack or Pi
        # resource: usage error 64, no out dir, no binary identity file, no
        # Pi probe. The REAL scripts (not a stub) run in a sandbox whose Pi
        # is 127.0.0.1/unreachable: a VALID set passes validation, reaches
        # the lane and reports the unreachable Pi (68) -- proof the
        # validation did not eat valid durations.
        sandbox = self.root / "sweep_sandbox"
        (sandbox / "scripts").mkdir(parents=True)
        shutil.copy(REPO / "scripts/pi_rendered_zone_sweep.sh",
                    sandbox / "scripts/pi_rendered_zone_sweep.sh")
        for name in ("scripts/eqemu/env.sh", "scripts/eqemu/restore_lib.sh",
                     "scripts/eqemu/up.sh", "scripts/eqemu/down.sh",
                     "scripts/pi/env.sh", "scripts/pi/mesa_nollvm.env",
                     "scripts/ci/zone_content_sweep_zones.tsv"):
            target = sandbox / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(REPO / name, target)
            target.chmod(0o755)
        (sandbox / "refactor/B854").mkdir(parents=True)
        (sandbox / "refactor/B854/sweep_results.tsv").write_text("zone\nqeynos2\n")
        (sandbox / "arm/mesa/prefix").mkdir(parents=True)
        (sandbox / "arm/bin").mkdir()
        for name in ("arm/bin/willeq", "arm/bin/willeq-nollvm"):
            (sandbox / name).write_text("#!/bin/sh\nexit 0\n")
            (sandbox / name).chmod(0o755)
        (sandbox / "arm/BUILD_INFO").write_text("sha=deadbeef\n")
        (sandbox / "arm/mesa/prefix/.willeq-mesa-pin").write_text("")
        env = dict(os.environ,
                   PI_SSH_OPTS="-o BatchMode=yes -o ConnectTimeout=2",
                   PI_HOST="127.0.0.1", PI_USER="nobody")
        env.pop("EQEMU_HOST", None)   # default local: no --allow-vm-lane needed
        env.pop("WILLEQ_MESA", None)  # default nollvm: the stubbed prefix suffices
        run_sweep = lambda extra: subprocess.run(
            ["bash", str(sandbox / "scripts/pi_rendered_zone_sweep.sh"),
             "--zones", "qeybos2", "--out", str(self.root / "sweep_out"),
             "--pi-dir", str(sandbox / "arm"), "--no-retry", *extra],
            env=env, capture_output=True, text=True, timeout=30)
        for option in ("--cap", "--stall", "--hold"):
            for value in BAD_DURATIONS:
                r = run_sweep([option, value])
                self.assertEqual(r.returncode, 64,
                                 f"{option}={value!r}: rc={r.returncode} {r.stderr}")
                self.assertIn("bad duration", r.stderr)
                self.assertFalse((self.root / "sweep_out").exists(),
                                 f"{option}={value!r}: an out dir was created")
                self.assertFalse((self.root / "sweep_out").joinpath("binary_identity.txt").exists())
        for value in GOOD_DURATIONS:
            r = run_sweep(["--cap", value, "--stall", "120", "--hold", value])
            # Past validation: it probed the Pi (127.0.0.1) and reported it
            # unreachable (68) -- never a usage error, never a lock/stack.
            self.assertEqual(r.returncode, 68,
                             f"{value!r}: rc={r.returncode} {r.stderr}")
            self.assertIn("unreachable", r.stderr)


if __name__ == "__main__":
    unittest.main()
