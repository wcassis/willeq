#!/usr/bin/env python3
"""B1016: the [gldriver] line's format contract, one shared input.

The audit of MR !127 (B1016, #337) found that the C++ EMITTER
(makeGlDriverLine, src/client/core/gl_driver_census.cpp) and the Python
PARSER (scripts/pi/rendered_zone_summarize.py's mesa_driver_field) were
pinned only against each suite's own hand-written strings: change the
emitter's format and every test stays green. This test removes that hole:
it runs the gldriver_line_emit tool (tests/gldriver_line_emit.cpp --
makeGlDriverLine's own stdout) and feeds its line to mesa_driver_field,
so the two formats cannot diverge silently.

Cases:
  1. A realistic llvmpipe line (system Mesa paths, a renderer string with
     spaces and parens): the parser must recover renderer:gallium:llvm
     exactly.
  2. The no-LLVM answer (gallium=none llvm=none): recovered as the words
     "none".
  3. A GL_RENDERER containing a single quote ("it's") CANNOT round-trip:
     the emitter writes it verbatim and the parser sees an unbalanced
     quote. The test pins that LIMIT on both sides (documented, not
     escaped -- no real GL_RENDERER string contains a ' ).
  4. The line wrapped in a real log prefix (timestamp/level/module) parses
     the same -- the parser skips the prefix.
  5. A log without the line reports empty.
"""
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.environ.get("WILLEQ_SOURCE_DIR",
                      os.path.dirname(os.path.dirname(HERE)))
_BUILD_DIR = os.environ.get("WILLEQ_BUILD_DIR")
TOOL = (os.path.join(_BUILD_DIR, "bin", "gldriver_line_emit") if _BUILD_DIR
        else os.path.join(REPO, "build", "bin", "gldriver_line_emit"))

SUMMARIZER = os.path.join(REPO, "scripts", "pi",
                          "rendered_zone_summarize.py")


def load_summarizer():
    # rendered_zone_summarize imports owned_memory (scripts/ci) and
    # gem_owner_table (scripts/pi): make both findable, the way the rig's
    # own test (scripts/pi/test_rendered_zone_gl_driver.py) runs it from
    # scripts/pi/.
    os.sys.path.insert(0, os.path.join(HERE, "..", "ci"))
    os.sys.path.insert(0, os.path.join(HERE, "..", "pi"))
    spec = importlib.util.spec_from_file_location(
        "rendered_zone_summarize", SUMMARIZER)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def emit(tool, *fields):
    """Run gldriver_line_emit with --k v pairs; return its stdout line."""
    args = [tool]
    for k, v in fields:
        args += ["--" + k, v]
    p = subprocess.run(args, capture_output=True, text=True)
    if p.returncode != 0:
        print(f"gldriver_line_emit failed ({p.returncode}): {p.stderr}")
        sys.exit(1)
    return p.stdout.strip()


def main():
    if not os.path.isfile(TOOL):
        print(f"SKIP (B1016 format contract): gldriver_line_emit not built at "
              f"{TOOL} (cmake --build build --target gldriver_line_emit)")
        return 125
    summary = load_summarizer()
    failures = []

    # 1. A realistic llvmpipe line (the x86 run's real shape).
    line = emit(TOOL,
                ("renderer", "llvmpipe (LLVM 20.1.2, 256 bits)"),
                ("gallium", "/usr/lib/x86_64-linux-gnu/libgallium-25.2.8.so"),
                ("llvm", "/usr/lib/x86_64-linux-gnu/libLLVM.so.20.1"))
    got = summary.mesa_driver_field([line])
    want = ("llvmpipe (LLVM 20.1.2, 256 bits)"
            ":/usr/lib/x86_64-linux-gnu/libgallium-25.2.8.so"
            ":/usr/lib/x86_64-linux-gnu/libLLVM.so.20.1")
    if got != want:
        failures.append(f"realistic line: parser saw {got!r}, want {want!r}")

    # 2. The no-LLVM answer: the word "none" round-trips as itself.
    line = emit(TOOL, ("renderer", "Mali400"),
                ("gallium", "none"), ("llvm", "none"))
    got = summary.mesa_driver_field([line])
    if got != "Mali400:none:none":
        failures.append(f"none/none line: parser saw {got!r}")

    # 3. A GL_RENDERER containing a single quote CANNOT round-trip: the
    #    emitter writes it verbatim (renderer='it's a renderer'), the parser
    #    stops at the FIRST quote, so parseGlDriverLine rejects the line
    #    (no closing quote) and mesa_driver_field finds nothing. The test
    #    pins the LIMIT on BOTH sides: a renderer with ' is out of format
    #    (no real GL_RENDERER string contains one -- Mesa's are paren/
    #    comma/space; documenting it instead of inventing an escape).
    line = emit(TOOL, ("renderer", "it's a renderer"),
                ("gallium", "/a/libgallium.so"), ("llvm", "none"))
    if summary.mesa_driver_field([line]) != "":
        failures.append(
            "a renderer containing ' must NOT parse (first quote wins): "
            "the emitter writes it verbatim and the parser sees an "
            "unbalanced quote")

    # 3b. Every renderer string the parser can handle, round-trips: the
    #     contract is byte-exact for the parseable class.
    line = emit(TOOL, ("renderer", "Mali400 (Lima, 4.3)"),
                ("gallium", "/x/libgallium-dri.so"), ("llvm", "none"))
    got = summary.mesa_driver_field([line])
    if got != "Mali400 (Lima, 4.3):/x/libgallium-dri.so:none":
        failures.append(f"round-trip: parser saw {got!r}")

    # 4. A real log prefix (timestamp/level/module) is skipped by the parser.
    prefixed = ("2026-10-10 12:00:00.123 [INFO ] [GRAPHICS] " +
                emit(TOOL, ("renderer", "r1"),
                     ("gallium", "/g.so"), ("llvm", "none")))
    got = summary.mesa_driver_field([prefixed])
    if got != "r1:/g.so:none":
        failures.append(f"prefixed line: parser saw {got!r}")

    # 5. A log without the line reports empty (the summarizer's contract).
    if summary.mesa_driver_field(["unrelated log line"]) != "":
        failures.append("a log without the line must give an empty field")

    if failures:
        print("test_gldriver_line_format: FAIL")
        for f in failures:
            print("  " + f)
        return 1
    print("test_gldriver_line_format: PASS "
          "(emitter output round-trips through mesa_driver_field)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
