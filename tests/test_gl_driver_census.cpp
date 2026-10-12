// B946 (GitLab #264): the client never said WHICH GL driver it loaded, and a
// willeq-nollvm binary whose DT_RPATH Mesa prefix is missing fell through to
// the system Mesa (libLLVM included) with no error at all.
//
// The [gldriver] line at GL init names the mapped libgallium (or "none"), the
// GL_RENDERER string, and whether libLLVM is mapped. The gallium/libLLVM paths
// come from /proc/self/maps via a SECOND, minimal maps parser (scanMapsText,
// gl_driver_census.cpp) -- the [memmap] census reads /proc/self/smaps
// (memory_ledger.cpp:720), a different file for T491's smaps fold.
//
// What this binary tests:
//   * parseGlDriverLine -- the format contract the rig's summarizer
//     (scripts/pi/rendered_zone_summarize.py mesa_driver_field, its own ctest)
//     and any future consumer rely on. B1016 (#337): the function itself
//     MOVED INTO THIS file (it was test-only and shipped in the client, a
//     zero-callers census row in a category it does not fit); the format
//     contract with the Python parser is pinned by GldriverLineFormatContract
//     over one shared input.
//   * parseElfRpath / nollvmPrefixFromRpath / glPrefixExpected -- how the
//     nollvm decision finds the prefix's lib directory in THIS process's OWN
//     DT_RPATH (the publish step, scripts/pi/mesa_nollvm_select.sh, writes
//     `<prefix>/lib` into it with patchelf --force-rpath): a hand build, a
//     deployed nollvm binary and a plain willeq binary all answer the same
//     way on any machine -- no build-time define, no new stamp step. The
//     synthetic-ELF fixtures need no GL, no Pi, no readelf.
//   * the missing-prefix DECISION (issue acceptance 2): a nollvm binary whose
//     baked-in prefix directory does not exist logs an ERROR naming the
//     directory and the fallback it will take, and CONTINUES -- a hand run on
//     a fresh Pi must still work; the rigs already fail such a run
//     (mesa_mismatch, exit 67). The decision is driven by a directory that
//     must/must-not exist, so it needs no GL and no Pi.
//   * glDriverInfo -- against THIS process's real /proc/self/maps (Linux):
//     the test binary maps its own loader libraries, so "gallium mapped" is
//     the negative case here and the positive case is pinned by the fixture
//     cases in parseGlDriverLine.

#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <string>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "client/core/gl_driver_census.h"

namespace fs = std::filesystem;
using eqt::memory::GlDriverInfo;
using eqt::memory::glDriverInfo;
using eqt::memory::glPrefixExpected;
using eqt::memory::makeGlDriverLine;
using eqt::memory::mesaPrefixMissing;
using eqt::memory::nollvmPrefixFromRpath;
using eqt::memory::parseElfRpath;
using eqt::memory::scanMapsText;

