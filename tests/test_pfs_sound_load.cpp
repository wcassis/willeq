// T392 (board B200): every sound that lives in a snd*.pfs archive must load.
//
// T386 measured 31 "Failed to load sound: <name> (not found on disk or in
// PFS)" lines and zero "Loaded sound from PFS" lines in one live session --
// the whole UI/combat sound set is silent. These tests pin the lookup against
// the archives actually shipped in the client tree, with no audio device and
// no server.
#ifdef WITH_AUDIO

#include <gtest/gtest.h>

#include "client/audio/audio_manager.h"
#include "client/audio/sound_buffer.h"
#include "client/willeq_database.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>

#include <unistd.h>

using namespace EQT::Audio;

namespace {

EQT::WilleqDatabase g_willeq_db;
bool g_willeq_db_available = false;

class WilleqDbEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        const char* env_path = std::getenv("WILLEQ_DB_PATH");
        std::string db_path = env_path ? env_path : WILLEQ_DB_PATH;
        std::ifstream probe(db_path);
        if (probe.good()) {
            probe.close();
            if (g_willeq_db.open(db_path)) {
                EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
                g_willeq_db_available = true;
            }
        }
    }
};

::testing::Environment* const g_willeq_db_env =
    ::testing::AddGlobalTestEnvironment(new WilleqDbEnvironment);

// data/eq_client is a symlink to the real client tree. Resolve it: the
// generated index cache records the CANONICAL path in its "eqPath" field, and
// AudioManager rejects the cache outright when eqPath_ does not match it byte
// for byte. Production passes the canonical path from willeq.json, so the
// tests must too -- otherwise every test here would take the full-rescan
// branch (T402 measured that at ~0.4 s a time).
std::string EqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    std::string path = env ? env : EQ_CLIENT_DATA_PATH;
    std::error_code ec;
    auto canonical = std::filesystem::canonical(path, ec);
    return ec ? path : canonical.string();
}

// The production client runs from the repo root, so the tests do too: the
// shipped data (data/willeq.db and friends) is found either way since T399,
// but running where production runs keeps this test honest. Since T402 the
// index cache itself is NOT under the checkout -- it lands in the run-local
// cache directory (<exe dir>/cache by default), so no test run can dirty the
// tree.
class RepoRootCwd {
public:
    RepoRootCwd() : saved_(std::filesystem::current_path()) {
        std::filesystem::current_path(WILLEQ_REPO_ROOT);
    }
    ~RepoRootCwd() { std::filesystem::current_path(saved_); }

private:
    std::filesystem::path saved_;
};

class PfsSoundLoadTest : public ::testing::Test {
protected:
    std::unique_ptr<RepoRootCwd> cwd_;
    std::unique_ptr<AudioManager> manager_;

    void SetUp() override {
        std::string eqPath = EqClientPath();
        if (!std::filesystem::exists(eqPath)) {
            GTEST_SKIP() << "EQ client path not found: " << eqPath;
        }
        if (!std::filesystem::exists(eqPath + "/snd2.pfs")) {
            GTEST_SKIP() << "snd2.pfs not found under " << eqPath;
        }

        cwd_ = std::make_unique<RepoRootCwd>();
        manager_ = std::make_unique<AudioManager>();
        // forceLoopback: never touch the host audio device (ALSA is absent on
        // the dev VM -- that is the standing AudioManagerTest waiver).
        if (!manager_->initialize(eqPath, /*forceLoopback=*/true)) {
            GTEST_SKIP() << "AudioManager::initialize failed";
        }
    }

    void TearDown() override {
        if (manager_) manager_->shutdown();
        manager_.reset();
        cwd_.reset();
    }

    void ExpectLoads(const char* name) {
        auto buffer = manager_->getSoundBufferByName(name);
        ASSERT_NE(buffer, nullptr) << name << " did not load from any snd*.pfs";
        EXPECT_TRUE(buffer->isValid()) << name;
        EXPECT_GT(buffer->getFrameCount(), 0u) << name;
        EXPECT_GT(buffer->getSampleRate(), 0u) << name;
    }

