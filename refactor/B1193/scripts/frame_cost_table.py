#!/usr/bin/env python3
# B1193 (#512): the per-phase frame-cost table from the harness's
# frame_cost.tsv (tools/model_viewer/frame_writer.cpp writes it, one row per
# frame: frame, render_ms, readback_ms, downscale_ms, encode_ms, write_ms).
#
# render_ms   render thread: beginScene -> endScene (the GPU work)
# readback_ms render thread: createScreenShot (the synchronous glReadPixels)
# downscale_ms / encode_ms / write_ms
#             the writer thread (the B1193 default path), or the render
#             thread with --frames-sync
#
# Usage:
#   python3 frame_cost_table.py <frame_cost.tsv> [<frame_cost.tsv> ...]
# prints one block per file: the per-phase averages in ms/frame, the render
# thread's ms/frame (render + readback), the output work's ms/frame
# (downscale + encode + write) and the implied frames/s of each.
import sys

def main():
    for path in sys.argv[1:]:
        rows = []
        with open(path) as f:
            header = f.readline().rstrip("\n").split("\t")
            for line in f:
                c = line.rstrip("\n").split("\t")
                if len(c) != len(header):
                    continue
                rows.append({k: float(v) for k, v in zip(header, c)})
        if not rows:
            print(f"{path}: no rows")
            continue
        n = len(rows)
        avg = {k: sum(r[k] for r in rows) / n for k in header if k != "frame"}
        render_thread = avg["render_ms"] + avg["readback_ms"]
        output = avg["downscale_ms"] + avg["encode_ms"] + avg["write_ms"]
        total = render_thread + output
        print(f"== {path} ({n} frames) ==")
        for k in ("render_ms", "readback_ms", "downscale_ms", "encode_ms", "write_ms"):
            print(f"  {k:<13} {avg[k]:9.3f} ms/f")
        print(f"  render thread {render_thread:9.3f} ms/f "
              f"({1000.0 / render_thread if render_thread else 0:.1f} fps ceiling)")
        print(f"  output work   {output:9.3f} ms/f")
        print(f"  whole frame   {total:9.3f} ms/f "
              f"({1000.0 / total if total else 0:.1f} fps if it all sat on one thread)")

if __name__ == "__main__":
    main()
