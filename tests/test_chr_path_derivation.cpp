/**
 * T369 (B105) — the character archive is derived from the FILENAME, never
 * from the whole path.
 *
 * `S3DLoader::loadCharacters()` pairs a zone archive with its `_chr.s3d`
 * companion. It used to do that by stripping the extension off the WHOLE
 * path, searching the result for the FIRST occurrence of `_chr`, and
 * truncating there. A directory component containing `_chr` therefore ate
 * the path:
 *
 *   /tmp/willeq_chrscan_1185004/beholder_chr.s3d  ->  /tmp/willeq_chr.s3d
 *
 * — measured by T366 Leg A by accident, when its scratch client tree was
 * named `willeq_chrscan_<pid>`. Every archive collapsed to the same bogus
 * path, every character load returned zero, and the client said nothing:
 * `RaceModelLoader::searchZoneChrFilesForModel()` files a zero-character
 * result as a negative cache entry and moves on.
 *
 * Three things are pinned here:
 *
 *   (a) a directory component containing `_chr` no longer truncates —
 *       asserted on the derivation directly AND end-to-end, by loading a
 *       real archive out of a directory named `..._chr_...`;
 *   (b) an IDENTITY pin: for every file in the real client tree the new
 *       derivation returns byte-for-byte what the old one returned. This
 *       one passes on both sides of the fix on purpose — it is the guard
 *       that the fix changed only the broken case;
 *   (c) the silence: a `_chr.s3d` that exists on disk and yields zero
 *       characters now logs a WARN naming the path.
 *
 * Offline: the real client tree plus (for (c)) the shipped `data/willeq.db`
 * and an EDT_NULL Irrlicht device. No server, no DISPLAY. Skips without them.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <iostream>

#include <unistd.h>

#include "common/logging.h"
#include "client/willeq_database.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#endif

namespace {

bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

std::string clientPath() {
    const char* envPath = std::getenv("EQ_CLIENT_PATH");
    std::string p = envPath ? envPath : EQ_CLIENT_DATA_PATH;
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}

// The derivation exactly as it stood before this unit: strip the extension
// off the WHOLE path, truncate at the first "_chr" ANYWHERE in it. Kept here
// as the reference for the identity pin -- the fix must reproduce it for
// every real archive name, and differ only where a DIRECTORY carried the
// marker.
std::string legacyDeriveChrArchivePath(const std::string& archivePath) {
    std::string basePath = archivePath;
    size_t dotPos = basePath.find_last_of('.');
    if (dotPos != std::string::npos) {
        basePath = basePath.substr(0, dotPos);
    }
    size_t chrSuffix = basePath.find("_chr");
    if (chrSuffix != std::string::npos) {
        basePath = basePath.substr(0, chrSuffix);
    }
    return basePath + "_chr.s3d";
}

}  // namespace

#ifdef EQT_HAS_GRAPHICS

using EQT::Graphics::S3DLoader;

// ---------------------------------------------------------------------------
// (a) the defect itself
// ---------------------------------------------------------------------------

// Pure derivation. Pre-fix every one of these returns "/tmp/willeq_chr.s3d"
// or "/tmp/eq_chr.s3d" -- the directory swallowed the filename.
TEST(ChrPathDerivationTest, DirectoryComponentContainingChrDoesNotTruncateThePath) {
    EXPECT_EQ(S3DLoader::deriveChrArchivePath("/tmp/willeq_chrscan_1185004/beholder_chr.s3d"),
              "/tmp/willeq_chrscan_1185004/beholder_chr.s3d");
    EXPECT_EQ(S3DLoader::deriveChrArchivePath("/tmp/willeq_chrscan_1185004/beholder.s3d"),
              "/tmp/willeq_chrscan_1185004/beholder_chr.s3d");
    // A `_chr` directory that is not the last one, and a zone whose own name
    // has nothing to strip.
    EXPECT_EQ(S3DLoader::deriveChrArchivePath("/opt/eq_chr_backup/live/qeynos2.s3d"),
              "/opt/eq_chr_backup/live/qeynos2_chr.s3d");
    // The `_chr2` variants must still fold onto `_chr` (acrylia_chr2.s3d and
    // friends are real files in the shipped tree).
    EXPECT_EQ(S3DLoader::deriveChrArchivePath("/tmp/a_chr_b/acrylia_chr2.s3d"),
              "/tmp/a_chr_b/acrylia_chr.s3d");
    // No directory at all: the relative form is preserved, no leading slash.
    EXPECT_EQ(S3DLoader::deriveChrArchivePath("arena.s3d"), "arena_chr.s3d");
}

// End-to-end through the real loader: the same archive, loaded out of a
// directory whose name contains `_chr`, must still find its characters.
// Pre-fix this yields zero characters and no diagnostic at all.
TEST(ChrPathDerivationTest, CharactersLoadFromAnArchiveUnderAChrNamedDirectory) {
    const std::string eq = clientPath();
    if (!fileExists(eq + "beholder_chr.s3d")) {
        GTEST_SKIP() << "EQ client tree not at " << eq;
    }

    std::error_code ec;
    const std::filesystem::path tree =
        std::filesystem::temp_directory_path() /
        ("willeq_chrscan_" + std::to_string(::getpid()));
    std::filesystem::remove_all(tree, ec);
    ASSERT_TRUE(std::filesystem::create_directories(tree, ec)) << ec.message();
    std::filesystem::create_symlink(eq + "beholder_chr.s3d", tree / "beholder_chr.s3d", ec);
    ASSERT_FALSE(ec) << ec.message();

    // The derivation must name the archive that is actually there.
    EXPECT_EQ(S3DLoader::deriveChrArchivePath((tree / "beholder_chr.s3d").string()),
              (tree / "beholder_chr.s3d").string());

    EQT::Graphics::S3DLoadOptions options;
    options.loadObjects = false;
    options.computeCombinedGeometry = false;
    options.loadCharacters = true;

    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone((tree / "beholder_chr.s3d").string(), options))
        << loader.getError();
    EXPECT_GT(loader.getCharacterCount(), 0u)
        << "no characters loaded from " << (tree / "beholder_chr.s3d").string()
        << " -- the `_chr` in the DIRECTORY name truncated the derived path";

    std::filesystem::remove_all(tree, ec);
}

// ---------------------------------------------------------------------------
// (b) the identity pin -- passes on both sides of the fix, by design
// ---------------------------------------------------------------------------

TEST(ChrPathDerivationTest, DerivationIsUnchangedForEveryFileInTheRealClientTree) {
    const std::string eq = clientPath();
    std::error_code ec;
    if (!std::filesystem::is_directory(eq, ec)) {
        GTEST_SKIP() << "EQ client tree not at " << eq;
    }

    size_t checked = 0, archives = 0, diffs = 0;
    for (const auto& entry : std::filesystem::directory_iterator(eq, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string p = entry.path().string();
        ++checked;
        // Archives only. The one non-archive divergence in the tree is pinned
        // separately below: a DOTFILE (`.DS_Store`) has no extension by
        // std::filesystem's rules, where the legacy rfind('.') treated the
        // whole name as one. Nothing derives a character archive from a
        // dotfile, and the new reading is the correct one.
        if (entry.path().extension() != ".s3d") continue;
        ++archives;
        const std::string want = legacyDeriveChrArchivePath(p);
        const std::string got = S3DLoader::deriveChrArchivePath(p);
        if (want != got) {
            ++diffs;
            EXPECT_EQ(got, want) << "derivation changed for " << p;
            if (diffs >= 10) break;  // don't drown the log
        }
    }

    EXPECT_EQ(diffs, 0u);
    EXPECT_GT(archives, 100u) << "only " << archives << " .s3d archives under " << eq
                              << " -- the pin covered almost nothing";
    RecordProperty("files_seen", static_cast<int>(checked));
    RecordProperty("s3d_archives_checked", static_cast<int>(archives));
    std::cerr << "[ T369 ] identity pin: " << archives << " .s3d archives (of "
              << checked << " files) under " << eq << ", " << diffs
              << " divergences\n";
}

// The one deliberate divergence, stated out loud. A leading-dot filename has
// no extension under std::filesystem, so the stem is the whole name; the
// legacy rfind('.') took the whole name for an extension and derived
// `<dir>/_chr.s3d`. No caller ever derives a character archive from a
// dotfile -- this pins which reading the tree now has, so a future reader
// does not mistake it for an oversight.
TEST(ChrPathDerivationTest, DotfilesKeepTheirNameInsteadOfBeingTreatedAsAnExtension) {
    EXPECT_EQ(S3DLoader::deriveChrArchivePath("/eq/.DS_Store"), "/eq/.DS_Store_chr.s3d");
    EXPECT_EQ(legacyDeriveChrArchivePath("/eq/.DS_Store"), "/eq/_chr.s3d");
}

// ---------------------------------------------------------------------------
// (c) the silence
// ---------------------------------------------------------------------------

namespace {
// Same shape as tests/test_constrained_texture_cache.cpp: the log drain
// thread flushes on a 50ms cadence, so poll for it after FlushThreadLog().
std::string captureLoggedOutput(const std::function<void()>& action) {
    SetLogLevel(LOG_WARN);
    testing::internal::CaptureStdout();
    action();
    FlushThreadLog();
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    return testing::internal::GetCapturedStdout();
}
}  // namespace

// A `_chr.s3d` that opens clean and holds ZERO characters is a defect
// signal. T366's fixture trick: symlink a plain ZONE archive (arena.s3d,
// geometry only) under a `_chr.s3d` name. Pre-fix that case reaches
// storeOtherChrCacheLocked() and the client prints nothing.
TEST(ChrPathDerivationTest, EmptyChrArchiveIsReportedRatherThanSilentlyCached) {
    const std::string dbPath = WILLEQ_DB_REAL_PATH;
    if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
    const std::string eq = clientPath();
    if (!fileExists(eq + "akanon_chr.s3d")) {
        GTEST_SKIP() << "EQ client tree not at " << eq;
    }

    EQT::WilleqDatabase db;
    ASSERT_TRUE(db.open(dbPath)) << dbPath;
    EQT::WilleqDatabase::setGlobalInstance(&db);

    std::error_code ec;
    const std::filesystem::path tree =
        std::filesystem::temp_directory_path() /
        ("willeq_t369_empty_" + std::to_string(::getpid()));
    std::filesystem::remove_all(tree, ec);
    ASSERT_TRUE(std::filesystem::create_directories(tree, ec)) << ec.message();
    // T366's junk-archive fixture: named `*_chr.s3d`, present on disk, and
    // yields not one character. Pre-fix the scan swallowed it in silence.
    {
        std::ofstream junk((tree / "zzbroken_chr.s3d").string(), std::ios::binary);
        junk << "not an s3d archive, not even close --------------------------";
    }
    // One real archive so the scan has something ordinary to do as well.
    std::filesystem::create_symlink(eq + "akanon_chr.s3d", tree / "akanon_chr.s3d", ec);
    ASSERT_FALSE(ec) << ec.message();

    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_NULL;
    params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        std::filesystem::remove_all(tree, ec);
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
        GTEST_SKIP() << "no EDT_NULL device";
    }

    auto loader = std::make_unique<EQT::Graphics::RaceModelLoader>(
        device->getSceneManager(), device->getVideoDriver(), device->getFileSystem());
    loader->setClientPath(tree.string());
    auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
        EQT::Graphics::ConstrainedRenderingPreset::TNT);
    auto textureCache = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
    textureCache->setDriver(device->getVideoDriver());
    loader->setConstrainedTextureCache(textureCache.get());
    loader->getMeshBuilder()->setConstrainedTextureCache(textureCache.get());
    loader->setUseOldModels(false);

    // Race 330 (FRM) lives only in globalpcfroglok_chr.s3d, which is not in
    // the tree, so the scan runs to completion over both archives.
    const std::string output = captureLoggedOutput([&]() {
        loader->ensureModelDataLoaded(330, 0);
    });

    EXPECT_NE(output.find("zzbroken_chr.s3d"), std::string::npos)
        << "the empty _chr.s3d was cached as a negative entry with no diagnostic.\n"
        << "--- captured log ---\n" << output;
    EXPECT_NE(output.find("yielded NO characters"), std::string::npos)
        << "--- captured log ---\n" << output;

    loader.reset();
    textureCache.reset();
    device->drop();
    EQT::WilleqDatabase::setGlobalInstance(nullptr);
    std::filesystem::remove_all(tree, ec);
    SetLogLevel(LOG_NONE);
}


#endif  // EQT_HAS_GRAPHICS