    // The path a player actually reaches: PlaySoundIntent -> playSound(id) ->
    // db->getSoundAsset(id) -> loadSound(name). The by-name form above never
    // broke; it is the id -> name hop that carried the CRLF taint, so every
    // family member is asserted through BOTH.
    void ExpectLoadsById(uint32_t id, const char* expectedName) {
        if (!g_willeq_db_available) {
            GTEST_SKIP() << "willeq.db not available";
        }
        auto* db = EQT::WilleqDatabase::globalInstance();
        ASSERT_NE(db, nullptr);
        EXPECT_EQ(db->getSoundAsset(id), expectedName) << "sound id " << id;

        auto buffer = manager_->getSoundBuffer(id);
        ASSERT_NE(buffer, nullptr) << "sound id " << id << " did not load";
        EXPECT_TRUE(buffer->isValid());
        EXPECT_GT(buffer->getFrameCount(), 0u);
    }
};

// The headline case: BuyItem.WAV / id 138, the coin sound T382 wired and T386
// found silent live.
TEST_F(PfsSoundLoadTest, BuyItemLoadsFromPfs) {
    ExpectLoads("BuyItem.WAV");
}

TEST_F(PfsSoundLoadTest, BuyItemLoadsById138) {
    ExpectLoadsById(138, "BuyItem.WAV");
}

// The class, not just the one row: the CRLF taint came from the db builder
// reading CRLF client text with std::getline, so every filename in the table
// carried a trailing CR. Nothing may hold a control character.
TEST_F(PfsSoundLoadTest, NoSoundAssetFilenameHoldsAControlChar) {
    if (!g_willeq_db_available) {
        GTEST_SKIP() << "willeq.db not available";
    }
    auto* db = EQT::WilleqDatabase::globalInstance();
    ASSERT_NE(db, nullptr);

    size_t seen = 0;
    size_t tainted = 0;
    std::string firstBad;
    for (uint32_t id = 0; id <= 3584; ++id) {
        std::string name = db->getSoundAsset(id);
        if (name.empty()) continue;
        ++seen;
        if (name.find_first_of("\r\n\t") != std::string::npos) {
            ++tainted;
            if (firstBad.empty()) firstBad = std::to_string(id) + " -> '" + name + "'";
        }
    }
    EXPECT_GT(seen, 2000u) << "sound_assets looks empty";
    EXPECT_EQ(tainted, 0u) << "first tainted row: " << firstBad;
}

// Five more from T386's list of 31, so the whole family is pinned rather than
// one name.
TEST_F(PfsSoundLoadTest, SwordBlkLoadsFromPfs) { ExpectLoads("SwordBlk.WAV"); }
TEST_F(PfsSoundLoadTest, SwingLoadsFromPfs)    { ExpectLoads("Swing.WAV"); }
TEST_F(PfsSoundLoadTest, Spell1LoadsFromPfs)   { ExpectLoads("Spell_1.wav"); }
TEST_F(PfsSoundLoadTest, LevelUpLoadsFromPfs)  { ExpectLoads("LevelUp.WAV"); }
TEST_F(PfsSoundLoadTest, WaterInLoadsFromPfs)  { ExpectLoads("WaterIn.WAV"); }

TEST_F(PfsSoundLoadTest, SwordBlkLoadsById120) { ExpectLoadsById(120, "SwordBlk.WAV"); }
TEST_F(PfsSoundLoadTest, SwingLoadsById118)    { ExpectLoadsById(118, "Swing.WAV"); }
TEST_F(PfsSoundLoadTest, Spell1LoadsById103)   { ExpectLoadsById(103, "Spell_1.wav"); }
TEST_F(PfsSoundLoadTest, LevelUpLoadsById139)  { ExpectLoadsById(139, "LevelUp.WAV"); }
TEST_F(PfsSoundLoadTest, WaterInLoadsById100)  { ExpectLoadsById(100, "WaterIn.WAV"); }

// The negative in the id direction too: an id with no row plays nothing and
// does not fall back to some other sound.
TEST_F(PfsSoundLoadTest, UnknownIdStillFails) {
    if (!g_willeq_db_available) {
        GTEST_SKIP() << "willeq.db not available";
    }
    EXPECT_TRUE(EQT::WilleqDatabase::globalInstance()->getSoundAsset(999999).empty());
    EXPECT_EQ(manager_->getSoundBuffer(999999), nullptr);
}

