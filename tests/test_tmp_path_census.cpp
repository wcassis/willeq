// T477 (refactor/T477_upload_leftovers_and_shared_tmp_names.md, board B571):
// THE /tmp CENSUS AS A TEST.
//
// /tmp is ONE directory for every worktree slot on the host (slot1..3, t1, t2,
// main), and ctest -j6 starts a binary's cases as concurrent processes. A test
// that writes a FIXED name under /tmp therefore shares that file with the same
// test running in another slot, or with its own sibling case, and one run can
// remove, truncate or reopen the file under the other's reader. T465 removed
// that race from test_headless_shutdown.cpp; B571 counted seventeen more names
// in nine files, and T477 found another nineteen spelled as bare "/tmp/..."
// literals. None had failed yet, which is exactly why it needs a ratchet
// instead of a memory.
//
// This file re-takes the census on every run. It reads every C++ source under
// tests/, strips comments, and finds every temp path: a string literal that
// begins with /tmp, or a call to std::filesystem::temp_directory_path(). The
// C++ statement holding it must also carry a per-process component -- a
// getpid() suffix, a mkdtemp()/mkstemp() template (XXXXXX), or a
// willeq_test::ScopedTempDir (tests/scoped_temp_dir.h) -- or the site must be
// in the waiver list below with a written reason. A test NAME is not a unique
// component: the same test in another slot has the same name.
//
// The patterns are spelled in pieces below so that this file does not match
// itself; it is scanned like any other.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::string kRoot = std::string(WILLEQ_REPO_ROOT) + "/";

// What a temp path looks like, and what makes one unique. Split so the
// source text of this file contains neither needle.
const std::string kTmpLiteral = std::string("\"/") + "tmp";         // "/tmp
const std::string kTempDirCall = std::string("temp_directory_") + "path";

const char* const kUniqueComponents[] = {
    "getpid",         // a pid suffix
    "mkdtemp",        // a mkdtemp() template ...
    "mkstemp",
    "XXXXXX",         // ... spelled wherever the template is built
    "ScopedTempDir",  // tests/scoped_temp_dir.h, which is mkdtemp()
};

// ---------------------------------------------------------------------------
// Waivers: temp paths that need no unique component, each with the reason.
// Keyed by repo-relative file plus a substring of the offending statement.
// ---------------------------------------------------------------------------
struct Waiver { const char* file; const char* statementContains; const char* why; };

const Waiver kWaivers[] = {
    {"tests/test_video_capture_flags.cpp", "capture",
     "a command-line VALUE parsed into ApplicationConfig and compared back; "
     "no file or directory is ever created"},
    {"tests/test_pose_log_flags.cpp", "pose_log.tsv",
     "a command-line VALUE parsed into ApplicationConfig and compared back "
     "(--pose-log); no file is ever created -- the row-format tests write "
     "under a getpid()-stemed name instead"},
    {"tests/test_pose_log_flags.cpp", "capture",
     "a command-line VALUE parsed into ApplicationConfig and compared back "
     "(--dump-frames-dir); no file or directory is ever created"},
    {"tests/test_zone_component.cpp", "k09-navmesh",
     "a setter/getter round trip on ZoneComponent's navmesh path string; the "
     "path is never opened"},
    {"tests/test_chr_path_derivation.cpp", "deriveChrArchivePath",
     "pure string derivation (S3DLoader::deriveChrArchivePath) on literal "
     "paths; the cases that touch disk in this file use a getpid() tree"},
    {"tests/test_hotkey_manager.cpp", "oss.str()",
     "the name is built one statement earlier from getpid() plus the test "
     "suite and case names (tempConfigPath())"},
    {"tests/test_willeq_database.cpp", "dir = tmp ? tmp :",
     "the DIRECTORY fallback when TMPDIR is unset; the file name joined onto "
     "it one statement later carries getpid() (TempDbPath())"},
    {"tests/test_item_model_mapping.cpp", "dir = tmp ? tmp :",
     "the DIRECTORY fallback when TMPDIR is unset; the file name joined onto "
     "it one statement later carries getpid() (TempDbPath())"},
};

const Waiver* FindWaiver(const std::string& file, const std::string& statement) {
    for (const auto& w : kWaivers) {
        if (file == w.file && statement.find(w.statementContains) != std::string::npos) return &w;
    }
    return nullptr;
}

