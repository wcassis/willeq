#!/usr/bin/env python3
"""B962 (GitLab #280): fold a B962_GEMDUMP owner dump (scripts/pi/lima_bo_census.c)
into a per-OWNER table of the client's Lima GEM buffers, and reconcile it with
the client's own [memgpu] gpu_buffers row (its memory cgroup's shmem) at the
same ledger point.

usage: gem_owner_table.py DUMP [--point P ...] [--client-log LOG] [--json]
                               [--detail OWNER] [--top N]
       gem_owner_table.py --events BO_EVENTS_LOG --client-log LOG [--point P ...]
  DUMP         the file the shim wrote (one "P ... / B ... / E" block per
               memory-ledger point the client recorded)
  --point      the point(s) to print (default: every point in the dump)
  --client-log the client's log (rendered_zone_run.py client.log): its
               [memgpu] line at the same point supplies gpu_buffers, and the
               residual (gpu_buffers - every regular BO) is printed BY NAME
  --detail     list the individual tags inside one owner row
  --json       machine-readable (the regression test reads this)
  --events     fold the shim's create/close EVENT log (B955_BOLOG) instead of a
               dump: the only record a client without the B962 dump call (a
               binary older than this unit) leaves. Cut at each [memgpu] line's
               wall-clock stamp via the log's "# clock" header; no tags, so the
               rows are by size/kind only (UNTAGGED_ROWS)

Owner rows (OWNERS below, first match wins) are decided from the tag the
creating thread had pushed, the BO's flags/size and whether Mesa mapped it.
Heap BOs (LIMA_BO_FLAG_HEAP: the GP tile heap) are listed with their MAXIMUM
size and NOT summed: their pages grow on GPU demand and the kernel charges
only the pages it has added (B955 measured the regular-BO sum within 45 KB of
the cgroup's shmem at startup), so the cgroup's figure minus the regular sum
is the heaps' grown pages plus anything else in the cgroup's shmem (the rig's
own frame dumps) -- printed as the residual, never hidden.
"""
import argparse
import json
import re
import sys

MIB = 1048576.0
STREAM_UPLOADER_BYTES = 1048576   # u_upload_create_default(): 1 MiB per buffer


def owner_of(bo):
    """(row, detail) for one live BO dict."""
    if "row" in bo:
        return bo["row"], "size %d" % bo["size"]
    tag = bo["tag"]
    inner = tag.split("|")[-1] if tag != "-" else "-"
    outer = tag.split("|")[0] if tag != "-" else "-"
    size = bo["size"]
    if bo["heap"]:
        return "lima: GP tile heap (max size, grows on demand)", tag
    if inner.startswith("tex:etc1a "):
        return "textures: ETC1 alpha companions", inner
    if inner.startswith("tex:etc1 "):
        return "textures: ETC1 (EQE1 packs)", inner
    if inner.startswith("tex:v2 "):
        return "textures: EQE1 v2 payloads (RGB565/RGBA4444/RGBA8888)", inner
    if inner.startswith("tex:rgba "):
        return "textures: RGBA8888 (Irrlicht addTexture)", inner
    if inner.startswith("tex:genmip "):
        return "textures: glGenerateMipmap reallocation", inner
    if inner.startswith("tex:atlas"):
        return "textures: zone atlas pages", inner
    if inner.startswith("tex:"):
        return "textures: other (" + inner.split(" ")[0] + ")", inner
    if inner.startswith("rtt"):
        return "render targets (RTT colour + depth)", inner
    if inner.startswith("vbo:"):
        return "vertex/index buffers (" + inner + ")", inner
    if outer == "egl:device" or inner == "egl:device":
        if size == STREAM_UPLOADER_BYTES and bo["mapped"]:
            return "mesa: stream uploader (1 MiB, context 1)", tag
        if size >= 1000000 and not bo["mapped"]:
            return "egl: window surface depth/stencil", tag
        if size == 262144:
            return "lima: PLB (context 1)", tag
        return "lima: context 1 + screen small BOs (PLB streams, pp buffer)", tag
    if outer == "egl:upload-thread-context":
        return "lima: upload-thread context (PLB, streams)", tag
    if inner == "swap" or outer == "swap":
        return "lima: per-flush BOs (PP stream cache, job data)", tag
    if inner.startswith("draw") or outer.startswith("draw") or inner == "clear" or inner == "screenshot":
        if size == STREAM_UPLOADER_BYTES and bo["mapped"]:
            return "mesa: stream uploader (1 MiB)", tag
        if size > STREAM_UPLOADER_BYTES and bo["mapped"]:
            return "mesa: stream uploader (oversize single upload)", tag
        return "lima: draw-time BOs (shader binaries, varyings in flight, PP streams)", tag
    if inner == "shader:compile":
        return "lima: shader binaries (at compile)", tag
    return "untagged (scope " + bo["scope"] + ")", tag


