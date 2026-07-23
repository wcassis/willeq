#!/usr/bin/env bash
# H02 — CI verification gate entry point: golden-frame + metric + memory
# assertions on top of H01's x86 GLES2 build (build-gles2/).
#
# Usage:
#   scripts/ci/visual_gate.sh           # run the gate against committed goldens
#   scripts/ci/visual_gate.sh --bless   # regenerate goldens + manifest + contact sheet
#
# Exit codes: 0 pass, 1 fail, 125 SKIP (build-gles2/ not present -- matches
# the SKIP_RETURN_CODE registered for the `visual_gate` ctest entry so the
# default test suite stays independent of the GLES2 build).
#
# All the actual logic lives in visual_gate.py (image decode/compare needs
# PIL, which is far more pleasant from Python than bash+ImageMagick alone).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

exec python3 "$SCRIPT_DIR/visual_gate.py" "$@"
