// T442 -- three dead renderer/UI members, deleted (boards B450, B451, B452).
//
// Every case here is a SOURCE CENSUS: it reads the tree at runtime through
// WILLEQ_REPO_ROOT, so the same binary measures the pre-fix and the post-fix
// sources. That is the pre-fix red for a unit whose whole fix is a deletion
// (the T436/T439 shape) -- there is no new seam to call, because the defect
// IS the code that is still there. The recipe: build, run (red), delete,
// rebuild, run (green). No stash -- the stash stack is shared across
// worktrees.
//
// The three items:
//
//   * B450 -- `PopupType` carried seven enumerators and only three were named
//     anywhere: `None`, `Inventory`, `Vendor`. `Spellbook`, `Bank`, `Loot`,
//     `Trade` and `Skills` had no producer and no consumer. This is NOT the
//     B434 defect repeated: each of those windows exists and is drawn, but
//     each tracks its own visibility on its own panel state
//     (`BankPanelState::isOpen`, `LootPanelState`, `SkillsPopupState`, ...),
//     exactly as T434's Options window and T436's bank and merchant windows
//     do. The enum shrank to the set fable actually drives.
//
//   * B451 -- `PlayerCameraComponent::checkCollisionIrrlicht()` was a
//     LOG_WARN plus `return false` ("nothing blocks you"), reached from five
//     `useBspFiltered ? checkCollisionBspFiltered(...) : checkCollisionIrrlicht(...)`
//     ternaries. `useBspFiltered` is
//     `ctx_.zoneBspTree && !ctx_.regionWorldTriangles.empty()` evaluated
//     inside `resolveIrrlichtCollision()`, which the caller enters only when
//     `regionWorldTriangles` is ALREADY non-empty -- so the false arm was
//     unreachable. Identical to the `findGroundZIrrlicht()` twin T440
//     deleted, and closed the same way.
//
//   * B452 -- `IrrlichtRenderer::fallbackMeshNode_` was `= nullptr` at its
//     declaration and the only two assignments in the whole tree were
//     `= nullptr`. Nothing ever created a node for it, yet eleven sites
//     tested it, dropped it, re-parented it, swapped its material to the
//     debug material, collected its mesh buffers and counted it in the scene
//     breakdown. The "single zone mesh" fallback its name refers to is
//     really `ZoneLifecycleComponent::createZoneMesh()` writing
//     `ctx_.zoneMeshNode` (`renderer_zone_draw.cpp`'s
//     `addOctreeSceneNode`), which is the only mesh node in that path that
//     anything ever assigns.
//
// Device-free, database-free, server-free.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <system_error>
#include <string>
#include <vector>

#include "client/graphics/ui/static_panels.h"

using namespace EQT::Graphics;

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

// A deleted symbol's TOMBSTONE names it on purpose, so a census that asserts
// "nothing reads this any more" has to look at code and not at comments.
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
        if (block != std::string::npos && line.find("*/", block) == std::string::npos) {
            line = line.substr(0, block);
            inBlock = true;
        }
        const size_t slashes = line.find("//");
        if (slashes != std::string::npos) line = line.substr(0, slashes);
        out += line;
        out += '\n';
    }
    return out;
}

// Every directory a "zero callers" claim must cover (T381/B166: tools/ was
// the one caller that made setSpellDatabase survive its first deletion).
const std::vector<const char*>& sourceDirs() {
    static const std::vector<const char*> dirs = {
        "src", "include", "tests", "tools", "benchmarks", "docker",
    };
    return dirs;
}

// Recursive walk over the source directories, code only, returning every
// (relative path, code-only text) pair for the C/C++ files.
void forEachSourceFile(const std::function<void(const std::string&, const std::string&)>& fn) {
    for (const char* dir : sourceDirs()) {
        const std::string root = kRoot + "/" + dir;
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(root, ec), end;
        if (ec) continue;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const std::string p = it->path().string();
            const std::string ext = it->path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".hpp" && ext != ".cc") continue;
            // This census names the deleted symbols in its own string
            // literals; it is the one file that may.
            if (it->path().filename() == "test_dead_renderer_members.cpp") continue;
            std::ifstream in(p);
            std::ostringstream ss;
            ss << in.rdbuf();
            fn(p.substr(kRoot.size() + 1), codeOnly(ss.str()));
        }
    }
}

}  // namespace

// ===========================================================================
// B450 -- PopupType shrank to the set fable drives
// ===========================================================================

TEST(B450Census, PopupTypeDeclaresOnlyTheThreeDrivenEnumerators) {
    const std::string panels =
        codeOnly(slurp("include/client/graphics/ui/static_panels.h"));

    const size_t at = panels.find("enum class PopupType");
    ASSERT_NE(at, std::string::npos);
    const std::string body = panels.substr(at, panels.find("};", at) - at);

    // The three that are named by production code or a test.
    EXPECT_TRUE(has(body, "None = 0"));
    EXPECT_TRUE(has(body, "Inventory"));
    EXPECT_TRUE(has(body, "Vendor"));

    // The five that were named by nothing (B450).
    for (const char* dead : {"Spellbook", "Bank", "Loot", "Trade", "Skills"}) {
        EXPECT_FALSE(has(body, dead))
            << "PopupType::" << dead << " is still declared (B450). That "
               "window exists and is drawn, but it tracks its own visibility "
               "on its own panel state, not through this enum -- so the "
               "enumerator has no producer and no consumer.";
    }
}

