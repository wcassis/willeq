/*
 * B1090: the source-tree paths a TEST or an OFFLINE TOOL reads, resolved at
 * RUN time instead of being baked into the compile line.
 *
 * Why: until B1090 every test translation unit was compiled with
 * -DWILLEQ_REPO_ROOT="<absolute checkout path>" (plus EQ_CLIENT_DATA_PATH,
 * WILLEQ_CONSTRAINED_PRESETS_JSON and ~35 per-target "<checkout>/<file>"
 * defines). The compile line therefore differed in every worktree, so ccache
 * could never reuse an object built in another worktree: 1301 of 2484 compile
 * lines carried the path, and a fresh worker worktree cost a cold ~65 min build
 * on the ThinkPad instead of a ~150 s warm one (B1087, #407). Now the compile
 * line carries only a path-FREE expression (WILLEQ_REPO_PATH("data/willeq.db"))
 * and the root is found when the binary runs.
 *
 * Root resolution, first hit wins (eqt::paths::ResolveRepoRoot):
 *   1. $WILLEQ_REPO_ROOT, if set and non-empty (an explicit override);
 *   2. willeq_paths.txt -- written by CMake at configure time into the build
 *      directory ("repo_root=<CMAKE_SOURCE_DIR>") -- in the running
 *      executable's directory or one of its kMaxParentWalk parents
 *      (<build>/bin/test_x finds <build>/willeq_paths.txt, wherever the build
 *      tree lives, in-tree or not);
 *   3. the first directory, walking up from the executable and then from the
 *      CWD, that holds this header (an in-tree build with no paths file);
 *   4. "." (the old CWD-relative behaviour).
 * EQ_CLIENT_DATA_PATH and WILLEQ_CONSTRAINED_PRESETS_JSON default to
 * <root>/data/eq_client and <root>/config/constrained_presets.json and take
 * environment variables of the same names as overrides.
 *
 * This is NOT how the client finds its shipped data -- that is
 * eqt::ResolveDataPath() (include/client/data_paths.h) and never uses a
 * build-tree path. The test targets get this header force-included
 * (tests/CMakeLists.txt), so every existing use of WILLEQ_REPO_ROOT,
 * EQ_CLIENT_DATA_PATH and WILLEQ_CONSTRAINED_PRESETS_JSON keeps working; the
 * macros now expand to a `const char*` EXPRESSION, not a string literal, so
 * they cannot be pasted onto a literal (`WILLEQ_REPO_ROOT "/x"` -- use
 * WILLEQ_REPO_PATH("x")) and cannot initialise a constexpr.
 *
 * Header-only on purpose: ~300 test targets each compile their own source
 * list, and none of them has to link anything new.
 */
#pragma once

#ifdef __cplusplus  // also force-included into the tests' C sources (sqlite3.c)

#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#endif

