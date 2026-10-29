#!/usr/bin/env python3
# B1183: merge the leg's steps.tsv (step epochs) with the /proc/pid/mem
# poller log (INIT/WR lines, t = seconds from attach, START epoch = attach
# wall time) into the per-step oracle table: the byte at each step end and
# the value-changing writes (WR) that landed inside the step, beside the
# step's still, packet rows and pcap packet-count window.
import csv
import re
import sys


def main():
    leg = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else sys.stdout

    # Poller log.
    wr = []          # (t_rel, frm, to)
    init = None
    start_epoch = None
    spawn = None
    keymap = []
    with open(f"{leg}/poll/poll.log") as f:
        for line in f:
            m = re.match(r"START epoch=([\d.]+) ", line)
            if m:
                start_epoch = float(m.group(1))
            m = re.match(r"INIT byte \+0x251 = (\d+)", line)
            if m:
                init = int(m.group(1))
            m = re.match(r"WR t=\s*([\d.]+) (\S+) -> (\d+)  spawn=0x[0-9a-f]+", line)
            if m:
                wr.append((float(m.group(1)), m.group(2), int(m.group(3))))
                spawn = m.group(0)
            if line.startswith("KEYMAP 0x"):
                keymap.append(line.strip())

    if start_epoch is None:
        sys.exit("poll.log has no START epoch line -- wrong poller version?")

    rows = []
    with open(f"{leg}/steps.tsv") as f:
        for r in csv.DictReader(f, delimiter="\t"):
            b, e = float(r["begin_epoch"]), float(r["end_epoch"])
            inside = [(t, frm, to) for (t, frm, to) in wr
                      if b <= start_epoch + t < e]
            prior = [w for w in wr if start_epoch + w[0] < e]
            byte = prior[-1][2] if prior else init
            rows.append({
                "step": r["step"],
                "begin": r["begin_epoch"],
                "end": r["end_epoch"],
                "action": r["action"],
                "target": r["target"],
                "still": r["still"],
                "byte_end": byte if byte is not None else "",
                "wr": "; ".join(
                    f"t+{t:.3f}({start_epoch + t:.3f}) {frm}->{to}"
                    for (t, frm, to) in inside),
                "packet_rows": r["packet_rows"],
            })

    w = open(out, "w") if out != sys.stdout else out
    w.write("attach_epoch\t%s\n" % start_epoch)
    w.write("init_byte\t%s\n" % init)
    for k in keymap:
        w.write(k + "\n")
    hdr = ["step", "begin", "end", "action", "target", "still",
           "oracle_byte_end", "wr_inside_step", "packet_rows"]
    w.write("\t".join(hdr) + "\n")
    for r in rows:
        w.write("\t".join([
            r["step"], r["begin"], r["end"], r["action"], r["target"],
            r["still"], str(r["byte_end"]), r["wr"],
            r["packet_rows"],
        ]) + "\n")
    if out != sys.stdout:
        w.close()
        print("wrote", out)


if __name__ == "__main__":
    main()
