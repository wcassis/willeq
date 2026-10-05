#!/usr/bin/env python3
"""Validate bounded crowd observation durations before acquiring resources."""
import argparse
import math


def duration(value):
    seconds = float(value)
    if not math.isfinite(seconds) or seconds < 0:
        raise argparse.ArgumentTypeError("duration must be finite and non-negative")
    return seconds


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("idle", type=duration)
    ap.add_argument("hold_after", type=duration)
    ap.add_argument("cap", type=int)
    a = ap.parse_args()
    if a.cap <= 0 or a.idle + a.hold_after >= a.cap:
        ap.error("cap must be positive and exceed idle + hold-after (allow startup/hops too)")


if __name__ == "__main__":
    main()
