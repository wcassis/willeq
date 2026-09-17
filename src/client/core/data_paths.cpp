/*
 * WillEQ shipped-data path resolution -- see include/client/data_paths.h for
 * why this exists (T397, board B221) and what the search order is.
 */

#include "client/data_paths.h"

#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include <limits.h>

namespace eqt {

namespace {

// How far up from the executable's own directory to look for the data tree.
// <repo>/build/bin/willeq needs two levels; the extra headroom covers a
// deeper staging layout without turning the search into a filesystem crawl.
constexpr int kMaxParentWalk = 3;

bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string ParentOf(const std::string& dir) {
    const auto slash = dir.find_last_of('/');
    if (slash == std::string::npos) return "";
    if (slash == 0) return "/";
    return dir.substr(0, slash);
}

}  // namespace

std::string ExecutableDirectory() {
    char buf[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    return ParentOf(std::string(buf));
}

std::string ResolveDataPath(const std::string& relative) {
    if (relative.empty()) return relative;

    // 1. An absolute path is already resolved; never rewrite it.
    if (relative[0] == '/') return relative;

    // 2. The CWD-relative path itself. FIRST on purpose: this is exactly what
    //    every caller did before T397, so a run from the repo root reads the
    //    same bytes it always did and no shipped behaviour moves.
    if (FileExists(relative)) return relative;

    // 3. An explicit override, for a staged install whose data tree is not
    //    beside the binary (the Orange Pi's NFS-mounted assets, say).
    if (const char* root = std::getenv("WILLEQ_DATA_ROOT")) {
        if (root[0] != '\0') {
            std::string candidate = std::string(root) + "/" + relative;
            if (FileExists(candidate)) return candidate;
        }
    }

    // 4. The executable's directory and its parents: <repo>/build/bin/willeq
    //    -> <repo>/config/spell_effects.json.
    std::string dir = ExecutableDirectory();
    for (int i = 0; i <= kMaxParentWalk && !dir.empty(); ++i) {
        std::string candidate = (dir == "/") ? ("/" + relative) : (dir + "/" + relative);
        if (FileExists(candidate)) return candidate;
        if (dir == "/") break;
        dir = ParentOf(dir);
    }

    // Nothing found. Hand back what the caller asked for so its own "could
    // not open X" message names X.
    return relative;
}

namespace {

// The candidate directories for a generated cache, in order. See the block
// comment in include/client/data_paths.h (T402, board B248): none of them is
// inside the checkout, which is the point.
//
// B845 (board B845): a non-empty WILLEQ_CACHE_DIR is AUTHORITATIVE. The
// original T402 design treated it as merely the FIRST candidate, so a read
// whose per-process dir was still empty fell through to the shared
// `<exe dir>/cache` (build/bin/cache/) and could short-circuit on a cache that
// a CONCURRENT test process had just written there. That shared-file
// read/write is what made the AudioStartupPeak cases flake under ctest -j
// (their fixture tried to keep the shared file hidden with a rename that a
// sibling's in-flight destructor could undo). When a caller sets
// WILLEQ_CACHE_DIR it is stating "this run owns this directory" -- so read and
// write are confined to it and never touch the shared fallbacks. An unset or
// empty variable keeps the full candidate walk, which is every production
// caller (none of them sets the variable).
std::vector<std::string> CacheDirCandidates() {
    std::vector<std::string> dirs;
    if (const char* explicitDir = std::getenv("WILLEQ_CACHE_DIR")) {
        if (explicitDir[0] != '\0') {
            dirs.push_back(explicitDir);
            return dirs;  // authoritative: no shared fallback
        }
    }
    if (const char* root = std::getenv("WILLEQ_DATA_ROOT")) {
        if (root[0] != '\0') dirs.push_back(std::string(root) + "/cache");
    }
    const std::string exeDir = ExecutableDirectory();
    if (!exeDir.empty()) dirs.push_back(exeDir + "/cache");
    dirs.push_back("cache");
    return dirs;
}

// mkdir -p for one directory path, no <filesystem> (this TU is linked into
// twenty-four targets and stays dependency-free).
bool EnsureDirectory(const std::string& dir) {
    if (dir.empty()) return false;
    struct stat st;
    if (::stat(dir.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    const std::string parent = ParentOf(dir);
    if (!parent.empty() && parent != dir) EnsureDirectory(parent);
    if (::mkdir(dir.c_str(), 0775) == 0) return true;
    return ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Writability by TRYING, not by asking. ::access(W_OK) is not reliable here:
// the dev tree lives on an NFSv3 mount whose ACCESS RPC answers "no write" for
// a directory this process can in fact write (mode 0777, server-side uid
// mapping) -- T402 lost a test run to exactly that. Create-and-unlink is the
// only honest probe.
bool DirectoryIsWritable(const std::string& dir) {
    const std::string probe = dir + "/.willeq_cache_write_test." + std::to_string(::getpid());
    const int fd = ::open(probe.c_str(), O_CREAT | O_WRONLY | O_EXCL, 0664);
    if (fd < 0) return false;
    ::close(fd);
    ::unlink(probe.c_str());
    return true;
}

}  // namespace

std::string ResolveCachePathForRead(const std::string& name) {
    if (name.empty()) return "";
    if (name[0] == '/') return FileExists(name) ? name : "";
    for (const auto& dir : CacheDirCandidates()) {
        const std::string candidate = dir + "/" + name;
        if (FileExists(candidate)) return candidate;
    }
    return "";
}

std::string ResolveCachePathForWrite(const std::string& name) {
    if (name.empty()) return "";
    if (name[0] == '/') return name;
    for (const auto& dir : CacheDirCandidates()) {
        if (!EnsureDirectory(dir)) continue;
        if (!DirectoryIsWritable(dir)) continue;
        const std::string full = dir + "/" + name;
        // T432: `name` may name a subdirectory (`ui/UI_Char_server.ini`). The
        // caller opens `full` directly, so the leaf directory has to exist by
        // the time we hand the path back -- a plain `name` is unaffected,
        // because its parent IS `dir`, which we just ensured.
        const std::string parent = ParentOf(full);
        if (parent != dir && !EnsureDirectory(parent)) continue;
        return full;
    }
    return "";
}

}  // namespace eqt