namespace {

// B1016 (#337): the [gldriver] line's C++ PARSER, moved out of
// src/client/core/gl_driver_census.cpp into THIS test, per the audit
// (finding 3): the function is test-only (zero production callers; the
// rig's consumer is the Python parser
// rendered_zone_summarize.mesa_driver_field), and a test-only free function
// shipped in the client does not fit the census's "seam" category (a test
// hook into a production class). Moved here, the zero-callers census no
// longer sees the declaration at all (it scans include/ and src/ only) and
// the exemption row was deleted from scripts/refactor/check_zero_callers.py.
// Behaviour is byte-for-byte the B946 body; the format contract with the
// Python parser stays pinned by GldriverLineFormatContract over one shared
// input (the emitter's own stdout). Known LIMIT, pinned below: a renderer
// containing a single quote cannot round-trip.
GlDriverInfo parseGlDriverLine(const std::string& text) {
    GlDriverInfo info;
    const std::string marker = "[gldriver] renderer='";
    const size_t pos = text.find(marker);
    if (pos == std::string::npos) return info;
    const size_t start = pos + marker.size();
    const size_t end = text.find('\'', start);
    if (end == std::string::npos) return info;
    info.renderer = text.substr(start, end - start);

    auto kv = [&](const char* key) -> std::string {
        const std::string tag = std::string(" ") + key + "=";
        const size_t p = text.find(tag, end + 1);
        if (p == std::string::npos) return std::string();
        size_t q = p + tag.size();
        while (q < text.size() && text[q] != ' ' && text[q] != '\n' && text[q] != '\r') ++q;
        return text.substr(p + tag.size(), q - (p + tag.size()));
    };
    const std::string g = kv("gallium");
    const std::string l = kv("llvm");
    if (g.empty() || l.empty()) return GlDriverInfo();
    info.gallium = g;
    info.llvm = l;
    info.ok = true;
    return info;
}

std::string writeLine(const fs::path& dir, const char* name, const std::string& body) {
    const fs::path p = dir / name;
    std::ofstream f(p);
    f << body;
    f.close();
    return p.string();
}

// A synthetic little-endian ELF with a DT_RPATH in its .dynamic section:
// 64-byte header, two phdrs at 0x40 (a PT_LOAD identity-mapping the whole
// file, and a PT_DYNAMIC), the two dynamic entries (DT_RPATH, DT_NULL),
// then the rpath string.
std::string makeDynElf(bool is64, const std::string& rpath) {
    auto u16 = [](unsigned v) { return std::string{char(v), char(v >> 8)}; };
    auto u32 = [](unsigned v) {
        return std::string{char(v), char(v >> 8), char(v >> 16), char(v >> 24)};
    };
    auto u64 = [](unsigned long long v) {
        std::string s;
        for (int i = 0; i < 8; ++i) s.push_back(char(v >> (8 * i)));
        return s;
    };
    const size_t phentsize = is64 ? 56 : 32;
    const size_t dynent = is64 ? 16 : 8;
    const size_t dynStart = 0x40 + 2 * phentsize;
    const size_t strOff = dynStart + 2 * dynent;
    const size_t fileSize = strOff + rpath.size() + 1;
    std::string e(fileSize, char(0));
    e.insert(0, "\x7f""ELF");
    e[4] = is64 ? char(2) : char(1);
    e[5] = char(1);                 // little-endian
    e[6] = char(1);
    e.replace(16, 2, u16(is64 ? 2 : 3));
    e.replace(18, 2, u16(0x28));
    e.replace(20, 4, u32(1));
    if (is64) {
        e.replace(24, 8, u64(0));   // e_entry
        e.replace(32, 8, u64(0x40)); // e_phoff
        e.replace(40, 8, u64(0));   // e_shoff
    } else {
        e.replace(24, 4, u32(0));
        e.replace(28, 4, u32(0x40));
        e.replace(32, 4, u32(0));
    }
    e.replace(is64 ? 48 : 36, 4, u32(0));  // e_flags
    e.replace(is64 ? 52 : 40, 2, u16(0x38));
    e.replace(is64 ? 54 : 42, 2, u16((unsigned)phentsize));
    e.replace(is64 ? 56 : 44, 2, u16(2));  // two phdrs
    // phdr[0]: PT_LOAD, the whole file, vaddr == offset (identity).
    const size_t h0 = 0x40;
    if (is64) {
        e.replace(h0, 4, u32(1)); e.replace(h0 + 4, 4, u32(5));
        e.replace(h0 + 8, 8, u64(0)); e.replace(h0 + 16, 8, u64(0));
        e.replace(h0 + 24, 8, u64(0)); e.replace(h0 + 32, 8, u64(fileSize));
        e.replace(h0 + 40, 8, u64(fileSize)); e.replace(h0 + 48, 8, u64(4));
    } else {
        e.replace(h0, 4, u32(1));
        e.replace(h0 + 4, 4, u32(0)); e.replace(h0 + 8, 4, u32(0));
        e.replace(h0 + 12, 4, u32(0)); e.replace(h0 + 16, 4, u32((unsigned)fileSize));
        e.replace(h0 + 20, 4, u32((unsigned)fileSize)); e.replace(h0 + 24, 4, u32(4));
    }
    // phdr[1]: PT_DYNAMIC at dynStart.
    const size_t h1 = 0x40 + phentsize;
    if (is64) {
        e.replace(h1, 4, u32(2)); e.replace(h1 + 4, 4, u32(6));
        e.replace(h1 + 8, 8, u64(dynStart)); e.replace(h1 + 16, 8, u64(dynStart));
        e.replace(h1 + 24, 8, u64(0)); e.replace(h1 + 32, 8, u64(2 * dynent));
        e.replace(h1 + 40, 8, u64(2 * dynent)); e.replace(h1 + 48, 8, u64(8));
    } else {
        e.replace(h1, 4, u32(2));
        e.replace(h1 + 4, 4, u32((unsigned)dynStart)); e.replace(h1 + 8, 4, u32((unsigned)dynStart));
        e.replace(h1 + 12, 4, u32(0)); e.replace(h1 + 16, 4, u32((unsigned)(2 * dynent)));
        e.replace(h1 + 20, 4, u32((unsigned)(2 * dynent))); e.replace(h1 + 24, 4, u32(4));
    }
    // The dynamic entries: DT_RPATH (tag 1) -> strOff (vaddr == offset), DT_NULL.
    size_t d = dynStart;
    if (is64) {
        e.replace(d, 4, u32(1)); e.replace(d + 8, 8, u64(strOff));
        d += 16;
        e.replace(d, 4, u32(0)); e.replace(d + 8, 8, u64(0));
    } else {
        e.replace(d, 4, u32(1)); e.replace(d + 4, 4, u32((unsigned)strOff));
        d += 8;
        e.replace(d, 4, u32(0)); e.replace(d + 4, 4, u32(0));
    }
    e.replace(strOff, rpath.size(), rpath);
    return e;
}

}  // namespace

