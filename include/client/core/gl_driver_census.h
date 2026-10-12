// B946 (GitLab #264): the [gldriver] line -- which GL driver the process
// REALLY loaded, from its own /proc/self/maps, at GL init.
//
// The problem: a willeq-nollvm binary (B935, #253) selects the shipped
// LLVM-less Mesa by an absolute DT_RPATH. If that directory is absent (a
// fresh Pi, a changed version, a hand run without deploy_mesa_prefix.sh),
// ld.so falls through to the default search path and the process loads the
// SYSTEM Mesa -- libLLVM included, ~30 MiB of RssFile more -- with no error
// at all. Only the Pi rigs caught it (mesa_mismatch, exit 67, from their own
// maps snapshots); a hand run, or any other launcher, had no net, and the
// client logged neither the driver library it mapped nor the GL renderer.
//
// This header is the device-free half: the line format, its parser (fixture-
// tested, no GL) and the missing-prefix decision. The /proc read and the
// one-time emission at GL init live in src/client/core/gl_driver_census.cpp.
//
// The gallium/libLLVM needles are found by a SECOND, minimal /proc parser:
// scanMapsText reads /proc/self/maps (the file is a list of mappings), which
// is a different file from the [memmap] census's /proc/self/smaps read
// (memory_ledger.cpp, T491's parseSmaps fold) -- the shape overlaps (a
// whitespace-delimited column layout, pathname last) but the consumers and
// the parsers are separate, on purpose: this one is two needles and never
// grows. scripts/pi/rendered_zone_summarize.py reads this line when present
// and reports driver= alongside its maps-snapshot proof (its ctest,
// RenderedZoneSummarizeGlDriver, is the rig's half of the net).

#ifndef CLIENT_CORE_GL_DRIVER_CENSUS_H
#define CLIENT_CORE_GL_DRIVER_CENSUS_H

#include <string>

namespace eqt {
namespace memory {

// One GL init's answer to "which driver did I load?".
struct GlDriverInfo {
    std::string renderer;  // GL_RENDERER, "" before a GL context exists
    std::string gallium;   // the mapped libgallium path, or "none"
    std::string llvm;      // the mapped libLLVM path, or "none"
    bool ok = false;       // false only where the caller says so (parse)
};

// The nollvm marker: the prefix NAME (the B935 convention,
// scripts/pi/mesa_nollvm.env) is `mesa-nollvm-<version>`, and the DT_RPATH
// the publish step bakes in is that prefix's lib directory -- so a rpath
// entry containing it is the binary saying "I was built against the shipped
// LLVM-less Mesa prefix". A plain willeq (no rpath) or any other rpath never
// matches.

// The RPATH/RUNPATH string of an ELF (the DT_RPATH/DT_RUNPATH of its
// .dynamic section, full colon-separated value), resolved from the bytes
// through the PT_LOAD segments (d_val is a vaddr). "" for a non-ELF, an ELF
// without a dynamic section, no rpath entry, a rpath value no LOAD segment
// covers, or a string that does not NUL-terminate in range. Fixture-
// testable without a GL context; the /proc/self/exe read that feeds the
// production call is `glPrefixExpected`'s job.
std::string parseElfRpath(const std::string& bytes);

// Pure half of `glPrefixExpected`: the nollvm prefix directory out of a
// full rpath string -- the first colon entry containing "mesa-nollvm-".
// B1016 (#337): returned AS-IS, because the publish step
// (scripts/pi/mesa_nollvm_select.sh) bakes in `<prefix>/lib` and the
// decision (mesaPrefixMissing) must test THAT directory -- the lib dir
// itself, where ld.so actually looks. (The old contract stripped a
// trailing "/lib" and the check ran against the parent, so a partial
// deploy that kept the prefix dir but lost lib/ logged no error.) "" when
// no entry carries the marker (a non-nollvm binary).
std::string nollvmPrefixFromRpath(const std::string& rpath);

// The prefix's lib directory this process was built against, from its OWN
// dynamic section -- "" when the process is not a nollvm build (no rpath,
// or no entry that names the prefix). Read once at GL init, like everything
// else in this header. A non-empty answer with a missing directory is the
// decision's trigger; whether the directory exists on THIS machine is
// mesaPrefixMissing's job (and the test rig makes its prefix dir real, so a
// fresh test run stays clean).
std::string glPrefixExpected();

// `parseGlDriverLine` over ONE log line (or any text): the first
// `[gldriver] renderer='...' gallium=... llvm=...` it finds, else ok=false.
// The timestamp/level/module prefix is part of real client.log lines, so it
// is expected and skipped, not required. `llvm=` and `gallium=` accept a
// bare path or the word "none"; a renderer is everything between the single
// quotes, spaces included.
GlDriverInfo parseGlDriverLine(const std::string& text);

// The EXACT line the client emits (no prefix): the contract the rig's
// summarizer and every future consumer parse against.
std::string makeGlDriverLine(const GlDriverInfo& info);

// The one-time emission. `renderer` is "" before a GL context exists. The
// /proc/self/maps read costs one small open+read+close and is never per
// frame; on non-Linux platforms it reports "none"/"none" with ok=true.
GlDriverInfo glDriverInfo(const std::string& renderer);

// Pure half of `glDriverInfo`: the two needles out of a (chunk of)
// /proc/self/maps text. `gallium` and `llvm` are overwritten with the LAST
// mapped path matching libgallium*/libLLVM* ("" if none). Fixture-testable
// without any /proc: the field layout (six whitespace-separated fields,
// pathname LAST) is exactly what the fixtures pin.
void scanMapsText(const std::string& text, std::string& gallium, std::string& llvm);

// The B946 decision (issue #264 acceptance 2), applied: the prefix's lib
// directory the process was built against comes from ITS OWN dynamic
// section (glPrefixExpected, above) -- the publish step,
// scripts/pi/mesa_nollvm_select.sh, is the one place that knows it (the
// same absolute path patchelf writes into DT_RPATH), so no build-time
// define is involved. If that directory does not exist at start, the
// client logs an ERROR naming the directory and the fallback it will take
// (the default search path, i.e. the system Mesa), and CONTINUES: a hand
// run on a fresh Pi still has to work, and the rigs already fail the run
// (mesa_mismatch, exit 67). Returns true -- "log the ERROR" -- exactly when
// the directory is non-empty and does not exist. A non-nollvm binary
// answers "" (no such rpath) and always gets false.
bool mesaPrefixMissing(const std::string& prefixLibDir);

}  // namespace memory
}  // namespace eqt

#endif  // CLIENT_CORE_GL_DRIVER_CENSUS_H
