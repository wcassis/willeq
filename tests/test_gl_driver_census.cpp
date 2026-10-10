// B946 (GitLab #264): the client never said WHICH GL driver it loaded, and a
// willeq-nollvm binary whose DT_RPATH Mesa prefix is missing fell through to
// the system Mesa (libLLVM included) with no error at all.
//
// The [gldriver] line at GL init names the mapped libgallium (or "none"), the
// GL_RENDERER string, and whether libLLVM is mapped. The gallium/libLLVM paths
// come from /proc/self/maps -- the SAME file the [memmap] census already reads
// for its smaps attribution (T491, include/client/core/memory_ledger.h
// parseSmaps); this TU only adds a /proc read for the two needles, no second
// smaps parser.
//
// What this binary tests:
//   * parseGlDriverLine -- the format contract the rig's summarizer
//     (scripts/pi/rendered_zone_summarize.py mesa_driver_field, its own ctest)
//     and any future consumer rely on.
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

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
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
using eqt::memory::parseGlDriverLine;
using eqt::memory::scanMapsText;

namespace {

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
                             "gallium=/usr/lib/x86_64-linux-gnu/dri/swrast_dri.so llvm=/usr/lib/x86_64-linux-gnu/libLLVM.so.18.1";
    const GlDriverInfo info = parseGlDriverLine(line);
    ASSERT_TRUE(info.ok);
    EXPECT_EQ(info.renderer, "llvmpipe (Mesa 25.1.7-1)");
    EXPECT_EQ(info.gallium, "/usr/lib/x86_64-linux-gnu/dri/swrast_dri.so");
    EXPECT_EQ(info.llvm, "/usr/lib/x86_64-linux-gnu/libLLVM.so.18.1");
}

TEST(GlDriverCensus, ParseNoLlvmIsNone) {
    const std::string line = "2026-10-10 12:00:00.000 [ERROR] [GRAPHICS] "
                             "[gldriver] renderer='Mali400' "
                             "gallium=/home/user/mesa-nollvm-25.2.8/lib/dri/lima_dri.so llvm=none";
    const GlDriverInfo info = parseGlDriverLine(line);
    ASSERT_TRUE(info.ok);
    EXPECT_EQ(info.renderer, "Mali400");
    EXPECT_EQ(info.gallium, "/home/user/mesa-nollvm-25.2.8/lib/dri/lima_dri.so");
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
                            "/home/user/mesa-nollvm-25.2.8/lib/dri/lima_dri.so",
                            "none"};
    const std::string line = makeGlDriverLine(info);
    EXPECT_EQ(line, "[gldriver] renderer='Mali400' "
                    "gallium=/home/user/mesa-nollvm-25.2.8/lib/dri/lima_dri.so "
                    "llvm=none");
    EXPECT_TRUE(parseGlDriverLine(line).ok);
    const GlDriverInfo back = parseGlDriverLine(line);
    EXPECT_EQ(back.renderer, info.renderer);
    EXPECT_EQ(back.gallium, info.gallium);
    EXPECT_EQ(back.llvm, info.llvm);
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
    EXPECT_EQ(nollvmPrefixFromRpath("/home/user/mesa-nollvm-25.2.8/lib"),
              "/home/user/mesa-nollvm-25.2.8");
    // The marker need not be the FIRST colon entry.
    EXPECT_EQ(nollvmPrefixFromRpath("/a:/home/pi/mesa-nollvm-25.2.8/lib:/b"),
              "/home/pi/mesa-nollvm-25.2.8");
    // An entry without a trailing /lib is returned as-is.
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
