#!/usr/bin/env python3
"""Exercise the actual wrappers with inert services; never contact EQEmu/Pi."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]


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
        self.write("scripts/pi_rendered_zone_sweep.sh", '''#!/bin/bash
printf '%s\\n' "$@" > "$TEST_ROOT/sweep.args"
while [ $# -gt 0 ]; do
    case "$1" in --out) out="$2"; shift 2 ;; *) shift ;; esac
done
mkdir -p "$out/zones/qeynos2"
cp "$TEST_ROOT/fixture.log" "$out/zones/qeynos2/client.log"
''')
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
        self.write("bin/sleep", "#!/bin/bash\n/bin/sleep 0.01\n")
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

    def run_wrapper(self, pi, extra=()):
        out = self.root / ("pi" if pi else "x86")
        args = ["--pi-dir", "unused"] if pi else ["--bin", str(self.root / "client")]
        return subprocess.run(["bash", str(self.root / "scripts/b964" /
                               ("pi_crowd.sh" if pi else "crowd_run.sh")),
                               "--zones", "qeynos2", "--variants", "tagnode200far",
                               "--out", str(out), *args, *extra],
                              env=self.env, capture_output=True, text=True, timeout=15)

    def test_defaults_and_explicit_controls_preserve_workload(self):
        for pi in (True, False):
            result = self.run_wrapper(pi)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            out = self.root / ("pi" if pi else "x86") / "qeynos2/tagnode200far"
            default = (out / "tour.txt").read_text()
            self.assertIn("# phase: idle\nwait 40.0", default)
            self.assertIn("# phase: hold\nwait 15.0", default)
            if pi:
                args = (self.root / "sweep.args").read_text().splitlines()
                self.assertEqual(args[args.index("--cap") + 1], "720")
            result = self.run_wrapper(pi, ["--idle", "180", "--hold-after", "75", "--cap", "800", "--frametiming"])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            changed = (out / "tour.txt").read_text()
            self.assertEqual(changed.replace("wait 180.0", "wait 40.0").replace(
                "wait 75.0", "wait 15.0").replace("/frametiming\n", ""), default)
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

    def test_invalid_controls_fail_before_resources(self):
        for pi in (True, False):
            for extra in (["--idle", "nan"], ["--hold-after", "inf"], ["--idle", "-1"],
                          ["--cap", "0"], ["--cap", "55"], ["--cap", "nan"]):
                self.assertEqual(self.run_wrapper(pi, extra).returncode, 3)
                self.assertFalse((self.root / "acquired").exists())
                self.assertFalse((self.root / "sweep.args").exists())

    def test_no_ready_process_is_bounded_by_real_runner(self):
        out = self.root / "no-ready"
        result = subprocess.run([sys.executable, str(REPO / "scripts/pi/rendered_zone_run.py"),
                                 "--out", str(out), "--cap", "0.2", "--stall", "10",
                                 "--stop-point", "never", "--", "/bin/sleep", "30"],
                                capture_output=True, text=True, timeout=10)
        summary = json.loads((out / "summary.json").read_text())
        self.assertIn("cap:", summary["kill_reason"])
        self.assertFalse(summary["points"])
        checked = subprocess.run([sys.executable, str(REPO / "scripts/b975/frametiming_table.py"),
                                  "--check", "--zone", "qeynos2", str(out / "client.log")],
                                 capture_output=True, text=True)
        self.assertEqual(checked.returncode, 1)


if __name__ == "__main__":
    unittest.main()