def read_dump(path):
    """{point: {"meta": {...}, "bos": [...]}}, in file order (a point recorded
    twice keeps the LAST block)."""
    points = {}
    order = []
    cur = None
    with open(path, errors="replace") as f:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if p[0] == "P" and len(p) >= 8:
                cur = {"meta": {"point": p[1], "mono": float(p[2]), "live": int(p[3]), "bytes": int(p[4]),
                                "created": int(p[5]), "closed": int(p[6]), "overflow": int(p[7])},
                       "bos": []}
            elif p[0] == "B" and cur is not None and len(p) >= 10:
                cur["bos"].append({"fd": int(p[1]), "handle": int(p[2]), "size": int(p[3]), "heap": int(p[4]),
                                   "mapped": int(p[5]), "seq": int(p[6]), "tid": int(p[7]), "scope": p[8],
                                   "tag": p[9]})
            elif p[0] == "E" and cur is not None:
                name = cur["meta"]["point"]
                if name not in points:
                    order.append(name)
                points[name] = cur
                cur = None
    return points, order


MEMGPU_RE = re.compile(r"\[memgpu\] point=(\S+) .*?gpu_buffers=(\d+) source=(\S+) .*?logical_textures=(\d+) "
                       r"logical_buffers=(\d+)")


def read_memgpu(path):
    out = {}
    if not path:
        return out
    with open(path, errors="replace") as f:
        for line in f:
            m = MEMGPU_RE.search(line)
            if m:
                out[m.group(1)] = {"gpu_buffers": int(m.group(2)), "source": m.group(3),
                                   "logical_textures": int(m.group(4)), "logical_buffers": int(m.group(5))}
    return out


def fold(block, memgpu=None):
    rows = {}
    heap_max = 0
    for bo in block["bos"]:
        row, detail = owner_of(bo)
        r = rows.setdefault(row, {"count": 0, "bytes": 0, "mapped_bytes": 0, "details": {}})
        r["count"] += 1
        if bo["heap"]:
            heap_max += bo["size"]
            r["bytes"] += 0
        else:
            r["bytes"] += bo["size"]
            if bo["mapped"]:
                r["mapped_bytes"] += bo["size"]
        d = r["details"].setdefault(detail, [0, 0])
        d[0] += 1
        d[1] += bo["size"]
    regular = sum(r["bytes"] for r in rows.values())
    out = {"point": block["meta"]["point"], "rows": rows, "regular_bytes": regular,
           "heap_max_bytes": heap_max, "live": block["meta"]["live"],
           "created": block["meta"]["created"], "closed": block["meta"]["closed"],
           "overflow": block["meta"]["overflow"]}
    if memgpu:
        out["gpu_buffers"] = memgpu["gpu_buffers"]
        out["gpu_buffers_source"] = memgpu["source"]
        out["logical_textures"] = memgpu["logical_textures"]
        out["logical_buffers"] = memgpu["logical_buffers"]
        out["residual_bytes"] = memgpu["gpu_buffers"] - regular
    return out


def print_table(t, detail=None, top=0):
    print("== point %s: %d live BOs (created %d, closed %d%s)" % (
        t["point"], t["live"], t["created"], t["closed"], ", TABLE OVERFLOW" if t["overflow"] else ""))
    rows = sorted(t["rows"].items(), key=lambda kv: -kv[1]["bytes"])
    print("  %-72s %6s %12s %9s %12s" % ("owner", "BOs", "bytes", "MiB", "mapped"))
    for name, r in rows:
        print("  %-72s %6d %12d %9.2f %12d" % (name[:72], r["count"], r["bytes"], r["bytes"] / MIB, r["mapped_bytes"]))
        if (detail and detail in name) or top:
            items = sorted(r["details"].items(), key=lambda kv: -kv[1][1])
            for tag, (n, b) in items[: (top or len(items))]:
                print("      %-70s %6d %12d" % (tag[:70], n, b))
    print("  %-72s %6s %12d %9.2f" % ("SUM of regular BOs", "", t["regular_bytes"], t["regular_bytes"] / MIB))
    print("  %-72s %6s %12d %9.2f" % ("(heap BOs, maximum size, not summed)", "", t["heap_max_bytes"],
                                       t["heap_max_bytes"] / MIB))
    if "gpu_buffers" in t:
        print("  %-72s %6s %12d %9.2f" % ("[memgpu] gpu_buffers (%s)" % t["gpu_buffers_source"], "",
                                           t["gpu_buffers"], t["gpu_buffers"] / MIB))
        print("  %-72s %6s %12d %9.2f" % ("residual = gpu_buffers - regular BOs (heap pages grown, tmpfs files)",
                                           "", t["residual_bytes"], t["residual_bytes"] / MIB))
        print("  %-72s %6s %12d %9.2f" % ("logical tally: textures", "", t["logical_textures"],
                                           t["logical_textures"] / MIB))
        print("  %-72s %6s %12d %9.2f" % ("logical tally: vertex/index buffers", "", t["logical_buffers"],
                                           t["logical_buffers"] / MIB))


