// T477 (board B571): a temporary directory that belongs to one process.
//
// /tmp is one directory for every worktree slot on the host, and ctest -j6
// runs a binary's cases as concurrent processes. A test that writes a FIXED
// name under /tmp therefore shares that file with the same test in another
// slot, or with its sibling case, and one run can remove or truncate the file
// under the other's reader. T465 removed that race from
// test_headless_shutdown.cpp with mkdtemp(); this is the same idiom as a
// reusable RAII type, so a site is one line.
//
// The directory is created by mkdtemp() under temp_directory_path(), so its
// name is unique per call (not just per process), and it is removed with
// everything in it when the object goes out of scope -- the TearDown() of a
// TEST body, or the fixture's destruction when it is a member.
//
// tests/test_tmp_path_census.cpp fails on a new temp path in tests/ that does
// not take a unique component; naming ScopedTempDir is one way to satisfy it.

#ifndef WILLEQ_TESTS_SCOPED_TEMP_DIR_H
#define WILLEQ_TESTS_SCOPED_TEMP_DIR_H

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <stdlib.h>
#include <unistd.h>  // B1039: macOS declares mkdtemp() here, not in <stdlib.h>

namespace willeq_test {

class ScopedTempDir {
public:
    // `stem` names the owner (e.g. "t376_clicks") so a leftover directory is
    // traceable; six random characters are appended by mkdtemp().
    explicit ScopedTempDir(const std::string& stem) {
        const std::string tmpl =
            (std::filesystem::temp_directory_path() / (stem + "_XXXXXX")).string();
        std::vector<char> name(tmpl.begin(), tmpl.end());
        name.push_back('\0');
        if (::mkdtemp(name.data()) == nullptr) {
            throw std::runtime_error("mkdtemp(" + tmpl + "): " + std::strerror(errno));
        }
        path_ = name.data();
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

    // A file or subdirectory inside this directory.
    std::filesystem::path operator/(const std::string& leaf) const { return path_ / leaf; }

private:
    std::filesystem::path path_;
};

}  // namespace willeq_test

#endif  // WILLEQ_TESTS_SCOPED_TEMP_DIR_H