TEST(B450Census, NothingInTheTreeNamesADeletedPopupEnumerator) {
    forEachSourceFile([](const std::string& rel, const std::string& code) {
        for (const char* dead : {"Spellbook", "Bank", "Loot", "Trade", "Skills"}) {
            EXPECT_FALSE(has(code, std::string("PopupType::") + dead))
                << rel << " still names PopupType::" << dead << " (B450)";
        }
    });
}

// The enumerators that survive still round-trip -- nothing that DID work
// stopped working.
TEST(B450Census, TheTwoDrivenPopupsStillRoundTrip) {
    InventoryPanelState inv;
    EXPECT_EQ(inv.activePopup, PopupType::None);
    inv.activePopup = PopupType::Inventory;
    EXPECT_EQ(inv.activePopup, PopupType::Inventory);
    inv.activePopup = PopupType::Vendor;
    EXPECT_EQ(inv.activePopup, PopupType::Vendor);
    inv.activePopup = PopupType::None;
    EXPECT_EQ(inv.activePopup, PopupType::None);
}

// ===========================================================================
// B451 -- checkCollisionIrrlicht() and its five unreachable ternaries
// ===========================================================================

TEST(B451Census, CheckCollisionIrrlichtIsGoneFromTheWholeTree) {
    forEachSourceFile([](const std::string& rel, const std::string& code) {
        EXPECT_FALSE(has(code, "checkCollisionIrrlicht"))
            << rel << " still names checkCollisionIrrlicht (B451). It was a "
               "LOG_WARN plus 'return false' behind a ternary whose selector "
               "can never be false inside resolveIrrlichtCollision().";
    });
}

// The five ternaries are gone with it: the physics TU calls the BSP-filtered
// check unconditionally, and `useBspFiltered` -- the selector that could
// never be false -- is gone too.
TEST(B451Census, ThePhysicsTuCallsTheBspFilteredCheckUnconditionally) {
    const std::string physics = codeOnly(
        slurp("src/client/graphics/player/renderer_player_physics.cpp"));

    EXPECT_FALSE(has(physics, "useBspFiltered"))
        << "the dead selector survives (B451)";

    // Five call sites remain -- the horizontal check, the two swim ceiling
    // checks and the two wall slides -- all unconditional now.
    size_t calls = 0;
    for (size_t at = physics.find("checkCollisionBspFiltered");
         at != std::string::npos;
         at = physics.find("checkCollisionBspFiltered", at + 1)) {
        ++calls;
    }
    EXPECT_EQ(calls, 5u)
        << "renderer_player_physics.cpp should call checkCollisionBspFiltered "
           "at exactly the five sites that used to be ternaries";
}

// The invariant that made the false arm unreachable, pinned so a future edit
// cannot quietly restore the branch: the entry guard is on
// regionWorldTriangles being non-empty, which is the same term the selector
// tested.
TEST(B451Census, ResolveIrrlichtCollisionIsEnteredOnlyWithRegionTriangles) {
    const std::string physics = codeOnly(
        slurp("src/client/graphics/player/renderer_player_physics.cpp"));
    EXPECT_TRUE(has(physics, "regionWorldTriangles.empty()"))
        << "the entry guard on resolveIrrlichtCollision() is what made the "
           "Irrlicht arm unreachable (B451); it must stay explicit";
}

// ===========================================================================
// B452 -- fallbackMeshNode_, a member nothing ever created
// ===========================================================================

TEST(B452Census, FallbackMeshNodeIsGoneFromTheWholeTree) {
    forEachSourceFile([](const std::string& rel, const std::string& code) {
        EXPECT_FALSE(has(code, "fallbackMeshNode"))
            << rel << " still names fallbackMeshNode (B452). Nothing in the "
               "tree ever assigned it a node: it was nullptr at its "
               "declaration and both assignments anywhere were '= nullptr'.";
    });
}

// The real fallback still has its creator. `createZoneMesh()` is the no-BSP
// path, and it is the only place a zone-wide mesh node is made.
TEST(B452Census, CreateZoneMeshStillCreatesTheRealFallbackNode) {
    const std::string draw = codeOnly(
        slurp("src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp"));
    EXPECT_TRUE(has(draw, "ctx_.zoneMeshNode = ctx_.smgr->addOctreeSceneNode"))
        << "createZoneMesh() is the real single-zone-mesh fallback (B452); if "
           "this assignment moves, say where it went";

    const std::string load = codeOnly(
        slurp("src/client/graphics/zone_load/renderer_zone_load.cpp"));
    EXPECT_TRUE(has(load, "ZoneLifecycle().createZoneMesh()"))
        << "the no-BSP/PVS install path must still call createZoneMesh()";
}

// The one site whose meaning was not simply 'never fires': the SIZE_MAX
// sentinel drawRegionMesh() took for the fallback had exactly one caller and
// went with it.
TEST(B452Census, DrawRegionMeshNoLongerCarriesTheFallbackSentinel) {
    const std::string draw = codeOnly(
        slurp("src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp"));
    EXPECT_FALSE(has(draw, "drawRegionMesh(SIZE_MAX)"))
        << "the SIZE_MAX sentinel was the fallback node's only caller (B452)";
    EXPECT_FALSE(has(draw, "regionIdx == SIZE_MAX"))
        << "drawRegionMesh()'s SIZE_MAX branch resolved to the fallback node "
           "and is unreachable without it (B452)";
}