TEST(GlDriverCensus, ParseWellFormedLine) {
    const std::string line = "2026-10-10 12:00:00.123 [INFO ] [GRAPHICS] "
                             "[gldriver] renderer='llvmpipe (Mesa 25.1.7-1)' "
                             "gallium=/usr/lib/x86_64-linux-gnu/libgallium-25.2.8.so llvm=/usr/lib/x86_64-linux-gnu/libLLVM.so.18.1";
    const GlDriverInfo info = parseGlDriverLine(line);
    ASSERT_TRUE(info.ok);
    EXPECT_EQ(info.renderer, "llvmpipe (Mesa 25.1.7-1)");
    EXPECT_EQ(info.gallium, "/usr/lib/x86_64-linux-gnu/libgallium-25.2.8.so");
    EXPECT_EQ(info.llvm, "/usr/lib/x86_64-linux-gnu/libLLVM.so.18.1");
}

TEST(GlDriverCensus, ParseNoLlvmIsNone) {
    const std::string line = "2026-10-10 12:00:00.000 [ERROR] [GRAPHICS] "
                             "[gldriver] renderer='Mali400' "
                             "gallium=/home/user/mesa-nollvm-25.2.8/lib/libgallium-25.2.8.so llvm=none";
    const GlDriverInfo info = parseGlDriverLine(line);
    ASSERT_TRUE(info.ok);
    EXPECT_EQ(info.renderer, "Mali400");
    EXPECT_EQ(info.gallium, "/home/user/mesa-nollvm-25.2.8/lib/libgallium-25.2.8.so");
    EXPECT_EQ(info.llvm, "none");
}

TEST(GlDriverCensus, ParseMultipleLinesTakesFirst) {
    const std::string a = "2026-10-10 12:00:00.000 [INFO ] [GRAPHICS] "
                          "[gldriver] renderer='a' gallium=/a/so llvm=none";
    const std::string b = "2026-10-10 12:00:01.000 [INFO ] [GRAPHICS] "
                          "[gldriver] renderer='b' gallium=none llvm=none";
    const std::string text = a + "\n" + b + "\n";
    const GlDriverInfo info = parseGlDriverLine(text);
    ASSERT_TRUE(info.ok);
    EXPECT_EQ(info.renderer, "a");
}

