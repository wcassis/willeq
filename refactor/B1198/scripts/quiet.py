#!/usr/bin/env python3
"""B1198 (#517) quiet gate: exit 0 iff load1 < 6.0 AND no other
`model_viewer --spell-test` is running (the recheck matrix's predicate;
a run measured under contention is not a number).

Usage: quiet.py <load1> <other_model_viewer_count>
"""
import sys

load, n = float(sys.argv[1]), int(sys.argv[2])
sys.exit(0 if load < 6.0 and n == 0 else 1)
