/*
 * WillEQ shipped-data path resolution -- see include/client/data_paths.h for
 * why this exists (T397, board B221) and what the search order is.
 */

#include "client/data_paths.h"

#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

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

}  // namespace eqt
