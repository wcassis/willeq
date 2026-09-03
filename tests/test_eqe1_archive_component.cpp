// L02 (Batch L, refactor/L02_eqe1_effects_component.md).
//
// Pins the Eqe1ArchiveComponent extracted from IrrlichtRenderer:
//
//   1. IrrlichtRenderer::Eqe1() reaches one real
//      EQT::Graphics::Eqe1ArchiveComponent, constructed in the renderer ctor.
//   2. The state that MOVED into the component (the EQE1ArchiveEntry struct,
//      the eqe1ArchiveEntries_ map, isPersistentEQE1Archive) behaves exactly as
//      it did on the renderer -- the built-in fallback list, the {zone}
//      placeholder substitution and the persistent-archive rule.
//   3. The bindings the move depends on are LIVE, proved by mutation rather
//      than by value equality (L01 Review, correction 9): writing
//      Context().config.constrainedConfig.eqe1Path -- a private renderer
//      member with no public setter -- changes what the component opens, and
//      the opened loader is then visible through the component's own report of
//      owner_.eqe1Loaders_. That one assertion exercises ctx_ and owner_ in a
//      single path.
//
// Genuinely headless, same idiom as test_rdp_component.cpp (L01): a
// default-constructed, never-Init()'d IrrlichtRenderer. Nothing here touches a
// GL context. The one asset-dependent test is skipped when the EQE1 tree is
// not mounted, exactly as test_eqe1_archive_config.cpp does.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/eqe1/eqe1_archive_component.h"

#include <algorithm>
#include <filesystem>
#include <string>

using namespace EQT::Graphics;

namespace {

namespace fs = std::filesystem;
const char* kEqe1Dir = "/mnt/projects/EverQuest_ETC1";

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

TEST(Eqe1ArchiveComponentTest, Eqe1ReturnsTheSameComponentEveryTime) {
    IrrlichtRenderer renderer;
    EXPECT_EQ(&renderer.Eqe1(), &renderer.Eqe1());
}

TEST(Eqe1ArchiveComponentTest, PersistentArchiveIsWilleqAndNothingElse) {
    // The static moved out of IrrlichtRenderer with the family (T214's rule).
    EXPECT_TRUE(Eqe1ArchiveComponent::isPersistentEQE1Archive("willeq"));
    EXPECT_FALSE(Eqe1ArchiveComponent::isPersistentEQE1Archive("global_chr"));
    EXPECT_FALSE(Eqe1ArchiveComponent::isPersistentEQE1Archive("qeynos2"));
    EXPECT_FALSE(Eqe1ArchiveComponent::isPersistentEQE1Archive(""));
}

TEST(Eqe1ArchiveComponentTest, ArchiveEntriesStartEmptySoInitLoadsTheConfigOnce) {
    // renderer_init.cpp guards loadEQE1ArchiveConfig() on exactly this.
    IrrlichtRenderer renderer;
    EXPECT_TRUE(renderer.Eqe1().archiveEntries().empty());
}

TEST(Eqe1ArchiveComponentTest, MissingConfigInstallsTheBuiltInFallbackList) {
    IrrlichtRenderer renderer;
    // A path that cannot exist: the loader reports false and installs the
    // hard-coded default entry rather than leaving the map empty.
    EXPECT_FALSE(renderer.Eqe1().loadEQE1ArchiveConfig(
        "config/this_file_does_not_exist_l02.json"));

    const auto& entries = renderer.Eqe1().archiveEntries();
    ASSERT_EQ(entries.count("default"), 1u);
    const auto& def = entries.at("default");
    EXPECT_EQ(def.zoneArchives,
              (std::vector<std::string>{"{zone}", "{zone}_obj", "{zone}_chr"}));
    EXPECT_TRUE(contains(def.globalArchives, "global_chr"));
    EXPECT_TRUE(contains(def.globalArchives, "gequip8"));
    // T223: gequip7.s3d does not exist; the fallback copy must not carry it.
    EXPECT_FALSE(contains(def.globalArchives, "gequip7"));
}

TEST(Eqe1ArchiveComponentTest, ResolveSubstitutesTheZonePlaceholder) {
    IrrlichtRenderer renderer;
    renderer.Eqe1().loadEQE1ArchiveConfig("config/this_file_does_not_exist_l02.json");

    const auto resolved = renderer.Eqe1().resolveEQE1Archives("qeynos2");
    EXPECT_EQ(resolved.zoneArchives,
              (std::vector<std::string>{"qeynos2", "qeynos2_obj", "qeynos2_chr"}));
    EXPECT_TRUE(contains(resolved.globalArchives, "global_chr"));
}

TEST(Eqe1ArchiveComponentTest, ResolveOnAnEmptyMapYieldsNothing) {
    IrrlichtRenderer renderer;
    const auto resolved = renderer.Eqe1().resolveEQE1Archives("qeynos2");
    EXPECT_TRUE(resolved.zoneArchives.empty());
    EXPECT_TRUE(resolved.globalArchives.empty());
}

TEST(Eqe1ArchiveComponentTest, NothingIsOpenOnAFreshRenderer) {
    IrrlichtRenderer renderer;
    EXPECT_EQ(renderer.Eqe1().getEQE1MappedBytesTotal(), 0u);
    // Reads owner_.eqe1Loaders_ under owner_.eqe1Mutex_ and returns early; a
    // no-op, not a crash.
    renderer.Eqe1().logEQE1ArchiveUsage();
}

TEST(Eqe1ArchiveComponentTest, NoEqe1PathOpensNothing) {
    IrrlichtRenderer renderer;
    ASSERT_TRUE(renderer.Context().config.constrainedConfig.eqe1Path.empty());
    EXPECT_EQ(renderer.Eqe1().getOrOpenEQE1("willeq"), nullptr);
    EXPECT_EQ(renderer.Eqe1().getEQE1MappedBytesTotal(), 0u);
}

TEST(Eqe1ArchiveComponentTest, OpeningThroughTheContextBoundConfigIsVisibleInTheReport) {
    std::error_code ec;
    if (!fs::is_directory(kEqe1Dir, ec) ||
        !fs::exists(fs::path(kEqe1Dir) / "willeq.eqe1", ec)) {
        GTEST_SKIP() << "EQE1 asset dir not available: " << kEqe1Dir;
    }

    IrrlichtRenderer renderer;
    // eqe1Path is a private renderer member with no public setter. Writing it
    // through the context is the mutation that proves ctx_.config really is a
    // reference to config_ -- and the open that follows lands in
    // owner_.eqe1Loaders_, which getEQE1MappedBytesTotal() reads back.
    renderer.Context().config.constrainedConfig.eqe1Path = kEqe1Dir;

    const EQE1Loader* loader = renderer.Eqe1().getOrOpenEQE1("willeq");
    ASSERT_NE(loader, nullptr);
    EXPECT_GT(renderer.Eqe1().getEQE1MappedBytesTotal(), 0u);

    // Second call is served from the same map, not a second mmap.
    EXPECT_EQ(renderer.Eqe1().getOrOpenEQE1("willeq"), loader);

    // A stem that is not there is negative-cached, and nothing else opens.
    const size_t bytes = renderer.Eqe1().getEQE1MappedBytesTotal();
    EXPECT_EQ(renderer.Eqe1().getOrOpenEQE1("no_such_pack_l02"), nullptr);
    EXPECT_EQ(renderer.Eqe1().getEQE1MappedBytesTotal(), bytes);
}

}  // namespace