TEST(GlDriverCensus, ParseGarbageFails) {
    EXPECT_FALSE(parseGlDriverLine("").ok);
    EXPECT_FALSE(parseGlDriverLine("no line here at all").ok);
    EXPECT_FALSE(parseGlDriverLine("[gldriver] renderer='x' gallium=/g").ok);      // llvm= missing
    EXPECT_FALSE(parseGlDriverLine("[gldriver] renderer= gallium=none llvm=none").ok);  // no quotes
    EXPECT_FALSE(parseGlDriverLine("prefix [gldriver] renderer='r'").ok);          // unbalanced quote
    // An EMPTY renderer with the kv fields intact IS well-formed: the client
    // emits renderer='' when the bridge has no context string (a build or
    // path that never reached the driver's initDriver capture), and the
    // round-trip test below depends on the parser accepting the emitter's
    // own output.
    EXPECT_TRUE(parseGlDriverLine("[gldriver] renderer='' gallium=none llvm=none").ok);
}

TEST(GlDriverCensus, LineFormatRoundTrips) {
    const GlDriverInfo info{"Mali400",
                            "/home/user/mesa-nollvm-25.2.8/lib/libgallium-25.2.8.so",
                            "none"};
    const std::string line = makeGlDriverLine(info);
    EXPECT_EQ(line, "[gldriver] renderer='Mali400' "
                    "gallium=/home/user/mesa-nollvm-25.2.8/lib/libgallium-25.2.8.so "
                    "llvm=none");
    EXPECT_TRUE(parseGlDriverLine(line).ok);
    const GlDriverInfo back = parseGlDriverLine(line);
    EXPECT_EQ(back.renderer, info.renderer);
    EXPECT_EQ(back.gallium, info.gallium);
    EXPECT_EQ(back.llvm, info.llvm);
}

// B1016 (#337), audit NIT: a renderer containing a single quote CANNOT
// round-trip -- the emitter writes it verbatim, the parser's quoted field
// ends at the first ' ("it's" parses back as renderer="it"). Documented
// LIMIT, pinned on the C++ side here and on the Python side by
// test_gldriver_line_format.py case 3: no real GL_RENDERER string contains
// a quote, so the format keeps the plain quoted contract rather than
// inventing an escape.
TEST(GlDriverCensus, SingleQuoteInRendererDoesNotRoundTrip) {
    const GlDriverInfo info{"it's renderer", "none", "none"};
    const std::string line = makeGlDriverLine(info);
    const GlDriverInfo back = parseGlDriverLine(line);
    EXPECT_TRUE(back.ok);
    EXPECT_EQ(back.renderer, "it");  // truncated at the embedded quote
}

TEST(GlDriverCensus, MesaPrefixMissingDecidesOnTheDirectory) {
    const fs::path tmp = fs::temp_directory_path() /
                         ("b946_gl_prefix_" + std::to_string(::getpid()));
    // The "present" control is a per-process directory too (the census
    // demands it for every temp path the test touches).
    const fs::path present = fs::temp_directory_path() /
                             ("b946_gl_present_" + std::to_string(::getpid()));
    ASSERT_TRUE(std::filesystem::create_directories(tmp));
    ASSERT_TRUE(std::filesystem::create_directories(present));
    ASSERT_FALSE(fs::exists(tmp / "lib"));
    // The decision must CONTINUE, not refuse: it reports, it does not throw.
    EXPECT_TRUE(mesaPrefixMissing((tmp / "lib").string()));
    EXPECT_FALSE(mesaPrefixMissing(present.string()));
    EXPECT_FALSE(mesaPrefixMissing(std::string()));   // a non-nollvm binary: empty answer, no check
    fs::remove_all(present);
    fs::remove_all(tmp);
}

TEST(GlDriverCensus, ElfRpathReadsDtRpath) {
    const std::string rpath = "/home/user/mesa-nollvm-25.2.8/lib";
    // Both shipped classes: the ARM build (32-bit) and the x86 one (64-bit).
    EXPECT_EQ(parseElfRpath(makeDynElf(false, rpath)), rpath);
    EXPECT_EQ(parseElfRpath(makeDynElf(true, rpath)), rpath);
}

TEST(GlDriverCensus, ElfRpathRejectsGarbage) {
    EXPECT_EQ(parseElfRpath("not an elf at all"), std::string());
    EXPECT_EQ(parseElfRpath(std::string()), std::string());
    EXPECT_EQ(parseElfRpath(makeDynElf(false, "").substr(0, 20)), std::string());  // truncated
    EXPECT_EQ(parseElfRpath(makeDynElf(true, "x")), std::string("x"));  // fixture round-trips
}

