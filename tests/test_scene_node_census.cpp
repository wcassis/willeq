// T443 (board B453) -- /dumpscene's `unaccounted` count, against a hand-built
// scene graph.
//
// The number that line exists to make trustworthy is `unaccounted`: scene
// nodes the renderer holds that no tracked category claims, i.e. a leak or an
// untracked creator. Every zone-side node was subtracted except
// `RendererContext::zoneMeshNode` -- the single-zone-mesh octree node
// `ZoneLifecycleComponent::createZoneMesh()` adds to the ROOT scene node in
// the no-BSP/PVS install arm -- so `/dumpscene` in Plane of Sky (the one
// playable zone with no BSP tree and no regions, T440) printed
// `unaccounted=1` and enumerated the zone mesh itself as an unidentified
// `octree` node. In a BSP zone the member is null and the geometry sits in
// `regionMeshNodes`, which WAS subtracted, which is why the count read 0
// there and the gap stayed invisible for as long as both existed.
//
// The arithmetic was eleven separate `otherNodes--; knownNodes.insert(x);`
// pairs inside DiagnosticsComponent::dumpScene(), reachable only from a live
// renderer. T443 lifted it into computeSceneNodeCensus() so a hand-built
// scene can measure it. The cases below therefore assert BOTH sides: the
// zone mesh accounted for (unaccounted=0) and the pre-fix arithmetic
// (unaccounted=1) pinned as the negative, so a revert is a red test rather
// than a wrong number in a log nobody reads.
//
// EDT_NULL device -- no window, no GPU, no DISPLAY. Same idiom as
// test_object_node_texture_guard.cpp.

#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#ifdef EQT_HAS_GRAPHICS
#include <irrlicht.h>

#include "client/graphics/diagnostics/scene_node_census.h"
#endif

namespace {

const std::string kRoot = WILLEQ_REPO_ROOT;

std::string slurp(const std::string& rel) {
    std::ifstream in(kRoot + "/" + rel);
    EXPECT_TRUE(in.good()) << "cannot open " << rel;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// A comment that NAMES a symbol must not satisfy a census that asks whether
// code USES it (T442's rule -- a tombstone or an explanatory note is not a
// call site).
std::string codeOnly(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    std::istringstream lines(src);
    std::string line;
    bool inBlock = false;
    while (std::getline(lines, line)) {
        if (inBlock) {
            const size_t end = line.find("*/");
            if (end == std::string::npos) continue;
            line = line.substr(end + 2);
            inBlock = false;
        }
        const size_t block = line.find("/*");
        if (block != std::string::npos) {
            const size_t end = line.find("*/", block + 2);
            if (end == std::string::npos) {
                line = line.substr(0, block);
                inBlock = true;
            } else {
                line = line.substr(0, block) + line.substr(end + 2);
            }
        }
        const size_t slashes = line.find("//");
        if (slashes != std::string::npos) line = line.substr(0, slashes);
        out += line;
        out += '\n';
    }
    return out;
}

}  // namespace

// ===========================================================================
// The source census -- this compiles and runs against a tree without the fix
// (given the census TU), and fails there.
// ===========================================================================

TEST(B453Census, TheBreakdownSubtractsTheZoneMeshNode) {
    const std::string src =
        slurp("src/client/graphics/diagnostics/diagnostics_component.cpp");
    const size_t at = src.find("// --- Other nodes ---");
    ASSERT_NE(at, std::string::npos);
    const std::string block = codeOnly(src.substr(at, 5000));
    EXPECT_TRUE(has(block, "ctx_.zoneMeshNode"))
        << "the scene breakdown still never accounts for the zone mesh (B453)";
    // It is accounted the way the collision node is: subtracted AND inserted
    // into the known set, so the enumeration below the line does not list it.
    EXPECT_TRUE(has(block, "census.singles.push_back(ctx_.zoneMeshNode)"));
}

TEST(B453Census, TheOtherZoneSideCategoriesAreStillAccounted) {
    const std::string src =
        slurp("src/client/graphics/diagnostics/diagnostics_component.cpp");
    const size_t at = src.find("// --- Other nodes ---");
    ASSERT_NE(at, std::string::npos);
    const std::string block = codeOnly(src.substr(at, 5000));
    for (const char* n : {"zoneCollisionNode()", "ctx_.zonePlaceholderNode",
                          "ctx_.camera", "sunLight_", "playerLightNode_",
                          "ctx_.regionMeshNodes", "ctx_.objectLights",
                          "collectSceneNodes"}) {
        EXPECT_TRUE(has(block, n)) << n << " left the breakdown";
    }
}

#ifdef EQT_HAS_GRAPHICS

using EQT::Graphics::computeSceneNodeCensus;
using EQT::Graphics::countSceneNodes;
using EQT::Graphics::SceneNodeCensusInputs;

class SceneNodeCensusTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        }
        smgr_ = device_->getSceneManager();
    }

    void TearDown() override {
        if (device_) device_->drop();
    }

    // A stand-in for the octree node createZoneMesh() adds: an ordinary mesh
    // node parented to the ROOT, which is the only property the census cares
    // about. addOctreeSceneNode needs real geometry; addEmptySceneNode does
    // not, and both are one root child.
    irr::scene::ISceneNode* addRootChild() {
        return smgr_->addEmptySceneNode(nullptr);
    }
};

