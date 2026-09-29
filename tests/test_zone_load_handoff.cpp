// B917 (issue #236) + B918 (issue #237): a server-directed zone change that
// lands while a zone load is still running, and the exit code of a run that
// dies on a loader FATAL.
//
// Application drives eqt::ZoneLoadHandoff from two threads (see
// include/client/zone_load_handoff.h). These tests drive the SAME calls in
// the SAME order -- post() on a game thread, take() + finishLoad() on the
// render side, takeApplicableCompletion() back on the game side -- with a real
// loading thread that opens the zone archive through the real S3DLoader, the
// call that logged "[FATAL] Failed to load zone S3D archive: qeynos.s3d" /
// ".s3d" on the Orange Pi (refactor/B854/RENDERED_SWEEP.md, finding 1). No
// server, no device.

#include <json/json.h>  // before eq.h -> logging.h (JSONCPP_VERSION_STRING)
// LoadZoneSources() is private; same seam as test_self_death_sequence.cpp.
#define private public
#include "client/eq.h"
#include "client/zone/zone_component.h"
#undef private
#include "client/fatal_exit.h"
#include "client/zone_load_handoff.h"
#include "client/eq_types.h"
#include "client/zone_bsp.h"
#include "client/scenario/scenario_runner.h"
#include "client/graphics/eq/s3d_loader.h"

#include "asset_requirement.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;
using eqt::ZoneLoadHandoff;
using eqt::ZoneLoadSnapshot;

namespace {

std::string EqClientPath() {
    std::string p;
    if (const char* env = std::getenv("EQ_CLIENT_PATH")) {
        p = env;
    } else {
        p = EQ_CLIENT_DATA_PATH;
    }
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}

ZoneLoadSnapshot Snapshot(const std::string& zone, const std::string& eqPath) {
    ZoneLoadSnapshot s;
    s.zoneName = zone;
    s.playerName = "Testchar";
    s.eqClientPath = eqPath;
    s.entities.resize(3);  // something the old shared slot would have lost
    return s;
}

// What the loading thread does with the snapshot that matters here: it reads
// ONLY its own copy, and opens the archive exactly the way
// ZoneLoadComponent::loadZoneStep1_S3D() builds the path (eqPath + zone +
// ".s3d"). `midLoad` runs before the open -- that is where the server's zone
// change lands.
struct LoadResult {
    std::string zoneSeen;
    std::string archivePath;
    size_t entitiesSeen = 0;
    bool opened = false;
};

LoadResult RunLoadingThread(const ZoneLoadSnapshot& mine, const std::function<void()>& midLoad) {
    LoadResult r;
    std::thread loading([&] {
        if (midLoad) midLoad();
        r.zoneSeen = mine.zoneName;
        r.entitiesSeen = mine.entities.size();
        r.archivePath = mine.eqClientPath + mine.zoneName + ".s3d";
        EQT::Graphics::S3DLoader loader;
        EQT::Graphics::S3DLoadOptions options;
        options.loadCharacters = false;
        options.computeCombinedGeometry = false;
        options.loadObjects = false;
        r.opened = loader.loadZone(r.archivePath, options);
    });
    loading.join();
    return r;
}

// The game thread's finalisation record: what EverQuest::OnGraphicsComplete()
// would have been called for.
struct GameSide {
    ZoneLoadHandoff& handoff;
    bool awaitingGraphics = false;   // LoadingPhase::GRAPHICS_LOADING_ZONE
    std::vector<std::string> readyFor;

    void post(ZoneLoadSnapshot s) {
        // Posted from a DIFFERENT thread than the render side, as in the client.
        std::thread game([&] { handoff.post(std::move(s)); });
        game.join();
        awaitingGraphics = true;
    }
    void tick() {
        if (auto c = handoff.takeApplicableCompletion(awaitingGraphics)) {
            readyFor.push_back(c->zoneName);
            awaitingGraphics = false;  // OnGraphicsComplete -> COMPLETE
        }
    }
};

}  // namespace

