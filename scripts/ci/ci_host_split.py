#!/usr/bin/env python3
"""B1042 (#362): how often did a pipeline's build and test land on different
x86 runners?  The measurement behind folding `build` + `test` into the one
`build-test` job (docs/ci.md, "build and test are one job").

Walks the newest pipelines (GitLab jobs API, `runner.id` per job) and, over
the last N that ran BOTH named jobs to success/failure, prints one row per
pipeline and the split percentage. Read-only. Token: ~/gitlab_token.txt.

    python3 scripts/ci/ci_host_split.py            # build vs test, last 50
    python3 scripts/ci/ci_host_split.py --first build-test --second gate

Pipelines after B1042 have no `build`/`test` pair, so the default reproduces
the pre-B1042 figure only while those pipelines are among the newest; pass
--before <pipeline id> to start older.
"""
import argparse
import json
import os
import ssl
import urllib.request

BASE = "https://gitlab.goldfarm.local/api/v4/projects/willeq%2Fwilleq"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--first", default="build")
    ap.add_argument("--second", default="test")
    ap.add_argument("-n", type=int, default=50)
    ap.add_argument("--before", type=int, default=0, help="only pipelines with id < this")
    a = ap.parse_args()
    tok = open(os.path.expanduser("~/gitlab_token.txt")).read().strip()
    ctx = ssl._create_unverified_context()  # self-signed cert

    def get(path):
        req = urllib.request.Request(BASE + path, headers={"PRIVATE-TOKEN": tok})
        return json.load(urllib.request.urlopen(req, context=ctx))

    rows, page = [], 1
    while len(rows) < a.n and page < 40:
        pls = get(f"/pipelines?per_page=50&page={page}&order_by=id&sort=desc")
        if not pls:
            break
        for p in pls:
            if a.before and p["id"] >= a.before:
                continue
            jobs = get(f"/pipelines/{p['id']}/jobs?per_page=100")

            def pick(name):
                js = [j for j in jobs if j["name"] == name and j["status"] in ("success", "failed") and j.get("runner")]
                return js[0] if js else None

            f, s = pick(a.first), pick(a.second)
            if f and s:
                rows.append((p["id"], p["ref"], f["runner"]["description"], s["runner"]["description"],
                             int(f["duration"] or 0), int(s["duration"] or 0), f["runner"]["id"] != s["runner"]["id"]))
                if len(rows) >= a.n:
                    break
        page += 1
    print(f"pipeline\tref\t{a.first}_host\t{a.second}_host\t{a.first}_s\t{a.second}_s\tsplit")
    for r in rows:
        print(*r, sep="\t")
    if rows:
        split = sum(1 for r in rows if r[-1])
        print(f"pipelines={len(rows)} split={split} pct={100 * split / len(rows):.1f}% ids={rows[-1][0]}..{rows[0][0]}")


if __name__ == "__main__":
    main()
