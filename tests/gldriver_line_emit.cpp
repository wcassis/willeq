// B1016 (#337): the [gldriver] line's FORMAT CONTRACT, made observable.
//
// The C++ emitter (makeGlDriverLine, gl_driver_census.cpp) and the Python
// parser (scripts/pi/rendered_zone_summarize.py's mesa_driver_field) used to
// be pinned only against each suite's OWN hand-written strings: change the
// emitter's format and every test stays green (the audit's finding 3, MR
// !127). This tool prints makeGlDriverLine(info) for a caller-supplied
// GlDriverInfo, so a test can feed the EMITTER'S OWN OUTPUT through the
// Python parser and assert the round trip (scripts/ci/
// test_gldriver_line_format.py, ctest GldriverLineFormatContract).
//
// Usage:
//   gldriver_line_emit [--renderer S] [--gallium S] [--llvm S]
//
// An omitted field is the emitter's own empty answer (printed as "none").
// stdout carries the line and nothing else.
#include "client/core/gl_driver_census.h"

#include <cstring>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    eqt::memory::GlDriverInfo info;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << flag << "\n";
                std::exit(2);
            }
            return true;
        };
        if (a == "--renderer" && need("--renderer"))
            info.renderer = argv[++i];
        else if (a == "--gallium" && need("--gallium"))
            info.gallium = argv[++i];
        else if (a == "--llvm" && need("--llvm"))
            info.llvm = argv[++i];
        else {
            std::cerr << "unknown argument: " << a << "\n";
            return 2;
        }
    }
    std::cout << eqt::memory::makeGlDriverLine(info) << "\n";
    return 0;
}