TEST(GlDriverCensus, NollvmPrefixFromRpath) {
    // B1016 (#337): the entry is returned AS-IS -- the publish step bakes
    // in `<prefix>/lib`, so the decision tests the lib directory itself
    // (the old contract stripped the trailing /lib and the check ran
    // against the parent: a partial deploy that kept the prefix dir but
    // lost lib/ logged no error).
    EXPECT_EQ(nollvmPrefixFromRpath("/home/user/mesa-nollvm-25.2.8/lib"),
              "/home/user/mesa-nollvm-25.2.8/lib");
    // The marker need not be the FIRST colon entry.
    EXPECT_EQ(nollvmPrefixFromRpath("/a:/home/pi/mesa-nollvm-25.2.8/lib:/b"),
              "/home/pi/mesa-nollvm-25.2.8/lib");
    // An entry without a trailing /lib is returned as-is too (the marker
    // alone identifies a nollvm build; the check tests whatever the entry
    // is).
    EXPECT_EQ(nollvmPrefixFromRpath("/home/pi/mesa-nollvm-25.2.8"),
              "/home/pi/mesa-nollvm-25.2.8");
    // No marker at all -- a plain willeq, or any other rpath.
    EXPECT_EQ(nollvmPrefixFromRpath("/usr/lib/x86_64-linux-gnu"), std::string());
    EXPECT_EQ(nollvmPrefixFromRpath(std::string()), std::string());
}

TEST(GlDriverCensus, ScanMapsTextFindsTheNeedles) {
    // A maps line is "start-end  perms  offset  dev  inode  [pathname]"
    // with the columns TAB-separated (glibc pads them). The pathname is
    // the SIXTH tab-separated token. These fixtures use the real
    // tab-separated layout (a space-separated fixture would have passed
    // the buggy space-searching draft -- that was the "none none" bug).
    const std::string maps =
        "00400000-00481000\tr-xp\t00000000\t08:01\t123\t/usr/bin/test_gl_driver_census\n"
        "7f1a00000000-7f1a00c2b000\tr-xp\t00000000\t08:01\t456\t/usr/lib/x86_64-linux-gnu/libgallium-25.2.8.so\n"
        "7f1a00c2b000-7f1a02000000\t---p\t00000000\t00:00\t0\t\n"
        "7f1a10000000-7f1a20000000\tr-xp\t00000000\t08:01\t789\t/usr/lib/x86_64-linux-gnu/libLLVM.so.20.1\n"
        "7f1a20000000-7f1a20020000\trw-p\t00000000\t00:00\t0\t[heap]\n";
    std::string gallium, llvm;
    scanMapsText(maps, gallium, llvm);
    EXPECT_EQ(gallium, "/usr/lib/x86_64-linux-gnu/libgallium-25.2.8.so");
    EXPECT_EQ(llvm, "/usr/lib/x86_64-linux-gnu/libLLVM.so.20.1");
}

TEST(GlDriverCensus, ScanMapsTextIgnoresNonNeedles) {
    const std::string maps =
        "7f1a00000000-7f1a00c2b000\tr-xp\t00000000\t08:01\t456\t/usr/lib/x86_64-linux-gnu/libEGL.so.1.1.0\n"
        "7f1a10000000-7f1a20000000\tr-xp\t00000000\t08:01\t789\t/usr/lib/x86_64-linux-gnu/libgallium.so\n";
    std::string gallium, llvm;
    scanMapsText(maps, gallium, llvm);
    EXPECT_EQ(gallium, "/usr/lib/x86_64-linux-gnu/libgallium.so");
    EXPECT_TRUE(llvm.empty());   // libEGL is not libLLVM
}

TEST(GlDriverCensus, ScanMapsTextLastMappingWins) {
    // A process that dlopens a second gallium (it does not in practice; the
    // rule is pinned anyway) reports the LAST mapping.
    const std::string maps =
        "7f1a00000000-7f1a00c2b000\tr-xp\t00000000\t08:01\t456\t/a/libgallium-1.so\n"
        "7f1a10000000-7f1a20000000\tr-xp\t00000000\t08:01\t789\t/b/libgallium-2.so\n";
    std::string gallium, llvm;
    scanMapsText(maps, gallium, llvm);
    EXPECT_EQ(gallium, "/b/libgallium-2.so");
}

