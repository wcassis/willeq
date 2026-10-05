#!/usr/bin/env python3
"""Validate bounded crowd observation durations before acquiring resources.

ONE definition of a valid duration for the crowd harnesses' --idle,
--hold-after, --cap, --stall and --hold (B987's QA finding F1): a plain
ASCII decimal -- `0` or `[1-9][0-9]*`, in 0..86400 seconds -- the form Bash
arithmetic and `timeout` both accept. Python's int()/float() also accept
underscores, signs, surrounding whitespace, leading zeros (0800 is an
invalid-octal token in Bash, 0720 is silently octal: the sweep's
`timeout $((CAP_S + 120))` becomes 584 while rendered_zone_run.py is told
720) and non-ASCII digits; every one of those would reach a Bash
comparison and print "integer expression expected" (the overall deadline
silently disabled) instead of a usage error before any lock, stack or Pi
resource is acquired.
"""
import argparse
import re
import sys

MIN_S = 0          # 0 is meaningful where it is today (the test lane)
MAX_S = 86400      # one day: far beyond any workload, trivially small for
                   # Bash arithmetic and `timeout`
RE_NONZERO = re.compile(r"^[1-9][0-9]*$")
RE_ZERO = re.compile(r"^0$")


def duration(value):
    return int(_seconds(value))


def _seconds(value):
    """ONE valid-spelling check; raises on anything that is not a plain
    ASCII decimal inside [MIN_S, MAX_S]."""
    if not (RE_ZERO.match(value) or RE_NONZERO.match(value)):
        raise ValueError(
            "duration must be plain ASCII decimal 0..%d "
            "(no underscores, signs, whitespace, leading zeros, fractions "
            "or non-ASCII digits): %r" % (MAX_S, value))
    seconds = int(value)
    if not MIN_S <= seconds <= MAX_S:
        raise ValueError("duration out of range %d..%d: %r" % (MIN_S, MAX_S, value))
    return seconds


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("idle", type=duration)
    ap.add_argument("hold_after", type=duration)
    ap.add_argument("cap", type=duration)
    a = ap.parse_args()
    if a.cap <= 0 or a.idle + a.hold_after >= a.cap:
        ap.error("cap must be positive and exceed idle + hold-after (allow startup/hops too)")
    # The wrappers consume the CANONICAL integers from this line (one per
    # argument), never the raw tokens; the shell re-checks each with its own
    # regex before doing any arithmetic with it.
    print("%d %d %d" % (a.idle, a.hold_after, a.cap))


if __name__ == "__main__":
    sys.exit(main())