// The Pi's charasis run, offline: zone-in to charasis, the server sends the
// character to qeynos while charasis is still loading.
TEST(ZoneLoadHandoff, AZoneChangeDuringALoadEndsLoadedInTheDestination) {
    const std::string eqPath = EqClientPath();
    eqt::test::requireEqClientAssets(eqPath + "qeynos.s3d");
    if (!fs::exists(eqPath + "charasis.s3d")) GTEST_SKIP() << "no charasis.s3d under " << eqPath;

    ZoneLoadHandoff handoff;
    GameSide game{handoff};

    game.post(Snapshot("charasis", eqPath));

    // Render thread: take the pending snapshot; the loading thread builds
    // from this private copy.
    ZoneLoadSnapshot active;
    uint64_t gen = 0;
    ASSERT_TRUE(handoff.take(active, gen));
    ASSERT_EQ(gen, 1u);
    EXPECT_FALSE(handoff.hasPending()) << "taking must empty the slot";

    // Mid-load: death -> bind point (qeynos). The game thread leaves the zone,
    // reconnects, and posts the destination's snapshot while the first load
    // is still running.
    LoadResult first = RunLoadingThread(active, [&] {
        game.awaitingGraphics = false;           // "Leaving zone..."
        game.post(Snapshot("qeynos", eqPath));   // OnGameStateComplete for qeynos
    });
    EXPECT_EQ(first.zoneSeen, "charasis")
        << "the running load's snapshot was overwritten by the zone change";
    EXPECT_EQ(first.entitiesSeen, 3u);
    EXPECT_TRUE(first.opened) << first.archivePath;

    // Render thread: join, publish.
    ASSERT_TRUE(handoff.finishLoad(gen, !first.opened, active.zoneName, nullptr));
    active = ZoneLoadSnapshot{};

    // Game thread: the charasis load finished, but the game is waiting for
    // qeynos. It must NOT be reported ready.
    game.tick();
    EXPECT_TRUE(game.readyFor.empty())
        << "the superseded charasis load was finalised as the zone the game is entering";

    // Render thread, next frame: the destination is still pending.
    ASSERT_TRUE(handoff.hasPending()) << "the destination zone's load request was lost";
    ASSERT_TRUE(handoff.take(active, gen));
    EXPECT_EQ(gen, 2u);
    ASSERT_EQ(active.zoneName, "qeynos") << "the follow-on load has no/the wrong zone name";
    ASSERT_EQ(active.eqClientPath, eqPath) << "the follow-on load lost the client path";
    EXPECT_TRUE(fs::exists(active.eqClientPath + active.zoneName + ".s3d"));

    LoadResult second = RunLoadingThread(active, nullptr);
    EXPECT_EQ(second.archivePath, eqPath + "qeynos.s3d");
    ASSERT_TRUE(second.opened) << "Failed to load zone S3D archive: " << second.archivePath;
    ASSERT_TRUE(handoff.finishLoad(gen, !second.opened, active.zoneName, nullptr));

    game.tick();
    ASSERT_EQ(game.readyFor.size(), 1u);
    EXPECT_EQ(game.readyFor[0], "qeynos");
    EXPECT_FALSE(handoff.loaderFailed());
    EXPECT_EQ(eqt::scenario::ProcessExitCode(false, 0, handoff.loaderFailed()), 0);
}

// The same change landing before the first load has even been started: only
// the destination is ever built.
TEST(ZoneLoadHandoff, AChangeBeforeTheLoadStartsLoadsOnlyTheDestination) {
    ZoneLoadHandoff handoff;
    GameSide game{handoff};
    game.post(Snapshot("charasis", "/eq/"));
    game.post(Snapshot("qeynos", "/eq/"));

    ZoneLoadSnapshot active;
    uint64_t gen = 0;
    ASSERT_TRUE(handoff.take(active, gen));
    EXPECT_EQ(active.zoneName, "qeynos");
    EXPECT_EQ(gen, 2u);
    EXPECT_FALSE(handoff.take(active, gen)) << "one post, one load";
    ASSERT_TRUE(handoff.finishLoad(gen, false, active.zoneName, nullptr));
    game.tick();
    ASSERT_EQ(game.readyFor.size(), 1u);
    EXPECT_EQ(game.readyFor[0], "qeynos");
}

// The plain zone-in (and a zone change AFTER ready) is unchanged: one load,
// finalised once.
TEST(ZoneLoadHandoff, AnOrdinaryZoneInIsFinalisedExactlyOnce) {
    ZoneLoadHandoff handoff;
    GameSide game{handoff};
    for (const char* zone : {"qeynos2", "qeynos"}) {
        game.post(Snapshot(zone, "/eq/"));
        ZoneLoadSnapshot active;
        uint64_t gen = 0;
        ASSERT_TRUE(handoff.take(active, gen));
        ASSERT_TRUE(handoff.finishLoad(gen, false, active.zoneName, nullptr));
        game.tick();
        game.tick();
    }
    EXPECT_EQ(game.readyFor, (std::vector<std::string>{"qeynos2", "qeynos"}));
}

// A load that finishes after the game started leaving the zone, but before
// the destination has been posted (the handshake window), is stale too.
TEST(ZoneLoadHandoff, ALoadFinishingWhileTheGameIsReconnectingIsNotReady) {
    ZoneLoadHandoff handoff;
    GameSide game{handoff};
    game.post(Snapshot("charasis", "/eq/"));
    ZoneLoadSnapshot active;
    uint64_t gen = 0;
    ASSERT_TRUE(handoff.take(active, gen));
    game.awaitingGraphics = false;  // killed: "Leaving zone...", handshake running
    ASSERT_TRUE(handoff.finishLoad(gen, false, active.zoneName, nullptr));
    game.tick();
    EXPECT_TRUE(game.readyFor.empty());

    game.post(Snapshot("qeynos", "/eq/"));
    ASSERT_TRUE(handoff.take(active, gen));
    ASSERT_TRUE(handoff.finishLoad(gen, false, active.zoneName, nullptr));
    game.tick();
    EXPECT_EQ(game.readyFor, (std::vector<std::string>{"qeynos"}));
}

