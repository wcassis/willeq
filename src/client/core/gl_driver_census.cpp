// B946 (GitLab #264): the [gldriver] line's /proc + ELF half -- see
// include/client/core/gl_driver_census.h for the problem and the decision.
// This TU is the only place that reads /proc/self/maps for it: one open,
// one read, one close, at GL init, never per frame. It must not be reached
// from inside an allocator hook (it allocates small std::strings), which is
// guaranteed by its only caller -- the renderer's GL init (glDriverInitLog
// in renderer_init.cpp, called once from initLoadingScreen).

#include "client/core/gl_driver_census.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace eqt {
namespace memory {

bool mesaPrefixMissing(const std::string& prefixLibDir) {
    if (prefixLibDir.empty()) return false;         // a non-nollvm binary: no such rpath, no check
    return !std::filesystem::exists(prefixLibDir);
}

// The RPATH/RUNPATH string of an ELF: walk the program headers for the
// PT_DYNAMIC segment, find DT_RPATH (preferring it over DT_RUNPATH), and
// resolve d_val (a vaddr) to a file offset through the PT_LOAD segments.
// "" for a non-ELF, little-endian-or-not (both shipped binaries are
// little-endian; a big-endian input is rejected, a willeq binary is never
// big-endian), an ELF without a dynamic section, no rpath entry, a value no
// LOAD segment covers, or a string that does not NUL-terminate in range.
std::string parseElfRpath(const std::string& bytes) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const size_t n = bytes.size();
    if (n < 20) return std::string();
    if (memcmp(p, "\x7f""ELF", 4) != 0) return std::string();
    const bool is64 = p[4] == 2;
    if (p[5] != 1) return std::string();
    auto rd = [&](size_t off, size_t w) -> uint64_t {
        if (off + w > n) return 0;
        uint64_t v = 0;
        for (size_t i = 0; i < w; ++i) v |= (uint64_t)p[off + i] << (8 * i);
        return v;
    };
    const size_t phoff = is64 ? rd(32, 8) : rd(28, 4);
    const uint16_t phentsize = (uint16_t)rd(is64 ? 54 : 42, 2);
    const uint16_t phnum = (uint16_t)rd(is64 ? 56 : 44, 2);
    if (phentsize < (is64 ? 56u : 32u) || phnum == 0) return std::string();
    struct Seg { uint64_t off, vaddr, filesz; };
    std::vector<Seg> loads;
    uint64_t dynOff = 0, dynSz = 0;
    bool haveDyn = false;
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t ph = phoff + (size_t)i * phentsize;
        if (ph + phentsize > n) return std::string();
        const uint32_t type = (uint32_t)rd(ph, 4);
        const uint64_t off = is64 ? rd(ph + 8, 8) : rd(ph + 4, 4);
        const uint64_t vaddr = is64 ? rd(ph + 16, 8) : rd(ph + 8, 4);
        const uint64_t filesz = is64 ? rd(ph + 32, 8) : rd(ph + 16, 4);
        if (off > n || off + filesz > n) continue;
        if (type == 1 /* PT_LOAD */) loads.push_back({off, vaddr, filesz});
        if (type == 2 /* PT_DYNAMIC */) { dynOff = off; dynSz = filesz; haveDyn = true; }
    }
    if (!haveDyn) return std::string();
    uint64_t rpathVal = 0, runpathVal = 0;
    bool haveRpath = false, haveRunpath = false;
    {
        const size_t entsz = is64 ? 16 : 8;
        size_t c = dynOff;
        while (c + entsz <= dynOff + dynSz) {
            const uint64_t tag = rd(c, 4);
            const uint64_t val = is64 ? rd(c + 8, 8) : rd(c + 4, 4);
            if (tag == 1 /* DT_RPATH */ && !haveRpath) { rpathVal = val; haveRpath = true; }
            else if (tag == 0x1d /* DT_RUNPATH */ && !haveRunpath) { runpathVal = val; haveRunpath = true; }
            if (tag == 0) break;
            c += entsz;
        }
    }
    uint64_t v = 0;
    if (haveRpath) v = rpathVal;
    else if (haveRunpath) v = runpathVal;
    else return std::string();
    for (const Seg& s : loads) {
        if (s.vaddr <= v && v < s.vaddr + s.filesz) {
            const size_t at = s.off + (size_t)(v - s.vaddr);
            const size_t end = bytes.find('\0', at);
            if (end != std::string::npos && end - at < s.filesz)
                return bytes.substr(at, end - at);
            return std::string();
        }
    }
    return std::string();
}