// The headline: a scene holding exactly a camera and the single zone mesh
// balances -- which is what /dumpscene should read in Plane of Sky.
TEST_F(SceneNodeCensusTest, TheSingleZoneMeshIsAccountedForAndNothingIsLeftOver) {
    auto* camera = smgr_->addCameraSceneNode();
    auto* zoneMesh = addRootChild();

    SceneNodeCensusInputs in;
    in.smgr = smgr_;
    in.singles = {camera, zoneMesh};

    const auto out = computeSceneNodeCensus(in);
    EXPECT_EQ(out.total, 2);
    EXPECT_EQ(out.unaccounted, 0);
    EXPECT_EQ(out.known.count(zoneMesh), 1u);
    EXPECT_EQ(out.known.count(camera), 1u);
}

// The pre-fix arithmetic, pinned as the negative: drop the zone mesh from the
// census and the count is exactly the 1 B453 reported.
TEST_F(SceneNodeCensusTest, OmittingTheZoneMeshIsTheOneUnaccountedNodeB453Reported) {
    auto* camera = smgr_->addCameraSceneNode();
    auto* zoneMesh = addRootChild();

    SceneNodeCensusInputs in;
    in.smgr = smgr_;
    in.singles = {camera};  // the pre-T443 set

    const auto out = computeSceneNodeCensus(in);
    EXPECT_EQ(out.unaccounted, 1);
    // And the enumeration would name the zone mesh, because it is not known.
    EXPECT_EQ(out.known.count(zoneMesh), 0u);
}

// A BSP zone: zoneMeshNode is null and the geometry is per-region. Passing
// the null member through must be a no-op, not an off-by-one.
TEST_F(SceneNodeCensusTest, ABspZonePassesANullZoneMeshAndStillBalances) {
    auto* camera = smgr_->addCameraSceneNode();
    auto* regionA = addRootChild();
    auto* regionB = addRootChild();

    SceneNodeCensusInputs in;
    in.smgr = smgr_;
    in.singles = {camera, regionA, regionB, /*zoneMeshNode=*/nullptr};

    const auto out = computeSceneNodeCensus(in);
    EXPECT_EQ(out.total, 3);
    EXPECT_EQ(out.unaccounted, 0);
}

// An entity is its model node plus every descendant it owns (name tag,
// equipment). One entry, four nodes.
TEST_F(SceneNodeCensusTest, AnEntitySubtreeIsSubtractedWhole) {
    auto* entity = addRootChild();
    auto* limb = smgr_->addEmptySceneNode(entity);
    smgr_->addEmptySceneNode(limb);   // grandchild
    smgr_->addEmptySceneNode(entity); // name tag

    SceneNodeCensusInputs in;
    in.smgr = smgr_;
    in.subtrees = {entity};

    const auto out = computeSceneNodeCensus(in);
    EXPECT_EQ(out.total, 4);
    EXPECT_EQ(out.unaccounted, 0);
    EXPECT_EQ(countSceneNodes(entity), 4);
}

// Doors cost two nodes each (pivot + mesh child) and only the pivot is a
// pointer anything else holds; sky and weather report a count and collect
// their own pointers. Both go through countedElsewhere/collected.
TEST_F(SceneNodeCensusTest, CountedElsewhereCategoriesBalanceAndStillSuppressTheEnumeration) {
    auto* pivot = addRootChild();
    smgr_->addEmptySceneNode(pivot);  // the door's mesh child
    auto* skyNode = addRootChild();

    SceneNodeCensusInputs in;
    in.smgr = smgr_;
    in.countedElsewhere = 3;  // 2 door + 1 sky
    in.collected = {pivot, skyNode};

    const auto out = computeSceneNodeCensus(in);
    EXPECT_EQ(out.total, 3);
    EXPECT_EQ(out.unaccounted, 0);
    EXPECT_EQ(out.known.count(pivot), 1u);
    EXPECT_EQ(out.known.count(skyNode), 1u);
}

// The same node reaching the census twice (a member two categories both
// name) must not double-subtract into a negative.
TEST_F(SceneNodeCensusTest, ADuplicateEntryIsCountedOnce) {
    auto* node = addRootChild();

    SceneNodeCensusInputs in;
    in.smgr = smgr_;
    in.singles = {node, node};

    const auto out = computeSceneNodeCensus(in);
    EXPECT_EQ(out.total, 1);
    EXPECT_EQ(out.unaccounted, 0);
}

// A real leak still shows.
TEST_F(SceneNodeCensusTest, AnUntrackedRootChildIsReportedAsUnaccounted) {
    auto* camera = smgr_->addCameraSceneNode();
    addRootChild();
    addRootChild();

    SceneNodeCensusInputs in;
    in.smgr = smgr_;
    in.singles = {camera};

    EXPECT_EQ(computeSceneNodeCensus(in).unaccounted, 2);
}

TEST_F(SceneNodeCensusTest, NoSceneManagerIsAllZeroes) {
    SceneNodeCensusInputs in;
    const auto out = computeSceneNodeCensus(in);
    EXPECT_EQ(out.total, 0);
    EXPECT_EQ(out.unaccounted, 0);
    EXPECT_TRUE(out.known.empty());
}

#endif  // EQT_HAS_GRAPHICS