// The negative: a name in no archive still reports not-found, once.
TEST_F(PfsSoundLoadTest, UnknownNameStillFails) {
    EXPECT_EQ(manager_->getSoundBufferByName("T392_no_such_sound.wav"), nullptr);
}

// ---------------------------------------------------------------------------
// T402 (board B248): the generated PFS index cache must NOT be written inside
// the checkout. It used to be a git-tracked config/snd_index_cache.json that
// every client run rewrote, so any run left the working tree dirty and a
// careless `git add -A` committed one machine's absolute EQ path.
//
// The scan is forced by pointing AudioManager at an ALIAS of the EQ tree (a
// directory of symlinks to the real snd*.pfs): no cache can match that eqPath,
// so scanPfsArchives() rescans and then saves -- which is the write under
// test. The assertion is on the repo's own config/ directory: nothing in it
// may appear, vanish or change.
// ---------------------------------------------------------------------------
TEST(PfsIndexCacheLocation, RescanWritesOutsideTheRepoConfigDir) {
    namespace fs = std::filesystem;

    const std::string eqPath = EqClientPath();
    if (!fs::exists(eqPath) || !fs::exists(eqPath + "/snd2.pfs")) {
        GTEST_SKIP() << "EQ client path not found: " << eqPath;
    }

    const fs::path configDir = fs::path(WILLEQ_REPO_ROOT) / "config";
    ASSERT_TRUE(fs::is_directory(configDir)) << configDir;

    auto census = [&configDir]() {
        std::map<std::string, std::pair<uintmax_t, fs::file_time_type>> out;
        for (const auto& e : fs::directory_iterator(configDir)) {
            if (!e.is_regular_file()) continue;
            out[e.path().filename().string()] = {e.file_size(), fs::last_write_time(e.path())};
        }
        return out;
    };
    const auto before = census();

    fs::path tmp = fs::temp_directory_path() /
                   ("willeq_t402_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    const fs::path eqAlias = tmp / "eq";
    const fs::path cacheDir = tmp / "cache";
    fs::create_directories(eqAlias);
    fs::create_directories(cacheDir);

    size_t archives = 0;
    for (const auto& e : fs::directory_iterator(eqPath)) {
        if (!e.is_regular_file()) continue;
        std::string lower = e.path().filename().string();
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.rfind("snd", 0) == 0 && lower.size() > 7 &&
            lower.substr(lower.size() - 4) == ".pfs") {
            fs::create_symlink(e.path(), eqAlias / e.path().filename());
            ++archives;
        }
    }
    ASSERT_GT(archives, 0u) << "no snd*.pfs under " << eqPath;

    ::setenv("WILLEQ_CACHE_DIR", cacheDir.c_str(), 1);
    {
        AudioManager mgr;
        // forceLoopback: never touch the host audio device.
        if (!mgr.initialize(eqAlias.string(), /*forceLoopback=*/true)) {
            ::unsetenv("WILLEQ_CACHE_DIR");
            fs::remove_all(tmp);
            GTEST_SKIP() << "AudioManager::initialize failed";
        }
        mgr.shutdown();
    }
    ::unsetenv("WILLEQ_CACHE_DIR");

    const auto after = census();
    const bool cacheWritten = fs::exists(cacheDir / "snd_index_cache.json");
    fs::remove_all(tmp);

    EXPECT_EQ(before.size(), after.size())
        << "the PFS index rescan added or removed a file under config/";
    for (const auto& [name, meta] : before) {
        auto it = after.find(name);
        ASSERT_NE(it, after.end()) << "config/" << name << " disappeared";
        EXPECT_EQ(meta.first, it->second.first) << "config/" << name << " changed size";
        EXPECT_EQ(meta.second, it->second.second) << "config/" << name << " was REWRITTEN "
            << "by a client run -- generated caches belong outside the checkout (B248)";
    }
    for (const auto& [name, meta] : after) {
        (void)meta;
        EXPECT_NE(before.find(name), before.end())
            << "config/" << name << " was CREATED by a client run (B248)";
    }
    EXPECT_TRUE(cacheWritten)
        << "the rescan saved no cache under $WILLEQ_CACHE_DIR; the next run rescans again";
}

} // namespace

#endif // WITH_AUDIO