// See the header: the first colon entry that names the shipped prefix.
// B1016 (#337): the entry is returned AS-IS (the publish step bakes in
// `<prefix>/lib`), so the decision tests the lib directory ITSELF -- the
// old code stripped the trailing "/lib" and the check ran against the
// parent: a partial deploy that left the prefix dir but lost lib/ logged
// no error while ld.so silently fell back to the system Mesa.
std::string nollvmPrefixFromRpath(const std::string& rpath) {
    const std::string kMarker = "mesa-nollvm-";
    size_t start = 0;
    while (start <= rpath.size()) {
        const size_t colon = rpath.find(':', start);
        const std::string entry =
            colon == std::string::npos ? rpath.substr(start)
                                       : rpath.substr(start, colon - start);
        if (entry.find(kMarker) != std::string::npos)
            return entry;
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return std::string();
}

std::string makeGlDriverLine(const GlDriverInfo& info) {
    std::string out = "[gldriver] renderer='" + info.renderer + "'";
    out += " gallium=" + (info.gallium.empty() ? std::string("none") : info.gallium);
    out += " llvm=" + (info.llvm.empty() ? std::string("none") : info.llvm);
    return out;
}

std::string glPrefixExpected() {
#if defined(__linux__)
    // /proc/self/exe resolves through symlinks to the REAL binary -- the
    // same file ld.so read the dynamic section of, so its DT_RPATH is
    // authoritative for whether THIS process was built as willeq-nollvm
    // (the publish step, scripts/pi/mesa_nollvm_select.sh, writes
    // <prefix>/lib into it; a symlink or a moved copy keeps its own).
    std::string bytes;
    FILE* f = std::fopen("/proc/self/exe", "rb");
    if (f) {
        char buf[8192];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.append(buf, n);
        std::fclose(f);
    }
    return nollvmPrefixFromRpath(parseElfRpath(bytes));
#else
    return std::string();
#endif
}

// The two needles out of a /proc/self/maps text (see the header). A maps
// line is "start-end  perms  offset  dev  inode  [pathname]": five column
// tokens before the pathname (which is absent for [heap]/[stack]/anonymous
// mappings). The columns are separated by spaces (with padding) -- the
// kernel emits spaces, never tabs, in /proc/*/maps -- so the skip is
// whitespace-agnostic anyway (a tab would just cost one extra iteration):
// skip the first five whitespace-separated tokens, whatever the separator.
// Two regressions are pinned by the fixtures: (1) the first draft skipped
// only four tokens and landed on the inode; (2) it then searched for ' '
// literally, which matched nothing on a tab-padded fixture line and made
// the census report "none none" for every process.
void scanMapsText(const std::string& text, std::string& gallium, std::string& llvm) {
    const char* const kGallium = "libgallium";
    const char* const kLlvm = "libLLVM";
    const size_t gLen = std::strlen(kGallium);
    const size_t lLen = std::strlen(kLlvm);
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        // Skip the first five whitespace-separated tokens (the columns);
        // see the function comment for the separator story and the two
        // regressions the fixtures pin.
        size_t p = 0;
        for (int i = 0; i < 5; ++i) {
            while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
            size_t e = p;
            while (e < line.size() && line[e] != ' ' && line[e] != '\t') ++e;
            if (e == p) { p = std::string::npos; break; }   // empty token: malformed line
            p = e;
        }
        if (p == std::string::npos || p >= line.size()) continue;
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;   // drop padding
        if (p >= line.size()) continue;
        const std::string path = line.substr(p);
        if (path.empty() || path[0] != '/') continue;
        const size_t base = path.find_last_of('/') + 1;
        const std::string name = path.substr(base);
        if (name.compare(0, gLen, kGallium) == 0) gallium = path;
        if (name.compare(0, lLen, kLlvm) == 0) llvm = path;
    }
}

GlDriverInfo glDriverInfo(const std::string& renderer) {
    GlDriverInfo info;
    info.renderer = renderer;
    // The needles. libgallium: every libgallium* mapping seen (ld.so maps
    // one; the newest wins, so the loop keeps overwriting). libLLVM: any
    // libLLVM* mapping -- its PRESENCE is the "loaded the system Mesa" fact
    // the nollvm decision warns about (the shipped prefix is LLVM-less).
    std::string gallium, llvm;
#if defined(__linux__)
    FILE* f = std::fopen("/proc/self/maps", "r");
    if (f) {
        std::string text;
        char buf[8192];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        std::fclose(f);
        scanMapsText(text, gallium, llvm);
    }
#endif
    info.gallium = gallium.empty() ? "none" : gallium;
    info.llvm = llvm.empty() ? "none" : llvm;
    info.ok = true;
    return info;
}

}  // namespace memory
}  // namespace eqt
