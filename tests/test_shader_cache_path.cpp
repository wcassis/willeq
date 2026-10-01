// B959 (GitLab #277): the GLES2 driver's program-binary cache lands in the
// resolved cache directory and nowhere under the current working directory.
//
// The defect. COpenGLES2Driver::initDriver() called
// enableBinaryCache("config/shader_cache", ...): a path RELATIVE TO THE CWD.
// B952's runs (scripts cd to the repo root on NFS) left 46 binaries inside a
// checkout, and from a CWD with no config/ directory every save was dropped
// silently. The driver cannot call eqt::ResolveCachePathForWrite() (it is an
// Irrlicht patch below the client), so the host injects the directory:
// eqt::ConfigureGles2ShaderCache() (client/graphics/gles2_shader_cache.h).
//
// The net. Run a real headless GLES2 device from a scratch CWD that DOES have
// a config/ directory (the shape that made the old code write), with
// $WILLEQ_CACHE_DIR naming a temp directory, and require (1) program binaries
// under <cache>/shader_cache, (2) no shader_cache directory and no .bin
// anywhere under the CWD. Red/green: with the driver's literal
// "config/shader_cache" back, (1) and (2) both fail.
//
// Self-skips when a GLES2 device is unavailable or the device does not expose
// GL_OES_get_program_binary (then there is no cache to place), like
// test_screenshot_leak.cpp.

#include <gtest/gtest.h>

#include "client/graphics/gles2_shader_cache.h"

#include <irrlicht.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::string MakeTempDir(const char* tag) {
    std::string tmpl = std::string("/tmp/b959_") + tag + "_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    return ::mkdtemp(buf.data()) ? std::string(buf.data()) : std::string();
}

// Recursively count regular files whose name ends in ".bin", and note whether
// any directory named shader_cache exists.
void Scan(const std::string& dir, int& bins, int& shaderCacheDirs) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = ::readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        const std::string full = dir + "/" + n;
        struct stat st;
        if (::lstat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (n == "shader_cache") ++shaderCacheDirs;
            Scan(full, bins, shaderCacheDirs);
        } else if (n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0) {
            ++bins;
        }
    }
    ::closedir(d);
}

void RemoveTree(const std::string& dir) {
    const std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
}

class ShaderCachePathTest : public ::testing::Test {
protected:
    std::string cwdBefore_, scratchCwd_, cacheDir_;
    std::string oldCacheVal_;
    bool hadOld_ = false;

    void SetUp() override {
        char buf[4096];
        ASSERT_NE(::getcwd(buf, sizeof(buf)), nullptr);
        cwdBefore_ = buf;
        scratchCwd_ = MakeTempDir("cwd");
        cacheDir_ = MakeTempDir("cache");
        ASSERT_FALSE(scratchCwd_.empty());
        ASSERT_FALSE(cacheDir_.empty());
        ASSERT_EQ(::mkdir((scratchCwd_ + "/config").c_str(), 0775), 0);
        if (const char* old = std::getenv("WILLEQ_CACHE_DIR")) {
            hadOld_ = true;
            oldCacheVal_ = old;
        }
        ASSERT_EQ(::chdir(scratchCwd_.c_str()), 0);
    }

    void TearDown() override {
        if (!cwdBefore_.empty()) (void)!::chdir(cwdBefore_.c_str());
        if (hadOld_) ::setenv("WILLEQ_CACHE_DIR", oldCacheVal_.c_str(), 1);
        else ::unsetenv("WILLEQ_CACHE_DIR");
        eqtSetShaderBinaryCacheDir("");
        RemoveTree(scratchCwd_);
        RemoveTree(cacheDir_);
    }

    // Create + drop a headless GLES2 device. Returns false (and skips) when
    // none is available.
    bool RunDevice() {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(256, 192);
        params.Fullscreen = true;  // surfaceless-EGL idiom
        irr::IrrlichtDevice* device = irr::createDeviceEx(params);
        if (!device) return false;
        device->drop();
        return true;
    }
};

}  // namespace

TEST_F(ShaderCachePathTest, BinariesLandInTheResolvedCacheDirNotUnderCwd) {
    ::setenv("WILLEQ_CACHE_DIR", cacheDir_.c_str(), 1);
    EXPECT_EQ(eqt::ResolveShaderCacheDirectory(), cacheDir_ + "/shader_cache");

    eqt::ConfigureGles2ShaderCache();
    if (!RunDevice()) {
        GTEST_SKIP() << "Failed to create EDT_OGLES2 device (surfaceless EGL "
                        "unavailable in this environment) - skipping.";
    }

    int cacheBins = 0, cacheDirs = 0;
    Scan(cacheDir_, cacheBins, cacheDirs);
    int cwdBins = 0, cwdDirs = 0;
    Scan(scratchCwd_, cwdBins, cwdDirs);

    if (cacheBins == 0 && cwdBins == 0) {
        GTEST_SKIP() << "device exposes no GL_OES_get_program_binary - no cache to place";
    }
    EXPECT_GT(cacheBins, 0) << "no program binary under " << cacheDir_ << "/shader_cache";
    EXPECT_EQ(cwdBins, 0) << "program binaries were written under the CWD " << scratchCwd_;
    EXPECT_EQ(cwdDirs, 0) << "a shader_cache directory was created under the CWD " << scratchCwd_;
}

TEST_F(ShaderCachePathTest, NoDirectoryMeansNoCacheAndNothingUnderCwd) {
    // A host that never injects a directory (or whose resolver found nothing
    // writable) gets no cache -- not a CWD-relative one.
    eqtSetShaderBinaryCacheDir(nullptr);
    if (!RunDevice()) GTEST_SKIP() << "no EDT_OGLES2 device - skipping.";

    int bins = 0, dirs = 0;
    Scan(scratchCwd_, bins, dirs);
    EXPECT_EQ(bins, 0);
    EXPECT_EQ(dirs, 0);
}