// B918: a loader failure on an archive that does not exist ends the process
// with kClientFatalExit, not 0.
TEST(ZoneLoadHandoff, ALoaderFailureExitsWithTheFatalCode) {
    const fs::path dir = fs::temp_directory_path() /
        ("b917_handoff_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const std::string eqPath = dir.string() + "/";

    ZoneLoadHandoff handoff;
    GameSide game{handoff};
    game.post(Snapshot("nosuchzone", eqPath));
    ZoneLoadSnapshot active;
    uint64_t gen = 0;
    ASSERT_TRUE(handoff.take(active, gen));
    LoadResult r = RunLoadingThread(active, nullptr);
    EXPECT_FALSE(r.opened) << "the archive does not exist: " << r.archivePath;

    EXPECT_FALSE(handoff.finishLoad(gen, !r.opened, active.zoneName, nullptr,
                                    "Failed to load zone S3D archive: " + r.archivePath));
    game.tick();
    EXPECT_TRUE(game.readyFor.empty()) << "a failed load must never be reported ready";
    ASSERT_TRUE(handoff.loaderFailed());
    EXPECT_NE(handoff.loaderFailureReason().find("nosuchzone.s3d"), std::string::npos);

    const int code = eqt::scenario::ProcessExitCode(false, 0, handoff.loaderFailed());
    EXPECT_EQ(code, eqt::scenario::kClientFatalExit);
    EXPECT_NE(code, 0) << "a FATAL loader death must not look like a clean /q";

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// B918: the game thread's own FATAL-then-quit -- LoadZoneSources() on a zone
// the client has no archive for (measured live: seeding the character into
// relic, "LoadZoneSources: Failed to open zone archive: .../relic.s3d", exit
// 0 in 4 s) -- is recorded for main() too.
TEST(ZoneLoadHandoff, ALoadZoneSourcesFailureExitsWithTheFatalCode) {
    const fs::path dir = fs::temp_directory_path() /
        ("b918_sources_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    eqt::ResetFatalExitForTest();

    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dir.string() + "/");
    ASSERT_FALSE(eq.IsQuitRequested());
    eq.Zone().LoadZoneSources("nosuchzone");

    EXPECT_TRUE(eq.IsQuitRequested()) << "LoadZoneSources' FATAL still quits";
    ASSERT_TRUE(eqt::FatalExitRecorded()) << "the FATAL was not recorded -- main() returns 0";
    EXPECT_NE(eqt::FatalExitReason().find("nosuchzone"), std::string::npos);
    EXPECT_EQ(eqt::scenario::ProcessExitCode(false, 0, eqt::FatalExitRecorded()),
              eqt::scenario::kClientFatalExit);

    eqt::ResetFatalExitForTest();
    EXPECT_EQ(eqt::scenario::ProcessExitCode(false, 0, eqt::FatalExitRecorded()), 0)
        << "a run with no FATAL (a clean /q) exits 0";
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// B918: the exit-code decision, all modes.
TEST(ProcessExitCode, CleanQuitIsZeroAndFatalIsNonZeroInEveryMode) {
    using namespace eqt::scenario;
    // Ordinary / --script run: /q, /camp, window close, SIGTERM.
    EXPECT_EQ(ProcessExitCode(false, 0, false), 0);
    // Ordinary / --script run that died on a FATAL.
    EXPECT_EQ(ProcessExitCode(false, 0, true), kClientFatalExit);
    // The code collides with nothing the lane already uses.
    for (int taken : {0, 1, 2, 3, 64, 65, 66, 67, 68, 69, 70, 71, 72}) {
        EXPECT_NE(kClientFatalExit, taken);
    }
    // --scenario: the taxonomy is untouched ...
    for (int c : {kScenarioPass, kScenarioFail, kScenarioTimeout, kScenarioCouldNotStart}) {
        EXPECT_EQ(ProcessExitCode(true, c, false), c);
    }
    // ... closed (a FATAL never escapes it) and never a false pass.
    EXPECT_EQ(ProcessExitCode(true, kScenarioPass, true), kScenarioTimeout);
    EXPECT_EQ(ProcessExitCode(true, kScenarioTimeout, true), kScenarioTimeout);
    EXPECT_EQ(ProcessExitCode(true, kScenarioFail, true), kScenarioFail);
    EXPECT_EQ(ProcessExitCode(true, kScenarioCouldNotStart, true), kScenarioCouldNotStart);
}
