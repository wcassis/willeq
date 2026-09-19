#!/usr/bin/env bash
# T503 -- pins the Pi lock contract against the real Pi (needs ssh reach).
# Exit 0 = PASS, 64 = Pi unreachable or lock held by a real owner (not run).
set -u
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
fail() { echo "FAIL: $*"; pi_lock_release selftest-a >/dev/null 2>&1; exit 1; }
pi_remote true 2>/dev/null || { echo "SKIP: Pi unreachable"; exit 64; }
held=$(pi_lock_show) && { echo "SKIP: lock held: $held"; exit 64; }
pi_lock_take selftest-a            || fail "take on a free lock"
pi_lock_take selftest-b            && fail "second take must be refused"
pi_lock_show | grep -q "^selftest-a " || fail "show names the owner"
pi_lock_release selftest-b; pi_lock_show >/dev/null || fail "a non-owner released the lock"
pi_lock_release selftest-a; pi_lock_show >/dev/null && fail "owner release left the lock"
echo "PASS: pi lock take/refuse/show/owner-only-release"