// Comments out, string and character literals kept (the needles live in
// them). Newlines survive so line numbers still mean something.
std::string StripCppComments(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool inBlock = false, inLine = false, inString = false, inChar = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char n = (i + 1 < text.size()) ? text[i + 1] : '\0';
        if (inBlock) {
            if (c == '*' && n == '/') { inBlock = false; ++i; }
            else if (c == '\n') out.push_back('\n');
            continue;
        }
        if (inLine) {
            if (c == '\n') { inLine = false; out.push_back('\n'); }
            continue;
        }
        if (inString || inChar) {
            out.push_back(c);
            if (c == '\\' && i + 1 < text.size()) { out.push_back(n); ++i; continue; }
            if ((inString && c == '"') || (inChar && c == '\'')) { inString = inChar = false; }
            continue;
        }
        if (c == '/' && n == '/') { inLine = true; ++i; continue; }
        if (c == '/' && n == '*') { inBlock = true; ++i; continue; }
        if (c == '"') inString = true;
        else if (c == '\'') inChar = true;
        out.push_back(c);
    }
    return out;
}

struct Site {
    size_t line = 0;
    std::string statement;   // whitespace-collapsed
};

bool IsBoundary(char c) { return c == ';' || c == '{' || c == '}'; }

// The C++ statement enclosing `pos`: back to the previous ; { or }, forward to
// the next ;. Crude, and enough: every temp-path site in tests/ is one
// statement, and a unique component is spelled inside that statement.
std::string EnclosingStatement(const std::string& code, size_t pos) {
    size_t begin = pos;
    while (begin > 0 && !IsBoundary(code[begin - 1])) --begin;
    size_t end = code.find(';', pos);
    if (end == std::string::npos) end = code.size();
    std::string raw = code.substr(begin, end - begin);
    std::string collapsed;
    bool space = false;
    for (char c : raw) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { space = true; continue; }
        if (space && !collapsed.empty()) collapsed.push_back(' ');
        space = false;
        collapsed.push_back(c);
    }
    return collapsed;
}

// Every temp-path site in one comment-stripped source.
std::vector<Site> FindTempSites(const std::string& code) {
    std::vector<Site> sites;
    std::vector<size_t> hits;
    for (const std::string& needle : {kTmpLiteral, kTempDirCall}) {
        for (size_t p = code.find(needle); p != std::string::npos; p = code.find(needle, p + 1)) {
            if (needle == kTmpLiteral) {
                // "/tmp followed by / or the closing quote: not "/tmpfoo".
                const size_t after = p + needle.size();
                if (after < code.size() && code[after] != '/' && code[after] != '"') continue;
            }
            hits.push_back(p);
        }
    }
    std::sort(hits.begin(), hits.end());
    for (size_t p : hits) {
        Site s;
        s.line = static_cast<size_t>(std::count(code.begin(), code.begin() + p, '\n')) + 1;
        s.statement = EnclosingStatement(code, p);
        if (!sites.empty() && sites.back().statement == s.statement) continue;  // two needles, one site
        sites.push_back(std::move(s));
    }
    return sites;
}

bool HasUniqueComponent(const std::string& statement) {
    for (const char* u : kUniqueComponents) {
        if (statement.find(u) != std::string::npos) return true;
    }
    return false;
}

std::string ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

bool IsCppSource(const fs::path& p) {
    const std::string ext = p.extension().string();
    return ext == ".cpp" || ext == ".cc" || ext == ".h" || ext == ".hpp";
}

}  // namespace

