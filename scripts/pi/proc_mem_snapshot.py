#!/usr/bin/env python3
"""B932: one memory snapshot of a running process, as JSON on stdout.

  * /proc/<pid>/status: VmRSS, RssAnon, RssFile, RssShmem, VmHWM
  * /proc/<pid>/smaps: per mapped FILE (grouped by path) the resident bytes
    (Rss), how much of that is anonymous (private COW pages of the mapping,
    Anonymous:), and the mapping size -- the same split the client's own
    [memmap] census prints, but for any process (kmscube has no ledger)
  * the list of every mapped shared object path -- the PROOF of which Mesa
    the process really loaded (the LLVM-less prefix vs the system one)
  * B955 `gpu`: every source of the process's GPU buffer memory (Lima GEM,
    outside RssAnon): the DRM fds' fdinfo drm-* keys (none on the Pi's 6.12
    kernel), the /dev/dri mappings (CPU-mapped BOs; Size only, never Rss),
    the process's memory cgroup (memory.stat shmem/kernel and how many
    processes share it -- exclusive under `systemd-run --user --scope`), the
    display controller's framebuffers this process allocated (debugfs, root:
    the scanout buffers live in CMA, not shmem) and the system Shmem line

usage: proc_mem_snapshot.py <pid> [--label TEXT]
Reading another user's smaps needs root (kmscube runs under sudo).
"""
import glob
import json
import os
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
    drm_sizes = {}   # B955: size -> count of each /dev/dri mapping (one BO each)
    cur = None
    cur_path = ""
    head = re.compile(r"^[0-9a-f]+-[0-9a-f]+ \S+ \S+ \S+ \d+\s*(.*)$")
    with open("/proc/%d/smaps" % pid) as f:
        for line in f:
            m = head.match(line)
            if m:
                path = m.group(1).strip() or "[anon]"
                cur = files.setdefault(path, {"rss": 0, "anon": 0, "size": 0, "maps": 0})
                cur["maps"] += 1
                cur_path = path
                continue
            if cur is None:
                continue
            k, _, v = line.partition(":")
            if k in ("Rss", "Anonymous", "Size"):
                cur[{"Rss": "rss", "Anonymous": "anon", "Size": "size"}[k]] += int(v.split()[0]) * 1024
                if k == "Size" and cur_path.startswith("/dev/dri/"):
                    b = int(v.split()[0]) * 1024
                    drm_sizes[b] = drm_sizes.get(b, 0) + 1
    sos = sorted(p for p in files if ".so" in p)
    gpu = gpu_sources(pid, status.get("Name", ""), files)
    gpu["drm_map_sizes"] = {str(k): v for k, v in sorted(drm_sizes.items())}
    out = {
        "pid": pid, "label": label, "status": status,
        "files": dict(sorted(files.items(), key=lambda kv: -kv[1]["rss"])),
        "shared_objects": sos,
        "llvm_mapped": any("libLLVM" in p for p in sos),
        "gallium": [p for p in sos if "libgallium" in p],
        "egl_mesa": [p for p in sos if "libEGL_mesa" in p],
        "gbm": [p for p in sos if "libgbm" in p or "dri_gbm" in p],
        "gpu": gpu,
    }
    return out


def _read(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return None


def gpu_sources(pid, comm, files):
    """B955: every per-process view of GPU buffer memory we can read."""
    out = {"drm_fds": 0, "fdinfo_drm_keys": {}, "drm_mapped_bytes": 0, "drm_mappings": 0,
           "memcg": None, "scanout_fb_bytes": None, "scanout_fbs": [], "system_shmem_bytes": None}
    try:
        fds = os.listdir("/proc/%d/fd" % pid)
    except OSError:
        fds = []
    for fd in fds:
        try:
            target = os.readlink("/proc/%d/fd/%s" % (pid, fd))
        except OSError:
            continue
        if not target.startswith("/dev/dri/"):
            continue
        out["drm_fds"] += 1
        text = _read("/proc/%d/fdinfo/%s" % (pid, fd)) or ""
        keys = {k.strip(): v.strip() for k, _, v in (l.partition(":") for l in text.splitlines())
                if k.startswith("drm-")}
        if keys:
            out["fdinfo_drm_keys"][fd] = keys
    for path, v in files.items():
        if path.startswith("/dev/dri/"):
            out["drm_mapped_bytes"] += v["size"]
            out["drm_mappings"] += v["maps"]
    cg = _read("/proc/%d/cgroup" % pid) or ""
    for line in cg.splitlines():
        if line.startswith("0::"):
            base = "/sys/fs/cgroup" + line[3:]
            stat = _read(base + "/memory.stat")
            procs = _read(base + "/cgroup.procs")
            if stat is not None:
                kv = dict(l.split(" ", 1) for l in stat.splitlines() if " " in l)
                out["memcg"] = {"path": line[3:],
                                "procs": len(procs.split()) if procs else 0,
                                "shmem": int(kv.get("shmem", 0)), "kernel": int(kv.get("kernel", 0)),
                                "slab": int(kv.get("slab", 0)), "anon": int(kv.get("anon", 0)),
                                "file": int(kv.get("file", 0))}
    # The scanout framebuffers: the display controller's debugfs names the
    # allocating process by its comm (15 chars). Root only; absent otherwise.
    total, seen = 0, False
    # dri/<minor> and dri/<name> are symlinks to the same directory: one each.
    fbfiles = sorted({os.path.realpath(p) for p in glob.glob("/sys/kernel/debug/dri/*/framebuffer")})
    for fbfile in fbfiles:
        text = _read(fbfile)
        if text is None:
            continue
        seen = True
        for block in re.split(r"\n(?=framebuffer\[)", text):
            m = re.search(r"allocated by = (.*)", block)
            if not m or m.group(1).strip() != comm[:15]:
                continue
            sizes = [int(x) for x in re.findall(r"^\s+size=(\d+)$", block, re.M)]
            dims = re.search(r"^\s+size=(\d+x\d+)$", block, re.M)
            out["scanout_fbs"].append({"file": fbfile, "bytes": sum(sizes),
                                       "dims": dims.group(1) if dims else ""})
            total += sum(sizes)
    if seen:
        out["scanout_fb_bytes"] = total
    mi = _read("/proc/meminfo") or ""
    m = re.search(r"^Shmem:\s+(\d+) kB", mi, re.M)
    if m:
        out["system_shmem_bytes"] = int(m.group(1)) * 1024
    return out


def main():
    pid = int(sys.argv[1])
    label = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--label" else ""
    json.dump(snapshot(pid, label), sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
