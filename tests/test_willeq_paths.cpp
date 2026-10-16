// B1090: the source-tree paths tests and offline tools read are resolved at
// RUN time (include/common/willeq_paths.h), so no compile line carries the
// checkout's absolute path and ccache can reuse objects across worktrees.
//
// Pinned here:
//   * the resolution order -- $WILLEQ_REPO_ROOT, then <build>/willeq_paths.txt
//     beside/above the executable, then the checkout marker walk, then ".";
//   * the paths-file format;
//   * RepoPath()/BinPath() hand out stable, interned C strings;
//   * this very binary resolves the real checkout, and the three legacy macros
//     still mean what they meant;
//   * the CMake net: no compile definition names ${CMAKE_SOURCE_DIR},
//     ${CMAKE_BINARY_DIR} or $<TARGET_FILE:...> -- the absolute paths that made
//     every worktree's compile lines different.

#include <gtest/gtest.h>

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "common/willeq_paths.h"

namespace {

namespace wp = eqt::paths;

class TempTree {
public:
    TempTree() {
        std::string tmpl = "/tmp/willeq_paths_test_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        const char* made = ::mkdtemp(buf.data());
        root_ = made ? made : "";
    }
    ~TempTree() {
        if (!root_.empty()) {
            const std::string cmd = "rm -rf '" + root_ + "'";
            (void)std::system(cmd.c_str());
        }
    }
    const std::string& root() const { return root_; }
    std::string mkdirs(const std::string& rel) const {
        std::string path = root_;
        std::stringstream ss(rel);
        std::string part;
        while (std::getline(ss, part, '/')) {
            if (part.empty()) continue;
            path += "/" + part;
            ::mkdir(path.c_str(), 0755);
        }
        return path;
    }
    void write(const std::string& rel, const std::string& content) const {
        std::ofstream(root_ + "/" + rel) << content;
    }

private:
    std::string root_;
};

TEST(WilleqPaths, EnvironmentOverrideWinsAndLosesTrailingSlashes) {
    TempTree t;
    ASSERT_FALSE(t.root().empty());
    const std::string bin = t.mkdirs("build/bin");
    t.mkdirs("src");
    t.write("build/willeq_paths.txt", "repo_root=" + t.root() + "/src\n");
    EXPECT_EQ(wp::ResolveRepoRoot("/somewhere/else//", bin, "/"), "/somewhere/else");
    // An EMPTY variable is "unset", not "the filesystem root".
    EXPECT_EQ(wp::ResolveRepoRoot("", bin, "/"), t.root() + "/src");
}

TEST(WilleqPaths, PathsFileBesideOrAboveTheExecutableIsRead) {
    TempTree t;
    const std::string src = t.mkdirs("checkout");
    const std::string bin = t.mkdirs("out/build/bin");
    t.write("out/build/willeq_paths.txt", "# generated\nrepo_root=" + src + "/\n");
    EXPECT_EQ(wp::ResolveRepoRoot(nullptr, bin, "/"), src);
    // A file in the executable's own directory is found first.
    const std::string other = t.mkdirs("other");
    t.write("out/build/bin/willeq_paths.txt", "repo_root=" + other + "\n");
    EXPECT_EQ(wp::ResolveRepoRoot(nullptr, bin, "/"), other);
}

TEST(WilleqPaths, PathsFileNamingAMissingDirectoryIsSkipped) {
    TempTree t;
    const std::string bin = t.mkdirs("repo/build/bin");
    t.mkdirs("repo/include/common");
    t.write("repo/include/common/willeq_paths.h", "");
    t.write("repo/build/willeq_paths.txt", "repo_root=" + t.root() + "/gone\n");
    // Falls through to the marker walk from the executable.
    EXPECT_EQ(wp::ResolveRepoRoot(nullptr, bin, "/"), t.root() + "/repo");
}

TEST(WilleqPaths, MarkerWalkFromTheCwdWhenTheExecutableFindsNothing) {
    TempTree t;
    const std::string exe = t.mkdirs("elsewhere/bin");
    const std::string cwd = t.mkdirs("tree/a/b");
    t.mkdirs("tree/include/common");
    t.write("tree/include/common/willeq_paths.h", "");
    EXPECT_EQ(wp::ResolveRepoRoot(nullptr, exe, cwd), t.root() + "/tree");
}

TEST(WilleqPaths, NothingFoundIsTheOldCwdRelativeDot) {
    TempTree t;
    const std::string exe = t.mkdirs("x/y");
    const std::string cwd = t.mkdirs("p/q");
    EXPECT_EQ(wp::ResolveRepoRoot(nullptr, exe, cwd), ".");
    EXPECT_EQ(wp::ResolveRepoRoot(nullptr, "", ""), ".");
}

TEST(WilleqPaths, PathsFileFormat) {
    TempTree t;
    t.write("f.txt",
            "# comment\n\nrepo_root_extra=/no\nrepo_root=/yes/this\r\nother=/x\n");
    const std::string f = t.root() + "/f.txt";
    EXPECT_EQ(wp::ReadPathsFileValue(f, "repo_root"), "/yes/this");
    EXPECT_EQ(wp::ReadPathsFileValue(f, "other"), "/x");
    EXPECT_EQ(wp::ReadPathsFileValue(f, "missing"), "");
    EXPECT_EQ(wp::ReadPathsFileValue(t.root() + "/absent.txt", "repo_root"), "");
}

TEST(WilleqPaths, JoinRepoPath) {
    EXPECT_EQ(wp::JoinRepoPath("/r", ""), "/r");
    EXPECT_EQ(wp::JoinRepoPath("/r", nullptr), "/r");
    EXPECT_EQ(wp::JoinRepoPath("/r", "data/x"), "/r/data/x");
    EXPECT_EQ(wp::JoinRepoPath("/r", "/data/x"), "/r/data/x");
}

TEST(WilleqPaths, InternedPointersAreStable) {
    const char* a = wp::RepoPath("data/willeq.db");
    for (int i = 0; i < 64; ++i) wp::RepoPath(("tests/data/k" + std::to_string(i)).c_str());
    EXPECT_EQ(a, wp::RepoPath("data/willeq.db"));
    EXPECT_EQ(std::string(a), wp::RepoRoot() + "/data/willeq.db");
    const char* b = wp::BinPath("willeq");
    EXPECT_EQ(b, wp::BinPath("willeq"));
    const std::string bs(b);
    EXPECT_EQ(bs.substr(bs.size() - 7), "/willeq");
}

// This binary, wherever its build tree lives, resolves the checkout it was
// configured from -- the property every test that reads a repo file relies on.
TEST(WilleqPaths, ThisBuildResolvesItsOwnCheckout) {
    struct stat st;
    const std::string root = wp::RepoRoot();
    EXPECT_EQ(::stat((root + "/CMakeLists.txt").c_str(), &st), 0) << root;
    EXPECT_EQ(::stat((root + "/" + wp::kRootMarker).c_str(), &st), 0) << root;
    EXPECT_EQ(std::string(WILLEQ_REPO_ROOT), root);
    if (const char* env = std::getenv("EQ_CLIENT_DATA_PATH"); env == nullptr || !*env) {
        EXPECT_EQ(std::string(EQ_CLIENT_DATA_PATH), root + "/data/eq_client");
    }
    if (const char* env = std::getenv("WILLEQ_CONSTRAINED_PRESETS_JSON"); env == nullptr || !*env) {
        EXPECT_EQ(std::string(WILLEQ_CONSTRAINED_PRESETS_JSON),
                  root + "/config/constrained_presets.json");
        EXPECT_EQ(::stat(WILLEQ_CONSTRAINED_PRESETS_JSON, &st), 0);
    }
}

// The regression net for the ccache defect itself: a compile definition that
// carries an absolute build/source path makes every worktree's compile line
// different. ENVIRONMENT properties and include directories are not compile
// definitions and are allowed.
TEST(WilleqPaths, NoCompileDefinitionCarriesAnAbsolutePath) {
    const std::string root = wp::RepoRoot();
    std::vector<std::string> files = {"CMakeLists.txt", "tests/CMakeLists.txt"};
    const std::string fragDir = root + "/tests/cmake";
    if (FILE* p = ::popen(("ls '" + fragDir + "'").c_str(), "r")) {
        char line[512];
        while (std::fgets(line, sizeof(line), p)) {
            std::string name(line);
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
            if (name.size() > 6 && name.substr(name.size() - 6) == ".cmake") {
                files.push_back("tests/cmake/" + name);
            }
        }
        ::pclose(p);
    }
    ASSERT_GT(files.size(), 10u) << "tests/cmake/*.cmake not found under " << root;

    // NAME="${CMAKE_SOURCE_DIR}..." / NAME=${PROJECT_BINARY_DIR}... /
    // NAME="$<TARGET_FILE:...>" -- the shapes B1090 removed.
    const std::regex bad(
        R"re(\b[A-Z][A-Z0-9_]*="?(\$\{(CMAKE|PROJECT)_(CURRENT_)?(SOURCE|BINARY)_DIR\}|\$<TARGET_FILE:))re");
    std::vector<std::string> hits;
    for (const auto& rel : files) {
        std::ifstream in(root + "/" + rel);
        ASSERT_TRUE(in.good()) << rel;
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            ++n;
            const auto hash = line.find('#');
            const std::string code = hash == std::string::npos ? line : line.substr(0, hash);
            if (code.find("ENVIRONMENT") != std::string::npos) continue;
            if (std::regex_search(code, bad)) hits.push_back(rel + ":" + std::to_string(n) + ": " + line);
        }
    }
    std::string msg;
    for (const auto& h : hits) msg += "  " + h + "\n";
    EXPECT_TRUE(hits.empty())
        << "compile definitions carrying an absolute path (write "
           "\"NAME=WILLEQ_REPO_PATH(\\\"repo/relative\\\")\" or "
           "WILLEQ_BIN_PATH_OF(\\\"$<TARGET_FILE_NAME:t>\\\"), B1090):\n"
        << msg;
}

}  // namespace
