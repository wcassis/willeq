#!/usr/bin/env python3
"""B952: tabulate impostor_run.sh logs (one row per run) as markdown + CSV.

usage: summarize.py <run-dir> [--csv out.csv]

Columns come from the benchmark's own lines, nothing is estimated:
  [memledger-owner] ... owner=tex.impostor live= textures= claimants= size= mode= fbo=
  [impostor-mem]    ... gpu_driver_delta= textures_driver_delta= rss_anon_delta= rss_shmem_delta=
                        sys_shmem_delta_kb= gpu_texture_bytes_delta= drm-*_delta=
  [impostor-timing] ... create_ms_per_impostor= refresh_ms_per_pass= refresh_ms_per_frame=
                        tier1_avg= tier2_avg= frame_p50_ms= frame_p95_ms=
                        frame_plus_prepass_p50_ms= frame_plus_prepass_p95_ms=
  [memledger] point=steady ... rss_anon= gpu_driver= owned= textures_driver=<live>/<peak>
and the .maps proof (libgallium / libLLVM mapped).
"""
import csv
import os
import re
import sys

KV = re.compile(r"(\S+?)=(\S+)")


def kv(line):
    # first occurrence wins: the steady [memledger] line carries gpu_driver twice
    # (the snapshot total, then the category's live/peak)
    out = {}
    for k, v in KV.findall(line):
        out.setdefault(k, v)
    return out


def parse(log):
    row = {}
    with open(log, errors="replace") as f:
        for line in f:
            if line.startswith("[memledger-owner]"):
                d = kv(line)
                row.update(row_bytes=int(d["live"]), textures=int(d["textures"]),
                           texture_bytes=int(d["texture_bytes"]), scratch_bytes=int(d["scratch_bytes"]),
                           claimants=int(d["claimants"]), size=int(d["size"]), mode=d["mode"],
                           fbo=d["fbo"])
            elif line.startswith("[impostor-mem]"):
                d = kv(line)
                for k in ("gpu_driver_delta", "textures_driver_delta", "rss_anon_delta",
                          "rss_shmem_delta", "rss_file_delta", "sys_shmem_delta_kb",
                          "gpu_texture_bytes_delta"):
                    row[k] = int(d.get(k, 0))
                row["drm"] = " ".join(f"{k}={v}" for k, v in d.items() if k.startswith("drm-"))
            elif line.startswith("[impostor-timing]"):
                d = kv(line)
                for k in ("create_ms_per_impostor", "refresh_ms_per_pass", "refresh_ms_per_frame",
                          "tier1_avg", "tier2_avg", "frame_p50_ms", "frame_p95_ms",
                          "frame_plus_prepass_p50_ms", "frame_plus_prepass_p95_ms"):
                    row[k] = float(d[k])
                row["created"] = int(d["created"])
                row["incomplete_fbo"] = int(d.get("incomplete_fbo", 0))
                row["refresh_every"] = int(d["refresh_every"])
            elif line.startswith("[memledger] point=steady"):
                d = kv(line)
                row["rss_anon"] = int(d["rss_anon"])
                row["gpu_driver"] = int(d["gpu_driver"])
                row["owned"] = int(d["owned"])
                row["textures_driver"] = int(d["textures_driver"].split("/")[0])
            elif line.startswith("  [variants] loaded="):
                row["variants"] = int(line.split("loaded=")[1].split()[0])
    return row


def mib(b):
    return f"{b / 1048576:.2f}"


def main():
    d = sys.argv[1]
    out_csv = sys.argv[sys.argv.index("--csv") + 1] if "--csv" in sys.argv else None
    rows = []
    for name in sorted(os.listdir(d)):
        if not name.endswith(".log"):
            continue
        label = name[:-4]
        r = parse(os.path.join(d, name))
        r["label"] = label
        maps = os.path.join(d, label + ".maps")
        if os.path.exists(maps):
            txt = open(maps).read()
            r["gallium"] = txt.count("libgallium")
            r["llvm"] = txt.count("libLLVM")
        rows.append(r)
    cols = ["label", "textures", "claimants", "row_bytes", "gpu_texture_bytes_delta",
            "gpu_driver_delta", "textures_driver_delta", "rss_anon_delta", "rss_shmem_delta",
            "sys_shmem_delta_kb", "rss_anon", "gpu_driver", "owned", "textures_driver",
            "create_ms_per_impostor", "refresh_ms_per_pass", "refresh_ms_per_frame", "tier1_avg",
            "tier2_avg", "frame_p50_ms", "frame_p95_ms", "frame_plus_prepass_p95_ms", "llvm", "incomplete_fbo",
            "drm"]
    print("| run | tex | claim | row MiB | drv tex tally MiB | d gpu_driver MiB | d textures_driver KiB "
          "| d RssAnon MiB | d RssShmem MiB | d sys Shmem MiB | RssAnon MiB | gpu_driver MiB | owned MiB "
          "| create ms/imp | refresh ms/pass | tier1/tier2 | p50 / p95 ms | p95 +prepass | llvm | bad FBO |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        if "row_bytes" not in r:
            print(f"| {r['label']} | (no impostor lines -- see its log) |")
            continue
        print(f"| {r['label']} | {r['textures']} | {r['claimants']} | {mib(r['row_bytes'])} | "
              f"{mib(r['gpu_texture_bytes_delta'])} | {mib(r['gpu_driver_delta'])} | "
              f"{r['textures_driver_delta'] / 1024:.1f} | {mib(r['rss_anon_delta'])} | "
              f"{mib(r['rss_shmem_delta'])} | {r['sys_shmem_delta_kb'] / 1024:.2f} | "
              f"{mib(r.get('rss_anon', 0))} | {mib(r.get('gpu_driver', 0))} | {mib(r.get('owned', 0))} | "
              f"{r['create_ms_per_impostor']:.3f} | {r['refresh_ms_per_pass']:.1f} | "
              f"{r['tier1_avg']:.1f}/{r['tier2_avg']:.1f} | {r['frame_p50_ms']:.1f} / "
              f"{r['frame_p95_ms']:.1f} | {r['frame_plus_prepass_p95_ms']:.1f} | {r.get('llvm', '?')} | "
              f"{r.get('incomplete_fbo', '?')} |")
    if out_csv:
        with open(out_csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
            w.writeheader()
            for r in rows:
                w.writerow(r)


if __name__ == "__main__":
    main()
