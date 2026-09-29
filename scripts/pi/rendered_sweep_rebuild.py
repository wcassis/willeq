#!/usr/bin/env python3
"""#102: rebuild a rendered sweep's results.tsv / results_retry.tsv from the
run dirs it stored (zones/<zone>, zones/<zone>_retry), with the CURRENT
scripts/pi/rendered_zone_summarize.py -- so a corrected pass criterion never
needs a Pi re-run. The previous files are kept as *.pre-rebuild.

usage: rendered_sweep_rebuild.py <sweep dir>
"""
import csv
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    d = sys.argv[1]
    for name, suffix in (("results.tsv", ""), ("results_retry.tsv", "_retry")):
        path = os.path.join(d, name)
        if not os.path.exists(path):
            continue
        with open(path) as f:
            rows = list(csv.DictReader(f, delimiter="\t"))
        shutil.copy(path, path + ".pre-rebuild")
        os.remove(path)
        for r in rows:
            run = os.path.join(d, "zones", r["zone"] + suffix)
            cmd = [sys.executable, os.path.join(HERE, "rendered_zone_summarize.py"), "--run", run,
                   "--zone", r["zone"], "--attempt", r["attempt"], "--tsv", path,
                   "--expansion", r["expansion"], "--s3d-bytes", r["s3d_bytes"]]
            if not os.path.exists(os.path.join(run, "client.log")):
                cmd += ["--preset-signature", r["signature"]]
            else:
                cmd += ["--dmesg", os.path.join(run, "dmesg_oom.txt")]
            subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