namespace eqt::paths {

/** Written by CMake into CMAKE_BINARY_DIR (root CMakeLists.txt, B1090). */
inline constexpr const char* kPathsFileName = "willeq_paths.txt";

/** <build>/bin needs one level; CI's per-ref <repo>/build/ref/<slug>/bin and
 *  an in-tree <repo>/build/bin marker walk need up to three. */
inline constexpr int kMaxParentWalk = 4;

/** The repo-relative file whose presence marks a checkout root (step 3). */
inline constexpr const char* kRootMarker = "include/common/willeq_paths.h";

namespace detail {

inline bool IsFile(const std::string& p) {
    struct stat st;
    return !p.empty() && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

inline bool IsDir(const std::string& p) {
    struct stat st;
    return !p.empty() && ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

inline std::string ParentOf(const std::string& dir) {
    const auto slash = dir.find_last_of('/');
    if (slash == std::string::npos) return "";
    if (slash == 0) return dir.size() > 1 ? "/" : "";
    return dir.substr(0, slash);
}

inline std::string ExecutableDirectory() {
#if defined(__APPLE__)
    char raw[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (::_NSGetExecutablePath(raw, &size) != 0) return "";
    char buf[PATH_MAX];
    if (::realpath(raw, buf) == nullptr) return "";
    return ParentOf(std::string(buf));
#else
    char buf[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    return ParentOf(std::string(buf));
#endif
}

inline std::string CurrentDirectory() {
    char buf[PATH_MAX];
    return ::getcwd(buf, sizeof(buf)) ? std::string(buf) : std::string();
}

inline std::string StripTrailingSlashes(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

}  // namespace detail

/**
 * The value of `key` in a willeq_paths.txt ("key=value" lines; blank lines and
 * '#' comments ignored; a trailing '\r' tolerated). "" when the file cannot be
 * read or has no such key.
 */
inline std::string ReadPathsFileValue(const std::string& file, const std::string& key) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.compare(0, eq, key) != 0 || eq != key.size()) continue;
        return line.substr(eq + 1);
    }
    return "";
}

/**
 * The pure resolution behind RepoRoot() (the order is in the header comment).
 * `envValue` is $WILLEQ_REPO_ROOT (nullptr when unset); `exeDir` and `cwd` are
 * where steps 2 and 3 start. Exposed so the order is unit-testable.
 */
inline std::string ResolveRepoRoot(const char* envValue, const std::string& exeDir,
                                   const std::string& cwd) {
    if (envValue != nullptr && envValue[0] != '\0') {
        return detail::StripTrailingSlashes(envValue);
    }
    std::string dir = exeDir;
    for (int i = 0; i <= kMaxParentWalk && !dir.empty(); ++i) {
        const std::string file = dir + "/" + kPathsFileName;
        if (detail::IsFile(file)) {
            const std::string root =
                detail::StripTrailingSlashes(ReadPathsFileValue(file, "repo_root"));
            if (detail::IsDir(root)) return root;
        }
        const std::string parent = detail::ParentOf(dir);
        if (parent == dir) break;
        dir = parent;
    }
    for (const std::string& start : {exeDir, cwd}) {
        dir = start;
        for (int i = 0; i <= kMaxParentWalk && !dir.empty(); ++i) {
            if (detail::IsFile(dir + "/" + kRootMarker)) return dir;
            const std::string parent = detail::ParentOf(dir);
            if (parent == dir) break;
            dir = parent;
        }
    }
    return ".";
}

/** The source-tree root of the build that produced the running binary. */
inline const std::string& RepoRoot() {
    static const std::string root = ResolveRepoRoot(
        std::getenv("WILLEQ_REPO_ROOT"), detail::ExecutableDirectory(), detail::CurrentDirectory());
    return root;
}

/** `root` + "/" + `relative`; the root itself for an empty `relative`. */
inline std::string JoinRepoPath(const std::string& root, const char* relative) {
    if (relative == nullptr || relative[0] == '\0') return root;
    if (relative[0] == '/') return root + relative;
    return root + "/" + relative;
}

/**
 * RepoRoot()/<relative> as a C string that lives for the rest of the process
 * (interned per distinct `relative`), so it can stand in anywhere the old
 * string-literal define did -- fopen(), std::string(), a `const char*` global.
 */
inline const char* RepoPath(const char* relative) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::unique_ptr<const std::string>> interned;
    const std::string key = relative ? relative : "";
    std::lock_guard<std::mutex> lock(mutex);
    auto it = interned.find(key);
    if (it == interned.end()) {
        it = interned
                 .emplace(key, std::make_unique<const std::string>(
                                   JoinRepoPath(RepoRoot(), key.c_str())))
                 .first;
    }
    return it->second->c_str();
}

/** $<envName> if set and non-empty, else RepoPath(relative). */
inline const char* RepoPathOrEnv(const char* envName, const char* relative) {
    const char* env = std::getenv(envName);
    return (env != nullptr && env[0] != '\0') ? env : RepoPath(relative);
}

/**
 * <directory of the running executable>/<name>, interned like RepoPath(). Every
 * target lands in <build>/bin (CMAKE_RUNTIME_OUTPUT_DIRECTORY), so a test finds
 * a sibling binary (willeq, gles2_zone_benchmark, ...) beside itself; CMake
 * passes only the path-free $<TARGET_FILE_NAME:...>.
 */
inline const char* BinPath(const char* name) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::unique_ptr<const std::string>> interned;
    const std::string key = name ? name : "";
    std::lock_guard<std::mutex> lock(mutex);
    auto it = interned.find(key);
    if (it == interned.end()) {
        static const std::string exeDir = detail::ExecutableDirectory();
        it = interned
                 .emplace(key, std::make_unique<const std::string>(
                                   exeDir.empty() ? key : exeDir + "/" + key))
                 .first;
    }
    return it->second->c_str();
}

}  // namespace eqt::paths

/** A repo-relative path, resolved at run time ("" = the root itself). */
#define WILLEQ_REPO_PATH(relative) (::eqt::paths::RepoPath(relative))
/** A binary beside the running executable (<build>/bin/<name>). */
#define WILLEQ_BIN_PATH_OF(name) (::eqt::paths::BinPath(name))

#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT WILLEQ_REPO_PATH("")
#endif
#ifndef EQ_CLIENT_DATA_PATH
#define EQ_CLIENT_DATA_PATH (::eqt::paths::RepoPathOrEnv("EQ_CLIENT_DATA_PATH", "data/eq_client"))
#endif
#ifndef WILLEQ_CONSTRAINED_PRESETS_JSON
#define WILLEQ_CONSTRAINED_PRESETS_JSON \
    (::eqt::paths::RepoPathOrEnv("WILLEQ_CONSTRAINED_PRESETS_JSON", "config/constrained_presets.json"))
#endif

#endif  // __cplusplus
