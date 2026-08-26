// T266: EQE1TextureIndex::resolve -- the archive to open for a lookup miss.
//
// T263 found that a race spawning AFTER zone load (GM #npctypespawn of race
// 195 in qeynos2) was fatal: its texture bwdhe0005 lives in bwd_chr, which
// the zone's load-time candidate set never named, and openArchiveContaining()
// refused every non-candidate -- under EQE1's all-or-nothing rule (decision
// #2) that is a client exit. Will's rule R2 (T214) says an archive is
// legitimately open when a lookup needed it and nothing open could serve it.
//
// Pins the three outcomes of resolve():
//   - candidate holds the name          -> that candidate, lateOpen=false
//   - only a non-candidate holds it     -> first owner in index order, lateOpen=true
//   - no archive holds it               -> false (stays the fatal path)
// and that a live tree resolves the T263 name exactly as the fix expects.

#include <gtest/gtest.h>

#include "client/graphics/eqe1_texture_index.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using EQT::Graphics::EQE1TextureIndex;

namespace {

class Eqe1TextureIndexResolveTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = std::filesystem::temp_directory_path() /
                ("t266_eqe1_index_" + std::to_string(::getpid()) + ".json");
        std::ofstream out(path_);
        // Index order is alphabetical, as the builder writes it. bwdhe0005 is
        // the T263 name: 9 owners in the real tree, bwd_chr first.
        out << R"({
  "archives": ["bwd_chr", "eastwastes_chr2", "global_chr", "qeynos2", "qeynos2_chr", "skyshrine2_chr"],
  "entries": {
    "bwdhe0005": [0, 1, 5],
    "clkhe0001": [2],
    "shared_name": [1, 2, 5]
  }
})";
        out.close();
        ASSERT_TRUE(index_.load(path_.string()));
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    std::filesystem::path path_;
    EQE1TextureIndex index_;
};

}  // namespace

TEST_F(Eqe1TextureIndexResolveTest, CandidateHoldingTheNameWinsInCandidateOrder) {
    // T207 fast path: the zone's candidate set decides among owners.
    EQE1TextureIndex::Resolution r;
    const std::vector<std::string> candidates = {"qeynos2_chr", "global_chr", "skyshrine2_chr",
                                                 "bwd_chr"};
    ASSERT_TRUE(index_.resolve("bwdhe0005", candidates, r));
    EXPECT_EQ(r.archive, "skyshrine2_chr");  // first CANDIDATE that holds it, not first owner
    EXPECT_FALSE(r.lateOpen);
    EXPECT_EQ(r.ownerCount, 3u);

    ASSERT_TRUE(index_.resolve("clkhe0001", candidates, r));
    EXPECT_EQ(r.archive, "global_chr");
    EXPECT_FALSE(r.lateOpen);
}

TEST_F(Eqe1TextureIndexResolveTest, NameInNoCandidateOpensItsOwnerAsLateOpen) {
    // The T263 shape: qeynos2's candidates do not include any bwd_chr owner.
    // Before T266 this returned nothing and the client exited. Rule R2: open
    // the archive that holds it -- the first owner in index order.
    EQE1TextureIndex::Resolution r;
    const std::vector<std::string> candidates = {"qeynos2_chr", "global_chr"};
    ASSERT_TRUE(index_.resolve("bwdhe0005", candidates, r));
    EXPECT_EQ(r.archive, "bwd_chr");
    EXPECT_TRUE(r.lateOpen);
    EXPECT_EQ(r.ownerCount, 3u);

    // Deterministic: an empty candidate set gives the same answer.
    ASSERT_TRUE(index_.resolve("bwdhe0005", {}, r));
    EXPECT_EQ(r.archive, "bwd_chr");
    EXPECT_TRUE(r.lateOpen);
}

TEST_F(Eqe1TextureIndexResolveTest, NameInNoArchiveResolvesNothing) {
    // Decision #2: a name the whole tree lacks is a real asset gap and must
    // stay on the fatal path -- resolve() must not invent an archive for it.
    EQE1TextureIndex::Resolution r;
    const std::vector<std::string> candidates = {"qeynos2_chr", "global_chr", "bwd_chr"};
    EXPECT_FALSE(index_.resolve("rathe0001", candidates, r));
    EXPECT_TRUE(r.archive.empty());
    EXPECT_FALSE(r.lateOpen);
    EXPECT_EQ(r.ownerCount, 0u);
}

TEST(Eqe1TextureIndexUnloaded, ResolveIsFalseBeforeLoad) {
    EQE1TextureIndex index;
    EQE1TextureIndex::Resolution r;
    EXPECT_FALSE(index.resolve("bwdhe0005", {"bwd_chr"}, r));
}

// The shipped index against the T263 reproduction: qeynos2's static candidate
// set must NOT hold bwdhe0005 (otherwise T263 could not have failed the way it
// did), and the late-open must be bwd_chr -- the archive race 195's model
// loads from.
TEST(Eqe1TextureIndexShipped, T263NameLateOpensBwdChrForQeynos2) {
    EQE1TextureIndex index;
    if (!index.load(EQE1_TEXTURE_INDEX_JSON)) {
        GTEST_SKIP() << "no shipped index at " << EQE1_TEXTURE_INDEX_JSON;
    }
    // config/eqe1_archives.json default globalArchives at b8725e94 plus the
    // qeynos2 zone archives. If the config grows a bwd_chr owner this test
    // should start failing loudly -- that would change the R2 story.
    const std::vector<std::string> qeynos2 = {
        "qeynos2", "qeynos2_obj", "qeynos2_chr", "global_chr", "global2_chr",
        "global3_chr", "global4_chr", "global5_chr", "global6_chr", "global7_chr",
        "gequip", "gequip2", "gequip3", "gequip4", "gequip5", "gequip6", "gequip8",
        "sky", "textures"};
    EQE1TextureIndex::Resolution r;
    ASSERT_TRUE(index.resolve("bwdhe0005", qeynos2, r));
    EXPECT_TRUE(r.lateOpen);
    EXPECT_EQ(r.archive, "bwd_chr");
    EXPECT_EQ(r.ownerCount, 9u);

    // With bwd_chr queued (what queueEntityPrepRequests now does for race
    // 195) the same name takes the fast path.
    std::vector<std::string> extended = qeynos2;
    extended.push_back("bwd_chr");
    ASSERT_TRUE(index.resolve("bwdhe0005", extended, r));
    EXPECT_FALSE(r.lateOpen);
    EXPECT_EQ(r.archive, "bwd_chr");
}