TEST(GlDriverCensus, ScanMapsTextRealLayoutFromProc) {
    // The layout /proc actually hands out: five column tokens before the
    // pathname, separated by whitespace (spaces with padding on this
    // kernel). The parser must be whitespace-agnostic.
    const std::string maps =
        "7f9915400000-7f991d608000 r-xp 00000000 fd:02 153624                     /usr/lib/x86_64-linux-gnu/libLLVM.so.20.1\n"
        "7f991d608000-7f991d609000 ---p 00000000 fd:02 153624                     \n";
    std::string gallium, llvm;
    scanMapsText(maps, gallium, llvm);
    EXPECT_EQ(llvm, "/usr/lib/x86_64-linux-gnu/libLLVM.so.20.1");
    EXPECT_TRUE(gallium.empty());
}

TEST(GlDriverCensus, ScanMapsTextTabSeparatedLayout) {
    // The same line, TAB-separated: the parser must accept this too
    // (other kernels / different glibc pad with tabs).
    const std::string maps =
        "7f9915400000-7f991d608000\tr-xp\t00000000\tfd:02\t153624\t/usr/lib/x86_64-linux-gnu/libLLVM.so.20.1\n";
    std::string gallium, llvm;
    scanMapsText(maps, gallium, llvm);
    EXPECT_EQ(llvm, "/usr/lib/x86_64-linux-gnu/libLLVM.so.20.1");
    EXPECT_TRUE(gallium.empty());
}

TEST(GlDriverCensus, GlPrefixExpectedOnThisProcess) {
    // The TEST binary is never built as willeq-nollvm: no rpath naming the
    // prefix in its own dynamic section, so the decision cannot fire on a
    // test run. (A published willeq-nollvm would answer its baked-in prefix
    // through the same call.)
    EXPECT_EQ(glPrefixExpected(), std::string());
}

// B1016 (#337): the audit's probe. The COMPOSED check -- exactly what
// renderer_init.cpp's glDriverInitLog() runs (nollvmPrefixFromRpath -> the
// missing-check) -- must report a MISSING prefix when the prefix dir EXISTS
// but its lib/ is absent (the partial/failed deploy that makes ld.so fall
// back to the system Mesa silently), and the ERROR must name <prefix>/lib
// -- the directory the DT_RPATH actually carries. On origin/fable the
// composition strips /lib and tests the PARENT: the probe's prefix dir
// exists, so the check reports PRESENT while lib/ is absent. RED there,
// GREEN after the root fix (the check is on the rpath dir itself).
TEST(GlDriverCensus, ComposedNollvmCheckCatchesLibMissingButPrefixPresent) {
    // The prefix name carries the nollvm marker (the B935 convention,
    // scripts/pi/mesa_nollvm.env); the DT_RPATH the publish step bakes in is
    // its lib directory (patchelf --force-rpath).
    const fs::path prefix =
        fs::temp_directory_path() / ("mesa-nollvm-9.9_" + std::to_string(::getpid()));
    const fs::path prefixLib = prefix / "lib";
    ASSERT_TRUE(std::filesystem::create_directories(prefix));  // prefix EXISTS
    ASSERT_FALSE(fs::exists(prefixLib));                        // lib/ is MISSING
    const std::string rpath =
        "/usr/local/lib:" + (prefix / "lib").string() + ":/usr/lib";
    const std::string expected = nollvmPrefixFromRpath(rpath);
    // The composition the renderer runs: the answer names the directory the
    // check must test, and that directory is <prefix>/lib (the DT_RPATH dir
    // itself, not its parent).
    ASSERT_EQ(expected, (prefix / "lib").string());
    const bool missing = mesaPrefixMissing(expected);
    EXPECT_TRUE(missing)
        << "the composed check must report MISSING: prefix dir exists but its "
           "lib/ is absent, so ld.so will silently fall back to the system "
           "Mesa (the exact defect the check exists to report). The check "
           "tested the parent (which exists) instead of "
           << (prefix / "lib").string();
    // The lib/ present control: once the deploy is complete the check must
    // stop firing.
    ASSERT_TRUE(std::filesystem::create_directories(prefixLib));
    EXPECT_FALSE(mesaPrefixMissing(expected));
    fs::remove_all(prefix);
}