# Rows for an UNTAGGED live set (a binary without the B962 tags): the sizes
# that identify a buffer by themselves on this client at 1280x720. Everything
# else is "untagged: other <size class>".
UNTAGGED_ROWS = [
    (lambda b: b["heap"], "lima: GP tile heap (max size, grows on demand)"),
    (lambda b: b["size"] == 3686400, "egl: window surface depth/stencil (1280x720 D24S8)"),
    (lambda b: b["size"] == 262144, "lima: PLB (256 KiB, two per context)"),
    (lambda b: b["size"] == STREAM_UPLOADER_BYTES, "untagged: 1 MiB (Mesa stream uploader size)"),
    (lambda b: b["size"] in (45056, 32768), "untagged: 45,056 / 32,768 B (a 256x256 ETC1 texture with / without its mips)"),
    (lambda b: b["size"] <= 4096, "untagged: <= 4 KiB"),
    (lambda b: b["size"] <= 65536, "untagged: 4-64 KiB"),
    (lambda b: b["size"] <= 1048576, "untagged: 64 KiB-1 MiB"),
    (lambda b: True, "untagged: > 1 MiB"),
]


def read_events(path):
    """[(mono, op, fd, handle, size, flags)], plus (realtime - monotonic)."""
    events = []
    offset = None
    with open(path, errors="replace") as f:
        for line in f:
            if line.startswith("# clock "):
                kv = dict(x.split("=") for x in line.split()[2:])
                offset = float(kv["realtime"]) - float(kv["monotonic"])
                continue
            p = line.split()
            if len(p) >= 6 and p[1] == "C":
                events.append((float(p[0]), "C", p[5], int(p[2]), int(p[3]), int(p[4])))
            elif len(p) >= 4 and p[1] in ("X", "I"):
                events.append((float(p[0]), p[1], p[3], int(p[2]), 0, 0))
    return events, offset


EPOCH_POINT_RE = re.compile(r"^(\d+\.\d+) .*\[memgpu\] point=(\S+) ")


def point_epochs(client_log):
    out = {}
    with open(client_log, errors="replace") as f:
        for line in f:
            m = EPOCH_POINT_RE.search(line)
            if m:
                out[m.group(2)] = float(m.group(1))
    return out


def events_block(events, mono_cut, point):
    live = {}
    created = closed = 0
    for t, op, fd, h, size, flags in events:
        if t > mono_cut:
            break
        if op == "C":
            live[(fd, h)] = (size, flags)
            created += 1
        elif op == "X" and live.pop((fd, h), None) is not None:
            closed += 1
    bos = []
    for (fd, h), (size, flags) in live.items():
        b = {"fd": fd, "handle": h, "size": size, "heap": flags & 1, "mapped": 0, "seq": 0, "tid": 0,
             "scope": "-", "tag": "-"}
        for test, row in UNTAGGED_ROWS:
            if test(b):
                b["row"] = row
                break
        bos.append(b)
    return {"meta": {"point": point, "mono": mono_cut, "live": len(bos), "bytes": sum(b["size"] for b in bos),
                     "created": created, "closed": closed, "overflow": 0}, "bos": bos}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", nargs="?", default="")
    ap.add_argument("--events", default="")
    ap.add_argument("--point", action="append", default=[])
    ap.add_argument("--client-log", default="")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--detail", default="")
    ap.add_argument("--top", type=int, default=0)
    a = ap.parse_args()
    memgpu = read_memgpu(a.client_log)
    if a.events:
        if not a.client_log:
            ap.error("--events needs --client-log (the point stamps)")
        events, offset = read_events(a.events)
        if offset is None:
            ap.error("%s has no '# clock' header (a pre-B962 shim)" % a.events)
        stamps = point_epochs(a.client_log)
        order = list(stamps)
        points = {p: events_block(events, stamps[p] - offset, p) for p in order}
    else:
        if not a.dump:
            ap.error("a DUMP or --events")
        points, order = read_dump(a.dump)
    want = a.point or order
    tables = []
    for p in want:
        if p not in points:
            print("point %s: not in the dump (have: %s)" % (p, ", ".join(order)), file=sys.stderr)
            return 1
        tables.append(fold(points[p], memgpu.get(p)))
    if a.json:
        for t in tables:
            for r in t["rows"].values():
                r["details"] = {k: {"count": v[0], "bytes": v[1]} for k, v in r["details"].items()}
        json.dump(tables, sys.stdout, indent=1)
        print()
        return 0
    for t in tables:
        print_table(t, a.detail, a.top)
    return 0


if __name__ == "__main__":
    sys.exit(main())
