#!/usr/bin/env python3
"""B932: one memory snapshot of a running process, as JSON on stdout.

  * /proc/<pid>/status: VmRSS, RssAnon, RssFile, RssShmem, VmHWM
  * /proc/<pid>/smaps: per mapped FILE (grouped by path) the resident bytes
    (Rss), how much of that is anonymous (private COW pages of the mapping,
    Anonymous:), and the mapping size -- the same split the client's own
    [memmap] census prints, but for any process (kmscube has no ledger)
  * the list of every mapped shared object path -- the PROOF of which Mesa
    the process really loaded (the LLVM-less prefix vs the system one)

usage: proc_mem_snapshot.py <pid> [--label TEXT]
Reading another user's smaps needs root (kmscube runs under sudo).
"""
import json
import re
import sys


def snapshot(pid, label=""):
    status = {}
    with open("/proc/%d/status" % pid) as f:
        for line in f:
            k, _, v = line.partition(":")
            if k in ("Name", "VmRSS", "RssAnon", "RssFile", "RssShmem", "VmHWM", "VmSize"):
                status[k] = v.strip() if k == "Name" else int(v.split()[0]) * 1024
    files = {}
    cur = None
    head = re.compile(r"^[0-9a-f]+-[0-9a-f]+ \S+ \S+ \S+ \d+\s*(.*)$")
    with open("/proc/%d/smaps" % pid) as f:
        for line in f:
            m = head.match(line)
            if m:
                path = m.group(1).strip() or "[anon]"
                cur = files.setdefault(path, {"rss": 0, "anon": 0, "size": 0, "maps": 0})
                cur["maps"] += 1
                continue
            if cur is None:
                continue
            k, _, v = line.partition(":")
            if k in ("Rss", "Anonymous", "Size"):
                cur[{"Rss": "rss", "Anonymous": "anon", "Size": "size"}[k]] += int(v.split()[0]) * 1024
    sos = sorted(p for p in files if ".so" in p)
    out = {
        "pid": pid, "label": label, "status": status,
        "files": dict(sorted(files.items(), key=lambda kv: -kv[1]["rss"])),
        "shared_objects": sos,
        "llvm_mapped": any("libLLVM" in p for p in sos),
        "gallium": [p for p in sos if "libgallium" in p],
        "egl_mesa": [p for p in sos if "libEGL_mesa" in p],
        "gbm": [p for p in sos if "libgbm" in p or "dri_gbm" in p],
    }
    return out


def main():
    pid = int(sys.argv[1])
    label = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--label" else ""
    json.dump(snapshot(pid, label), sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