TEST(GlDriverCensus, RealMapsAreConsistent) {
    // The test binary maps its own glibc/gmock -- never libgallium -- so this
    // pins the NEGATIVE half against the live /proc; the positive half is the
    // fixture above. If this binary ever links a mesa library the gallium
    // half must be updated to expect it (never deleted: the rig's mesa_mismatch
    // net depends on this file's needles being the client's own).
    const GlDriverInfo info = glDriverInfo(std::string());
    ASSERT_TRUE(info.ok);
    EXPECT_EQ(info.renderer, std::string());         // no GL context in this binary
    EXPECT_EQ(info.gallium, "none");
    EXPECT_EQ(info.llvm, "none");
    // The line form the client emits is well-formed enough to parse back.
    const GlDriverInfo back = parseGlDriverLine(makeGlDriverLine(info));
    ASSERT_TRUE(back.ok);
    EXPECT_EQ(back.gallium, info.gallium);
}

// B1016 (#337): the NET the audit found missing. No test asserted that a
// REAL willeq run prints the [gldriver] line: deleting glDriverInitLog()
// (renderer_init.cpp) left every unit test green (the pipeline 757 traces
// show no `gldriver` outside the unit tests). This spawns the real binary --
// the TEST-NET-1 config idiom of test_memory_ledger, plus --renderer gles2
// (the surfaceless-EGL default, no display) so GL init actually runs -- and
// waits for the line in its own output. Deletion of glDriverInitLog() turns
// it red. The gate keeps its own real-run leg (visual_gate.py --gldriver-
// smoke); this one runs on any host with llvmpipe.
namespace {

struct GlDriverRunChild {
    pid_t pid = -1;
    int outFd = -1;
    bool eof = false;
    bool reaped = false;
    std::string output;

    ~GlDriverRunChild() {
        if (pid > 0 && !reaped) {
            ::kill(pid, SIGKILL);
            int st = 0;
            ::waitpid(pid, &st, 0);
        }
        if (outFd >= 0) ::close(outFd);
    }

    bool start(const std::string& bin, const std::string& config,
               std::string& error) {
        std::signal(SIGPIPE, SIG_IGN);
        int inPipe[2];
        int outPipe[2];
        if (::pipe2(inPipe, O_CLOEXEC) != 0 ||
            ::pipe2(outPipe, O_CLOEXEC) != 0) {
            error = std::string("pipe2: ") + std::strerror(errno);
            return false;
        }
        pid = ::fork();
        if (pid < 0) {
            error = std::string("fork: ") + std::strerror(errno);
            return false;
        }
        if (pid == 0) {
            ::close(inPipe[1]);
            ::close(outPipe[0]);
            ::dup2(inPipe[0], STDIN_FILENO);
            ::dup2(outPipe[1], STDOUT_FILENO);
            ::dup2(outPipe[1], STDERR_FILENO);
            // The logger's drain thread block-buffers a pipe (B839);
            // unbuffered, the [gldriver] line lands the moment it is
            // flushed, not at death.
            ::setvbuf(stdout, nullptr, _IONBF, 0);
            ::setvbuf(stderr, nullptr, _IONBF, 0);
            std::vector<const char*> argv = {
                bin.c_str(), "-c", config.c_str(), "--renderer", "gles2",
                "--no-audio", "--log-level=INFO", nullptr};
            ::execv(bin.c_str(), const_cast<char* const*>(argv.data()));
            _exit(127);
        }
        ::close(inPipe[0]);
        ::close(inPipe[1]);  // EOF: the client has nothing to read
        ::close(outPipe[1]);
        outFd = outPipe[0];
        return true;
    }

