#!/usr/bin/env python3
"""Apply only independently detected matrix recurrences to the shipped TSV."""

import argparse
import csv
from pathlib import Path


def read(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader((line for line in stream if not line.startswith("#")), delimiter="\t"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--table", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    rows = read(args.table)
    detected = {(r["race"], r["gender"], r["picker"]): r for r in read(args.report)}
    assert len(rows) == len(detected)
    verified = 0
    for row in rows:
        key = row["race"], row["gender"], row["picker"]
        count = detected[key]["detected_count"]
        if row["visible"] == "1" and count.isdecimal() and int(count) == int(row["max"]) - int(row["min"]) + 1:
            row["count_measured"] = count
            row["status"] = "COUNT-MEASURED+SAMPLED" if row["status"] == "COUNT-MEASURED+SAMPLED" else "COUNT-MEASURED"
            verified += 1
        else:
            row["count_measured"] = "UNMEASURED"
            row["status"] = "UNMEASURED"
    with args.table.open("w", newline="") as stream:
        stream.write("# Numeric endpoints are server candidates; COUNT-MEASURED requires independent image recurrence. See refactor/B888-CC1/README.md.\n")
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    print(f"{verified} independently detected counts; {len(rows) - verified} UNMEASURED")


if __name__ == "__main__":
    main()
