#!/usr/bin/env bash
# B1094 (#414) -- the installed-vs-pin gate, hard ONLY on fable.
#
# Runs scripts/build_willeq_pack.sh --check-installed (installed pack vs
# data/willeq.eqe1.sha256, no build) and converts its verdict:
#   - on fable (CI_COMMIT_BRANCH=fable) a mismatch is a hard FAIL -- a fable
#     tip whose pin no longer matches the installed shared pack means a
#     merge changed the pin and the orchestrator has not installed yet, and
#     the pipeline must say so loudly;
#   - anywhere else a mismatch is printed and PASSED -- a pack-changing
#     branch legitimately carries a new pin against the fable-installed pack,
#     and its pipeline stays green on its own (the old behaviour coupled
#     every branch to one mutable shared file; see issue #414).
#
# Extra args are forwarded to build_willeq_pack.sh (e.g. --output <path> to
# check a pack other than the installed one, --sha-file for the pin).
set -u

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"

if bash "$REPO_ROOT/scripts/build_willeq_pack.sh" --check-installed "$@"; then
    exit 0
fi

if [ "${CI_COMMIT_BRANCH:-}" = "fable" ]; then
    echo "GATE: failing on fable -- the installed pack does not match the pin."
    exit 1
fi

echo "NOTE: the installed pack does not match this branch's pin, but"
echo "      CI_COMMIT_BRANCH='${CI_COMMIT_BRANCH:-unset}' is not fable -- a"
echo "      pack-changing branch is expected to differ from the installed"
echo "      pack. The orchestrator installs the shared pack at merge (B1094)."
exit 0