    // Drain until `deadline` or EOF. Returns true when `needle` arrives.
    bool readUntil(const std::string& needle,
                   std::chrono::steady_clock::time_point deadline) {
        for (;;) {
            if (output.find(needle) != std::string::npos) return true;
            if (eof || std::chrono::steady_clock::now() >= deadline) return false;
            pollfd pfd{outFd, POLLIN, 0};
            int ready = ::poll(&pfd, 1, 500);
            if (ready < 0 && errno != EINTR) return false;
            if (ready > 0) {
                char buf[8192];
                ssize_t n = ::read(outFd, buf, sizeof(buf));
                if (n > 0) {
                    output.append(buf, static_cast<size_t>(n));
                    continue;
                }
                eof = true;
            }
        }
    }

    void stop() {
        if (pid > 0 && !reaped) {
            ::kill(pid, SIGTERM);
            int st = 0;
            if (::waitpid(pid, &st, 0) > 0) reaped = true;
        }
    }
};

}  // namespace

TEST(GlDriverCensus, RealRunPrintsTheGlDriverLine) {
#ifndef WILLEQ_BIN_PATH
    GTEST_SKIP() << "WILLEQ_BIN_PATH not defined at configure time";
#else
    // The same self-contained TEST-NET-1 config as
    // MemoryLedger.HeadlessClientNamesAtLeast95PercentOfItsStartupBytes:
    // the child never connects, and --renderer gles2 (the surfaceless-EGL
    // default, no display) makes GL init -- and glDriverInitLog -- run.
    // Graphics init additionally validates eq_client_path (application.cpp
    // initConfigValidation): the repo's data/eq_client symlink when present
    // (CI's before_script re-makes it; a tree without assets skips), else
    // an empty per-process temp dir under the UNIQUE mkdtemp dir below
    // (TmpPathCensus: a fixed /tmp name shared by every concurrent ctest
    // case is a B571 hazard) -- validation only stats the directory; GL
    // init and the [gldriver] line do not read an EQ file before emission.
    std::string eqClientPath;
    {
        fs::path p = fs::absolute(
            fs::path(WILLEQ_SOURCE_DIR) / "data" / "eq_client");
        std::error_code ec;
        if (fs::is_directory(p, ec) && !ec) eqClientPath = p.string();
    }
    std::string tmpl =
        (fs::temp_directory_path() / "b1016_gldriver_run_XXXXXX").string();
    std::vector<char> name(tmpl.begin(), tmpl.end());
    name.push_back('\0');
    ASSERT_NE(::mkdtemp(name.data()), nullptr) << "mkdtemp: " << std::strerror(errno);
    const std::string dir = name.data();
    if (eqClientPath.empty()) {
        fs::create_directories(dir + "/fake_eq");
        eqClientPath = dir + "/fake_eq";
    }
    const std::string configPath = dir + "/config.json";
    {
        std::ofstream out(configPath);
        out << "{\n"
            << "  \"clients\": [\n"
            << "    {\n"
            << "      \"character\": \"B1016Test\",\n"
            << "      \"host\": \"192.0.2.1\",\n"
            << "      \"port\": 5998,\n"
            << "      \"server\": \"B1016NoServer\",\n"
            << "      \"user\": \"b1016test\",\n"
            << "      \"pass\": \"test\",\n"
            << "      \"eq_client_path\": \"" << eqClientPath << "\"\n"
            << "    }\n"
            << "  ]\n"
            << "}\n";
        ASSERT_TRUE(out);
    }

    GlDriverRunChild child;
    std::string error;
    ASSERT_TRUE(child.start(WILLEQ_BIN_PATH, configPath, error)) << error;

    // GL init on this host (llvmpipe, surfaceless) is a matter of seconds
    // after the device is created; 120 s covers a loaded CI box.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    const bool got = child.readUntil("[gldriver] renderer='", deadline);
    child.stop();
    fs::remove_all(dir);

    ASSERT_TRUE(got)
        << "a real willeq run never printed the [gldriver] line -- "
           "glDriverInitLog() (renderer_init.cpp) is not being reached or "
           "emitted. Output so far:\n"
        << child.output;
    const size_t at = child.output.find("[gldriver]");
    const size_t nl = child.output.find('\n', at);
    std::cout << "[B1016] real run emitted: "
              << child.output.substr(at, nl == std::string::npos
                                         ? std::string::npos
                                         : nl - at)
              << std::endl;
#endif
}