// The census itself.
TEST(TmpPathCensus, EveryTempPathInTestsHasAPerProcessComponent) {
    const fs::path tests = fs::path(kRoot) / "tests";
    ASSERT_TRUE(fs::is_directory(tests)) << "no tests/ under WILLEQ_REPO_ROOT=" << kRoot;

    size_t files = 0, sites = 0, unique = 0, waived = 0;
    std::vector<std::string> waiversUsed;
    std::ostringstream failures;
    size_t failureCount = 0;

    for (const auto& entry : fs::recursive_directory_iterator(tests)) {
        if (!entry.is_regular_file() || !IsCppSource(entry.path())) continue;
        ++files;
        const std::string rel = fs::relative(entry.path(), kRoot).generic_string();
        for (const Site& s : FindTempSites(StripCppComments(ReadFile(entry.path())))) {
            ++sites;
            if (HasUniqueComponent(s.statement)) { ++unique; continue; }
            if (const Waiver* w = FindWaiver(rel, s.statement)) {
                ++waived;
                waiversUsed.push_back(std::string(w->file) + "|" + w->statementContains);
                continue;
            }
            ++failureCount;
            failures << "\n  " << rel << ":" << s.line << "  " << s.statement;
        }
    }

    EXPECT_EQ(failureCount, 0u)
        << failureCount << " temp path(s) in tests/ with no per-process component. "
        << "Every slot on this host and every concurrent ctest case writes the same "
        << "name, so one run can remove or truncate the file under another's reader "
        << "(board B571). Use willeq_test::ScopedTempDir (tests/scoped_temp_dir.h), "
        << "or a getpid() suffix removed in TearDown(); add a waiver in this file "
        << "only for a path that never touches the filesystem, with the reason:"
        << failures.str();

    // A census that finds nothing proves nothing: the scanner must see the
    // tree it guards. Measured at T477: 350 sources, 37 temp sites, of which
    // 22 carry a unique component and 15 are waived (the waived sites are
    // five files' worth of statements; a waiver covers every statement in its
    // file that contains its substring).
    EXPECT_GT(files, 200u) << "scanned only " << files << " sources under " << tests;
    EXPECT_GT(unique, 15u) << "found only " << unique << " unique temp sites -- is the scanner blind?";

    // A waiver that matches nothing is stale and must go.
    for (const auto& w : kWaivers) {
        const std::string key = std::string(w.file) + "|" + w.statementContains;
        EXPECT_NE(std::find(waiversUsed.begin(), waiversUsed.end(), key), waiversUsed.end())
            << "waiver matches no site any more, delete it: " << key;
    }
    std::cout << "[census] " << files << " sources, " << sites << " temp sites: "
              << unique << " unique, " << waived << " waived, " << failureCount << " failing\n";
}

// The census must be able to see what it forbids, and must not flag what it
// allows. Snippets are built from pieces for the same reason as the needles.
TEST(TmpPathCensus, TheClassifierMatchesTheSitesT477Fixed) {
    const std::string tmpLit = std::string("\"/") + "tmp/";
    const std::string tdp = std::string("std::filesystem::temp_directory_") + "path()";

    // The two B571 shapes, verbatim before T477: a bare literal and a
    // temp_directory_path() join, the second split across two lines.
    const std::string bareLiteral =
        "void f() {\n    const char* tmpPath = " + tmpLit + "test_bad_magic.eqe1\";\n}\n";
    const std::string joined =
        "TEST(A, B) {\n    const fs::path log = " + tdp + " /\n"
        "                         \"t375_op_report_shape.sleq\";\n}\n";
    for (const std::string& src : {bareLiteral, joined}) {
        const auto sites = FindTempSites(StripCppComments(src));
        ASSERT_EQ(sites.size(), 1u) << src;
        EXPECT_FALSE(HasUniqueComponent(sites[0].statement)) << sites[0].statement;
    }
    EXPECT_EQ(FindTempSites(joined)[0].line, 2u);

    // The accepted shapes: T465's mkdtemp template, a getpid() suffix split
    // across lines, and the helper.
    const std::string mk = "std::string t = (" + tdp + " / \"t30_XXXXXX\").string();";
    const std::string pid = "path_ = " + tdp + " /\n    (\"t266_\" + std::to_string(::getpid()));";
    const std::string helper = "const willeq_test::ScopedTempDir tmp(\"t376\"); const fs::path log = tmp / \"x.sleq\";";
    for (const std::string& src : {mk, pid}) {
        const auto sites = FindTempSites(StripCppComments(src));
        ASSERT_EQ(sites.size(), 1u) << src;
        EXPECT_TRUE(HasUniqueComponent(sites[0].statement)) << sites[0].statement;
    }
    EXPECT_TRUE(FindTempSites(StripCppComments(helper)).empty())
        << "a ScopedTempDir join names no temp path at all";

    // A test name is NOT unique across slots.
    const std::string byName = "p = " + tdp + " / ::testing::UnitTest::GetInstance()->current_test_info()->name();";
    ASSERT_EQ(FindTempSites(byName).size(), 1u);
    EXPECT_FALSE(HasUniqueComponent(FindTempSites(byName)[0].statement));

    // Comments are not code; and "/tmpfoo" is not /tmp.
    EXPECT_TRUE(FindTempSites(StripCppComments("int a; // " + tmpLit + "x\"\n/* " + tdp + " */ int b;")).empty());
    EXPECT_TRUE(FindTempSites(std::string("auto s = \"/") + "tmpfoo\";").empty());
}
