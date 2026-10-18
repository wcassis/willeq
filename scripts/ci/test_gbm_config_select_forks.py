#!/usr/bin/env python3
"""B1034 (#357): the chooseConfigForGbmFormat wrapper must stay byte-identical
in both device forks (docker/irrlicht-drm/ and docker/irrlicht-drm-x86/).
The pure decision lives in the shared header gbm_config_select.h; the wrapper
that calls it (count-query, attribute readout, call, warning, return) is
device code, copied by hand into each fork. If the two wrappers drift, one
lane silently runs a different EGL-config choice than the other.

Extract the function from each fork and diff. Exit 0 = identical, 1 = drift.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
ARM = REPO / "docker/irrlicht-drm/CIrrDeviceFB.cpp"
X86 = REPO / "docker/irrlicht-drm-x86/CIrrDeviceFB.cpp"

def extract_wrapper(path: Path) -> str:
    """The text from the `static bool chooseConfigForGbmFormat` line to its
    closing `}` (depth-balanced)."""
    text = path.read_text()
    lines = text.splitlines(keepends=True)
    start = None
    for i, line in enumerate(lines):
        if line.startswith("static bool chooseConfigForGbmFormat"):
            start = i
            break
    if start is None:
        print(f"FAIL: chooseConfigForGbmFormat not found in {path}", file=sys.stderr)
        sys.exit(1)
    depth = 0
    started = False
    for i in range(start, len(lines)):
        depth += lines[i].count("{") - lines[i].count("}")
        if "{" in lines[i]:
            started = True
        if started and depth == 0:
            return "".join(lines[start:i + 1])
    print(f"FAIL: unbalanced braces from chooseConfigForGbmFormat in {path}", file=sys.stderr)
    sys.exit(1)

arm_wrapper = extract_wrapper(ARM)
x86_wrapper = extract_wrapper(X86)

if arm_wrapper == x86_wrapper:
    print(f"PASS: chooseConfigForGbmFormat wrapper identical in both forks "
          f"({len(arm_wrapper.splitlines())} lines)")
    sys.exit(0)
else:
    import difflib
    diff = difflib.unified_diff(
        arm_wrapper.splitlines(keepends=True),
        x86_wrapper.splitlines(keepends=True),
        fromfile="docker/irrlicht-drm/CIrrDeviceFB.cpp",
        tofile="docker/irrlicht-drm-x86/CIrrDeviceFB.cpp",
        lineterm="",
    )
    print("FAIL: chooseConfigForGbmFormat wrapper differs between forks:", file=sys.stderr)
    for line in diff:
        print(line, file=sys.stderr)
    sys.exit(1)
